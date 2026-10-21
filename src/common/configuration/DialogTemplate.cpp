// SPDX-FileCopyrightText: 2025-2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#include "common/configuration/DialogTemplate.h"
#include <utility>

namespace sally::configuration
{
    namespace
    {
        constexpr std::uint32_t SetFont = 0x40;
        constexpr std::uint32_t FixedSystemFont = 0x08;

        struct Reader
        {
            const std::uint8_t* Bytes;
            std::size_t Size;
            std::size_t At = 0;

            bool Skip(std::size_t count)
            {
                if (At > Size || count > Size - At)
                    return false;
                At += count;
                return true;
            }
            bool Word(std::uint16_t& value)
            {
                if (!Skip(2))
                    return false;
                value = Bytes[At - 2] | (static_cast<std::uint16_t>(Bytes[At - 1]) << 8);
                return true;
            }
            bool Dword(std::uint32_t& value)
            {
                std::uint16_t low, high;
                if (!Word(low) || !Word(high))
                    return false;
                value = low | (static_cast<std::uint32_t>(high) << 16);
                return true;
            }
            bool Align()
            {
                return Skip((4 - At % 4) % 4);
            }
            bool Text(std::wstring& text, std::uint16_t& ordinal)
            {
                text.clear();
                ordinal = 0;
                std::uint16_t value;
                if (!Word(value))
                    return false;
                if (value == 0xffff)
                    return Word(ordinal);
                while (value != 0)
                {
                    text.push_back(static_cast<wchar_t>(value));
                    if (!Word(value))
                        return false;
                }
                return true;
            }
        };

        struct Positions
        {
            std::size_t Style = 0;
            std::size_t Font = 0;
            std::size_t Controls = 0;
        };

        bool Parse(const void* bytes, std::size_t size, DialogTemplate& output, Positions& positions)
        {
            if (bytes == nullptr || size < 4)
                return false;
            Reader reader{static_cast<const std::uint8_t*>(bytes), size};
            std::uint16_t first, second, count, width, height, ignored;
            std::uint32_t ignoredDword;
            if (!reader.Word(first) || !reader.Word(second))
                return false;
            output.Extended = first == 1 && second == 0xffff;
            if (output.Extended)
            {
                if (!reader.Skip(8))
                    return false; // help ID and extended style
                positions.Style = reader.At;
                if (!reader.Dword(output.Style))
                    return false;
            }
            else
            {
                output.Style = first | (static_cast<std::uint32_t>(second) << 16);
                if (!reader.Skip(4))
                    return false; // extended style
            }
            if (!reader.Word(count) || !reader.Skip(4) || !reader.Word(width) || !reader.Word(height))
                return false;
            output.Width = static_cast<std::int16_t>(width);
            output.Height = static_cast<std::int16_t>(height);
            std::wstring ignoredText;
            if (!reader.Text(ignoredText, ignored) || !reader.Text(ignoredText, ignored) ||
                !reader.Text(output.Title, ignored))
                return false;
            positions.Font = reader.At;
            if ((output.Style & SetFont) != 0)
            {
                if (!reader.Word(output.Font.Points))
                    return false;
                if (output.Extended)
                {
                    std::uint16_t attributes;
                    if (!reader.Word(output.Font.Weight) || !reader.Word(attributes))
                        return false;
                    output.Font.Italic = static_cast<std::uint8_t>(attributes);
                    output.Font.Charset = static_cast<std::uint8_t>(attributes >> 8);
                }
                if (!reader.Text(output.Font.Face, ignored) || ignored != 0)
                    return false;
            }
            // There need not be trailing alignment bytes in a zero-control resource.
            positions.Controls = reader.At;
            if (count != 0)
            {
                if (!reader.Align())
                    return false;
                positions.Controls = reader.At;
            }
            for (std::uint16_t i = 0; i < count; ++i)
            {
                if (!reader.Align())
                    return false;
                DialogControl control;
                if (output.Extended)
                {
                    if (!reader.Skip(8) || !reader.Dword(control.Style) || !reader.Skip(8) ||
                        !reader.Dword(control.Id))
                        return false;
                }
                else
                {
                    if (!reader.Dword(control.Style) || !reader.Dword(ignoredDword) || !reader.Skip(8) ||
                        !reader.Word(ignored))
                        return false;
                    control.Id = ignored;
                }
                if (!reader.Text(control.ClassName, control.ClassOrdinal) ||
                    !reader.Text(control.Text, ignored))
                    return false;
                std::uint16_t extra;
                if (!reader.Word(extra))
                    return false;
                // The standard template's size includes its own WORD; the extended
                // template's extraCount counts only following creation-data bytes.
                if (extra != 0)
                {
                    if (!output.Extended && extra < 2)
                        return false;
                    if (!reader.Skip(output.Extended ? extra : extra - 2))
                        return false;
                }
                output.Controls.push_back(std::move(control));
            }
            return true;
        }

        void AppendWord(std::vector<std::uint8_t>& bytes, std::uint16_t value)
        {
            bytes.push_back(static_cast<std::uint8_t>(value));
            bytes.push_back(static_cast<std::uint8_t>(value >> 8));
        }
    } // namespace

    bool ReadDialogTemplate(const void* bytes, std::size_t size, DialogTemplate& output)
    {
        DialogTemplate parsed;
        Positions positions;
        if (!Parse(bytes, size, parsed, positions))
        {
            output = DialogTemplate();
            return false;
        }
        output = std::move(parsed);
        return true;
    }

    bool SetDialogTemplateFont(const void* bytes, std::size_t size, const DialogFont& font,
                               std::vector<std::uint8_t>& output)
    {
        output.clear();
        DialogTemplate parsed;
        Positions positions;
        if (font.Face.empty() || font.Face.find(L'\0') != std::wstring::npos || font.Points == 0 ||
            !Parse(bytes, size, parsed, positions))
            return false;
        const auto* original = static_cast<const std::uint8_t*>(bytes);
        std::vector<std::uint8_t> replacement(original, original + positions.Font);
        const auto style = (parsed.Style | SetFont) & ~FixedSystemFont;
        for (int i = 0; i != 4; ++i)
            replacement[positions.Style + i] = static_cast<std::uint8_t>(style >> (i * 8));
        AppendWord(replacement, font.Points);
        if (parsed.Extended)
        {
            AppendWord(replacement, font.Weight);
            AppendWord(replacement, font.Italic | (static_cast<std::uint16_t>(font.Charset) << 8));
        }
        for (const auto ch : font.Face)
        {
            if (static_cast<std::uint32_t>(ch) > 0xffff)
                return false;
            AppendWord(replacement, static_cast<std::uint16_t>(ch));
        }
        AppendWord(replacement, 0);
        if (!parsed.Controls.empty())
            while (replacement.size() % 4 != 0)
                replacement.push_back(0);
        replacement.insert(replacement.end(), original + positions.Controls, original + size);
        output = std::move(replacement);
        return true;
    }
} // namespace sally::configuration
