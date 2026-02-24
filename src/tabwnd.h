// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later
// CommentsTranslationProject: TRANSLATED

#pragma once

//
// ****************************************************************************

class CTabWindow : public CWindow
{
public:
    CFilesWindow* FilesWindow;

    //  protected:
    //    TDirectArray<CTabItem> TabItems;

public:
    CTabWindow(CFilesWindow* filesWindow);
    ~CTabWindow();

    void DestroyWindow();
    int GetNeededHeight();

protected:
    virtual LRESULT WindowProc(UINT uMsg, WPARAM wParam, LPARAM lParam);
};
