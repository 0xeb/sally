// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "Win32TextCodec.h"

#include <string>

namespace sally::file_list
{
enum class TextEncoding
{
    LegacyAcp,
    Utf8,
};

// File-list output preserves the historical ACP format only when every character has an exact
// representation. Otherwise it switches the whole record batch to strict UTF-8; the caller owns
// BOM and append-policy decisions. Both outputs remain unchanged on failure.
inline Win32TextConversionResult EncodeText(const std::wstring& text, std::string& bytes,
                                            TextEncoding& encoding)
{
    std::string candidate;
    Win32TextConversionResult result = Win32EncodeText(CP_ACP, text, candidate);
    TextEncoding candidateEncoding = TextEncoding::LegacyAcp;
    if (!result.Succeeded())
    {
        if (result.Error != Win32TextConversionError::UnrepresentableCharacter)
            return result;
        result = Win32EncodeText(CP_UTF8, text, candidate);
        if (!result.Succeeded())
            return result;
        candidateEncoding = TextEncoding::Utf8;
    }

    bytes.swap(candidate);
    encoding = candidateEncoding;
    return result;
}
} // namespace sally::file_list
