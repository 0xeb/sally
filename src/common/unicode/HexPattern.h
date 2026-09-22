// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace Sally::Unicode
{
// Parses the viewer/find hex-search syntax. Hex digits produce literal bytes;
// text inside quotes is encoded as UTF-8. Spaces outside quotes are ignored.
bool ParseHexPattern(std::wstring_view text, std::vector<std::uint8_t>& bytes);

// Formats literal bytes as the viewer's space-separated hexadecimal search syntax. The result is
// dynamically sized and published only when the complete selection can be represented.
bool FormatHexPattern(const std::uint8_t* bytes, std::size_t size,
                      std::wstring& text) noexcept;

// Applies the interactive hex editor's spacing rules without imposing a
// fixed text capacity. Selection offsets are updated with the inserted or
// removed characters.
void NormalizeHexPatternInput(std::wstring& text, int& selectionStart, int& selectionEnd);
}
