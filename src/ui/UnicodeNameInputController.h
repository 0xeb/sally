// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <string>
#include <windows.h>

class CUnicodeNameInputController
{
public:
    CUnicodeNameInputController();
    ~CUnicodeNameInputController();

    BOOL EnableForCombo(HWND parent, int controlID, const std::wstring& initialText,
                        wchar_t* historyW[], int historyWCount, int maxChars, int selectionEnd);
    void Reset();

    BOOL IsEnabled() const { return HUnicodeCombo != NULL; }
    HWND GetControlHandle() const { return HUnicodeCombo; }

    std::wstring GetText() const;
    void SetText(const std::wstring& text) const;
    void SyncSelectionToEdit() const;

private:
    HWND HParent;
    HWND HLegacyCombo;
    HWND HUnicodeCombo;
    HFONT HOwnedFont;
    LONG_PTR LegacyID;
    int ControlID;
};

