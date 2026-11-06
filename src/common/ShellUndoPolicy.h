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

// The Edit menu's Undo item does not ask the shell: reading its Undo caption means building the
// Desktop's whole context menu, which loads every shell extension. For a panel the item is offered
// whenever nothing is busy; choosing it (or Ctrl+Z) asks the shell and confirms what it would undo.
inline bool UndoItemEnabled(Focus focus, bool textCanUndo, bool busy)
{
    return ChooseTarget(focus, textCanUndo, true, busy) != Target::None;
}

// A menu caption as plain text for a message: without the shortcut after a tab and without
// accelerator marks ("&Undo" is "Undo"; "&&" is a literal '&').
inline std::wstring PlainMenuText(const std::wstring& caption)
{
    const size_t end = caption.find(L'\t');
    const size_t length = end == std::wstring::npos ? caption.size() : end;
    std::wstring text;
    for (size_t i = 0; i < length; ++i)
    {
        const wchar_t c = caption[i];
        // East Asian captions put the accelerator in its own group, "撤销(&U)" (ASCII or
        // full-width parentheses): the whole group goes.
        if ((c == L'(' || c == L'\xFF08') && i + 3 < length && caption[i + 1] == L'&' &&
            caption[i + 2] != L'&' && (caption[i + 3] == L')' || caption[i + 3] == L'\xFF09'))
        {
            i += 3;
            continue;
        }
        if (c == L'&')
        {
            ++i; // the marked character follows; "&&" is a literal '&'
            if (i < length)
                text.push_back(caption[i]);
            continue;
        }
        text.push_back(c);
    }
    while (!text.empty() && text.back() == L' ')
        text.pop_back();
    return text;
}

// The confirmation: 'pattern' is a translated sentence with one %s, filled with the operation as
// Windows names it in its own language (e.g. "Undo Delete"), or with 'fallback' when it names none.
// The shell's undo history is shared with File Explorer and other programs, so the operation may
// not be one made in Sally; the text says what will be undone before anything happens.
inline std::wstring UndoConfirmation(const std::wstring& pattern, const std::wstring& shellCaption,
                                     const std::wstring& fallback)
{
    std::wstring what = PlainMenuText(shellCaption);
    if (what.empty())
        what = PlainMenuText(fallback);
    std::wstring text = pattern;
    const size_t at = text.find(L"%s");
    if (at != std::wstring::npos)
        text.replace(at, 2, what);
    return text;
}
} // namespace sally::undo
