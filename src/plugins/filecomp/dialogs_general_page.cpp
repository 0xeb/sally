// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"

DWORD LastCfgPage;

// CPropertyDialog aliases this pointer until Execute() consumes it, so retain a
// dedicated dynamically owned title rather than borrowing LangStr's cyclic buffer.
static LPCWSTR HoldPropertyDialogTitle(int resID) noexcept
{
    static thread_local std::wstring title;
    try
    {
        title = LangStr(resID);
        return title.c_str();
    }
    catch (...)
    {
        return L"";
    }
}

// ****************************************************************************
//
// CPropPageGeneral
//

CPropPageGeneral::CPropPageGeneral(CConfiguration* configuration, DWORD* changeFlag)
    : CCommonPropSheetPage(NULL, HLanguage, IDD_CFGGENERAL, IDD_CFGGENERAL, PSP_HASHELP, NULL)
{
    CALL_STACK_MESSAGE1("CPropPageGeneral::CPropPageGeneral(, , , , )");
    HFont = NULL;
    Configuration = configuration;
    LogFont = Configuration->FileViewLogFont;
    UseViewerFont = Configuration->UseViewerFont;
    ChangeFlag = changeFlag;
}

CPropPageGeneral::~CPropPageGeneral()
{
    CALL_STACK_MESSAGE1("CPropPageGeneral::~CPropPageGeneral()");
    if (HFont)
        DeleteObject(HFont);
}

void CPropPageGeneral::Validate(CTransferInfo& ti)
{
    CALL_STACK_MESSAGE1("CPropPageGeneral::Validate()");
    int i;
    ti.EditLine(IDE_CONTEXT, i);
    if (i < 0)
    {
        SG->SalMessageBox(HWindow, SPLLoadStrOwned(SG, HLanguage, IDS_POSITIVEVALUE).c_str(), SPLLoadStrOwned(SG, HLanguage, IDS_ERROR).c_str(), MB_ICONERROR);
        ti.ErrorOn(IDE_CONTEXT);
        return;
    }
    ti.EditLine(IDE_TABSIZE, i);
    if (i <= 0)
    {
        SG->SalMessageBox(HWindow, SPLLoadStrOwned(SG, HLanguage, IDS_POSITIVEVALUE).c_str(), SPLLoadStrOwned(SG, HLanguage, IDS_ERROR).c_str(), MB_ICONERROR);
        ti.ErrorOn(IDE_TABSIZE);
        return;
    }
}

void CPropPageGeneral::Transfer(CTransferInfo& ti)
{
    CALL_STACK_MESSAGE1("CPropPageGeneral::Transfer()");
    CConfiguration oldCfg = *Configuration;
    ti.CheckBox(IDC_CONFIRM, Configuration->ConfirmSelection);
    ti.EditLine(IDE_CONTEXT, Configuration->Context);
    ti.EditLine(IDE_TABSIZE, Configuration->TabSize);
    ti.CheckBox(IDC_DETAILDIFFS, Configuration->DetailedDifferences);
    ti.CheckBox(IDC_VIEW_HORIZONTAL, Configuration->HorizontalView);
    ti.CheckBox(IDC_LOADONSTART, LoadOnStart);
    HWND whiteSpace = GetDlgItem(HWindow, IDC_WHITESPACE);
    if (ti.Type == ttDataFromWindow)
    {
        if (memcmp(&Configuration->FileViewLogFont, &LogFont, sizeof(LogFont)) != 0 ||
            Configuration->UseViewerFont != UseViewerFont)
        {
            Configuration->FileViewLogFont = LogFont;
            Configuration->UseViewerFont = UseViewerFont;
            *ChangeFlag |= CC_FONT;
        }
        Configuration->WhiteSpace = (unsigned char)SendMessage(whiteSpace, CB_GETCURSEL, 0, 0) + 1;
        if (oldCfg.Context != Configuration->Context)
            *ChangeFlag |= CC_CONTEXT;
        if (oldCfg.TabSize != Configuration->TabSize)
            *ChangeFlag |= CC_TABSIZE;
        if (oldCfg.WhiteSpace != Configuration->WhiteSpace)
            *ChangeFlag |= CC_CHAR;
    }
    else
    {
        SendMessage(whiteSpace, CB_SETCURSEL, (int)Configuration->WhiteSpace - 1, 0);
    }
}

void CPropPageGeneral::LoadControls(BOOL initCombo)
{
    CALL_STACK_MESSAGE2("CPropPageGeneral::LoadControls(%d)", initCombo);

    // initialize the edit line for displaying the font sample
    HWND hEdit = GetDlgItem(HWindow, IDE_FONT);
    LOGFONT logFont;
    logFont = LogFont;
    logFont.lfHeight = SalGUI->GetWindowFontHeight(hEdit); // use the edit line font size to present the font
    if (HFont)
        DeleteObject(HFont);
    HFont = CreateFontIndirect(&logFont);

    HDC hDC = GetDC(HWindow);

    SendMessage(hEdit, WM_SETFONT, (WPARAM)HFont, MAKELPARAM(TRUE, 0));
    // logFont.lfFaceName is LOGFONT's own WCHAR[32] under _UNICODE - this call only ever wants a
    // narrow scratch buffer for the composed description text, so use a dedicated one rather than
    // borrowing the (possibly wide) struct field.
    // NOTE (pre-existing, out of scope for this pass): IDS_FONTDESCRIPTION's own %s still receives
    // LogFont.lfFaceName directly - under _UNICODE that argument is WCHAR[32], not narrow, so
    // sprintf's %s would read it as bytes rather than text. sprintf's varargs are unchecked by the
    // compiler, so this compiles either way; matches the same class of latent bug flagged (not
    // fixed) at pefile.cpp's GetExceptionMessage+fprintf earlier this codebase.
    // BUG FIX, not just a widening. lfFaceName is WCHAR[32] under UNICODE, and the
    // comment above already said so: sprintf's %s read it as BYTES, printing one character and
    // then stopping at the first zero high byte. varargs are unchecked, so it compiled and
    // shipped. Wide format, wide printf, wide field - all three agree now.
    wchar_t faceNameBuf[LF_FACESIZE * 4];
    _snwprintf_s(faceNameBuf, _countof(faceNameBuf), _TRUNCATE, LangStr(IDS_FONTDESCRIPTION).c_str(),
                 MulDiv(-LogFont.lfHeight, 72, GetDeviceCaps(hDC, LOGPIXELSY)),
                 LogFont.lfFaceName);
    SetWindowTextW(hEdit, faceNameBuf);

    // initialize the combo boxes for character selection
    HWND whiteSpace = GetDlgItem(HWindow, IDC_WHITESPACE);
    SendMessage(whiteSpace, WM_SETFONT, (WPARAM)HFont, FALSE);
    if (initCombo)
    {
        SendMessage(whiteSpace, WM_SETREDRAW, FALSE, 0);
        SendMessage(whiteSpace, CB_RESETCONTENT, 0, 0);
        wchar_t buf[20];
        int i;
        for (i = 1; i < 256; i++)
        {
            _snwprintf_s(buf, _countof(buf), _TRUNCATE, L"%-3d - %lc", i,
                         TCharSpecific<char>::Display[i]);
            SendMessageW(whiteSpace, CB_ADDSTRING, 0, (LPARAM)buf);
        }
        SendMessage(whiteSpace, WM_SETREDRAW, TRUE, 0);
    }

    ReleaseDC(HWindow, hDC);
}

INT_PTR
CPropPageGeneral::DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    CALL_STACK_MESSAGE4("CPropPageGeneral::WindowProc(0x%X, 0x%IX, 0x%IX)", uMsg,
                        wParam, lParam);
    switch (uMsg)
    {
    case WM_INITDIALOG:
    {
        SalGUI->AttachButton(HWindow, IDB_FONT, BTF_RIGHTARROW);
        LoadControls(TRUE);
        break;
    }

    case WM_COMMAND:
    {
        switch (LOWORD(wParam))
        {
        case IDB_FONT:
        {
            HMENU hMenu, hSubMenu;

            hMenu = LoadMenu(HLanguage, MAKEINTRESOURCE(IDM_CTX_FONT_MENU));
            hSubMenu = GetSubMenu(hMenu, 0);
            CheckMenuItem(hSubMenu, CM_CUSTOMFONT, MF_BYCOMMAND | (UseViewerFont ? MF_UNCHECKED : MF_CHECKED));
            CheckMenuItem(hSubMenu, CM_AUTOFONT, MF_BYCOMMAND | (UseViewerFont ? MF_CHECKED : MF_UNCHECKED));

            RECT r;
            GetWindowRect(GetDlgItem(HWindow, IDB_FONT), &r);

            TPMPARAMS tpmPar;
            tpmPar.cbSize = sizeof(tpmPar);
            tpmPar.rcExclude = r;
            switch (TrackPopupMenuEx(hSubMenu, TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RIGHTBUTTON | TPM_NONOTIFY | TPM_RETURNCMD, r.right, r.top, HWindow, &tpmPar))
            {
            case CM_CUSTOMFONT:
            {
                CHOOSEFONT cf;
                LOGFONT logFont = LogFont;
                memset(&cf, 0, sizeof(CHOOSEFONT));
                cf.lStructSize = sizeof(CHOOSEFONT);
                cf.hwndOwner = HWindow;
                cf.hDC = NULL;
                cf.lpLogFont = &logFont;
                cf.iPointSize = 10;
                cf.Flags = CF_NOVERTFONTS | CF_FIXEDPITCHONLY | CF_SCREENFONTS |
                           CF_INITTOLOGFONTSTRUCT | CF_ENABLEHOOK;
                cf.lpfnHook = ComDlgHookProc;
                if (ChooseFont(&cf) != 0)
                {
                    LogFont = logFont;
                    UseViewerFont = FALSE;
                }
                break;
            }

            case CM_AUTOFONT:
            {
                LogFont = Configuration->InternalViewerFont;
                UseViewerFont = TRUE;
                break;
            }
            }
            DestroyMenu(hMenu);
            LoadControls(FALSE);
            return 0;
        }
        }
        break;
    }
    }
    return CCommonPropSheetPage::DialogProc(uMsg, wParam, lParam);
}

// ****************************************************************************
//
// CConfigurationDialog
//

// helper object for centering the configuration dialog over its parent
class CCenteredPropertyWindow : public CWindow
{
protected:
    virtual LRESULT WindowProc(UINT uMsg, WPARAM wParam, LPARAM lParam)
    {
        switch (uMsg)
        {
        case WM_WINDOWPOSCHANGING:
        {
            WINDOWPOS* pos = (WINDOWPOS*)lParam;
            if (pos->flags & SWP_SHOWWINDOW)
                CenterWindow(HWindow);
            break;
        }

        case WM_APP + 1000: // we should detach from the dialog (centering is done)
        {
            DetachWindow();
            delete this; // a bit of a hack, but nothing will touch 'this' anymore so it's fine
            return 0;
        }
        }
        return CWindow::WindowProc(uMsg, wParam, lParam);
    }
};

#ifndef LPDLGTEMPLATEEX
#include <pshpack1.h>
typedef struct DLGTEMPLATEEX
{
    WORD dlgVer;
    WORD signature;
    DWORD helpID;
    DWORD exStyle;
    DWORD style;
    WORD cDlgItems;
    short x;
    short y;
    short cx;
    short cy;
} DLGTEMPLATEEX, *LPDLGTEMPLATEEX;
#include <poppack.h>
#endif // LPDLGTEMPLATEEX

// helper callback for centering the configuration dialog over the parent and removing the '?' button from the caption
int CALLBACK CenterCallback(HWND HWindow, UINT uMsg, LPARAM lParam)
{
    CALL_STACK_MESSAGE3("CenterCallback(, 0x%X, 0x%IX)", uMsg, lParam);
    if (uMsg == PSCB_INITIALIZED) // attach to the dialog
    {
        CCenteredPropertyWindow* wnd = new CCenteredPropertyWindow;
        if (wnd != NULL)
        {
            wnd->AttachToWindow(HWindow);
            if (wnd->HWindow == NULL)
                delete wnd; // the window is not attached, delete it right here
            else
            {
                PostMessage(wnd->HWindow, WM_APP + 1000, 0, 0); // detach CCenteredPropertyWindow from the dialog
            }
        }
    }
    if (uMsg == PSCB_PRECREATE) // remove the '?' button from the property sheet header
    {
        // Remove the DS_CONTEXTHELP style from the dialog box template
        if (((LPDLGTEMPLATEEX)lParam)->signature == 0xFFFF)
            ((LPDLGTEMPLATEEX)lParam)->style &= ~DS_CONTEXTHELP;
        else
            ((LPDLGTEMPLATE)lParam)->style &= ~DS_CONTEXTHELP;
    }
    return 0;
}

CConfigurationDialog::CConfigurationDialog(HWND parent, CConfiguration* configuration,
                                           SALCOLOR* colors, CCompareOptions* options, DWORD* changeFlag)
    : CPropertyDialog(parent, HLanguage, HoldPropertyDialogTitle(IDS_CONFIGURATIONTITLE),
                      LastCfgPage, PSH_HASHELP | PSH_NOAPPLYNOW | PSH_USECALLBACK, NULL, &LastCfgPage,
                      CenterCallback),
      Page1(configuration, changeFlag),
      Page2(colors, changeFlag, &Page1),
      Page3(options, changeFlag)
{
    CALL_STACK_MESSAGE1("CConfigurationDialog::CConfigurationDialog(, , , , )");
    Add(&Page1); // Genereal
    Add(&Page2); // Default Compare Options
    Add(&Page3); // Colors

    *changeFlag = 0;
}
