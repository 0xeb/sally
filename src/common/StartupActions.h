// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

// -do <action>: what Sally does once it has started and its panels are set (-l, -r, -a, -p). The
// action works on the focused item of the active panel, exactly as its menu command does, e.g.
//   sally.exe -l C:\Pictures\beach.jpg -p 1 -do view
// The names below are the whole list; an unknown name is a command-line error. Each runs Sally's own
// command, never a simulated key, so a changed hot key or keyboard layout does not matter.

#pragma once

#include <string>
#include <wchar.h>

namespace sally::actions
{

enum class StartupAction
{
    View, // F3
    Edit, // F4
};

struct StartupActionName
{
    const wchar_t* Name;
    StartupAction Action;
};

inline constexpr StartupActionName kStartupActions[] = {
    {L"view", StartupAction::View},
    {L"edit", StartupAction::Edit},
};

// The action of a name, compared without case.
inline bool FindStartupAction(const std::wstring& name, StartupAction& action)
{
    for (const StartupActionName& entry : kStartupActions)
    {
        if (_wcsicmp(name.c_str(), entry.Name) == 0)
        {
            action = entry.Action;
            return true;
        }
    }
    return false;
}

} // namespace sally::actions
