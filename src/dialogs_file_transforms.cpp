// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"

#include "ui/IPrompter.h"
#include "common/IFileSystem.h"
#include "common/SalPathWide.h"
#include "common/unicode/helpers.h"
#include "common/unicode/ComboSyncPolicy.h"
#include "common/IEnvironment.h"
#include "mainwnd.h"
#include "plugins.h"
#include "fileswnd.h"
#include "usermenu.h"
#include "execute.h"
#include "cfgdlg.h"
#include "dialogs.h"
#include "zip.h"
#include "gui.h"
#include "codetbl.h"
#include "worker.h"
#include "menu.h"
#include "darkmode.h"

#include <vector>

//
// ****************************************************************************
// CChangeCaseDlg
//

CChangeCaseDlg::CChangeCaseDlg(HWND parent, BOOL selectionContainsDirectory)
    : CCommonDialog(HLanguage, IDD_CHANGECASE, IDD_CHANGECASE, parent, ooStandard)
{
    SelectionContainsDirectory = selectionContainsDirectory;
    FileNameFormat = 2;
    Change = 0;
    SubDirs = FALSE;
}

void CChangeCaseDlg::Transfer(CTransferInfo& ti)
{
    ti.RadioButton(IDC_CAPITALIZE, 1, FileNameFormat);
    ti.RadioButton(IDC_LOWERCASE, 2, FileNameFormat);
    ti.RadioButton(IDC_UPPERCASE, 3, FileNameFormat);
    ti.RadioButton(IDC_PARTMIXEDCASE, 7, FileNameFormat);

    ti.RadioButton(IDC_WHOLENAME, 0, Change);
    ti.RadioButton(IDC_ONLYNAME, 1, Change);
    ti.RadioButton(IDC_ONLYEXT, 2, Change);

    ti.CheckBox(IDC_RECURSESUBDIRS, SubDirs);
    if (ti.Type == ttDataToWindow)
        EnableWindow(GetDlgItem(HWindow, IDC_RECURSESUBDIRS), SelectionContainsDirectory);
}

//
// ****************************************************************************
// CConvertFilesDlg
//

CConvertFilesDlg::CConvertFilesDlg(HWND parent, BOOL selectionContainsDirectory)
    : CCommonDialog(HLanguage, IDD_CHANGECODING, IDD_CHANGECODING, parent, ooStandard)
{
    CodeTables.Init(HWindow);

    SelectionContainsDirectory = selectionContainsDirectory;
    Mask = L"*.*";
    Change = 0;
    SubDirs = FALSE;
    CodeType = 0;
    EOFType = 0;
}

void CConvertFilesDlg::Validate(CTransferInfo& ti)
{
    CALL_STACK_MESSAGE1("CConvertFilesDlg::Validate()");
    HWND hWnd;
    if (ti.GetControl(hWnd, IDE_FILEMASK))
    {
        if (ti.Type == ttDataFromWindow)
        {
            const std::wstring text = GetWindowTextStringW(hWnd);
            CMaskGroup mask(text.c_str());
            int errorPos;
            if (!mask.PrepareMasks(errorPos))
            {
                gPrompter->ShowError(LoadStrW(IDS_ERRORTITLE), LoadStrW(IDS_INCORRECTSYNTAX));
                SetFocus(hWnd);
                SendMessage(hWnd, CB_SETEDITSEL, 0, MAKELPARAM(errorPos, errorPos + 1));
                ti.ErrorOn(IDE_FILEMASK);
            }
        }
    }

    if (ti.IsGood())
    {
        int noneEOF = IsDlgButtonChecked(HWindow, IDC_CHC_EOFNONE) == BST_CHECKED;
        if (noneEOF && CodeType == 0)
        {
            gPrompter->ShowError(LoadStrW(IDS_ERRORTITLE), LoadStrW(IDS_SPECIFY_CONVERT_ACTION));
            ti.ErrorOn(IDC_CHC_CHANGECODING);
        }
    }
}

void CConvertFilesDlg::Transfer(CTransferInfo& ti)
{
    wchar_t** history = Configuration.ConvertHistory;
    HWND hWnd;
    if (ti.GetControl(hWnd, IDE_FILEMASK))
    {
        if (ti.Type == ttDataToWindow)
        {
            LoadComboFromStdHistoryValues(hWnd, history, CONVERT_HISTORY_SIZE);
            SendMessageW(hWnd, WM_SETTEXT, 0, (LPARAM)Mask.c_str());
        }
        else
        {
            Mask = GetWindowTextStringW(hWnd);
            AddValueToStdHistoryValues(history, CONVERT_HISTORY_SIZE, Mask.c_str(), FALSE);
        }
    }

    ti.RadioButton(IDC_CHC_EOFNONE, 0, EOFType);
    ti.RadioButton(IDC_CHC_EOFCRLF, 1, EOFType);
    ti.RadioButton(IDC_CHC_EOFLF, 2, EOFType);
    ti.RadioButton(IDC_CHC_EOFCR, 3, EOFType);

    ti.CheckBox(IDC_RECURSESUBDIRS, SubDirs);

    if (ti.Type == ttDataToWindow)
        EnableWindow(GetDlgItem(HWindow, IDC_RECURSESUBDIRS), SelectionContainsDirectory);
}

void CConvertFilesDlg::UpdateCodingText()
{
    std::wstring name;
    CodeTables.GetCodeName(CodeType, name);

    // remove &
    RemoveAmpersands(name.data());
    name.resize(wcslen(name.c_str()));

    SetDlgItemTextW(HWindow, IDC_CHC_CODING, name.c_str());
}
/*
int CEOFTypes[4] =
{
  IDS_EOF_NONE,
  IDS_EOF_CRLF,
  IDS_EOF_LF,
  IDS_EOF_CR
};

  void
CConvertFilesDlg::UpdateEOFText()
{
  wchar_t *p = LoadStr(CEOFTypes[EOFType]);
  // remove &
  RemoveAmpersands(p);

  SetDlgItemText(HWindow, IDC_CHC_EOF, p);
}
*/

INT_PTR
CConvertFilesDlg::DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
    case WM_INITDIALOG:
    {
        new CButton(HWindow, IDC_CHC_CHANGECODING, BTF_RIGHTARROW);
        UpdateCodingText();
        //      UpdateEOFText();

        CHyperLink* hl = new CHyperLink(HWindow, IDC_FILEMASK_HINT, STF_DOTUNDERLINE);
        if (hl != NULL)
            hl->SetActionShowHint(LoadStrW(IDS_MASKS_HINT));

        break;
    }

    case WM_COMMAND:
    {
        if (LOWORD(wParam) == IDC_CHC_CHANGECODING)
        {
            RECT r;
            GetWindowRect(GetDlgItem(HWindow, IDC_CHC_CHANGECODING), &r);
            HMENU hMenu = CreatePopupMenu();
            CodeTables.InitMenu(hMenu, CodeType);
            TPMPARAMS tpmPar;
            tpmPar.cbSize = sizeof(tpmPar);
            tpmPar.rcExclude = r;
            DWORD cmd = TrackPopupMenuEx(hMenu, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_RIGHTBUTTON, r.right, r.top, HWindow, &tpmPar);
            if (cmd != 0)
            {
                CodeType = cmd - CM_CODING_MIN;
                UpdateCodingText();
            }
            DestroyMenu(hMenu);
            return 0;
        }
        /*
      if (LOWORD(wParam) == IDC_CHC_CHANGEEOF)
      {
        RECT r;
        GetWindowRect(GetDlgItem(HWindow, IDC_CHC_CHANGEEOF), &r);
        POINT p;
        p.x = r.right;
        p.y = r.top;
        HMENU hMenu = CreatePopupMenu();

        MENUITEMINFO mi;
        memset(&mi, 0, sizeof(mi));
        mi.cbSize = sizeof(mi);

        int i;
        for (i = 0; i < 4; i++)
        {
          wchar_t *p = LoadStr(CEOFTypes[i]);
          mi.fMask = MIIM_TYPE | MIIM_ID | MIIM_STATE;
          mi.fType = MFT_STRING;
          mi.wID = i + 1;                   // +1 because of 'None'
          mi.dwTypeData = p;
          mi.cch = strlen(p);
          mi.fState = (i == EOFType ? MFS_CHECKED : MFS_UNCHECKED);
          InsertMenuItem(hMenu, i, TRUE, &mi);
          SetMenuItemBitmaps(hMenu, mi.wID, MF_BYCOMMAND, NULL, HMenuCheckDot);
        }

        // insert a separator after none
        mi.fMask = MIIM_TYPE;
        mi.fType = MFT_SEPARATOR;
        InsertMenuItem(hMenu, 1, TRUE, &mi);

        DWORD cmd = TrackPopupMenuEx(hMenu, TPM_RETURNCMD | TPM_LEFTALIGN |
                                     TPM_RIGHTBUTTON, p.x, p.y,
                                     HWindow, NULL);
        if (cmd != 0)
        {
          EOFType = cmd - 1;
          UpdateEOFText();
        }
        DestroyMenu(hMenu);
        return 0;

      }
*/
        break;
    }
    }

    return CCommonDialog::DialogProc(uMsg, wParam, lParam);
}

//
// ****************************************************************************
// CFilterDialog
//

CFilterDialog::CFilterDialog(HWND parent, CMaskGroup* filter, wchar_t** filterHistory,
                             BOOL* use /*, BOOL *inverse*/)
    : CCommonDialog(HLanguage, IDD_CHANGEFILTER, IDD_CHANGEFILTER, parent)
{
    Filter = filter;
    UseFilter = use;
    //  Inverse = inverse;
    FilterHistory = filterHistory;
}

void CFilterDialog::Validate(CTransferInfo& ti)
{
    CALL_STACK_MESSAGE1("CFilterDialog::Validate()");
    BOOL useFilter;
    ti.RadioButton(IDC_DONTUSEFILTER, FALSE, useFilter);
    ti.RadioButton(IDC_USEFILTER, TRUE, useFilter);
    if (useFilter)
    {
        const std::wstring candidate = GetWindowTextStringW(GetDlgItem(HWindow, IDE_FILTER));
        CMaskGroup candidateMasks(candidate.c_str(), Filter->GetExtendedMode());
        int errorPos;
        if (!candidateMasks.PrepareMasks(errorPos))
        {
            gPrompter->ShowError(LoadStrW(IDS_ERRORTITLE), LoadStrW(IDS_INCORRECTSYNTAX));
            SetFocus(GetDlgItem(HWindow, IDE_FILTER));
            SendMessage(GetDlgItem(HWindow, IDE_FILTER), EM_SETSEL, errorPos, errorPos + 1);
            ti.ErrorOn(IDE_FILTER);
        }
    }
}

void CFilterDialog::Transfer(CTransferInfo& ti)
{
    CALL_STACK_MESSAGE1("CFilterDialog::Transfer()");
    ti.RadioButton(IDC_DONTUSEFILTER, FALSE, *UseFilter);
    ti.RadioButton(IDC_USEFILTER, TRUE, *UseFilter);
    //  ti.CheckBox(IDC_INVERSEFILTER, *Inverse);

    if (ti.Type == ttDataToWindow)
        EnableControls();
    /*
  The filter edit is transferred dynamically by CFilterDialog::Transfer.
  int errorPos;
  Filter->PrepareMasks(errorPos);
  */
    wchar_t** history = FilterHistory;
    HWND hWnd;
    if (ti.GetControl(hWnd, IDE_FILTER))
    {
        if (ti.Type == ttDataToWindow)
        {
            LoadComboFromStdHistoryValues(hWnd, history, FILTER_HISTORY_SIZE);
            // wide - the ttDataFromWindow branch below already reads this
            // same control wide; seeding it narrow mangled a non-ASCII mask on open.
            SendMessageW(hWnd, WM_SETTEXT, 0, (LPARAM)Filter->GetMasksString());
        }
        else
        {
            const std::wstring masks = GetWindowTextStringW(hWnd);
            Filter->SetMasksString(masks.c_str());
            AddValueToStdHistoryValues(history, FILTER_HISTORY_SIZE, Filter->GetMasksString(), FALSE);
        }
    }
    int errorPos;
    Filter->PrepareMasks(errorPos);
}

void CFilterDialog::EnableControls()
{
    //  BOOL filter = IsDlgButtonChecked(HWindow, IDC_USEFILTER) == BST_CHECKED;
    //  EnableWindow(GetDlgItem(HWindow, IDC_INVERSEFILTER), filter);
    //  if (!filter)
    //    CheckDlgButton(HWindow, IDC_INVERSEFILTER, BST_UNCHECKED);
}

INT_PTR
CFilterDialog::DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
    case WM_INITDIALOG:
    {
        InstallWordBreakProc(GetDlgItem(HWindow, IDE_FILTER)); // install WordBreakProc into the combobox

        CHyperLink* hl = new CHyperLink(HWindow, IDC_FILEMASK_HINT, STF_DOTUNDERLINE);
        if (hl != NULL)
            hl->SetActionShowHint(LoadStrW(IDS_MASKS_HINT));

        if (*UseFilter)
        { // we want our own focus in the editbox filter
            CCommonDialog::DialogProc(uMsg, wParam, lParam);
            SendMessage(HWindow, WM_NEXTDLGCTL, (WPARAM)GetDlgItem(HWindow, IDE_FILTER), TRUE);
            return FALSE;
        }

        break;
    }

    case WM_COMMAND:
    {
        if (HIWORD(wParam) == CBN_EDITCHANGE || HIWORD(wParam) == CBN_SELCHANGE)
        {
            if (IsDlgButtonChecked(HWindow, IDC_DONTUSEFILTER))
            {
                CheckDlgButton(HWindow, IDC_USEFILTER, BST_CHECKED);
                CheckDlgButton(HWindow, IDC_DONTUSEFILTER, BST_UNCHECKED);
                EnableControls();
            }
        }
        if (HIWORD(wParam) == BN_CLICKED &&
            (LOWORD(wParam) == IDC_DONTUSEFILTER || LOWORD(wParam) == IDC_USEFILTER))
        {
            EnableControls();
            if (LOWORD(wParam) == IDC_USEFILTER)
                SendMessage(HWindow, WM_NEXTDLGCTL, (WPARAM)GetDlgItem(HWindow, IDE_FILTER), TRUE);
        }
        break;
    }
    }
    return CCommonDialog::DialogProc(uMsg, wParam, lParam);
}

//
// ****************************************************************************
// CCopyMoveDialog
//

// The dialog and its native edit/combo controls remain Unicode in every path. Path is the
// caller's sole dynamic UTF-16 input/output value; no mode switch or mirror exists.
CCopyMoveDialog::CCopyMoveDialog(HWND parent, std::wstring& path, const wchar_t* title,
                                 CTruncatedString* subject, DWORD helpID,
                                 wchar_t* history[], int historyCount, BOOL directoryHelper)
    : CCommonDialog(HLanguage, history ? IDD_COPYMOVEDIALOG_CB : IDD_COPYMOVEDIALOG, parent),
      Path(path)
{
    DirectoryHelper = FALSE;
    if (directoryHelper)
    {
        if (history != NULL)
        {
            ResID = IDD_COPYMOVEDIALOG_CB_BT;
            DirectoryHelper = TRUE;
        }
        else
            TRACE_E("CCopyMoveDialog without history and with directoryHelper is not supported.");
    }
    Title = title;
    Subject = subject;
    History = history;
    HistoryCount = historyCount;
    UnicodeFont = NULL; // created only if the dialog font cannot render the name
    SetHelpID(helpID); // the dialog serves multiple purposes - set the proper helpID
    SelectionEnd = -1; // -1 = select all
}

void CCopyMoveDialog::SetSelectionEnd(int selectionEnd)
{
    SelectionEnd = selectionEnd;
}

void CCopyMoveDialog::Transfer(CTransferInfo& ti)
{
    CALL_STACK_MESSAGE1("CCopyMoveDialog::Transfer()");
    if (History != NULL)
    {
        HWND hWnd;
        if (ti.GetControl(hWnd, IDE_PATH))
        {
            if (ti.Type == ttDataToWindow)
            {
                LoadComboFromStdHistoryValues(hWnd, History, HistoryCount);
                SendMessageW(hWnd, WM_SETTEXT, 0, (LPARAM)Path.c_str());
            }
            else
            {
                Path = GetWindowTextStringW(hWnd);
                AddValueToStdHistoryValues(History, HistoryCount, Path.c_str(), FALSE);
            }
        }
    }
    else
    {
        HWND hEdit;
        if (ti.GetControl(hEdit, IDE_PATH))
        {
            if (ti.Type == ttDataToWindow)
                SetWindowTextW(hEdit, Path.c_str());
            else
                Path = GetWindowTextStringW(hEdit);
        }
    }
}

INT_PTR
CCopyMoveDialog::DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
    case WM_INITDIALOG:
    {
        InstallWordBreakProc(GetDlgItem(HWindow, IDE_PATH)); // install WordBreakProc into the combobox

        CreateKeyForwarder(HWindow, IDE_PATH); // so that we receive WM_USER_KEYDOWN
        if (DirectoryHelper)
        {
            ChangeToIconButton(HWindow, IDB_BROWSE, IDI_DIRECTORY);   // the button will have a folder icon and an arrow to the right
            VerticalAlignChildToChild(HWindow, IDB_BROWSE, IDE_PATH); // place the button precisely after the editline
        }

        SetWindowTextW(HWindow, Title);
        HWND hSubject = GetDlgItem(HWindow, IDS_SUBJECT);
        if (Subject->TruncateText(hSubject))
        {
            SetWindowTextW(hSubject, Subject->GetW());
        }

        INT_PTR ret = CCommonDialog::DialogProc(uMsg, wParam, lParam);

        HWND hCombo = GetDlgItem(HWindow, IDE_PATH);

        if (hCombo != NULL)
        {
            // Owned; released on WM_DESTROY. NULL when the dialog font already copes.
            UnicodeFont = EnsureComboFontCanRenderW(hCombo, Path.c_str());
        }

        PostMessage(hCombo, CB_SETEDITSEL, 0, MAKELPARAM(0, SelectionEnd));

        return FALSE;
    }

    case WM_USER_KEYDOWN:
    {
        BOOL processed = FALSE;
        if (DirectoryHelper)
        {
            // Always wide, NULL handle: InvokeDirectoryMenuCommandW
            // falls back to GetDlgItem(hDialog, editID) with the W APIs.
            processed = OnDirectoryKeyDownW((DWORD)lParam, HWindow, IDE_PATH, IDB_BROWSE, NULL);
        }
        if (!processed)
            processed = OnKeyDownHandleSelectAll((DWORD)lParam, HWindow, IDE_PATH);
        SetWindowLongPtr(HWindow, DWLP_MSGRESULT, processed);
        return processed;
    }

    case WM_USER_BUTTON:
    {
        OnDirectoryButtonW(HWindow, IDE_PATH, IDB_BROWSE, wParam, lParam, NULL);
        return 0;
    }

    case WM_DESTROY:
    {
        // The controller is gone from this dialog; the only thing left
        // to release is the DEFAULT_CHARSET font clone, when one was needed.
        if (UnicodeFont != NULL)
        {
            DeleteObject(UnicodeFont);
            UnicodeFont = NULL;
        }
        break;
    }

    case WM_COMMAND:
    {
        // The selection-sync block that stood here copied the wide item
        // the user picked in the drop-down into the replacement combo's edit, because
        // that replacement was a separate control from the list. A native combo puts its
        // own selection into its own edit; there is nothing to synchronise.
        // Fall through to base class for all WM_COMMAND messages (IDOK, IDCANCEL, etc.)
    }
    }

    return CCommonDialog::DialogProc(uMsg, wParam, lParam);
}

//
// ****************************************************************************
// CEditNewFileDialog
//

CEditNewFileDialog::CEditNewFileDialog(HWND parent, std::wstring& path, CTruncatedString* subject,
                                       wchar_t* history[], int historyCount)
    // 'history' is wchar_t* now (dialogs.h), matching the sole real
    // caller (files_window_view_edit.cpp, Configuration.EditNewHistory - already
    // wchar_t*[EDITNEW_HISTORY_SIZE]). The trailing LoadStrW(...) 12th argument was
    // stale drift from before 'title' became wide unconditionally; base
    // CCopyMoveDialog's constructor takes 11 arguments total.
    : CCopyMoveDialog(parent, path, LoadStrW(IDS_EDITNEWFILE), subject, IDD_EDITNEWDIALOG,
                      history, historyCount, FALSE)
{
    ResID = IDD_COPYMOVEDIALOG_CB_BTSML;
}

INT_PTR
CEditNewFileDialog::DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
    case WM_INITDIALOG:
    {
        ChangeToArrowButton(HWindow, IDB_BROWSE);
        VerticalAlignChildToChild(HWindow, IDB_BROWSE, IDE_PATH); // place the button precisely after the editline
        break;
    }

    case WM_COMMAND:
    {
        if (LOWORD(wParam) == IDB_BROWSE)
        {
            /* used by the export_mnu.py script which generates salmenu.mnu for the Translator
   keep synchronized with the InsertMenu() call below...
MENU_TEMPLATE_ITEM EditNewFileDialogMenu[] = 
{
  {MNTT_PB, 0
  {MNTT_IT, IDS_EDITNEWFILE_SAVEASDEFAULT
  {MNTT_IT, IDS_EDITNEWFILE_REVERTDEFAULT
  {MNTT_PE, 0
};
*/
            HMENU hMenu = CreatePopupMenu();
            InsertMenuW(hMenu, 0xFFFFFFFF, MF_BYCOMMAND | MF_STRING, 1, LoadStrW(IDS_EDITNEWFILE_SAVEASDEFAULT));
            // both IDS_EDITNEWFILE_REVERTDEFAULT and IDS_EDITNEWFILE_DEFAULTNAME
            // are translator-owned strings; narrowing them here mangled item 2 while item 1,
            // built wide two lines above in the same menu, rendered correctly.
            std::wstring buffW = FormatStrW(LoadStrW(IDS_EDITNEWFILE_REVERTDEFAULT), LoadStrW(IDS_EDITNEWFILE_DEFAULTNAME));
            InsertMenuW(hMenu, 0xFFFFFFFF, MF_BYCOMMAND | MF_STRING, 2, buffW.c_str());

            TPMPARAMS tpmPar;
            tpmPar.cbSize = sizeof(tpmPar);
            GetWindowRect((HWND)lParam, &tpmPar.rcExclude);
            DWORD cmd = TrackPopupMenuEx(hMenu, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_RIGHTBUTTON, tpmPar.rcExclude.right, tpmPar.rcExclude.top,
                                         HWindow, &tpmPar);
            if (cmd == 1)
            {
                Configuration.UseEditNewFileDefault = TRUE;
                Configuration.EditNewFileDefault = GetWindowTextStringW(GetDlgItem(HWindow, IDE_PATH));
            }
            if (cmd == 2)
            {
                Configuration.UseEditNewFileDefault = FALSE;
                Configuration.EditNewFileDefault.clear();
            }
            return 0;
        }
        break;
    }
    }
    return CCopyMoveDialog::DialogProc(uMsg, wParam, lParam);
}

//
// ****************************************************************************
// CCopyMoveMoreDialog
//

CCopyMoveMoreDialog::CCopyMoveMoreDialog(HWND parent, std::wstring& path, const wchar_t* title,
                                         CTruncatedString* subject, DWORD helpID,
                                         wchar_t* history[], int historyCount, CCriteriaData* criteriaInOut,
                                         BOOL havePermissions, BOOL supportsADS)
    : CCommonDialog(HLanguage, IDD_COPYMOVEMOREDIALOG, helpID, parent),
      Path(path)
{
    if (history == NULL)
        TRACE_E("CCopyMoveMoreDialog without history is not supported.");

    Title = title;
    Subject = subject;
    History = history;
    HistoryCount = historyCount;
    CriteriaInOut = criteriaInOut;
    Criteria = new CCriteriaData();
    *Criteria = *CriteriaInOut;
    Expanded = TRUE;
    HavePermissions = havePermissions;
    SupportsADS = supportsADS;
    MoreButton = NULL;
    UnicodeFont = NULL; // created only if the dialog font cannot render the name
}

CCopyMoveMoreDialog::~CCopyMoveMoreDialog()
{
    if (Criteria != NULL)
    {
        delete Criteria;
        Criteria = NULL;
    }
}

void CCopyMoveMoreDialog::Transfer(CTransferInfo& ti)
{
    CALL_STACK_MESSAGE1("CCopyMoveMoreDialog::Transfer()");
    if (History != NULL)
    {
        HWND hWnd;
        if (ti.GetControl(hWnd, IDE_PATH))
        {
            if (ti.Type == ttDataToWindow)
            {
                LoadComboFromStdHistoryValues(hWnd, History, HistoryCount);
                SendMessageW(hWnd, WM_SETTEXT, 0, (LPARAM)Path.c_str());
            }
            else
            {
                // One read path, from the dialog's own combo - see
                // CCopyMoveDialog above. The controller branch is gone.
                Path = GetWindowTextStringW(hWnd);
                AddValueToStdHistoryValues(History, HistoryCount, Path.c_str(), FALSE);
            }
        }
    }
    else
    {
        HWND hEditCtl;
        if (ti.GetControl(hEditCtl, IDE_PATH))
        {
            if (ti.Type == ttDataToWindow)
                SetWindowTextW(hEditCtl, Path.c_str());
            else
                Path = GetWindowTextStringW(hEditCtl);
        }
    }
    TransferCriteriaControls(ti);
}

BOOL GetSpeedLimit(int sel, wchar_t* speedLimitText, DWORD* returnSpeedLimit)
{
    if (sel >= 0 && sel <= 3)
    {
        __int64 speedLimit = 0;
        wchar_t* s = speedLimitText;
        while (*s != 0 && *s <= ' ')
            s++;
        while (*s >= '0' && *s <= '9')
        {
            speedLimit = 10 * speedLimit + (*s - '0');
            if (speedLimit - 1 > 0xFFFFFFFF)
                break;
            s++;
        }
        while (*s != 0 && *s <= ' ')
            s++;
        if (*s == 0)
        {
            switch (sel)
            {
            case 1:
                speedLimit *= 1024;
                break;
            case 2:
                speedLimit *= 1024 * 1024;
                break;
            case 3:
                speedLimit *= 1024 * 1024 * 1024;
                break;
            }
            if (speedLimit - 1 == 0xFFFFFFFF)
                speedLimit--; // treat 4GB as 0xFFFFFFFF, otherwise we cannot store the number
            if (speedLimit > 0 && speedLimit <= 0xFFFFFFFF)
            {
                if (returnSpeedLimit != NULL)
                    *returnSpeedLimit = (DWORD)speedLimit;
                return TRUE;
            }
        }
    }
    return FALSE;
}

void CCopyMoveMoreDialog::TransferCriteriaControls(CTransferInfo& ti)
{
    ti.CheckBox(IDC_CM_NEWER, Criteria->OverwriteOlder);
    ti.CheckBox(IDC_CM_STARTONIDLE, Criteria->StartOnIdle);
    ti.CheckBox(IDC_CM_SECURITY, Criteria->CopySecurity);
    ti.CheckBox(IDC_CM_COPYATTRS, Criteria->CopyAttrs);
    ti.CheckBox(IDC_CM_DIRTIME, Criteria->PreserveDirTime);
    ti.CheckBox(IDC_CM_IGNADS, Criteria->IgnoreADS);
    ti.CheckBox(IDC_CM_EMPTY, Criteria->SkipEmptyDirs);
    ti.CheckBox(IDC_CM_NAMED, Criteria->UseMasks);
    ti.CheckBox(IDC_CM_SPEEDLIMIT, Criteria->UseSpeedLimit);
    if (ti.Type == ttDataToWindow)
        SetDlgItemTextW(HWindow, IDC_CM_NAMED_MASK, Criteria->Masks.GetMasksString());
    else
    {
        const std::wstring masks = GetWindowTextStringW(GetDlgItem(HWindow, IDC_CM_NAMED_MASK));
        Criteria->Masks.SetMasksString(masks.c_str());
    }
    if (ti.Type == ttDataFromWindow)
    {
        int errpos = 0;
        // masks must go out in the Prepared state
        if (!Criteria->Masks.PrepareMasks(errpos)) // invalid mask, this shouldn't happen thanks to validation
            Criteria->UseMasks = FALSE;
        Criteria->Advanced.GetAdvancedDescription(Criteria->UseAdvanced);
        // Advanced must also be prepared
        Criteria->Advanced.PrepareForTest();

        if (Criteria->UseSpeedLimit)
        {
            int sel = (int)SendDlgItemMessage(HWindow, IDC_CM_SPEEDLIMITUNITS, CB_GETCURSEL, 0, 0);
            wchar_t speedLimitText[20];
            GetDlgItemTextW(HWindow, IDE_CM_SPEEDLIMIT, speedLimitText, 20);
            if (GetSpeedLimit(sel, speedLimitText, &Criteria->SpeedLimit))
                Configuration.LastUsedSpeedLimit = Criteria->SpeedLimit;
            else
                Criteria->UseSpeedLimit = FALSE;
        }
    }
    if (ti.Type == ttDataToWindow)
    {
        DWORD speedLimNum = Configuration.LastUsedSpeedLimit;
        if (Criteria->UseSpeedLimit)
            speedLimNum = Criteria->SpeedLimit;
        int speedLimUnits = 0;
        if (speedLimNum == 0xFFFFFFFF)
        {
            speedLimNum = 4;
            speedLimUnits = 3;
        }
        else
        {
            while (speedLimNum % 1024 == 0)
            {
                speedLimNum /= 1024;
                speedLimUnits++;
                if (speedLimNum == 0 || speedLimUnits > 3) // cannot happen, just for peace of mind
                {
                    TRACE_E("CCopyMoveMoreDialog::TransferCriteriaControls(): unexpected situation!");
                    speedLimNum = 4;
                    speedLimUnits = 3;
                    break;
                }
            }
        }

        HWND speedLimitUnits = GetDlgItem(HWindow, IDC_CM_SPEEDLIMITUNITS);
        SendMessageW(speedLimitUnits, CB_RESETCONTENT, 0, 0);
        SendMessageW(speedLimitUnits, CB_ADDSTRING, 0, (LPARAM)LoadStrOwned(IDS_SPEED_B_per_s).c_str());
        SendMessageW(speedLimitUnits, CB_ADDSTRING, 0, (LPARAM)LoadStrOwned(IDS_SPEED_KB_per_s).c_str());
        SendMessageW(speedLimitUnits, CB_ADDSTRING, 0, (LPARAM)LoadStrOwned(IDS_SPEED_MB_per_s).c_str());
        SendMessageW(speedLimitUnits, CB_ADDSTRING, 0, (LPARAM)LoadStrOwned(IDS_SPEED_GB_per_s).c_str());
        SendMessageW(speedLimitUnits, CB_SETCURSEL, speedLimUnits, 0);

        HWND speedLimit = GetDlgItem(HWindow, IDE_CM_SPEEDLIMIT);
        wchar_t num[20];
        swprintf_s(num, _countof(num), L"%u", speedLimNum);
        SetWindowTextW(speedLimit, num);
        SendMessage(speedLimit, EM_LIMITTEXT, 19, 0);

        UpdateAdvancedText();
        EnableControls();
    }
}

void CCopyMoveMoreDialog::UpdateAdvancedText()
{
    BOOL dirty;
    const std::wstring description = Criteria->Advanced.GetAdvancedDescription(dirty);
    SetDlgItemTextW(HWindow, IDC_CM_ADVANCED_INFO, description.c_str());
    EnableWindow(GetDlgItem(HWindow, IDC_CM_ADVANCED_INFO), dirty);
}

void CCopyMoveMoreDialog::Validate(CTransferInfo& ti)
{
    CALL_STACK_MESSAGE1("CCopyMoveMoreDialog::Validate()");

    BOOL useSpeedLimit;
    ti.CheckBox(IDC_CM_SPEEDLIMIT, useSpeedLimit);
    if (useSpeedLimit)
    {
        int sel = (int)SendDlgItemMessage(HWindow, IDC_CM_SPEEDLIMITUNITS, CB_GETCURSEL, 0, 0);
        wchar_t speedLimitText[20];
        GetDlgItemTextW(HWindow, IDE_CM_SPEEDLIMIT, speedLimitText, 20);
        if (!GetSpeedLimit(sel, speedLimitText, NULL))
        {
            gPrompter->ShowError(LoadStrW(IDS_ERRORTITLE), LoadStrW(IDS_SPEEDLIMITSIZE));
            ti.ErrorOn(IDE_CM_SPEEDLIMIT);
            return;
        }
    }

    BOOL useMasks;
    ti.CheckBox(IDC_CM_NAMED, useMasks);
    if (useMasks)
    {
        const std::wstring buf = GetWindowTextStringW(GetDlgItem(HWindow, IDC_CM_NAMED_MASK));
        CMaskGroup masks;
        masks.SetMasksString(buf.c_str());
        int errorPos;
        if (!masks.PrepareMasks(errorPos))
        {
            gPrompter->ShowError(LoadStrW(IDS_ERRORTITLE), LoadStrW(IDS_INCORRECTSYNTAX));
            SetFocus(GetDlgItem(HWindow, IDC_CM_NAMED_MASK));
            SendMessage(GetDlgItem(HWindow, IDC_CM_NAMED_MASK), EM_SETSEL, errorPos, errorPos + 1);
            ti.ErrorOn(IDC_CM_NAMED_MASK);
        }
    }
}

HDWP CCopyMoveMoreDialog::OffsetControl(HDWP hdwp, int id, int yOffset)
{
    HWND hCtrl = GetDlgItem(HWindow, id);
    RECT r;
    GetWindowRect(hCtrl, &r);
    ScreenToClient(HWindow, (LPPOINT)&r);

    hdwp = HANDLES(DeferWindowPos(hdwp, hCtrl, NULL, r.left, r.top + yOffset, 0, 0, SWP_NOSIZE | SWP_NOZORDER));
    return hdwp;
}

void CCopyMoveMoreDialog::SetOptionsButtonState(BOOL more)
{
    CheckDlgButton(HWindow, IDC_MORE, more ? BST_CHECKED : BST_UNCHECKED);

    // if the button is pressed (options expanded), it's not MORE but DROPDOWN (and vice versa)
    DWORD btnFlags = MoreButton->GetFlags();
    if (more)
    {
        btnFlags &= ~BTF_MORE;
        btnFlags |= BTF_DROPDOWN;
    }
    else
    {
        btnFlags |= BTF_MORE;
        btnFlags &= ~BTF_DROPDOWN;
    }
    MoreButton->SetFlags(btnFlags, TRUE);
}

void CCopyMoveMoreDialog::DisplayMore(BOOL more, BOOL fast)
{
    // hide the concealed controls so they are removed from the tab order
    int controls[] = {IDC_CM_NEWER, IDC_CM_STARTONIDLE, IDC_CM_SPEEDLIMIT, IDE_CM_SPEEDLIMIT,
                      IDC_CM_SPEEDLIMITUNITS, IDC_CM_SECURITY, IDC_CM_COPYATTRS,
                      IDC_CM_DIRTIME, IDC_CM_IGNADS, IDC_CM_EMPTY, IDC_CM_NAMED_MASK, IDC_CM_NAMED,
                      IDC_FILEMASK_HINT, IDC_CM_ADVANCED, IDC_CM_ADVANCED_INFO,
                      IDC_CM_SEPARATOR, -1};

    int wndHeight = OriginalHeight;
    if (!more)
        wndHeight -= SpacerHeight;
    SetWindowPos(HWindow, NULL, 0, 0, OriginalWidth, wndHeight,
                 SWP_NOZORDER | SWP_NOMOVE);

    HWND hFocus = GetFocus();
    int i;
    for (i = 0; controls[i] != -1; i++)
    {
        HWND hCtrl = GetDlgItem(HWindow, controls[i]);
        if (!more && hCtrl == hFocus)
        {
            SendMessage(HWindow, DM_SETDEFID, IDOK, 0);
            SetFocus(GetDlgItem(HWindow, IDE_PATH));
        }
        ShowWindow(hCtrl, more ? SW_SHOW : SW_HIDE);
    }

    int yOffset = more ? SpacerHeight : -SpacerHeight;

    HDWP hdwp = HANDLES(BeginDeferWindowPos(4));
    if (hdwp != NULL)
    {
        hdwp = OffsetControl(hdwp, IDOK, yOffset);
        hdwp = OffsetControl(hdwp, IDCANCEL, yOffset);
        hdwp = OffsetControl(hdwp, IDC_MORE, yOffset);
        hdwp = OffsetControl(hdwp, IDHELP, yOffset);
        HANDLES(EndDeferWindowPos(hdwp));
    }

    SetOptionsButtonState(more);

    if (!more && !fast) // fast is TRUE when the controls hold default values and don't need resetting
    {
        Criteria->Reset();
        CTransferInfo ti(HWindow, ttDataToWindow);
        TransferCriteriaControls(ti);
    }
    Expanded = more;
}

void CCopyMoveMoreDialog::EnableControls()
{
    BOOL named = IsDlgButtonChecked(HWindow, IDC_CM_NAMED);
    EnableWindow(GetDlgItem(HWindow, IDC_CM_NAMED_MASK), named);
    BOOL speedLimit = IsDlgButtonChecked(HWindow, IDC_CM_SPEEDLIMIT);
    EnableWindow(GetDlgItem(HWindow, IDE_CM_SPEEDLIMIT), speedLimit);
    EnableWindow(GetDlgItem(HWindow, IDC_CM_SPEEDLIMITUNITS), speedLimit);
}

/*
BOOL
CCopyMoveMoreDialog::ManageHiddenShortcuts(const MSG *msg)
{
  if (msg->message == WM_SYSKEYDOWN)
  {
    BOOL controlPressed = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    BOOL altPressed = (GetKeyState(VK_MENU) & 0x8000) != 0;
    BOOL shiftPressed = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
    if (!controlPressed && altPressed && !shiftPressed)
    {
      // if Alt+? is pressed and the Options section is collapsed, it's worth checking further
      if (!IsDlgButtonChecked(HWindow, IDC_FIND_GREP))
      {
        // try the hotkeys of the monitored controls
        int resID[] = {IDC_FIND_CONTAINING_TEXT, IDC_FIND_HEX, IDC_FIND_CASE,
                       IDC_FIND_WHOLE, IDC_FIND_REGULAR, -1}; // (terminate with -1)
                       int i;
        for (i = 0; resID[i] != -1; i++)
        {
          wchar_t key = GetControlHotKey(HWindow, resID[i]);
          if (key != 0 && (WPARAM)key == msg->wParam)
          {
            // expand the options section
            CheckDlgButton(HWindow, IDC_FIND_GREP, BST_CHECKED);
            SendMessage(HWindow, WM_COMMAND, MAKEWPARAM(IDC_FIND_GREP, BN_CLICKED), 0);
            return FALSE; // expanded, the rest is handled by IsDialogMessage after we return
          }
        }
      }
    }
  }
  return FALSE; // not our message
}
*/

INT_PTR
CCopyMoveMoreDialog::DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
    case WM_INITDIALOG:
    {
        InstallWordBreakProc(GetDlgItem(HWindow, IDE_PATH)); // install WordBreakProc into the combobox

        // since 2.53 we can save options, so IDC_CM_STARTONIDLE must always be enabled so the user can preset it
        // EnableWindow(GetDlgItem(HWindow, IDC_CM_STARTONIDLE), !OperationsQueue.IsEmpty());
        EnableWindow(GetDlgItem(HWindow, IDC_CM_SECURITY), HavePermissions);
        EnableWindow(GetDlgItem(HWindow, IDC_CM_IGNADS), SupportsADS);

        MoreButton = new CButton(HWindow, IDC_MORE, BTF_MORE | BTF_CHECKBOX);
        SetOptionsButtonState(TRUE);

        CreateKeyForwarder(HWindow, IDE_PATH);                  // so that we receive WM_USER_KEYDOWN
        ChangeToIconButton(HWindow, IDB_BROWSE, IDI_DIRECTORY); // the button will have a folder icon and an arrow to the right

        CHyperLink* hl = new CHyperLink(HWindow, IDC_FILEMASK_HINT, STF_DOTUNDERLINE);
        if (hl != NULL)
            hl->SetActionShowHint(LoadStrW(IDS_MASKS_HINT));

        SetWindowTextW(HWindow, Title);
        HWND hSubject = GetDlgItem(HWindow, IDS_SUBJECT);
        if (Subject->TruncateText(hSubject))
        {
            // BUG FIX, found by collapsing the CTruncatedString mirror rather
            // than by a report. This used to branch on Subject->IsWide(): the wide arm set the
            // text directly, the narrow arm escaped ampersands first. IsWide() has answered
            // TRUE for every Subject since the mirror's only setter started forcing it, so the
            // escaping arm has been DEAD - and it is the one that was right here.
            //
            // IDD_COPYMOVEMOREDIALOG is the ONLY subject static in lang.rc declared WITHOUT
            // SS_NOPREFIX (its four IDD_COPYMOVEDIALOG siblings, IDD_PACK and IDD_UNPACK all
            // have it). So in THIS dialog a literal '&' in a file name is an accelerator
            // prefix: "R&D report.txt" rendered as "RD report.txt" with a underlined D.
            //
            // The two sibling sites a few hundred lines away in this file collapsed to the
            // plain call, correctly - their controls carry SS_NOPREFIX and their two arms were
            // genuinely identical. This one is not a symmetric pair and must not be flattened
            // the same way.
            std::wstring buff = Subject->Get();
            bool firstAmpersand = true;
            for (size_t i = 0; i < buff.length(); ++i)
            {
                if (buff[i] != L'&')
                    continue;
                if (firstAmpersand)
                    firstAmpersand = false;
                else
                {
                    buff.insert(i, 1, L'&');
                    ++i;
                }
            }
            SetWindowTextW(hSubject, buff.c_str());
        }

        // now we are at full size => measure the dialog
        RECT r;
        GetWindowRect(HWindow, &r);
        OriginalWidth = r.right - r.left;
        OriginalHeight = r.bottom - r.top;
        // original button positions
        GetWindowRect(GetDlgItem(HWindow, IDOK), &r);
        ScreenToClient(HWindow, (LPPOINT)&r);
        OriginalButtonsY = r.top;
        // separator height
        GetWindowRect(GetDlgItem(HWindow, IDC_CM_SPACER), &r);
        SpacerHeight = r.bottom - r.top;

        if (!Criteria->IsDirty()) // collapse the dialog if Criteria do not contain any data
            DisplayMore(FALSE, TRUE);

        INT_PTR ret = CCommonDialog::DialogProc(uMsg, wParam, lParam);

        HWND hCombo = GetDlgItem(HWindow, IDE_PATH);
        if (hCombo != NULL)
        {
            // Owned; released on WM_DESTROY. NULL when the dialog font already copes.
            UnicodeFont = EnsureComboFontCanRenderW(hCombo, Path.c_str());
        }
        return ret;
    }

    case WM_USER_KEYDOWN:
    {
        // Always wide, NULL handle - the helper falls back to the
        // dialog's own control with the W APIs.
        BOOL processed = OnDirectoryKeyDownW((DWORD)lParam, HWindow, IDE_PATH, IDB_BROWSE, NULL);
        if (!processed)
            processed = OnKeyDownHandleSelectAll((DWORD)lParam, HWindow, IDE_PATH);
        SetWindowLongPtr(HWindow, DWLP_MSGRESULT, processed);
        return processed;
    }

    case WM_USER_BUTTON:
    {
        // Always wide, NULL handle.
        OnDirectoryButtonW(HWindow, IDE_PATH, IDB_BROWSE, wParam, lParam, NULL);
        return 0;
    }

    case WM_DESTROY:
    {
        // Only the owned font clone is left to release.
        if (UnicodeFont != NULL)
        {
            DeleteObject(UnicodeFont);
            UnicodeFont = NULL;
        }
        break;
    }

    case WM_USER_BUTTONDROPDOWN:
    {
        if (LOWORD(wParam) == IDC_MORE)
        {
            HWND hCtrl = GetDlgItem(HWindow, (int)wParam);
            RECT r;
            GetWindowRect(hCtrl, &r);

            CMenuPopup* popup = new CMenuPopup;
            if (popup != NULL)
            {
                /* used by the export_mnu.py script which generates salmenu.mnu for the Translator
   keep synchronized with the InsertItem() call below...
MENU_TEMPLATE_ITEM CopyMoveMoreDialogMenu[] = 
{
  {MNTT_PB, 0
  {MNTT_IT, IDS_COPYMOVE_RESETHIDE
  {MNTT_IT, IDS_COPYMOVE_SAVEASDEF
  {MNTT_IT, IDS_COPYMOVE_RESETDEFS
  {MNTT_PE, 0
};
*/
                MENU_ITEM_INFO mii;
                mii.Mask = MENU_MASK_TYPE | MENU_MASK_STRING | MENU_MASK_ID;
                mii.Type = MENU_TYPE_STRING;

                mii.String = LoadStrW(IDS_COPYMOVE_RESETHIDE);
                mii.ID = 1;
                popup->InsertItem(-1, TRUE, &mii);

                mii.String = LoadStrW(IDS_COPYMOVE_SAVEASDEF);
                mii.ID = 2;
                popup->InsertItem(-1, TRUE, &mii);

                mii.String = LoadStrW(IDS_COPYMOVE_RESETDEFS);
                mii.ID = 3;
                popup->InsertItem(-1, TRUE, &mii);

                BOOL selectMenuItem = LOWORD(lParam);
                DWORD flags = MENU_TRACK_RETURNCMD;
                if (selectMenuItem)
                {
                    popup->SetSelectedItemIndex(0);
                    flags |= MENU_TRACK_SELECT;
                }
                switch (popup->Track(flags, r.left, r.bottom, HWindow, &r))
                {
                case 1: // Reset and hide options
                {
                    PostMessage(HWindow, WM_COMMAND, MAKELPARAM(IDC_MORE, BN_CLICKED), 0);
                    break;
                }

                case 2: // Save options as defaults
                {
                    // to save the options, they must pass validation
                    if (ValidateData())
                    {
                        if (TransferData(ttDataFromWindow))
                            CopyMoveOptions.Set(Criteria->IsDirty() ? Criteria : NULL); // save the new default
                    }
                    break;
                }

                case 3: // Reset options and defaults
                {
                    CopyMoveOptions.Set(NULL);

                    // clear the dialog
                    Criteria->Reset();
                    TransferData(ttDataToWindow);
                    break;
                }
                }
                delete popup;
            }
        }
        break;
    }

    case WM_COMMAND:
    {
        // The selection-sync block that stood here copied the wide item
        // chosen in the drop-down into the replacement combo's edit. A native combo puts
        // its own selection into its own edit; there is nothing to synchronise.

        if (HIWORD(wParam) == BN_CLICKED)
        {
            switch (LOWORD(wParam))
            {
            case IDC_CM_STARTONIDLE:
            case IDC_CM_NEWER:
            case IDC_CM_SPEEDLIMIT:
            case IDC_CM_COPYATTRS:
            case IDC_CM_SECURITY:
            case IDC_CM_DIRTIME:
            case IDC_CM_IGNADS:
            case IDC_CM_EMPTY:
            case IDC_CM_NAMED:
            case IDC_CM_ADVANCED:
            {
                if (!Expanded)
                    DisplayMore(TRUE, FALSE);
                break;
            }
            }

            EnableControls();

            // if the user clicked at the mask enabling checkbox, they probably want to edit it
            if (LOWORD(wParam) == IDC_CM_NAMED)
            {
                if (IsDlgButtonChecked(HWindow, IDC_CM_NAMED))
                    SendMessage(HWindow, WM_NEXTDLGCTL, FALSE, FALSE); // focus to the mask
                else
                    SetDlgItemTextW(HWindow, IDC_CM_NAMED_MASK, L"*.*"); // default value for the mask
            }

            // if the user clicked at the speed-limit checkbox, they probably want to edit it
            if (LOWORD(wParam) == IDC_CM_SPEEDLIMIT)
            {
                if (IsDlgButtonChecked(HWindow, IDC_CM_SPEEDLIMIT))
                    SendMessage(HWindow, WM_NEXTDLGCTL, FALSE, FALSE); // focus to the editbox
            }

            if (LOWORD(wParam) == IDC_MORE)
            {
                DisplayMore(!Expanded, FALSE);
                return 0;
            }

            if (LOWORD(wParam) == IDC_CM_ADVANCED)
            {
                CFilterCriteriaDialog dlg(HWindow, &Criteria->Advanced, FALSE);
                if (dlg.Execute() == IDOK)
                    UpdateAdvancedText();
                return 0;
            }

            if (LOWORD(wParam) == IDOK)
            {
                // custom OK handling -- we need to propagate Criteria out
                if (!ValidateData() ||
                    !TransferData(ttDataFromWindow))
                    return TRUE;
                *CriteriaInOut = *Criteria;
                EndDialog(HWindow, wParam);
                return TRUE;
            }
        }
        break;
    }
    }

    return CCommonDialog::DialogProc(uMsg, wParam, lParam);
}

//
// ****************************************************************************
// CChangeDirDlg
//

CChangeDirDlg::CChangeDirDlg(HWND parent, std::wstring& path, BOOL* sendDirectlyToPlugin)
    : CCommonDialog(HLanguage, IDD_CHANGEDIR, IDD_CHANGEDIR, parent),
      Path(path)
{
    SendDirectlyToPlugin = sendDirectlyToPlugin;
}

void CChangeDirDlg::Transfer(CTransferInfo& ti)
{
    CALL_STACK_MESSAGE1("CChangeDirDlg::Transfer()");
    wchar_t** history = Configuration.ChangeDirHistory;
    HWND hWnd;
    if (ti.GetControl(hWnd, IDE_PATH))
    {
        if (ti.Type == ttDataToWindow)
        {
            LoadComboFromStdHistoryValues(hWnd, history, CHANGEDIR_HISTORY_SIZE);
            SendMessageW(hWnd, WM_SETTEXT, 0, (LPARAM)Path.c_str());
        }
        else
        {
            Path = GetWindowTextStringW(hWnd);
            AddValueToStdHistoryValues(history, CHANGEDIR_HISTORY_SIZE, Path.c_str(), FALSE);
        }
    }
    if (SendDirectlyToPlugin != NULL)
        ti.CheckBox(IDC_SENDDIRECTTOPLG, *SendDirectlyToPlugin);
}

INT_PTR
CChangeDirDlg::DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    CALL_STACK_MESSAGE4("CChangeDirDlg::DialogProc(0x%X, 0x%IX, 0x%IX)", uMsg, wParam, lParam);
    switch (uMsg)
    {
    case WM_INITDIALOG:
    {
        if (SendDirectlyToPlugin == NULL)
            EnableWindow(GetDlgItem(HWindow, IDC_SENDDIRECTTOPLG), FALSE);
        InstallWordBreakProc(GetDlgItem(HWindow, IDE_PATH));    // install WordBreakProc into the combobox
        CreateKeyForwarder(HWindow, IDE_PATH);                  // so that we receive WM_USER_KEYDOWN
        ChangeToIconButton(HWindow, IDB_BROWSE, IDI_DIRECTORY); // the button will have a folder icon and an arrow to the right

        CHyperLink* hl = new CHyperLink(HWindow, IDC_CHANGEDIR_HINT, STF_DOTUNDERLINE);
        if (hl != NULL)
            hl->SetActionShowHint(LoadStrW(IDS_CHANGEDIR_HINT));

        SendDlgItemMessageW(HWindow, IDE_PATH, CB_SETEDITSEL, 0, MAKELPARAM(0, -1));
        break;
    }

    case WM_USER_KEYDOWN:
    {
        BOOL processed = OnDirectoryKeyDownW((DWORD)lParam, HWindow, IDE_PATH, IDB_BROWSE, NULL);
        if (!processed)
            processed = OnKeyDownHandleSelectAll((DWORD)lParam, HWindow, IDE_PATH);
        SetWindowLongPtr(HWindow, DWLP_MSGRESULT, processed);
        return processed;
    }

    case WM_USER_BUTTON:
    {
        OnDirectoryButtonW(HWindow, IDE_PATH, IDB_BROWSE, wParam, lParam, NULL);
        return 0;
    }
    }

    return CCommonDialog::DialogProc(uMsg, wParam, lParam);
}

//
// ****************************************************************************
// CDriveInfo
//

static bool GetNetworkConnectionTextW(const std::wstring& localName, std::wstring& remoteName)
{
    DWORD capacity = 256;
    for (;;)
    {
        remoteName.assign(capacity, L'\0');
        DWORD length = capacity;
        const DWORD result = WNetGetConnectionW(localName.c_str(), remoteName.data(), &length);
        if (result == NO_ERROR)
        {
            remoteName.resize(wcslen(remoteName.c_str()));
            return true;
        }
        if (result != ERROR_MORE_DATA)
            return false;
        capacity = length > capacity ? length : capacity * 2;
    }
}

static bool GetNetworkUserTextW(const std::wstring& localName, std::wstring& userName)
{
    DWORD capacity = 256;
    for (;;)
    {
        userName.assign(capacity, L'\0');
        DWORD length = capacity;
        const DWORD result = WNetGetUserW(localName.c_str(), userName.data(), &length);
        if (result == NO_ERROR)
        {
            userName.resize(wcslen(userName.c_str()));
            return true;
        }
        if (result != ERROR_MORE_DATA)
            return false;
        capacity = length > capacity ? length : capacity * 2;
    }
}

CDriveInfo::CDriveInfo(HWND parent, const wchar_t* path, CObjectOrigin origin)
    : CCommonDialog(HLanguage, IDD_DRIVEINFO, IDD_DRIVEINFO, parent, origin),
      VolumePath(path != NULL ? path : L"")
{
    HDriveIcon = NULL;
}

void CDriveInfo::Validate(CTransferInfo& ti)
{
    CALL_STACK_MESSAGE1("CDriveInfo::Validate()");
    HWND edit;
    if (ti.GetControl(edit, IDE_VOLNAME) && ti.Type == ttDataFromWindow)
    {
        const std::wstring newName = GetWindowTextStringW(edit);

        if (OldVolumeName != newName)
        {
            std::wstring volumePathWithBackslash = VolumePath;
            SalPathAddBackslashW(volumePathWithBackslash);
            BOOL handsOffLeft = SalPathIsPrefix(volumePathWithBackslash.c_str(), MainWindow->LeftPanel->GetPathW());
            BOOL handsOffRight = SalPathIsPrefix(volumePathWithBackslash.c_str(), MainWindow->RightPanel->GetPathW());
            if (handsOffLeft)
                MainWindow->LeftPanel->HandsOff(TRUE);
            if (handsOffRight)
                MainWindow->RightPanel->HandsOff(TRUE);
            //      SAD_SetUACParentWindow(HWindow);
            //      BOOL res = SAD_SetVolumeLabel(volumePathWithBackslash, newName);
            //      DWORD err = SAD_GetLastError();
            const FileResult result = gFileSystem->SetVolumeLabel(volumePathWithBackslash.c_str(), newName.c_str());
            if (handsOffLeft)
                MainWindow->LeftPanel->HandsOff(FALSE);
            if (handsOffRight)
                MainWindow->RightPanel->HandsOff(FALSE);
            if (!result.success)
            {
                std::wstring msg = FormatStrW(LoadStrW(IDS_UNABLETOCHANGEDRIVELABEL), GetErrorTextOwned(result.errorCode).c_str());
                gPrompter->ShowError(LoadStrW(IDS_ERRORTITLE), msg.c_str());
                ti.ErrorOn(IDE_VOLNAME);
            }
        }
    }
}

void CDriveInfo::Transfer(CTransferInfo& ti)
{
    CALL_STACK_MESSAGE1("CDriveInfo::Transfer()");
    if (ti.Type == ttDataToWindow)
    {
        BOOL err;
        //---  GetVolumeInformation
        std::wstring volumeName;
        std::wstring volumePathWithBackslash;
        DWORD volumeSerialNumber;
        DWORD maximumComponentLength;
        DWORD fileSystemFlags;
        std::wstring fileSystemName;
        std::wstring junctionOrSymlinkTgt;
        int linkType;
        // MyGetVolumeInformationW (consts.h:1044) returns its two path outputs
        // as std::wstring* rather than caller buffers, so they are received into locals and
        // copied into dynamically owned UTF-16 values used by every later operation.
        std::wstring rootReparseW, junctionTgtW;
        err = (MyGetVolumeInformationW(VolumePath.c_str(), &rootReparseW, &junctionTgtW, &linkType,
                                       &volumeName, &volumeSerialNumber, &maximumComponentLength,
                                       &fileSystemFlags, &fileSystemName) == 0);
        volumePathWithBackslash = std::move(rootReparseW);
        junctionOrSymlinkTgt = std::move(junctionTgtW);
        VolumePath = volumePathWithBackslash;
        SalPathAddBackslashW(volumePathWithBackslash);
        //---  GetVolumeInformation - display
        if (!err)
        {
            SetWindowTextW(GetDlgItem(HWindow, IDE_VOLNAME), volumeName.c_str());
            OldVolumeName = volumeName;

            std::wstring mountPoint;
            std::wstring guidPath;
            if (GetResolvedPathMountPointAndGUIDW(VolumePath.c_str(), &mountPoint, &guidPath))
            {
                SetWindowTextW(GetDlgItem(HWindow, IDT_MOUNTPOINT), mountPoint.c_str());
                SetWindowTextW(GetDlgItem(HWindow, IDT_GUIDPATH), guidPath.c_str());
            }

            std::wstring titlePath = VolumePath;
            if (!titlePath.empty() && titlePath.back() == L'\\')
                titlePath.pop_back();
            const std::wstring title = L"(" + titlePath + L") " + GetWindowTextStringW(HWindow);
            SetWindowTextW(HWindow, title.c_str());

            const std::wstring serialNumber = FormatStrW(
                L"%04X-%04X", HIWORD(volumeSerialNumber), LOWORD(volumeSerialNumber));
            SetWindowTextW(GetDlgItem(HWindow, IDT_VOLSERNUM), serialNumber.c_str());

            SetWindowTextW(GetDlgItem(HWindow, IDT_LONGNAMES),
                           (maximumComponentLength > 100) ? LoadStrW(IDS_INFODLGYES)
                                                          : LoadStrW(IDS_INFODLGNO));

            std::wstring flagsText;
            const auto appendFlag = [&flagsText](const wchar_t* flag) {
                if (!flagsText.empty())
                    flagsText.append(L", ");
                flagsText.append(flag);
            };
            if (fileSystemFlags & FS_CASE_IS_PRESERVED)
                appendFlag(LoadStrW(IDS_INFODLGFLAG1));
            if (fileSystemFlags & FS_CASE_SENSITIVE)
                appendFlag(LoadStrW(IDS_INFODLGFLAG2));
            if (fileSystemFlags & FS_UNICODE_STORED_ON_DISK)
                appendFlag(LoadStrW(IDS_INFODLGFLAG3));
            if (fileSystemFlags & FS_PERSISTENT_ACLS)
                appendFlag(LoadStrW(IDS_INFODLGFLAG4));
            if (fileSystemFlags & FS_FILE_COMPRESSION)
                appendFlag(LoadStrW(IDS_INFODLGFLAG5));
            if (fileSystemFlags & FS_VOL_IS_COMPRESSED)
                appendFlag(LoadStrW(IDS_INFODLGFLAG6));
            if (fileSystemFlags & FILE_NAMED_STREAMS)
                appendFlag(LoadStrW(IDS_INFODLGFLAG7));
            if (fileSystemFlags & FILE_READ_ONLY_VOLUME)
                appendFlag(LoadStrW(IDS_INFODLGFLAG8));
            if (fileSystemFlags & FILE_SUPPORTS_ENCRYPTION)
                appendFlag(LoadStrW(IDS_INFODLGFLAG9));
            if (fileSystemFlags & FILE_SUPPORTS_OBJECT_IDS)
                appendFlag(LoadStrW(IDS_INFODLGFLAG10));
            if (fileSystemFlags & FILE_SUPPORTS_REPARSE_POINTS)
                appendFlag(LoadStrW(IDS_INFODLGFLAG11));
            if (fileSystemFlags & FILE_SUPPORTS_SPARSE_FILES)
                appendFlag(LoadStrW(IDS_INFODLGFLAG12));
            if (fileSystemFlags & FILE_VOLUME_QUOTAS)
                appendFlag(LoadStrW(IDS_INFODLGFLAG13));
            SetWindowTextW(GetDlgItem(HWindow, IDT_FILESYSTEMFLAGS), flagsText.c_str());

            SetWindowTextW(GetDlgItem(HWindow, IDT_FILESYSTEMNAME), fileSystemName.c_str());
        }
        //---  GetDiskFreeSpace
        DWORD sectorsPerCluster;
        DWORD bytesPerSector;
        DWORD numberOfFreeClusters;
        DWORD totalNumberOfClusters;
        err = (MyGetDiskFreeSpaceW(volumePathWithBackslash.c_str(), &sectorsPerCluster,
                                   &bytesPerSector, &numberOfFreeClusters, &totalNumberOfClusters) == 0);

        CQuadWord diskTotalBytes = CQuadWord(-1, -1), diskFreeBytes;
        ULARGE_INTEGER availBytes, totalBytes, freeBytes;
        if (GetDiskFreeSpaceExW(volumePathWithBackslash.c_str(), &availBytes, &totalBytes, &freeBytes))
        {
            diskTotalBytes.Value = (unsigned __int64)totalBytes.QuadPart;
            diskFreeBytes.Value = (unsigned __int64)availBytes.QuadPart;
        }
        if (diskTotalBytes == CQuadWord(-1, -1) && !err)
        {
            diskTotalBytes = CQuadWord(bytesPerSector, 0) * CQuadWord(sectorsPerCluster, 0) *
                             CQuadWord(totalNumberOfClusters, 0);
            diskFreeBytes = CQuadWord(bytesPerSector, 0) * CQuadWord(sectorsPerCluster, 0) *
                            CQuadWord(numberOfFreeClusters, 0);
        }
        //---  GetDiskFreeSpace - display
        if (!err)
        {
            SetWindowTextW(GetDlgItem(HWindow, IDT_SPC), NumberToStr(CQuadWord(sectorsPerCluster, 0)).c_str());

            SetWindowTextW(GetDlgItem(HWindow, IDT_BPS), NumberToStr(CQuadWord(bytesPerSector, 0)).c_str());

            const std::wstring clusterCount = CQuadWord(bytesPerSector, 0) * CQuadWord(sectorsPerCluster, 0) != CQuadWord(0, 0)
                                                  ? NumberToStr(diskTotalBytes / (CQuadWord(bytesPerSector, 0) * CQuadWord(sectorsPerCluster, 0)))
                                                  : std::wstring();
            SetWindowTextW(GetDlgItem(HWindow, IDT_NOC), clusterCount.c_str());

            const std::wstring bytesPerCluster = CQuadWord(bytesPerSector, 0) * CQuadWord(sectorsPerCluster, 0) != CQuadWord(0, 0)
                                                     ? NumberToStr(CQuadWord(bytesPerSector, 0) * CQuadWord(sectorsPerCluster, 0))
                                                     : std::wstring();
            SetWindowTextW(GetDlgItem(HWindow, IDT_BPC), bytesPerCluster.c_str());
        }
        if (diskTotalBytes != CQuadWord(-1, -1))
        {
            double used = 1 - (diskFreeBytes <= diskTotalBytes && diskTotalBytes > CQuadWord(0, 0) ? diskFreeBytes.GetDouble() / diskTotalBytes.GetDouble() : 1);
            Graph->SetUsed(used);

            int spaceForLongAndShort = 0;
            RECT tmpR1;
            RECT tmpR2;
            GetWindowRect(GetDlgItem(HWindow, IDT_CAPACITY), &tmpR1);
            GetWindowRect(GetDlgItem(HWindow, IDB_GRAPH), &tmpR2);
            spaceForLongAndShort = tmpR2.left - tmpR1.left;

            SetWindowTextW(GetDlgItem(HWindow, IDT_CAPACITY), PrintDiskSize(diskTotalBytes, 2).c_str());
            SetWindowTextW(GetDlgItem(HWindow, IDT_CAPACITY_SHORT), PrintDiskSize(diskTotalBytes, 0).c_str());
            SetWindowTextW(GetDlgItem(HWindow, IDT_FREESPACE), PrintDiskSize(diskFreeBytes, 2).c_str());
            SetWindowTextW(GetDlgItem(HWindow, IDT_FREESPACE_SHORT), PrintDiskSize(diskFreeBytes, 0).c_str());
            if (diskTotalBytes >= diskFreeBytes)
                diskTotalBytes -= diskFreeBytes;
            else
                diskTotalBytes.SetUI64(0); // rather zero than complete nonsense
            SetWindowTextW(GetDlgItem(HWindow, IDT_USEDSPACE), PrintDiskSize(diskTotalBytes, 2).c_str());
            SetWindowTextW(GetDlgItem(HWindow, IDT_USEDSPACE_SHORT), PrintDiskSize(diskTotalBytes, 0).c_str());
            // position the static controls
            int height;
            RECT r;
            GetClientRect(GetDlgItem(HWindow, IDT_CAPACITY), &r);
            height = r.bottom - r.top;
            int longWidth = 0;
            GrowWidth(IDT_CAPACITY, longWidth);
            GrowWidth(IDT_FREESPACE, longWidth);
            GrowWidth(IDT_USEDSPACE, longWidth);
            longWidth++; // switching to an editline caused the right edges to be offset by one pixel

            int y1, y2, y3;
            int x;

            GetWindowRect(GetDlgItem(HWindow, IDT_CAPACITY), &r);
            ScreenToClient(HWindow, (LPPOINT)&r);
            x = r.left;

            y1 = r.top;
            SetWindowPos(GetDlgItem(HWindow, IDT_CAPACITY), NULL, 0, 0, longWidth, height,
                         SWP_NOMOVE | SWP_NOZORDER | SWP_NOOWNERZORDER);

            GetWindowRect(GetDlgItem(HWindow, IDT_FREESPACE), &r);
            ScreenToClient(HWindow, (LPPOINT)&r);
            y2 = r.top;
            SetWindowPos(GetDlgItem(HWindow, IDT_FREESPACE), NULL, 0, 0, longWidth, height,
                         SWP_NOMOVE | SWP_NOZORDER | SWP_NOOWNERZORDER);

            GetWindowRect(GetDlgItem(HWindow, IDT_USEDSPACE), &r);
            ScreenToClient(HWindow, (LPPOINT)&r);
            y3 = r.top;
            SetWindowPos(GetDlgItem(HWindow, IDT_USEDSPACE), NULL, 0, 0, longWidth, height,
                         SWP_NOMOVE | SWP_NOZORDER | SWP_NOOWNERZORDER);

            int shortWidth = 0;
            GrowWidth(IDT_CAPACITY_SHORT, shortWidth);
            GrowWidth(IDT_FREESPACE_SHORT, shortWidth);
            GrowWidth(IDT_USEDSPACE_SHORT, shortWidth);
            shortWidth++;                                                                 // switching to an editline caused the right edges to be offset by one pixel
            x = r.left + longWidth + (spaceForLongAndShort - longWidth - shortWidth) / 2; // center SHORT between LONG and GRAPH
            if (x < r.left + longWidth)
                x = r.left + longWidth + height;
            SetWindowPos(GetDlgItem(HWindow, IDT_CAPACITY_SHORT), NULL, x, y1, shortWidth, height,
                         SWP_NOZORDER | SWP_NOOWNERZORDER);
            SetWindowPos(GetDlgItem(HWindow, IDT_FREESPACE_SHORT), NULL, x, y2, shortWidth, height,
                         SWP_NOZORDER | SWP_NOOWNERZORDER);
            SetWindowPos(GetDlgItem(HWindow, IDT_USEDSPACE_SHORT), NULL, x, y3, shortWidth, height,
                         SWP_NOZORDER | SWP_NOOWNERZORDER);
        }
        //---  GetDriveType
        UINT driveType;
        std::wstring remoteName;
        bool remoteNameValid = false;
        std::wstring userName;
        bool userNameValid = false;
        driveType = MyGetDriveTypeW(volumePathWithBackslash.c_str());
        err = (driveType == 0 || driveType == 1);
        if (driveType == DRIVE_REMOTE)
        {
            std::wstring localName = volumePathWithBackslash;
            if (localName.size() <= 3 && localName.size() >= 2 && localName[1] == L':')
                localName.resize(2); // "x:\\" -> "x:"
            remoteNameValid = GetNetworkConnectionTextW(localName, remoteName);
            userNameValid = GetNetworkUserTextW(localName, userName);
        }
        //---  GetDriveType - display
        if (!err)
        {
            std::wstring driveTypeText;
            switch (driveType)
            {
            case DRIVE_REMOVABLE:
                driveTypeText = LoadStrW(IDS_INFODLGTYPE1);
                break;
            case DRIVE_FIXED:
                driveTypeText = LoadStrW(IDS_INFODLGTYPE2);
                break;
            case DRIVE_REMOTE:
            {
                driveTypeText = LoadStrW(IDS_INFODLGTYPE3);
                if (remoteNameValid || userNameValid)
                {
                    driveTypeText += L" ";
                    driveTypeText += FormatStrW(LoadStrW(IDS_INFODLGTYPE8),
                                                remoteNameValid ? remoteName.c_str() : L"",
                                                userNameValid ? userName.c_str() : L"");
                }
                break;
            }
            case DRIVE_CDROM:
                driveTypeText = LoadStrW(IDS_INFODLGTYPE4);
                break;
            case DRIVE_RAMDISK:
                driveTypeText = LoadStrW(IDS_INFODLGTYPE5);
                break;
            default:
                driveTypeText = FormatStrW(LoadStrW(IDS_INFODLGTYPE6), driveType);
                break;
            }
            BOOL substInfo = FALSE;
            if (!volumePathWithBackslash.empty() && volumePathWithBackslash[0] != L'\\' &&
                volumePathWithBackslash.size() <= 3)
            {
                wchar_t drive = towupper(volumePathWithBackslash[0]);
                std::wstring substTarget;
                if (GetSubstInformationW(static_cast<BYTE>(drive - L'A'), substTarget))
                {
                    substInfo = TRUE;
                    driveTypeText += L" ";
                    driveTypeText += FormatStrW(LoadStrW(IDS_INFODLGTYPE7), substTarget.c_str());
                }
            }
            if (!substInfo && !junctionOrSymlinkTgt.empty())
            {
                driveTypeText += L" ";
                driveTypeText += FormatStrW(LoadStrW(linkType == 2 ? IDS_INFODLGTYPE9 : IDS_INFODLGTYPE10),
                                            junctionOrSymlinkTgt.c_str());
            }
            SetWindowTextW(GetDlgItem(HWindow, IDT_DRIVETYPE), driveTypeText.c_str());
        }
        //---  GetDriveIcon
        HDriveIcon = GetDriveIconW(volumePathWithBackslash.c_str(), driveType, TRUE, TRUE);
        SendDlgItemMessage(HWindow, IDI_DI_DRIVE, STM_SETIMAGE, IMAGE_ICON, (LPARAM)HDriveIcon);
    }
}

void CDriveInfo::GrowWidth(int resID, int& width)
{
    wchar_t buff[200];
    int minWidth = 0;

    HWND hItem = GetDlgItem(HWindow, resID);
    GetWindowTextW(hItem, buff, 200);
    wcscat_s(buff, _countof(buff), L"M"); // the editbox has some margins; adding "M" compensates for them
    HFONT hFont = (HFONT)SendMessage(hItem, WM_GETFONT, 0, 0);

    SIZE sz;
    HDC hDC = HANDLES(GetDC(HWindow));
    HFONT hOldFont = (HFONT)SelectObject(hDC, hFont);
    GetTextExtentPoint32W(hDC, buff, (int)wcslen(buff), &sz);
    SelectObject(hDC, hOldFont);
    HANDLES(ReleaseDC(HWindow, hDC));

    if (width < sz.cx)
        width = sz.cx;
}

INT_PTR
CDriveInfo::DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    CALL_STACK_MESSAGE4("CDriveInfo::DialogProc(0x%X, 0x%IX, 0x%IX)", uMsg, wParam, lParam);
    switch (uMsg)
    {
    case WM_INITDIALOG:
    {
        COLORREF FreeLight, FreeDark, UsedLight, UsedDark;
        HDC hdc = HANDLES(GetDC(HWindow));
        int devCaps = GetDeviceCaps(hdc, NUMCOLORS);
        HANDLES(ReleaseDC(HWindow, hdc));
        if (devCaps == -1) // more than 256 colors
        {
            FreeLight = RGB(35, 245, 156);
            FreeDark = RGB(9, 159, 96);
            UsedLight = RGB(74, 163, 234);
            UsedDark = RGB(18, 95, 156);
        }
        else
        {
            FreeLight = RGB(0, 255, 0);
            FreeDark = RGB(0, 128, 0);
            UsedLight = RGB(0, 0, 255);
            UsedDark = RGB(0, 0, 128);
        }

        CColorRectangle* cr;
        cr = new CColorRectangle(HWindow, IDB_FREESPACE);
        if (cr != NULL)
            cr->SetColor(FreeLight);
        cr = new CColorRectangle(HWindow, IDB_USEDSPACE);
        if (cr != NULL)
            cr->SetColor(UsedLight);

        Graph = new CColorGraph(HWindow, IDB_GRAPH); // JRYFIXME - rewrite to W10 look; see disk properties, it won't be comfortable to use GDI+ maybe our SVG?
        if (Graph != NULL)
            Graph->SetColor(FreeLight, FreeDark, UsedLight, UsedDark);

        break;
    }

    case WM_DESTROY:
    {
        if (HDriveIcon != NULL)
            HANDLES(DestroyIcon(HDriveIcon));
        break;
    }

        //    case WM_COMMAND:
        //    {
        //      if (HIWORD(wParam) == EN_CHANGE)
        //        Dirty = TRUE;
        //      break;
        //    }
    }
    return CCommonDialog::DialogProc(uMsg, wParam, lParam);
}

//
// ****************************************************************************
// CEnterPasswdDialog
//

CEnterPasswdDialog::CEnterPasswdDialog(HWND parent, const wchar_t* path, const wchar_t* user,
                                       CObjectOrigin origin)
    : CCommonDialog(HLanguage, IDD_ENTERPASSWD, IDD_ENTERPASSWD, parent, origin)
{
    Path = path;
    if (user != NULL)
        User = user;
    else
        User.clear();
    Passwd.clear();
}

void CEnterPasswdDialog::Validate(CTransferInfo& ti)
{
    CALL_STACK_MESSAGE1("CEnterPasswdDialog::Validate()");
    /*  // empty user-name = default username
  HWND edit;
  if (ti.GetControl(edit, IDE_NETUSER) && ti.Type == ttDataFromWindow)
  {
    if (SendMessage(edit, WM_GETTEXTLENGTH, 0, 0) == 0)
    {
      SalMessageBoxW(HWindow, LoadStrW(IDS_EMPTYUSERNAME),
                    LoadStrW(IDS_ERRORTITLE), MB_OK | MB_ICONEXCLAMATION);
      ti.ErrorOn(IDE_NETUSER);
    }
  }
*/
}

void CEnterPasswdDialog::Transfer(CTransferInfo& ti)
{
    ti.EditLineW(IDE_NETPASSWD, Passwd);
    ti.EditLineW(IDE_NETUSER, User);
    if (ti.Type == ttDataToWindow)
    {
        SendDlgItemMessageW(HWindow, IDE_NETPASSWD, EM_LIMITTEXT, PASSWORD_MAXLEN - 1, 0);
        SendDlgItemMessageW(HWindow, IDE_NETUSER, EM_LIMITTEXT, USERNAME_MAXLEN - 1, 0);
    }
}

INT_PTR
CEnterPasswdDialog::DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
    case WM_INITDIALOG:
    {
        SetWindowTextW(GetDlgItem(HWindow, IDS_NETPATH), Path);
        break;
    }
    }

    return CCommonDialog::DialogProc(uMsg, wParam, lParam);
}

//
// ****************************************************************************
// CPackDialog
//

CPackDialog::CPackDialog(HWND parent, std::wstring& path, const std::wstring& pathAlt,
                         CTruncatedString* subject, CPackerConfig* config)
    : CCommonDialog(HLanguage, IDD_PACK, IDD_PACK, parent, ooStandard, NULL),
      Path(path),
      PathAlt(pathAlt)
{
    Subject = subject;
    PackerConfig = config;
    SelectionEnd = -1;
}

void CPackDialog::SetSelectionEnd(int selectionEnd)
{
    SelectionEnd = selectionEnd;
}

void CPackDialog::Transfer(CTransferInfo& ti)
{
    CALL_STACK_MESSAGE1("CPackDialog::Transfer()");
    HWND combo;
    if (ti.GetControl(combo, IDC_PACKER))
    {
        if (ti.Type == ttDataToWindow)
        {
            SendMessageW(combo, CB_RESETCONTENT, 0, 0);
            int i;
            for (i = 0; i < PackerConfig->GetPackersCount(); i++)
            {
                SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)PackerConfig->GetPackerTitle(i));
            }
            // sets the position in the combo, preferedPacker == -1 -> no selection
            SendMessageW(combo, CB_SETCURSEL, (WPARAM)PackerConfig->GetPreferedPacker(), 0);

            i = (int)SendMessageW(combo, CB_GETCURSEL, 0, 0);
            if (i != CB_ERR)
            {
                BOOL supMove = TRUE;
                if (PackerConfig->GetPackerType(i) == CUSTOMPACKER_EXTERNAL)
                {
                    supMove = PackerConfig->GetPackerSupMove(i);
                }
                EnableWindow(GetDlgItem(HWindow, IDC_MOVEFILES), supMove);
            }
        }
        else // ttDataFromWindow
        {
            int i = (int)SendMessageW(combo, CB_GETCURSEL, (WPARAM)PackerConfig->GetPreferedPacker(), 0);
            if (i != CB_ERR)
                PackerConfig->SetPreferedPacker(i);
            else
                PackerConfig->SetPreferedPacker(-1);
        }
    }

    if (ti.Type == ttDataToWindow)
    {
        // WARNING: code must stay consistent with CPackDialog::DialogProc/WM_COMMAND
        ti.GetControl(combo, IDE_PATH);
        SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)Path.c_str());
        // if the alternative path matches the first one, don't add it (target isn't ptDisk)
        if (_wcsicmp(Path.c_str(), PathAlt.c_str()) != 0)
            SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)PathAlt.c_str());
        SendMessageW(combo, CB_SETCURSEL, 0, 0);
    }
    else
    {
        // Read the native Unicode combo directly into the caller's
        // one archive-name owner. There is no ACP copy-back or overlay-mode latch.
        HWND hEdit;
        if (ti.GetControl(hEdit, IDE_PATH))
        {
            const int len = GetWindowTextLengthW(hEdit);
            if (len > 0)
            {
                std::vector<wchar_t> buffer((size_t)len + 1);
                GetWindowTextW(hEdit, buffer.data(), len + 1);
                Path = buffer.data();
            }
            else
                Path.clear();
        }
    }

    if (ti.Type == ttDataFromWindow) // if the entered name lacks an extension, add it automatically
    {
        if (PackerConfig->GetPreferedPacker() != -1) // if we have an extension, otherwise don't change the entered name
        {
            if (!Path.empty() && Path.back() != L'\\')
            {
                std::wstring extension = L".";
                extension += PackerConfig->GetPackerExt(PackerConfig->GetPreferedPacker());
                SalPathAddExtensionW(Path, extension.c_str());
            }
        }
    }

    HWND move;
    if (ti.GetControl(move, IDC_MOVEFILES))
    {
        if (ti.Type == ttDataToWindow)
        {
            ti.CheckBox(IDC_MOVEFILES, PackerConfig->Move);
        }
        else // ttDataFromWindow
        {
            if (IsWindowEnabled(move))
            {
                ti.CheckBox(IDC_MOVEFILES, PackerConfig->Move);
            }
            else
            {
                PackerConfig->Move = FALSE;
            }
        }
    }
}

BOOL CPackDialog::ChangeExtension(std::wstring& name, const wchar_t* ext)
{
    if (name.empty() || name.back() == L'\\')
        return FALSE;

    std::wstring extension = L".";
    extension += ext;
    return SalPathRenameExtensionW(name, extension.c_str());
}

INT_PTR
CPackDialog::DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    CALL_STACK_MESSAGE4("CPackDialog::DialogProc(0x%X, 0x%IX, 0x%IX)", uMsg, wParam, lParam);
    switch (uMsg)
    {
    case WM_INITDIALOG:
    {
        InstallWordBreakProc(GetDlgItem(HWindow, IDE_PATH)); // install WordBreakProc into the editline

        HWND hSubject = GetDlgItem(HWindow, IDS_SUBJECT);
        if (Subject->TruncateText(hSubject))
            SetWindowTextW(hSubject, Subject->GetW());

        INT_PTR ret = CCommonDialog::DialogProc(uMsg, wParam, lParam);

        // we can select only the name without the dot and extension
        PostMessageW(GetDlgItem(HWindow, IDE_PATH), CB_SETEDITSEL, 0, MAKELPARAM(0, SelectionEnd));
        return FALSE;
    }

    case WM_COMMAND:
    {
        if (HIWORD(wParam) == CBN_SELCHANGE && LOWORD(wParam) == IDC_PACKER)
        {
            int i = (int)SendMessageW((HWND)lParam, CB_GETCURSEL, 0, 0);
            if (i != CB_ERR)
            {
                // swap extensions
                std::wstring name;
                std::wstring name2;

                int curSel = (int)SendDlgItemMessageW(HWindow, IDE_PATH, CB_GETCURSEL, 0, 0);
                if (curSel == CB_ERR) // we must retrieve the text here because CB_RESETCONTENT would wipe it
                {
                    HWND pathWindow = GetDlgItem(HWindow, IDE_PATH);
                    const int len = GetWindowTextLengthW(pathWindow);
                    if (len > 0)
                    {
                        std::vector<wchar_t> buffer((size_t)len + 1);
                        GetWindowTextW(pathWindow, buffer.data(), len + 1);
                        name2 = buffer.data();
                    }
                }

                // WARNING: code must stay consistent with CPackDialog::Transfer
                // swap extensions in the combobox
                SendDlgItemMessageW(HWindow, IDE_PATH, CB_RESETCONTENT, 0, 0);
                name = Path;
                if (ChangeExtension(name, PackerConfig->GetPackerExt(i)))
                    SendDlgItemMessageW(HWindow, IDE_PATH, CB_ADDSTRING, 0, (LPARAM)name.c_str());
                else
                    SendDlgItemMessageW(HWindow, IDE_PATH, CB_ADDSTRING, 0, (LPARAM)Path.c_str());

                // if the alternative path matches the first one, don't add it (target isn't ptDisk)
                if (_wcsicmp(Path.c_str(), PathAlt.c_str()) != 0)
                {
                    name = PathAlt;
                    if (ChangeExtension(name, PackerConfig->GetPackerExt(i)))
                        SendDlgItemMessageW(HWindow, IDE_PATH, CB_ADDSTRING, 0, (LPARAM)name.c_str());
                    else
                        SendDlgItemMessageW(HWindow, IDE_PATH, CB_ADDSTRING, 0, (LPARAM)PathAlt.c_str());
                }

                if (curSel != CB_ERR)
                    SendDlgItemMessageW(HWindow, IDE_PATH, CB_SETCURSEL, (WPARAM)curSel, 0);
                else
                {
                    // if the editline was modified, change the extension there as well
                    if (ChangeExtension(name2, PackerConfig->GetPackerExt(i)))
                        SetWindowTextW(GetDlgItem(HWindow, IDE_PATH), name2.c_str());
                }

                BOOL supMove = TRUE;
                if (PackerConfig->GetPackerType(i) == CUSTOMPACKER_EXTERNAL)
                {
                    supMove = PackerConfig->GetPackerSupMove(i);
                }
                EnableWindow(GetDlgItem(HWindow, IDC_MOVEFILES), supMove);
            }
        }
        break;
    }
    }

    return CCommonDialog::DialogProc(uMsg, wParam, lParam);
}

//
// ****************************************************************************
// CUnpackDialog
//

CUnpackDialog::CUnpackDialog(HWND parent, std::wstring& path, const std::wstring& pathAlt, std::wstring& mask,
                             CTruncatedString* subject, CUnpackerConfig* config,
                             BOOL* delArchiveWhenDone)
    : CCommonDialog(HLanguage, IDD_UNPACK, IDD_UNPACK, parent),
      Mask(mask),
      Path(path),
      PathAlt(pathAlt)
{
    Subject = subject;
    UnpackerConfig = config;
    DelArchiveWhenDone = delArchiveWhenDone;
}

void CUnpackDialog::EnableDelArcCheckbox()
{
    int i = (int)SendDlgItemMessage(HWindow, IDC_PACKER, CB_GETCURSEL, 0, 0);
    EnableWindow(GetDlgItem(HWindow, IDC_DELETEARCHIVEFILES),
                 i != CB_ERR && UnpackerConfig->GetUnpackerType(i) != CUSTOMUNPACKER_EXTERNAL);
}

void CUnpackDialog::Transfer(CTransferInfo& ti)
{
    CALL_STACK_MESSAGE1("CUnpackDialog::Transfer()");
    HWND combo;
    if (ti.GetControl(combo, IDC_PACKER))
    {
        if (ti.Type == ttDataToWindow)
        {
            SendMessage(combo, CB_RESETCONTENT, 0, 0);
            int i;
            for (i = 0; i < UnpackerConfig->GetUnpackersCount(); i++)
            {
                SendMessage(combo, CB_ADDSTRING, 0, (LPARAM)UnpackerConfig->GetUnpackerTitle(i));
            }
            // set the position in the combo, preferredUnpacker == -1 -> no selection
            SendMessage(combo, CB_SETCURSEL, (WPARAM)UnpackerConfig->GetPreferedUnpacker(), 0);
            EnableDelArcCheckbox();
        }
        else // ttDataFromWindow
        {
            int i = (int)SendMessage(combo, CB_GETCURSEL, 0, 0);
            if (i != CB_ERR)
                UnpackerConfig->SetPreferedUnpacker(i);
            else
                UnpackerConfig->SetPreferedUnpacker(-1);
        }
    }
    if (ti.Type == ttDataToWindow)
    {
        ti.GetControl(combo, IDE_PATH);
        SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)Path.c_str());
        // if the alternative path matches the first one, don't add it (target isn't ptDisk)
        if (_wcsicmp(Path.c_str(), PathAlt.c_str()) != 0)
            SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)PathAlt.c_str());
        SendMessageW(combo, CB_SETCURSEL, 0, 0);
        ti.CheckBox(IDC_DELETEARCHIVEFILES, *DelArchiveWhenDone);
    }
    else
    {
        HWND hEdit;
        if (ti.GetControl(hEdit, IDE_PATH))
            Path = GetWindowTextStringW(hEdit);
        if (IsWindowEnabled(GetDlgItem(HWindow, IDC_DELETEARCHIVEFILES)))
            ti.CheckBox(IDC_DELETEARCHIVEFILES, *DelArchiveWhenDone);
        else
            *DelArchiveWhenDone = FALSE;
    }

    HWND maskEdit;
    if (ti.GetControl(maskEdit, IDE_MASK))
    {
        if (ti.Type == ttDataToWindow)
            SetWindowTextW(maskEdit, Mask.c_str());
        else
            Mask = GetWindowTextStringW(maskEdit);
    }
}

INT_PTR
CUnpackDialog::DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
    case WM_INITDIALOG:
    {
        InstallWordBreakProc(GetDlgItem(HWindow, IDE_PATH)); // install WordBreakProc into the editline
        InstallWordBreakProc(GetDlgItem(HWindow, IDE_MASK)); // install WordBreakProc into the editline

        HWND hSubject = GetDlgItem(HWindow, IDS_SUBJECT);
        if (Subject->TruncateText(hSubject))
        {
            // wide - mirrors the CCopyMoveDialog/CCopyMoveMoreDialog
            // precedent; UnpackZIPArchive now seeds Subject wide (SetW with the
            // audited archiveNameW/fileNameW), so this dialog needs the same check.
                SetWindowTextW(hSubject, Subject->GetW());
        }

        CHyperLink* hl = new CHyperLink(HWindow, IDC_FILEMASK_HINT, STF_DOTUNDERLINE);
        if (hl != NULL)
            hl->SetActionShowHint(LoadStrW(IDS_MASKS_HINT));

        break;
    }

    case WM_COMMAND:
    {
        if (HIWORD(wParam) == CBN_SELCHANGE && LOWORD(wParam) == IDC_PACKER)
            EnableDelArcCheckbox();
        break;
    }
    }

    return CCommonDialog::DialogProc(uMsg, wParam, lParam);
}

//
// ****************************************************************************
// CZIPSizeResultsDlg
//

CZIPSizeResultsDlg::CZIPSizeResultsDlg(HWND parent, const CQuadWord& size, int files, int dirs)
    : CCommonDialog(HLanguage, IDD_ZIPSIZERESULTS, parent)
{
    Size = size;
    Files = files;
    Dirs = dirs;
}

INT_PTR
CZIPSizeResultsDlg::DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
    case WM_INITDIALOG:
    {
        /*
      RECT r1;                        // horizontal centering of the dialog
      GetWindowRect(HWindow, &r1);
      RECT r2;
      GetWindowRect(MainWindow->GetActivePanelHWND(), &r2);
      int width = r1.right - r1.left;
      r1.left = (r2.right + r2.left - width) / 2;
      MoveWindow(HWindow, r1.left, r1.top, width, r1.bottom - r1.top, FALSE);
      */
        SetWindowTextW(GetDlgItem(HWindow, IDS_SIZE), PrintDiskSize(Size, 1).c_str());
        SetWindowTextW(GetDlgItem(HWindow, IDS_FILESCOUNT), NumberToStr(CQuadWord(Files, 0)).c_str());
        SetWindowTextW(GetDlgItem(HWindow, IDS_DIRSCOUNT), NumberToStr(CQuadWord(Dirs, 0)).c_str());
        break;
    }
    }

    return CCommonDialog::DialogProc(uMsg, wParam, lParam);
}

//***************************************************************************
//
// CChangeIconDialog
//

CChangeIconDialog::CChangeIconDialog(HWND hParent, std::wstring& iconFile, int* iconIndex)
    : CCommonDialog(HLanguage, IDD_CHANGEICON, IDD_CHANGEICON, hParent)
{
    IconFile = &iconFile;
    IconIndex = iconIndex;
    Dirty = FALSE;
    Icons = NULL;
    IconsCount = 0;
}

CChangeIconDialog::~CChangeIconDialog()
{
    DestroyIcons();
}

void CChangeIconDialog::GetShell32(std::wstring& fileName)
{
    if (!gEnvironment->GetSystemDirectory(fileName).success)
        fileName.clear();
    SalPathAppendW(fileName, L"SHELL32.DLL");
    SetDlgItemTextW(HWindow, IDE_CHI_FILENAME, fileName.c_str());
}

void CChangeIconDialog::Transfer(CTransferInfo& ti)
{
    if (ti.Type == ttDataToWindow)
    {
        SetDlgItemTextW(HWindow, IDE_CHI_FILENAME, IconFile->c_str());
        LoadIcons();
        Dirty = FALSE;
    }
    else
    {
        HWND edit = GetDlgItem(HWindow, IDE_CHI_FILENAME);
        const int length = GetWindowTextLengthW(edit);
        std::vector<wchar_t> text((size_t)length + 1);
        GetWindowTextW(edit, text.data(), length + 1);
        IconFile->assign(text.data());
        int curSel = (int)SendDlgItemMessage(HWindow, IDL_CHI_LIST, LB_GETCURSEL, 0, 0);
        if (curSel == LB_ERR || curSel >= (int)IconsCount)
        {
            std::wstring fileName;
            GetShell32(fileName);
            *IconFile = fileName;
            LoadIcons();
            *IconIndex = 0;
        }
        else
        {
            *IconIndex = curSel;
        }
    }
}

BOOL CChangeIconDialog::LoadIcons()
{
    HWND edit = GetDlgItem(HWindow, IDE_CHI_FILENAME);
    const int length = GetWindowTextLengthW(edit);
    std::vector<wchar_t> text((size_t)length + 1);
    GetWindowTextW(edit, text.data(), length + 1);
    std::wstring fileName(text.data());
    int counter = 0;

AGAIN:
    counter++;
    DestroyIcons();
    SendDlgItemMessage(HWindow, IDL_CHI_LIST, LB_SETCOUNT, 0, 0);

    if (MainWindow->GetActivePanel()->CheckPath(FALSE, fileName.c_str()) != ERROR_SUCCESS)
    {
        // fileName is genuinely wide now (GetDlgItemTextW above) -
        // AnsiToWide on it would misread its bytes as CP_ACP; just use it directly.
        std::wstring msg = FormatStrW(LoadStrW(IDS_CANNONTFINDFILE), fileName.c_str());
        gPrompter->ShowError(LoadStrW(IDS_ERRORTITLE), msg.c_str());

        // fall back to default
        GetShell32(fileName);
    }

    // enumeration of icons from *.ICO, *.EXE, *.DLL files, including 16-bit PE
    int iconsCount = ExtractIconExW(fileName.c_str(), -1, NULL, NULL, 0);
    if (iconsCount > 0)
    {
        Icons = new HICON[iconsCount];
        if (Icons != NULL)
        {
            IconsCount = ExtractIconExW(fileName.c_str(), 0, Icons, NULL, iconsCount);
            // add the HIcon handle to HANDLES
            for (DWORD i = 0; i < IconsCount; i++)
                HANDLES_ADD(__htIcon, __hoLoadImage, Icons[i]);
        }
        else
            TRACE_E(LOW_MEMORY);
    }

    if (IconsCount == 0)
    {
        std::wstring msg = FormatStrW(LoadStrW(IDS_NOICONS), fileName.c_str());
        gPrompter->ShowError(LoadStrW(IDS_ERRORTITLE), msg.c_str());

        // fall back to default
        GetShell32(fileName);
        if (counter < 2) // safety check
            goto AGAIN;
    }

    SendDlgItemMessage(HWindow, IDL_CHI_LIST, LB_SETCOUNT, IconsCount, 0);

    if (IconsCount > 0 && *IconIndex < (int)IconsCount)
        SendDlgItemMessage(HWindow, IDL_CHI_LIST, LB_SETCURSEL, *IconIndex, 0);

    return TRUE;
}

void CChangeIconDialog::DestroyIcons()
{
    int i;
    for (i = 0; i < (int)IconsCount; i++)
        HANDLES(DestroyIcon(Icons[i]));
    delete[] Icons;
    Icons = NULL;
    IconsCount = 0;
}

INT_PTR
CChangeIconDialog::DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
    case WM_INITDIALOG:
    {
        HWND hList = GetDlgItem(HWindow, IDL_CHI_LIST);
        SendMessage(hList, LB_SETCOLUMNWIDTH, 32 + 8, 0);
        SendMessage(hList, LB_SETITEMHEIGHT, 0, MAKELPARAM(32 + 8, 0));

        // adjust the list box size
        RECT wRect;
        RECT cRect;
        GetWindowRect(hList, &wRect);
        GetClientRect(hList, &cRect);

        int deltaH = cRect.bottom - 4 * (32 + 8);

        SetWindowPos(hList, NULL, 0, 0,
                     wRect.right - wRect.left,
                     wRect.bottom - wRect.top - deltaH,
                     SWP_NOMOVE | SWP_NOZORDER);
        break;
    }

    case WM_DRAWITEM:
    {
        LPDRAWITEMSTRUCT lpdis = (LPDRAWITEMSTRUCT)lParam;
        if ((int)lpdis->itemID >= 0 && (int)lpdis->itemID < (int)IconsCount)
        {
            RECT r = lpdis->rcItem;

            // draw the background
            DarkModeColors colors;
            DarkMode_GetColors(&colors);
            COLORREF bkColor = (lpdis->itemState & ODS_SELECTED) ? colors.Highlight : colors.InputBackground;
            HBRUSH hBrush = HANDLES(CreateSolidBrush(bkColor));
            if (hBrush != NULL)
            {
                FillRect(lpdis->hDC, &r, hBrush);
                HANDLES(DeleteObject(hBrush));
            }

            // draw the icon
            DrawIconEx(lpdis->hDC, r.left + 4, r.top + 4, Icons[lpdis->itemID], 32, 32, 0, NULL, DI_NORMAL);

            if (lpdis->itemState & ODS_FOCUS)
                DrawFocusRect(lpdis->hDC, &r);
        }
        return TRUE;
    }

    case WM_COMMAND:
    {
        if (LOWORD(wParam) == IDL_CHI_LIST && HIWORD(wParam) == LBN_DBLCLK)
        {
            PostMessage(HWindow, WM_COMMAND, MAKELPARAM(IDOK, BN_CLICKED), 0);
            break;
        }

        if (LOWORD(wParam) == IDOK)
        {
            if (Dirty)
            {
                Dirty = FALSE;
                LoadIcons();
                return 0;
            }
            break;
        }

        if (LOWORD(wParam) == IDE_CHI_FILENAME)
        {
            if (HIWORD(wParam) == EN_CHANGE)
                Dirty = TRUE;
            if (HIWORD(wParam) == EN_KILLFOCUS)
            {
                if (Dirty)
                {
                    LoadIcons();
                    Dirty = FALSE;
                }
            }
            break;
        }

        if (LOWORD(wParam) == IDL_CHI_BROWSE)
        {
            if (BrowseCommand(HWindow, IDE_CHI_FILENAME, IDS_ICOFILTER))
            {
                LoadIcons();
                Dirty = FALSE;
            }
            return 0;
        }
        break;
    }
    }
    return CCommonDialog::DialogProc(uMsg, wParam, lParam);
}

//****************************************************************************
//
// CWaitWindow
//

CWaitWindow::CWaitWindow(HWND hParent, int textResID, BOOL showCloseButton, CObjectOrigin origin, BOOL showProgressBar)
    : CWindow(origin)
{
    HParent = hParent;
    HForegroundWnd = NULL;
    if (textResID != 0)
    {
        const wchar_t* t = LoadStrW(textResID); // Text is wide
        Text = t ? t : L"";
    }
    ShowCloseButton = showCloseButton;
    ShowProgressBar = showProgressBar;
    BarMax = 0;
    BarPos = 0;
    NeedWrap = FALSE;
    CacheBitmap = NULL;
}

CWaitWindow::~CWaitWindow()
{
    if (CacheBitmap != NULL)
        delete (CacheBitmap);
}

void CWaitWindow::SetText(const wchar_t* text)
{
    Text = text ? text : L"";
    if (HWindow != NULL && IsWindowVisible(HWindow))
    {
        HDC hDC = GetDC(HWindow);
        if (CacheBitmap == NULL) // only when the text changes we use a cache bitmap
        {
            CacheBitmap = new CBitmap();
            if (CacheBitmap != NULL)
            {
                RECT r;
                GetClientRect(HWindow, &r);
                if (!CacheBitmap->CreateBmp(hDC, r.right, r.bottom))
                {
                    delete (CacheBitmap);
                    CacheBitmap = NULL;
                }
            }
        }
        PaintText(hDC);
        ReleaseDC(HWindow, hDC);
    }
}

void CWaitWindow::SetCaption(const wchar_t* text)
{
    Caption = text ? text : L"";
}

#define WAITWINDOW_HMARGIN 21
#define WAITWINDOW_VMARGIN 14

HWND CWaitWindow::Create(HWND hForegroundWnd)
{
    if (Text.empty())
    {
        TRACE_E("CWaitWindow::Create(): you must set text for wait-wnd first!");
        return NULL;
    }
    HForegroundWnd = hForegroundWnd;

    if (HForegroundWnd != NULL)
        HForegroundWnd = GetTopVisibleParent(HForegroundWnd);

    // compute the window position; primarily center to HForegroundWindow, secondarily to MainWindow
    HWND hCenterWnd = NULL;
    if (HForegroundWnd != NULL)
        hCenterWnd = HForegroundWnd;
    else if (MainWindow != NULL)
        hCenterWnd = MainWindow->HWindow;

    RECT clipRect;
    MultiMonGetClipRectByWindow(hCenterWnd, &clipRect, NULL);

    int scrW = clipRect.right - clipRect.left;
    int scrH = clipRect.bottom - clipRect.top;

    // compute the text size => window size
    NeedWrap = FALSE;
    HDC dc = HANDLES(GetDC(NULL));
    if (dc != NULL)
    {
        HFONT old = (HFONT)SelectObject(dc, EnvFont);

        RECT tR;
        tR.left = 0;
        tR.top = 0;
        tR.right = 1;
        tR.bottom = 1;
        DrawTextW(dc, Text.c_str(), -1, &tR, DT_CALCRECT | DT_LEFT | DT_NOPREFIX);
        if (tR.right + 2 * WAITWINDOW_HMARGIN >= scrW)
        {
            tR.right = (int)(scrW / 1.8);
            tR.bottom = 1;
            DrawTextW(dc, Text.c_str(), -1, &tR, DT_CALCRECT | DT_LEFT | DT_NOPREFIX | DT_WORDBREAK);
            NeedWrap = TRUE;
        }
        TextSize.cx = tR.right;
        TextSize.cy = tR.bottom;
        SelectObject(dc, old);
        HANDLES(ReleaseDC(NULL, dc));
    }

    // an application compiled with a newer platform toolset handles window sizes differently, see
    // https://social.msdn.microsoft.com/Forums/vstudio/en-US/7ca548b5-8931-41dc-ac1d-ed9aed223d7a/different-dialog-box-position-and-size-with-visual-c-2012
    // https://connect.microsoft.com/VisualStudio/feedback/details/768135/different-dialog-box-size-and-position-when-compiled-in-visual-c-2012-vs-2010-2008
    // so we use a hack: create the window first, then measure the client area and adjust the window size after
    // note: AdjustWindowRectEx is unusable because it lies; the original frame addition is unusable too, see the links above

    int width = TextSize.cx + 2 * WAITWINDOW_HMARGIN;
    int height = TextSize.cy + 2 * WAITWINDOW_VMARGIN;

    if (ShowProgressBar)
    {
        height += 8; // create space for the progress bar

        BarRect.left = WAITWINDOW_HMARGIN;
        BarRect.top = WAITWINDOW_VMARGIN + TextSize.cy + 7;
        BarRect.right = BarRect.left + TextSize.cx;
        BarRect.bottom = BarRect.top + 4;
    }

    CreateEx(WS_EX_DLGMODALFRAME | WS_EX_TOOLWINDOW,
              SAFEWAIT_CLASSNAMEW,
              Caption.empty() ? L"Sally" : Caption.c_str(),
              WS_BORDER | WS_OVERLAPPED | (ShowCloseButton ? WS_SYSMENU : 0),
              0, 0, width, height,
              HParent,
              NULL,
              HInstance,
              this);

    // hack: adjust window size in real time so it works with both old (5 / XP compatible) and new toolsets
    RECT clientR;
    GetClientRect(HWindow, &clientR);
    width += width - (clientR.right - clientR.left);
    height += height - (clientR.bottom - clientR.top);

    SetWindowPos(HWindow, HWND_TOPMOST, 0, 0, width, height, SWP_NOMOVE | SWP_NOACTIVATE);
    MultiMonCenterWindow(HWindow, hCenterWnd, TRUE);

    // if HForegroundWnd != NULL, the display will be handled elsewhere
    if (HForegroundWnd == NULL)
        SetWindowPos(HWindow, HWND_NOTOPMOST, 0, 0, 0, 0,
                     SWP_SHOWWINDOW | SWP_NOACTIVATE | SWP_NOMOVE | SWP_NOSIZE);

    return HWindow;
}

void CWaitWindow::SetProgressMax(DWORD max)
{
    if (ShowProgressBar)
    {
        BarMax = max;
        if (HWindow != NULL)
            PaintProgressBar(NULL);
    }
    else
        TRACE_E("CWaitWindow::SetProgressMax() The progress bar must be enabled in the CWaitWindow constructor");
}
void CWaitWindow::SetProgressPos(DWORD pos)
{
    if (ShowProgressBar)
    {
        BarPos = pos;
        if (HWindow != NULL)
            PaintProgressBar(NULL);
    }
    else
        TRACE_E("CWaitWindow::SetProgressPos() The progress bar must be enabled in the CWaitWindow constructor");
}

extern BOOL SafeWaitWindowClosePressed;

void CWaitWindow::PaintProgressBar(HDC dc)
{
    BOOL releaseDC = FALSE;
    if (dc == NULL)
    {
        if (HWindow == NULL)
            return;
        dc = GetDC(HWindow);
        releaseDC = TRUE;
    }
    MoveToEx(dc, BarRect.left, BarRect.top, NULL);
    LineTo(dc, BarRect.right, BarRect.top);
    LineTo(dc, BarRect.right, BarRect.bottom);
    LineTo(dc, BarRect.left, BarRect.bottom);
    LineTo(dc, BarRect.left, BarRect.top);

    int width = BarRect.right - BarRect.left;
    if (width > 2)
    {
        int done = 0;
        if (BarMax > 0)
            done = ((width - 1) * BarPos) / BarMax;
        if (done > width - 1)
            done = width - 1;
        DarkModeColors colors;
        DarkMode_GetColors(&colors);
        RECT r = BarRect;
        r.left++;
        r.top++;
        RECT r2 = r;
        r2.right = r2.left + done;
        HBRUSH hDoneBrush = HANDLES(CreateSolidBrush(colors.Highlight));
        if (hDoneBrush != NULL)
        {
            FillRect(dc, &r2, hDoneBrush);
            HANDLES(DeleteObject(hDoneBrush));
        }
        r2 = r;
        r2.left = r2.left + done;
        HBRUSH hTodoBrush = HANDLES(CreateSolidBrush(colors.InputBackground));
        if (hTodoBrush != NULL)
        {
            FillRect(dc, &r2, hTodoBrush);
            HANDLES(DeleteObject(hTodoBrush));
        }
    }
    if (releaseDC)
        ReleaseDC(HWindow, dc);
    GdiFlush();
}

void CWaitWindow::PaintText(HDC hDC)
{
    RECT r;
    GetClientRect(HWindow, &r);

    r.left = (r.right - TextSize.cx) / 2;
    r.top = (r.bottom - TextSize.cy) / 2;
    r.right = r.left + TextSize.cx;
    r.bottom = r.top + TextSize.cy;

    if (ShowProgressBar)
    {
        r.top -= 4;
        r.bottom -= 4;
    }

    if (!Text.empty())
    {

        HDC hDestDC = hDC;
        if (CacheBitmap != NULL && CacheBitmap->HMemDC != NULL)
            hDestDC = CacheBitmap->HMemDC;

        DarkModeColors colors;
        DarkMode_GetColors(&colors);
        HBRUSH hBrush = HANDLES(CreateSolidBrush(colors.DialogBackground));
        if (hBrush != NULL)
        {
            FillRect(hDestDC, &r, hBrush);
            HANDLES(DeleteObject(hBrush));
        }

        HFONT hOldFont = (HFONT)SelectObject(hDestDC, EnvFont);
        int prevBkMode = SetBkMode(hDestDC, TRANSPARENT);
        SetTextColor(hDestDC, colors.DialogText);
        // we won't clip so that we survive minor text extension
        // that may occur during a SetText call
        DrawTextW(hDestDC, Text.c_str(), (int)Text.length(), &r, DT_LEFT | DT_NOPREFIX | DT_NOCLIP | (NeedWrap ? DT_WORDBREAK : 0));
        SetBkMode(hDestDC, prevBkMode);
        SelectObject(hDestDC, hOldFont);

        if (CacheBitmap != NULL && CacheBitmap->HMemDC != NULL)
        {
            BitBlt(hDC, r.left, r.top, r.right - r.left, r.bottom - r.top,
                   CacheBitmap->HMemDC, r.left, r.top, SRCCOPY);
        }
    }
    GdiFlush();
}

LRESULT
CWaitWindow::WindowProc(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
    case WM_ERASEBKGND:
    {
        HDC hDC = (HDC)wParam;

        RECT r;
        GetClientRect(HWindow, &r);
        DarkModeColors colors;
        DarkMode_GetColors(&colors);
        HBRUSH hBrush = HANDLES(CreateSolidBrush(colors.DialogBackground));
        if (hBrush != NULL)
        {
            FillRect(hDC, &r, hBrush);
            HANDLES(DeleteObject(hBrush));
        }

        PaintText(hDC);

        if (ShowProgressBar)
            PaintProgressBar(hDC);

        GdiFlush();

        return TRUE; // background is erased
    }

    case WM_NCHITTEST:
    {
        // prevent moving the window by dragging the title bar
        // also block the tooltip over the Close button
        return HTCLIENT;
    }

    case WM_NCLBUTTONDBLCLK:
    case WM_NCLBUTTONDOWN:
    case WM_NCLBUTTONUP:
    case WM_NCMBUTTONDBLCLK:
    case WM_NCMBUTTONDOWN:
    case WM_NCMBUTTONUP:
    case WM_NCRBUTTONDBLCLK:
    case WM_NCRBUTTONDOWN:
    case WM_NCRBUTTONUP:
    case WM_LBUTTONDBLCLK:
    case WM_LBUTTONDOWN:
    case WM_LBUTTONUP:
    case WM_MBUTTONDBLCLK:
    case WM_MBUTTONDOWN:
    case WM_MBUTTONUP:
    case WM_RBUTTONDBLCLK:
    case WM_RBUTTONDOWN:
    case WM_RBUTTONUP:
    {
        HWND hActivate;
        if (HForegroundWnd != NULL)
            hActivate = HForegroundWnd;
        else
            hActivate = MainWindow->HWindow;
        SetForegroundWindow(hActivate);
        SetActiveWindow(hActivate);
        // just to be safe, let's do one more round...
        SetForegroundWindow(hActivate);
        SetActiveWindow(hActivate);

        // JR: we originally caught only WM_LBUTTONDOWN, but Manison reported Automation couldn't detect close-button clicks
        // I found that WM_NCLBUTTONDOWN is sent when the user clicks
        if (uMsg == WM_LBUTTONDOWN || uMsg == WM_NCLBUTTONDOWN)
        {
            // if the user clicked the Close button, set the global variable
            if (CWindow::WindowProc(WM_NCHITTEST, NULL, GetMessagePos()) == HTCLOSE)
                SafeWaitWindowClosePressed = TRUE;
        }
        return 0;
    }
    }
    return CWindow::WindowProc(uMsg, wParam, lParam);
}

//****************************************************************************
//
// CConversionTablesDialog
//

// This build does not define _UNICODE, so ListView_SetItemText
// resolves to the A form only, which would narrow wide item text through the
// active code page. Mirrors the identical local macro already used by
// dialogs_highlight_registry.cpp, dialogs_tip_of_day.cpp and others.
#define ListView_SetItemTextW(hwndLV, i, iSubItem_, pszText_) \
    {                                                         \
        LV_ITEMW _ms_lvi;                                     \
        _ms_lvi.iSubItem = iSubItem_;                         \
        _ms_lvi.pszText = pszText_;                           \
        SNDMSG((hwndLV), LVM_SETITEMTEXTW, (WPARAM)(i), (LPARAM)(LV_ITEM*)&_ms_lvi); \
    }

CConversionTablesDialog::CConversionTablesDialog(HWND parent, std::wstring& dirName)
    : CCommonDialog(HLanguage, IDD_CONVERSION_TABLES, IDD_CONVERSION_TABLES, parent),
      DirName(dirName)
{
    HListView = NULL;
}

CConversionTablesDialog::~CConversionTablesDialog()
{
    CodeTables.FreePreloadedConversions();
}

void CConversionTablesDialog::Transfer(CTransferInfo& ti)
{
    if (ti.Type == ttDataToWindow)
    {
        // load all available conversions
        CodeTables.PreloadAllConversions();

        HListView = GetDlgItem(HWindow, IDC_CT_LIST);

        DWORD exFlags = LVS_EX_FULLROWSELECT;
        DWORD origFlags = ListView_GetExtendedListViewStyle(HListView);
        ListView_SetExtendedListViewStyle(HListView, origFlags | exFlags); // 4.71

        // Fill the list view from dynamically owned UTF-16 resource text.
        LVCOLUMNW lvc;
        lvc.mask = LVCF_TEXT | LVCF_SUBITEM | LVCF_FMT;
        std::wstring columnText = LoadStrOwned(IDS_CONVERSION_DESCRIPTION);
        lvc.pszText = columnText.data();
        lvc.iSubItem = 0;
        lvc.fmt = LVCFMT_LEFT;
        SendMessageW(HListView, LVM_INSERTCOLUMNW, 0, (LPARAM)&lvc);

        columnText = LoadStrOwned(IDS_CONVERSION_CODEPAGE);
        lvc.pszText = columnText.data();
        lvc.iSubItem = 1;
        lvc.fmt = LVCFMT_RIGHT;
        SendMessageW(HListView, LVM_INSERTCOLUMNW, 1, (LPARAM)&lvc);

        columnText = LoadStrOwned(IDS_CONVERSION_PATH);
        lvc.pszText = columnText.data();
        lvc.iSubItem = 2;
        lvc.fmt = LVCFMT_LEFT;
        SendMessageW(HListView, LVM_INSERTCOLUMNW, 2, (LPARAM)&lvc);

        RECT r;
        GetClientRect(HListView, &r);
        ListView_SetColumnWidth(HListView, 0, r.right / 2.3);
        ListView_SetColumnWidth(HListView, 1, LVSCW_AUTOSIZE_USEHEADER);
        ListView_SetColumnWidth(HListView, 2, LVSCW_AUTOSIZE_USEHEADER);

        int index = 0;
        const wchar_t* winCodePage;
        DWORD winCodePageIdentifier;
        const wchar_t* winCodePageDescription;
        const wchar_t* dirName;
        const std::wstring bestDirName = CodeTables.GetBestPreloadedConversion(DirName.c_str());
        int bestIndex = -1;

        while (CodeTables.EnumPreloadedConversions(&index, &winCodePage, &winCodePageIdentifier,
                                                   &winCodePageDescription, &dirName))
        {
            // bestDirName/dirName are both wide now - _wcsicmp, not the
            // narrow stricmp (which compiled silently against the former path buffer's implicit
            // wchar_t* conversion but compared the wrong byte width - pattern seen
            // repeatedly this codebase as a silent, uncaught bug, not just a type error).
            if (bestIndex == -1 && _wcsicmp(bestDirName.c_str(), dirName) == 0)
                bestIndex = index - 1;
            LVITEM lvi;
            lvi.mask = 0;
            lvi.iItem = index - 1;
            lvi.iSubItem = 0;
            ListView_InsertItem(HListView, &lvi);

            ListView_SetItemTextW(HListView, index - 1, 0, (wchar_t*)winCodePageDescription);
            const std::wstring codePage = std::to_wstring(winCodePageIdentifier);
            ListView_SetItemTextW(HListView, index - 1, 1, const_cast<wchar_t*>(codePage.c_str()));
            const std::wstring path = L"convert\\" + std::wstring(dirName) + L"\\convert.cfg";
            ListView_SetItemTextW(HListView, index - 1, 2, const_cast<wchar_t*>(path.c_str()));
        }
        const std::wstring codePage = std::to_wstring(GetACP());
        SetDlgItemTextW(HWindow, IDC_CT_CODEPAGE, codePage.c_str());
        if (bestIndex != -1)
        {
            DWORD state = LVIS_SELECTED | LVIS_FOCUSED;
            ListView_SetItemState(HListView, bestIndex, state, state);
            ListView_EnsureVisible(HListView, bestIndex, FALSE);
        }
    }
    else
    {
        int index = ListView_GetNextItem(HListView, -1, LVIS_FOCUSED);
        if (index != -1)
        {
            const wchar_t* winCodePage;
            DWORD winCodePageIdentifier;
            const wchar_t* winCodePageDescription;
            const wchar_t* dirName;
            if (CodeTables.EnumPreloadedConversions(&index, &winCodePage, &winCodePageIdentifier,
                                                    &winCodePageDescription, &dirName))
            {
                DirName = dirName;
            }
        }
    }
}

INT_PTR
CConversionTablesDialog::DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
    case WM_SYSCOLORCHANGE:
    {
        DarkMode_ApplyListTreeThemeRecursive(GetDlgItem(HWindow, IDC_CT_LIST));
        break;
    }
    }
    return CCommonDialog::DialogProc(uMsg, wParam, lParam);
}
