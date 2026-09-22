// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"
#include "plugin_window_text.h"

// Wide. This existed to widen a narrow LoadStr at the last moment; LangStr
// means there is nothing left to widen.
static int ShowMessage(HWND parent, const wchar_t* text, const wchar_t* caption, UINT type)
{
    return SalamanderGeneral->SalMessageBox(parent, text, caption, type);
}

//****************************************************************************

void HistoryComboBox(HWND hWindow, CTransferInfo& ti, int ctrlID, std::wstring& text,
                     int historySize, std::wstring history[], BOOL secretValue)
{
    CALL_STACK_MESSAGE4("HistoryComboBox(, , %d, wide-dynamic, %d, %d)",
                        ctrlID, historySize, secretValue);
    HWND combo;
    if (!ti.GetControl(combo, ctrlID))
        return;

    try
    {
        if (ti.Type == ttDataFromWindow)
        {
            std::wstring staged = SPLGetWindowTextOwned(combo);
            text.swap(staged);

            if (ti.IsGood() && !secretValue && !text.empty())
            {
                int existing = -1;
                for (int i = 0; i < historySize && !history[i].empty(); i++)
                {
                    if (history[i] == text)
                    {
                        existing = i;
                        break;
                    }
                }
                if (existing >= 0)
                {
                    for (int i = existing; i > 0; i--)
                        history[i].swap(history[i - 1]);
                }
                else
                {
                    std::wstring newText;
                    if (!FtpStoreWideText(text, newText))
                    {
                        ti.ErrorOn(ctrlID);
                        return;
                    }
                    for (int i = historySize - 1; i > 0; i--)
                        history[i].swap(history[i - 1]);
                    history[0].swap(newText);
                }
            }
        }

        SendMessageW(combo, CB_RESETCONTENT, 0, 0);
        HWND edit = GetWindow(combo, GW_CHILD);
        if (edit != NULL)
            SendMessageW(edit, EM_LIMITTEXT, 0, 0);
        SetWindowTextW(combo, text.c_str());
        for (int i = 0; i < historySize && !history[i].empty(); i++)
            SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(history[i].c_str()));
    }
    catch (...)
    {
        ti.ErrorOn(ctrlID);
    }
}

//
// ****************************************************************************
// CSendFTPCommandDlg
//

CSendFTPCommandDlg::CSendFTPCommandDlg(HWND parent)
    : CCenteredDialog(HLanguage, IDD_SENDCOMMANDDLG, IDD_SENDCOMMANDDLG, parent)
{
    ChangePathInPanel = TRUE;
    RefreshWorkingPath = TRUE;
    if (!Config.SendSecretCommand && !Config.CommandHistory[0].empty())
        FtpStoreWideText(Config.CommandHistory[0], Command);
}

CSendFTPCommandDlg::~CSendFTPCommandDlg()
{
    FTPSecureWipe(Command);
}

void CSendFTPCommandDlg::Validate(CTransferInfo& ti)
{
    std::wstring cmd;
    BOOL secret = IsDlgButtonChecked(HWindow, IDC_SECRETCOMMAND) == BST_CHECKED;
    if (secret)
        ti.EditLine(IDE_FTPCOMMAND_PASSWD, cmd);
    else
        ti.EditLine(IDC_FTPCOMMAND, cmd);
    if (ti.IsGood() && cmd.empty())
    {
        ShowMessage(HWindow, LangStr(IDS_CMDMAYNOTBEEMPTY).c_str(),
                                         LangStr(IDS_FTPERRORTITLE).c_str(), MB_OK | MB_ICONEXCLAMATION);
        ti.ErrorOn(secret ? IDE_FTPCOMMAND_PASSWD : IDC_FTPCOMMAND);
    }
    FTPSecureWipe(cmd);
}

void CSendFTPCommandDlg::Transfer(CTransferInfo& ti)
{
    ti.CheckBox(IDC_SECRETCOMMAND, Config.SendSecretCommand);
    ti.CheckBox(IDC_CHANGEPATHINPANEL, ChangePathInPanel);
    ti.CheckBox(IDC_REFRESHWORKINGPATH, RefreshWorkingPath);
    if (ti.Type == ttDataToWindow)
    {
        HistoryComboBox(HWindow, ti, IDC_FTPCOMMAND, Command,
                        COMMAND_HISTORY_SIZE, Config.CommandHistory, Config.SendSecretCommand);
        ti.EditLine(IDE_FTPCOMMAND_PASSWD, Command);
    }
    else
    {
        if (!Config.SendSecretCommand)
        {
            HistoryComboBox(HWindow, ti, IDC_FTPCOMMAND, Command,
                            COMMAND_HISTORY_SIZE, Config.CommandHistory, Config.SendSecretCommand);
        }
        else
            ti.EditLine(IDE_FTPCOMMAND_PASSWD, Command);
    }
}

void CSendFTPCommandDlg::EnableControls()
{
    BOOL secret = IsDlgButtonChecked(HWindow, IDC_SECRETCOMMAND) == BST_CHECKED;
    HWND combo = GetDlgItem(HWindow, IDC_FTPCOMMAND);
    HWND edit = GetWindow(combo, GW_CHILD);
    HWND passwdEdit = GetDlgItem(HWindow, IDE_FTPCOMMAND_PASSWD);

    if (secret)
    {
        if (!IsWindowVisible(passwdEdit))
        {
            BOOL focus = GetFocus() == edit;
            ShowWindow(combo, SW_HIDE);
            ShowWindow(passwdEdit, SW_SHOW);
            if (focus)
                SetFocus(passwdEdit);
        }
    }
    else
    {
        if (!IsWindowVisible(combo))
        {
            BOOL focus = GetFocus() == passwdEdit;
            ShowWindow(passwdEdit, SW_HIDE);
            ShowWindow(combo, SW_SHOW);
            if (focus)
                SetFocus(edit);
        }
    }
    //  SendMessage(edit, EM_SETPASSWORDCHAR, secret ? '*' : 0, 0);
    //  InvalidateRect(combo, NULL, TRUE);
    //  UpdateWindow(combo);
}

INT_PTR
CSendFTPCommandDlg::DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    CALL_STACK_MESSAGE4("CSendFTPCommandDlg::DialogProc(0x%X, 0x%IX, 0x%IX)", uMsg, wParam, lParam);
    switch (uMsg)
    {
    case WM_INITDIALOG:
    {
        INT_PTR ret = CCenteredDialog::DialogProc(uMsg, wParam, lParam);
        PostMessage(HWindow, WM_APP + 1000, 0, 0);
        return ret;
    }

    case WM_APP + 1000:
    {
        EnableControls();
        return TRUE; // message processed
    }

    case WM_COMMAND:
    {
        if (HIWORD(wParam) == BN_CLICKED && LOWORD(wParam) == IDC_SECRETCOMMAND)
        {
            BOOL secret = IsDlgButtonChecked(HWindow, IDC_SECRETCOMMAND) == BST_CHECKED;
            HWND combo = GetDlgItem(HWindow, IDC_FTPCOMMAND);
            HWND edit = GetWindow(combo, GW_CHILD);
            HWND passwdEdit = GetDlgItem(HWindow, IDE_FTPCOMMAND_PASSWD);

            // we must swap the current texts between the edit and the combo
            const std::wstring text = SPLGetWindowTextOwned(secret ? edit : passwdEdit);
            if (SetWindowTextW(!secret ? edit : passwdEdit, text.c_str()))
            {
                EnableControls();
            }
            else
            {
                CheckDlgButton(HWindow, IDC_SECRETCOMMAND,
                               secret ? BST_UNCHECKED : BST_CHECKED);
                ShowMessage(HWindow, LangStr(IDS_OPERDOPPR_LOWMEM).c_str(),
                            LangStr(IDS_FTPERRORTITLE).c_str(), MB_OK | MB_ICONEXCLAMATION);
            }

            return TRUE;
        }
        break;
    }
    }
    return CCenteredDialog::DialogProc(uMsg, wParam, lParam);
}

//
// ****************************************************************************
// CServersListbox
//

CServersListbox::CServersListbox(CConfigPageServers* dlg, int ctrlID)
    : CWindow(dlg->HWindow, ctrlID)
{
    ParentDlg = dlg;
}

void CServersListbox::OpenContextMenu(int curSel, int menuX, int menuY)
{
    HMENU main = LoadMenu(HLanguage, MAKEINTRESOURCE(IDM_SERVERSACTIONS));
    if (main != NULL)
    {
        HMENU subMenu = GetSubMenu(main, 0);
        if (subMenu != NULL)
        {
            int count = ParentDlg->TmpServerTypeList != NULL ? ParentDlg->TmpServerTypeList->Count : 0;
            MyEnableMenuItem(subMenu, IDB_EDITSERVER, count != 0);
            MyEnableMenuItem(subMenu, CM_COPYSERVERTO, count != 0);
            MyEnableMenuItem(subMenu, CM_RENAMESERVER, count != 0);
            MyEnableMenuItem(subMenu, CM_REMOVESERVER, count != 0);
            MyEnableMenuItem(subMenu, CM_EXPORTSERVER, count != 0);
            MyEnableMenuItem(subMenu, IDB_MOVESERVERUP, curSel > 0);
            MyEnableMenuItem(subMenu, IDB_MOVESERVERDOWN, curSel + 1 < count);
            DWORD cmd = TrackPopupMenuEx(subMenu, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_RIGHTBUTTON,
                                         menuX, menuY, HWindow, NULL);
            if (cmd != 0)
                PostMessage(ParentDlg->HWindow, WM_COMMAND, cmd, 0);
        }
        DestroyMenu(main);
    }
}

LRESULT
CServersListbox::WindowProc(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
    {
        switch (wParam)
        {
        case VK_INSERT:
            PostMessage(ParentDlg->HWindow, WM_COMMAND, IDB_NEWSERVER, 0);
            break;
        case VK_F2:
            PostMessage(ParentDlg->HWindow, WM_COMMAND, CM_RENAMESERVER, 0);
            break;
        case VK_DELETE:
            PostMessage(ParentDlg->HWindow, WM_COMMAND, CM_REMOVESERVER, 0);
            break;

        case VK_UP:
        case VK_DOWN:
        {
            if (GetKeyState(VK_MENU) & 0x8000) // Alt key pressed
            {
                PostMessage(ParentDlg->HWindow, WM_COMMAND,
                            (LOWORD(wParam) == VK_UP ? IDB_MOVESERVERUP : IDB_MOVESERVERDOWN), 0);
            }
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
        break; // let the keys pass through, the listbox should not handle them, no problem
    }

    case WM_LBUTTONDBLCLK:
    {
        PostMessage(ParentDlg->HWindow, WM_COMMAND, IDB_EDITSERVER, 0);
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
        if (HIWORD(item) == 0 && item >= 0 && ParentDlg->TmpServerTypeList != NULL &&
            item < ParentDlg->TmpServerTypeList->Count && curSel != item)
        {
            SendMessage(HWindow, LB_SETCURSEL, item, 0);
            SendMessage(ParentDlg->HWindow, WM_COMMAND, MAKELPARAM(IDL_SUPPORTEDSERVERS, LBN_SELCHANGE), 0);
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
// CConnectAdvancedDlg
//

CConnectAdvancedDlg::CConnectAdvancedDlg(HWND parent, CFTPServer* server,
                                         CFTPProxyServerList* sourceTmpFTPProxyServerList)
    : CCenteredDialog(HLanguage, IDD_CONNECTADVANCED, IDD_CONNECTADVANCED, parent)
{
    SourceTmpFTPProxyServerList = sourceTmpFTPProxyServerList;
    TmpFTPProxyServerList = new CFTPProxyServerList;
    if (TmpFTPProxyServerList != NULL)
    {
        if (!SourceTmpFTPProxyServerList->CopyMembersToList(*TmpFTPProxyServerList))
        {
            delete TmpFTPProxyServerList;
            TmpFTPProxyServerList = NULL;
        }
    }
    else
        TRACE_E(LOW_MEMORY);

    Server = server;
    LastUseMaxCon = -1;
    LastUseTotSpeed = -1;
    LastKeepConnectionAlive = -1;
    KASendCmd = -1;
}

CConnectAdvancedDlg::~CConnectAdvancedDlg()
{
    if (TmpFTPProxyServerList != NULL)
        delete TmpFTPProxyServerList;
}

void CConnectAdvancedDlg::Validate(CTransferInfo& ti)
{
    int port;
    ti.EditLine(IDC_CONNECTTOPORT, port);
    if (!ti.IsGood())
        return; // an error has already occurred
    if (port <= 0 || port >= 65536)
    {
        ShowMessage(HWindow, LangStr(IDS_PORTISUSHORT).c_str(),
                                         LangStr(IDS_FTPERRORTITLE).c_str(), MB_OK | MB_ICONEXCLAMATION);
        ti.ErrorOn(IDC_CONNECTTOPORT);
        return;
    }

    // test if "max. concurrent connections" (if used) is valid number
    int enableMaxConcCon;
    ti.CheckBox(IDC_MAXCONCURRENTCON, enableMaxConcCon);
    int maxConcCon;
    if (enableMaxConcCon == 1)
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

    int useKeepAlive;
    ti.CheckBox(IDC_USEKEEPALIVE, useKeepAlive);
    if (useKeepAlive == 1)
    {
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
    }

    // test if "server speed limit" (if used) is valid number
    int enableSrvSpeedLimit;
    ti.CheckBox(IDC_SRVSPEEDLIMIT, enableSrvSpeedLimit);
    double srvSpeedLimit;
    if (enableSrvSpeedLimit == 1)
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

    if (ti.IsGood() && SendDlgItemMessage(HWindow, IDC_LISTCOMMAND, WM_GETTEXTLENGTH, 0, 0) == 0)
    {
        ShowMessage(HWindow, LangStr(IDS_CMDMAYNOTBEEMPTY).c_str(),
                                         LangStr(IDS_FTPERRORTITLE).c_str(), MB_OK | MB_ICONEXCLAMATION);
        ti.ErrorOn(IDC_LISTCOMMAND);
        return;
    }
}

void CConnectAdvancedDlg::Transfer(CTransferInfo& ti)
{
    if (TmpFTPProxyServerList != NULL)
    {
        ProxyComboBox(HWindow, ti, IDC_PROXYSERVER, Server->ProxyServerUID, TRUE,
                      TmpFTPProxyServerList);
        if (ti.Type == ttDataFromWindow)                                            // copy the data back to the source
            TmpFTPProxyServerList->CopyMembersToList(*SourceTmpFTPProxyServerList); // CheckProxyServersUID() for the edited bookmark list is called right after this dialog closes
    }

    if (ti.Type == ttDataToWindow)
    {
        SendDlgItemMessage(HWindow, IDE_TARGETPATH, EM_SETLIMITTEXT, 0, 0);
        SetDlgItemTextW(HWindow, IDE_TARGETPATH, Server->TargetPanelPath.c_str());
    }
    else
    {
        try
        {
            Server->TargetPanelPath = SPLGetDlgItemTextOwned(HWindow, IDE_TARGETPATH);
        }
        catch (...)
        {
            ShowMessage(HWindow, LangStr(IDS_OPERDOPPR_LOWMEM).c_str(),
                        LangStr(IDS_FTPERRORTITLE).c_str(), MB_OK | MB_ICONEXCLAMATION);
            ti.ErrorOn(IDE_TARGETPATH);
            return;
        }
    }
    HWND combo;
    if (ti.GetControl(combo, IDC_SERVERTYPE))
    {
        if (ti.Type == ttDataToWindow)
        {
            SendMessageW(combo, CB_RESETCONTENT, 0, 0);
            SendMessageW(combo, CB_ADDSTRING, 0,
                         reinterpret_cast<LPARAM>(LangStr(IDS_SRVTYPEAUTODETECT).c_str()));
            int index;
            Config.LockServerTypeList()->AddNamesToCombo(
                combo, Server->ServerType.empty() ? NULL : Server->ServerType.c_str(), index);
            Config.UnlockServerTypeList();
            SendMessage(combo, CB_SETCURSEL, index + 1, 0);
        }
        else
        {
            int i = (int)SendMessage(combo, CB_GETCURSEL, 0, 0);
            CServerTypeList* serverTypeList = Config.LockServerTypeList(); // everything in the main thread, Config.ServerTypeList should not change (from the moment the combo is filled)
            if (i != CB_ERR && i > 0 && i - 1 < serverTypeList->Count)
            {
                if (!FtpStoreLocalTextBytes(serverTypeList->At(i - 1)->TypeName,
                                            Server->ServerType))
                    ti.ErrorOn(IDC_SERVERTYPE);
            }
            else
            {
                Server->ServerType.clear();
            }
            Config.UnlockServerTypeList();
        }
    }
    if (ti.GetControl(combo, IDC_TRANSFERMODE))
    {
        if (ti.Type == ttDataToWindow)
        {
            SendMessageW(combo, CB_RESETCONTENT, 0, 0);
            int resIDs[] = {IDS_TRANSFMODEDEFAULT, IDS_TRANSFMODEBINARY, IDS_TRANSFMODEASCII,
                            IDS_TRANSFMODEAUTO, -1};
            int i;
            for (i = 0; resIDs[i] != -1; i++)
            {
                SendMessageW(combo, CB_ADDSTRING, 0,
                             reinterpret_cast<LPARAM>(LangStr(resIDs[i]).c_str()));
            }
            if (Server->TransferMode < 0)
                Server->TransferMode = 0;
            if (Server->TransferMode > 3)
                Server->TransferMode = 3;
            SendMessage(combo, CB_SETCURSEL, Server->TransferMode, 0);
        }
        else
        {
            int i = (int)SendMessage(combo, CB_GETCURSEL, 0, 0);
            if (i != CB_ERR && i >= 0 && i <= 3)
                Server->TransferMode = i;
            else
                TRACE_E("Unexpected situation in CConnectAdvancedDlg::Transfer().");
        }
    }
    if (ti.GetControl(combo, IDC_LISTCOMMAND))
    {
        if (ti.Type == ttDataToWindow)
        {
            SendMessageW(combo, CB_RESETCONTENT, 0, 0);
            const wchar_t* defaults[] = {L"LIST", L"NLST", L"LIST -a"};
            for (const wchar_t* value : defaults)
                SendMessageW(combo, CB_ADDSTRING, 0,
                             reinterpret_cast<LPARAM>(value));
            HWND edit = GetWindow(combo, GW_CHILD);
            if (edit != NULL)
                SendMessageW(edit, EM_LIMITTEXT, 0, 0);
            if (!Server->ListCommand.empty())
            {
                if (!SetWindowLocalText(combo, Server->ListCommand.c_str()))
                    ti.ErrorOn(IDC_LISTCOMMAND);
            }
            else
                SendMessageW(combo, CB_SETCURSEL, 0, 0); // select the LIST_CMD_TEXT text
        }
        else
        {
            std::string listCmd;
            if (!ReadWindowLocalText(combo, listCmd))
                ti.ErrorOn(IDC_LISTCOMMAND);
            else if (listCmd != LIST_CMD_TEXT)
                Server->ListCommand.swap(listCmd);
            else
                Server->ListCommand.clear();
        }
    }
    ti.EditLine(IDC_CONNECTTOPORT, Server->Port);
    ti.EditLine(IDE_INITFTPCOMMANDS, Server->InitFTPCommands);

    ti.CheckBox(IDC_USEKEEPALIVE, Server->KeepConnectionAlive);
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
            // verify that KeepAliveCommand stays within bounds (perhaps only a direct registry edit could break it)
            if (Config.KeepAliveCommand >= i)
                Config.KeepAliveCommand = i - 1;
            if (Config.KeepAliveCommand < 0)
                Config.KeepAliveCommand = 0;
            if (Server->KeepAliveCommand >= i)
                Server->KeepAliveCommand = i - 1;
            if (Server->KeepAliveCommand < 0)
                Server->KeepAliveCommand = 0;
        }

        if (ti.Type == ttDataToWindow)
        {
            switch (Server->KeepConnectionAlive)
            {
            case 0: // UNCHECKED
            {
                SendMessage(combo, CB_SETCURSEL, -1, 0);
                SetDlgItemTextW(HWindow, IDE_KEEPALIVEEVERY, L"");
                SetDlgItemTextW(HWindow, IDE_KEEPALIVESTOPAFTER, L"");
                break;
            }

            case 1: // CHECKED
            {
                SendMessage(combo, CB_SETCURSEL, Server->KeepAliveCommand, 0);
                ti.EditLine(IDE_KEEPALIVEEVERY, Server->KeepAliveSendEvery);
                ti.EditLine(IDE_KEEPALIVESTOPAFTER, Server->KeepAliveStopAfter);
                break;
            }

            default: // INDETERMINATE
            {
                SendMessage(combo, CB_SETCURSEL, Config.KeepAliveCommand, 0);
                ti.EditLine(IDE_KEEPALIVEEVERY, Config.KeepAliveSendEvery);
                ti.EditLine(IDE_KEEPALIVESTOPAFTER, Config.KeepAliveStopAfter);
                break;
            }
            }
        }
        else
        {
            if (Server->KeepConnectionAlive == 1) // CHECKED
            {
                int i = (int)SendMessage(combo, CB_GETCURSEL, 0, 0);
                if (i != CB_ERR)
                    Server->KeepAliveCommand = i;
                ti.EditLine(IDE_KEEPALIVEEVERY, Server->KeepAliveSendEvery);
                ti.EditLine(IDE_KEEPALIVESTOPAFTER, Server->KeepAliveStopAfter);
            }
        }
    }

    ti.CheckBox(IDC_USEPASSIVEMODE, Server->UsePassiveMode);
    ti.CheckBox(IDC_MAXCONCURRENTCON, Server->UseMaxConcurrentConnections);
    if (ti.Type == ttDataFromWindow && Server->UseMaxConcurrentConnections == 1)
    {
        ti.EditLine(IDE_MAXCONCURRENTCON, Server->MaxConcurrentConnections);
    }
    ti.CheckBox(IDC_SRVSPEEDLIMIT, Server->UseServerSpeedLimit);
    if (ti.Type == ttDataFromWindow && Server->UseServerSpeedLimit == 1)
    {
        const wchar_t buff[] = L"%g";
        ti.EditLine(IDE_SRVSPEEDLIMIT, Server->ServerSpeedLimit, buff);
        if (ti.Type == ttDataFromWindow &&
            Server->ServerSpeedLimit < 0.001)
            Server->ServerSpeedLimit = 0.001; // at least 1 byte per second
    }
    ti.CheckBox(IDC_USELISTINGSCACHE, Server->UseListingsCache);
    ti.CheckBox(IDC_ENCRYPTCONTROLCONN, Server->EncryptControlConnection);
    ti.CheckBox(IDC_ENCRYPTDATACONN, Server->EncryptDataConnection);
    if (ti.Type == ttDataFromWindow)
    {
        int compressData;
        ti.CheckBox(IDC_COMPRESSDATA, compressData);
        switch (compressData)
        {
        case 0:
            Server->CompressData = 0;
            break; // NO
        case 1:
            Server->CompressData = 6;
            break; // Yes, default compression is 6
        case 2:
            Server->CompressData = -1;
            break; // Take the default from the globals
        }
    }
    else
    {
        int compressData;
        switch (Server->CompressData)
        {
        case 0:
            compressData = 0;
            break; // NO
        case -1:
            compressData = 2;
            break; // Take the default from the globals
        default:
        case 6:
            compressData = 1;
            break; // Yes, default compression is 6
        }
        ti.CheckBox(IDC_COMPRESSDATA, compressData);
    }
} /* CConnectAdvancedDlg::Transfer */

void CheckboxCombo(HWND dlg, int checkboxID, int comboID, int* lastCheck, int* valueBuf,
                   int checkedVal, BOOL globValUsed, int globVal)
{
    int setVal;
    int check = IsDlgButtonChecked(dlg, checkboxID);
    EnableWindow(GetDlgItem(dlg, comboID), check == BST_CHECKED);
    if (*lastCheck != check)
    {
        if (*lastCheck == 1)
            *valueBuf = (int)SendDlgItemMessage(dlg, comboID, CB_GETCURSEL, 0, 0);
        switch (check)
        {
        case 0:
            setVal = -1;
            break; // disabled
        case 1:
        {
            if (*valueBuf == -1)
                *valueBuf = checkedVal;
            setVal = *valueBuf;
            break; // enabled
        }

        default: // third state
        {
            if (globValUsed)
                setVal = globVal;
            else
                setVal = -1; // unused
            break;
        }
        }
        SendDlgItemMessage(dlg, comboID, CB_SETCURSEL, setVal, 0);
        *lastCheck = check;
    }
}

void CConnectAdvancedDlg::EnableControls()
{
    CheckboxEditLineInteger(HWindow, IDC_MAXCONCURRENTCON, IDE_MAXCONCURRENTCON, &LastUseMaxCon,
                            MaxConnectionsText, Server->MaxConcurrentConnections,
                            Config.UseMaxConcurrentConnections, Config.MaxConcurrentConnections);

    CheckboxEditLineDouble(HWindow, IDC_SRVSPEEDLIMIT, IDE_SRVSPEEDLIMIT, &LastUseTotSpeed,
                           TotalSpeedText, Server->ServerSpeedLimit,
                           Config.UseServerSpeedLimit, Config.ServerSpeedLimit);

    int auxLastKeepConnectionAlive = LastKeepConnectionAlive;
    CheckboxCombo(HWindow, IDC_USEKEEPALIVE, IDC_KEEPALIVESEND, &auxLastKeepConnectionAlive,
                  &KASendCmd, Server->KeepAliveCommand, Config.KeepAlive, Config.KeepAliveCommand);
    auxLastKeepConnectionAlive = LastKeepConnectionAlive;
    CheckboxEditLineInteger(HWindow, IDC_USEKEEPALIVE, IDE_KEEPALIVEEVERY, &auxLastKeepConnectionAlive,
                            KeepAliveEveryText, Server->KeepAliveSendEvery,
                            Config.KeepAlive, Config.KeepAliveSendEvery);
    auxLastKeepConnectionAlive = LastKeepConnectionAlive;
    CheckboxEditLineInteger(HWindow, IDC_USEKEEPALIVE, IDE_KEEPALIVESTOPAFTER, &auxLastKeepConnectionAlive,
                            KeepAliveStopAfterText, Server->KeepAliveStopAfter,
                            Config.KeepAlive, Config.KeepAliveStopAfter);
    LastKeepConnectionAlive = auxLastKeepConnectionAlive;
    EnableWindow(GetDlgItem(HWindow, IDC_ENCRYPTCONTROLCONN), TRUE);
    EnableWindow(GetDlgItem(HWindow, IDC_ENCRYPTDATACONN), IsDlgButtonChecked(HWindow, IDC_ENCRYPTCONTROLCONN));
}

INT_PTR
CConnectAdvancedDlg::DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    CALL_STACK_MESSAGE4("CConnectAdvancedDlg::DialogProc(0x%X, 0x%IX, 0x%IX)", uMsg, wParam, lParam);
    switch (uMsg)
    {
    case WM_INITDIALOG:
    {
        SalamanderGUI->AttachButton(HWindow, IDB_ADDPROXYSRV, BTF_DROPDOWN);

        SalamanderGeneral->InstallWordBreakProc(GetDlgItem(HWindow, IDE_TARGETPATH));
        INT_PTR ret = CCenteredDialog::DialogProc(uMsg, wParam, lParam);
        EnableControls();
        return ret;
    }

    case WM_USER_BUTTONDROPDOWN:
    {
        if (LOWORD(wParam) == IDB_ADDPROXYSRV) // dropdown menu on the Add button
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
                        // enable the menu items
                        HWND combo = GetDlgItem(HWindow, IDC_PROXYSERVER);
                        int sel = (int)SendMessage(combo, CB_GETCURSEL, 0, 0);
                        int count = (int)SendMessage(combo, CB_GETCOUNT, 0, 0);
                        int fixedItems = 2; // "not used" + "default"
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
        if (LOWORD(wParam) == IDB_BROWSE)
        {
            const std::wstring initDir = SPLGetDlgItemTextOwned(HWindow, IDE_TARGETPATH);
            const std::wstring title = SPLGetWindowTextOwned(HWindow);
            std::wstring path;
            if (SPLGetTargetDirectoryOwned(SalamanderGeneral, HWindow, HWindow,
                                           title.c_str(),
                                           SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_SELECTTARGETDIR).c_str(),
                                           path, FALSE, initDir.c_str()))
            {
                SetDlgItemTextW(HWindow, IDE_TARGETPATH, path.c_str());
            }
            return TRUE;
        }

        if (HIWORD(wParam) == BN_CLICKED &&
            (LOWORD(wParam) == IDC_MAXCONCURRENTCON || LOWORD(wParam) == IDC_SRVSPEEDLIMIT ||
             LOWORD(wParam) == IDC_USEKEEPALIVE || LOWORD(wParam) == IDC_ENCRYPTCONTROLCONN))
        {
            if (LOWORD(wParam) == IDC_ENCRYPTCONTROLCONN)
                CheckDlgButton(HWindow, IDC_ENCRYPTDATACONN, IsDlgButtonChecked(HWindow, IDC_ENCRYPTCONTROLCONN));
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
                TmpFTPProxyServerList->EditProxyServer(HWindow, GetDlgItem(HWindow, IDC_PROXYSERVER), TRUE);
                return TRUE;
            }

            case CM_DELETEPROXYSRV:
            {
                TmpFTPProxyServerList->DeleteProxyServer(HWindow, GetDlgItem(HWindow, IDC_PROXYSERVER), TRUE);
                return TRUE;
            }

            case CM_MOVEUPPROXYSRV:
            {
                TmpFTPProxyServerList->MoveUpProxyServer(GetDlgItem(HWindow, IDC_PROXYSERVER), TRUE);
                return TRUE;
            }

            case CM_MOVEDOWNPROXYSRV:
            {
                TmpFTPProxyServerList->MoveDownProxyServer(GetDlgItem(HWindow, IDC_PROXYSERVER), TRUE);
                return TRUE;
            }
            }
        }
        break;
    }
    }
    return CCenteredDialog::DialogProc(uMsg, wParam, lParam);
}

//
// ****************************************************************************
// CRenameDlg
//

CRenameDlg::CRenameDlg(HWND parent, std::wstring& name, BOOL newServer,
                       const wchar_t* copyFromName)
    : CCenteredDialog(HLanguage, newServer ? IDD_NEWBOOKMARKDLG : IDD_RENAMEDLG, parent)
{
    NameText = &name;
    NewServer = newServer;
    CopyDataFromFocusedServer = FALSE;
    AddBookmark = FALSE;
    ServerTypes = TRUE;
    if (copyFromName != NULL)
        FtpStoreWideText(copyFromName, CopyFromName);
}

CRenameDlg::CRenameDlg(HWND parent, std::wstring& name, BOOL newServer, BOOL addBookmark)
    : CCenteredDialog(HLanguage, newServer ? IDD_NEWBOOKMARKDLG : IDD_RENAMEDLG, parent)
{
    NameText = &name;
    NewServer = newServer;
    CopyDataFromFocusedServer = FALSE;
    AddBookmark = addBookmark;
    ServerTypes = FALSE;
    CopyFromName.clear();
}

void CRenameDlg::Validate(CTransferInfo& ti)
{
    std::wstring textW;
    try
    {
        textW = SPLGetDlgItemTextOwned(HWindow, IDE_NAME);
    }
    catch (...)
    {
        ti.ErrorOn(IDE_NAME);
        return;
    }
    if (textW.empty())
    {
        ShowMessage(HWindow, LangStr(IDS_MAYNOTBEEMPTY).c_str(),
                                         LangStr(IDS_FTPERRORTITLE).c_str(), MB_OK | MB_ICONEXCLAMATION);
        ti.ErrorOn(IDE_NAME);
    }
}

void CRenameDlg::Transfer(CTransferInfo& ti)
{
    if (ti.Type == ttDataToWindow)
        SetDlgItemTextW(HWindow, IDE_NAME, NameText->c_str());
    else
    {
        try
        {
            *NameText = SPLGetDlgItemTextOwned(HWindow, IDE_NAME);
        }
        catch (...)
        {
            ti.ErrorOn(IDE_NAME);
        }
    }
    if (NewServer)
        ti.CheckBox(IDC_COPYFOCUSEDSRV, CopyDataFromFocusedServer);
}

INT_PTR
CRenameDlg::DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    CALL_STACK_MESSAGE4("CRenameDlg::DialogProc(0x%X, 0x%IX, 0x%IX)", uMsg, wParam, lParam);
    if (uMsg == WM_INITDIALOG)
    {
        if (ServerTypes)
            SetWindowTextW(HWindow, LangStr(NewServer ? IDS_SRVTYPENEWTITLE : IDS_SRVTYPERENAMETITLE).c_str());
        if (AddBookmark)
        {
            SetWindowTextW(HWindow, LangStr(IDS_ADDBOOKMARKTITLE).c_str());
            SetDlgItemTextW(HWindow, IDT_SUBJECT, LangStr(IDS_ADDBOOKMARKTEXT).c_str());
        }
        else
        {
            if (!NewServer) // rename
            {
                const std::wstring subject = SPLFormatStringOwned(
                    LangStr(IDS_RENAMESRVTO).c_str(), NameText->c_str());
                SetDlgItemTextW(HWindow, IDT_SUBJECT, subject.c_str());
            }
            else // new
            {
                if (!ServerTypes || !CopyFromName.empty())
                {
                    std::wstring checkboxName = ServerTypes ? CopyFromName : *NameText;
                    try
                    {
                        SPLDuplicateAmpersandsOwned(SalamanderGeneral, checkboxName);
                        const std::wstring checkboxText = SPLFormatStringOwned(
                            LangStr(ServerTypes ? IDS_SRVTYPECOPYFROM : IDS_COPYDATAFROM).c_str(),
                            !checkboxName.empty() ? checkboxName.c_str() : LangStr(IDS_QUICKCONNECT).c_str());
                        SetDlgItemTextW(HWindow, IDC_COPYFOCUSEDSRV, checkboxText.c_str());
                    }
                    catch (...)
                    {
                        SetDlgItemTextW(HWindow, IDC_COPYFOCUSEDSRV, L"");
                    }
                }
                else // new server type + empty list = necessary to hide/disable the "copy from" checkbox
                {
                    ShowWindow(GetDlgItem(HWindow, IDC_COPYFOCUSEDSRV), SW_HIDE);
                }
                if (!ServerTypes && !CopyDataFromFocusedServer)
                    NameText->clear(); // empty name for a new server (it will be set in Transfer())
                if (ServerTypes)
                    SetDlgItemTextW(HWindow, IDT_NEWNAME, LangStr(IDS_SRVTYPENEWSUBJECT).c_str());
            }
        }
    }
    return CCenteredDialog::DialogProc(uMsg, wParam, lParam);
}

//
// ****************************************************************************
// CEnterStrDlg
//

CEnterStrDlg::CEnterStrDlg(HWND parent, const wchar_t* title, const wchar_t* text, std::wstring& data,
                           BOOL hideChars, const wchar_t* connectingToAs, BOOL allowEmpty)
    : CCenteredDialog(HLanguage, IDD_ENTERSTRDLG, parent)
{
    Title = title;
    Text = text;
    Data = &data;
    HideChars = hideChars;
    ConnectingToAs = connectingToAs;
    AllowEmpty = allowEmpty;
}

void CEnterStrDlg::Validate(CTransferInfo& ti)
{
    if (!AllowEmpty && SendDlgItemMessage(HWindow, IDE_STRING, WM_GETTEXTLENGTH, 0, 0) == 0)
    {
        ShowMessage(HWindow, LangStr(IDS_MAYNOTBEEMPTY).c_str(),
                                         LangStr(IDS_FTPERRORTITLE).c_str(), MB_OK | MB_ICONEXCLAMATION);
        ti.ErrorOn(IDE_STRING);
    }
}

void CEnterStrDlg::Transfer(CTransferInfo& ti)
{
    ti.EditLine(IDE_STRING, *Data);
}

INT_PTR
CEnterStrDlg::DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    CALL_STACK_MESSAGE4("CEnterStrDlg::DialogProc(0x%X, 0x%IX, 0x%IX)", uMsg, wParam, lParam);
    if (uMsg == WM_INITDIALOG)
    {
        if (!HideChars)
            SendDlgItemMessage(HWindow, IDE_STRING, EM_SETPASSWORDCHAR, NULL, 0);
        if (Title != NULL && Title[0] != 0)
            SetWindowTextW(HWindow, Title);
        if (Text != NULL && Text[0] != 0)
            SetDlgItemTextW(HWindow, IDT_STRING, Text);
        if (ConnectingToAs != NULL && ConnectingToAs[0] != 0)
            SetDlgItemTextW(HWindow, IDT_CONNECTINGTOAS, ConnectingToAs);
    }
    return CCenteredDialog::DialogProc(uMsg, wParam, lParam);
}

//
// ****************************************************************************
// CLoginErrorDlg
//

CLoginErrorDlg::CLoginErrorDlg(HWND parent, const wchar_t* serverReply,
                               CProxyScriptParams* proxyScriptParams,
                               const wchar_t* connectingTo, const wchar_t* title,
                               const wchar_t* retryWithoutAskingText,
                               const wchar_t* errorTitle, BOOL disableUser,
                               BOOL hideApplyToAll, BOOL proxyUsed)
    : CCenteredDialog(HLanguage, proxyUsed ? IDD_LOGINERRORDLGEX : IDD_LOGINERRORDLG, parent)
{
    ServerReply = serverReply;
    ProxyScriptParams = proxyScriptParams;
    RetryWithoutAsking = FALSE;
    LoginChanged = FALSE;
    ConnectingTo = connectingTo;
    Title = title;
    RetryWithoutAskingText = retryWithoutAskingText;
    ErrorTitle = errorTitle;
    DisableUser = disableUser;
    HideApplyToAll = hideApplyToAll;
    ApplyToAll = TRUE;
    ProxyUsed = proxyUsed;
}

void CLoginErrorDlg::Validate(CTransferInfo& ti)
{
    if (SendDlgItemMessage(HWindow, IDE_USERNAME, WM_GETTEXTLENGTH, 0, 0) == 0)
    {
        ShowMessage(HWindow, LangStr(IDS_MAYNOTBEEMPTY).c_str(),
                                         LangStr(IDS_FTPERRORTITLE).c_str(), MB_OK | MB_ICONEXCLAMATION);
        ti.ErrorOn(IDE_USERNAME);
    }
}

void CLoginErrorDlg::Transfer(CTransferInfo& ti)
{
    std::wstring originalUser;
    std::wstring originalPassword;
    std::wstring originalAccount;
    std::wstring originalProxyUser;
    std::wstring originalProxyPassword;

    if (ti.Type == ttDataFromWindow &&
        (!FtpStoreWideText(ProxyScriptParams->User, originalUser) ||
         !FtpStoreWideText(ProxyScriptParams->Password, originalPassword) ||
         !FtpStoreWideText(ProxyScriptParams->Account, originalAccount) ||
         !FtpStoreWideText(ProxyScriptParams->ProxyUser, originalProxyUser) ||
         !FtpStoreWideText(ProxyScriptParams->ProxyPassword, originalProxyPassword)))
    {
        FTPSecureWipe(originalPassword);
        FTPSecureWipe(originalAccount);
        FTPSecureWipe(originalProxyPassword);
        ti.ErrorOn(IDE_USERNAME);
        return;
    }
    ti.EditLine(IDE_USERNAME, ProxyScriptParams->User);
    ti.EditLine(IDE_PASSWORD, ProxyScriptParams->Password);
    ti.EditLine(IDE_ACCOUNT, ProxyScriptParams->Account);
    if (ProxyUsed)
    {
        ti.EditLine(IDE_PROXYUSER, ProxyScriptParams->ProxyUser);
        ti.EditLine(IDE_PROXYPASSWD, ProxyScriptParams->ProxyPassword);
    }
    ti.CheckBox(IDC_RETRYWITHOUTASK, RetryWithoutAsking);
    if (!HideApplyToAll)
        ti.CheckBox(IDC_APPLYTOALL, ApplyToAll);

    if (ti.Type == ttDataFromWindow)
    {
        LoginChanged = (originalUser != ProxyScriptParams->User ||
                        originalPassword != ProxyScriptParams->Password ||
                        originalAccount != ProxyScriptParams->Account ||
                        ProxyUsed && originalProxyUser != ProxyScriptParams->ProxyUser ||
                        ProxyUsed && originalProxyPassword != ProxyScriptParams->ProxyPassword);
        FTPSecureWipe(originalPassword);
        FTPSecureWipe(originalAccount);
        FTPSecureWipe(originalProxyPassword);
    }
}

INT_PTR
CLoginErrorDlg::DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    CALL_STACK_MESSAGE4("CLoginErrorDlg::DialogProc(0x%X, 0x%IX, 0x%IX)", uMsg, wParam, lParam);
    if (uMsg == WM_INITDIALOG)
    {
        if (Title != NULL)
            SetWindowTextW(HWindow, Title);

        SetDlgItemTextW(HWindow, IDE_SERVERREPLY, ServerReply);
        if (ConnectingTo != NULL)
            SetDlgItemTextW(HWindow, IDT_CONNECTINGTO, ConnectingTo);
        if (RetryWithoutAskingText != NULL)
            SetDlgItemTextW(HWindow, IDC_RETRYWITHOUTASK, RetryWithoutAskingText);
        if (ErrorTitle != NULL)
            SetDlgItemTextW(HWindow, IDT_ERRORTITLE, ErrorTitle);

        if (HideApplyToAll)
        {
            // remove the "apply to all" checkbox
            RECT r1;
            GetWindowRect(GetDlgItem(HWindow, IDC_APPLYTOALL), &r1);
            RECT r2;
            GetWindowRect(GetDlgItem(HWindow, IDC_RETRYWITHOUTASK), &r2);
            int delta = r1.bottom - r2.bottom;
            DestroyWindow(GetDlgItem(HWindow, IDC_APPLYTOALL));

            // shrink the dialog
            RECT windowR;
            GetWindowRect(HWindow, &windowR);
            SetWindowPos(HWindow, NULL, 0, 0, windowR.right - windowR.left, windowR.bottom - windowR.top - delta,
                         SWP_NOZORDER | SWP_NOMOVE);
        }

        if (DisableUser)
        {
            EnableWindow(GetDlgItem(HWindow, IDE_USERNAME), FALSE);
            SetFocus(GetDlgItem(HWindow, IDE_PASSWORD)); // we want our own focus
            CCenteredDialog::DialogProc(uMsg, wParam, lParam);
            return FALSE;
        }
    }
    return CCenteredDialog::DialogProc(uMsg, wParam, lParam);
}

//
// ****************************************************************************
// CConfigPageConfirmations
//

CConfigPageConfirmations::CConfigPageConfirmations() : CCommonPropSheetPage(NULL, HLanguage, IDD_CFGCONFIRMATIONS, IDD_CFGCONFIRMATIONS, PSP_HASHELP, NULL)
{
}

void CConfigPageConfirmations::Transfer(CTransferInfo& ti)
{
    int showCloseCon = !Config.AlwaysNotCloseCon;
    ti.CheckBox(IDC_ALWAYSNOTCLOSE, showCloseCon);
    Config.AlwaysNotCloseCon = !showCloseCon;
    int showDisconnect = !Config.AlwaysDisconnect;
    ti.CheckBox(IDC_ALWAYSDISCONNECT, showDisconnect);
    Config.AlwaysDisconnect = !showDisconnect;
    int showReconnect = !Config.AlwaysReconnect;
    ti.CheckBox(IDC_ALWAYSRECONNECT, showReconnect);
    Config.AlwaysReconnect = !showReconnect;
    ti.CheckBox(IDC_WARNWHENCONLOST, Config.WarnWhenConLost);
    int showOverwrite = !Config.AlwaysOverwrite;
    ti.CheckBox(IDC_ALWAYSOVERWRITE, showOverwrite);
    Config.AlwaysOverwrite = !showOverwrite;
    ti.CheckBox(IDC_HINTLISTHIDDENFILES, Config.HintListHiddenFiles);
}

//
// ****************************************************************************
// CConfigPageAdvanced
//

CConfigPageAdvanced::CConfigPageAdvanced() : CCommonPropSheetPage(NULL, HLanguage, IDD_CFGADVANCED, IDD_CFGADVANCED, PSP_HASHELP, NULL)
{
}

void CConfigPageAdvanced::Validate(CTransferInfo& ti)
{
    int num;
    int arr[] = {IDE_SRVREPLIESTIMEOUT, IDE_DELAYBETWCONRETR, IDE_CONNECTRETRIES,
                 IDE_NODATATRTIMEOUT, IDE_RESUMEMINFILESIZE, IDE_RESUMEOVERLAP, -1};
    BOOL gzthzero[] = {TRUE, FALSE, FALSE, TRUE, TRUE, FALSE, -1}; // testing > 0 (TRUE) or >= 0 (FALSE)
    int i;
    for (i = 0; arr[i] != -1; i++)
    {
        ti.EditLine(arr[i], num);
        if (!ti.IsGood())
            return; // an error has already occurred
        if (gzthzero[i] && num <= 0)
        {
            ShowMessage(HWindow, LangStr(IDS_MUSTBEGRTHANZERO).c_str(),
                                             LangStr(IDS_FTPERRORTITLE).c_str(), MB_OK | MB_ICONEXCLAMATION);
            ti.ErrorOn(arr[i]);
            return;
        }
        if (!gzthzero[i] && num < 0)
        {
            ShowMessage(HWindow, LangStr(IDS_MUSTBEPOSITIVE).c_str(),
                                             LangStr(IDS_FTPERRORTITLE).c_str(), MB_OK | MB_ICONEXCLAMATION);
            ti.ErrorOn(arr[i]);
            return;
        }
    }

    ti.EditLine(IDE_MEMCACHESIZELIMIT, num);
    if (!ti.IsGood())
        return; // an error has already occurred
    if (num < 100 || num > 1 * 1024 * 1024)
    {
        ShowMessage(HWindow, LangStr(IDS_INVALIDMEMCACHESIZE).c_str(),
                                         LangStr(IDS_FTPERRORTITLE).c_str(), MB_OK | MB_ICONEXCLAMATION);
        ti.ErrorOn(IDE_MEMCACHESIZELIMIT);
        return;
    }

    ti.EditLine(IDE_RESUMEOVERLAP, num);
    if (!ti.IsGood())
        return; // an error has already occurred
    if (num < 0 || num > 1024 * 1024 * 1024)
    {
        ShowMessage(HWindow, LangStr(IDS_INVALIDRESUMEOVERLAP).c_str(),
                                         LangStr(IDS_FTPERRORTITLE).c_str(), MB_OK | MB_ICONEXCLAMATION);
        ti.ErrorOn(IDE_RESUMEOVERLAP);
        return;
    }
}

void CConfigPageAdvanced::Transfer(CTransferInfo& ti)
{
    int num = (int)(Config.CacheMaxSize / CQuadWord(1024, 0)).Value;
    ti.EditLine(IDE_MEMCACHESIZELIMIT, num);
    if (ti.Type == ttDataFromWindow)
        Config.CacheMaxSize = CQuadWord(num, 0) * CQuadWord(1024, 0);

    HANDLES(EnterCriticalSection(&Config.ConParamsCS));
    ti.EditLine(IDE_SRVREPLIESTIMEOUT, Config.ServerRepliesTimeout);
    ti.EditLine(IDE_DELAYBETWCONRETR, Config.DelayBetweenConRetries);
    ti.EditLine(IDE_CONNECTRETRIES, Config.ConnectRetries);
    ti.EditLine(IDE_RESUMEMINFILESIZE, Config.ResumeMinFileSize);
    ti.EditLine(IDE_RESUMEOVERLAP, Config.ResumeOverlap);
    ti.EditLine(IDE_NODATATRTIMEOUT, Config.NoDataTransferTimeout);
    HANDLES(LeaveCriticalSection(&Config.ConParamsCS));
}

//
// ****************************************************************************
// CConfigPageLogs
//

CConfigPageLogs::CConfigPageLogs() : CCommonPropSheetPage(NULL, HLanguage, IDD_CFGLOGS, IDD_CFGLOGS, PSP_HASHELP, NULL)
{
    LastLogMaxSize = -1;
    LastMaxClosedConLogs = -1;
}

void CConfigPageLogs::Validate(CTransferInfo& ti)
{
    int enable;
    ti.CheckBox(IDC_ENABLELOGGING, enable);
    if (enable)
    {
        int num;
        int arr[] = {IDE_LOGMAXSIZE, IDE_MAXCLOSEDCONLOGS, -1};
        int arr2[] = {IDC_LOGMAXSIZE, IDC_MAXCLOSEDCONLOGS};
        int canBeZero[] = {FALSE, TRUE};
        int i;
        for (i = 0; arr[i] != -1; i++)
        {
            int used;
            ti.CheckBox(arr2[i], used);
            if (used)
            {
                ti.EditLine(arr[i], num);
                if (!ti.IsGood())
                    return; // an error has already occurred
                if (num < 0 || (!canBeZero[i] && num == 0))
                {
                    ShowMessage(HWindow, LangStr(canBeZero[i] ? IDS_MUSTBEPOSITIVE : IDS_MUSTBEGRTHANZERO).c_str(),
                                                     LangStr(IDS_FTPERRORTITLE).c_str(), MB_OK | MB_ICONEXCLAMATION);
                    ti.ErrorOn(arr[i]);
                    return;
                }
            }
        }
    }
}

void CConfigPageLogs::Transfer(CTransferInfo& ti)
{
    ti.CheckBox(IDC_ENABLELOGGING, Config.EnableLogging);
    ti.CheckBox(IDC_LOGMAXSIZE, Config.UseLogMaxSize);
    ti.CheckBox(IDC_MAXCLOSEDCONLOGS, Config.UseMaxClosedConLogs);
    ti.CheckBox(IDC_DISABLELOGWORKERS, Config.DisableLoggingOfWorkers);

    if (ti.Type == ttDataFromWindow)
    {
        if (Config.EnableLogging)
        {
            if (Config.UseLogMaxSize)
            {
                ti.EditLine(IDE_LOGMAXSIZE, Config.LogMaxSize);
                Config.LogMaxSize = (Config.LogMaxSize * 1024) / 1024; // trim any excessively large number
                if (Config.LogMaxSize <= 0)
                    Config.LogMaxSize = 1;
            }
            if (Config.UseMaxClosedConLogs)
                ti.EditLine(IDE_MAXCLOSEDCONLOGS, Config.MaxClosedConLogs);
        }
        Logs.ConfigChanged();
    }
}

void CConfigPageLogs::EnableControls()
{
    if (IsDlgButtonChecked(HWindow, IDC_ENABLELOGGING) == BST_CHECKED)
    {
        HWND hwnd = GetDlgItem(HWindow, IDC_LOGMAXSIZE);
        if (!IsWindowEnabled(hwnd))
            EnableWindow(hwnd, TRUE);
        hwnd = GetDlgItem(HWindow, IDC_MAXCLOSEDCONLOGS);
        if (!IsWindowEnabled(hwnd))
            EnableWindow(hwnd, TRUE);
        hwnd = GetDlgItem(HWindow, IDC_DISABLELOGWORKERS);
        if (!IsWindowEnabled(hwnd))
            EnableWindow(hwnd, TRUE);
        CheckboxEditLineInteger(HWindow, IDC_LOGMAXSIZE, IDE_LOGMAXSIZE, &LastLogMaxSize, LogMaxSizeText,
                                Config.LogMaxSize, 0, 0);
        CheckboxEditLineInteger(HWindow, IDC_MAXCLOSEDCONLOGS, IDE_MAXCLOSEDCONLOGS, &LastMaxClosedConLogs, MaxClosedConnectionLogsText,
                                Config.MaxClosedConLogs, 0, 0);
    }
    else
    {
        if (LastLogMaxSize == -1)
        {
            CheckboxEditLineInteger(HWindow, IDC_LOGMAXSIZE, IDE_LOGMAXSIZE, &LastLogMaxSize, LogMaxSizeText,
                                    Config.LogMaxSize, 0, 0);
        }
        if (LastMaxClosedConLogs == -1)
        {
            CheckboxEditLineInteger(HWindow, IDC_MAXCLOSEDCONLOGS, IDE_MAXCLOSEDCONLOGS, &LastMaxClosedConLogs, MaxClosedConnectionLogsText,
                                    Config.MaxClosedConLogs, 0, 0);
        }
        EnableWindow(GetDlgItem(HWindow, IDC_LOGMAXSIZE), FALSE);
        EnableWindow(GetDlgItem(HWindow, IDC_MAXCLOSEDCONLOGS), FALSE);
        EnableWindow(GetDlgItem(HWindow, IDE_LOGMAXSIZE), FALSE);
        EnableWindow(GetDlgItem(HWindow, IDE_MAXCLOSEDCONLOGS), FALSE);
        EnableWindow(GetDlgItem(HWindow, IDC_DISABLELOGWORKERS), FALSE);
    }
}

INT_PTR
CConfigPageLogs::DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    CALL_STACK_MESSAGE4("CConfigPageLogs::DialogProc(0x%X, 0x%IX, 0x%IX)", uMsg, wParam, lParam);
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
        if (HIWORD(wParam) == BN_CLICKED &&
            (LOWORD(wParam) == IDC_ENABLELOGGING ||
             LOWORD(wParam) == IDC_LOGMAXSIZE ||
             LOWORD(wParam) == IDC_MAXCLOSEDCONLOGS))
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
// CProxyServerDlg
//

CProxyServerDlg::CProxyServerDlg(HWND parent, CFTPProxyServerList* tmpFTPProxyServerList,
                                 CFTPProxyServer* proxy, BOOL edit)
    : CCenteredDialog(HLanguage, IDD_PROXYSERVER, IDD_PROXYSERVER, parent)
{
    TmpFTPProxyServerList = tmpFTPProxyServerList;
    Proxy = proxy;
    Edit = edit;
}

void CProxyServerDlg::Validate(CTransferInfo& ti)
{
    std::wstring proxyName;
    ti.EditLine(IDE_PRXSRV_NAME, proxyName);
    if (!ti.IsGood())
        return;
    if (!TmpFTPProxyServerList->IsProxyNameOK(Proxy, proxyName.c_str()))
    {
        ShowMessage(HWindow, LangStr(IDS_INVPROXYNAME).c_str(),
                                         LangStr(IDS_FTPERRORTITLE).c_str(), MB_OK | MB_ICONEXCLAMATION);
        ti.ErrorOn(IDE_PRXSRV_NAME);
        return;
    }

    HWND combo = GetDlgItem(HWindow, IDC_PRXSRV_TYPE);
    int proxyType = (int)SendMessage(combo, CB_GETCURSEL, 0, 0);
    if (proxyType == CB_ERR || proxyType < 0 || proxyType > fpstOwnScript)
        proxyType = fpstSocks4;
    BOOL proxyHostNeeded = HaveHostAndPort((CFTPProxyServerType)proxyType);
    std::wstring proxyHost;
    if (proxyHostNeeded)
    {
        ti.EditLine(IDE_PRXSRV_ADDRESS, proxyHost);
        if (wcschr(proxyHost.c_str(), L':') != NULL)
        {
            ShowMessage(HWindow, LangStr(IDS_INVPROXYADDRESS).c_str(),
                                             LangStr(IDS_FTPERRORTITLE).c_str(), MB_OK | MB_ICONEXCLAMATION);
            ti.ErrorOn(IDE_PRXSRV_ADDRESS);
            return;
        }

        int port;
        ti.EditLine(IDE_PRXSRV_PORT, port);
        if (!ti.IsGood())
            return; // an error has already occurred
        if (port <= 0 || port >= 65536)
        {
            ShowMessage(HWindow, LangStr(IDS_PORTISUSHORT).c_str(),
                                             LangStr(IDS_FTPERRORTITLE).c_str(), MB_OK | MB_ICONEXCLAMATION);
            ti.ErrorOn(IDE_PRXSRV_PORT);
            return;
        }
    }

    std::wstring proxyScriptText;
    if (proxyType == fpstOwnScript)
    {
        ti.EditLine(IDE_PRXSRV_SCRIPT, proxyScriptText);
        if (!ti.IsGood())
            return;
        std::string proxyScript;
        if (!FtpEncodeLocalText(proxyScriptText.c_str(), proxyScript))
        {
            ShowMessage(HWindow, LangStr(IDS_PRXSCRIPT_CANNOTENCODE).c_str(),
                        LangStr(IDS_FTPERRORTITLE).c_str(), MB_OK | MB_ICONEXCLAMATION);
            ti.ErrorOn(IDE_PRXSRV_SCRIPT);
            return;
        }
        const char* errPos = NULL;
        std::string errorDescription;
        BOOL lowMemory = FALSE;
        if (!ProcessProxyScript(FtpLocalTextCodec(), proxyScript.c_str(), &errPos, -1, NULL, NULL, NULL, NULL, NULL,
                                &errorDescription, &proxyHostNeeded, &lowMemory))
        {
            if (lowMemory)
            {
                ShowMessage(HWindow, LangStr(IDS_OPERDOPPR_LOWMEM).c_str(),
                            LangStr(IDS_FTPERRORTITLE).c_str(), MB_OK | MB_ICONEXCLAMATION);
                ti.ErrorOn(IDE_PRXSRV_SCRIPT);
                return;
            }
            std::wstring errorDescriptionW;
            if (!FtpDecodeLocalText(errorDescription, errorDescriptionW))
            {
                ShowMessage(HWindow, LangStr(IDS_OPERDOPPR_LOWMEM).c_str(),
                            LangStr(IDS_FTPERRORTITLE).c_str(), MB_OK | MB_ICONEXCLAMATION);
                ti.ErrorOn(IDE_PRXSRV_SCRIPT);
                return;
            }
            ShowMessage(HWindow, errorDescriptionW.c_str(),
                        LangStr(IDS_FTPERRORTITLE).c_str(), MB_OK | MB_ICONEXCLAMATION);
            size_t errorOffset = 0;
            const size_t byteOffset = errPos != NULL ? static_cast<size_t>(errPos - proxyScript.c_str()) : 0;
            FtpLocalByteOffsetToUtf16(proxyScript, byteOffset, errorOffset);
            SendDlgItemMessage(HWindow, IDE_PRXSRV_SCRIPT, EM_SETSEL, errorOffset, errorOffset);
            SendDlgItemMessage(HWindow, IDE_PRXSRV_SCRIPT, EM_SCROLLCARET, 0, 0); // scroll caret into view
            SendMessage(HWindow, WM_NEXTDLGCTL, (WPARAM)GetDlgItem(HWindow, IDE_PRXSRV_SCRIPT), TRUE);
            ti.ErrorOn(IDE_PRXSRV_SCRIPT);
            return;
        }
    }
    if (proxyHostNeeded && proxyHost.empty())
    {
        ShowMessage(HWindow, LangStr(IDS_EMPTYPROXYADDRESS).c_str(),
                                         LangStr(IDS_FTPERRORTITLE).c_str(), MB_OK | MB_ICONEXCLAMATION);
        ti.ErrorOn(IDE_PRXSRV_ADDRESS);
        return;
    }

    if (proxyType == fpstFTP_transparent)
    {
        std::wstring proxyUser;
        ti.EditLine(IDE_PRXSRV_USER, proxyUser);
        if (proxyUser.empty())
        {
            ShowMessage(HWindow, LangStr(IDS_EMPTYUSERFORTRANSPRX).c_str(),
                                             LangStr(IDS_FTPERRORTITLE).c_str(), MB_OK | MB_ICONEXCLAMATION);
            ti.ErrorOn(IDE_PRXSRV_USER);
            return;
        }
    }
}

void CProxyServerDlg::Transfer(CTransferInfo& ti)
{
    std::wstring proxyName;
    int proxyType;
    std::wstring proxyHost;
    int proxyPort;
    std::wstring proxyUser;
    std::wstring proxyPlainPassword;
    int saveProxyPassword;
    std::wstring proxyScriptText;
    std::string proxyScript;

    if (!FtpStoreWideText(Proxy->ProxyName, proxyName))
    {
        ti.ErrorOn(IDE_PRXSRV_NAME);
        return;
    }
    proxyType = Proxy->ProxyType;
    proxyPort = Proxy->ProxyPort;
    if (!FtpStoreWideText(Proxy->ProxyHost, proxyHost) ||
        !FtpStoreWideText(Proxy->ProxyUser, proxyUser))
    {
        ti.ErrorOn(IDE_PRXSRV_ADDRESS);
        return;
    }

    if (ti.Type == ttDataToWindow)
    {
        BOOL lockedPassword = TRUE;

        proxyPlainPassword.clear();
        CSalamanderPasswordManagerAbstract* passwordManager = SalamanderGeneral->GetSalamanderPasswordManager();
        if (!Proxy->SaveProxyPassword || !passwordManager->IsUsingMasterPassword() || passwordManager->IsMasterPasswordSet())
        {
            if (Proxy->ProxyEncryptedPassword != NULL)
            { // scrambled/encrypted -> plain
                std::wstring plainPassword;
                if (FTPDecryptPasswordW(passwordManager,
                                        Proxy->ProxyEncryptedPassword,
                                        Proxy->ProxyEncryptedPasswordSize,
                                        &plainPassword))
                {
                    proxyPlainPassword.swap(plainPassword);
                    FTPSecureWipe(plainPassword);
                    lockedPassword = FALSE;
                }
            }
            else
                lockedPassword = FALSE;
        }
        ShowHidePasswordControls(lockedPassword, FALSE);
    }

    saveProxyPassword = Proxy->SaveProxyPassword;
    if (ti.Type == ttDataToWindow &&
        !FtpDecodeLocalText(Proxy->ProxyScript, proxyScriptText))
    {
        ti.ErrorOn(IDE_PRXSRV_SCRIPT);
        return;
    }

    ti.EditLine(IDE_PRXSRV_NAME, proxyName);
    HWND combo;
    if (ti.GetControl(combo, IDC_PRXSRV_TYPE))
    {
        if (ti.Type == ttDataToWindow)
        {
            SendMessage(combo, CB_RESETCONTENT, 0, 0);

            int type = fpstSocks4 - 1;
            while (type != fpstOwnScript)
            {
                std::wstring typeName;
                if (!GetProxyTypeNameW((CFTPProxyServerType)++type, typeName))
                {
                    ti.ErrorOn(IDC_PRXSRV_TYPE);
                    return;
                }
                SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(typeName.c_str()));
            }

            SendMessage(combo, CB_SETCURSEL, proxyType, 0);
        }
        else
        {
            int sel = (int)SendMessage(combo, CB_GETCURSEL, 0, 0);
            if (sel != CB_ERR && sel >= 0 && sel <= fpstOwnScript)
                proxyType = sel;
            else
                proxyType = fpstSocks4;
        }
    }
    if (ti.Type == ttDataToWindow || HaveHostAndPort((CFTPProxyServerType)proxyType))
    {
        ti.EditLine(IDE_PRXSRV_ADDRESS, proxyHost);
        ti.EditLine(IDE_PRXSRV_PORT, proxyPort);
    }
    else
    {
        proxyHost.clear();
        proxyPort = 0;
    }
    ti.EditLine(IDE_PRXSRV_USER, proxyUser);
    if (ti.Type == ttDataToWindow || HavePassword((CFTPProxyServerType)proxyType))
    {
        ti.EditLine(IDE_PRXSRV_PASSWD, proxyPlainPassword);
        ti.CheckBox(IDC_PRXSRV_SAVEPASSWD, saveProxyPassword);
    }
    else
    {
        FTPSecureWipe(proxyPlainPassword);
        saveProxyPassword = FALSE;
    }

    if (proxyType == fpstOwnScript)
    {
        ti.EditLine(IDE_PRXSRV_SCRIPT, proxyScriptText);
        if (ti.Type == ttDataFromWindow && ti.IsGood() &&
            !FtpEncodeLocalText(proxyScriptText.c_str(), proxyScript))
        {
            ShowMessage(HWindow, LangStr(IDS_PRXSCRIPT_CANNOTENCODE).c_str(),
                        LangStr(IDS_FTPERRORTITLE).c_str(), MB_OK | MB_ICONEXCLAMATION);
            ti.ErrorOn(IDE_PRXSRV_SCRIPT);
        }
    }
    else
    {
        if (ti.Type == ttDataFromWindow)
            proxyScript.clear();
    }
    if (ti.Type == ttDataToWindow)
        EnableControls(proxyType != fpstOwnScript, FALSE);
    if (ti.Type == ttDataFromWindow)
    {
        BOOL deallocPassword = FALSE;                                 // FALSE = the password has not changed yet
        BYTE* proxyEncryptedPassword = Proxy->ProxyEncryptedPassword; // it may be only scrambled
        int proxyEncryptedPasswordSize = Proxy->ProxyEncryptedPasswordSize;
        if (!IsWindowVisible(GetDlgItem(HWindow, IDB_PRXSRV_PASSWD_CHANGE)))
        {
            if (proxyPlainPassword.empty()) // store an empty password as NULL
            {
                proxyEncryptedPassword = NULL;
                proxyEncryptedPasswordSize = 0;
            }
            else
            {
                CSalamanderPasswordManagerAbstract* passwordManager = SalamanderGeneral->GetSalamanderPasswordManager();
                BOOL encrypt = saveProxyPassword && passwordManager->IsUsingMasterPassword() && passwordManager->IsMasterPasswordSet();
                BYTE* stagedEncryptedPassword = NULL;
                int stagedEncryptedPasswordSize = 0;
                if (FTPEncryptPasswordW(passwordManager, proxyPlainPassword.c_str(),
                                        &stagedEncryptedPassword,
                                        &stagedEncryptedPasswordSize, encrypt))
                {
                    proxyEncryptedPassword = stagedEncryptedPassword;
                    proxyEncryptedPasswordSize = stagedEncryptedPasswordSize;
                    deallocPassword = TRUE;
                }
                else
                    ti.ErrorOn(IDE_PRXSRV_PASSWD);
            }
        }
        if (ti.IsGood() &&
            !TmpFTPProxyServerList->SetProxyServer(Proxy,
                                                   Proxy->ProxyUID,
                                                   proxyName.c_str(),
                                                   (CFTPProxyServerType)proxyType,
                                                   proxyHost.empty() ? NULL : proxyHost.c_str(),
                                                   proxyPort,
                                                   proxyUser.c_str(),
                                                   proxyEncryptedPassword,
                                                   proxyEncryptedPasswordSize,
                                                   saveProxyPassword,
                                                   GetStrOrNULL(proxyScript.c_str())))
            ti.ErrorOn(IDE_PRXSRV_NAME);

        if (deallocPassword && proxyEncryptedPassword != NULL)
        {
            // release the buffer allocated in EncryptPassword()
            memset(proxyEncryptedPassword, 0, proxyEncryptedPasswordSize);
            SalamanderGeneral->Free(proxyEncryptedPassword);
        }
        FTPSecureWipe(proxyPlainPassword);
    }
}

void CProxyServerDlg::EnableControls(BOOL initScriptText, BOOL initProxyPort)
{
    HWND edit = GetDlgItem(HWindow, IDE_PRXSRV_SCRIPT);
    HWND combo = GetDlgItem(HWindow, IDC_PRXSRV_TYPE);
    int type = (int)SendMessage(combo, CB_GETCURSEL, 0, 0);
    if (type == CB_ERR || type < 0 || type > fpstOwnScript)
        type = fpstSocks4;
    if (initScriptText)
    {
        std::wstring scriptText;
        const char* encodedScript = type == fpstOwnScript ? "Connect to:" : GetProxyScriptText((CFTPProxyServerType)type, TRUE);
        if (FtpDecodeLocalText(encodedScript, scriptText))
            SetWindowTextW(edit, scriptText.c_str());
    }
    if (initProxyPort)
    {
        HWND portEdit = GetDlgItem(HWindow, IDE_PRXSRV_PORT);
        const std::wstring port = std::to_wstring(GetProxyDefaultPort((CFTPProxyServerType)type));
        SetWindowTextW(portEdit, port.c_str());
    }

    DWORD style = GetWindowLong(edit, GWL_STYLE);
    if (type != fpstOwnScript)
    {
        if ((style & ES_READONLY) == 0)
            SendMessage(edit, EM_SETREADONLY, TRUE, 0);
    }
    else
    {
        if ((style & ES_READONLY) != 0)
            SendMessage(edit, EM_SETREADONLY, FALSE, 0);
    }
    BOOL enablePassword = HavePassword((CFTPProxyServerType)type);
    if ((IsWindowEnabled(GetDlgItem(HWindow, IDE_PRXSRV_PASSWD)) != 0) != enablePassword)
        EnableWindow(GetDlgItem(HWindow, IDE_PRXSRV_PASSWD), enablePassword);
    if ((IsWindowEnabled(GetDlgItem(HWindow, IDC_PRXSRV_SAVEPASSWD)) != 0) != enablePassword)
        EnableWindow(GetDlgItem(HWindow, IDC_PRXSRV_SAVEPASSWD), enablePassword);

    BOOL enableHostAndPort = HaveHostAndPort((CFTPProxyServerType)type);
    if ((IsWindowEnabled(GetDlgItem(HWindow, IDE_PRXSRV_ADDRESS)) != 0) != enableHostAndPort)
        EnableWindow(GetDlgItem(HWindow, IDE_PRXSRV_ADDRESS), enableHostAndPort);
    if ((IsWindowEnabled(GetDlgItem(HWindow, IDE_PRXSRV_PORT)) != 0) != enableHostAndPort)
        EnableWindow(GetDlgItem(HWindow, IDE_PRXSRV_PORT), enableHostAndPort);

    EnableWindow(GetDlgItem(HWindow, IDE_PRXSRV_PASSWD_LOCKED), FALSE);
}

LRESULT
CProxyScriptControlWindow::WindowProc(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
    case WM_GETDLGCODE: // ensure the text in the edit box is not constantly selected
    {
        LRESULT ret = CWindow::WindowProc(uMsg, wParam, lParam);
        return (ret & (~DLGC_HASSETSEL));
    }

    case WM_RBUTTONDOWN:
    {
        DWORD ret = (DWORD)SendMessage(HWindow, EM_CHARFROMPOS, 0, lParam);
        DWORD i = (unsigned short)LOWORD(ret);
        if (GetFocus() != HWindow)
        {
            SendMessage(GetParent(HWindow), WM_NEXTDLGCTL, (WPARAM)HWindow, TRUE);
            SendMessage(HWindow, EM_SETSEL, i, i); // always change the caret position
        }
        else
        {
            DWORD start, end;
            SendMessage(HWindow, EM_GETSEL, (WPARAM)&start, (LPARAM)&end);
            if ((i < start || i >= end) && (i < end || i >= start))
                SendMessage(HWindow, EM_SETSEL, i, i); // click outside the selection -> change the caret position
        }
        break;
    }

    case WM_CONTEXTMENU:
    {
        int x = GET_X_LPARAM(lParam);
        int y = GET_Y_LPARAM(lParam);
        if (x == -1 || y == -1)
        {
            POINT p;
            if (GetCaretPos(&p))
            {
                ClientToScreen(HWindow, &p);
                x = p.x;
                y = p.y;
            }
            else
            {
                RECT r;
                GetWindowRect(HWindow, &r);
                r.right = r.left;
                r.bottom = r.top;
                SalamanderGeneral->MultiMonEnsureRectVisible(&r, FALSE);
                x = r.left;
                y = r.top;
            }
        }

        HMENU main = LoadMenu(HLanguage, MAKEINTRESOURCE(IDM_PRXSRVSCRIPTMENU));
        if (main != NULL)
        {
            HMENU subMenu = GetSubMenu(main, 0);
            if (subMenu != NULL)
            {
                BOOL canModify = (GetWindowLong(GetDlgItem(GetParent(HWindow), IDE_PRXSRV_SCRIPT), GWL_STYLE) & ES_READONLY) == 0;
                EnableMenuItem(subMenu, 0, MF_BYPOSITION | (canModify ? MF_ENABLED : MF_DISABLED | MF_GRAYED));
                MyEnableMenuItem(subMenu, CM_PSS_UNDO, canModify && SendMessage(HWindow, EM_CANUNDO, 0, 0));
                DWORD start, end;
                SendMessage(HWindow, EM_GETSEL, (WPARAM)&start, (LPARAM)&end);
                MyEnableMenuItem(subMenu, CM_PSS_CUT, canModify && start != end);
                MyEnableMenuItem(subMenu, CM_PSS_COPY, start != end);
                MyEnableMenuItem(subMenu, CM_PSS_PASTE, canModify && (IsClipboardFormatAvailable(CF_TEXT) || IsClipboardFormatAvailable(CF_UNICODETEXT)));
                MyEnableMenuItem(subMenu, CM_PSS_DELETE, canModify && start != end);
                DWORD len = GetWindowTextLength(HWindow);
                MyEnableMenuItem(subMenu, CM_PSS_SELECTALL, start - end != len && end - start != len);
                DWORD cmd = TrackPopupMenuEx(subMenu, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_RIGHTBUTTON,
                                             x, y, HWindow, NULL);
                int strID = -1;
                switch (cmd)
                {
                case CM_PSS_UNDO:
                    SendMessage(HWindow, EM_UNDO, 0, 0);
                    break;
                case CM_PSS_CUT:
                    SendMessage(HWindow, WM_CUT, 0, 0);
                    break;
                case CM_PSS_COPY:
                    SendMessage(HWindow, WM_COPY, 0, 0);
                    break;
                case CM_PSS_PASTE:
                    SendMessage(HWindow, WM_PASTE, 0, 0);
                    break;
                case CM_PSS_DELETE:
                    SendMessageW(HWindow, EM_REPLACESEL, TRUE, (LPARAM)L"");
                    break;
                case CM_PSS_SELECTALL:
                    SendMessage(HWindow, EM_SETSEL, 0, -1);
                    break;

                case CM_PSS_3XX:
                    strID = 0;
                    break;
                case CM_PSS_PROXYHOST:
                    strID = 1;
                    break;
                case CM_PSS_PROXYPORT:
                    strID = 2;
                    break;
                case CM_PSS_PROXYUSER:
                    strID = 3;
                    break;
                case CM_PSS_PROXYPASSWD:
                    strID = 4;
                    break;
                case CM_PSS_HOST:
                    strID = 5;
                    break;
                case CM_PSS_PORT:
                    strID = 6;
                    break;
                case CM_PSS_USER:
                    strID = 7;
                    break;
                case CM_PSS_PASSWD:
                    strID = 8;
                    break;
                case CM_PSS_ACCOUNT:
                    strID = 9;
                    break;
                }
                if (strID >= 0)
                {
                    const wchar_t* strArr[] = {
                        L"\r\n3xx:",
                        L"$(ProxyHost)",
                        L"$(ProxyPort)",
                        L"$(ProxyUser)",
                        L"$(ProxyPassword)",
                        L"$(Host)",
                        L"$(Port)",
                        L"$(User)",
                        L"$(Password)",
                        L"$(Account)",
                    };
                    if (strID < 10 /* number of strings in strArr - UPDATE !!! */)
                    {
                        const wchar_t* str = strArr[strID];
                        std::wstring text;
                        if (ReadWindowTextOwnedW(HWindow, text))
                        {                                                                                     // skip an inserted EOL
                            DWORD pos = min(start, end);
                            if (pos <= text.size() &&
                                (pos == 0 || pos >= 2 && text[pos - 2] == L'\r' && text[pos - 1] == L'\n')) // start of the line
                            {                                                                                     // skip an inserted EOL
                                if (*str == L'\r')
                                    str++;
                                if (*str == L'\n')
                                    str++;
                            }
                            SendMessageW(HWindow, EM_REPLACESEL, TRUE, reinterpret_cast<LPARAM>(str));
                        }
                    }
                }
            }
            DestroyMenu(main);
        }
        return 0;
    }
    }
    return CWindow::WindowProc(uMsg, wParam, lParam);
}

void CProxyServerDlg::AlignPasswordControls()
{
    // the non-password edit line (with info text) will be moved to where the password edit line starts; it will also be stretched to the button
    RECT editRect;
    GetWindowRect(GetDlgItem(HWindow, IDE_PRXSRV_PASSWD), &editRect);
    RECT editLockedRect;
    GetWindowRect(GetDlgItem(HWindow, IDE_PRXSRV_PASSWD_LOCKED), &editLockedRect);
    int width = editLockedRect.right - editRect.left;
    MapWindowPoints(NULL, HWindow, (POINT*)&editRect, 2);
    SetWindowPos(GetDlgItem(HWindow, IDE_PRXSRV_PASSWD_LOCKED), NULL, editRect.left, editRect.top, width, editLockedRect.bottom - editLockedRect.top, SWP_NOZORDER);

    // the password edit line will be as long as the one above it
    GetWindowRect(GetDlgItem(HWindow, IDE_PRXSRV_USER), &editRect);
    SetWindowPos(GetDlgItem(HWindow, IDE_PRXSRV_PASSWD), NULL, 0, 0, editRect.right - editRect.left, editRect.bottom - editRect.top, SWP_NOMOVE | SWP_NOZORDER);
}

void CProxyServerDlg::ShowHidePasswordControls(BOOL lockedPassword, BOOL focusEdit)
{
    BOOL showUnlockButton = lockedPassword;
    ShowWindow(GetDlgItem(HWindow, IDE_PRXSRV_PASSWD), !showUnlockButton);
    ShowWindow(GetDlgItem(HWindow, IDE_PRXSRV_PASSWD_LOCKED), showUnlockButton);
    ShowWindow(GetDlgItem(HWindow, IDB_PRXSRV_PASSWD_CHANGE), showUnlockButton);

    if (!showUnlockButton && focusEdit)
    {
        SetFocus(GetDlgItem(HWindow, IDE_PRXSRV_PASSWD));
        SendDlgItemMessage(HWindow, IDE_PRXSRV_PASSWD, CB_SETEDITSEL, 0, MAKELPARAM(0, -1));
        SendMessage(HWindow, DM_SETDEFID, IDE_PRXSRV_PASSWD, 0);
    }
}

INT_PTR
CProxyServerDlg::DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    CALL_STACK_MESSAGE4("CProxyServerDlg::DialogProc(0x%X, 0x%IX, 0x%IX)", uMsg, wParam, lParam);
    switch (uMsg)
    {
    case WM_INITDIALOG:
    {
        // move the password edit lines
        AlignPasswordControls();

        // Unlock button: besides the Unlock command it will also have Clear to delete the password
        SalamanderGUI->AttachButton(HWindow, IDB_PRXSRV_PASSWD_CHANGE, BTF_DROPDOWN);

        // we want the password edit line to send WM_APP_SHOWPASSWORD on ctrl+right-click
        CPasswordEditLine* passwordEL = new CPasswordEditLine(HWindow, IDE_PRXSRV_PASSWD);

        CGUIHyperLinkAbstract* hint = SalamanderGUI->AttachHyperLink(HWindow, IDC_PRXSRV_SAVEPASSWD_HINT, STF_HYPERLINK_COLOR | STF_UNDERLINE);
        hint->SetActionPostCommand(IDC_PRXSRV_SAVEPASSWD_HINT);

        // if the user has not set a master password in Salamander, storing passwords is not safe
        CSalamanderPasswordManagerAbstract* passwordManager = SalamanderGeneral->GetSalamanderPasswordManager();
        // if the user uses the password manager, change the "it is not secure" message
        if (passwordManager->IsUsingMasterPassword())
            SetDlgItemTextW(HWindow, IDC_PRXSRV_SAVEPASSWD_HINT,
                            LangStr(IDS_SAVEPASSWORD_PROTECTED).c_str());

        // disable select-all on focus and ensure a custom context menu for the script variables edit
        CProxyScriptControlWindow* wnd = new CProxyScriptControlWindow(HWindow, IDE_PRXSRV_SCRIPT);
        if (wnd != NULL && wnd->HWindow == NULL)
            delete wnd; // attaching failed - it will not deallocate itself
        if (Edit)
            SetWindowTextW(HWindow, LangStr(IDS_EDITPROXYSRVTITLE).c_str());
        break;
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
                // Take the complete password directly from the native edit control.
                const int passwordLength = GetWindowTextLengthW((HWND)wParam);
                std::vector<wchar_t> passwordStorage(static_cast<size_t>(passwordLength) + 1, L'\0');
                GetWindowTextW((HWND)wParam, passwordStorage.data(),
                               static_cast<int>(passwordStorage.size()));
                const std::wstring text = SPLFormatStringOwned(
                    LangStr(IDS_PASSWORDIS).c_str(), passwordStorage.data());
                params.Flags = MSGBOXEX_YESNO | MSGBOXEX_ESCAPEENABLED | MSGBOXEX_DEFBUTTON2 |
                               MSGBOXEX_ICONINFORMATION | MSGBOXEX_SILENT;
                params.Text = text.c_str();
                if (SalamanderGeneral->SalMessageBoxEx(&params) == IDYES)
                    SalamanderGeneral->CopyTextToClipboard(passwordStorage.data(), -1, FALSE, NULL);
                SecureZeroMemory(passwordStorage.data(), passwordStorage.size() * sizeof(wchar_t));
            }
        }
        return 0;
    }

    case WM_USER_BUTTONDROPDOWN:
    {
        if (LOWORD(wParam) == IDB_PRXSRV_PASSWD_CHANGE) // dropdown menu on the Unlock button
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
        switch (LOWORD(wParam))
        {
        case IDC_PRXSRV_TYPE:
        {
            if (HIWORD(wParam) == CBN_SELCHANGE)
                EnableControls(TRUE, TRUE);
            break;
        }

        case IDC_PRXSRV_SAVEPASSWD_HINT:
        {
            // open Salamander help with guidance for the password manager
            SalamanderGeneral->OpenHtmlHelpForSalamander(HWindow, HHCDisplayContext, HTMLHELP_SALID_PWDMANAGER, FALSE);
            break;
        }

        case IDC_PRXSRV_SAVEPASSWD:
        {
            if (HIWORD(wParam) == BN_CLICKED)
            {
                BOOL unlockVisible = IsWindowVisible(GetDlgItem(HWindow, IDB_PRXSRV_PASSWD_CHANGE));
                CSalamanderPasswordManagerAbstract* passwordManager = SalamanderGeneral->GetSalamanderPasswordManager();
                if (IsDlgButtonChecked(HWindow, IDC_PRXSRV_SAVEPASSWD) == BST_CHECKED)
                {
                    // if the user checked "Save password" and the password edit line is currently shown, the user uses the master password and the password is not entered
                    if (!unlockVisible && passwordManager->IsUsingMasterPassword() && !passwordManager->IsMasterPasswordSet())
                    {
                        // ask for the master password
                        if (!passwordManager->AskForMasterPassword(HWindow))
                            CheckDlgButton(HWindow, IDC_PRXSRV_SAVEPASSWD, BST_UNCHECKED); // if the user did not enter a valid master password, restore the checkbox to its previous state
                    }
                }
                else
                {
                    if (unlockVisible && Proxy->ProxyEncryptedPassword == NULL)
                    { // empty password with Save Password turned off -> hide Unlock and show an empty password edit box
                        CTransferInfo ti(HWindow, ttDataToWindow);
                        std::wstring emptyPassword;
                        ti.EditLine(IDE_PRXSRV_PASSWD, emptyPassword, FALSE);

                        ShowHidePasswordControls(FALSE, FALSE);
                    }
                }
            }
            break;
        }

        case CM_CLEARPASSWORD:
        {
            int ret = ShowMessage(HWindow, LangStr(IDS_CLEARPASSWORD_CONFIRMATION).c_str(),
                                                       LangStr(IDS_FTPPLUGINTITLE).c_str(), MB_YESNO | MSGBOXEX_ESCAPEENABLED | /*MB_DEFBUTTON2 | */ MSGBOXEX_ICONQUESTION | MSGBOXEX_SILENT);
            if (ret == IDYES)
            {
                // the user wanted to delete the password
                CTransferInfo ti(HWindow, ttDataToWindow);
                std::wstring emptyPassword;
                ti.EditLine(IDE_PRXSRV_PASSWD, emptyPassword, FALSE);

                // clear the save password checkbox
                BOOL clear = FALSE;
                ti.CheckBox(IDC_PRXSRV_SAVEPASSWD, clear);

                ShowHidePasswordControls(FALSE, TRUE);
            }
            break;
        }

        case CM_UNLOCKPASSWORD:
        case IDB_PRXSRV_PASSWD_CHANGE:
        {
            CSalamanderPasswordManagerAbstract* passwordManager = SalamanderGeneral->GetSalamanderPasswordManager();
            // either the master password is not used, or it is used and already entered, or the user will enter it now
            if (!passwordManager->IsUsingMasterPassword() || passwordManager->IsMasterPasswordSet() || passwordManager->AskForMasterPassword(HWindow))
            {
                // if the master password is used, verify that it can decrypt this password
                std::wstring proxyPlainPassword;
                BOOL havePlainPassword = FALSE;
                if (Proxy->ProxyEncryptedPassword != NULL)
                {
                    havePlainPassword = FTPDecryptPasswordW(
                        passwordManager, Proxy->ProxyEncryptedPassword,
                        Proxy->ProxyEncryptedPasswordSize, &proxyPlainPassword);
                }
                if (!passwordManager->IsUsingMasterPassword() ||
                    Proxy->ProxyEncryptedPassword != NULL && !havePlainPassword)
                {
                    int ret = ShowMessage(HWindow, LangStr(IDS_CANNOT_DECRYPT_PASSWORD_DELETE).c_str(),
                                                               LangStr(IDS_FTPERRORTITLE).c_str(), MB_YESNO | MSGBOXEX_ESCAPEENABLED | MB_DEFBUTTON2 | MB_ICONEXCLAMATION);
                    if (ret == IDNO)
                        break;
                    // the user wanted to delete the password
                    CTransferInfo ti(HWindow, ttDataToWindow);
                    std::wstring empty;
                    ti.EditLine(IDE_PRXSRV_PASSWD, empty);
                    // clear the save password checkbox
                    BOOL clear = FALSE;
                    ti.CheckBox(IDC_PRXSRV_SAVEPASSWD, clear);
                }
                if (havePlainPassword || Proxy->ProxyEncryptedPassword == NULL)
                {
                    // insert the decrypted password into the edit line
                    CTransferInfo ti(HWindow, ttDataToWindow);
                    std::wstring empty;
                    ti.EditLine(IDE_PRXSRV_PASSWD,
                                Proxy->ProxyEncryptedPassword == NULL ? empty : proxyPlainPassword);
                    FTPSecureWipe(proxyPlainPassword);
                }
                ShowHidePasswordControls(FALSE, TRUE);
            }
            break;
        }
        }
        break;
    }
    }
    return CCenteredDialog::DialogProc(uMsg, wParam, lParam);
}
