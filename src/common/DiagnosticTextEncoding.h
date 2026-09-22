// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "common/Win32TextCodec.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <limits>
#include <string>

namespace sally::diagnostic
{

// Diagnostic byte streams are compatibility/presentation sinks, never semantic text owners.
// These adapters keep allocation and code-page selection at that boundary and publish only a
// complete result. Exact conversion is used where the legacy API can refuse; best-effort ACP is
// reserved for reports that also carry an escaped or otherwise lossless representation.
inline bool DecodeAcp(const char* bytes, size_t length, std::wstring& text) noexcept
{
    return static_cast<bool>(Win32DecodeText(GetACP(), bytes, length, text));
}

inline bool DecodeAcp(const char* bytes, std::wstring& text) noexcept
{
    if (bytes == nullptr)
    {
        std::wstring empty;
        text.swap(empty);
        return true;
    }
    return DecodeAcp(bytes, std::strlen(bytes), text);
}

inline bool DecodeAcpLossy(const char* bytes, size_t length, std::wstring& text) noexcept
{
    return static_cast<bool>(Win32DecodeTextPermissive(GetACP(), bytes, length, text));
}

inline std::wstring DecodeAcpLossy(const char* bytes) noexcept
{
    std::wstring decoded;
    if (bytes != nullptr)
        DecodeAcpLossy(bytes, std::strlen(bytes), decoded);
    return decoded;
}

// Trace Server stores received records in raw-relocatable arrays whose fields are malloc-owned
// pointers. Keep that layout while confining ACP decoding and allocation to this named boundary.
inline wchar_t* DuplicateDecodedAcp(const char* bytes, size_t length) noexcept
{
    std::wstring decoded;
    if (!DecodeAcpLossy(bytes, length, decoded) ||
        decoded.size() == (std::numeric_limits<size_t>::max)() ||
        decoded.size() + 1 > (std::numeric_limits<size_t>::max)() / sizeof(wchar_t))
    {
        return nullptr;
    }

    wchar_t* copy = static_cast<wchar_t*>(
        std::malloc((decoded.size() + 1) * sizeof(wchar_t)));
    if (copy == nullptr)
        return nullptr;
    std::wmemcpy(copy, decoded.c_str(), decoded.size() + 1);
    return copy;
}

inline bool EncodeAcpExact(const wchar_t* text, size_t length, std::string& bytes) noexcept
{
    return static_cast<bool>(Win32EncodeText(GetACP(), text, length, bytes));
}

inline bool EncodeAcpExact(const std::wstring& text, std::string& bytes) noexcept
{
    return EncodeAcpExact(text.data(), text.size(), bytes);
}

inline bool EncodeAcpLossy(const wchar_t* text, size_t length, std::string& bytes) noexcept
{
    return static_cast<bool>(Win32EncodeTextLossy(GetACP(), text, length, bytes));
}

inline bool EncodeAcpLossy(const wchar_t* text, std::string& bytes) noexcept
{
    if (text == nullptr)
        return false;
    return EncodeAcpLossy(text, std::wcslen(text), bytes);
}

inline std::string EncodeAcpLossy(const wchar_t* text) noexcept
{
    std::string encoded;
    EncodeAcpLossy(text, encoded);
    return encoded;
}

inline std::string EncodeAcpLossy(const std::wstring& text) noexcept
{
    std::string encoded;
    EncodeAcpLossy(text.data(), text.size(), encoded);
    return encoded;
}

inline bool EncodeUtf8(const wchar_t* text, std::string& bytes) noexcept
{
    if (text == nullptr)
        return false;
    return static_cast<bool>(Win32EncodeText(CP_UTF8, text, std::wcslen(text), bytes));
}

inline size_t CompleteAcpPrefix(const std::string& bytes, size_t capacity) noexcept
{
    size_t length = (std::min)(bytes.size(), capacity);
    while (length != 0 && length != bytes.size())
    {
        std::wstring decoded;
        const Win32TextConversionResult result =
            Win32DecodeText(GetACP(), bytes.data(), length, decoded);
        if (result)
            break;
        if (result.Error == Win32TextConversionError::OutOfMemory)
            return 0;
        --length;
    }
    return length;
}

inline bool EncodeAcpLossyPrefix(const wchar_t* text, size_t length, size_t capacity,
                                 std::string& bytes) noexcept
{
    std::string converted;
    if (!EncodeAcpLossy(text, length, converted))
        return false;
    converted.resize(CompleteAcpPrefix(converted, capacity));
    bytes.swap(converted);
    return true;
}

inline bool CopyAcpLossy(const wchar_t* text, char* destination, size_t capacity,
                         size_t* written = nullptr) noexcept
{
    if (written != nullptr)
        *written = 0;
    if (destination == nullptr || capacity == 0)
        return false;
    destination[0] = '\0';
    if (text == nullptr)
        return false;

    std::string converted;
    if (!EncodeAcpLossyPrefix(text, std::wcslen(text), capacity - 1, converted))
        return false;
    if (!converted.empty())
        std::memcpy(destination, converted.data(), converted.size());
    destination[converted.size()] = '\0';
    if (written != nullptr)
        *written = converted.size();
    return true;
}

inline bool CopyDecodedAcp(const char* bytes, wchar_t* destination, size_t capacity) noexcept
{
    if (destination == nullptr || capacity == 0)
        return false;
    destination[0] = L'\0';
    if (bytes == nullptr)
        return false;

    std::wstring decoded;
    if (!DecodeAcp(bytes, decoded))
        return false;
    size_t copied = (std::min)(decoded.size(), capacity - 1);
    if (copied < decoded.size() && copied != 0 &&
        decoded[copied - 1] >= 0xd800 && decoded[copied - 1] <= 0xdbff &&
        decoded[copied] >= 0xdc00 && decoded[copied] <= 0xdfff)
    {
        --copied;
    }
    if (copied != 0)
        std::wmemcpy(destination, decoded.data(), copied);
    destination[copied] = L'\0';
    return true;
}

} // namespace sally::diagnostic
