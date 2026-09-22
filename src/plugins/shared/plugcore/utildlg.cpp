// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"
#include "plugin_narrow_compat.h"

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

// ****************************************************************************

void HistoryComboBox(CTransferInfo& ti, int id, char* text, int textMax,
                     int historySize, char** history)
{
    CALL_STACK_MESSAGE4("HistoryComboBox(, %d, , %d, %d, )", id, textMax,
                        historySize);
    HWND combo;

    if (!ti.GetControl(combo, id))
        return;

    if (ti.Type == ttDataFromWindow)
    {
        ti.EditLine(id, text, textMax);

        int toMove = historySize - 1;

        // check whether the same item is already in history
        int i;
        for (i = 0; i < historySize; i++)
        {
            if (history[i] == NULL)
                break;
            if (SG->StrICmp(ToWideArg(history[i]).c_str(), ToWideArg(text).c_str()) == 0)
            {
                toMove = i;
                break;
            }
        }
        // allocate memory for the new item
        char* ptr = new char[strlen(text) + 1];
        if (ptr)
        {
            // free memory of the removed item
            if (history[toMove])
                delete[] history[toMove];
            // make room for the path we will store
            for (i = toMove; i > 0; i--)
                history[i] = history[i - 1];
            // store the path
            strcpy(ptr, text);
            history[0] = ptr;
        }
    }
    SendMessage(combo, CB_RESETCONTENT, 0, 0);
    for (int i = 0; i < historySize; i++)
    {
        if (history[i] == NULL)
            break;
        SendMessage(combo, CB_ADDSTRING, 0, (LPARAM)history[i]);
    }
    if (ti.Type == ttDataFromWindow)
        SendMessage(combo, CB_SETCURSEL, 0, 0);
    else
    {
        SendMessage(combo, CB_LIMITTEXT, textMax - 1, 0);
        SendMessage(combo, WM_SETTEXT, 0, (LPARAM)text);
        SendMessage(combo, CB_SETEDITSEL, 0, -1);
    }
}
