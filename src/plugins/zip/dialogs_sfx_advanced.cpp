// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"
#include <crtdbg.h>
#include <ostream>
#include <commctrl.h>
#include <stdio.h>

#include "spl_com.h"
#include "spl_base.h"
#include "spl_gen.h"
#include "spl_arc.h"
#include "spl_menu.h"
#include "dbg.h"

#include "array2.h"

#include "selfextr\\comdefs.h"
#include "config.h"
#include "typecons.h"
#include "zip.rh"
#include "zip.rh2"
#include "lang\lang.rh"
#include "chicon.h"
#include "common.h"
#include "add_del.h"
#include "dialogs.h"
#include "prevsfx.h"

#include "iosfxset.h"

TIndirectArray2<CFavoriteSfx> Favorities(8);
CFavoriteSfx LastUsedSfxSet;

//******************************************************************************
//
// CAdvancedSEDialog
//

INT_PTR WINAPI AdvancedSEDlgProc(HWND hDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    CALL_STACK_MESSAGE4("AdvancedSEDlgProc(, 0x%X, 0x%IX, 0x%IX)", uMsg, wParam,
                        lParam);
    static CAdvancedSEDialog* dlg = NULL;

    switch (uMsg)
    {
    case WM_INITDIALOG:
        SalamanderGUI->ArrangeHorizontalLines(hDlg);
        dlg = (CAdvancedSEDialog*)lParam;
        dlg->Dlg = hDlg;
        return dlg->DialogProc(uMsg, wParam, lParam);

    default:
        if (dlg)
            return dlg->DialogProc(uMsg, wParam, lParam);
    }
    return FALSE;
}

int CAdvancedSEDialog::Proceed()
{
    CALL_STACK_MESSAGE1("CAdvancedSEDialog::Proceed()");

    EnableWindow(Parent, FALSE);

    CreateDialogParam(HLanguage, MAKEINTRESOURCE(IDD_ADVANCEDSE),
                      Parent, AdvancedSEDlgProc, (LPARAM)this);
    if (!Dlg)
        return -1;

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0)) // && IsWindow(Dlg))
    {
        if (!TranslateAccelerator(Dlg, Accel, &msg) &&
            !IsDialogMessage(Dlg, &msg))
        {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    EnableWindow(Parent, TRUE);

    return Result;
}

INT_PTR
CAdvancedSEDialog::DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    CALL_STACK_MESSAGE4("CAdvancedSEDialog::DialogProc(0x%X, 0x%IX, 0x%IX)", uMsg,
                        wParam, lParam);
    switch (uMsg)
    {
    case WM_INITDIALOG:
        return OnInit(wParam, lParam);
    case WM_COMMAND:
        switch (LOWORD(wParam))
        {
        //controls
        /*
        case IDC_CURRENT:
          return OnCurrent(HIWORD(wParam), LOWORD(wParam), (HWND) lParam);
        case IDC_TEMP:
          return OnTemp(HIWORD(wParam), LOWORD(wParam), (HWND) lParam);
        */
        case IDC_REMOVE:
            return OnRemove(HIWORD(wParam), LOWORD(wParam), (HWND)lParam);
        case IDC_WAITFOR:
            return OnWaitFor(HIWORD(wParam), LOWORD(wParam), (HWND)lParam);
        case IDC_TEXTS:
            return OnTexts(HIWORD(wParam), LOWORD(wParam), (HWND)lParam);
        case IDC_TARGETDIR:
            return OnTargetDir(HIWORD(wParam), LOWORD(wParam), (HWND)lParam);
        case IDC_SPECDIR:
            return OnSpecDir(HIWORD(wParam), LOWORD(wParam), (HWND)lParam);
        case IDC_AUTO:
            return OnAuto(HIWORD(wParam), LOWORD(wParam), (HWND)lParam);
        case IDC_CHANGEICON:
            return OnChangeIcon(HIWORD(wParam), LOWORD(wParam), (HWND)lParam);
        case IDC_LANGUAGE:
            return OnChangeLanguage(HIWORD(wParam), LOWORD(wParam), (HWND)lParam);
        case IDOK:
            return OnOK(HIWORD(wParam), LOWORD(wParam), (HWND)lParam);
        case IDCANCEL:
            EndDialog(IDCANCEL);
            return TRUE;
        case IDHELP:
            SalamanderGeneral->OpenHtmlHelp(Dlg, HHCDisplayContext, IDD_ADVANCEDSE, FALSE);
            return TRUE;

        //spec dir menu
        case CM_TEMP:
        case CM_PROGFILES:
        case CM_WINDIR:
        case CM_SYSDIR:
        case CM_ENVVAR:
        case CM_REGENTRY:
            return OnSpecDirMenu(LOWORD(wParam));
        //menu
        case CM_SFX_EXPORT:
            return OnExport();
        case CM_SFX_IMPORT:
            return OnImport();
        case CM_SFX_PREVIEW:
            return OnPreview();
        case CM_SFX_RESETALL:
            return OnResetAll();
        case CM_SFX_RESETVALUES:
            return OnResetValues();
        case CM_SFX_RESETTEXTS:
            return OnResetTexts();
        case CM_SFX_ADD:
            return OnAddFavorite();
        case CM_SFX_FAVORITIES:
            return OnFavorities();
        case CM_SFX_MANAGE:
            return OnManageFavorities();
        case CM_SFX_LASTUSED:
            return OnLastUsed();

        default:
        {
            if (LOWORD(wParam) >= CM_SFX_FAVORITE &&
                LOWORD(wParam) < CM_SFX_FAVORITE + Favorities.Count)
                return OnFavoriteOption(LOWORD(wParam));
        }
        }
        break;

    case WM_HELP:
        SalamanderGeneral->OpenHtmlHelp(Dlg, HHCDisplayContext, IDD_ADVANCEDSE, FALSE);
        return TRUE;
        /*
    case WM_CONTEXTMENU:
    {
      HMENU  menu = LoadMenu(HLanguage, MAKEINTRESOURCE(IDM_FAVMANMENU));
      if (menu)
      {
        HMENU subMenu = GetSubMenu(menu, 0);
        if (subMenu)
        {
          int ret = TrackPopupMenuEx(subMenu, TPM_LEFTALIGN | TPM_TOPALIGN | TPM_NONOTIFY | TPM_RETURNCMD,
                           LOWORD(lParam), HIWORD(lParam), NULL, NULL);
          ret = GetLastError();
        }
        DestroyMenu(menu);
      }
      break;
    }
    */

    case WM_DESTROY:
        if (SpecDirMenu)
            DestroyMenu(SpecDirMenu);
        if (LargeIcon)
            DestroyIcon(LargeIcon);
        if (SmallIcon)
            DestroyIcon(SmallIcon);
        SubClassSmallIcon(IDC_EXEICON, false);
        PostQuitMessage(0);
        break;

    case WM_USER_GETICON:
        SetWindowLongPtr(Dlg, DWLP_MSGRESULT, (LONG_PTR)SmallIcon);
        return TRUE;
    }
    return FALSE;
}

BOOL CAdvancedSEDialog::OnInit(WPARAM wParam, LPARAM lParam)
{
    CALL_STACK_MESSAGE3("CAdvancedSEDialog::OnInit(0x%IX, 0x%IX)", wParam, lParam);
    ExtractIconExW(ZipTextToWide(TmpSfxSettings.IconFile).c_str(), TmpSfxSettings.IconIndex,
                  &LargeIcon, &SmallIcon, 1);
    SubClassSmallIcon(IDC_EXEICON, true);

    ResetValueControls();

    const std::wstring selectedSfxFile = ZipTextToWide(TmpSfxSettings.SfxFile);
    if (!LoadSfxLangs(Dlg, selectedSfxFile.c_str(), false))
    {
        EndDialog(IDCANCEL);
        return TRUE;
    }

    if (!InitMenu())
    {
        EndDialog(IDCANCEL);
        return TRUE;
    }

    LRESULT i = SendDlgItemMessage(Dlg, IDC_LANGUAGE, CB_GETCURSEL, 0, 0);
    if (i != CB_ERR)
    {
        CurrentSfxLang = (CSfxLang*)SendDlgItemMessage(Dlg, IDC_LANGUAGE, CB_GETITEMDATA, i, 0);
        if ((LRESULT)CurrentSfxLang == CB_ERR)
        {
            TRACE_E("nepodarilo se ziskat aktualni jazyk z comboboxu");
            CurrentSfxLang = NULL;
        }
    }
    else
    {
        TRACE_E("nepodarilo se ziskat aktualni jazyk z comboboxu");
        CurrentSfxLang = NULL;
    }

    CenterDlgToParent();
    return TRUE;
}

void CAdvancedSEDialog::ResetValueControls()
{
    CALL_STACK_MESSAGE1("CAdvancedSEDialog::ResetValueControls()");
    HWND wnd = GetFocus();
    SetFocus(GetDlgItem(Dlg, IDC_TARGETDIR));
    if (wnd)
    {
        int id = GetDlgCtrlID(wnd);
        if (id == IDC_SPECDIR || id == IDC_WAITFOR || id == IDC_TEXTS || id == IDC_CHANGEICON ||
            id == IDCANCEL || id == IDHELP)
        {
            LONG style = GetWindowLong(wnd, GWL_STYLE);
            SetWindowLong(wnd, GWL_STYLE, (style & ~BS_DEFPUSHBUTTON) | BS_PUSHBUTTON);
            wnd = GetDlgItem(Dlg, IDOK);
            style = GetWindowLong(wnd, GWL_STYLE);
            if (style)
                SetWindowLong(wnd, GWL_STYLE, (style & ~BS_PUSHBUTTON) | BS_DEFPUSHBUTTON);
        }
    }
    SendDlgItemMessageW(Dlg, IDC_TARGETDIR, EM_SETLIMITTEXT, 0, 0);
    const std::wstring targetDirectory = ZipTextToWide(TmpSfxSettings.TargetDir);
    SetDlgItemTextW(Dlg, IDC_TARGETDIR, targetDirectory.c_str());
    if (lstrcmpiA(TmpSfxSettings.TargetDir, SFX_TDTEMP))
        EnableWindow(GetDlgItem(Dlg, IDC_REMOVE), FALSE);
    if (!(TmpSfxSettings.Flags & SE_NOTALLOWCHANGE))
        SendDlgItemMessage(Dlg, IDC_ALLOWUSER, BM_SETCHECK, (WPARAM)BST_CHECKED, 0);
    else
        SendDlgItemMessage(Dlg, IDC_ALLOWUSER, BM_SETCHECK, (WPARAM)BST_UNCHECKED, 0);
    if (TmpSfxSettings.Flags & SE_REMOVEAFTER)
    {
        EnableWindow(GetDlgItem(Dlg, IDC_WAITFOR), TRUE);
        SendDlgItemMessage(Dlg, IDC_REMOVE, BM_SETCHECK, (WPARAM)BST_CHECKED, 0);
    }
    else
    {
        EnableWindow(GetDlgItem(Dlg, IDC_WAITFOR), FALSE);
        SendDlgItemMessage(Dlg, IDC_REMOVE, BM_SETCHECK, (WPARAM)BST_UNCHECKED, 0);
    }
    if (TmpSfxSettings.Flags & SE_AUTO)
    {
        SendDlgItemMessage(Dlg, IDC_AUTO, BM_SETCHECK, (WPARAM)BST_CHECKED, 0);
        EnableWindow(GetDlgItem(Dlg, IDC_HIDEMAINDLG), TRUE);
    }
    else
    {
        SendDlgItemMessage(Dlg, IDC_AUTO, BM_SETCHECK, (WPARAM)BST_UNCHECKED, 0);
        EnableWindow(GetDlgItem(Dlg, IDC_HIDEMAINDLG), FALSE);
    }
    if (TmpSfxSettings.Flags & SE_HIDEMAINDLG)
        SendDlgItemMessage(Dlg, IDC_HIDEMAINDLG, BM_SETCHECK, (WPARAM)BST_CHECKED, 0);
    else
        SendDlgItemMessage(Dlg, IDC_HIDEMAINDLG, BM_SETCHECK, (WPARAM)BST_UNCHECKED, 0);
    if (TmpSfxSettings.Flags & SE_SHOWSUMARY)
        SendDlgItemMessage(Dlg, IDC_SUMDLG, BM_SETCHECK, (WPARAM)BST_CHECKED, 0);
    else
        SendDlgItemMessage(Dlg, IDC_SUMDLG, BM_SETCHECK, (WPARAM)BST_UNCHECKED, 0);
    if (TmpSfxSettings.Flags & SE_OVEWRITEALL)
        SendDlgItemMessage(Dlg, IDC_OVEWRITE, BM_SETCHECK, (WPARAM)BST_CHECKED, 0);
    else
        SendDlgItemMessage(Dlg, IDC_OVEWRITE, BM_SETCHECK, (WPARAM)BST_UNCHECKED, 0);
    if (TmpSfxSettings.Flags & SE_AUTODIR)
        SendDlgItemMessage(Dlg, IDC_AUTODIR, BM_SETCHECK, (WPARAM)BST_CHECKED, 0);
    else
        SendDlgItemMessage(Dlg, IDC_AUTODIR, BM_SETCHECK, (WPARAM)BST_UNCHECKED, 0);
    if (TmpSfxSettings.Flags & SE_REQUIRESADMIN)
        SendDlgItemMessage(Dlg, IDC_REQSADMIN, BM_SETCHECK, (WPARAM)BST_CHECKED, 0);
    else
        SendDlgItemMessage(Dlg, IDC_REQSADMIN, BM_SETCHECK, (WPARAM)BST_UNCHECKED, 0);
    SendDlgItemMessageW(Dlg, IDC_EXECUTE, EM_SETLIMITTEXT, 0, 0);
    const std::wstring command = ZipTextToWide(TmpSfxSettings.Command);
    SetDlgItemTextW(Dlg, IDC_EXECUTE, command.c_str());
}

void CAdvancedSEDialog::EndDialog(int result)
{
    CALL_STACK_MESSAGE2("CAdvancedSEDialog::EndDialog(%d)", result);
    EnableWindow(Parent, TRUE);
    DestroyWindow(Dlg);
    Result = result;
}

/*
BOOL CAdvancedSEDialog::OnCurrent(WORD wNotifyCode, WORD wID, HWND hwndCtl)
{
  if (SendDlgItemMessage(Dlg, wID, BM_GETCHECK, 0, 0) == BST_CHECKED)
  {
    //PackOptions->Flags = (PackOptions->Flags & ~SE_DIRFLAGSMASK) | SE_CURRENTDIR;
    SendDlgItemMessage(Dlg, IDC_REMOVE, BM_SETCHECK, (WPARAM) BST_UNCHECKED, 0);
    EnableWindow(GetDlgItem(Dlg, IDC_REMOVE), FALSE);
  }
  return TRUE;
}

BOOL CAdvancedSEDialog::OnTemp(WORD wNotifyCode, WORD wID, HWND hwndCtl)
{
  if (SendDlgItemMessage(Dlg, wID, BM_GETCHECK, 0, 0) == BST_CHECKED)
  {
    //PackOptions->Flags = (PackOptions->Flags & ~SE_DIRFLAGSMASK) | SE_TEMPDIR;
    EnableWindow(GetDlgItem(Dlg, IDC_REMOVE), TRUE);
  }
  return TRUE;
}
*/

BOOL CAdvancedSEDialog::OnTargetDir(WORD wNotifyCode, WORD wID, HWND hwndCtl)
{
    CALL_STACK_MESSAGE3("CAdvancedSEDialog::OnTargetDir(0x%X, 0x%X, )",
                        wNotifyCode, wID);
    if (wNotifyCode == EN_UPDATE)
    {
        std::wstring targetDirectory = SPLGetDlgItemTextOwned(Dlg, IDC_TARGETDIR);
        while (!targetDirectory.empty() && targetDirectory.back() == L' ')
            targetDirectory.pop_back();
        const std::wstring tempDirectory = ZipTextToWide(SFX_TDTEMP);
        if (CompareStringOrdinal(targetDirectory.c_str(), -1, tempDirectory.c_str(), -1, TRUE) == CSTR_EQUAL)
        {
            EnableWindow(GetDlgItem(Dlg, IDC_REMOVE), TRUE);
        }
        else
        {
            SendDlgItemMessage(Dlg, IDC_REMOVE, BM_SETCHECK, (WPARAM)BST_UNCHECKED, 0);
            EnableWindow(GetDlgItem(Dlg, IDC_REMOVE), FALSE);
            EnableWindow(GetDlgItem(Dlg, IDC_WAITFOR), FALSE);
        }
        return TRUE;
    }
    return FALSE;
}

BOOL CAdvancedSEDialog::OnSpecDir(WORD wNotifyCode, WORD wID, HWND hwndCtl)
{
    CALL_STACK_MESSAGE1("CAdvancedSEDialog::OnSpecDir()");

    if (!SpecDirMenu)
        SpecDirMenu = LoadMenu(HLanguage, MAKEINTRESOURCE(IDM_SPECDIR));
    if (!SpecDirMenu)
    {
        SalamanderGeneral->SalMessageBox(Dlg, LoadStrW(IDS_ERRLOADMENU).c_str(), LoadStrW(IDS_ERROR).c_str(),
                                         MB_OK | MB_ICONEXCLAMATION);
        return TRUE;
    }
    if (!SpecDirMenuPopup)
        SpecDirMenuPopup = GetSubMenu(SpecDirMenu, 0);
    if (SpecDirMenuPopup)
    {
        RECT r;
        GetWindowRect(hwndCtl, &r);
        TrackPopupMenuEx(SpecDirMenuPopup, TPM_LEFTALIGN | TPM_TOPALIGN, r.right, r.top, Dlg, NULL);
    }
    return TRUE;
}

BOOL CAdvancedSEDialog::OnSpecDirMenu(WORD itemID)
{
    CALL_STACK_MESSAGE1("CAdvancedSEDialog::OnSpecDirMenu()");

    const char* string;
    switch (itemID)
    {
    case CM_TEMP:
        string = SFX_TDTEMP;
        break;
    case CM_PROGFILES:
        string = SFX_TDPROGFILES;
        break;
    case CM_WINDIR:
        string = SFX_TDWINDIR;
        break;
    case CM_SYSDIR:
        string = SFX_TDSYSDIR;
        break;
    case CM_ENVVAR:
        string = SFX_TDENVVAR;
        break;
    case CM_REGENTRY:
        string = SFX_TDREGVAL;
        break;
    }
    // The tokens stay protocol ASCII - they are matched with StrNICmp against the
    // narrow SFX target path - but the edit control is wide (SetDlgItemTextW /
    // SPLGetDlgItemTextOwned), and EM_REPLACESEL resolves to the W message, which
    // would read these bytes as UTF-16.
    const std::wstring insertText = ZipTextToWide(string);
    SendDlgItemMessageW(Dlg, IDC_TARGETDIR, EM_REPLACESEL, TRUE,
                        reinterpret_cast<LPARAM>(insertText.c_str()));
    if (itemID == CM_ENVVAR || itemID == CM_REGENTRY)
    {
        DWORD start, end;
        SendDlgItemMessage(Dlg, IDC_TARGETDIR, EM_GETSEL, (WPARAM)&start, (LPARAM)&end);
        SendDlgItemMessage(Dlg, IDC_TARGETDIR, EM_SETSEL, (WPARAM)end - 1, (LPARAM)end - 1);
    }
    SetFocus(GetDlgItem(Dlg, IDC_TARGETDIR));
    HWND button = GetDlgItem(Dlg, IDC_SPECDIR);
    LONG style = GetWindowLong(button, GWL_STYLE);
    if (style)
        SetWindowLong(button, GWL_STYLE, (style & ~BS_DEFPUSHBUTTON) | BS_PUSHBUTTON);
    button = GetDlgItem(Dlg, IDOK);
    style = GetWindowLong(button, GWL_STYLE);
    if (style)
        SetWindowLong(button, GWL_STYLE, (style & ~BS_PUSHBUTTON) | BS_DEFPUSHBUTTON);
    return TRUE;
}

BOOL CAdvancedSEDialog::OnRemove(WORD wNotifyCode, WORD wID, HWND hwndCtl)
{
    CALL_STACK_MESSAGE3("CAdvancedSEDialog::OnRemove(0x%X, 0x%X, )", wNotifyCode,
                        wID);
    if (SendMessage(hwndCtl, BM_GETCHECK, 0, 0) == BST_CHECKED)
    {
        //PackOptions->Flags = (PackOptions->Flags & ~SE_DIRFLAGSMASK) | SE_TEMPDIR;
        EnableWindow(GetDlgItem(Dlg, IDC_WAITFOR), TRUE);
    }
    else
    {
        SendDlgItemMessage(Dlg, IDC_HIDEMAINDLG, BM_SETCHECK, (WPARAM)BST_UNCHECKED, 0);
        EnableWindow(GetDlgItem(Dlg, IDC_WAITFOR), FALSE);
    }
    return TRUE;
}

BOOL CAdvancedSEDialog::OnWaitFor(WORD wNotifyCode, WORD wID, HWND hwndCtl)
{
    CALL_STACK_MESSAGE3("CAdvancedSEDialog::OnWaitFor(0x%X, 0x%X, )",
                        wNotifyCode, wID);
    WaitForDialog(Dlg, TmpSfxSettings.WaitFor, _countof(TmpSfxSettings.WaitFor));
    return TRUE;
}

BOOL CAdvancedSEDialog::OnTexts(WORD wNotifyCode, WORD wID, HWND hwndCtl)
{
    CALL_STACK_MESSAGE1("CAdvancedSEDialog::OnText()");
    LRESULT i = SendDlgItemMessage(Dlg, IDC_LANGUAGE, CB_GETCURSEL, 0, 0);
    if (i != CB_ERR)
    {
        CSfxLang* lang = (CSfxLang*)SendDlgItemMessage(Dlg, IDC_LANGUAGE, CB_GETITEMDATA, i, 0);
        if ((LRESULT)lang != CB_ERR)
        {
            CSfxTextsDialog dlg(Dlg, &TmpSfxSettings, lang);
            dlg.Proceed();
        }
        else
            TRACE_E("nepodarilo se ziskat aktualni jazyk z comboboxu");
    }
    else
        TRACE_E("nepodarilo se ziskat aktualni jazyk z comboboxu");

    return TRUE;
}

BOOL CAdvancedSEDialog::OnAuto(WORD wNotifyCode, WORD wID, HWND hwndCtl)
{
    CALL_STACK_MESSAGE3("CAdvancedSEDialog::OnAuto(0x%X, 0x%X, )", wNotifyCode,
                        wID);
    if (SendDlgItemMessage(Dlg, wID, BM_GETCHECK, 0, 0) == BST_CHECKED)
    {
        //PackOptions->Flags = (PackOptions->Flags & ~SE_DIRFLAGSMASK) | SE_TEMPDIR;
        EnableWindow(GetDlgItem(Dlg, IDC_HIDEMAINDLG), TRUE);
    }
    else
    {
        SendDlgItemMessage(Dlg, IDC_HIDEMAINDLG, BM_SETCHECK, (WPARAM)BST_UNCHECKED, 0);
        EnableWindow(GetDlgItem(Dlg, IDC_HIDEMAINDLG), FALSE);
    }

    return TRUE;
}

typedef BOOL(WINAPI* FPickIconDlg)(HWND hwndOwner, LPWSTR lpstrFile,
                                   DWORD nMaxFile, LPDWORD lpdwIconIndex);

static constexpr size_t PICK_ICON_INITIAL_CHARS = 260; // undocumented shell adapter contract

// PickIconDlg is an undocumented shell ordinal with a caller-buffer ABI and no
// required-size result. Keep its mandatory MAX_PATH-compatible scratch storage
// confined to this adapter; ordinary icon-path ownership remains dynamic.
static BOOL PickIconPathOwned(FPickIconDlg picker, HWND owner, std::wstring& path,
                              DWORD& index)
{
    const size_t capacity = (std::max)(path.size() + 1, PICK_ICON_INITIAL_CHARS);
    std::vector<wchar_t> buffer(capacity, L'\0');
    memcpy(buffer.data(), path.c_str(), (path.size() + 1) * sizeof(wchar_t));
    if (!picker(owner, buffer.data(), static_cast<DWORD>(buffer.size()), &index))
        return FALSE;
    path.assign(buffer.data());
    return TRUE;
}

BOOL CAdvancedSEDialog::OnChangeIcon(WORD wNotifyCode, WORD wID, HWND hwndCtl)
{
    CALL_STACK_MESSAGE3("CAdvancedSEDialog::OnChangeIcon(0x%X, 0x%X, )",
                        wNotifyCode, wID);
    int errorID = 0;
    HINSTANCE Shell32DLL = LoadLibraryW(L"shell32.dll");
    if (Shell32DLL)
    {
        FPickIconDlg PickIconDlg = (FPickIconDlg)GetProcAddress(Shell32DLL, (LPCSTR)62); // Min: XP (shell32.dll version 6.0)
        if (PickIconDlg)
        {
            std::wstring file = ZipTextToWide(TmpSfxSettings.IconFile);
            DWORD index = TmpSfxSettings.IconIndex;
            if (PickIconPathOwned(PickIconDlg, Dlg, file, index))
            {
                std::wstring expanded;
                if (SPLExpandEnvironmentStringsOwned(file.c_str(), expanded))
                    file = std::move(expanded);

                std::string encodedFile;
                if (!TryWideToZipText(file.c_str(), encodedFile) ||
                    encodedFile.size() >= _countof(TmpSfxSettings.IconFile))
                    errorID = IDS_TOOLONGNAME;

                HICON iconLarge, iconSmall;
                CIcon* icons;
                int count;
                if (!errorID)
                    switch (LoadIcons(file.c_str(), index, &icons, &count))
                {
                case 1:
                    errorID = IDS_ERROPENICO;
                    break;
                case 2:
                    errorID = IDS_ERRLOADLIB;
                    break;
                case 3:
                    errorID = IDS_ERRLOADLIB2;
                    break;
                case 4:
                    errorID = IDS_ERRLOADICON;
                    break;
                }
                if (!errorID)
                {
                    if (ExtractIconExW(file.c_str(), index, &iconLarge, &iconSmall, 1))
                    {
                        lstrcpyA(TmpSfxSettings.IconFile, encodedFile.c_str());
                        TmpSfxSettings.IconIndex = index;
                        if (SmallIcon)
                            DestroyIcon(SmallIcon);
                        if (LargeIcon)
                            DestroyIcon(LargeIcon);
                        SmallIcon = iconSmall;
                        LargeIcon = iconLarge;
                        if (PackOptions->Icons)
                            DestroyIcons(PackOptions->Icons, PackOptions->IconsCount);
                        PackOptions->Icons = icons;
                        PackOptions->IconsCount = count;
                        InvalidateRect(GetDlgItem(Dlg, IDC_EXEICON), NULL, TRUE);
                        UpdateWindow(GetDlgItem(Dlg, IDC_EXEICON));
                    }
                    else
                    {
                        DestroyIcons(icons, count);
                        errorID = IDS_ERRLOADICON;
                    }
                }
            }
        }
        else
            errorID = IDS_ERRGETPROCADDRESS;
    }
    else
        errorID = IDS_ERRLAODSHELLDLL;
    if (errorID)
    {
        int e = GetLastError();
        const std::wstring message = FormatZipErrorMessage(errorID, e);
        SalamanderGeneral->SalMessageBox(Dlg, message.c_str(),
                                         LoadStrW(IDS_ERROR).c_str(), MB_OK | MB_ICONEXCLAMATION);
    }
    if (Shell32DLL)
        FreeLibrary(Shell32DLL);
    return TRUE;
}

BOOL CAdvancedSEDialog::OnChangeLanguage(WORD wNotifyCode, WORD wID, HWND hwndCtl)
{
    CALL_STACK_MESSAGE3("CAdvancedSEDialog::OnChangeLanguage(0x%X, 0x%X, )",
                        wNotifyCode, wID);

    if (wNotifyCode == CBN_SELENDOK)
    {
        LRESULT i = SendDlgItemMessage(Dlg, IDC_LANGUAGE, CB_GETCURSEL, 0, 0);
        if (i != CB_ERR)
        {
            CSfxLang* lang = (CSfxLang*)SendDlgItemMessage(Dlg, IDC_LANGUAGE, CB_GETITEMDATA, i, 0);
            if ((LRESULT)lang != CB_ERR)
            {
                if (CurrentSfxLang && CurrentSfxLang != lang)
                {
                    if (lstrcmpiA(CurrentSfxLang->DlgTitle, TmpSfxSettings.Title) == 0 &&
                        lstrcmpiA(CurrentSfxLang->DlgText, TmpSfxSettings.Text) == 0 &&
                        lstrcmpiA(CurrentSfxLang->ButtonText, TmpSfxSettings.ExtractBtnText) == 0 &&
                        lstrcmpiA(CurrentSfxLang->Vendor, TmpSfxSettings.Vendor) == 0 &&
                        lstrcmpiA(CurrentSfxLang->WWW, TmpSfxSettings.WWW) == 0 &&
                        (TmpSfxSettings.MBoxText.empty()) &&
                        *TmpSfxSettings.MBoxTitle == 0)
                    {
                        goto REPLACE;
                    }
                    else
                    {
                        switch (*ChangeLangReaction)
                        {
                        case CLR_ASK:
                            if (ChangeTextsDialog(Dlg, ChangeLangReaction) != IDOK)
                                break;
                        case CLR_REPLACE:
                        {
                        REPLACE:
                            strcpy(TmpSfxSettings.Title, lang->DlgTitle);
                            strcpy(TmpSfxSettings.Text, lang->DlgText);
                            strcpy(TmpSfxSettings.ExtractBtnText, lang->ButtonText);
                            strcpy(TmpSfxSettings.Vendor, lang->Vendor);
                            strcpy(TmpSfxSettings.WWW, lang->WWW);
                            break;
                        }
                        }
                    }
                }
                CurrentSfxLang = lang;
            }
        }
    }

    return TRUE;
}

BOOL CAdvancedSEDialog::OnOK(WORD wNotifyCode, WORD wID, HWND hwndCtl)
{
    CALL_STACK_MESSAGE3("CAdvancedSEDialog::OnOK(0x%X, 0x%X, )", wNotifyCode, wID);
    if (GetSettings(&TmpSfxSettings))
    {
        LRESULT i = SendDlgItemMessage(Dlg, IDC_LANGUAGE, CB_GETCURSEL, 0, 0);
        if (i != CB_ERR)
        {
            CSfxLang* lang = (CSfxLang*)SendDlgItemMessage(Dlg, IDC_LANGUAGE, CB_GETITEMDATA, i, 0);
            if ((LRESULT)lang != CB_ERR)
            {
                // Stays narrow: this composes PackOptions->About, which
                // iosfxset.cpp serializes into the SFX script as bytes.
                char buffer[2048];
                buffer[0] = 0;
                if (strcmp(TmpSfxSettings.Vendor, lang->Vendor) != 0 || strcmp(TmpSfxSettings.WWW, lang->WWW) != 0)
                    sprintf(buffer, "%s\r\n%s\r\n\r\n", lang->Vendor, lang->WWW);
                strcat_s(buffer, lang->AboutLicenced);
                lstrcpynA(PackOptions->About, buffer, SE_MAX_ABOUT);
            }
        }

        PackOptions->SfxSettings = TmpSfxSettings;

        EndDialog(IDOK);
    }

    return TRUE;
}

BOOL CAdvancedSEDialog::GetSettings(CSfxSettings* sfxSettings)
{
    CALL_STACK_MESSAGE1("CAdvancedSEDialog::GetSettings()");
    CSfxSettings settings = TmpSfxSettings; // so that the name gets copied
    //settings = *sfxSettings;// to avoid overwriting IconFile and IconIndex
    settings.Flags = 0;
    std::wstring targetDirectory = SPLGetDlgItemTextOwned(Dlg, IDC_TARGETDIR);
    while (!targetDirectory.empty() && targetDirectory.back() == L' ')
        targetDirectory.pop_back();
    if (!CopyWideToZipText(targetDirectory.c_str(), settings.TargetDir,
                           _countof(settings.TargetDir)))
    {
        SalamanderGeneral->SalMessageBox(Dlg, LoadStrW(IDS_TOOLONGNAME).c_str(), LoadStrW(IDS_ERROR).c_str(),
                                         MB_OK | MB_ICONEXCLAMATION);
        SetFocus(GetDlgItem(Dlg, IDC_TARGETDIR));
        return FALSE;
    }
    // verify the syntax
    DWORD ret = ParseTargetDir(settings.TargetDir, NULL, NULL, NULL, NULL, NULL);
    if (ret)
    {
        switch (LOWORD(ret))
        {
        case 1:
            SalamanderGeneral->SalMessageBox(Dlg, LoadStrW(IDS_BADTEMP).c_str(), LoadStrW(IDS_ERROR).c_str(), MB_OK | MB_ICONEXCLAMATION);
            break;

        case 2:
            SalamanderGeneral->SalMessageBox(Dlg, LoadStrW(IDS_MISBAR).c_str(), LoadStrW(IDS_ERROR).c_str(), MB_OK | MB_ICONEXCLAMATION);
            break;

        case 3:
            SalamanderGeneral->SalMessageBox(Dlg, LoadStrW(IDS_BADVAR).c_str(), LoadStrW(IDS_ERROR).c_str(), MB_OK | MB_ICONEXCLAMATION);
            break;

        case 4:
            SalamanderGeneral->SalMessageBox(Dlg, LoadStrW(IDS_BADKEY).c_str(), LoadStrW(IDS_ERROR).c_str(), MB_OK | MB_ICONEXCLAMATION);
            break;
        }
        const size_t byteOffset = HIWORD(ret);
        const std::wstring validPrefix = ZipTextToWide(
            std::string(settings.TargetDir, min(byteOffset, strlen(settings.TargetDir))).c_str());
        SendDlgItemMessageW(Dlg, IDC_TARGETDIR, EM_SETSEL, validPrefix.size(), validPrefix.size());
        SetFocus(GetDlgItem(Dlg, IDC_TARGETDIR));
        return FALSE;
    }
    if (SendDlgItemMessage(Dlg, IDC_ALLOWUSER, BM_GETCHECK, 0, 0) != BST_CHECKED)
    {
        settings.Flags |= SE_NOTALLOWCHANGE;
    }
    const std::wstring command = SPLGetDlgItemTextOwned(Dlg, IDC_EXECUTE);
    if (!CopyWideToZipText(command.c_str(), settings.Command, _countof(settings.Command)))
    {
        SalamanderGeneral->SalMessageBox(Dlg, LoadStrW(IDS_TOOLONGNAME).c_str(), LoadStrW(IDS_ERROR).c_str(),
                                         MB_OK | MB_ICONEXCLAMATION);
        SetFocus(GetDlgItem(Dlg, IDC_EXECUTE));
        return FALSE;
    }
    if (SendDlgItemMessage(Dlg, IDC_REMOVE, BM_GETCHECK, 0, 0) == BST_CHECKED)
    {
        settings.Flags |= SE_REMOVEAFTER;
    }
    LRESULT i = SendDlgItemMessage(Dlg, IDC_LANGUAGE, CB_GETCURSEL, 0, 0);
    if (i != CB_ERR)
    {
        CSfxLang* lang = (CSfxLang*)SendDlgItemMessage(Dlg, IDC_LANGUAGE, CB_GETITEMDATA, i, 0);
        if ((LRESULT)lang != CB_ERR)
        {
            lstrcpyA(settings.SfxFile, lang->FileName.c_str());
        }
    }
    if (SendDlgItemMessage(Dlg, IDC_HIDEMAINDLG, BM_GETCHECK, 0, 0) == BST_CHECKED)
    {
        settings.Flags |= SE_HIDEMAINDLG;
    }
    if (SendDlgItemMessage(Dlg, IDC_AUTO, BM_GETCHECK, 0, 0) == BST_CHECKED)
    {
        settings.Flags |= SE_AUTO;
    }
    if (SendDlgItemMessage(Dlg, IDC_SUMDLG, BM_GETCHECK, 0, 0) == BST_CHECKED)
    {
        settings.Flags |= SE_SHOWSUMARY;
    }
    if (SendDlgItemMessage(Dlg, IDC_OVEWRITE, BM_GETCHECK, 0, 0) == BST_CHECKED)
    {
        settings.Flags |= SE_OVEWRITEALL;
    }
    if (SendDlgItemMessage(Dlg, IDC_AUTODIR, BM_GETCHECK, 0, 0) == BST_CHECKED)
    {
        settings.Flags |= SE_AUTODIR;
    }
    if (SendDlgItemMessage(Dlg, IDC_REQSADMIN, BM_GETCHECK, 0, 0) == BST_CHECKED)
    {
        settings.Flags |= SE_REQUIRESADMIN;
    }
    *sfxSettings = settings;
    return TRUE;
}

// compares two strings and ignores single '&'
int CompareMenuItems(char* name1, char* name2)
{
    CALL_STACK_MESSAGE1("CompareMenuItems(, )");
    char buf1[MAX_FAVNAME];
    char buf2[MAX_FAVNAME];

    // remove single '&'
    char* sour = name1;
    char* dest = buf1;

    while (*(*sour == '&' ? sour++ : sour))
    {
        *dest++ = *sour++;
    }
    *dest = 0;

    sour = name2;
    dest = buf2;
    while (*(*sour == '&' ? sour++ : sour))
    {
        *dest++ = *sour++;
    }
    *dest = 0;

    return SalamanderGeneral->StrICmp(ZipTextToWide(buf1).c_str(), ZipTextToWide(buf2).c_str());

    /* this was case sensitive, otherwise OK
  int ret = 0 ;

  while (!(ret = *(unsigned char *)(*name1 == '&' ? name1++ : name1) -
                 *(unsigned char *)(*name2 == '&' ? name2++ : name2)) && *name2)
      ++name1, ++name2;

  if (ret < 0)
    ret = -1;
  else
  {
    if (ret > 0)
      ret = 1;
  }

  return ret;
*/
}

void SortFavoriteSettings(int left, int right)
{
    CALL_STACK_MESSAGE_NONE
    int i = left, j = right;
    char* pivot = Favorities[(i + j) / 2]->Name;
    do
    {
        while (CompareMenuItems(Favorities[i]->Name, pivot) < 0 && i < right)
            i++;
        while (CompareMenuItems(pivot, Favorities[j]->Name) < 0 && j > left)
            j--;
        if (i <= j)
        {
            CFavoriteSfx* tmp = Favorities[i];
            Favorities[i] = Favorities[j];
            Favorities[j] = tmp;
            i++;
            j--;
        }
    } while (i <= j); // do they have to match?
    if (left < j)
        SortFavoriteSettings(left, j);
    if (i < right)
        SortFavoriteSettings(i, right);
}

BOOL CAdvancedSEDialog::InitMenu()
{
    CALL_STACK_MESSAGE1("CAdvancedSEDialog::InitMenu()");
    Accel = LoadAccelerators(DLLInstance, MAKEINTRESOURCE(IDA_SFXACCELS));
    if (!Accel)
    {
        SalamanderGeneral->SalMessageBox(Dlg, LoadStrW(IDS_ERRLOADACCELS).c_str(), LoadStrW(IDS_ERROR).c_str(),
                                         MB_OK | MB_ICONEXCLAMATION);
        return FALSE;
    }
    Menu = LoadMenu(HLanguage, MAKEINTRESOURCE(IDM_SFXMENU));
    if (!Menu)
    {
        SalamanderGeneral->SalMessageBox(Dlg, LoadStrW(IDS_ERRLOADMENU).c_str(), LoadStrW(IDS_ERROR).c_str(),
                                         MB_OK | MB_ICONEXCLAMATION);
        return FALSE;
    }

    CreateFavoritesMenu();

    MENUITEMINFOW mi;
    if (!*LastUsedSfxSet.Name)
    {
        memset(&mi, 0, sizeof(mi));
        mi.cbSize = sizeof(mi);
        mi.fMask = MIIM_STATE;
        mi.fState = MFS_DISABLED;
        SetMenuItemInfoW(Menu, CM_SFX_LASTUSED, FALSE, &mi);
    }

    // ensure the window keeps the correct size even after adding the menu
    RECT wr, cr1, cr2;
    GetClientRect(Dlg, &cr1);

    SetMenu(Dlg, Menu);

    GetClientRect(Dlg, &cr2);
    GetWindowRect(Dlg, &wr);
    LONG w = wr.right - wr.left;
    LONG h = wr.bottom - wr.top + cr1.bottom - cr2.bottom;
    SetWindowPos(Dlg, 0, 0, 0, w, h, SWP_NOMOVE | SWP_NOZORDER);

    return TRUE;
}

BOOL CAdvancedSEDialog::CreateFavoritesMenu()
{
    CALL_STACK_MESSAGE1("CAdvancedSEDialog::CreateFavoritesMenu()");
    MENUITEMINFOW mi;
    if (FavoritiesMenu)
    {
        // remove the submenu from the 'Favorities' item
        memset(&mi, 0, sizeof(mi));
        mi.cbSize = sizeof(mi);
        mi.fMask = MIIM_SUBMENU;
        mi.hSubMenu = NULL;
        SetMenuItemInfoW(Menu, CM_SFX_FAVORITIES, FALSE, &mi);
        DestroyMenu(FavoritiesMenu);
    }
    FavoritiesMenu = CreatePopupMenu();
    if (!FavoritiesMenu)
    {
        TRACE_E("Nedostali jsme FavoritiesMenu ve funkci InitMenu.");
        return FALSE;
    }
    if (Favorities.Count == 0)
    {
        memset(&mi, 0, sizeof(mi));
        mi.cbSize = sizeof(mi);
        mi.fMask = MIIM_TYPE | MIIM_ID | MIIM_STATE;
        mi.fType = MFT_STRING;
        mi.fState = MFS_DISABLED;
        mi.wID = CM_SFX_FAVORITE;
        // Named local: LangStr returns by value, so dwTypeData pointed at a
        // freed buffer by the time lstrlenW and InsertMenuItemW read it below.
        std::wstring emptyLabelW = LangStr(IDS_EMPTY);
        mi.dwTypeData = const_cast<LPWSTR>(emptyLabelW.c_str());
        mi.cch = lstrlenW(mi.dwTypeData);
        InsertMenuItemW(FavoritiesMenu, 0, TRUE, &mi);
    }
    else
    {
        SortFavoriteSettings(0, Favorities.Count - 1);
        int i;
        for (i = 0; i < Favorities.Count; i++)
        {
            CFavoriteSfx* fav;

            fav = Favorities[i];
            memset(&mi, 0, sizeof(mi));
            mi.cbSize = sizeof(mi);
            mi.fMask = MIIM_TYPE | MIIM_ID | MIIM_DATA;
            mi.fType = MFT_STRING;
            mi.wID = CM_SFX_FAVORITE + i;
            mi.dwItemData = (ULONG_PTR)fav;
            // fav->Name is a narrow registry field, so it bridges here. The
            // holder must outlive the call below - mi.dwTypeData borrows, it does not copy.
            std::wstring favNameW = ZipTextToWide(fav->Name);
            mi.dwTypeData = favNameW.data();
            mi.cch = (UINT)favNameW.size();
            InsertMenuItemW(FavoritiesMenu, i, TRUE, &mi);
        }
        memset(&mi, 0, sizeof(mi));
        mi.cbSize = sizeof(mi);
        mi.fMask = MIIM_TYPE;
        mi.fType = MFT_SEPARATOR;
        InsertMenuItemW(FavoritiesMenu, Favorities.Count /*i++*/, TRUE, &mi);

        memset(&mi, 0, sizeof(mi));
        mi.cbSize = sizeof(mi);
        mi.fMask = MIIM_TYPE | MIIM_ID;
        mi.fType = MFT_STRING;
        mi.wID = CM_SFX_MANAGE;
        mi.dwTypeData = const_cast<LPWSTR>(LangStr(IDS_MANAGE).c_str());
        mi.cch = lstrlenW(mi.dwTypeData);
        InsertMenuItemW(FavoritiesMenu, Favorities.Count + 1 /*i*/, TRUE, &mi);
    }

    // assign the submenu to the 'Favorities' item
    memset(&mi, 0, sizeof(mi));
    mi.cbSize = sizeof(mi);
    mi.fMask = MIIM_SUBMENU;
    mi.hSubMenu = FavoritiesMenu;
    SetMenuItemInfoW(Menu, CM_SFX_FAVORITIES, FALSE, &mi);

    return TRUE;
}

#define SFX_SET_SIGNATURE 0x53584653
#define SFX_SET_CURRENTVERSION 1

struct CSettingsHeader
{
    DWORD Signature;
    DWORD Version;
    DWORD DataSize;
};

BOOL CAdvancedSEDialog::OnImport()
{
    CALL_STACK_MESSAGE1("CAdvancedSEDialog::OnImport()");
    OPENFILENAMEW ofn;
    memset(&ofn, 0, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = Dlg;
    std::wstring filter = LangStr(IDS_SETTINGSFILE).c_str();
    filter.push_back(L'\0');
    filter += L"*.set";
    filter.push_back(L'\0');
    filter += LangStr(IDS_ALLFILES).c_str();
    filter.push_back(L'\0');
    filter += L"*.*";
    filter.push_back(L'\0');
    filter.push_back(L'\0');
    ofn.lpstrFilter = filter.c_str();
    ofn.nFilterIndex = 1;
    if (!PackObject->Config.LastExportPath.empty())
    {
        ofn.lpstrInitialDir = PackObject->Config.LastExportPath.c_str();
    }
    ofn.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_HIDEREADONLY | OFN_NOCHANGEDIR;
    ofn.lpstrDefExt = L"set";

    std::vector<std::wstring> selectedFiles;
    if (SPLSafeGetOpenFileNamesOwned(SalamanderGeneral, &ofn, selectedFiles) &&
        !selectedFiles.empty())
    {
        const std::wstring& selectedFile = selectedFiles[0];
        CFile* file;
        int ret = PackObject->CreateCFile(&file, selectedFile.c_str(), GENERIC_READ, FILE_SHARE_READ,
                                          OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, PE_NOSKIP, NULL,
                                          false, false);
        if (ret)
        {
            if (ret == ERR_LOWMEM)
            {
                SalamanderGeneral->SalMessageBox(Dlg, LoadStrW(IDS_LOWMEM).c_str(), LoadStrW(IDS_ERROR).c_str(), MB_OK | MB_ICONEXCLAMATION);
            }
            return TRUE;
        }

        char* buffer = (char*)malloc((unsigned)file->Size + 1);
        if (!buffer)
        {
            SalamanderGeneral->SalMessageBox(Dlg, LoadStrW(IDS_LOWMEM).c_str(), LoadStrW(IDS_ERROR).c_str(), MB_OK | MB_ICONEXCLAMATION);
        }
        else
        {
            if (!PackObject->Read(file, buffer, (unsigned)file->Size, NULL, NULL))
            {
                buffer[file->Size] = 0;
                CSfxSettings settings;

                settings.Flags = SE_SHOWSUMARY;
                if (DefLanguage)
                    lstrcpyA(settings.SfxFile, DefLanguage->FileName.c_str());
                else
                {
                    TRACE_E("CAdvancedSEDialog::OnImport(), neni naloadena DefLanguage");
                    settings.SfxFile[0] = 0;
                }

                std::wstring zip2sfxDirW;
                std::string zip2sfxDir;
                if (SPLGetModuleFileNameOwned(DLLInstance, zip2sfxDirW))
                {
                    SPLCutDirectoryOwned(SalamanderGeneral, zip2sfxDirW);
                    SPLSalPathAppendOwned(zip2sfxDirW, L"zip2sfx");
                    SPLSalPathAddBackslashOwned(zip2sfxDirW);
                    TryWideToZipText(zip2sfxDirW.c_str(), zip2sfxDir);
                }

                const auto stripSfxFilePath = [&settings]()
                {
                    const char* slash = strrchr(settings.SfxFile, '\\');
                    const char* forwardSlash = strrchr(settings.SfxFile, '/');
                    if (forwardSlash != NULL && (slash == NULL || forwardSlash > slash))
                        slash = forwardSlash;
                    if (slash != NULL)
                        memmove(settings.SfxFile, slash + 1, strlen(slash + 1) + 1);
                };

                // load them the first time to obtain the SFX package name (not a mandatory parameter)
                ret = ImportSFXSettings(buffer, &settings, zip2sfxDir.c_str());
                if (ret == 0)
                {
                    stripSfxFilePath();
                    CSfxLang* lang = NULL;
                    int i;
                    for (i = 0; i < SfxLanguages->Count; i++)
                    {
                        lang = (*SfxLanguages)[i];
                        if (lstrcmpiA(lang->FileName.c_str(), settings.SfxFile) == 0)
                            break;
                        lang = NULL;
                    }
                    if (!lang)
                    {
                        const std::wstring message = SPLFormatStringOwned(
                            LangStr(IDS_NOLANGFILE).c_str(), ZipTextToWide(settings.SfxFile).c_str());
                        SalamanderGeneral->SalMessageBox(Dlg, message.c_str(), LoadStrW(IDS_ERROR).c_str(), MB_OK | MB_ICONEXCLAMATION);
                    }
                    else
                    {
                        settings.Flags = SE_SHOWSUMARY;
                        lstrcpyA(settings.Text, lang->DlgText);
                        lstrcpyA(settings.Title, lang->DlgTitle);
                        lstrcpyA(settings.ExtractBtnText, lang->ButtonText);
                        lstrcpyA(settings.Vendor, lang->Vendor);
                        lstrcpyA(settings.WWW, lang->WWW);
                        std::wstring moduleFileW;
                        if (SPLGetModuleFileNameOwned(DLLInstance, moduleFileW))
                            CopyWideToZipText(moduleFileW.c_str(), settings.IconFile,
                                              _countof(settings.IconFile));
                        settings.IconIndex = -IDI_SFXICON;
                        // load them a second time to get the texts (these parameters are optional)
                        // if they are not provided, use the texts from the SFX package, either the specified
                        // or the default one
                        ImportSFXSettings(buffer, &settings, zip2sfxDir.c_str());

                        stripSfxFilePath();

                        std::wstring iconFileW = ZipTextToWide(settings.IconFile);
                        std::wstring rootPathW;
                        size_t rootLen = 0;
                        if (SPLGetRootPathOwned(SalamanderGeneral, iconFileW.c_str(), rootPathW))
                            rootLen = (std::min)(rootPathW.size(), iconFileW.size());
                        SPLSalRemovePointsFromPathOwned(SalamanderGeneral, iconFileW, rootLen);
                        CopyWideToZipText(iconFileW.c_str(), settings.IconFile,
                                          _countof(settings.IconFile));

                        if (LoadFavSettings(&settings))
                        {
                            ResetValueControls();
                            PackObject->Config.LastExportPath = selectedFile;
                            SPLCutDirectoryOwned(SalamanderGeneral,
                                                 PackObject->Config.LastExportPath);
                        }
                    }
                }
                else
                {
                    int errID;
                    switch (ret)
                    {
                    case 1:
                        errID = IDS_BADTEMP2;
                        break;
                    case 2:
                        errID = IDS_MISBAR2;
                        break;
                    case 3:
                        errID = IDS_BADVAR2;
                        break;
                    case 4:
                        errID = IDS_BADKEY2;
                        break;
                    case 5:
                        errID = IDS_MISSINGVERSION;
                        break;
                    case 6:
                        errID = IDS_BADVERSION;
                        break;
                    case 8:
                        errID = IDS_BADMSGBOXTYPE;
                        break;
                    case 7:
                    default:
                        errID = IDS_ERRFORMAT;
                        break;
                    }

                    SalamanderGeneral->SalMessageBox(Dlg, LoadStrW(errID).c_str(), LoadStrW(IDS_ERROR).c_str(),
                                                     MB_OK | MB_ICONEXCLAMATION);
                }
            }
            free(buffer);
        }
        PackObject->CloseCFile(file);
    }

    return TRUE;
}

BOOL CAdvancedSEDialog::OnExport()
{
    CALL_STACK_MESSAGE1("CAdvancedSEDialog::OnExport()");
    CSfxSettings settings;
    if (!GetSettings(&settings))
        return TRUE;
    std::wstring fullPathW;
    if (!SPLGetModuleFileNameOwned(DLLInstance, fullPathW))
        return TRUE;
    SPLCutDirectoryOwned(SalamanderGeneral, fullPathW);
    SPLSalPathAppendOwned(fullPathW, L"sfx");
    const std::wstring sfxFileW = ZipTextToWide(settings.SfxFile);
    SPLSalPathAppendOwned(fullPathW, sfxFileW.c_str());
    if (!CopyWideToZipText(fullPathW.c_str(), settings.SfxFile,
                           _countof(settings.SfxFile)))
    {
        SalamanderGeneral->SalMessageBox(Dlg, LoadStrW(IDS_TOOLONGNAME).c_str(), LoadStrW(IDS_ERROR).c_str(),
                                         MB_OK | MB_ICONEXCLAMATION);
        return TRUE;
    }
    //lstrcpyA(settings.IconFile, TmpSfxSettings.IconFile);
    //settings.IconIndex = TmpSfxSettings.IconIndex;

    OPENFILENAMEW ofn;
    memset(&ofn, 0, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = Dlg;
    ofn.nFilterIndex = 1;
    ofn.Flags = OFN_EXPLORER | /*OFN_FILEMUSTEXIST | */ OFN_HIDEREADONLY | OFN_NOCHANGEDIR;

    std::wstring filterW = LangStr(IDS_SETTINGSFILE).c_str();
    filterW.push_back(L'\0');
    filterW += L"*.set";
    filterW.push_back(L'\0');
    filterW.push_back(L'\0');
    ofn.lpstrFilter = filterW.c_str();
    if (!PackObject->Config.LastExportPath.empty())
        ofn.lpstrInitialDir = PackObject->Config.LastExportPath.c_str();
    ofn.lpstrDefExt = L"set";

    std::wstring fileNameW;
    if (SPLSafeGetSaveFileNameOwned(SalamanderGeneral, &ofn, fileNameW))
    {
        // test whether it already exists
        if (SalamanderGeneral->SalGetFileAttributes(fileNameW.c_str()) == 0xFFFFFFFF ||
            SalamanderGeneral->SalMessageBox(Dlg, LoadStrW(IDS_EXPORTOVEWRITE).c_str(), LoadStrW(IDS_PLUGINNAME).c_str(),
                                              MB_YESNO | MB_ICONQUESTION) == IDYES)
        {
            CFile* file;
            int ret = PackObject->CreateCFile(&file, fileNameW.c_str(), GENERIC_WRITE, FILE_SHARE_READ,
                                              CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, PE_NOSKIP, NULL,
                                              false, false);
            if (ret)
            {
                if (ret == ERR_LOWMEM)
                {
                    SalamanderGeneral->SalMessageBox(Dlg, LoadStrW(IDS_LOWMEM).c_str(), LoadStrW(IDS_ERROR).c_str(),
                                                     MB_OK | MB_ICONEXCLAMATION);
                }
                return TRUE;
            }

            if (PackObject->ExportSFXSettings(file, &settings))
            {
                PackObject->CloseCFile(file);
                PackObject->Config.LastExportPath = fileNameW;
                SPLCutDirectoryOwned(SalamanderGeneral,
                                     PackObject->Config.LastExportPath);
                // notify the change on the path
                SalamanderGeneral->PostChangeOnPathNotification(
                    PackObject->Config.LastExportPath.c_str(), FALSE);
            }
            else
            {
                PackObject->CloseCFile(file);
                DeleteFileW(fileNameW.c_str());
            }
        }
    }

    return TRUE;
}

BOOL CAdvancedSEDialog::OnPreview()
{
    CALL_STACK_MESSAGE1("CAdvancedSEDialog::OnPreview()");
    CSfxSettings settings;

    if (!GetSettings(&settings))
        return TRUE;

    LRESULT retL = SendDlgItemMessage(Dlg, IDC_LANGUAGE, CB_GETCURSEL, 0, 0);
    if (retL != CB_ERR)
    {
        CSfxLang* lang = (CSfxLang*)SendDlgItemMessage(Dlg, IDC_LANGUAGE, CB_GETITEMDATA, retL, 0);
        if ((LRESULT)lang != CB_ERR)
        {
            // Stays narrow - see above: this is SFX script content.
            char buffer[2048];
            buffer[0] = 0;
            if (strcmp(TmpSfxSettings.Vendor, lang->Vendor) != 0 || strcmp(TmpSfxSettings.WWW, lang->WWW) != 0)
                sprintf(buffer, "%s\r\n%s\r\n\r\n", lang->Vendor, lang->WWW);
            strcat_s(buffer, lang->AboutLicenced);
            lstrcpynA(PackOptions->About, buffer, SE_MAX_ABOUT);
        }
    }

    DWORD e;
    std::wstring tmpNameW;
    if (!SPLSalGetTempFileNameOwned(SalamanderGeneral, NULL, L"Sal", tmpNameW,
                                    TRUE, &e))
    {
        const std::wstring message = FormatZipErrorMessage(IDS_ERRGETTEMP, e);
        SalamanderGeneral->SalMessageBox(Dlg, message.c_str(),
                                         LoadStrW(IDS_ERROR).c_str(), MB_OK | MB_ICONEXCLAMATION);
        return TRUE;
    }
    int ret = PackObject->CreateCFile(&PackObject->TempFile, tmpNameW.c_str(), GENERIC_WRITE, FILE_SHARE_READ,
                                      CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, PE_NOSKIP, NULL,
                                      false, false);
    if (ret)
    {
        if (ret == ERR_LOWMEM)
        {
            SalamanderGeneral->SalMessageBox(Dlg, LoadStrW(IDS_LOWMEM).c_str(), LoadStrW(IDS_ERROR).c_str(), MB_OK | MB_ICONEXCLAMATION);
        }
        DeleteFileW(tmpNameW.c_str());
        return TRUE;
    }

    ret = PackObject->WriteSfxExecutable(tmpNameW.c_str(), settings.SfxFile, TRUE, 0);
    PackObject->CloseCFile(PackObject->TempFile);
    PackObject->TempFile = NULL;
    if (ret)
    {
        if (ret != IDS_NODISPLAY)
            SalamanderGeneral->SalMessageBox(Dlg, LoadStrW(ret).c_str(), LoadStrW(IDS_ERROR).c_str(), MB_OK | MB_ICONEXCLAMATION);
        DeleteFileW(tmpNameW.c_str());
        return TRUE;
    }

    HINSTANCE sfxHInstance = LoadLibraryExW(tmpNameW.c_str(), NULL, LOAD_LIBRARY_AS_DATAFILE);
    if (sfxHInstance)
    {
        CALL_STACK_MESSAGE1("Preview SFX Dialog");
        CPreviewInitData data;
        const std::wstring aboutButton1 = LangStr(IDS_SFXABOUTBTN1);
        const std::wstring aboutButton2 = LangStr(IDS_SFXABOUTBTN2);
        data.Settings = &settings;
        data.About = PackOptions->About;
        data.AboutButton1 = aboutButton1.c_str();
        data.AboutButton2 = aboutButton2.c_str();
        data.LargeIcon = LargeIcon;
        data.SmallIcon = SmallIcon;
        data.SfxHInstance = sfxHInstance;
        HRSRC hrsrc = FindResource(sfxHInstance, MAKEINTRESOURCE(SE_IDD_SFXDIALOG), RT_DIALOG);
        if (hrsrc != NULL)
        {
            HGLOBAL hGlobal = LoadResource(sfxHInstance, hrsrc);
            if (hGlobal != NULL)
            {
                INT_PTR ret2 = DialogBoxIndirectParam(HLanguage, (LPCDLGTEMPLATE)hGlobal, Dlg, SfxPreviewDlgProc,
                                                      (LPARAM)&data);
                if (ret2 == 0 || ret2 == -1)
                    TRACE_E("DialogBoxIndirectParam failed");
            }
            else
                TRACE_E("LoadResource failed");
        }
        else
            TRACE_E("Dialog template SE_IDD_SFXDIALOG was not found.");

        FreeLibrary(sfxHInstance);
    }
    else
        TRACE_E("LoadLibraryEx failed");

    DeleteFileW(tmpNameW.c_str());

    return TRUE;
}

BOOL CAdvancedSEDialog::OnResetAll()
{
    CALL_STACK_MESSAGE1("CAdvancedSEDialog::OnResetAll()");
    OnResetValues();
    OnResetTexts();

    return TRUE;
}

BOOL CAdvancedSEDialog::OnResetValues()
{
    CALL_STACK_MESSAGE1("CAdvancedSEDialog::OnResetValues()");

    // we cannot directly use TmpSfxSettings = DefOptions.SfxSetting, because it would overwrite
    // the icon, which might then become invalid
    TmpSfxSettings.Flags = DefOptions.SfxSettings.Flags;
    *TmpSfxSettings.Command = 0;
    lstrcpyA(TmpSfxSettings.TargetDir, DefOptions.SfxSettings.TargetDir);
    TmpSfxSettings.MBoxStyle = DefOptions.SfxSettings.MBoxStyle;
    *TmpSfxSettings.MBoxTitle = 0;
    TmpSfxSettings.SetMBoxText("");
    *TmpSfxSettings.WaitFor = 0;

    if (!DefLanguage)
    {
        //MessageBox(Dlg, LangStr(IDS_NODEFSFX), LangStr(IDS_ERROR), MB_OK | MB_ICONEXCLAMATION);
        TRACE_E("Neni naloudena DefLanguage pro sfx advanced dialog a je volano reset values.");
    }
    else
    {
        CurrentSfxLang = DefLanguage;
        wchar_t langName[128];
        if (GetLocaleInfoW(MAKELCID(MAKELANGID(DefLanguage->LangID, SUBLANG_NEUTRAL), SORT_DEFAULT), LOCALE_SLANGUAGE, langName, 128))
        {
            wchar_t* c = wcschr(langName, L' ');
            if (c)
                *c = 0;
            if (SendDlgItemMessageW(Dlg, IDC_LANGUAGE, CB_SELECTSTRING, -1, reinterpret_cast<LPARAM>(langName)) == CB_ERR)
            {
                SendDlgItemMessageW(Dlg, IDC_LANGUAGE, CB_SETCURSEL, 0, 0);
            }
        }
        lstrcpyA(TmpSfxSettings.SfxFile, DefLanguage->FileName.c_str());

        HICON iconLarge, iconSmall;
        CIcon* icons;
        int count;
        std::wstring fileW;
        int errorID = 0;
        if (!SPLGetModuleFileNameOwned(DLLInstance, fileW) ||
            !CopyWideToZipText(fileW.c_str(), TmpSfxSettings.IconFile,
                               _countof(TmpSfxSettings.IconFile)))
            errorID = IDS_TOOLONGNAME;
        if (!errorID)
            switch (LoadIcons(fileW.c_str(), -IDI_SFXICON, &icons, &count))
        {
        case 1:
            errorID = IDS_ERROPENICO;
            break;
        case 2:
            errorID = IDS_ERRLOADLIB;
            break;
        case 3:
            errorID = IDS_ERRLOADLIB2;
            break;
        case 4:
            errorID = IDS_ERRLOADICON;
            break;
        }
        if (!errorID)
        {
            if (ExtractIconExW(fileW.c_str(), -IDI_SFXICON, &iconLarge, &iconSmall, 1))
            {
                TmpSfxSettings.IconIndex = -IDI_SFXICON;
                if (SmallIcon)
                    DestroyIcon(SmallIcon);
                if (LargeIcon)
                    DestroyIcon(LargeIcon);
                SmallIcon = iconSmall;
                LargeIcon = iconLarge;
                if (PackOptions->Icons)
                    DestroyIcons(PackOptions->Icons, PackOptions->IconsCount);
                PackOptions->Icons = icons;
                PackOptions->IconsCount = count;
                InvalidateRect(GetDlgItem(Dlg, IDC_EXEICON), NULL, TRUE);
                UpdateWindow(GetDlgItem(Dlg, IDC_EXEICON));
            }
            else
            {
                DestroyIcons(icons, count);
                errorID = IDS_ERRLOADICON;
            }
        }
        if (errorID)
        {
            int e = GetLastError();
            const std::wstring message = FormatZipErrorMessage(errorID, e);
            SalamanderGeneral->SalMessageBox(Dlg, message.c_str(), LoadStrW(IDS_ERROR).c_str(), MB_OK | MB_ICONEXCLAMATION);
        }
    }

    ResetValueControls();

    return TRUE;
}

BOOL CAdvancedSEDialog::OnResetTexts()
{
    CALL_STACK_MESSAGE1("CAdvancedSEDialog::OnResetTexts()");
    LRESULT i = SendDlgItemMessage(Dlg, IDC_LANGUAGE, CB_GETCURSEL, 0, 0);
    if (i != CB_ERR)
    {
        CSfxLang* lang = (CSfxLang*)SendDlgItemMessage(Dlg, IDC_LANGUAGE, CB_GETITEMDATA, i, 0);
        if ((LRESULT)lang != CB_ERR)
        {
            TmpSfxSettings.MBoxStyle = MB_OK;
            TmpSfxSettings.SetMBoxText("");
            *TmpSfxSettings.MBoxTitle = 0;
            lstrcpyA(TmpSfxSettings.Text, lang->DlgText);
            lstrcpyA(TmpSfxSettings.Title, lang->DlgTitle);
            lstrcpyA(TmpSfxSettings.ExtractBtnText, lang->ButtonText);
            lstrcpyA(TmpSfxSettings.Vendor, lang->Vendor);
            lstrcpyA(TmpSfxSettings.WWW, lang->WWW);
        }
    }

    return TRUE;
}

BOOL CAdvancedSEDialog::OnAddFavorite()
{
    CALL_STACK_MESSAGE1("CAdvancedSEDialog::OnAddFavorite()");
    if (!FavoritiesMenu)
        return TRUE;
    CFavoriteSfx* newFav = new CFavoriteSfx;

    if (!newFav)
    {
        SalamanderGeneral->SalMessageBox(Dlg, LoadStrW(IDS_LOWMEM).c_str(), LoadStrW(IDS_ERROR).c_str(), MB_OK | MB_ICONEXCLAMATION);
        return TRUE;
    }

    if (!GetSettings(&newFav->Settings))
    {
        delete newFav;
        return TRUE;
    }

    //lstrcpyA(newFav->Settings.IconFile, TmpSfxSettings.IconFile);
    //newFav->Settings.IconIndex = TmpSfxSettings.IconIndex;

    *newFav->Name = 0;
    CRenFavDialog renFav(Dlg, newFav->Name, false);

    if (renFav.Proceed() != IDOK)
    {
        delete newFav;
        return TRUE;
    }

    /*
  // find the index at which we will insert it
  int index;
  for (index = 0; index < Favorities.Count; index++)
  {
    if (CompareMenuItems(newFav->Name, Favorities[index]->Name) < 0) break;
  }
  */

    if (!Favorities.Add(newFav))
    {
        delete newFav;
        SalamanderGeneral->SalMessageBox(Dlg, LoadStrW(IDS_LOWMEM).c_str(), LoadStrW(IDS_ERROR).c_str(), MB_OK | MB_ICONEXCLAMATION);
        return TRUE;
    }
    // sorting happens in CreateFavoritiesMenu
    //SortFavoriteSettings(0, Favorities.Count - 1);

    CreateFavoritesMenu();

    /*
  // remove "empty" if we are adding the first item
  if (Favorities.Count == 1) DeleteMenu(FavoritiesMenu, 0, MF_BYPOSITION);

  MENUITEMINFO mi;

  memset(&mi, 0, sizeof(mi));
  mi.cbSize = sizeof(mi);
  mi.fMask = MIIM_TYPE | MIIM_ID | MIIM_DATA;
  mi.fType = MFT_STRING;
  mi.wID = CM_SFX_FAVORITE + Favorities.Count - 1;
  mi.dwItemData = (ULONG_PTR) newFav;
  mi.dwTypeData = newFav->Name;
  mi.cch = lstrlenA(mi.dwTypeData);
  InsertMenuItem(FavoritiesMenu, index, TRUE, &mi);
  */

    return TRUE;
}

BOOL CAdvancedSEDialog::OnRenameFavorite()
{
    CALL_STACK_MESSAGE1("CAdvancedSEDialog::OnRenameFavorite()");
    SalamanderGeneral->SalMessageBox(Dlg, LoadStrW(IDS_UNDERCOSTRUCT).c_str(), LoadStrW(IDS_PLUGINNAME).c_str(), MB_OK | MB_ICONINFORMATION);
    return TRUE;
}

BOOL CAdvancedSEDialog::OnDeleteteFavorite()
{
    CALL_STACK_MESSAGE1("CAdvancedSEDialog::OnDeleteteFavorite()");
    SalamanderGeneral->SalMessageBox(Dlg, LoadStrW(IDS_UNDERCOSTRUCT).c_str(), LoadStrW(IDS_PLUGINNAME).c_str(), MB_OK | MB_ICONINFORMATION);
    return TRUE;
}

BOOL CAdvancedSEDialog::OnRemoveAllFavorities()
{
    CALL_STACK_MESSAGE1("CAdvancedSEDialog::OnRemoveAllFavorities()");
    SalamanderGeneral->SalMessageBox(Dlg, LoadStrW(IDS_UNDERCOSTRUCT).c_str(), LoadStrW(IDS_PLUGINNAME).c_str(), MB_OK | MB_ICONINFORMATION);
    return TRUE;
}

BOOL CAdvancedSEDialog::OnManageFavorities()
{
    CALL_STACK_MESSAGE1("CAdvancedSEDialog::OnManageFavorities()");
    ManageFavoritiesDialog(Dlg);
    CreateFavoritesMenu();
    return TRUE;
}

BOOL CAdvancedSEDialog::OnFavorities()
{
    CALL_STACK_MESSAGE1("CAdvancedSEDialog::OnFavorities()");
    if (!FavoritiesMenu)
        return TRUE;

    POINT p;

    p.x = 10;
    p.y = 10;

    ClientToScreen(Dlg, &p);
    TrackPopupMenuEx(FavoritiesMenu, TPM_LEFTALIGN | TPM_TOPALIGN, p.x, p.y, Dlg, NULL);

    return TRUE;
}

BOOL CAdvancedSEDialog::OnFavoriteOption(WORD itemID)
{
    CALL_STACK_MESSAGE2("CAdvancedSEDialog::OnFavoriteOption(0x%X)", itemID);
    MENUITEMINFO mi;

    mi.cbSize = sizeof(MENUITEMINFO);
    mi.fMask = MIIM_DATA;
    if (!GetMenuItemInfo(FavoritiesMenu, itemID, FALSE, &mi))
    {
        TRACE_E("Nejde ziskat item data vubrane polozky z menu favorities.");
        return TRUE;
    }
    CFavoriteSfx* fav = (CFavoriteSfx*)mi.dwItemData;

    if (LoadFavSettings(&fav->Settings))
    {
        ResetValueControls();
    }

    return TRUE;
}

BOOL CAdvancedSEDialog::OnLastUsed()
{
    CALL_STACK_MESSAGE1("CAdvancedSEDialog::OnLastUsed()");
    if (*LastUsedSfxSet.Name)
    {
        if (LoadFavSettings(&LastUsedSfxSet.Settings))
        {
            ResetValueControls();
        }
    }
    return TRUE;
}

BOOL CAdvancedSEDialog::LoadFavSettings(CSfxSettings* sfxSettings)
{
    CALL_STACK_MESSAGE1("CAdvancedSEDialog::LoadFavSettings()");
    CSfxLang* lang = NULL;
    int i;
    for (i = 0; i < SfxLanguages->Count; i++)
    {
        lang = (*SfxLanguages)[i];
        if (lstrcmpiA(lang->FileName.c_str(), sfxSettings->SfxFile) == 0)
            break;
        lang = NULL;
    }
    if (!lang)
    {
        const std::wstring message = SPLFormatStringOwned(
            LangStr(IDS_NOLANGFILE).c_str(), ZipTextToWide(sfxSettings->SfxFile).c_str());
        SalamanderGeneral->SalMessageBox(Dlg, message.c_str(), LoadStrW(IDS_ERROR).c_str(), MB_OK | MB_ICONEXCLAMATION);
        return FALSE;
    }

    wchar_t langName[128];
    if (GetLocaleInfoW(MAKELCID(MAKELANGID(lang->LangID, SUBLANG_NEUTRAL), SORT_DEFAULT), LOCALE_SLANGUAGE, langName, 128))
    {
        wchar_t* c = wcschr(langName, L' ');
        if (c)
            *c = 0;
        CurrentSfxLang = lang;
        SendDlgItemMessageW(Dlg, IDC_LANGUAGE, CB_SELECTSTRING, -1, reinterpret_cast<LPARAM>(langName));
    }

    TmpSfxSettings.Flags = sfxSettings->Flags;
    lstrcpyA(TmpSfxSettings.Command, sfxSettings->Command);
    lstrcpyA(TmpSfxSettings.SfxFile, sfxSettings->SfxFile);
    lstrcpyA(TmpSfxSettings.Text, sfxSettings->Text);
    lstrcpyA(TmpSfxSettings.Title, sfxSettings->Title);
    TmpSfxSettings.MBoxStyle = sfxSettings->MBoxStyle;
    TmpSfxSettings.SetMBoxText(sfxSettings->MBoxText.c_str());
    lstrcpyA(TmpSfxSettings.MBoxTitle, sfxSettings->MBoxTitle);
    lstrcpyA(TmpSfxSettings.TargetDir, sfxSettings->TargetDir);
    lstrcpyA(TmpSfxSettings.ExtractBtnText, sfxSettings->ExtractBtnText);
    lstrcpyA(TmpSfxSettings.Vendor, sfxSettings->Vendor);
    lstrcpyA(TmpSfxSettings.WWW, sfxSettings->WWW);
    lstrcpyA(TmpSfxSettings.WaitFor, sfxSettings->WaitFor);

    HICON iconLarge, iconSmall;
    CIcon* icons;
    int count;
    int errorID = 0;
    const std::wstring iconFile = ZipTextToWide(sfxSettings->IconFile);
    switch (LoadIcons(iconFile.c_str(), sfxSettings->IconIndex, &icons, &count))
    {
    case 1:
        errorID = IDS_ERROPENICO;
        break;
    case 2:
        errorID = IDS_ERRLOADLIB;
        break;
    case 3:
        errorID = IDS_ERRLOADLIB2;
        break;
    case 4:
        errorID = IDS_ERRLOADICON;
        break;
    }
    if (!errorID)
    {
        if (ExtractIconExW(iconFile.c_str(), sfxSettings->IconIndex,
                           &iconLarge, &iconSmall, 1))
        {
            lstrcpyA(TmpSfxSettings.IconFile, sfxSettings->IconFile);
            TmpSfxSettings.IconIndex = sfxSettings->IconIndex;
            if (SmallIcon)
                DestroyIcon(SmallIcon);
            if (LargeIcon)
                DestroyIcon(LargeIcon);
            SmallIcon = iconSmall;
            LargeIcon = iconLarge;
            if (PackOptions->Icons)
                DestroyIcons(PackOptions->Icons, PackOptions->IconsCount);
            PackOptions->Icons = icons;
            PackOptions->IconsCount = count;
            InvalidateRect(GetDlgItem(Dlg, IDC_EXEICON), NULL, TRUE);
            UpdateWindow(GetDlgItem(Dlg, IDC_EXEICON));
        }
        else
        {
            DestroyIcons(icons, count);
            errorID = IDS_ERRLOADICON;
        }
    }
    if (errorID)
    {
        int err = GetLastError();
        const std::wstring message = FormatZipErrorMessage(errorID, err);
        SalamanderGeneral->SalMessageBox(Dlg, message.c_str(), LoadStrW(IDS_ERROR).c_str(), MB_OK | MB_ICONEXCLAMATION);
    }
    return TRUE;
}

//******************************************************************************
//
// CSfxTextsDialog
//

INT_PTR WINAPI SfxTextsDlgProc(HWND hDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    CALL_STACK_MESSAGE4("SfxTextsDlgProc(, 0x%X, 0x%IX, 0x%IX)", uMsg, wParam,
                        lParam);
    static CSfxTextsDialog* dlg = NULL;

    switch (uMsg)
    {
    case WM_INITDIALOG:
        SalamanderGUI->ArrangeHorizontalLines(hDlg);
        dlg = (CSfxTextsDialog*)lParam;
        dlg->Dlg = hDlg;
        return dlg->DialogProc(uMsg, wParam, lParam);

    default:
        if (dlg)
            return dlg->DialogProc(uMsg, wParam, lParam);
    }
    return FALSE;
}

INT_PTR CSfxTextsDialog::Proceed()
{
    CALL_STACK_MESSAGE1("CSfxTextsDialog::Proceed()");
    return DialogBoxParam(HLanguage, MAKEINTRESOURCE(IDD_SFXTEXTS),
                          Parent, SfxTextsDlgProc, (LPARAM)this);
}

INT_PTR CSfxTextsDialog::DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    CALL_STACK_MESSAGE4("CSfxTextsDialog::DialogProc(0x%X, 0x%IX, 0x%IX)", uMsg,
                        wParam, lParam);
    switch (uMsg)
    {
    case WM_INITDIALOG:
        return OnInit(wParam, lParam);
    case WM_COMMAND:
        switch (LOWORD(wParam))
        {
        //controls
        case IDOK:
            return OnOK(HIWORD(wParam), LOWORD(wParam), (HWND)lParam);
        case IDCANCEL:
            EndDialog(Dlg, IDCANCEL);
            return TRUE;

        case IDC_RESET:
            return OnReset();

        case IDHELP:
            SalamanderGeneral->OpenHtmlHelp(Dlg, HHCDisplayContext, IDD_SFXTEXTS, FALSE);
            return TRUE;
        }
        break;

    case WM_HELP:
        SalamanderGeneral->OpenHtmlHelp(Dlg, HHCDisplayContext, IDD_SFXTEXTS, FALSE);
        return TRUE;

        //case WM_DESTROY: break;
    }
    return FALSE;
}

BOOL CSfxTextsDialog::OnInit(WPARAM wParam, LPARAM lParam)
{
    CALL_STACK_MESSAGE1("CSfxTextsDialog::OnInit");

    //!note: below we rely strictly on the order
    int i = 0;
    SendDlgItemMessage(Dlg, IDC_MBOXBUTTONS, CB_ADDSTRING, 0, (LPARAM)LangStr(IDS_MBOK).c_str());
    SendDlgItemMessage(Dlg, IDC_MBOXBUTTONS, CB_SETITEMDATA, i++, (LPARAM)MB_OK);
    SendDlgItemMessage(Dlg, IDC_MBOXBUTTONS, CB_ADDSTRING, 0, (LPARAM)LangStr(IDS_MBOKCANCEL).c_str());
    SendDlgItemMessage(Dlg, IDC_MBOXBUTTONS, CB_SETITEMDATA, i++, MB_OKCANCEL);
    SendDlgItemMessage(Dlg, IDC_MBOXBUTTONS, CB_ADDSTRING, 0, (LPARAM)LangStr(IDS_MBYESNO).c_str());
    SendDlgItemMessage(Dlg, IDC_MBOXBUTTONS, CB_SETITEMDATA, i++, MB_YESNO);
    SendDlgItemMessage(Dlg, IDC_MBOXBUTTONS, CB_ADDSTRING, 0, (LPARAM)LangStr(IDS_MBAGREEDISAGREE).c_str());
    SendDlgItemMessage(Dlg, IDC_MBOXBUTTONS, CB_SETITEMDATA, i++, SE_MBAGREEDISAGREE);
    i = 0;
    SendDlgItemMessage(Dlg, IDC_MBOXICON, CB_ADDSTRING, 0, (LPARAM)LangStr(IDS_MBNOICON).c_str());
    SendDlgItemMessage(Dlg, IDC_MBOXICON, CB_SETITEMDATA, i++, (LPARAM)0);
    SendDlgItemMessage(Dlg, IDC_MBOXICON, CB_ADDSTRING, 0, (LPARAM)LangStr(IDS_MBEXCLAMATION).c_str());
    SendDlgItemMessage(Dlg, IDC_MBOXICON, CB_SETITEMDATA, i++, MB_ICONEXCLAMATION);
    SendDlgItemMessage(Dlg, IDC_MBOXICON, CB_ADDSTRING, 0, (LPARAM)LangStr(IDS_MBINFORMATION).c_str());
    SendDlgItemMessage(Dlg, IDC_MBOXICON, CB_SETITEMDATA, i++, MB_ICONINFORMATION);
    SendDlgItemMessage(Dlg, IDC_MBOXICON, CB_ADDSTRING, 0, (LPARAM)LangStr(IDS_MBQUESTION).c_str());
    SendDlgItemMessage(Dlg, IDC_MBOXICON, CB_SETITEMDATA, i++, MB_ICONQUESTION);
    SendDlgItemMessage(Dlg, IDC_MBOXICON, CB_ADDSTRING, 0, (LPARAM)LangStr(IDS_LONGMESSAGE).c_str());
    SendDlgItemMessage(Dlg, IDC_MBOXICON, CB_SETITEMDATA, i++, SE_LONGMESSAGE);

    ResetControls(SfxSettings->MBoxStyle, SfxSettings->MBoxTitle, SfxSettings->MBoxText.c_str(),
                  SfxSettings->Title, SfxSettings->Text, SfxSettings->ExtractBtnText,
                  SfxSettings->Vendor, SfxSettings->WWW);

    CenterDlgToParent();
    return TRUE;
}

void CSfxTextsDialog::ResetControls(UINT mboxStyle, const char* mboxTitle, const char* mboxText,
                                    const char* title, const char* text, const char* button,
                                    const char* vendor, const char* www)
{
    CALL_STACK_MESSAGE1("CSfxTextsDialog::ResetControls");
    int i = 0;
    if ((int)mboxStyle < 0)
    {
        switch (mboxStyle)
        {
        case SE_MBOK:
            i = 0;
            break;
        case SE_MBOKCANCEL:
            i = 1;
            break;
        case SE_MBYESNO:
            i = 2;
            break;
        case SE_MBAGREEDISAGREE:
            i = 3;
            break;
        }
    }
    else
    {
        switch (mboxStyle & 0x0F)
        {
        case MB_OK:
            i = 0;
            break;
        case MB_OKCANCEL:
            i = 1;
            break;
        case MB_YESNO:
            i = 2;
            break;
        }
    }
    SendDlgItemMessage(Dlg, IDC_MBOXBUTTONS, CB_SETCURSEL, i, 0);
    if ((int)mboxStyle < 0)
        i = 4;
    else
    {
        switch (mboxStyle & 0xF0)
        {
        case 0:
            i = 0;
            break;
        case MB_ICONEXCLAMATION:
            i = 1;
            break;
        case MB_ICONINFORMATION:
            i = 2;
            break;
        case MB_ICONQUESTION:
            i = 3;
            break;
        }
    }
    SendDlgItemMessage(Dlg, IDC_MBOXICON, CB_SETCURSEL, i, 0);
    const auto setEncodedText = [this](int item, const char* value)
    {
        const std::wstring wide = ZipTextToWide(value);
        SendDlgItemMessageW(Dlg, item, EM_SETLIMITTEXT, 0, 0);
        SetDlgItemTextW(Dlg, item, wide.c_str());
    };
    setEncodedText(IDC_MBOXTEXT, mboxText);
    setEncodedText(IDC_MBOXTITLE, mboxTitle);
    setEncodedText(IDC_TEXT, text);
    setEncodedText(IDC_TITLE, title);
    setEncodedText(IDC_BUTTONTEXT, button);
    setEncodedText(IDE_VENDOR, vendor);
    setEncodedText(IDE_WWW, www);
}

BOOL CSfxTextsDialog::OnOK(WORD wNotifyCode, WORD wID, HWND hwndCtl)
{
    CALL_STACK_MESSAGE1("CSfxTextsDialog::OnOK()");
    CSfxSettings settings = *SfxSettings;

    settings.MBoxStyle = 0;
    // read the message box type
    LRESULT i = SendDlgItemMessage(Dlg, IDC_MBOXICON, CB_GETCURSEL, 0, 0);
    if (i != CB_ERR)
    {
        UINT ui = (UINT)(SendDlgItemMessage(Dlg, IDC_MBOXICON, CB_GETITEMDATA, i, 0) & 0xffffffff); // X64 - ITEMDATA contains a DWORD
        if (ui != CB_ERR)
            settings.MBoxStyle |= ui;
    }
    // read the button configuration
    i = SendDlgItemMessage(Dlg, IDC_MBOXBUTTONS, CB_GETCURSEL, 0, 0);
    if (i != CB_ERR)
    {
        UINT ui = (UINT)(SendDlgItemMessage(Dlg, IDC_MBOXBUTTONS, CB_GETITEMDATA, i, 0) & 0xffffffff); // X64 - ITEMDATA contains a DWORD
        if (ui != CB_ERR)
        {
            if ((int)settings.MBoxStyle < 0)
            {
                switch (i)
                {
                case 0:
                    ui = SE_MBOK;
                    break;
                case 1:
                    ui = SE_MBOKCANCEL;
                    break;
                case 2:
                    ui = SE_MBYESNO;
                    break;
                case 3:
                    ui = SE_MBAGREEDISAGREE;
                    break;
                }
            }
            else
            {
                if ((int)ui < 0)
                {
                    SalamanderGeneral->SalMessageBox(Dlg, LoadStrW(IDS_BADMSGBOXTYPE).c_str(), LoadStrW(IDS_ERROR).c_str(), MB_OK | MB_ICONEXCLAMATION);
                    return TRUE;
                }
            }
            settings.MBoxStyle |= ui;
        }
    }

    const std::wstring messageText = SPLGetDlgItemTextOwned(Dlg, IDC_MBOXTEXT);
    if (!TryWideToZipText(messageText.c_str(), settings.MBoxText))
    {
        SalamanderGeneral->SalMessageBox(Dlg, LoadStrW(IDS_ERRFORMAT).c_str(), LoadStrW(IDS_ERROR).c_str(),
                                         MB_OK | MB_ICONEXCLAMATION);
        return TRUE;
    }
    const auto readEncodedField = [this](int item, char* field, int fieldSize)
    {
        const std::wstring value = SPLGetDlgItemTextOwned(Dlg, item);
        if (CopyWideToZipText(value.c_str(), field, fieldSize))
            return true;
        SalamanderGeneral->SalMessageBox(Dlg, LoadStrW(IDS_TOOLONGNAME).c_str(), LoadStrW(IDS_ERROR).c_str(),
                                         MB_OK | MB_ICONEXCLAMATION);
        SetFocus(GetDlgItem(Dlg, item));
        return false;
    };
    if (!readEncodedField(IDC_MBOXTITLE, settings.MBoxTitle, _countof(settings.MBoxTitle)))
        return TRUE;
    if (!lstrlenA(settings.MBoxTitle) && !settings.MBoxText.empty())
    {
        SalamanderGeneral->SalMessageBox(Dlg, LoadStrW(IDS_ERRBADMBOXTITLE).c_str(), LoadStrW(IDS_ERROR).c_str(), MB_OK | MB_ICONEXCLAMATION);
        return TRUE;
    }

    if (!readEncodedField(IDC_TITLE, settings.Title, _countof(settings.Title)))
        return TRUE;
    if (!lstrlenA(settings.Title))
    {
        SalamanderGeneral->SalMessageBox(Dlg, LoadStrW(IDS_ERRBADTITLE).c_str(), LoadStrW(IDS_ERROR).c_str(), MB_OK | MB_ICONEXCLAMATION);
        return TRUE;
    }
    if (!readEncodedField(IDC_TEXT, settings.Text, _countof(settings.Text)) ||
        !readEncodedField(IDC_BUTTONTEXT, settings.ExtractBtnText,
                          _countof(settings.ExtractBtnText)))
        return TRUE;
    if (!lstrlenA(settings.ExtractBtnText))
    {
        SalamanderGeneral->SalMessageBox(Dlg, LoadStrW(IDS_BADBUTTONTEXT).c_str(), LoadStrW(IDS_ERROR).c_str(), MB_OK | MB_ICONEXCLAMATION);
        return TRUE;
    }
    if (!readEncodedField(IDE_VENDOR, settings.Vendor, _countof(settings.Vendor)) ||
        !readEncodedField(IDE_WWW, settings.WWW, _countof(settings.WWW)))
        return TRUE;

    *SfxSettings = settings;

    EndDialog(Dlg, IDOK);
    return TRUE;
}

BOOL CSfxTextsDialog::OnReset()
{
    CALL_STACK_MESSAGE1("CSfxTextsDialog::OnReset()");
    ResetControls(MB_OK, "", "", CurrentSfxLang->DlgTitle, CurrentSfxLang->DlgText,
                  CurrentSfxLang->ButtonText, CurrentSfxLang->Vendor,
                  CurrentSfxLang->WWW);
    return TRUE;
}

//******************************************************************************
//
// CManageFavoritiesDialog
//

INT_PTR WINAPI ManageFavoritiesDlgProc(HWND hDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    CALL_STACK_MESSAGE4("ManageFavoritiesDlgProc(, 0x%X, 0x%IX, 0x%IX)", uMsg,
                        wParam, lParam);
    static CManageFavoritiesDialog* dlg = NULL;

    switch (uMsg)
    {
    case WM_INITDIALOG:
        SalamanderGUI->ArrangeHorizontalLines(hDlg);
        dlg = (CManageFavoritiesDialog*)lParam;
        dlg->Dlg = hDlg;
        return dlg->DialogProc(uMsg, wParam, lParam);

    default:
        if (dlg)
            return dlg->DialogProc(uMsg, wParam, lParam);
    }
    return FALSE;
}

INT_PTR CManageFavoritiesDialog::Proceed()
{
    CALL_STACK_MESSAGE1("CManageFavoritiesDialog::Proceed()");
    return DialogBoxParam(HLanguage, MAKEINTRESOURCE(IDD_MANFAVS),
                          Parent, ManageFavoritiesDlgProc, (LPARAM)this);
}

INT_PTR CManageFavoritiesDialog::DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    CALL_STACK_MESSAGE4("CManageFavoritiesDialog::DialogProc(0x%X, 0x%IX, 0x%IX)",
                        uMsg, wParam, lParam);
    switch (uMsg)
    {
    case WM_INITDIALOG:
        return OnInit(wParam, lParam);
    case WM_COMMAND:
        switch (LOWORD(wParam))
        {
        //controls
        case IDOK:
            EndDialog(Dlg, IDOK);
            return TRUE;
        case IDCANCEL:
            EndDialog(Dlg, IDCANCEL);
            return TRUE;

        case IDC_FAVORITIES:
            return OnFavorities(HIWORD(wParam), LOWORD(wParam), (HWND)lParam);
        case IDC_RENAME:
            return OnRenameFavorite();
        case IDC_REMOVE:
            return OnRemoveFavorite();
        case IDC_REMOVEALL:
            return OnRemoveAllFavorities();
        }
        break;

        //case WM_DESTROY: break;
    }
    return FALSE;
}

BOOL CManageFavoritiesDialog::OnInit(WPARAM wParam, LPARAM lParam)
{
    CALL_STACK_MESSAGE1("CManageFavoritiesDialog::OnInit");

    HWND wnd = GetDlgItem(Dlg, IDC_FAVORITIES);

    int i;
    for (i = 0; i < Favorities.Count; i++)
    {
        // Name stays a persisted narrow field; the listbox is wide.
        SendMessageW(wnd, LB_ADDSTRING, 0,
                     reinterpret_cast<LPARAM>(ZipTextToWide(Favorities[i]->Name).c_str()));
    }
    SendMessage(wnd, LB_SETCURSEL, 0, 0);

    CenterDlgToParent();
    return TRUE;
}

BOOL CManageFavoritiesDialog::OnFavorities(WORD wNotifyCode, WORD wID, HWND hwndCtl)
{
    CALL_STACK_MESSAGE3("CManageFavoritiesDialog::OnFavorities(0x%X, 0x%X, )",
                        wNotifyCode, wID);
    switch (wNotifyCode)
    {
    case LBN_KILLFOCUS:
    {
        /*
      SendMessage(hwndCtl, LB_SETCURSEL, -1, 0);
      FavFocus = FALSE;
      */
        break;
    }
    case LBN_SETFOCUS:
    {
        /*
      int i = SendMessage(hwndCtl, LB_GETCARETINDEX, 0, 0);
      if (i != LB_ERR) SendMessage(hwndCtl, LB_SETCURSEL, i, 0);
      FavFocus = TRUE;
      */
        break;
    }
    }
    return FALSE;
}

BOOL CManageFavoritiesDialog::OnRenameFavorite()
{
    CALL_STACK_MESSAGE1("CManageFavoritiesDialog::OnRenameFavorite()");
    HWND wnd = GetDlgItem(Dlg, IDC_FAVORITIES);
    int i = (int)SendMessage(wnd, LB_GETCARETINDEX, 0, 0);
    if (i != LB_ERR && i >= 0 && i < Favorities.Count)
    {
        CRenFavDialog renFav(Dlg, Favorities[i]->Name, true);

        if (renFav.Proceed() == IDOK)
        {
            CFavoriteSfx* fav = Favorities[i];
            SortFavoriteSettings(0, Favorities.Count - 1);
            SendMessage(wnd, LB_RESETCONTENT, 0, 0);
            int j = 0;
            for (i = 0; i < Favorities.Count; i++)
            {
                SendMessageW(wnd, LB_ADDSTRING, 0,
                             reinterpret_cast<LPARAM>(ZipTextToWide(Favorities[i]->Name).c_str()));
                if (strcmp(fav->Name, Favorities[i]->Name) == 0)
                    j = i;
            }
            /*
      SendMessage(wnd, LB_SETCARETINDEX, j, FALSE);
      if (FavFocus) SendMessage(wnd, LB_SETCURSEL, j, 0);
      */
            SendMessage(wnd, LB_SETCURSEL, j, 0);
        }
    }
    return TRUE;
}

BOOL CManageFavoritiesDialog::OnRemoveFavorite()
{
    CALL_STACK_MESSAGE1("CManageFavoritiesDialog::OnRemoveFavorite()");
    int i = (int)SendDlgItemMessage(Dlg, IDC_FAVORITIES, LB_GETCARETINDEX, 0, 0);
    if (i != LB_ERR && i >= 0 && i < Favorities.Count)
    {
        wchar_t buf[500];
        swprintf_s(buf, LangStr(IDS_REMOVEWARN).c_str(), ZipTextToWide(Favorities[i]->Name).c_str());
        if (SalamanderGeneral->SalMessageBox(Dlg, buf, LoadStrW(IDS_REMOVEWARNTITLE).c_str(), MB_YESNO) == IDYES)
        {
            SendDlgItemMessage(Dlg, IDC_FAVORITIES, LB_DELETESTRING, i, 0);
            Favorities.Delete(i);
            if (Favorities.Count > i + 1)
                SortFavoriteSettings(i, Favorities.Count - 1);
        }
        if (Favorities.Count)
        {
            int j = Favorities.Count > i ? i : Favorities.Count - 1;
            /*
      SendDlgItemMessage(Dlg, IDC_FAVORITIES, LB_SETCARETINDEX, j, FALSE);
      if (FavFocus) SendDlgItemMessage(Dlg, IDC_FAVORITIES, LB_SETCURSEL, j, 0);
      */
            SendDlgItemMessage(Dlg, IDC_FAVORITIES, LB_SETCURSEL, j, 0);
        }
    }
    return TRUE;
}

BOOL CManageFavoritiesDialog::OnRemoveAllFavorities()
{
    CALL_STACK_MESSAGE1("CManageFavoritiesDialog::OnRemoveAllFavorities()");
    if (Favorities.Count &&
        SalamanderGeneral->SalMessageBox(Dlg, LoadStrW(IDS_REMOVEALLWARN).c_str(),
                                         LoadStrW(IDS_REMOVEWARNTITLE).c_str(), MB_YESNO) == IDYES)
    {
        SendDlgItemMessage(Dlg, IDC_FAVORITIES, LB_RESETCONTENT, 0, 0);
        Favorities.Destroy();
    }
    return TRUE;
}

INT_PTR ManageFavoritiesDialog(HWND parent)
{
    CALL_STACK_MESSAGE1("ManageFavoritiesDialog()");
    CManageFavoritiesDialog dlg(parent);
    return dlg.Proceed();
}
