// SPDX-FileCopyrightText: 2025-2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <string>

namespace sally::undo
{
enum class Focus
{
    Other,
    Text,
    Panel
};
enum class Target
{
    None,
    Text,
    Shell
};

inline Target ChooseTarget(Focus focus, bool textCanUndo, bool shellAvailable, bool busy)
{
    if (focus == Focus::Text)
        return textCanUndo ? Target::Text : Target::None;
    if (focus == Focus::Panel && shellAvailable && !busy)
        return Target::Shell;
    return Target::None;
}

inline std::wstring MenuCaption(const std::wstring& shellCaption, const std::wstring& fallback)
{
    std::wstring caption = shellCaption.empty() ? fallback : shellCaption;
    const auto shortcut = caption.find(L'\t');
    if (shortcut != std::wstring::npos)
        caption.resize(shortcut);
    caption += L"\tCtrl+Z";
    return caption;
}
} // namespace sally::undo
