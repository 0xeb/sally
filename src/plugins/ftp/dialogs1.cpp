// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"

// Wide. This existed to widen a narrow LoadStr at the last moment; LangStr
// means there is nothing left to widen.
static int ShowMessage(HWND parent, const wchar_t* text, const wchar_t* caption, UINT type)
{
    return SalamanderGeneral->SalMessageBox(parent, text, caption, type);
}

TIndirectArray<CDialog> ModelessDlgs(2, 2, dtNoDelete); // array of "Welcome Message" dialogs

void MyEnableMenuItem(HMENU subMenu, int cmd, BOOL enable)
{
    EnableMenuItem(subMenu, cmd, MF_BYCOMMAND | (enable ? MF_ENABLED : MF_GRAYED));
}

//
// ****************************************************************************
// CCenteredDialog
//

INT_PTR
CCenteredDialog::DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    CALL_STACK_MESSAGE4("CCenteredDialog::DialogProc(0x%X, 0x%IX, 0x%IX)", uMsg, wParam, lParam);
    switch (uMsg)
    {
    case WM_INITDIALOG:
    {
        // horizontal and vertical centering of the dialog relative to the parent
        if (Parent != NULL)
            SalamanderGeneral->MultiMonCenterWindow(HWindow, Parent, TRUE);
        break; // I want the focus from DefDlgProc
    }
    }
    return CDialog::DialogProc(uMsg, wParam, lParam);
}

void CCenteredDialog::NotifDlgJustCreated()
{
    SalamanderGUI->ArrangeHorizontalLines(HWindow);
}

//
// ****************************************************************************
// CCommonPropSheetPage
//

void CCommonPropSheetPage::NotifDlgJustCreated()
{
    SalamanderGUI->ArrangeHorizontalLines(HWindow);
}

BOOL SetWindowLocalText(HWND window, const char* bytes) noexcept
{
    std::wstring text;
    if (!FtpDecodeLocalText(bytes, text))
        return FALSE;
    return SetWindowTextW(window, text.c_str());
}

BOOL ReadWindowLocalText(HWND window, std::string& bytes) noexcept
{
    try
    {
        const std::wstring text = SPLGetWindowTextOwned(window);
        return FtpEncodeLocalText(text.c_str(), bytes);
    }
    catch (...)
    {
        return FALSE;
    }
}

//
// ****************************************************************************
// CConfigPageGeneral
//

CConfigPageGeneral::CConfigPageGeneral() : CCommonPropSheetPage(NULL, HLanguage, IDD_CFGGENERAL, IDD_CFGGENERAL, PSP_HASHELP, NULL)
{
    LastTotSpeed = -1;
}

void CConfigPageGeneral::Validate(CTransferInfo& ti)
{
    // test if "total speed limit" (if used) is valid number
    int enableTotalSpeedLimit;
    ti.CheckBox(IDC_ENABLETOTSPEEDLIM, enableTotalSpeedLimit);
    double totalSpeedLimit;
    if (enableTotalSpeedLimit)
    {
        const wchar_t buff[] = L"%g";
        ti.EditLine(IDE_TOTALSPEEDLIMIT, totalSpeedLimit, buff);
        if (ti.IsGood() && totalSpeedLimit <= 0)
        {
            ShowMessage(HWindow, LangStr(IDS_MUSTBEGRTHANZERO).c_str(),
                                             LangStr(IDS_FTPERRORTITLE).c_str(), MB_OK | MB_ICONEXCLAMATION);
            ti.ErrorOn(IDE_TOTALSPEEDLIMIT);
        }
    }
}

void CConfigPageGeneral::Transfer(CTransferInfo& ti)
{
    ti.CheckBox(IDC_WELCOMEMESSAGE, Config.ShowWelcomeMessage);
    ti.CheckBox(IDC_PRIORITYTOPANELCON, Config.PriorityToPanelConnections);
    ti.CheckBox(IDC_ENABLETOTSPEEDLIM, Config.EnableTotalSpeedLimit);

    if (ti.Type == ttDataFromWindow && Config.EnableTotalSpeedLimit)
    {
        const wchar_t buff[] = L"%g";
        ti.EditLine(IDE_TOTALSPEEDLIMIT, Config.TotalSpeedLimit, buff);
        if (ti.Type == ttDataFromWindow &&
            Config.TotalSpeedLimit < 0.001)
            Config.TotalSpeedLimit = 0.001; // at least 1 byte per second
    }

    ti.CheckBox(IDC_ACCEPTHEXESCSEQ, Config.ConvertHexEscSeq);
    ti.CheckBox(IDC_CLOSEOPERDLGIFSUCCESS, Config.CloseOperationDlgIfSuccessfullyFinished);
    ti.CheckBox(IDC_OPENSOLVEERRIFIDLE, Config.OpenSolveErrIfIdle);

    std::wstring passwd;
    if (ti.Type == ttDataFromWindow)
    {
        ti.EditLine(IDE_ANONYMOUSPASSWD, passwd, TRUE);
        if (ti.IsGood() && !Config.SetAnonymousPasswd(passwd.c_str()))
            ti.ErrorOn(IDE_ANONYMOUSPASSWD);
    }
    else
    {
        if (!Config.GetAnonymousPasswd(passwd))
            passwd.clear();
        ti.EditLine(IDE_ANONYMOUSPASSWD, passwd, TRUE);
    }
}

static void CheckboxEditLine(BOOL isInt, HWND dlg, int checkboxID, int editID, int* lastCheck,
                             std::wstring& valueText, int checkedValInteger,
                             double checkedValDouble, BOOL globValUsed, int globValInteger,
                             double globValDouble) noexcept
{
    try
    {
        const int check = IsDlgButtonChecked(dlg, checkboxID);
        EnableWindow(GetDlgItem(dlg, editID), check == BST_CHECKED);
        if (*lastCheck != check)
        {
            if (*lastCheck == BST_CHECKED)
                valueText = SPLGetDlgItemTextOwned(dlg, editID);

            std::wstring displayedText;
            switch (check)
            {
            case BST_UNCHECKED:
                break; // switched off (empty string)

            case BST_CHECKED:
                if (valueText.empty())
                    valueText = isInt ? SPLFormatStringOwned(L"%d", checkedValInteger)
                                      : SPLFormatStringOwned(L"%g", checkedValDouble);
                displayedText = valueText;
                break;

            default: // third state
                if (globValUsed)
                    displayedText = isInt ? SPLFormatStringOwned(L"%d", globValInteger)
                                          : SPLFormatStringOwned(L"%g", globValDouble);
                else
                    displayedText.clear(); // not used (empty string)
                break;
            }
            SetDlgItemTextW(dlg, editID, displayedText.c_str());
            *lastCheck = check;
        }
    }
    catch (...)
    {
        // This helper runs from dialog callbacks; keep the previous value on allocation failure.
    }
}

void CheckboxEditLineInteger(HWND dlg, int checkboxID, int editID, int* lastCheck,
                             std::wstring& valueText, int checkedVal,
                             BOOL globValUsed, int globVal) noexcept
{
    CheckboxEditLine(TRUE, dlg, checkboxID, editID, lastCheck, valueText,
                     checkedVal, 0, globValUsed, globVal, 0);
}

void CheckboxEditLineDouble(HWND dlg, int checkboxID, int editID, int* lastCheck,
                            std::wstring& valueText, double checkedVal,
                            BOOL globValUsed, double globVal) noexcept
{
    CheckboxEditLine(FALSE, dlg, checkboxID, editID, lastCheck, valueText,
                     0, checkedVal, globValUsed, 0, globVal);
}

void CConfigPageGeneral::EnableControls()
{
    CheckboxEditLineDouble(HWindow, IDC_ENABLETOTSPEEDLIM, IDE_TOTALSPEEDLIMIT, &LastTotSpeed, TotalSpeedText,
                           Config.TotalSpeedLimit, 0, 0);
}

INT_PTR
CConfigPageGeneral::DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    CALL_STACK_MESSAGE4("CConfigPageGeneral::DialogProc(0x%X, 0x%IX, 0x%IX)", uMsg, wParam, lParam);
    switch (uMsg)
    {
    case WM_INITDIALOG:
    {
        INT_PTR ret = CCommonPropSheetPage::DialogProc(uMsg, wParam, lParam);
        EnableControls();
        return ret;
    }

    case WM_COMMAND:
    {
        if (HIWORD(wParam) == BN_CLICKED && LOWORD(wParam) == IDC_ENABLETOTSPEEDLIM)
        {
            EnableControls();
        }
        break;
    }
    }
    return CCommonPropSheetPage::DialogProc(uMsg, wParam, lParam);
}

//
// ****************************************************************************
// CConfigPageDefaults
//

CConfigPageDefaults::CConfigPageDefaults() : CCommonPropSheetPage(NULL, HLanguage, IDD_CFGDEFAULTS, IDD_CFGDEFAULTS, PSP_HASHELP, NULL)
{
    LastMaxCon = -1;
    LastSrvSpeed = -1;
    TmpFTPProxyServerList = NULL;
}

CConfigPageDefaults::~CConfigPageDefaults()
{
    if (TmpFTPProxyServerList != NULL)
        delete TmpFTPProxyServerList;
}

void CConfigPageDefaults::Validate(CTransferInfo& ti)
{
    // check for passive mode when using an HTTP proxy (an HTTP proxy supports only passive data transfers)
    int passiveMode;
    ti.CheckBox(IDC_PASSIVE, passiveMode);
    if (TmpFTPProxyServerList != NULL && !passiveMode)
    {
        int defaultProxySrvUID;
        ProxyComboBox(HWindow, ti, IDC_PROXYSERVER, defaultProxySrvUID, FALSE,
                      TmpFTPProxyServerList);
        if (TmpFTPProxyServerList->GetProxyType(defaultProxySrvUID) == fpstHTTP1_1)
        {
            ShowMessage(HWindow, LangStr(IDS_HTTPNEEDPASSIVETRMODE).c_str(),
                                             LangStr(IDS_FTPERRORTITLE).c_str(), MB_OK | MB_ICONEXCLAMATION);
            ti.ErrorOn(IDC_PASSIVE);
            return;
        }
    }

    // test if "max. concurrent connections" (if used) is valid number
    int enableMaxConcCon;
    ti.CheckBox(IDC_MAXCONCURRENTCON, enableMaxConcCon);
    int maxConcCon;
    if (enableMaxConcCon)
    {
        ti.EditLine(IDE_MAXCONCURRENTCON, maxConcCon);
        if (!ti.IsGood())
            return; // an error has already occurred
        if (ti.IsGood() && maxConcCon <= 0)
        {
            ShowMessage(HWindow, LangStr(IDS_MUSTBEGRTHANZERO).c_str(),
                                             LangStr(IDS_FTPERRORTITLE).c_str(), MB_OK | MB_ICONEXCLAMATION);
            ti.ErrorOn(IDE_MAXCONCURRENTCON);
            return;
        }
    }

    // test if "server speed limit" (if used) is valid number
    int enableSrvSpeedLimit;
    ti.CheckBox(IDC_SRVSPEEDLIMIT, enableSrvSpeedLimit);
    double srvSpeedLimit;
    if (enableSrvSpeedLimit)
    {
        const wchar_t buff[] = L"%g";
        ti.EditLine(IDE_SRVSPEEDLIMIT, srvSpeedLimit, buff);
        if (!ti.IsGood())
            return; // an error has already occurred
        if (ti.IsGood() && srvSpeedLimit <= 0)
        {
            ShowMessage(HWindow, LangStr(IDS_MUSTBEGRTHANZERO).c_str(),
                                             LangStr(IDS_FTPERRORTITLE).c_str(), MB_OK | MB_ICONEXCLAMATION);
            ti.ErrorOn(IDE_SRVSPEEDLIMIT);
            return;
        }
    }

    int num;
    int arr[] = {IDE_KEEPALIVEEVERY, IDE_KEEPALIVESTOPAFTER, -1};
    int i;
    for (i = 0; arr[i] != -1; i++)
    {
        ti.EditLine(arr[i], num);
        if (!ti.IsGood())
            return; // an error has already occurred
        if (num <= 0)
        {
            ShowMessage(HWindow, LangStr(IDS_MUSTBEGRTHANZERO).c_str(),
                                             LangStr(IDS_FTPERRORTITLE).c_str(), MB_OK | MB_ICONEXCLAMATION);
            ti.ErrorOn(arr[i]);
            return;
        }
    }

    int stop, every;
    ti.EditLine(IDE_KEEPALIVEEVERY, every);
    ti.EditLine(IDE_KEEPALIVESTOPAFTER, stop);
    if (every > 10000)
    {
        ShowMessage(HWindow, LangStr(IDS_KAEVERYTOOBIG).c_str(),
                                         LangStr(IDS_FTPERRORTITLE).c_str(), MB_OK | MB_ICONEXCLAMATION);
        ti.ErrorOn(IDE_KEEPALIVEEVERY);
        return;
    }
    if (stop > 10000)
    {
        ShowMessage(HWindow, LangStr(IDS_KASTOPTOOBIG).c_str(),
                                         LangStr(IDS_FTPERRORTITLE).c_str(), MB_OK | MB_ICONEXCLAMATION);
        ti.ErrorOn(IDE_KEEPALIVESTOPAFTER);
        return;
    }
    if (stop * 60 < every)
    {
        ShowMessage(HWindow, LangStr(IDS_KAEVERYGRTHSTOP).c_str(),
                                         LangStr(IDS_FTPERRORTITLE).c_str(), MB_OK | MB_ICONEXCLAMATION);
        ti.ErrorOn(IDE_KEEPALIVESTOPAFTER);
        return;
    }

    // test if "ASCII mask" (if used) is valid
    int transferMode = trmBinary;
    ti.RadioButton(IDC_AUTODETECT, trmAutodetect, transferMode);
    if (transferMode == trmAutodetect)
    {
        std::wstring masks;
        ti.EditLine(IDE_ASCIIMASKS, masks);

        CSalamanderMaskGroup* maskGroup = SalamanderGeneral->AllocSalamanderMaskGroup();
        if (maskGroup != NULL)
        {
            maskGroup->SetMasksString(masks.c_str(), FALSE);
            int err;
            if (!maskGroup->PrepareMasks(err))
            {
                ShowMessage(HWindow, LangStr(IDS_INCORRECTSYNTAX).c_str(),
                                                 LangStr(IDS_FTPERRORTITLE).c_str(), MB_OK | MB_ICONEXCLAMATION);
                ti.ErrorOn(IDE_ASCIIMASKS);
                PostMessage(GetDlgItem(HWindow, IDE_ASCIIMASKS), EM_SETSEL, err, err); // marking the error position
            }
            SalamanderGeneral->FreeSalamanderMaskGroup(maskGroup);
        }
        if (!ti.IsGood())
            return; // an error occurred
    }
}

// support for combo boxes with proxy servers
void ProxyComboBox(HWND hWindow, CTransferInfo& ti, int ctrlID, int& proxyUID, BOOL addDefault,
                   CFTPProxyServerList* proxyServerList)
{
    CALL_STACK_MESSAGE2("ProxyComboBox(, , %d,)", ctrlID);
    HWND hwnd;
    if (ti.GetControl(hwnd, ctrlID))
    {
        if (ti.Type == ttDataToWindow)
            proxyServerList->InitCombo(hwnd, proxyUID, addDefault);
        else
            proxyServerList->GetProxyUIDFromCombo(hwnd, proxyUID, addDefault);
    }
}

void CConfigPageDefaults::Transfer(CTransferInfo& ti)
{
    if (TmpFTPProxyServerList != NULL)
    {
        ProxyComboBox(HWindow, ti, IDC_PROXYSERVER, Config.DefaultProxySrvUID, FALSE,
                      TmpFTPProxyServerList);
        if (ti.Type == ttDataFromWindow) // copy the data back into the configuration
        {
            TmpFTPProxyServerList->CopyMembersToList(Config.FTPProxyServerList);
            Config.FTPServerList.CheckProxyServersUID(Config.FTPProxyServerList);
            // No need to validate Config.DefaultProxySrvUID, it was refreshed a few lines above
        }
    }

    ti.CheckBox(IDC_PASSIVE, Config.PassiveMode);
    ti.CheckBox(IDC_KEEPALIVE, Config.KeepAlive);

    ti.CheckBox(IDC_MAXCONCURRENTCON, Config.UseMaxConcurrentConnections);
    if (ti.Type == ttDataFromWindow && Config.UseMaxConcurrentConnections)
    {
        ti.EditLine(IDE_MAXCONCURRENTCON, Config.MaxConcurrentConnections);
    }

    ti.CheckBox(IDC_SRVSPEEDLIMIT, Config.UseServerSpeedLimit);
    if (ti.Type == ttDataFromWindow && Config.UseServerSpeedLimit)
    {
        const wchar_t buff[] = L"%g";
        ti.EditLine(IDE_SRVSPEEDLIMIT, Config.ServerSpeedLimit, buff);
        if (ti.Type == ttDataFromWindow &&
            Config.ServerSpeedLimit < 0.001)
            Config.ServerSpeedLimit = 0.001; // at least 1 byte per second
    }

    ti.CheckBox(IDC_USELISTINGSCACHE, Config.UseListingsCache);

    if (ti.Type == ttDataFromWindow)
    {
        int compressData;
        ti.CheckBox(IDC_COMPRESSDATA, compressData);
        switch (compressData)
        {
        case 0:
            Config.CompressData = 0;
            break; // NO
        case 1:
            Config.CompressData = 6;
            break; // Yes, default compression is 6
        }
    }
    else
    {
        int compressData;
        switch (Config.CompressData)
        {
        case 0:
            compressData = 0;
            break; // NO
        default:
        case 6:
            compressData = 1;
            break; // Yes, default compression is 6
        }
        ti.CheckBox(IDC_COMPRESSDATA, compressData);
    }

    ti.EditLine(IDE_KEEPALIVEEVERY, Config.KeepAliveSendEvery);
    ti.EditLine(IDE_KEEPALIVESTOPAFTER, Config.KeepAliveStopAfter);

    HWND combo;
    if (ti.GetControl(combo, IDC_KEEPALIVESEND))
    {
        if (ti.Type == ttDataToWindow)
        {
            SendMessageW(combo, CB_RESETCONTENT, 0, 0);
            int strID[] = {IDS_KEEPALIVECMDNOOP, IDS_KEEPALIVECMDPWD, IDS_KEEPALIVECMDNLST,
                           IDS_KEEPALIVECMDLIST, -1};
            int i;
            for (i = 0; strID[i] != -1; i++)
            {
                SendMessageW(combo, CB_ADDSTRING, 0,
                             reinterpret_cast<LPARAM>(LangStr(strID[i]).c_str()));
            }
            // verify that KeepAliveCommand is within bounds (only direct registry editing could break it)
            if (Config.KeepAliveCommand >= i)
                Config.KeepAliveCommand = i - 1;
            if (Config.KeepAliveCommand < 0)
                Config.KeepAliveCommand = 0;
            SendMessage(combo, CB_SETCURSEL, Config.KeepAliveCommand, 0);
        }
        else
        {
            int i = (int)SendMessage(combo, CB_GETCURSEL, 0, 0);
            if (i != CB_ERR)
                Config.KeepAliveCommand = i;
        }
    }

    ti.RadioButton(IDC_BINARYMODE, trmBinary, Config.TransferMode);
    ti.RadioButton(IDC_ASCIIMODE, trmASCII, Config.TransferMode);
    ti.RadioButton(IDC_AUTODETECT, trmAutodetect, Config.TransferMode);

    std::wstring masks;
    if (ti.Type == ttDataToWindow)
        masks = SPLGetMasksStringOwned(Config.ASCIIFileMasks);
    ti.EditLine(IDE_ASCIIMASKS, masks);
    if (ti.Type == ttDataFromWindow)
    {
        if (Config.TransferMode == trmAutodetect)
            Config.ASCIIFileMasks->SetMasksString(masks.c_str(), FALSE);
    }
}

void CConfigPageDefaults::EnableControls()
{
    BOOL enable = IsDlgButtonChecked(HWindow, IDC_AUTODETECT) == BST_CHECKED;
    EnableWindow(GetDlgItem(HWindow, IDE_ASCIIMASKS), enable);

    CheckboxEditLineInteger(HWindow, IDC_MAXCONCURRENTCON, IDE_MAXCONCURRENTCON, &LastMaxCon, MaxConnectionsText,
                            Config.MaxConcurrentConnections, 0, 0);
    CheckboxEditLineDouble(HWindow, IDC_SRVSPEEDLIMIT, IDE_SRVSPEEDLIMIT, &LastSrvSpeed, ServerSpeedText,
                           Config.ServerSpeedLimit, 0, 0);
}

INT_PTR
CConfigPageDefaults::DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    CALL_STACK_MESSAGE4("CConfigPageDefaults::DialogProc(0x%X, 0x%IX, 0x%IX)", uMsg, wParam, lParam);
    switch (uMsg)
    {
    case WM_INITDIALOG:
    {
        TmpFTPProxyServerList = new CFTPProxyServerList;
        if (TmpFTPProxyServerList != NULL)
        {
            if (!Config.FTPProxyServerList.CopyMembersToList(*TmpFTPProxyServerList))
            {
                delete TmpFTPProxyServerList;
                TmpFTPProxyServerList = NULL;
            }
        }
        else
            TRACE_E(LOW_MEMORY);

        SalamanderGUI->AttachButton(HWindow, IDB_ADDPROXYSRV, BTF_DROPDOWN);

        CGUIHyperLinkAbstract* hint = SalamanderGUI->AttachHyperLink(HWindow, IDT_ASCIIMASKHINTS, STF_DOTUNDERLINE);
        if (hint != NULL)
            hint->SetActionShowHint(SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_MASKS_HINT).c_str());
        INT_PTR ret = CCommonPropSheetPage::DialogProc(uMsg, wParam, lParam);
        EnableControls();
        return ret;
    }

    case WM_USER_BUTTONDROPDOWN:
    {
        if (LOWORD(wParam) == IDB_ADDPROXYSRV) // drop-down menu on the Add button
        {
            HMENU main = LoadMenu(HLanguage, MAKEINTRESOURCE(IDM_ADDPROXYSERVER));
            if (main != NULL)
            {
                HMENU subMenu = GetSubMenu(main, 0);
                if (subMenu != NULL)
                {
                    CGUIMenuPopupAbstract* salMenu = SalamanderGUI->CreateMenuPopup();
                    if (salMenu != NULL)
                    {
                        // enable menu items
                        HWND combo = GetDlgItem(HWindow, IDC_PROXYSERVER);
                        int sel = (int)SendMessage(combo, CB_GETCURSEL, 0, 0);
                        int count = (int)SendMessage(combo, CB_GETCOUNT, 0, 0);
                        int fixedItems = 1; // this will be 2 for "not used" + "default"
                        EnableMenuItem(subMenu, CM_EDITPROXYSRV, MF_BYCOMMAND | ((sel != CB_ERR && count != CB_ERR ? sel >= fixedItems : FALSE) ? MF_ENABLED : MF_DISABLED | MF_GRAYED));
                        EnableMenuItem(subMenu, CM_DELETEPROXYSRV, MF_BYCOMMAND | ((sel != CB_ERR && count != CB_ERR ? sel >= fixedItems : FALSE) ? MF_ENABLED : MF_DISABLED | MF_GRAYED));
                        EnableMenuItem(subMenu, CM_MOVEUPPROXYSRV, MF_BYCOMMAND | ((sel != CB_ERR && count != CB_ERR ? sel > fixedItems : FALSE) ? MF_ENABLED : MF_DISABLED | MF_GRAYED));
                        EnableMenuItem(subMenu, CM_MOVEDOWNPROXYSRV, MF_BYCOMMAND | ((sel != CB_ERR && count != CB_ERR ? sel >= fixedItems && sel + 1 < count : FALSE) ? MF_ENABLED : MF_DISABLED | MF_GRAYED));

                        salMenu->SetTemplateMenu(subMenu);

                        RECT r;
                        GetWindowRect(GetDlgItem(HWindow, (int)wParam), &r);
                        BOOL selectMenuItem = LOWORD(lParam);
                        DWORD flags = MENU_TRACK_RETURNCMD;
                        if (selectMenuItem)
                        {
                            salMenu->SetSelectedItemIndex(0);
                            flags |= MENU_TRACK_SELECT;
                        }
                        DWORD cmd = salMenu->Track(flags, r.left, r.bottom, HWindow, &r);
                        if (cmd != 0)
                            PostMessage(HWindow, WM_COMMAND, cmd, 0);
                        SalamanderGUI->DestroyMenuPopup(salMenu);
                    }
                }
                DestroyMenu(main);
            }
        }
        return TRUE;
    }

    case WM_COMMAND:
    {
        if (HIWORD(wParam) == BN_CLICKED &&
            (LOWORD(wParam) == IDC_AUTODETECT ||
             LOWORD(wParam) == IDC_ASCIIMODE ||
             LOWORD(wParam) == IDC_BINARYMODE ||
             LOWORD(wParam) == IDC_MAXCONCURRENTCON ||
             LOWORD(wParam) == IDC_SRVSPEEDLIMIT))
        {
            EnableControls();
        }
        if (TmpFTPProxyServerList != NULL)
        {
            switch (LOWORD(wParam))
            {
            case IDB_ADDPROXYSRV:
            {
                TmpFTPProxyServerList->AddProxyServer(HWindow, GetDlgItem(HWindow, IDC_PROXYSERVER));
                return TRUE;
            }

            case CM_EDITPROXYSRV:
            {
                TmpFTPProxyServerList->EditProxyServer(HWindow, GetDlgItem(HWindow, IDC_PROXYSERVER), FALSE);
                return TRUE;
            }

            case CM_DELETEPROXYSRV:
            {
                TmpFTPProxyServerList->DeleteProxyServer(HWindow, GetDlgItem(HWindow, IDC_PROXYSERVER), FALSE);
                return TRUE;
            }

            case CM_MOVEUPPROXYSRV:
            {
                TmpFTPProxyServerList->MoveUpProxyServer(GetDlgItem(HWindow, IDC_PROXYSERVER), FALSE);
                return TRUE;
            }

            case CM_MOVEDOWNPROXYSRV:
            {
                TmpFTPProxyServerList->MoveDownProxyServer(GetDlgItem(HWindow, IDC_PROXYSERVER), FALSE);
                return TRUE;
            }
            }
        }
        break;
    }
    }
    return CCommonPropSheetPage::DialogProc(uMsg, wParam, lParam);
}

//
// ****************************************************************************
// CConfigDlg
//

// helper object for centering the configuration dialog relative to the parent
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
            {
                HWND hParent = GetParent(HWindow);
                if (hParent != NULL)
                    SalamanderGeneral->MultiMonCenterWindow(HWindow, hParent, TRUE);
            }
            break;
        }

        case WM_APP + 1000: // we should detach from the dialog (already centered)
        {
            DetachWindow();
            delete this; // a bit hacky, but nothing touches 'this' anymore, so it's fine
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

// helper callback for centering the configuration dialog relative to the parent and removing the '?' button from the caption
int CALLBACK CenterCallback(HWND HWindow, UINT uMsg, LPARAM lParam)
{
    if (uMsg == PSCB_INITIALIZED) // attach to the dialog
    {
        CCenteredPropertyWindow* wnd = new CCenteredPropertyWindow;
        if (wnd != NULL)
        {
            wnd->AttachToWindow(HWindow);
            if (wnd->HWindow == NULL)
                delete wnd; // window is not attached, destroy it right here
            else
            {
                PostMessage(wnd->HWindow, WM_APP + 1000, 0, 0); // to detach CCenteredPropertyWindow from the dialog
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

CConfigDlg::CConfigDlg(HWND parent)
    : CPropertyDialog(parent, HLanguage, LangStr(IDS_CONFIGTITLE).c_str(),
                      Config.LastCfgPage, PSH_HASHELP | PSH_USECALLBACK | PSH_NOAPPLYNOW,
                      NULL, &Config.LastCfgPage, CenterCallback)
{
    Add(&PageGeneral);
    Add(&PageDefaults);
    Add(&PageConfirmations);
    Add(&PageOperations);
    Add(&PageOperations2);
    Add(&PageAdvanced);
    Add(&PageLogs);
    Add(&PageServers);
}

//
// ****************************************************************************
// CBookmarksListbox
//

CBookmarksListbox::CBookmarksListbox(CConnectDlg* dlg, int ctrlID)
    : CWindow(dlg->HWindow, ctrlID)
{
    ParentDlg = dlg;
}

void CBookmarksListbox::MoveUpDown(BOOL moveUp)
{
    int i = (int)SendMessage(HWindow, LB_GETCURSEL, 0, 0);
    if (i != LB_ERR)
    {
        if (moveUp)
        {
            if (i > 0)
                ParentDlg->MoveItem(HWindow, i, i - 1);
        }
        else
        {
            if (i + 1 < SendMessage(HWindow, LB_GETCOUNT, 0, 0))
                ParentDlg->MoveItem(HWindow, i, i + 1);
        }
    }
}

void CBookmarksListbox::OpenContextMenu(int curSel, int menuX, int menuY)
{
    HMENU main = LoadMenu(HLanguage,
                          MAKEINTRESOURCE(ParentDlg->AddBookmarkMode == 0 ? IDM_SRVCONTEXTMENU : IDM_SRVCONTEXTMENU2));
    if (main != NULL)
    {
        HMENU subMenu = GetSubMenu(main, 0);
        if (subMenu != NULL)
        {
            MyEnableMenuItem(subMenu, IDB_RENAMEBOOKMARK, curSel > 0);
            MyEnableMenuItem(subMenu, IDB_REMOVEBOOKMARK, curSel > 0);
            MyEnableMenuItem(subMenu, CM_MOVEUPBOOKMARK, curSel > 1);
            MyEnableMenuItem(subMenu, CM_MOVEDOWNBOOKMARK, curSel > 0 && curSel < ParentDlg->TmpFTPServerList.Count);
            DWORD cmd = TrackPopupMenuEx(subMenu, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_RIGHTBUTTON,
                                         menuX, menuY, HWindow, NULL);
            if (cmd == CM_MOVEUPBOOKMARK || cmd == CM_MOVEDOWNBOOKMARK)
            {
                MoveUpDown(cmd == CM_MOVEUPBOOKMARK);
            }
            else
            {
                if (cmd != 0)
                    PostMessage(ParentDlg->HWindow, WM_COMMAND, cmd, 0);
            }
        }
        DestroyMenu(main);
    }
}

LRESULT
CBookmarksListbox::WindowProc(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
    {
        switch (wParam)
        {
        case VK_INSERT:
            PostMessage(ParentDlg->HWindow, WM_COMMAND, IDB_NEWBOOKMARK, 0);
            break;
        case VK_F2:
            PostMessage(ParentDlg->HWindow, WM_COMMAND, IDB_RENAMEBOOKMARK, 0);
            break;
        case VK_DELETE:
            PostMessage(ParentDlg->HWindow, WM_COMMAND, IDB_REMOVEBOOKMARK, 0);
            break;

        case VK_UP:
        case VK_DOWN:
        {
            if (GetKeyState(VK_MENU) & 0x8000)
                MoveUpDown(LOWORD(wParam) == VK_UP); // Alt pressed
            break;
        }

        case VK_F10:
            if ((GetKeyState(VK_SHIFT) & 0x8000) == 0)
                break;
        case VK_APPS:
        {
            int curSel = (int)SendMessage(HWindow, LB_GETCURSEL, 0, 0);
            if (curSel != LB_ERR)
            {
                RECT r;
                SendMessage(HWindow, LB_GETITEMRECT, curSel, (LPARAM)&r);
                POINT p;
                p.x = r.left;
                p.y = r.bottom;
                ClientToScreen(HWindow, &p);
                OpenContextMenu(curSel, p.x + 10, p.y);
            }
            break;
        }
        }
        break; // let the keys propagate, the listbox should not handle them, no problem
    }

    case WM_LBUTTONDBLCLK:
    {
        if (ParentDlg->AddBookmarkMode == 0)
            PostMessage(ParentDlg->HWindow, WM_COMMAND, IDOK, 0);
        break;
    }

    case WM_RBUTTONDOWN:
    {
        if (GetFocus() != HWindow)
        {
            SendMessage(ParentDlg->HWindow, WM_NEXTDLGCTL, (WPARAM)HWindow, TRUE);
        }
        int curSel = (int)SendMessage(HWindow, LB_GETCURSEL, 0, 0);
        int item = (int)SendMessage(HWindow, LB_ITEMFROMPOINT, 0,
                                    MAKELPARAM(LOWORD((int)lParam), HIWORD((int)lParam))); // FIXME_X64 suspicious cast
        if (HIWORD(item) == 0 && item >= 0 && item <= ParentDlg->TmpFTPServerList.Count &&
            curSel != item)
        {
            SendMessage(HWindow, LB_SETCURSEL, item, 0);
            SendMessage(ParentDlg->HWindow, WM_COMMAND, MAKELPARAM(IDL_BOOKMARKS, LBN_SELCHANGE), 0);
            curSel = item;
        }

        if (HIWORD(item) == 0 && curSel != LB_ERR)
        {
            POINT p;
            GetCursorPos(&p);
            OpenContextMenu(curSel, p.x, p.y);
        }
        break;
    }
    }
    return CWindow::WindowProc(uMsg, wParam, lParam);
}

//
// ****************************************************************************
// CConnectDlg
//

CConnectDlg::CConnectDlg(HWND parent, int addBookmarkMode)
    : CCenteredDialog(HLanguage, IDD_CONNECT, addBookmarkMode != 0 ? IDH_ORGBOOKMARKSDLG : IDD_CONNECT, parent)
{
    OK = Config.FTPServerList.CopyMembersToList(TmpFTPServerList);
    if (OK)
        OK = Config.FTPProxyServerList.CopyMembersToList(TmpFTPProxyServerList);
    CanChangeFocus = TRUE;
    DragIndex = -1;
    ExtraDragDropItemAdded = FALSE;
    AddBookmarkMode = addBookmarkMode;
    LastRawHostAddress.clear();
}

void CConnectDlg::Validate(CTransferInfo& ti)
{
}

void CConnectDlg::Transfer(CTransferInfo& ti)
{
    HWND list;
    if (ti.GetControl(list, IDL_BOOKMARKS))
    {
        if (ti.Type == ttDataToWindow)
        {
            SendMessage(list, WM_SETREDRAW, FALSE, 0);
            SendMessageW(list, LB_RESETCONTENT, 0, 0);
            SendMessageW(list, LB_ADDSTRING, 0,
                         reinterpret_cast<LPARAM>(LangStr(IDS_QUICKCONNECT).c_str()));
            TmpFTPServerList.AddNamesToListbox(list);
            if (Config.LastBookmark > TmpFTPServerList.Count)
            {
                Config.LastBookmark = TmpFTPServerList.Count;
            }
            if (Config.LastBookmark < 0)
                Config.LastBookmark = 0;
            SendMessage(list, LB_SETCURSEL, AddBookmarkMode != 2 ? Config.LastBookmark : TmpFTPServerList.Count, 0);
            SendMessage(list, WM_SETREDRAW, TRUE, 0);
            if (AddBookmarkMode == 0 && Config.LastBookmark == 0 /* quick connect */)
                PostMessage(HWindow, WM_NEXTDLGCTL, (WPARAM)GetDlgItem(HWindow, IDE_HOSTADDRESS), TRUE);
        }
        else
        {
            int i = (int)SendMessage(list, LB_GETCURSEL, 0, 0);
            if (i != LB_ERR)
                Config.LastBookmark = i;

            // copy the data back into the configuration (button: Connect or Close)
            TmpFTPServerList.CopyMembersToList(Config.FTPServerList);
            TmpFTPProxyServerList.CopyMembersToList(Config.FTPProxyServerList);
            // verify that the "default" proxy server still exists
            if (Config.DefaultProxySrvUID != -1 &&
                !Config.FTPProxyServerList.IsValidUID(Config.DefaultProxySrvUID))
            {
                Config.DefaultProxySrvUID = -1; // "not used"
            }
        }
    }

    // restore the non-expanded variant of the Address string (history stores what the user typed, not the split result)
    if (ti.IsGood() && ti.Type == ttDataFromWindow && Config.LastBookmark == 0)
        SetWindowTextW(GetDlgItem(HWindow, IDE_HOSTADDRESS), LastRawHostAddress.c_str());
    std::wstring hostAddress;
    HistoryComboBox(HWindow, ti, IDE_HOSTADDRESS, hostAddress,
                    HOSTADDRESS_HISTORY_SIZE, Config.HostAddressHistory,
                    Config.LastBookmark != 0 /* store in history only during Quick Connect*/);
    std::wstring initialPath;
    HistoryComboBox(HWindow, ti, IDE_INITIALPATH, initialPath,
                    INITIALPATH_HISTORY_SIZE, Config.InitPathHistory,
                    Config.LastBookmark != 0 /* store in history only during Quick Connect*/);
}

static BOOL AppendAdvancedText(std::wstring& text, std::wstring_view addition) noexcept
{
    try
    {
        if (!text.empty())
            text.append(L", ");
        text.append(addition);
        return TRUE;
    }
    catch (...)
    {
        return FALSE;
    }
}

void CConnectDlg::SelChanged()
{
    CFTPServer* s;
    int i;
    if (!GetCurSelServer(&s, &i))
        return; // unexpected situation

    BOOL lockedPassword = TRUE;
    std::wstring password;
    if (s->AnonymousConnection)
    {
        if (!Config.GetAnonymousPasswd(password))
            password.clear();
        lockedPassword = FALSE;
    }
    else
    {
        CSalamanderPasswordManagerAbstract* passwordManager = SalamanderGeneral->GetSalamanderPasswordManager();
        if (!s->SavePassword || !passwordManager->IsUsingMasterPassword() || passwordManager->IsMasterPasswordSet())
        {
            if (s->EncryptedPassword != NULL)
            { // scrambled/encrypted -> plain
                std::wstring plainPassword;
                if (FTPDecryptPasswordW(passwordManager, s->EncryptedPassword,
                                        s->EncryptedPasswordSize, &plainPassword))
                {
                    password.swap(plainPassword);
                    FTPSecureWipe(plainPassword);
                    lockedPassword = FALSE;
                }
            }
            else
                lockedPassword = FALSE;
        }
    }

    ShowHidePasswordControls(lockedPassword, FALSE);

    CTransferInfo ti(HWindow, ttDataToWindow);
    ti.EditLine(IDE_HOSTADDRESS, s->Address);
    ti.EditLine(IDE_INITIALPATH, s->InitialPath);
    ti.CheckBox(IDC_ANONYMOUSLOGIN, s->AnonymousConnection);
    std::wstring userName = s->AnonymousConnection ? L"anonymous" : s->UserName;
    ti.EditLine(IDE_USERNAME, userName);
    ti.EditLine(IDE_PASSWORD, password);
    FTPSecureWipe(password);

    int savePasswd = (s->AnonymousConnection || i == 0) ? FALSE : s->SavePassword;
    ti.CheckBox(IDC_SAVEPASSWORD, savePasswd);

    std::wstring advancedInfo;
    BOOL advancedGood = TRUE;
    const auto appendResource = [&](int resourceID) {
        if (!advancedGood)
            return;
        try
        {
            advancedGood = AppendAdvancedText(advancedInfo, LangStr(resourceID));
        }
        catch (...)
        {
            advancedGood = FALSE;
        }
    };
    const auto appendFormatted = [&](int resourceID, const auto& value) {
        if (!advancedGood)
            return;
        try
        {
            const std::wstring format = LangStr(resourceID);
            const std::wstring formatted = SPLFormatStringOwned(format.c_str(), value);
            advancedGood = AppendAdvancedText(advancedInfo, formatted);
        }
        catch (...)
        {
            advancedGood = FALSE;
        }
    };
    if (s->ProxyServerUID != -2)
    {
        std::wstring proxyName;
        if (TmpFTPProxyServerList.GetProxyName(proxyName, s->ProxyServerUID))
            appendFormatted(IDS_ADVSTRPROXYSRV, proxyName.c_str());
        else
            TRACE_E("Unexpected situation in CConnectDlg::SelChanged(): invalid ProxyServerUID!");
    }
    if (s->Port != IPPORT_FTP)
        appendFormatted(IDS_ADVSTRPORT, s->Port);
    if (s->TransferMode != 0)
    {
        switch (s->TransferMode)
        {
        case 1:
            appendResource(IDS_ADVSTRTRMODBINARY);
            break;
        case 2:
            appendResource(IDS_ADVSTRTRMODASCII);
            break;
        default:
            appendResource(IDS_ADVSTRTRMODAUTO);
            break;
        }
    }
    if (s->UsePassiveMode != 2)
        appendResource(s->UsePassiveMode == 0 ? IDS_ADVSTRSERVERPASVNO : IDS_ADVSTRSERVERPASVYES);
    if (s->KeepConnectionAlive != 2)
        appendResource(s->KeepConnectionAlive == 0 ? IDS_ADVSTRKEEPALIVENO : IDS_ADVSTRKEEPALIVEYES);
    if (s->UseMaxConcurrentConnections != 2)
    {
        if (s->UseMaxConcurrentConnections == 1)
            appendFormatted(IDS_ADVSTRMAXCONCURCON, s->MaxConcurrentConnections);
        else
            appendResource(IDS_ADVSTRUNLIMITEDCON);
    }
    if (s->UseServerSpeedLimit != 2)
    {
        if (s->UseServerSpeedLimit == 1)
            appendFormatted(IDS_ADVSTRSPEEDLIM, s->ServerSpeedLimit);
        else
            appendResource(IDS_ADVSTRUNLIMITEDSPEED);
    }
    if (!s->ServerType.empty())
    {
        std::wstring serverType;
        if (GetTypeNameForUser(s->ServerType.c_str(), serverType))
            appendFormatted(IDS_ADVSTRSERVERTYPE, serverType.c_str());
        else
            advancedGood = FALSE;
    }
    if (s->UseListingsCache != 2)
        appendResource(s->UseListingsCache == 0 ? IDS_ADVSTRUSECACHENO : IDS_ADVSTRUSECACHEYES);
    if (s->EncryptControlConnection == 1)
        appendResource(s->EncryptDataConnection == 1 ? IDS_ADVSTRSSLCONTRDATA : IDS_ADVSTRSSLCONTRONLY);
    if (s->CompressData != -1)
        appendResource(s->CompressData == 0 ? IDS_ADVSTRNOMODEZ : IDS_ADVSTRMODEZ);

    if (!s->ListCommand.empty())
    {
        std::wstring command;
        if (FtpDecodeLocalText(s->ListCommand, command))
            appendFormatted(IDS_ADVSTRLISTCOMMAND, command.c_str());
        else
            advancedGood = FALSE;
    }
    if (!s->InitFTPCommands.empty())
    {
        std::wstring commands;
        if (FtpDecodeLocalText(s->InitFTPCommands, commands))
            appendFormatted(IDS_ADVSTRINITFTPCMDS, commands.c_str());
        else
            advancedGood = FALSE;
    }
    if (!s->TargetPanelPath.empty())
        appendFormatted(IDS_ADVSTRTARGETPATH, s->TargetPanelPath.c_str());
    if (!advancedGood || advancedInfo.empty())
        advancedInfo = LangStr(IDS_ADVSTRNONE).c_str();
    SetDlgItemTextW(HWindow, IDE_ADVANCEDINFO, advancedInfo.c_str());
}

void CConnectDlg::EnableControls()
{
    BOOL quickCon = SendMessage(GetDlgItem(HWindow, IDL_BOOKMARKS), LB_GETCURSEL, 0, 0) == 0;
    BOOL enable = IsDlgButtonChecked(HWindow, IDC_ANONYMOUSLOGIN) == BST_UNCHECKED &&
                  (!quickCon || AddBookmarkMode == 0);
    EnableWindow(GetDlgItem(HWindow, IDT_USERNAME), enable);
    EnableWindow(GetDlgItem(HWindow, IDE_USERNAME), enable);
    EnableWindow(GetDlgItem(HWindow, IDT_PASSWORD), enable);
    EnableWindow(GetDlgItem(HWindow, IDE_PASSWORD), enable);
    if (enable)
        enable = !quickCon;
    EnableWindow(GetDlgItem(HWindow, IDC_SAVEPASSWORD), enable);
    EnableWindow(GetDlgItem(HWindow, IDE_PASSWORD_LOCKED), FALSE);
    EnableWindow(GetDlgItem(HWindow, IDB_PASSWORD_CHANGE), enable);
    EnableWindow(GetDlgItem(HWindow, IDC_SAVEPASSWORD_HINT), enable);

    enable = !quickCon;
    if (!enable && GetFocus() == GetDlgItem(HWindow, IDB_REMOVEBOOKMARK))
    {
        SendMessage(HWindow, WM_NEXTDLGCTL, (WPARAM)GetDlgItem(HWindow, IDL_BOOKMARKS), TRUE);
    }
    EnableWindow(GetDlgItem(HWindow, IDB_RENAMEBOOKMARK), enable);
    EnableWindow(GetDlgItem(HWindow, IDB_REMOVEBOOKMARK), enable);

    if (AddBookmarkMode != 0) // "organize bookmarks"
    {
        enable = !quickCon;
        EnableWindow(GetDlgItem(HWindow, IDT_HOSTADDRESS), enable);
        EnableWindow(GetDlgItem(HWindow, IDE_HOSTADDRESS), enable);
        EnableWindow(GetDlgItem(HWindow, IDT_INITIALPATH), enable);
        EnableWindow(GetDlgItem(HWindow, IDE_INITIALPATH), enable);
        EnableWindow(GetDlgItem(HWindow, IDC_ANONYMOUSLOGIN), enable);
        EnableWindow(GetDlgItem(HWindow, IDB_ADVACED), enable);
        EnableWindow(GetDlgItem(HWindow, IDE_ADVANCEDINFO), enable);
    }
}

void CConnectDlg::AlignPasswordControls()
{
    // the non-password edit line (with the info text) will be moved to where the password edit line starts and stretched to the button
    RECT editRect;
    GetWindowRect(GetDlgItem(HWindow, IDE_PASSWORD), &editRect);
    RECT editLockedRect;
    GetWindowRect(GetDlgItem(HWindow, IDE_PASSWORD_LOCKED), &editLockedRect);
    int width = editLockedRect.right - editRect.left;
    MapWindowPoints(NULL, HWindow, (POINT*)&editRect, 2);
    SetWindowPos(GetDlgItem(HWindow, IDE_PASSWORD_LOCKED), NULL, editRect.left, editRect.top, width, editLockedRect.bottom - editLockedRect.top, SWP_NOZORDER);

    // the password edit line will be as long as the one above it
    GetWindowRect(GetDlgItem(HWindow, IDE_USERNAME), &editRect);
    SetWindowPos(GetDlgItem(HWindow, IDE_PASSWORD), NULL, 0, 0, editRect.right - editRect.left, editRect.bottom - editRect.top, SWP_NOMOVE | SWP_NOZORDER);
}

void CConnectDlg::ShowHidePasswordControls(BOOL lockedPassword, BOOL focusEdit)
{
    CFTPServer* s;
    int i;
    if (!GetCurSelServer(&s, &i))
        return; // unexpected situation

    BOOL quickCon = (i == 0);
    BOOL showUnlockButton = lockedPassword;
    ShowWindow(GetDlgItem(HWindow, IDE_PASSWORD), !showUnlockButton);
    ShowWindow(GetDlgItem(HWindow, IDE_PASSWORD_LOCKED), showUnlockButton);
    ShowWindow(GetDlgItem(HWindow, IDB_PASSWORD_CHANGE), showUnlockButton);

    if (!showUnlockButton && focusEdit)
    {
        SetFocus(GetDlgItem(HWindow, IDE_PASSWORD));
        SendDlgItemMessage(HWindow, IDE_PASSWORD, CB_SETEDITSEL, 0, MAKELPARAM(0, -1));
        SendMessage(HWindow, DM_SETDEFID, IDE_PASSWORD, 0);
    }
}

void CConnectDlg::RefreshList(BOOL focusLast)
{
    HWND list = GetDlgItem(HWindow, IDL_BOOKMARKS);
    int focus = (int)SendMessage(list, LB_GETCURSEL, 0, 0);
    int topIndex = (int)SendMessage(list, LB_GETTOPINDEX, 0, 0);
    SendMessage(list, WM_SETREDRAW, FALSE, 0);
    SendMessageW(list, LB_RESETCONTENT, 0, 0);
    SendMessageW(list, LB_ADDSTRING, 0,
                 reinterpret_cast<LPARAM>(LangStr(IDS_QUICKCONNECT).c_str()));
    TmpFTPServerList.AddNamesToListbox(list);
    int count = (int)SendMessage(list, LB_GETCOUNT, 0, 0);
    if (focus >= count)
        focus = count - 1;
    if (focusLast)
        focus = count - 1;
    SendMessage(list, LB_SETTOPINDEX, topIndex, 0);
    SendMessage(list, LB_SETCURSEL, focus, 0);
    SendMessage(list, WM_SETREDRAW, TRUE, 0);
}

void CConnectDlg::MoveItem(HWND list, int fromIndex, int toIndex, int topIndex)
{
    if (fromIndex > 0 && toIndex > 0 && fromIndex != toIndex &&                   // nobody is allowed to move the quick-connect entry
        fromIndex <= TmpFTPServerList.Count && toIndex <= TmpFTPServerList.Count) // movement stays within the array
    {
        fromIndex--;
        toIndex--;
        // swap the data in the array
        CFTPServer* s = TmpFTPServerList[fromIndex];
        TmpFTPServerList.Detach(fromIndex);
        if (TmpFTPServerList.IsGood())
        {
            TmpFTPServerList.Insert(toIndex, s);
            if (TmpFTPServerList.IsGood())
            {
                // swap the data in the listbox
                SendMessage(list, WM_SETREDRAW, FALSE, 0);
                if (topIndex == -1)
                    topIndex = (int)SendMessage(list, LB_GETTOPINDEX, 0, 0);
                SendMessage(list, LB_DELETESTRING, fromIndex + 1, 0);
                SendMessageW(list, LB_INSERTSTRING, toIndex + 1, (LPARAM)s->ItemName.c_str());
                SendMessage(list, LB_SETTOPINDEX, topIndex, 0);
                SendMessage(list, LB_SETCURSEL, toIndex + 1, 0);
                SendMessage(list, WM_SETREDRAW, TRUE, 0);
            }
            else
            {
                SendMessage(list, LB_DELETESTRING, fromIndex + 1, 0);
                TmpFTPServerList.ResetState();
                delete s; // left outside the array, delete it
            }
        }
        else
            TmpFTPServerList.ResetState();
    }
}

BOOL CConnectDlg::GetCurSelServer(CFTPServer** server, int* index)
{
    CFTPServer* s;
    int i = (int)SendMessage(GetDlgItem(HWindow, IDL_BOOKMARKS), LB_GETCURSEL, 0, 0);
    if (i == 0)
        s = &Config.QuickConnectServer;
    else
    {
        if (i >= 0 && i - 1 < TmpFTPServerList.Count)
            s = TmpFTPServerList.At(i - 1);
        else
        {
            TRACE_E("Unexpected situation in CConnectDlg::GetCurSelServer()!");
            return FALSE; // unexpected situation
        }
    }
    *server = s;
    if (index != NULL)
        *index = i;
    return TRUE;
}

UINT DragListboxMsg = 0; // message ID corresponding to the DRAGLISTMSGSTRING (drag&drop listbox)

INT_PTR
CConnectDlg::DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    SLOW_CALL_STACK_MESSAGE4("CConnectDlg::DialogProc(0x%X, 0x%IX, 0x%IX)", uMsg, wParam, lParam);
    switch (uMsg)
    {
    case WM_INITDIALOG:
    {
        SalamanderGeneral->InstallWordBreakProc(GetDlgItem(HWindow, IDE_HOSTADDRESS));
        SalamanderGeneral->InstallWordBreakProc(GetDlgItem(HWindow, IDE_INITIALPATH));
        if (AddBookmarkMode != 0)
        {
            SetWindowTextW(HWindow, LangStr(IDS_ORGANIZEBOOKMARKS).c_str());
            HWND ok = GetDlgItem(HWindow, IDOK);
            HWND close = GetDlgItem(HWindow, IDB_CLOSE);
            SendMessage(HWindow, DM_SETDEFID, IDB_CLOSE, 0);
            SendMessage(ok, BM_SETSTYLE, BS_PUSHBUTTON, TRUE);
            SendMessage(close, BM_SETSTYLE, BS_DEFPUSHBUTTON, TRUE);
            ShowWindow(ok, SW_HIDE);
        }

        // Unlock button: besides the Unlock command it will also have Clear to delete the password
        SalamanderGUI->AttachButton(HWindow, IDB_PASSWORD_CHANGE, BTF_DROPDOWN);

        // we want to receive WM_APP_SHOWPASSWORD from the password edit line on Ctrl+right click
        CPasswordEditLine* passwordEL = new CPasswordEditLine(HWindow, IDE_PASSWORD);

        // reposition the password edit lines
        AlignPasswordControls();

        CGUIHyperLinkAbstract* hint = SalamanderGUI->AttachHyperLink(HWindow, IDC_SAVEPASSWORD_HINT, STF_HYPERLINK_COLOR | STF_UNDERLINE);
        hint->SetActionPostCommand(IDC_SAVEPASSWORD_HINT);

        // if the user has not set a master password in Salamander, storing passwords is not safe
        CSalamanderPasswordManagerAbstract* passwordManager = SalamanderGeneral->GetSalamanderPasswordManager();
        // if the user uses the password manager, change the "it is not secure" message
        if (passwordManager->IsUsingMasterPassword())
            SetDlgItemTextW(HWindow, IDC_SAVEPASSWORD_HINT,
                            LangStr(IDS_SAVEPASSWORD_PROTECTED).c_str());

        // attach to the listbox (because Alt+arrow keys do not reach WM_VKEYTOITEM)
        CBookmarksListbox* list = new CBookmarksListbox(this, IDL_BOOKMARKS);
        if (list != NULL)
        {
            if (list->HWindow == NULL)
                delete list; // if the control is missing, release the object (otherwise it releases itself)
            else
            {
                MakeDragList(list->HWindow);
                if (DragListboxMsg == 0)
                    DragListboxMsg = RegisterWindowMessage(DRAGLISTMSGSTRING);
            }
        }

        INT_PTR ret = CCenteredDialog::DialogProc(uMsg, wParam, lParam);
        SelChanged();
        EnableControls();
        return ret;
    }

    case WM_APP_SHOWPASSWORD:
    {
        MSGBOXEX_PARAMS params;
        const std::wstring caption = LangStr(IDS_FTPPLUGINTITLE);
        const std::wstring text = LangStr(IDS_SHOWPASSWORD_CONFIRMATION);
        memset(&params, 0, sizeof(params));
        params.HParent = HWindow;
        params.Flags = MSGBOXEX_YESNO | MSGBOXEX_ESCAPEENABLED |
                       MSGBOXEX_ICONQUESTION | MSGBOXEX_SILENT;
        params.Caption = caption.c_str();
        params.Text = text.c_str();
        if (SalamanderGeneral->SalMessageBoxEx(&params) == IDYES)
        {
            CSalamanderPasswordManagerAbstract* passwordManager = SalamanderGeneral->GetSalamanderPasswordManager();
            // ask for the master password even if we already know it
            if (!passwordManager->IsUsingMasterPassword() || passwordManager->AskForMasterPassword(HWindow))
            {
                // Pull the complete password directly from the native edit control.
                const int passwordLength = GetWindowTextLengthW((HWND)wParam);
                std::vector<wchar_t> passwordStorage(static_cast<size_t>(passwordLength) + 1, L'\0');
                GetWindowTextW((HWND)wParam, passwordStorage.data(),
                               static_cast<int>(passwordStorage.size()));
                // The formatted message embeds the plaintext password, so it is a secret in its own
                // right and must be wiped, not merely destroyed - pre-unicode zeroed both the
                // password and the formatted buffer (`memset(buff, 0, 1000)`). Widening kept the
                // wipe on the edit-control copy and lost it on the message, which is the half that
                // survives longest: a freed std::wstring allocation is handed to the next allocation
                // as-is, and it can reach a crash dump or the page file.
                std::wstring revealText = SPLFormatStringOwned(
                    LangStr(IDS_PASSWORDIS).c_str(), passwordStorage.data());
                CFTPSecureWideStringGuard revealTextGuard(revealText);
                params.Flags = MSGBOXEX_YESNO | MSGBOXEX_ESCAPEENABLED | MSGBOXEX_DEFBUTTON2 |
                               MSGBOXEX_ICONINFORMATION | MSGBOXEX_SILENT;
                params.Text = revealText.c_str();
                if (SalamanderGeneral->SalMessageBoxEx(&params) == IDYES)
                    SalamanderGeneral->CopyTextToClipboard(passwordStorage.data(), -1, FALSE, NULL);
                SecureZeroMemory(passwordStorage.data(), passwordStorage.size() * sizeof(wchar_t));
            }
        }
        return 0;
    }

    case WM_USER_BUTTONDROPDOWN:
    {
        if (LOWORD(wParam) == IDB_PASSWORD_CHANGE) // drop-down menu on the Unlock button
        {
            HMENU main = LoadMenu(HLanguage, MAKEINTRESOURCE(IDM_UNLOCKPASSWORD));
            if (main != NULL)
            {
                HMENU subMenu = GetSubMenu(main, 0);
                if (subMenu != NULL)
                {
                    CGUIMenuPopupAbstract* salMenu = SalamanderGUI->CreateMenuPopup();
                    if (salMenu != NULL)
                    {
                        salMenu->SetTemplateMenu(subMenu);

                        RECT r;
                        GetWindowRect(GetDlgItem(HWindow, (DWORD)wParam), &r);
                        BOOL selectMenuItem = LOWORD(lParam);
                        DWORD flags = MENU_TRACK_RETURNCMD;
                        if (selectMenuItem)
                        {
                            salMenu->SetSelectedItemIndex(0);
                            flags |= MENU_TRACK_SELECT;
                        }
                        DWORD cmd = salMenu->Track(flags, r.left, r.bottom, HWindow, &r);
                        if (cmd != 0)
                            PostMessage(HWindow, WM_COMMAND, cmd, 0);
                        SalamanderGUI->DestroyMenuPopup(salMenu);
                    }
                }
                DestroyMenu(main);
            }
        }
        return TRUE;
    }

    case WM_COMMAND:
    {
        switch (LOWORD(wParam))
        {
        case IDOK: // to deliver EN_KILLFOCUS even when Enter (default button = Connect) is pressed
        case IDB_CLOSE:
        {
            HWND button = GetDlgItem(HWindow, LOWORD(wParam));
            if (CanChangeFocus && button != NULL && GetFocus() != button)
            {
                SendMessage(HWindow, WM_NEXTDLGCTL, (WPARAM)button, TRUE);
                PostMessage(HWindow, uMsg, wParam, lParam); // postpone this command
                CanChangeFocus = FALSE;                     // prevents an infinite loop
                return TRUE;                                // do nothing; wait for the kill focus in the edit boxes
            }
            CanChangeFocus = TRUE;

            if (LOWORD(wParam) == IDB_CLOSE)
            {
                if (!ValidateData() ||
                    !TransferData(ttDataFromWindow))
                    return TRUE;
                if (Modal)
                    EndDialog(HWindow, wParam);
                else
                    DestroyWindow(HWindow);
                return TRUE;
            }
            else // IDOK
            {
                CFTPServer* s;
                int i;
                if (!GetCurSelServer(&s, &i))
                    break; // unexpected situation

                if (s->Address.empty())
                {
                    ShowMessage(HWindow, LangStr(IDS_HOSTMAYNOTBEEMPTY).c_str(),
                                                     LangStr(IDS_FTPERRORTITLE).c_str(), MB_OK | MB_ICONEXCLAMATION);

                    HWND ctrl = GetDlgItem(HWindow, IDE_HOSTADDRESS);
                    HWND wnd = GetFocus();
                    while (wnd != NULL && wnd != ctrl)
                        wnd = GetParent(wnd);
                    if (wnd == NULL) // set focus only if the control is not an ancestor of GetFocus
                    {                // for example, the edit line in the combo box
                        SendMessage(HWindow, WM_NEXTDLGCTL, (WPARAM)ctrl, TRUE);
                    }
                    return TRUE;
                }

                // verify that active transfer mode is not requested with an HTTP 1.1 proxy
                if (s->UsePassiveMode == 0 || s->UsePassiveMode == 2 && Config.PassiveMode == 0)
                {
                    int proxyServerUID = s->ProxyServerUID;
                    if (proxyServerUID == -2)
                        proxyServerUID = Config.DefaultProxySrvUID;
                    if (TmpFTPProxyServerList.GetProxyType(proxyServerUID) == fpstHTTP1_1)
                    {
                        ShowMessage(HWindow, LangStr(IDS_HTTPNEEDPASSIVETRMODE2).c_str(),
                                                         LangStr(IDS_FTPERRORTITLE).c_str(), MB_OK | MB_ICONEXCLAMATION);
                        return TRUE;
                    }
                }

                // if the connection needs passwords, we must be able to decrypt them
                // test the bookmark
                if (!s->EnsurePasswordCanBeDecrypted(HWindow))
                    return TRUE; // failed to enter the master password or it probably failed to decrypt the password

                // WARNING: s->EnsurePasswordCanBeDecrypted() might have cleared the password in 's' and its stored copy, which means
                //        returning to the dialog (return TRUE) requires performing a "refresh"

                // test the proxy server
                int proxyServerUID = s->ProxyServerUID;
                if (proxyServerUID == -2)
                    proxyServerUID = Config.DefaultProxySrvUID;
                if (!TmpFTPProxyServerList.EnsurePasswordCanBeDecrypted(HWindow, proxyServerUID))
                {
                    // "refresh", reason a few lines above (s->EnsurePasswordCanBeDecrypted)
                    SelChanged();
                    EnableControls();

                    return TRUE; // failed to enter the master password or they could not decrypt the password
                }
            }
            break;
        }

        case IDL_BOOKMARKS:
        {
            if (HIWORD(wParam) == LBN_SELCHANGE)
            {
                SelChanged();
                EnableControls();
            }
            break;
        }

        case IDC_SAVEPASSWORD_HINT:
        {
            // open Salamander help with information about the password manager
            SalamanderGeneral->OpenHtmlHelpForSalamander(HWindow, HHCDisplayContext, HTMLHELP_SALID_PWDMANAGER, FALSE);
            break;
        }

        case CM_CLEARPASSWORD:
        {
            CFTPServer* s;
            int i;
            if (!GetCurSelServer(&s, &i))
                break; // unexpected situation

            int ret = ShowMessage(HWindow, LangStr(IDS_CLEARPASSWORD_CONFIRMATION).c_str(),
                                                       LangStr(IDS_FTPPLUGINTITLE).c_str(), MB_YESNO | MSGBOXEX_ESCAPEENABLED | /*MB_DEFBUTTON2 | */ MSGBOXEX_ICONQUESTION | MSGBOXEX_SILENT);
            if (ret == IDYES)
            {
                // user requested to delete the password
                UpdateEncryptedPassword(&s->EncryptedPassword, &s->EncryptedPasswordSize, NULL, 0);
                // clear the save password checkbox
                s->SavePassword = FALSE;

                ShowHidePasswordControls(FALSE, TRUE);
                SelChanged();
            }
            break;
        }

        case CM_UNLOCKPASSWORD:
        case IDB_PASSWORD_CHANGE:
        {
            CSalamanderPasswordManagerAbstract* passwordManager = SalamanderGeneral->GetSalamanderPasswordManager();
            // either the master password is not used, it is already set, or the user enters it now
            if (!passwordManager->IsUsingMasterPassword() || passwordManager->IsMasterPasswordSet() || passwordManager->AskForMasterPassword(HWindow))
            {
                CFTPServer* s;
                int i;
                if (!GetCurSelServer(&s, &i))
                    break; // unexpected situation

                // if the master password is used, verify that this password can be decrypted with it
                if (!passwordManager->IsUsingMasterPassword() ||
                    s->EncryptedPassword != NULL &&
                        !FTPDecryptPasswordW(passwordManager, s->EncryptedPassword,
                                             s->EncryptedPasswordSize, NULL))
                {
                    int ret = ShowMessage(HWindow, LangStr(IDS_CANNOT_DECRYPT_PASSWORD_DELETE).c_str(),
                                                               LangStr(IDS_FTPERRORTITLE).c_str(), MB_YESNO | MSGBOXEX_ESCAPEENABLED | MB_DEFBUTTON2 | MB_ICONEXCLAMATION);
                    if (ret == IDNO)
                        break;
                    // user requested to delete the password
                    UpdateEncryptedPassword(&s->EncryptedPassword, &s->EncryptedPasswordSize, NULL, 0);
                    // clear the save password checkbox
                    s->SavePassword = FALSE;
                }
                ShowHidePasswordControls(FALSE, TRUE);
                SelChanged();
            }
            break;
        }

        case IDB_ADVACED:        // advanced options dialog
        case IDB_NEWBOOKMARK:    // new bookmark dialog
        case CM_COPYSRVTO:       // "copy bookmark to" dialog
        case IDB_RENAMEBOOKMARK: // rename bookmark dialog
        case IDB_REMOVEBOOKMARK: // remove bookmark dialog
        case IDE_HOSTADDRESS:    // change in text/checkbox -> change in data
        case IDE_INITIALPATH:
        case IDE_USERNAME:
        case IDE_PASSWORD:
        case IDC_SAVEPASSWORD:
        case IDC_ANONYMOUSLOGIN:
        {
            CFTPServer* s;
            int i;
            if (!GetCurSelServer(&s, &i))
                break; // unexpected situation

            CTransferInfo ti(HWindow, ttDataFromWindow);
            switch (LOWORD(wParam))
            {
            case IDB_ADVACED:
            {
                CSalamanderPasswordManagerAbstract* passwordManager = SalamanderGeneral->GetSalamanderPasswordManager();
                BOOL oldMPSet = passwordManager->IsUsingMasterPassword() && passwordManager->IsMasterPasswordSet();
                if (CConnectAdvancedDlg(HWindow, s, &TmpFTPProxyServerList).Execute() == IDOK)
                {
                    TmpFTPServerList.CheckProxyServersUID(TmpFTPProxyServerList);
                    SelChanged();
                    EnableControls();
                }
                else
                {
                    if (!oldMPSet && passwordManager->IsUsingMasterPassword() && passwordManager->IsMasterPasswordSet())
                    { // although Cancel, the user entered the master password, so a "refresh" is needed
                        SelChanged();
                        EnableControls();
                    }
                }
                return TRUE; // do not process further
            }

            case IDB_NEWBOOKMARK:
            case CM_COPYSRVTO:
            case IDB_RENAMEBOOKMARK:
            {
                if (LOWORD(wParam) == IDB_RENAMEBOOKMARK && i <= 0)
                    return TRUE; // quick connect cannot be renamed

                std::wstring name = s->ItemName;
                CRenameDlg dlg(HWindow, name,
                               LOWORD(wParam) == IDB_NEWBOOKMARK || LOWORD(wParam) == CM_COPYSRVTO,
                               FALSE);
                if (LOWORD(wParam) == CM_COPYSRVTO)
                    dlg.CopyDataFromFocusedServer = TRUE;
                if (dlg.Execute() == IDOK)
                {
                    if (LOWORD(wParam) == IDB_NEWBOOKMARK || LOWORD(wParam) == CM_COPYSRVTO) // new/copy to
                    {
                        if (dlg.CopyDataFromFocusedServer)
                        {
                            TmpFTPServerList.AddServer(name.c_str(),
                                                       s->Address.c_str(),
                                                       s->InitialPath.c_str(),
                                                       s->AnonymousConnection,
                                                       s->UserName.c_str(),
                                                       s->EncryptedPassword,
                                                       s->EncryptedPasswordSize,
                                                       s->SavePassword,
                                                       s->ProxyServerUID,
                                                       s->TargetPanelPath.c_str(),
                                                       s->ServerType.empty() ? NULL : s->ServerType.c_str(),
                                                       s->TransferMode,
                                                       s->Port,
                                                       s->UsePassiveMode,
                                                       s->KeepConnectionAlive,
                                                       s->UseMaxConcurrentConnections,
                                                       s->MaxConcurrentConnections,
                                                       s->UseServerSpeedLimit,
                                                       s->ServerSpeedLimit,
                                                       s->UseListingsCache,
                                                       s->InitFTPCommands.c_str(),
                                                       s->ListCommand.c_str(),
                                                       s->KeepAliveSendEvery,
                                                       s->KeepAliveStopAfter,
                                                       s->KeepAliveCommand,
                                                       s->EncryptControlConnection,
                                                       s->EncryptDataConnection,
                                                       s->CompressData);
                        }
                        else
                            TmpFTPServerList.AddServer(name.c_str());
                    }
                    else // rename
                    {
                        s->ItemName = name;
                    }
                    RefreshList(LOWORD(wParam) == IDB_NEWBOOKMARK || LOWORD(wParam) == CM_COPYSRVTO);
                    SelChanged();
                    EnableControls();
                }
                return TRUE; // do not process further
            }

            case IDB_REMOVEBOOKMARK:
            {
                std::wstring prompt;
                try
                {
                    prompt = SPLFormatStringOwned(LangStr(IDS_REMOVECONFIRM).c_str(),
                                                  s->ItemName.c_str());
                }
                catch (...)
                {
                    return TRUE;
                }
                if (i > 0) // quick connect cannot be deleted (should always be false)
                {
                    if (ShowMessage(HWindow, prompt.c_str(), LangStr(IDS_FTPPLUGINTITLE).c_str(),
                                    MB_YESNO | MB_ICONQUESTION) == IDYES)
                    {
                        TmpFTPServerList.Delete(i - 1);
                        RefreshList();
                        SelChanged();
                        EnableControls();
                    }
                }

                return TRUE; // do not process further
            }

            case IDE_HOSTADDRESS:
            {
                if (HIWORD(wParam) == CBN_KILLFOCUS)
                {
                    ti.EditLine(IDE_HOSTADDRESS, LastRawHostAddress);
                    std::wstring parsedAddress;
                    if (ti.IsGood() && FtpStoreWideText(LastRawHostAddress, parsedAddress))
                    {
                        try
                        {
                        size_t start = 0;
                        while (start < parsedAddress.size() && parsedAddress[start] <= L' ')
                            start++; // skip whitespace

                        int isFTPS = 0; // 0 = nothing, 1 = enable, 2 = disable
                        const size_t remaining = parsedAddress.size() - start;
                        if (remaining > AssignedFSName.size() &&
                            _wcsnicmp(parsedAddress.c_str() + start, AssignedFSName.c_str(), AssignedFSName.size()) == 0 &&
                            parsedAddress[start + AssignedFSName.size()] == L':')
                        {
                            start += AssignedFSName.size() + 1;
                            isFTPS = 2;
                        }
                        else if (remaining > AssignedFSNameFTPS.size() &&
                                 _wcsnicmp(parsedAddress.c_str() + start, AssignedFSNameFTPS.c_str(), AssignedFSNameFTPS.size()) == 0 &&
                                 parsedAddress[start + AssignedFSNameFTPS.size()] == L':')
                        {
                            start += AssignedFSNameFTPS.size() + 1;
                            isFTPS = 1;
                        }
                        parsedAddress.erase(0, start);

                        if (Config.ConvertHexEscSeq &&
                            !FTPConvertHexEscapeSequencesW(parsedAddress))
                            ti.ErrorOn(IDE_HOSTADDRESS);

                        if (ti.IsGood())
                        {
                            if (isFTPS != 0)
                            {
                                s->EncryptControlConnection = isFTPS == 1 ? 1 : 0;
                                s->EncryptDataConnection = isFTPS == 1 ? 1 : 0;
                            }

                            wchar_t *user, *plainPassword, *host, *port, *path;
                            wchar_t firstCharOfPath = L'/';
                            FTPSplitPathW(parsedAddress.data(), &user, &plainPassword, &host, &port,
                                          &path, &firstCharOfPath, 0);
                            if (user != NULL && *user != 0)
                            {
                                if (wcscmp(L"anonymous", user) == 0 &&
                                    (plainPassword == NULL || *plainPassword == 0))
                                    s->AnonymousConnection = TRUE;
                                else
                                {
                                    s->AnonymousConnection = FALSE;
                                    if (!FtpStoreWideText(user, s->UserName))
                                        ti.ErrorOn(IDE_HOSTADDRESS);
                                }
                            }

                            if (ti.IsGood() && plainPassword != NULL && *plainPassword != 0)
                            {
                                CSalamanderPasswordManagerAbstract* passwordManager = SalamanderGeneral->GetSalamanderPasswordManager();
                                if (s->SavePassword && passwordManager->IsUsingMasterPassword() &&
                                    !passwordManager->IsMasterPasswordSet() &&
                                    !passwordManager->AskForMasterPassword(HWindow))
                                {
                                    s->SavePassword = FALSE;
                                    CheckDlgButton(HWindow, IDC_SAVEPASSWORD, BST_UNCHECKED);
                                }
                                s->AnonymousConnection = FALSE;

                                BYTE* encryptedPassword = NULL;
                                int encryptedPasswordSize = 0;
                                const BOOL encrypt = s->SavePassword &&
                                                     passwordManager->IsUsingMasterPassword() &&
                                                     passwordManager->IsMasterPasswordSet();
                                if (!FTPEncryptPasswordW(passwordManager, plainPassword,
                                                         &encryptedPassword,
                                                         &encryptedPasswordSize, encrypt) ||
                                    !UpdateEncryptedPassword(&s->EncryptedPassword,
                                                             &s->EncryptedPasswordSize,
                                                             encryptedPassword,
                                                             encryptedPasswordSize))
                                    ti.ErrorOn(IDE_HOSTADDRESS);
                                if (encryptedPassword != NULL)
                                {
                                    SecureZeroMemory(encryptedPassword, encryptedPasswordSize);
                                    SalamanderGeneral->Free(encryptedPassword);
                                }
                            }

                            if (ti.IsGood() && !FtpStoreWideText(host != NULL ? host : L"", s->Address))
                                ti.ErrorOn(IDE_HOSTADDRESS);

                            if (ti.IsGood() && port != NULL && *port != 0)
                            {
                                unsigned value = 0;
                                const wchar_t* digit = port;
                                while (*digit >= L'0' && *digit <= L'9' && value <= 65535)
                                {
                                    value = value * 10 + static_cast<unsigned>(*digit - L'0');
                                    digit++;
                                }
                                if (*digit == 0 && value >= 1 && value <= 65535)
                                    s->Port = static_cast<int>(value);
                            }

                            if (ti.IsGood() && path != NULL)
                            {
                                try
                                {
                                    std::wstring initialPath;
                                    const CFTPServerPathType type = GetFTPServerPathTypeW(NULL, NULL, path);
                                    if (type == ftpsptOpenVMS || type == ftpsptMVS ||
                                        type == ftpsptIBMz_VM || type == ftpsptOS2)
                                        initialPath.assign(path);
                                    else
                                    {
                                        initialPath.push_back(firstCharOfPath);
                                        initialPath.append(path);
                                    }
                                    s->InitialPath.swap(initialPath);
                                }
                                catch (...)
                                {
                                    ti.ErrorOn(IDE_HOSTADDRESS);
                                }
                            }
                        }

                        if (ti.IsGood())
                        {
                            SelChanged();
                            EnableControls();
                        }
                        }
                        catch (...)
                        {
                            ti.ErrorOn(IDE_HOSTADDRESS);
                        }
                        FTPSecureWipe(parsedAddress); // may contain an inline password
                    }
                    else
                        ti.ErrorOn(IDE_HOSTADDRESS);
                }
                break;
            }

            case IDE_INITIALPATH:
            {
                if (HIWORD(wParam) == CBN_KILLFOCUS)
                {
                    std::wstring initialPath;
                    ti.EditLine(IDE_INITIALPATH, initialPath);
                    if (!ti.IsGood())
                        break;
                    const BOOL changed = initialPath != s->InitialPath;
                    s->InitialPath.swap(initialPath);
                    if (changed)
                    {
                        SelChanged();
                        EnableControls();
                    }
                }
                break;
            }

            case IDC_ANONYMOUSLOGIN:
            {
                if (HIWORD(wParam) == BN_CLICKED)
                {
                    ti.CheckBox(IDC_ANONYMOUSLOGIN, s->AnonymousConnection);
                    EnableControls();
                    SelChanged();
                }
                break;
            }

            case IDE_USERNAME:
            {
                if (HIWORD(wParam) == EN_KILLFOCUS && !s->AnonymousConnection)
                {
                    std::wstring userName;
                    ti.EditLine(IDE_USERNAME, userName);
                    if (ti.IsGood())
                        s->UserName.swap(userName);
                }
                break;
            }

            case IDE_PASSWORD:
            {
                CSalamanderPasswordManagerAbstract* passwordManager = SalamanderGeneral->GetSalamanderPasswordManager();
                if (HIWORD(wParam) == EN_KILLFOCUS && !s->AnonymousConnection &&
                    (!s->SavePassword || !passwordManager->IsUsingMasterPassword() || passwordManager->IsMasterPasswordSet())) // just to be safe: exclude the case when the edit box is disabled (editing via the Unlock button)
                {
                    std::wstring plainPassword;
                    ti.EditLine(IDE_PASSWORD, plainPassword);

                    if (!plainPassword.empty())
                    {
                        BYTE* encryptedPassword = NULL; // may be just scrambled
                        int encryptedPasswordSize = 0;
                        // only stored passwords make sense to encrypt
                        BOOL encrypt = s->SavePassword && passwordManager->IsUsingMasterPassword() && passwordManager->IsMasterPasswordSet();
                        if (FTPEncryptPasswordW(passwordManager, plainPassword.c_str(),
                                                &encryptedPassword,
                                                &encryptedPasswordSize, encrypt))
                        {
                            if (!UpdateEncryptedPassword(&s->EncryptedPassword, &s->EncryptedPasswordSize,
                                                         encryptedPassword, encryptedPasswordSize))
                                ti.ErrorOn(IDE_PASSWORD);
                            // free the buffer allocated in EncryptPassword()
                            memset(encryptedPassword, 0, encryptedPasswordSize);
                            SalamanderGeneral->Free(encryptedPassword);
                        }
                    }
                    else
                        UpdateEncryptedPassword(&s->EncryptedPassword, &s->EncryptedPasswordSize, NULL, 0);
                    FTPSecureWipe(plainPassword);
                }
                break;
            }

            case IDC_SAVEPASSWORD:
            {
                if (HIWORD(wParam) == BN_CLICKED && !s->AnonymousConnection)
                {
                    ti.CheckBox(IDC_SAVEPASSWORD, s->SavePassword);
                    // if the user checked "Save password" and the password edit line is currently visible, the user uses the master password and the password is not entered
                    BOOL unlockVisible = IsWindowVisible(GetDlgItem(HWindow, IDB_PASSWORD_CHANGE));
                    CSalamanderPasswordManagerAbstract* passwordManager = SalamanderGeneral->GetSalamanderPasswordManager();
                    if (!unlockVisible && s->SavePassword && passwordManager->IsUsingMasterPassword() && !passwordManager->IsMasterPasswordSet())
                    {
                        // ask for the master password
                        if (!passwordManager->AskForMasterPassword(HWindow))
                        {
                            // if the user did not enter a valid master password, revert the checkbox to the state before the change
                            s->SavePassword = FALSE; // reflect the check box change in the data
                            CheckDlgButton(HWindow, IDC_SAVEPASSWORD, BST_UNCHECKED);
                        }
                    }
                    else
                    {
                        if (unlockVisible && !s->SavePassword && s->EncryptedPassword == NULL)
                            SelChanged(); // empty password without Save Password enabled -> hide Unlock and show an empty password edit box
                    }
                }
                break;
            }
            }
            break;
        }
        }
        break;
    }
    }

    if (uMsg == DragListboxMsg && wParam == IDL_BOOKMARKS)
    {
        DRAGLISTINFO* pdli = (DRAGLISTINFO*)lParam;
        switch (pdli->uNotification)
        {
        case DL_BEGINDRAG:
        {
            DragIndex = LBItemFromPt(pdli->hWnd, pdli->ptCursor, TRUE);
            if (DragIndex > 0 && DragIndex <= TmpFTPServerList.Count)
            {
                SetWindowLongPtr(HWindow, DWLP_MSGRESULT, TRUE);
            }
            else
            {
                DragIndex = -1;
                SetWindowLongPtr(HWindow, DWLP_MSGRESULT, FALSE);
            }
            break;
        }

        case DL_DRAGGING:
        {
            if (!ExtraDragDropItemAdded)
            {
                SendMessageW(pdli->hWnd, LB_ADDSTRING, 0, (LPARAM)L""); // add an empty string at the end (because of the insertion marker after the items)
                ExtraDragDropItemAdded = TRUE;
            }

            int i = LBItemFromPt(pdli->hWnd, pdli->ptCursor, TRUE);
            DrawInsert(HWindow, pdli->hWnd, i);
            if (i > 0 && i <= TmpFTPServerList.Count + 1 && DragIndex != i &&
                DragIndex + 1 != i)
            {
                if (DragCursor != NULL)
                {
                    SetCursor(DragCursor);
                    SetWindowLongPtr(HWindow, DWLP_MSGRESULT, 0);
                }
                else
                    SetWindowLongPtr(HWindow, DWLP_MSGRESULT, DL_MOVECURSOR);
            }
            else
                SetWindowLongPtr(HWindow, DWLP_MSGRESULT, DL_STOPCURSOR);
            break;
        }

        case DL_DROPPED:
        {
            int topIndex = (int)SendMessage(pdli->hWnd, LB_GETTOPINDEX, 0, 0);
            DrawInsert(HWindow, pdli->hWnd, -1);
            int index = LBItemFromPt(pdli->hWnd, pdli->ptCursor, TRUE);

            // remove the empty string from the end (it was there for the insertion marker after the items)
            int count = (int)SendMessage(pdli->hWnd, LB_GETCOUNT, 0, 0);
            if (count != LB_ERR && count > 0 && ExtraDragDropItemAdded)
            {
                SendMessage(pdli->hWnd, LB_DELETESTRING, count - 1, 0);
                ExtraDragDropItemAdded = FALSE;
            }

            // move the item
            if (DragIndex != -1 && DragIndex != index && DragIndex + 1 != index)
            {
                if (index > DragIndex)
                    index--;
                MoveItem(pdli->hWnd, DragIndex, index, topIndex); // unrealistic drop is handled inside MoveItem
            }
            break;
        }

        case DL_CANCELDRAG:
        {
            DrawInsert(HWindow, pdli->hWnd, -1);
            DragIndex = -1;

            // remove the empty string from the end (it was there for the insertion marker after the items)
            int count = (int)SendMessage(pdli->hWnd, LB_GETCOUNT, 0, 0);
            if (count != LB_ERR && count > 0 && ExtraDragDropItemAdded)
            {
                SendMessage(pdli->hWnd, LB_DELETESTRING, count - 1, 0);
                ExtraDragDropItemAdded = FALSE;
            }
            break;
        }
        }
        return TRUE;
    }

    return CCenteredDialog::DialogProc(uMsg, wParam, lParam);
}

//
// ****************************************************************************
// CPasswordEditLine
//

CPasswordEditLine::CPasswordEditLine(HWND hDlg, int ctrlID)
    : CWindow(hDlg, ctrlID)
{
}

LRESULT CPasswordEditLine::WindowProc(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
    case WM_RBUTTONDOWN:
    {
        BOOL controlPressed = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
        BOOL altPressed = (GetKeyState(VK_MENU) & 0x8000) != 0;
        BOOL shiftPressed = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
        if (IsWindowEnabled(HWindow) && controlPressed && !altPressed && !shiftPressed)
        {
            // verify that the edit line contains something
            if (GetWindowTextLengthW(HWindow) > 0)
            {
                PostMessage(GetParent(HWindow), WM_APP_SHOWPASSWORD, (WPARAM)HWindow, lParam);
                return 0;
            }
        }
        break;
    }
    }
    return CWindow::WindowProc(uMsg, wParam, lParam);
}
