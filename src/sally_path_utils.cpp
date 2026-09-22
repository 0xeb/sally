// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"

#include "mainwnd.h"
#include "usermenu.h"
#include "plugins.h"
#include "fileswnd.h"
#include "cfgdlg.h"
#include "dialogs.h"
#include "pack.h"
#include "execute.h"
#include "shellib.h"
#include "menu.h"
#include "common/widepath.h"
#include "common/fsutil.h"
#include "common/WorkdirsHistorySerializer.h"

#include <vector>
#include "ui/IPrompter.h"
#include "common/IFileSystem.h"
#include "common/IEnvironment.h"
#include "common/DiagnosticTextEncoding.h"
#include "common/unicode/helpers.h"
#include "common/unicode/PanelPathPolicy.h"

CUserMenuIconBkgndReader UserMenuIconBkgndReader;

// ****************************************************************************

// 2026-08-25: the narrow SalPathRemoveBackslash/SalPathStripPath/
// SalPathRemoveExtension/SalPathAddExtension/SalPathRenameExtension(char*, ...) were deleted -
// confirmed-dead (zero callers anywhere; the legacy v107 ABI shim forwards each to the
// corresponding WideGeneral.SalPath...W method, never to these free functions). Their wide
// siblings in common/SalPathWide.h/.cpp are the sole surviving implementations.

// Wide version - creates temp file/directory and returns its path
// Returns empty string on failure (sets LastError)
std::wstring SalGetTempFileNameW(const wchar_t* path, const wchar_t* prefix, bool file)
{
    std::wstring tmpDir;

    if (path == nullptr)
    {
        auto tempResult = gEnvironment->GetTempPath(tmpDir);
        if (!tempResult.success)
        {
            TRACE_E("Unable to get TEMP directory.");
            SetLastError(tempResult.errorCode);
            return L"";
        }

        DWORD attrs = gFileSystem->GetFileAttributes(tmpDir.c_str());
        if (attrs == INVALID_FILE_ATTRIBUTES)
        {
            gPrompter->ShowError(LoadStrW(IDS_ERRORTITLE), LoadStrW(IDS_TMPDIRERROR));
            auto sysResult = gEnvironment->GetSystemDirectory(tmpDir);
            if (!sysResult.success)
            {
                TRACE_E("Unable to get system directory.");
                SetLastError(sysResult.errorCode);
                return L"";
            }
        }
    }
    else
    {
        tmpDir = path;
    }

    // Ensure trailing backslash
    if (!tmpDir.empty() && tmpDir.back() != L'\\')
        tmpDir += L'\\';
    
    // Append prefix
    if (prefix != nullptr)
        tmpDir += prefix;
    
    size_t baseLen = tmpDir.length();
    
    // Generate unique name with random suffix
    DWORD randNum = (GetTickCount() & 0xFFF);
    wchar_t suffix[16];
    
    while (true)
    {
        swprintf_s(suffix, L"%X.tmp", randNum++);
        tmpDir.resize(baseLen);
        tmpDir += suffix;
        
        if (file)
        {
            HANDLE h = gFileSystem->CreateFile(tmpDir.c_str(), GENERIC_WRITE, 0, NULL, CREATE_NEW,
                                               FILE_ATTRIBUTE_NORMAL, NULL);
            HANDLES_ADD_EX(__otQuiet, h != INVALID_HANDLE_VALUE, __htFile, __hoCreateFile, h, GetLastError(), TRUE);
            if (h != INVALID_HANDLE_VALUE)
            {
                HANDLES_REMOVE(h, __htFile, "IFileSystem::CloseHandle");
                gFileSystem->CloseFileHandle(h);
                return tmpDir;
            }
        }
        else
        {
            const FileResult result = gFileSystem->CreateDirectory(tmpDir.c_str());
            if (result.success)
            {
                return tmpDir;
            }
            SetLastError(result.errorCode);
        }
        
        DWORD err = GetLastError();
        if (err != ERROR_FILE_EXISTS && err != ERROR_ALREADY_EXISTS)
        {
            TRACE_EW(L"Unable to create temporary " << (file ? L"file" : L"directory") << L": " << GetErrorTextOwned(err).c_str());
            SetLastError(err);
            return L"";
        }
    }
}

// 2026-08-25: the narrow SalGetTempFileName(char*, ...) thin adapter was deleted -
// confirmed-dead (zero callers; the legacy v107 ABI shim forwards to
// WideGeneral.SalGetTempFileName, never to this free function).

// ****************************************************************************

int HandleFileException(EXCEPTION_POINTERS* e, char* fileMem, DWORD fileMemSize)
{
    if (e->ExceptionRecord->ExceptionCode == EXCEPTION_IN_PAGE_ERROR) // in-page-error definitely means a file error
    {
        return EXCEPTION_EXECUTE_HANDLER; // execute __except block
    }
    else
    {
        if (e->ExceptionRecord->ExceptionCode == EXCEPTION_ACCESS_VIOLATION &&    // access violation means a file error only if the error address corresponds to the file
            (e->ExceptionRecord->NumberParameters >= 2 &&                         // we have something to test
             e->ExceptionRecord->ExceptionInformation[1] >= (ULONG_PTR)fileMem && // error ptr in file view
             e->ExceptionRecord->ExceptionInformation[1] < ((ULONG_PTR)fileMem) + fileMemSize))
        {
            return EXCEPTION_EXECUTE_HANDLER; // execute __except block
        }
        else
        {
            return EXCEPTION_CONTINUE_SEARCH; // throw exception further ... to call-stack
        }
    }
}

// ****************************************************************************

// SalRemovePointsFromPath + SalGetFullNameW moved to common/SalGetFullName.cpp (shared with private tests).
// Production implementation of the SalGetFullName.h host seam.
const wchar_t* SalGetDefaultDirForDrive(wchar_t lowerDriveLetter)
{
    return DefaultDir[lowerDriveLetter - L'a'].c_str();
}

// SalGetFullNameW moved to common/SalGetFullName.cpp.

// ****************************************************************************

TDirectArray<HANDLE> AuxThreads(10, 5);

void AuxThreadBody(BOOL add, HANDLE thread, BOOL testIfFinished)
{
    // Prevent re-entrance
    static CCriticalSection cs;
    CEnterCriticalSection enterCS(cs);

    static BOOL finished = FALSE;
    if (!finished) // after calling TerminateAuxThreads() we no longer accept anything
    {
        if (add)
        {
            // clean array from threads that have already finished
            for (int i = 0; i < AuxThreads.Count; i++)
            {
                DWORD code;
                if (!GetExitCodeThread(AuxThreads[i], &code) || code != STILL_ACTIVE)
                { // thread has already finished
                    HANDLES(CloseHandle(AuxThreads[i]));
                    AuxThreads.Delete(i);
                    i--;
                }
            }
            BOOL skipAdd = FALSE;
            if (testIfFinished)
            {
                DWORD code;
                if (!GetExitCodeThread(thread, &code) || code != STILL_ACTIVE)
                { // thread has already finished
                    HANDLES(CloseHandle(thread));
                    skipAdd = TRUE;
                }
            }
            // add new thread
            if (!skipAdd)
                AuxThreads.Add(thread);
        }
        else
        {
            finished = TRUE;
            for (int i = 0; i < AuxThreads.Count; i++)
            {
                HANDLE t = AuxThreads[i];
                DWORD code;
                if (GetExitCodeThread(t, &code) && code == STILL_ACTIVE)
                { // thread still running, terminating it
                    TerminateThread(t, 666);
                    WaitForSingleObject(t, INFINITE); // wait until thread actually finishes, sometimes it takes a while
                }
                HANDLES(CloseHandle(t));
            }
            AuxThreads.DestroyMembers();
        }
    }
    else
        TRACE_E("AuxThreadBody(): calling after TerminateAuxThreads() is not supported! add=" << add);
}

void AddAuxThread(HANDLE thread, BOOL testIfFinished)
{
    AuxThreadBody(TRUE, thread, testIfFinished);
}

void TerminateAuxThreads()
{
    AuxThreadBody(FALSE, NULL, FALSE);
}

// ****************************************************************************

/*
#define STOPREFRESHSTACKSIZE 50

class CStopRefreshStack
{
  protected:
    DWORD CallerCalledFromArr[STOPREFRESHSTACKSIZE];  // array of return addresses of functions from where BeginStopRefresh() was called
    DWORD CalledFromArr[STOPREFRESHSTACKSIZE];        // array of addresses from where BeginStopRefresh() was called
    int Count;                                        // number of elements in the previous two arrays
    int Ignored;                                      // number of BeginStopRefresh() calls that had to be ignored (STOPREFRESHSTACKSIZE too small -> increase if needed)

  public:
    CStopRefreshStack() {Count = 0; Ignored = 0;}
    ~CStopRefreshStack() {CheckIfEmpty(3);} // three BeginStopRefresh() are OK: BeginStopRefresh() is called for both panels and third is called from WM_USER_CLOSE_MAINWND (which is called first)

    void Push(DWORD caller_called_from, DWORD called_from);
    void Pop(DWORD caller_called_from, DWORD called_from);
    void CheckIfEmpty(int checkLevel);
};

void
CStopRefreshStack::Push(DWORD caller_called_from, DWORD called_from)
{
  if (Count < STOPREFRESHSTACKSIZE)
  {
    CallerCalledFromArr[Count] = caller_called_from;
    CalledFromArr[Count] = called_from;
    Count++;
  }
  else
  {
    Ignored++;
    TRACE_E("CStopRefreshStack::Push(): you should increase STOPREFRESHSTACKSIZE! ignored=" << Ignored);
  }
}

void
CStopRefreshStack::Pop(DWORD caller_called_from, DWORD called_from)
{
  if (Ignored == 0)
  {
    if (Count > 0)
    {
      Count--;
      if (CallerCalledFromArr[Count] != caller_called_from)
      {
        TRACE_E("CStopRefreshStack::Pop(): strange situation: BeginCallerCalledFrom!=StopCallerCalledFrom - BeginCalledFrom,StopCalledFrom");
        TRACE_E("CStopRefreshStack::Pop(): strange situation: 0x" << std::hex <<
                CallerCalledFromArr[Count] << "!=0x" << caller_called_from << " - 0x" <<
                CalledFromArr[Count] << ",0x" << called_from << std::dec);
      }
    }
    else TRACE_E("CStopRefreshStack::Pop(): unexpected call!");
  }
  else Ignored--;
}

void
CStopRefreshStack::CheckIfEmpty(int checkLevel)
{
  if (Count > checkLevel)
  {
    TRACE_E("CStopRefreshStack::CheckIfEmpty(" << checkLevel << "): listing remaining BeginStopRefresh calls: CallerCalledFrom,CalledFrom");
    int i;
    for (i = 0; i < Count; i++)
    {
      TRACE_E("CStopRefreshStack::CheckIfEmpty():: 0x" << std::hex <<
              CallerCalledFromArr[i] << ",0x" << CalledFromArr[i] << std::dec);
    }
  }
}

CStopRefreshStack StopRefreshStack;
*/

void BeginStopRefresh(BOOL debugSkipOneCaller, BOOL debugDoNotTestCaller)
{
    /*
#ifdef _DEBUG     // test if BeginStopRefresh() and EndStopRefresh() are called from the same function (based on return address of calling function -> so it won't recognize "error" when called from different functions that are both called from the same function)
  DWORD *register_ebp;
  __asm mov register_ebp, ebp
  DWORD called_from, caller_called_from;
  __try
  {
    called_from = *(DWORD*)((char*)register_ebp + 4);

if this code ever needs to be revived, note that it can be replaced (x86 and x64):
    called_from = *(DWORD_PTR *)_AddressOfReturnAddress();

    if (debugSkipOneCaller) caller_called_from = *(DWORD*)((char*)(*(DWORD *)(*register_ebp)) + 4);
    else caller_called_from = *(DWORD*)((char*)(*register_ebp) + 4);
  }
  __except (EXCEPTION_EXECUTE_HANDLER)
  {
    called_from = -1;
    caller_called_from = -1;
  }
  StopRefreshStack.Push(debugDoNotTestCaller ? 0 : caller_called_from, called_from);
#endif // _DEBUG
*/

    //  if (StopRefresh == 0) TRACE_I("Begin stop refresh mode");
    StopRefresh++;
}

void EndStopRefresh(BOOL postRefresh, BOOL debugSkipOneCaller, BOOL debugDoNotTestCaller)
{
    /*
#ifdef _DEBUG     // test if BeginStopRefresh() and EndStopRefresh() are called from the same function (based on return address of calling function -> so it won't recognize "error" when called from different functions that are both called from the same function)
  DWORD *register_ebp;
  __asm mov register_ebp, ebp
  DWORD called_from, caller_called_from;
  __try
  {
    called_from = *(DWORD*)((char*)register_ebp + 4);

if this code ever needs to be revived, note that it can be replaced (x86 and x64):
    called_from = *(DWORD_PTR *)_AddressOfReturnAddress();

    if (debugSkipOneCaller) caller_called_from = *(DWORD*)((char*)(*(DWORD *)(*register_ebp)) + 4);
    else caller_called_from = *(DWORD*)((char*)(*register_ebp) + 4);
  }
  __except (EXCEPTION_EXECUTE_HANDLER)
  {
    called_from = -1;
    caller_called_from = -1;
  }
  StopRefreshStack.Pop(debugDoNotTestCaller ? 0 : caller_called_from, called_from);
#endif // _DEBUG
*/

    if (StopRefresh < 1)
    {
        TRACE_E("Incorrect call to EndStopRefresh().");
        StopRefresh = 0;
    }
    else
    {
        if (--StopRefresh == 0)
        {
            //      TRACE_I("End stop refresh mode");
            // if we blocked any refresh, give it a chance to run
            if (postRefresh && MainWindow != NULL)
            {
                if (MainWindow->LeftPanel != NULL)
                {
                    PostMessage(MainWindow->LeftPanel->HWindow, WM_USER_SM_END_NOTIFY, 0, 0);
                }
                if (MainWindow->RightPanel != NULL)
                {
                    PostMessage(MainWindow->RightPanel->HWindow, WM_USER_SM_END_NOTIFY, 0, 0);
                }
            }

            if (MainWindow != NULL && MainWindow->NeedToResentDispachChangeNotif &&
                !AlreadyInPlugin) // if still in plugin, sending message makes no sense
            {
                MainWindow->NeedToResentDispachChangeNotif = FALSE;

                // post request to dispatch change notification messages on paths
                HANDLES(EnterCriticalSection(&TimeCounterSection));
                int t1 = MyTimeCounter++;
                HANDLES(LeaveCriticalSection(&TimeCounterSection));
                PostMessage(MainWindow->HWindow, WM_USER_DISPACHCHANGENOTIF, 0, t1);
            }
        }
    }
}

// ****************************************************************************

void BeginStopIconRepaint()
{
    StopIconRepaint++;
}

void EndStopIconRepaint(BOOL postRepaint)
{
    if (StopIconRepaint > 0)
    {
        if (--StopIconRepaint == 0 && PostAllIconsRepaint)
        {
            if (postRepaint && MainWindow != NULL)
            {
                PostMessage(MainWindow->HWindow, WM_USER_REPAINTALLICONS, 0, 0);
            }
            PostAllIconsRepaint = FALSE;
        }
    }
    else
    {
        TRACE_E("Incorrect call to EndStopIconRepaint().");
        StopIconRepaint = 0;
    }
}

// ****************************************************************************

void BeginStopStatusbarRepaint()
{
    StopStatusbarRepaint++;
}

void EndStopStatusbarRepaint()
{
    if (StopStatusbarRepaint > 0)
    {
        if (--StopStatusbarRepaint == 0 && PostStatusbarRepaint)
        {
            PostStatusbarRepaint = FALSE;
            PostMessage(MainWindow->HWindow, WM_USER_REPAINTSTATUSBARS, 0, 0);
        }
    }
    else
    {
        TRACE_E("Incorrect call to EndStopStatusbarRepaint().");
        StopStatusbarRepaint = 0;
    }
}

// ****************************************************************************

BOOL CanChangeDirectory()
{
    if (ChangeDirectoryAllowed == 0)
        return TRUE;
    else
    {
        ChangeDirectoryRequest = TRUE;
        return FALSE;
    }
}

// ****************************************************************************

void AllowChangeDirectory(BOOL allow)
{
    if (allow)
    {
        if (ChangeDirectoryAllowed == 0)
        {
            TRACE_E("Incorrect call to AllowChangeDirectory().");
            return;
        }
        if (--ChangeDirectoryAllowed == 0)
        {
            if (ChangeDirectoryRequest)
                SetCurrentDirectoryToSystem();
            ChangeDirectoryRequest = FALSE;
        }
    }
    else
        ChangeDirectoryAllowed++;
}

// ****************************************************************************

void SetCurrentDirectoryToSystem()
{
    std::wstring sysDir;
    if (gEnvironment->GetSystemDirectory(sysDir).success)
        gEnvironment->SetCurrentDirectory(sysDir.c_str());
}

// ****************************************************************************

// Wide-native. The narrow version this replaces took the WIDE cFileName from
// SalFindFirstFileHW and pushed it through WideCharToMultiByte(CP_ACP, ...) only to widen it again
// two lines later - a round trip that could not succeed and had two distinct failure modes:
//
//   * A name the active code page cannot spell became '?', so DeleteFile ran on a path that does
//     not exist. The file survived, so RemoveDirectory on the parent then failed too, and the
//     temporary directory LEAKED PERMANENTLY - every extraction or view of an archive holding a
//     non-ANSI name left one behind.
//   * That conversion had no WC_NO_BEST_FIT_CHARS, so best-fit mapping could produce the name of a
//     DIFFERENT REAL FILE - and this function's next move is to delete what it just named.
//
// Nothing is narrowed here now: the find result is used exactly as Windows returned it.
void _RemoveTemporaryDirW(const wchar_t* dir)
{
    std::wstring path(dir);
    if (!path.empty() && path.back() != L'\\')
        path += L'\\';
    const size_t baseLen = path.length();

    WIN32_FIND_DATAW file;
    HANDLE find = SalFindFirstFileHW((path + L'*').c_str(), &file);
    if (find != INVALID_HANDLE_VALUE)
    {
        do
        {
            if (file.cFileName[0] == 0 || wcscmp(file.cFileName, L".") == 0 ||
                wcscmp(file.cFileName, L"..") == 0)
            {
                continue;
            }

            path.resize(baseLen);
            path += file.cFileName;

            ClearReadOnlyAttr(path.c_str(), file.dwFileAttributes);
            if (file.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
                _RemoveTemporaryDirW(path.c_str());
            else
                gFileSystem->DeleteFile(path.c_str());
        } while (SalLPFindNextFile(find, &file));
        SalLPFindClose(find);
    }

    path.resize(baseLen > 0 ? baseLen - 1 : 0); // drop the trailing separator
    SalLPRemoveDirectory(path.c_str());
}

void RemoveTemporaryDirW(const wchar_t* dir)
{
    CALL_STACK_MESSAGE1("RemoveTemporaryDirW()");
    if (dir == NULL || *dir == 0)
        return;

    gEnvironment->SetCurrentDirectory(dir); // so it deletes better (system likes cur-dir)
    _RemoveTemporaryDirW(dir);
    SetCurrentDirectoryToSystem(); // must leave it, otherwise it won't be deletable

    ClearReadOnlyAttr(dir);
    SalLPRemoveDirectory(dir);
}

// 2026-08-25: the narrow RemoveTemporaryDir(char*) thin adapter was deleted -
// confirmed-dead (zero callers; the legacy v107 ABI shim forwards to
// WideGeneral.RemoveTemporaryDir, never to this free function).

// ****************************************************************************

// Wide-native, same shape and same reason as _RemoveTemporaryDirW above: the old
// narrow form pushed the WIDE cFileName through WideCharToMultiByte(CP_ACP, WC_NO_BEST_FIT_CHARS,
// ...) just to build the recursion path, so a subdirectory name the active code page cannot spell
// became '?' (silently skipped, left behind) or, without the NO_BEST_FIT flag anywhere else in this
// family, could alias a different real directory. Nothing is narrowed here now.
void _RemoveEmptyDirsW(const wchar_t* dir)
{
    std::wstring path(dir);
    if (!path.empty() && path.back() != L'\\')
        path += L'\\';
    const size_t baseLen = path.length();

    WIN32_FIND_DATAW file;
    HANDLE find = SalFindFirstFileHW((path + L'*').c_str(), &file);
    if (find != INVALID_HANDLE_VALUE)
    {
        do
        {
            if (file.cFileName[0] == 0 || wcscmp(file.cFileName, L".") == 0 ||
                wcscmp(file.cFileName, L"..") == 0)
            {
                continue;
            }

            if (file.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            {
                path.resize(baseLen);
                path += file.cFileName;
                ClearReadOnlyAttr(path.c_str(), file.dwFileAttributes);
                _RemoveEmptyDirsW(path.c_str());
            }
        } while (SalLPFindNextFile(find, &file));
        SalLPFindClose(find);
    }

    path.resize(baseLen > 0 ? baseLen - 1 : 0); // drop the trailing separator
    SalLPRemoveDirectory(path.c_str());
}

void RemoveEmptyDirsW(const wchar_t* dir)
{
    CALL_STACK_MESSAGE1("RemoveEmptyDirsW()");
    if (dir == NULL || *dir == 0)
        return;

    gEnvironment->SetCurrentDirectory(dir); // so it deletes better (system likes cur-dir)
    _RemoveEmptyDirsW(dir);
    SetCurrentDirectoryToSystem(); // must leave it, otherwise it won't be deletable

    ClearReadOnlyAttr(dir);
    SalLPRemoveDirectory(dir);
}

// 2026-08-25: the narrow RemoveEmptyDirs(char*) thin adapter was deleted -
// confirmed-dead (zero callers anywhere - unlike its neighbors this one was never part of the
// plugin SDK/ABI at all). RemoveEmptyDirsW above is the sole surviving implementation.

// ****************************************************************************

BOOL CheckAndCreateDirectoryOwnedW(const wchar_t* dir, HWND parent, BOOL quiet,
                                   std::wstring* errorText, std::wstring* firstCreatedDir,
                                   BOOL noRetryButton, BOOL manualCrDir)
{
    CALL_STACK_MESSAGE1("CheckAndCreateDirectoryOwnedW()");
AGAIN:
    if (parent == NULL)
        parent = MainWindow->HWindow;
    if (errorText != NULL)
        errorText->clear();
    if (firstCreatedDir != NULL)
        firstCreatedDir->clear();
    int dirLen = (int)wcslen(dir);
    DWORD attrs = gFileSystem->GetFileAttributes(dir);
    std::wstring buf; // for error messages
    std::wstring name;
    if (attrs == 0xFFFFFFFF) // probably doesn't exist, allow creating it
    {
        const std::wstring root = GetRootPath(dir);
        if (dirLen <= (int)root.length()) // dir is root directory
        {
            buf = FormatStrW(LoadStrW(IDS_CREATEDIRFAILED), dir);
            if (errorText != NULL)
                *errorText = buf;
            else
                gPrompter->ShowError(LoadStrW(IDS_ERRORTITLE), buf.c_str());
            return FALSE;
        }
        int msgBoxRet = IDCANCEL;
        if (!quiet)
        {
            // if user hasn't suppressed it, show info about directory non-existence
            if (Configuration.CnfrmCreateDir)
            {
                std::wstring msg = FormatStrW(LoadStrW(IDS_CREATEDIRECTORY), dir);
                bool dontShow = !Configuration.CnfrmCreateDir;
                PromptResult res = gPrompter->ConfirmWithCheckbox(LoadStrW(IDS_QUESTION), msg.c_str(),
                                                                  LoadStrW(IDS_DONTSHOWAGAINCD), &dontShow);
                msgBoxRet = (res.type == PromptResult::kOk) ? IDOK : IDCANCEL;
                Configuration.CnfrmCreateDir = !dontShow;
            }
            else
                msgBoxRet = IDOK;
        }
        if (quiet || msgBoxRet == IDOK)
        {
            name = dir;
            while (1) // find first existing directory
            {
                const size_t slash = name.rfind(L'\\');
                if (slash == std::wstring::npos)
                {
                    buf = FormatStrW(LoadStrW(IDS_CREATEDIRFAILED), dir);
                    if (errorText != NULL)
                        *errorText = buf;
                    else
                        gPrompter->ShowError(LoadStrW(IDS_ERRORTITLE), buf.c_str());
                    return FALSE;
                }
                if (slash > root.length())
                    name.resize(slash);
                else
                {
                    name = root;
                    break; // we're already at root directory
                }
                attrs = gFileSystem->GetFileAttributes(name.c_str());
                if (attrs != 0xFFFFFFFF) // name exists
                {
                    if (attrs & FILE_ATTRIBUTE_DIRECTORY)
                        break; // we'll build from this directory
                    else       // it's a file, that wouldn't work ...
                    {
                        buf = FormatStrW(LoadStrW(IDS_NAMEUSEDFORFILE), name.c_str());
                        if (errorText != NULL)
                            *errorText = buf;
                        else
                        {
                            // CFileErrorDlg keeps caption/file/error as raw const wchar_t*,
                            // so the GetErrorTextOwned() temporary died at the end of the
                            // constructor statement and Execute() read freed memory.
                            const std::wstring errTextW = GetErrorTextOwned(ERROR_ALREADY_EXISTS);
                            if (noRetryButton)
                            {
                                CFileErrorDlg dlg(parent, LoadStrW(IDS_ERRORCREATINGDIR), dir, errTextW.c_str(), FALSE, IDD_ERROR3);
                                dlg.Execute();
                            }
                            else
                            {
                                CFileErrorDlg dlg(parent, LoadStrW(IDS_ERRORCREATINGDIR), dir, errTextW.c_str(), TRUE, 0);
                                if (dlg.Execute() == IDRETRY)
                                    goto AGAIN;
                                // SalMessageBoxW(parent, buf, LoadStrW(IDS_ERRORTITLE), MB_OK | MB_ICONEXCLAMATION);
                            }
                        }
                        return FALSE;
                    }
                }
            }
            SalPathAddBackslashW(name);
            const wchar_t* st = dir + name.length();
            if (*st == L'\\')
                st++;
            BOOL first = TRUE;
            while (*st != 0)
            {
                BOOL invalidName = manualCrDir && *st <= L' '; // spaces at the beginning of created directory name are undesirable only during manual creation (Windows can handle it, but it's potentially dangerous)
                const wchar_t* slash = wcschr(st, L'\\');
                if (slash == NULL)
                    slash = st + wcslen(st);
                name.append(st, (size_t)(slash - st));
                if (name.back() <= L' ' || name.back() == L'.')
                    invalidName = TRUE; // spaces and dots at the end of created directory name are undesirable
            AGAIN2:
                if (invalidName || !SalLPCreateDirectory(name.c_str(), NULL))
                {
                    DWORD lastErr = invalidName ? ERROR_INVALID_NAME : GetLastError();
                    // ERROR_ALREADY_EXISTS is not a failure - the directory is there, which is what we want
                    if (lastErr != ERROR_ALREADY_EXISTS)
                    {
                        buf = FormatStrW(LoadStrW(IDS_CREATEDIRFAILED), name.c_str());
                        if (errorText != NULL)
                            *errorText = buf;
                        else
                        {
                            // See above: CFileErrorDlg does not own its error text.
                            const std::wstring errTextW = GetErrorTextOwned(lastErr);
                            if (noRetryButton)
                            {
                                CFileErrorDlg dlg(parent, LoadStrW(IDS_ERRORCREATINGDIR), dir, errTextW.c_str(), FALSE, IDD_ERROR3);
                                dlg.Execute();
                            }
                            else
                            {
                                CFileErrorDlg dlg(parent, LoadStrW(IDS_ERRORCREATINGDIR), dir, errTextW.c_str(), TRUE, 0);
                                if (dlg.Execute() == IDRETRY)
                                    goto AGAIN2;
                                //              SalMessageBoxW(parent, buf, LoadStrW(IDS_ERRORTITLE), MB_OK | MB_ICONEXCLAMATION);
                            }
                        }
                        return FALSE;
                    }
                }
                else
                {
                    if (first && firstCreatedDir != NULL)
                        *firstCreatedDir = name;
                    first = FALSE;
                }
                name.push_back(L'\\');
                if (*slash == L'\\')
                    slash++;
                st = slash;
            }
            return TRUE;
        }
        return FALSE;
    }
    if (attrs & FILE_ATTRIBUTE_DIRECTORY)
        return TRUE;
    else // file, that wouldn't work ...
    {
        buf = FormatStrW(LoadStrW(IDS_NAMEUSEDFORFILE), dir);
        if (errorText != NULL)
            *errorText = buf;
        else
        {
            // See above: CFileErrorDlg does not own its error text.
            const std::wstring errTextW = GetErrorTextOwned(ERROR_ALREADY_EXISTS);
            if (noRetryButton)
            {
                CFileErrorDlg dlg(parent, LoadStrW(IDS_ERRORCREATINGDIR), dir, errTextW.c_str(), FALSE, IDD_ERROR3);
                dlg.Execute();
            }
            else
            {
                CFileErrorDlg dlg(parent, LoadStrW(IDS_ERRORCREATINGDIR), dir, errTextW.c_str(), TRUE, 0);
                if (dlg.Execute() == IDRETRY)
                    goto AGAIN;
                //        SalMessageBoxW(parent, buf, LoadStrW(IDS_ERRORTITLE), MB_OK | MB_ICONEXCLAMATION);
            }
        }
        return FALSE;
    }
}

BOOL CheckAndCreateDirectoryW(const wchar_t* dir, HWND parent, BOOL quiet, wchar_t* errBuf,
                              int errBufSize, wchar_t* newDir, int newDirSize, BOOL noRetryButton,
                              BOOL manualCrDir)
{
    std::wstring errorText;
    std::wstring firstCreatedDir;
    const BOOL result = CheckAndCreateDirectoryOwnedW(dir, parent, quiet,
                                                       errBuf != NULL ? &errorText : NULL,
                                                       newDir != NULL ? &firstCreatedDir : NULL,
                                                       noRetryButton, manualCrDir);
    if (errBuf != NULL && errBufSize > 0)
        wcsncpy_s(errBuf, errBufSize, errorText.c_str(), _TRUNCATE);
    if (newDir != NULL && newDirSize > 0)
        wcsncpy_s(newDir, newDirSize, firstCreatedDir.c_str(), _TRUNCATE);
    return result;
}

// Fixed caller-buffer projection for the frozen plugin interface. Core callers use the owned form.
// 2026-08-25: the narrow CheckAndCreateDirectory(char*, ...) thin adapter was
// deleted - confirmed-dead (zero callers; the legacy v107 ABI shim forwards to
// WideGeneral.CheckAndCreateDirectory, never to this free function).

//
// ****************************************************************************
// CToolTipWindow
//

LRESULT
CToolTipWindow::WindowProc(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    if (uMsg == TTM_WINDOWFROMPOINT)
        return (LRESULT)ToolWindow;
    return CWindow::WindowProc(uMsg, wParam, lParam);
}

//
// ****************************************************************************
// CPathHistoryItem
//

CPathHistoryItem::CPathHistoryItem(int type, const wchar_t* pathOrArchiveOrFSName,
                                   const wchar_t* archivePathOrFSUserPart, HICON hIcon,
                                   CPluginFSInterfaceAbstract* pluginFS)
{
    Type = type;
    HIcon = hIcon;
    PluginFS = NULL;

    TopIndex = -1;

    if (Type == 0) // disk
    {
        // Drop the trailing backslash unless the path is exactly a root. This ran twice
        // before - once for the ANSI mirror and once for its wide twin - with a comment
        // asking the second copy to stay in step with the first.
        std::wstring root = GetRootPath(pathOrArchiveOrFSName);
        const wchar_t* e = pathOrArchiveOrFSName + wcslen(pathOrArchiveOrFSName);
        if (root.size() < (size_t)(e - pathOrArchiveOrFSName) || // it is not a root path
            pathOrArchiveOrFSName[0] == L'\\')                    // it is a UNC path
        {
            if (e > pathOrArchiveOrFSName && *(e - 1) == L'\\')
                --e;
            PathOrArchiveOrFSName.assign(pathOrArchiveOrFSName, e - pathOrArchiveOrFSName);
        }
        else // it is a normal root path (c:\\)
        {
            PathOrArchiveOrFSName = std::move(root);
        }
    }
    else
    {
        if (Type == 1 || Type == 2) // archive or FS (just copy of both strings)
        {
            if (Type == 2)
                PluginFS = pluginFS;
            PathOrArchiveOrFSName = pathOrArchiveOrFSName;
            ArchivePathOrFSUserPart = archivePathOrFSUserPart;
        }
        else
            TRACE_E("CPathHistoryItem::CPathHistoryItem(): unknown 'type'");
    }
}

CPathHistoryItem::~CPathHistoryItem()
{
    if (HIcon != NULL)
        HANDLES(DestroyIcon(HIcon));
}

void CPathHistoryItem::ChangeData(int topIndex, const wchar_t* focusedName)
{
    TopIndex = topIndex;
    // The wide confirmation that used to sit beside this compare guarded against two
    // different names sharing one CP_ACP mirror. FocusedName is wide, so the compare is
    // exact. It stays case-SENSITIVE: the old primary compare was strcmp, and the
    // case-insensitive twin was ANDed onto it, so strcmp always decided the outcome.
    if (!FocusedName.empty() && focusedName != NULL &&
        wcscmp(FocusedName.c_str(), focusedName) == 0)
        return; // no change -> end
    if (focusedName != NULL)
        FocusedName = focusedName;
    else
        FocusedName.clear();
}

std::wstring CPathHistoryItem::GetPath() const
{
    if (PathOrArchiveOrFSName.empty())
        return {};

    std::wstring path = PathOrArchiveOrFSName;
    if ((Type == 1 || Type == 2) && (!ArchivePathOrFSUserPart.empty() || Type == 2))
    {
        path.push_back(Type == 1 ? L'\\' : L':');
        path += ArchivePathOrFSUserPart;
    }

    // Menu labels use '&' as an accelerator marker, so preserve literal ampersands.
    for (size_t i = 0; i < path.size(); i++)
    {
        if (path[i] == L'&')
        {
            path.insert(i, 1, L'&');
            i++;
        }
    }
    return path;
}

HICON
CPathHistoryItem::GetIcon()
{
    return HIcon;
}

BOOL DuplicateAmpersands(wchar_t* buffer, int bufferSize, BOOL skipFirstAmpersand)
{
    if (buffer == NULL)
    {
        TRACE_E("Unexpected situation (1) in DuplicateAmpersands()");
        return FALSE;
    }
    wchar_t* s = buffer;
    // bufferSize counts CHARACTERS, matching wcslen and every call site.
    int l = (int)wcslen(buffer);
    if (l >= bufferSize)
    {
        TRACE_E("Unexpected situation (2) in DuplicateAmpersands()");
        return FALSE;
    }
    BOOL ret = TRUE;
    BOOL first = TRUE;
    while (*s != 0)
    {
        if (*s == L'&')
        {
            if (!(skipFirstAmpersand && first))
            {
                if (l + 1 < bufferSize)
                {
                    // CHARACTER counts on both sides; memmove wants BYTES.
                    memmove(s + 1, s, (size_t)(l - (s - buffer) + 1) * sizeof(wchar_t)); // double '&'
                    l++;
                    s++;
                }
                else // doesn't fit, trim buffer
                {
                    ret = FALSE;
                    memmove(s + 1, s, (size_t)(l - (s - buffer)) * sizeof(wchar_t)); // double '&', trim by one character
                    buffer[l] = 0;
                    s++;
                }
            }
            first = FALSE;
        }
        s++;
    }
    return ret;
}

void RemoveAmpersands(wchar_t* text)
{
    if (text == NULL)
    {
        TRACE_E("Unexpected situation in RemoveAmpersands().");
        return;
    }
    wchar_t* s = text;
    while (*s != 0 && *s != L'&')
        s++;
    if (*s != 0)
    {
        wchar_t* d = s;
        while (*s != 0)
        {
            if (*s != L'&')
                *d++ = *s++;
            else
            {
                if (*(s + 1) == L'&')
                    *d++ = *s++; // pair "&&" -> replace with '&'
                s++;
            }
        }
        *d = 0;
    }
}

BOOL CPathHistoryItem::Execute(CFilesWindow* panel)
{
    BOOL ret = TRUE; // normally return success
    if (!PathOrArchiveOrFSName.empty()) // valid data
    {
        int failReason;
        BOOL clear = TRUE;
        if (Type == 0) // disk
        {
            // This used to pick between the native-wide ChangePathToDisk and the narrow
            // ChangePathToDisk on "is the wide twin populated?". There is one value now and
            // it is always wide, so the predicate was constant-true and the narrow arm dead.
            BOOL diskOk = panel->ChangePathToDisk(panel->HWindow, PathOrArchiveOrFSName.c_str(), TopIndex,
                                                   FocusedName.empty() ? NULL : FocusedName.c_str(), NULL,
                                                   TRUE, FALSE, FALSE, &failReason, TRUE, FSTRYCLOSE_CHANGEPATH);
            if (!diskOk)
            {
                if (failReason == CHPPFR_CANNOTCLOSEPATH)
                {
                    ret = FALSE;   // stay in place
                    clear = FALSE; // no jump, no need to clear top-indexes
                }
            }
        }
        else
        {
            if (Type == 1) // archive
            {
                // Same collapsed routing as the disk arm above.
                BOOL archOk = panel->ChangePathToArchive(PathOrArchiveOrFSName.c_str(),
                                                          ArchivePathOrFSUserPart.c_str(), TopIndex,
                                                          FocusedName.empty() ? NULL : FocusedName.c_str(), FALSE, NULL, TRUE, &failReason, FALSE, FALSE, TRUE);
                if (!archOk)
                {
                    if (failReason == CHPPFR_CANNOTCLOSEPATH)
                    {
                        ret = FALSE;   // stay in place
                        clear = FALSE; // no jump, no need to clear top indexes
                    }
                    else
                    {
                        if (failReason == CHPPFR_SHORTERPATH || failReason == CHPPFR_FILENAMEFOCUSED)
                        {
                            std::wstring msg = FormatStrW(LoadStrW(IDS_PATHINARCHIVENOTFOUND),
                                                          ArchivePathOrFSUserPart.c_str());
                            gPrompter->ShowError(LoadStrW(IDS_ERRORCHANGINGDIR), msg.c_str());
                        }
                    }
                }
            }
            else
            {
                if (Type == 2) // FS
                {
                    BOOL done = FALSE;
                    // if FS interface is known in which the path was last opened, try to
                    // find it among detached ones and use it
                    if (MainWindow != NULL && PluginFS != NULL && // if FS interface is known
                        (!panel->Is(ptPluginFS) ||                // and if it's not currently in panel
                         !panel->GetPluginFS()->Contains(PluginFS)))
                    {
                        CDetachedFSList* list = MainWindow->DetachedFSList;
                        int i;
                        for (i = 0; i < list->Count; i++)
                        {
                            if (list->At(i)->Contains(PluginFS))
                            {
                                done = TRUE;
                                // try changing to requested path (it was there last time, don't need to test IsOurPath),
                                // also reconnect detached FS
                                if (!panel->ChangePathToDetachedFS(i, TopIndex, FocusedName.empty() ? NULL : FocusedName.c_str(), TRUE, &failReason,
                                                                   PathOrArchiveOrFSName.c_str(), ArchivePathOrFSUserPart.c_str()))
                                {
                                    if (failReason == CHPPFR_CANNOTCLOSEPATH)
                                    {
                                        ret = FALSE;   // stay in place
                                        clear = FALSE; // no jump, no need to clear top indexes
                                    }
                                }

                                break; // end, another match with PluginFS is out of question
                            }
                        }
                    }

                    // if previous part failed and path cannot be listed in FS interface in panel,
                    // try to find detached FS interface that could list the path (to avoid
                    // unnecessarily opening new FS)
                    int fsNameIndex;
                    BOOL convertPathToInternalDummy = FALSE;
                    if (!done && MainWindow != NULL &&
                        (!panel->Is(ptPluginFS) || // FS interface in panel cannot list the path
                         !panel->GetPluginFS()->Contains(PluginFS) &&
                             !panel->IsPathFromActiveFS(PathOrArchiveOrFSName.c_str(), ArchivePathOrFSUserPart,
                                                        fsNameIndex, convertPathToInternalDummy)))
                    {
                        CDetachedFSList* list = MainWindow->DetachedFSList;
                        int i;
                        for (i = 0; i < list->Count; i++)
                        {
                            if (list->At(i)->IsPathFromThisFS(PathOrArchiveOrFSName.c_str(), ArchivePathOrFSUserPart.c_str()))
                            {
                                done = TRUE;
                                // try changing to requested path, also reconnect detached FS
                                if (!panel->ChangePathToDetachedFS(i, TopIndex, FocusedName.empty() ? NULL : FocusedName.c_str(), TRUE, &failReason,
                                                                   PathOrArchiveOrFSName.c_str(), ArchivePathOrFSUserPart.c_str()))
                                {
                                    if (failReason == CHPPFR_SHORTERPATH) // almost success (path is just shortened) (CHPPFR_FILENAMEFOCUSED not a risk here)
                                    {                                     // restore FS interface record
                                        if (panel->Is(ptPluginFS))
                                            PluginFS = panel->GetPluginFS()->GetInterface();
                                    }
                                    if (failReason == CHPPFR_CANNOTCLOSEPATH)
                                    {
                                        ret = FALSE;   // stay in place
                                        clear = FALSE; // no jump, no need to clear top indexes
                                    }
                                }
                                else // complete success
                                {    // restore FS interface record
                                    if (panel->Is(ptPluginFS))
                                        PluginFS = panel->GetPluginFS()->GetInterface();
                                }

                                break;
                            }
                        }
                    }

                    // when nothing else works, open new FS interface or just change path on active FS interface
                    if (!done)
                    {
                        if (!panel->ChangePathToPluginFS(PathOrArchiveOrFSName.c_str(), ArchivePathOrFSUserPart.c_str(), TopIndex,
                                                         FocusedName.empty() ? NULL : FocusedName.c_str(), FALSE, 2, NULL, TRUE, &failReason))
                        {
                            if (failReason == CHPPFR_SHORTERPATH ||   // almost success (path is just shortened)
                                failReason == CHPPFR_FILENAMEFOCUSED) // almost success (path just changed to file and it was focused)
                            {                                         // restore FS interface record
                                if (panel->Is(ptPluginFS))
                                    PluginFS = panel->GetPluginFS()->GetInterface();
                            }
                            if (failReason == CHPPFR_CANNOTCLOSEPATH)
                            {
                                ret = FALSE;   // stay in place
                                clear = FALSE; // no jump, no need to clear top indexes
                            }
                        }
                        else // complete success
                        {    // restore FS interface record
                            if (panel->Is(ptPluginFS))
                                PluginFS = panel->GetPluginFS()->GetInterface();
                        }
                    }
                }
            }
        }
        if (clear)
            panel->TopIndexMem.Clear(); // long jump
    }
    UpdateWindow(MainWindow->HWindow);
    return ret;
}

BOOL CPathHistoryItem::IsTheSamePath(CPathHistoryItem& item, CPluginFSInterfaceEncapsulation* curPluginFS)
{
    if (Type == item.Type)
    {
        if (Type == 0) // disk
        {
            const std::wstring path1 = GetPath();
            const std::wstring path2 = item.GetPath();
            // GetPath now renders the single wide value, so this compare is
            // exact and the wide confirmation that used to be ANDed on has nothing left to add.
            if (StrICmpW(path1.c_str(), path2.c_str()) == 0)
                return TRUE;
        }
        else
        {
            if (Type == 1) // archivee
            {
                // Both values are wide, so both compares are exact and the two
                // wide confirmations collapse into them. Case sensitivity is unchanged: the
                // archive file stays case-INsensitive, the path inside it case-sensitive.
                if (StrICmpW(PathOrArchiveOrFSName.c_str(), item.PathOrArchiveOrFSName.c_str()) == 0 &&
                    wcscmp(ArchivePathOrFSUserPart.c_str(), item.ArchivePathOrFSUserPart.c_str()) == 0)
                {
                    return TRUE;
                }
            }
            else
            {
                if (Type == 2) // FS
                {
                    if (StrICmpW(PathOrArchiveOrFSName.c_str(), item.PathOrArchiveOrFSName.c_str()) == 0) // fs-name is "case-insensitive"
                    {
                        if (wcscmp(ArchivePathOrFSUserPart.c_str(), item.ArchivePathOrFSUserPart.c_str()) == 0) // fs-user-part is "case-sensitive"
                            return TRUE;
                        if (curPluginFS != NULL && // also handle case when both fs-user-parts are identical because FS returns TRUE from IsCurrentPath for them (generally we would need to introduce method for comparing two fs-user-parts, but I don't want to do it just for histories, maybe later...)
                            StrICmpW(PathOrArchiveOrFSName.c_str(), curPluginFS->GetPluginFSName()) == 0)
                        {
                            int fsNameInd = curPluginFS->GetPluginFSNameIndex();
                            if (curPluginFS->IsCurrentPath(fsNameInd, fsNameInd, ArchivePathOrFSUserPart.c_str()) &&
                                curPluginFS->IsCurrentPath(fsNameInd, fsNameInd, item.ArchivePathOrFSUserPart.c_str()))
                            {
                                return TRUE;
                            }
                        }
                    }
                }
            }
        }
    }
    return FALSE;
}

//
// ****************************************************************************
// CPathHistory
//

CPathHistory::CPathHistory(BOOL dontChangeForwardIndex) : Paths(10, 5)
{
    ForwardIndex = -1;
    Lock = FALSE;
    DontChangeForwardIndex = dontChangeForwardIndex;
    NewItem = NULL;
}

CPathHistory::~CPathHistory()
{
    if (NewItem != NULL)
        delete NewItem;
}

void CPathHistory::ClearHistory()
{
    Paths.DestroyMembers();

    if (NewItem != NULL)
    {
        delete NewItem;
        NewItem = NULL;
    }
}

void CPathHistory::ClearPluginFSFromHistory(CPluginFSInterfaceAbstract* fs)
{
    if (NewItem != NULL && NewItem->PluginFS == fs)
    {
        NewItem->PluginFS = NULL; // FS was just closed -> set to NULL
    }
    int i;
    for (i = 0; i < Paths.Count; i++)
    {
        CPathHistoryItem* item = Paths[i];
        if (item->Type == 2 && item->PluginFS == fs)
            item->PluginFS = NULL; // FS was just closed -> set to NULL
    }
}

void CPathHistory::FillBackForwardPopupMenu(CMenuPopup* popup, BOOL forward)
{
    // item IDs must be in the range <1..?>
    MENU_ITEM_INFO mii;
    mii.Mask = MENU_MASK_TYPE | MENU_MASK_ID | MENU_MASK_STRING;
    mii.Type = MENU_TYPE_STRING;

    if (forward)
    {
        if (ForwardIndex != -1)
        {
            int id = 1;
            int i;
            for (i = ForwardIndex; i < Paths.Count; i++)
            {
                std::wstring path = Paths[i]->GetPath();
                mii.String = path.data();
                mii.ID = id++;
                popup->InsertItem(-1, TRUE, &mii);
            }
        }
    }
    else
    {
        int id = 2;
        int count = (ForwardIndex == -1) ? Paths.Count : ForwardIndex;
        int i;
        for (i = count - 2; i >= 0; i--)
        {
            std::wstring path = Paths[i]->GetPath();
            mii.String = path.data();
            mii.ID = id++;
            popup->InsertItem(-1, TRUE, &mii);
        }
    }
}

void CPathHistory::FillHistoryPopupMenu(CMenuPopup* popup, DWORD firstID, int maxCount,
                                        BOOL separator)
{
    MENU_ITEM_INFO mii;
    mii.Mask = MENU_MASK_TYPE | MENU_MASK_ID | MENU_MASK_STRING | MENU_MASK_ICON;
    mii.Type = MENU_TYPE_STRING;

    int firstIndex = popup->GetItemCount();

    int added = 0; // number of added items

    int id = firstID;
    int count = (ForwardIndex == -1) ? Paths.Count : ForwardIndex;
    int i;
    for (i = count - 1; i >= 0; i--)
    {
        if (maxCount != -1 && added >= maxCount)
            break;
        std::wstring path = Paths[i]->GetPath();
        mii.String = path.data();
        mii.HIcon = Paths[i]->GetIcon();
        mii.ID = id++;
        popup->InsertItem(-1, TRUE, &mii);
        added++;
    }

    if (added > 0)
        popup->AssignHotKeys();

    if (separator && added > 0)
    {
        // vlozime separator
        mii.Mask = MENU_MASK_TYPE;
        mii.Type = MENU_TYPE_SEPARATOR;
        popup->InsertItem(firstIndex, TRUE, &mii);
    }
}

void CPathHistory::Execute(int index, BOOL forward, CFilesWindow* panel, BOOL allItems, BOOL removeItem)
{
    if (Lock)
        return;

    CPathHistoryItem* item = NULL; // if we need to remove path, save pointer to it for lookup

    BOOL change = TRUE;
    if (forward)
    {
        if (HasForward())
        {
            if (ForwardIndex + index - 1 < Paths.Count)
            {
                Lock = TRUE;
                item = Paths[ForwardIndex + index - 1];
                change = item->Execute(panel);
                if (!change)
                    item = NULL; // failed to change path => leave it in history
                Lock = FALSE;
            }
            if (change && !DontChangeForwardIndex)
                ForwardIndex = ForwardIndex + index;
            if (ForwardIndex >= Paths.Count)
                ForwardIndex = -1;
        }
    }
    else
    {
        index--; // because numbering starts from 2 in FillPopupMenu
        if (HasBackward() || allItems && HasPaths())
        {
            int count = ((ForwardIndex == -1) ? Paths.Count : ForwardIndex) - 1;
            if (count - index >= 0) // have where to go (it's not the last item)
            {
                if (count - index < Paths.Count)
                {
                    Lock = TRUE;
                    item = Paths[count - index];
                    change = item->Execute(panel);
                    if (!change)
                        item = NULL; // failed to change path => leave it in history
                    Lock = FALSE;
                }
                if (change && !DontChangeForwardIndex)
                    ForwardIndex = count - index + 1;
            }
        }
    }
    IdleRefreshStates = TRUE; // force check of status variables on next Idle

    if (NewItem != NULL)
    {
        // The wide twins that had to be threaded through this flush by hand
        // are gone; there is one value and re-adding it cannot degrade it.
        AddPathUnique(NewItem->Type, NewItem->PathOrArchiveOrFSName.c_str(), NewItem->ArchivePathOrFSUserPart.c_str(),
                      NewItem->HIcon, NewItem->PluginFS, NULL);
        NewItem->HIcon = NULL; // AddPathUnique method took over responsibility for icon destruction
        delete NewItem;
        NewItem = NULL;
    }
    if (removeItem && item != NULL)
    {
        if (DontChangeForwardIndex)
        {
            // remove executed item from list
            Lock = TRUE;
            int i;
            for (i = 0; i < Paths.Count; i++)
            {
                if (Paths[i] == item)
                {
                    Paths.Delete(i);
                    break;
                }
            }
            Lock = FALSE;
        }
        else
        {
            TRACE_E("Path removing is not supported for this setting.");
        }
    }
}

void CPathHistory::ChangeActualPathData(int type, const wchar_t* pathOrArchiveOrFSName,
                                        const wchar_t* archivePathOrFSUserPart,
                                        CPluginFSInterfaceAbstract* pluginFS,
                                        CPluginFSInterfaceEncapsulation* curPluginFS,
                                        int topIndex, const wchar_t* focusedName)
{
    if (Paths.Count > 0)
    {
        CPathHistoryItem n(type, pathOrArchiveOrFSName, archivePathOrFSUserPart, NULL, pluginFS);
        CPathHistoryItem* n2 = NULL;
        if (ForwardIndex != -1)
        {
            if (ForwardIndex > 0)
                n2 = Paths[ForwardIndex - 1];
            else
                TRACE_E("Unexpected situation in CPathHistory::ChangeActualPathData");
        }
        else
            n2 = Paths[Paths.Count - 1];

        if (n2 != NULL && n.IsTheSamePath(*n2, curPluginFS)) // same paths -> change data
            n2->ChangeData(topIndex, focusedName);
    }
}

void CPathHistory::RemoveActualPath(int type, const wchar_t* pathOrArchiveOrFSName,
                                    const wchar_t* archivePathOrFSUserPart,
                                    CPluginFSInterfaceAbstract* pluginFS,
                                    CPluginFSInterfaceEncapsulation* curPluginFS)
{
    if (Lock)
        return;
    if (Paths.Count > 0)
    {
        if (ForwardIndex == -1)
        {
            CPathHistoryItem n(type, pathOrArchiveOrFSName, archivePathOrFSUserPart, NULL, pluginFS);
            CPathHistoryItem* n2 = Paths[Paths.Count - 1];
            if (n.IsTheSamePath(*n2, curPluginFS)) // same paths -> delete record
                Paths.Delete(Paths.Count - 1);
        }
        else
            TRACE_E("Unexpected situation in CPathHistory::RemoveActualPath(): ForwardIndex != -1");
    }
}

void CPathHistory::AddPath(int type, const wchar_t* pathOrArchiveOrFSName, const wchar_t* archivePathOrFSUserPart,
                           CPluginFSInterfaceAbstract* pluginFS, CPluginFSInterfaceEncapsulation* curPluginFS)
{
    if (Lock)
        return;

    CPathHistoryItem* n = new CPathHistoryItem(type, pathOrArchiveOrFSName, archivePathOrFSUserPart,
                                               NULL, pluginFS);
    if (n == NULL)
    {
        TRACE_E(LOW_MEMORY);
        return;
    }
    if (Paths.Count > 0)
    {
        CPathHistoryItem* n2 = NULL;
        if (ForwardIndex != -1)
        {
            if (ForwardIndex > 0)
                n2 = Paths[ForwardIndex - 1];
            else
                TRACE_E("Unexpected situation in CPathHistory::AddPath");
        }
        else
            n2 = Paths[Paths.Count - 1];

        if (n2 != NULL && n->IsTheSamePath(*n2, curPluginFS))
        {
            delete n;
            return; // same paths -> nothing to do
        }
    }

    // path really needs to be added ...
    if (ForwardIndex != -1)
    {
        while (Paths.IsGood() && ForwardIndex < Paths.Count)
        {
            Paths.Delete(ForwardIndex);
        }
        ForwardIndex = -1;
    }
    while (Paths.IsGood() && Paths.Count > PATH_HISTORY_SIZE)
    {
        Paths.Delete(0);
    }
    Paths.Add(n);
    if (!Paths.IsGood())
    {
        delete n;
        Paths.ResetState();
    }
}

void CPathHistory::AddPathUnique(int type, const wchar_t* pathOrArchiveOrFSName, const wchar_t* archivePathOrFSUserPart,
                                 HICON hIcon, CPluginFSInterfaceAbstract* pluginFS,
                                 CPluginFSInterfaceEncapsulation* curPluginFS)
{
    CPathHistoryItem* n = new CPathHistoryItem(type, pathOrArchiveOrFSName, archivePathOrFSUserPart,
                                               hIcon, pluginFS);
    if (Lock)
    {
        if (NewItem != NULL)
        {
            TRACE_E("Unexpected situation in CPathHistory::AddPathUnique()");
            delete NewItem;
        }
        NewItem = n;
        return;
    }

    if (n == NULL)
    {
        TRACE_E(LOW_MEMORY);
        if (hIcon != NULL)
            HANDLES(DestroyIcon(hIcon)); // need to destroy the icon
        return;
    }
    if (Paths.Count > 0)
    {
        int i;
        for (i = 0; i < Paths.Count; i++)
        {
            CPathHistoryItem* item = Paths[i];

            if (n->IsTheSamePath(*item, curPluginFS))
            {
                if (type == 2 && pluginFS != NULL)
                { // it's an FS, replace pluginFS (so the path opens on the last FS for this path)
                    item->PluginFS = pluginFS;
                }
                delete n;
                if (i < Paths.Count - 1)
                {
                    // move the path to the top of the list
                    Paths.Add(item);
                    if (Paths.IsGood())
                        Paths.Detach(i); // if adding succeeded, remove the source
                    if (!Paths.IsGood())
                        Paths.ResetState();
                }
                return; // same paths -> nothing to do
            }
        }
    }

    // path really needs to be added ...
    if (ForwardIndex != -1)
    {
        while (Paths.IsGood() && ForwardIndex < Paths.Count)
        {
            Paths.Delete(ForwardIndex);
        }
        ForwardIndex = -1;
    }
    while (Paths.IsGood() && Paths.Count > PATH_HISTORY_SIZE)
    {
        Paths.Delete(0);
    }
    Paths.Add(n);
    if (!Paths.IsGood())
    {
        delete n;
        Paths.ResetState();
    }
}

void CPathHistory::SaveToRegistry(HKEY hKey, const wchar_t* name, BOOL onlyClear)
{
    HKEY historyKey;
    if (CreateKeyW(hKey, name, historyKey))
    {
        ClearKey(historyKey);

        if (!onlyClear) // if key should not just be cleared, save values from history
        {
            // Project the in-memory CPathHistoryItem list into the plain-data
            // sally::path::history::Entry stream the serializer consumes. Wide
            // twins are preferred when populated; ANSI mirrors are widened
            // through CP_ACP for plugin FS items (still ANSI-only today).
            std::vector<sally::path::history::Entry> entries;
            entries.reserve(Paths.Count);
            for (int i = 0; i < Paths.Count; i++)
            {
                CPathHistoryItem* item = Paths[i];
                sally::path::history::Entry entry;
                switch (item->Type)
                {
                case 0:
                    entry.kind = sally::path::history::EntryKind::Disk;
                    break;
                case 1:
                    entry.kind = sally::path::history::EntryKind::Archive;
                    break;
                case 2:
                    entry.kind = sally::path::history::EntryKind::PluginFS;
                    break;
                default:
                    TRACE_E("CPathHistory::SaveToRegistry() unknown path type");
                    continue;
                }
                entry.nameW = item->PathOrArchiveOrFSName;
                if (entry.kind != sally::path::history::EntryKind::Disk)
                    entry.userPartW = item->ArchivePathOrFSUserPart;
                entries.push_back(std::move(entry));
            }

            sally::path::history::WriteEntries(gRegistry, historyKey, entries);
        }
        CloseKey(historyKey);
    }
}

namespace
{
// The serializer stays dependency-light; this bridge supplies the application's
// plugin-FS parser without changing the UTF-16 ownership of the persisted entry.
bool PluginFSPathDetectorBridge(const wchar_t* path,
                                std::wstring& outFsName,
                                std::wstring& outUserPart)
{
    std::wstring fsName;
    const wchar_t* userPart = nullptr;
    if (!IsPluginFSPath(path, &fsName, &userPart))
        return false;
    outFsName = std::move(fsName);
    outUserPart = userPart != nullptr ? userPart : L"";
    return true;
}
} // namespace

void CPathHistory::LoadFromRegistry(HKEY hKey, const wchar_t* name)
{
    ClearHistory();
    HKEY historyKey;
    if (OpenKeyW(hKey, name, historyKey))
    {
        // Read the wide REG_SZ stream through the serializer; plugin FS
        // detection is bridged through PluginFSPathDetectorBridge so the
        // serializer module itself stays decoupled from consts.h.
        std::vector<sally::path::history::Entry> entries;
        sally::path::history::ReadEntries(gRegistry, historyKey, entries,
                                          &PluginFSPathDetectorBridge);

        for (const sally::path::history::Entry& entry : entries)
        {
            int type = static_cast<int>(entry.kind);
            const wchar_t* userPartPtr = entry.kind == sally::path::history::EntryKind::Disk
                                             ? nullptr
                                             : entry.userPartW.c_str();
            AddPath(type, entry.nameW.c_str(), userPartPtr, NULL, NULL);
        }
        CloseKey(historyKey);
    }
}

//
// ****************************************************************************
// CUserMenuIconData
//

CUserMenuIconData::CUserMenuIconData(const wchar_t* fileName, DWORD iconIndex, const wchar_t* umCommand)
{
    FileName = fileName != NULL ? fileName : L"";
    IconIndex = iconIndex;
    UMCommand = umCommand != NULL ? umCommand : L"";
    LoadedIcon = NULL;
}

CUserMenuIconData::~CUserMenuIconData()
{
    if (LoadedIcon != NULL)
    {
        HANDLES(DestroyIcon(LoadedIcon));
        LoadedIcon = NULL;
    }
}

void CUserMenuIconData::Clear()
{
    FileName.clear();
    IconIndex = -1;
    UMCommand.clear();
    LoadedIcon = NULL;
}

//
// ****************************************************************************
// CUserMenuIconDataArr
//

HICON
CUserMenuIconDataArr::GiveIconForUMI(const wchar_t* fileName, DWORD iconIndex, const wchar_t* umCommand)
{
    CALL_STACK_MESSAGE1("CUserMenuIconDataArr::GiveIconForUMI(, ,)");
    for (int i = 0; i < Count; i++)
    {
        CUserMenuIconData* item = At(i);
        if (item->IconIndex == iconIndex &&
            item->FileName == fileName &&
            item->UMCommand == umCommand)
        {
            HICON icon = item->LoadedIcon; // NULL LoadedIcon, otherwise it would be deallocated (via DestroyIcon())
            item->Clear();                 // don't want to shift array (during deletion) - slow+unnecessary, so just clear item so it's skipped faster during search
            return icon;
        }
    }
    TRACE_E("CUserMenuIconDataArr::GiveIconForUMI(): unexpected situation: item not found!");
    return NULL;
}

//
// ****************************************************************************
// CUserMenuIconBkgndReader
//

CUserMenuIconBkgndReader::CUserMenuIconBkgndReader()
{
    SysColorsChanged = FALSE;
    HANDLES(InitializeCriticalSection(&CS));
    IconReaderThreadUID = 1;
    CurIRThreadIDIsValid = FALSE;
    CurIRThreadID = -1;
    AlreadyStopped = FALSE;
    UserMenuIconsInUse = 0;
    UserMenuIIU_BkgndReaderData = NULL;
    UserMenuIIU_ThreadID = 0;
}

CUserMenuIconBkgndReader::~CUserMenuIconBkgndReader()
{
    if (UserMenuIIU_BkgndReaderData != NULL) // they really won't be needed anymore, release them
    {
        delete UserMenuIIU_BkgndReaderData;
        UserMenuIIU_BkgndReaderData = NULL;
    }
    HANDLES(DeleteCriticalSection(&CS));
}

unsigned BkgndReadingIconsThreadBody(void* param)
{
    CALL_STACK_MESSAGE1("BkgndReadingIconsThreadBody()");
    SetThreadNameInVCAndTrace(L"UMIconReader");
    TRACE_I("Begin");
    // aby chodilo GetFileOrPathIconAux (obsahuje COM/OLE sracky)
    if (OleInitialize(NULL) != S_OK)
        TRACE_E("Error in OleInitialize.");

    CUserMenuIconDataArr* bkgndReaderData = (CUserMenuIconDataArr*)param;
    DWORD threadID = bkgndReaderData->GetIRThreadID();

    for (int i = 0; UserMenuIconBkgndReader.IsCurrentIRThreadID(threadID) && i < bkgndReaderData->Count; i++)
    {
        CUserMenuIconData* item = bkgndReaderData->At(i);
        HICON umIcon;
        if (!item->FileName.empty() &&
            gFileSystem->GetFileAttributes(item->FileName.c_str()) != INVALID_FILE_ATTRIBUTES && // accessibility test (instead of CheckPath)
            ExtractIconExW(item->FileName.c_str(), item->IconIndex, NULL, &umIcon, 1) == 1)
        {
            HANDLES_ADD(__htIcon, __hoLoadImage, umIcon); // pridame handle na 'umIcon' do HANDLES
        }
        else
        {
            umIcon = NULL;
            if (!item->UMCommand.empty())
            { // in case previous method failed - try to get icon from system
                DWORD attrs = gFileSystem->GetFileAttributes(item->UMCommand.c_str());
                if (attrs != INVALID_FILE_ATTRIBUTES) // accessibility test (instead of CheckPath)
                {
                    umIcon = GetFileOrPathIconAuxW(item->UMCommand.c_str(), FALSE,
                                                   (attrs & FILE_ATTRIBUTE_DIRECTORY) != 0);
                }
            }
        }
        item->LoadedIcon = umIcon; // save result: loaded icon or NULL on error
    }

    UserMenuIconBkgndReader.ReadingFinished(threadID, bkgndReaderData);
    OleUninitialize();
    TRACE_I("End");
    return 0;
}

unsigned BkgndReadingIconsThreadEH(void* param)
{
#ifndef CALLSTK_DISABLE
    __try
    {
#endif // CALLSTK_DISABLE
        return BkgndReadingIconsThreadBody(param);
#ifndef CALLSTK_DISABLE
    }
    __except (CCallStack::HandleException(GetExceptionInformation()))
    {
        TRACE_I("Thread BkgndReadingIconsThread: calling ExitProcess(1).");
        //    ExitProcess(1);
        TerminateProcess(GetCurrentProcess(), 1); // harder exit (ExitProcess still calls something)
        return 1;
    }
#endif // CALLSTK_DISABLE
}

DWORD WINAPI BkgndReadingIconsThread(void* param)
{
#ifndef CALLSTK_DISABLE
    CCallStack stack;
#endif // CALLSTK_DISABLE
    return BkgndReadingIconsThreadEH(param);
}

void CUserMenuIconBkgndReader::StartBkgndReadingIcons(CUserMenuIconDataArr* bkgndReaderData)
{
    CALL_STACK_MESSAGE1("CUserMenuIconBkgndReader::StartBkgndReadingIcons()");
    HANDLE thread = NULL;
    HANDLES(EnterCriticalSection(&CS));
    CurIRThreadIDIsValid = FALSE;
    if (!AlreadyStopped && bkgndReaderData != NULL && bkgndReaderData->Count > 0)
    {
        DWORD newThreadID = IconReaderThreadUID++;
        bkgndReaderData->SetIRThreadID(newThreadID);
        thread = HANDLES(CreateThread(NULL, 0, BkgndReadingIconsThread, bkgndReaderData, 0, NULL));
        if (thread != NULL)
        {
            // main thread runs at higher priority, if icons should be read as fast
            // as before introducing reading in separate thread, we must also increase its priority
            SetThreadPriority(thread, THREAD_PRIORITY_ABOVE_NORMAL);

            bkgndReaderData = NULL; // passed to thread, won't free them here
            CurIRThreadIDIsValid = TRUE;
            CurIRThreadID = newThreadID;
            AddAuxThread(thread); // if thread doesn't finish in time, kill it before closing software
        }
        else
            TRACE_E("CUserMenuIconBkgndReader::StartBkgndReadingIcons(): unable to start thread for reading user menu icons.");
    }
    if (bkgndReaderData != NULL)
        delete bkgndReaderData;
    HANDLES(LeaveCriticalSection(&CS));

    // pause for a short moment, if icons are read quickly, "simple"
    // variants won't show at all (less blinking) + some users reported that due to concurrent icon loading into panel
    // icon reading into usermenu slowed down quite roughly and because of that icons on usermenu toolbar show
    // with big delay, which is ugly, this should prevent it (it will simply handle just slow
    // usermenu icon loading, which is the goal of this whole task)
    if (thread != NULL)
    {
        //    TRACE_I("Waiting for finishing of thread for reading user menu icons...");
        BOOL finished = WaitForSingleObject(thread, 500) == WAIT_OBJECT_0;
        //    TRACE_I("Thread for reading user menu icons is " << (finished ? "FINISHED." : "still running..."));
    }
}

void CUserMenuIconBkgndReader::EndProcessing()
{
    CALL_STACK_MESSAGE1("CUserMenuIconBkgndReader::EndProcessing()");
    HANDLES(EnterCriticalSection(&CS));
    CurIRThreadIDIsValid = FALSE;
    AlreadyStopped = TRUE;
    HANDLES(LeaveCriticalSection(&CS));
}

BOOL CUserMenuIconBkgndReader::IsCurrentIRThreadID(DWORD threadID)
{
    CALL_STACK_MESSAGE2("CUserMenuIconBkgndReader::IsCurrentIRThreadID(%d)", threadID);
    HANDLES(EnterCriticalSection(&CS));
    BOOL ret = CurIRThreadIDIsValid && CurIRThreadID == threadID;
    HANDLES(LeaveCriticalSection(&CS));
    return ret;
}

BOOL CUserMenuIconBkgndReader::IsReadingIcons()
{
    CALL_STACK_MESSAGE1("CUserMenuIconBkgndReader::IsReadingIcons()");
    HANDLES(EnterCriticalSection(&CS));
    BOOL ret = CurIRThreadIDIsValid;
    HANDLES(LeaveCriticalSection(&CS));
    return ret;
}

void CUserMenuIconBkgndReader::ReadingFinished(DWORD threadID, CUserMenuIconDataArr* bkgndReaderData)
{
    CALL_STACK_MESSAGE2("CUserMenuIconBkgndReader::ReadingFinished(%d,)", threadID);
    HANDLES(EnterCriticalSection(&CS));
    BOOL ok = CurIRThreadIDIsValid && CurIRThreadID == threadID;
    HWND mainWnd = ok ? MainWindow->HWindow : NULL;
    HANDLES(LeaveCriticalSection(&CS));

    if (ok) // User Menu is still waiting for these icons
        PostMessage(mainWnd, WM_USER_USERMENUICONS_READY, (WPARAM)bkgndReaderData, (LPARAM)threadID);
    else
        delete bkgndReaderData;
}

void CUserMenuIconBkgndReader::BeginUserMenuIconsInUse()
{
    CALL_STACK_MESSAGE1("CUserMenuIconBkgndReader::BeginUserMenuIconsInUse()");
    HANDLES(EnterCriticalSection(&CS));
    UserMenuIconsInUse++;
    if (UserMenuIconsInUse > 2)
        TRACE_E("CUserMenuIconBkgndReader::BeginUserMenuIconsInUse(): unexpected situation, report to Petr!");
    HANDLES(LeaveCriticalSection(&CS));
}

void CUserMenuIconBkgndReader::EndUserMenuIconsInUse()
{
    CALL_STACK_MESSAGE1("CUserMenuIconBkgndReader::EndUserMenuIconsInUse()");
    HANDLES(EnterCriticalSection(&CS));
    if (UserMenuIconsInUse == 0)
        TRACE_E("CUserMenuIconBkgndReader::EndUserMenuIconsInUse(): unexpected situation, report to Petr!");
    else
    {
        UserMenuIconsInUse--;
        if (UserMenuIconsInUse == 0 && UserMenuIIU_BkgndReaderData != NULL)
        { // last lock, if we have saved data to process, send it
            if (CurIRThreadIDIsValid && CurIRThreadID == UserMenuIIU_ThreadID)
            {
                PostMessage(MainWindow->HWindow, WM_USER_USERMENUICONS_READY,
                            (WPARAM)UserMenuIIU_BkgndReaderData, (LPARAM)UserMenuIIU_ThreadID);
            }
            else // nobody wants the data anymore, just free it
                delete UserMenuIIU_BkgndReaderData;
            UserMenuIIU_BkgndReaderData = NULL;
            UserMenuIIU_ThreadID = 0;
        }
    }
    HANDLES(LeaveCriticalSection(&CS));
}

BOOL CUserMenuIconBkgndReader::EnterCSIfCanUpdateUMIcons(CUserMenuIconDataArr** bkgndReaderData, DWORD threadID)
{
    CALL_STACK_MESSAGE2("CUserMenuIconBkgndReader::EnterCSIfCanUpdateUMIcons(, %d)", threadID);
    HANDLES(EnterCriticalSection(&CS));
    BOOL ret = FALSE;
    if (CurIRThreadIDIsValid && CurIRThreadID == threadID)
    {
        if (UserMenuIconsInUse > 0)
        {
            if (UserMenuIIU_BkgndReaderData != NULL) // if some are already saved, free them (enter cfg during loading, then color change and it comes here second time)
                delete UserMenuIIU_BkgndReaderData;
            UserMenuIIU_BkgndReaderData = *bkgndReaderData;
            UserMenuIIU_ThreadID = threadID;
            *bkgndReaderData = NULL; // caller passed us data this way, we'll free them ourselves later
        }
        else
        {
            ret = TRUE;
            TRACE_I("Updating user menu icons to results from reading thread no. " << threadID);
        }
    }
    if (!ret)
        HANDLES(LeaveCriticalSection(&CS));
    return ret;
}

void CUserMenuIconBkgndReader::LeaveCSAfterUMIconsUpdate()
{
    CurIRThreadIDIsValid = FALSE; // by this icons are passed to usermenu (IsReadingIcons() must return FALSE)
    HANDLES(LeaveCriticalSection(&CS));
}

//
// ****************************************************************************
// CUserMenuItem
//

CUserMenuItem::CUserMenuItem(const wchar_t* name, const wchar_t* umCommand, const wchar_t* arguments, const wchar_t* initDir, const wchar_t* icon,
                             int throughShell, int closeShell, int useWindow, int showInToolbar, CUserMenuItemType type,
                             CUserMenuIconDataArr* bkgndReaderData)
{
    UMIcon = NULL;
    ThroughShell = throughShell;
    CloseShell = closeShell;
    UseWindow = useWindow;
    ShowInToolbar = showInToolbar;
    Type = type;
    Set(name, umCommand, arguments, initDir, icon);
    if (Type == umitItem || Type == umitSubmenuBegin)
        GetIconHandle(bkgndReaderData, FALSE);
}

CUserMenuItem::CUserMenuItem()
{
    UMIcon = NULL;
    ThroughShell = TRUE;
    CloseShell = TRUE;
    UseWindow = TRUE;
    ShowInToolbar = TRUE;
    Type = umitItem;
    static wchar_t emptyBuffer[] = L"";
    static wchar_t nameBuffer[] = L"\"$(Name)\"";
    static wchar_t fullPathBuffer[] = L"$(FullPath)";
    Set(emptyBuffer, emptyBuffer, nameBuffer, fullPathBuffer, emptyBuffer);
}

CUserMenuItem::CUserMenuItem(CUserMenuItem& item, CUserMenuIconDataArr* bkgndReaderData)
{
    UMIcon = NULL;
    ThroughShell = item.ThroughShell;
    CloseShell = item.CloseShell;
    UseWindow = item.UseWindow;
    ShowInToolbar = item.ShowInToolbar;
    Type = item.Type;
    Set(item.ItemName.c_str(), item.UMCommand.c_str(), item.Arguments.c_str(), item.InitDir.c_str(), item.Icon.c_str());
    if (Type == umitItem)
    {
        if (bkgndReaderData == NULL) // here it's a copy to cfg dialog, we don't propagate newly loaded icons there (wait until dialog end)
        {
            UMIcon = DuplicateIcon(NULL, item.UMIcon); // GetIconHandle(); unnecessarily slow
            if (UMIcon != NULL)                        // add 'UMIcon' handle to HANDLES
                HANDLES_ADD(__htIcon, __hoLoadImage, UMIcon);
        }
        else
            GetIconHandle(bkgndReaderData, FALSE);
    }
    if (Type == umitSubmenuBegin)
    {
        if (item.UMIcon != HGroupIcon)
            TRACE_E("CUserMenuItem::CUserMenuItem(): unexpected submenu item icon.");
        UMIcon = HGroupIcon;
    }
}

CUserMenuItem::~CUserMenuItem()
{
    // umitSubmenuBegin shares one icon
    if (UMIcon != NULL && Type != umitSubmenuBegin)
        HANDLES(DestroyIcon(UMIcon));
    // std::string members auto-destroyed
}

BOOL CUserMenuItem::Set(const wchar_t* name, const wchar_t* umCommand, const wchar_t* arguments, const wchar_t* initDir, const wchar_t* icon)
{
    ItemName = name;
    UMCommand = umCommand;
    Arguments = arguments;
    InitDir = initDir;
    Icon = icon;
    return TRUE;
}

void CUserMenuItem::SetType(CUserMenuItemType type)
{
    if (Type != type)
    {
        if (type == umitSubmenuBegin)
        {
            // switching to shared icon, delete allocated one
            if (UMIcon != NULL)
            {
                HANDLES(DestroyIcon(UMIcon));
                UMIcon = NULL;
            }
        }
        if (Type == umitSubmenuBegin)
            UMIcon = NULL; // leaving shared icon
    }
    Type = type;
}

BOOL CUserMenuItem::GetIconHandle(CUserMenuIconDataArr* bkgndReaderData, BOOL getIconsFromReader)
{
    if (Type == umitSubmenuBegin)
    {
        UMIcon = HGroupIcon;
        return TRUE;
    }

    if (UMIcon != NULL)
    {
        HANDLES(DestroyIcon(UMIcon));
        UMIcon = NULL;
    }

    if (Type == umitSeparator) // separator has no icon
        return TRUE;

    // try to extract icon from specified file
    std::wstring fileName;
    DWORD iconIndex = -1;
    if (MainWindow != NULL && !Icon.empty())
    {
        // Icon is in format "filename,resID"
        // perform decomposition
        const wchar_t* iconStr = Icon.c_str();
        const wchar_t* iterator = iconStr + Icon.length() - 1;
        while (iterator > iconStr && *iterator != L',')
            iterator--;
        if (iterator > iconStr && *iterator == L',')
        {
            fileName.assign(iconStr, iterator - iconStr);
            iterator++;
            iconIndex = _wtoi(iterator);
        }
    }

    if (bkgndReaderData == NULL && !fileName.empty() && // read icons right here
        MainWindow->GetActivePanel() != NULL &&
        MainWindow->GetActivePanel()->CheckPath(FALSE, fileName.c_str()) == ERROR_SUCCESS &&
        ExtractIconExW(fileName.c_str(), iconIndex, NULL, &UMIcon, 1) == 1)
    {
        HANDLES_ADD(__htIcon, __hoLoadImage, UMIcon); // add 'UMIcon' handle to HANDLES
        return TRUE;
    }

    // in case previous method failed - try to get icon from system
    std::wstring umCommand;
    if (MainWindow != NULL && !UMCommand.empty() &&
        ExpandCommand(MainWindow->HWindow, UMCommand.c_str(), umCommand, TRUE))
    {
        while (umCommand.length() > 2 && umCommand.front() == L'"' && umCommand.back() == L'"')
            umCommand = umCommand.substr(1, umCommand.length() - 2);
    }
    else
        umCommand.clear();

    if (bkgndReaderData == NULL && !umCommand.empty() && // read icons right here
        MainWindow->GetActivePanel() != NULL &&
        MainWindow->GetActivePanel()->CheckPath(FALSE, umCommand.c_str()) == ERROR_SUCCESS)
    {
        DWORD attrs = gFileSystem->GetFileAttributes(umCommand.c_str());
        UMIcon = GetFileOrPathIconAuxW(umCommand.c_str(), FALSE,
                                       (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY)) != 0);
        if (UMIcon != NULL)
            return TRUE;
    }

    if (bkgndReaderData != NULL)
    {
        if (getIconsFromReader) // icons are already loaded, just take the right one
        {
            UMIcon = bkgndReaderData->GiveIconForUMI(fileName.c_str(), iconIndex, umCommand.c_str());
            if (UMIcon != NULL)
                return TRUE;
        }
        else // request loading of needed icon
            bkgndReaderData->Add(new CUserMenuIconData(fileName.c_str(), iconIndex, umCommand.c_str()));
    }

    // extract default icon from shell32.dll
    UMIcon = SalLoadImage(2, 1, IconSizes[ICONSIZE_16], IconSizes[ICONSIZE_16], IconLRFlags);
    return TRUE;
}

BOOL CUserMenuItem::GetHotKey(wchar_t* key)
{
    if (ItemName.empty() || Type == umitSeparator)
        return FALSE;
    const wchar_t* iterator = ItemName.c_str();
    while (*iterator != 0)
    {
        if (*iterator == L'&' && *(iterator + 1) != 0 && *(iterator + 1) != L'&')
        {
            *key = *(iterator + 1);
            return TRUE;
        }
        iterator++;
    }
    return FALSE;
}

//
// ****************************************************************************
// CUserMenuItems
//

BOOL CUserMenuItems::LoadUMI(CUserMenuItems& source, BOOL readNewIconsOnBkgnd)
{
    CUserMenuItem* item;
    DestroyMembers();
    CUserMenuIconDataArr* bkgndReaderData = readNewIconsOnBkgnd ? new CUserMenuIconDataArr() : NULL;
    int i;
    for (i = 0; i < source.Count; i++)
    {
        item = new CUserMenuItem(*source[i], bkgndReaderData);
        Add(item);
    }
    if (readNewIconsOnBkgnd)
        UserMenuIconBkgndReader.StartBkgndReadingIcons(bkgndReaderData); // WARNING: frees 'bkgndReaderData'
    return TRUE;
}

int CUserMenuItems::GetSubmenuEndIndex(int index)
{
    int level = 1;
    int i;
    for (i = index + 1; i < Count; i++)
    {
        CUserMenuItem* item = At(i);
        if (item->Type == umitSubmenuBegin)
            level++;
        else
        {
            if (item->Type == umitSubmenuEnd)
            {
                level--;
                if (level == 0)
                    return i;
            }
        }
    }
    return -1;
}

//****************************************************************************
//
// Mouse Wheel support
//

// Default values for SPI_GETWHEELSCROLLLINES and
// SPI_GETWHEELSCROLLCHARS
#define DEFAULT_LINES_TO_SCROLL 3
#define DEFAULT_CHARS_TO_SCROLL 3

// handle of the old mouse hook procedure
HHOOK HOldMouseWheelHookProc = NULL;
BOOL MouseWheelMSGThroughHook = FALSE;
DWORD MouseWheelMSGTime = 0;
BOOL GotMouseWheelScrollLines = FALSE;
BOOL GotMouseWheelScrollChars = FALSE;

UINT GetMouseWheelScrollLines()
{
    static UINT uCachedScrollLines;

    // if we've already got it and we're not refreshing,
    // return what we've already got

    if (GotMouseWheelScrollLines)
        return uCachedScrollLines;

    // see if we can find the mouse window

    GotMouseWheelScrollLines = TRUE;

    static UINT msgGetScrollLines;
    static WORD nRegisteredMessage = 0;

    if (nRegisteredMessage == 0)
    {
        msgGetScrollLines = ::RegisterWindowMessage(MSH_SCROLL_LINES);
        if (msgGetScrollLines == 0)
            nRegisteredMessage = 1; // couldn't register!  never try again
        else
            nRegisteredMessage = 2; // it worked: use it
    }

    if (nRegisteredMessage == 2)
    {
        HWND hwMouseWheel = NULL;
        hwMouseWheel = FindWindow(MSH_WHEELMODULE_CLASS, MSH_WHEELMODULE_TITLE);
        if (hwMouseWheel && msgGetScrollLines)
        {
            uCachedScrollLines = (UINT)::SendMessage(hwMouseWheel, msgGetScrollLines, 0, 0);
            return uCachedScrollLines;
        }
    }

    // couldn't use the window -- try system settings
    uCachedScrollLines = DEFAULT_LINES_TO_SCROLL;
    ::SystemParametersInfo(SPI_GETWHEELSCROLLLINES, 0, &uCachedScrollLines, 0);

    return uCachedScrollLines;
}

#define SPI_GETWHEELSCROLLCHARS 0x006C

UINT GetMouseWheelScrollChars()
{
    static UINT uCachedScrollChars;
    if (GotMouseWheelScrollChars)
        return uCachedScrollChars;

    if (WindowsVistaAndLater)
    {
        if (!SystemParametersInfo(SPI_GETWHEELSCROLLCHARS, 0, &uCachedScrollChars, 0))
            uCachedScrollChars = DEFAULT_CHARS_TO_SCROLL;
    }
    else
        uCachedScrollChars = DEFAULT_CHARS_TO_SCROLL;
    GotMouseWheelScrollChars = TRUE;
    return uCachedScrollChars;
}

BOOL PostMouseWheelMessage(MSG* pMSG)
{
    // let find window under mouse cursor
    HWND hWindow = WindowFromPoint(pMSG->pt);
    if (hWindow != NULL)
    {
        wchar_t className[101];
        className[0] = 0;
        if (GetClassNameW(hWindow, className, 100) != 0)
        {
            // some versions of synaptics touchpad (for example on HP notebooks) show their window with scrolling symbol under cursor
            // in such case we won't try to route to "correct" window under cursor, because
            // touchpad will handle it itself
            // https://forum.altap.cz/viewtopic.php?f=24&t=6039
            if (wcscmp(className, L"SynTrackCursorWindowClass") == 0 || wcscmp(className, L"Syn Visual Class") == 0)
            {
                //TRACE_I("Synaptics touchpad detected className="<<className);
                hWindow = pMSG->hwnd;
            }
            else
            {
                DWORD winProcessId = 0;
                GetWindowThreadProcessId(hWindow, &winProcessId);
                if (winProcessId != GetCurrentProcessId()) // no point sending WM_USER_* outside our process
                    hWindow = pMSG->hwnd;
            }
        }
        else
        {
            TRACE_E("GetClassNameW() failed!");
            hWindow = pMSG->hwnd;
        }
        // if it's a ScrollBar with a parent, post message to parent.
        // Scrollbars in panels aren't subclassed, so this is currently the only way
        // panel can learn about wheel rotation when cursor is over scrollbar.
        className[0] = 0;
        if (GetClassNameW(hWindow, className, 100) == 0 || StrICmpW(className, L"scrollbar") == 0)
        {
            HWND hParent = GetParent(hWindow);
            if (hParent != NULL)
                hWindow = hParent;
        }
        PostMessage(hWindow, pMSG->message == WM_MOUSEWHEEL ? WM_USER_MOUSEWHEEL : WM_USER_MOUSEHWHEEL, pMSG->wParam, pMSG->lParam);
    }
    return TRUE;
}

// hook procedure for mouse messages
LRESULT CALLBACK MenuWheelHookProc(int nCode, WPARAM wParam, LPARAM lParam)
{
    //  CALL_STACK_MESSAGE4("MenuWheelHookProc(%d, 0x%IX, 0x%IX)", nCode, wParam, lParam);
    LRESULT retValue = 0;

    retValue = CallNextHookEx(HOldMouseWheelHookProc, nCode, wParam, lParam);

    if (nCode < 0)
        return retValue;

    MSG* pMSG = (MSG*)lParam;
    MessagesKeeper.Add(pMSG); // if Salam crashes, we'll have message history

    // we're only interested in WM_MOUSEWHEEL and WM_MOUSEHWHEEL
    //
    // 7.10.2009 - AS253_B1_IB34: Manison reported that horizontal scroll doesn't work for him under Windows Vista.
    // It worked for me (this way). After installing Intellipoint drivers v7 (previously I didn't have any special drivers on Vista x64)
    // WM_MOUSEHWHEEL messages stopped going through here and went directly to
    // Salamander panel. So I'm disabling this path and messages will be caught only in panel.
    // note: we could probably cut off WM_MOUSEWHEEL handling the same way, but I won't risk
    // breaking something on older OS (we can try it with transition to W2K and later)
    // note2: if it turns out we need to catch WM_MOUSEHWHEEL through this hook too, we should
    // perform runtime detection that WM_MOUSEHWHEEL messages flow through here and subsequently disable their processing
    // in panels and commandline.

    // 30.11.2012 - someone appeared on forum for whom WM_MOUSEHWEEL doesn't go through message hook (same as before
    // with Manison in case of WM_MOUSEHWHEEL): https://forum.altap.cz/viewtopic.php?f=24&t=6039
    // so now we'll also catch message in individual windows where it can potentially go (according to focus)
    // and subsequently route it so it's delivered to window under cursor, as we've always done

    // currently we'll let both WM_MOUSEWHEEL and WM_MOUSEHWHEEL through and see what beta testers say

    if ((pMSG->message != WM_MOUSEWHEEL && pMSG->message != WM_MOUSEHWHEEL) || (wParam == PM_NOREMOVE))
        return retValue;

    // if message arrived "recently" through second channel, ignore this channel
    if (!MouseWheelMSGThroughHook && MouseWheelMSGTime != 0 && (GetTickCount() - MouseWheelMSGTime < MOUSEWHEELMSG_VALID))
        return retValue;
    MouseWheelMSGThroughHook = TRUE;
    MouseWheelMSGTime = GetTickCount();

    PostMouseWheelMessage(pMSG);

    return retValue;
}

BOOL InitializeMenuWheelHook()
{
    // setup hook for mouse messages
    DWORD threadID = GetCurrentThreadId();
    HOldMouseWheelHookProc = SetWindowsHookEx(WH_GETMESSAGE, // HANDLES can't handle!
                                              MenuWheelHookProc,
                                              NULL, threadID);
    return (HOldMouseWheelHookProc != NULL);
}

BOOL ReleaseMenuWheelHook()
{
    // unhook mouse messages
    if (HOldMouseWheelHookProc != NULL)
    {
        UnhookWindowsHookEx(HOldMouseWheelHookProc); // HANDLES can't handle!
        HOldMouseWheelHookProc = NULL;
    }
    return TRUE;
}

//
// *****************************************************************************
// CFileTimeStampsItem
//

CFileTimeStampsItem::CFileTimeStampsItem()
{
    memset(&LastWrite, 0, sizeof(LastWrite));
    FileSize = CQuadWord(0, 0);
    Attr = 0;
}

CFileTimeStampsItem::~CFileTimeStampsItem()
{
}

BOOL CFileTimeStampsItem::Set(const wchar_t* zipRoot, const wchar_t* sourcePath, const wchar_t* fileName,
                              const wchar_t* dosFileName, const FILETIME& lastWrite, const CQuadWord& fileSize,
                              DWORD attr)
{
    if (*zipRoot == L'\\')
        zipRoot++;
    ZIPRoot = zipRoot;
    // zip-root has no backslash at beginning or end
    if (!ZIPRoot.empty() && ZIPRoot.back() == L'\\')
        ZIPRoot.pop_back();
    SourcePath = sourcePath;
    // source-path has no backslash at end
    if (!SourcePath.empty() && SourcePath.back() == L'\\')
        SourcePath.pop_back();
    FileName = fileName;
    if (dosFileName[0] != 0)
        DosFileName = dosFileName;
    LastWrite = lastWrite;
    FileSize = fileSize;
    Attr = attr;
    return TRUE;
}

//
// *****************************************************************************
// CFileTimeStamps
//

BOOL CFileTimeStamps::AddFile(const wchar_t* zipFile, const wchar_t* zipRoot, const wchar_t* sourcePath,
                              const wchar_t* fileName, const wchar_t* dosFileName,
                              const FILETIME& lastWrite, const CQuadWord& fileSize, DWORD attr)
{
    if (ZIPFile.empty())
        ZIPFile = zipFile;
    else
    {
        if (wcscmp(zipFile, ZIPFile.c_str()) != 0)
        {
            TRACE_E("Unexpected situation in CFileTimeStamps::AddFile().");
            return FALSE;
        }
    }

    CFileTimeStampsItem* item = new CFileTimeStampsItem;
    if (item == NULL ||
        !item->Set(zipRoot, sourcePath, fileName, dosFileName, lastWrite, fileSize, attr))
    {
        if (item != NULL)
            delete item;
        TRACE_E(LOW_MEMORY);
        return FALSE;
    }

    // test if it's not already here (not before item construction due to string modification - '\\')
    // wide: AND a wide confirmation onto the narrow FileName match - two different
    // non-ASCII entry names whose CP_ACP mirrors collide would otherwise be wrongly treated as the
    // same file, and the second one's timestamp change would never get detected/packed back. Skips
    // the check when either side has no wide value (old code path or a caller with none).
    int i;
    for (i = 0; i < List.Count; i++)
    {
        CFileTimeStampsItem* item2 = List[i];
        // FileName is wide now, so this comparison is the whole truth. The wide-twin
        // confirmation that used to sit here guarded two entries whose CP_ACP mirrors
        // collided; there is no mirror left to collide.
        if (StrICmpW(item->FileName.c_str(), item2->FileName.c_str()) == 0 &&
            StrICmpW(item->SourcePath.c_str(), item2->SourcePath.c_str()) == 0)
        {
            delete item;
            return FALSE; // already here, don't add another ...
        }
    }

    List.Add(item);
    if (!List.IsGood())
    {
        delete item;
        List.ResetState();
        return FALSE;
    }
    return TRUE;
}

struct CFileTimeStampsEnum2Info
{
    TIndirectArray<CFileTimeStampsItem>* PackList;
    int Index;
};

const wchar_t* WINAPI FileTimeStampsEnum2(HWND parent, int enumFiles, const wchar_t** dosName, BOOL* isDir,
                                          CQuadWord* size, DWORD* attr, FILETIME* lastWrite, void* param,
                                          int* errorOccured)
{ // we enumerate only files, so enumFiles can be completely omitted
    if (errorOccured != NULL)
        *errorOccured = SALENUM_SUCCESS;
    CFileTimeStampsEnum2Info* data = (CFileTimeStampsEnum2Info*)param;

    if (enumFiles == -1)
    {
        if (dosName != NULL)
            *dosName = NULL;
        if (isDir != NULL)
            *isDir = FALSE;
        if (size != NULL)
            *size = CQuadWord(0, 0);
        if (attr != NULL)
            *attr = 0;
        if (lastWrite != NULL)
            memset(lastWrite, 0, sizeof(FILETIME));
        data->Index = 0;
        return NULL;
    }

    if (data->Index < data->PackList->Count)
    {
        CFileTimeStampsItem* item = data->PackList->At(data->Index++);
        if (dosName != NULL)
            *dosName = item->DosFileName.empty() ? item->FileName.c_str() : item->DosFileName.c_str();
        if (isDir != NULL)
            *isDir = FALSE;
        if (size != NULL)
            *size = item->FileSize;
        if (attr != NULL)
            *attr = item->Attr;
        if (lastWrite != NULL)
            *lastWrite = item->LastWrite;
        return item->FileName.c_str();
    }
    else
        return NULL;
}

void CFileTimeStamps::AddFilesToListBox(HWND list)
{
    int i;
    for (i = 0; i < List.Count; i++)
    {
        std::wstring path = List[i]->ZIPRoot;
        SalPathAppendW(path, List[i]->FileName.c_str());
        SendMessageW(list, LB_ADDSTRING, 0, (LPARAM)path.c_str());
    }
}

void CFileTimeStamps::Remove(int* indexes, int count)
{
    int i;
    for (i = 0; i < count; i++)
    {
        int index = indexes[count - i - 1];   // delete from back - less shifting + indexes don't shift
        if (index < List.Count && index >= 0) // just for safety
        {
            List.Delete(index);
        }
    }
}

BOOL CDynamicStringImp::Add(const wchar_t* str, int len)
{
    if (len == -1)
        len = (int)wcslen(str);
    else
    {
        if (len == -2)
            len = (int)wcslen(str) + 1;
    }
    if (Length + len >= Allocated)
    {
        // Allocated/Length are CHARACTER counts; realloc wants BYTES.
        wchar_t* text = (wchar_t*)realloc(Text, (size_t)(Length + len + 100) * sizeof(wchar_t));
        if (text == NULL)
        {
            TRACE_E(LOW_MEMORY);
            return FALSE;
        }
        Allocated = Length + len + 100;
        Text = text;
    }
    memcpy(Text + Length, str, (size_t)len * sizeof(wchar_t));
    Length += len;
    Text[Length] = 0;
    return TRUE;
}

void CDynamicStringImp::DetachData()
{
    Text = NULL;
    Allocated = 0;
    Length = 0;
}

void CFileTimeStamps::CopyFilesTo(HWND parent, int* indexes, int count, const wchar_t* initPath)
{
    CALL_STACK_MESSAGE3("CFileTimeStamps::CopyFilesTo(, , %d, %ls)", count, initPath);
    std::wstring path; // GetTargetDirectoryW owns the sizing
    if (count > 0 &&
        GetTargetDirectoryW(parent, parent, LoadStrW(IDS_BROWSEARCUPDATE),
                           LoadStrW(IDS_BROWSEARCUPDATETEXT), path, FALSE, initPath))
    {
        CDynamicStringImp fromStr, toStr;
        BOOL ok = TRUE;
        int i;
        for (i = 0; i < count; i++)
        {
            int index = indexes[i];
            if (index < List.Count && index >= 0) // just for safety
            {
                CFileTimeStampsItem* item = List[index];
                std::wstring name = item->SourcePath;
                SalPathAppendW(name, item->FileName.c_str());
                ok &= fromStr.Add(name.c_str(), (int)name.size() + 1);

                name = path;
                SalPathAppendW(name, item->ZIPRoot.c_str());
                SalPathAppendW(name, item->FileName.c_str());
                ok &= toStr.Add(name.c_str(), (int)name.size() + 1);
            }
        }
        fromStr.Add(L"\0", 2); // for safety add two more nulls at the end (no Add, also ok)
        toStr.Add(L"\0", 2);   // for safety add two more nulls at the end (no Add, also ok)

        if (ok)
        {
            CShellExecuteWnd shellExecuteWnd;
            SHFILEOPSTRUCTW fo;
            fo.hwnd = shellExecuteWnd.Create(parent, L"SEW: CFileTimeStamps::CopyFilesTo");
            fo.wFunc = FO_COPY;
            fo.pFrom = fromStr.Text;
            fo.pTo = toStr.Text;
            fo.fFlags = FOF_SIMPLEPROGRESS | FOF_NOCONFIRMMKDIR | FOF_MULTIDESTFILES;
            fo.fAnyOperationsAborted = FALSE;
            fo.hNameMappings = NULL;
            const std::wstring title = LoadStrW(IDS_BROWSEARCUPDATE); // own it while other threads may reuse LoadStr storage
            fo.lpszProgressTitle = title.c_str();
            // perform the actual copying - amazingly easy, unfortunately it crashes sometimes ;-)
            CALL_STACK_MESSAGE1("CFileTimeStamps::CopyFilesTo::SHFileOperation");
            SHFileOperationW(&fo);
        }
    }
}

void CFileTimeStamps::CheckAndPackAndClear(HWND parent, BOOL* someFilesChanged, BOOL* archMaybeUpdated)
{
    CALL_STACK_MESSAGE1("CFileTimeStamps::CheckAndPackAndClear()");
    //---  remove files from list that weren't changed
    BeginStopRefresh();
    if (someFilesChanged != NULL)
        *someFilesChanged = FALSE;
    if (archMaybeUpdated != NULL)
        *archMaybeUpdated = FALSE;
    WIN32_FIND_DATAW data;
    int i;
    for (i = List.Count - 1; i >= 0; i--)
    {
        CFileTimeStampsItem* item = List[i];
        std::wstring path = item->SourcePath;
        SalPathAppendW(path, item->FileName.c_str());
        BOOL kill = TRUE;
        HANDLE find = SalFindFirstFileHW(path.c_str(), &data);
        if (find != INVALID_HANDLE_VALUE)
        {
            SalLPFindClose(find);
            if (CompareFileTime(&data.ftLastWriteTime, &item->LastWrite) != 0 ||    // times differ
                CQuadWord(data.nFileSizeLow, data.nFileSizeHigh) != item->FileSize) // sizes differ
            {
                item->FileSize = CQuadWord(data.nFileSizeLow, data.nFileSizeHigh); // take new size
                item->LastWrite = data.ftLastWriteTime;
                item->Attr = data.dwFileAttributes;
                kill = FALSE;
            }
        }
        if (kill)
        {
            List.Delete(i);
        }
    }

    if (List.Count > 0)
    {
        if (someFilesChanged != NULL)
            *someFilesChanged = TRUE;
        // during critical shutdown we pretend updated files don't exist, we won't manage to pack them back to archive
        // but we mustn't delete them, after startup user must have a chance to pack updated files
        // manually into archive
        if (!CriticalShutdown)
        {
            CArchiveUpdateDlg dlg(parent, this, Panel);
            BOOL showDlg = TRUE;
            while (showDlg)
            {
                showDlg = FALSE;
                if (dlg.Execute() == IDOK)
                {
                    if (archMaybeUpdated != NULL)
                        *archMaybeUpdated = TRUE;
                    //--- pack changed files, in groups with same zip-root and source-path
                    TIndirectArray<CFileTimeStampsItem> packList(10, 5); // list of all with same zip-root and source-path
                    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_NORMAL);
                    while (!showDlg && List.Count > 0)
                    {
                        CFileTimeStampsItem* item1 = List[0];
                        const wchar_t *r1, *s1;
                        if (item1 != NULL)
                        {
                            r1 = item1->ZIPRoot.c_str();
                            s1 = item1->SourcePath.c_str();
                            packList.Add(item1);
                            List.Detach(0);
                        }
                        for (i = List.Count - 1; i >= 0; i--) // quadratic complexity hopefully won't be a problem here
                        {                                     // going from back, because Detach is "simpler" that way
                            CFileTimeStampsItem* item2 = List[i];
                            const wchar_t* r2 = item2->ZIPRoot.c_str();
                            const wchar_t* s2 = item2->SourcePath.c_str();
                            if (wcscmp(r1, r2) == 0 && // matching zip-root (case-sensitive comparison necessary - update test\A.txt and Test\b.txt must not happen at once)
                                StrICmpW(s1, s2) == 0)  // matching source-path
                            {
                                packList.Add(item2);
                                List.Detach(i);
                            }
                        }

                        // call pack for packList
                        BOOL loop = TRUE;
                        while (loop)
                        {
                            CFileTimeStampsEnum2Info data2;
                            data2.PackList = &packList;
                            data2.Index = 0;
                            gEnvironment->SetCurrentDirectory(s1);
                            if (Panel->CheckPath(TRUE, NULL, ERROR_SUCCESS, TRUE, parent) == ERROR_SUCCESS &&
                                PackCompress(parent, Panel, ZIPFile.c_str(), r1, FALSE, s1, FileTimeStampsEnum2, &data2))
                                loop = FALSE;
                            else
                            {
                                loop = gPrompter->AskYesNo(LoadStrW(IDS_QUESTION), LoadStrW(IDS_UPDATEFAILED)).type == PromptResult::kYes;
                                if (!loop) // "cancel", detach files from disk-cache, otherwise it deletes them
                                {
                                    List.Add(packList.GetData(), packList.Count);
                                    packList.DetachMembers();
                                    showDlg = TRUE; // show Archive Update dialog again (with remaining files)
                                }
                            }
                            SetCurrentDirectoryToSystem();
                        }

                        packList.DestroyMembers();
                    }
                    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);
                }
            }
        }
    }

    List.DestroyMembers();
    ZIPFile.clear();
    EndStopRefresh();
}

//****************************************************************************
//
// CTopIndexMem
//

void CTopIndexMem::Push(const wchar_t* path, int topIndex)
{
    // check if path follows Path (path==Path+"\\name")
    const wchar_t* s = path + wcslen(path);
    if (s > path && *(s - 1) == L'\\')
        s--;
    BOOL ok;
    if (s == path)
        ok = FALSE;
    else
    {
        if (s > path && *s == L'\\')
            s--;
        while (s > path && *s != L'\\')
            s--;

        int l = (int)Path.size();
        if (l > 0 && Path[l - 1] == L'\\')
            l--;
        ok = s - path == l && StrNICmpW(path, Path.c_str(), l) == 0;
    }

    if (ok) // follows -> remember next top-index
    {
        if (TopIndexesCount == TOP_INDEX_MEM_SIZE) // need to remove first top-index from memory
        {
            int i;
            for (i = 0; i < TOP_INDEX_MEM_SIZE - 1; i++)
                TopIndexes[i] = TopIndexes[i + 1];
            TopIndexesCount--;
        }
        Path = path;
        TopIndexes[TopIndexesCount++] = topIndex;
    }
    else // doesn't follow -> first top-index in sequence
    {
        Path = path;
        TopIndexesCount = 1;
        TopIndexes[0] = topIndex;
    }
}

BOOL CTopIndexMem::FindAndPop(const wchar_t* path, int& topIndex)
{
    // check if path matches Path (path==Path)
    int l1 = (int)wcslen(path);
    if (l1 > 0 && path[l1 - 1] == L'\\')
        l1--;
    int l2 = (int)Path.size();
    if (l2 > 0 && Path[l2 - 1] == L'\\')
        l2--;
    if (l1 == l2 && StrNICmpW(path, Path.c_str(), l1) == 0)
    {
        if (TopIndexesCount > 0)
        {
            size_t len = Path.size();
            if (len > 0 && Path[len - 1] == L'\\')
                len--;
            const size_t separator = len > 0 ? Path.rfind(L'\\', len - 1) : std::wstring::npos;
            if (separator == std::wstring::npos)
                Path.clear();
            else
                Path.resize(separator);
            topIndex = TopIndexes[--TopIndexesCount];
            return TRUE;
        }
        else // we don't have this value anymore (wasn't saved or small memory->was discarded)
        {
            Clear();
            return FALSE;
        }
    }
    else // query for different path -> clear memory, long jump occurred
    {
        Clear();
        return FALSE;
    }
}

//*****************************************************************************

CFileHistory::CFileHistory()
    : Files(10, 10)
{
}

void CFileHistory::ClearHistory()
{
    Files.DestroyMembers();
}

BOOL CFileHistory::AddFile(CFileHistoryItemTypeEnum type, DWORD handlerID, const wchar_t* fileName)
{
    CALL_STACK_MESSAGE4("CFileHistory::AddFile(%d, %u, %ls)", type, handlerID, fileName);

    // search existing items to see if item being added already exists
    int i;
    for (i = 0; i < Files.Count; i++)
    {
        CFileHistoryItem* item = Files[i];
        if (item->Equal(type, handlerID, fileName))
        {
            // if yes, just pull it to top position
            if (i > 0)
            {
                Files.Detach(i);
                if (!Files.IsGood())
                    Files.ResetState(); // can't fail, only reports lack of memory for array shrinking
                Files.Insert(0, item);
                if (!Files.IsGood())
                {
                    Files.ResetState();
                    delete item;
                    return FALSE;
                }
            }
            return TRUE;
        }
    }

    // item doesn't exist - insert it at top position
    CFileHistoryItem* item = new CFileHistoryItem(type, handlerID, fileName);
    if (item == NULL)
    {
        TRACE_E(LOW_MEMORY);
        return FALSE;
    }
    if (!item->IsGood())
    {
        delete item;
        return FALSE;
    }
    Files.Insert(0, item);
    if (!Files.IsGood())
    {
        Files.ResetState();
        delete item;
        return FALSE;
    }
    // cut to 30 items
    if (Files.Count > 30)
        Files.Delete(30);

    return TRUE;
}

BOOL CFileHistory::FillPopupMenu(CMenuPopup* popup)
{
    CALL_STACK_MESSAGE1("CFileHistory::FillPopupMenu()");

    // fill items
    MENU_ITEM_INFO mii;
    mii.Mask = MENU_MASK_TYPE | MENU_MASK_ID | MENU_MASK_ICON | MENU_MASK_STRING;
    mii.Type = MENU_TYPE_STRING;
    int i;
    for (i = 0; i < Files.Count; i++)
    {
        CFileHistoryItem* item = Files[i];

        // separate name from path with '\t' character - it will be in separate column
        std::wstring name = item->FileName;
        const size_t separator = name.rfind(L'\\');
        if (separator == std::wstring::npos)
            return FALSE;
        name.insert(separator + 1, 1, L'\t');
        const wchar_t* text = L"";
        // double '&' so it doesn't display as underline
        for (size_t pos = 0; pos < name.size(); pos++)
        {
            if (name[pos] == L'&')
            {
                name.insert(pos, 1, L'&');
                pos++;
            }
        }

        mii.HIcon = item->HIcon;
        switch (item->Type)
        {
        case fhitView:
            text = LoadStrW(IDS_FILEHISTORY_VIEW);
            break;
        case fhitEdit:
            text = LoadStrW(IDS_FILEHISTORY_EDIT);
            break;
        case fhitOpen:
            text = LoadStrW(IDS_FILEHISTORY_OPEN);
            break;
        default:
            TRACE_E("Unknown Type=" << item->Type);
        }
        name += FormatStrW(L"\t(%ls)", text); // append way file is opened
        mii.String = name.data();
        mii.ID = i + 1;
        popup->InsertItem(-1, TRUE, &mii);
    }
    if (i > 0)
    {
        popup->SetStyle(MENU_POPUP_THREECOLUMNS); // first two columns are left-aligned
        popup->AssignHotKeys();
    }
    return TRUE;
}

BOOL CFileHistory::Execute(int index)
{
    CALL_STACK_MESSAGE2("CFileHistory::Execute(%d)", index);
    if (index < 1 || index > Files.Count)
    {
        TRACE_E("Index is out of range");
        return FALSE;
    }
    return Files[index - 1]->Execute();
    return TRUE;
}

BOOL CFileHistory::HasItem()
{
    return Files.Count > 0;
}

//****************************************************************************
//
// Directory editline/combobox support
//

#define DIRECTORY_COMMAND_BROWSE 1    // browse directory
#define DIRECTORY_COMMAND_LEFT 3      // path from left panel
#define DIRECTORY_COMMAND_RIGHT 4     // path from right panel
#define DIRECTORY_COMMAND_HOTPATHF 5  // first hot path
#define DIRECTORY_COMMAND_HOTPATHL 35 // last hot path

// 2026-08-25: the narrow SetEditOrComboText(char*) was deleted - confirmed-dead
// (zero callers anywhere; it was never even forward-declared in a header). The wide twin below
// is the sole surviving, actually-called implementation.
//
// Same combo-vs-plain-edit class detection as the deleted narrow form, but through SendMessageW -
// the target EDIT/COMBOBOX control is Unicode-capable via USER32 regardless of the parent
// dialog's own width (see winliblt.h's EditLine).
BOOL SetEditOrComboTextW(HWND hWnd, const wchar_t* text)
{
    wchar_t className[31];
    className[0] = 0;
    if (GetClassNameW(hWnd, className, 30) == 0)
    {
        TRACE_E("GetClassName failed on hWnd=0x" << hWnd);
        return FALSE;
    }

    HWND hEdit;
    if (StrICmpW(className, L"edit") != 0)
    {
        hEdit = GetWindow(hWnd, GW_CHILD);
        if (hEdit == NULL ||
            GetClassNameW(hEdit, className, 30) == 0 ||
            StrICmpW(className, L"edit") != 0)
        {
            TRACE_E("Edit window was not found hWnd=0x" << hWnd);
            return FALSE;
        }
    }
    else
        hEdit = hWnd;

    SendMessageW(hEdit, WM_SETTEXT, 0, (LPARAM)text);
    SendMessageW(hEdit, EM_SETSEL, 0, lstrlenW(text));
    return TRUE;
}

DWORD TrackDirectoryMenu(HWND hDialog, int buttonID, BOOL selectMenuItem)
{
    RECT r;
    GetWindowRect(GetDlgItem(hDialog, buttonID), &r);

    CMenuPopup popup;
    MENU_ITEM_INFO mii;
    mii.Mask = MENU_MASK_TYPE | MENU_MASK_ID | MENU_MASK_STRING | MENU_MASK_STATE;
    mii.Type = MENU_TYPE_STRING;
    mii.State = 0;

    MENU_ITEM_INFO miiSep;
    miiSep.Mask = MENU_MASK_TYPE;
    miiSep.Type = MENU_TYPE_SEPARATOR;

    /* used by export_mnu.py script which generates salmenu.mnu for Translator
   keep synchronized with InsertItem() calls below...
MENU_TEMPLATE_ITEM CopyMoveBrowseMenu[] = 
{
  {MNTT_PB, 0
  {MNTT_IT, IDS_PATHMENU_BROWSE
  {MNTT_IT, IDS_PATHMENU_LEFT
  {MNTT_IT, IDS_PATHMENU_RIGHT
  {MNTT_PE, 0
};
*/

    mii.ID = DIRECTORY_COMMAND_BROWSE;
    mii.String = LoadStrW(IDS_PATHMENU_BROWSE);
    popup.InsertItem(0xFFFFFFFF, TRUE, &mii);

    //  mii.ID = 2;
    //  mii.String = "Tree...\tCtrl+T";
    //  popup.InsertItem(0xFFFFFFFF, TRUE, &mii);

    popup.InsertItem(0xFFFFFFFF, TRUE, &miiSep);

    mii.ID = DIRECTORY_COMMAND_LEFT;
    mii.String = LoadStrW(IDS_PATHMENU_LEFT);
    popup.InsertItem(0xFFFFFFFF, TRUE, &mii);

    mii.ID = DIRECTORY_COMMAND_RIGHT;
    mii.String = LoadStrW(IDS_PATHMENU_RIGHT);
    popup.InsertItem(0xFFFFFFFF, TRUE, &mii);

    // attach hotpaths if they exist
    DWORD firstID = DIRECTORY_COMMAND_HOTPATHF;
    MainWindow->HotPaths.FillHotPathsMenu(&popup, firstID, FALSE, FALSE, FALSE, TRUE);

    DWORD flags = MENU_TRACK_RETURNCMD;
    if (selectMenuItem)
    {
        popup.SetSelectedItemIndex(0);
        flags |= MENU_TRACK_SELECT;
    }
    return popup.Track(flags, r.right, r.top, hDialog, &r);
}

DWORD OnKeyDownHandleSelectAll(DWORD keyCode, HWND hDialog, int editID)
{
    // from Windows Vista SelectAll works natively, so leave select all to them
    if (WindowsVistaAndLater)
        return FALSE;

    BOOL controlPressed = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    BOOL altPressed = (GetKeyState(VK_MENU) & 0x8000) != 0;
    BOOL shiftPressed = (GetKeyState(VK_SHIFT) & 0x8000) != 0;

    if (controlPressed && !shiftPressed && !altPressed)
    {
        if (keyCode == 'A')
        {
            // select all
            HWND hChild = GetDlgItem(hDialog, editID);
            if (hChild != NULL)
            {
                wchar_t className[30];
                GetClassNameW(hChild, className, 29);
                className[29] = 0;
                BOOL combo = (StrICmpW(className, L"combobox") == 0);
                if (combo)
                    SendMessage(hChild, CB_SETEDITSEL, 0, MAKELPARAM(0, -1));
                else
                    SendMessage(hChild, EM_SETSEL, 0, -1);
                return TRUE;
            }
        }
    }
    return FALSE;
}

void InvokeDirectoryMenuCommand(DWORD cmd, HWND hDialog, int editID, int editBufSize);
void InvokeDirectoryMenuCommandW(DWORD cmd, HWND hDialog, int editID, HWND hUnicodeCtrl);

void OnDirectoryButton(HWND hDialog, int editID, int editBufSize, int buttonID, WPARAM wParam, LPARAM lParam)
{
    BOOL selectMenuItem = LOWORD(lParam);
    DWORD cmd = TrackDirectoryMenu(hDialog, buttonID, selectMenuItem);
    InvokeDirectoryMenuCommand(cmd, hDialog, editID, editBufSize);
}

void OnDirectoryButtonW(HWND hDialog, int editID, int buttonID, WPARAM wParam, LPARAM lParam, HWND hUnicodeCtrl)
{
    BOOL selectMenuItem = LOWORD(lParam);
    DWORD cmd = TrackDirectoryMenu(hDialog, buttonID, selectMenuItem);
    InvokeDirectoryMenuCommandW(cmd, hDialog, editID, hUnicodeCtrl);
}

DWORD OnDirectoryKeyDown(DWORD keyCode, HWND hDialog, int editID, int editBufSize, int buttonID)
{
    BOOL controlPressed = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    BOOL altPressed = (GetKeyState(VK_MENU) & 0x8000) != 0;
    BOOL shiftPressed = (GetKeyState(VK_SHIFT) & 0x8000) != 0;

    if (!controlPressed && !shiftPressed && altPressed && keyCode == VK_RIGHT)
    {
        OnDirectoryButton(hDialog, editID, editBufSize, buttonID, MAKELPARAM(buttonID, 0), MAKELPARAM(TRUE, 0));
        return TRUE;
    }
    if (controlPressed && !shiftPressed && !altPressed)
    {
        switch (keyCode)
        {
        case 'B':
        {
            InvokeDirectoryMenuCommand(DIRECTORY_COMMAND_BROWSE, hDialog, editID, editBufSize);
            return TRUE;
        }

        case 219: // '['
        case 221: // ']'
        {
            InvokeDirectoryMenuCommand((keyCode == 219) ? DIRECTORY_COMMAND_LEFT : DIRECTORY_COMMAND_RIGHT, hDialog, editID, editBufSize);
            return TRUE;
        }

        case '1':
        case '2':
        case '3':
        case '4':
        case '5':
        case '6':
        case '7':
        case '8':
        case '9':
        case '0':
        {
            int index = keyCode == '0' ? 9 : keyCode - '1';
            InvokeDirectoryMenuCommand(DIRECTORY_COMMAND_HOTPATHF + index, hDialog, editID, editBufSize);
            return TRUE;
        }
        }
    }
    return FALSE;
}

DWORD OnDirectoryKeyDownW(DWORD keyCode, HWND hDialog, int editID, int buttonID, HWND hUnicodeCtrl)
{
    BOOL controlPressed = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    BOOL altPressed = (GetKeyState(VK_MENU) & 0x8000) != 0;
    BOOL shiftPressed = (GetKeyState(VK_SHIFT) & 0x8000) != 0;

    if (!controlPressed && !shiftPressed && altPressed && keyCode == VK_RIGHT)
    {
        OnDirectoryButtonW(hDialog, editID, buttonID, MAKELPARAM(buttonID, 0), MAKELPARAM(TRUE, 0), hUnicodeCtrl);
        return TRUE;
    }
    if (controlPressed && !shiftPressed && !altPressed)
    {
        switch (keyCode)
        {
        case 'B':
            InvokeDirectoryMenuCommandW(DIRECTORY_COMMAND_BROWSE, hDialog, editID, hUnicodeCtrl);
            return TRUE;
        case 219:
        case 221:
            InvokeDirectoryMenuCommandW((keyCode == 219) ? DIRECTORY_COMMAND_LEFT : DIRECTORY_COMMAND_RIGHT, hDialog, editID, hUnicodeCtrl);
            return TRUE;
        case '1':
        case '2':
        case '3':
        case '4':
        case '5':
        case '6':
        case '7':
        case '8':
        case '9':
        case '0':
        {
            int index = keyCode == '0' ? 9 : keyCode - '1';
            InvokeDirectoryMenuCommandW(DIRECTORY_COMMAND_HOTPATHF + index, hDialog, editID, hUnicodeCtrl);
            return TRUE;
        }
        }
    }
    return FALSE;
}

void InvokeDirectoryMenuCommand(DWORD cmd, HWND hDialog, int editID, int /*editBufSize*/)
{
    InvokeDirectoryMenuCommandW(cmd, hDialog, editID, NULL);
}

void InvokeDirectoryMenuCommandW(DWORD cmd, HWND hDialog, int editID, HWND hUnicodeCtrl)
{
    std::wstring pathW;
    BOOL setPathToEdit = FALSE;
    switch (cmd)
    {
    case 0:
        return;

    case DIRECTORY_COMMAND_BROWSE:
    {
        pathW = GetWindowTextStringW(hUnicodeCtrl != NULL ? hUnicodeCtrl : GetDlgItem(hDialog, editID));
        const std::wstring caption = GetWindowTextStringW(hDialog);
        if (GetTargetDirectoryW(hDialog, hDialog, caption.c_str(), LoadStrW(IDS_BROWSETARGETDIRECTORY), pathW, FALSE, pathW.c_str()))
            setPathToEdit = TRUE;
        break;
    }

    case DIRECTORY_COMMAND_LEFT:
    case DIRECTORY_COMMAND_RIGHT:
    {
        CFilesWindow* panel = (cmd == DIRECTORY_COMMAND_LEFT) ? MainWindow->LeftPanel : MainWindow->RightPanel;
        if (panel != NULL && panel->GetGeneralPath(pathW, TRUE))
            setPathToEdit = TRUE;
        break;
    }

    default:
    {
        if (cmd >= DIRECTORY_COMMAND_HOTPATHF && cmd <= DIRECTORY_COMMAND_HOTPATHL)
        {
            if (MainWindow->GetExpandedHotPath(hDialog, cmd - DIRECTORY_COMMAND_HOTPATHF, pathW))
            {
                setPathToEdit = TRUE;
            }
        }
        else
            TRACE_E("Unknown cmd=" << cmd);
    }
    }

    if (setPathToEdit)
    {
        if (hUnicodeCtrl != NULL)
            SetWindowTextW(hUnicodeCtrl, pathW.c_str());
        else
            SetWindowTextW(GetDlgItem(hDialog, editID), pathW.c_str());
    }
}

//****************************************************************************
//
// CKeyForwarder
//

class CKeyForwarder : public CWindow
{
protected:
    BOOL SkipCharacter; // prevents beeping for processed keys
    HWND HDialog;       // dialog where we'll send WM_USER_KEYDOWN
    int CtrlID;         // for WM_USER_KEYDOWN

public:
    CKeyForwarder(HWND hDialog, int ctrlID, CObjectOrigin origin = ooAllocated);

protected:
    virtual LRESULT WindowProc(UINT uMsg, WPARAM wParam, LPARAM lParam);
};

// Attach as a Unicode subclass unconditionally. There is nothing to opt out of:
// WindowProc below never inspects a character. WM_CHAR only consults SkipCharacter,
// and WM_KEYDOWN forwards a virtual-key code - both identical under A and W. So
// the subclass must not downgrade a Unicode edit/combo child.
CKeyForwarder::CKeyForwarder(HWND hDialog, int ctrlID, CObjectOrigin origin)
    : CWindow(origin)
{
    SkipCharacter = FALSE;
    HDialog = hDialog;
    CtrlID = ctrlID;
}

LRESULT
CKeyForwarder::WindowProc(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    CALL_STACK_MESSAGE4("CKeyForwarder::WindowProc(0x%X, 0x%IX, 0x%IX)", uMsg, wParam, lParam);
    switch (uMsg)
    {
    case WM_CHAR:
    {
        if (SkipCharacter)
        {
            SkipCharacter = FALSE;
            return 0;
        }
        break;
    }

    case WM_SYSKEYDOWN:
    case WM_KEYDOWN:
    {
        SkipCharacter = TRUE; // prevent beeping
        BOOL ret = (BOOL)SendMessage(HDialog, WM_USER_KEYDOWN, MAKELPARAM(CtrlID, 0), wParam);
        if (ret)
            return 0;
        SkipCharacter = FALSE;
        break;
    }

    case WM_SYSKEYUP:
    case WM_KEYUP:
    {
        SkipCharacter = FALSE; // just in case
        break;
    }
    }
    return CWindow::WindowProc(uMsg, wParam, lParam);
}

BOOL CreateKeyForwarder(HWND hDialog, int ctrlID)
{
    HWND hWindow = GetDlgItem(hDialog, ctrlID);
    wchar_t className[31];
    className[0] = 0;
    if (GetClassNameW(hWindow, className, 30) == 0 || StrICmpW(className, L"edit") != 0)
    {
        // it might be a combobox, try to reach for inner edit
        hWindow = GetWindow(hWindow, GW_CHILD);
        if (hWindow == NULL || GetClassNameW(hWindow, className, 30) == 0 || StrICmpW(className, L"edit") != 0)
        {
            TRACE_E("CreateKeyForwarder: edit window was not found ClassName is " << sally::diagnostic::EncodeAcpLossy(className));
            return FALSE;
        }
    }

    CKeyForwarder* edit = new CKeyForwarder(hDialog, ctrlID);
    if (edit != NULL)
    {
        edit->AttachToWindow(hWindow);
        return TRUE;
    }
    return FALSE;
}
