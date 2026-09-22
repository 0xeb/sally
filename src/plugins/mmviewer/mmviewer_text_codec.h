// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "common/Win32TextCodec.h"

#include <string>
#include <string_view>

namespace mmviewer
{

inline bool DecodeMetadataText(UINT codePage, const char* bytes, size_t length,
                               std::wstring& text) noexcept
{
    return Win32DecodeText(codePage, bytes, length, text).Succeeded();
}

// Terminal fallback only. The strict form above is load-bearing when probing
// UTF-8: its FAILURE is how AddItem detects that a tag is not UTF-8 and retries
// in the active code page. But once that retry is itself the last resort,
// strictness just discarded the whole metadata row - one odd byte in an ID3
// comment and the item vanished from the viewer instead of rendering with
// substitutions. Use this for the final attempt, never for the probe.
inline bool DecodeMetadataTextPermissive(UINT codePage, const char* bytes, size_t length,
                                         std::wstring& text) noexcept
{
    return Win32DecodeTextPermissive(codePage, bytes, length, text).Succeeded();
}

inline bool EncodeUtf16TagAsUtf8(std::wstring_view text, std::string& bytes) noexcept
{
    return Win32EncodeText(CP_UTF8, text.data(), text.size(), bytes).Succeeded();
}

} // namespace mmviewer
