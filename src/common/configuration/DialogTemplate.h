// SPDX-FileCopyrightText: 2025-2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace sally::configuration
{
    struct DialogFont
    {
        std::uint16_t Points = 9;
        std::uint16_t Weight = 400;
        std::uint8_t Italic = 0;
        std::uint8_t Charset = 1;
        std::wstring Face;
    };

    struct DialogControl
    {
        std::uint32_t Id = 0;
        std::uint32_t Style = 0;
        std::uint16_t ClassOrdinal = 0;
        std::wstring ClassName;
        std::wstring Text;
    };

    struct DialogTemplate
    {
        bool Extended = false;
        std::uint32_t Style = 0;
        std::int16_t Width = 0;
        std::int16_t Height = 0;
        std::wstring Title;
        DialogFont Font;
        std::vector<DialogControl> Controls;
    };

    // Parse the resource itself, without creating windows or reading live edit
    // controls. Search metadata must never include user-entered paths or passwords.
    bool ReadDialogTemplate(const void* bytes, std::size_t size, DialogTemplate& output);

    // Preserve dialog-unit coordinates, control IDs, styles and creation data while
    // selecting the font before USER32 measures and creates the dialog and controls.
    bool SetDialogTemplateFont(const void* bytes, std::size_t size, const DialogFont& font,
                               std::vector<std::uint8_t>& output);
} // namespace sally::configuration
