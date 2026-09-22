// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"

// Wide: see editor.cpp's Command/Arguments/InitDir for why - GetValueW's REG_SZ
// case is a raw memcpy of the registry's actual UTF-16LE bytes, so a narrow
// buffer here was silently corrupted on every load/save.
std::wstring LastExportPath;

BOOL ExportKey(const wchar_t* fullName)
{
    CALL_STACK_MESSAGE1("ExportKey()");
    std::wstring dialogFile = LastExportPath;
    std::wstring path = fullName ? fullName : L"";
    SPLSalPathAddBackslashOwned(dialogFile);
    while (1)
    {
        BOOL direct = FALSE;
        CExportDialog dlg(GetParent(), path, &dialogFile, &direct);
        if (dlg.Execute() != IDOK)
            return FALSE;

        // separate the user part from the FS path
        if (!direct)
        {
            if (!RemoveFSNameFromPath(path))
            {
                Error(IDS_NOTREGEDTPATH);
                continue; // show the dialog again
            }
            if (path.empty())
            {
                Error(IDS_BADPATH);
                continue; // show the dialog again
            }
        }

        if (path != L"\\")
        {
            while (!path.empty() && path.back() == L'\\')
                path.pop_back();
            if (path.empty())
            {
                Error(IDS_BADPATH);
                continue; // show the dialog again
            }
        }

        LPWSTR key;
        int root;
        if (!ParseFullPath(path.data(), key, root))
        {
            Error(IDS_BADPATH);
            continue; // show the dialog again
        }

        // verify that the key exists
        if (root != -1)
        {
            HKEY hKey;
            int err = RegOpenKeyExW(PredefinedHKeys[root].HKey, key, 0, KEY_READ, &hKey);
            if (err != ERROR_SUCCESS)
            {
                ErrorL(err, IDS_OPEN);
                continue; // show the dialog again
            }
            RegCloseKey(hKey);
        }

        const std::wstring file = dialogFile;

        // verify that the target file does not exist
        DWORD attr = SG->SalGetFileAttributes(file.c_str());
        if (attr != -1)
        {
            if (attr & FILE_ATTRIBUTE_DIRECTORY)
            {
                Error(IDS_FILENAMEISDIR);
                continue; // show the dialog again
            }
            if (SG->DialogQuestion(GetParent(), BUTTONS_YESNOCANCEL, file.c_str(),
                                   LoadStrW(IDS_OVERWRITE).c_str(), LoadStrW(IDS_OVERWRITETITLE).c_str()) != DIALOG_YES)
                continue; // show the dialog again
            SG->ClearReadOnlyAttr(file.c_str());
            if (!DeleteFileW(file.c_str()))
            {
                Error(IDS_REPLACEERROR);
                continue; // show the dialog again
            }
        }

        LastExportPath = file;
        SPLCutDirectoryOwned(SG, LastExportPath);

        std::wstring command;
        if (root != -1) // regedit.exe can do "export all", so we'll use it for this task even after XP
        {
            // starting with XP we invoke the reg.exe command line, see https://forum.altap.cz/viewtopic.php?f=24&t=5682
            // the advantage of reg.exe is that from Vista onward it does not require UAC elevation for exports
            std::wstring sysdir;
            SPLGetSystemDirectoryOwned(sysdir);
            SPLSalPathAddBackslashOwned(sysdir);
            command = L"\"" + sysdir + L"reg.exe\" EXPORT \"" +
                      (path.front() == L'\\' ? path.substr(1) : path) + L"\" \"" + file + L"\"";
        }
        else
        {
            std::wstring windir;
            SPLGetWindowsDirectoryOwned(windir);
            SPLSalPathAddBackslashOwned(windir);
            if (root != -1)
                command = L"\"" + windir + L"regedit.exe\" /e \"" + file + L"\" \"" +
                          (path.front() == L'\\' ? path.substr(1) : path) + L"\"";
            else
                command = L"\"" + windir + L"regedit.exe\" /e \"" + file + L"\"";
        }

        STARTUPINFOW si;
        PROCESS_INFORMATION pi;
        memset(&si, 0, sizeof(si));
        si.cb = sizeof(si);
        si.lpTitle = NULL;
        si.dwFlags = STARTF_USESHOWWINDOW;
        si.wShowWindow = SW_HIDE;

        if (!CreateProcessW(NULL, command.data(), NULL, NULL, FALSE, CREATE_DEFAULT_ERROR_MODE | NORMAL_PRIORITY_CLASS,
                            NULL, NULL, &si, &pi))
            return Error(IDS_PROCESS2, (root != -1) ? L"reg.exe" : L"regedit.exe");

        SG->CreateSafeWaitWindow(LoadStrW(IDS_EXPORTING).c_str(), LoadStrW(IDS_PLUGINNAME).c_str(), 500, FALSE, SG->GetMainWindowHWND());

        WaitForSingleObject(pi.hProcess, INFINITE);

        SG->DestroySafeWaitWindow();

        attr = SG->SalGetFileAttributes(file.c_str());
        if (attr == -1)
            Error(IDS_BADEXPORT);

        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);

        // announce the change on the path (our file was added)
        std::wstring changedPath = file;
        SPLCutDirectoryOwned(SG, changedPath);
        SG->PostChangeOnPathNotification(changedPath.c_str(), FALSE);

        break;
    }
    return TRUE;
}
