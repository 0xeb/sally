// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "common/Win32TextCodec.h"

#include <cstdint>
#include <cstring>
#include <string>

namespace sally::legacy_search
{

// The historical regexp and Moore search implementations consume byte strings. Keep their
// process-ACP and UTF-8 projections explicit at this boundary; wide UI/search ownership must not
// acquire a second narrow mirror.
inline Win32TextConversionResult EncodePatternAcpExact(
    const std::wstring& pattern, std::string& bytes) noexcept
{
    return Win32EncodeText(GetACP(), pattern.data(), pattern.size(), bytes);
}

inline bool EncodePatternAcpLossy(const std::wstring& pattern,
                                  std::string& bytes) noexcept
{
    return static_cast<bool>(
        Win32EncodeTextLossy(GetACP(), pattern.data(), pattern.size(), bytes));
}

inline Win32TextConversionResult EncodePatternUtf8(
    const std::wstring& pattern, std::string& bytes) noexcept
{
    return Win32EncodeText(CP_UTF8, pattern.data(), pattern.size(), bytes);
}

inline bool DecodeAcp(const char* bytes, size_t length,
                      std::wstring& text) noexcept
{
    return static_cast<bool>(
        Win32DecodeTextPermissive(GetACP(), bytes, length, text));
}

inline std::wstring DecodeEngineAcp(const char* text) noexcept
{
    std::wstring decoded;
    if (text != nullptr)
        DecodeAcp(text, std::strlen(text), decoded);
    return decoded;
}

// Legacy viewer mode deliberately renders one visual cell per source byte. A DBCS lead byte is
// therefore incomplete by itself and becomes U+FFFD rather than borrowing its neighboring cell.
inline wchar_t DecodeDisplayCellAcp(std::uint8_t byte) noexcept
{
    const char source = static_cast<char>(byte);
    std::wstring decoded;
    if (!DecodeAcp(&source, 1, decoded) || decoded.size() != 1)
        return L'\xFFFD';
    return decoded[0];
}

} // namespace sally::legacy_search
