// Base class of the plugin's modal dialogs: centers the dialog over its parent (DS_CENTER in the templates covers
// the case without a parent).
#pragma once

class CCenteredDialog : public CDialog
{
public:
    CCenteredDialog(HINSTANCE modul, int resID, HWND parent) : CDialog(modul, resID, parent) {}

protected:
    INT_PTR DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam) override
    {
        if (uMsg == WM_INITDIALOG && Parent != nullptr)
            SalamanderGeneral->MultiMonCenterWindow(HWindow, Parent, TRUE);
        return CDialog::DialogProc(uMsg, wParam, lParam);
    }
};
