// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later
// CommentsTranslationProject: TRANSLATED

#include "precomp.h"
#include <sddl.h>

#include "common/IClipboard.h"
#include "common/IFileSystem.h"
#include "ui/IPrompter.h"
#include "ui/UnicodeHistoryUtils.h"
#include "common/unicode/ShellExtensionReason.h"
#include "common/unicode/helpers.h"
#include "common/IEnvironment.h"
#include "common/fsutil.h"
#include "common/OpenFileSelection.h"
#include "cfgdlg.h"
#include "mainwnd.h"
#include "plugins.h"
#include "fileswnd.h"
#include "dialogs.h"
#include "tasklist.h"
#include "md5.h"

CProgressDlgArray ProgressDlgArray; // array of disk operation dialogs (only dialogs running in their own threads)

// section for calling GetNextFileNameForViewer, GetPreviousFileNameForViewer,
// IsFileNameForViewerSelected, and SetSelectionOnFileNameForViewer
CRITICAL_SECTION FileNamesEnumSect;
// section for working with data tied to enumeration (FileNamesEnumSources, FileNamesEnumData,
// FileNamesEnumDone, NextRequestUID, and NextSourceUID)
CRITICAL_SECTION FileNamesEnumDataSect;

// array of sources for enumeration: even indexes (including zero): UID, odd indexes: HWND
TDirectArray<HWND> FileNamesEnumSources(10, 10);
// structure containing the enumeration request and results
CFileNamesEnumData FileNamesEnumData;
// the event is "signaled" once the source fills FileNamesEnumData with the result
HANDLE FileNamesEnumDone;
// next free UID of a request
int NextRequestUID = 0;
// next free UID of a source
int NextSourceUID = 0;

HWND GetWndToFlash(HWND parent)
{
    HWND mainWnd = parent;
    if (parent != NULL)
    {
        HWND tmp;
        while ((tmp = ::GetParent(mainWnd)) != NULL && IsWindowEnabled(tmp))
            mainWnd = tmp;
        HWND foregrWnd = GetForegroundWindow();
        if (foregrWnd != mainWnd)
        {
            while ((tmp = ::GetParent(mainWnd)) != NULL)
                mainWnd = tmp;
            FlashWindow(mainWnd, TRUE);
        }
        else
            mainWnd = NULL;
    }
    return mainWnd;
}

BOOL CALLBACK CloseAllOwnedEnabledDialogsEnumProc(HWND wnd, LPARAM lParam)
{
    HWND parent = (HWND)lParam;
    LONG style = GetWindowLong(wnd, GWL_STYLE);
    if ((style & WS_CHILD) == 0 && IsWindowEnabled(wnd) && IsWindowVisible(wnd))
    {
        wchar_t clsName[200];
        if (GetClassNameW(wnd, clsName, _countof(clsName)) != 0 && wcscmp(clsName, L"#32770") == 0) // DIALOG class
        {
            HWND owner = wnd;
            while (1)
            {
                owner = IsWindow(owner) ? GetWindow(owner, GW_OWNER) : NULL;
                if (owner == NULL)
                    break;
                if (owner == parent)
                {
                    PostMessage(wnd, WM_CLOSE, 0, 0);
                    break;
                }
            }
        }
    }
    return TRUE; // walk through all windows; there may be multiple dialogs next to each other from the same owner
}

void CloseAllOwnedEnabledDialogs(HWND parent, DWORD tid)
{
    if (!IsWindowEnabled(parent))
        EnumThreadWindows(tid == 0 ? GetCurrentThreadId() : tid, CloseAllOwnedEnabledDialogsEnumProc, (LPARAM)parent);
}

void InitFileNamesEnumForViewers()
{
    CALL_STACK_MESSAGE_NONE
    HANDLES(InitializeCriticalSection(&FileNamesEnumSect));
    HANDLES(InitializeCriticalSection(&FileNamesEnumDataSect));
    FileNamesEnumDone = HANDLES(CreateEvent(NULL, FALSE, FALSE, NULL)); // auto, nonsignaled
    if (FileNamesEnumDone == NULL)
        TRACE_E("Unable to create synchronization event for enumerating of file names for viewers!");
}

void ReleaseFileNamesEnumForViewers()
{
    CALL_STACK_MESSAGE_NONE
    if (FileNamesEnumDone != NULL)
        HANDLES(CloseHandle(FileNamesEnumDone));
    HANDLES(DeleteCriticalSection(&FileNamesEnumDataSect));
    HANDLES(DeleteCriticalSection(&FileNamesEnumSect));
}

BOOL IsFileEnumSourcePanel(int srcUID, int* panel)
{
    CALL_STACK_MESSAGE2("IsFileEnumSourcePanel(%d,)", srcUID);

    BOOL ret = FALSE;
    if (panel != NULL)
        *panel = -1;
    HANDLES(EnterCriticalSection(&FileNamesEnumDataSect));
    int i;
    for (i = 0; i < FileNamesEnumSources.Count; i += 2)
    {
        if ((int)(UINT_PTR)(FileNamesEnumSources[i]) == srcUID)
        {
            if (i + 1 < FileNamesEnumSources.Count &&
                MainWindow != NULL && MainWindow->LeftPanel != NULL && MainWindow->RightPanel != NULL) // just to be extra safe
            {
                HWND hWnd = FileNamesEnumSources[i + 1];
                if (MainWindow->LeftPanel->HWindow == hWnd)
                {
                    ret = TRUE;
                    if (panel != NULL)
                        *panel = PANEL_LEFT;
                }
                if (MainWindow->RightPanel->HWindow == hWnd)
                {
                    ret = TRUE;
                    if (panel != NULL)
                        *panel = PANEL_RIGHT;
                }
            }
            break;
        }
    }
    HANDLES(LeaveCriticalSection(&FileNamesEnumDataSect));
    return ret;
}

BOOL GetFileNameForViewer(CFileNamesEnumRequestType requestType, int srcUID, int* lastFileIndex, const wchar_t* lastFileName,
                          BOOL preferSelected, BOOL onlyAssociatedExtensions, std::wstring* fileName,
                          BOOL* noMoreFiles, BOOL* srcBusy, CPluginInterfaceAbstract* plugin,
                          BOOL* isFileSelected, BOOL select)
{
    CALL_STACK_MESSAGE9("GetFileNameForViewer(%d, %d, %d, %ls, %d, %d, %ls, , , %d)",
                        requestType, srcUID, *lastFileIndex, lastFileName, preferSelected,
                         onlyAssociatedExtensions,
                         fileName != NULL ? fileName->c_str() : L"", select);
    if (noMoreFiles != NULL)
        *noMoreFiles = FALSE;
    if (srcBusy != NULL)
        *srcBusy = FALSE;
    if (isFileSelected != NULL)
        *isFileSelected = FALSE;
    if (FileNamesEnumDone == NULL)
        return FALSE; // this will probably never happen, but we still handle it (error: "source does not exist")

    BOOL ret = FALSE;
    HANDLES(EnterCriticalSection(&FileNamesEnumSect));

    HANDLES(EnterCriticalSection(&FileNamesEnumDataSect));
    int reqUID = FileNamesEnumData.RequestUID = NextRequestUID++;
    FileNamesEnumData.RequestType = requestType;
    FileNamesEnumData.SrcUID = srcUID;
    FileNamesEnumData.LastFileIndex = *lastFileIndex;
    FileNamesEnumData.LastFileName = lastFileName != NULL ? lastFileName : L"";
    FileNamesEnumData.PreferSelected = preferSelected;
    FileNamesEnumData.OnlyAssociatedExtensions = onlyAssociatedExtensions;
    FileNamesEnumData.Plugin = plugin;
    FileNamesEnumData.FileNameW.clear();
    FileNamesEnumData.TimedOut = FALSE;
    FileNamesEnumData.Found = FALSE;
    FileNamesEnumData.NoMoreFiles = FALSE;
    FileNamesEnumData.SrcBusy = FALSE;
    FileNamesEnumData.IsFileSelected = FALSE;
    FileNamesEnumData.Select = select;
    ResetEvent(FileNamesEnumDone);
    HWND hWnd = NULL;
    int i;
    for (i = 0; i < FileNamesEnumSources.Count; i += 2)
    {
        if ((int)(UINT_PTR)(FileNamesEnumSources[i]) == srcUID)
        {
            if (i + 1 < FileNamesEnumSources.Count)
                hWnd = FileNamesEnumSources[i + 1];
            break;
        }
    }
    HANDLES(LeaveCriticalSection(&FileNamesEnumDataSect));

    if (hWnd != NULL)
    {
        PostMessage(hWnd, WM_USER_ENUMFILENAMES, reqUID, 0);

        DWORD waitRes = WaitForSingleObject(FileNamesEnumDone, FILENAMESENUM_TIMEOUT);
        if (waitRes == WAIT_OBJECT_0) // done
        {
            HANDLES(EnterCriticalSection(&FileNamesEnumDataSect));
            *lastFileIndex = FileNamesEnumData.LastFileIndex;
            if (fileName != NULL)
                *fileName = FileNamesEnumData.FileNameW;
            if (noMoreFiles != NULL)
                *noMoreFiles = FileNamesEnumData.NoMoreFiles;
            if (srcBusy != NULL)
                *srcBusy = FileNamesEnumData.SrcBusy;
            if (isFileSelected != NULL)
                *isFileSelected = FileNamesEnumData.IsFileSelected;
            ret = FileNamesEnumData.Found;
            HANDLES(LeaveCriticalSection(&FileNamesEnumDataSect));
        }
        else // maybe a timeout
        {
            HANDLES(EnterCriticalSection(&FileNamesEnumDataSect));
            waitRes = WaitForSingleObject(FileNamesEnumDone, 0);
            if (waitRes == WAIT_OBJECT_0) // done (the timeout was a false alarm; the message was delivered, the name lookup just was not finished)
            {
                *lastFileIndex = FileNamesEnumData.LastFileIndex;
                if (fileName != NULL)
                    *fileName = FileNamesEnumData.FileNameW;
                if (noMoreFiles != NULL)
                    *noMoreFiles = FileNamesEnumData.NoMoreFiles;
                if (srcBusy != NULL)
                    *srcBusy = FileNamesEnumData.SrcBusy;
                if (isFileSelected != NULL)
                    *isFileSelected = FileNamesEnumData.IsFileSelected;
                ret = FileNamesEnumData.Found;
            }
            else // a real timeout (timed out while delivering the message to the source)
            {
                FileNamesEnumData.TimedOut = TRUE;
                if (srcBusy != NULL)
                    *srcBusy = TRUE;
            }
            HANDLES(LeaveCriticalSection(&FileNamesEnumDataSect));
        }
    }

    HANDLES(LeaveCriticalSection(&FileNamesEnumSect));
    return ret;
}

BOOL GetNextFileNameForViewer(int srcUID, int* lastFileIndex, const wchar_t* lastFileName,
                              BOOL preferSelected, BOOL onlyAssociatedExtensions, std::wstring* fileName,
                              BOOL* noMoreFiles, BOOL* srcBusy,
                              CPluginInterfaceAbstract* plugin)
{
    CALL_STACK_MESSAGE_NONE
    return GetFileNameForViewer(fnertFindNext, srcUID, lastFileIndex, lastFileName, preferSelected,
                                onlyAssociatedExtensions, fileName, noMoreFiles,
                                 srcBusy, plugin, NULL, FALSE);
}

BOOL GetPreviousFileNameForViewer(int srcUID, int* lastFileIndex, const wchar_t* lastFileName,
                                  BOOL preferSelected, BOOL onlyAssociatedExtensions,
                                  std::wstring* fileName, BOOL* noMoreFiles, BOOL* srcBusy,
                                  CPluginInterfaceAbstract* plugin)
{
    CALL_STACK_MESSAGE_NONE
    return GetFileNameForViewer(fnertFindPrevious, srcUID, lastFileIndex, lastFileName, preferSelected,
                                onlyAssociatedExtensions, fileName, noMoreFiles,
                                 srcBusy, plugin, NULL, FALSE);
}

BOOL IsFileNameForViewerSelected(int srcUID, int lastFileIndex, const wchar_t* lastFileName,
                                 BOOL* isFileSelected, BOOL* srcBusy)
{
    CALL_STACK_MESSAGE_NONE
    return GetFileNameForViewer(fnertIsSelected, srcUID, &lastFileIndex, lastFileName, FALSE,
                                FALSE, NULL, NULL, srcBusy, NULL, isFileSelected, FALSE);
}

BOOL SetSelectionOnFileNameForViewer(int srcUID, int lastFileIndex, const wchar_t* lastFileName,
                                     BOOL select, BOOL* srcBusy)
{
    CALL_STACK_MESSAGE_NONE
    return GetFileNameForViewer(fnertSetSelection, srcUID, &lastFileIndex, lastFileName, FALSE,
                                FALSE, NULL, NULL, srcBusy, NULL, NULL, select);
}

void EnumFileNamesChangeSourceUID(HWND hWnd, int* srcUID)
{
    CALL_STACK_MESSAGE1("EnumFileNamesChangeSourceUID()");
    HANDLES(EnterCriticalSection(&FileNamesEnumDataSect));
    *srcUID = NextSourceUID++;
    int i;
    for (i = 1; i < FileNamesEnumSources.Count; i += 2)
    {
        if (FileNamesEnumSources[i] == hWnd)
        {
            FileNamesEnumSources[i - 1] = (HWND)(UINT_PTR)*srcUID;
            break;
        }
    }
    if (i >= FileNamesEnumSources.Count)
        TRACE_E("Incorrect call to EnumFileNamesChangeSourceUID(): hWnd is not in array of sources!");
    HANDLES(LeaveCriticalSection(&FileNamesEnumDataSect));
}

void EnumFileNamesAddSourceUID(HWND hWnd, int* srcUID)
{
    CALL_STACK_MESSAGE1("EnumFileNamesAddSourceUID()");
    HANDLES(EnterCriticalSection(&FileNamesEnumDataSect));
    *srcUID = NextSourceUID++;
    FileNamesEnumSources.Add((HWND)(UINT_PTR)(*srcUID));
    if (FileNamesEnumSources.IsGood())
    {
        FileNamesEnumSources.Add(hWnd);
        if (!FileNamesEnumSources.IsGood())
        {
            FileNamesEnumSources.ResetState();
            FileNamesEnumSources.Delete(FileNamesEnumSources.Count - 1);
            if (!FileNamesEnumSources.IsGood())
                FileNamesEnumSources.ResetState();
        }
    }
    else
        FileNamesEnumSources.ResetState();
    HANDLES(LeaveCriticalSection(&FileNamesEnumDataSect));
}

void EnumFileNamesRemoveSourceUID(HWND hWnd)
{
    CALL_STACK_MESSAGE1("EnumFileNamesRemoveSourceUID()");
    HANDLES(EnterCriticalSection(&FileNamesEnumDataSect));
    int i;
    for (i = 1; i < FileNamesEnumSources.Count; i += 2)
    {
        if (FileNamesEnumSources[i] == hWnd)
        {
            FileNamesEnumSources.Delete(i);
            if (!FileNamesEnumSources.IsGood())
                FileNamesEnumSources.ResetState();
            FileNamesEnumSources.Delete(i - 1);
            if (!FileNamesEnumSources.IsGood())
                FileNamesEnumSources.ResetState();
            break;
        }
    }
    HANDLES(LeaveCriticalSection(&FileNamesEnumDataSect));
}

void AddValueToStdHistoryValues(wchar_t** historyArr, int historyItemsCount,
                                const wchar_t* value, BOOL caseSensitiveValue)
{
    CALL_STACK_MESSAGE1("AddValueToStdHistoryValues()");
    AddValueToWideHistory(historyArr, historyItemsCount, value, caseSensitiveValue);
}


// 2026-08-26: the narrow IsPathOnVolumeSupADS(char*, ...) was deleted - confirmed-dead
// (zero real callers anywhere: core, plugins, tests - its only tests/ hits were a readBody() range
// marker and a ratchet CountTokens() absence check, neither a real call). IsPathOnVolumeSupADSW
// below is the only one actually used.

// Wide sibling - asked of a lossy CP_ACP mirror, IsPathOnVolumeSupADS could
// misreport ADS support for a non-ASCII panel/source path (same class of defect as
// the earlier MyGetVolumeInformationW fix for the adjacent ACL-support query).
BOOL IsPathOnVolumeSupADSW(const wchar_t* path, BOOL* isFAT32)
{
    CALL_STACK_MESSAGE1("IsPathOnVolumeSupADSW()");
    DWORD fileSystemFlags;
    std::wstring fileSystemName;
    if (isFAT32 != NULL)
        *isFAT32 = FALSE;
    if (!MyGetVolumeInformationW(path, NULL, NULL, NULL, NULL, NULL, NULL, &fileSystemFlags, &fileSystemName))
    {
        TRACE_EW(L"MyGetVolumeInformationW failed for: " << path);
        return TRUE; // we would rather assume the filesystem supports ADS; if not, something will fail later
    }
    if (isFAT32 != NULL)
        *isFAT32 = StrICmpW(fileSystemName.c_str(), L"FAT32") == 0;
    return (fileSystemFlags & FILE_NAMED_STREAMS) != 0 && StrICmpW(fileSystemName.c_str(), L"FAT") != 0 || // flag for ADS support (+ not a FAT volume — incorrectly reports ADS support on Windows DFS server) or
           StrICmpW(fileSystemName.c_str(), L"NTFS") == 0 && fileSystemFlags == 0x1F;                      // NTFS from NT 4.0
}

//******************************************************************************
//
// PrintDiskSize
//

std::wstring PrintDiskSize(const CQuadWord& size2, int mode)
{
    CALL_STACK_MESSAGE3("PrintDiskSize(, %g, %d)", size2.GetDouble(), mode);
    CQuadWord size(size2);

    std::wstring result;
    if (mode == 1 || mode == 2)
    {
        const std::wstring expanded = ExpandPluralStringOwnedW(
            HLanguage != NULL ? LoadStrW(IDS_PLURAL_X_BYTES) : L"{!}%s byte{s|0||1|s}",
            1, &size);
        const std::wstring number = NumberToStr(size);
        result = FormatStrW(expanded.c_str(), number.c_str());
    }
    switch (mode)
    {
    case 0: //mode==0 "1.23 MB"
    case 1: //mode==1 "1 230 000 bytes, 1.23 MB"
    case 4: //mode==4; always at least 3 significant digits, e.g. "2.00 MB"
    {
        int i = 0;
        while (size > CQuadWord(0xFFFFFFFF, 0)) // first reduce it so it can be conveniently converted to double
        {
            size /= CQuadWord(1024, 0); // integer division! (shifting would work too, but...)
            i++;
        }

        double sizeDouble = size.GetDouble(); // perform the rest of the calculation in double because we need decimal places
        for (; i < 6; i++)
        {
            if (sizeDouble >= 1023.5)
                sizeDouble /= 1024; // division in double!
            else
                break;
        }
        const wchar_t* s = NULL;
        switch (i)
        {
        case 0:
        {
            if (mode == 0 || mode == 4)
                s = HLanguage != NULL ? LoadStrW(IDS_SIZE_B) : L"B";
            break;
        }

        case 1:
            s = HLanguage != NULL ? LoadStrW(IDS_SIZE_KB) : L"KB";
            break;
        case 2:
            s = HLanguage != NULL ? LoadStrW(IDS_SIZE_MB) : L"MB";
            break;
        case 3:
            s = HLanguage != NULL ? LoadStrW(IDS_SIZE_GB) : L"GB";
            break;
        case 4:
            s = HLanguage != NULL ? LoadStrW(IDS_SIZE_TB) : L"TB";
            break;
        case 5:
            s = HLanguage != NULL ? LoadStrW(IDS_SIZE_PB) : L"PB";
            break;
        case 6:
            s = HLanguage != NULL ? LoadStrW(IDS_SIZE_EB) : L"EB";
            break;
        }

        if (s != NULL)
        {
            if (sizeDouble > 0.01)
                sizeDouble += 1E-7; // we only use the first three digits; this removes 0.9999999 rounding glitches
            else
                sizeDouble = 0;
            std::wstring n;
            if (sizeDouble >= 999.5 && sizeDouble < 1000)
                sizeDouble = 1000; // the "rounding" will not happen automatically
            if (sizeDouble >= 1000)
                n = FormatStrW(L"%.4g", sizeDouble);
            else
            {
                n = FormatStrW(L"%.3g", sizeDouble);
                if (mode == 4)
                {
                    if (n.size() == 1)
                        n += L".00"; // "2" -> "2.00"
                    else if (n[1] == L'.')
                    {
                        if (n.size() == 3)
                            n += L'0'; // "2.2" -> "2.20"
                    }
                    else if (n.size() == 2)
                        n += L".0"; // "22" -> "22.0"
                }
            }
            PointToLocalDecimalSeparator(n);
            result += FormatStrW(L"%s%s %s%s", (mode == 1) ? L" (" : L"",
                                 n.c_str(), s, (mode == 1) ? L")" : L"");
        }
        break;
    }

    case 3: //mode==3 (always in whole KB)
    {
        size += CQuadWord(1023, 0);
        size /= CQuadWord(1024, 0); // integer division! (it could also be done by bit-shifting, but...)
        result = NumberToStr(size);
        result += L' ';
        result += HLanguage != NULL ? LoadStrW(IDS_SIZE_KB) : L"KB";
        break;
    }
    }
    return result;
}

//******************************************************************************
//
// PrintTimeLeft
//

std::wstring PrintTimeLeft(CQuadWord const& secs)
{
    CALL_STACK_MESSAGE1("PrintTimeLeft(,)");
    //  sprintf(buf, "%u:%02u:%02u", (int)(secs / CQuadWord(3600, 0)).Value,
    //          (int)((secs / CQuadWord(60, 0)) % CQuadWord(60, 0)).Value,
    //          (int)(secs % CQuadWord(60, 0)).Value);

    int s = (int)(secs % CQuadWord(60, 0)).Value;
    int m = (int)((secs / CQuadWord(60, 0)) % CQuadWord(60, 0)).Value;
    int h = (int)(secs / CQuadWord(3600, 0)).Value;
    std::wstring result;
    if (h > 0)
        result = FormatStrW(ProgDlgHoursStr.c_str(), h);
    if (m > 0)
    {
        if (!result.empty())
            result += L' ';
        result += FormatStrW(ProgDlgMinutesStr.c_str(), m);
    }
    if (s > 0 || result.empty())
    {
        if (!result.empty())
            result += L' ';
        result += FormatStrW(ProgDlgSecsStr.c_str(), s);
    }
    return result;
}

//
// ****************************************************************************
// CNames
//

CNames::CNames()
    : Dirs(10, 50), Files(10, 300)
{
    CaseSensitive = FALSE;
    NeedSort = TRUE;
}

CNames::~CNames()
{
    Clear();
}

void CNames::SetCaseSensitive(BOOL caseSensitive)
{
    if (CaseSensitive != caseSensitive)
    {
        CaseSensitive = caseSensitive;
        if (Dirs.Count > 1 || Files.Count > 1)
            NeedSort = TRUE;
    }
}

void SortNames(wchar_t* files[], int left, int right)
{

LABEL_SortNames:

    int i = left, j = right;
    wchar_t* pivot = files[(i + j) / 2];

    do
    {
        while (StrICmpW(files[i], pivot) < 0 && i < right)
            i++;
        while (StrICmpW(pivot, files[j]) < 0 && j > left)
            j--;

        if (i <= j)
        {
            wchar_t* swap = files[i];
            files[i] = files[j];
            files[j] = swap;
            i++;
            j--;
        }
    } while (i <= j);

    // we replaced the following "nice" code with one that saves stack space significantly (max. log(N) recursion depth)
    //  if (left < j) SortNames(files, left, j);
    //  if (i < right) SortNames(files, i, right);

    if (left < j)
    {
        if (i < right)
        {
            if (j - left < right - i) // we need to sort both "halves", so send the smaller one into recursion and process the other via "goto"
            {
                SortNames(files, left, j);
                left = i;
                goto LABEL_SortNames;
            }
            else
            {
                SortNames(files, i, right);
                right = j;
                goto LABEL_SortNames;
            }
        }
        else
        {
            right = j;
            goto LABEL_SortNames;
        }
    }
    else
    {
        if (i < right)
        {
            left = i;
            goto LABEL_SortNames;
        }
    }
}

void SortNamesCaseSensitive(wchar_t* files[], int left, int right)
{

LABEL_SortNamesCaseSensitive:

    int i = left, j = right;
    wchar_t* pivot = files[(i + j) / 2];

    do
    {
        while (wcscmp(files[i], pivot) < 0 && i < right)
            i++;
        while (wcscmp(pivot, files[j]) < 0 && j > left)
            j--;

        if (i <= j)
        {
            wchar_t* swap = files[i];
            files[i] = files[j];
            files[j] = swap;
            i++;
            j--;
        }
    } while (i <= j);

    // we replaced the following "nice" code with one that saves stack space significantly (max. log(N) recursion depth)
    //  if (left < j) SortNamesCaseSensitive(files, left, j);
    //  if (i < right) SortNamesCaseSensitive(files, i, right);

    if (left < j)
    {
        if (i < right)
        {
            if (j - left < right - i) // we need to sort both "halves", so send the smaller one into recursion and process the other via "goto"
            {
                SortNamesCaseSensitive(files, left, j);
                left = i;
                goto LABEL_SortNamesCaseSensitive;
            }
            else
            {
                SortNamesCaseSensitive(files, i, right);
                right = j;
                goto LABEL_SortNamesCaseSensitive;
            }
        }
        else
        {
            right = j;
            goto LABEL_SortNamesCaseSensitive;
        }
    }
    else
    {
        if (i < right)
        {
            left = i;
            goto LABEL_SortNamesCaseSensitive;
        }
    }
}

BOOL FindNameInArray(TDirectArray<wchar_t*>* items, const wchar_t* name, BOOL caseSensitive, int* foundOnIndex)
{
    int l = 0, r = items->Count - 1, m;
    while (1)
    {
        m = (l + r) / 2;
        int res = caseSensitive ? wcscmp(items->At(m), name) : StrICmpW(items->At(m), name);
        if (res == 0)
        {
            if (foundOnIndex != NULL)
                *foundOnIndex = m;
            return TRUE; // found
        }
        else
        {
            if (res > 0)
            {
                if (l == r || l > m - 1)
                    return FALSE; // not found
                r = m - 1;
            }
            else
            {
                if (l == r)
                    return FALSE; // not found
                l = m + 1;
            }
        }
    }
}

void CNames::Sort()
{
    if (!NeedSort) // not necessary; we are already sorted
        return;

    if (Dirs.Count > 1)
    {
        if (CaseSensitive)
            SortNamesCaseSensitive(Dirs.GetData(), 0, Dirs.Count - 1);
        else
            SortNames(Dirs.GetData(), 0, Dirs.Count - 1);
    }

    if (Files.Count > 1)
    {
        if (CaseSensitive)
            SortNamesCaseSensitive(Files.GetData(), 0, Files.Count - 1);
        else
            SortNames(Files.GetData(), 0, Files.Count - 1);
    }

    NeedSort = FALSE;
}

void CNames::Clear()
{
    int i;
    for (i = 0; i < Dirs.Count; i++)
        free(Dirs[i]);
    Dirs.DestroyMembers();
    for (i = 0; i < Files.Count; i++)
        free(Files[i]);
    Files.DestroyMembers();
}

BOOL CNames::Add(BOOL nameIsDir, const wchar_t* name)
{
    wchar_t* dup = DupStr(name);
    if (dup == NULL)
    {
        TRACE_E(LOW_MEMORY);
        return FALSE;
    }

    TDirectArray<wchar_t*>* items = nameIsDir ? &Dirs : &Files;

    items->Add(dup);
    if (!items->IsGood())
    {
        items->ResetState();
        return FALSE;
    }

    NeedSort = TRUE;

    return TRUE;
}

BOOL CNames::Contains(BOOL nameIsDir, const wchar_t* name, int* foundOnIndex)
{
    TDirectArray<wchar_t*>* items;
    if (foundOnIndex != NULL)
        *foundOnIndex = -1;
    if (nameIsDir)
    {
        if (Dirs.Count == 0)
            return FALSE;
        items = &Dirs;
    }
    else
    {
        if (Files.Count == 0)
            return FALSE;
        items = &Files;
    }

    if (NeedSort)
    {
        TRACE_E("CNames::Contains is called on unsorted data. Calling Sort() now.");
        Sort();
    }

    return FindNameInArray(items, name, CaseSensitive, foundOnIndex);
}

BOOL CNames::LoadFromClipboard(HWND hWindow)
{
    CALL_STACK_MESSAGE1("CNamesFromClipboard::LoadFromClipboard()");

    Clear();

    // Get text from clipboard using IClipboard interface
    std::wstring clipTextW;
    auto result = gClipboard->GetText(clipTextW);
    if (!result.success)
    {
        if (result.errorCode != ERROR_NOT_FOUND)
            TRACE_E("CNames::LoadFromClipboard(): gClipboard->GetText() failed!");
        return TRUE;
    }

    const wchar_t* text = clipTextW.c_str();
    const wchar_t* textEnd = text + clipTextW.length();

    // search from the left for CR | LF | CRLF or the end of memory
    // add the found file and directory names to the array
    const wchar_t* s = text;
    const wchar_t* begin = s;
    while (s <= textEnd)
    {
        // watch out! 's' and 'begin' will point past valid memory at the end => DO NOT READ
        if (s >= textEnd || *s == '\r' || *s == '\n' || *s == 0)
        {
            if (s - begin > 0)
            {
                std::wstring name(begin, s);
                // trim the backslash and whitespace at the end of the path
                if (!name.empty() && name.back() == L'\\')
                    name.pop_back();
                while (!name.empty() && name.back() <= L' ')
                    name.pop_back();

                // keep only the leaf and trim whitespace at its beginning
                const size_t separator = name.find_last_of(L'\\');
                if (separator != std::wstring::npos)
                    name.erase(0, separator + 1);
                size_t first = 0;
                while (first < name.length() && name[first] <= L' ')
                    ++first;
                if (first != 0)
                    name.erase(0, first);

                if (!name.empty())
                {
                    if (!Add(FALSE, name.c_str()))
                        break;
                }
            }
            begin = s + 1; // we are at the terminator, move the start past it
        }
        s++;
    }

    return TRUE;
}

//******************************************************************************
//
// CProgressDlgArray
//

CProgressDlgArray::CProgressDlgArray() : Dlgs(5, 10)
{
    HANDLES(InitializeCriticalSection(&Monitor));
}

CProgressDlgArray::~CProgressDlgArray()
{
    if (Dlgs.Count > 0)
        TRACE_E("Unexpected situation in CProgressDlgArray::~CProgressDlgArray(): some dialog with disk operation still exists!");
    HANDLES(DeleteCriticalSection(&Monitor));
}

CProgressDlgArrItem*
CProgressDlgArray::PrepareNewDlg()
{
    CALL_STACK_MESSAGE1("CProgressDlgArray::PrepareNewDlg()");

    HANDLES(EnterCriticalSection(&Monitor));
    CProgressDlgArrItem* ret = new CProgressDlgArrItem;
    if (ret != NULL)
    {
        Dlgs.Add(ret);
        if (!Dlgs.IsGood())
        {
            Dlgs.ResetState();
            delete ret;
            ret = NULL;
        }
    }
    else
        TRACE_E(LOW_MEMORY);
    HANDLES(LeaveCriticalSection(&Monitor));
    return ret;
}

void CProgressDlgArray::SetDlgData(CProgressDlgArrItem* dlg, HANDLE dlgThread, HWND dlgWindow)
{
    CALL_STACK_MESSAGE1("CProgressDlgArray::SetDlgData()");

    HANDLES(EnterCriticalSection(&Monitor));
    if (dlgThread != NULL)
        dlg->DlgThread = dlgThread;
    if (dlgWindow != NULL)
        dlg->DlgWindow = dlgWindow;
    HANDLES(LeaveCriticalSection(&Monitor));
}

void CProgressDlgArray::RemoveDlg(CProgressDlgArrItem* dlg)
{
    CALL_STACK_MESSAGE1("CProgressDlgArray::RemoveDlg()");

    HANDLES(EnterCriticalSection(&Monitor));
    int i;
    for (i = Dlgs.Count - 1; i >= 0; i--)
    {
        if (Dlgs[i] == dlg)
        {
            Dlgs.Delete(i);
            if (!Dlgs.IsGood())
                Dlgs.ResetState();
            break;
        }
    }
    if (i < 0)
        TRACE_E("CProgressDlgArray::RemoveDlg(): specified dialog was not found!");
    HANDLES(LeaveCriticalSection(&Monitor));
}

int CProgressDlgArray::RemoveFinishedDlgs()
{
    CALL_STACK_MESSAGE1("CProgressDlgArray::RemoveFinishedDlgs()");

    HANDLES(EnterCriticalSection(&Monitor));
    int i;
    for (i = Dlgs.Count - 1; i >= 0; i--)
    {
        CProgressDlgArrItem* dlg = Dlgs[i];
        DWORD code;
        if (dlg->DlgThread != NULL &&
            (!GetExitCodeThread(dlg->DlgThread, &code) || code != STILL_ACTIVE))
        { // found a finished thread, remove it from the array
            HANDLES(CloseHandle(dlg->DlgThread));
            Dlgs.Delete(i);
            if (!Dlgs.IsGood())
                Dlgs.ResetState();
        }
    }
    int count = Dlgs.Count;
    HANDLES(LeaveCriticalSection(&Monitor));
    return count;
}

void CProgressDlgArray::ClearDlgWindow(HWND hdlg)
{
    CALL_STACK_MESSAGE1("CProgressDlgArray::ClearDlgWindow()");

    HANDLES(EnterCriticalSection(&Monitor));
    if (hdlg != NULL)
    {
        int i;
        for (i = Dlgs.Count - 1; i >= 0; i--)
        {
            CProgressDlgArrItem* dlg = Dlgs[i];
            if (dlg->DlgWindow == hdlg)
            {
                dlg->DlgWindow = NULL;
                break;
            }
        }
        if (i < 0)
            TRACE_E("CProgressDlgArray::ClearDlgWindow(): specified dialog was not found!");
    }
    HANDLES(LeaveCriticalSection(&Monitor));
}

HWND CProgressDlgArray::GetNextOpenedDlg(int* index)
{
    CALL_STACK_MESSAGE1("CProgressDlgArray::GetNextOpenedDlg()");

    HANDLES(EnterCriticalSection(&Monitor));
    HWND ret = NULL;
    if (index != NULL)
    {
        if (*index >= Dlgs.Count)
            *index = 0;
        int attempts = 0;
        while (*index < Dlgs.Count)
        {
            ret = Dlgs[*index]->DlgWindow;
            if (ret != NULL)
            {
                (*index)++;
                break; // found an open dialog, return it
            }
            else // we encountered a closed dialog; we have to skip it
            {
                if (++attempts >= Dlgs.Count)
                    break; // all dialogs in the array are closed
                (*index)++;
                if (*index >= Dlgs.Count)
                    *index = 0;
            }
        }
    }
    HANDLES(LeaveCriticalSection(&Monitor));
    return ret;
}

void CProgressDlgArray::PostCancelToAllDlgs()
{
    CALL_STACK_MESSAGE1("CProgressDlgArray::PostCancelToAllDlgs()");

    HANDLES(EnterCriticalSection(&Monitor));
    int i;
    for (i = Dlgs.Count - 1; i >= 0; i--)
    {
        CProgressDlgArrItem* dlg = Dlgs[i];
        if (dlg->DlgWindow != NULL)
            PostMessage(dlg->DlgWindow, WM_USER_CANCELPROGRDLG, 0, 0);
    }
    HANDLES(LeaveCriticalSection(&Monitor));
}

void CProgressDlgArray::PostIconChange()
{
    CALL_STACK_MESSAGE1("CProgressDlgArray::PostIconChange()");

    HANDLES(EnterCriticalSection(&Monitor));
    int i;
    for (i = Dlgs.Count - 1; i >= 0; i--)
    {
        CProgressDlgArrItem* dlg = Dlgs[i];
        if (dlg->DlgWindow != NULL)
            PostMessage(dlg->DlgWindow, WM_USER_PROGRDLG_UPDATEICON, 0, 0);
    }
    HANDLES(LeaveCriticalSection(&Monitor));
}

//****************************************************************************
//
// CShellExecuteWnd
//

CShellExecuteWnd::CShellExecuteWnd()
    : CWindow(ooStatic)
{
    CanClose = FALSE;
}

CShellExecuteWnd::~CShellExecuteWnd()
{
    CanClose = TRUE;
    if (HWindow != NULL)
        DestroyWindow(HWindow);
}

HWND CShellExecuteWnd::Create(HWND hParent, const wchar_t* format, ...)
{
    va_list args;
    va_start(args, format);
    if (HWindow != NULL)
    {
        TRACE_E("CShellExecuteWnd::Create HWindow=0x" << HWindow);
    }
    else
    {
        CanClose = FALSE;
        va_list measureArgs;
        va_copy(measureArgs, args);
        const int required = _vscwprintf(format, measureArgs);
        va_end(measureArgs);
        std::vector<wchar_t> formatted(required >= 0 ? static_cast<size_t>(required) + 1 : 1, L'\0');
        if (required >= 0)
            _vsnwprintf_s(formatted.data(), formatted.size(), _TRUNCATE, format, args);

        // we inherit the size of the parent window because some users complained that emails
        // opened from SS (via Mozilla) are displayed in a tiny window; some shell extensions probably
        // take the size of CMINVOKECOMMANDINFOEX::hwnd into account when constructing their window, and 1x1 pixel was not
        // an optimal size
        RECT r;
        if (!IsWindow(hParent) || !GetClientRect(hParent, &r))
        {
            // hopefully a good default...
            r.right = 800;
            r.bottom = 600;
        }

        // the window will be stretched across the MainWindow/Find area and must be transparent (otherwise the dialog background will not redraw)
        // example: if I remove WS_EX_TRANSPARENT, open Find and press Delete (to the Recycle Bin), then move the
        // confirmation dialog for deleting to the Recycle Bin, the background of the Find window will not repaint beneath it (this shell window,
        // which has WM_ERASEBKGND/WM_PAINT suppressed, will show up there)
        CWindow::CreateEx(WS_EX_TRANSPARENT,
                           SHELLEXECUTE_CLASSNAMEW,
                           formatted.data(),
                           WS_CHILDWINDOW | WS_CLIPSIBLINGS | WS_CLIPCHILDREN | WS_VISIBLE, // without WS_CLIPSIBLINGS the main window flickered
                           0, 0, r.right, r.bottom,
                           hParent,
                           (HMENU)0,
                           HInstance,
                           this);
    }
    va_end(args);
    if (HWindow != NULL)
        return HWindow;
    else
        return hParent; // in case of an error we return at least the correct parent
}

LRESULT
CShellExecuteWnd::WindowProc(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    CALL_STACK_MESSAGE4("CShellExecuteWnd::WindowProc(0x%X, 0x%IX, 0x%IX)", uMsg, wParam, lParam);

    switch (uMsg)
    {
    case WM_ERASEBKGND:
    {
        // the window will be tucked under other child windows, but we'll disable erase just to be safe
        return TRUE;
    }

    case WM_DESTROY:
    {
        if (!CanClose)
        {
            MSG msg; // flush the message queue (WMP9 buffered Enter and pressed OK for us)
            // while (PeekMessageW(&msg, HWindow, 0, 0, PM_REMOVE));  // Petr: replaced it with just discarding messages from the keyboard (without TranslateMessage and DispatchMessage an endless loop threatens; observed when unloading Automation with memory leaks, before the message box about leaks appeared there was an infinite loop, WM_PAINT kept being added to the queue and we kept throwing it away)
            while (PeekMessageW(&msg, NULL, WM_KEYFIRST, WM_KEYLAST, PM_REMOVE))
                ;

            MSGBOXEX_PARAMS params;
            memset(&params, 0, sizeof(params));
            params.HParent = HWindow;
            params.Flags = MSGBOXEX_OK | MSGBOXEX_ICONINFORMATION;
            params.Caption = SALAMANDER_TEXT_VERSIONW();
            const std::wstring message = LoadStrOwned(IDS_SHELLEXTBREAK2);
            params.Text = message.c_str();
            SalMessageBoxEx(&params);

            // trigger a breakpoint
            SetBugReportReasonBreak(L"Some faulty shell extension has destroyed our window.\r\n" +
                                    GetWindowTextStringW(HWindow));
            TaskList.FireEvent(TASKLIST_TODO_BREAK, GetCurrentProcessId());

            // freeze this thread
            while (1)
                Sleep(1000);
            /*
        // the creation of the bug report has started in a separate thread
        // now we ensure this thread gets stuck as long as the bug report dialog is open
        // this thread will freeze on the following macro thanks to the DontSuspend and ExceptionExists variables
        CALL_STACK_MESSAGE1("CShellExecuteWnd::WindowProc: LOCK");
*/
        }
        break;
    }
    }
    return CWindow::WindowProc(uMsg, wParam, lParam);
}

struct EnumWndStruct
{
    std::wstring* Text;
    int Count;
};

static std::wstring GetWindowClassNameStringW(HWND hwnd)
{
    std::vector<wchar_t> className(64, L'\0');
    for (;;)
    {
        const int copied = GetClassNameW(hwnd, className.data(), (int)className.size());
        if (copied <= 0)
            return std::wstring();
        if ((size_t)copied < className.size() - 1)
            return std::wstring(className.data(), copied);
        className.resize(className.size() * 2, L'\0');
    }
}

BOOL CALLBACK EnumChildProc(HWND hwnd, LPARAM lParam)
{
    // is this a window whose class is SHELLEXECUTE_CLASSNAME?
    const std::wstring className = GetWindowClassNameStringW(hwnd);
    if (!className.empty() && _wcsicmp(className.c_str(), SHELLEXECUTE_CLASSNAMEW) == 0)
    {
        EnumWndStruct* data = (EnumWndStruct*)lParam;
        // the window title holds the requested string
        const std::wstring title = GetWindowTextStringW(hwnd);
        if (!title.empty())
        {
            Sally::Unicode::AppendShellExtensionWindowTitle(*data->Text, title);
        }
        data->Count++;
    }
    return TRUE; // keep searching; we want every window
}

int EnumCShellExecuteWnd(HWND hParent, std::wstring& text)
{
    // enumerate all child windows of hParent (also descends into sub-children)
    EnumWndStruct data;
    data.Text = &text;
    data.Count = 0;
    EnumChildWindows(hParent, EnumChildProc, (LPARAM)&data);
    return data.Count;
}

int IsFileLink(const wchar_t* fileExtension)
{
    // Was: fold three characters through LowerCase[] - a 256-entry BYTE table
    // (common/str.h:23) indexed by a wchar_t, an out-of-bounds read above U+00FF - into a
    // scratch buffer, then compare *(DWORD*)lowerExt against *(DWORD*)"lnk". A DWORD spans
    // four narrow characters but only TWO wide ones, so once the extension went wide the
    // comparison could never match and no file was reported as a link.
    //
    // _wcsicmp needs neither the scratch buffer nor the fold table, and the original's
    // "longer than three characters" early-out is implicit: a longer extension cannot equal
    // a three-character literal. Unlike the paint copy, this buffer fed nothing else, so
    // there is no stored-key fold to keep in step.
    return (_wcsicmp(fileExtension, L"lnk") == 0 ||
            _wcsicmp(fileExtension, L"pif") == 0 ||
            _wcsicmp(fileExtension, L"url") == 0)
               ? 1
               : 0;
}

// Wide version - no MAX_PATH buffers
BOOL IsSambaDrivePathW(const wchar_t* path)
{
    std::wstring root = GetRootPath(path);
    if (root.empty())
        return FALSE;

    DWORD dummy1, flags;
    wchar_t fsName[64]; // filesystem names are short (NTFS, FAT32, exFAT, etc.)
    if (GetVolumeInformationW(root.c_str(), NULL, 0, NULL, &dummy1, &flags, fsName, _countof(fsName)))
    {
        return _wcsicmp(fsName, L"NTFS") == 0 && (flags & FS_UNICODE_STORED_ON_DISK) == 0;
    }
    return FALSE;
}

// 2026-08-26: the narrow ANSI wrapper IsSambaDrivePath(char*) was deleted -
// confirmed-dead (zero callers anywhere: core, plugins, tests). IsSambaDrivePathW above is the
// only one actually used.

CTargetPathState GetTargetPathState(CTargetPathState upperDirState, const wchar_t* targetPath)
{
    switch (upperDirState)
    {
    case tpsUnknown:
    {
        DWORD attr = gFileSystem->GetFileAttributes(targetPath);
        if (attr == INVALID_FILE_ATTRIBUTES)
        {
            TRACE_E("GetTargetPathState(): unexpected situation, target path should always exists!");
            return tpsNotEncryptedNotExisting; // if the path truly does not exist, nothing happens anyway, and if we just cannot read the attributes, assume the path has no Encrypted attribute
        }
        if (attr & FILE_ATTRIBUTE_ENCRYPTED)
            return tpsEncryptedExisting;
        else
            return tpsNotEncryptedExisting;
    }

    case tpsEncryptedExisting:
    case tpsNotEncryptedExisting:
    {
        DWORD attr = gFileSystem->GetFileAttributes(targetPath);
        if (attr == INVALID_FILE_ATTRIBUTES) // the next subdirectory no longer exists, inherit the Encrypted attribute
            return upperDirState == tpsEncryptedExisting ? tpsEncryptedNotExisting : tpsNotEncryptedNotExisting;
        if (attr & FILE_ATTRIBUTE_ENCRYPTED)
            return tpsEncryptedExisting;
        else
            return tpsNotEncryptedExisting;
    }

    case tpsEncryptedNotExisting:
    case tpsNotEncryptedNotExisting:
        return upperDirState;
    }
    TRACE_E("GetTargetPathState(): unexpected situation, unknown value of upperDirState!");
    return tpsUnknown;
}

// Forwarder. LPOPENFILENAME IS LPOPENFILENAMEW now that UNICODE is unconditional,
// so this and SafeGetOpenFileNameW had become the same function written twice - and the copy below was
// the better one: it null-checks lpstrFile/nMaxFile before writing lpstrFile[0] (a plugin passing
// a NULL buffer crashed here) and passes NULL rather than an empty string when the Documents
// fallback itself fails. The signature stays spelled LPOPENFILENAME because it mirrors
// CSalamanderGeneral::SafeGetOpenFileName (plugins/shared/spl_gen.h) exactly; that vtable slot is
// unchanged.
BOOL SafeGetOpenFileName(LPOPENFILENAME lpofn) { return SafeGetOpenFileNameW(lpofn); }

// Forwarder. LPOPENFILENAME IS LPOPENFILENAMEW now that UNICODE is unconditional,
// so this and SafeGetSaveFileNameW had become the same function written twice - and the copy below was
// the better one: it null-checks lpstrFile/nMaxFile before writing lpstrFile[0] (a plugin passing
// a NULL buffer crashed here) and passes NULL rather than an empty string when the Documents
// fallback itself fails. The signature stays spelled LPOPENFILENAME because it mirrors
// CSalamanderGeneral::SafeGetSaveFileName (plugins/shared/spl_gen.h) exactly; that vtable slot is
// unchanged.
BOOL SafeGetSaveFileName(LPOPENFILENAME lpofn) { return SafeGetSaveFileNameW(lpofn); }

// Wide implementation shared by caller-owned and SDK buffer adapters.
BOOL SafeGetOpenFileNameW(LPOPENFILENAMEW lpofn)
{
    BOOL ret = GetOpenFileNameW(lpofn);
    if (!ret && FNERR_INVALIDFILENAME == CommDlgExtendedError())
    {
        // Windows refuse to open the dialog for a path like "C:\" or for a non-existent path.
        // In that case, force Documents
        std::wstring initDir;
        const wchar_t* oldInitDir = lpofn->lpstrInitialDir;
        if (!GetMyDocumentsOrDesktopPathW(initDir))
            initDir.clear();
        lpofn->lpstrInitialDir = initDir.empty() ? NULL : initDir.c_str();
        if (lpofn->lpstrFile != NULL && lpofn->nMaxFile > 0)
            lpofn->lpstrFile[0] = L'\0';
        ret = GetOpenFileNameW(lpofn);
        lpofn->lpstrInitialDir = oldInitDir;
    }
    if (!ret && CommDlgExtendedError() != 0 /* only if this is not Cancel in the dialog */)
        TRACE_E("Cannot open OpenFile dialog box. CommDlgExtendedError()=" << CommDlgExtendedError());
    return ret;
}

// Wide sibling of SafeGetSaveFileName, mirroring SafeGetOpenFileNameW's shape.
BOOL SafeGetSaveFileNameW(LPOPENFILENAMEW lpofn)
{
    BOOL ret = GetSaveFileNameW(lpofn);
    if (!ret && FNERR_INVALIDFILENAME == CommDlgExtendedError())
    {
        // Windows refuse to open the dialog for a path like "C:\" or for a non-existent path.
        // In that case, force Documents
        std::wstring initDir;
        const wchar_t* oldInitDir = lpofn->lpstrInitialDir;
        if (!GetMyDocumentsOrDesktopPathW(initDir))
            initDir.clear();
        lpofn->lpstrInitialDir = initDir.empty() ? NULL : initDir.c_str();
        if (lpofn->lpstrFile != NULL && lpofn->nMaxFile > 0)
            lpofn->lpstrFile[0] = L'\0';
        ret = GetSaveFileNameW(lpofn);
        lpofn->lpstrInitialDir = oldInitDir;
    }
    if (!ret && CommDlgExtendedError() != 0 /* only if this is not Cancel in the dialog */)
        TRACE_E("Cannot open SaveFile dialog box. CommDlgExtendedError()=" << CommDlgExtendedError());
    return ret;
}

static BOOL SafeGetFileNameOwnedW(LPOPENFILENAMEW lpofn, const std::wstring& seed,
                                  std::vector<wchar_t>& buffer, BOOL save)
{
    if (lpofn == NULL)
        return FALSE;

    const LPWSTR callerBuffer = lpofn->lpstrFile;
    const DWORD callerCapacity = lpofn->nMaxFile;

    // Starting size for the result buffer - a guess, not a ceiling; the loop below
    // grows it on FNERR_BUFFERTOOSMALL.
    //
    // It must not be derived from the seed. GetOpenFileNameW reports a short buffer
    // only AFTER the user has chosen, by closing the dialog and failing, so each
    // growth step costs a whole extra trip through the dialog. With the seed's length
    // as the first guess, an empty seed produced a two-character buffer - and
    // SafeGetOpenFileNamesOwnedW passes no seed at all, so every multi-select made the
    // user pick their files twice.
    static const size_t startingChars = 4096;
    buffer.assign((std::max<size_t>)(seed.length() + 1, startingChars), L'\0');

    BOOL selected = FALSE;
    while (buffer.size() <= MAXDWORD)
    {
        std::copy(seed.begin(), seed.end(), buffer.begin());
        buffer[seed.length()] = L'\0';
        lpofn->lpstrFile = buffer.data();
        lpofn->nMaxFile = static_cast<DWORD>(buffer.size());
        selected = save ? SafeGetSaveFileNameW(lpofn) : SafeGetOpenFileNameW(lpofn);
        if (selected || CommDlgExtendedError() != FNERR_BUFFERTOOSMALL)
            break;

        const size_t required = *reinterpret_cast<const WORD*>(buffer.data());
        const size_t doubled = buffer.size() <= MAXDWORD / 2 ? buffer.size() * 2 : MAXDWORD;
        const size_t nextSize = (std::max)(doubled, required + 1);
        if (nextSize <= buffer.size() || nextSize > MAXDWORD)
            break;
        buffer.assign(nextSize, L'\0');
    }

    lpofn->lpstrFile = callerBuffer;
    lpofn->nMaxFile = callerCapacity;
    return selected;
}

BOOL SafeGetOpenFileNameOwnedW(LPOPENFILENAMEW lpofn, std::wstring& fileName)
{
    std::vector<wchar_t> buffer;
    if (!SafeGetFileNameOwnedW(lpofn, fileName, buffer, FALSE))
        return FALSE;
    fileName.assign(buffer.data());
    return TRUE;
}

BOOL SafeGetSaveFileNameOwnedW(LPOPENFILENAMEW lpofn, std::wstring& fileName)
{
    std::vector<wchar_t> buffer;
    if (!SafeGetFileNameOwnedW(lpofn, fileName, buffer, TRUE))
        return FALSE;
    fileName.assign(buffer.data());
    return TRUE;
}

BOOL SafeGetOpenFileNamesOwnedW(LPOPENFILENAMEW lpofn, std::vector<std::wstring>& fileNames)
{
    std::vector<wchar_t> buffer;
    if (!SafeGetFileNameOwnedW(lpofn, std::wstring(), buffer, FALSE))
        return FALSE;
    fileNames = sally::unicode::DecodeOpenFileSelection(buffer.data(), buffer.size());
    return TRUE;
}

// Wide version - no MAX_PATH buffer limitations
void GetIfPathIsInaccessibleGoToW(std::wstring& path, BOOL forceIsMyDocs)
{
    path.clear();
    if (forceIsMyDocs || Configuration.IfPathIsInaccessibleGoToIsMyDocs)
    {
        if (!GetMyDocumentsOrDesktopPathW(path))
        {
            std::wstring winPath;
            if (gEnvironment->GetWindowsDirectory(winPath).success)
                path = GetRootPath(winPath.c_str());
            else
                path = L"C:\\";
        }
    }
    else
    {
        path = Configuration.IfPathIsInaccessibleGoTo;
        if (path.length() >= 2 && path[1] == L':')
        {
            path[0] = towupper(path[0]);
            if (path.length() == 2)
            { // "C:" -> "C:\"
                path += L'\\';
            }
        }
    }
}

HICON SalLoadImage(int vistaResID, int otherResID, int cx, int cy, UINT flags)
{
    // JRYFIXME - convert to calling SalLoadIcon
    // Try imageres.dll first, fall back to shell32.dll for Wine compatibility
    if (ImageResDLL != NULL)
    {
        return SalLoadIcon(ImageResDLL, vistaResID, cx);
    }
    else if (Shell32DLL != NULL)
    {
        return SalLoadIcon(Shell32DLL, otherResID, cx);
    }
    return NULL;
}

HICON LoadArchiveIcon(int cx, int cy, UINT flags)
{
    // JRYFIXME - convert to calling SalLoadIcon
    // Try imageres.dll (174), fall back to shell32.dll (165) for Wine compatibility
    if (ImageResDLL != NULL)
    {
        return SalLoadIcon(ImageResDLL, 174, cx);
    }
    else if (Shell32DLL != NULL)
    {
        return SalLoadIcon(Shell32DLL, 165, cx);
    }
    return NULL;
}

BOOL DuplicateBackslashes(std::wstring& text) noexcept
{
    try
    {
        std::wstring escaped;
        escaped.reserve(text.size());
        for (wchar_t ch : text)
        {
            escaped.push_back(ch);
            if (ch == L'\\')
                escaped.push_back(ch);
        }
        text.swap(escaped);
        return TRUE;
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
}

BOOL GetStringSid(LPWSTR* stringSid)
{
    *stringSid = NULL;

    HANDLE hToken = NULL;
    DWORD dwBufferSize = 0;
    PTOKEN_USER pTokenUser = NULL;

    // Open the access token associated with the calling process.
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &hToken))
    {
        TRACE_E("OpenProcessToken failed.");
        return FALSE;
    }

    // get the size of the memory buffer needed for the SID
    GetTokenInformation(hToken, TokenUser, NULL, 0, &dwBufferSize);

    pTokenUser = (PTOKEN_USER)malloc(dwBufferSize);
    memset(pTokenUser, 0, dwBufferSize);

    // Retrieve the token information in a TOKEN_USER structure.
    if (!GetTokenInformation(hToken, TokenUser, pTokenUser, dwBufferSize, &dwBufferSize))
    {
        TRACE_E("GetTokenInformation failed.");
        CloseHandle(hToken);
        return FALSE;
    }

    CloseHandle(hToken);

    if (!IsValidSid(pTokenUser->User.Sid))
    {
        TRACE_E("The owner SID is invalid.\n");
        free(pTokenUser);
        return FALSE;
    }

    // the caller must free the returned memory using LocalFree, see MSDN
    ConvertSidToStringSidW(pTokenUser->User.Sid, stringSid);

    free(pTokenUser);

    return TRUE;
}

BOOL GetSidMD5(BYTE* sidMD5)
{
    ZeroMemory(sidMD5, 16);

    HANDLE hToken = NULL;
    DWORD dwBufferSize = 0;
    PTOKEN_USER pTokenUser = NULL;

    // Open the access token associated with the calling process.
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &hToken))
    {
        TRACE_E("OpenProcessToken failed.");
        return FALSE;
    }

    // get the size of the memory buffer needed for the SID
    GetTokenInformation(hToken, TokenUser, NULL, 0, &dwBufferSize);

    pTokenUser = (PTOKEN_USER)malloc(dwBufferSize);
    memset(pTokenUser, 0, dwBufferSize);

    // Retrieve the token information in a TOKEN_USER structure.
    if (!GetTokenInformation(hToken, TokenUser, pTokenUser, dwBufferSize, &dwBufferSize))
    {
        TRACE_E("GetTokenInformation failed.");
        CloseHandle(hToken);
        return FALSE;
    }

    CloseHandle(hToken);

    if (!IsValidSid(pTokenUser->User.Sid))
    {
        TRACE_E("The owner SID is invalid.\n");
        free(pTokenUser);
        return FALSE;
    }

    MD5 context;
    context.update((BYTE*)pTokenUser->User.Sid, GetLengthSid(pTokenUser->User.Sid));
    context.finalize();
    memcpy(sidMD5, context.digest, 16);

    free(pTokenUser);

    return TRUE;
}

// For more information see:
//   http://www.codeguru.com/cpp/w-p/win32/tutorials/print.php/c4545 (A NotQuiteNullDacl Class)
//   http://forums.microsoft.com/msdn/ShowPost.aspx?PostID=748596&SiteID=1 (already deals with Vista)
//   Programming Server-Side Applications for Microsoft Windows 2000, Richter/Clark, Microsoft Press 2000, Chapter 10, pp 458-460
//   Ask Dr. Gui #49 (I cannot reliably find it online, so I'm inserting it here):
// Mutex Madness
// Dear Dr. GUI:
// I'm a French engineer and my English isn't perfect so I hope that you understand my question.
// I have a DLL that creates a mutex. I have some problems synchronizing all processes that use my DLL. I create the mutex with code that looks like the following:
// ghMutexExe = CreateMutex(NULL, FALSE , "APPLICOM_IO_MUTEX");
// When I use my DLL with an application that is running in Real time priority, I can't run another application that uses the same DLL. When I use the function:
// ghMutexExe = CreateMutex(NULL, FALSE , "APPLICOM_IO_MUTEX");
// It returns NULL (ghMutexExe = NULL), and GetLastError returns 5 (Access is denied. ERROR_ACCESS_DENIED).
// Can you help me?
// Bertrand Lauret
//
// Dr. GUI replies:
// Your English is jus fine. (Dr, GUI is just glad you didn't ask to get my reply in French.) Most people do not know this, but the good doctor is bilingual. He speaks English and C++.
// It is certainly possible to have problems with thread synchronization due to differing priorities of threads. However, it is very unlikely that you would receive an ERROR_ACCESS_DENIED when trying to obtain a handle to an existing mutex because of the priority class of the creating process.
// Typically, ERROR_ACCESS_DENIED is returned as a result of failure because of the security implications of the function being called. Let me describe a scenario where this could happen with Microsoft Windows NT? or Windows 2000:
// Process A is running as a service (perhaps in real time, perhaps not) and creates a named mutex passing NULL as the first parameter indicating default security for the object.
// Process B is launched by the interactive user and attempts to obtain a handle to the named mutex through a similar call to CreateMutex. This call fails with ERROR_ACCESS_DENIED because the default security of the service excludes all but the local system for ALL_ACCESS security to the object. This process does not have access even if it is running as an administrator of the system.
// This type of failure is the result of a combination of points:
// Most services are installed in the local system account and run with special security rights as a result.
// Processes running in the local system account grant GENERIC_ALL access to other processes running in the local system, and READ_CONTROL, GENERIC_EXECUTE, and GENERIC_READ access to members of the Administrators group. All other access to the object by any other users or groups is denied.
// All calls to CreateMutex implicitly request MUTEX_ALL_ACCESS for the object in question. An interactive user does not have the rights required to obtain a handle to an object created from the local system security context as a result.
// There are several solutions to this problem:
// You can set the security descriptor in your call to CreateMutex to contain a "NULL DACL." An object with NULL-DACL security grants all access to everyone, regardless of security context. One downside of this approach is that the object is now completely unsecured. In fact, a malicious application could obtain a handle to a named object created with a NULL DACL and change its security access such that other processes are unable to use the object, effectively ruining the object. PLEASE NOTE: The doctor seriously advises against this "solution."
// A second option would be to create a security descriptor that explicitly grants the necessary rights to the built-in group: Everyone. In the case of a mutex, this would be MUTEX_ALL_ACCESS. This is preferable because it will not allow malicious (or buggy) software to affect other software's access to the object.
// You can also choose to explicitly allow access to the user accounts that will need to use the object. This type of explicit security can be good but comes with the disadvantage of needing to know who will need access at the time the object is created.
// My preference in cases of creating objects for general availability to users of the system is the second. Regardless of which approach you choose, you have the additional choice of whether to apply the security descriptor to each object as it is created, or to change the default security for your process by changing the token's default DACL. In this case, you would continue to pass NULL for the security parameter when creating an object.
// Dr. GUI thinks that in your case you should only set the security for specific objects because you are developing a DLL, which may not want to change the security for every object in the process.
// The following function wraps CreateMutex with the additional functionality of creating security that is more relaxed than is the default for a service:
/*
    // grant the mutex every possible right (so opening between AsAdmin and User accounts works, for example)
    // calling ObtainAccessableMutex() would be cleaner; see its extensive comment
    SECURITY_ATTRIBUTES secAttr;
    wchar_t secDesc[ SECURITY_DESCRIPTOR_MIN_LENGTH ];
    secAttr.nLength = sizeof(secAttr);
    secAttr.bInheritHandle = FALSE;
    secAttr.lpSecurityDescriptor = &secDesc;
    InitializeSecurityDescriptor(secAttr.lpSecurityDescriptor, SECURITY_DESCRIPTOR_REVISION);
    // give the security descriptor a NULL DACL, done using the  "TRUE, (PACL)NULL" here
    SetSecurityDescriptorDacl(secAttr.lpSecurityDescriptor, TRUE, 0, FALSE);
*/

/*
// according to http://forums.microsoft.com/msdn/ShowPost.aspx?PostID=748596&SiteID=1
// the integrity level should be set on Vista, but I have not been able to reproduce the problem on Vista/Server 2008
// so for now I am keeping it only in the comment until we run into it
//
// Windows Integrity Mechanism Design
// http://msdn.microsoft.com/en-us/library/bb625963.aspx
if (windowsVistaAndLater) // FIXME: I have not encountered a situation on Vista where I had to deal with integrity levels (between AsAdmin / regular applications)
{
  PSECURITY_DESCRIPTOR pSD;
  ConvertStringSecurityDescriptorToSecurityDescriptor(
      "S:(ML;;NW;;;LW)", // this means "low integrity"
      SDDL_REVISION_1,
      &pSD,
      NULL);
  PACL pSacl = NULL;                  // not allocated
  BOOL fSaclPresent = FALSE;
  BOOL fSaclDefaulted = FALSE;
  GetSecurityDescriptorSacl(
      pSD,
      &fSaclPresent,
      &pSacl,
      &fSaclDefaulted);
  SetSecurityDescriptorSacl(secAttr.lpSecurityDescriptor, TRUE, pSacl, FALSE);
}
*/

SECURITY_ATTRIBUTES* CreateAccessableSecurityAttributes(SECURITY_ATTRIBUTES* sa, SECURITY_DESCRIPTOR* sd,
                                                        DWORD allowedAccessMask, PSID* psidEveryone, PACL* paclNewDacl)
{
    SID_IDENTIFIER_AUTHORITY siaWorld = SECURITY_WORLD_SID_AUTHORITY;
    int nAclSize;

    *psidEveryone = NULL;
    *paclNewDacl = NULL;

    // Create the everyone sid
    if (!AllocateAndInitializeSid(&siaWorld, 1, SECURITY_WORLD_RID, 0, 0, 0, 0, 0, 0, 0, psidEveryone))
    {
        TRACE_E("CreateAccessableSecurityAttributes(): AllocateAndInitializeSid() failed!");
        goto ErrorExit;
    }

    nAclSize = GetLengthSid(psidEveryone) * 2 + sizeof(ACCESS_ALLOWED_ACE) + sizeof(ACCESS_DENIED_ACE) + sizeof(ACL);
    *paclNewDacl = (PACL)LocalAlloc(LPTR, nAclSize);
    if (*paclNewDacl == NULL)
    {
        TRACE_E("CreateAccessableSecurityAttributes(): LocalAlloc() failed!");
        goto ErrorExit;
    }
    if (!InitializeAcl(*paclNewDacl, nAclSize, ACL_REVISION))
    {
        TRACE_E("CreateAccessableSecurityAttributes(): InitializeAcl() failed!");
        goto ErrorExit;
    }
    if (!AddAccessDeniedAce(*paclNewDacl, ACL_REVISION, WRITE_DAC | WRITE_OWNER, *psidEveryone))
    {
        TRACE_E("CreateAccessableSecurityAttributes(): AddAccessDeniedAce() failed!");
        goto ErrorExit;
    }
    if (!AddAccessAllowedAce(*paclNewDacl, ACL_REVISION, allowedAccessMask, *psidEveryone))
    {
        TRACE_E("CreateAccessableSecurityAttributes(): AddAccessAllowedAce() failed!");
        goto ErrorExit;
    }
    if (!InitializeSecurityDescriptor(sd, SECURITY_DESCRIPTOR_REVISION))
    {
        TRACE_E("CreateAccessableSecurityAttributes(): InitializeSecurityDescriptor() failed!");
        goto ErrorExit;
    }
    if (!SetSecurityDescriptorDacl(sd, TRUE, *paclNewDacl, FALSE))
    {
        TRACE_E("CreateAccessableSecurityAttributes(): SetSecurityDescriptorDacl() failed!");
        goto ErrorExit;
    }
    sa->nLength = sizeof(SECURITY_ATTRIBUTES);
    sa->bInheritHandle = FALSE;
    sa->lpSecurityDescriptor = sd;
    return sa;

ErrorExit:
    if (*paclNewDacl != NULL)
    {
        LocalFree(*paclNewDacl);
        *paclNewDacl = NULL;
    }
    if (*psidEveryone != NULL)
    {
        FreeSid(*psidEveryone);
        *psidEveryone = NULL;
    }
    return NULL;
}

//****************************************************************************
//
// GetProcessIntegrityLevel (taken from MSDN)
// On success, returns TRUE and fills the DWORD referenced by 'integrityLevel'
// otherwise (on failure or on OS versions older than Vista) it returns FALSE
//

BOOL GetProcessIntegrityLevel(DWORD* integrityLevel)
{
    HANDLE hToken;
    HANDLE hProcess;

    DWORD dwLengthNeeded;
    DWORD dwError = ERROR_SUCCESS;

    PTOKEN_MANDATORY_LABEL pTIL = NULL;
    DWORD dwIntegrityLevel;

    BOOL ret = FALSE;

    if (WindowsVistaAndLater) // integrity levels were introduced starting with Windows Vista
    {
        hProcess = GetCurrentProcess();
        if (OpenProcessToken(hProcess, TOKEN_QUERY, &hToken))
        {
            // Get the Integrity level.
            if (!GetTokenInformation(hToken, (_TOKEN_INFORMATION_CLASS)25 /*TokenIntegrityLevel*/, NULL, 0, &dwLengthNeeded))
            {
                dwError = GetLastError();
                if (dwError == ERROR_INSUFFICIENT_BUFFER)
                {
                    pTIL = (PTOKEN_MANDATORY_LABEL)LocalAlloc(0, dwLengthNeeded);
                    if (pTIL != NULL)
                    {
                        if (GetTokenInformation(hToken, (_TOKEN_INFORMATION_CLASS)25 /*TokenIntegrityLevel*/, pTIL, dwLengthNeeded, &dwLengthNeeded))
                        {
                            dwIntegrityLevel = *GetSidSubAuthority(pTIL->Label.Sid, (DWORD)(UCHAR)(*GetSidSubAuthorityCount(pTIL->Label.Sid) - 1));
                            ret = TRUE;

                            /*
              if (dwIntegrityLevel == SECURITY_MANDATORY_LOW_RID)
              {
               // Low Integrity
               wprintf(L"Low Process");
              }
              else if (dwIntegrityLevel >= SECURITY_MANDATORY_MEDIUM_RID && 
                   dwIntegrityLevel < SECURITY_MANDATORY_HIGH_RID)
              {
               // Medium Integrity
               wprintf(L"Medium Process");
              }
              else if (dwIntegrityLevel >= SECURITY_MANDATORY_HIGH_RID)
              {
               // High Integrity
               wprintf(L"High Integrity Process");
              }
              else if (dwIntegrityLevel >= SECURITY_MANDATORY_SYSTEM_RID)
              {
               // System Integrity
               wprintf(L"System Integrity Process");
              }
              */
                        }
                        LocalFree(pTIL);
                    }
                }
            }
            CloseHandle(hToken);
        }
    }
    if (ret)
        *integrityLevel = dwIntegrityLevel;
    else
        *integrityLevel = 0;
    return ret;
}

// Wide sibling.
//
// THE HAZARD HERE IS THE COUNT, NOT THE STRING. 'lpcbData' is a size in BYTES on
// both sides of RegQueryValueW — it does NOT become a character count when the
// data goes wide. So every terminator calculation the narrow version does by
// indexing bytes has to be re-expressed: the last code unit lives at
// index (*lpcbData / sizeof(wchar_t)) - 1, and reserving room for a terminator
// costs sizeof(wchar_t), not 1. Getting that wrong would either truncate the
// last character or silently write one WCHAR past the caller's buffer.
LONG SalRegQueryValueW(HKEY hKey, LPCWSTR lpSubKey, LPWSTR lpData, PLONG lpcbData)
{
    DWORD dataBufSize = lpData == NULL || lpcbData == NULL ? 0 : *lpcbData;
    LONG ret = RegQueryValueW(hKey, lpSubKey, lpData, lpcbData);
    if (lpcbData != NULL &&
        (ret == ERROR_MORE_DATA || lpData == NULL && ret == ERROR_SUCCESS))
    {
        *lpcbData += (LONG)sizeof(wchar_t); // proactively ask for a possible extra null terminator
    }
    if (ret == ERROR_SUCCESS && lpData != NULL)
    {
        const LONG units = *lpcbData / (LONG)sizeof(wchar_t);
        if (units < 1 || lpData[units - 1] != 0)
        {
            if ((DWORD)*lpcbData + sizeof(wchar_t) <= dataBufSize) // REG_SZ / REG_EXPAND_SZ only, so one terminator is enough
            {
                lpData[units] = 0;
                *lpcbData += (LONG)sizeof(wchar_t);
            }
            else // not enough room for the null terminator in the buffer
            {
                *lpcbData += (LONG)sizeof(wchar_t); // request the necessary null terminator
                return ERROR_MORE_DATA;
            }
        }
    }
    return ret;
}

// Collapsed into a forwarder onto SalRegQueryValueW above, whose signature this
// is byte-identical to. The body that used to live here was the NARROW original with (wchar_t*)
// casts bolted on: it indexed the wchar_t array with *lpcbData - a BYTE count - so an unterminated
// REG_SZ had its terminator written at byte offset 2 * (*lpcbData), past the caller's buffer.
// Exactly the hazard SalRegQueryValueW's own header comment warns about. Forwarding deletes the
// second, wrong copy of the terminator arithmetic rather than repairing it in parallel.
// NOTE: consts.h:2668 still declares a NARROW SalRegQueryValue inside extern "C", so nothing can
// reach this overload yet; widening that declaration is a separate ~10-caller cluster.
LONG SalRegQueryValue(HKEY hKey, LPCWSTR lpSubKey, LPWSTR lpData, PLONG lpcbData)
{
    return SalRegQueryValueW(hKey, lpSubKey, lpData, lpcbData);
}

LONG SalRegQueryValueEx(HKEY hKey, LPCWSTR lpValueName, LPDWORD lpReserved,
                        LPDWORD lpType, LPBYTE lpData, LPDWORD lpcbData)
{
    DWORD dataBufSize = lpData == NULL ? 0 : *lpcbData;
    DWORD type = REG_NONE;
    // RegQueryValueExW: lpValueName is already LPCWSTR here, so the unsuffixed
    // form was a live C2664 against RegQueryValueExA.
    LONG ret = RegQueryValueExW(hKey, lpValueName, lpReserved, &type, lpData, lpcbData);
    if (lpType != NULL)
        *lpType = type;
    if (type == REG_SZ || type == REG_MULTI_SZ || type == REG_EXPAND_SZ)
    {
        // THE COUNT IS IN BYTES, THE DATA IS IN WCHARs. RegQueryValueExW reports
        // 'lpcbData' as a BYTE count - going wide does not turn it into a character count. This
        // block used to index (wchar_t*)lpData with *lpcbData directly, so an unterminated REG_SZ
        // got its terminator written at byte offset 2 * (*lpcbData): one whole value-length past
        // the caller's buffer. Every terminator now costs sizeof(wchar_t) and every subscript goes
        // through 'units', mirroring SalRegQueryValueW above.
        if (hKey != HKEY_PERFORMANCE_DATA &&
            lpcbData != NULL &&
            (ret == ERROR_MORE_DATA || lpData == NULL && ret == ERROR_SUCCESS))
        {
            // proactively ask for the possible extra null terminator(s)
            (*lpcbData) += (type == REG_MULTI_SZ ? 2 : 1) * (DWORD)sizeof(wchar_t);
            return ret;
        }
        if (ret == ERROR_SUCCESS && lpData != NULL)
        {
            wchar_t* data = (wchar_t*)lpData;
            DWORD units = *lpcbData / (DWORD)sizeof(wchar_t);
            if (units < 1 || data[units - 1] != 0)
            {
                if (*lpcbData + sizeof(wchar_t) <= dataBufSize)
                {
                    data[units++] = 0;
                    *lpcbData += (DWORD)sizeof(wchar_t);
                }
                else // not enough room for the null terminator in the buffer
                {
                    // request the necessary null terminator(s)
                    (*lpcbData) += (type == REG_MULTI_SZ ? 2 : 1) * (DWORD)sizeof(wchar_t);
                    return ERROR_MORE_DATA;
                }
            }
            if (type == REG_MULTI_SZ && (units < 2 || data[units - 2] != 0))
            {
                if (*lpcbData + sizeof(wchar_t) <= dataBufSize)
                {
                    data[units++] = 0;
                    *lpcbData += (DWORD)sizeof(wchar_t);
                }
                else // not enough room for the second null terminator in the buffer
                {
                    *lpcbData += (DWORD)sizeof(wchar_t); // request the necessary null terminator
                    return ERROR_MORE_DATA;
                }
            }
        }
    }
    return ret;
}

//******************************************************************************
//
// SalGetProcessId
//
// Works under W2K as well (SDK7.1 pretends that GetProcessId is available under W2K,
// but that is a mistake, and SDK8 already requires _WIN32_WINNT >= 0x0501 for GetProcessId
//
// As long as we want to support Windows 2000, we cannot statically link either GetProcessId() or ZwQueryInformationProcess.
//

DWORD SalGetProcessId(HANDLE hProcess)
{
    // called at the start of the process, so we cannot rely on global variables that are set later,
    // such as WindowsVistaAndLater or NtDLL
    static BOOL osDetected = FALSE;
    static BOOL vistaAndLater = FALSE;
    if (!osDetected)
    {
        vistaAndLater = SalIsWindowsVersionOrGreater(6, 0, 0);
        osDetected = TRUE;
    }
    DWORD ret = 0xffffffff;
    if (vistaAndLater)
    {
        // the function already existed in XPSP1, but there ZwQueryInformationProcess still worked 100%, see below
        typedef DWORD(WINAPI * PGetProcessId)(IN HANDLE Process);
        HINSTANCE hDLL = NOHANDLES(LoadLibraryA("kernel32.dll"));
        if (hDLL != NULL)
        {
            PGetProcessId pGetProcessId = (PGetProcessId)GetProcAddress(hDLL, "GetProcessId");
            if (pGetProcessId != NULL)
                ret = pGetProcessId(hProcess);
            else
                TRACE_E("SalGetProcessId() failed while calling GetProcessId()");
            NOHANDLES(FreeLibrary(hDLL));
        }
    }
    else
    {
// for W2K and XP use the "undocumented" API http://msdn.microsoft.com/en-us/library/windows/desktop/ms687420%28v=vs.85%29.aspx
// which Microsoft threatens to discontinue eventually
#if !defined PROCESSINFOCLASS
        typedef LONG PROCESSINFOCLASS;
#endif
#if !defined PPEB
        typedef struct _PEB* PPEB;
#endif
#if !defined PROCESS_BASIC_INFORMATION
        typedef struct _PROCESS_BASIC_INFORMATION
        {
            PVOID Reserved1;
            PPEB PebBaseAddress;
            PVOID Reserved2[2];
            ULONG_PTR UniqueProcessId;
            PVOID Reserved3;
        } PROCESS_BASIC_INFORMATION;
#endif
        typedef NTSTATUS(WINAPI * PFN_ZWQUERYINFORMATIONPROCESS)(HANDLE, PROCESSINFOCLASS, PVOID, ULONG, PULONG);

        HINSTANCE hDLL = NOHANDLES(LoadLibraryA("ntdll.dll"));
        if (hDLL != NULL)
        {
            PFN_ZWQUERYINFORMATIONPROCESS fnProcInfo = PFN_ZWQUERYINFORMATIONPROCESS(GetProcAddress(hDLL, "ZwQueryInformationProcess"));
            if (fnProcInfo != NULL)
            {
                PROCESS_BASIC_INFORMATION pbi;
                ZeroMemory(&pbi, sizeof(PROCESS_BASIC_INFORMATION));
                if (fnProcInfo(hProcess, 0, &pbi, sizeof(PROCESS_BASIC_INFORMATION), NULL) == 0) // STATUS_SUCCESS
                    ret = (DWORD)pbi.UniqueProcessId;
                else
                    TRACE_E("SalGetProcessId() failed while calling ZwQueryInformationProcess()");
            }
            else
                TRACE_E("SalGetProcessId() failed to get ZwQueryInformationProcess() proc address");
            NOHANDLES(FreeLibrary(hDLL));
        }
        else
            TRACE_E("SalGetProcessId() failed to load ntdll.dll");
    }
    return ret;
}
