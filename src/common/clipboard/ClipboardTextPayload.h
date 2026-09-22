// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "common/Win32TextCodec.h"

#include <cstring>
#include <cwchar>
#include <new>
#include <stdexcept>
#include <string>

namespace sally::clipboard
{

inline DWORD DecodeUnicodeClipboardPayload(const void* data, size_t byteSize,
                                            std::wstring& text) noexcept
{
    if (data == nullptr || byteSize < sizeof(wchar_t) || byteSize % sizeof(wchar_t) != 0)
        return ERROR_INVALID_DATA;

    const wchar_t* source = static_cast<const wchar_t*>(data);
    const size_t capacity = byteSize / sizeof(wchar_t);
    const wchar_t* terminator = std::wmemchr(source, L'\0', capacity);
    if (terminator == nullptr)
        return ERROR_INVALID_DATA;

    try
    {
        std::wstring candidate(source, terminator);
        text.swap(candidate);
        return ERROR_SUCCESS;
    }
    catch (const std::bad_alloc&)
    {
        return ERROR_NOT_ENOUGH_MEMORY;
    }
    catch (const std::length_error&)
    {
        return ERROR_NOT_ENOUGH_MEMORY;
    }
}

inline DWORD DecodeAnsiClipboardPayload(const void* data, size_t byteSize, UINT codePage,
                                         std::wstring& text) noexcept
{
    if (data == nullptr || byteSize == 0)
        return ERROR_INVALID_DATA;

    const char* source = static_cast<const char*>(data);
    const char* terminator = static_cast<const char*>(std::memchr(source, '\0', byteSize));
    if (terminator == nullptr)
        return ERROR_INVALID_DATA;

    std::wstring candidate;
    const Win32TextConversionResult converted = Win32DecodeTextPermissive(
        codePage, source, static_cast<size_t>(terminator - source), candidate);
    if (!converted)
        return converted.Win32Error;
    text.swap(candidate);
    return ERROR_SUCCESS;
}

} // namespace sally::clipboard
