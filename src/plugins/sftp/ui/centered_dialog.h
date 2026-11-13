// Base class of the plugin's modal dialogs: centers the dialog over its parent (DS_CENTER in the templates covers
// the case without a parent) and carries the help topic id (F1 and the Help button call the callback set with
// SetupWinLibHelp, see HelpCallback in fs/sftp.cpp).
#pragma once

class CCenteredDialog : public CDialog
{
public:
    // helpID: an IDH_* id of sftp.rh2
    CCenteredDialog(HINSTANCE modul, int resID, UINT helpID, HWND parent, CObjectOrigin origin = ooStandard)
        : CDialog(modul, resID, helpID, parent, origin) {}

    // Without a topic: F1 does nothing (the dialog still swallows it, it does not fall through to the parent)
    CCenteredDialog(HINSTANCE modul, int resID, HWND parent) : CDialog(modul, resID, parent) {}

protected:
    INT_PTR DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam) override
    {
        if (uMsg == WM_INITDIALOG && Parent != nullptr)
            SalamanderGeneral->MultiMonCenterWindow(HWindow, Parent, TRUE);
        return CDialog::DialogProc(uMsg, wParam, lParam);
    }
};
