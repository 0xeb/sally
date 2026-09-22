// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"

#include <limits>

#include "common/CreateDirectoryFlow.h"
#include "common/fsutil.h"

#include "ui/IPrompter.h"
#include "common/unicode/AnsiToolPathPolicy.h"
#include "common/unicode/WideTextRange.h"
#include "common/LocalPathResolution.h"
#include "common/SalGetFullName.h"
#include "common/SalPathWide.h"
#include "common/IFileSystem.h"
#include "common/PluginStringBuffer.h"
#include "menu.h"
#include "cfgdlg.h"
#include "dialogs.h"
#include "darkmode.h"
#include "mainwnd.h"
#include "plugins.h"
#include "filesbox.h"
#include "fileswnd.h"
#include "stswnd.h"
#include "editwnd.h"
#include "zip.h"
#include "cache.h"
#include "viewer.h"
#include "codetbl.h"
#include "shellib.h"
#include "gui.h"
#include "tasklist.h"
#include "olespy.h"
#include "md5.h"
#include "geticon.h"
#include "pack.h"
extern "C"
{
#include "shexreg.h"
}
#include "salshlib.h"
#include "crypt\fileenc.h"
#include "crypt\sha1.h"
#include "pwdmngr.h"

CPackerConfig PackerConfig;
CUnpackerConfig UnpackerConfig;

const wchar_t* STR_NONE = L"(none)";

CSalamanderDirectory GlobalEmptySalDir(FALSE); // returned as an empty sal-dir (instead of NULL) - only for archives

HWND ProgressDialogActivateDrop = NULL;

//
// ****************************************************************************
// CZIPUnpackProgress
//

CZIPUnpackProgress::CZIPUnpackProgress() : CCommonDialog(HLanguage, IDD_ZIPUNPACKPROG, NULL, ooStatic)
{
    Init();
}

void CZIPUnpackProgress::Init()
{
    SetTotal(CQuadWord(0, 0), CQuadWord(0, 0));
    ActualSize = CQuadWord(0, 0);
    ActualSize2 = CQuadWord(0, 0);
    Title = NULL;
    Cancel = FALSE;
    SetRemapNames(NULL, NULL);
    int i;
    for (i = 0; i < ZIP_UNPACK_NUMLINES; i++)
        LinesCache[i][0] = 0;
    CacheIndex = 0;
    CacheIsDirty = FALSE;
    SizeIsDirty = FALSE;
    Size2IsDirty = FALSE;
    LastTickCount = 0;
    FileProgress = FALSE;
    TaskBarList3 = NULL;
}

CZIPUnpackProgress::CZIPUnpackProgress(const wchar_t* title, HWND parent, const CQuadWord& totalSize, CITaskBarList3* taskBarList3)
    : CCommonDialog(HLanguage, IDD_ZIPUNPACKPROG, parent, ooStatic)
{
    SetTotal(totalSize, CQuadWord(0, 0));
    ActualSize = CQuadWord(0, 0);
    ActualSize2 = CQuadWord(0, 0);
    Title = title;
    Cancel = FALSE;
    SetRemapNames(NULL, NULL);
    int i;
    for (i = 0; i < ZIP_UNPACK_NUMLINES; i++)
        LinesCache[i][0] = 0;
    CacheIndex = 0;
    CacheIsDirty = FALSE;
    SizeIsDirty = FALSE;
    Size2IsDirty = FALSE;
    LastTickCount = 0;
    FileProgress = FALSE;
    TaskBarList3 = taskBarList3;
}

void CZIPUnpackProgress::Set(const wchar_t* title, HWND parent, const CQuadWord& totalSize, BOOL fileProgress)
{
    ResID = IDD_ZIPUNPACKPROG;
    SetTotal(totalSize, CQuadWord(0, 0));
    SetParent(parent);
    Title = title;
    ActualSize = CQuadWord(0, 0);
    ActualSize2 = CQuadWord(0, 0);
    FileProgress = fileProgress;
}

void CZIPUnpackProgress::Set(const wchar_t* title, HWND parent, const CQuadWord& totalSize1,
                             const CQuadWord& totalSize2)
{
    ResID = IDD_ZIPUNPACKPROG2;
    SetTotal(totalSize1, totalSize2);
    SetParent(parent);
    Title = title;
    ActualSize = CQuadWord(0, 0);
    ActualSize2 = CQuadWord(0, 0);
    FileProgress = FALSE;
}

void CZIPUnpackProgress::SetTotal(const CQuadWord& total1, const CQuadWord& total2)
{
    if (total1 != CQuadWord(-1, -1))
    {
        TotalSize = max(CQuadWord(1, 0), total1);
        SizeIsDirty = FALSE;
    }
    if (total2 != CQuadWord(-1, -1))
    {
        TotalSize2 = max(CQuadWord(1, 0), total2);
        Size2IsDirty = FALSE;
    }
}

void CZIPUnpackProgress::SetRemapNames(const wchar_t* nameFrom, const wchar_t* nameTo)
{
    RemapNameFrom = nameFrom;
    RemapNameTo = nameTo;
}

void CZIPUnpackProgress::DoRemapNames(wchar_t* txt, int bufLen)
{
    if (RemapNameFrom != NULL && RemapNameTo != NULL)
    {
        wchar_t* s = wcsstr(txt, RemapNameFrom);
        if (s != NULL)
        {
            int len = (int)wcslen(txt);
            int lenFrom = (int)wcslen(RemapNameFrom);
            int lenTo = (int)wcslen(RemapNameTo);
            if (len - lenFrom + lenTo < bufLen)
            {
                memmove(s + lenTo, s + lenFrom, (len - ((s + lenFrom) - txt) + 1) * sizeof(wchar_t));
                memcpy(s, RemapNameTo, lenTo * sizeof(wchar_t));
            }
            else
                TRACE_E("Remap: too long name.");
        }
    }
}

void CZIPUnpackProgress::SetTaskBarList3(CITaskBarList3* taskBarList3)
{
    TaskBarList3 = taskBarList3;
}

void CZIPUnpackProgress::DispatchMessages()
{
    // pump the message queue
    MSG msg;
    while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE))
    {
        if (!IsWindow(HWindow) || !IsDialogMessage(HWindow, &msg))
        {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
}

BOOL CZIPUnpackProgress::HasTwoProgress()
{
    return ResID == IDD_ZIPUNPACKPROG2;
}

int CZIPUnpackProgress::SetSize(const CQuadWord& size1, const CQuadWord& size2, BOOL delayedPaint)
{
    // does the user want to set size1?
    if (size1 != CQuadWord(-1, -1))
    {
        CQuadWord newSize = max(CQuadWord(0, 0), size1);
        if (newSize != ActualSize)
        {
            ActualSize = newSize;
            SizeIsDirty = TRUE;
        }
    }
    // does the user want to set size2?
    if (size2 != CQuadWord(-1, -1))
    {
        CQuadWord newSize = max(CQuadWord(0, 0), size2);
        if (newSize != ActualSize2)
        {
            ActualSize2 = newSize;
            Size2IsDirty = TRUE;
        }
    }
    return AddSize(0, delayedPaint);
}

int CZIPUnpackProgress::AddSize(int size, BOOL delayedPaint)
{
    // time-critical function
    //  CALL_STACK_MESSAGE2("CZIPUnpackProgress::AddSize(%d)", size);

    ActualSize += CQuadWord(size, 0);
    if (size != 0)
        SizeIsDirty = TRUE;

    if (HasTwoProgress())
    {
        ActualSize2 += CQuadWord(size, 0);
        if (size != 0)
            Size2IsDirty = TRUE;
    }

    if (!delayedPaint)
    {
        // should we draw the text immediately
        FlushDataToControls();
    }

    // every 100 ms redraw the changed data (text + progress bars)
    DWORD ticks = GetTickCount();
    if (ticks - LastTickCount > 100)
    {
        LastTickCount = ticks;
        // if we have not repainted a moment ago, do it now
        if (delayedPaint)
            FlushDataToControls();
    }

    DispatchMessages(); // give the user a moment ...

    return !Cancel;
}

void CZIPUnpackProgress::NewLine(const wchar_t* txt, BOOL delayedPaint)
{
    // time-critical function
    //  CALL_STACK_MESSAGE2("CZIPUnpackProgress::NewLine(%s)", txt);
    if (txt == NULL)
        return;

    while (1) // output even multiple lines into the dialog
    {
        while (*txt != 0 && (*txt == L'\r' || *txt == L'\n' || *txt == L' ' || *txt == L'\t'))
            txt++;
        if (*txt == 0)
            break;

        // store it in the cache that we display on WM_TIMER

        // the cache index cycles through the items
        CacheIndex++;
        if (CacheIndex >= ZIP_UNPACK_NUMLINES)
            CacheIndex = 0;

        wchar_t* s = LinesCache[CacheIndex];
        wchar_t* sEnd = s + 300 - 1;
        while (*txt != 0 && *txt != L'\r' && *txt != L'\n') // read one line + convert '/' -> '\\'
        {
            if (*txt == L'/')
            {
                if (s < sEnd)
                    *s++ = L'\\';
                txt++;
            }
            else
            {
                if (s < sEnd)
                    *s++ = *txt++;
                else
                    txt++; // simply ignore the rest of the text (it would not fit in the dialog anyway)
            }
        }
        *s = 0;
        DoRemapNames(LinesCache[CacheIndex], 300);

        // we dirtied the cache
        CacheIsDirty = TRUE;
    }

    if (!delayedPaint)
    {
        // should we draw the text immediately
        FlushDataToControls();
    }

    // every 100 ms redraw the changed data (text + progress bars)
    DWORD ticks = GetTickCount();
    if (ticks - LastTickCount > 100)
    {
        LastTickCount = ticks;
        // if we have not repainted a moment ago, do it now
        if (delayedPaint)
            FlushDataToControls();
    }

    // be careful, do not call here
    DispatchMessages(); // give the user a moment ...
}

void CZIPUnpackProgress::EnableCancel(BOOL enable)
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

            DispatchMessages(); // give the user a moment ...
        }
    }
}

void CZIPUnpackProgress::FlushDataToControls()
{
    // texts
    if (CacheIsDirty)
    {
        int index = CacheIndex;
        int i;
        for (i = ZIP_UNPACK_NUMLINES - 1; i >= 0; i--)
        {
            if (Lines[i] != NULL)
                Lines[i]->SetText(LinesCache[index]);
            index--;
            if (index < 0)
                index = ZIP_UNPACK_NUMLINES - 1;
        }
        CacheIsDirty = FALSE;
    }

    if (TaskBarList3 != NULL)
    {
        if (HasTwoProgress())
        {
            if (Size2IsDirty)
                TaskBarList3->SetProgress2(ActualSize2, TotalSize2);
        }
        else
        {
            if (SizeIsDirty)
                TaskBarList3->SetProgress2(ActualSize, TotalSize);
        }
    }

    // size
    if (SizeIsDirty)
    {
        if (Summary != NULL)
        {
            Summary->SetProgress2(ActualSize, TotalSize);
        }
        SizeIsDirty = FALSE;
    }

    // size2
    if (Size2IsDirty)
    {
        if (Summary2 != NULL)
        {
            Summary2->SetProgress2(ActualSize2, TotalSize2);
        }
        Size2IsDirty = FALSE;
    }
}

INT_PTR
CZIPUnpackProgress::DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
    case WM_INITDIALOG:
    {
        if (ResID == IDD_ZIPUNPACKPROG && FileProgress) // it is necessary to replace the text "Total:" with "File:"
            SetDlgItemTextW(HWindow, IDT_PROGTITLE, LoadStrW(IDS_UNPACKFILEPROGRESS));

        SetWindowTextW(HWindow, Title);
        // the entire object assumes that the allocations may have failed
        int i;
        for (i = 0; i < ZIP_UNPACK_NUMLINES; i++)
        {
            if ((Lines[i] = new CStaticText(HWindow, IDS_ZIPLINE1 + i, STF_PATH_ELLIPSIS | STF_CACHED_PAINT)) == NULL)
                TRACE_E(LOW_MEMORY);
        }
        if ((Summary = new CProgressBar(HWindow, IDC_ZIPSUMMARY)) == NULL)
            TRACE_E(LOW_MEMORY);
        if (HasTwoProgress())
        {
            if ((Summary2 = new CProgressBar(HWindow, IDC_ZIPSUMMARY2)) == NULL)
                TRACE_E(LOW_MEMORY);
        }
        break;
    }

    case WM_DESTROY:
    {
        if (TaskBarList3 != NULL)
            TaskBarList3->SetProgressState(TBPF_NOPROGRESS);
        break;
    }

    case WM_COMMAND:
    {
        // if the user clicked the Cancel button and has not confirmed it earlier, ask again
        if (LOWORD(wParam) == IDCANCEL && !Cancel)
        {
            // the Cancel button must be enabled
            if (IsWindowEnabled(GetDlgItem(HWindow, IDCANCEL)))
            {
                // to avoid repainting under the message box, repaint explicitly now
                FlushDataToControls();

                // ask the user whether they want to abort the operation
                Cancel = (gPrompter->AskYesNo(LoadStrW(IDS_QUESTION), LoadStrW(IDS_CANCELOPERATION)).type == PromptResult::kYes);
            }
        }
        // do not let the command fall through, otherwise the dialog would close
        return TRUE;
    }
    }
    return CCommonDialog::DialogProc(uMsg, wParam, lParam);
}

//
// ****************************************************************************
// CSalamanderGeneral
//

CSalamanderGeneral::CSalamanderGeneral()
{
    Plugin = NULL;
    LanguageModule = NULL;
    HelpFileName.clear();
}

CSalamanderGeneral::~CSalamanderGeneral()
{
    if (LanguageModule != NULL)
    {
        TRACE_E("CSalamanderGeneral::~CSalamanderGeneral(): unexpected situation!");
        HANDLES(FreeLibrary(LanguageModule));
    }
}

void CSalamanderGeneral::Clear()
{
    if (LanguageModule != NULL)
        HANDLES(FreeLibrary(LanguageModule));
    LanguageModule = NULL;
    HelpFileName.clear();
}

// wide. Pure dispatch, so widening it is a matter of pointing at
// SalMessageBoxW - the wide message box - rather than a port.
int CSalamanderGeneral::ShowMessageBox(const wchar_t* text, const wchar_t* title, int type)
{
    if (MainThreadID != GetCurrentThreadId()) // Petr: just close; I do not have the energy to track down every wrong call
        TRACE_E("You can call CSalamanderGeneral::ShowMessageBox() only from main thread!");
    HWND parent = GetMsgBoxParent();
    switch (type)
    {
    case MSGBOX_INFO:
    {
        return SalMessageBoxW(parent, text, title, MB_OK | MB_ICONINFORMATION);
    }

    case MSGBOX_ERROR:
    {
        return SalMessageBoxW(parent, text, title, MB_OK | MB_ICONEXCLAMATION);
    }

    case MSGBOX_EX_ERROR:
    {
        return SalMessageBoxW(parent, text, title, MB_OKCANCEL | MB_ICONEXCLAMATION);
    }

    case MSGBOX_QUESTION:
    {
        return SalMessageBoxW(parent, text, title, MB_YESNO | MB_ICONQUESTION);
    }

    case MSGBOX_EX_QUESTION:
    {
        return SalMessageBoxW(parent, text, title, MB_YESNOCANCEL | MB_ICONQUESTION);
    }

    case MSGBOX_WARNING:
    {
        return SalMessageBoxW(parent, text, title, MB_OK | MB_ICONWARNING);
    }

    case MSGBOX_EX_WARNING:
    {
        return SalMessageBoxW(parent, text, title, MB_YESNOCANCEL | MB_ICONWARNING);
    }

    default:
    {
        TRACE_E("Unknown type of box in CSalamanderGeneral::ShowMessageBox().");
        return 0;
    }
    }
}

int CSalamanderGeneral::SalMessageBox(HWND hParent, LPCWSTR lpText, LPCWSTR lpCaption, UINT uType)
{
    return ::SalMessageBoxW(hParent, lpText, lpCaption, uType);
}

int CSalamanderGeneral::SalMessageBoxEx(const MSGBOXEX_PARAMS* params)
{
    return ::SalMessageBoxEx(params);
}

HWND CSalamanderGeneral::GetMsgBoxParent()
{
    if (MainThreadID != GetCurrentThreadId()) // Petr: just close; I do not have the energy to track down every wrong call
        TRACE_E("You can call CSalamanderGeneral::GetMsgBoxParent() only from main thread!");
    // if the following code should change, it must also be updated in EnterPlugin - so the check keeps working
    return PluginProgressDialog != NULL ? PluginProgressDialog : PluginMsgBoxParent;
}

int DialogError(HWND parent, DWORD flags, const wchar_t* fileName,
                const wchar_t* error, const wchar_t* title)
{
    HWND mainWnd = GetWndToFlash(parent);
    DWORD resID;
    BOOL noSkip;
    switch (flags & BUTTONS_MASK)
    {
    case BUTTONS_OK:
    {
        resID = IDD_ERROR3;
        noSkip = FALSE; // does not apply; something about resID == 0
        break;
    }

    case BUTTONS_RETRYCANCEL:
    {
        resID = 0;
        noSkip = TRUE;
        break;
    }

    case BUTTONS_SKIPCANCEL:
    {
        resID = IDD_ERROR2;
        noSkip = FALSE; // does not apply; something about resID == 0
        break;
    }

    case BUTTONS_RETRYSKIPCANCEL:
    {
        resID = 0;
        noSkip = FALSE;
        break;
    }

    default:
    {
        TRACE_E("CSalamanderGeneral::DialogError: unknow flags=0x" << std::hex << flags << std::dec);
        return DIALOG_FAIL;
    }
    }
    // the name goes in the WIDE slot; the narrow one is unread
    // whenever fileW is non-NULL, which it always is here.
    int res = (int)CFileErrorDlg(parent, title != NULL ? title : ::LoadStrW(IDS_ERRORTITLE),
                                 fileName, error, noSkip, resID)
                  .Execute();
    if (mainWnd != NULL)
        FlashWindow(mainWnd, FALSE);
    switch (res)
    {
    case IDOK:
        return DIALOG_OK;
    case IDRETRY:
        return DIALOG_RETRY;
    case IDB_SKIP:
        return DIALOG_SKIP;
    case IDB_SKIPALL:
        return DIALOG_SKIPALL;
    case IDCANCEL:
        return DIALOG_CANCEL;
    default:
        return DIALOG_FAIL;
    }
}

int CSalamanderGeneral::DialogError(HWND parent, DWORD flags, const wchar_t* fileName,
                                    const wchar_t* error, const wchar_t* title)
{
    if (fileName == NULL || error == NULL)
    {
        TRACE_E("Invalid parametr (fileName == NULL || error == NULL) in CSalamanderGeneral::DialogError!");
        if (fileName == NULL)
            fileName = L"";
        if (error == NULL)
            error = L"";
    }
    // The old AnsiToWide pair is GONE, not moved: the ABI is wide
    // now, so nothing narrows on the way in and nothing is widened back. NOTE the
    // asymmetry that survives on purpose - a NULL 'title' still means "use the
    // standard Error caption", so it is passed through untouched.
    return ::DialogError(parent, flags, fileName, error, title);
}

int DialogOverwrite(HWND parent, DWORD flags, const wchar_t* fileName1, const wchar_t* fileData1,
                    const wchar_t* fileName2, const wchar_t* fileData2)
{
    HWND mainWnd = GetWndToFlash(parent);
    BOOL yesnocancel;
    switch (flags & BUTTONS_MASK)
    {
    case BUTTONS_YESALLSKIPCANCEL:
    {
        yesnocancel = FALSE;
        break;
    }

    case BUTTONS_YESNOCANCEL:
    {
        yesnocancel = TRUE;
        break;
    }

    default:
    {
        TRACE_E("CSalamanderGeneral::DialogOverwrite: unknow flags=0x" << std::hex << flags << std::dec);
        return DIALOG_FAIL;
    }
    }

    // The old AnsiToWide pair is GONE - the ABI is wide, so the
    // attr strings arrive wide. Names go to the WIDE slots; the narrow twins are
    // unread whenever those are non-NULL.
    int res = (int)COverwriteDlg(parent, fileName1, fileData1 != NULL ? fileData1 : L"",
                                 fileName2, fileData2 != NULL ? fileData2 : L"",
                                 yesnocancel, FALSE)
                  .Execute();
    if (mainWnd != NULL)
        FlashWindow(mainWnd, FALSE);
    switch (res)
    {
    case IDYES:
        return DIALOG_YES;
    case IDNO:
        return DIALOG_NO;
    case IDB_ALL:
        return DIALOG_ALL;
    case IDB_SKIP:
        return DIALOG_SKIP;
    case IDB_SKIPALL:
        return DIALOG_SKIPALL;
    case IDCANCEL:
        return DIALOG_CANCEL;
    default:
        return DIALOG_FAIL;
    }
}

int CSalamanderGeneral::DialogOverwrite(HWND parent, DWORD flags, const wchar_t* fileName1, const wchar_t* fileData1,
                                        const wchar_t* fileName2, const wchar_t* fileData2)
{
    return ::DialogOverwrite(parent, flags, fileName1, fileData1, fileName2, fileData2);
}

int DialogQuestion(HWND parent, DWORD flags, const wchar_t* fileName,
                   const wchar_t* question, const wchar_t* title)
{
    HWND mainWnd = GetWndToFlash(parent);
    BOOL yesnocancel, yesallcancel;
    switch (flags & BUTTONS_MASK)
    {
    case BUTTONS_YESALLSKIPCANCEL:
    {
        yesnocancel = FALSE;
        yesallcancel = FALSE;
        break;
    }

    case BUTTONS_YESNOCANCEL:
    {
        yesnocancel = TRUE;
        yesallcancel = FALSE;
        break;
    }

    case BUTTONS_YESALLCANCEL:
    {
        yesnocancel = TRUE;
        yesallcancel = TRUE;
        break;
    }

    default:
    {
        TRACE_E("CSalamanderGeneral::DialogQuestion: unknow flags=0x" << std::hex << flags << std::dec);
        return DIALOG_FAIL;
    }
    }
    int res = (int)CHiddenOrSystemDlg(parent, title != NULL ? title : ::LoadStrW(IDS_QUESTION), fileName,
                                      question, yesnocancel, yesallcancel)
                  .Execute();
    if (mainWnd != NULL)
        FlashWindow(mainWnd, FALSE);
    switch (res)
    {
    case IDYES:
        return DIALOG_YES;
    case IDNO:
        return DIALOG_NO;
    case IDB_ALL:
        return DIALOG_ALL;
    case IDB_SKIP:
        return DIALOG_SKIP;
    case IDB_SKIPALL:
        return DIALOG_SKIPALL;
    case IDCANCEL:
        return DIALOG_CANCEL;
    default:
        return DIALOG_FAIL;
    }
}

int CSalamanderGeneral::DialogQuestion(HWND parent, DWORD flags, const wchar_t* fileName,
                                       const wchar_t* question, const wchar_t* title)
{
    if (fileName == NULL || question == NULL)
    {
        TRACE_E("Invalid parametr (fileName == NULL || question == NULL) in CSalamanderGeneral::DialogQuestion!");
        if (fileName == NULL)
            fileName = L"";
        if (question == NULL)
            question = L"";
    }
    // The task-12 AnsiToWide pair that stood here is GONE, not moved:
    // the ABI itself is wide now, so nothing narrows on the way in and nothing
    // has to be widened back.
    return ::DialogQuestion(parent, flags, fileName, question, title);
}

HWND CSalamanderGeneral::GetMainWindowHWND()
{
    return MainWindow != NULL ? MainWindow->HWindow : NULL;
}

void RestoreFocusInSourcePanel()
{
    if (MainWindow != NULL)
    {
        CFilesWindow* p1 = MainWindow->GetActivePanel();
        if (p1 != NULL)
        {
            if (!MainWindow->EditMode)
                MainWindow->FocusPanel(p1);
            else
            {
                if (MainWindow->EditWindow != NULL && MainWindow->EditWindow->HWindow != NULL)
                    SetFocus(MainWindow->EditWindow->HWindow);
            }
        }
    }
}

void CSalamanderGeneral::RestoreFocusInSourcePanel()
{
    ::RestoreFocusInSourcePanel();
}

BOOL CSalamanderGeneral::CheckAndCreateDirectory(const wchar_t* dir, HWND parent, BOOL quiet,
                                                 CSalamanderStringBuffer* errorText,
                                                 CSalamanderStringBuffer* firstCreatedDir,
                                                 BOOL manualCrDir)
{
    std::wstring error;
    std::wstring firstCreated;
    const BOOL result = ::CheckAndCreateDirectoryOwnedW(
        dir, parent, quiet, errorText != NULL ? &error : NULL,
        firstCreatedDir != NULL ? &firstCreated : NULL, FALSE, manualCrDir);
    if ((errorText != NULL && !sally::plugin_abi::WriteStringBuffer(*errorText, error)) ||
        (firstCreatedDir != NULL &&
         !sally::plugin_abi::WriteStringBuffer(*firstCreatedDir, firstCreated)))
        return FALSE;
    return result;
}

BOOL CSalamanderGeneral::TestFreeSpace(HWND parent, const wchar_t* path, const CQuadWord& totalSize,
                                       const wchar_t* messageTitle)
{
    return ::TestFreeSpace(parent, path, totalSize, messageTitle);
}

void CSalamanderGeneral::GetDiskFreeSpace(CQuadWord* retValue, const wchar_t* path, CQuadWord* total)
{
    if (retValue == NULL)
    {
        TRACE_E("Unexpected situation in CSalamanderGeneral::GetDiskFreeSpace(): retValue is NULL!");
        return;
    }
    *retValue = MyGetDiskFreeSpaceW(path, total);
}

BOOL CSalamanderGeneral::SalGetDiskFreeSpace(const wchar_t* path, LPDWORD lpSectorsPerCluster,
                                             LPDWORD lpBytesPerSector, LPDWORD lpNumberOfFreeClusters,
                                             LPDWORD lpTotalNumberOfClusters)
{
    return MyGetDiskFreeSpaceW(path, lpSectorsPerCluster, lpBytesPerSector,
                               lpNumberOfFreeClusters, lpTotalNumberOfClusters);
}

BOOL CSalamanderGeneral::SalGetVolumeInformation(const wchar_t* path, CSalamanderStringBuffer* rootOrCurReparsePoint,
                                                 CSalamanderStringBuffer* volumeName, LPDWORD lpVolumeSerialNumber,
                                                 LPDWORD lpMaximumComponentLength, LPDWORD lpFileSystemFlags,
                                                 CSalamanderStringBuffer* fileSystemName)
{
    if ((rootOrCurReparsePoint != NULL &&
         !sally::plugin_abi::IsValidStringBuffer(*rootOrCurReparsePoint)) ||
        (volumeName != NULL &&
         !sally::plugin_abi::IsValidStringBuffer(*volumeName)) ||
        (fileSystemName != NULL &&
         !sally::plugin_abi::IsValidStringBuffer(*fileSystemName)) ||
        (rootOrCurReparsePoint != NULL && rootOrCurReparsePoint == volumeName) ||
        (rootOrCurReparsePoint != NULL && rootOrCurReparsePoint == fileSystemName) ||
        (volumeName != NULL && volumeName == fileSystemName))
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    std::wstring rootText;
    std::wstring volumeText;
    std::wstring fileSystemText;
    DWORD stagedSerial = 0;
    DWORD stagedMaximumComponentLength = 0;
    DWORD stagedFlags = 0;
    if (!MyGetVolumeInformationW(
            path, rootOrCurReparsePoint != NULL ? &rootText : NULL, NULL, NULL,
            volumeName != NULL ? &volumeText : NULL,
            lpVolumeSerialNumber != NULL ? &stagedSerial : NULL,
            lpMaximumComponentLength != NULL ? &stagedMaximumComponentLength : NULL,
            lpFileSystemFlags != NULL ? &stagedFlags : NULL,
            fileSystemName != NULL ? &fileSystemText : NULL))
        return FALSE;

    const auto reserve = [](CSalamanderStringBuffer* buffer,
                            const std::wstring& value) -> bool {
        if (buffer == NULL)
            return true;
        if (value.size() >= (std::numeric_limits<DWORD>::max)())
        {
            SetLastError(ERROR_FILENAME_EXCED_RANGE);
            return false;
        }
        return sally::plugin_abi::ReserveStringBuffer(
            *buffer, static_cast<DWORD>(value.size() + 1));
    };
    if (!reserve(rootOrCurReparsePoint, rootText) ||
        !reserve(volumeName, volumeText) ||
        !reserve(fileSystemName, fileSystemText))
        return FALSE;

    const auto publish = [](CSalamanderStringBuffer* buffer,
                            const std::wstring& value) {
        if (buffer != NULL)
        {
            std::wmemmove(buffer->Data, value.c_str(), value.size() + 1);
            buffer->Length = static_cast<DWORD>(value.size());
        }
    };
    publish(rootOrCurReparsePoint, rootText);
    publish(volumeName, volumeText);
    publish(fileSystemName, fileSystemText);
    if (lpVolumeSerialNumber != NULL)
        *lpVolumeSerialNumber = stagedSerial;
    if (lpMaximumComponentLength != NULL)
        *lpMaximumComponentLength = stagedMaximumComponentLength;
    if (lpFileSystemFlags != NULL)
        *lpFileSystemFlags = stagedFlags;
    return TRUE;
}

UINT CSalamanderGeneral::SalGetDriveType(const wchar_t* path)
{
    return MyGetDriveTypeW(path);
}

void CSalamanderGeneral::RemoveTemporaryDir(const wchar_t* dir)
{
    ::RemoveTemporaryDirW(dir);
}

BOOL CSalamanderGeneral::PrepareMask(const wchar_t* src, CSalamanderStringBuffer* mask)
{
    if (mask == NULL)
        return FALSE;
    std::wstring prepared(src != NULL ? src : L"");
    prepared.push_back(L'\0');
    ::PrepareMask(prepared.data(), src != NULL ? src : L"");
    prepared.resize(wcslen(prepared.c_str()));
    return sally::plugin_abi::WriteStringBuffer(*mask, prepared);
}

// native-wide mask matching.
BOOL CSalamanderGeneral::AgreeMask(const wchar_t* filename, const wchar_t* mask, BOOL hasExtension)
{
    return ::AgreeMask(filename, mask, hasExtension, FALSE);
}

BOOL CSalamanderGeneral::MaskName(const wchar_t* name, const wchar_t* mask,
                                 CSalamanderStringBuffer* maskedName)
{
    if (maskedName == NULL || name == NULL)
        return FALSE;
    const std::wstring result = MaskNameOwnedW(name, mask);
    return sally::plugin_abi::WriteStringBuffer(*maskedName, result);
}

BOOL CSalamanderGeneral::PrepareExtMask(const wchar_t* src, CSalamanderStringBuffer* mask)
{
    return PrepareMask(src, mask);
}

// wide; same wide matcher, extendedMode on.
BOOL CSalamanderGeneral::AgreeExtMask(const wchar_t* filename, const wchar_t* mask, BOOL hasExtension)
{
    return ::AgreeMask(filename, mask, hasExtension, TRUE);
}

void* CSalamanderGeneral::Alloc(int size)
{
    return malloc(size);
}

void* CSalamanderGeneral::Realloc(void* ptr, int size)
{
    return realloc(ptr, size);
}

void CSalamanderGeneral::Free(void* ptr)
{
    free(ptr);
}

wchar_t* CSalamanderGeneral::DupStr(const wchar_t* str)
{
    return ::DupStr(str);
}

void CSalamanderGeneral::GetLowerAndUpperCase(unsigned char** lowerCase, unsigned char** upperCase)
{
    CALL_STACK_MESSAGE1("CSalamanderGeneral::GetLowerAndUpperCase(,)");
    if (lowerCase != NULL)
        *lowerCase = LowerCase;
    if (upperCase != NULL)
        *upperCase = UpperCase;
}

BOOL CSalamanderGeneral::ToLowerCase(CSalamanderStringBuffer* text)
{
    CALL_STACK_MESSAGE1("CSalamanderGeneral::ToLowerCase()");
    std::wstring value;
    if (text == NULL ||
        !sally::plugin_abi::ReadStringBuffer(*text, value))
        return FALSE;
    sally::unicode::LowerCaseInPlaceW(value.data());
    return sally::plugin_abi::WriteStringBuffer(*text, value);
}

BOOL CSalamanderGeneral::ToUpperCase(CSalamanderStringBuffer* text)
{
    CALL_STACK_MESSAGE1("CSalamanderGeneral::ToUpperCase()");
    std::wstring value;
    if (text == NULL ||
        !sally::plugin_abi::ReadStringBuffer(*text, value))
        return FALSE;
    sally::unicode::UpperCaseInPlaceW(value.data());
    return sally::plugin_abi::WriteStringBuffer(*text, value);
}

// wide; lengths in WCHARs, -1 = wcslen.
int CSalamanderGeneral::StrCmpEx(const wchar_t* s1, int l1, const wchar_t* s2, int l2)
{
    return ::StrCmpExW(s1, l1, s2, l2);
}

// wide.
BOOL CSalamanderGeneral::StrICpy(const wchar_t* src,
                                CSalamanderStringBuffer* dest)
{
    if (src == NULL || dest == NULL)
        return FALSE;
    std::wstring folded;
    return ::StrICpyW(folded, src) &&
           sally::plugin_abi::WriteStringBuffer(*dest, folded);
}

// wide; the folding internal was added in the previous commit.
int CSalamanderGeneral::StrICmp(const wchar_t* s1, const wchar_t* s2)
{
    return ::StrICmpW(s1, s2);
}

// wide; folds via CompareFolded. Lengths in WCHARs, -1 = wcslen.
int CSalamanderGeneral::StrICmpEx(const wchar_t* s1, int l1, const wchar_t* s2, int l2)
{
    return ::StrICmpExW(s1, l1, s2, l2);
}

// wide; 'n' counts WCHARs, not bytes.
int CSalamanderGeneral::StrNICmp(const wchar_t* s1, const wchar_t* s2, int n)
{
    return ::StrNICmpW(s1, s2, n);
}

int CSalamanderGeneral::MemICmp(const void* buf1, const void* buf2, int n)
{
    return ::MemICmp(buf1, buf2, n);
}

// wide; sort.cpp already defined the wide comparator.
int CSalamanderGeneral::RegSetStrICmp(const wchar_t* s1, const wchar_t* s2)
{
    return ::RegSetStrICmpW(s1, s2);
}

// wide; sort.cpp already defined the wide comparator.
int CSalamanderGeneral::RegSetStrICmpEx(const wchar_t* s1, int l1, const wchar_t* s2, int l2, BOOL* numericalyEqual)
{
    return ::RegSetStrICmpExW(s1, l1, s2, l2, numericalyEqual);
}

// wide; sort.cpp already defined the wide comparator.
int CSalamanderGeneral::RegSetStrCmp(const wchar_t* s1, const wchar_t* s2)
{
    return ::RegSetStrCmpW(s1, s2);
}

// wide; sort.cpp already defined the wide comparator.
int CSalamanderGeneral::RegSetStrCmpEx(const wchar_t* s1, int l1, const wchar_t* s2, int l2, BOOL* numericalyEqual)
{
    return ::RegSetStrCmpExW(s1, l1, s2, l2, numericalyEqual);
}

CFilesWindow*
CSalamanderGeneral::GetPanel(int panel)
{
    return MainWindow->GetPanel(panel);
}

BOOL CSalamanderGeneral::GetPanelPath(int panel, CSalamanderStringBuffer* pathBuffer,
                                     int* type, DWORD* archiveOrFSOffset,
                                     BOOL convertFSPathToExternal)
{
    CALL_STACK_MESSAGE2("CSalamanderGeneral::GetPanelPath(%d, , ,)", panel);
    if (type != NULL)
        *type = 0; // unknown
    if (archiveOrFSOffset != NULL)
        *archiveOrFSOffset = SAL_STRING_BUFFER_NPOS;
    if (MainThreadID != GetCurrentThreadId())
    {
        TRACE_E("You can call CSalamanderGeneral::GetPanelPath() only from main thread!");
        if (type != NULL)
            *type = 0;
        return FALSE;
    }
    CFilesWindow* p = GetPanel(panel);
    if (p != NULL)
    {
        std::wstring path;
        size_t offset = std::wstring::npos;
        if (p->Is(ptZIPArchive))
        {
            if (type != NULL)
                *type = PATH_TYPE_ARCHIVE;
            offset = wcslen(p->GetZIPArchive());
            path = p->GetZIPArchive();
            if (p->GetZIPPath()[0] != 0)
            {
                if (p->GetZIPPath()[0] != '\\')
                    path += L'\\';
                path += p->GetZIPPath();
            }
        }
        else
        {
            if (p->Is(ptPluginFS))
            {
                if (type != NULL)
                    *type = PATH_TYPE_FS;
                offset = wcslen(p->GetPluginFS()->GetPluginFSName());
                std::wstring userPart;
                if (!p->GetPluginFS()->NotEmpty() || !p->GetPluginFS()->GetCurrentPathW(userPart))
                {
                    return FALSE; // error
                }
                if (convertFSPathToExternal)
                {
                    if (!p->GetPluginFS()->GetPluginInterfaceForFS()->ConvertPathToExternalW(
                            p->GetPluginFS()->GetPluginFSName(), p->GetPluginFS()->GetPluginFSNameIndex(), userPart))
                        return FALSE;
                }
                path = p->GetPluginFS()->GetPluginFSName();
                path += L':';
                path += userPart;
            }
            else
            {
                if (p->Is(ptDisk))
                {
                    if (type != NULL)
                        *type = PATH_TYPE_WINDOWS;
                    path = p->GetPathW();
                }
                else
                {
                    TRACE_E("Unexpected situation in CSalamanderGeneral::GetPanelPath()");
                    return FALSE;
                }
            }
        }

        if (pathBuffer != NULL &&
            !sally::plugin_abi::WriteStringBuffer(*pathBuffer, path))
            return FALSE;

        if (archiveOrFSOffset != NULL && offset != std::wstring::npos)
        {
            if (offset > (std::numeric_limits<DWORD>::max)())
            {
                SetLastError(ERROR_FILENAME_EXCED_RANGE);
                return FALSE;
            }
            *archiveOrFSOffset = static_cast<DWORD>(offset);
        }

        SetLastError(ERROR_SUCCESS);
        return TRUE;
    }
    return FALSE;
}

BOOL CSalamanderGeneral::GetLastWindowsPanelPath(
    int panel, CSalamanderStringBuffer* pathBuffer)
{
    CALL_STACK_MESSAGE2("CSalamanderGeneral::GetLastWindowsPanelPath(%d, )", panel);
    if (MainThreadID != GetCurrentThreadId())
    {
        TRACE_E("You can call CSalamanderGeneral::GetLastWindowsPanelPath() only from main thread!");
        return FALSE;
    }
    CFilesWindow* p = GetPanel(panel);
    if (p != NULL && pathBuffer != NULL)
    {
        return sally::plugin_abi::WriteStringBuffer(*pathBuffer,
                                                     p->GetPathW());
    }
    return FALSE;
}

CPluginDataInterfaceAbstract*
CSalamanderGeneral::GetPanelPluginData(int panel)
{
    CALL_STACK_MESSAGE2("CSalamanderGeneral::GetPanelPluginData(%d)", panel);
    if (MainThreadID != GetCurrentThreadId())
    {
        TRACE_E("You can call CSalamanderGeneral::GetPanelPluginData() only from main thread!");
        return NULL;
    }
    CFilesWindow* p = GetPanel(panel);
    if (p != NULL)
    {
        CPluginDataInterfaceAbstract* iface = p->PluginData.GetInterface();
        if (iface != NULL && p->PluginData.GetPluginInterface() != Plugin)
            iface = NULL; // the object is not from this plugin -> it gets nothing
        return iface;
    }
    return NULL;
}

CPluginFSInterfaceAbstract*
CSalamanderGeneral::GetPanelPluginFS(int panel)
{
    CALL_STACK_MESSAGE2("CSalamanderGeneral::GetPanelPluginFS(%d)", panel);
    if (MainThreadID != GetCurrentThreadId())
    {
        TRACE_E("You can call CSalamanderGeneral::GetPanelPluginFS() only from main thread!");
        return NULL;
    }
    CFilesWindow* p = GetPanel(panel);
    if (p != NULL && p->Is(ptPluginFS))
    {
        CPluginFSInterfaceAbstract* iface = p->GetPluginFS()->GetInterface();
        if (iface != NULL && p->GetPluginFS()->GetPluginInterface() != Plugin)
            iface = NULL; // the object is not from this plugin -> it gets nothing
        return iface;
    }
    return NULL;
}

const CFileData*
CSalamanderGeneral::GetPanelFocusedItem(int panel, BOOL* isDir)
{
    CALL_STACK_MESSAGE2("CSalamanderGeneral::GetPanelFocusedItem(%d,)", panel);
    if (MainThreadID != GetCurrentThreadId())
    {
        TRACE_E("You can call CSalamanderGeneral::GetPanelFocusedItem() only from main thread!");
        return NULL;
    }
    CFilesWindow* p = GetPanel(panel);
    if (p != NULL)
    {
        int caret = p->GetCaretIndex();
        if (caret >= 0 && caret < p->Files->Count + p->Dirs->Count)
        {
            if (isDir != NULL)
                *isDir = caret < p->Dirs->Count;
            return (caret < p->Dirs->Count) ? &p->Dirs->At(caret) : &p->Files->At(caret - p->Dirs->Count);
        }
    }
    return NULL;
}

const CFileData*
CSalamanderGeneral::GetPanelItem(int panel, int* index, BOOL* isDir)
{
    CALL_STACK_MESSAGE2("CSalamanderGeneral::GetPanelItem(%d,)", panel);
    if (MainThreadID != GetCurrentThreadId())
    {
        TRACE_E("You can call CSalamanderGeneral::GetPanelItem() only from main thread!");
        return NULL;
    }
    CFilesWindow* p = GetPanel(panel);
    if (p != NULL && index != NULL)
    {
        int i = *index;
        if (i < 0)
            return NULL;                          // enumeration already finished
        if (i < p->Files->Count + p->Dirs->Count) // enumerate more items
        {
            *index = i + 1; // next time move to the following item
            if (isDir != NULL)
                *isDir = i < p->Dirs->Count;
            return (i < p->Dirs->Count) ? &p->Dirs->At(i) : &p->Files->At(i - p->Dirs->Count);
        }
        else
        {
            *index = -1; // end of enumeration
            return NULL;
        }
    }
    return NULL;
}

BOOL CSalamanderGeneral::GetPanelSelection(int panel, int* selectedFiles, int* selectedDirs)
{
    CALL_STACK_MESSAGE2("CSalamanderGeneral::GetPanelSelection(%d, ,)", panel);
    if (MainThreadID != GetCurrentThreadId())
    {
        TRACE_E("You can call CSalamanderGeneral::GetPanelSelection() only from main thread!");
        return FALSE;
    }
    CFilesWindow* p = GetPanel(panel);
    if (p != NULL)
    {
        int count = p->GetSelCount();
        int selDirs = 0;
        if (count > 0)
        {
            CFilesArray* dirs = p->Dirs;
            // count how many directories are selected (the rest of the selected items are files)
            int i;
            for (i = 0; i < dirs->Count; i++) // ".." cannot be selected; the test would be pointless
            {
                if (dirs->At(i).Selected)
                    selDirs++;
            }
        }
        else
            count = 0;

        if (selectedDirs != NULL)
            *selectedDirs = selDirs;
        if (selectedFiles != NULL)
            *selectedFiles = count - selDirs;

        int i = p->GetCaretIndex();
        return p->Dirs->Count + p->Files->Count > 0 && // the panel is not empty
               (i != 0 || count > 0 || p->Dirs->Count == 0 ||
                wcscmp(p->Dirs->At(0).Name, L"..") != 0); // the focus is not on the up-dir, or at least one item is selected
    }
    return FALSE;
}

const CFileData*
CSalamanderGeneral::GetPanelSelectedItem(int panel, int* index, BOOL* isDir)
{
    SLOW_CALL_STACK_MESSAGE2("CSalamanderGeneral::GetPanelSelectedItem(%d, ,)", panel);
    if (MainThreadID != GetCurrentThreadId())
    {
        TRACE_E("You can call CSalamanderGeneral::GetPanelSelectedItem() only from main thread!");
        return NULL;
    }
    CFilesWindow* p = GetPanel(panel);
    if (p != NULL && index != NULL)
    {
        int i = *index;
        if (i < 0)
            return NULL;                             // enumeration already finished
        while (i < p->Files->Count + p->Dirs->Count) // searching for the next selected item
        {
            CFileData* data = (i < p->Dirs->Count) ? &p->Dirs->At(i) : &p->Files->At(i - p->Dirs->Count);
            if (data->Selected) // selected item?
            {
                *index = i + 1; // next time start searching from the following item
                if (isDir != NULL)
                    *isDir = i < p->Dirs->Count;
                return data; // return the found selected item
            }
            i++;
        }
        *index = -1; // end of enumeration; no selected items remain
    }
    return NULL;
}

void CSalamanderGeneral::SelectPanelItem(int panel, const CFileData* file, BOOL select)
{
    CALL_STACK_MESSAGE3("CSalamanderGeneral::SelectPanelItem(%d, , %d)", panel, select);
    if (MainThreadID != GetCurrentThreadId())
    {
        TRACE_E("You can call CSalamanderGeneral::SelectPanelItem() only from main thread!");
        return;
    }
    CFilesWindow* p = GetPanel(panel);
    if (p != NULL)
    {
        int index = -1; // index of 'file' in the panel
        if (p->Dirs->Count > 0)
        {
            CFileData* first = &p->Dirs->At(0);
            CFileData* last = &p->Dirs->At(p->Dirs->Count - 1);
            if (first <= file && file <= last)
                index = (int)(file - first); // it is a directory
        }
        if (index == -1 && p->Files->Count > 0)
        {
            CFileData* first = &p->Files->At(0);
            CFileData* last = &p->Files->At(p->Files->Count - 1);
            if (first <= file && file <= last)
                index = p->Dirs->Count + (int)(file - first); // it is a directory
        }
        if (index != -1)
            p->SetSel(select, index, FALSE); // change selection
        else
            TRACE_E("Invalid parameter 'file' in CSalamanderGeneral::SelectPanelItem().");
    }
}

void CSalamanderGeneral::RepaintChangedItems(int panel)
{
    CALL_STACK_MESSAGE2("CSalamanderGeneral::RepaintChangedItems(%d)", panel);
    if (MainThreadID != GetCurrentThreadId())
    {
        TRACE_E("You can call CSalamanderGeneral::RepaintChangedItems() only from main thread!");
        return;
    }
    CFilesWindow* p = GetPanel(panel);
    if (p != NULL)
    {
        p->RepaintListBox(DRAWFLAG_DIRTY_ONLY | DRAWFLAG_SKIP_VISTEST);
        PostMessage(p->HWindow, WM_USER_SELCHANGED, 0, 0); // sel-change notify
    }
}

void CSalamanderGeneral::SelectAllPanelItems(int panel, BOOL select, BOOL repaint)
{
    CALL_STACK_MESSAGE4("CSalamanderGeneral::SelectAllPanelItems(%d, %d, %d)", panel, select, repaint);
    if (MainThreadID != GetCurrentThreadId())
    {
        TRACE_E("You can call CSalamanderGeneral::SelectAllPanelItems() only from main thread!");
        return;
    }
    CFilesWindow* p = GetPanel(panel);
    if (p != NULL)
    {
        p->SetSel(select, -1, repaint); // change selection
        if (repaint)
            PostMessage(p->HWindow, WM_USER_SELCHANGED, 0, 0); // sel-change notify
    }
}

void CSalamanderGeneral::SetPanelFocusedItem(int panel, const CFileData* file, BOOL partVis)
{
    CALL_STACK_MESSAGE3("CSalamanderGeneral::SetPanelFocusedItem(%d, , %d)", panel, partVis);
    if (MainThreadID != GetCurrentThreadId())
    {
        TRACE_E("You can call CSalamanderGeneral::SetPanelFocusedItem() only from main thread!");
        return;
    }
    CFilesWindow* p = GetPanel(panel);
    if (p != NULL)
    {
        int index = -1; // index of 'file' in the panel
        if (p->Dirs->Count > 0)
        {
            CFileData* first = &p->Dirs->At(0);
            CFileData* last = &p->Dirs->At(p->Dirs->Count - 1);
            if (first <= file && file <= last)
                index = (int)(file - first); // it is a directory
        }
        if (index == -1 && p->Files->Count > 0)
        {
            CFileData* first = &p->Files->At(0);
            CFileData* last = &p->Files->At(p->Files->Count - 1);
            if (first <= file && file <= last)
                index = p->Dirs->Count + (int)(file - first); // it is a directory
        }
        if (index != -1)
            p->SetCaretIndex(index, partVis); // change focus
        else
            TRACE_E("Invalid parameter 'file' in CSalamanderGeneral::SetPanelFocusedItem().");
    }
}

BOOL CSalamanderGeneral::GetFilterFromPanel(int panel,
                                            CSalamanderStringBuffer* masks)
{
    CALL_STACK_MESSAGE2("CSalamanderGeneral::GetFilterFromPanel(%d, )", panel);
    if (MainThreadID != GetCurrentThreadId())
    {
        TRACE_E("You can call CSalamanderGeneral::GetFilterFromPanel() only from main thread!");
        return FALSE;
    }
    CFilesWindow* p = GetPanel(panel);
    BOOL ret = FALSE;
    if (p != NULL && p->FilterEnabled && masks != NULL)
    {
        ret = sally::plugin_abi::WriteStringBuffer(
            *masks, p->Filter.GetMasksString());
    }
    return ret;
}

// returns the position of the source panel (is it on the left or on the right?), returns PANEL_LEFT or PANEL_RIGHT
int CSalamanderGeneral::GetSourcePanel()
{
    if (MainThreadID != GetCurrentThreadId())
    {
        TRACE_E("You can call CSalamanderGeneral::GetSourcePanel() only from main thread!");
        return PANEL_LEFT;
    }
    if (MainWindow->GetActivePanel() == MainWindow->LeftPanel)
        return PANEL_LEFT;
    else
        return PANEL_RIGHT;
}

// activates the other panel (like the TAB key); panels marked through PANEL_SOURCE and PANEL_TARGET
// swap naturally as a result
void CSalamanderGeneral::ChangePanel()
{
    if (MainThreadID != GetCurrentThreadId())
    {
        TRACE_E("You can call CSalamanderGeneral::ChangePanel() only from main thread!");
        return;
    }
    MainWindow->ChangePanel();
}

void CSalamanderGeneral::SkipOneActivateRefresh()
{
    ::SkipOneActivateRefresh = TRUE;
    PostMessage(MainWindow->HWindow, WM_USER_SKIPONEREFRESH, 0, 0);
}

BOOL CSalamanderGeneral::SalGetTempFileName(const wchar_t* path, const wchar_t* prefix,
                                            CSalamanderStringBuffer* tmpName, BOOL file, DWORD* err)
{
    CALL_STACK_MESSAGE1("CSalamanderGeneral::SalGetTempFileName()");
    if (tmpName == NULL || !sally::plugin_abi::IsValidStringBuffer(*tmpName))
    {
        if (err != NULL)
            *err = ERROR_INVALID_PARAMETER;
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    SetLastError(NO_ERROR);
    const std::wstring result = ::SalGetTempFileNameW(path, prefix, file != FALSE);
    if (result.empty())
    {
        if (err != NULL)
            *err = GetLastError();
        return FALSE;
    }

    if (!sally::plugin_abi::WriteStringBuffer(*tmpName, result))
    {
        const DWORD publicationError = GetLastError();
        if (file)
            gFileSystem->DeleteFile(result.c_str());
        else
            SalLPRemoveDirectory(result.c_str());
        if (err != NULL)
            *err = publicationError;
        return FALSE;
    }

    if (err != NULL)
        *err = NO_ERROR;
    return TRUE;
}

BOOL CSalamanderGeneral::NumberToStr(const CQuadWord& number, CSalamanderStringBuffer* text)
{
    return text != NULL &&
           sally::plugin_abi::WriteStringBuffer(*text, ::NumberToStr(number));
}

BOOL CSalamanderGeneral::PrintDiskSize(const CQuadWord& size, int mode,
                                      CSalamanderStringBuffer* text)
{
    return text != NULL &&
           sally::plugin_abi::WriteStringBuffer(*text, ::PrintDiskSize(size, mode));
}

BOOL CSalamanderGeneral::PrintTimeLeft(const CQuadWord& secs,
                                      CSalamanderStringBuffer* text)
{
    return text != NULL &&
           sally::plugin_abi::WriteStringBuffer(*text, ::PrintTimeLeft(secs));
}

// wide: forwards to the wide internal that already existed.
BOOL CSalamanderGeneral::HasTheSameRootPath(const wchar_t* path1, const wchar_t* path2)
{
    return ::HasTheSameRootPath(path1, path2);
}

// wide; the internal was ported in the same commit.
int CSalamanderGeneral::CommonPrefixLength(const wchar_t* path1, const wchar_t* path2)
{
    return ::CommonPrefixLength(path1, path2);
}

// wide; builds on CommonPrefixLength.
BOOL CSalamanderGeneral::PathIsPrefix(const wchar_t* prefix, const wchar_t* path)
{
    return ::SalPathIsPrefix(prefix, path);
}

// wide: forwards to the wide internal that already existed.
BOOL CSalamanderGeneral::IsTheSamePath(const wchar_t* path1, const wchar_t* path2)
{
    return ::IsTheSamePath(path1, path2);
}

BOOL CSalamanderGeneral::GetRootPath(const wchar_t* path,
                                     CSalamanderStringBuffer* root)
{
    const std::wstring full = ::GetRootPath(path);
    return root != NULL && !full.empty() &&
           sally::plugin_abi::WriteStringBuffer(*root, full);
}

BOOL CSalamanderGeneral::CutDirectory(CSalamanderStringBuffer* path,
                                     CSalamanderStringBuffer* cutDir)
{
    if (path == NULL)
        return FALSE;
    std::wstring pathText;
    if (!sally::plugin_abi::ReadStringBuffer(*path, pathText))
        return FALSE;
    std::vector<wchar_t> buffer(pathText.begin(), pathText.end());
    buffer.push_back(L'\0');
    wchar_t* cut = NULL;
    if (!::CutDirectory(buffer.data(), cutDir != NULL ? &cut : NULL))
        return FALSE;
    pathText.assign(buffer.data());
    const std::wstring cutText = cut != NULL ? std::wstring(cut) : std::wstring();
    if ((cutDir != NULL && !sally::plugin_abi::WriteStringBuffer(*cutDir, cutText)) ||
        !sally::plugin_abi::WriteStringBuffer(*path, pathText))
        return FALSE;
    return TRUE;
}

BOOL CSalamanderGeneral::SalPathAppend(CSalamanderStringBuffer* path,
                                      const wchar_t* name)
{
    if (path == NULL || name == NULL)
        return FALSE;
    std::wstring pathText;
    if (!sally::plugin_abi::ReadStringBuffer(*path, pathText))
        return FALSE;
    SPLSalPathAppendOwned(pathText, name);
    return sally::plugin_abi::WriteStringBuffer(*path, pathText);
}

BOOL CSalamanderGeneral::SalPathAddBackslash(CSalamanderStringBuffer* path)
{
    if (path == NULL)
        return FALSE;
    std::wstring pathText;
    if (!sally::plugin_abi::ReadStringBuffer(*path, pathText))
        return FALSE;
    SPLSalPathAddBackslashOwned(pathText);
    return sally::plugin_abi::WriteStringBuffer(*path, pathText);
}

BOOL CSalamanderGeneral::SalPathRemoveBackslash(CSalamanderStringBuffer* path)
{
    if (path == NULL)
        return FALSE;
    std::wstring pathText;
    if (!sally::plugin_abi::ReadStringBuffer(*path, pathText))
        return FALSE;
    if (!pathText.empty() && pathText.back() == L'\\')
        pathText.pop_back();
    return sally::plugin_abi::WriteStringBuffer(*path, pathText);
}

BOOL CSalamanderGeneral::SalPathStripPath(CSalamanderStringBuffer* path)
{
    if (path == NULL)
        return FALSE;
    std::wstring pathText;
    if (!sally::plugin_abi::ReadStringBuffer(*path, pathText))
        return FALSE;
    ::SalPathStripPathW(pathText);
    return sally::plugin_abi::WriteStringBuffer(*path, pathText);
}

BOOL CSalamanderGeneral::SalPathRemoveExtension(CSalamanderStringBuffer* path)
{
    if (path == NULL)
        return FALSE;
    std::wstring pathText;
    if (!sally::plugin_abi::ReadStringBuffer(*path, pathText))
        return FALSE;
    ::SalPathRemoveExtensionW(pathText);
    return sally::plugin_abi::WriteStringBuffer(*path, pathText);
}

BOOL CSalamanderGeneral::SalPathAddExtension(CSalamanderStringBuffer* path,
                                            const wchar_t* extension)
{
    if (path == NULL || extension == NULL)
        return FALSE;
    std::wstring pathText;
    if (!sally::plugin_abi::ReadStringBuffer(*path, pathText))
        return FALSE;
    if (!::SalPathAddExtensionW(pathText, extension))
        return FALSE;
    return sally::plugin_abi::WriteStringBuffer(*path, pathText);
}

BOOL CSalamanderGeneral::SalPathRenameExtension(CSalamanderStringBuffer* path,
                                               const wchar_t* extension)
{
    if (path == NULL || extension == NULL)
        return FALSE;
    std::wstring pathText;
    if (!sally::plugin_abi::ReadStringBuffer(*path, pathText))
        return FALSE;
    if (!::SalPathRenameExtensionW(pathText, extension))
        return FALSE;
    return sally::plugin_abi::WriteStringBuffer(*path, pathText);
}

const wchar_t*
CSalamanderGeneral::SalPathFindFileName(const wchar_t* path)
{
    return ::SalPathFindFileNameW(path);
}

BOOL CSalamanderGeneral::SalGetFullName(CSalamanderStringBuffer* name,
                                        int* errTextID, const wchar_t* curDir,
                                        CSalamanderStringBuffer* nextFocus)
{
    if (MainThreadID != GetCurrentThreadId())
    {
        TRACE_E("You can call CSalamanderGeneral::SalGetFullName() only from main thread!");
        if (errTextID != NULL)
            *errTextID = GFN_PATHISINVALID;
        return FALSE;
    }
    if (name == NULL)
        return FALSE;

    std::wstring nameW;
    if (!sally::plugin_abi::ReadStringBuffer(*name, nameW) ||
        (nextFocus != NULL && !sally::plugin_abi::IsValidStringBuffer(*nextFocus)))
        return FALSE;
    std::wstring focusW;
    BOOL ret = ::SalGetFullNameW(nameW, errTextID, curDir,
                                 nextFocus != NULL ? &focusW : NULL, NULL, FALSE);
    if (ret)
    {
        if (nameW.size() >= (std::numeric_limits<DWORD>::max)() ||
            focusW.size() >= (std::numeric_limits<DWORD>::max)())
        {
            SetLastError(ERROR_FILENAME_EXCED_RANGE);
            ret = FALSE;
        }
        else
        {
            const bool reserved =
                sally::plugin_abi::ReserveStringBuffer(
                    *name, static_cast<DWORD>(nameW.size() + 1)) &&
                (nextFocus == NULL || sally::plugin_abi::ReserveStringBuffer(
                                          *nextFocus, static_cast<DWORD>(focusW.size() + 1)));
            if (!reserved)
                ret = FALSE;
            else
            {
                if (nextFocus != NULL)
                    sally::plugin_abi::WriteStringBuffer(*nextFocus, focusW);
                sally::plugin_abi::WriteStringBuffer(*name, nameW);
            }
        }
    }
    if (errTextID != NULL)
    {
        switch (*errTextID)
        {
        case IDS_SERVERNAMEMISSING:
            *errTextID = GFN_SERVERNAMEMISSING;
            break;
        case IDS_SHARENAMEMISSING:
            *errTextID = GFN_SHARENAMEMISSING;
            break;
        case IDS_TOOLONGPATH:
            *errTextID = GFN_TOOLONGPATH;
            break;
        case IDS_INVALIDDRIVE:
            *errTextID = GFN_INVALIDDRIVE;
            break;
        case IDS_INCOMLETEFILENAME:
            *errTextID = GFN_INCOMLETEFILENAME;
            break;
        case IDS_EMPTYNAMENOTALLOWED:
            *errTextID = GFN_EMPTYNAMENOTALLOWED;
            break;
        case IDS_PATHISINVALID:
            *errTextID = GFN_PATHISINVALID;
            break;
        }
    }

    return ret;
}

void CSalamanderGeneral::SalUpdateDefaultDir(BOOL activePrefered)
{
    if (MainThreadID != GetCurrentThreadId())
    {
        TRACE_E("You can call CSalamanderGeneral::SalUpdateDefaultDir() only from main thread!");
        return;
    }
    if (MainWindow != NULL)
        MainWindow->UpdateDefaultDir(activePrefered);
}

BOOL CSalamanderGeneral::GetGFNErrorText(int GFN,
                                         CSalamanderStringBuffer* text)
{
    const wchar_t* s = NULL;
    switch (GFN)
    {
    case GFN_SERVERNAMEMISSING:
        s = ::LoadStrW(IDS_SERVERNAMEMISSING);
        break;
    case GFN_SHARENAMEMISSING:
        s = ::LoadStrW(IDS_SHARENAMEMISSING);
        break;
    case GFN_TOOLONGPATH:
        s = ::LoadStrW(IDS_TOOLONGPATH);
        break;
    case GFN_INVALIDDRIVE:
        s = ::LoadStrW(IDS_INVALIDDRIVE);
        break;
    case GFN_INCOMLETEFILENAME:
        s = ::LoadStrW(IDS_INCOMLETEFILENAME);
        break;
    case GFN_EMPTYNAMENOTALLOWED:
        s = ::LoadStrW(IDS_EMPTYNAMENOTALLOWED);
        break;
    case GFN_PATHISINVALID:
        s = ::LoadStrW(IDS_PATHISINVALID);
        break;
    }
    return text != NULL && sally::plugin_abi::WriteStringBuffer(
                               *text, s != NULL ? std::wstring(s) : std::wstring());
}

BOOL CSalamanderGeneral::GetErrorText(int err, CSalamanderStringBuffer* text)
{
    return text != NULL && sally::plugin_abi::WriteStringBuffer(
                               *text, ::GetErrorTextOwned(err));
}

// v108: wide is the primary. The former narrow LoadStr and its
// LoadStrW sibling collapsed into this one wide entry point.
BOOL CSalamanderGeneral::LoadStr(HINSTANCE module, int resID,
                                CSalamanderStringBuffer* text)
{
    if (module == NULL || text == NULL)
    {
        TRACE_E("CSalamanderGeneral::LoadStr(): module == NULL");
        return FALSE;
    }
    return sally::plugin_abi::WriteStringBuffer(
        *text, ::LoadStrOwned(resID, module));
}

COLORREF
CSalamanderGeneral::GetCurrentColor(int color)
{
    int index;
    SALCOLOR* arr = CurrentColors;
    switch (color)
    {
    // CurrentColors
    case SALCOL_FOCUS_ACTIVE_NORMAL:
        index = FOCUS_ACTIVE_NORMAL;
        break;
    case SALCOL_FOCUS_ACTIVE_SELECTED:
        index = FOCUS_ACTIVE_SELECTED;
        break;
    case SALCOL_FOCUS_FG_INACTIVE_NORMAL:
        index = FOCUS_FG_INACTIVE_NORMAL;
        break;
    case SALCOL_FOCUS_FG_INACTIVE_SELECTED:
        index = FOCUS_FG_INACTIVE_SELECTED;
        break;
    case SALCOL_FOCUS_BK_INACTIVE_NORMAL:
        index = FOCUS_BK_INACTIVE_NORMAL;
        break;
    case SALCOL_FOCUS_BK_INACTIVE_SELECTED:
        index = FOCUS_BK_INACTIVE_SELECTED;
        break;
    case SALCOL_ITEM_FG_NORMAL:
        index = ITEM_FG_NORMAL;
        break;
    case SALCOL_ITEM_FG_SELECTED:
        index = ITEM_FG_SELECTED;
        break;
    case SALCOL_ITEM_FG_FOCUSED:
        index = ITEM_FG_FOCUSED;
        break;
    case SALCOL_ITEM_FG_FOCSEL:
        index = ITEM_FG_FOCSEL;
        break;
    case SALCOL_ITEM_FG_HIGHLIGHT:
        index = ITEM_FG_HIGHLIGHT;
        break;
    case SALCOL_ITEM_BK_NORMAL:
        index = ITEM_BK_NORMAL;
        break;
    case SALCOL_ITEM_BK_SELECTED:
        index = ITEM_BK_SELECTED;
        break;
    case SALCOL_ITEM_BK_FOCUSED:
        index = ITEM_BK_FOCUSED;
        break;
    case SALCOL_ITEM_BK_FOCSEL:
        index = ITEM_BK_FOCSEL;
        break;
    case SALCOL_ITEM_BK_HIGHLIGHT:
        index = ITEM_BK_HIGHLIGHT;
        break;
    case SALCOL_ICON_BLEND_SELECTED:
        index = ICON_BLEND_SELECTED;
        break;
    case SALCOL_ICON_BLEND_FOCUSED:
        index = ICON_BLEND_FOCUSED;
        break;
    case SALCOL_ICON_BLEND_FOCSEL:
        index = ICON_BLEND_FOCSEL;
        break;
    case SALCOL_PROGRESS_FG_NORMAL:
        index = PROGRESS_FG_NORMAL;
        break;
    case SALCOL_PROGRESS_FG_SELECTED:
        index = PROGRESS_FG_SELECTED;
        break;
    case SALCOL_PROGRESS_BK_NORMAL:
        index = PROGRESS_BK_NORMAL;
        break;
    case SALCOL_PROGRESS_BK_SELECTED:
        index = PROGRESS_BK_SELECTED;
        break;
    case SALCOL_HOT_PANEL:
        index = HOT_PANEL;
        break;
    case SALCOL_HOT_ACTIVE:
        index = HOT_ACTIVE;
        break;
    case SALCOL_HOT_INACTIVE:
        index = HOT_INACTIVE;
        break;
    case SALCOL_ACTIVE_CAPTION_FG:
        index = ACTIVE_CAPTION_FG;
        break;
    case SALCOL_ACTIVE_CAPTION_BK:
        index = ACTIVE_CAPTION_BK;
        break;
    case SALCOL_INACTIVE_CAPTION_FG:
        index = INACTIVE_CAPTION_FG;
        break;
    case SALCOL_INACTIVE_CAPTION_BK:
        index = INACTIVE_CAPTION_BK;
        break;
    case SALCOL_THUMBNAIL_NORMAL:
        index = THUMBNAIL_FRAME_NORMAL;
        break;
    case SALCOL_THUMBNAIL_SELECTED:
        index = THUMBNAIL_FRAME_FOCUSED;
        break;
    case SALCOL_THUMBNAIL_FOCUSED:
        index = THUMBNAIL_FRAME_SELECTED;
        break;
    case SALCOL_THUMBNAIL_FOCSEL:
        index = THUMBNAIL_FRAME_FOCSEL;
        break;
    // ViewerColors
    case SALCOL_VIEWER_FG_NORMAL:
        index = VIEWER_FG_NORMAL;
        arr = ViewerColors;
        break;
    case SALCOL_VIEWER_BK_NORMAL:
        index = VIEWER_BK_NORMAL;
        arr = ViewerColors;
        break;
    case SALCOL_VIEWER_FG_SELECTED:
        index = VIEWER_FG_SELECTED;
        arr = ViewerColors;
        break;
    case SALCOL_VIEWER_BK_SELECTED:
        index = VIEWER_BK_SELECTED;
        arr = ViewerColors;
        break;

    default:
    {
        TRACE_E("Invalid color constant!");
        return COLORREF(0);
    }
    }
    return GetCOLORREF(arr[index]);
}

BOOL CSalamanderGeneral::GetPluginFSName(CSalamanderStringBuffer* name,
                                         int fsNameIndex)
{
    CALL_STACK_MESSAGE2("CSalamanderGeneral::GetPluginFSName(, %d)", fsNameIndex);
    if (MainThreadID != GetCurrentThreadId())
    {
        TRACE_E("You can call CSalamanderGeneral::GetPluginFSName() only from main thread!");
        return FALSE;
    }
    CPluginData* data = Plugins.GetPluginData(Plugin);
    if (data != NULL && data->SupportFS && fsNameIndex >= 0 && fsNameIndex < (int)data->FSNames.size())
        return name != NULL && sally::plugin_abi::WriteStringBuffer(
                                   *name, data->FSNames[fsNameIndex]);
    else
    {
        TRACE_E("CSalamanderGeneral::GetPluginFSName(): incorrect call (not supporting FS or 'fsNameIndex' is out of range)!");
        return FALSE;
    }
}

BOOL CSalamanderGeneral::SetFlagLoadOnSalamanderStart(BOOL start)
{
    CALL_STACK_MESSAGE2("CSalamanderGeneral::SetFlagLoadOnSalamanderStart(%d)", start);
    if (MainThreadID != GetCurrentThreadId())
    {
        TRACE_E("You can call CSalamanderGeneral::SetFlagLoadOnSalamanderStart() only from main thread!");
        return FALSE;
    }
    CPluginData* data = Plugins.GetPluginData(Plugin);
    if (data != NULL)
    {
        BOOL prev = data->LoadOnStart != 0;
        data->LoadOnStart = start != 0;
        return prev;
    }
    else
    {
        TRACE_E("Unexpected situation in CSalamanderGeneral::SetFlagLoadOnSalamanderStart().");
        return FALSE;
    }
}

void CSalamanderGeneral::PostUnloadThisPlugin()
{
    CALL_STACK_MESSAGE1("CSalamanderGeneral::PostUnloadThisPlugin()");
    if (MainThreadID == GetCurrentThreadId())
    { // because of calls from the entry point where Plugin is set to -1 (just to look up plugin data)
        // before WM_USER_POSTCMDORUNLOADPLUGIN would arrive, Plugin would be reset (according to the entry point's return value)
        CPluginData* data = Plugins.GetPluginData(Plugin);
        if (data != NULL)
        {
            data->ShouldUnload = TRUE;
            ExecCmdsOrUnloadMarkedPlugins = TRUE;
        }
        else
        {
            TRACE_E("Unexpected situation in CSalamanderGeneral::PostUnloadThisPlugin().");
        }
    }
    else // outside the entry point the Plugin is certainly set...
    {
        if (MainWindow != NULL && MainWindow->HWindow != NULL)
        {
            // check for a call while the entry point is starting (Plugin is set to -1)
            if ((INT_PTR)Plugin == -1)
            {
                TRACE_E("You can call CSalamanderGeneral::PostUnloadThisPlugin only from main "
                        "thread when plugin entry-point is not finished yet!");
            }
            else
            {
                PostMessage(MainWindow->HWindow, WM_USER_POSTCMDORUNLOADPLUGIN, (WPARAM)Plugin, 0);
            }
        }
        else
        {
            TRACE_E("Unexpected situation (2) in CSalamanderGeneral::PostUnloadThisPlugin().");
        }
    }
}

void CSalamanderGeneral::PostPluginMenuChanged()
{
    CALL_STACK_MESSAGE1("CSalamanderGeneral::PostPluginMenuChanged()");
    if (MainThreadID == GetCurrentThreadId())
    { // because of calls from the entry point where Plugin is set to -1 (just to look up plugin data)
        // before WM_USER_POSTCMDORUNLOADPLUGIN would arrive, Plugin would be reset (according to the entry point's return value)
        CPluginData* data = Plugins.GetPluginData(Plugin);
        if (data != NULL)
        {
            data->ShouldRebuildMenu = TRUE;
            ExecCmdsOrUnloadMarkedPlugins = TRUE;
        }
        else
        {
            TRACE_E("Unexpected situation in CSalamanderGeneral::PostPluginMenuChanged().");
        }
    }
    else // outside the entry point the Plugin is certainly set...
    {
        if (MainWindow != NULL && MainWindow->HWindow != NULL)
        {
            // check for a call while the entry point is starting (Plugin is set to -1)
            if ((INT_PTR)Plugin == -1)
            {
                TRACE_E("You can call CSalamanderGeneral::PostPluginMenuChanged only from main "
                        "thread when plugin entry-point is not finished yet!");
            }
            else
            {
                PostMessage(MainWindow->HWindow, WM_USER_POSTCMDORUNLOADPLUGIN, (WPARAM)Plugin, 1);
            }
        }
        else
        {
            TRACE_E("Unexpected situation (2) in CSalamanderGeneral::PostPluginMenuChanged().");
        }
    }
}

void CSalamanderGeneral::PostMenuExtCommand(int id, BOOL waitForSalIdle)
{
    CALL_STACK_MESSAGE3("CSalamanderGeneral::PostMenuExtCommand(%d, %d)", id, waitForSalIdle);
    if (waitForSalIdle)
    {
        if (id < 0 || id >= 1000000)
        {
            TRACE_E("CSalamanderGeneral::PostMenuExtCommand: id is invalid (" << id << " is not in range 0-999999).");
            return;
        }

        if (MainThreadID == GetCurrentThreadId())
        { // because of calls from the entry point where Plugin is set to -1 (just to look up plugin data)
            // before WM_USER_POSTCMDORUNLOADPLUGIN would arrive, Plugin would be reset (according to the entry point's return value)
            CPluginData* data = Plugins.GetPluginData(Plugin);
            if (data != NULL)
            {
                data->Commands.Add(500 + id); // salCmd values are in the <0, 499> range; 500 is the first free number
                ExecCmdsOrUnloadMarkedPlugins = TRUE;
            }
            else
            {
                TRACE_E("Unexpected situation in CSalamanderGeneral::PostMenuExtCommand().");
            }
        }
        else // outside the entry point the Plugin is certainly set...
        {
            if (MainWindow != NULL && MainWindow->HWindow != NULL)
            {
                // check for a call while the entry point is starting (Plugin is set to -1)
                if ((INT_PTR)Plugin == -1)
                {
                    TRACE_E("You can call CSalamanderGeneral::PostMenuExtCommand only from main "
                            "thread when plugin entry-point is not finished yet!");
                }
                else
                { // 0 - unload, 1 - rebuild menu, 2-501 salCmd, 502-1000501 menuCmd
                    PostMessage(MainWindow->HWindow, WM_USER_POSTCMDORUNLOADPLUGIN, (WPARAM)Plugin, 502 + id);
                }
            }
            else
            {
                TRACE_E("Unexpected situation (2) in CSalamanderGeneral::PostMenuExtCommand().");
            }
        }
    }
    else
    {
        if (MainThreadID == GetCurrentThreadId())
        { // check for a call from the entry point (Plugin is set to -1)
            if ((INT_PTR)Plugin == -1)
            {
                TRACE_E("You may not call CSalamanderGeneral::PostMenuExtCommand from entry-point!");
                return;
            }
        }
        else
        {
            // check for a call while the entry point is starting (Plugin is set to -1)
            if ((INT_PTR)Plugin == -1)
            {
                TRACE_E("You may not call CSalamanderGeneral::PostMenuExtCommand when "
                        "entry-point is not finished yet!");
                return;
            }
        }
        if (MainWindow != NULL && MainWindow->HWindow != NULL)
        {
            PostMessage(MainWindow->HWindow, WM_USER_POSTMENUEXTCMD, (WPARAM)Plugin, (LPARAM)id);
        }
        else
        {
            TRACE_E("Unexpected situation in CSalamanderGeneral::PostMenuExtCommand().");
        }
    }
}

BOOL CSalamanderGeneral::SalamanderIsNotBusy(DWORD* lastIdleTime)
{
    CALL_STACK_MESSAGE1("CSalamanderGeneral::SalamanderIsNotBusy()");
    return ::SalamanderIsNotBusy(lastIdleTime);
}

void CSalamanderGeneral::CallLoadOrSaveConfiguration(BOOL load,
                                                     FSalLoadOrSaveConfiguration loadOrSaveFunc,
                                                     void* param)
{
    CALL_STACK_MESSAGE2("CSalamanderGeneral::CallLoadOrSaveConfiguration(%d, ,)", load);
    if (MainThreadID != GetCurrentThreadId())
    {
        TRACE_E("You can call CSalamanderGeneral::CallLoadOrSaveConfiguration() only from main thread!");
        return;
    }
    CPluginData* data = Plugins.GetPluginData(Plugin);
    if (data != NULL)
    {
        data->CallLoadOrSaveConfiguration(load, loadOrSaveFunc, param);
    }
    else
    {
        TRACE_E("Unexpected situation in CSalamanderGeneral::CallLoadOrSaveConfiguration().");
    }
}

// wide; the storage in CPluginData widened with it.
void CSalamanderGeneral::SetPluginBugReportInfo(const wchar_t* message, const wchar_t* email)
{
    CALL_STACK_MESSAGE2("CSalamanderGeneral::SetPluginBugReportInfo(%S)", message);
    if (MainThreadID != GetCurrentThreadId())
    {
        TRACE_E("You can call CSalamanderGeneral::SetPluginBugReportInfo() only from main thread!");
        return;
    }
    CPluginData* data = Plugins.GetPluginData(Plugin);
    if (data != NULL)
    {
        if (message != NULL)
            data->BugReportMessage = message;
        else
            data->BugReportMessage.clear();
        if (email != NULL)
        {
            data->BugReportEMail = email;
            if (data->BugReportEMail.length() > 100)
            {
                data->BugReportEMail.resize(100);
            }
        }
        else
            data->BugReportEMail.clear();
    }
    else
    {
        TRACE_E("Unexpected situation in CSalamanderGeneral::SetPluginBugReportInfo().");
    }
}

void CSalamanderGeneral::FocusNameInPanel(int panel, const wchar_t* path, const wchar_t* name)
{
    CALL_STACK_MESSAGE4("CSalamanderGeneral::FocusNameInPanel(%d, %ls, %ls)", panel, path, name);
    if (MainThreadID != GetCurrentThreadId())
    {
        TRACE_E("You can call CSalamanderGeneral::FocusNameInPanel() only from main thread!");
        return;
    }
    if (name == NULL || path == NULL)
    {
        TRACE_E("CSalamanderGeneral::FocusNameInPanel(): incorrect parameters (name == NULL || path == NULL)!");
        return;
    }
    CFilesWindow* p = GetPanel(panel);
    if (p != NULL)
    {
        CFocusFileDataW focus = {name, path};
        SendMessage(p->HWindow, WM_USER_FOCUSFILEW, (WPARAM)&focus, 0);
    }
}

BOOL CSalamanderGeneral::ChangePanelPath(int panel, const wchar_t* path, int* failReason,
                                         int suggestedTopIndex, const wchar_t* suggestedFocusName,
                                         BOOL convertFSPathToInternal)
{
    CALL_STACK_MESSAGE6("CSalamanderGeneral::ChangePanelPath(%d, %ls, , %d, %ls, %d)",
                        panel, path, suggestedTopIndex, suggestedFocusName, convertFSPathToInternal);
    if (MainThreadID != GetCurrentThreadId())
    {
        TRACE_E("You can call CSalamanderGeneral::ChangePanelPath() only from main thread!");
        if (failReason != NULL)
            *failReason = CHPPFR_INVALIDPATH;
        return FALSE;
    }
    CFilesWindow* p = GetPanel(panel);
    if (p != NULL)
    {
        return p->ChangeDir(path, suggestedTopIndex, suggestedFocusName, 3 /*change-dir*/,
                            failReason, convertFSPathToInternal);
    }
    if (failReason != NULL)
        *failReason = CHPPFR_INVALIDPATH;
    return FALSE;
}

// wide. Routes to CFilesWindow::ChangePathToDisk, the native-wide
// wide implementation (not a narrow-then-widen shim), so both path and
// suggestedFocusName remain Unicode through the navigation owner.
BOOL CSalamanderGeneral::ChangePanelPathToDisk(int panel, const wchar_t* path, int* failReason,
                                               int suggestedTopIndex, const wchar_t* suggestedFocusName)
{
    CALL_STACK_MESSAGE5("CSalamanderGeneral::ChangePanelPathToDisk(%d, %ls, , %d, %ls)",
                        panel, path, suggestedTopIndex, suggestedFocusName);
    if (MainThreadID != GetCurrentThreadId())
    {
        TRACE_E("You can call CSalamanderGeneral::ChangePanelPathToDisk() only from main thread!");
        if (failReason != NULL)
            *failReason = CHPPFR_INVALIDPATH;
        return FALSE;
    }
    CFilesWindow* p = GetPanel(panel);
    if (p != NULL)
    {
        return p->ChangePathToDisk(GetMsgBoxParent(), path, suggestedTopIndex, suggestedFocusName,
                                    NULL, TRUE, FALSE, FALSE, failReason);
    }
    if (failReason != NULL)
        *failReason = CHPPFR_INVALIDPATH;
    return FALSE;
}

// wide signature; DEBT, not a full fix. ChangePathToArchive exists but
// narrows internally (TryExactAnsiFallback) and REFUSES rather than navigates when
// 'archive'/'archivePath' cannot round-trip CP_ACP exactly - so a plugin passing a
// genuinely Unicode-only archive name still gets an error box, not a listing. This
// widening removes the narrowing step at the SDK boundary; it retires to a real fix
// only once ChangePathToArchive's own refusal goes (the
// panel/archive-open chain).
BOOL CSalamanderGeneral::ChangePanelPathToArchive(int panel, const wchar_t* archive, const wchar_t* archivePath,
                                                  int* failReason, int suggestedTopIndex,
                                                  const wchar_t* suggestedFocusName, BOOL forceUpdate)
{
    CALL_STACK_MESSAGE7("CSalamanderGeneral::ChangePanelPathToArchive(%d, %ls, %ls, , %d, %ls, %d)",
                        panel, archive, archivePath, suggestedTopIndex, suggestedFocusName, forceUpdate);
    if (MainThreadID != GetCurrentThreadId())
    {
        TRACE_E("You can call CSalamanderGeneral::ChangePanelPathToArchive() only from main thread!");
        if (failReason != NULL)
            *failReason = CHPPFR_INVALIDPATH;
        return FALSE;
    }
    CFilesWindow* p = GetPanel(panel);
    if (p != NULL)
    {
        return p->ChangePathToArchive(archive, archivePath, suggestedTopIndex, suggestedFocusName,
                                       forceUpdate, NULL, TRUE, failReason);
    }
    if (failReason != NULL)
        *failReason = CHPPFR_INVALIDPATH;
    return FALSE;
}

BOOL CSalamanderGeneral::ChangePanelPathToPluginFS(int panel, const wchar_t* fsName, const wchar_t* fsUserPart,
                                                   int* failReason, int suggestedTopIndex,
                                                   const wchar_t* suggestedFocusName, BOOL forceUpdate,
                                                   BOOL convertPathToInternal)
{
    CALL_STACK_MESSAGE8("CSalamanderGeneral::ChangePanelPathToPluginFS(%d, %ls, %ls, , %d, %ls, %d, %d)",
                        panel, fsName, fsUserPart, suggestedTopIndex, suggestedFocusName, forceUpdate,
                        convertPathToInternal);
    if (MainThreadID != GetCurrentThreadId())
    {
        TRACE_E("You can call CSalamanderGeneral::ChangePanelPathToPluginFS() only from main thread!");
        if (failReason != NULL)
            *failReason = CHPPFR_INVALIDPATH;
        return FALSE;
    }
    CFilesWindow* p = GetPanel(panel);
    if (p != NULL)
    {
        return p->ChangePathToPluginFS(fsName, fsUserPart, suggestedTopIndex, suggestedFocusName,
                                        forceUpdate, 2 /*report all errors*/, NULL, TRUE, failReason,
                                        FALSE, FALSE, convertPathToInternal);
    }
    if (failReason != NULL)
        *failReason = CHPPFR_INVALIDPATH;
    return FALSE;
}

BOOL CSalamanderGeneral::ChangePanelPathToDetachedFS(int panel, CPluginFSInterfaceAbstract* detachedFS,
                                                     int* failReason, int suggestedTopIndex,
                                                     const wchar_t* suggestedFocusName)
{
    CALL_STACK_MESSAGE4("CSalamanderGeneral::ChangePanelPathToDetachedFS(%d, , , %d, %ls)",
                        panel, suggestedTopIndex, suggestedFocusName);
    if (MainThreadID != GetCurrentThreadId())
    {
        TRACE_E("You can call CSalamanderGeneral::ChangePanelPathToDetachedFS() only from main thread!");
        if (failReason != NULL)
            *failReason = CHPPFR_INVALIDPATH;
        return FALSE;
    }
    CFilesWindow* p = GetPanel(panel);
    if (p != NULL)
    {
        int fsIndex = -1;
        CDetachedFSList* list = MainWindow->DetachedFSList;
        int i;
        for (i = 0; i < list->Count; i++)
        {
            if (list->At(i)->GetInterface() == detachedFS)
            {
                fsIndex = i;
                break;
            }
        }
        if (fsIndex != -1)
        {
            return p->ChangePathToDetachedFS(fsIndex, suggestedTopIndex, suggestedFocusName, TRUE, failReason);
        }
        else
        {
            TRACE_E("Parameter 'detachedFS' is not detached FS in "
                    "CSalamanderGeneral::ChangePanelPathToDetachedFS().");
        }
    }
    if (failReason != NULL)
        *failReason = CHPPFR_INVALIDPATH;
    return FALSE;
}

BOOL CSalamanderGeneral::ChangePanelPathToFixedDrive(int panel, int* failReason)
{
    CALL_STACK_MESSAGE2("CSalamanderGeneral::ChangePanelPathToFixedDrive(%d,)", panel);
    if (MainThreadID != GetCurrentThreadId())
    {
        TRACE_E("You can call CSalamanderGeneral::ChangePanelPathToFixedDrive() only from main thread!");
        if (failReason != NULL)
            *failReason = CHPPFR_INVALIDPATH;
        return FALSE;
    }
    CFilesWindow* p = GetPanel(panel);
    if (p != NULL)
    {
        return p->ChangeToFixedDrive(GetMsgBoxParent(), NULL, TRUE, FALSE, failReason);
    }
    if (failReason != NULL)
        *failReason = CHPPFR_INVALIDPATH;
    return FALSE;
}

BOOL CSalamanderGeneral::ChangePanelPathToRescuePathOrFixedDrive(int panel, int* failReason)
{
    CALL_STACK_MESSAGE2("CSalamanderGeneral::ChangePanelPathToRescuePathOrFixedDrive(%d,)", panel);
    if (MainThreadID != GetCurrentThreadId())
    {
        TRACE_E("You can call CSalamanderGeneral::ChangePanelPathToRescuePathOrFixedDrive() only from main thread!");
        if (failReason != NULL)
            *failReason = CHPPFR_INVALIDPATH;
        return FALSE;
    }
    CFilesWindow* p = GetPanel(panel);
    if (p != NULL)
    {
        return p->ChangeToRescuePathOrFixedDrive(GetMsgBoxParent(), NULL, TRUE, FALSE, FSTRYCLOSE_CHANGEPATH, failReason);
    }
    if (failReason != NULL)
        *failReason = CHPPFR_INVALIDPATH;
    return FALSE;
}

void CSalamanderGeneral::RefreshPanelPath(int panel, BOOL forceRefresh, BOOL focusFirstNewItem)
{
    CALL_STACK_MESSAGE4("CSalamanderGeneral::RefreshPanelPath(%d, %d, %d)",
                        panel, forceRefresh, focusFirstNewItem);
    if (MainThreadID != GetCurrentThreadId())
    {
        TRACE_E("You can call CSalamanderGeneral::RefreshPanelPath() only from main thread!");
        return;
    }
    CFilesWindow* p = GetPanel(panel);
    if (p != NULL)
    {
        if (forceRefresh && p->Is(ptZIPArchive))
        { // for archives ensure a hard refresh by invalidating the archive stamp
            p->SetZIPArchiveSize(CQuadWord(-1, -1));
        }
        p->FocusFirstNewItem = focusFirstNewItem;
        p->RefreshDirectory(FALSE, forceRefresh);
    }
}

void CSalamanderGeneral::PostRefreshPanelPath(int panel, BOOL focusFirstNewItem)
{
    CALL_STACK_MESSAGE3("CSalamanderGeneral::PostRefreshPanelPath(%d, %d)", panel, focusFirstNewItem);
    CFilesWindow* p = GetPanel(panel);
    if (p != NULL)
    {
        // post a hard refresh
        HANDLES(EnterCriticalSection(&TimeCounterSection));
        int t1 = MyTimeCounter++;
        HANDLES(LeaveCriticalSection(&TimeCounterSection));
        p->FocusFirstNewItem = focusFirstNewItem; // not synchronized (may be called outside the main thread) but should not matter
        PostMessage(p->HWindow, WM_USER_REFRESH_DIR, 0, t1);
    }
}

void CSalamanderGeneral::PostRefreshPanelFS(CPluginFSInterfaceAbstract* modifiedFS, BOOL focusFirstNewItem)
{
    CALL_STACK_MESSAGE2("CSalamanderGeneral::PostRefreshPanelFS(, %d)", focusFirstNewItem);
    PostRefreshPanelFS2(modifiedFS, focusFirstNewItem);
}

BOOL CSalamanderGeneral::PostRefreshPanelFS2(CPluginFSInterfaceAbstract* modifiedFS, BOOL focusFirstNewItem)
{
    CALL_STACK_MESSAGE2("CSalamanderGeneral::PostRefreshPanelFS2(, %d)", focusFirstNewItem);
    CFilesWindow* p = NULL;
    if (MainWindow != NULL)
    {
        // no synchronization issue, because PluginFS is cleared only after CloseFS, which
        // should terminate the thread monitoring FS changes (after CloseFS there should be no call to
        // PostRefreshPanelFS2)
        if (MainWindow->LeftPanel != NULL && MainWindow->LeftPanel->Is(ptPluginFS) &&
            MainWindow->LeftPanel->GetPluginFS()->Contains(modifiedFS))
        {
            p = MainWindow->LeftPanel;
        }
        if (MainWindow->RightPanel != NULL && MainWindow->RightPanel->Is(ptPluginFS) &&
            MainWindow->RightPanel->GetPluginFS()->Contains(modifiedFS))
        {
            p = MainWindow->RightPanel;
        }
    }
    if (p != NULL)
    {
        // post a hard refresh
        HANDLES(EnterCriticalSection(&TimeCounterSection));
        int t1 = MyTimeCounter++;
        HANDLES(LeaveCriticalSection(&TimeCounterSection));
        p->FocusFirstNewItem = focusFirstNewItem; // not synchronized (may be called outside the main thread) but should not matter
        PostMessage(p->HWindow, WM_USER_REFRESH_DIR, 0, t1);
        return TRUE;
    }
    else
        return FALSE;
}

BOOL CSalamanderGeneral::CloseDetachedFS(HWND parent, CPluginFSInterfaceAbstract* detachedFS)
{
    CALL_STACK_MESSAGE1("CSalamanderGeneral::CloseDetachedFS(,)");
    if (MainThreadID != GetCurrentThreadId())
    {
        TRACE_E("You can call CSalamanderGeneral::CloseDetachedFS() only from main thread!");
        return FALSE;
    }
    if (MainWindow->DetachedFSList->IsGood()) // to guarantee Delete succeeds
    {
        CDetachedFSList* list = MainWindow->DetachedFSList;
        int i;
        for (i = 0; i < list->Count; i++)
        {
            if (list->At(i)->GetInterface() == detachedFS)
            {
                CPluginFSInterfaceEncapsulation* fs = list->At(i);
                BOOL dummy;
                if (fs->TryCloseOrDetach(FALSE, FALSE, dummy, FSTRYCLOSE_PLUGINCLOSEDETACHEDFS)) // the FS has no objection to closing
                {
                    CPluginInterfaceForFSEncapsulation plugin(fs->GetPluginInterfaceForFS()->GetInterface(),
                                                              fs->GetPluginInterfaceForFS()->GetBuiltForVersion());
                    if (plugin.NotEmpty())
                    {
                        fs->ReleaseObject(parent);
                        plugin.CloseFS(fs->GetInterface());
                        list->Delete(i);
                        if (!list->IsGood())
                            list->ResetState();
                        return TRUE;
                    }
                    else
                        TRACE_E("Unexpected situation in CSalamanderGeneral::CloseDetachedFS()");
                }
                break;
            }
        }
    }
    return FALSE;
}

BOOL CSalamanderGeneral::DuplicateAmpersands(CSalamanderStringBuffer* text)
{
    CALL_STACK_MESSAGE1("CSalamanderGeneral::DuplicateAmpersands()");
    if (text == NULL)
        return FALSE;
    std::wstring input;
    if (!sally::plugin_abi::ReadStringBuffer(*text, input))
        return FALSE;
    std::wstring result;
    result.reserve(input.size() * 2);
    for (wchar_t ch : input)
    {
        result.push_back(ch);
        if (ch == L'&')
            result.push_back(ch);
    }
    return sally::plugin_abi::WriteStringBuffer(*text, result);
}

BOOL CSalamanderGeneral::RemoveAmpersands(CSalamanderStringBuffer* text)
{
    CALL_STACK_MESSAGE1("CSalamanderGeneral::RemoveAmpersands()");
    if (text == NULL)
        return FALSE;
    std::wstring input;
    if (!sally::plugin_abi::ReadStringBuffer(*text, input))
        return FALSE;
    std::wstring result;
    result.reserve(input.size());
    for (size_t i = 0; i < input.size(); ++i)
    {
        if (input[i] != L'&')
            result.push_back(input[i]);
        else if (i + 1 < input.size() && input[i + 1] == L'&')
        {
            result.push_back(L'&');
            ++i;
        }
    }
    return sally::plugin_abi::WriteStringBuffer(*text, result);
}

BOOL CSalamanderGeneral::ValidateVarString(HWND msgParent, const wchar_t* varText, int& errorPos1, int& errorPos2,
                                           const CSalamanderVarStrEntry* variables)
{
    CALL_STACK_MESSAGE2("CSalamanderGeneral::ValidateVarString(, %ls, , ,)", varText);
    if (varText == NULL || variables == NULL)
    {
        TRACE_E("CSalamanderGeneral::ValidateVarString(): invalid parameters!");
        return FALSE;
    }
    return ::ValidateVarStringW(msgParent, varText, errorPos1, errorPos2, variables);
}

BOOL CSalamanderGeneral::ExpandVarString(HWND msgParent, const wchar_t* varText, CSalamanderStringBuffer* buffer,
                                         const CSalamanderVarStrEntry* variables, void* param,
                                         BOOL ignoreEnvVarNotFoundOrTooLong,
                                         CSalamanderTextRangeBuffer* varPlacements,
                                         BOOL detectMaxVarWidths, int* maxVarWidths,
                                         int maxVarWidthsCount)
{
    CALL_STACK_MESSAGE5("CSalamanderGeneral::ExpandVarString(, %ls, , , , %d, , , %d, , %d)",
                        varText, ignoreEnvVarNotFoundOrTooLong, detectMaxVarWidths,
                        maxVarWidthsCount);
    if (buffer == NULL || varText == NULL || variables == NULL)
    {
        TRACE_E("CSalamanderGeneral::ExpandVarString(): invalid parameters!");
        return FALSE;
    }
    return ::ExpandVarString(msgParent, varText, buffer, variables, param,
                             ignoreEnvVarNotFoundOrTooLong, varPlacements,
                             detectMaxVarWidths, maxVarWidths, maxVarWidthsCount);
}

BOOL CSalamanderGeneral::EnumInstalledModules(
    int* index, CSalamanderStringBuffer* module,
    CSalamanderStringBuffer* version)
{
    CALL_STACK_MESSAGE1("CSalamanderGeneral::EnumInstalledModules(, ,)");
    if (MainThreadID != GetCurrentThreadId())
    {
        TRACE_E("You can call CSalamanderGeneral::EnumInstalledModules() only from main thread!");
        return FALSE;
    }
    if (index == NULL || module == NULL || version == NULL)
        return FALSE;

    std::wstring moduleValue;
    std::wstring versionValue;
    if (!Plugins.EnumInstalledModules(index, moduleValue, versionValue))
        return FALSE;

    return sally::plugin_abi::WriteStringBuffer(*module, moduleValue) &&
           sally::plugin_abi::WriteStringBuffer(*version, versionValue);
}

// wide primary; the narrow overload and the W suffix are gone.
BOOL CSalamanderGeneral::CopyTextToClipboard(const wchar_t* text, int textLen, BOOL showEcho, HWND echoParent)
{
    CALL_STACK_MESSAGE3("CSalamanderGeneral::CopyTextToClipboard(, %d, %d,)", textLen, showEcho);
    // j.r. threw the text parameter, which did not have to be null-terminated
    if (text == NULL)
    {
        TRACE_E("Unexpected parameter (NULL) in CSalamanderGeneral::CopyTextToClipboard().");
        return FALSE;
    }
    return ::CopyTextToClipboardW(text, textLen, showEcho, echoParent);
}

BOOL CSalamanderGeneral::IsPluginInstalled(const wchar_t* pluginSPL)
{
    CALL_STACK_MESSAGE2("CSalamanderGeneral::IsPluginInstalled(%ls)", pluginSPL);
    if (MainThreadID != GetCurrentThreadId())
    {
        TRACE_E("You can call CSalamanderGeneral::IsPluginInstalled() only from main thread!");
        return FALSE;
    }
    if (pluginSPL != NULL)
    {
        CPluginData* data = Plugins.GetPluginDataFromSuffix(pluginSPL);
        return data != NULL;
    }
    else
    {
        TRACE_E("Unexpected parameter 'pluginSPL' (NULL) in CSalamanderGeneral::IsPluginInstalled().");
        return FALSE;
    }
}

BOOL ViewFileInPluginViewerW(const wchar_t* sourceFileName, const wchar_t* pluginSPL,
                             CSalamanderPluginViewerData* pluginData,
                             BOOL useCache, const wchar_t* rootTmpPath,
                             const wchar_t* fileNameInCache, int& error)
{
    error = -1; // unknown
    if (pluginData == NULL || pluginData->Size < sizeof(CSalamanderPluginViewerData) ||
        sourceFileName == NULL || sourceFileName[0] == 0)
    {
        TRACE_E("Unexpected value of 'pluginData' in CSalamanderGeneral::ViewFileInPluginViewer!");
        return FALSE;
    }

    CALL_STACK_MESSAGE7("CSalamanderGeneral::ViewFileInPluginViewer(%ls, %d, %ls, %d, %ls, %ls,)",
                        pluginSPL, pluginData->Size, sourceFileName, useCache,
                        (useCache ? rootTmpPath : L"(ignored)"),
                        (useCache ? fileNameInCache : L"(ignored)"));

    wchar_t viewUniqueName[50]; // we need a unique name for the viewed file in the cache
    viewUniqueName[0] = 0;
    const wchar_t* fileName; // name of the file we will pass to the viewer
    if (useCache)
    {
        // verify that 'fileNameInCache' is valid (a name without path)
        const wchar_t* s = NULL;
        if (fileNameInCache != NULL)
        {
            s = fileNameInCache;
            while (*s != 0 && *s != L'\\' && *s != L'/' && *s != L':' &&
                   *s >= 32 && *s != L'<' && *s != L'>' && *s != L'|' && *s != L'"')
                s++;
        }
        if (fileNameInCache == NULL || fileNameInCache[0] == 0 || *s != 0)
        {
            TRACE_E("Unexpected value of 'fileNameInCache' in CSalamanderGeneral::ViewFileInPluginViewer!");
            error = 3;
            gFileSystem->DeleteFile(sourceFileName);
            return FALSE;
        }

        // insert the file 'pluginData->FileName' into the disk cache under the name 'fileNameInCache'
        while (1)
        {
            swprintf_s(viewUniqueName, _countof(viewUniqueName), L"ViewFile %X", GetTickCount());
            BOOL exists;
            fileName = DiskCache.GetName(viewUniqueName, fileNameInCache, &exists, TRUE, rootTmpPath, FALSE, NULL, NULL);
            if (fileName == NULL) // error (if 'exists' is TRUE -> fatal, otherwise "file already exists")
            {
                if (!exists)
                    Sleep(100); // the file exists -> almost impossible, still handle it
                else            // fatal error
                {
                    error = 3;
                    gFileSystem->DeleteFile(sourceFileName);
                    return FALSE; // fatal error
                }
            }
            else
                break; // we have the name in the disk cache, all OK
        }
        if (!::SalMoveFile(sourceFileName, fileName))
        {
            DWORD err = GetLastError();
            TRACE_EW(L"Unable to move file to disk cache! (error " << ::GetErrorTextOwned(err).c_str() << L")");
            gFileSystem->DeleteFile(sourceFileName);
            DiskCache.ReleaseName(viewUniqueName, FALSE);
            error = 3;
            return FALSE;
        }
        else // successfully obtained a temp file; we must call NamePrepared()
        {
            CQuadWord size(0, 0);
            HANDLE file = gFileSystem->CreateFile(fileName, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                                  NULL, OPEN_EXISTING, 0, NULL);
            if (file != INVALID_HANDLE_VALUE)
            { // ignore the error; the file size is not that important
                uint64_t fileSize = 0;
                if (gFileSystem->GetHandleFileSize(file, &fileSize).success)
                    size.SetUI64(fileSize);
                gFileSystem->CloseFileHandle(file);
            }

            DiskCache.NamePrepared(viewUniqueName, size);
        }
    }
    else
        fileName = sourceFileName;

    // position for viewers
    WINDOWPLACEMENT place;
    place.length = sizeof(WINDOWPLACEMENT);
    GetWindowPlacement(MainWindow->HWindow, &place);
    // GetWindowPlacement accounts for the taskbar, so if the taskbar is at the top or left,
    // the values are shifted by its dimensions. Apply a correction.
    RECT monitorRect;
    RECT workRect;
    MultiMonGetClipRectByRect(&place.rcNormalPosition, &workRect, &monitorRect);
    OffsetRect(&place.rcNormalPosition, workRect.left - monitorRect.left,
               workRect.top - monitorRect.top);
    //we do not want a minimized viewer, even if the main window is minimized
    if (place.showCmd == SW_MINIMIZE || place.showCmd == SW_SHOWMINIMIZED ||
        place.showCmd == SW_SHOWMINNOACTIVE)
        place.showCmd = SW_SHOWNORMAL;

    // finally open the viewer itself
    BOOL diskCacheNameClosed = FALSE;
    error = 0;
    if (pluginSPL != NULL) // viewer from a plug-in
    {
        CPluginData* data = Plugins.GetPluginDataFromSuffix(pluginSPL);
        if (data != NULL && data->SupportViewer)
        {
            if (data->InitDLL(MainWindow->HWindow)
                /*&& PluginIfaceForViewer.NotEmpty()*/) // redundant, because downgrade is impossible and InitDLL checks the interfaces
            {
                HANDLE lock = NULL;
                BOOL lockOwner = FALSE;
                BOOL ret = data->GetPluginInterfaceForViewer()->ViewFile(fileName, place.rcNormalPosition.left,
                                                                         place.rcNormalPosition.top,
                                                                         place.rcNormalPosition.right - place.rcNormalPosition.left,
                                                                         place.rcNormalPosition.bottom - place.rcNormalPosition.top,
                                                                         place.showCmd, Configuration.AlwaysOnTop,
                                                                         useCache, &lock, &lockOwner, pluginData, -1, -1);
                if (!ret)
                {
                    TRACE_E("PluginIfaceForViewer.ViewFile() returns error.");
                    error = 2;
                }
                else
                {
                    if (useCache && lock != NULL)
                    {
                        if (lockOwner) // add the handle for 'lock' to HANDLES (the disk cache will want to close it and will look for it)
                            HANDLES_ADD(__htEvent, __hoCreateEvent, lock);
                        DiskCache.AssignName(viewUniqueName, lock, lockOwner, crtDirect);
                        diskCacheNameClosed = TRUE;
                    }
                }
            }
            else
                error = 1;
        }
        else
            error = 1;
        if (error == 1)
            TRACE_E("Unable to load plugin.");
    }
    else // internal viewer
    {
        if (Configuration.SavePosition &&
            Configuration.WindowPlacement.length != 0)
        {
            place = Configuration.WindowPlacement;
            // GetWindowPlacement accounts for the taskbar, so if the taskbar is at the top or left,
            // the values are shifted by its dimensions. Apply a correction.
            RECT monitorRect2;
            RECT workRect2;
            MultiMonGetClipRectByRect(&place.rcNormalPosition, &workRect2, &monitorRect2);
            OffsetRect(&place.rcNormalPosition, workRect2.left - monitorRect2.left,
                       workRect2.top - monitorRect2.top);
            MultiMonEnsureRectVisible(&place.rcNormalPosition, TRUE);
        }

        HANDLE lock = NULL;
        BOOL lockOwner = FALSE;
        if (OpenViewer(fileName, vtText,
                       place.rcNormalPosition.left,
                       place.rcNormalPosition.top,
                       place.rcNormalPosition.right - place.rcNormalPosition.left,
                       place.rcNormalPosition.bottom - place.rcNormalPosition.top,
                       place.showCmd, useCache, &lock, &lockOwner, pluginData, -1, -1))
        {
            if (useCache && lock != NULL)
            {
                DiskCache.AssignName(viewUniqueName, lock, lockOwner, crtDirect);
                diskCacheNameClosed = TRUE;
            }
        }
        else
        {
            TRACE_E("OpenViewer() returns error.");
            error = 2;
        }
    }

    // if we did not assign a name in the disk cache, release the record...
    if (useCache && !diskCacheNameClosed)
    {
        DiskCache.ReleaseName(viewUniqueName, FALSE);
        // The cache already removed the file and deallocated fileName.
    }
    return error == 0; // returning success?
}

BOOL CSalamanderGeneral::ViewFileInPluginViewer(const wchar_t* pluginSPL,
                                                CSalamanderPluginViewerData* pluginData,
                                                BOOL useCache, const wchar_t* rootTmpPath,
                                                const wchar_t* fileNameInCache, int& error)
{
    error = -1; // unknown

    // guard against calls from outside the main thread and from the entry point
    if (MainThreadID != GetCurrentThreadId() || (INT_PTR)Plugin == -1)
    {
        if (MainThreadID == GetCurrentThreadId()) // if both errors occur (different thread + unfinished entry point), the entry point takes priority
            TRACE_E("You may not call CSalamanderGeneral::ViewFileInPluginViewer from entry-point!");
        else
            TRACE_E("You can call CSalamanderGeneral::ViewFileInPluginViewer only from main thread!");
        return FALSE;
    }

    if (pluginData == NULL || pluginData->FileName == NULL)
        return FALSE;
    const std::wstring sourceFileName = pluginData->FileName;
    return ::ViewFileInPluginViewerW(sourceFileName.c_str(), pluginSPL, pluginData, useCache,
                                     rootTmpPath, fileNameInCache, error);
}

// wide: forwards to the wide internal that already existed.
// %S is a wide string in a narrow format string under MSVC.
void CSalamanderGeneral::ExecuteAssociation(HWND parent, const wchar_t* path, const wchar_t* name)
{
    CALL_STACK_MESSAGE4("CSalamanderGeneral::ExecuteAssociation(0x%p, %S, %S)", parent, path, name);
    if (MainThreadID != GetCurrentThreadId())
    {
        TRACE_E("You can call CSalamanderGeneral::ExecuteAssociation() only from main thread!");
        return;
    }
    MainWindow->SetDefaultDirectories(); // so the starting process inherits the correct current directories
    ::ExecuteAssociationW(parent, path, name);
}

int CSalamanderGeneral::GetPanelTopIndex(int panel)
{
    CALL_STACK_MESSAGE2("CSalamanderGeneral::GetPanelTopIndex(%d)", panel);
    if (MainThreadID != GetCurrentThreadId())
    {
        TRACE_E("You can call CSalamanderGeneral::GetPanelTopIndex() only from main thread!");
        return 0;
    }
    CFilesWindow* p = GetPanel(panel);
    if (p != NULL)
        return p->ListBox->GetTopIndex();
    return 0; // error; should not happen...
}

void CSalamanderGeneral::GetPanelEnumFilesParams(int panel, int* enumFilesSourceUID, int* enumFilesCurrentIndex)
{
    CALL_STACK_MESSAGE2("CSalamanderGeneral::GetPanelEnumFilesParams(%d, ,)", panel);
    if (enumFilesCurrentIndex != NULL)
        *enumFilesCurrentIndex = -1;
    if (enumFilesSourceUID != NULL)
        *enumFilesSourceUID = -1;
    else
    {
        TRACE_E("CSalamanderGeneral::GetPanelEnumFilesParams(): 'enumFilesSourceUID' cannot be NULL!");
        return;
    }
    if (MainThreadID != GetCurrentThreadId())
    {
        TRACE_E("You can call CSalamanderGeneral::GetPanelEnumFilesParams() only from main thread!");
        return;
    }

    CFilesWindow* p = GetPanel(panel);
    if (p != NULL && p->Is(ptDisk))
    {
        *enumFilesSourceUID = p->EnumFileNamesSourceUID;
        if (enumFilesCurrentIndex != NULL)
        {
            int i = p->GetCaretIndex();
            if (i >= p->Dirs->Count && i < p->Dirs->Count + p->Files->Count)
                *enumFilesCurrentIndex = i - p->Dirs->Count;
        }
    }
}

BOOL CSalamanderGeneral::GetPanelWithPluginFS(CPluginFSInterfaceAbstract* pluginFS, int& panel)
{
    CALL_STACK_MESSAGE1("CSalamanderGeneral::GetPanelWithPluginFS(, )");
    if (MainThreadID != GetCurrentThreadId())
    {
        TRACE_E("You can call CSalamanderGeneral::GetPanelWithPluginFS() only from main thread!");
        return FALSE;
    }
    if (pluginFS == NULL)
        return FALSE;
    if (MainWindow->LeftPanel->Is(ptPluginFS) &&
        MainWindow->LeftPanel->GetPluginFS()->GetInterface() == pluginFS)
    {
        panel = PANEL_LEFT;
        return TRUE;
    }
    if (MainWindow->RightPanel->Is(ptPluginFS) &&
        MainWindow->RightPanel->GetPluginFS()->GetInterface() == pluginFS)
    {
        panel = PANEL_RIGHT;
        return TRUE;
    }
    return FALSE;
}

// wide. MainWindow's wide overload landed earlier, where two
// core callers were found feeding it a lossily-narrowed panel path and so
// posting a notification for a directory that did not exist.
void CSalamanderGeneral::PostChangeOnPathNotification(const wchar_t* path, BOOL includingSubdirs)
{
    CALL_STACK_MESSAGE3("CSalamanderGeneral::PostChangeOnPathNotification(%S, %d)", path, includingSubdirs);
    MainWindow->PostChangeOnPathNotificationW(path, includingSubdirs);
}

DWORD
CSalamanderGeneral::SalCheckPath(BOOL echo, const wchar_t* path, DWORD err, HWND parent)
{
    // %S is a WIDE string in a narrow format string under MSVC.
    CALL_STACK_MESSAGE4("CSalamanderGeneral::SalCheckPath(%d, %S, %u,)", echo, path, err);
    if (MainThreadID != GetCurrentThreadId())
    {
        TRACE_E("You can call CSalamanderGeneral::SalCheckPath() only from main thread!");
        return ERROR_SUCCESS;
    }
    return ::SalCheckPathW(echo, path, err, TRUE, parent); // the value of 'postRefresh' does not matter (StopRefresh is surely > 0)
}

BOOL CSalamanderGeneral::SalCheckAndRestorePath(HWND parent, const wchar_t* path, BOOL tryNet)
{
    CALL_STACK_MESSAGE3("CSalamanderGeneral::SalCheckAndRestorePath(, %S, %d)", path, tryNet);
    if (MainThreadID != GetCurrentThreadId())
    {
        TRACE_E("You can call CSalamanderGeneral::SalCheckAndRestorePath() only from main thread!");
        return FALSE;
    }
    return ::SalCheckAndRestorePathW(parent, path, tryNet); // wide internal
}

BOOL CSalamanderGeneral::SalCheckAndRestorePathWithCut(HWND parent, std::wstring& path, BOOL& tryNet, DWORD& err,
                                                       DWORD& lastErr, BOOL& pathInvalid, BOOL& cut,
                                                       BOOL donotReconnect)
{
    CALL_STACK_MESSAGE3("CSalamanderGeneral::SalCheckAndRestorePathWithCut(, , %d, , , , , %d)",
                        tryNet, donotReconnect);
    if (MainThreadID != GetCurrentThreadId())
    {
        TRACE_E("You can call CSalamanderGeneral::SalCheckAndRestorePathWithCut() only from main thread!");
        lastErr = err = ERROR_SUCCESS;
        pathInvalid = TRUE;
        cut = FALSE;
        return FALSE;
    }
    return ::SalCheckAndRestorePathWithCutW(parent, path, tryNet, err, lastErr, pathInvalid, cut,
                                            donotReconnect);
}

BOOL CSalamanderGeneral::SalParsePath(HWND parent, CSalamanderStringBuffer* path, int& type,
                                      BOOL& isDir, DWORD& secondPartOffset,
                                      const wchar_t* errorTitle, CSalamanderStringBuffer* nextFocus,
                                      BOOL curPathIsDiskOrArchive, const wchar_t* curPath,
                                      const wchar_t* curArchivePath, int* error)
{
    CALL_STACK_MESSAGE2("CSalamanderGeneral::SalParsePath(, , , , , , , %d, , , ,)",
                        curPathIsDiskOrArchive);
    secondPartOffset = SAL_STRING_BUFFER_NPOS;
    if (MainThreadID != GetCurrentThreadId())
    {
        TRACE_E("You can call CSalamanderGeneral::SalParsePath() only from main thread!");
        if (error != NULL)
            *error = SPP_WINDOWSPATHERROR;
        return FALSE;
    }

    const auto refuseBoundary = [&]() {
        type = -1;
        isDir = FALSE;
        secondPartOffset = SAL_STRING_BUFFER_NPOS;
        if (error != NULL)
            *error = SPP_WINDOWSPATHERROR;
        return FALSE;
    };

    if (path == NULL || path == nextFocus)
        return refuseBoundary();
    std::wstring originalPath;
    if (!sally::plugin_abi::ReadStringBuffer(*path, originalPath))
        return refuseBoundary();

    // The optional context strings may point inside the mutable path buffer.
    // Consume the complete input record before calling the wide owner.
    const std::wstring errorTitleStorage =
        errorTitle != NULL ? errorTitle : L"";
    const std::wstring curPathStorage = curPath != NULL ? curPath : L"";
    const std::wstring curArchivePathStorage =
        curArchivePath != NULL ? curArchivePath : L"";
    std::wstring stagedPath = originalPath;
    std::wstring stagedFocus;
    int stagedType = -1;
    BOOL stagedIsDir = FALSE;
    wchar_t* stagedSecondPart = NULL;
    int stagedError = 0;
    const BOOL result = ::SalParsePathW(
        parent, stagedPath, stagedType, stagedIsDir, stagedSecondPart,
        errorTitle != NULL ? errorTitleStorage.c_str() : NULL,
        nextFocus != NULL ? &stagedFocus : NULL, curPathIsDiskOrArchive,
        curPath != NULL ? curPathStorage.c_str() : NULL,
        curArchivePath != NULL ? curArchivePathStorage.c_str() : NULL,
        error != NULL ? &stagedError : NULL);

    if (result != FALSE && stagedSecondPart == NULL)
        return refuseBoundary();

    size_t stagedSecondPartOffset = std::wstring::npos;
    if (stagedSecondPart != NULL)
    {
        const uintptr_t pathAddress =
            reinterpret_cast<uintptr_t>(stagedPath.c_str());
        const uintptr_t pointerAddress =
            reinterpret_cast<uintptr_t>(stagedSecondPart);
        if (stagedPath.size() >
            ((std::numeric_limits<uintptr_t>::max)() - pathAddress) /
                sizeof(wchar_t))
            return refuseBoundary();
        const uintptr_t endAddress =
            pathAddress + stagedPath.size() * sizeof(wchar_t);
        if (pointerAddress < pathAddress || pointerAddress > endAddress ||
            (pointerAddress - pathAddress) % sizeof(wchar_t) != 0)
            return refuseBoundary();
        stagedSecondPartOffset =
            (pointerAddress - pathAddress) / sizeof(wchar_t);
    }

    if (stagedPath.size() >= (std::numeric_limits<DWORD>::max)() ||
        stagedFocus.size() >= (std::numeric_limits<DWORD>::max)() ||
        !sally::plugin_abi::ReserveStringBuffer(
            *path, static_cast<DWORD>(stagedPath.size() + 1)) ||
        (nextFocus != NULL && !sally::plugin_abi::ReserveStringBuffer(
                                  *nextFocus, static_cast<DWORD>(stagedFocus.size() + 1))))
        return refuseBoundary();
    if (nextFocus != NULL)
        sally::plugin_abi::WriteStringBuffer(*nextFocus, stagedFocus);
    sally::plugin_abi::WriteStringBuffer(*path, stagedPath);
    type = stagedType;
    isDir = stagedIsDir;
    secondPartOffset = stagedSecondPartOffset != std::wstring::npos
                           ? static_cast<DWORD>(stagedSecondPartOffset)
                           : SAL_STRING_BUFFER_NPOS;
    if (error != NULL)
        *error = stagedError;
    return result;
}

BOOL CSalamanderGeneral::SalSplitWindowsPath(HWND parent, const wchar_t* title,
                                             const wchar_t* errorTitle, int selCount,
                                             CSalamanderStringBuffer* path, DWORD secondPartOffset,
                                             BOOL pathIsDir, BOOL backslashAtEnd,
                                             const wchar_t* dirName, const wchar_t* curDiskPath,
                                             CSalamanderStringBuffer* mask)
{
    // The %s arguments are gone from the trace: the call-stack formatter is
    // narrow, and rendering wide paths through it would reintroduce exactly the
    // CP_ACP round trip this widening removes - in the diagnostic that gets read
    // when something has already gone wrong.
    CALL_STACK_MESSAGE4("CSalamanderGeneral::SalSplitWindowsPath(, , , %d, , , %d, %d, , ,)",
                        selCount, pathIsDir, backslashAtEnd);
    if (path == NULL || mask == NULL || path == mask)
        return FALSE;
    std::wstring ownedPath;
    if (!sally::plugin_abi::ReadStringBuffer(*path, ownedPath) ||
        secondPartOffset > ownedPath.size())
        return FALSE;
    std::wstring ownedMask;
    const BOOL result = ::SalSplitWindowsPathOwnedW(
        parent, title, errorTitle, selCount, ownedPath, secondPartOffset,
        pathIsDir, backslashAtEnd, dirName, curDiskPath, ownedMask);
    if (ownedPath.size() >= (std::numeric_limits<DWORD>::max)() ||
        ownedMask.size() >= (std::numeric_limits<DWORD>::max)() ||
        !sally::plugin_abi::ReserveStringBuffer(
            *path, static_cast<DWORD>(ownedPath.size() + 1)) ||
        !sally::plugin_abi::ReserveStringBuffer(
            *mask, static_cast<DWORD>(ownedMask.size() + 1)))
        return FALSE;
    sally::plugin_abi::WriteStringBuffer(*mask, ownedMask);
    sally::plugin_abi::WriteStringBuffer(*path, ownedPath);
    return result;
}

BOOL CSalamanderGeneral::SalSplitGeneralPath(HWND parent, const wchar_t* title,
                                             const wchar_t* errorTitle, int selCount,
                                             CSalamanderStringBuffer* path, DWORD afterRootOffset,
                                             DWORD secondPartOffset, BOOL pathIsDir,
                                             BOOL backslashAtEnd, const wchar_t* dirName,
                                             const wchar_t* curPath, CSalamanderStringBuffer* mask,
                                             CSalamanderStringBuffer* newDirs,
                                             SGP_IsTheSamePathF isTheSamePathF)
{
    CALL_STACK_MESSAGE4("CSalamanderGeneral::SalSplitGeneralPath(, , , %d, , , , %d, %d, , , , ,)",
                        selCount, pathIsDir, backslashAtEnd);
    if (path == NULL || mask == NULL || path == mask || path == newDirs || mask == newDirs)
        return FALSE;
    std::wstring ownedPath;
    if (!sally::plugin_abi::ReadStringBuffer(*path, ownedPath) ||
        afterRootOffset > ownedPath.size() || secondPartOffset > ownedPath.size())
        return FALSE;
    std::wstring ownedMask;
    std::wstring ownedNewDirs;
    const BOOL result = ::SalSplitGeneralPathOwnedW(
        parent, title, errorTitle, selCount, ownedPath, afterRootOffset,
        secondPartOffset, pathIsDir, backslashAtEnd, dirName, curPath,
        ownedMask, newDirs != NULL ? &ownedNewDirs : NULL, isTheSamePathF);
    if (ownedPath.size() >= (std::numeric_limits<DWORD>::max)() ||
        ownedMask.size() >= (std::numeric_limits<DWORD>::max)() ||
        ownedNewDirs.size() >= (std::numeric_limits<DWORD>::max)() ||
        !sally::plugin_abi::ReserveStringBuffer(
            *path, static_cast<DWORD>(ownedPath.size() + 1)) ||
        !sally::plugin_abi::ReserveStringBuffer(
            *mask, static_cast<DWORD>(ownedMask.size() + 1)) ||
        (newDirs != NULL && !sally::plugin_abi::ReserveStringBuffer(
                                *newDirs, static_cast<DWORD>(ownedNewDirs.size() + 1))))
        return FALSE;
    if (newDirs != NULL)
        sally::plugin_abi::WriteStringBuffer(*newDirs, ownedNewDirs);
    sally::plugin_abi::WriteStringBuffer(*mask, ownedMask);
    sally::plugin_abi::WriteStringBuffer(*path, ownedPath);
    return result;
}

BOOL CSalamanderGeneral::SalRemovePointsFromPath(
    CSalamanderStringBuffer* afterRoot)
{
    CALL_STACK_MESSAGE1("CSalamanderGeneral::SalRemovePointsFromPath()");
    if (afterRoot == NULL)
        return FALSE;
    std::wstring path;
    if (!sally::plugin_abi::ReadStringBuffer(*afterRoot, path))
        return FALSE;
    std::vector<wchar_t> buffer(path.begin(), path.end());
    buffer.push_back(L'\0');
    const BOOL result = ::SalRemovePointsFromPath(buffer.data());
    if (!sally::plugin_abi::WriteStringBuffer(*afterRoot,
                                               std::wstring(buffer.data())))
        return FALSE;
    return result;
}

BOOL CSalamanderGeneral::GetConfigParameter(int paramID, void* buffer, int bufferSize, int* type)
{
    SLOW_CALL_STACK_MESSAGE3("CSalamanderGeneral::GetConfigParameter(%d, , %d,)", paramID, bufferSize);
    if (MainThreadID != GetCurrentThreadId())
    {
        TRACE_E("You can call CSalamanderGeneral::GetConfigParameter() only from main thread!");
        if (type != NULL)
            *type = SALCFGTYPE_NOTFOUND;
        return FALSE;
    }
    char auxBuf[500];
    int auxType = SALCFGTYPE_BOOL;
    int auxDataSize = 4;
    BOOL ret = TRUE;
    switch (paramID)
    {
    case SALCFG_SELOPINCLUDEDIRS:
        *((DWORD*)auxBuf) = (DWORD)Configuration.IncludeDirs;
        break;
    case SALCFG_SAVEONEXIT:
        *((DWORD*)auxBuf) = (DWORD)Configuration.AutoSave;
        break;
    case SALCFG_MINBEEPWHENDONE:
        *((DWORD*)auxBuf) = (DWORD)Configuration.MinBeepWhenDone;
        break;
    case SALCFG_HIDEHIDDENORSYSTEMFILES:
        *((DWORD*)auxBuf) = (DWORD)Configuration.NotHiddenSystemFiles;
        break;
    case SALCFG_ALWAYSONTOP:
        *((DWORD*)auxBuf) = (DWORD)Configuration.AlwaysOnTop;
        break;
        //    case SALCFG_FASTDIRMOVE: *((DWORD *)auxBuf) = (DWORD)Configuration.FastDirectoryMove; break;
    case SALCFG_SORTUSESLOCALE:
        *((DWORD*)auxBuf) = (DWORD)Configuration.SortUsesLocale;
        break;
    case SALCFG_SORTDETECTNUMBERS:
        *((DWORD*)auxBuf) = (DWORD)Configuration.SortDetectNumbers;
        break;
    case SALCFG_SORTBYEXTDIRSASFILES:
        *((DWORD*)auxBuf) = (DWORD)Configuration.SortDirsByExt;
        break;
    case SALCFG_SINGLECLICK:
        *((DWORD*)auxBuf) = (DWORD)Configuration.SingleClick;
        break;
    case SALCFG_TOPTOOLBARVISIBLE:
        *((DWORD*)auxBuf) = (DWORD)Configuration.TopToolBarVisible;
        break;
    case SALCFG_MIDDLETOOLBARVISIBLE:
        *((DWORD*)auxBuf) = (DWORD)Configuration.MiddleToolBarVisible;
        break;
    case SALCFG_BOTTOMTOOLBARVISIBLE:
        *((DWORD*)auxBuf) = (DWORD)Configuration.BottomToolBarVisible;
        break;
    case SALCFG_USERMENUTOOLBARVISIBLE:
        *((DWORD*)auxBuf) = (DWORD)Configuration.UserMenuToolBarVisible;
        break;
    case SALCFG_SAVEHISTORY:
        *((DWORD*)auxBuf) = (DWORD)Configuration.SaveHistory;
        break;
    case SALCFG_ENABLECMDLINEHISTORY:
        *((DWORD*)auxBuf) = (DWORD)Configuration.EnableCmdLineHistory;
        break;
    case SALCFG_SAVECMDLINEHISTORY:
        *((DWORD*)auxBuf) = (DWORD)Configuration.SaveCmdLineHistory;
        break;
    case SALCFG_SIZEFORMAT:
        *((DWORD*)auxBuf) = (DWORD)Configuration.SizeFormat;
        break;
    case SALCFG_SELECTWHOLENAME:
        *((DWORD*)auxBuf) = (DWORD)Configuration.QuickRenameSelectAll;
        break;

    case SALCFG_FILENAMEFORMAT:
    {
        auxType = SALCFGTYPE_INT;
        auxDataSize = 4;
        *((DWORD*)auxBuf) = (DWORD)Configuration.FileNameFormat;
        break;
    }

    case SALCFG_USERECYCLEBIN:
    {
        auxType = SALCFGTYPE_INT;
        auxDataSize = 4;
        *((DWORD*)auxBuf) = (DWORD)Configuration.UseRecycleBin;
        break;
    }

    case SALCFG_COMPDIRSUSETIMERES:
        *((DWORD*)auxBuf) = (DWORD)Configuration.UseTimeResolution;
        break;

    case SALCFG_COMPDIRTIMERES:
    {
        auxType = SALCFGTYPE_INT;
        auxDataSize = 4;
        *((DWORD*)auxBuf) = (DWORD)Configuration.TimeResolution;
        break;
    }

    case SALCFG_CNFRMFILEDIRDEL:
        *((DWORD*)auxBuf) = (DWORD)Configuration.CnfrmFileDirDel;
        break;
    case SALCFG_CNFRMNEDIRDEL:
        *((DWORD*)auxBuf) = (DWORD)Configuration.CnfrmNEDirDel;
        break;
    case SALCFG_CNFRMFILEOVER:
        *((DWORD*)auxBuf) = (DWORD)Configuration.CnfrmFileOver;
        break;
    case SALCFG_CNFRMDIROVER:
        *((DWORD*)auxBuf) = (DWORD)Configuration.CnfrmDirOver;
        break;
    case SALCFG_CNFRMSHFILEDEL:
        *((DWORD*)auxBuf) = (DWORD)Configuration.CnfrmSHFileDel;
        break;
    case SALCFG_CNFRMSHDIRDEL:
        *((DWORD*)auxBuf) = (DWORD)Configuration.CnfrmSHDirDel;
        break;
    case SALCFG_CNFRMSHFILEOVER:
        *((DWORD*)auxBuf) = (DWORD)Configuration.CnfrmSHFileOver;
        break;
    case SALCFG_CNFRMCREATEPATH:
        *((DWORD*)auxBuf) = (DWORD)Configuration.CnfrmCreatePath;
        break;
    case SALCFG_DRVSPECFLOPPYMON:
        *((DWORD*)auxBuf) = (DWORD)Configuration.DrvSpecFloppyMon;
        break;
    case SALCFG_DRVSPECFLOPPYSIM:
        *((DWORD*)auxBuf) = (DWORD)Configuration.DrvSpecFloppySimple;
        break;
    case SALCFG_DRVSPECREMOVABLEMON:
        *((DWORD*)auxBuf) = (DWORD)Configuration.DrvSpecRemovableMon;
        break;
    case SALCFG_DRVSPECREMOVABLESIM:
        *((DWORD*)auxBuf) = (DWORD)Configuration.DrvSpecRemovableSimple;
        break;
    case SALCFG_DRVSPECFIXEDMON:
        *((DWORD*)auxBuf) = (DWORD)Configuration.DrvSpecFixedMon;
        break;
    case SALCFG_DRVSPECFIXEDSIMPLE:
        *((DWORD*)auxBuf) = (DWORD)Configuration.DrvSpecFixedSimple;
        break;
    case SALCFG_DRVSPECREMOTEMON:
        *((DWORD*)auxBuf) = (DWORD)Configuration.DrvSpecRemoteMon;
        break;
    case SALCFG_DRVSPECREMOTESIMPLE:
        *((DWORD*)auxBuf) = (DWORD)Configuration.DrvSpecRemoteSimple;
        break;
    case SALCFG_DRVSPECREMOTEDONOTREF:
        *((DWORD*)auxBuf) = (DWORD)Configuration.DrvSpecRemoteDoNotRefreshOnAct;
        break;
    case SALCFG_DRVSPECCDROMMON:
        *((DWORD*)auxBuf) = (DWORD)Configuration.DrvSpecCDROMMon;
        break;
    case SALCFG_DRVSPECCDROMSIMPLE:
        *((DWORD*)auxBuf) = (DWORD)Configuration.DrvSpecCDROMSimple;
        break;

    case SALCFG_VIEWEREOLCRLF:
        *((DWORD*)auxBuf) = (DWORD)Configuration.EOL_CRLF;
        break;
    case SALCFG_VIEWEREOLCR:
        *((DWORD*)auxBuf) = (DWORD)Configuration.EOL_CR;
        break;
    case SALCFG_VIEWEREOLLF:
        *((DWORD*)auxBuf) = (DWORD)Configuration.EOL_LF;
        break;
    case SALCFG_VIEWEREOLNULL:
        *((DWORD*)auxBuf) = (DWORD)Configuration.EOL_NULL;
        break;
    case SALCFG_VIEWERSAVEPOSITION:
        *((DWORD*)auxBuf) = (DWORD)Configuration.SavePosition;
        break;
    case SALCFG_VIEWERWRAPTEXT:
        *((DWORD*)auxBuf) = (DWORD)Configuration.WrapText;
        break;
    case SALCFG_AUTOCOPYSELTOCLIPBOARD:
        *((DWORD*)auxBuf) = (DWORD)Configuration.AutoCopySelection;
        break;

    case SALCFG_VIEWERTABSIZE:
    {
        auxType = SALCFGTYPE_INT;
        auxDataSize = 4;
        *((DWORD*)auxBuf) = (DWORD)Configuration.TabSize;
        break;
    }

    case SALCFG_VIEWERFONT:
    {
        auxType = SALCFGTYPE_LOGFONT;
        auxDataSize = sizeof(LOGFONT);
        if (UseCustomViewerFont)
            *((LOGFONT*)auxBuf) = ViewerLogFont;
        else
            GetDefaultViewerLogFont((LOGFONT*)auxBuf);
        break;
    }

    case SALCFG_ARCOTHERPANELFORPACK:
        *((DWORD*)auxBuf) = (DWORD)Configuration.UseAnotherPanelForPack;
        break;
    case SALCFG_ARCOTHERPANELFORUNPACK:
        *((DWORD*)auxBuf) = (DWORD)Configuration.UseAnotherPanelForUnpack;
        break;
    case SALCFG_ARCSUBDIRBYARCFORUNPACK:
        *((DWORD*)auxBuf) = (DWORD)Configuration.UseSubdirNameByArchiveForUnpack;
        break;
    case SALCFG_ARCUSESIMPLEICONS:
        *((DWORD*)auxBuf) = (DWORD)Configuration.UseSimpleIconsInArchives;
        break;

    default:
    {
        auxType = SALCFGTYPE_NOTFOUND;
        auxDataSize = 0;
        TRACE_E("Unknown parameter ID (" << paramID << ") in CSalamanderGeneral::GetConfigParameter().");
        ret = FALSE;
    }
    }
    if (type != NULL)
        *type = auxType;
    if (auxDataSize > 0 && auxDataSize <= bufferSize)
        memcpy(buffer, auxBuf, auxDataSize);
    else
    {
        if (bufferSize > 0 && auxDataSize > 0) // copy at least what fits
        {
            memcpy(buffer, auxBuf, bufferSize);
            if (auxType == SALCFGTYPE_STRING)
                ((char*)buffer)[bufferSize - 1] = 0; // trim the string with a zero
        }
        ret = FALSE;
    }
    return ret;
}

BOOL CSalamanderGeneral::GetConfigParameterString(int paramID,
                                                  CSalamanderStringBuffer* value)
{
    if (value == NULL || MainThreadID != GetCurrentThreadId())
    {
        SetLastError(value == NULL ? ERROR_INVALID_PARAMETER : ERROR_INVALID_THREAD_ID);
        return FALSE;
    }
    try
    {
        std::wstring result;
        switch (paramID)
        {
        case SALCFG_INFOLINECONTENT:
            result = Configuration.InfoLineContent;
            break;
        case SALCFG_RECYCLEBINMASKS:
            result = Configuration.RecycleMasks.GetMasksString();
            break;
        case SALCFG_IFPATHISINACCESSIBLEGOTO:
            GetIfPathIsInaccessibleGoToW(result);
            break;
        default:
            SetLastError(ERROR_NOT_FOUND);
            return FALSE;
        }
        return sally::plugin_abi::WriteStringBuffer(*value, result);
    }
    catch (const std::bad_alloc&)
    {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
    }
    catch (const std::length_error&)
    {
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
    }
    catch (...)
    {
        SetLastError(ERROR_GEN_FAILURE);
    }
    return FALSE;
}

BOOL CSalamanderGeneral::AlterFileName(const wchar_t* srcName, int format,
                                      int changedParts, BOOL isDir,
                                      CSalamanderStringBuffer* targetName)
{
    if (targetName == nullptr || srcName == nullptr)
        return FALSE;

    CALL_STACK_MESSAGE5("CSalamanderGeneral::AlterFileName(, %ls, %d, %d, %d)",
                        srcName, format, changedParts, isDir);
    const std::wstring altered = ::AlterFileNameW(
        srcName, format, changedParts, isDir != FALSE);
    return sally::plugin_abi::WriteStringBuffer(*targetName, altered);
}

void CSalamanderGeneral::CreateSafeWaitWindow(const wchar_t* message, const wchar_t* caption,
                                              int delay, BOOL showCloseButton, HWND hForegroundWnd)
{
    CALL_STACK_MESSAGE6("CSalamanderGeneral::CreateSafeWaitWindow(%ls, %ls, %d, %d, 0x%p)",
                        message ? message : L"(null)",
                        caption ? caption : L"(null)",
                        delay, showCloseButton, hForegroundWnd);
    ::CreateSafeWaitWindow(message, caption, delay, showCloseButton, hForegroundWnd);
}

void CSalamanderGeneral::DestroySafeWaitWindow()
{
    CALL_STACK_MESSAGE1("CSalamanderGeneral::DestroySafeWaitWindow()");
    ::DestroySafeWaitWindow();
}

void CSalamanderGeneral::ShowSafeWaitWindow(BOOL show)
{
    CALL_STACK_MESSAGE2("CSalamanderGeneral::ShowSafeWaitWindow(%d)", show);
    ::ShowSafeWaitWindow(show);
}

BOOL CSalamanderGeneral::GetSafeWaitWindowClosePressed()
{
    CALL_STACK_MESSAGE1("CSalamanderGeneral::GetSafeWaitWindowClosePressed()");
    return ::GetSafeWaitWindowClosePressed();
}

// The conversion that was left here is now gone - it existed only
// because the ABI was narrow while the wait window was already wide, and it
// said so at the time ("dies when the ABI widens"). This is that moment: the
// internal never had a narrow form, so widening the ABI removed a CP_ACP round
// trip outright rather than relocating one.
void CSalamanderGeneral::SetSafeWaitWindowText(const wchar_t* message)
{
    CALL_STACK_MESSAGE2("CSalamanderGeneral::SetSafeWaitWindowText(%ls)", message ? message : L"(null)");
    ::SetSafeWaitWindowText(message);
}

BOOL CSalamanderGeneral::GetFileFromCache(const wchar_t* uniqueFileName, const wchar_t*& tmpName,
                                          HANDLE fileLock)
{
    CALL_STACK_MESSAGE2("CSalamanderGeneral::GetFileFromCache(%ls, ,)", uniqueFileName ? uniqueFileName : L"(null)");
    tmpName = NULL;
    if (uniqueFileName == NULL || fileLock == NULL)
    {
        TRACE_E("Invalid parameter in CSalamanderGeneral::GetFileFromCache!");
        return FALSE;
    }

    BOOL fileExists;
    const wchar_t* name = DiskCache.GetName(uniqueFileName, NULL, &fileExists, FALSE, NULL, FALSE, NULL, NULL);
    if (name != NULL) // file found
    {
        if (!fileExists) // some helpful soul deleted it straight from the disk
        {
            // cannot prepare the file; tell the disk cache we give up
            DiskCache.ReleaseName(uniqueFileName, FALSE);
        }
        else
        {
            if (DiskCache.AssignName(uniqueFileName, fileLock, FALSE, crtCache))
            {
                tmpName = name;
                return TRUE;
            }
        }
    }
    return FALSE;
}

void CSalamanderGeneral::UnlockFileInCache(HANDLE fileLock)
{
    CALL_STACK_MESSAGE2("CSalamanderGeneral::UnlockFileInCache(0x%p)", fileLock);

    SetEvent(fileLock); // start cleaning up the file
    DiskCache.WaitForIdle();
    ResetEvent(fileLock); // finish cleaning up the file
}

BOOL CSalamanderGeneral::MoveFileToCache(const wchar_t* uniqueFileName, const wchar_t* nameInCache,
                                         const wchar_t* rootTmpPath, const wchar_t* newFileName,
                                         const CQuadWord& newFileSize, BOOL* alreadyExists)
{
    CALL_STACK_MESSAGE6("CSalamanderGeneral::MoveFileToCache(%ls, %ls, %ls, %ls, %g, )",
                        uniqueFileName ? uniqueFileName : L"(null)",
                        nameInCache ? nameInCache : L"(null)",
                        rootTmpPath ? rootTmpPath : L"(null)",
                        newFileName ? newFileName : L"(null)", newFileSize.GetDouble());
    if (alreadyExists != NULL)
        *alreadyExists = FALSE;
    if (uniqueFileName == NULL || newFileName == NULL || nameInCache == NULL)
    {
        TRACE_E("Invalid parameter in CSalamanderGeneral::GetFileFromCache!");
        return FALSE;
    }

    // verify that 'nameInCache' is valid (a name without a path)
    const wchar_t* s = nameInCache;
    while (*s != 0 && *s != L'\\' && *s != L'/' && *s != L':' &&
           *s >= 32 && *s != L'<' && *s != L'>' && *s != L'|' && *s != L'"')
        s++;
    if (nameInCache[0] == 0 || *s != 0)
    {
        TRACE_E("Unexpected value of 'nameInCache' in CSalamanderGeneral::MoveFileToCache!");
        return FALSE;
    }

    // add the file 'newFileName' to the disk cache under the name 'uniqueFileName'
    BOOL exists;
    const wchar_t* fileName = DiskCache.GetName(uniqueFileName, nameInCache, &exists, TRUE, rootTmpPath, FALSE, NULL, NULL);
    if (fileName == NULL) // error (if 'exists' is TRUE -> fatal, otherwise "file already exists")
    {
        if (alreadyExists != NULL)
            *alreadyExists = !exists;
        return FALSE;
    }

    if (!::SalMoveFile(newFileName, fileName))
    {
        DWORD err = GetLastError();
        TRACE_EW(L"Unable to move file to disk cache! (error " << ::GetErrorTextOwned(err).c_str() << L")");
        DiskCache.ReleaseName(uniqueFileName, FALSE); // nothing to keep in the cache
        return FALSE;
    }
    else // successfully obtained a temp file; we must call NamePrepared()
    {
        DiskCache.NamePrepared(uniqueFileName, newFileSize);
        DiskCache.ReleaseName(uniqueFileName, TRUE); // leave the prepared file in the cache (even if it is not locked)
        return TRUE;
    }
}

void CSalamanderGeneral::RemoveOneFileFromCache(const wchar_t* uniqueFileName)
{
    CALL_STACK_MESSAGE2("CSalamanderGeneral::RemoveOneFileFromCache(%ls)",
                        uniqueFileName ? uniqueFileName : L"(null)");
    if (uniqueFileName == NULL)
    {
        TRACE_E("Invalid parametr (NULL) in CSalamanderGeneral::RemoveOneFileFromCache!");
        return;
    }
    DiskCache.FlushOneFile(uniqueFileName);
}

void CSalamanderGeneral::RemoveFilesFromCache(const wchar_t* fileNamesRoot)
{
    CALL_STACK_MESSAGE2("CSalamanderGeneral::RemoveFilesFromCache(%ls)",
                        fileNamesRoot ? fileNamesRoot : L"(null)");
    if (fileNamesRoot == NULL)
    {
        TRACE_E("Invalid parametr (NULL) in CSalamanderGeneral::RemoveFilesFromCache!");
        return;
    }
    DiskCache.FlushCache(fileNamesRoot);
}

BOOL CSalamanderGeneral::EnumConversionTables(HWND parent, int* index, const wchar_t** name, const char** table)
{
    if (index == NULL)
    {
        TRACE_E("Unexpected value of 'index' (NULL) in CSalamanderGeneral::EnumConversionTables().");
        return FALSE;
    }
    CALL_STACK_MESSAGE2("CSalamanderGeneral::EnumConversionTables(, %d, ,)", *index);
    parent = (parent == NULL ? MainWindow->HWindow : parent);
    return CodeTables.EnumCodeTables(parent, index, name, table);
}

BOOL CSalamanderGeneral::GetConversionTable(HWND parent, char* table, const wchar_t* conversion)
{
    CALL_STACK_MESSAGE2("CSalamanderGeneral::GetConversionTable(, , %ls)", conversion);
    if (table == NULL)
    {
        TRACE_E("Invalid parametr (table==NULL) in CSalamanderGeneral::GetConversionTable!");
        return FALSE;
    }
    parent = (parent == NULL ? MainWindow->HWindow : parent);
    BOOL ret = CodeTables.Init(parent);
    int codeType;
    if (ret)
        ret &= CodeTables.GetCodeType(conversion, codeType);
    if (ret)
        ret &= CodeTables.GetCode(table, codeType);
    return ret;
}

BOOL CSalamanderGeneral::GetWindowsCodePage(
    HWND parent, CSalamanderStringBuffer* codePage)
{
    CALL_STACK_MESSAGE1("CSalamanderGeneral::GetWindowsCodePage(,)");
    parent = (parent == NULL ? MainWindow->HWindow : parent);
    CodeTables.Init(parent);
    return codePage != NULL && sally::plugin_abi::WriteStringBuffer(
                                   *codePage, CodeTables.GetWinCodePage());
}

void CSalamanderGeneral::RecognizeFileType(HWND parent, const char* pattern,
                                           int patternLen, BOOL forceText,
                                           BOOL* isText,
                                           CSalamanderStringBuffer* codePage)
{
    CALL_STACK_MESSAGE3("CSalamanderGeneral::RecognizeFileType(, , %d, %d, ,)", patternLen, forceText);
    parent = (parent == NULL ? MainWindow->HWindow : parent);
    std::wstring codePageValue;
    ::RecognizeFileType(parent, pattern, patternLen, forceText, isText,
                        codePage != NULL ? &codePageValue : NULL);
    if (codePage != NULL)
        sally::plugin_abi::WriteStringBuffer(*codePage, codePageValue);
}

BOOL CSalamanderGeneral::IsANSIText(const char* text, int textLen)
{
    CALL_STACK_MESSAGE2("CSalamanderGeneral::IsANSIText(, %d)", textLen);

    const unsigned char* s = (const unsigned char*)text;
    const unsigned char* end = s + textLen;
    while (s < end)
    {
        if (*s < ' ' && *s != '\a' && *s != '\b' && *s != '\r' && // *s != 0 must not be here because of Unicode files ("0A 00" cannot be expanded to "0D 0A 00")
            *s != '\f' && *s != '\n' && *s != '\t' && *s != '\v' &&
            *s != '\x1a' && *s != '\x04' && *s != '\x06')
        { // disallowed character
            break;
        }
        s++;
    }
    return s == end;
}

// wide. %S is a wide string in a narrow format string under MSVC.
BOOL CSalamanderGeneral::SalMoveFile(const wchar_t* srcName, const wchar_t* destName, DWORD* err)
{
    CALL_STACK_MESSAGE3("CSalamanderGeneral::SalMoveFile(%S, %S,)", srcName, destName);
    BOOL ret = ::SalMoveFile(srcName, destName);
    if (err != NULL)
        *err = GetLastError();
    return ret;
}

BOOL CSalamanderGeneral::SalGetFileSize(HANDLE file, CQuadWord& size, DWORD& err)
{
    CALL_STACK_MESSAGE1("CSalamanderGeneral::SalGetFileSize(, ,)");
    return ::SalGetFileSize(file, size, err);
}

BOOL CSalamanderGeneral::GetTargetDirectory(HWND parent, HWND hCenterWindow, const wchar_t* title,
                                            const wchar_t* comment, CSalamanderStringBuffer* path,
                                            BOOL onlyNet, const wchar_t* initDir)
{
    CALL_STACK_MESSAGE2("CSalamanderGeneral::GetTargetDirectory(, , , , , %d,)", onlyNet);
    if (path == NULL)
        return FALSE;

    // Straight to the wide core. Note this bypasses the narrow ::GetTargetDirectory
    // entirely, which is deliberate: that one takes an unsized char* and reaches
    // ResolveNetHoodPath, so routing plugins through it is what made the documented MAX_PATH
    // contract unsafe. 'title'/'initDir' may ALIAS 'path' (ftp does exactly that), so nothing is
    // written to 'path' until the dialog has closed and both have been consumed.
    std::wstring result;
    if (!::GetTargetDirectoryW(parent, hCenterWindow, title, comment, result, onlyNet, initDir))
        return FALSE;

    return sally::plugin_abi::WriteStringBuffer(*path, result);
}

void CSalamanderGeneral::CallPluginOperationFromDisk(int panel, SalPluginOperationFromDisk callback,
                                                     void* param)
{
    CALL_STACK_MESSAGE2("CSalamanderGeneral::CallPluginOperationFromDisk(%d, ,)", panel);
    if (MainThreadID != GetCurrentThreadId())
    {
        TRACE_E("You can call CSalamanderGeneral::CallPluginOperationFromDisk() only from main thread!");
        return;
    }
    if (callback == NULL)
    {
        TRACE_E("Unexpected value of parameter 'callback' (NULL) in CSalamanderGeneral::CallPluginOperationFromDisk().");
        return;
    }
    CFilesWindow* p = GetPanel(panel);
    if (p != NULL)
    {
        if (!p->Is(ptDisk))
        {
            TRACE_E("CSalamanderGeneral::CallPluginOperationFromDisk(): there must be windows (disk) path in panel!");
            return;
        }
        if (p->Files->Count + p->Dirs->Count <= 0)
        {
            TRACE_I("CSalamanderGeneral::CallPluginOperationFromDisk(): no items in panel!");
            return;
        }
        // prepare data for enumerating files and directories from the panel
        CPanelTmpEnumData data;
        int oneIndex = -1;
        int count = p->GetSelCount();
        if (count > 0) // some files are selected
        {
            data.IndexesCount = count;
            data.Indexes = new int[count];
            if (data.Indexes == NULL)
            {
                TRACE_E(LOW_MEMORY);
                return;
            }
            else
                p->GetSelItems(count, data.Indexes);
        }
        else // take the focus
        {
            oneIndex = p->GetCaretIndex();

            BOOL subDir;
            if (p->Dirs->Count > 0)
                subDir = (wcscmp(p->Dirs->At(0).Name, L"..") == 0);
            else
                subDir = FALSE;
            if (oneIndex == 0 && subDir)
            {
                TRACE_E("Unexpected situation in CSalamanderGeneral::CallPluginOperationFromDisk(): no files nor directories selected and focus is on up-dir symbol.");
                return;
            }

            data.IndexesCount = 1;
            data.Indexes = &oneIndex; // not deallocated
        }
        data.CurrentIndex = 0;
        data.ZIPPath = p->GetZIPPath();
        data.Dirs = p->Dirs;
        data.Files = p->Files;
        data.ArchiveDir = p->GetArchiveDir();
        data.WorkPathW = p->GetPathW();
        data.EnumLastDir = NULL;
        data.EnumLastIndex = -1;

        callback(p->GetPathW(), PanelEnumDiskSelection, &data, param);

        if (count > 0)
            delete[] (data.Indexes);
    }
}

BYTE CSalamanderGeneral::GetUserDefaultCharset()
{
    return (BYTE)UserCharset;
}

class CSalamanderBMSearchDataImp : public CSalamanderBMSearchData
{
protected:
    CSearchData Moore;

public:
    CSalamanderBMSearchDataImp() : Moore() {}

    virtual void WINAPI Set(const char* pattern, WORD flags) { Moore.Set(pattern, flags); }
    virtual void WINAPI Set(const char* pattern, const int length, WORD flags) { Moore.Set(pattern, length, flags); }
    virtual void WINAPI SetFlags(WORD flags) { Moore.SetFlags(flags); }
    virtual int WINAPI GetLength() const { return Moore.GetLength(); }
    virtual const char* WINAPI GetPattern() const { return Moore.GetPattern(); }
    virtual BOOL WINAPI IsGood() const { return Moore.IsGood(); }
    virtual int WINAPI SearchForward(const char* text, int length, int start) { return Moore.SearchForward(text, length, start); }
    virtual int WINAPI SearchBackward(const char* text, int length) { return Moore.SearchBackward(text, length); }
};

CSalamanderBMSearchData*
CSalamanderGeneral::AllocSalamanderBMSearchData()
{
    CALL_STACK_MESSAGE1("CSalamanderGeneral::AllocSalamanderBMSearchData()");
    CSalamanderBMSearchData* ret = new CSalamanderBMSearchDataImp;
    if (ret == NULL)
        TRACE_E(LOW_MEMORY);
    return ret;
}

void CSalamanderGeneral::FreeSalamanderBMSearchData(CSalamanderBMSearchData* data)
{
    CALL_STACK_MESSAGE1("CSalamanderGeneral::FreeSalamanderBMSearchData()");
    if (data != NULL)
        delete ((CSalamanderBMSearchDataImp*)data);
}

class CSalamanderREGEXPSearchDataImp : public CSalamanderREGEXPSearchData
{
protected:
    CRegularExpression REGEXP;

    // Both Set and SetFlags rebuild the folded pattern, so the encoding has to be chosen
    // before either of them runs - not once at construction, since a plugin may call
    // SetFlags again with a different encoding on the same object.
    void ApplyFoldEncoding(WORD flags)
    {
        REGEXP.SetFoldEncoding((flags & SASF_UTF8) != 0 ? CRegularExpression::FoldEncoding::Utf8
                                                        : CRegularExpression::FoldEncoding::Acp);
    }

public:
    CSalamanderREGEXPSearchDataImp() : REGEXP() {}

    virtual BOOL WINAPI Set(const char* pattern, WORD flags)
    {
        ApplyFoldEncoding(flags);
        return REGEXP.Set(pattern, flags);
    }
    virtual BOOL WINAPI SetFlags(WORD flags)
    {
        ApplyFoldEncoding(flags);
        return REGEXP.SetFlags(flags);
    }
    virtual const char* WINAPI GetLastErrorText() const { return REGEXP.GetLastErrorText(); }
    virtual const char* WINAPI GetPattern() const { return REGEXP.GetPattern(); }
    virtual BOOL WINAPI SetLine(const char* start, const char* end)
    {
        REGEXP.SetLine(start, end);
        return TRUE;
    }
    virtual int WINAPI SearchForward(int start, int& foundLen) { return REGEXP.SearchForward(start, foundLen); }
    virtual int WINAPI SearchBackward(int length, int& foundLen) { return REGEXP.SearchBackward(length, foundLen); }
};

CSalamanderREGEXPSearchData*
CSalamanderGeneral::AllocSalamanderREGEXPSearchData()
{
    CALL_STACK_MESSAGE1("CSalamanderGeneral::AllocSalamanderREGEXPSearchData()");
    CSalamanderREGEXPSearchData* ret = new CSalamanderREGEXPSearchDataImp;
    return ret;
}

void CSalamanderGeneral::FreeSalamanderREGEXPSearchData(CSalamanderREGEXPSearchData* data)
{
    CALL_STACK_MESSAGE1("CSalamanderGeneral::FreeSalamanderREGEXPSearchData()");
    if (data != NULL)
        delete ((CSalamanderREGEXPSearchDataImp*)data);
}

struct CSalCommandsAux
{
    int SalCmd;
    int Cmd;
    int TextID;
    DWORD* Enabled; // NULL == "always TRUE"
    int Type;
};

CSalCommandsAux SalCommandsArray[] = // ends with an item whose 'SalCmd' == -1
    {
        {SALCMD_VIEW, CM_VIEW, IDS_MENU_FILES_VIEW, &EnablerViewFile, sctyForFocusedFile},
        {SALCMD_ALTVIEW, CM_ALTVIEW, IDS_MENU_FILES_ALTVIEW, &EnablerViewFile, sctyForFocusedFile},
        {SALCMD_VIEWWITH, CM_VIEW_WITH, IDS_SALCMD_VIEWWITH, &EnablerViewFile, sctyForFocusedFile},
        {SALCMD_EDIT, CM_EDIT, IDS_MENU_FILES_EDIT, &EnablerFileOnDiskOrArchive, sctyForFocusedFile},
        {SALCMD_EDITWITH, CM_EDIT_WITH, IDS_SALCMD_EDITWITH, &EnablerFileOnDiskOrArchive, sctyForFocusedFile},

        {SALCMD_OPEN, CM_OPEN, IDS_SALCMD_OPEN, NULL, sctyForFocusedFileOrDirectory},
        {SALCMD_QUICKRENAME, CM_RENAMEFILE, IDS_MENU_FILES_RENAME, &EnablerQuickRename, sctyForFocusedFileOrDirectory},

        {SALCMD_COPY, CM_COPYFILES, IDS_MENU_FILES_COPY, &EnablerFilesCopy, sctyForSelectedFilesAndDirectories},
        {SALCMD_MOVE, CM_MOVEFILES, IDS_MENU_FILES_MOVE, &EnablerFilesMove, sctyForSelectedFilesAndDirectories},
        {SALCMD_EMAIL, CM_EMAILFILES, IDS_MENU_FILES_EMAIL, &EnablerFilesOnDisk, sctyForSelectedFilesAndDirectories},
        {SALCMD_DELETE, CM_DELETEFILES, IDS_MENU_FILES_DELETE, &EnablerFilesDelete, sctyForSelectedFilesAndDirectories},
        {SALCMD_PROPERTIES, CM_PROPERTIES, IDS_MENU_FILES_PROPERTIES, &EnablerShowProperties, sctyForSelectedFilesAndDirectories},
        {SALCMD_CHANGECASE, CM_CHANGECASE, IDS_MENU_FILES_CHANGECASE, &EnablerFilesOnDisk, sctyForSelectedFilesAndDirectories},
        {SALCMD_CHANGEATTRS, CM_CHANGEATTR, IDS_MENU_FILES_CHANGEATTR, &EnablerChangeAttrs, sctyForSelectedFilesAndDirectories},
        {SALCMD_OCCUPIEDSPACE, CM_OCCUPIEDSPACE, IDS_MENU_CMD_OCCUPIED, &EnablerOccupiedSpace, sctyForSelectedFilesAndDirectories},

        {SALCMD_EDITNEWFILE, CM_EDITNEW, IDS_MENU_FILES_EDITNEW, &EnablerOnDisk, sctyForCurrentPath},
        {SALCMD_REFRESH, CM_ACTIVEREFRESH, IDS_MENU_LEFT_REFRESH, NULL, sctyForCurrentPath},
        {SALCMD_CREATEDIRECTORY, CM_CREATEDIR, IDS_MENU_CMD_CREATEDIR, &EnablerCreateDir, sctyForCurrentPath},
        {SALCMD_DRIVEINFO, CM_DRIVEINFO, IDS_MENU_CMD_DRIVEINFO, &EnablerDriveInfo, sctyForCurrentPath},
        {SALCMD_CALCDIRSIZES, CM_CALCDIRSIZES, IDS_MENU_CMD_CALCDIRSIZES, &EnablerCalcDirSizes, sctyForCurrentPath},

        {SALCMD_DISCONNECT, CM_DISCONNECTNET, IDS_MENU_CMD_DISCONNECTNET, NULL, sctyForConnectedDrivesAndFS},

        {-1, -1, -1, NULL, sctyUnknown} // terminator
};

int GetWMCommandFromSalCmd(int salCmd)
{
    int index = 0;
    while (SalCommandsArray[index].SalCmd != -1)
    {
        if (SalCommandsArray[index].SalCmd == salCmd)
            return SalCommandsArray[index].Cmd;
        index++;
    }
    TRACE_E("You have used CSalamanderGeneral::PostSalamanderCommand for invalid command (" << salCmd << ")!");
    return -1;
}

BOOL CSalamanderGeneral::GetSalamanderCommand(
    int salCmd, CSalamanderStringBuffer* name, BOOL* enabled, int* type)
{
    CALL_STACK_MESSAGE2("CSalamanderGeneral::GetSalamanderCommand(%d, , ,)", salCmd);
    int index = 0;
    while (SalCommandsArray[index].SalCmd != -1)
    {
        if (SalCommandsArray[index].SalCmd == salCmd) // found it
        {
            // need to compute the command states; SalCommandsArray uses them
            MainWindow->OnEnterIdle();

            if (name != NULL && !sally::plugin_abi::WriteStringBuffer(
                                    *name, ::LoadStrW(SalCommandsArray[index].TextID)))
                return FALSE;
            if (SalCommandsArray[index].Enabled != NULL)
            {
                if (enabled != NULL)
                    *enabled = *(SalCommandsArray[index].Enabled);
            }
            if (type != NULL)
                *type = SalCommandsArray[index].Type;

            return TRUE;
        }
        index++;
    }
    return FALSE;
}

BOOL CSalamanderGeneral::EnumSalamanderCommands(
    int* index, int* salCmd, CSalamanderStringBuffer* name, BOOL* enabled,
    int* type)
{
    CALL_STACK_MESSAGE1("CSalamanderGeneral::EnumSalamanderCommands(, , , ,)");
    if (salCmd != NULL)
        *salCmd = -1;
    if (name != NULL &&
        !sally::plugin_abi::WriteStringBuffer(*name, std::wstring()))
        return FALSE;
    if (enabled != NULL)
        *enabled = TRUE;
    if (type != NULL)
        *type = sctyUnknown;

    if (index != NULL && *index >= 0 && SalCommandsArray[*index].SalCmd != -1)
    {
        if (*index == 0)
        {
            // need to compute the command states; SalCommandsArray uses them
            MainWindow->OnEnterIdle();
        }

        if (salCmd != NULL)
            *salCmd = SalCommandsArray[*index].SalCmd;
        if (name != NULL && !sally::plugin_abi::WriteStringBuffer(
                                *name, ::LoadStrW(SalCommandsArray[*index].TextID)))
            return FALSE;
        if (SalCommandsArray[*index].Enabled != NULL)
        {
            if (enabled != NULL)
                *enabled = *(SalCommandsArray[*index].Enabled);
        }
        if (type != NULL)
            *type = SalCommandsArray[*index].Type;

        (*index)++;
        return TRUE;
    }
    return FALSE;
}

void CSalamanderGeneral::PostSalamanderCommand(int salCmd)
{
    CALL_STACK_MESSAGE2("CSalamanderGeneral::PostSalamanderCommand(%d)", salCmd);
    if (salCmd < 0 || salCmd >= 500)
    {
        TRACE_E("CSalamanderGeneral::PostSalamanderCommand: salCmd is invalid (" << salCmd << " is not in range 0-499).");
        return;
    }

    if (MainThreadID == GetCurrentThreadId())
    { // because of calls from the entry point where Plugin is set to -1 (just to look up plugin data)
        // before WM_USER_POSTCMDORUNLOADPLUGIN would arrive, Plugin would be reset (according to the entry point's return value)
        CPluginData* data = Plugins.GetPluginData(Plugin);
        if (data != NULL)
        {
            data->Commands.Add(salCmd);
            ExecCmdsOrUnloadMarkedPlugins = TRUE;
        }
        else
        {
            TRACE_E("Unexpected situation in CSalamanderGeneral::PostSalamanderCommand().");
        }
    }
    else // outside the entry point the Plugin is certainly set...
    {
        if (MainWindow != NULL && MainWindow->HWindow != NULL)
        {
            // check for a call while the entry point is starting (Plugin is set to -1)
            if ((INT_PTR)Plugin == -1)
            {
                TRACE_E("You can call CSalamanderGeneral::PostSalamanderCommand only from main "
                        "thread when plugin entry-point is not finished yet!");
            }
            else
            { // 0 - unload, 1 - rebuild menu, 2-501 salCmd, 502-1000501 menuCmd
                PostMessage(MainWindow->HWindow, WM_USER_POSTCMDORUNLOADPLUGIN, (WPARAM)Plugin, 2 + salCmd);
            }
        }
        else
        {
            TRACE_E("Unexpected situation (2) in CSalamanderGeneral::PostSalamanderCommand().");
        }
    }
}

void CSalamanderGeneral::SetUserWorkedOnPanelPath(int panel)
{
    CALL_STACK_MESSAGE2("CSalamanderGeneral::SetUserWorkedOnPanelPath(%d)", panel);
    if (MainThreadID != GetCurrentThreadId())
    {
        TRACE_E("You can call CSalamanderGeneral::SetUserWorkedOnPanelPath() only from main thread!");
        return;
    }
    CFilesWindow* p = GetPanel(panel);
    if (p != NULL)
        p->UserWorkedOnThisPath = TRUE;
}

void CSalamanderGeneral::StoreSelectionOnPanelPath(int panel)
{
    CALL_STACK_MESSAGE2("CSalamanderGeneral::StoreSelectionOnPanelPath(%d)", panel);
    if (MainThreadID != GetCurrentThreadId())
    {
        TRACE_E("You can call CSalamanderGeneral::StoreSelectionOnPanelPath() only from main thread!");
        return;
    }
    CFilesWindow* p = GetPanel(panel);
    if (p != NULL)
        p->StoreSelection();
}

class CSalamanderMaskGroupImp : public CSalamanderMaskGroup
{
protected:
    CMaskGroup maskGroup;

public:
    CSalamanderMaskGroupImp() : maskGroup() {}

    // Straight onto CMaskGroup's wide primaries now that the class
    // stores masks wide - no conversion at this layer at all.
    virtual void WINAPI SetMasksString(const wchar_t* masks, BOOL extendedMode) { maskGroup.SetMasksString(masks, extendedMode); }
    virtual BOOL WINAPI GetMasksString(CSalamanderStringBuffer* masks)
    {
        return masks != NULL && sally::plugin_abi::WriteStringBuffer(
                                    *masks, maskGroup.GetMasksString());
    }
    virtual BOOL WINAPI GetExtendedMode() { return maskGroup.GetExtendedMode(); }
    virtual BOOL WINAPI PrepareMasks(int& errorPos) { return maskGroup.PrepareMasks(errorPos); }
    virtual BOOL WINAPI AgreeMasks(const wchar_t* fileName, const wchar_t* fileExt) { return maskGroup.AgreeMasks(fileName, fileExt); }
};

CSalamanderMaskGroup*
CSalamanderGeneral::AllocSalamanderMaskGroup()
{
    CALL_STACK_MESSAGE1("CSalamanderGeneral::AllocSalamanderMaskGroup()");
    CSalamanderMaskGroup* ret = new CSalamanderMaskGroupImp;
    if (ret == NULL)
        TRACE_E(LOW_MEMORY);
    return ret;
}

void CSalamanderGeneral::FreeSalamanderMaskGroup(CSalamanderMaskGroup* maskGroup)
{
    CALL_STACK_MESSAGE1("CSalamanderGeneral::FreeSalamanderMaskGroup()");
    if (maskGroup != NULL)
        delete ((CSalamanderMaskGroupImp*)maskGroup);
}

DWORD
CSalamanderGeneral::UpdateCrc32(const void* buffer, DWORD count, DWORD crcVal)
{
    CALL_STACK_MESSAGE_NONE
    return ::UpdateCrc32(buffer, count, crcVal);
}

class CSalamanderMD5Imp : public CSalamanderMD5
{
protected:
    MD5 md5;

public:
    CSalamanderMD5Imp() : md5() {}

    virtual void WINAPI Init() { md5.init(); }
    virtual void WINAPI Update(const void* input, DWORD input_length) { md5.update((unsigned char*)input, input_length); }
    virtual void WINAPI Finalize() { md5.finalize(); }
    virtual void WINAPI GetDigest(void* dest) { memcpy(dest, md5.digest, 16); }
};

CSalamanderMD5*
CSalamanderGeneral::AllocSalamanderMD5()
{
    CALL_STACK_MESSAGE1("CSalamanderGeneral::AllocSalamanderMD5()");
    CSalamanderMD5Imp* ret = new CSalamanderMD5Imp;
    if (ret == NULL)
        TRACE_E(LOW_MEMORY);
    return ret;
}

void CSalamanderGeneral::FreeSalamanderMD5(CSalamanderMD5* md5)
{
    CALL_STACK_MESSAGE1("CSalamanderGeneral::FreeSalamanderMD5()");
    if (md5 != NULL)
        delete ((CSalamanderMD5Imp*)md5);
}

BOOL CSalamanderGeneral::LookForSubTexts(CSalamanderStringBuffer* text,
                                         CSalamanderTextRangeBuffer* varPlacements)
{
    CALL_STACK_MESSAGE_NONE
    if (text == NULL || varPlacements == NULL)
        return FALSE;
    std::wstring parsed;
    if (!sally::plugin_abi::ReadStringBuffer(*text, parsed))
        return FALSE;
    std::vector<sally::unicode::WideTextRange> parsedRanges;
    if (!::LookForSubTexts(parsed, parsedRanges))
        return FALSE;
    try
    {
        std::vector<CSalamanderTextRange> publishedRanges;
        publishedRanges.reserve(parsedRanges.size());
        for (const sally::unicode::WideTextRange& range : parsedRanges)
        {
            if (range.Offset > (std::numeric_limits<DWORD>::max)() ||
                range.Length > (std::numeric_limits<DWORD>::max)())
                return FALSE;
            publishedRanges.push_back(
                {static_cast<DWORD>(range.Offset), static_cast<DWORD>(range.Length)});
        }
        return sally::plugin_abi::WriteTextAndRanges(
                   *text, *varPlacements, parsed, publishedRanges)
                   ? TRUE
                   : FALSE;
    }
    catch (const std::bad_alloc&)
    {
        return FALSE;
    }
    catch (const std::length_error&)
    {
        return FALSE;
    }
}

void CSalamanderGeneral::WaitForESCRelease()
{
    CALL_STACK_MESSAGE_NONE
    ::WaitForESCRelease();
}

DWORD
CSalamanderGeneral::GetMouseWheelScrollLines()
{
    CALL_STACK_MESSAGE_NONE
    return ::GetMouseWheelScrollLines();
}

DWORD
CSalamanderGeneral::GetMouseWheelScrollChars()
{
    CALL_STACK_MESSAGE_NONE
    return ::GetMouseWheelScrollChars();
}

HWND CSalamanderGeneral::GetTopVisibleParent(HWND hParent)
{
    CALL_STACK_MESSAGE_NONE
    return ::GetTopVisibleParent(hParent);
}

BOOL CSalamanderGeneral::MultiMonGetDefaultWindowPos(HWND hByWnd, POINT* p)
{
    CALL_STACK_MESSAGE_NONE
    return ::MultiMonGetDefaultWindowPos(hByWnd, p);
}

void CSalamanderGeneral::MultiMonGetClipRectByRect(const RECT* rect, RECT* workClipRect, RECT* monitorClipRect)
{
    CALL_STACK_MESSAGE_NONE
    ::MultiMonGetClipRectByRect(rect, workClipRect, monitorClipRect);
}

void CSalamanderGeneral::MultiMonGetClipRectByWindow(HWND hByWnd, RECT* workClipRect, RECT* monitorClipRect)
{
    CALL_STACK_MESSAGE_NONE
    ::MultiMonGetClipRectByWindow(hByWnd, workClipRect, monitorClipRect);
}

void CSalamanderGeneral::MultiMonCenterWindow(HWND hWindow, HWND hByWnd, BOOL findTopWindow)
{
    CALL_STACK_MESSAGE_NONE
    ::MultiMonCenterWindow(hWindow, hByWnd, findTopWindow);
}

BOOL CSalamanderGeneral::MultiMonEnsureRectVisible(RECT* rect, BOOL partialOK)
{
    CALL_STACK_MESSAGE_NONE
    return ::MultiMonEnsureRectVisible(rect, partialOK);
}

BOOL CSalamanderGeneral::InstallWordBreakProc(HWND hWindow)
{
    CALL_STACK_MESSAGE_NONE
    return ::InstallWordBreakProc(hWindow);
}

BOOL CSalamanderGeneral::IsFirstInstance3OrLater()
{
    CALL_STACK_MESSAGE_NONE
    return FirstInstance_3_or_later;
}

BOOL CSalamanderGeneral::ExpandPluralString(const wchar_t* format,
                                            int parametersCount,
                                            const CQuadWord* parametersArray,
                                            CSalamanderStringBuffer* text)
{
    CALL_STACK_MESSAGE3("CSalamanderGeneral::ExpandPluralString(%ls, %d, ,)",
                        format, parametersCount);
    return text != NULL && format != NULL &&
           sally::plugin_abi::WriteStringBuffer(
               *text, ExpandPluralStringOwnedW(format, parametersCount,
                                                parametersArray));
}

BOOL CSalamanderGeneral::ExpandPluralFilesDirs(int files, int dirs, int mode,
                                               BOOL forDlgCaption,
                                               CSalamanderStringBuffer* text)
{
    CALL_STACK_MESSAGE5("CSalamanderGeneral::ExpandPluralFilesDirs(%d, %d, %d, %d,)",
                        files, dirs, mode, forDlgCaption);
    return text != NULL && sally::plugin_abi::WriteStringBuffer(
                               *text, ExpandPluralFilesDirsTextW(
                                          files, dirs, mode, forDlgCaption));
}

BOOL CSalamanderGeneral::ExpandPluralBytesFilesDirs(
    const CQuadWord& selectedBytes, int files, int dirs, BOOL useSubTexts,
    CSalamanderStringBuffer* text)
{
    CALL_STACK_MESSAGE5("CSalamanderGeneral::ExpandPluralBytesFilesDirs(%g, %d, %d, %d,)",
                        selectedBytes.GetDouble(), files, dirs, useSubTexts);
    return text != NULL && sally::plugin_abi::WriteStringBuffer(
                               *text, ExpandPluralBytesFilesDirsTextW(
                                          selectedBytes, files, dirs,
                                          useSubTexts));
}

BOOL CSalamanderGeneral::GetCommonFSOperSourceDescr(
    int panel, int selectedFiles, int selectedDirs,
    const wchar_t* fileOrDirName, BOOL isDir, BOOL forDlgCaption,
    CSalamanderStringBuffer* sourceDescr)
{
    CALL_STACK_MESSAGE6("CSalamanderGeneral::GetCommonFSOperSourceDescr(%d, %d, %d, , %d, %d,)",
                        panel, selectedFiles, selectedDirs, isDir, forDlgCaption);
    if (MainThreadID != GetCurrentThreadId())
    {
        TRACE_E("You can call CSalamanderGeneral::GetCommonFSOperSourceDescr() only from main thread!");
        return FALSE;
    }
    if (sourceDescr == NULL)
    {
        TRACE_E("CSalamanderGeneral::GetCommonFSOperSourceDescr(): 'sourceDescr' may not be NULL!");
        return FALSE;
    }
    if (selectedFiles + selectedDirs <= 1 && panel == -1 && fileOrDirName == NULL)
    {
        TRACE_E("CSalamanderGeneral::GetCommonFSOperSourceDescr(): 'fileOrDirName' may not be NULL!");
        return FALSE;
    }
    std::wstring description;
    if (selectedFiles + selectedDirs <= 1) // one selected item or the focus
    {
        BOOL nameIsDir;
        std::wstring name;
        if (panel != -1)
        {
            const CFileData* f;
            if (selectedFiles == 0 && selectedDirs == 0)
                f = GetPanelFocusedItem(panel, &nameIsDir);
            else
            {
                int index = 0;
                f = GetPanelSelectedItem(panel, &index, &nameIsDir);
            }
            if (f != NULL && f->Name != NULL)
            {
                // CFileData::Name is already the exact wide form; the retired
                // NameW/UseWideName() split (since removed) no longer applies.
                name = f->Name;
            }
            else
            {
                TRACE_E("Unexpected situation in CSalamanderGeneral::GetCommonFSOperSourceDescr()!");
                return FALSE;
            }
        }
        else
        {
            name = (fileOrDirName != NULL) ? fileOrDirName : L"";
            nameIsDir = isDir;
        }
        int fileNameFormat;
        GetConfigParameter(SALCFG_FILENAMEFORMAT, &fileNameFormat,
                           sizeof(fileNameFormat), NULL);
        const std::wstring formatedFileName =
            ::AlterFileNameW(name.c_str(), fileNameFormat, 0, nameIsDir != FALSE);
        description = FormatStrW(
            ::LoadStrW(nameIsDir ? (forDlgCaption ? IDS_DLG_QUESTION_DIRECTORY : IDS_QUESTION_DIRECTORY) : (forDlgCaption ? IDS_DLG_QUESTION_FILE : IDS_QUESTION_FILE)),
            formatedFileName.c_str());
    }
    else // multiple directories and files
    {
        description = ExpandPluralFilesDirsTextW(
            selectedFiles, selectedDirs, epfdmNormal, forDlgCaption);
    }
    return sally::plugin_abi::WriteStringBuffer(*sourceDescr, description);
}

BOOL CSalamanderGeneral::AddStrToStr(CSalamanderStringBuffer* dstText,
                                    const wchar_t* srcStr)
{
    CALL_STACK_MESSAGE1("CSalamanderGeneral::AddStrToStr(, ,)");
    if (dstText == NULL || srcStr == NULL)
        return FALSE;
    std::wstring text;
    if (!sally::plugin_abi::ReadStringBuffer(*dstText, text))
        return FALSE;
    try
    {
        const std::wstring::size_type embeddedNull = text.find(L'\0');
        if (embeddedNull != std::wstring::npos)
            text.resize(embeddedNull);
        text.push_back(L'\0');
        text.append(srcStr);
    }
    catch (const std::bad_alloc&)
    {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }
    catch (const std::length_error&)
    {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }
    return sally::plugin_abi::WriteStringBuffer(*dstText, text);
}

// wide: forwards to the wide internal that already existed.
BOOL CSalamanderGeneral::SalIsValidFileNameComponent(const wchar_t* fileNameComponent)
{
    CALL_STACK_MESSAGE1("CSalamanderGeneral::SalIsValidFileNameComponent()");
    return ::SalIsValidFileNameComponentW(fileNameComponent);
}

BOOL CSalamanderGeneral::SalMakeValidFileNameComponent(
    CSalamanderStringBuffer* fileNameComponent)
{
    CALL_STACK_MESSAGE1("CSalamanderGeneral::SalMakeValidFileNameComponent()");
    if (fileNameComponent == NULL)
        return FALSE;
    std::wstring input;
    if (!sally::plugin_abi::ReadStringBuffer(*fileNameComponent, input))
        return FALSE;
    return sally::plugin_abi::WriteStringBuffer(
        *fileNameComponent, ::SalMakeValidFileNameComponentW(input.c_str()));
}

BOOL CSalamanderGeneral::IsFileEnumSourcePanel(int srcUID, int* panel)
{
    CALL_STACK_MESSAGE2("CSalamanderGeneral::IsFileEnumSourcePanel(%d,)", srcUID);
    return ::IsFileEnumSourcePanel(srcUID, panel);
}

BOOL CSalamanderGeneral::GetNextFileNameForViewer(int srcUID, int* lastFileIndex, const wchar_t* lastFileName,
                                                   BOOL preferSelected, BOOL onlyAssociatedExtensions,
                                                   CSalamanderStringBuffer* fileName, BOOL* noMoreFiles, BOOL* srcBusy)
{
    CALL_STACK_MESSAGE5("CSalamanderGeneral::GetNextFileNameForViewer(%d, , , %d, %d, %ls, ,)",
                        srcUID, preferSelected, onlyAssociatedExtensions,
                        fileName != NULL && fileName->Data != NULL
                            ? fileName->Data
                            : L"");
    if (fileName == NULL || lastFileIndex == NULL)
    {
        if (noMoreFiles != NULL)
            *noMoreFiles = FALSE;
        if (srcBusy != NULL)
            *srcBusy = FALSE;
        TRACE_E("CSalamanderGeneral::GetNextFileNameForViewer(): invalid parameters (fileName == NULL || lastFileIndex == NULL)!");
        return FALSE;
    }
    if (Plugin == NULL || (INT_PTR)Plugin == -1)
    {
        if (noMoreFiles != NULL)
            *noMoreFiles = FALSE;
        if (srcBusy != NULL)
            *srcBusy = FALSE;
        TRACE_E("CSalamanderGeneral::GetNextFileNameForViewer(): unexpected call, plugin is not initialized yet!");
        return FALSE;
    }
    int stagedIndex = *lastFileIndex;
    BOOL stagedNoMoreFiles = FALSE;
    BOOL stagedSrcBusy = FALSE;
    std::wstring stagedFileName;
    const BOOL result = ::GetNextFileNameForViewer(
        srcUID, &stagedIndex, lastFileName, preferSelected,
        onlyAssociatedExtensions, &stagedFileName, &stagedNoMoreFiles,
        &stagedSrcBusy, Plugin);
    if (result && !sally::plugin_abi::WriteStringBuffer(*fileName,
                                                        stagedFileName))
        return FALSE;
    *lastFileIndex = stagedIndex;
    if (noMoreFiles != NULL)
        *noMoreFiles = stagedNoMoreFiles;
    if (srcBusy != NULL)
        *srcBusy = stagedSrcBusy;
    return result;
}

BOOL CSalamanderGeneral::GetPreviousFileNameForViewer(int srcUID, int* lastFileIndex, const wchar_t* lastFileName,
                                                       BOOL preferSelected, BOOL onlyAssociatedExtensions,
                                                       CSalamanderStringBuffer* fileName, BOOL* noMoreFiles, BOOL* srcBusy)
{
    CALL_STACK_MESSAGE5("CSalamanderGeneral::GetPreviousFileNameForViewer(%d, , , %d, %d, %ls, ,)",
                        srcUID, preferSelected, onlyAssociatedExtensions,
                        fileName != NULL && fileName->Data != NULL
                            ? fileName->Data
                            : L"");
    if (fileName == NULL || lastFileIndex == NULL)
    {
        if (noMoreFiles != NULL)
            *noMoreFiles = FALSE;
        if (srcBusy != NULL)
            *srcBusy = FALSE;
        TRACE_E("CSalamanderGeneral::GetPreviousFileNameForViewer(): invalid parameters (fileName == NULL || lastFileIndex == NULL)!");
        return FALSE;
    }
    if (Plugin == NULL || (INT_PTR)Plugin == -1)
    {
        if (noMoreFiles != NULL)
            *noMoreFiles = FALSE;
        if (srcBusy != NULL)
            *srcBusy = FALSE;
        TRACE_E("CSalamanderGeneral::GetPreviousFileNameForViewer(): unexpected call, plugin is not initialized yet!");
        return FALSE;
    }
    int stagedIndex = *lastFileIndex;
    BOOL stagedNoMoreFiles = FALSE;
    BOOL stagedSrcBusy = FALSE;
    std::wstring stagedFileName;
    const BOOL result = ::GetPreviousFileNameForViewer(
        srcUID, &stagedIndex, lastFileName, preferSelected,
        onlyAssociatedExtensions, &stagedFileName, &stagedNoMoreFiles,
        &stagedSrcBusy, Plugin);
    if (result && !sally::plugin_abi::WriteStringBuffer(*fileName,
                                                        stagedFileName))
        return FALSE;
    *lastFileIndex = stagedIndex;
    if (noMoreFiles != NULL)
        *noMoreFiles = stagedNoMoreFiles;
    if (srcBusy != NULL)
        *srcBusy = stagedSrcBusy;
    return result;
}

BOOL CSalamanderGeneral::IsFileNameForViewerSelected(int srcUID, int lastFileIndex,
                                                     const wchar_t* lastFileName,
                                                     BOOL* isFileSelected, BOOL* srcBusy)
{
    CALL_STACK_MESSAGE3("CSalamanderGeneral::IsFileNameForViewerSelected(%d, %d, , , ,)",
                        srcUID, lastFileIndex);
    if (isFileSelected == NULL)
    {
        if (srcBusy != NULL)
            *srcBusy = FALSE;
        TRACE_E("CSalamanderGeneral::IsFileNameForViewerSelected(): invalid parameters (isFileSelected == NULL)!");
        return FALSE;
    }
    return ::IsFileNameForViewerSelected(srcUID, lastFileIndex, lastFileName,
                                         isFileSelected, srcBusy);
}

BOOL CSalamanderGeneral::SetSelectionOnFileNameForViewer(int srcUID, int lastFileIndex,
                                                         const wchar_t* lastFileName, BOOL select,
                                                         BOOL* srcBusy)
{
    CALL_STACK_MESSAGE4("CSalamanderGeneral::SetSelectionOnFileNameForViewer(%d, %d, , %d,)",
                        srcUID, lastFileIndex, select);
    return ::SetSelectionOnFileNameForViewer(srcUID, lastFileIndex, lastFileName, select, srcBusy);
}

BOOL CSalamanderGeneral::GetStdHistoryValues(int historyID, wchar_t*** historyArr, int* historyItemsCount)
{
    CALL_STACK_MESSAGE2("CSalamanderGeneral::GetStdHistoryValues(%d, ,)", historyID);
    if (MainThreadID != GetCurrentThreadId())
    {
        TRACE_E("You can call CSalamanderGeneral::GetStdHistoryValues() only from main thread!");
        if (historyArr != NULL)
            *historyArr = NULL;
        if (historyItemsCount != NULL)
            *historyItemsCount = 0;
        return FALSE;
    }
    if (historyArr == NULL || historyItemsCount == NULL)
    {
        TRACE_E("CSalamanderGeneral::GetStdHistoryValues(): invalid parameters!");
        return FALSE;
    }
    switch (historyID)
    {
    case SALHIST_COPYMOVETGT:
    {
        *historyArr = Configuration.CopyHistory;
        *historyItemsCount = COPY_HISTORY_SIZE;
        return TRUE;
    }

    case SALHIST_CREATEDIR:
    {
        *historyArr = Configuration.CreateDirHistory;
        *historyItemsCount = CREATEDIR_HISTORY_SIZE;
        return TRUE;
    }

    case SALHIST_CHANGEDIR:
    {
        *historyArr = Configuration.ChangeDirHistory;
        *historyItemsCount = CHANGEDIR_HISTORY_SIZE;
        return TRUE;
    }

    case SALHIST_QUICKRENAME:
    {
        *historyArr = Configuration.QuickRenameHistory;
        *historyItemsCount = QUICKRENAME_HISTORY_SIZE;
        return TRUE;
    }

    case SALHIST_EDITNEW:
    {
        *historyArr = Configuration.EditNewHistory;
        *historyItemsCount = EDITNEW_HISTORY_SIZE;
        return TRUE;
    }

    case SALHIST_CONVERT:
    {
        *historyArr = Configuration.ConvertHistory;
        *historyItemsCount = CONVERT_HISTORY_SIZE;
        return TRUE;
    }

    default:
    {
        *historyArr = NULL;
        *historyItemsCount = 0;
        return FALSE;
    }
    }
}

void CSalamanderGeneral::AddValueToStdHistoryValues(wchar_t** historyArr, int historyItemsCount,
                                                    const wchar_t* value, BOOL caseSensitiveValue)
{
    CALL_STACK_MESSAGE4("CSalamanderGeneral::AddValueToStdHistoryValues(, %d, %ls, %d)",
                        historyItemsCount, value, caseSensitiveValue);
    if (MainThreadID != GetCurrentThreadId())
    {
        TRACE_E("You can call CSalamanderGeneral::AddValueToStdHistoryValues() only from main thread!");
        return;
    }
    if (historyArr == NULL || value == NULL)
    {
        TRACE_E("CSalamanderGeneral::AddValueToStdHistoryValues(): 'historyArr' and 'value' may not be NULL!");
        return;
    }
    ::AddValueToStdHistoryValues(historyArr, historyItemsCount, value, caseSensitiveValue);
}

void CSalamanderGeneral::LoadComboFromStdHistoryValues(HWND combo, wchar_t** historyArr, int historyItemsCount)
{
    CALL_STACK_MESSAGE2("CSalamanderGeneral::LoadComboFromStdHistoryValues(, , %d)",
                        historyItemsCount);
    if (MainThreadID != GetCurrentThreadId())
    {
        TRACE_E("You can call CSalamanderGeneral::LoadComboFromStdHistoryValues() only from main thread!");
        return;
    }
    if (historyArr == NULL && historyItemsCount > 0)
    {
        TRACE_E("CSalamanderGeneral::LoadComboFromStdHistoryValues(): 'historyArr' may not be NULL!");
        return;
    }
    ::LoadComboFromStdHistoryValues(combo, historyArr, historyItemsCount);
}

BOOL CSalamanderGeneral::CanUse256ColorsBitmap()
{
    CALL_STACK_MESSAGE1("CSalamanderGeneral::CanUse256ColorsBitmap()");
    return ::Use256ColorsBitmap();
}

HWND CSalamanderGeneral::GetWndToFlash(HWND parent)
{
    CALL_STACK_MESSAGE1("CSalamanderGeneral::GetWndToFlash()");
    return ::GetWndToFlash(parent);
}

void ActivateDropTarget(HWND dropTarget, HWND progressWnd)
{
    if (dropTarget != NULL)
    { // this dirty hack removes the activated state without an active application (visually inactive, but WM_ACTIVATEAPP with "activate" already arrived)
        HWND tgtWnd = dropTarget;
        HWND tmp;
        while ((tmp = ::GetParent(tgtWnd)) != NULL && IsWindowEnabled(tmp))
            tgtWnd = tmp;
        if (MainWindow != NULL && tgtWnd != MainWindow->HWindow)
        { // perform it only if it is not an operation inside our Salamander
            SetForegroundWindow(progressWnd);
            SetForegroundWindow(tgtWnd);
            //        TRACE_I("SetForegroundWindow: " << hex << tgtWnd);
        }
    }
}

void CSalamanderGeneral::ActivateDropTarget(HWND dropTarget, HWND progressWnd)
{
    CALL_STACK_MESSAGE1("CSalamanderGeneral::ActivateDropTarget(,)");
    ::ActivateDropTarget(dropTarget, progressWnd);
}

void CSalamanderGeneral::PostOpenPackDlgForThisPlugin(int delFilesAfterPacking)
{
    CALL_STACK_MESSAGE2("CSalamanderGeneral::PostOpenPackDlgForThisPlugin(%d)", delFilesAfterPacking);
    if (MainThreadID != GetCurrentThreadId())
    {
        TRACE_E("You can call CSalamanderGeneral::PostOpenPackDlgForThisPlugin() only from main thread!");
        return;
    }

    CPluginData* data = Plugins.GetPluginData(Plugin);
    if (data != NULL)
    {
        data->OpenPackDlg = TRUE;
        data->PackDlgDelFilesAfterPacking = delFilesAfterPacking;
        OpenPackOrUnpackDlgForMarkedPlugins = TRUE;
    }
    else
    {
        TRACE_E("Unexpected situation in CSalamanderGeneral::PostOpenPackDlgForThisPlugin().");
    }
}

void CSalamanderGeneral::PostOpenUnpackDlgForThisPlugin(const wchar_t* unpackMask)
{
    CALL_STACK_MESSAGE1("CSalamanderGeneral::PostOpenUnpackDlgForThisPlugin()");
    if (MainThreadID != GetCurrentThreadId())
    {
        TRACE_E("You can call CSalamanderGeneral::PostOpenUnpackDlgForThisPlugin() only from main thread!");
        return;
    }

    CPluginData* data = Plugins.GetPluginData(Plugin);
    if (data != NULL)
    {
        data->OpenUnpackDlg = TRUE;
        if (unpackMask != NULL)
            data->UnpackDlgUnpackMask = unpackMask;
        else
            data->UnpackDlgUnpackMask.clear();
        OpenPackOrUnpackDlgForMarkedPlugins = TRUE;
    }
    else
    {
        TRACE_E("Unexpected situation in CSalamanderGeneral::PostOpenUnpackDlgForThisPlugin().");
    }
}

HANDLE
CSalamanderGeneral::SalCreateFileEx(const wchar_t* fileName, DWORD desiredAccess, DWORD shareMode,
                                    DWORD flagsAndAttributes, DWORD* err)
{
    CALL_STACK_MESSAGE1("CSalamanderGeneral::SalCreateFileEx()");
    HANDLE ret = ::SalCreateFileEx(fileName, desiredAccess, shareMode, flagsAndAttributes, NULL);
    if (err != NULL)
        *err = GetLastError();
    return ret;
}

// wide: forwards to the wide internal that already existed.
BOOL CSalamanderGeneral::SalCreateDirectoryEx(const wchar_t* name, DWORD* err)
{
    CALL_STACK_MESSAGE1("CSalamanderGeneral::SalCreateDirectoryEx()");
    return ::SalCreateDirectoryExW(name, err);
}

void CSalamanderGeneral::PanelStopMonitoring(int panel, BOOL stopMonitoring)
{
    CALL_STACK_MESSAGE3("CSalamanderGeneral::PanelStopMonitoring(%d, %d)", panel, stopMonitoring);
    if (MainThreadID != GetCurrentThreadId())
    {
        TRACE_E("You can call CSalamanderGeneral::PanelStopMonitoring() only from main thread!");
        return;
    }
    CFilesWindow* p = GetPanel(panel);
    if (p != NULL)
        p->HandsOff(stopMonitoring);
}

CSalamanderDirectoryAbstract*
CSalamanderGeneral::AllocSalamanderDirectory(BOOL isForFS)
{
    CALL_STACK_MESSAGE2("CSalamanderGeneral::AllocSalamanderDirectory(%d)", isForFS);
    CSalamanderDirectory* ret = new CSalamanderDirectory(isForFS);
    if (ret == NULL)
        TRACE_E(LOW_MEMORY);
    else
        ret->AllocAddCache();
    return ret;
}

void CSalamanderGeneral::FreeSalamanderDirectory(CSalamanderDirectoryAbstract* salDir)
{
    CALL_STACK_MESSAGE1("CSalamanderGeneral::FreeSalamanderDirectory()");
    if (salDir != NULL)
        delete ((CSalamanderDirectory*)salDir);
}

BOOL CSalamanderGeneral::AddPluginFSTimer(int timeout, CPluginFSInterfaceAbstract* timerOwner,
                                          DWORD timerParam) // FIXME_X64 - review the Salamander interface to ensure we do not pass parameters that should hold x64 pointers; for example here 'timerParam'
{
    CALL_STACK_MESSAGE3("CSalamanderGeneral::AddPluginFSTimer(%d, , 0x%X)", timeout, timerParam);
    if (MainThreadID != GetCurrentThreadId())
    {
        TRACE_E("You can call CSalamanderGeneral::AddPluginFSTimer() only from main thread!");
        return FALSE;
    }
    if (timeout < 0)
    {
        TRACE_E("CSalamanderGeneral::AddPluginFSTimer(): invalid timeout value (" << timeout << ")!");
        return FALSE;
    }
    if (timerOwner == NULL)
    {
        TRACE_E("CSalamanderGeneral::AddPluginFSTimer(): invalid timerOwner (NULL)!");
        return FALSE;
    }
    return Plugins.AddPluginFSTimer(timeout, timerOwner, timerParam);
}

int CSalamanderGeneral::KillPluginFSTimer(CPluginFSInterfaceAbstract* timerOwner, BOOL allTimers,
                                          DWORD timerParam)
{
    CALL_STACK_MESSAGE3("CSalamanderGeneral::KillPluginFSTimer(, %d, 0x%X)", allTimers, timerParam);
    if (MainThreadID != GetCurrentThreadId())
    {
        TRACE_E("You can call CSalamanderGeneral::KillPluginFSTimer() only from main thread!");
        return 0;
    }
    if (timerOwner == NULL)
    {
        TRACE_E("CSalamanderGeneral::KillPluginFSTimer(): invalid timer owner (NULL)!");
        return 0;
    }
    return Plugins.KillPluginFSTimer(timerOwner, allTimers, timerParam);
}

BOOL CSalamanderGeneral::GetChangeDriveMenuItemVisibility()
{
    CALL_STACK_MESSAGE1("CSalamanderGeneral::GetChangeDriveMenuItemVisibility()");
    if (MainThreadID != GetCurrentThreadId())
    {
        TRACE_E("You can call CSalamanderGeneral::GetChangeDriveMenuItemVisibility() only from main thread!");
        return FALSE;
    }
    CPluginData* data = Plugins.GetPluginData(Plugin);
    if (data != NULL)
        return data->ChDrvMenuFSItemVisible;
    else
        TRACE_E("Unexpected situation in CSalamanderGeneral::GetChangeDriveMenuItemVisibility().");
    return FALSE;
}

void CSalamanderGeneral::SetChangeDriveMenuItemVisibility(BOOL visible)
{
    CALL_STACK_MESSAGE2("CSalamanderGeneral::SetChangeDriveMenuItemVisibility(%d)", visible);
    if (MainThreadID != GetCurrentThreadId())
    {
        TRACE_E("You can call CSalamanderGeneral::SetChangeDriveMenuItemVisibility() only from main thread!");
        return;
    }
    CPluginData* data = Plugins.GetPluginData(Plugin);
    if (data != NULL)
        data->ChDrvMenuFSItemVisible = (visible != FALSE);
    else
        TRACE_E("Unexpected situation in CSalamanderGeneral::SetChangeDriveMenuItemVisibility().");
}

void CSalamanderGeneral::OleSpySetBreak(int alloc)
{
    CALL_STACK_MESSAGE2("CSalamanderGeneral::OleSpySetBreak(%d)", alloc);
    ::OleSpySetBreak(alloc);
}

HICON
CSalamanderGeneral::GetSalamanderIcon(int icon, int iconSize)
{
    CALL_STACK_MESSAGE3("CSalamanderGeneral::GetSalamanderIcon(%d, %d)", icon, iconSize);

    if (MainThreadID != GetCurrentThreadId())
    {
        TRACE_E("You can call CSalamanderGeneral::GetSalamanderIcon() only from main thread!");
        return NULL;
    }

    CSymbolsImageListIndexes iconIndex;
    switch (icon)
    {
    case SALICON_EXECUTABLE:
        iconIndex = symbolsExecutable;
        break;
    case SALICON_DIRECTORY:
        iconIndex = symbolsDirectory;
        break;
    case SALICON_NONASSOCIATED:
        iconIndex = symbolsNonAssociated;
        break;
    case SALICON_ASSOCIATED:
        iconIndex = symbolsAssociated;
        break;
    case SALICON_UPDIR:
        iconIndex = symbolsUpDir;
        break;
    case SALICON_ARCHIVE:
        iconIndex = symbolsArchive;
        break;
    default:
    {
        TRACE_E("CSalamanderGeneral::GetSalamanderIcon: invalid icon=" << icon << " forcing SALICON_NONASSOCIATED");
        iconIndex = symbolsNonAssociated;
    }
    }

    CIconSizeEnum salIconSize;
    switch (iconSize)
    {
    case SALICONSIZE_16:
        salIconSize = ICONSIZE_16;
        break;
    case SALICONSIZE_32:
        salIconSize = ICONSIZE_32;
        break;
    case SALICONSIZE_48:
        salIconSize = ICONSIZE_48;
        break;
    default:
    {
        TRACE_E("CSalamanderGeneral::GetSalamanderIcon: invalid iconSize=" << iconSize << " forcing SALICONSIZE_16");
        salIconSize = ICONSIZE_16;
    }
    }

    CIconList* list = SimpleIconLists[salIconSize];
    if (list == NULL)
    {
        TRACE_E("CSalamanderGeneral::GetSalamanderIcon: list == NULL");
        return NULL;
    }

    return list->GetIcon(iconIndex, FALSE);
}

static CIconSizeEnum GetPluginIconSize(int iconSize)
{
    CIconSizeEnum salIconSize;
    switch (iconSize)
    {
    case SALICONSIZE_16:
        salIconSize = ICONSIZE_16;
        break;
    case SALICONSIZE_32:
        salIconSize = ICONSIZE_32;
        break;
    case SALICONSIZE_48:
        salIconSize = ICONSIZE_48;
        break;
    default:
    {
        TRACE_E("CSalamanderGeneral::GetFileIcon: invalid iconSize=" << iconSize << " forcing SALICONSIZE_16");
        salIconSize = ICONSIZE_16;
    }
    }
    return salIconSize;
}

BOOL CSalamanderGeneral::GetFileIcon(const wchar_t* path, HICON* hIcon,
                                     int iconSize, BOOL fallbackToDefIcon,
                                     BOOL defIconIsDir)
{
    CALL_STACK_MESSAGE5("CSalamanderGeneral::GetFileIcon(%ls, , %d, %d, %d)",
                        path, iconSize, fallbackToDefIcon, defIconIsDir);
    return ::GetFileIcon(path, hIcon, GetPluginIconSize(iconSize),
                         fallbackToDefIcon, defIconIsDir);
}

BOOL CSalamanderGeneral::GetFileIconFromPIDL(LPCITEMIDLIST pidl, HICON* hIcon,
                                             int iconSize, BOOL fallbackToDefIcon,
                                             BOOL defIconIsDir)
{
    CALL_STACK_MESSAGE5("CSalamanderGeneral::GetFileIconFromPIDL(%p, , %d, %d, %d)",
                        pidl, iconSize, fallbackToDefIcon, defIconIsDir);
    return ::GetFileIconFromPIDL(pidl, hIcon, GetPluginIconSize(iconSize),
                                 fallbackToDefIcon, defIconIsDir);
}

class CSalamanderPNG : public CSalamanderPNGAbstract
{
public:
    virtual HBITMAP WINAPI LoadPNGBitmap(HINSTANCE hInstance, LPCWSTR lpBitmapName, DWORD flags, COLORREF unused)
    {
        HBITMAP hBitmap = ::LoadPNGBitmap(hInstance, lpBitmapName, flags);
        if (hBitmap != NULL) // the handle is handed over to the plug-in; the plug-in is responsible for destroying it, remove it from Salamander HANDLES
            HANDLES_REMOVE(hBitmap, __htHandle_comp_with_DeleteObject, "DeleteObject");
        return hBitmap;
    }

    virtual HBITMAP WINAPI LoadRawPNGBitmap(const void* rawPNG, DWORD rawPNGSize, DWORD flags, COLORREF unused)
    {
        HBITMAP hBitmap = ::LoadRawPNGBitmap(rawPNG, rawPNGSize, flags);
        if (hBitmap != NULL) // the handle is handed over to the plug-in; the plug-in is responsible for destroying it, remove it from Salamander HANDLES
            HANDLES_REMOVE(hBitmap, __htHandle_comp_with_DeleteObject, "DeleteObject");
        return hBitmap;
    }
};

CSalamanderPNG SalamanderPNG;

CSalamanderPNGAbstract*
CSalamanderGeneral::GetSalamanderPNG()
{
    CALL_STACK_MESSAGE_NONE
    return &SalamanderPNG;
}

CSalamanderPasswordManagerAbstract*
CSalamanderGeneral::GetSalamanderPasswordManager()
{
    CALL_STACK_MESSAGE1("CSalamanderGeneral::GetSalamanderPasswordManager()");
    if (MainThreadID != GetCurrentThreadId())
    {
        TRACE_E("You can call CSalamanderGeneral::GetSalamanderPasswordManager() only from main thread!");
        return NULL;
    }
    CPluginData* data = Plugins.GetPluginData(Plugin);
    if (data != NULL)
        return &data->SalamanderPasswordManager;
    else
        TRACE_E("Unexpected situation in CSalamanderGeneral::GetSalamanderPasswordManager().");
    return NULL;
}

class CSalamanderCrypt : public CSalamanderCryptAbstract
{
public:
    /* AES functions */
    virtual int WINAPI AESInit(CSalAES* aes,
                               int mode,       /* Mode (key size) to be used (input) */
                               LPCSTR pwd,     /* User specified password (input)    */
                               size_t pwd_len, /* Password length (input)            */
                               LPBYTE salt,    /* Salt (input)                       */
                               LPWORD pwd_ver) /* 2 byte password verifier (output)  */
    {
        _ASSERT(sizeof(aes->nonce) == sizeof(((fcrypt_ctx*)aes)->nonce));
        _ASSERT(sizeof(aes->encr_bfr) == sizeof(((fcrypt_ctx*)aes)->encr_bfr));
        _ASSERT(sizeof(aes->encr_ctx) == sizeof(((fcrypt_ctx*)aes)->encr_ctx));
        _ASSERT(sizeof(aes->auth_ctx) == sizeof(((fcrypt_ctx*)aes)->auth_ctx));
        _ASSERT(sizeof(aes->nonce) == sizeof(((fcrypt_ctx*)aes)->nonce));
        _ASSERT(sizeof(CSalAES) == sizeof(fcrypt_ctx));
        return fcrypt_init(mode, (unsigned char*)pwd, (unsigned int)pwd_len, salt, (unsigned char*)pwd_ver, (fcrypt_ctx*)aes);
    }

    virtual void WINAPI AESEncrypt(CSalAES* aes, LPVOID data, size_t dataLen)
    {
        fcrypt_encrypt((unsigned char*)data, (unsigned int)dataLen, (fcrypt_ctx*)aes);
    }

    virtual void WINAPI AESDecrypt(CSalAES* aes, LPVOID data, size_t dataLen)
    {
        fcrypt_decrypt((unsigned char*)data, (unsigned int)dataLen, (fcrypt_ctx*)aes);
    }

    virtual void WINAPI AESEnd(CSalAES* aes, LPBYTE mac, LPDWORD pMacLen)
    {
        if (pMacLen)
            *pMacLen = SAL_AES_MAC_LENGTH(aes->mode);
        fcrypt_end(mac, (fcrypt_ctx*)aes);
    }

    /* SHA1 functions */
    virtual void WINAPI SHA1Init(CSalSHA1* sha1)
    {
        _ASSERT(sizeof(CSalSHA1) == sizeof(SHA1_Context));
        ::SHA1Init((SHA1_CTX*)sha1);
    }

    virtual void WINAPI SHA1Update(CSalSHA1* sha1, const LPBYTE data, size_t dataLen)
    {
        ::SHA1Update((SHA1_CTX*)sha1, data, (unsigned int)dataLen);
    }

    virtual void WINAPI SHA1Final(CSalSHA1* sha1, BYTE digest[20])
    {
        ::SHA1Final(digest, (SHA1_CTX*)sha1);
    }
};

CSalamanderCrypt SalamanderCrypt;

CSalamanderCryptAbstract* GetSalamanderCrypt() // for Salamander's internal use
{
    CALL_STACK_MESSAGE_NONE
    return &SalamanderCrypt;
}

CSalamanderCryptAbstract*
CSalamanderGeneral::GetSalamanderCrypt()
{
    CALL_STACK_MESSAGE_NONE
    return &SalamanderCrypt;
}

// wide. The wide internal was added in the same commit - this
// method was deferred in an earlier batch precisely because it did not exist,
// and widening the signature over a narrowing implementation would have moved
// the loss inward rather than removing it.
BOOL CSalamanderGeneral::FileExists(const wchar_t* fileName)
{
    CALL_STACK_MESSAGE2("CSalamanderGeneral::FileExists(%S)", fileName);
    return ::FileExistsW(fileName);
}

void CSalamanderGeneral::DisconnectFSFromPanel(HWND parent, int panel)
{
    CALL_STACK_MESSAGE2("CSalamanderGeneral::DisconnectFSFromPanel(, %d)", panel);
    if (MainThreadID != GetCurrentThreadId())
    {
        TRACE_E("You can call CSalamanderGeneral::DisconnectFSFromPanel() only from main thread!");
        return;
    }
    // The path stays wide ALL the way through now. ChangePanelPathToDisk
    // used to be the narrow final hop (panel chain at the time), which is
    // why this used to refuse rather than guess for any path CP_ACP could not spell
    // exactly - falling through to the rescue path below for a perfectly valid
    // Unicode-only "last visited path". ChangePanelPathToDisk is wide now (routes to
    // ChangePathToDisk), so that refusal is gone: it was compensating for a narrow
    // SDK method, not a fundamental limit.
    BOOL rescueOrFixed = TRUE;
    CFilesWindow* sourcePanel = GetPanel(panel);
    if (sourcePanel != NULL)
    { // change the path to the last visited Windows path
        BOOL tryNet = FALSE;
        DWORD err;
        DWORD lastErr;
        BOOL pathInvalid;
        BOOL cut;
        std::wstring pathW(sourcePanel->GetPathW());
        if (::SalCheckAndRestorePathWithCutW(parent, pathW, tryNet, err, lastErr,
                                             pathInvalid, cut, TRUE))
        {
            int failReason;
            if (ChangePanelPathToDisk(panel, pathW.c_str(), &failReason) ||
                failReason != CHPPFR_INVALIDPATH) // except for the "bad path" error (closing the FS was refused, etc.)
            {
                rescueOrFixed = FALSE;
            }
        }
    }
    if (rescueOrFixed)
        ChangePanelPathToRescuePathOrFixedDrive(panel); // "always false"
}

BOOL CSalamanderGeneral::IsArchiveHandledByThisPlugin(const wchar_t* name)
{
    CALL_STACK_MESSAGE2("CSalamanderGeneral::IsArchiveHandledByThisPlugin(%S)", name);
    if (Plugin == NULL || (INT_PTR)Plugin == -1)
    {
        TRACE_E("CSalamanderGeneral::IsArchiveHandledByThisPlugin() unexpected call, plugin is not initialized yet!");
        return FALSE;
    }
    CPluginData* data = Plugins.GetPluginData(Plugin);
    if (data == NULL)
    {
        TRACE_E("Unexpected situation in CSalamanderGeneral::IsArchiveHandledByThisPlugin()");
        return FALSE;
    }
    if (!data->SupportPanelView)
        return FALSE;

    int format = PackerFormatConfig.PackIsArchive(name);
    if (format != 0) // found a supported archive
    {
        format--;
        int index = PackerFormatConfig.GetUnpackerIndex(format);
        if (index < 0) // view: is it internal processing (plug-in)?
        {
            CPluginData* foundData = Plugins.Get(-index - 1);
            if (foundData == data) // is it us?
                return TRUE;
        }
    }
    return FALSE;
}

DWORD
CSalamanderGeneral::GetIconLRFlags()
{
    return IconLRFlags;
}

int CSalamanderGeneral::IsFileLink(const wchar_t* fileExtension)
{
    CALL_STACK_MESSAGE_NONE
    //  CALL_STACK_MESSAGE1("CSalamanderGeneral::IsFileLink()");
    if (fileExtension == NULL)
        return 0;
    return ::IsFileLink(fileExtension);
}

DWORD
CSalamanderGeneral::GetImageListColorFlags()
{
    CALL_STACK_MESSAGE_NONE
    return ::GetImageListColorFlags();
}

void CSalamanderGeneral::SetHelpFileName(const wchar_t* chmName)
{
    CALL_STACK_MESSAGE_NONE
    if (chmName == NULL || *chmName == 0)
        TRACE_E("CSalamanderGeneral::SetHelpFileName(): invalid parameter 'chmName'.");
    else
        HelpFileName = chmName;
}

BOOL CSalamanderGeneral::OpenHtmlHelp(HWND parent, CHtmlHelpCommand command, DWORD_PTR dwData, BOOL quiet)
{
    CALL_STACK_MESSAGE4("CSalamanderGeneral::OpenHtmlHelp(, %d, %Iu, %d)", command, dwData, quiet);
    if (HelpFileName.empty())
    {
        TRACE_E("CSalamanderGeneral::OpenHtmlHelp(): plugin must call CSalamanderGeneral::SetHelpFileName() first!");
        return FALSE;
    }
    else
    {
        return ::OpenHtmlHelp(HelpFileName.c_str(), parent, command, dwData, quiet);
    }
}

BOOL CSalamanderGeneral::OpenHtmlHelpForSalamander(HWND parent, CHtmlHelpCommand command, DWORD_PTR dwData, BOOL quiet)
{
    CALL_STACK_MESSAGE4("CSalamanderGeneral::OpenHtmlHelpForSalamander(, %d, %Iu, %d)", command, dwData, quiet);
    DWORD_PTR newData = dwData;
    if (command == HHCDisplayContext)
    {
        switch (dwData)
        {
        case HTMLHELP_SALID_PWDMANAGER:
            newData = IDH_PWDMANAGER;
            break; // password manager help
        default:
        {
            TRACE_E("CSalamanderGeneral::OpenHtmlHelpForSalamander(): invalid dwData parameter, see allowed HTMLHELP_SALID_XXX constants.");
            return FALSE;
        }
        }
    }
    return ::OpenHtmlHelp(NULL, parent, command, newData, quiet);
}

BOOL CSalamanderGeneral::PathsAreOnTheSameVolume(const wchar_t* path1, const wchar_t* path2,
                                                 BOOL* resIsOnlyEstimation)
{
    CALL_STACK_MESSAGE1("CSalamanderGeneral::PathsAreOnTheSameVolume(, ,)");
    return ::PathsAreOnTheSameVolumeW(path1, path2, resIsOnlyEstimation);
}

BOOL CSalamanderGeneral::SafeGetOpenFileName(LPOPENFILENAME lpofn)
{
    CALL_STACK_MESSAGE_NONE
    return ::SafeGetOpenFileName(lpofn);
}

BOOL CSalamanderGeneral::SafeGetSaveFileName(LPOPENFILENAME lpofn)
{
    CALL_STACK_MESSAGE_NONE
    return ::SafeGetSaveFileName(lpofn);
}

void CSalamanderGeneral::SetPluginIsNethood()
{
    CALL_STACK_MESSAGE_NONE
    if (MainThreadID != GetCurrentThreadId() || (INT_PTR)Plugin != -1)
    {
        TRACE_E("You can call CSalamanderGeneral::SetPluginIsNethood() only from entry-point!");
        return;
    }
    CPluginData* data = Plugins.GetPluginData(Plugin);
    if (data != NULL)
        data->PluginIsNethood = TRUE;
    else
        TRACE_E("Unexpected situation in CSalamanderGeneral::SetPluginIsNethood().");
}

void CSalamanderGeneral::SetPluginUsesPasswordManager()
{
    CALL_STACK_MESSAGE_NONE
    if (MainThreadID != GetCurrentThreadId() || (INT_PTR)Plugin != -1)
    {
        TRACE_E("You can call CSalamanderGeneral::SetPluginUsesPasswordManager() only from entry-point!");
        return;
    }
    CPluginData* data = Plugins.GetPluginData(Plugin);
    if (data != NULL)
        data->PluginUsesPasswordManager = TRUE;
    else
        TRACE_E("Unexpected situation in CSalamanderGeneral::SetPluginUsesPasswordManager().");
}

void CSalamanderGeneral::OpenNetworkContextMenu(HWND parent, int panel, BOOL forItems, int menuX,
                                                int menuY, const wchar_t* netPathW, wchar_t* newlyMappedDrive)
{
    CALL_STACK_MESSAGE5("CSalamanderGeneral::OpenNetworkContextMenu(, %d, %d, %d, %d, ,)",
                        panel, forItems, menuX, menuY);

    if (newlyMappedDrive != NULL)
        *newlyMappedDrive = 0;

    // Keep the public route wide. The special "\\" and "\server" shell
    // namespace forms cannot bind through SHParseDisplayName, so the network-root helper owns
    // the one necessary ACP fallback when it walks that namespace.

    if (MainThreadID != GetCurrentThreadId())
    {
        TRACE_E("You can call CSalamanderGeneral::OpenNetworkContextMenu() only from main thread!");
        return;
    }

    if (netPathW == NULL || netPathW[0] != L'\\' || netPathW[1] != L'\\' || wcschr(netPathW + 2, L'\\') != NULL)
    {
        TRACE_E("CSalamanderGeneral::OpenNetworkContextMenu(): invalid netPath");
        return;
    }

    CFilesWindow* p = GetPanel(panel);
    if (p != NULL)
    {
        BeginStopRefresh(); // no refreshes needed (formality: the call comes from a plug-in, so refreshes are already disabled by EnterPlugin)

        int* indexes = NULL;
        int index = 0;
        int count = 0;
        if (forItems)
        {
            BOOL subDir;
            if (p->Dirs->Count > 0)
                subDir = (wcscmp(p->Dirs->At(0).Name, L"..") == 0);
            else
                subDir = FALSE;

            count = p->GetSelCount();
            if (count != 0)
            {
                indexes = new int[count];
                p->GetSelItems(count, indexes, TRUE); // we stepped back from this (see GetSelItems): for context menus we start from the focused item and end with the item before the focus (there is an intermediate wrap to the start of the name list) (the system does the same, e.g., Add To Windows Media Player List on MP3 files)
            }
            else
            {
                index = p->GetCaretIndex();
                if (subDir && index == 0)
                {
                    EndStopRefresh();
                    return;
                }
            }
        }
        else
            index = -1;

        if (p->ContextMenu != NULL)
        {
            TRACE_E("CSalamanderGeneral::OpenNetworkContextMenu: p->ContextMenu must be NULL (probably forbidden recursive call)!");
        }
        else
        {
            if (forItems)
            {
                CTmpEnumData data;
                data.Indexes = (count == 0) ? &index : indexes;
                data.Panel = p;
                p->ContextMenu = CreateIContextMenu2(MainWindow->HWindow, netPathW, (count == 0) ? 1 : count,
                                                     EnumFileNames, &data);
            }
            else
            {
                p->ContextMenu = CreateNetworkRootContextMenu(MainWindow->HWindow, netPathW);
            }

            HMENU h = CreatePopupMenu();
            if (p->ContextMenu != NULL && h != NULL)
            {
                UINT flags = CMF_NORMAL | CMF_EXPLORE;
                // handle the pressed Shift key - extended context menu; under W2K it contains, for example, Run as...
#define CMF_EXTENDEDVERBS 0x00000100 // rarely used verbs
                BOOL shiftPressed = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
                if (shiftPressed)
                    flags |= CMF_EXTENDEDVERBS;

                ShellActionAux5(flags, p, h);
                RemoveUselessSeparatorsFromMenu(h);

                int cmd = 0;
                if (GetMenuItemCount(h) > 0) // guard against a completely trimmed menu
                {
                    CMenuPopup contextPopup;
                    contextPopup.SetTemplateMenu(h);
                    cmd = contextPopup.Track(MENU_TRACK_RETURNCMD | MENU_TRACK_RIGHTBUTTON,
                                             menuX, menuY, parent, NULL);
                }
                if (cmd != 0)
                {
                    CALL_STACK_MESSAGE1("CSalamanderGeneral::OpenNetworkContextMenu::exec");

                    std::wstring cmdName;
                    AuxGetCommandString(p->ContextMenu, cmd, GCS_VERBW, NULL, cmdName);

                    // the Map Network Drive command is 40 on XP, 43 on W2K, and only under Vista has a defined cmdName
                    if ((_wcsicmp(cmdName.c_str(), L"connectNetworkDrive") == 0 ||
                         !WindowsVistaAndLater && cmd == 40) &&
                        forItems && netPathW[2] != 0)
                    {
                        std::wstring root(netPathW);
                        int focus = p->GetCaretIndex();
                        SalPathAppendW(root, (focus < p->Dirs->Count ? p->Dirs->At(focus) : p->Files->At(focus - p->Dirs->Count)).Name);
                        wchar_t newDrive = 0;
                        p->ConnectNet(TRUE, root.c_str(), FALSE /* called from a plug-in; must not change the panel path, otherwise
                                                we would return to a deallocated FS object */
                                      ,
                                      &newDrive);
                        if (newlyMappedDrive != NULL)
                            *newlyMappedDrive = newDrive; // a letter A-Z, ASCII by definition
                    }
                    else
                    {
                        CShellExecuteWnd shellExecuteWnd;
                        CMINVOKECOMMANDINFOEX ici;
                        ZeroMemory(&ici, sizeof(CMINVOKECOMMANDINFOEX));
                        ici.cbSize = sizeof(CMINVOKECOMMANDINFOEX);
                        ici.fMask = CMIC_MASK_PTINVOKE;
                        if (CanUseShellExecuteWndAsParent(cmdName.c_str()))
                            ici.hwnd = shellExecuteWnd.Create(MainWindow->HWindow, L"SEW: CSalamanderGeneral::OpenNetworkContextMenu cmd=%d", cmd);
                        else
                            ici.hwnd = MainWindow->HWindow;
                        ici.lpVerb = MAKEINTRESOURCEA(cmd); // CMINVOKECOMMANDINFOEX::lpVerb (inherited from the base struct) stays LPCSTR regardless of the Ex/wide fields - MAKEINTRESOURCEA matches that always, not the TCHAR-generic macro
                        ici.nShow = SW_SHOWNORMAL;
                        ici.ptInvoke.x = menuX;
                        ici.ptInvoke.y = menuY;

                        AuxInvokeCommand(p, (CMINVOKECOMMANDINFO*)&ici);

                        IdleRefreshStates = TRUE;  // during the next Idle force checking the status variables
                        IdleCheckClipboard = TRUE; // also request checking the clipboard
                    }
                }
            }
            {
                CALL_STACK_MESSAGE1("CSalamanderGeneral::OpenNetworkContextMenu::release");
                ShellActionAux6(p);
                if (h != NULL)
                    DestroyMenu(h);
            }
        }

        if (count != 0)
            delete[] (indexes);

        EndStopRefresh();
    }
}

BOOL CSalamanderGeneral::DuplicateBackslashes(CSalamanderStringBuffer* text)
{
    CALL_STACK_MESSAGE1("CSalamanderGeneral::DuplicateBackslashes()");
    if (text == NULL)
        return FALSE;
    std::wstring value;
    if (!sally::plugin_abi::ReadStringBuffer(*text, value))
        return FALSE;
    if (!::DuplicateBackslashes(value))
        return FALSE;
    return sally::plugin_abi::WriteStringBuffer(*text, value);
}

int CSalamanderGeneral::StartThrobber(int panel, const wchar_t* tooltip, int delay)
{
    CALL_STACK_MESSAGE4("CSalamanderGeneral::StartThrobber(%d, %ls, %d)", panel, tooltip, delay);

    if (MainThreadID != GetCurrentThreadId())
    {
        TRACE_E("You can call CSalamanderGeneral::StartThrobber() only from main thread!");
        return -1;
    }

    CFilesWindow* p = GetPanel(panel);
    if (p != NULL && p->DirectoryLine != NULL)
    {
        p->DirectoryLine->SetThrobber(TRUE, delay);
        p->DirectoryLine->SetThrobberTooltipW(tooltip);
        return p->DirectoryLine->ChangeThrobberID();
    }
    return -1;
}

BOOL CSalamanderGeneral::StopThrobber(int id)
{
    CALL_STACK_MESSAGE2("CSalamanderGeneral::StopThrobber(%d)", id);

    if (MainThreadID != GetCurrentThreadId())
    {
        TRACE_E("You can call CSalamanderGeneral::StopThrobber() only from main thread!");
        return FALSE;
    }

    if (MainWindow->LeftPanel->DirectoryLine != NULL &&
        MainWindow->LeftPanel->DirectoryLine->IsThrobberVisible(id))
    {
        MainWindow->LeftPanel->DirectoryLine->SetThrobber(FALSE);
        return TRUE;
    }
    if (MainWindow->RightPanel->DirectoryLine != NULL &&
        MainWindow->RightPanel->DirectoryLine->IsThrobberVisible(id))
    {
        MainWindow->RightPanel->DirectoryLine->SetThrobber(FALSE);
        return TRUE;
    }
    return FALSE;
}

void CSalamanderGeneral::ShowSecurityIcon(int panel, BOOL showIcon, BOOL isLocked,
                                          const wchar_t* tooltip)
{
    CALL_STACK_MESSAGE5("CSalamanderGeneral::ShowSecurityIcon(%d, %d, %d, %ls)",
                        panel, showIcon, isLocked, tooltip);

    if (MainThreadID != GetCurrentThreadId())
    {
        TRACE_E("You can call CSalamanderGeneral::ShowSecurityIcon() only from main thread!");
        return;
    }

    CFilesWindow* p = GetPanel(panel);
    if (p != NULL && p->DirectoryLine != NULL)
    {
        p->DirectoryLine->SetSecurity(showIcon ? (isLocked ? sisSecured : sisUnsecured) : sisNone);
        p->DirectoryLine->SetSecurityTooltipW(tooltip);
    }
}

void CSalamanderGeneral::RemoveCurrentPathFromHistory(int panel)
{
    CALL_STACK_MESSAGE2("CSalamanderGeneral::RemoveCurrentPathFromHistory(%d)", panel);

    if (MainThreadID != GetCurrentThreadId())
    {
        TRACE_E("You can call CSalamanderGeneral::RemoveCurrentPathFromHistory() only from main thread!");
        return;
    }

    CFilesWindow* p = GetPanel(panel);
    if (p != NULL)
    {
        p->RemoveCurrentPathFromHistory();
        if (MainWindow != NULL)
            MainWindow->DirHistoryRemoveActualPath(p);
        p->UserWorkedOnThisPath = FALSE;
    }
}

BOOL CSalamanderGeneral::IsUserAdmin()
{
    CALL_STACK_MESSAGE1("CSalamanderGeneral::IsUserAdmin()");
    return ::IsUserAdmin();
}

BOOL CSalamanderGeneral::IsRemoteSession()
{
    CALL_STACK_MESSAGE1("CSalamanderGeneral::IsRemoteSession()");
    return ::IsRemoteSession();
}

DWORD
CSalamanderGeneral::SalWNetAddConnection2Interactive(LPNETRESOURCE lpNetResource)
{
    CALL_STACK_MESSAGE1("CSalamanderGeneral::SalWNetAddConnection2Interactive()");
    DWORD err;
    RestoreNetworkConnectionW(NULL, NULL, NULL, &err, lpNetResource);
    return err;
}

void CSalamanderGeneral::GetFocusedItemMenuPos(POINT* pos)
{
    CALL_STACK_MESSAGE1("CSalamanderGeneral::GetFocusedItemMenuPos()");

    if (pos == NULL)
    {
        TRACE_E("CSalamanderGeneral::GetFocusedItemMenuPos(): invalid 'pos' (NULL)!");
        return;
    }

    pos->x = 0;
    pos->y = 0;

    if (MainThreadID != GetCurrentThreadId())
    {
        TRACE_E("You can call CSalamanderGeneral::GetFocusedItemMenuPos() only from main thread!");
        return;
    }

    if (MainWindow != NULL)
    {
        CFilesWindow* activePanel = MainWindow->GetActivePanel();
        if (activePanel != NULL)
        {
            activePanel->GetContextMenuPos(pos);
            return;
        }
    }

    pos->x = 0;
    pos->y = 0;
}

void CSalamanderGeneral::LockMainWindow(BOOL lock, HWND hToolWnd,
                                        const wchar_t* lockReason)
{
    CALL_STACK_MESSAGE4("CSalamanderGeneral::LockMainWindow(%d, 0x%p, %ls)", lock, hToolWnd, lockReason);
    if (MainThreadID != GetCurrentThreadId())
    {
        TRACE_E("You can call CSalamanderGeneral::LockMainWindow() only from main thread!");
        return;
    }
    if (MainWindow != NULL)
        MainWindow->LockUI(lock, hToolWnd, lockReason);
}

BOOL CSalamanderGeneral::GetMenuItemHotKey(int id, WORD* hotKey,
                                           CSalamanderStringBuffer* hotKeyText)
{
    CALL_STACK_MESSAGE2("CSalamanderGeneral::GetMenuItemHotKey(%d, , , )", id);
    if (MainThreadID != GetCurrentThreadId())
    {
        TRACE_E("You can call CSalamanderGeneral::GetMenuItemHotKey() only from main thread!");
        return FALSE;
    }
    BOOL ret = FALSE;
    CPluginData* data = Plugins.GetPluginData(Plugin);
    if (data != NULL)
    {
        if (hotKeyText != NULL &&
            !sally::plugin_abi::IsValidStringBuffer(*hotKeyText))
            return FALSE;
        std::wstring text;
        ret = data->GetMenuItemHotKey(id, hotKey,
                                      hotKeyText != NULL ? &text : NULL);
        if (ret && hotKeyText != NULL &&
            !sally::plugin_abi::WriteStringBuffer(*hotKeyText, text))
            return FALSE;
    }
    else
        TRACE_E("Unexpected situation in CSalamanderGeneral::GetMenuItemHotKey().");
    return ret;
}

LONG CSalamanderGeneral::SalRegQueryValue(HKEY hKey, LPCWSTR lpSubKey, LPWSTR lpData, PLONG lpcbData)
{
    CALL_STACK_MESSAGE1("CSalamanderGeneral::SalRegQueryValue(, , ,)");
    return ::SalRegQueryValueW(hKey, lpSubKey, lpData, lpcbData);
}

LONG CSalamanderGeneral::SalRegQueryValueEx(HKEY hKey, LPCWSTR lpValueName, LPDWORD lpReserved,
                                            LPDWORD lpType, LPBYTE lpData, LPDWORD lpcbData)
{
    CALL_STACK_MESSAGE1("CSalamanderGeneral::SalRegQueryValueEx(, , , , ,)");
    return ::SalRegQueryValueEx(hKey, lpValueName, lpReserved, lpType, lpData, lpcbData);
}

DWORD
CSalamanderGeneral::SalGetFileAttributes(const wchar_t* fileName)
{
    CALL_STACK_MESSAGE1("CSalamanderGeneral::SalGetFileAttributes()");
    return ::SalGetFileAttributes(fileName);
}

BOOL CSalamanderGeneral::IsPathOnSSD(const wchar_t* path)
{
    CALL_STACK_MESSAGE1("CSalamanderGeneral::IsPathOnSSD()");
    return ::IsPathOnSSDW(path);
}

// wide: forwards to the wide internal that already existed.
BOOL CSalamanderGeneral::IsUNCPath(const wchar_t* path)
{
    CALL_STACK_MESSAGE1("CSalamanderGeneral::IsUNCPath()");
    return ::IsUNCPathW(path);
}

BOOL CSalamanderGeneral::ResolveSubsts(CSalamanderStringBuffer* resPath)
{
    CALL_STACK_MESSAGE1("CSalamanderGeneral::ResolveSubsts()");
    if (resPath == NULL)
        return FALSE;
    std::wstring path;
    if (!sally::plugin_abi::ReadStringBuffer(*resPath, path) ||
        !::ResolveSubstsW(path))
        return FALSE;
    return sally::plugin_abi::WriteStringBuffer(*resPath, path);
}

BOOL CSalamanderGeneral::ResolveLocalPathWithReparsePoints(
    const wchar_t* path, CSalamanderStringBuffer* resPath,
    BOOL* cutResPathIsPossible, BOOL* rootOrCurReparsePointSet,
    CSalamanderStringBuffer* rootOrCurReparsePoint,
    CSalamanderStringBuffer* junctionOrSymlinkTgt, int* linkType,
    CSalamanderStringBuffer* netPath)
{
    CALL_STACK_MESSAGE1("CSalamanderGeneral::ResolveLocalPathWithReparsePoints()");
    if (path == NULL || resPath == NULL ||
        !sally::plugin_abi::IsValidStringBuffer(*resPath) ||
        (rootOrCurReparsePoint != NULL && !sally::plugin_abi::IsValidStringBuffer(*rootOrCurReparsePoint)) ||
        (junctionOrSymlinkTgt != NULL && !sally::plugin_abi::IsValidStringBuffer(*junctionOrSymlinkTgt)) ||
        (netPath != NULL && !sally::plugin_abi::IsValidStringBuffer(*netPath)))
        return FALSE;

    CLocalPathResolutionW res;
    ::ResolveLocalPathWithReparsePointsW(path, res);

    if (!sally::plugin_abi::WriteStringBuffer(*resPath, res.ResPath) ||
        (rootOrCurReparsePoint != NULL && !sally::plugin_abi::WriteStringBuffer(*rootOrCurReparsePoint, res.RootOrCurReparsePoint)) ||
        (junctionOrSymlinkTgt != NULL && !sally::plugin_abi::WriteStringBuffer(*junctionOrSymlinkTgt, res.JunctionOrSymlinkTgt)) ||
        (netPath != NULL && !sally::plugin_abi::WriteStringBuffer(*netPath, res.NetPath)))
        return FALSE;
    if (cutResPathIsPossible != NULL)
        *cutResPathIsPossible = res.CutResPathIsPossible;
    if (rootOrCurReparsePointSet != NULL)
        *rootOrCurReparsePointSet = res.RootOrCurReparsePointSet;
    if (linkType != NULL)
        *linkType = res.LinkType;
    return TRUE;
}

BOOL CSalamanderGeneral::GetResolvedPathMountPointAndGUID(const wchar_t* path,
                                                          CSalamanderStringBuffer* mountPoint,
                                                          CSalamanderStringBuffer* guidPath)
{
    CALL_STACK_MESSAGE1("CSalamanderGeneral::GetResolvedPathMountPointAndGUID()");
    if (path == NULL)
        return FALSE;

    if ((mountPoint != NULL && !sally::plugin_abi::IsValidStringBuffer(*mountPoint)) ||
        (guidPath != NULL && !sally::plugin_abi::IsValidStringBuffer(*guidPath)))
        return FALSE;
    std::wstring mountPointW, guidPathW;
    if (!::GetResolvedPathMountPointAndGUIDW(path,
                                             mountPoint != NULL ? &mountPointW : NULL,
                                             guidPath != NULL ? &guidPathW : NULL))
    {
        return FALSE;
    }
    return (mountPoint == NULL || sally::plugin_abi::WriteStringBuffer(*mountPoint, mountPointW)) &&
           (guidPath == NULL || sally::plugin_abi::WriteStringBuffer(*guidPath, guidPathW));
}

BOOL CSalamanderGeneral::PointToLocalDecimalSeparator(CSalamanderStringBuffer* text)
{
    CALL_STACK_MESSAGE1("CSalamanderGeneral::PointToLocalDecimalSeparator()");
    if (text == NULL)
        return FALSE;
    std::wstring value;
    if (!sally::plugin_abi::ReadStringBuffer(*text, value))
        return FALSE;
    if (!::PointToLocalDecimalSeparator(value))
        return FALSE;
    return sally::plugin_abi::WriteStringBuffer(*text, value);
}

void CSalamanderGeneral::SetPluginIconOverlays(int iconOverlaysCount, HICON* iconOverlays)
{
    CALL_STACK_MESSAGE2("CSalamanderGeneral::SetPluginIconOverlays(%d,)", iconOverlaysCount);
    if (MainThreadID != GetCurrentThreadId())
    { // this is a call error; we do not release the icons, not interested...
        TRACE_E("You can call CSalamanderGeneral::SetPluginIconOverlays() only from main thread!");
        return;
    }
    CPluginData* data = Plugins.GetPluginData(Plugin);
    if (data != NULL)
    {
        data->ReleaseIconOverlays();
        if (iconOverlaysCount > 0)
        {
            if (iconOverlays != NULL)
            {
                data->IconOverlays = (HICON*)malloc(3 * iconOverlaysCount * sizeof(HICON));
                memcpy(data->IconOverlays, iconOverlays, 3 * iconOverlaysCount * sizeof(HICON));
                data->IconOverlaysCount = iconOverlaysCount;

                BOOL err = FALSE;
                for (int i = 0; i < 3 * iconOverlaysCount; i++)
                {
                    if (data->IconOverlays[i] == NULL)
                    {
                        if (!err)
                            TRACE_E("CSalamanderGeneral::SetPluginIconOverlays(): invalid 'iconOverlays' (contains at least one NULL instead of icon handle)!");
                        err = TRUE;
                    }
                    else
                        HANDLES_ADD(__htIcon, __hoLoadImage, data->IconOverlays[i]);
                }
                if (err)
                    data->ReleaseIconOverlays();
            }
            else
                TRACE_E("CSalamanderGeneral::SetPluginIconOverlays(): invalid 'iconOverlays' (NULL)!");
        }
        else
        {
            if (iconOverlaysCount < 0)
                TRACE_E("CSalamanderGeneral::SetPluginIconOverlays(): invalid 'iconOverlaysCount' (negative value)!");
        }
    }
    else
        TRACE_E("Unexpected situation in CSalamanderGeneral::SetPluginIconOverlays().");
}

// wide; the internal was widened in the same commit.
BOOL CSalamanderGeneral::SalGetFileSize2(const wchar_t* fileName, CQuadWord& size, DWORD* err)
{
    CALL_STACK_MESSAGE1("CSalamanderGeneral::SalGetFileSize2()");
    return ::SalGetFileSize2(fileName, size, err);
}

BOOL CSalamanderGeneral::GetLinkTgtFileSize(HWND parent, const wchar_t* fileName, CQuadWord* size,
                                            BOOL* cancel, BOOL* ignoreAll)
{
    CALL_STACK_MESSAGE1("CSalamanderGeneral::GetLinkTgtFileSize()");
    return ::GetLinkTgtFileSize(parent, fileName, NULL, size, cancel, ignoreAll);
}

// wide; the internal was widened in the same commit.
BOOL CSalamanderGeneral::DeleteDirLink(const wchar_t* name, DWORD* err)
{
    CALL_STACK_MESSAGE1("CSalamanderGeneral::DeleteDirLink()");
    return ::DeleteDirLink(name, err);
}

// wide: forwards to the wide internal that already existed.
BOOL CSalamanderGeneral::ClearReadOnlyAttr(const wchar_t* name, DWORD attr)
{
    CALL_STACK_MESSAGE1("CSalamanderGeneral::ClearReadOnlyAttr()");
    return ::ClearReadOnlyAttr(name, attr);
}

BOOL CSalamanderGeneral::IsCriticalShutdown()
{
    return CriticalShutdown;
}

void CSalamanderGeneral::CloseAllOwnedEnabledDialogs(HWND parent, DWORD tid)
{
    CALL_STACK_MESSAGE2("CSalamanderGeneral::CloseAllOwnedEnabledDialogs(, %d)", tid);
    ::CloseAllOwnedEnabledDialogs(parent, tid);
}

BOOL CSalamanderGeneral::GetThemeInfo(CSalamanderThemeInfo* info)
{
    SLOW_CALL_STACK_MESSAGE1("CSalamanderGeneral::GetThemeInfo()");
    if (MainThreadID != GetCurrentThreadId())
    {
        TRACE_E("You can call CSalamanderGeneral::GetThemeInfo() only from main thread!");
        return FALSE;
    }
    if (info == NULL || info->Size < sizeof(CSalamanderThemeInfo))
        return FALSE;

    int themeMode = SALTHEME_MODE_UNKNOWN;
    switch (Configuration.ThemeMode)
    {
    case THEME_MODE_LIGHT:
        themeMode = SALTHEME_MODE_LIGHT;
        break;
    case THEME_MODE_DARK:
        themeMode = SALTHEME_MODE_DARK;
        break;
    case THEME_MODE_SYSTEM:
        themeMode = SALTHEME_MODE_SYSTEM;
        break;
    }

    info->ThemeMode = themeMode;
    info->UseDarkColors = DarkMode_ShouldUseDark();
    return TRUE;
}

//
// ****************************************************************************
// CSalamanderForOperations
//

CSalamanderForOperations::CSalamanderForOperations(CFilesWindow* panel)
{
    Panel = panel;
    FocusWnd = NULL;
    ThreadID = GetCurrentThreadId();
    Destroyed = FALSE;
}

CSalamanderForOperations::~CSalamanderForOperations()
{
    if (UnpackProgress.HWindow != NULL)
    {
        TRACE_E("Progress dialog remains opened.");
        CloseProgressDialog();
    }
    Destroyed = TRUE;
}

void CSalamanderForOperations::OpenProgressDialog(const wchar_t* title, BOOL twoProgressBars, HWND parent,
                                                  BOOL fileProgress)
{
    if (ThreadID != GetCurrentThreadId())
    {
        TRACE_E("You can call CSalamanderForOperations::OpenProgressDialog() only from thread ID:" << ThreadID);
        return;
    }
    if (Destroyed)
    {
        TRACE_E("You are calling CSalamanderForOperations::OpenProgressDialog() after destruction!");
        return;
    }
    if (UnpackProgress.HWindow == NULL)
    {
        if (parent == NULL)
        {
            parent = MainWindow->HWindow;
            UnpackProgress.SetTaskBarList3(&MainWindow->TaskBarList3);
        }
        if (!twoProgressBars)
            UnpackProgress.Set(title, parent, CQuadWord(0, 0), fileProgress);
        else
            UnpackProgress.Set(title, parent, CQuadWord(0, 0), CQuadWord(0, 0));
        FocusWnd = GetFocus();
        EnableWindow(UnpackProgress.GetParent(), FALSE);
        UnpackProgress.Create();

        ActivateDropTarget(ProgressDialogActivateDrop, UnpackProgress.HWindow);

        ProgressDialog2 = twoProgressBars;
        PluginProgressDialog = UnpackProgress.HWindow;
    }
}

void CSalamanderForOperations::CloseProgressDialog()
{
    if (ThreadID != GetCurrentThreadId())
    {
        TRACE_E("You can call CSalamanderForOperations::CloseProgressDialog() only from thread ID:" << ThreadID);
        return;
    }
    if (Destroyed)
    {
        TRACE_E("You are calling CSalamanderForOperations::CloseProgressDialog() after destruction!");
        return;
    }
    if (UnpackProgress.HWindow != NULL)
    {
        PluginProgressDialog = NULL;
        EnableWindow(UnpackProgress.GetParent(), TRUE);
        HWND actWnd = GetForegroundWindow();
        BOOL activate = actWnd == UnpackProgress.HWindow || actWnd == UnpackProgress.GetParent();
        DestroyWindow(UnpackProgress.HWindow);
        if (activate && FocusWnd != NULL)
            SetFocus(FocusWnd);
        UnpackProgress.Init();
    }
}

void CSalamanderForOperations::ProgressSetTotalSize(const CQuadWord& totalSize1, const CQuadWord& totalSize2)
{
    if (ThreadID != GetCurrentThreadId())
    {
        TRACE_E("You can call CSalamanderForOperations::ProgressSetTotalSize() only from thread ID:" << ThreadID);
        return;
    }
    if (Destroyed)
    {
        TRACE_E("You are calling CSalamanderForOperations::ProgressSetTotalSize() after destruction!");
        return;
    }
    if (!ProgressDialog2 && totalSize2 != CQuadWord(-1, -1))
    {
        TRACE_E("Incorrect call to CSalamanderForOperations::ProgressSetTotalSize(): progress dialog has only one progress!");
        return;
    }
    if (UnpackProgress.HWindow != NULL)
        UnpackProgress.SetTotal(totalSize1, totalSize2);
}

void CSalamanderForOperations::ProgressDialogAddText(const wchar_t* txt, BOOL delayedPaint)
{
    if (ThreadID != GetCurrentThreadId())
    {
        TRACE_E("You can call CSalamanderForOperations::ProgressDialogAddText() only from thread ID:" << ThreadID);
        return;
    }
    if (Destroyed)
    {
        TRACE_E("You are calling CSalamanderForOperations::ProgressDialogAddText() after destruction!");
        return;
    }
    if (UnpackProgress.HWindow != NULL)
        UnpackProgress.NewLine(txt, delayedPaint);
}

BOOL CSalamanderForOperations::ProgressAddSize(int size, BOOL delayedPaint)
{
    if (ThreadID != GetCurrentThreadId())
    {
        TRACE_E("You can call CSalamanderForOperations::ProgressAddSize() only from thread ID:" << ThreadID);
        return FALSE;
    }
    if (Destroyed)
    {
        TRACE_E("You are calling CSalamanderForOperations::ProgressAddSize() after destruction!");
        return FALSE;
    }
    if (UnpackProgress.HWindow != NULL)
        return UnpackProgress.AddSize(size, delayedPaint) == 1;
    return TRUE;
}

BOOL CSalamanderForOperations::ProgressSetSize(const CQuadWord& size1, const CQuadWord& size2, BOOL delayedPaint)
{
    if (ThreadID != GetCurrentThreadId())
    {
        TRACE_E("You can call CSalamanderForOperations::ProgressSetSize() only from thread ID:" << ThreadID);
        return FALSE;
    }
    if (Destroyed)
    {
        TRACE_E("You are calling CSalamanderForOperations::ProgressSetSize() after destruction!");
        return FALSE;
    }
    if (!ProgressDialog2 && size2 != CQuadWord(-1, -1))
    {
        TRACE_E("Incorrect call to CSalamanderForOperations::ProgressSetSize(): progress dialog has only one progress!");
        return FALSE;
    }
    if (UnpackProgress.HWindow != NULL)
        return UnpackProgress.SetSize(size1, size2, delayedPaint) == 1;
    return TRUE;
}

void CSalamanderForOperations::ProgressEnableCancel(BOOL enable)
{
    if (ThreadID != GetCurrentThreadId())
    {
        TRACE_E("You can call CSalamanderForOperations::ProgressEnableCancel() only from thread ID:" << ThreadID);
        return;
    }
    if (Destroyed)
    {
        TRACE_E("You are calling CSalamanderForOperations::ProgressEnableCancel() after destruction!");
        return;
    }
    if (UnpackProgress.HWindow != NULL)
        UnpackProgress.EnableCancel(enable);
}

// The current operations interface is UTF-16. sdk107 byte callers reach it
// through CLegacySalamanderForOperations, which widens once before this implementation.
BOOL CSalamanderForOperations::MoveFiles(const wchar_t* source, const wchar_t* target, const wchar_t* remapNameFrom,
                                         const wchar_t* remapNameTo)
{
    if (ThreadID != GetCurrentThreadId())
    {
        TRACE_E("You can call CSalamanderForOperations::MoveFiles() only from thread ID:" << ThreadID);
        return FALSE;
    }
    if (Destroyed)
    {
        TRACE_E("You are calling CSalamanderForOperations::MoveFiles() after destruction!");
        return FALSE;
    }
    if (Panel == NULL)
    {
        TRACE_E("Incorrect call to CSalamanderForOperations::MoveFiles");
        return FALSE;
    }
    return Panel->MoveFiles(source, target, remapNameFrom, remapNameTo);
}

//
// ****************************************************************************
// CSalamanderForViewFileOnFS
//

const wchar_t*
CSalamanderForViewFileOnFS::AllocFileNameInCache(HWND parent, const wchar_t* uniqueFileName, const wchar_t* nameInCache,
                                                 const wchar_t* rootTmpPath, BOOL& fileExists)
{
    CALL_STACK_MESSAGE4("CSalamanderForViewFileOnFS::AllocFileNameInCache(, %ls, %ls, %ls, )",
                        uniqueFileName, nameInCache, rootTmpPath);
    if (CallsCounter > 0)
    {
        TRACE_E("You are calling CSalamanderForViewFileOnFS::AllocFileNameInCache more than once! Is it O.K.?");
    }

    int errorCode;
    const wchar_t* name = DiskCache.GetName(uniqueFileName, nameInCache, &fileExists, FALSE, rootTmpPath, FALSE, NULL, &errorCode);
    if (name == NULL)
    {
        std::wstring msg = LoadStrW(IDS_VIEWFILEFAILED);
        gPrompter->ShowError(LoadStrW(IDS_ERRORTITLE), msg.c_str());
    }
    else
    {
        CallsCounter++;
    }

    return name;
}

BOOL CSalamanderForViewFileOnFS::OpenViewer(HWND parent, const wchar_t* fileName, HANDLE* fileLock,
                                            BOOL* fileLockOwner)
{
    CALL_STACK_MESSAGE2("CSalamanderForViewFileOnFS::OpenViewer(, %ls, ,)", fileName);

    HANDLE lock = NULL;
    BOOL lockOwner = FALSE;

    BOOL ret = ViewFileInt(parent, fileName, AltView, HandlerID,
                           (fileLock != NULL && fileLockOwner != NULL),
                           lock, lockOwner, FALSE, -1, -1);

    if (fileLock != NULL)
        *fileLock = lock;
    if (fileLockOwner != NULL)
        *fileLockOwner = lockOwner;
    return ret;
}

void CSalamanderForViewFileOnFS::FreeFileNameInCache(const wchar_t* uniqueFileName, BOOL fileExists, BOOL newFileOK,
                                                     const CQuadWord& newFileSize, HANDLE fileLock,
                                                     BOOL fileLockOwner, BOOL removeAsSoonAsPossible)
{
    CALL_STACK_MESSAGE8("CSalamanderForViewFileOnFS::FreeFileNameInCache(%ls, %d, %d, %g, 0x%p, %d, %d)",
                        uniqueFileName, fileExists, newFileOK, newFileSize.GetDouble(), fileLock,
                        fileLockOwner, removeAsSoonAsPossible);

    if (CallsCounter == 0)
    {
        TRACE_E("Unmatched call to CSalamanderForViewFileOnFS::FreeFileNameInCache!");
        return;
    }
    CallsCounter--;

    if (!fileExists) // newly downloaded copy of the file
    {
        if (newFileOK) // the download was successful
        {
            DiskCache.NamePrepared(uniqueFileName, newFileSize);
        }
        else
        {
            DiskCache.ReleaseName(uniqueFileName, FALSE); // the download failed, nothing to cache
            return;                                       // nothing else to address
        }
    }

    if (fileLock != NULL) // we have the viewer's "lock" object; link the viewer and the disk cache
    {
        DiskCache.AssignName(uniqueFileName, fileLock, fileLockOwner,
                             (fileExists || removeAsSoonAsPossible) ? crtDirect : crtCache); // for files present in the disk cache we use crtDirect, because it does not affect the "lifetime" setting (it stays as the file's author requested)
    }
    else // the viewer did not open or simply does not have a "lock" object
    {
        DiskCache.ReleaseName(uniqueFileName, !fileExists && !removeAsSoonAsPossible); // if 'removeAsSoonAsPossible' is not TRUE, at least try to keep a copy of the file in the disk cache (if it was not an existing file, we do not change its "lifetime")
    }
}

//
// ****************************************************************************
// CSalamanderDirectory
//

CSalamanderDirectory::CSalamanderDirectory(BOOL isForFS, DWORD validData, DWORD flags)
    : Dirs(10, 200), SalamDirs(10, 200), Files(10, 200)
{
    ValidData = validData;
    if (flags == -1)
        flags = isForFS ? SALDIRFLAG_IGNOREDUPDIRS : 0;
    Flags = flags;
    IsForFS = isForFS;
    AddCache = NULL;
}

CSalamanderDirectory::~CSalamanderDirectory()
{
    Clear(NULL); // plug-in data are released only in the root sal-dir
    FreeAddCache();
}

void CSalamanderDirectory::AllocAddCache()
{
    if (AddCache == NULL)
    {
        AddCache = new CSalamanderDirectoryAddCache;
        if (AddCache != NULL)
            AddCache->Dir = NULL;
        // if allocating the cache fails, it is fine; we are fully functional without it
    }
}

void CSalamanderDirectory::FreeAddCache()
{
    if (AddCache != NULL)
    {
        delete AddCache;
        AddCache = NULL;
    }
}

int CSalamanderDirectory::SalDirStrCmp(const wchar_t* s1, const wchar_t* s2)
{
    if (Flags & SALDIRFLAG_CASESENSITIVE)
        return wcscmp(s1, s2);
    else
        return StrICmpW(s1, s2);
}

int CSalamanderDirectory::SalDirStrCmpEx(const wchar_t* s1, int l1, const wchar_t* s2, int l2)
{
    if (Flags & SALDIRFLAG_CASESENSITIVE)
        return StrCmpExW(s1, l1, s2, l2);
    else
        return StrICmpExW(s1, l1, s2, l2);
}

void CSalamanderDirectory::Clear(CPluginDataInterfaceAbstract* pluginData)
{
    if (pluginData != NULL) // release plug-in-specific data
    {
        CPluginDataInterfaceEncapsulation plugin(pluginData, STR_NONE, STR_NONE, NULL, 0);
        BOOL releaseFiles = plugin.CallReleaseForFiles();
        BOOL releaseDirs = plugin.CallReleaseForDirs();
        if (releaseFiles || releaseDirs)
        {
            ReleasePluginData(plugin, releaseFiles, releaseDirs);
        }
    }
    int i;
    for (i = 0; i < SalamDirs.Count; i++)
    {
        CSalamanderDirectory* salDir = SalamDirs[i];
        if (salDir != NULL)
            delete salDir;
    }
    SalamDirs.DestroyMembers();
    Dirs.DestroyMembers();
    Files.DestroyMembers();
    if (AddCache != NULL)
    {
        AddCache->Path.clear();
        AddCache->Dir = NULL;
    }
    ValidData = VALID_DATA_ALL_FS_ARC;
    Flags = IsForFS ? SALDIRFLAG_IGNOREDUPDIRS : 0;
}

void CSalamanderDirectory::SetValidData(DWORD validData)
{
    if (ValidData != validData)
    {
        ValidData = validData;
        int i;
        for (i = 0; i < SalamDirs.Count; i++)
        {
            CSalamanderDirectory* salDir = SalamDirs[i];
            if (salDir != NULL)
                salDir->SetValidData(validData);
        }
    }
}

void CSalamanderDirectory::SetFlags(DWORD flags)
{
    if (Flags != flags)
    {
        Flags = flags;
        int i;
        for (i = 0; i < SalamDirs.Count; i++)
        {
            CSalamanderDirectory* salDir = SalamDirs[i];
            if (salDir != NULL)
                salDir->SetFlags(flags);
        }
    }
}

CSalamanderDirectory*
CSalamanderDirectory::AllocSalamDir(int index)
{
    CALL_STACK_MESSAGE_NONE // time-critical method

        if (index < 0 || index >= SalamDirs.Count || SalamDirs[index] != NULL)
    {
        TRACE_E("Unexpected error in CSalamanderDirectory::AllocSalamDir().");
        return NULL;
    }
    CSalamanderDirectory* dir = new CSalamanderDirectory(IsForFS, ValidData, Flags);
    if (dir == NULL)
    {
        TRACE_E(LOW_MEMORY);
        return NULL;
    }
    SalamDirs[index] = dir;
    return dir;
}

// ***************************************************************************
// FindDir:
//
// 'path' - input: path in the archive (relative to this directory)
// 's' - output: points past the first name in the path 'path'
// 'i' - output: index of the found subdirectory (which should continue processing the path 's')
// 'file' - input: if the directory must be created, where to copy data from
// 'pluginData' - input: interface for creating plug-in-specific data for the new directory (if needed)
// 'archivePath' - input: full path in the archive ('path' and 's' both point into it)

BOOL CSalamanderDirectory::FindDir(const wchar_t* path, const wchar_t*& s, int& i, const CFileData& file,
                                   CPluginDataInterfaceAbstract* pluginData, const wchar_t* archivePath)
{
    CALL_STACK_MESSAGE_NONE // time-critical method
        //  CALL_STACK_MESSAGE2("CSalamanderDirectory::FindDir(%s, , , ,)", path);
        s = path;
    while (*s != 0 && *s != L'\\')
        s++;

    for (i = 0; i < Dirs.Count; i++)
    {
        if (SalDirStrCmpEx(Dirs[i].Name, Dirs[i].NameLen, path, (int)(s - path)) == 0)
            break;
    }
    if (i == Dirs.Count) // we must create it
    {
        CFileData data;
        //--- name
        data.Name = (wchar_t*)malloc(((s - path) + 1) * sizeof(wchar_t)); // allocation
        if (data.Name == NULL)
        {
            TRACE_E(LOW_MEMORY);
            return FALSE;
        }
        memcpy(data.Name, path, (s - path) * sizeof(wchar_t)); // copy of the text
        data.Name[s - path] = 0;
        data.NameLen = (int)(s - path);
        //--- extension
        if (!Configuration.SortDirsByExt)
            data.Ext = data.Name + data.NameLen; // directories have no extensions
        else
        {
            const wchar_t* ss = s;
            while (--ss >= path && *ss != L'.')
                ;
            if (ss >= path)
                data.Ext = data.Name + (ss - path + 1); // ".cvspass" is an extension in Windows...
                                                        //      if (ss > path) data.Ext = data.Name + (ss - path + 1);
            else
                data.Ext = data.Name + data.NameLen;
        }
        //--- other fields
        data.Size = CQuadWord(0, 0);
        data.Attr = 0;
        data.LastWrite = file.LastWrite; // take the date from the first file in the directory
        data.DosName = NULL;
        data.PluginData = 0;
        data.Hidden = 0;
        data.IsLink = 0;
        data.IsOffline = 0;
        // private Salamander data
        data.Association = 0;
        data.Selected = 0;
        data.Shared = 0;
        data.Archive = 0;
        data.SizeValid = 0;
        data.Dirty = 0; // optional, kept only for formality
        data.CutToClip = 0;
        data.IconOverlayIndex = ICONOVERLAYINDEX_NOTUSED;
        data.IconOverlayDone = 0;

        if (pluginData != NULL) // let the plug-in add its specific data
        {
            const std::wstring arcPath(archivePath, s - archivePath);
            CPluginDataInterfaceEncapsulation plugin(pluginData, STR_NONE, STR_NONE, NULL, 0);
            if (!plugin.GetFileDataForNewDir(arcPath.c_str(), data)) // cannot add the plug-in data
            {
                free(data.Name);
                return FALSE;
            }
        }

        Dirs.Add(data);
        if (!Dirs.IsGood())
        {
            Dirs.ResetState();
            if (pluginData != NULL) // release plug-in-specific data
            {
                CPluginDataInterfaceEncapsulation plugin(pluginData, STR_NONE, STR_NONE, NULL, 0);
                if (plugin.CallReleaseForDirs())
                    plugin.ReleasePluginData2(data, TRUE);
            }
            free(data.Name);
            return FALSE;
        }
        //--- adding the Salamander directory corresponding to the new directory
        /*
    CSalamanderDirectory *dir = new CSalamanderDirectory(IsForFS, ValidData, Flags);
    if (dir != NULL) SalamDirs.Add((DWORD)dir);
    else TRACE_E(LOW_MEMORY);
    if (dir == NULL || !SalamDirs.IsGood())
    {
      if (dir != NULL) delete dir;
      SalamDirs.ResetState();
      if (pluginData != NULL)   // release plug-in-specific data
      {
        CPluginDataInterfaceEncapsulation plugin(pluginData, STR_NONE, STR_NONE, NULL, 0);
        if (plugin.CallReleaseForDirs()) plugin.ReleasePluginData2(Dirs[Dirs.Count - 1], TRUE);
      }
      Dirs.Delete(Dirs.Count - 1);
      return FALSE;
    }
*/
        SalamDirs.Add(NULL); // add NULL (the object will be allocated the first time it is needed)
        if (!SalamDirs.IsGood())
        {
            SalamDirs.ResetState();
            if (pluginData != NULL) // release plug-in-specific data
            {
                CPluginDataInterfaceEncapsulation plugin(pluginData, STR_NONE, STR_NONE, NULL, 0);
                if (plugin.CallReleaseForDirs())
                    plugin.ReleasePluginData2(Dirs[Dirs.Count - 1], TRUE);
            }
            Dirs.Delete(Dirs.Count - 1);
            if (!Dirs.IsGood())
                Dirs.ResetState();
            return FALSE;
        }
    }
    return TRUE;
}

BOOL CSalamanderDirectory::AddFile(const wchar_t* path, CFileData& file, CPluginDataInterfaceAbstract* pluginData)
{
    CALL_STACK_MESSAGE_NONE // time-critical method

    const size_t pathLen = path != NULL ? wcslen(path) : 0;

    //  TRACE_I("AddFile path="<<path<<" file="<<file.Name);

    // zero out variables that the plugin does not define
    if ((ValidData & VALID_DATA_EXTENSION) == 0)
        file.Ext = file.Name + file.NameLen;
    if ((ValidData & VALID_DATA_DOSNAME) == 0)
        file.DosName = NULL;
    if ((ValidData & VALID_DATA_SIZE) == 0)
        file.Size = CQuadWord(0, 0);
    if ((ValidData & VALID_DATA_DATE) == 0 || (ValidData & VALID_DATA_TIME) == 0)
    {
        SYSTEMTIME st;
        FILETIME ft;
        if ((ValidData & (VALID_DATA_DATE | VALID_DATA_TIME)) == 0 ||
            FileTimeToLocalFileTime(&file.LastWrite, &ft) &&
                FileTimeToSystemTime(&ft, &st))
        {
            if ((ValidData & VALID_DATA_DATE) == 0) // missing date
            {
                st.wYear = 1602;
                st.wMonth = 1;
                st.wDay = 1;
                st.wDayOfWeek = 2;
            }
            if ((ValidData & VALID_DATA_TIME) == 0) // missing time
            {
                st.wHour = 0;
                st.wMinute = 0;
                st.wSecond = 0;
                st.wMilliseconds = 0;
            }
            SystemTimeToFileTime(&st, &ft);
            LocalFileTimeToFileTime(&ft, &file.LastWrite);
        }
        else // invalid file.LastWrite
        {
            TRACE_E("CSalamanderDirectory::AddFile(): invalid file.LastWrite!");
            file.LastWrite.dwLowDateTime = 0;
            file.LastWrite.dwHighDateTime = 0;
        }
    }
    if ((ValidData & VALID_DATA_ATTRIBUTES) == 0)
        file.Attr = 0;
    if ((ValidData & VALID_DATA_HIDDEN) == 0)
        file.Hidden = 0;
    if ((ValidData & VALID_DATA_ISLINK) == 0)
        file.IsLink = 0;
    if ((ValidData & VALID_DATA_ISOFFLINE) == 0)
        file.IsOffline = 0;
    if ((ValidData & VALID_DATA_ICONOVERLAY) == 0)
        file.IconOverlayIndex = ICONOVERLAYINDEX_NOTUSED;

    file.Association = 0;
    file.Selected = 0;
    file.Shared = 0;
    file.Archive = 0;
    file.SizeValid = 0;
    file.Dirty = 0; // optional, kept only for formality
    file.CutToClip = 0;
    file.IconOverlayDone = 0;

    // if we have the path cached from the previous addition, we can insert the file right into its place
    if (path != NULL && AddCache != NULL && pathLen > 0 &&
        pathLen == AddCache->Path.size() && wmemcmp(path, AddCache->Path.data(), pathLen) == 0)
    {
        // the cache already held our path, so we can insert the file immediately
        AddCache->Dir->Files.Add(file);
        if (!AddCache->Dir->Files.IsGood())
        {
            AddCache->Dir->Files.ResetState();
            return FALSE;
        }
        return TRUE;
    }

    CSalamanderDirectory* ret = AddFileInt(path, file, pluginData, path);

    // if the insertion succeeded and the cache is used, remember the path
    if (ret != NULL && AddCache != NULL && pathLen > 0)
    {
        AddCache->Path.assign(path, pathLen);
        AddCache->Dir = ret;
    }

    return ret != NULL;
}

BOOL CSalamanderDirectory::AddDir(const wchar_t* path, CFileData& dir, CPluginDataInterfaceAbstract* pluginData)
{
    CALL_STACK_MESSAGE_NONE // time-critical method

    //  TRACE_I("AddDir path="<<path<<" dir="<<dir.Name);

    // zero out variables that the plugin does not define
    if ((ValidData & VALID_DATA_EXTENSION) == 0)
        dir.Ext = dir.Name + dir.NameLen;
    if ((ValidData & VALID_DATA_DOSNAME) == 0)
        dir.DosName = NULL;
    if ((ValidData & VALID_DATA_SIZE) == 0)
        dir.Size = CQuadWord(0, 0);
    if ((ValidData & VALID_DATA_DATE) == 0 || (ValidData & VALID_DATA_TIME) == 0)
    {
        SYSTEMTIME st;
        FILETIME ft;
        if ((ValidData & (VALID_DATA_DATE | VALID_DATA_TIME)) == 0 ||
            FileTimeToLocalFileTime(&dir.LastWrite, &ft) &&
                FileTimeToSystemTime(&ft, &st))
        {
            if ((ValidData & VALID_DATA_DATE) == 0) // missing date
            {
                st.wYear = 1602;
                st.wMonth = 1;
                st.wDay = 1;
                st.wDayOfWeek = 2;
            }
            if ((ValidData & VALID_DATA_TIME) == 0) // missing time
            {
                st.wHour = 0;
                st.wMinute = 0;
                st.wSecond = 0;
                st.wMilliseconds = 0;
            }
            SystemTimeToFileTime(&st, &ft);
            LocalFileTimeToFileTime(&ft, &dir.LastWrite);
        }
        else // invalid dir.LastWrite
        {
            TRACE_E("CSalamanderDirectory::AddDir(): invalid dir.LastWrite!");
            dir.LastWrite.dwLowDateTime = 0;
            dir.LastWrite.dwHighDateTime = 0;
        }
    }
    if ((ValidData & VALID_DATA_ATTRIBUTES) == 0)
        dir.Attr = 0;
    if ((ValidData & VALID_DATA_HIDDEN) == 0)
        dir.Hidden = 0;
    if ((ValidData & VALID_DATA_ISLINK) == 0)
        dir.IsLink = 0;
    if ((ValidData & VALID_DATA_ISOFFLINE) == 0)
        dir.IsOffline = 0;
    if ((ValidData & VALID_DATA_ICONOVERLAY) == 0)
        dir.IconOverlayIndex = ICONOVERLAYINDEX_NOTUSED;

    dir.Association = 0;
    dir.Selected = 0;
    dir.Shared = 0;
    dir.Archive = 0;
    dir.SizeValid = 0;
    dir.Dirty = 0; // optional, kept only for formality
    dir.CutToClip = 0;
    dir.IconOverlayDone = 0;

    return AddDirInt(path, dir, pluginData, path) != NULL;
}

int CSalamanderDirectory::GetFilesCount() const
{
    CALL_STACK_MESSAGE_NONE // time-critical method
        return Files.Count;
}

int CSalamanderDirectory::GetDirsCount() const
{
    CALL_STACK_MESSAGE_NONE // time-critical method
        return Dirs.Count;
}

CFileData const*
CSalamanderDirectory::GetFile(int i) const
{
    CALL_STACK_MESSAGE_NONE // time-critical method
        if (i >= 0 && i < Files.Count) return &(*((CFilesArray*)&Files))[i];
    else return NULL;
}

CFileData const*
CSalamanderDirectory::GetDir(int i) const
{
    CALL_STACK_MESSAGE_NONE // time-critical method
        if (i >= 0 && i < Dirs.Count) return &(*((CFilesArray*)&Dirs))[i];
    else return NULL;
}

CSalamanderDirectoryAbstract const*
CSalamanderDirectory::GetSalDir(int i) const
{
    CALL_STACK_MESSAGE_NONE // time-critical method
        if (i >= 0 && i < SalamDirs.Count)
    {
        CSalamanderDirectoryAbstract const* salDir = (CSalamanderDirectoryAbstract const*)(*((TDirectArray<CSalamanderDirectory*>*)&SalamDirs))[i];
        if (salDir == NULL)
            salDir = &GlobalEmptySalDir; // it's an empty directory - return the global empty directory
        return salDir;
    }
    else return NULL;
}

CSalamanderDirectory*
CSalamanderDirectory::AddFileInt(const wchar_t* path, CFileData& file,
                                 CPluginDataInterfaceAbstract* pluginData, const wchar_t* archivePath)
{
    CALL_STACK_MESSAGE_NONE // time-critical method; in addition, path may be NULL
                            //  CALL_STACK_MESSAGE3("CSalamanderDirectory::AddFileInt(%s, , , %s)", path, archivePath);

        if (path != NULL)
    {
        if (*path == L'\\')
            path++;
        if (*path != 0) // not this directory; find the subdirectory
        {
            const wchar_t* s;
            int i;
            if (!FindDir(path, s, i, file, pluginData, archivePath))
                return NULL;

            CSalamanderDirectory* salDir = SalamDirs[i];
            if (salDir != NULL ||                    // already allocated
                (salDir = AllocSalamDir(i)) != NULL) // or succeeded in allocating a new object
            {
                return salDir->AddFileInt(s, file, pluginData, archivePath);
            }
            else
                return NULL;
        }
    }

    // note: if AddCache applies, the item is added directly in AddFile
    Files.Add(file);
    if (!Files.IsGood())
    {
        Files.ResetState();
        return NULL;
    }
    return this;
}

CSalamanderDirectory*
CSalamanderDirectory::AddDirInt(const wchar_t* path, CFileData& dir,
                                CPluginDataInterfaceAbstract* pluginData, const wchar_t* archivePath)
{
    CALL_STACK_MESSAGE_NONE // time-critical method; in addition, path may be NULL
                            //  CALL_STACK_MESSAGE3("CSalamanderDirectory::AddDirInt(%s, , , %s)", path, archivePath);

        if (path != NULL)
    {
        if (*path == L'\\')
            path++;
        if (*path != 0) // not this directory; find the subdirectory
        {
            const wchar_t* s;
            int i;
            if (!FindDir(path, s, i, dir, pluginData, archivePath))
                return NULL;

            CSalamanderDirectory* salDir = SalamDirs[i];
            if (salDir != NULL ||                    // already allocated
                (salDir = AllocSalamDir(i)) != NULL) // or succeeded in allocating a new object
            {
                return salDir->AddDirInt(s, dir, pluginData, archivePath);
            }
            else
                return NULL;
        }
    }

    BOOL newDir = TRUE;
    if ((Flags & SALDIRFLAG_IGNOREDUPDIRS) == 0) // if we should test for duplicate directories
    {
        int i;
        for (i = 0; i < Dirs.Count; i++)
        {
            if (SalDirStrCmpEx(Dirs[i].Name, Dirs[i].NameLen, dir.Name, dir.NameLen) == 0)
                break;
        }
        newDir = (i == Dirs.Count); // not created yet
        if (!newDir)                // updating existing data
        {
            if (pluginData != NULL) // release plug-in-specific data
            {
                CPluginDataInterfaceEncapsulation plugin(pluginData, STR_NONE, STR_NONE, NULL, 0);
                if (plugin.CallReleaseForDirs())
                    plugin.ReleasePluginData2(Dirs[i], TRUE);
            }

            if (Dirs[i].Name != NULL)
                free(Dirs[i].Name);
            Dirs[i].Name = dir.Name; // rather take the new name (for possible data after '\0' in the string)
            Dirs[i].Ext = dir.Ext;
            Dirs[i].Size = dir.Size;
            Dirs[i].Attr = dir.Attr;
            Dirs[i].LastWrite = dir.LastWrite;
            if (Dirs[i].DosName != NULL)
                free(Dirs[i].DosName);
            Dirs[i].DosName = dir.DosName;
            Dirs[i].PluginData = dir.PluginData;
            // Dirs[i].NameLen should be the same as dir.NameLen
            Dirs[i].Hidden = dir.Hidden;
            Dirs[i].IsLink = dir.IsLink;
            Dirs[i].IsOffline = dir.IsOffline;
            // the remainder of Dirs[i] should be zeroed just like the rest of dir
        }
    }
    if (newDir)
    {
        //--- adding the Salamander directory corresponding to the new directory
        /*
    CSalamanderDirectory *SalamDir = new CSalamanderDirectory(IsForFS, ValidData, Flags);
    if (SalamDir != NULL) SalamDirs.Add((DWORD)SalamDir);
    else TRACE_E(LOW_MEMORY);
    if (SalamDir == NULL || !SalamDirs.IsGood())
    {
      if (SalamDir != NULL) delete SalamDir;
      SalamDirs.ResetState();
      return FALSE;
    }

    Dirs.Add(dir);
    if (!Dirs.IsGood())
    {
      Dirs.ResetState();
      SalamDirs.Delete(SalamDirs.Count - 1);
      delete SalamDir;
      return FALSE;
    }
*/
        if (IsForFS && dir.NameLen == 2 && dir.Name[0] == '.' && dir.Name[1] == '.')
        {
            CFileData* firstDir = Dirs.Count > 0 ? &Dirs[0] : NULL;
            if (firstDir != NULL && firstDir->NameLen == 2 &&
                firstDir->Name[0] == '.' && firstDir->Name[1] == '.')
            { // an up-directory is already present
                TRACE_E("CSalamanderDirectory::AddFile(): you can add up-dir (\"..\") at most once!");
                return NULL;
            }
            SalamDirs.Insert(0, NULL); // add NULL (the object will be allocated the first time it is needed)
            if (!SalamDirs.IsGood())
            {
                SalamDirs.ResetState();
                return NULL;
            }

            Dirs.Insert(0, dir);
            if (!Dirs.IsGood())
            {
                Dirs.ResetState();
                SalamDirs.Delete(0);
                if (!SalamDirs.IsGood())
                    SalamDirs.ResetState();
                return NULL;
            }
        }
        else
        {
            SalamDirs.Add(NULL); // add NULL (the object will be allocated the first time it is needed)
            if (!SalamDirs.IsGood())
            {
                SalamDirs.ResetState();
                return NULL;
            }

            Dirs.Add(dir);
            if (!Dirs.IsGood())
            {
                Dirs.ResetState();
                SalamDirs.Delete(SalamDirs.Count - 1);
                if (!SalamDirs.IsGood())
                    SalamDirs.ResetState();
                return NULL;
            }
        }
    }
    return this;
}

extern int DeltaForTotalCount(int total);

void CSalamanderDirectory::SetApproximateCount(int files, int dirs)
{
    CALL_STACK_MESSAGE3("CSalamanderDirectory::SetApproximateCount(%d, %d)", files, dirs);
    if (files > 1)
    {
        if (Files.Count == 0)
            Files.SetDelta(DeltaForTotalCount(files));
        else
            TRACE_E("CSalamanderDirectory::SetApproximateCount() Files.Count = " << Files.Count);
    }
    if (dirs > 1)
    {
        if (Dirs.Count == 0)
            Dirs.SetDelta(DeltaForTotalCount(dirs));
        else
            TRACE_E("CSalamanderDirectory::SetApproximateCount() Dirs.Count = " << Dirs.Count);
    }
}

void CSalamanderDirectory::ReleasePluginData(CPluginDataInterfaceEncapsulation& pluginData,
                                             BOOL releaseFiles, BOOL releaseDirs)
{
    SLOW_CALL_STACK_MESSAGE3("CSalamanderDirectory::ReleasePluginData(, %d, %d)",
                             releaseFiles, releaseDirs);
    if (releaseFiles)
        pluginData.ReleaseFilesOrDirs(&Files, FALSE);
    if (releaseDirs)
        pluginData.ReleaseFilesOrDirs(&Dirs, TRUE);
    int i;
    for (i = 0; i < SalamDirs.Count; i++)
    {
        CSalamanderDirectory* salDir = SalamDirs[i];
        if (salDir != NULL)
            salDir->ReleasePluginData(pluginData, releaseFiles, releaseDirs);
    }
}

CFilesArray*
CSalamanderDirectory::GetDirs(const wchar_t* path)
{
    CALL_STACK_MESSAGE2("CSalamanderDirectory::GetDirs(%ls)", path);
    if (path != NULL)
    {
        if (*path == L'\\')
            path++;
        if (*path != 0) // some subdirectory
        {
            const wchar_t* s = path;
            while (*s != 0 && *s != L'\\')
                s++;

            int i;
            for (i = 0; i < Dirs.Count; i++)
            {
                if (SalDirStrCmpEx(Dirs[i].Name, Dirs[i].NameLen, path, (int)(s - path)) == 0)
                {
                    CSalamanderDirectory* salDir = SalamDirs[i];
                    if (salDir != NULL ||                    // already allocated
                        (salDir = AllocSalamDir(i)) != NULL) // or succeeded in allocating a new object
                    {
                        return salDir->GetDirs(s);
                    }
                    else
                        return NULL; // low memory error (as if the directory did not exist)
                }
            }
        }
        else
            return &Dirs;
    }
    return NULL;
}

CFilesArray*
CSalamanderDirectory::GetFiles(const wchar_t* path)
{
    CALL_STACK_MESSAGE2("CSalamanderDirectory::GetFiles(%ls)", path);
    if (path != NULL)
    {
        if (*path == L'\\')
            path++;
        if (*path != 0) // some subdirectory
        {
            const wchar_t* s = path;
            while (*s != 0 && *s != L'\\')
                s++;

            int i;
            for (i = 0; i < Dirs.Count; i++)
            {
                if (SalDirStrCmpEx(Dirs[i].Name, Dirs[i].NameLen, path, (int)(s - path)) == 0)
                {
                    CSalamanderDirectory* salDir = SalamDirs[i];
                    if (salDir != NULL ||                    // already allocated
                        (salDir = AllocSalamDir(i)) != NULL) // or succeeded in allocating a new object
                    {
                        return salDir->GetFiles(s);
                    }
                    else
                        return NULL; // low memory error (as if the directory did not exist)
                }
            }
        }
        else
            return &Files;
    }
    return NULL;
}

const CFileData*
CSalamanderDirectory::GetUpperDir(const wchar_t* path)
{
    CALL_STACK_MESSAGE2("CSalamanderDirectory::GetUpperDir(%ls)", path);
    if (path != NULL)
    {
        if (*path == L'\\')
            path++;
        if (*path != 0) // some subdirectory
        {
            const wchar_t* s = path;
            while (*s != 0 && *s != L'\\')
                s++;

            int i;
            for (i = 0; i < Dirs.Count; i++)
            {
                if (SalDirStrCmpEx(Dirs[i].Name, Dirs[i].NameLen, path, (int)(s - path)) == 0)
                {
                    if (*s == 0 || *(s + 1) == 0)
                        return &Dirs[i]; // the last path component = the requested parent directory
                    else
                    {
                        CSalamanderDirectory* salDir = SalamDirs[i];
                        if (salDir != NULL ||                    // already allocated
                            (salDir = AllocSalamDir(i)) != NULL) // or succeeded in allocating a new object
                        {
                            return salDir->GetUpperDir(s);
                        }
                        else
                            return NULL; // low memory error (as if the directory did not exist)
                    }
                }
            }
        }
        else
            return NULL; // for root return NULL
    }
    return NULL; // for root and unknown paths return NULL
}

CQuadWord
CSalamanderDirectory::GetSize(int* dirsCount, int* filesCount, TDirectArray<CQuadWord>* sizes)
{
    CALL_STACK_MESSAGE1("CSalamanderDirectory::GetSize(,)");
    CQuadWord size(0, 0);
    int i;
    for (i = 0; i < Files.Count; i++)
    {
        size += Files[i].Size;
        if (sizes != NULL)
            sizes->Add(Files[i].Size); // addition failure is handled at the level of the output dialog
    }
    if (filesCount != NULL)
        *filesCount += Files.Count;
    for (i = 0; i < SalamDirs.Count; i++)
    {
        CSalamanderDirectory* salDir = SalamDirs[i];
        if (salDir != NULL)
            size += salDir->GetSize(dirsCount, filesCount, sizes);
    }
    if (dirsCount != NULL)
        *dirsCount += SalamDirs.Count;
    return size;
}

CQuadWord
CSalamanderDirectory::GetDirSize(const wchar_t* path, const wchar_t* dirName, int* dirsCount,
                                 int* filesCount, TDirectArray<CQuadWord>* sizes)
{
    CALL_STACK_MESSAGE3("CSalamanderDirectory::GetDirSize(%ls, %ls, , ,)", path, dirName);
    if (path != NULL)
    {
        if (*path == L'\\')
            path++;
        if (*path != 0) // some subdirectory
        {
            const wchar_t* s = path;
            while (*s != 0 && *s != L'\\')
                s++;

            int i;
            for (i = 0; i < Dirs.Count; i++)
            {
                if (SalDirStrCmpEx(Dirs[i].Name, Dirs[i].NameLen, path, (int)(s - path)) == 0)
                {
                    CSalamanderDirectory* salDir = SalamDirs[i];
                    if (salDir != NULL)
                        return salDir->GetDirSize(s, dirName, dirsCount, filesCount, sizes);
                    else
                        return CQuadWord(0, 0); // contains nothing; otherwise it would already be allocated
                }
            }
        }
        else
        {
            int i;
            for (i = 0; i < Dirs.Count; i++)
            {
                if (SalDirStrCmp(Dirs[i].Name, dirName) == 0)
                {
                    CSalamanderDirectory* salDir = SalamDirs[i];
                    if (salDir != NULL)
                        return salDir->GetSize(dirsCount, filesCount, sizes);
                    else
                        return CQuadWord(0, 0); // contains nothing; otherwise it would already be allocated
                }
            }
            TRACE_E("Incorrect call to CSalamanderDirectory::GetDirSize() - directory does not exist!");
            return CQuadWord(0, 0); // not found
        }
    }
    return CQuadWord(0, 0);
}

CSalamanderDirectory*
CSalamanderDirectory::GetSalamanderDir(const wchar_t* path, BOOL readOnly)
{
    CALL_STACK_MESSAGE_NONE
    // CALL_STACK_MESSAGE3("CSalamanderDirectory::GetSalamanderDir(%s, %d)", path, readOnly);
    if (path != NULL)
    {
        if (*path == L'\\')
            path++;
        if (*path != 0) // some subdirectory
        {
            const wchar_t* s = path;
            while (*s != 0 && *s != L'\\')
                s++;

            int i;
            for (i = 0; i < Dirs.Count; i++)
            {
                if (SalDirStrCmpEx(Dirs[i].Name, Dirs[i].NameLen, path, (int)(s - path)) == 0)
                {
                    CSalamanderDirectory* salDir = SalamDirs[i];
                    if (salDir != NULL)
                        return salDir->GetSalamanderDir(s, readOnly);
                    else // an empty directory
                    {
                        if (readOnly)
                            return &GlobalEmptySalDir; // read-only - return the global empty directory
                        else                           // for writing
                        {
                            if ((salDir = AllocSalamDir(i)) != NULL) // we must allocate a new object
                            {
                                return salDir->GetSalamanderDir(s, readOnly);
                            }
                            else
                                return NULL; // allocation error
                        }
                    }
                }
            }
        }
        else
            return this;
    }
    return NULL;
}

CSalamanderDirectory*
CSalamanderDirectory::GetSalamanderDir(int i)
{
    if (i >= 0 && i < SalamDirs.Count)
    {
        CSalamanderDirectory* salDir = SalamDirs[i];
        if (salDir == NULL)
            salDir = &GlobalEmptySalDir; // it's an empty directory - return the global empty directory
        return salDir;
    }
    else
        return NULL;
}

int CSalamanderDirectory::GetIndex(const wchar_t* dir)
{
    if (dir != NULL)
    {
        int i;
        for (i = 0; i < Dirs.Count; i++)
        {
            if (SalDirStrCmp(Dirs[i].Name, dir) == 0)
                return i;
        }
    }
    return -1; // not found
}

// ****************************************************************************

BOOL TestFreeSpace(HWND parent, const wchar_t* path, const CQuadWord& totalSize, const wchar_t* messageTitle)
{
    CQuadWord freeSpace = MyGetDiskFreeSpaceW(path);
    if (freeSpace != CQuadWord(-1, -1) && freeSpace < totalSize)
    {
        const std::wstring totalSizeText = NumberToStr(totalSize);
        const std::wstring freeSpaceText = NumberToStr(freeSpace);
        std::wstring msg = FormatStrW(LoadStrW(IDS_NOTENOUGHSPACE), totalSizeText.c_str(), freeSpaceText.c_str());
        return gPrompter->AskYesNo(messageTitle, msg.c_str()).type == PromptResult::kYes;
    }
    return TRUE;
}
