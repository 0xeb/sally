// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "Win32TextCodec.h"

#include <string>

namespace checkver
{

// GitHub release metadata and Sally's version field are strict UTF-8 while they are
// owned by the byte-oriented release parser. Decode only when presenting those values.
inline bool Utf8ToWide(const std::string& bytes, std::wstring& text)
{
    return Win32DecodeText(CP_UTF8, bytes, text).Succeeded();
}

inline bool WideToUtf8(const std::wstring& text, std::string& bytes)
{
    return Win32EncodeText(CP_UTF8, text, bytes).Succeeded();
}

inline std::wstring Utf8ToWideOrEmpty(const std::string& bytes)
{
    std::wstring text;
    Utf8ToWide(bytes, text);
    return text;
}

} // namespace checkver
