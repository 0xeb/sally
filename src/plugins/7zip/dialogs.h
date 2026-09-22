// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

//****************************************************************************
//
// CCommonDialog
//
// Dialog centered to the parent
//

class CCommonDialog : public CDialog
{
public:
    CCommonDialog(HINSTANCE hInstance, int resID, HWND hParent, CObjectOrigin origin = ooStandard);
    CCommonDialog(HINSTANCE hInstance, int resID, int helpID, HWND hParent, CObjectOrigin origin = ooStandard);

protected:
    INT_PTR DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam);

    virtual void NotifDlgJustCreated();
};

//****************************************************************************
//
// CCompressParamsDlg
//

class CCompressParamsDlg
{
private:
    int IDCfgCompressLevel;
    int IDCfgCompressMethod;
    int IDCfgDictSize;
    int IDCfgWordSize;
    int IDCfgSolidArchive;

public:
    CCompressParams* CompressParams;
    HWND HWindow;

    CCompressParamsDlg(CCompressParams* compressParams, int IDCfgCompressLevel, int IDCfgCompressMethod, int IDCfgDictSize,
                       int IDCfgWordSize, int IDCfgSolidArchive);
    virtual void Transfer(CTransferInfo& ti);

    INT_PTR DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam);

protected:
    void FillCompressLevelCombo();
    void FillCompressMethodCombo();
    void FillDictSizeCombo(int values[], int size);
    void FillWordSizeCombo(int values[], int size);

    void OnChangeMethod();
    void OnChangeLevel();
};

//****************************************************************************
//
// CConfigurationDialog
//

class CConfigurationDialog : public CCommonDialog
{
public:
    CCompressParamsDlg CompressParamsDlg;
    CConfig Cfg;

    CConfigurationDialog(HWND hParent);

    virtual void Transfer(CTransferInfo& ti);

protected:
    INT_PTR DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam);
};

//****************************************************************************
//
// CNewArchiveDialog
//

#define PASSWORD_LEN 128

class CExtOptionsDialog : public CCommonDialog
{
private:
    std::wstring Archive;

    char Password[PASSWORD_LEN];
    char ConfirmedPassword[PASSWORD_LEN];
    BOOL NotAgain;
    BOOL Encrypt;

    // Owning, like Archive above. It was a raw non-owning pointer whose only
    // two callers fed it LangStr(...).c_str() - a temporary destroyed at the
    // end of that statement, long before Execute() ran and WM_INITDIALOG read
    // it, so the dialog caption came from freed memory.
    std::wstring Title;

    CCompressParamsDlg CompressParamsDlg;

public:
    CCompressParams CompressParams;

    CExtOptionsDialog(HWND hParent);
    virtual void Transfer(CTransferInfo& ti);
    BOOL CreateChilds();

    BOOL IsPasswordDefined() { return Encrypt; }
    char* GetPassword() { return Password; }
    BOOL GetNotAgain() { return NotAgain; }
    void SetArchiveName(const wchar_t* archiveName) { Archive = archiveName != NULL ? archiveName : L""; }
    void SetTitle(const wchar_t* title) { Title = title != NULL ? title : L""; }

protected:
    INT_PTR DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam);
};

//****************************************************************************
//
// CEnterPasswordDialog
//

class CEnterPasswordDialog : public CCommonDialog
{
private:
    //    char FileName[MAX_PATH];
    char Password[PASSWORD_LEN];

public:
    CEnterPasswordDialog(HWND hParent);
    virtual void Transfer(CTransferInfo& ti);

    char* GetPassword() { return Password; }
    //    void SetFileName(const char *fileName) { lstrcpy(FileName, fileName); }

protected:
    INT_PTR DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam);
};
