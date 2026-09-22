// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once


//#define PD_NO_SELFEXT   0x1
//#define PD_NO_MULTIVOL  0x2
#define PD_REMOVALBE 0x4
#define PD_NEWARCHIVE 0x8

#define LSD_NOIGNORE 0x1

#define CHD_ALL 0x1
#define CHD_SEQNAMES 0x2
#define CHD_FIRST 0x4
#define CHD_WINZIP 0x8 // prefer winzip names

#define WM_USER_GETICON (WM_APP + 1)
#define WM_USER_SETVOLSIZE (WM_APP + 2)

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
    void SubClassStatic(DWORD wID, bool subclass);
    void SubClassSmallIcon(DWORD wID, bool subclass);
};

class CPackDialog : public CDlgRoot
{
    CZipPack* PackObject; //used in adcanced self-extr settings dialog
    CConfiguration* Config;
    CExtendedOptions* PackOptions;
    //unsigned            CustomVolSize;//in KB
    char DecimalSeparator[5];
    int DecimalSeparatorLen;
    //unsigned            Units;
    std::wstring ZipFile;
    unsigned Flags;
    HWND CBEditCtrl;

public:
    CPackDialog(HWND parent, CZipPack* packObject, CConfiguration* config,
                CExtendedOptions* packOptions, const wchar_t* zipFile, unsigned flags) : CDlgRoot(parent)
    {
        PackObject = packObject;
        Config = config;
        PackOptions = packOptions;
        Flags = flags;
        ZipFile = zipFile != NULL ? zipFile : L"";
        CBEditCtrl = NULL;
        DecimalSeparator[0] = '.';
        DecimalSeparator[1] = 0;
        DecimalSeparatorLen = 1;
    }
    INT_PTR Proceed();

    void SubClassComboBox(DWORD wID, bool subclass);
    INT_PTR DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam);
    BOOL OnInit(WPARAM wParam, LPARAM lParam);
    BOOL OnMultiVol(WORD wNotifyCode, WORD wID, HWND hwndCtl);
    BOOL OnSeqName(WORD wNotifyCode, WORD wID, HWND hwndCtl);
    BOOL OnSelfExtr(WORD wNotifyCode, WORD wID, HWND hwndCtl);
    BOOL OnEncrypt(WORD wNotifyCode, WORD wID, HWND hwndCtl);
    //BOOL OnUnits(WORD wNotifyCode, WORD wID, HWND hwndCtl);
    BOOL SetVolSize(WPARAM wParam, LPARAM lParam);
    BOOL OnVolSize(WORD wNotifyCode, WORD wID, HWND hwndCtl);
    BOOL OnAdvanced(WORD wNotifyCode, WORD wID, HWND hwndCtl);
    BOOL OnConfig(WORD wNotifyCode, WORD wID, HWND hwndCtl);
    BOOL OnOK(WORD wNotifyCode, WORD wID, HWND hwndCtl);
    void ResetControls();
};

INT_PTR PackDialog(HWND parent, CZipPack* packObject, CConfiguration* config,
                   CExtendedOptions* packOptions, const wchar_t* zipFile, unsigned flags);

class CConfigDialog : public CDlgRoot
{
    CConfiguration* Config;

public:
    CConfigDialog(HWND parent, CConfiguration* config) : CDlgRoot(parent)
    {
        Config = config;
    }
    INT_PTR Proceed();

    INT_PTR DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam);
    BOOL OnInit(WPARAM wParam, LPARAM lParam);
    BOOL OnOK(WORD wNotifyCode, WORD wID, HWND hwndCtl);
    BOOL OnDefault(WORD wNotifyCode, WORD wID, HWND hwndCtl);
};

class CPasswordDialog : public CDlgRoot
{
    HICON Lock;
    const wchar_t* File;
    char* Password;

public:
    CPasswordDialog(HWND parent, const wchar_t* file, char* password) : CDlgRoot(parent)
    {
        File = file;
        Password = password;
    }

    INT_PTR Proceed();

    INT_PTR DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam);
    BOOL OnInit(WPARAM wParam, LPARAM lParam);
    BOOL OnOK(WORD wNotifyCode, WORD wID, HWND hwndCtl);
};

INT_PTR PasswordDialog(HWND parent, const wchar_t* file, char* password);

class CLowDiskSpaceDialog : public CDlgRoot
{
    const wchar_t* Text;
    const wchar_t* Path;
    __INT64 FreeSpace;
    __INT64 VolumeSize;
    int Flags;

public:
    // W - these reach the controls via SendDlgItemMessage(WM_SETTEXT), which
    // resolves to the W form under UNICODE, so narrow buffers were being read as UTF-16.
    CLowDiskSpaceDialog(HWND parent, const wchar_t* text, const wchar_t* path,
                        __INT64 freeSpace, __INT64 volumeSize, int flags) : CDlgRoot(parent)
    {
        Text = text;
        Path = path;
        FreeSpace = freeSpace;
        VolumeSize = volumeSize;
        Flags = flags;
    }

    INT_PTR Proceed();

    INT_PTR DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam);
    BOOL OnInit(WPARAM wParam, LPARAM lParam);
};

INT_PTR LowDiskSpaceDialog(HWND parent, const wchar_t* text, const wchar_t* path,
                           __INT64 freeSpace, __INT64 volumeSize, int flags);

class CChangeDiskDialog : public CDlgRoot
{
    const wchar_t* Text;

public:
    CChangeDiskDialog(HWND parent, const wchar_t* text) : CDlgRoot(parent)
    {
        Text = text;
    }

    INT_PTR Proceed();

    INT_PTR DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam);
    BOOL OnInit(WPARAM wParam, LPARAM lParam);
};

INT_PTR ChangeDiskDialog(HWND parent, const wchar_t* text);

class CChangeDiskDialog2 : public CDlgRoot
{
    //int     VolumeNumber;
    std::wstring& FileName;
    std::wstring CurrentPath;

public:
    CChangeDiskDialog2(HWND parent, std::wstring& fileName) : CDlgRoot(parent), FileName(fileName)
    {
        CurrentPath = FileName;
        SPLCutDirectoryOwned(SalamanderGeneral, CurrentPath);
    }

    INT_PTR Proceed();

    INT_PTR DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam);
    BOOL OnInit(WPARAM wParam, LPARAM lParam);
    BOOL OnBrowse(WORD wNotifyCode, WORD wID, HWND hwndCtl);
    BOOL OnOK(WORD wNotifyCode, WORD wID, HWND hwndCtl);
};

INT_PTR ChangeDiskDialog2(HWND parent, std::wstring& fileName);

class CChangeDiskDialog3 : public CDlgRoot
{
    int VolumeNumber;
    bool Last;
    std::wstring& OldName;
    unsigned* Flags;
    std::wstring CurrentPath;

public:
    CChangeDiskDialog3(HWND parent, int volNum, bool last,
                       std::wstring& fileName, unsigned* flags) : CDlgRoot(parent), OldName(fileName)
    {
        VolumeNumber = volNum;
        Last = last;
        Flags = flags;
        CurrentPath = OldName;
        SPLCutDirectoryOwned(SalamanderGeneral, CurrentPath);
    }

    INT_PTR Proceed();

    INT_PTR DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam);
    BOOL OnInit(WPARAM wParam, LPARAM lParam);
    BOOL OnSeqNames(WORD wNotifyCode, WORD wID, HWND hwndCtl);
    BOOL OnWinZip(WORD wNotifyCode, WORD wID, HWND hwndCtl);
    BOOL OnBrowse(WORD wNotifyCode, WORD wID, HWND hwndCtl);
    BOOL OnOK(WORD wNotifyCode, WORD wID, HWND hwndCtl);
};

INT_PTR ChangeDiskDialog3(HWND parent, int volNum, bool last,
                          std::wstring& fileName, unsigned* flags);

class COverwriteDialog : public CDlgRoot
{
    const wchar_t* File;
    const wchar_t* Attr;

public:
    COverwriteDialog(HWND parent, const wchar_t* file, const wchar_t* attr) : CDlgRoot(parent)
    {
        File = file;
        Attr = attr;
    }

    INT_PTR Proceed();

    INT_PTR DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam);
    BOOL OnInit(WPARAM wParam, LPARAM lParam);
};

INT_PTR OverwriteDialog(HWND parent, const wchar_t* file, const wchar_t* attr);

class COverwriteDialog2 : public CDlgRoot
{
    const wchar_t* File;
    const wchar_t* Attr;

public:
    COverwriteDialog2(HWND parent, const wchar_t* file, const wchar_t* attr) : CDlgRoot(parent)
    {
        File = file;
        Attr = attr;
    }

    INT_PTR Proceed();

    INT_PTR DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam);
    BOOL OnInit(WPARAM wParam, LPARAM lParam);
};

INT_PTR OverwriteDialog2(HWND parent, const wchar_t* file, const wchar_t* attr);

class CAdvancedSEDialog : public CDlgRoot
{
    CExtendedOptions* PackOptions;
    CZipPack* PackObject;
    CSfxSettings TmpSfxSettings;
    HMENU Menu;
    HMENU FavoritiesMenu;
    HMENU SpecDirMenu;
    HMENU SpecDirMenuPopup;
    HACCEL Accel;
    HICON LargeIcon;
    HICON SmallIcon;
    int Result;
    CSfxLang* CurrentSfxLang;
    int* ChangeLangReaction;

public:
    CAdvancedSEDialog(HWND parent, CZipPack* packObject, CExtendedOptions* packOptions, int* changeLangReaction) : CDlgRoot(parent)
    {
        PackObject = packObject;
        PackOptions = packOptions;
        TmpSfxSettings = PackOptions->SfxSettings;
        LargeIcon = NULL;
        SmallIcon = NULL;
        Result = -1;
        SpecDirMenu = NULL;
        SpecDirMenuPopup = NULL;
        ChangeLangReaction = changeLangReaction;
    }

    int Proceed();

    INT_PTR DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam);
    BOOL OnInit(WPARAM wParam, LPARAM lParam);
    void ResetValueControls();
    void EndDialog(int result);
    BOOL OnTargetDir(WORD wNotifyCode, WORD wID, HWND hwndCtl);
    BOOL OnSpecDir(WORD wNotifyCode, WORD wID, HWND hwndCtl);
    BOOL OnSpecDirMenu(WORD itemID);
    BOOL OnRemove(WORD wNotifyCode, WORD wID, HWND hwndCtl);
    BOOL OnWaitFor(WORD wNotifyCode, WORD wID, HWND hwndCtl);
    BOOL OnTexts(WORD wNotifyCode, WORD wID, HWND hwndCtl);
    BOOL OnAuto(WORD wNotifyCode, WORD wID, HWND hwndCtl);
    BOOL OnChangeIcon(WORD wNotifyCode, WORD wID, HWND hwndCtl);
    BOOL OnChangeLanguage(WORD wNotifyCode, WORD wID, HWND hwndCtl);
    BOOL OnOK(WORD wNotifyCode, WORD wID, HWND hwndCtl);
    BOOL GetSettings(CSfxSettings* sfxSettings);
    BOOL InitMenu();
    BOOL CreateFavoritesMenu();
    BOOL OnImport();
    BOOL OnExport();
    BOOL OnPreview();
    BOOL OnResetAll();
    BOOL OnResetValues();
    BOOL OnResetTexts();
    BOOL OnAddFavorite();
    BOOL OnRenameFavorite();
    BOOL OnDeleteteFavorite();
    BOOL OnRemoveAllFavorities();
    BOOL OnManageFavorities();
    BOOL OnFavorities();
    BOOL OnFavoriteOption(WORD itemID);
    BOOL OnLastUsed();
    BOOL LoadFavSettings(CSfxSettings* sfxSettings);
};

class CSfxTextsDialog : public CDlgRoot
{
    CSfxSettings* SfxSettings;
    CSfxLang* CurrentSfxLang;

public:
    CSfxTextsDialog(HWND parent, CSfxSettings* sfxSettings, CSfxLang* currentSfxLang) : CDlgRoot(parent)
    {
        SfxSettings = sfxSettings;
        CurrentSfxLang = currentSfxLang;
    }

    INT_PTR Proceed();

    INT_PTR DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam);
    BOOL OnInit(WPARAM wParam, LPARAM lParam);
    void ResetControls(UINT mboxStyle, const char* mboxTitle, const char* mboxText,
                       const char* title, const char* text, const char* button,
                       const char* vendor, const char* www);
    BOOL OnOK(WORD wNotifyCode, WORD wID, HWND hwndCtl);
    BOOL OnReset();
};

class CManageFavoritiesDialog : public CDlgRoot
{
public:
    BOOL FavFocus;

    CManageFavoritiesDialog(HWND parent) : CDlgRoot(parent)
    {
        FavFocus = FALSE;
    }

    INT_PTR Proceed();

    INT_PTR DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam);
    BOOL OnInit(WPARAM wParam, LPARAM lParam);
    //BOOL OnOK(WORD wNotifyCode, WORD wID, HWND hwndCtl);
    BOOL OnFavorities(WORD wNotifyCode, WORD wID, HWND hwndCtl);
    BOOL OnRenameFavorite();
    BOOL OnRemoveFavorite();
    BOOL OnRemoveAllFavorities();
};

INT_PTR ManageFavoritiesDialog(HWND parent);

class CRenFavDialog : public CDlgRoot
{
    bool Rename;
    char* Name;

public:
    CRenFavDialog(HWND parent, char* name, bool rename) : CDlgRoot(parent)
    {
        Name = name;
        Rename = rename;
    }

    INT_PTR Proceed();

    INT_PTR DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam);
    BOOL OnInit(WPARAM wParam, LPARAM lParam);
    BOOL OnOK(WORD wNotifyCode, WORD wID, HWND hwndCtl);
};

class CCreateSFXDialog : public CDlgRoot
{
    CZipPack* PackObject; //used in adcanced self-extr settings dialog
    CExtendedOptions* PackOptions;
    std::wstring& ZipName;
    std::wstring& ExeName;

public:
    CCreateSFXDialog(HWND parent, std::wstring& zipName, std::wstring& exeName,
                     CExtendedOptions* options, CZipPack* packObject) : CDlgRoot(parent), ZipName(zipName), ExeName(exeName)
    {
        PackOptions = options;
        PackObject = packObject;
    }

    INT_PTR Proceed();

    INT_PTR DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam);
    BOOL OnInit(WPARAM wParam, LPARAM lParam);
    BOOL OnOK(WORD wNotifyCode, WORD wID, HWND hwndCtl);
    BOOL OnAdvanced(WORD wNotifyCode, WORD wID, HWND hwndCtl);
};

class CCommentDialog : public CDlgRoot
{
    HWND CommentHWnd;
    CZipPack* PackObject;
    BOOL FirstShow;

public:
    CCommentDialog(HWND parent, CZipPack* packObject) : CDlgRoot(parent)
    {
        PackObject = packObject;
        FirstShow = TRUE;
    }

    INT_PTR Proceed();

    INT_PTR DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam);
    BOOL OnInit(WPARAM wParam, LPARAM lParam);
    BOOL OnCancel();
    BOOL OnSave();
    BOOL Save();
};

class CWaitForDialog : public CDlgRoot
{
    char* EncodedOutput;
    size_t EncodedCapacity;
    std::wstring WaitFor;

public:
    CWaitForDialog(HWND parent, char* waitFor, size_t waitForCapacity) : CDlgRoot(parent)
    {
        EncodedOutput = waitFor;
        EncodedCapacity = waitForCapacity;
        WaitFor = ZipTextToWide(waitFor);
    }

    INT_PTR Proceed();

    INT_PTR DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam);
    BOOL OnInit(WPARAM wParam, LPARAM lParam);
    BOOL OnOK();
};

INT_PTR
WaitForDialog(HWND parent, char* waitFor, size_t waitForCapacity);

class CChangeTextsDialog : public CDlgRoot
{
    int* ChangeLangReaction;

public:
    CChangeTextsDialog(HWND parent, int* changeLangReaction) : CDlgRoot(parent)
    {
        ChangeLangReaction = changeLangReaction;
    }

    INT_PTR Proceed();
    INT_PTR DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam);
};

INT_PTR
ChangeTextsDialog(HWND parent, int* changeLangReaction);

extern TIndirectArray2<CSfxLang>* SfxLanguages;
extern CSfxLang* DefLanguage;

extern TIndirectArray2<CFavoriteSfx> Favorities;
extern CFavoriteSfx LastUsedSfxSet;

int CompareMenuItems(char* name1, char* name2);

BOOL LoadSfxLangs(HWND dlg, const wchar_t* selectedSfxFile, bool isConfig);
BOOL LoadLangChache(HWND parent);
int FormatNumber(__UINT64 number, wchar_t* buffer, const wchar_t* text);
