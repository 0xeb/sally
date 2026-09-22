// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"

//
// ****************************************************************************
// CPluginFSInterface
//

BOOL CPluginFSInterface::TryCloseOrDetach(BOOL forceClose, BOOL canDetach, BOOL& detach, int reason)
{
    detach = FALSE;
    BOOL ret = TRUE;
    BOOL close = TRUE;
    if (!CalledFromDisconnectDialog &&                // when called from the Disconnect dialog there is no point in asking about anything
        !forceClose &&                                // when forced there is no point in asking about anything
        reason != FSTRYCLOSE_UNLOADCLOSEFS &&         // when unloading the plugin there is no point in asking (the FS in the panel is idle, not transferring, etc.)
        reason != FSTRYCLOSE_UNLOADCLOSEDETACHEDFS && // when unloading the plugin there is no point in asking (the detached FS is idle, not transferring, etc.)
        reason != FSTRYCLOSE_PLUGINCLOSEDETACHEDFS)   // disconnecting (from the context menu in Alt+F1/F2) a detached FS, there is no point in asking about anything

    {
        if (reason == FSTRYCLOSE_CHANGEPATH)
        {
            if (canDetach && !Config.DisconnectCommandUsed) // "close or detach" and it is not the "Disconnect" command
            {
                int res;
                if (!Config.AlwaysNotCloseCon)
                {
                    MSGBOXEX_PARAMS params;
                    const std::wstring caption = LangStr(IDS_FTPPLUGINTITLE);
                    const std::wstring text = LangStr(IDS_CLOSECONINPANEL);
                    const std::wstring checkBoxText = LangStr(IDS_ALWAYSNOTCLOSE);
                    memset(&params, 0, sizeof(params));
                    params.HParent = SalamanderGeneral->GetMsgBoxParent();
                    params.Flags = MSGBOXEX_YESNOCANCEL | MSGBOXEX_ICONQUESTION | MSGBOXEX_SILENT | MSGBOXEX_HINT;
                    params.Caption = caption.c_str();
                    params.Text = text.c_str();
                    params.CheckBoxText = checkBoxText.c_str();
                    params.CheckBoxValue = &Config.AlwaysNotCloseCon;
                    /* used by the export_mnu.py script that generates salmenu.mnu for the Translator
   we let the message box buttons handle hotkey collisions by simulating that this is a menu
MENU_TEMPLATE_ITEM MsgBoxButtons[] = 
{
  {MNTT_PB, 0
  {MNTT_IT, IDS_DISCONNECTBUTTON
  {MNTT_IT, IDS_KEEPCONBUTTON
  {MNTT_PE, 0
};
*/
                    const std::wstring aliasBtnNames = SPLFormatStringOwned(
                        L"%d\t%s\t%d\t%s", DIALOG_YES, LangStr(IDS_DISCONNECTBUTTON).c_str(),
                        DIALOG_NO, LangStr(IDS_KEEPCONBUTTON).c_str());
                    params.AliasBtnNames = aliasBtnNames.c_str();
                    res = SalamanderGeneral->SalMessageBoxEx(&params);
                    // redraw the main window (so the user does not stare at the rest of the dialog during the entire disconnect)
                    UpdateWindow(SalamanderGeneral->GetMainWindowHWND());
                }
                else
                    res = IDNO;

                detach = (res == IDNO);
                ret = (res != IDCANCEL);
                close = (res == IDYES);
            }
            else
            {
                int res = IDYES;
                if (Config.DisconnectCommandUsed && !Config.AlwaysDisconnect && !IsDetached)
                {
                    MSGBOXEX_PARAMS params;
                    const std::wstring caption = LangStr(IDS_FTPPLUGINTITLE);
                    const std::wstring text = LangStr(IDS_WANTDISCONNECT);
                    const std::wstring checkBoxText = LangStr(IDS_ALWAYSCLOSE);
                    memset(&params, 0, sizeof(params));
                    params.HParent = SalamanderGeneral->GetMsgBoxParent();
                    params.Flags = MSGBOXEX_YESNO | MSGBOXEX_ESCAPEENABLED |
                                   MSGBOXEX_ICONQUESTION | MSGBOXEX_SILENT | MSGBOXEX_HINT;
                    params.Caption = caption.c_str();
                    params.Text = text.c_str();
                    params.CheckBoxText = checkBoxText.c_str();
                    params.CheckBoxValue = &Config.AlwaysDisconnect;
                    res = SalamanderGeneral->SalMessageBoxEx(&params);
                    // redraw the main window (so the user does not stare at the rest of the dialog during the entire disconnect)
                    UpdateWindow(SalamanderGeneral->GetMainWindowHWND());
                }
                close = ret = (res == IDYES);
            }
        }
        else
        {
            if (reason == FSTRYCLOSE_CHANGEPATHFAILURE || reason == FSTRYCLOSE_ATTACHFAILURE)
            {
                close = ret = TRUE; // this prompt proved to be very confusing and incomprehensible, so we simply prefer Disconnect
                                    /*
        // "no path on the FTP is accessible, disconnect?"
        close = ret = (SalamanderGeneral->SalMessageBox(SalamanderGeneral->GetMsgBoxParent(),
                                                        LoadStr(IDS_NOPATHACCESSINPANEL),
                                                        LoadStr(IDS_FTPPLUGINTITLE),
                                                        MB_YESNO | MSGBOXEX_ESCAPEENABLED |
                                                        MSGBOXEX_ICONQUESTION) == IDYES);
        // redraw the main window (so the user does not stare at the rest of the dialog during the entire disconnect)
        UpdateWindow(SalamanderGeneral->GetMainWindowHWND());
*/
            }
        }
    }

    if (close && ControlConnection != NULL && ControlConnection->IsConnected())
    { // we close the connection here so that in the case of a new connection to the same server
        // with a single-connection limit there is a chance of success (with this connection it would already be
        // two per server -> the server would reject it due to its limit)
        ControlConnection->CloseControlConnection(SalamanderGeneral->GetMsgBoxParent());
    }
    return ret;
}

void CPluginFSInterface::Event(int event, DWORD param)
{
    switch (event)
    {
    case FSE_DETACHED:
    {
        if (ControlConnection != NULL)
            ControlConnection->LogMessage(LangStr(IDS_LOGMSGDETACHED).c_str(), -1, TRUE);
        Logs.RefreshListOfLogsInLogsDlg();
        IsDetached = TRUE;
        break;
    }

    case FSE_ACTIVATEREFRESH:
    {
        if (RefreshPanelOnActivation)
        {
            RefreshPanelOnActivation = FALSE;
            SalamanderGeneral->PostRefreshPanelFS(this); // perform a refresh when the Salamander main window gets activated
        }

        CPluginFSInterface* fs = (CPluginFSInterface*)SalamanderGeneral->GetPanelPluginFS(PANEL_SOURCE);
        if (fs == this)
        {
            ActivateWelcomeMsg();
            if (Config.AlwaysShowLogForActPan)
                Logs.ActivateLog(GetLogUID()); // after activating the worker connection log from the operation dialog, the panel log must be reactivated when switching back to the panel
        }
        break;
    }

    case FSE_ATTACHED:
        if (ControlConnection != NULL)
            ControlConnection->LogMessage(LangStr(IDS_LOGMSGATTACHED).c_str(), -1, TRUE);
        // break;  // should not be here
    case FSE_OPENED:
    {
        RefreshValuesOfPanelCtrlCon();
        Logs.RefreshListOfLogsInLogsDlg();
        if (IsDetached && Config.AlwaysShowLogForActPan)
            Logs.ActivateLog(GetLogUID());
        IsDetached = FALSE;
        break;
    }

    case FSE_CLOSEORDETACHCANCELED:
    {
        if (Config.AlwaysShowLogForActPan)
        {
            CPluginFSInterface* fs = (CPluginFSInterface*)SalamanderGeneral->GetPanelPluginFS(PANEL_SOURCE);
            if (fs != NULL)
                Logs.ActivateLog(fs->GetLogUID());
        }
        break;
    }

    case FSE_TIMER: // delayed panel refresh (to give the connection time to return from the operation dialog to the panel)
    {
        DWORD paramLimit = 4; // maximum one second waiting for the connection (we wait to see whether the worker will try to hand over the connection)
        DWORD ti;
        if (ControlConnection != NULL &&
            ControlConnection->GetIsSocketConnectedLastCallTime(&ti) && GetTickCount() - ti < 5000)
        {
            paramLimit = 24; // maximum five seconds waiting for the connection (we know the worker is trying to hand it over)
        }
        if (param >= paramLimit || // param < paramLimit: start the refresh only if we have the connection, param >= paramLimit always allows the refresh
            ControlConnection != NULL && ControlConnection->IsConnected())
        {
            // we need the refresh even in an inactive Salamander (people complained that listing retrieval starts when Salamander gets focus and that it should have finished long ago) + delay the refresh until the control connection
            // is handed back from the operation to the panel => post a menu command and perform a hard refresh there
            CPluginFSInterface* fsLeft = (CPluginFSInterface*)SalamanderGeneral->GetPanelPluginFS(PANEL_LEFT);
            CPluginFSInterface* fsRight = (CPluginFSInterface*)SalamanderGeneral->GetPanelPluginFS(PANEL_RIGHT);
            if (fsLeft == this || fsRight == this)
                SalamanderGeneral->PostMenuExtCommand(fsLeft == this ? FTPCMD_REFRESHLEFTPANEL : FTPCMD_REFRESHRIGHTPANEL, TRUE); // wait for "sal-idle"
        }
        else
            SalamanderGeneral->AddPluginFSTimer(200, this, param + 1);
        break;
    }

    case FSE_PATHCHANGED:
    {
        const wchar_t* pToolTip = NULL;
        std::wstring toolTip;
        BOOL bLocked = FALSE, bShow = FALSE;

        if (ControlConnection)
        {
            CCertificate* cert = ControlConnection->GetCertificate();
            if (cert != NULL)
            {
                bShow = TRUE;
                bLocked = cert->IsVerified();
                // LoadStr(...)+ToWideArg was a double round trip -
                // SalamanderGeneral->LoadStr is the real wide source LoadStr narrows.
                toolTip = SPLLoadStrOwned(SalamanderGeneral, HLanguage,
                                          bLocked ? IDS_SSL_SECURITY_OK : IDS_SSL_SECURITY_UNVERIFIED);
                pToolTip = toolTip.c_str();
                cert->Release();
            }
        }
        SalamanderGeneral->ShowSecurityIcon(param, bShow, bLocked, pToolTip);
        break;
    }
    }
}

void CPluginFSInterface::ReleaseObject(HWND parent)
{
    if (ControlConnection != NULL)
    {
        if (ControlConnection->IsConnected())
        {
            ControlConnection->CloseControlConnection(SalamanderGeneral->GetMsgBoxParent());
        }
        else
        {
            // if we have not yet written what led to closing the connection, do it now
            // at least into the log (there is no point in showing a window, the user is probably no longer interested)
            ControlConnection->CheckCtrlConClose(TRUE, FALSE, parent, TRUE);
        }

        // if the closed FS is in a panel, LeftPanelCtrlCon or RightPanelCtrlCon needs to be cleared
        HANDLES(EnterCriticalSection(&PanelCtrlConSect));
        if (LeftPanelCtrlCon == ControlConnection)
            LeftPanelCtrlCon = NULL;
        if (RightPanelCtrlCon == ControlConnection)
            RightPanelCtrlCon = NULL;
        HANDLES(LeaveCriticalSection(&PanelCtrlConSect));

        DeleteSocket(ControlConnection);
        ControlConnection = NULL;
        Logs.RefreshListOfLogsInLogsDlg();
    }
}

DWORD
CPluginFSInterface::GetSupportedServices()
{
    return FS_SERVICE_CONTEXTMENU |
           //FS_SERVICE_SHOWPROPERTIES |
           FS_SERVICE_CHANGEATTRS |
           FS_SERVICE_COPYFROMDISKTOFS |
           FS_SERVICE_MOVEFROMDISKTOFS |
           FS_SERVICE_MOVEFROMFS |
           FS_SERVICE_COPYFROMFS |
           FS_SERVICE_DELETE |
           FS_SERVICE_VIEWFILE |
           FS_SERVICE_CREATEDIR |
           FS_SERVICE_ACCEPTSCHANGENOTIF |
           FS_SERVICE_QUICKRENAME |
           //FS_SERVICE_COMMANDLINE |
           //FS_SERVICE_SHOWINFO |
           //FS_SERVICE_GETFREESPACE |
           FS_SERVICE_GETFSICON |
           FS_SERVICE_GETNEXTDIRLINEHOTPATH |
           FS_SERVICE_GETCHANGEDRIVEORDISCONNECTITEM |
           FS_SERVICE_GETPATHFORMAINWNDTITLE |
           FS_SERVICE_SHOWSECURITYINFO;
}

BOOL CPluginFSInterface::GetChangeDriveOrDisconnectItem(const wchar_t* fsName, wchar_t*& title, HICON& icon, BOOL& destroyIcon) try
{
    std::wstring userPartText;
    if (!BuildUserPartText(userPartText, ""))
        return FALSE;

    std::wstring text = L"\t";
    text.append(fsName != NULL ? fsName : L"");
    text.push_back(L':');
    text.append(userPartText);
    if (!text.empty() && text.back() == L'/')
        text.pop_back(); // trim '/' from the end of the path (it must be "relative")
    for (size_t at = 0; (at = text.find(L'&', at)) != std::wstring::npos; at += 2)
        text.insert(at, 1, L'&');
    title = SalamanderGeneral->DupStr(text.c_str());
    if (title == NULL)
        return FALSE; // low memory, no item will be added

    icon = FTPIcon;
    destroyIcon = FALSE;
    return TRUE;
}
catch (...)
{
    SetLastError(ERROR_NOT_ENOUGH_MEMORY);
    return FALSE;
}

HICON
CPluginFSInterface::GetFSIcon(BOOL& destroyIcon)
{
    destroyIcon = FALSE;
    return FTPIcon;
}

BOOL CPluginFSInterface::GetNextDirectoryLineHotPath(const wchar_t* text, int pathLen, int& offset)
{
    if (DirLineHotPathType == ftpsptEmpty && ControlConnection != NULL)
    { // based on the current path in the FS determine the path type in the Directory Line
        DirLineHotPathType = ControlConnection->GetFTPServerPathType(Path.c_str());
        DirLineHotPathUserLength = FTPGetUserLengthW(User.c_str());
    }

    const wchar_t* end = text + pathLen;
    const wchar_t* root = text; // pointer past the root path
    while (*root != 0 && *root != L':')
        root++; // skip the fs-name
    if (*root == L':')
    {
        root = FTPFindPathW(root + 1, DirLineHotPathUserLength);
        if (*root == L'/' || *root == L'\\')
            root++;                          // skip the first '/' or '\\' from the path
        if (DirLineHotPathType == ftpsptOS2) // for OS/2 paths skip the disk root ("C:/") as well
        {
            while (root < end && *root != L'/' && *root != L'\\')
                root++;
            if (*root == L'/' || *root == L'\\')
                root++; // skip '/' or '\\' from the disk root path
        }
        if (DirLineHotPathType == ftpsptTandem) // for Tandem paths skip the "system" ("\\SYSTEM"), a shorter path makes no sense
            while (root < end && *root != L'.')
                root++;
    }
    const wchar_t* s = text + offset;
    if (s >= end)
    {
        DirLineHotPathType = ftpsptEmpty; // end of hot-text detection
        DirLineHotPathUserLength = 0;
        return FALSE;
    }
    if (s < root)
        offset = (int)(root - text);
    else
    {
        switch (DirLineHotPathType)
        {
        case ftpsptOpenVMS: // "PUB$DEVICE:[PUB.VMS.DIR2]"
        {                   // the separator is a dot, "^." is an escape sequence for '.'
            if (*s == L'.')
                s++;
            BOOL vmsEsc = FALSE;
            while (s < end)
            {
                if (*s == L'^')
                    vmsEsc = !vmsEsc;
                else
                {
                    if (vmsEsc)
                        vmsEsc = FALSE;
                    else
                    {
                        if (*s == L'.')
                            break;
                    }
                }
                s++;
            }
            if (*s == L'.' && *(s + 1) == L']')
                s = end;
            break;
        }

        case ftpsptIBMz_VM: // "ACADEM:ANONYMOU.PICS" (+the root must end with a dot: "ACADEM:ANONYMOU.")
        case ftpsptMVS:     // "'VEA0016.MAIN.CLIST'"
        case ftpsptTandem:  // \SYSTEM.$VVVVV.SUBVOLUM.FILENAME
        {                   // the separator is a dot
            if (*s == L'.')
                s++;
            while (s < end && *s != L'.')
                s++;
            if (*s == L'.')
            {
                if (DirLineHotPathType == ftpsptMVS && *(s + 1) == L'\'')
                    s = end;
                else
                {
                    if (DirLineHotPathType == ftpsptIBMz_VM)
                    {
                        const wchar_t* z_VMRootDir = root; // position of the first dot (root) in IBM_z/VM paths
                        while (z_VMRootDir < end && *z_VMRootDir != L'.')
                            z_VMRootDir++;
                        if (s == z_VMRootDir)
                            s++; // the root must end with a dot, other paths probably do not
                    }
                }
            }
            break;
        }

        case ftpsptNetware: // "/pub/altap/sally" or "\pub\altap\sally"
        case ftpsptWindows: // "/pub/altap/sally" or "\pub\altap\sally"
        case ftpsptOS2:     // e.g. C:/DIR1/DIR2 or C:\DIR1\DIR2
        {                   // the separators are '/' and '\\'
            if (*s == L'/' || *s == L'\\')
                s++;
            while (s < end && *s != L'/' && *s != L'\\')
                s++;
            break;
        }

        default: // Unix and others: "/pub/altap/sally" (but also "/\dir-with-backslash")
        {        // the separator is '/'
            if (*s == L'/')
                s++;
            while (s < end && *s != L'/')
                s++;
            break;
        }
        }
        offset = (int)(s - text);
    }
    if (offset == pathLen)
    {
        DirLineHotPathType = ftpsptEmpty; // end of hot-text detection
        DirLineHotPathUserLength = 0;
    }
    return TRUE;
}

BOOL CPluginFSInterface::CompleteDirectoryLineHotPath(CSalamanderStringBuffer* path)
{
    if (path == NULL)
        return FALSE;
    std::wstring pathText;
    if (!sally::plugin_abi::ReadStringBuffer(*path, pathText))
        return FALSE;
    if (ControlConnection != NULL)
    { // based on the current path in the FS determine the path type in the Directory Line
        CFTPServerPathType type = ControlConnection->GetFTPServerPathType(Path.c_str());
        if (type == ftpsptOpenVMS || type == ftpsptMVS) // only OpenVMS and MVS need to add the "bracket"
        {
            // obtain a pointer past the root path
            const wchar_t* root = pathText.c_str();
            while (*root != 0 && *root != L':')
                root++; // skip the fs-name
            if (*root == L':')
            {
                root = FTPFindPathW(root + 1, FTPGetUserLengthW(User.c_str()));
                if (*root == L'/' || *root == L'\\')
                    root++; // skip the first '/' or '\\' from the path
            }
            const size_t pathLen = pathText.size();

            // check the "bracket" and add it if missing
            if (type == ftpsptOpenVMS) // OpenVMS - the bracket is ']'
            {
                const wchar_t* s = root;
                BOOL vmsEsc = FALSE;
                while (*s != 0)
                {
                    if (*s == L'^')
                        vmsEsc = !vmsEsc;
                    else
                    {
                        if (vmsEsc)
                            vmsEsc = FALSE;
                        else
                        {
                            if (*s == L'[')
                                break;
                        }
                    }
                    s++;
                }
                if (*s == L'[')
                {
                    if (pathLen == 0 || pathText[pathLen - 1] != L']' ||
                        FTPIsVMSEscapeSequenceW(pathText.c_str(), pathText.c_str() + (pathLen - 1)))
                    {
                        pathText.push_back(L']');
                    }
                }
            }
            else // MVS - the bracket is '\''
            {
                if (wcschr(root, L'\'') != NULL &&
                    (pathLen == 0 || pathText[pathLen - 1] != L'\''))
                    pathText.push_back(L'\'');
            }
        }
    }
    return sally::plugin_abi::WriteStringBuffer(*path, pathText) ? TRUE : FALSE;
}

BOOL CPluginFSInterface::GetPathForMainWindowTitle(const wchar_t* fsName, int mode, CSalamanderStringBuffer* buf) try
{
    if (fsName == NULL || buf == NULL ||
        !sally::plugin_abi::IsValidStringBuffer(*buf))
        return FALSE;
    CFTPServerPathType pathType = ControlConnection != NULL ? ControlConnection->GetFTPServerPathType(Path.c_str()) : ftpsptUnknown;
    BOOL needFullPath = FALSE;
    const size_t sourceLength = Path.size();
    if (sourceLength > (std::numeric_limits<int>::max)() - 8)
        return FALSE;
    std::vector<char> path(sourceLength + 8, '\0');
    std::vector<char> result(sourceLength + 1, '\0');
    memcpy(path.data(), Path.c_str(), sourceLength + 1);
    auto publishByteText = [&](const std::string& bytes, BOOL includeFSName) -> BOOL
    {
        std::wstring title;
        if (includeFSName)
        {
            if (!BuildFullPathText(fsName, bytes.c_str(), title))
                return FALSE;
        }
        else if (!DecodeSessionBytes(bytes, title))
            return FALSE;
        return sally::plugin_abi::WriteStringBuffer(*buf, title)
                   ? TRUE
                   : FALSE;
    };
    if (mode == 1) // "Directory Name Only" mode
    {
        memcpy(path.data(), Path.c_str(), sourceLength + 1);
        if (pathType == ftpsptUnknown)
            pathType = ftpsptUnix;
        if (FTPCutDirectory(pathType, path.data(), static_cast<int>(path.size()),
                            result.data(), static_cast<int>(result.size()), NULL)) // trimming succeeded
        {
            return publishByteText(std::string(result.data()), FALSE);
        }
        else // it is the root or some other error occurred (unknown path type)
        {
            needFullPath = TRUE;
        }
    }
    else
    {
        if (mode == 2) // "Shortened Path" mode
        {
            char* trimStart = NULL;
            char* trimEnd = NULL;
            memcpy(path.data(), Path.c_str(), sourceLength + 1);
            char* const pathData = path.data();
            switch (pathType)
            {
            case ftpsptIBMz_VM: // "ACADEM:ANONYMOU.PICS" (+the root must end with a dot: "ACADEM:ANONYMOU.")
            {                   // the separator is a dot
                trimStart = strchr(pathData, '.');
                char* s = pathData + strlen(pathData) - 1; // ignore the last character (if it is a dot we skip it anyway)
                while (--s > pathData && *s != '.')
                    ;
                if (s > pathData)
                    trimEnd = s + 1;
                if (trimStart + 1 == trimEnd)
                    trimEnd = NULL; // only one dot in the path -> do not trim
                break;
            }

            case ftpsptOpenVMS: // "PUB$DEVICE:[PUB.VMS.DIR2]"
            {                   // the separator is a dot, "^." is an escape sequence for '.'
                char* s = pathData;
                BOOL vmsEsc = FALSE;
                while (*s != 0)
                {
                    if (*s == '^')
                        vmsEsc = !vmsEsc;
                    else
                    {
                        if (vmsEsc)
                            vmsEsc = FALSE;
                        else
                        {
                            if (*s == '[')
                                break;
                        }
                    }
                    s++;
                }
                if (*s == '[')
                    trimStart = s + 1;

                s = pathData + strlen(pathData) - 1;
                if (s > pathData && *(s - 1) == '.')
                    s--; // the last character must be ']', we do not test it and skip the last '.'
                while (--s > pathData && (*s != '.' || FTPIsVMSEscapeSequence(pathData, s)))
                    ; // non-escaped '.'
                if (s > pathData)
                    trimEnd = s + 1;
                if (trimStart + 1 == trimEnd)
                    trimEnd = NULL; // e.g. "[.pub]" -> do not trim
                break;
            }

            case ftpsptMVS: // "'VEA0016.MAIN.CLIST'"
            {               // the separator is a dot
                trimStart = strchr(pathData, '\'');
                if (trimStart != NULL)
                    trimStart++;
                char* s = pathData + strlen(pathData) - 1;
                if (s > pathData && *(s - 1) == '.')
                    s--; // the last character must be '\'', we do not test it and skip the last '.'
                while (--s > pathData && *s != '.')
                    ;
                if (s > pathData)
                    trimEnd = s + 1;
                if (trimStart + 1 == trimEnd)
                    trimEnd = NULL; // e.g. "'.pub'" -> do not trim
                break;
            }

            case ftpsptNetware: // "/pub/altap/sally" or "\pub\altap\sally"
            case ftpsptWindows: // "/pub/altap/sally" or "\pub\altap\sally"
            {                   // the separators are '/' and '\\'
                if (path[0] == '/' || path[0] == '\\')
                    trimStart = pathData + 1;
                else
                    trimStart = pathData;
                char* s = pathData + strlen(pathData) - 1; // do not consider '/' or '\\' at the end of the string
                while (--s > pathData && *s != '/' && *s != '\\')
                    ;
                if (s > pathData)
                    trimEnd = s;
                break;
            }

            case ftpsptOS2: // e.g. C:/DIR1/DIR2 or C:\DIR1\DIR2
            {
                if (path[0] != 0 && path[1] == ':')
                {
                    trimStart = pathData + 2;
                    if (*trimStart == '/' || *trimStart == '\\')
                        trimStart++;
                }
                else
                    trimStart = pathData;
                char* s = pathData + strlen(pathData) - 1; // do not consider '/' or '\\' at the end of the string
                while (--s > pathData && *s != '/' && *s != '\\')
                    ;
                if (s > pathData)
                    trimEnd = s;
                break;
            }

            case ftpsptTandem: // \SYSTEM.$VVVVV.SUBVOLUM.FILENAME
            {
                if (path[0] == '/' || path[0] == '\\')
                    trimStart = pathData + 1;
                else
                    trimStart = pathData;
                char* s = pathData + strlen(pathData) - 1; // do not consider '.' at the end of the string
                while (--s > pathData && *s != '.')
                    ;
                if (s > pathData)
                    trimEnd = s + 1;
                break;
            }

            default: // Unix and others: "/pub/altap/sally" (but also "/\dir-with-backslash")
            {        // the separator is '/'
                if (path[0] == '/')
                    trimStart = pathData + 1;
                else
                    trimStart = pathData;
                char* s = pathData + strlen(pathData) - 1; // do not consider '/' at the end of the string
                while (--s > pathData && *s != '/')
                    ;
                if (s > pathData)
                    trimEnd = s;
                break;
            }
            }
            // trim the path
            if (trimStart != NULL && trimEnd != NULL && trimEnd > trimStart)
            {
                memmove(trimStart + 3, trimEnd, strlen(trimEnd) + 1);
                memcpy(trimStart, "...", 3);
                return publishByteText(std::string(path.data()), TRUE);
            }
            else
                needFullPath = TRUE;
        }
    }
    if (needFullPath)
    {
        return publishByteText(Path, TRUE);
    }
    SetLastError(ERROR_SUCCESS);
    return FALSE; // the path can be created by the standard algorithm
}
catch (...)
{
    SetLastError(ERROR_NOT_ENOUGH_MEMORY);
    return FALSE;
}

void CPluginFSInterface::AcceptChangeOnPathNotification(const wchar_t* fsName, const wchar_t* path, BOOL includingSubdirs) try
{
    // WARNING: in 'includingSubdirs' for FTP paths 0x02 is ORed in when it is a "soft refresh"

    const size_t pathLength = wcslen(path);
    BOOL isFTP = pathLength > AssignedFSName.size() &&
                 SalamanderGeneral->StrNICmp(path, AssignedFSName.c_str(), (int)AssignedFSName.size()) == 0 && // this is our fs-name for FTP
                 path[AssignedFSName.size()] == L':';                                                           // our fs-name is not just a prefix
    BOOL isFTPS = pathLength > AssignedFSNameFTPS.size() &&
                  SalamanderGeneral->StrNICmp(path, AssignedFSNameFTPS.c_str(), (int)AssignedFSNameFTPS.size()) == 0 && // this is our fs-name for FTPS
                  path[AssignedFSNameFTPS.size()] == L':';                                                             // our fs-name is not just a prefix

    if (isFTP || isFTPS)
    {
        // test whether the user-part paths match or at least share the same prefix
        const size_t assignedFSNameLength = isFTP ? AssignedFSName.size() : AssignedFSNameFTPS.size();
        const std::wstring path1(path + assignedFSNameLength + 1);
        std::wstring path2W;
        if (!BuildUserPartText(path2W))
            return;
        int userLength = FTPGetUserLengthW(User.c_str());
        if (FTPHasTheSameRootPathW(path1.c_str(), path2W.c_str(), userLength)) // same root (connection)
        {
            const wchar_t* p1 = FTPFindPathW(path1.c_str(), userLength);
            const wchar_t* p2 = FTPFindPathW(path2W.c_str(), userLength);
            CFTPServerPathType type = GetFTPServerPathType(Path.c_str());
            if (FTPIsPrefixOfServerPathW(type, p1, p2, !(includingSubdirs & 0x01))) // prefix matches or it is an exact match
            {
                NextRefreshWontClearCache = TRUE;
                if (includingSubdirs & 0x02 /* soft refresh */) // perform the refresh once the panel activates
                {
                    if (GetForegroundWindow() != SalamanderGeneral->GetMainWindowHWND()) // Salamander main window is inactive
                        RefreshPanelOnActivation = TRUE;                                 // the refresh will run when FSE_ACTIVATEREFRESH is received
                    else
                        SalamanderGeneral->PostRefreshPanelFS(this); // Salamander main window active: refresh the FS in the panel
                }
                else // refresh immediately (do not wait for the panel to activate)
                {
                    SalamanderGeneral->AddPluginFSTimer(100, this, 0); // delay the panel refresh so the connection can return from the operation dialog (after finishing the operation a path-change notification is sent and the dialog closes)
                }
            }
        }
    }
}
catch (...)
{
    SetLastError(ERROR_NOT_ENOUGH_MEMORY);
}

void CPluginFSInterface::AddBookmark(HWND parent)
{
    std::wstring name = Host;
    std::wstring pathText;
    if (!DecodeSessionBytes(Path, pathText))
        return;
    if (CRenameDlg(parent, name, FALSE, TRUE).Execute() == IDOK)
    {
        BOOL anonymous = User == L"anonymous";
        if (Config.FTPServerList.AddServer(name.c_str(),
                                           Host.c_str(),
                                           pathText.c_str(),
                                           anonymous,
                                           anonymous ? NULL : User.c_str(),
                                           NULL,
                                           0,
                                           FALSE,
                                           -2,
                                           NULL,
                                           NULL,
                                           0,
                                           Port))
        {
            // bookmark added -> let the user place the new bookmark in the bookmark list
            CConnectDlg dlg(parent, 2);
            if (dlg.IsGood())
                dlg.Execute();
        }
    }
}

void CPluginFSInterface::SendUserFTPCommand(HWND parent)
{
    CSendFTPCommandDlg dlg(parent);
    if (ControlConnection != NULL && // (always true)
        dlg.Execute() == IDOK)
    {
        // store the path for which we will report a change after the command is executed
        const std::wstring& fsName = ControlConnection->GetEncryptControlConnection() == 1 ? AssignedFSNameFTPS : AssignedFSName;
        std::wstring changedPath;
        if (!BuildFullPathText(fsName.c_str(), Path.c_str(), changedPath))
            return;

        const CFtpTextCodec commandCodec = ControlConnection->GetTextCodec();
        std::string commandBytes;
        CFTPSecureByteStringGuard commandBytesGuard(commandBytes);
        const CFtpTextEncodeStatus commandStatus = commandCodec.EncodeWithStatus(
            dlg.Command.data(), dlg.Command.size(), commandBytes);
        if (commandStatus == CFtpTextEncodeStatus::InvalidInput)
        {
            SalamanderGeneral->SalMessageBox(parent,
                                             LangStr(IDS_FTPCOMMAND_CANNOTENCODE).c_str(),
                                             LangStr(IDS_FTPERRORTITLE).c_str(),
                                             MB_OK | MB_ICONEXCLAMATION);
            return;
        }

        std::wstring commandForLog;
        CFTPSecureWideStringGuard commandForLogGuard(commandForLog);
        BOOL commandForLogReady = FALSE;
        try
        {
            if (Config.SendSecretCommand)
            {
                const std::wstring secretCommandText = LangStr(IDS_SECRETFTPCOMMAND);
                commandForLogReady = FtpStoreWideText(secretCommandText, commandForLog);
            }
            else
                commandForLogReady = FtpStoreWideText(dlg.Command, commandForLog);
        }
        catch (...)
        {
        }
        if (!commandForLogReady)
        {
            SalamanderGeneral->SalMessageBox(parent,
                                             LangStr(IDS_OPERDOPPR_LOWMEM).c_str(),
                                             LangStr(IDS_FTPERRORTITLE).c_str(),
                                             MB_OK | MB_ICONEXCLAMATION);
            return;
        }
        std::string commandForLogBytes;
        CFTPSecureByteStringGuard commandForLogBytesGuard(commandForLogBytes);
        std::string cmdBuf;
        CFTPSecureByteStringGuard cmdBufGuard(cmdBuf);
        std::string logBuf;
        CFTPSecureByteStringGuard logBufGuard(logBuf);
        std::wstring logMessage;
        const BOOL logCommandReady = Config.SendSecretCommand
                                         ? (commandCodec.Encode(commandForLog.data(), commandForLog.size(), commandForLogBytes) ||
                                            FtpStoreProtocolBytes("<secret command>", commandForLogBytes))
                                         : FtpStoreProtocolBytes(commandBytes, commandForLogBytes);
        if (commandStatus != CFtpTextEncodeStatus::Success || !logCommandReady ||
            !FTPBuildUserCommandBytes(commandBytes, commandForLogBytes, cmdBuf, logBuf) ||
            !FtpFormatWideText(LangStr(IDS_LOGMSGSENDINGUSRCMD).c_str(),
                               commandForLog, logMessage))
        {
            SalamanderGeneral->SalMessageBox(parent,
                                             LangStr(IDS_OPERDOPPR_LOWMEM).c_str(),
                                             LangStr(IDS_FTPERRORTITLE).c_str(),
                                             MB_OK | MB_ICONEXCLAMATION);
            return;
        }
        ControlConnection->LogMessage(logMessage.c_str(), static_cast<int>(logMessage.size()), TRUE);

        TotalConnectAttemptNum = 1; // user-initiated action -> if reconnection is needed, this is the first reconnect attempt
        const std::string* retryMessageToUse = NULL;
        BOOL canRetry = FALSE;
        std::string nextRetryMessage;

        BOOL commandSent = FALSE;
        BOOL reconnected = FALSE;
        int ftpReplyCode;
        std::string ftpReply;
        std::string changePathReply;
        while (ReconnectIfNeeded(parent, &reconnected, TRUE,
                                 &TotalConnectAttemptNum, retryMessageToUse)) // reconnect if necessary
        {
            BOOL run = FALSE;
            BOOL ok = TRUE;
            std::string newPath;
            BOOL needChangeDir = reconnected; // after reconnect try to set the working directory again
            if (!reconnected)                 // already connected for a longer time, verify that the working directory matches 'Path'
            {
                // use the cache, under normal circumstances the path should be there
                ok = ControlConnection->GetCurrentWorkingPath(parent, newPath, FALSE,
                                                              &canRetry, &nextRetryMessage);
                if (!ok && canRetry) // "retry" is allowed
                {
                    run = TRUE;
                    retryMessageToUse = &nextRetryMessage;
                }
                if (ok && newPath != Path) // working directory on the server differs - change required
                    needChangeDir = TRUE;             // (assumption: the server returns the same working path string)
            }
            if (ok && needChangeDir) // working directory needs to be changed
            {
                int panel;
                BOOL notInPanel = !SalamanderGeneral->GetPanelWithPluginFS(this, panel);
                BOOL success;
                // SendChangeWorkingPath() calls ReconnectIfNeeded() on connection failure, luckily that
                // does not matter because the code preceding this call runs only when reconnect did not
                // occur - "if (!reconnected)" - if reconnect happens, both code paths are the same
                ok = ControlConnection->SendChangeWorkingPath(notInPanel, panel == PANEL_LEFT, parent, Path.c_str(),
                                                              User, &success,
                                                              changePathReply, NULL,
                                                              &TotalConnectAttemptNum, NULL, TRUE, NULL);
                if (ok && !success && !Path.empty()) // send succeeded, but the server reports an error (+ignore the error for an empty path) -> user command cannot
                {                                   // be sent (it may be tied to the current path in the panel)
                    std::wstring errText;
                    if (!FtpFormatServerReplyMessage(ControlConnection->GetTextCodec(),
                                                     LangStr(IDS_CHANGEWORKPATHERROR).c_str(),
                                                     std::string_view(Path.data(), Path.size()),
                                                     changePathReply, errText))
                        errText = LangStr(IDS_OPERDOPPR_LOWMEM);
                    SalamanderGeneral->SalMessageBox(parent, errText.c_str(),
                                                     SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_FTPERRORTITLE).c_str(),
                                                     MB_OK | MB_ICONEXCLAMATION);
                    ok = FALSE;
                }
            }
            if (ok)
            {
                commandSent = TRUE;
                if (ControlConnection->SendFTPCommand(parent, cmdBuf.c_str(), logBuf.c_str(), NULL,
                                                      ControlConnection->GetWaitTime(WAITWND_COMOPER), NULL,
                                                      &ftpReplyCode, &ftpReply, FALSE,
                                                      TRUE, TRUE, &canRetry, &nextRetryMessage,
                                                      NULL)) // no fatal error occurred
                {
                    if (dlg.RefreshWorkingPath) // the Path probably changed, invalidate the Path listing cache
                        UploadListingCache.ReportUnknownChange(User.c_str(), Host.c_str(), Port, Path.c_str(), GetFTPServerPathType(Path.c_str()));

                    if (dlg.ChangePathInPanel)
                    {
                        int panel;
                        BOOL notInPanel = !SalamanderGeneral->GetPanelWithPluginFS(this, panel);
                        ok = ControlConnection->GetCurrentWorkingPath(parent, newPath, TRUE,
                                                                      &canRetry, &nextRetryMessage);
                        if (!ok && canRetry) // "retry" is allowed
                        {
                            run = TRUE;
                            retryMessageToUse = &nextRetryMessage;
                        }
                        if (ok && !notInPanel && newPath != Path)
                        { // the path changed (+otherwise everything is ok and we are in the panel ("always true")) -> change
                            // the path in the panel (assumption: the server returns the same working path string)
                            std::wstring newUserPartText;
                            if (BuildUserPartText(newUserPartText, newPath.c_str()))
                            {
                                ChangePathOnlyGetCurPathTime = GetTickCount(); // optimize ChangePath() called right after obtaining the working path
                                SalamanderGeneral->ChangePanelPathToPluginFS(
                                    panel, fsName.c_str(),
                                    newUserPartText.c_str());
                            }
                        }
                    }

                    if (ok) // everything went well, report the command result to the user
                    {
                        while (!logBuf.empty() && (logBuf.back() == '\n' || logBuf.back() == '\r'))
                            logBuf.pop_back(); // trim CRLF from the end of the command text for the log
                        const CFtpTextCodec textCodec = ControlConnection->GetTextCodec();
                        std::wstring replyText;
                        std::wstring commandText;
                        if (!textCodec.Decode(ftpReply.data(), ftpReply.size(), replyText))
                            replyText = L"";
                        if (!textCodec.Decode(logBuf.data(), logBuf.size(), commandText))
                            commandText = L"";
                        CWelcomeMsgDlg(parent, replyText.c_str(), TRUE, commandText.c_str()).Execute(); // show the response, nothing more...
                        SalamanderGeneral->WaitForESCRelease();
                    }
                }
                else
                {
                    if (dlg.RefreshWorkingPath) // the Path may have changed, invalidate the Path listing cache
                        UploadListingCache.ReportUnknownChange(User.c_str(), Host.c_str(), Port, Path.c_str(), GetFTPServerPathType(Path.c_str()));

                    if (canRetry) // "retry" is allowed
                    {
                        run = TRUE;
                        retryMessageToUse = &nextRetryMessage;
                    }
                }
            }
            if (!run)
                break;
        }

        if (dlg.RefreshWorkingPath && commandSent)
            SalamanderGeneral->PostChangeOnPathNotification(changedPath.c_str(), TRUE | 0x02 /* soft refresh */);
    }
}

void CPluginFSInterface::ShowRawListing(HWND parent)
{
    const CFtpTextCodec textCodec = ControlConnection->GetTextCodec();
    const std::string_view listing = PathListing.has_value()
                                         ? std::string_view(*PathListing)
                                         : std::string_view("", 0);
    std::wstring listingText;
    if (!textCodec.Decode(listing.data(), listing.size(), listingText))
        listingText = L"";
    CWelcomeMsgDlg(parent, listingText.c_str(), FALSE, NULL, TRUE, listing.data(),
                   static_cast<int>(listing.size())).Execute();
}

int CPluginFSInterface::GetLogUID()
{
    return (ControlConnection != NULL ? ControlConnection->GetLogUID() : -1);
}

void CPluginFSInterface::ContextMenu(const wchar_t* fsName, HWND parent, int menuX, int menuY, int type,
                                     int panel, int selectedFiles, int selectedDirs)
{
    if (type == fscmPathInPanel || type == fscmPanel || // menus for the current path and for the panel are identical
        type == fscmItemsInPanel)                       // the item menu will contain commands for items
    {
        HMENU main = LoadMenuW(HLanguage, MAKEINTRESOURCEW(IDM_ACTPATHCONTEXTMENU));
        if (main != NULL)
        {
            HMENU subMenu;
            BOOL destroySubMenu = FALSE;
            if (type == fscmItemsInPanel)
            {
                subMenu = CreatePopupMenu();
                destroySubMenu = TRUE;
            }
            else
                subMenu = GetSubMenu(main, 0);
            if (subMenu != NULL)
            {
                if (type != fscmItemsInPanel)
                {
                    SetTransferModeCheckMarksInSubMenu(subMenu, 0);
                    SetListHiddenFilesCheckInMenu(subMenu);
                    SetShowCertStateInMenu(subMenu);
                }
                else
                {
                    MENUITEMINFOW mi;
                    std::wstring nameBuf;

                    BOOL isfocusedDir = FALSE;
                    SalamanderGeneral->GetPanelFocusedItem(panel, &isfocusedDir);

                    // insert Salamander commands
                    int i = 0;
                    int index = 0;
                    int salCmd;
                    BOOL enabled;
                    int type2, lastType = sctyUnknown;
                    while (SPLEnumSalamanderCommandsOwned(
                        SalamanderGeneral, &index, &salCmd, nameBuf, &enabled,
                        &type2))
                    {
                        if (!enabled || salCmd == SALCMD_OPEN || // we cannot "open" files yet, so do not add it (we can do it only for directories and there it is uninteresting)
                            type2 == sctyForFocusedFile && isfocusedDir ||
                            (type2 != sctyForFocusedFile && type2 != sctyForFocusedFileOrDirectory &&
                             type2 != sctyForSelectedFilesAndDirectories))
                        {
                            continue;
                        }

                        if (type2 != lastType && lastType != sctyUnknown) // insert a separator
                        {
                            memset(&mi, 0, sizeof(mi));
                            mi.cbSize = sizeof(mi);
                            mi.fMask = MIIM_TYPE;
                            mi.fType = MFT_SEPARATOR;
                            InsertMenuItemW(subMenu, i++, TRUE, &mi);
                        }
                        lastType = type2;

                        // insert Salamander commands
                        memset(&mi, 0, sizeof(mi));
                        mi.cbSize = sizeof(mi);
                        mi.fMask = MIIM_TYPE | MIIM_ID | MIIM_STATE;
                        mi.fType = MFT_STRING;
                        mi.wID = salCmd + 5000; // shift all Salamander commands by 5000 so they can be distinguished from FTP commands
                        mi.dwTypeData = nameBuf.data();
                        mi.cch = (UINT)nameBuf.size();
                        mi.fState = MFS_ENABLED;
                        InsertMenuItemW(subMenu, i++, TRUE, &mi);
                    }
                }

                DWORD cmd = TrackPopupMenuEx(subMenu, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_RIGHTBUTTON,
                                             menuX, menuY, parent, NULL);
                if (cmd >= 5000)
                    SalamanderGeneral->PostSalamanderCommand(cmd - 5000);
                else
                {
                    switch (cmd)
                    {
                    case CM_DISCONNECTSRV:
                    {
                        GlobalDisconnectPanel = panel;
                        SalamanderGeneral->PostMenuExtCommand(FTPCMD_DISCONNECT, TRUE); // runs later in "sal-idle"
                        break;
                    }

                    case CM_REFRESHPATH:
                    {
                        SalamanderGeneral->PostRefreshPanelFS(this, FALSE);
                        break;
                    }

                    case CM_ADDBOOKMARK:
                    {
                        AddBookmark(parent);
                        break;
                    }

                    case CM_SENDFTPCOMMAND:
                    {
                        SendUserFTPCommand(parent);
                        break;
                    }

                    case CM_SHOWRAWLISTING:
                    {
                        ShowRawListing(parent);
                        break;
                    }

                    case CM_LISTHIDDENFILES:
                    {
                        ToggleListHiddenFiles(parent);
                        SalamanderGeneral->PostRefreshPanelFS(this, FALSE);
                        break;
                    }

                    case CM_SHOWLOG:
                    {
                        GlobalShowLogUID = GetLogUID();
                        if (GlobalShowLogUID == -1 || !Logs.HasLogWithUID(GlobalShowLogUID))
                        {
                            SalamanderGeneral->ShowMessageBox(SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_FSHAVENOLOG).c_str(), SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_FTPERRORTITLE).c_str(), MSGBOX_ERROR);
                        }
                        else                                                              // open the Logs window with the selected log GlobalShowLogUID
                            SalamanderGeneral->PostMenuExtCommand(FTPCMD_SHOWLOGS, TRUE); // runs later in "sal-idle"
                        break;
                    }

                    case CM_TRMODEAUTO:
                    case CM_TRMODEASCII:
                    case CM_TRMODEBINARY:
                        SetTransferModeByMenuCmd(cmd);
                        break;

                    case CM_SHOWCERT:
                    {
                        ShowSecurityInfo(parent);
                        break;
                    }
                    }
                }
                if (destroySubMenu)
                    DestroyMenu(subMenu);
            }
            DestroyMenu(main);
        }
    }
}

void CPluginFSInterface::SetTransferModeCheckMarksInSubMenu(HMENU menu, int submenuNumber)
{
    HMENU trModeMenu = NULL;
    int count = GetMenuItemCount(menu);
    int i;
    for (i = 0; i < count; i++)
    {
        trModeMenu = GetSubMenu(menu, i);
        if (trModeMenu != NULL && submenuNumber-- == 0)
            break;
    }
    if (trModeMenu != NULL)
    {
        CheckMenuItem(trModeMenu, CM_TRMODEBINARY, MF_BYCOMMAND | (TransferMode == trmBinary ? MF_CHECKED : MF_UNCHECKED));
        CheckMenuItem(trModeMenu, CM_TRMODEASCII, MF_BYCOMMAND | (TransferMode == trmASCII ? MF_CHECKED : MF_UNCHECKED));
        CheckMenuItem(trModeMenu, CM_TRMODEAUTO, MF_BYCOMMAND | (TransferMode != trmBinary && TransferMode != trmASCII ? MF_CHECKED : MF_UNCHECKED));
    }
}

void CPluginFSInterface::SetTransferModeByMenuCmd(int cmd)
{
    switch (cmd)
    {
    case CM_TRMODEBINARY:
        TransferMode = trmBinary;
        break;
    case CM_TRMODEASCII:
        TransferMode = trmASCII;
        break;
    default:
        TransferMode = trmAutodetect;
        break;
    }
}

void CPluginFSInterface::SetTransferModeByMenuCmd2(int id)
{
    switch (id)
    {
    case FTPCMD_TRMODEBINARY:
        TransferMode = trmBinary;
        break;
    case FTPCMD_TRMODEASCII:
        TransferMode = trmASCII;
        break;
    default:
        TransferMode = trmAutodetect;
        break;
    }
}

DWORD
CPluginFSInterface::GetTransferModeCmdState(int id)
{
    switch (id)
    {
    case FTPCMD_TRMODEBINARY:
        return MENU_ITEM_STATE_ENABLED |
               (TransferMode == trmBinary ? MENU_ITEM_STATE_CHECKED | MENU_ITEM_STATE_RADIO : 0);
    case FTPCMD_TRMODEASCII:
        return MENU_ITEM_STATE_ENABLED |
               (TransferMode == trmASCII ? MENU_ITEM_STATE_CHECKED | MENU_ITEM_STATE_RADIO : 0);
    default:
        return MENU_ITEM_STATE_ENABLED |
               (TransferMode == trmAutodetect ? MENU_ITEM_STATE_CHECKED | MENU_ITEM_STATE_RADIO : 0);
    }
}

void CPluginFSInterface::SetListHiddenFilesCheckInMenu(HMENU menu)
{
    if (ControlConnection != NULL) // "always true"
    {
        CheckMenuItem(menu, CM_LISTHIDDENFILES, MF_BYCOMMAND | (ControlConnection->IsListCommandLIST_a() ? MF_CHECKED : MF_UNCHECKED));
    }
}

void CPluginFSInterface::SetShowCertStateInMenu(HMENU menu)
{
    if (ControlConnection != NULL) // "always true"
    {
        EnableMenuItem(menu, CM_SHOWCERT, MF_BYCOMMAND | (ControlConnection->GetEncryptControlConnection() == 1 ? MF_ENABLED : MF_GRAYED));
    }
}

void CPluginFSInterface::ToggleListHiddenFiles(HWND parent)
{
    if (Config.HintListHiddenFiles)
    {
        MSGBOXEX_PARAMS params;
        const std::wstring caption = LangStr(IDS_FTPPLUGINTITLE).c_str();
        const std::wstring text = LangStr(IDS_HINTLISTHIDDENFILES).c_str();
        const std::wstring checkBoxText = LangStr(IDS_DONOTSHOWHINTLISTHF).c_str();
        memset(&params, 0, sizeof(params));
        params.HParent = parent;
        params.Flags = MSGBOXEX_OK | MSGBOXEX_ICONINFORMATION | MSGBOXEX_HINT;
        params.Caption = caption.c_str();
        params.Text = text.c_str();
        params.CheckBoxText = checkBoxText.c_str();
        int doNotShowHint = !Config.HintListHiddenFiles;
        params.CheckBoxValue = &doNotShowHint;
        SalamanderGeneral->SalMessageBoxEx(&params);
        Config.HintListHiddenFiles = !doNotShowHint;
    }
    if (ControlConnection != NULL) // "always true"
        ControlConnection->ToggleListCommandLIST_a();
}

BOOL CPluginFSInterface::IsListCommandLIST_a()
{
    if (ControlConnection != NULL) // "always true"
        return ControlConnection->IsListCommandLIST_a();
    return FALSE;
}

//
// ****************************************************************************
// CSimpleListPluginDataInterface
//

void CSimpleListPluginDataInterface::ReleasePluginData(CFileData& file, BOOL isDir)
{
    wchar_t* text = (wchar_t*)file.PluginData;
    if (text != NULL)
        free(text);
}

static wchar_t* SimpleTransferBuffer = NULL;

// callback invoked from Salamander to obtain text
void WINAPI GetRowText()
{
    const wchar_t* text = (const wchar_t*)((*TransferFileData)->PluginData);
    if (text != NULL)
    {
        *TransferLen = static_cast<int>(wcslen(text));
        if (*TransferLen > TRANSFER_BUFFER_MAX)
            *TransferLen = TRANSFER_BUFFER_MAX;
        wmemcpy(SimpleTransferBuffer, text, *TransferLen);
    }
    else
    {
        *TransferLen = 0;
    }
}

void CSimpleListPluginDataInterface::SetupView(BOOL leftPanel, CSalamanderViewAbstract* view,
                                               const wchar_t* archivePath, const CFileData* upperDir)
{
    view->SetViewMode(VIEW_MODE_DETAILED, VALID_DATA_NONE); // switch to Detailed mode + remove other columns, keep only the Name column
    view->GetTransferVariables(TransferFileData, TransferIsDir, SimpleTransferBuffer, TransferLen, TransferRowData,
                               TransferPluginDataIface, TransferActCustomData);

    // rename the standard Name column to Line
    view->SetColumnName(0, SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_LINENUMCOLUMN).c_str(),
                        SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_LINENUMCOLUMNDESC).c_str());

    CColumn column;
    lstrcpynW(column.Name, SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_RAWLISTCOLUMN).c_str(), COLUMN_NAME_MAX);
    lstrcpynW(column.Description, SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_RAWLISTCOLUMNDESC).c_str(), COLUMN_DESCRIPTION_MAX);
    column.GetText = GetRowText;
    column.SupportSorting = 0;
    column.LeftAlignment = 1;
    column.ID = COLUMN_ID_CUSTOM;
    column.Width = leftPanel ? LOWORD(ListingColumnWidth) : HIWORD(ListingColumnWidth);
    column.FixedWidth = leftPanel ? LOWORD(ListingColumnFixedWidth) : HIWORD(ListingColumnFixedWidth);
    view->InsertColumn(1, &column);
}

void CSimpleListPluginDataInterface::ColumnFixedWidthShouldChange(BOOL leftPanel, const CColumn* column, int newFixedWidth)
{
    if (leftPanel)
        ListingColumnFixedWidth = MAKELONG(newFixedWidth, HIWORD(ListingColumnFixedWidth));
    else
        ListingColumnFixedWidth = MAKELONG(LOWORD(ListingColumnFixedWidth), newFixedWidth);
    if (newFixedWidth)
        ColumnWidthWasChanged(leftPanel, column, column->Width);
}

void CSimpleListPluginDataInterface::ColumnWidthWasChanged(BOOL leftPanel, const CColumn* column, int newWidth)
{
    if (leftPanel)
        ListingColumnWidth = MAKELONG(newWidth, HIWORD(ListingColumnWidth));
    else
        ListingColumnWidth = MAKELONG(LOWORD(ListingColumnWidth), newWidth);
}

BOOL CSimpleListPluginDataInterface::GetInfoLineContent(int panel, const CFileData* file, BOOL isDir,
                                                        int selectedFiles, int selectedDirs,
                                                        BOOL displaySize, const CQuadWord& selectedSize,
                                                        CSalamanderStringBuffer* buffer,
                                                        CSalamanderTextRangeBuffer* hotTexts)
{
    if (buffer == NULL || hotTexts == NULL)
        return FALSE;
    if (file != NULL)
    {
        try
        {
            const wchar_t* text = (const wchar_t*)(file->PluginData);
            const std::wstring output =
                SPLFormatStringOwned(L"%ls: %ls", file->Name, text != NULL ? text : L"");
            std::vector<CSalamanderTextRange> ranges;
            if (text != NULL && text[0] != L'\0')
            {
                const size_t offset = wcslen(file->Name) + 2;
                if (offset > (std::numeric_limits<DWORD>::max)() ||
                    output.size() - offset > (std::numeric_limits<DWORD>::max)())
                    return FALSE;
                ranges.push_back({static_cast<DWORD>(offset),
                                  static_cast<DWORD>(output.size() - offset)});
            }
            return sally::plugin_abi::WriteTextAndRanges(
                       *buffer, *hotTexts, output, ranges)
                       ? TRUE
                       : FALSE;
        }
        catch (...)
        {
            return FALSE;
        }
    }
    else
        return FALSE; // let Salamander display the item counts and empty panel information
}

//
// ****************************************************************************
// CFTPListingPluginDataInterface
//

CFTPListingPluginDataInterface::CFTPListingPluginDataInterface(TIndirectArray<CSrvTypeColumn>* columns,
                                                               BOOL deleteColumns, DWORD validDataMask,
                                                               BOOL isVMS, const CFtpTextCodec& textCodec)
    : TextCodec(textCodec)
{
    Columns = columns;
    DeleteColumns = deleteColumns;
    RawNameOffset = 0;
    ItemDataSize = sizeof(char*);
    BlocksColumnOffset = -1;
    BytesColumnOffset = -1;
    DateColumnOffset = -1;
    TimeColumnOffset = -1;
    DataOffsets = new DWORD[Columns->Count];
    if (DataOffsets != NULL)
    {
        BOOL hasSize = FALSE;
        int i;
        for (i = 0; i < Columns->Count; i++)
        {
            switch (Columns->At(i)->Type)
            {
            case stctSize:
            {
                hasSize = TRUE;
                BlocksColumnOffset = -1; // CFileData::Size has higher priority than the generic size in blocks
                BytesColumnOffset = -1;  // CFileData::Size has higher priority than the generic size in bytes
                DataOffsets[i] = -1;     // store the data directly in CFileData
                break;
            }

            case stctDate:
            {
                if (DateColumnOffset == -1)
                {
                    int nameID = Columns->At(i)->NameID;
                    if (nameID == 3 /* date */) // found the column with the last write date (take the first from the left)
                        DateColumnOffset = -2;  // stored in CFileData::LastWrite
                }
                DataOffsets[i] = -1; // store the data directly in CFileData
                break;
            }

            case stctTime:
            {
                if (TimeColumnOffset == -1)
                {
                    int nameID = Columns->At(i)->NameID;
                    if (nameID == 4 /* time */) // found the column with the last write time (take the first from the left)
                        TimeColumnOffset = -2;  // stored in CFileData::LastWrite
                }
                DataOffsets[i] = -1; // store the data directly in CFileData
                break;
            }

            case stctGeneralText:
            {
                DataOffsets[i] = ItemDataSize;
                ItemDataSize += sizeof(char*);
                break;
            }

            case stctGeneralDate:
            {
                if (DateColumnOffset == -1)
                {
                    int nameID = Columns->At(i)->NameID;
                    if (nameID == 3 /* date */) // found the column with the last write date (take the first from the left)
                        DateColumnOffset = ItemDataSize;
                }
                DataOffsets[i] = ItemDataSize;
                ItemDataSize += sizeof(CFTPDate);
                break;
            }

            case stctGeneralTime:
            {
                if (TimeColumnOffset == -1)
                {
                    int nameID = Columns->At(i)->NameID;
                    if (nameID == 4 /* time */) // found the column with the last write time (take the first from the left)
                        TimeColumnOffset = ItemDataSize;
                }
                DataOffsets[i] = ItemDataSize;
                ItemDataSize += sizeof(CFTPTime);
                break;
            }

            case stctGeneralNumber:
            {
                if (!hasSize && BytesColumnOffset == -1)
                {
                    int nameID = Columns->At(i)->NameID;
                    if (nameID == 2 /* size */)
                    {                            // found the column with file sizes in bytes (take the first from the left)
                        BlocksColumnOffset = -1; // general size in bytes has higher priority than in blocks
                        BytesColumnOffset = ItemDataSize;
                    }
                    else
                    {
                        if ((nameID == 10 /* block size */ || nameID == 22 /* Physical Block Length */) &&
                            BlocksColumnOffset == -1)
                        { // found the column with file sizes in blocks (take the first from the left)
                            BlocksColumnOffset = ItemDataSize;
                        }
                    }
                }
                DataOffsets[i] = ItemDataSize;
                ItemDataSize += sizeof(__int64);
                break;
            }

            default:
                DataOffsets[i] = -1;
                break; // store the data directly in CFileData
            }
        }
    }
    IsVMS = isVMS;
    ValidDataMask = validDataMask | GetPLValidDataMask(); // must be called only after locating the BytesColumnOffset offsets, etc.
}

CFTPListingPluginDataInterface::~CFTPListingPluginDataInterface()
{
    if (DataOffsets != NULL)
        delete[] DataOffsets;
    if (DeleteColumns)
        delete Columns; // it was only a copy, so we need to deallocate it
}

void CFTPListingPluginDataInterface::StoreStringToColumn(CFileData& file, int column, char* str)
{
#ifdef _DEBUG
    if (column < 0 || column >= Columns->Count)
    {
        TRACE_E("CFTPListingPluginDataInterface::StoreStringToColumn(): Invalid column index!");
        return;
    }
    if (Columns->At(column)->Type != stctGeneralText)
    {
        TRACE_E("CFTPListingPluginDataInterface::StoreStringToColumn(): Unexpected type of column!");
        return;
    }
#endif
    char** data = (char**)(((char*)file.PluginData) + DataOffsets[column]);
    if (*data != NULL)
        free(*data);
    *data = str;
}

BOOL CFTPListingPluginDataInterface::StoreRawName(CFileData& file, const char* name, size_t length)
{
    if (file.PluginData == 0 || name == NULL)
        return FALSE;

    char* copy = (char*)SalamanderGeneral->Alloc((int)length + 1);
    if (copy == NULL)
        return FALSE;
    memcpy(copy, name, length);
    copy[length] = 0;

    char** data = (char**)(((char*)file.PluginData) + RawNameOffset);
    if (*data != NULL)
        SalamanderGeneral->Free(*data);
    *data = copy;
    return TRUE;
}

const char* CFTPListingPluginDataInterface::GetRawName(const CFileData& file) const
{
    if (file.PluginData == 0)
        return NULL;
    return *(char**)(((char*)file.PluginData) + RawNameOffset);
}

BOOL CFTPListingPluginDataInterface::GetWireName(const CFileData& file, const CFtpTextCodec& codec,
                                                  std::string& name) const
{
    const char* rawName = GetRawName(file);
    if (rawName != NULL)
    {
        name.assign(rawName);
        return TRUE;
    }
    return codec.Encode(file.Name, file.NameLen, name);
}

BOOL CFTPListingPluginDataInterface::DecodeTextForPresentation(
    std::string_view bytes, std::wstring& text) const noexcept
{
    return FtpDecodeServerTextForPresentation(TextCodec, bytes, text);
}

void CFTPListingPluginDataInterface::StoreDayToColumn(CFileData& file, int column, BYTE day)
{
#ifdef _DEBUG
    if (column < 0 || column >= Columns->Count)
    {
        TRACE_E("CFTPListingPluginDataInterface::StoreDayToColumn(): Invalid column index!");
        return;
    }
    if (Columns->At(column)->Type != stctGeneralDate)
    {
        TRACE_E("CFTPListingPluginDataInterface::StoreDayToColumn(): Unexpected type of column!");
        return;
    }
#endif
    CFTPDate* data = (CFTPDate*)(((char*)file.PluginData) + DataOffsets[column]);
    data->Day = day;
}

void CFTPListingPluginDataInterface::StoreMonthToColumn(CFileData& file, int column, BYTE month)
{
#ifdef _DEBUG
    if (column < 0 || column >= Columns->Count)
    {
        TRACE_E("CFTPListingPluginDataInterface::StoreMonthToColumn(): Invalid column index!");
        return;
    }
    if (Columns->At(column)->Type != stctGeneralDate)
    {
        TRACE_E("CFTPListingPluginDataInterface::StoreMonthToColumn(): Unexpected type of column!");
        return;
    }
#endif
    CFTPDate* data = (CFTPDate*)(((char*)file.PluginData) + DataOffsets[column]);
    data->Month = month;
}

void CFTPListingPluginDataInterface::StoreYearToColumn(CFileData& file, int column, WORD year)
{
#ifdef _DEBUG
    if (column < 0 || column >= Columns->Count)
    {
        TRACE_E("CFTPListingPluginDataInterface::StoreYearToColumn(): Invalid column index!");
        return;
    }
    if (Columns->At(column)->Type != stctGeneralDate)
    {
        TRACE_E("CFTPListingPluginDataInterface::StoreYearToColumn(): Unexpected type of column!");
        return;
    }
#endif
    CFTPDate* data = (CFTPDate*)(((char*)file.PluginData) + DataOffsets[column]);
    data->Year = year;
}

void CFTPListingPluginDataInterface::StoreDateToColumn(CFileData& file, int column, BYTE day,
                                                       BYTE month, WORD year)
{
#ifdef _DEBUG
    if (column < 0 || column >= Columns->Count)
    {
        TRACE_E("CFTPListingPluginDataInterface::StoreDateToColumn(): Invalid column index!");
        return;
    }
    if (Columns->At(column)->Type != stctGeneralDate)
    {
        TRACE_E("CFTPListingPluginDataInterface::StoreDateToColumn(): Unexpected type of column!");
        return;
    }
#endif
    CFTPDate* data = (CFTPDate*)(((char*)file.PluginData) + DataOffsets[column]);
    data->Day = day;
    data->Month = month;
    data->Year = year;
}

void CFTPListingPluginDataInterface::StoreTimeToColumn(CFileData& file, int column, BYTE hour, BYTE minute,
                                                       BYTE second, WORD millisecond)
{
#ifdef _DEBUG
    if (column < 0 || column >= Columns->Count)
    {
        TRACE_E("CFTPListingPluginDataInterface::StoreTimeToColumn(): Invalid column index!");
        return;
    }
    if (Columns->At(column)->Type != stctGeneralTime)
    {
        TRACE_E("CFTPListingPluginDataInterface::StoreTimeToColumn(): Unexpected type of column!");
        return;
    }
#endif
    CFTPTime* data = (CFTPTime*)(((char*)file.PluginData) + DataOffsets[column]);
    data->Hour = hour;
    data->Minute = minute;
    data->Second = second;
    data->Millisecond = millisecond;
}

void CFTPListingPluginDataInterface::StoreNumberToColumn(CFileData& file, int column, __int64 number)
{
#ifdef _DEBUG
    if (column < 0 || column >= Columns->Count)
    {
        TRACE_E("CFTPListingPluginDataInterface::StoreNumberToColumn(): Invalid column index!");
        return;
    }
    if (Columns->At(column)->Type != stctGeneralNumber)
    {
        TRACE_E("CFTPListingPluginDataInterface::StoreNumberToColumn(): Unexpected type of column!");
        return;
    }
#endif
    __int64* data = (__int64*)(((char*)file.PluginData) + DataOffsets[column]);
    *data = number;
}

char* CFTPListingPluginDataInterface::GetStringFromColumn(const CFileData& file, int column)
{
#ifdef _DEBUG
    if (column < 0 || column >= Columns->Count)
    {
        TRACE_E("CFTPListingPluginDataInterface::GetStringFromColumn(): Invalid column index!");
        return NULL;
    }
    if (Columns->At(column)->Type != stctGeneralText)
    {
        TRACE_E("CFTPListingPluginDataInterface::GetStringFromColumn(): Unexpected type of column!");
        return NULL;
    }
#endif
    return *(char**)(((char*)file.PluginData) + DataOffsets[column]);
}

void CFTPListingPluginDataInterface::GetDateFromColumn(const CFileData& file, int column, SYSTEMTIME* stVal)
{
#ifdef _DEBUG
    if (column < 0 || column >= Columns->Count)
    {
        TRACE_E("CFTPListingPluginDataInterface::GetDateFromColumn(): Invalid column index!");
        return;
    }
    if (Columns->At(column)->Type != stctGeneralDate)
    {
        TRACE_E("CFTPListingPluginDataInterface::GetDateFromColumn(): Unexpected type of column!");
        return;
    }
#endif
    CFTPDate* data = (CFTPDate*)(((char*)file.PluginData) + DataOffsets[column]);
    stVal->wDay = data->Day;
    stVal->wMonth = data->Month;
    stVal->wYear = data->Year;
}

void CFTPListingPluginDataInterface::GetDateFromColumn(const CFileData& file, int column, CFTPDate* date)
{
#ifdef _DEBUG
    if (column < 0 || column >= Columns->Count)
    {
        TRACE_E("CFTPListingPluginDataInterface::GetDateFromColumn(): Invalid column index!");
        return;
    }
    if (Columns->At(column)->Type != stctGeneralDate)
    {
        TRACE_E("CFTPListingPluginDataInterface::GetDateFromColumn(): Unexpected type of column!");
        return;
    }
#endif
    *date = *(CFTPDate*)(((char*)file.PluginData) + DataOffsets[column]);
}

void CFTPListingPluginDataInterface::GetTimeFromColumn(const CFileData& file, int column, SYSTEMTIME* stVal)
{
#ifdef _DEBUG
    if (column < 0 || column >= Columns->Count)
    {
        TRACE_E("CFTPListingPluginDataInterface::GetTimeFromColumn(): Invalid column index!");
        return;
    }
    if (Columns->At(column)->Type != stctGeneralTime)
    {
        TRACE_E("CFTPListingPluginDataInterface::GetTimeFromColumn(): Unexpected type of column!");
        return;
    }
#endif
    CFTPTime* data = (CFTPTime*)(((char*)file.PluginData) + DataOffsets[column]);
    stVal->wHour = data->Hour;
    stVal->wMinute = data->Minute;
    stVal->wSecond = data->Second;
    stVal->wMilliseconds = data->Millisecond;
}

void CFTPListingPluginDataInterface::GetTimeFromColumn(const CFileData& file, int column, CFTPTime* time)
{
#ifdef _DEBUG
    if (column < 0 || column >= Columns->Count)
    {
        TRACE_E("CFTPListingPluginDataInterface::GetTimeFromColumn(): Invalid column index!");
        return;
    }
    if (Columns->At(column)->Type != stctGeneralTime)
    {
        TRACE_E("CFTPListingPluginDataInterface::GetTimeFromColumn(): Unexpected type of column!");
        return;
    }
#endif
    *time = *(CFTPTime*)(((char*)file.PluginData) + DataOffsets[column]);
}

__int64
CFTPListingPluginDataInterface::GetNumberFromColumn(const CFileData& file, int column)
{
#ifdef _DEBUG
    if (column < 0 || column >= Columns->Count)
    {
        TRACE_E("CFTPListingPluginDataInterface::GetNumberFromColumn(): Invalid column index!");
        return 0;
    }
    if (Columns->At(column)->Type != stctGeneralNumber)
    {
        TRACE_E("CFTPListingPluginDataInterface::GetNumberFromColumn(): Unexpected type of column!");
        return 0;
    }
#endif
    return *(__int64*)(((char*)file.PluginData) + DataOffsets[column]);
}

BOOL CFTPListingPluginDataInterface::AllocPluginData(CFileData& file)
{
    if (ItemDataSize == 0) // no data are stored outside CFileData
    {
        file.PluginData = 0;
        return TRUE;
    }

    char* base = (char*)(file.PluginData = (DWORD_PTR)malloc(ItemDataSize));
    if ((void*)(file.PluginData) == NULL)
    {
        TRACE_E(LOW_MEMORY);
        return FALSE;
    }
    memset(base, 0, ItemDataSize); // strings must be zeroed, the rest is zeroed just for clarity
    return TRUE;
}

void CFTPListingPluginDataInterface::ClearPluginData(CFileData& file)
{
    if (file.PluginData != 0)
    {
        char* base = (char*)file.PluginData;
        char* rawName = *(char**)(base + RawNameOffset);
        if (rawName != NULL)
            SalamanderGeneral->Free(rawName);
        int i;
        for (i = 0; i < Columns->Count; i++)
        {
            if (Columns->At(i)->Type == stctGeneralText)
            {
                char* data = *(char**)(base + DataOffsets[i]);
                if (data != NULL)
                    SalamanderGeneral->Free(data);
            }
            // stctGeneralDate, stctGeneralTime, and stctGeneralNumber are not allocated separately; they are freed together with file.PluginData
        }
        memset(base, 0, ItemDataSize); // strings must be zeroed, the rest is zeroed just for clarity
    }
}

void CFTPListingPluginDataInterface::ReleasePluginData(CFileData& file, BOOL isDir)
{
    if (file.PluginData != 0)
    {
        char* base = (char*)file.PluginData;
        char* rawName = *(char**)(base + RawNameOffset);
        if (rawName != NULL)
            SalamanderGeneral->Free(rawName);
        int i;
        for (i = 0; i < Columns->Count; i++)
        {
            if (Columns->At(i)->Type == stctGeneralText)
            {
                char* data = *(char**)(base + DataOffsets[i]);
                if (data != NULL)
                    SalamanderGeneral->Free(data);
            }
            // stctGeneralDate, stctGeneralTime, and stctGeneralNumber are not allocated separately; they are freed together with file.PluginData
        }
        free(base);
    }
}

int CFTPListingPluginDataInterface::FindRightsColumn()
{
    int i;
    for (i = 0; i < Columns->Count; i++)
    {
        CSrvTypeColumn* col = Columns->At(i);
        if (col->NameID == 6 /* Rights */ && col->Type == stctGeneralText)
            return i;
    }
    return -1;
}

BOOL CFTPListingPluginDataInterface::GetSize(const CFileData& file, CQuadWord& size, BOOL& inBytes)
{
    if (ValidDataMask & VALID_DATA_SIZE)
    {
        size = file.Size;
        inBytes = TRUE;
        return TRUE;
    }
    else
    {
        if (BytesColumnOffset != -1)
        {
            size.SetUI64(*(__int64*)(((char*)(file.PluginData)) + BytesColumnOffset));
            if (size.Value == INT64_EMPTYNUMBER)
                size.Set(0, 0); // if the column contains an empty value, return zero size
            inBytes = TRUE;
            return TRUE;
        }
        else
        {
            if (BlocksColumnOffset != -1)
            {
                size.SetUI64(*(__int64*)(((char*)(file.PluginData)) + BlocksColumnOffset));
                if (size.Value == INT64_EMPTYNUMBER)
                    size.Set(0, 0); // if the column contains an empty value, return zero size
                inBytes = FALSE;
                return TRUE;
            }
            else
                return FALSE;
        }
    }
}

BOOL CFTPListingPluginDataInterface::GetBasicName(const CFileData& file, const wchar_t** name,
                                                  const wchar_t** ext, std::wstring& storage) noexcept
{
    if (IsVMS)
    {
        try
        {
            storage.assign(file.Name, file.NameLen);
        }
        catch (...)
        {
            return FALSE;
        }
        if (FTPVMSCutFileVersion(storage.data(), file.NameLen))
        {
            storage.resize(wcslen(storage.c_str()));
            *name = storage.c_str();
            if (file.NameLen > (DWORD)(file.Ext - file.Name))
                *ext = storage.c_str() + (file.Ext - file.Name);
            else
                *ext = storage.c_str() + storage.size(); // in case the name has no extension (otherwise ';' is always after the extension)
            return TRUE;
        }
        else
            TRACE_EW(L"Unexpected situation in CFTPListingPluginDataInterface::GetBasicName(): not "
                     L"a VMS name? ("
                     << file.Name << L")");
    }
    *name = file.Name;
    *ext = file.Ext;
    return TRUE;
}

void CFTPListingPluginDataInterface::GetLastWriteDateAndTime(const CFileData& file, BOOL* dateAndTimeValid,
                                                             CFTPDate* date, CFTPTime* time)
{
    *dateAndTimeValid = (TimeColumnOffset != -1 || DateColumnOffset != -1);
    if (TimeColumnOffset == -2 || DateColumnOffset == -2)
    {
        DWORD mask = (TimeColumnOffset == -2 ? VALID_DATA_TIME : 0) | (DateColumnOffset == -2 ? VALID_DATA_DATE : 0);
        if ((ValidDataMask & mask) == mask) // just to be sure
        {
            FILETIME ft;
            SYSTEMTIME st;
            FileTimeToLocalFileTime(&file.LastWrite, &ft);
            FileTimeToSystemTime(&ft, &st);
            date->Year = st.wYear;
            date->Month = (BYTE)st.wMonth;
            date->Day = (BYTE)st.wDay;
            time->Hour = st.wHour;
            time->Minute = st.wMinute;
            time->Second = st.wSecond;
            time->Millisecond = st.wMilliseconds;
        }
        else
        {
            TRACE_E("Unexpected situation in CFTPListingPluginDataInterface::GetLastWriteDateAndTime()!");
            *dateAndTimeValid = FALSE;
        }
    }
    if (*dateAndTimeValid)
    {
        if (DateColumnOffset == -1)
            memset(date, 0, sizeof(CFTPDate)); // unknown date = use the "empty value" (date->Day == 0)
        else
        {
            if (DateColumnOffset != -2)
                *date = *(CFTPDate*)(((char*)(file.PluginData)) + DateColumnOffset);
        }
        if (TimeColumnOffset == -1 || date->Day == 0) // unknown time or unknown ("empty value") date (setting time without a date makes no sense -> the time is considered unknown as well)
        {                                             // unknown time: we may use the "empty value" (see further adjustments of 'time')
            memset(time, 0, sizeof(CFTPTime));
            time->Hour = 24;
        }
        else
        {
            if (TimeColumnOffset != -2)
                *time = *(CFTPTime*)(((char*)(file.PluginData)) + TimeColumnOffset);
        }

        // if the time is unknown and the date is known, adjust the time to 0:00:00 (do not use the "empty value")
        if (time->Hour == 24 && date->Day != 0)
            memset(time, 0, sizeof(CFTPTime));
    }
}

BOOL CFTPListingPluginDataInterface::GetByteSize(const CFileData* file, BOOL isDir, CQuadWord* size)
{
    if (BytesColumnOffset != -1)
    {
        size->SetUI64(*(__int64*)(((char*)(file->PluginData)) + BytesColumnOffset));
        return size->Value != INT64_EMPTYNUMBER; // return TRUE only if the size is known
    }
    return FALSE;
}

BOOL CFTPListingPluginDataInterface::GetLastWriteDate(const CFileData* file, BOOL isDir, SYSTEMTIME* date)
{
    if (DateColumnOffset != -1 && DateColumnOffset != -2)
    {
        CFTPDate d = *(CFTPDate*)(((char*)(file->PluginData)) + DateColumnOffset);
        if (d.Day != 0) // if the date is not an empty value
        {
            date->wYear = d.Year;
            date->wMonth = d.Month;
            date->wDay = d.Day;
            date->wDayOfWeek = 0;
            return TRUE;
        }
    }
    return FALSE;
}

BOOL CFTPListingPluginDataInterface::GetLastWriteTime(const CFileData* file, BOOL isDir, SYSTEMTIME* time)
{
    if (TimeColumnOffset != -1 && TimeColumnOffset != -2)
    {
        CFTPTime t = *(CFTPTime*)(((char*)(file->PluginData)) + TimeColumnOffset);
        if (t.Hour != 24) // if the time is not an empty value
        {
            time->wHour = t.Hour;
            time->wMinute = t.Minute;
            time->wSecond = t.Second;
            time->wMilliseconds = t.Millisecond;
            return TRUE;
        }
    }
    return FALSE;
}

DWORD
CFTPListingPluginDataInterface::GetPLValidDataMask()
{
    return (BytesColumnOffset != -1 ? VALID_DATA_PL_SIZE : 0) |
           (DateColumnOffset != -1 && DateColumnOffset != -2 ? VALID_DATA_PL_DATE : 0) |
           (TimeColumnOffset != -1 && TimeColumnOffset != -2 ? VALID_DATA_PL_TIME : 0);
}
