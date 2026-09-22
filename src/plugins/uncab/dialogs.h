// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

class CDlgRoot
{
public:
    HWND Parent;
    HWND Dlg;

    CDlgRoot(HWND parent)
    {
        Parent = parent;
        Dlg = NULL;
    }

    void CenterDlgToParent();
    void SubClassStatic(DWORD wID, BOOL subclass);
};

class CNextVolumeDialog : public CDlgRoot
{
    std::wstring* VolumePath;
    std::wstring VolumeNameW;
    std::wstring InitialVolumePathW;
    std::wstring DiskNameW;
    int CabNumber;
    std::wstring CurrentPath;

public:
    CNextVolumeDialog(HWND parent, const std::wstring& volumeName, std::wstring* volumePath,
                      const std::wstring& initialVolumePath, const std::wstring& diskName,
                      int cabNumber);
    INT_PTR Proceed();

    INT_PTR DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam);
    BOOL OnInit(WPARAM wParam, LPARAM lParam);
    BOOL OnBrowse(WORD wNotifyCode, WORD wID, HWND hwndCtl);
    BOOL OnOK(WORD wNotifyCode, WORD wID, HWND hwndCtl);
};

// volumePath is both the directory to offer and where the user's choice lands;
// it stays UTF-16 so the browse result is never squeezed through the CAB bytes.
INT_PTR NextVolumeDialog(HWND parent, char* volumeName, std::wstring& volumePath, char* diskName, int cabNumber);

class CContinuedFileDialog : public CDlgRoot
{
    std::wstring File;

public:
    CContinuedFileDialog(HWND parent, const std::wstring& file) : CDlgRoot(parent)
    {
        File = file;
    }
    INT_PTR Proceed();

    INT_PTR DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam);
    BOOL OnInit(WPARAM wParam, LPARAM lParam);
    //    BOOL OnOK(WORD wNotifyCode, WORD wID, HWND hwndCtl);
};

INT_PTR ContinuedFileDialog(HWND parent, const std::wstring& file);

class CConfigDialog : public CDlgRoot
{

public:
    CConfigDialog(HWND parent) : CDlgRoot(parent) { ; }

    INT_PTR Proceed();

    INT_PTR DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam);
    BOOL OnInit(WPARAM wParam, LPARAM lParam);
    BOOL OnOK(WORD wNotifyCode, WORD wID, HWND hwndCtl);
};

INT_PTR ConfigDialog(HWND parent);

class CAttentionDialog : public CDlgRoot
{
public:
    CAttentionDialog(HWND parent) : CDlgRoot(parent) { ; }

    INT_PTR Proceed();

    INT_PTR DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam);
    BOOL OnOK(WORD wNotifyCode, WORD wID, HWND hwndCtl);
};

INT_PTR AttentionDialog(HWND parent);
