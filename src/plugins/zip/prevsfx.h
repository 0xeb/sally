// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

struct CPreviewInitData
{
    CSfxSettings* Settings;
    // W. Unlike CSfxSettings above - which IS the SFX script's byte format -
    // these three never leave the process: prevsfx.cpp is this plugin's own preview dialog
    // proc and hands them straight to SetDlgItemText.
    // About is the SFX script's own text - char About[SE_MAX_ABOUT], serialized by
    // iosfxset.cpp - so it stays bytes end to end. The two button labels are ordinary
    // localized UI, so they are wide.
    const char* About;
    const wchar_t* AboutButton1;
    const wchar_t* AboutButton2;
    HICON LargeIcon;
    HICON SmallIcon;
    HINSTANCE SfxHInstance;
};

#define WM_USER_SETHCURSOR (WM_APP + 1)

INT_PTR WINAPI SfxPreviewDlgProc(HWND hDlg, UINT uMsg, WPARAM wParam, LPARAM lParam);
