// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "common/Win32TextCodec.h"

#include <climits>
#include <cstddef>
#include <string>

namespace sally::dbviewer
{
enum class LogicalCellValue
{
    Unknown,
    False,
    True,
};

inline LogicalCellValue ClassifyLogicalCellByte(char value) noexcept
{
    if (value == 'T' || value == 't' || value == 'Y' || value == 'y')
        return LogicalCellValue::True;
    if (value == 'F' || value == 'f' || value == 'N' || value == 'n' ||
        value == '0')
        return LogicalCellValue::False;
    return LogicalCellValue::Unknown;
}

// DBF and legacy CSV cells remain encoded bytes in the parser. Apply the
// selected single-byte conversion table, then cross once into UTF-16 at the
// rendering boundary. Publication is transactional and this UI adapter never
// lets allocation failures escape a window callback.
inline bool DecodeLegacyDisplayText(const char* text, size_t textLen,
                                    const unsigned char* codeTable,
                                    std::wstring& output) noexcept
{
    if (text == nullptr && textLen != 0)
        return false;
    if (textLen > static_cast<size_t>(INT_MAX))
        return false;

    try
    {
        if (textLen == 0)
        {
            std::wstring staged;
            output.swap(staged);
            return true;
        }

        std::string mapped(text, textLen);
        if (codeTable != nullptr)
        {
            for (char& value : mapped)
                value = static_cast<char>(codeTable[static_cast<unsigned char>(value)]);
        }

        // Permissive on purpose: this is the RENDERING boundary, which is
        // exactly the "best-effort diagnostic rendering is preferable to
        // refusing the whole payload" case Win32TextCodec.h reserves the
        // permissive variants for. Strict decoding blanked the ENTIRE cell on
        // one byte the active code page cannot decode, where the legacy viewer
        // showed the rest of the row.
        return Win32DecodeTextPermissive(CP_ACP, mapped.data(), mapped.size(), output)
            .Succeeded();
    }
    catch (...)
    {
        return false;
    }
}

inline bool DecodeUtf8DisplayText(const char* text, size_t textLen,
                                  std::wstring& output) noexcept
{
    // Permissive, for the same reason as DecodeLegacyDisplayText above and at the
    // same boundary: this decodes for RENDERING, and the alternative to a
    // best-effort rendering here is a blank cell.
    //
    // This used to be strict, on the argument that invalid UTF-8 is malformed data
    // rather than merely unmappable text. That distinction is real, but it argues
    // for showing U+FFFD - the character whose whole job is to say "malformed" -
    // not for showing nothing. One stray Latin-1 byte in a mislabelled export made
    // GetColumnName/GetCellText answer NULL with *textLen = 0, so the entire cell
    // vanished; the legacy viewer showed the rest of the row. Both csvlib call
    // sites inherit this.
    return Win32DecodeTextPermissive(CP_UTF8, text, textLen, output).Succeeded();
}
} // namespace sally::dbviewer
