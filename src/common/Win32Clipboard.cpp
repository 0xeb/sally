// SPDX-FileCopyrightText: 2026 Open Salamander Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"
#include "IClipboard.h"
#include <shellapi.h>  // For HDROP, DragQueryFileW

// RAII wrapper for clipboard open/close
class ClipboardSession
{
public:
    ClipboardSession(HWND owner = NULL) : m_open(false)
    {
        m_open = (::OpenClipboard(owner) != FALSE);
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
        size_t size = (len + 1) * sizeof(wchar_t);

        HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE | GMEM_DDESHARE, size);
        if (hMem == NULL)
            return ClipboardResult::Error(GetLastError());

        wchar_t* dest = (wchar_t*)GlobalLock(hMem);
        if (dest == nullptr)
        {
            DWORD err = GetLastError();
            GlobalFree(hMem);
            return ClipboardResult::Error(err);
        }

        wcscpy(dest, text);
        GlobalUnlock(hMem);

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
        text.clear();

        ClipboardSession session;
        if (!session.IsOpen())
            return ClipboardResult::Error(GetLastError());

        // Try Unicode first
        HANDLE hData = ::GetClipboardData(CF_UNICODETEXT);
        if (hData != NULL)
        {
            const wchar_t* src = (const wchar_t*)GlobalLock(hData);
            if (src != nullptr)
            {
                text = src;
                GlobalUnlock(hData);
                return ClipboardResult::Ok();
            }
        }

        // Fall back to ANSI
        hData = ::GetClipboardData(CF_TEXT);
        if (hData != NULL)
        {
            const char* src = (const char*)GlobalLock(hData);
            if (src != nullptr)
            {
                // Convert ANSI to Unicode
                int len = MultiByteToWideChar(CP_ACP, 0, src, -1, NULL, 0);
                if (len > 0)
                {
                    text.resize(len - 1);  // -1 to exclude null terminator
                    MultiByteToWideChar(CP_ACP, 0, src, -1, &text[0], len);
                }
                GlobalUnlock(hData);
                return ClipboardResult::Ok();
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
        paths.clear();

        ClipboardSession session;
        if (!session.IsOpen())
            return ClipboardResult::Error(GetLastError());

        HANDLE hData = ::GetClipboardData(CF_HDROP);
        if (hData == NULL)
            return ClipboardResult::Error(ERROR_NOT_FOUND);

        HDROP hDrop = (HDROP)hData;
        UINT count = DragQueryFileW(hDrop, 0xFFFFFFFF, NULL, 0);

        paths.reserve(count);
        for (UINT i = 0; i < count; i++)
        {
            UINT len = DragQueryFileW(hDrop, i, NULL, 0);
            if (len > 0)
            {
                std::wstring path(len, L'\0');
                DragQueryFileW(hDrop, i, &path[0], len + 1);
                paths.push_back(std::move(path));
            }
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

        // Note: Caller should have clipboard open already for multiple SetRawData calls
        // For single calls, we open/close here
        ClipboardSession session;
        if (!session.IsOpen())
            return ClipboardResult::Error(GetLastError());

        HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE | GMEM_DDESHARE, size > 0 ? size : 1);
        if (hMem == NULL)
            return ClipboardResult::Error(GetLastError());

        if (size > 0)
        {
            void* dest = GlobalLock(hMem);
            if (dest == nullptr)
            {
                DWORD err = GetLastError();
                GlobalFree(hMem);
                return ClipboardResult::Error(err);
            }
            memcpy(dest, data, size);
            GlobalUnlock(hMem);
        }

        if (::SetClipboardData(format, hMem) == NULL)
        {
            DWORD err = GetLastError();
            GlobalFree(hMem);
            return ClipboardResult::Error(err);
        }

        return ClipboardResult::Ok();
    }

    ClipboardResult GetRawData(uint32_t format, std::vector<uint8_t>& data) override
    {
        data.clear();

        ClipboardSession session;
        if (!session.IsOpen())
            return ClipboardResult::Error(GetLastError());

        HANDLE hData = ::GetClipboardData(format);
        if (hData == NULL)
            return ClipboardResult::Error(ERROR_NOT_FOUND);

        SIZE_T size = GlobalSize(hData);
        if (size > 0)
        {
            const void* src = GlobalLock(hData);
            if (src != nullptr)
            {
                data.resize(size);
                memcpy(data.data(), src, size);
                GlobalUnlock(hData);
            }
        }

        return ClipboardResult::Ok();
    }

    uint32_t RegisterFormat(const wchar_t* name) override
    {
        if (name == nullptr)
            return 0;
        return ::RegisterClipboardFormatW(name);
    }

private:
    void SetAnsiText(const wchar_t* text, size_t wideLen)
    {
        // Convert to ANSI and set CF_TEXT for compatibility
        int ansiLen = WideCharToMultiByte(CP_ACP, 0, text, (int)wideLen, NULL, 0, NULL, NULL);
        if (ansiLen <= 0)
            return;

        HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE | GMEM_DDESHARE, ansiLen + 1);
        if (hMem == NULL)
            return;

        char* dest = (char*)GlobalLock(hMem);
        if (dest == nullptr)
        {
            GlobalFree(hMem);
            return;
        }

        WideCharToMultiByte(CP_ACP, 0, text, (int)wideLen, dest, ansiLen, NULL, NULL);
        dest[ansiLen] = '\0';
        GlobalUnlock(hMem);

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
