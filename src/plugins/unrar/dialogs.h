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

// Wide. This dialog is created with DialogBoxParam, which is DialogBoxParamW -
// so its edit control is a Unicode window and WM_GETTEXT hands back wchar_t. Reading it into a
// char[MAX_PATH] (as OnOK did) overran that buffer by a factor of two and produced a garbage
// volume name; the archive-path plumbing around it is wide anyway.
class CNextVolumeDialog : public CDlgRoot
{
    std::wstring& VolumeName;
    std::wstring CurrentPath;
    LPCWSTR Message;

public:
    CNextVolumeDialog(HWND parent, std::wstring& volumeName,
                      const wchar_t* message = NULL);
    INT_PTR Proceed();

    INT_PTR DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam);
    BOOL OnInit(WPARAM wParam, LPARAM lParam);
    BOOL OnBrowse(WORD wNotifyCode, WORD wID, HWND hwndCtl);
    BOOL OnOK(WORD wNotifyCode, WORD wID, HWND hwndCtl);
};

INT_PTR NextVolumeDialog(HWND parent, std::wstring& volumeName,
                         const wchar_t* message = NULL);

class CContinuedFileDialog : public CDlgRoot
{
    const wchar_t* File;

public:
    CContinuedFileDialog(HWND parent, const wchar_t* file) : CDlgRoot(parent)
    {
        File = file;
    }
    INT_PTR Proceed();

    INT_PTR DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam);
    BOOL OnInit(WPARAM wParam, LPARAM lParam);
    BOOL OnOK(WORD wNotifyCode, WORD wID, HWND hwndCtl);
};

INT_PTR ContinuedFileDialog(HWND parent, const wchar_t* file);

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

#define PD_NOSKIP 0x01
#define PD_NOSKIPALL 0x02
#define PD_NOALL 0x04

// Wide, for the same reason as CNextVolumeDialog above and with a sharper edge:
// WM_GETTEXT with a MAX_PASSWORD count against char Password[MAX_PASSWORD] overwrote MAX_PASSWORD
// bytes past the end of a CPluginDataInterface member.
class CPasswordDialog : public CDlgRoot
{
    wchar_t* Password;
    const wchar_t* FileName;
    DWORD Flags;

public:
    CPasswordDialog(HWND parent, const wchar_t* fileName, wchar_t* password, DWORD flags) : CDlgRoot(parent)
    {
        Password = password;
        FileName = fileName;
        Flags = flags;
    }

    INT_PTR Proceed();

    INT_PTR DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam);
    BOOL OnInit(WPARAM wParam, LPARAM lParam);
};

INT_PTR PasswordDialog(HWND parent, const wchar_t* fileName, wchar_t* password, DWORD flags);

class CAttentionDialog : public CDlgRoot
{
public:
    CAttentionDialog(HWND parent) : CDlgRoot(parent) { ; }

    INT_PTR Proceed();

    INT_PTR DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam);
    BOOL OnOK(WORD wNotifyCode, WORD wID, HWND hwndCtl);
};

INT_PTR AttentionDialog(HWND parent);
