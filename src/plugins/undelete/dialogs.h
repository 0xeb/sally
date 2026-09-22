// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

class CSnapshotProgressDlg : public CDialog
{
protected:
    CGUIProgressBarAbstract* ProgressBar;
    BOOL WantCancel; // TRUE when user wants Cancel

public:
    CSnapshotProgressDlg(HWND parent, CObjectOrigin origin = ooStandard);
    void SetProgressText(int resID);
    void SetProgressText(int resID, int number);
    void SetProgress(DWORD progress);
    BOOL GetWantCancel();

protected:
    virtual INT_PTR DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam);
};

class CCopyProgressDlg : public CDialog
{
protected:
    CGUIProgressBarAbstract *ProgressBar1, *ProgressBar2;
    CGUIStaticTextAbstract *Label1, *Label2;
    BOOL WantCancel;
    std::wstring SrcName;
    std::wstring DestName;
    DWORD LastTick, FileProgress, TotalProgress;
    BOOL Changed[4];

public:
    CCopyProgressDlg(HWND parent, CObjectOrigin origin = ooStandard);
    void SetSourceFileName(const wchar_t* fileName);
    void SetDestFileName(const wchar_t* fileName);
    void SetFileProgress(DWORD progress);
    void SetTotalProgress(DWORD progress);
    BOOL GetWantCancel();
    void UpdateControls(BOOL now = FALSE);

protected:
    virtual INT_PTR DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam);
};

class CConnectDialog : public CDialog
{
public:
    CConnectDialog(HWND parent, int panel);
    std::wstring Volume;
    int Panel;

protected:
    HWND hList;
    HIMAGELIST hDrivesImg;

    void AddVolumeDetails(const wchar_t* root, const wchar_t* volumeID, const wchar_t* volumeFS,
                          const CQuadWord& bytesTotal, const CQuadWord& bytesFree,
                          const wchar_t* volumeName, int serial, BOOL selected);
    void InitDrives();
    BOOL OnDialogOK();
    void OnImageBrowse();

public:
    virtual void Transfer(CTransferInfo& ti);
    virtual INT_PTR DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam);
};

class CFileNameDialog : public CDialog
{
public:
    CFileNameDialog(HWND parent, std::wstring& filename);
    BOOL AllPressed;

protected:
    std::wstring& FileName;

    virtual void Transfer(CTransferInfo& ti);
    virtual INT_PTR DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam);
};

class CConfigDialog : public CDialog
{
public:
    CConfigDialog(HWND parent);

protected:
    virtual void Transfer(CTransferInfo& ti);
    virtual INT_PTR DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam);
};

class CRestoreDialog : public CDialog
{
public:
    CRestoreDialog(HWND parent);

    std::wstring TargetPath;

protected:
    virtual INT_PTR DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam);
};

class CRestoreProgressDlg : public CCopyProgressDlg
{
public:
    CRestoreProgressDlg(HWND parent, CObjectOrigin origin = ooStandard)
        : CCopyProgressDlg(parent, origin) {}

protected:
    virtual INT_PTR DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam);
};
