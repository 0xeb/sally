// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"

//****************************************************************************
//
// CCommonDialog
//

CCommonDialog::CCommonDialog(HINSTANCE hInstance, int resID, HWND hParent, CObjectOrigin origin)
    : CDialog(hInstance, resID, hParent, origin)
{
}

CCommonDialog::CCommonDialog(HINSTANCE hInstance, int resID, int helpID, HWND hParent, CObjectOrigin origin)
    : CDialog(hInstance, resID, helpID, hParent, origin)
{
}

INT_PTR
CCommonDialog::DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
    case WM_INITDIALOG:
    {
        // horizontal and vertical centering of the dialog relative to the parent
        if (Parent != NULL)
            SalamanderGeneral->MultiMonCenterWindow(HWindow, Parent, TRUE);
        break; // request focus from DefDlgProc
    }
    }
    return CDialog::DialogProc(uMsg, wParam, lParam);
}

void CCommonDialog::NotifDlgJustCreated()
{
    SalamanderGUI->ArrangeHorizontalLines(HWindow);
}

// ****************************************************************************
// CProgressDlg
//

CProgressDlg::CProgressDlg(HWND parent, const wchar_t* title, const wchar_t* operation, CObjectOrigin origin, int resID)
    : CCommonDialog(HLanguage, resID ? resID : IDD_PROGRESSDLG, parent, origin)
{
    ProgressBar = NULL;
    WantCancel = FALSE;
    LastTickCount = 0;
    TextCache.clear();
    TextCacheIsDirty = FALSE;
    ProgressCache = 0;
    ProgressCacheIsDirty = FALSE;
    ProgressTotalCache = 0;
    ProgressTotalCacheIsDirty = FALSE;

    Title = title != NULL ? title : L"";
    Operation = operation != NULL ? operation : L"";
}

void CProgressDlg::Set(const wchar_t* fileName, DWORD progressTotal, BOOL dalayedPaint)
{
    TextCache = fileName != NULL ? fileName : L"";
    TextCacheIsDirty = TRUE;

    if (progressTotal != ProgressTotalCache)
    {
        ProgressTotalCache = progressTotal;
        ProgressTotalCacheIsDirty = TRUE;
    }

    if (!dalayedPaint)
        FlushDataToControls();
}

void CProgressDlg::SetProgress(DWORD progressTotal, DWORD progress, BOOL dalayedPaint)
{
    if (progressTotal != ProgressTotalCache)
    {
        ProgressTotalCache = progressTotal;
        ProgressTotalCacheIsDirty = TRUE;
    }

    if (progress != ProgressCache)
    {
        ProgressCache = progress;
        ProgressCacheIsDirty = TRUE;
    }

    if (!dalayedPaint)
        FlushDataToControls();
}

void CProgressDlg::EnableCancel(BOOL enable)
{
    if (HWindow != NULL)
    {
        HWND cancel = GetDlgItem(HWindow, IDCANCEL);
        if (IsWindowEnabled(cancel) != enable)
        {
            EnableWindow(cancel, enable);
            if (enable)
                SetFocus(cancel);
            PostMessage(cancel, BM_SETSTYLE, enable ? BS_DEFPUSHBUTTON : BS_PUSHBUTTON, TRUE);

            MSG msg;
            while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) // give the user a moment ...
            {
                if (!IsWindow(HWindow) || !IsDialogMessage(HWindow, &msg))
                {
                    TranslateMessage(&msg);
                    DispatchMessageW(&msg);
                }
            }
        }
    }
}

BOOL CProgressDlg::GetWantCancel()
{
    // every 100 ms repaint the changed data (text + progress bars)
    DWORD ticks = GetTickCount();
    if (ticks - LastTickCount > 100)
    {
        LastTickCount = ticks;
        FlushDataToControls();
    }

    MSG msg;
    while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) // give the user a moment ...
    {
        if (!IsWindow(HWindow) || !IsDialogMessage(HWindow, &msg))
        {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    return WantCancel;
}

void CProgressDlg::FlushDataToControls()
{
    if (HWindow == NULL)
        return;

    if (TextCacheIsDirty)
    {
        SetDlgItemTextW(HWindow, IDT_FILENAME, TextCache.c_str());
        TextCacheIsDirty = FALSE;
    }

    if (ProgressTotalCacheIsDirty)
    {
        if (ProgressTotalCache == -1)
            ::SetWindowTextW(HWindow, Title.c_str());
        else
        {
            const std::wstring caption = SPLFormatStringOwned(
                L"(%d %%) %s", (int)((ProgressTotalCache /*+ 5*/) / 10), Title.c_str());
            ::SetWindowTextW(HWindow, caption.c_str()); // do not round (100% must appear only at 100% and not at 99.5%)
        }

        ProgressBar->SetProgress(ProgressTotalCache, NULL);
        ProgressTotalCacheIsDirty = FALSE;
    }
    else if (ProgressTotalCache == -1)
        ProgressBar->SetProgress(ProgressTotalCache, NULL);
}

INT_PTR
CProgressDlg::DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    CALL_STACK_MESSAGE4("CProgressDlg::DialogProc(0x%X, 0x%IX, 0x%IX)", uMsg, wParam, lParam);
    switch (uMsg)
    {
    case WM_INITDIALOG:
    {
        // use the Salamander progress bar
        ProgressBar = SalamanderGUI->AttachProgressBar(HWindow, IDP_PROGRESSBAR);
        if (ProgressBar == NULL)
        {
            DestroyWindow(HWindow); // error -> do not open the dialog
            return FALSE;           // end of processing
        }

        ::SetWindowTextW(HWindow, Title.c_str());
        SetDlgItemTextW(HWindow, IDT_OPERATION, Operation.c_str());

        break; // request focus from DefDlgProc
    }

    case WM_COMMAND:
    {
        if (LOWORD(wParam) == IDCANCEL)
        {
            if (!WantCancel)
            {
                FlushDataToControls();

                if (SalamanderGeneral->SalMessageBox(HWindow, LangStr(IDS_YESNO_CANCEL).c_str(), TitleWMobile,
                                                     MB_YESNO | MB_ICONQUESTION) == IDYES)
                {
                    WantCancel = TRUE;
                    EnableCancel(FALSE);
                }
            }
            return TRUE;
        }
        break;
    }
    }
    return CCommonDialog::DialogProc(uMsg, wParam, lParam);
}

// ****************************************************************************
// CProgress2Dlg
//

CProgress2Dlg::CProgress2Dlg(HWND parent, const wchar_t* title, const wchar_t* operation, const wchar_t* operation2, CObjectOrigin origin, int resID)
    : CProgressDlg(parent, title, operation, origin, resID ? resID : IDD_PROGRESS2DLG)
{
    ProgressBar2 = NULL;
    TextCache2.clear();
    TextCache2IsDirty = FALSE;

    Operation2 = operation2 != NULL ? operation2 : L"";
}

void CProgress2Dlg::Set(const wchar_t* fileName, const wchar_t* fileName2, BOOL dalayedPaint)
{
    TextCache = fileName != NULL ? fileName : L"";
    TextCache2 = fileName2 != NULL ? fileName2 : L"";
    TextCache2IsDirty = TextCacheIsDirty = TRUE;

    if (!dalayedPaint)
        FlushDataToControls();
}

void CProgress2Dlg::FlushDataToControls()
{
    if (HWindow == NULL)
        return;

    CProgressDlg::FlushDataToControls();

    if (TextCache2IsDirty)
    {
        SetDlgItemTextW(HWindow, IDT_FILENAME2, TextCache2.c_str());
        TextCache2IsDirty = FALSE;
    }

    if (ProgressCacheIsDirty)
    {
        ProgressBar2->SetProgress(ProgressCache, NULL);
        ProgressCacheIsDirty = FALSE;
    }
    else if (ProgressCache == -1)
        ProgressBar2->SetProgress(ProgressCache, NULL);
}

INT_PTR
CProgress2Dlg::DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    CALL_STACK_MESSAGE4("CProgress2Dlg::DialogProc(0x%X, 0x%IX, 0x%IX)", uMsg, wParam, lParam);
    switch (uMsg)
    {
    case WM_INITDIALOG:
    {
        // use the Salamander progress bar
        ProgressBar2 = SalamanderGUI->AttachProgressBar(HWindow, IDP_PROGRESSBAR2);
        if (ProgressBar2 == NULL)
        {
            DestroyWindow(HWindow); // error -> do not open the dialog
            return FALSE;           // end of processing
        }

        SetDlgItemTextW(HWindow, IDT_OPERATION2, Operation2.c_str());

        break; // request focus from DefDlgProc
    }
    }
    return CProgressDlg::DialogProc(uMsg, wParam, lParam);
}

//****************************************************************************
//
// CChangeAttrDialog
//

CChangeAttrDialog::CChangeAttrDialog(HWND parent, CObjectOrigin origin, DWORD attr, DWORD attrDiff,
                                     BOOL selectionContainsDirectory,
                                     const SYSTEMTIME* timeModified,
                                     const SYSTEMTIME* timeCreated,
                                     const SYSTEMTIME* timeAccessed)
    : CCommonDialog(HLanguage, IDD_ATTRIBUTES, IDD_ATTRIBUTES, parent, origin)
{
    SelectionContainsDirectory = selectionContainsDirectory;
    Archive = (attr & FILE_ATTRIBUTE_ARCHIVE) != 0;
    ReadOnly = (attr & FILE_ATTRIBUTE_READONLY) != 0;
    Hidden = (attr & FILE_ATTRIBUTE_HIDDEN) != 0;
    System = (attr & FILE_ATTRIBUTE_SYSTEM) != 0;
    if (attrDiff & FILE_ATTRIBUTE_ARCHIVE)
        Archive = 2;
    if (attrDiff & FILE_ATTRIBUTE_READONLY)
        ReadOnly = 2;
    if (attrDiff & FILE_ATTRIBUTE_HIDDEN)
        Hidden = 2;
    if (attrDiff & FILE_ATTRIBUTE_SYSTEM)
        System = 2;
    ArchiveDirty = FALSE;
    ReadOnlyDirty = FALSE;
    HiddenDirty = FALSE;
    SystemDirty = FALSE;
    RecurseSubDirs = FALSE;
    ChangeTimeModified = FALSE;
    ChangeTimeCreated = FALSE;
    ChangeTimeAccessed = FALSE;
    TimeModified = *timeModified;
    TimeCreated = *timeCreated;
    TimeAccessed = *timeAccessed;
    HModifiedDate = NULL;
    HModifiedTime = NULL;
    HCreatedDate = NULL;
    HCreatedTime = NULL;
    HAccessedDate = NULL;
    HAccessedTime = NULL;
}

BOOL CChangeAttrDialog::GetAndValidateTime(CTransferInfo* ti, int resIDDate, int resIDTime, SYSTEMTIME* time)
{
    HWND hDate = GetDlgItem(HWindow, resIDDate);
    HWND hTime = GetDlgItem(HWindow, resIDTime);

    SYSTEMTIME st;
    SYSTEMTIME st2;
    DateTime_GetSystemtime(hDate, &st);
    DateTime_GetSystemtime(hTime, &st2);
    st2.wYear = st.wYear;
    st2.wMonth = st.wMonth;
    st2.wDayOfWeek = st.wDayOfWeek;
    st2.wDay = st.wDay;
    st2.wMilliseconds = 0;

    FILETIME dummyFT;
    if (!SystemTimeToFileTime(&st2, &dummyFT))
    {
        SalamanderGeneral->SalMessageBox(HWindow, LangStr(IDS_ERR_INVALIDDATEFORMAT).c_str(), TitleWMobileError,
                                         MB_OK | MB_ICONEXCLAMATION);
        ti->ErrorOn(resIDDate);
        return FALSE;
    }
    *time = st2;
    return TRUE;
}

void CChangeAttrDialog::Transfer(CTransferInfo& ti)
{
    ti.CheckBox(IDC_ARCHIVE, Archive);
    ti.CheckBox(IDC_READONLY, ReadOnly);
    ti.CheckBox(IDC_HIDDEN, Hidden);
    ti.CheckBox(IDC_SYSTEM, System);
    ti.CheckBox(IDC_RECURSESUBDIRS, RecurseSubDirs);

    if (ti.Type == ttDataToWindow)
    {
        EnableWindow(GetDlgItem(HWindow, IDC_RECURSESUBDIRS), SelectionContainsDirectory);

        // first populate the dates
        DateTime_SetSystemtime(HModifiedDate, GDT_VALID, &TimeModified);
        DateTime_SetSystemtime(HCreatedDate, GDT_VALID, &TimeCreated);
        DateTime_SetSystemtime(HAccessedDate, GDT_VALID, &TimeAccessed);
        // then set the state to disabled (cannot be done in a single operation)
        DateTime_SetSystemtime(HModifiedDate, GDT_NONE, &TimeModified);
        DateTime_SetSystemtime(HCreatedDate, GDT_NONE, &TimeCreated);
        DateTime_SetSystemtime(HAccessedDate, GDT_NONE, &TimeAccessed);
        // populate the times
        DateTime_SetSystemtime(HModifiedTime, GDT_VALID, &TimeModified);
        DateTime_SetSystemtime(HCreatedTime, GDT_VALID, &TimeCreated);
        DateTime_SetSystemtime(HAccessedTime, GDT_VALID, &TimeAccessed);
        // work around a common controls bug -- if SetFocus is not called,
        // all three controls appear focused
        SetFocus(HModifiedDate);
        SetFocus(HCreatedDate);
        SetFocus(HAccessedDate);
    }
    if (ti.Type == ttDataFromWindow)
    {
        SYSTEMTIME dummy;
        ChangeTimeModified = DateTime_GetSystemtime(HModifiedDate, &dummy) == GDT_VALID;
        ChangeTimeCreated = DateTime_GetSystemtime(HCreatedDate, &dummy) == GDT_VALID;
        ChangeTimeAccessed = DateTime_GetSystemtime(HAccessedDate, &dummy) == GDT_VALID;
        BOOL ret = TRUE;
        if (ret & ChangeTimeModified)
            ret &= GetAndValidateTime(&ti, IDC_ATTR_MODIFIED_DATE, IDC_ATTR_MODIFIED_TIME, &TimeModified);
        if (ret & ChangeTimeCreated)
            ret &= GetAndValidateTime(&ti, IDC_ATTR_CREATED_DATE, IDC_ATTR_CREATED_TIME, &TimeCreated);
        if (ret & ChangeTimeAccessed)
            ret &= GetAndValidateTime(&ti, IDC_ATTR_ACCESSED_DATE, IDC_ATTR_ACCESSED_TIME, &TimeAccessed);
    }

    if (ti.Type == ttDataToWindow)
        EnableWindows();
}

void CChangeAttrDialog::EnableWindows()
{
    SYSTEMTIME st;
    BOOL enabledM = DateTime_GetSystemtime(HModifiedDate, &st) == GDT_VALID;
    EnableWindow(HModifiedTime, enabledM);
    BOOL enabledC = DateTime_GetSystemtime(HCreatedDate, &st) == GDT_VALID;
    EnableWindow(HCreatedTime, enabledC);
    BOOL enabledA = DateTime_GetSystemtime(HAccessedDate, &st) == GDT_VALID;
    EnableWindow(HAccessedTime, enabledA);
    EnableWindow(GetDlgItem(HWindow, IDC_ATTR_CURRENT), enabledM | enabledC | enabledA);
}

INT_PTR
CChangeAttrDialog::DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
    case WM_INITDIALOG:
    {
        HModifiedDate = GetDlgItem(HWindow, IDC_ATTR_MODIFIED_DATE);
        HModifiedTime = GetDlgItem(HWindow, IDC_ATTR_MODIFIED_TIME);
        HCreatedDate = GetDlgItem(HWindow, IDC_ATTR_CREATED_DATE);
        HCreatedTime = GetDlgItem(HWindow, IDC_ATTR_CREATED_TIME);
        HAccessedDate = GetDlgItem(HWindow, IDC_ATTR_ACCESSED_DATE);
        HAccessedTime = GetDlgItem(HWindow, IDC_ATTR_ACCESSED_TIME);
        break;
    }
    case WM_COMMAND:
    {
        if (HIWORD(wParam) == BN_CLICKED)
        {
            switch (LOWORD(wParam))
            {
            case IDC_ARCHIVE:
                ArchiveDirty = TRUE;
                break;
            case IDC_READONLY:
                ReadOnlyDirty = TRUE;
                break;
            case IDC_HIDDEN:
                HiddenDirty = TRUE;
                break;
            case IDC_SYSTEM:
                SystemDirty = TRUE;
                break;

            case IDC_RECURSESUBDIRS:
            {
                // if the user enables Include Subdirs, set the untouched check boxes to grayed,
                // because we do not know what the selected directories contain
                // if the user turns it off again, restore the untouched check boxes to their original state
                BOOL checked = IsDlgButtonChecked(HWindow, IDC_RECURSESUBDIRS) == BST_CHECKED;
                if (!ArchiveDirty)
                    CheckDlgButton(HWindow, IDC_ARCHIVE, checked ? 2 : Archive);
                if (!ReadOnlyDirty)
                    CheckDlgButton(HWindow, IDC_READONLY, checked ? 2 : ReadOnly);
                if (!HiddenDirty)
                    CheckDlgButton(HWindow, IDC_HIDDEN, checked ? 2 : Hidden);
                if (!SystemDirty)
                    CheckDlgButton(HWindow, IDC_SYSTEM, checked ? 2 : System);
                break;
            }
            case IDC_ATTR_CURRENT:
            {
                SYSTEMTIME st;
                GetLocalTime(&st); // fetch the current time

                SYSTEMTIME dummy;
                if (DateTime_GetSystemtime(HModifiedDate, &dummy) == GDT_VALID)
                {
                    DateTime_SetSystemtime(HModifiedDate, GDT_VALID, &st);
                    DateTime_SetSystemtime(HModifiedTime, GDT_VALID, &st);
                }
                if (DateTime_GetSystemtime(HCreatedDate, &dummy) == GDT_VALID)
                {
                    DateTime_SetSystemtime(HCreatedDate, GDT_VALID, &st);
                    DateTime_SetSystemtime(HCreatedTime, GDT_VALID, &st);
                }
                if (DateTime_GetSystemtime(HAccessedDate, &dummy) == GDT_VALID)
                {
                    DateTime_SetSystemtime(HAccessedDate, GDT_VALID, &st);
                    DateTime_SetSystemtime(HAccessedTime, GDT_VALID, &st);
                }
                break;
            }
            }
        }
        break;
    }

    case WM_NOTIFY:
    {
        LPNMHDR nmh = (LPNMHDR)lParam;
        if (nmh->code == DTN_DATETIMECHANGE)
            EnableWindows();
        break;
    }
    }
    return CCommonDialog::DialogProc(uMsg, wParam, lParam);
}
