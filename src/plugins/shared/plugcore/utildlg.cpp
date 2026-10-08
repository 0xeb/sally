// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"

// ****************************************************************************
//
// CDialogEx
//

INT_PTR
CDialogEx::DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    CALL_STACK_MESSAGE_NONE
    switch (uMsg)
    {
    case WM_INITDIALOG:
    {
        DialogStack.Push(HWindow);
        SG->MultiMonCenterWindow(HWindow, CenterToHWnd, FALSE);
        break;
    }

    case WM_DESTROY:
    {
        DialogStack.Pop();
        break;
    }
    }
    return CDialog::DialogProc(uMsg, wParam, lParam);
}
void CDialogEx::NotifDlgJustCreated()
{
    SalGUI->ArrangeHorizontalLines(HWindow);
}

