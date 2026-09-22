// SPDX-FileCopyrightText: 2025-2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#ifdef SALLY_CLIPBOARD_STANDALONE
#include <windows.h>
#include <ole2.h>
#include <cstring>
#include <utility>
#else
#include "precomp.h"
#endif
#include "IClipboard.h"
#include "Win32TextCodec.h"
#include "clipboard/ClipboardTextPayload.h"
#include <shellapi.h>  // For HDROP, DragQueryFileW
#include <cstring>
#include <limits>

// RAII wrapper for clipboard open/close
class ClipboardSession
{
public:
    ClipboardSession(HWND owner = NULL) : m_open(false)
    {
        // Clipboard viewers and shell extensions commonly hold the clipboard for a
        // few milliseconds. Keep the UI responsive while tolerating that race.
        for (int attempt = 0; attempt < 20 && !m_open; ++attempt)
        {
            m_open = (::OpenClipboard(owner) != FALSE);
            if (!m_open && attempt != 19)
                ::Sleep(5);
        }
    }
    ~ClipboardSession()
    {
        if (m_open)
            ::CloseClipboard();
    }
    bool IsOpen() const { return m_open; }

private:
    bool m_open;
    ClipboardSession(const ClipboardSession&);
    ClipboardSession& operator=(const ClipboardSession&);
};

class GlobalMemoryLock
{
public:
    explicit GlobalMemoryLock(HANDLE memory) : Memory(memory), Address(::GlobalLock(memory)) {}
    ~GlobalMemoryLock()
    {
        if (Address != nullptr)
            ::GlobalUnlock(Memory);
    }

    void* Get() const { return Address; }

private:
    HANDLE Memory;
    void* Address;
    GlobalMemoryLock(const GlobalMemoryLock&);
    GlobalMemoryLock& operator=(const GlobalMemoryLock&);
};

// Win32 implementation of IClipboard
class Win32Clipboard : public IClipboard
{
public:
    ClipboardResult SetText(const wchar_t* text) override
    {
        if (text == nullptr)
            return ClipboardResult::Error(ERROR_INVALID_PARAMETER);

        ClipboardSession session;
        if (!session.IsOpen())
            return ClipboardResult::Error(GetLastError());

        if (!::EmptyClipboard())
            return ClipboardResult::Error(GetLastError());

        size_t len = wcslen(text);
        if (len == (std::numeric_limits<size_t>::max)() ||
            len + 1 > (std::numeric_limits<SIZE_T>::max)() / sizeof(wchar_t))
        {
            return ClipboardResult::Error(ERROR_ARITHMETIC_OVERFLOW);
        }
        size_t size = (len + 1) * sizeof(wchar_t);

        HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE | GMEM_DDESHARE, size);
        if (hMem == NULL)
            return ClipboardResult::Error(GetLastError());

        {
            GlobalMemoryLock lock(hMem);
            wchar_t* dest = static_cast<wchar_t*>(lock.Get());
            if (dest == nullptr)
            {
                DWORD err = GetLastError();
                GlobalFree(hMem);
                return ClipboardResult::Error(err);
            }

            memcpy(dest, text, size);
        }

        if (::SetClipboardData(CF_UNICODETEXT, hMem) == NULL)
        {
            DWORD err = GetLastError();
            GlobalFree(hMem);
            return ClipboardResult::Error(err);
        }

        // Also set ANSI version for compatibility with older apps
        SetAnsiText(text, len);

        return ClipboardResult::Ok();
    }

    ClipboardResult GetText(std::wstring& text) override
    {
        ClipboardSession session;
        if (!session.IsOpen())
            return ClipboardResult::Error(GetLastError());

        // Try Unicode first
        HANDLE hData = ::GetClipboardData(CF_UNICODETEXT);
        if (hData != NULL)
        {
            const SIZE_T byteSize = GlobalSize(hData);
            GlobalMemoryLock lock(hData);
            const wchar_t* src = static_cast<const wchar_t*>(lock.Get());
            if (src != nullptr)
            {
                const DWORD error = sally::clipboard::DecodeUnicodeClipboardPayload(
                    src, byteSize, text);
                return error == ERROR_SUCCESS ? ClipboardResult::Ok()
                                              : ClipboardResult::Error(error);
            }
        }

        // Fall back to ANSI
        hData = ::GetClipboardData(CF_TEXT);
        if (hData != NULL)
        {
            const SIZE_T byteSize = GlobalSize(hData);
            GlobalMemoryLock lock(hData);
            const char* src = static_cast<const char*>(lock.Get());
            if (src != nullptr)
            {
                const DWORD error = sally::clipboard::DecodeAnsiClipboardPayload(
                    src, byteSize, GetACP(), text);
                return error == ERROR_SUCCESS ? ClipboardResult::Ok()
                                              : ClipboardResult::Error(error);
            }
        }

        return ClipboardResult::Error(ERROR_NOT_FOUND);
    }

    bool HasText() override
    {
        return ::IsClipboardFormatAvailable(CF_UNICODETEXT) ||
               ::IsClipboardFormatAvailable(CF_TEXT);
    }

    bool HasFileDrop() override
    {
        return ::IsClipboardFormatAvailable(CF_HDROP) != FALSE;
    }

    ClipboardResult GetFilePaths(std::vector<std::wstring>& paths) override
    {
        ClipboardSession session;
        if (!session.IsOpen())
            return ClipboardResult::Error(GetLastError());

        HANDLE hData = ::GetClipboardData(CF_HDROP);
        if (hData == NULL)
            return ClipboardResult::Error(ERROR_NOT_FOUND);

        HDROP hDrop = (HDROP)hData;
        UINT count = DragQueryFileW(hDrop, 0xFFFFFFFF, NULL, 0);

        try
        {
            std::vector<std::wstring> candidate;
            candidate.reserve(count);
            for (UINT i = 0; i < count; i++)
            {
                const UINT len = DragQueryFileW(hDrop, i, NULL, 0);
                if (len == 0)
                    return ClipboardResult::Error(ERROR_INVALID_DATA);
                std::wstring path(static_cast<size_t>(len) + 1, L'\0');
                const UINT written = DragQueryFileW(hDrop, i, path.data(), len + 1);
                if (written != len)
                    return ClipboardResult::Error(ERROR_INVALID_DATA);
                path.resize(written);
                candidate.push_back(std::move(path));
            }
            paths.swap(candidate);
        }
        catch (const std::bad_alloc&)
        {
            return ClipboardResult::Error(ERROR_NOT_ENOUGH_MEMORY);
        }
        catch (const std::length_error&)
        {
            return ClipboardResult::Error(ERROR_NOT_ENOUGH_MEMORY);
        }

        return ClipboardResult::Ok();
    }

    ClipboardResult Clear() override
    {
        ClipboardSession session;
        if (!session.IsOpen())
            return ClipboardResult::Error(GetLastError());

        if (!::EmptyClipboard())
            return ClipboardResult::Error(GetLastError());

        return ClipboardResult::Ok();
    }

    bool HasFormat(uint32_t format) override
    {
        return ::IsClipboardFormatAvailable(format) != FALSE;
    }

    ClipboardResult SetRawData(uint32_t format, const void* data, size_t size) override
    {
        if (data == nullptr && size > 0)
            return ClipboardResult::Error(ERROR_INVALID_PARAMETER);

        ClipboardSession session;
        if (!session.IsOpen())
            return ClipboardResult::Error(GetLastError());

        return SetRawDataInOpenClipboard(format, data, size);
    }

    ClipboardResult SetRawDataBatch(const ClipboardRawData* entries, size_t count) override
    {
        if (entries == nullptr && count > 0)
            return ClipboardResult::Error(ERROR_INVALID_PARAMETER);
        for (size_t i = 0; i < count; ++i)
        {
            if (entries[i].format == 0 || (entries[i].data == nullptr && entries[i].size > 0))
                return ClipboardResult::Error(ERROR_INVALID_PARAMETER);
        }

        ClipboardSession session;
        if (!session.IsOpen())
            return ClipboardResult::Error(GetLastError());

        for (size_t i = 0; i < count; ++i)
        {
            ClipboardResult result = SetRawDataInOpenClipboard(entries[i].format, entries[i].data, entries[i].size);
            if (!result.success)
            {
                // Fail closed: without Preferred DropEffect, consumers may treat a
                // cut data object as a copy operation.
                ::EmptyClipboard();
                return result;
            }
        }
        return ClipboardResult::Ok();
    }

    ClipboardResult GetRawData(uint32_t format, std::vector<uint8_t>& data) override
    {
        ClipboardSession session;
        if (!session.IsOpen())
            return ClipboardResult::Error(GetLastError());

        HANDLE hData = ::GetClipboardData(format);
        if (hData == NULL)
            return ClipboardResult::Error(ERROR_NOT_FOUND);

        SIZE_T size = GlobalSize(hData);
        try
        {
            std::vector<uint8_t> candidate;
            if (size > 0)
            {
                GlobalMemoryLock lock(hData);
                const void* src = lock.Get();
                if (src == nullptr)
                    return ClipboardResult::Error(GetLastError());
                candidate.resize(size);
                memcpy(candidate.data(), src, size);
            }
            data.swap(candidate);
        }
        catch (const std::bad_alloc&)
        {
            return ClipboardResult::Error(ERROR_NOT_ENOUGH_MEMORY);
        }
        catch (const std::length_error&)
        {
            return ClipboardResult::Error(ERROR_NOT_ENOUGH_MEMORY);
        }

        return ClipboardResult::Ok();
    }

    uint32_t RegisterFormat(const wchar_t* name) override
    {
        if (name == nullptr)
            return 0;
        return ::RegisterClipboardFormatW(name);
    }

    ClipboardResult SetDataObject(IDataObject* dataObject) override
    {
        HRESULT result = ::OleSetClipboard(dataObject);
        return SUCCEEDED(result) ? ClipboardResult::Ok()
                                 : ClipboardResult::Error(static_cast<uint32_t>(result));
    }

    ClipboardResult GetDataObject(IDataObject** dataObject) override
    {
        if (dataObject == nullptr)
            return ClipboardResult::Error(ERROR_INVALID_PARAMETER);

        *dataObject = nullptr;
        HRESULT result = ::OleGetClipboard(dataObject);
        return SUCCEEDED(result) ? ClipboardResult::Ok()
                                 : ClipboardResult::Error(static_cast<uint32_t>(result));
    }

private:
    ClipboardResult SetRawDataInOpenClipboard(uint32_t format, const void* data, size_t size)
    {
        HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE | GMEM_DDESHARE, size > 0 ? size : 1);
        if (hMem == NULL)
            return ClipboardResult::Error(GetLastError());

        if (size > 0)
        {
            GlobalMemoryLock lock(hMem);
            void* dest = lock.Get();
            if (dest == nullptr)
            {
                DWORD err = GetLastError();
                GlobalFree(hMem);
                return ClipboardResult::Error(err);
            }
            memcpy(dest, data, size);
        }

        if (::SetClipboardData(format, hMem) == NULL)
        {
            DWORD err = GetLastError();
            GlobalFree(hMem);
            return ClipboardResult::Error(err);
        }
        return ClipboardResult::Ok();
    }

    void SetAnsiText(const wchar_t* text, size_t wideLen)
    {
        // Convert to ANSI and set CF_TEXT for compatibility
        std::string encoded;
        if (!Win32EncodeTextLossy(GetACP(), text, wideLen, encoded))
            return;

        HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE | GMEM_DDESHARE, encoded.size() + 1);
        if (hMem == NULL)
            return;

        {
            GlobalMemoryLock lock(hMem);
            char* dest = static_cast<char*>(lock.Get());
            if (dest == nullptr)
            {
                GlobalFree(hMem);
                return;
            }

            if (!encoded.empty())
                memcpy(dest, encoded.data(), encoded.size());
            dest[encoded.size()] = '\0';
        }

        if (::SetClipboardData(CF_TEXT, hMem) == NULL)
            GlobalFree(hMem);
    }
};

// Singleton instance
static Win32Clipboard g_win32Clipboard;
IClipboard* gClipboard = &g_win32Clipboard;

IClipboard* GetWin32Clipboard()
{
    return &g_win32Clipboard;
}
