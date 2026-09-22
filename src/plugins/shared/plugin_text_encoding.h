// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <windows.h>

#include <climits>
#include <cstring>
#include <cwchar>
#include <string>

namespace sally::plugin_text
{

// Process-ACP conversion is retained only for plugin-owned compatibility bytes. These adapters
// are transactional and dynamically sized so callers never acquire mutable conversion buffers.
inline bool DecodeAcp(const char* bytes, size_t length, std::wstring& text) noexcept
{
    if ((bytes == nullptr && length != 0) || length > static_cast<size_t>(INT_MAX))
        return false;
    if (length == 0)
    {
        std::wstring empty;
        text.swap(empty);
        return true;
    }

    const UINT codePage = GetACP();
    const DWORD flags = codePage == CP_UTF8 ? MB_ERR_INVALID_CHARS : 0;
    const int inputLength = static_cast<int>(length);
    const int required = MultiByteToWideChar(codePage, flags, bytes, inputLength, nullptr, 0);
    if (required <= 0)
        return false;
    try
    {
        std::wstring candidate(static_cast<size_t>(required), L'\0');
        if (MultiByteToWideChar(codePage, flags, bytes, inputLength,
                                candidate.data(), required) != required)
        {
            return false;
        }
        text.swap(candidate);
        return true;
    }
    catch (...)
    {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return false;
    }
}

inline bool DecodeAcp(const char* bytes, std::wstring& text) noexcept
{
    return bytes != nullptr && DecodeAcp(bytes, std::strlen(bytes), text);
}

inline bool EncodeAcpExact(const wchar_t* value, size_t length, std::string& bytes) noexcept
{
    if ((value == nullptr && length != 0) || length > static_cast<size_t>(INT_MAX))
        return false;
    if (length == 0)
    {
        std::string empty;
        bytes.swap(empty);
        return true;
    }

    const UINT codePage = GetACP();
    const bool utf8 = codePage == CP_UTF8;
    const DWORD flags = utf8 ? WC_ERR_INVALID_CHARS : WC_NO_BEST_FIT_CHARS;
    BOOL usedDefault = FALSE;
    BOOL* usedDefaultPointer = utf8 ? nullptr : &usedDefault;
    const int inputLength = static_cast<int>(length);
    const int required = WideCharToMultiByte(codePage, flags, value, inputLength, nullptr, 0,
                                             nullptr, usedDefaultPointer);
    if (required <= 0 || usedDefault)
        return false;
    try
    {
        std::string candidate(static_cast<size_t>(required), '\0');
        usedDefault = FALSE;
        if (WideCharToMultiByte(codePage, flags, value, inputLength, candidate.data(), required,
                                nullptr, usedDefaultPointer) != required ||
            usedDefault)
        {
            return false;
        }
        bytes.swap(candidate);
        return true;
    }
    catch (...)
    {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return false;
    }
}

inline bool EncodeAcpExact(const wchar_t* value, std::string& bytes) noexcept
{
    return value != nullptr && EncodeAcpExact(value, std::wcslen(value), bytes);
}

inline bool EncodeAcpLossy(const wchar_t* value, std::string& bytes) noexcept
{
    if (value == nullptr)
        return false;
    const size_t length = std::wcslen(value);
    if (length > static_cast<size_t>(INT_MAX))
        return false;
    if (length == 0)
    {
        std::string empty;
        bytes.swap(empty);
        return true;
    }

    const UINT codePage = GetACP();
    const int inputLength = static_cast<int>(length);
    const int required = WideCharToMultiByte(codePage, 0, value, inputLength, nullptr, 0,
                                             nullptr, nullptr);
    if (required <= 0)
        return false;
    try
    {
        std::string candidate(static_cast<size_t>(required), '\0');
        if (WideCharToMultiByte(codePage, 0, value, inputLength, candidate.data(), required,
                                nullptr, nullptr) != required)
        {
            return false;
        }
        bytes.swap(candidate);
        return true;
    }
    catch (...)
    {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return false;
    }
}

} // namespace sally::plugin_text
