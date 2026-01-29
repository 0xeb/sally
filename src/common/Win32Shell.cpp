// SPDX-FileCopyrightText: 2026 Open Salamander Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"
#include "IShell.h"
#include <shlobj.h>
#include <stdlib.h>
#include <string.h>

class Win32Shell : public IShell
{
public:
    ShellExecResult Execute(const ShellExecInfo& info) override
    {
        SHELLEXECUTEINFOW sei;
        memset(&sei, 0, sizeof(sei));
        sei.cbSize = sizeof(sei);
        sei.fMask = SEE_MASK_FLAG_DDEWAIT | SEE_MASK_NOCLOSEPROCESS;
        sei.hwnd = info.hwnd;
        sei.lpVerb = info.verb;
        sei.lpFile = info.file;
        sei.lpParameters = info.parameters;
        sei.lpDirectory = info.directory;
        sei.nShow = info.showCommand;

        if (ShellExecuteExW(&sei))
        {
            // Close the process handle if one was returned
            if (sei.hProcess)
                CloseHandle(sei.hProcess);
            return ShellExecResult::Ok(sei.hInstApp);
        }

        return ShellExecResult::Error(GetLastError());
    }

    ShellResult FileOperation(ShellFileOp operation,
                              const wchar_t* sourcePaths,
                              const wchar_t* destPath,
                              DWORD flags,
                              HWND hwnd) override
    {
        SHFILEOPSTRUCTW op;
        memset(&op, 0, sizeof(op));
        op.hwnd = hwnd;
        op.wFunc = static_cast<UINT>(operation);
        op.pFrom = sourcePaths;
        op.pTo = destPath;
        op.fFlags = static_cast<FILEOP_FLAGS>(flags);

        int result = SHFileOperationW(&op);
        if (result != 0)
            return ShellResult::Error(result);

        if (op.fAnyOperationsAborted)
            return ShellResult::Error(ERROR_CANCELLED);

        return ShellResult::Ok();
    }

    ShellResult GetFileInfo(const wchar_t* path,
                            DWORD attributes,
                            SHFILEINFOW& info,
                            UINT flags) override
    {
        memset(&info, 0, sizeof(info));
        DWORD_PTR result = SHGetFileInfoW(path, attributes, &info, sizeof(info), flags);

        if (result == 0 && !(flags & SHGFI_SYSICONINDEX))
            return ShellResult::Error(GetLastError());

        return ShellResult::Ok();
    }

    bool BrowseForFolder(HWND hwnd,
                         const wchar_t* title,
                         UINT flags,
                         std::wstring& selectedPath) override
    {
        wchar_t path[MAX_PATH];
        path[0] = L'\0';

        BROWSEINFOW bi;
        memset(&bi, 0, sizeof(bi));
        bi.hwndOwner = hwnd;
        bi.pszDisplayName = path;
        bi.lpszTitle = title;
        bi.ulFlags = flags;

        LPITEMIDLIST pidl = SHBrowseForFolderW(&bi);
        if (!pidl)
            return false;

        BOOL success = SHGetPathFromIDListW(pidl, path);
        CoTaskMemFree(pidl);

        if (success)
        {
            selectedPath = path;
            return true;
        }

        return false;
    }

    ShellResult GetSpecialFolderPath(int csidl,
                                     std::wstring& path,
                                     bool create) override
    {
        wchar_t buffer[MAX_PATH];
        buffer[0] = L'\0';

        HRESULT hr = SHGetFolderPathW(nullptr, csidl | (create ? CSIDL_FLAG_CREATE : 0),
                                      nullptr, SHGFP_TYPE_CURRENT, buffer);
        if (FAILED(hr))
            return ShellResult::Error(hr);

        path = buffer;
        return ShellResult::Ok();
    }
};

// Global instance
static Win32Shell g_win32Shell;
IShell* gShell = &g_win32Shell;

IShell* GetWin32Shell()
{
    return &g_win32Shell;
}
