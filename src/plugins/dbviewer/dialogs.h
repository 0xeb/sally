// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <string>

#include "find_history.h"

class CRendererWindow;

extern sally::dbviewer::FindHistoryEntries FindHistory;

//****************************************************************************
//
// CCommonDialog
//
// Dialog centered relative to the parent
//

class CCommonDialog : public CDialog
{
public:
    CCommonDialog(HINSTANCE hInstance, int resID, HWND hParent);
    CCommonDialog(HINSTANCE hInstance, int resID, int helpID, HWND hParent);

protected:
    INT_PTR DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam);

    virtual void NotifDlgJustCreated();
};

//****************************************************************************
//
// CGoToDialog
//

class CGoToDialog : public CCommonDialog
{
protected:
    int* pRecord;
    int RecordCount;

public:
    CGoToDialog(HWND hParent, int* record, int recordCount);

    virtual void Transfer(CTransferInfo& ti);

protected:
    virtual INT_PTR DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam);
};

//****************************************************************************
//
// CCSVOptionsDialog
//

struct CCSVConfig;

class CCSVOptionsDialog : public CCommonDialog
{
private:
    CCSVConfig* CfgCSV;
    CCSVConfig* DefaultCfgCSV;

public:
    CCSVOptionsDialog(HWND hParent, CCSVConfig* cfgCSV, CCSVConfig* defaultCfgCSV);

    virtual void Transfer(CTransferInfo& ti);

protected:
    INT_PTR DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam);

private:
    void MyTransfer(CTransferInfo& ti, CCSVConfig* cfg);
    void EnableControls();
};

//****************************************************************************
//
// CPropertiesDialog
//

class CPropertiesDialog : public CCommonDialog
{
protected:
    CRendererWindow* Renderer;

public:
    CPropertiesDialog(HWND hParent, CRendererWindow* renderer);

    virtual void Transfer(CTransferInfo& ti);
};

//****************************************************************************
//
// CConfigurationDialog
//

class CConfigurationDialog : public CCommonDialog
{
protected:
    BOOL bEnableCSVOptions;
    HFONT HFont;
    BOOL UseCustomFont;
    LOGFONT LogFont;

public:
    CConfigurationDialog(HWND hParent, BOOL enableCSVOptions);
    ~CConfigurationDialog();

    virtual void Transfer(CTransferInfo& ti);

protected:
    INT_PTR DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam);

    void SetFontText(); // set the text in the control based on the current font
};

//****************************************************************************
//
// CColumnsDialog
//

class CColumnsDialog : public CCommonDialog
{
protected:
    CRendererWindow* Renderer;
    HWND HListView;
    BOOL DisableNotification;
    TDirectArray<CDatabaseColumn> MyColumns; // copy of columns for configuration purposes
    DWORD MinDlgW, MinDlgH, PrevDlgW, PrevDlgH;

public:
    CColumnsDialog(HWND hParent, CRendererWindow* renderer);

    virtual void Transfer(CTransferInfo& ti);
    virtual void Validate(CTransferInfo& ti);

protected:
    INT_PTR DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam);

    void OnMove(BOOL up);

    void RecalcLayout(DWORD NewDlgW, DWORD NewDlgH);

    void SetLVTexts(int index); // configure list view columns according to MyColumns
};

// ****************************************************************************

class CFindDialog : public CCommonDialog
{
public:
    int Forward, // forward/backward (1/0)
        WholeWords,
        CaseSensitive,
        Regular;

    std::wstring Text;

    CFindDialog(HINSTANCE hInstance, int resID, int helpID)
        : CCommonDialog(hInstance, resID, helpID, NULL)
    {
        Forward = TRUE;
        WholeWords = FALSE;
        CaseSensitive = FALSE;
        Regular = FALSE;
    }

    CFindDialog& operator=(const CFindDialog& d)
    {
        Forward = d.Forward;
        WholeWords = d.WholeWords;
        CaseSensitive = d.CaseSensitive;
        Regular = d.Regular;
        Text = d.Text;
        return *this;
    }

    virtual void Transfer(CTransferInfo& ti);

protected:
    INT_PTR DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam);
};
