// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "SurrogateAssembler.h"

namespace sally::input
{
    inline bool ShouldForwardPanelCharacterToCommandLine(wchar_t character, bool controlPressed, bool altPressed,
                                                         bool quickSearchActive, bool enterQuickSearchWithAlt, bool noRun)
    {
        return !noRun && !controlPressed && !altPressed && !quickSearchActive &&
               character != L' ' && SurrogateAssembler::IsUsableInput(character) &&
               enterQuickSearchWithAlt;
    }
} // namespace sally::input
