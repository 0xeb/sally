// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"
#include "splitcbn.h"
#include "splitcbn.rh"
#include "splitcbn.rh2"
#include "lang\lang.rh"
#include "dialogs.h"
#include "split.h"
#include "combine.h"
#include "plugindarkmode.h"

// *****************************************************************************
//
//  SPLIT DIALOG
//

#define STRICT_SYNTAX // disallows nonsense values in COMBO_SIZE

namespace split
{

    static const wchar_t* pszFileName;
    static CQuadWord qwFileSize;
    static std::wstring* pszTargetDir;
    static CQuadWord* pqwPartialSize;

    static HWND hDialog;
    static BOOL bDontHandleEdit = FALSE;

    static CQuadWord GetSizeOfLastPart(const CQuadWord& filesize, const CQuadWord& partsize)
    {
        CALL_STACK_MESSAGE3("GetSizeOfLastPart(%I64u, %I64u)", filesize.Value, partsize.Value);
        if (!partsize.Value)
        {
            TRACE_E("S/C.GetSizeOfLastPart: partsize == 0 ?!?");
            return CQuadWord(0, 0); // guard against division by zero; should not happen
        }
        CQuadWord last;
        last.Value = filesize.Value % partsize.Value;
        return last.Value ? last : partsize;
    }

    static void OnSizeChange(const CQuadWord& size)
    {
        CALL_STACK_MESSAGE2("OnSizeChange(%I64u)", size.Value);
        bDontHandleEdit = TRUE;
        *pqwPartialSize = size;
        if (size != SIZE_AUTODETECT)
        {
            const std::wstring lastPart = SPLPrintDiskSizeOwned(
                SalamanderGeneral, GetSizeOfLastPart(qwFileSize, size), 1);
            SetDlgItemTextW(hDialog, IDC_EDIT_LASTPART, lastPart.c_str());
            if (size.Value)
            {
                const std::wstring count = std::to_wstring(
                    ((qwFileSize - CQuadWord(1, 0)) / size + CQuadWord(1, 0)).Value);
                SetDlgItemTextW(hDialog, IDC_EDIT_NUMBER, count.c_str());
            }
        }
        else
        {
            SetDlgItemTextW(hDialog, IDC_EDIT_LASTPART, L"");
            SetDlgItemTextW(hDialog, IDC_EDIT_NUMBER, L"");
        }
        bDontHandleEdit = FALSE;
    }

    static void OnComboSelChange()
    {
        CALL_STACK_MESSAGE1("OnComboSelChange()");
        int cursel = (int)SendMessage(GetDlgItem(hDialog, IDC_COMBO_SIZE), CB_GETCURSEL, 0, 0);
        if (cursel == CB_ERR)
            return;
        switch (cursel)
        {
        case 0:
            OnSizeChange(CQuadWord(1457664, 0));
            break; // 1.44 MB Floppy
        case 1:
            OnSizeChange(CQuadWord(730112, 0));
            break; // 720 KB Floppy
        case 2:
            OnSizeChange(CQuadWord(1213952, 0));
            break; // 1.2 MB Floppy
        case 3:
            OnSizeChange(CQuadWord(362496, 0));
            break; // 360 KB Floppy
        case 4:
            OnSizeChange(CQuadWord(100431872, 0));
            break; // 100 MB ZIP
        case 5:
            OnSizeChange(CQuadWord(250331136, 0));
            break; // 250 MB ZIP
        case 6:
            OnSizeChange(CQuadWord(125829120, 0));
            break; // 120 MB LS-120
        case 7:
            OnSizeChange(CQuadWord(650 * 1024 * 1024, 0));
            break; // 650 MB
        case 8:
            OnSizeChange(CQuadWord(700 * 1024 * 1024, 0));
            break; // 700 MB
        case 9:
            OnSizeChange(SIZE_AUTODETECT);
            break;
        }
        EnableWindow(GetDlgItem(hDialog, IDOK), TRUE);
    }

    static void OnComboEditChange()
    {
        CALL_STACK_MESSAGE1("OnComboEditChange()");
        const std::wstring text = SPLGetDlgItemTextOwned(hDialog, IDC_COMBO_SIZE);
        std::wstring number;
        number.reserve(text.size());

        // remove spaces and tabs and replace commas with dots
        size_t i = 0;
        while (i < text.size() &&
               (wcschr(L" \t0123456789.,", text[i]) != NULL || text[i] == 0xA0))
        {
            if (text[i] != L' ' && text[i] != L'\t' && text[i] != 0xA0)
                number.push_back((text[i] == L',') ? L'.' : text[i]);
            ++i;
        }

        size_t suffixEnd = i;
        while (suffixEnd < text.size() && wcschr(L" \t()[]{}.;,", text[suffixEnd]) == NULL)
            ++suffixEnd;
        const std::wstring suffix = text.substr(i, suffixEnd - i);

        // determine the multiplier
        DWORD multiplier = 1;
        BOOL ok = TRUE;
        if (!lstrcmpiW(suffix.c_str(), LangStr(IDS_SIZE_KB).c_str()) || !lstrcmpiW(suffix.c_str(), LangStr(IDS_SIZE_K).c_str()))
            multiplier = 1024;
        else if (!lstrcmpiW(suffix.c_str(), LangStr(IDS_SIZE_MB).c_str()))
            multiplier = 1024 * 1024;
        else if (!lstrcmpiW(suffix.c_str(), LangStr(IDS_SIZE_GB).c_str()))
            multiplier = 1024 * 1024 * 1024;
#ifdef STRICT_SYNTAX
        else if (!suffix.empty() && lstrcmpiW(suffix.c_str(), LangStr(IDS_SIZE_B).c_str()) && lstrcmpiW(suffix.c_str(), LangStr(IDS_BYTES).c_str()) && lstrcmpiW(suffix.c_str(), LangStr(IDS_BYTE).c_str()))
            ok = FALSE;
#endif

        double size;
        wchar_t* numberEnd = NULL;
        size = wcstod(number.c_str(), &numberEnd);
        if (ok && numberEnd != number.c_str() && *numberEnd == 0 && size > 0 &&
            (size * multiplier < (double)0xffffffffffffffff))
        {
            OnSizeChange(CQuadWord().SetDouble(size * multiplier));
            EnableWindow(GetDlgItem(hDialog, IDOK), TRUE);
        }
        else
        {
            bDontHandleEdit = TRUE;
            SetDlgItemTextW(hDialog, IDC_EDIT_NUMBER, L"?");
            SetDlgItemTextW(hDialog, IDC_EDIT_LASTPART, L"?");
            EnableWindow(GetDlgItem(hDialog, IDOK), FALSE);
            bDontHandleEdit = FALSE;
        }
    }

    static void OnEditChange()
    {
        CALL_STACK_MESSAGE1("OnEditChange()");
        if (bDontHandleEdit)
            return;
        std::wstring text = SPLGetDlgItemTextOwned(hDialog, IDC_EDIT_NUMBER);
        int parts = _wtol(text.c_str());
        if (CQuadWord(2 * parts, 0) > qwFileSize)
        {
            parts = (int)(qwFileSize.Value / 2);
            bDontHandleEdit = TRUE;
            text = std::to_wstring(parts);
            SetDlgItemTextW(hDialog, IDC_EDIT_NUMBER, text.c_str());
            bDontHandleEdit = FALSE;
        }
        if (parts > 0)
        {
            CQuadWord size = (qwFileSize + CQuadWord(parts - 1, 0)) / CQuadWord(parts, 0); // round up
            *pqwPartialSize = size;
            const std::wstring sizeText = SPLNumberToStrOwned(SalamanderGeneral, size);
            SetDlgItemTextW(hDialog, IDC_COMBO_SIZE, sizeText.c_str());
            const std::wstring lastPart = SPLPrintDiskSizeOwned(
                SalamanderGeneral, GetSizeOfLastPart(qwFileSize, size), 1);
            SetDlgItemTextW(hDialog, IDC_EDIT_LASTPART, lastPart.c_str());
            EnableWindow(GetDlgItem(hDialog, IDOK), TRUE);
        }
        else
        {
            SetDlgItemTextW(hDialog, IDC_COMBO_SIZE, L"");
            SetDlgItemTextW(hDialog, IDC_EDIT_LASTPART, L"?");
            EnableWindow(GetDlgItem(hDialog, IDOK), FALSE);
        }
    }

    static INT_PTR CALLBACK SplitDlgProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
    {
        CALL_STACK_MESSAGE4("SplitDlgProc( , 0x%X, 0x%IX, 0x%IX)", uMsg, wParam, lParam);
        switch (uMsg)
        {
        case WM_INITDIALOG:
        {
            hDialog = hWnd;
            SalamanderGUI->ArrangeHorizontalLines(hWnd);
            CenterWindow(hWnd);

            SendDlgItemMessage(hWnd, IDC_SPLIT_ICON, STM_SETIMAGE, IMAGE_ICON,
                               (LPARAM)LoadIcon(DLLInstance, MAKEINTRESOURCE(IDI_SPLIT)));

            // composed WIDE throughout. This produced a wide number, narrowed
            // it, formatted into a narrow buffer, then widened the result again for a wide
            // API - two lossy hops in a path that is wide at both ends.
            const std::wstring numberW = SPLNumberToStrOwned(SalamanderGeneral, qwFileSize);
            const std::wstring titleW = SPLFormatStringOwned(LangStr(IDS_SPLITTITLE).c_str(), numberW.c_str());
            SalamanderGUI->SetSubjectTruncatedText(GetDlgItem(hWnd, IDC_STATIC_TITLE), titleW.c_str(),
                                                   pszFileName, FALSE, FALSE);

            SetDlgItemTextW(hWnd, IDC_EDIT_DIR, pszTargetDir->c_str());
            CheckDlgButton(hWnd, IDC_RADIO_SIZE, BST_CHECKED);
            SendMessage(hWnd, WM_COMMAND, IDC_RADIO_SIZE, 0);
            SendMessage(GetDlgItem(hWnd, IDC_EDIT_NUMBER), EM_SETLIMITTEXT, 3, 0);

            HWND h = GetDlgItem(hWnd, IDC_COMBO_SIZE);
            SendMessage(h, CB_ADDSTRING, 0, (LPARAM)LangStr(IDS_144FLOPPY).c_str());
            SendMessage(h, CB_ADDSTRING, 0, (LPARAM)LangStr(IDS_720FLOPPY).c_str());
            SendMessage(h, CB_ADDSTRING, 0, (LPARAM)LangStr(IDS_12FLOPPY).c_str());
            SendMessage(h, CB_ADDSTRING, 0, (LPARAM)LangStr(IDS_360FLOPPY).c_str());
            SendMessage(h, CB_ADDSTRING, 0, (LPARAM)LangStr(IDS_100MB_ZIP).c_str());
            SendMessage(h, CB_ADDSTRING, 0, (LPARAM)LangStr(IDS_250MB_ZIP).c_str());
            SendMessage(h, CB_ADDSTRING, 0, (LPARAM)LangStr(IDS_120MB_LS120).c_str());
            SendMessage(h, CB_ADDSTRING, 0, (LPARAM)LangStr(IDS_650MB_CDR).c_str());
            SendMessage(h, CB_ADDSTRING, 0, (LPARAM)LangStr(IDS_700MB_CDR).c_str());
            SendMessage(h, CB_ADDSTRING, 0, (LPARAM)LangStr(IDS_AUTODETECT).c_str());
            SendMessage(h, CB_SETCURSEL, 0, 0);
            OnComboSelChange();
            SetFocus(h);

            /*HWND h = GetDlgItem(hWnd, IDC_EDITNUMBER);
      LONG style = GetWindowLong(h, GWL_STYLE);
      SetWindowLong(h, GWL_STYLE, style | WS_VSCROLL);*/
            return FALSE;
        }

        case WM_HELP:
        {
            if ((GetKeyState(VK_CONTROL) & 0x8000) == 0 && (GetKeyState(VK_SHIFT) & 0x8000) == 0)
                SalamanderGeneral->OpenHtmlHelp(hWnd, HHCDisplayContext, IDD_SPLIT, FALSE);
            return TRUE; // do not let F1 fall through to the parent even if help is not displayed
        }

        case WM_COMMAND:
            switch (LOWORD(wParam))
            {
            case IDHELP:
            {
                if ((GetKeyState(VK_CONTROL) & 0x8000) == 0 && (GetKeyState(VK_SHIFT) & 0x8000) == 0)
                    SalamanderGeneral->OpenHtmlHelp(hWnd, HHCDisplayContext, IDD_SPLIT, FALSE);
                return TRUE;
            }

            case IDOK:
            {
                *pszTargetDir = SPLGetDlgItemTextOwned(hWnd, IDC_EDIT_DIR);
                EndDialog(hWnd, TRUE);
                break;
            }

            case IDCANCEL:
                EndDialog(hWnd, FALSE);
                break;

            case IDC_RADIO_NUMBER:
            case IDC_RADIO_SIZE:
            {
                BOOL bSize = (LOWORD(wParam) == IDC_RADIO_SIZE);
                EnableWindow(GetDlgItem(hWnd, IDC_COMBO_SIZE), bSize);
                EnableWindow(GetDlgItem(hWnd, IDC_EDIT_NUMBER), !bSize);
                if (!bSize)
                    if (*pqwPartialSize == SIZE_AUTODETECT)
                        SetDlgItemTextW(hWnd, IDC_EDIT_NUMBER, L"1");
                    else
                        OnEditChange();
                break;
            }

            case IDC_COMBO_SIZE:
                switch (HIWORD(wParam))
                {
                case CBN_EDITCHANGE:
                    OnComboEditChange();
                    break;
                case CBN_SELCHANGE:
                    OnComboSelChange();
                    break;
                }
                break;

            case IDC_EDIT_NUMBER:
                if (HIWORD(wParam) == EN_CHANGE)
                    OnEditChange();
                break;

            case IDC_BUTTON_BROWSE:
            {
                HWND parent = SalamanderGeneral->GetMsgBoxParent();
                std::wstring text = SPLGetDlgItemTextOwned(hWnd, IDC_EDIT_DIR);
                const std::wstring initialDirectory = text;
                if (SPLGetTargetDirectoryOwned(
                        SalamanderGeneral, hWnd, parent, LangStr(IDS_SPLIT).c_str(),
                        LangStr(IDS_SELECTDIR).c_str(), text, FALSE,
                        initialDirectory.c_str()))
                    SetDlgItemTextW(hWnd, IDC_EDIT_DIR, text.c_str());
                break;
            }

            case IDC_BUTTON_CONFIG:
            {
                BOOL oldSplitToOther = configSplitToOther;
                BOOL oldSplitToSubdir = configSplitToSubdir;
                ConfigDialog(hWnd);
                if (oldSplitToOther != configSplitToOther || oldSplitToSubdir != configSplitToSubdir)
                {
                    std::wstring targetDirW;
                    if (GetTargetDir(targetDirW, pszFileName, TRUE))
                        SetDlgItemTextW(hWnd, IDC_EDIT_DIR, targetDirW.c_str());
                }
                break;
            }
            }
            return TRUE;

        default:
            return FALSE;
        }
    }

} // namespace split
using namespace split;

BOOL SplitDialog(const wchar_t* fileName, CQuadWord& fileSize,
                 std::wstring& targetDir,
                 CQuadWord* partialSize, HWND hParent)
{
    CALL_STACK_MESSAGE4("SplitDialog(%ls, %I64u, %ls, , )", fileName,
                        fileSize.Value, targetDir.c_str());
    pszFileName = fileName;
    qwFileSize = fileSize;
    pszTargetDir = &targetDir;
    pqwPartialSize = partialSize;
    return (BOOL)DialogBoxParam(HLanguage, MAKEINTRESOURCE(IDD_SPLIT), hParent, SplitDlgProc, 0);
}

// *****************************************************************************
//
//  COMBINE DIALOG
//

namespace combine
{

    static TIndirectArray<wchar_t>* files;
    static std::wstring* targetName;
    static BOOL bOrigCrcFound;
    static UINT32 origCrc;
    static UINT uDragMsg;
    static HWND hDialog, hLB;
    static int DragIndex = -1;
    static int currentIndex = -1;
    static HICON hFileIcon;
    static CSalamanderForOperationsAbstract* salamander;

    struct ITEMDATA
    {
        int index;
        //HICON hIcon;
        std::wstring text;
    };

    static BOOL AddFile(const wchar_t* fullName, BOOL bUpdateArray = TRUE)
    {
        CALL_STACK_MESSAGE3("AddFile(%ls, %ld)", fullName, bUpdateArray);

        if (bUpdateArray)
        {
            wchar_t* dup = _wcsdup(fullName);
            if (dup == NULL)
            {
                SalamanderGeneral->SalMessageBox(hDialog, LangStr(IDS_OUTOFMEM).c_str(), LangStr(IDS_COMBINE).c_str(),
                                                 MB_OK | MB_ICONEXCLAMATION);
                return FALSE;
            }
            files->Add(dup);
        }

        std::wstring dir;
        SPLGetPanelPathOwned(SalamanderGeneral, PANEL_SOURCE, dir);
        const wchar_t* name = SalamanderGeneral->SalPathFindFileName(fullName);
        ITEMDATA* pid = new ITEMDATA;
        if ((name - fullName - 1) == static_cast<int>(dir.size()) &&
            !_wcsnicmp(dir.c_str(), fullName, name - fullName - 1))
            pid->text = name;
        else
            pid->text = fullName;
        pid->index = (int)SendMessage(hLB, LB_GETCOUNT, 0, 0) - 1;
        SendMessage(hLB, LB_INSERTSTRING, pid->index, 1);
        /*SHFILEINFO sfi;
  SHGetFileInfo(fullName, 0, &sfi, sizeof(sfi), SHGFI_ICON | SHGFI_SMALLICON);
  pid->hIcon = sfi.hIcon;*/
        SendMessage(hLB, LB_SETITEMDATA, pid->index, (LPARAM)pid);

        SendMessage(hLB, LB_SETCURSEL, currentIndex = pid->index, 0);
        return TRUE;
    }

    static void MoveItem(int oldindex, int newindex)
    {
        CALL_STACK_MESSAGE3("MoveItem(%ld, %ld)", oldindex, newindex);
        if (oldindex < newindex)
            newindex--;
        if (oldindex == newindex)
            return;

        wchar_t* olditem = (*files)[oldindex];
        wchar_t** data = (wchar_t**)files->GetData();
        if (oldindex > newindex)
            memmove(data + newindex + 1, data + newindex, (oldindex - newindex) * sizeof(wchar_t*));
        else
            memmove(data + oldindex, data + oldindex + 1, (newindex - oldindex) * sizeof(wchar_t*));
        (*files)[newindex] = olditem;

        SendMessage(hLB, WM_SETREDRAW, FALSE, 0);
        ITEMDATA* pid = (ITEMDATA*)SendMessage(hLB, LB_GETITEMDATA, oldindex, 0);
        SendMessage(hLB, LB_DELETESTRING, oldindex, 0);
        SendMessage(hLB, LB_INSERTSTRING, newindex, 1);
        SendMessage(hLB, LB_SETITEMDATA, newindex, (LPARAM)pid);
        SendMessage(hLB, LB_SETCURSEL, newindex, 0);
        SendMessage(hLB, WM_SETREDRAW, TRUE, 0);
        currentIndex = newindex;
    }

    static HWND ClearDefaultStyle(int id)
    {
        HWND hCtrl = GetDlgItem(hDialog, id);
        LONG style = GetWindowLong(hCtrl, GWL_STYLE);
        SetWindowLong(hCtrl, GWL_STYLE, style & ~BS_DEFPUSHBUTTON);
        InvalidateRect(hCtrl, NULL, FALSE);
        return hCtrl;
    }

    static void EnableButtons()
    {
        CALL_STACK_MESSAGE1("EnableButtons()");
        EnableWindow(ClearDefaultStyle(IDC_BUTTON_UP),
                     currentIndex != LB_ERR && currentIndex > 0 && currentIndex < files->Count);
        EnableWindow(ClearDefaultStyle(IDC_BUTTON_DOWN),
                     currentIndex != LB_ERR && currentIndex < files->Count - 1);
        EnableWindow(ClearDefaultStyle(IDC_BUTTON_REMOVE),
                     currentIndex != LB_ERR && currentIndex < files->Count);
        EnableWindow(GetDlgItem(hDialog, IDC_BUTTON_CRC),
                     files->Count ? TRUE : FALSE);
        SendMessage(hDialog, DM_SETDEFID, IDOK, 0);
    }

    static void OnRemove(int index)
    {
        CALL_STACK_MESSAGE2("OnRemove(%ld)", index);
        files->Delete(index);
        delete (ITEMDATA*)SendMessage(hLB, LB_GETITEMDATA, index, 0);
        SendMessage(hLB, LB_DELETESTRING, index, 0);
        if (index && index >= files->Count)
            index--;
        SendMessage(hLB, LB_SETCURSEL, currentIndex = index, 0);
        EnableButtons();
    }

    static void OnAdd()
    {
        CALL_STACK_MESSAGE1("OnAdd()");
        OPENFILENAME ofn;
        ZeroMemory(&ofn, sizeof(ofn));
        ofn.lStructSize = sizeof(ofn);
        ofn.hwndOwner = hDialog;
        ofn.hInstance = HLanguage;
        ofn.lpstrCustomFilter = NULL;
        ofn.Flags = OFN_ALLOWMULTISELECT | OFN_PATHMUSTEXIST | OFN_EXPLORER | OFN_READONLY | OFN_NOCHANGEDIR;

        std::wstring initdirW;
        SPLGetPanelPathOwned(SalamanderGeneral, PANEL_SOURCE, initdirW);
        std::wstring filterW = LangStr(IDS_ADDFILTER).c_str();
        filterW.push_back(L'\0');
        filterW += L"*.*";
        filterW.push_back(L'\0');
        filterW.push_back(L'\0');
        ofn.lpstrFilter = filterW.c_str();
        std::wstring titleW = LangStr(IDS_ADDTITLE).c_str();
        ofn.lpstrTitle = titleW.c_str();
        ofn.lpstrInitialDir = initdirW.c_str();

        std::vector<std::wstring> selectedFiles;
        if (SPLSafeGetOpenFileNamesOwned(SalamanderGeneral, &ofn,
                                         selectedFiles))
        {
            SendMessage(hLB, WM_SETREDRAW, FALSE, 0);
            for (const std::wstring& fullName : selectedFiles)
            {
                if (!AddFile(fullName.c_str()))
                    return;
            }
            SendMessage(hLB, WM_SETREDRAW, TRUE, 0);
        }
    }

    static void OnBrowse()
    {
        CALL_STACK_MESSAGE1("OnBrowse()");
        OPENFILENAME ofn;
        ZeroMemory(&ofn, sizeof(ofn));
        ofn.lStructSize = sizeof(ofn);
        ofn.hwndOwner = hDialog;
        ofn.hInstance = HLanguage;
        ofn.lpstrCustomFilter = NULL;
        ofn.Flags = OFN_PATHMUSTEXIST | OFN_EXPLORER | OFN_OVERWRITEPROMPT | OFN_NOCHANGEDIR;
        std::wstring initdirW;
        SPLGetPanelPathOwned(SalamanderGeneral, PANEL_SOURCE, initdirW);
        std::wstring fileW;
        std::wstring filterW = LangStr(IDS_ADDFILTER).c_str();
        filterW.push_back(L'\0');
        filterW += L"*.*";
        filterW.push_back(L'\0');
        filterW.push_back(L'\0');
        ofn.lpstrFilter = filterW.c_str();
        std::wstring titleW = LangStr(IDS_BROWSETITLE).c_str();
        ofn.lpstrTitle = titleW.c_str();
        ofn.lpstrInitialDir = initdirW.c_str();
        if (SPLSafeGetSaveFileNameOwned(SalamanderGeneral, &ofn, fileW))
            SetDlgItemTextW(hDialog, IDC_EDIT_TARGET, fileW.c_str());
    }

    static void OnCRC(HWND parent)
    {
        CALL_STACK_MESSAGE1("OnCRC( )");
        CRCDialog(*files, bOrigCrcFound, origCrc, parent, salamander);
    }

    static void UpdateHorizontalScrollbar(HWND hWnd)
    {
        int minWidth = 0;
        HFONT hFont = (HFONT)SendMessage(hWnd, WM_GETFONT, 0, 0);
        HDC hDC = GetDC(hWnd);
        HFONT hOldFont = (HFONT)SelectObject(hDC, hFont);
        int cnt = (int)SendMessage(hWnd, LB_GETCOUNT, 0, 0);

        int i;
        for (i = 0; i < cnt; i++)
        {
            SIZE sz;
            ITEMDATA* pid = (ITEMDATA*)SendMessage(hLB, LB_GETITEMDATA, i, 0);

            if (pid && (pid->index >= 0))
            {
                GetTextExtentPoint32W(hDC, pid->text.c_str(),
                                      static_cast<int>(pid->text.size()), &sz);
                if (sz.cx > minWidth)
                    minWidth = sz.cx;
            }
        }
        SendMessage(hLB, LB_SETHORIZONTALEXTENT, 23 /*icon*/ + 5 /*reasonable border*/ + minWidth, 0);
        SelectObject(hDC, hOldFont);
        ReleaseDC(hWnd, hDC);
    }

    static INT_PTR CALLBACK CombineDlgProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
    {
        CALL_STACK_MESSAGE4("CombineDlgProc( , 0x%X, 0x%IX, 0x%IX)", uMsg, wParam, lParam);
        switch (uMsg)
        {
        case WM_INITDIALOG:
        {
            SalamanderGUI->ArrangeHorizontalLines(hWnd);
            CenterWindow(hDialog = hWnd);

            hLB = GetDlgItem(hWnd, IDC_LIST_FILES);
            MakeDragList(hLB);
            uDragMsg = RegisterWindowMessage(DRAGLISTMSGSTRING);

            ITEMDATA* pid = new ITEMDATA;
            pid->index = -1;
            SendMessage(hLB, LB_INSERTSTRING, 0, 1);
            SendMessage(hLB, LB_SETITEMDATA, 0, (LPARAM)pid);

            int i;
            for (i = 0; i < files->Count; i++)
                AddFile((*files)[i], FALSE);

            int minHeight = 16;
            HFONT hFont = (HFONT)SendMessage(hLB, WM_GETFONT, 0, 0);
            HDC hDC = GetDC(hWnd);
            TEXTMETRIC tm;
            HFONT hOldFont = (HFONT)SelectObject(hDC, hFont);
            GetTextMetrics(hDC, &tm);
            minHeight = max(tm.tmHeight, minHeight);
            SelectObject(hDC, hOldFont);
            ReleaseDC(hWnd, hDC);
            minHeight += 2;
            SendMessage(hLB, LB_SETITEMHEIGHT, 0, minHeight);

            SendMessage(hLB, LB_SETCURSEL, files->Count, 0);
            SendMessage(hLB, LB_SETCURSEL, 0, 0);
            SendMessage(hLB, LB_SETCURSEL, currentIndex = -1, 0);
            UpdateHorizontalScrollbar(hLB);

            HWND hEdit = GetDlgItem(hWnd, IDC_EDIT_TARGET);
            SetWindowTextW(hEdit, targetName->c_str());
            SetFocus(GetDlgItem(hWnd, IDC_EDIT_TARGET));
            SendMessage(hEdit, EM_SETSEL, 0, -1);

            hFileIcon = (HICON)LoadImage(DLLInstance, MAKEINTRESOURCE(IDI_FILE), IMAGE_ICON,
                                         0, 0, LR_DEFAULTCOLOR);

            EnableButtons();
            return FALSE;
        }

        case WM_DESTROY:
        {
            int count = (int)SendMessage(hLB, LB_GETCOUNT, 0, 0);
            int i;
            for (i = 0; i < count; i++)
                delete (ITEMDATA*)SendMessage(hLB, LB_GETITEMDATA, i, 0);
            DestroyIcon(hFileIcon);
            return TRUE;
        }

        case WM_HELP:
        {
            if ((GetKeyState(VK_CONTROL) & 0x8000) == 0 && (GetKeyState(VK_SHIFT) & 0x8000) == 0)
                SalamanderGeneral->OpenHtmlHelp(hWnd, HHCDisplayContext, IDD_COMBINE, FALSE);
            return TRUE; // do not let F1 fall through to the parent even if help is not displayed
        }

        case WM_COMMAND:
        {
            switch (LOWORD(wParam))
            {
            case IDHELP:
                if ((GetKeyState(VK_CONTROL) & 0x8000) == 0 && (GetKeyState(VK_SHIFT) & 0x8000) == 0)
                    SalamanderGeneral->OpenHtmlHelp(hWnd, HHCDisplayContext, IDD_COMBINE, FALSE);
                break;

            case IDOK:
            {
                *targetName = SPLGetDlgItemTextOwned(hWnd, IDC_EDIT_TARGET);
                EndDialog(hWnd, TRUE);
                break;
            }

            case IDCANCEL:
                EndDialog(hWnd, FALSE);
                break;

            case IDC_BUTTON_UP:
                MoveItem(currentIndex, currentIndex - 1);
                EnableButtons();
                break;

            case IDC_BUTTON_DOWN:
                MoveItem(currentIndex, currentIndex + 2);
                EnableButtons();
                break;

            case IDC_BUTTON_ADD:
                OnAdd();
                UpdateHorizontalScrollbar(GetDlgItem(hWnd, IDC_LIST_FILES));
                EnableButtons();
                break;

            case IDC_BUTTON_REMOVE:
                OnRemove(currentIndex);
                UpdateHorizontalScrollbar(GetDlgItem(hWnd, IDC_LIST_FILES));
                break;

            case IDC_BUTTON_BROWSE2:
                OnBrowse();
                break;

            case IDC_BUTTON_CRC:
                OnCRC(hWnd);
                break;

            case IDC_LIST_FILES:
                if (HIWORD(wParam) == LBN_SELCHANGE)
                {
                    currentIndex = (int)SendMessage(hLB, LB_GETCURSEL, 0, 0);
                    EnableButtons();
                }
                break;
            }
            return TRUE;
        }

        case WM_VKEYTOITEM:
        {
            if (LOWORD(wParam) == VK_DELETE)
            {
                if (HIWORD(wParam) < files->Count)
                {
                    OnRemove(HIWORD(wParam));
                    EnableButtons();
                }
                return -2;
            }
            else
                return -1;
        }

        case WM_DRAWITEM:
        {
            DRAWITEMSTRUCT* dis = (DRAWITEMSTRUCT*)lParam;
            if (dis->itemID == -1)
                return TRUE;
            HDC hDC = dis->hDC;
            RECT r = dis->rcItem;
            ITEMDATA* pid = (ITEMDATA*)dis->itemData;
            BOOL selected = (dis->itemState & ODS_SELECTED);
            BOOL focused = selected && GetFocus() == GetDlgItem(hWnd, dis->CtlID);

            PluginDarkMode_FillOwnerDrawBackground(hDC, &r, selected, focused);

            if (pid->index != -1)
            {
                DrawIconEx(hDC, r.left + 2, r.top + 1, /*pid->hIcon*/ hFileIcon, 16, 16, 0, NULL, DI_NORMAL);
                r.left += 21;
                SetTextColor(hDC, PluginDarkMode_GetOwnerDrawTextColor(selected, TRUE));
                SetBkMode(hDC, TRANSPARENT);
                DrawTextW(hDC, pid->text.c_str(), -1, &r,
                          DT_SINGLELINE | DT_LEFT | DT_VCENTER);
                r.left -= 21;
            }
            if (focused)
            {
                SetTextColor(hDC, RGB(0, 0, 0));
                DrawFocusRect(hDC, &r);
            }

            return TRUE;
        }

        default:
        {
            if (uMsg == uDragMsg && wParam == IDC_LIST_FILES)
            {
                DRAGLISTINFO* pdli = (DRAGLISTINFO*)lParam;
                switch (pdli->uNotification)
                {
                case DL_BEGINDRAG:
                {
                    DragIndex = LBItemFromPt(hLB, pdli->ptCursor, TRUE);
                    if (DragIndex < files->Count)
                        SetWindowLongPtr(hWnd, DWLP_MSGRESULT, TRUE);
                    else
                    {
                        DragIndex = -1;
                        SetWindowLongPtr(hWnd, DWLP_MSGRESULT, FALSE);
                    }
                    break;
                }

                case DL_DRAGGING:
                {
                    DrawInsert(hWnd, hLB, LBItemFromPt(hLB, pdli->ptCursor, TRUE));
                    SetCursor(LoadCursor(DLLInstance, MAKEINTRESOURCE(IDC_DRAG)));
                    SetWindowLongPtr(hWnd, DWLP_MSGRESULT, 0);
                    break;
                }

                case DL_DROPPED:
                {
                    int index;
                    if (DragIndex != -1 && (index = LBItemFromPt(hLB, pdli->ptCursor, TRUE)) != -1)
                    {
                        MoveItem(DragIndex, index);
                        DrawInsert(hWnd, hLB, -1);
                        EnableButtons();
                    }
                    break;
                }

                case DL_CANCELDRAG:
                {
                    DrawInsert(hWnd, hLB, -1);
                    DragIndex = -1;
                    break;
                }
                }
                return TRUE;
            }
            else
                return FALSE;
        }
        }
    }

} // namespace combine
using namespace combine;

BOOL CombineDialog(TIndirectArray<wchar_t>& f, std::wstring& t, BOOL b,
                   UINT32 c, HWND hParent,
                   CSalamanderForOperationsAbstract* sal)
{
    CALL_STACK_MESSAGE4("CombineDialog( , %ls, %ld, %X, , )", t.c_str(), b, c);
    files = &f;
    targetName = &t;
    bOrigCrcFound = b;
    origCrc = c;
    salamander = sal;
    uDragMsg = 0xffffffff;
    return (BOOL)DialogBoxParam(HLanguage, MAKEINTRESOURCE(IDD_COMBINE), hParent, CombineDlgProc, 0);
}

// *****************************************************************************
//
//  CONFIG DIALOG
//

static INT_PTR CALLBACK ConfigDlgProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    CALL_STACK_MESSAGE4("ConfigDlgProc( , 0x%X, 0x%IX, 0x%IX)", uMsg, wParam, lParam);
    switch (uMsg)
    {
    case WM_INITDIALOG:
        SalamanderGUI->ArrangeHorizontalLines(hWnd);
        CenterWindow(hWnd);
        CheckDlgButton(hWnd, IDC_CHECK_INCLUDE, configIncludeFileExt ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(hWnd, IDC_CHECK_CREATE, configCreateBatchFile ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(hWnd, IDC_CHECK_SPLITOTHER, configSplitToOther ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(hWnd, IDC_CHECK_COMBINEOTHER, configCombineToOther ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(hWnd, IDC_CHECK_SPLITSUB, configSplitToSubdir /*&& configSplitToOther*/ ? BST_CHECKED : BST_UNCHECKED);
        //EnableWindow(GetDlgItem(hWnd, IDC_CHECK_SPLITSUB), configSplitToOther);
        return TRUE;

    case WM_HELP:
    {
        if ((GetKeyState(VK_CONTROL) & 0x8000) == 0 && (GetKeyState(VK_SHIFT) & 0x8000) == 0)
            SalamanderGeneral->OpenHtmlHelp(hWnd, HHCDisplayContext, IDD_CONFIG, FALSE);
        return TRUE; // do not let F1 fall through to the parent even if help is not displayed
    }

    case WM_COMMAND:
        switch (LOWORD(wParam))
        {
        case IDHELP:
            if ((GetKeyState(VK_CONTROL) & 0x8000) == 0 && (GetKeyState(VK_SHIFT) & 0x8000) == 0)
                SalamanderGeneral->OpenHtmlHelp(hWnd, HHCDisplayContext, IDD_CONFIG, FALSE);
            break;

        case IDOK:
            configIncludeFileExt = IsDlgButtonChecked(hWnd, IDC_CHECK_INCLUDE) == BST_CHECKED;
            configCreateBatchFile = IsDlgButtonChecked(hWnd, IDC_CHECK_CREATE) == BST_CHECKED;
            configSplitToOther = IsDlgButtonChecked(hWnd, IDC_CHECK_SPLITOTHER) == BST_CHECKED;
            configCombineToOther = IsDlgButtonChecked(hWnd, IDC_CHECK_COMBINEOTHER) == BST_CHECKED;
            configSplitToSubdir = IsDlgButtonChecked(hWnd, IDC_CHECK_SPLITSUB) == BST_CHECKED;
            // FALL THROUGH
        case IDCANCEL:
            EndDialog(hWnd, 0);
            return TRUE;

            /*case IDC_CHECK_SPLITOTHER: 
        {
          BOOL splitother = IsDlgButtonChecked(hWnd, IDC_CHECK_SPLITOTHER) == BST_CHECKED;
          if (!splitother) CheckDlgButton(hWnd, IDC_CHECK_SPLITSUB, BST_UNCHECKED);
          EnableWindow(GetDlgItem(hWnd, IDC_CHECK_SPLITSUB), splitother);
          return TRUE;
        }*/
        }
        return TRUE;
    }
    return FALSE;
}

void ConfigDialog(HWND hParent)
{
    CALL_STACK_MESSAGE1("ConfigDialog()");
    DialogBoxParam(HLanguage, MAKEINTRESOURCE(IDD_CONFIG), hParent, ConfigDlgProc, 0);
}

// *****************************************************************************
//
//  CRC DIALOG
//

static BOOL bCrcFound;
static UINT32 originalCrc, calcCrc;

static INT_PTR CALLBACK CRCDlgProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    CALL_STACK_MESSAGE4("CRCDlgProc( , 0x%X, 0x%IX, 0x%IX)", uMsg, wParam, lParam);
    switch (uMsg)
    {
    case WM_INITDIALOG:
    {
        SalamanderGUI->ArrangeHorizontalLines(hWnd);
        CenterWindow(hWnd);
        std::wstring text = SPLFormatStringOwned(LangStr(IDS_CRCHEX).c_str(), calcCrc);
        SetDlgItemTextW(hWnd, IDC_EDIT_CRC1, text.c_str());
        text = SPLFormatStringOwned(LangStr(IDS_CRCDEC).c_str(), calcCrc);
        SetDlgItemTextW(hWnd, IDC_EDIT_CRC2, text.c_str());

        // WARNING! the obtained icon must be destroyed in WM_DESTROY
        HICON icon = (HICON)LoadImage(DLLInstance, MAKEINTRESOURCE(IDI_WARN), IMAGE_ICON, 16, 16, LR_DEFAULTCOLOR);
        SendDlgItemMessage(hWnd, IDC_ICON_WARN, STM_SETIMAGE, IMAGE_ICON, (LPARAM)icon);
        icon = (HICON)LoadImage(DLLInstance, MAKEINTRESOURCE(IDI_OK), IMAGE_ICON, 16, 16, LR_DEFAULTCOLOR);
        SendDlgItemMessage(hWnd, IDC_ICON_OK, STM_SETIMAGE, IMAGE_ICON, (LPARAM)icon);

        if (bCrcFound)
        {
            text = SPLFormatStringOwned(LangStr(IDS_CRCHEX).c_str(), originalCrc);
            SetDlgItemTextW(hWnd, IDC_EDIT_CRC3, text.c_str());
            text = SPLFormatStringOwned(LangStr(IDS_CRCDEC).c_str(), originalCrc);
            SetDlgItemTextW(hWnd, IDC_EDIT_CRC4, text.c_str());
            ShowWindow(GetDlgItem(hWnd, calcCrc == originalCrc ? IDC_ICON_OK : IDC_ICON_WARN), SW_SHOW);
        }
        else
            SetDlgItemTextW(hWnd, IDC_EDIT_CRC3, LangStr(IDS_CRCNOTFOUND).c_str());

        return TRUE;
    }

    case WM_COMMAND:
    {
        if (LOWORD(wParam) == IDCANCEL)
            EndDialog(hWnd, 0);
        return TRUE;
    }

    case WM_DESTROY:
    {
        // an icon obtained without the LR_SHARED flag must be destroyed
        HICON hIcon = (HICON)SendDlgItemMessage(hWnd, IDC_ICON_WARN, STM_SETIMAGE, IMAGE_ICON, NULL);
        if (hIcon != NULL)
            DestroyIcon(hIcon);
        hIcon = (HICON)SendDlgItemMessage(hWnd, IDC_ICON_OK, STM_SETIMAGE, IMAGE_ICON, NULL);
        if (hIcon != NULL)
            DestroyIcon(hIcon);
        return FALSE;
    }

    default:
        return FALSE;
    }
}

void CRCDialog(TIndirectArray<wchar_t>& files, BOOL bf, UINT32 oc, HWND parent,
               CSalamanderForOperationsAbstract* salamander)
{
    CALL_STACK_MESSAGE3("CRCDialog( , %ld, %X, , )", bf, oc);

    UINT32 cc;
    if (!CombineFiles(files, NULL, TRUE, FALSE, cc, FALSE, NULL, parent, salamander))
        return;

    bCrcFound = bf;
    originalCrc = oc;
    calcCrc = cc;

    DialogBoxParam(HLanguage, MAKEINTRESOURCE(IDD_CRC), parent, CRCDlgProc, 0);
}

// *****************************************************************************
//
//  FILE CRC DIALOG
//

/*static BOOL CALLBACK FileCRCDlgProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
  CALL_STACK_MESSAGE4("FileCRCDlgProc( , 0x%X, 0x%IX, 0x%IX)", uMsg, wParam, lParam);
  switch (uMsg)
  {
    case WM_INITDIALOG:
    {
      SalamanderGUI->ArrangeHorizontalLines(hWnd);
      CenterWindow(hWnd);
      std::wstring text = SPLFormatStringOwned(
          LangStr(IDS_FILECRCTITLE).c_str(),
          SalamanderGeneral->GetPanelFocusedItem(PANEL_SOURCE, NULL)->Name);
      SetDlgItemTextW(hWnd, IDC_STATIC_CRCTITLE, text.c_str());
      text = SPLFormatStringOwned(LangStr(IDS_CRCHEX).c_str(), lParam);
      SetDlgItemTextW(hWnd, IDC_EDIT_CRC1, text.c_str());
      text = SPLFormatStringOwned(LangStr(IDS_CRCDEC).c_str(), lParam);
      SetDlgItemTextW(hWnd, IDC_EDIT_CRC2, text.c_str());

      SendDlgItemMessage(hWnd, IDC_ICON_OK, STM_SETIMAGE, IMAGE_ICON,
                         (LPARAM)LoadIcon(DLLInstance, MAKEINTRESOURCE(IDI_OK)));

      return TRUE;
    }

    case WM_COMMAND:
    {
      if (LOWORD(wParam) == IDCANCEL) EndDialog(hWnd, 0);
      return TRUE;
    }

    default:
      return FALSE;
  }
}


void FileCRCDialog(HWND parent, CSalamanderForOperationsAbstract* salamander)
{
  CALL_STACK_MESSAGE1("FileCRCDialog( , )");
  UINT32 crc;
  if (!CalculateFileCRC(crc, parent, salamander)) return;
  DialogBoxParam(HLanguage, MAKEINTRESOURCE(IDD_FILECRC), parent, FileCRCDlgProc, crc);
}*/
