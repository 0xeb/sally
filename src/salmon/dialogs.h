// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <string>

enum CDialogTaskEnum
{
    dteCompress,
    dteMinidump,
    dteDialog
};

class CMainDialog : public CDialog
{
protected:
    HFONT HBoldFont;
    BOOL Compressing;
    BOOL Minidumping;
    CCompressParams CompressParams;
    CMinidumpParams MinidumpParams;
    std::wstring CurrentProgressText;
    BOOL MinidumpOnOpen;    // should minidump generation start after opening the window?
    std::wstring IssueAddress; // the new GitHub issue, filled in from the newest report

public:
    CMainDialog(HINSTANCE modul, int resID, BOOL minidumpOnOpen);
    ~CMainDialog();

    virtual void Transfer(CTransferInfo& ti);

protected:
    virtual INT_PTR DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam);

    void ShowChilds(CDialogTaskEnum task, BOOL enable);
    void CenterControl(int resID);

    // Opens the prepared issue in the browser and closes the reporter; 'packed' tells whether
    // the report files were packed into archives, which are then all that is kept of them.
    void FinishReport(BOOL packed);
};
