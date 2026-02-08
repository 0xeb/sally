// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-License-Identifier: GPL-2.0-or-later
// CommentsTranslationProject: TRANSLATED

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
#include "ui/IPrompter.h"
#include "common/IFileSystem.h"
#include "common/IEnvironment.h"
#include "common/unicode/helpers.h"

CUserMenuIconBkgndReader UserMenuIconBkgndReader;

// ****************************************************************************

BOOL SalPathAppend(char* path, const char* name, int pathSize)
{
    if (path == NULL || name == NULL)
    {
        TRACE_E("Unexpected situation in SalPathAppend()");
        return FALSE;
    }
    if (*name == '\\')
        name++;
    int l = (int)strlen(path);
    if (l > 0 && path[l - 1] == '\\')
        l--;
    if (*name != 0)
    {
        int n = (int)strlen(name);
        if (l + 1 + n < pathSize) // do we fit including the null terminator?
        {
            if (l != 0)
                path[l] = '\\';
            else
                l = -1;
            memcpy(path + l + 1, name, n + 1);
        }
        else
            return FALSE;
    }
    else
        path[l] = 0;
    return TRUE;
}

// ****************************************************************************

BOOL SalPathAddBackslash(char* path, int pathSize)
{
    if (path == NULL)
    {
        TRACE_E("Unexpected situation in SalPathAddBackslash()");
        return FALSE;
    }
    int l = (int)strlen(path);
    if (l > 0 && path[l - 1] != '\\')
    {
        if (l + 1 < pathSize)
        {
            path[l] = '\\';
            path[l + 1] = 0;
        }
        else
            return FALSE;
    }
    return TRUE;
}

// Wide version - appends name to path (modifies path in-place)
// Handles leading/trailing backslashes properly
void SalPathAppendW(std::wstring& path, const wchar_t* name)
{
    if (name == nullptr)
        return;
    
    // Skip leading backslash in name
    if (*name == L'\\')
        name++;
    
    // Remove trailing backslash from path
    if (!path.empty() && path.back() == L'\\')
        path.pop_back();
    
    // Append name if non-empty
    if (*name != L'\0')
    {
        if (!path.empty())
            path += L'\\';
        path += name;
    }
}

// Wide version - ensures path ends with backslash
void SalPathAddBackslashW(std::wstring& path)
{
    if (!path.empty() && path.back() != L'\\')
        path += L'\\';
}

// Wide version - removes trailing backslash
void SalPathRemoveBackslashW(std::wstring& path)
{
    if (!path.empty() && path.back() == L'\\')
        path.pop_back();
}

// Wide version - strips path leaving just filename
// "C:\foo\bar.txt" -> "bar.txt", "bar.txt" -> "bar.txt"
void SalPathStripPathW(std::wstring& path)
{
    size_t pos = path.rfind(L'\\');
    if (pos != std::wstring::npos)
        path = path.substr(pos + 1);
}

// Wide version - finds filename portion of path
// Returns pointer within the string to the filename part
const wchar_t* SalPathFindFileNameW(const wchar_t* path)
{
    if (path == nullptr)
        return nullptr;

    const wchar_t* result = path;
    for (const wchar_t* p = path; *p != L'\0'; p++)
    {
        if (*p == L'\\')
            result = p + 1;
    }
    return result;
}

// Wide version - removes extension from path
// "C:ooar.txt" -> "C:ooar"
void SalPathRemoveExtensionW(std::wstring& path)
{
    size_t len = path.length();
    for (size_t i = len; i > 0; i--)
    {
        if (path[i - 1] == L'.')
        {
            path.resize(i - 1);
            return;
        }
        if (path[i - 1] == L'\\')
            return;  // No extension found
    }
}

// Wide version - adds extension if not already present
// Returns true if extension was added or already exists
bool SalPathAddExtensionW(std::wstring& path, const wchar_t* extension)
{
    if (extension == nullptr)
        return false;

    size_t len = path.length();
    for (size_t i = len; i > 0; i--)
    {
        if (path[i - 1] == L'.')
            return true;  // Extension already exists
        if (path[i - 1] == L'\\')
            break;  // No extension, add it
    }
    path += extension;
    return true;
}

// Wide version - replaces extension (or adds if none)
// "C:ooar.txt" + ".bak" -> "C:ooar.bak"
bool SalPathRenameExtensionW(std::wstring& path, const wchar_t* extension)
{
    if (extension == nullptr)
        return false;

    size_t len = path.length();
    for (size_t i = len; i > 0; i--)
    {
        if (path[i - 1] == L'.')
        {
            path.resize(i - 1);
            break;
        }
        if (path[i - 1] == L'\\')
            break;  // No existing extension
    }
    path += extension;
    return true;
}


// ****************************************************************************

void SalPathRemoveBackslash(char* path)
{
    if (path == NULL)
    {
        TRACE_E("Unexpected situation in SalPathRemoveBackslash()");
        return;
    }
    int l = (int)strlen(path);
    if (l > 0 && path[l - 1] == '\\')
        path[l - 1] = 0;
}

void SalPathStripPath(char* path)
{
    if (path == NULL)
    {
        TRACE_E("Unexpected situation in SalPathStripPath()");
        return;
    }
    char* name = strrchr(path, '\\');
    if (name != NULL)
        memmove(path, name + 1, strlen(name + 1) + 1);
}

void SalPathRemoveExtension(char* path)
{
    if (path == NULL)
    {
        TRACE_E("Unexpected situation in SalPathRemoveExtension()");
        return;
    }

    int len = (int)strlen(path);
    char* iterator = path + len - 1;
    while (iterator >= path)
    {
        if (*iterator == '.')
        {
            //      if (iterator != path && *(iterator - 1) != '\\')  // ".cvspass" in Windows is an extension ...
            *iterator = 0;
            break;
        }
        if (*iterator == '\\')
            break;
        iterator--;
    }
}

BOOL SalPathAddExtension(char* path, const char* extension, int pathSize)
{
    if (path == NULL || extension == NULL)
    {
        TRACE_E("Unexpected situation in SalPathAddExtension()");
        return FALSE;
    }

    int len = (int)strlen(path);
    char* iterator = path + len - 1;
    while (iterator >= path)
    {
        if (*iterator == '.')
        {
            //      if (iterator != path && *(iterator - 1) != '\\')  // ".cvspass" in Windows is an extension ...
            return TRUE; // extension already exists
                         //      break;  // no point in searching further
        }
        if (*iterator == '\\')
            break;
        iterator--;
    }

    int extLen = (int)strlen(extension);
    if (len + extLen < pathSize)
    {
        memcpy(path + len, extension, extLen + 1);
        return TRUE;
    }
    else
        return FALSE;
}

BOOL SalPathRenameExtension(char* path, const char* extension, int pathSize)
{
    if (path == NULL || extension == NULL)
    {
        TRACE_E("Unexpected situation in SalPathRenameExtension()");
        return FALSE;
    }

    int len = (int)strlen(path);
    char* iterator = path + len - 1;
    while (iterator >= path)
    {
        if (*iterator == '.')
        {
            //      if (iterator != path && *(iterator - 1) != '\\')  // ".cvspass" in Windows is an extension ...
            //      {
            len = (int)(iterator - path);
            break; // extension already exists -> overwrite it
                   //      }
                   //      break;
        }
        if (*iterator == '\\')
            break;
        iterator--;
    }

    int extLen = (int)strlen(extension);
    if (len + extLen < pathSize)
    {
        memcpy(path + len, extension, extLen + 1);
        return TRUE;
    }
    else
        return FALSE;
}

const char* SalPathFindFileName(const char* path)
{
    if (path == NULL)
    {
        TRACE_E("Unexpected situation in SalPathFindFileName()");
        return NULL;
    }

    int len = (int)strlen(path);
    const char* iterator = path + len - 2;
    while (iterator >= path)
    {
        if (*iterator == '\\')
            return iterator + 1;
        iterator--;
    }
    return path;
}

// ****************************************************************************

// Wide version - creates temp file/directory and returns its path
// Returns empty string on failure (sets LastError)
std::wstring SalGetTempFileNameW(const wchar_t* path, const wchar_t* prefix, bool file)
{
    std::wstring tmpDir;
    tmpDir.reserve(32768); // SAL_MAX_LONG_PATH
    
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
            HANDLE h = CreateFileW(tmpDir.c_str(), GENERIC_WRITE, 0, NULL, CREATE_NEW,
                                   FILE_ATTRIBUTE_NORMAL, NULL);
            if (h != INVALID_HANDLE_VALUE)
            {
                CloseHandle(h);
                return tmpDir;
            }
        }
        else
        {
            if (CreateDirectoryW(tmpDir.c_str(), NULL))
            {
                return tmpDir;
            }
        }
        
        DWORD err = GetLastError();
        if (err != ERROR_FILE_EXISTS && err != ERROR_ALREADY_EXISTS)
        {
            TRACE_E("Unable to create temporary " << (file ? "file" : "directory") << ": " << GetErrorText(err));
            SetLastError(err);
            return L"";
        }
    }
}

BOOL SalGetTempFileName(const char* path, const char* prefix, char* tmpName, BOOL file)
{
    // Thin wrapper - calls wide version and converts result
    std::wstring result = SalGetTempFileNameW(
        path ? AnsiToWide(path).c_str() : nullptr,
        AnsiToWide(prefix).c_str(),
        file != FALSE);
    
    if (result.empty())
        return FALSE;
    
    // Convert result back to ANSI (truncates if > MAX_PATH)
    std::string ansiResult = WideToAnsi(result);
    if (ansiResult.length() >= MAX_PATH)
    {
        TRACE_E("Temp file path too long for ANSI buffer");
        SetLastError(ERROR_BUFFER_OVERFLOW);
        return FALSE;
    }
    
    strcpy(tmpName, ansiResult.c_str());
    return TRUE;
}

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

BOOL SalRemovePointsFromPath(char* afterRoot)
{
    char* d = afterRoot; // pointer after root path
    while (*d != 0)
    {
        while (*d != 0 && *d != '.')
            d++;
        if (*d == '.')
        {
            if (d == afterRoot || d > afterRoot && *(d - 1) == '\\') // '.' after root path or "\."
            {
                if (*(d + 1) == '.' && (*(d + 2) == '\\' || *(d + 2) == 0)) // ".."
                {
                    char* l = d - 1;
                    while (l > afterRoot && *(l - 1) != '\\')
                        l--;
                    if (l >= afterRoot) // remove directory + ".."
                    {
                        if (*(d + 2) == 0)
                            *l = 0;
                        else
                            memmove(l, d + 3, strlen(d + 3) + 1);
                        d = l;
                    }
                    else
                        return FALSE; // ".." cannot be removed
                }
                else
                {
                    if (*(d + 1) == '\\' || *(d + 1) == 0) // "."
                    {
                        if (*(d + 1) == 0)
                            *d = 0;
                        else
                            memmove(d, d + 2, strlen(d + 2) + 1);
                    }
                    else
                        d++;
                }
            }
            else
                d++;
        }
    }
    return TRUE;
}

BOOL SalRemovePointsFromPath(WCHAR* afterRoot)
{
    WCHAR* d = afterRoot; // pointer after root path
    while (*d != 0)
    {
        while (*d != 0 && *d != L'.')
            d++;
        if (*d == L'.')
        {
            if (d == afterRoot || d > afterRoot && *(d - 1) == L'\\') // '.' after root path or "\."
            {
                if (*(d + 1) == L'.' && (*(d + 2) == L'\\' || *(d + 2) == 0)) // ".."
                {
                    WCHAR* l = d - 1;
                    while (l > afterRoot && *(l - 1) != L'\\')
                        l--;
                    if (l >= afterRoot) // remove directory + ".."
                    {
                        if (*(d + 2) == 0)
                            *l = 0;
                        else
                            memmove(l, d + 3, sizeof(WCHAR) * (lstrlenW(d + 3) + 1));
                        d = l;
                    }
                    else
                        return FALSE; // ".." cannot be removed
                }
                else
                {
                    if (*(d + 1) == L'\\' || *(d + 1) == 0) // "."
                    {
                        if (*(d + 1) == 0)
                            *d = 0;
                        else
                            memmove(d, d + 2, sizeof(WCHAR) * (lstrlenW(d + 2) + 1));
                    }
                    else
                        d++;
                }
            }
            else
                d++;
        }
    }
    return TRUE;
}

BOOL SalGetFullName(char* name, int* errTextID, const char* curDir, char* nextFocus,
                    BOOL* callNethood, int nameBufSize, BOOL allowRelPathWithSpaces)
{
    CALL_STACK_MESSAGE5("SalGetFullName(%s, , %s, , , %d, %d)", name, curDir, nameBufSize, allowRelPathWithSpaces);
    int err = 0;

    int rootOffset = 3; // offset of directory part start (3 for "c:\path")
    char* s = name;
    while (*s >= 1 && *s <= ' ')
        s++;
    if (*s == '\\' && *(s + 1) == '\\') // UNC (\\server\share\...)
    {                                   // eliminate spaces at the beginning of the path
        if (s != name)
            memmove(name, s, strlen(s) + 1);
        s = name + 2;
        if (*s == '.' || *s == '?')
            err = IDS_PATHISINVALID; // paths like \\?\Volume{6e76293d-1828-11df-8f3c-806e6f6e6963}\ and \\.\PhysicalDisk5\ are simply not supported here...
        else
        {
            if (*s == 0 || *s == '\\')
            {
                if (callNethood != NULL)
                    *callNethood = *s == 0;
                err = IDS_SERVERNAMEMISSING;
            }
            else
            {
                while (*s != 0 && *s != '\\')
                    s++; // prejeti servername
                if (*s == '\\')
                    s++;
                if (s - name > nameBufSize - 1)
                    err = IDS_SERVERNAMEMISSING; // found text is too long to be a server
                else
                {
                    if (*s == 0 || *s == '\\')
                    {
                        if (callNethood != NULL)
                            *callNethood = *s == 0 && (*(s - 1) != '.' || *(s - 2) != '\\') && (*(s - 1) != '\\' || *(s - 2) != '.' || *(s - 3) != '\\'); // not "\\." or "\\.\" (beginning of path like "\\.\C:\")
                        err = IDS_SHARENAMEMISSING;
                    }
                    else
                    {
                        while (*s != 0 && *s != '\\')
                            s++; // prejeti sharename
                        if ((s - name) + 1 > nameBufSize - 1)
                            err = IDS_SHARENAMEMISSING; // found text is too long to be a share (+1 for trailing backslash)
                        if (*s == '\\')
                            s++;
                    }
                }
            }
        }
    }
    else // path specified using drive (c:\...)
    {
        if (*s != 0)
        {
            if (*(s + 1) == ':') // "c:..."
            {
                if (*(s + 2) == '\\') // "c:\..."
                {                     // eliminate spaces at the beginning of the path
                    if (s != name)
                        memmove(name, s, strlen(s) + 1);
                }
                else // "c:path..."
                {
                    int l1 = (int)strlen(s + 2); // length of remainder ("path...")
                    if (LowerCase[*s] >= 'a' && LowerCase[*s] <= 'z')
                    {
                        const char* head;
                        if (curDir != NULL && LowerCase[curDir[0]] == LowerCase[*s])
                            head = curDir;
                        else
                            head = DefaultDir[LowerCase[*s] - 'a'];
                        int l2 = (int)strlen(head);
                        if (head[l2 - 1] != '\\')
                            l2++; // space for '\\'
                        if (l1 + l2 >= nameBufSize)
                            err = IDS_TOOLONGPATH;
                        else // construct full path
                        {
                            memmove(name + l2, s + 2, l1 + 1);
                            *(name + l2 - 1) = '\\';
                            memmove(name, head, l2 - 1);
                        }
                    }
                    else
                        err = IDS_INVALIDDRIVE;
                }
            }
            else
            {
                if (curDir != NULL)
                {
                    // for relative paths without '\\' at the beginning, with 'allowRelPathWithSpaces' enabled, we won't consider
                    // spaces as an error (directory and file names can start with a space, even though Windows and other software
                    // including Salam try to prevent it)
                    if (allowRelPathWithSpaces && *s != '\\')
                        s = name;
                    int l1 = (int)strlen(s);
                    if (*s == '\\') // "\path...."
                    {
                        if (curDir[0] == '\\' && curDir[1] == '\\') // UNC
                        {
                            const char* root = curDir + 2;
                            while (*root != 0 && *root != '\\')
                                root++;
                            root++; // '\\'
                            while (*root != 0 && *root != '\\')
                                root++;
                            if (l1 + (root - curDir) >= nameBufSize)
                                err = IDS_TOOLONGPATH;
                            else // construct path from current disk root
                            {
                                memmove(name + (root - curDir), s, l1 + 1);
                                memmove(name, curDir, root - curDir);
                            }
                            rootOffset = (int)(root - curDir) + 1;
                        }
                        else
                        {
                            if (l1 + 2 >= nameBufSize)
                                err = IDS_TOOLONGPATH;
                            else
                            {
                                memmove(name + 2, s, l1 + 1);
                                name[0] = curDir[0];
                                name[1] = ':';
                            }
                        }
                    }
                    else // "path..."
                    {
                        if (nextFocus != NULL)
                        {
                            char* test = name;
                            while (*test != 0 && *test != '\\')
                                test++;
                            if (*test == 0 && (int)strlen(name) < MAX_PATH)
                                strcpy(nextFocus, name);
                        }

                        int l2 = (int)strlen(curDir);
                        if (curDir[l2 - 1] != '\\')
                            l2++;
                        if (l1 + l2 >= nameBufSize)
                            err = IDS_TOOLONGPATH;
                        else
                        {
                            memmove(name + l2, s, l1 + 1);
                            name[l2 - 1] = '\\';
                            memmove(name, curDir, l2 - 1);
                        }
                    }
                }
                else
                    err = IDS_INCOMLETEFILENAME;
            }
            s = name + rootOffset;
        }
        else
        {
            name[0] = 0;
            err = IDS_EMPTYNAMENOTALLOWED;
        }
    }

    if (err == 0) // eliminate '.' and '..' in path
    {
        if (!SalRemovePointsFromPath(s))
            err = IDS_PATHISINVALID;
    }

    if (err == 0) // remove any unwanted backslash from end of string
    {
        int l = (int)strlen(name);
        if (l > 1 && name[1] == ':') // path type "c:\path"
        {
            if (l > 3) // not a root path
            {
                if (name[l - 1] == '\\')
                    name[l - 1] = 0; // trim backslash
            }
            else
            {
                name[2] = '\\'; // root path, backslash required ("c:\")
                name[3] = 0;
            }
        }
        else
        {
            if (name[0] == '\\' && name[1] == '\\' && name[2] == '.' && name[3] == '\\' && name[4] != 0 && name[5] == ':') // path type "\\.\C:\"
            {
                if (l > 7) // not a root path
                {
                    if (name[l - 1] == '\\')
                        name[l - 1] = 0; // trim backslash
                }
                else
                {
                    name[6] = '\\'; // root path, backslash required ("\\.\C:\")
                    name[7] = 0;
                }
            }
            else // UNC path
            {
                if (l > 0 && name[l - 1] == '\\')
                    name[l - 1] = 0; // trim backslash
            }
        }
    }

    if (errTextID != NULL)
        *errTextID = err;

    return err == 0;
}

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

void _RemoveTemporaryDir(const char* dir)
{
    CPathBuffer path;
    WIN32_FIND_DATA file;
    strcpy(path, dir);
    char* end = path + strlen(path);
    if (*(end - 1) != '\\')
        *end++ = '\\';
    strcpy(end, "*");
    HANDLE find = SalFindFirstFileH(path, &file);
    if (find != INVALID_HANDLE_VALUE)
    {
        do
        {
            if (file.cFileName[0] != 0 && strcmp(file.cFileName, "..") && strcmp(file.cFileName, ".") &&
                (end - (char*)path) + strlen(file.cFileName) < path.Size() - 2)
            {
                strcpy(end, file.cFileName);
                ClearReadOnlyAttr(path, file.dwFileAttributes);
                if (file.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
                    _RemoveTemporaryDir(path);
                else
                    gFileSystem->DeleteFile(AnsiToWide(path).c_str());
            }
        } while (FindNextFile(find, &file));
        HANDLES(FindClose(find));
    }
    *(end - 1) = 0;
    SalLPRemoveDirectory(path);
}

void RemoveTemporaryDir(const char* dir)
{
    CALL_STACK_MESSAGE2("RemoveTemporaryDir(%s)", dir);
    EnvSetCurrentDirectoryA(gEnvironment, dir); // so it deletes better (system likes cur-dir)
    if (strlen(dir) < SAL_MAX_LONG_PATH)
        _RemoveTemporaryDir(dir);
    SetCurrentDirectoryToSystem(); // must leave it, otherwise it won't be deletable

    ClearReadOnlyAttr(dir);
    SalLPRemoveDirectory(dir);
}

// ****************************************************************************

void _RemoveEmptyDirs(const char* dir)
{
    CPathBuffer path;
    WIN32_FIND_DATA file;
    strcpy(path, dir);
    char* end = path + strlen(path);
    if (*(end - 1) != '\\')
        *end++ = '\\';
    strcpy(end, "*");
    HANDLE find = SalFindFirstFileH(path, &file);
    if (find != INVALID_HANDLE_VALUE)
    {
        do
        {
            if (file.cFileName[0] != 0 && strcmp(file.cFileName, "..") && strcmp(file.cFileName, "."))
            {
                if ((file.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
                    (end - (char*)path) + strlen(file.cFileName) < path.Size() - 2)
                {
                    strcpy(end, file.cFileName);
                    ClearReadOnlyAttr(path, file.dwFileAttributes);
                    _RemoveEmptyDirs(path);
                }
            }
        } while (FindNextFile(find, &file));
        HANDLES(FindClose(find));
    }
    *(end - 1) = 0;
    SalLPRemoveDirectory(path);
}

void RemoveEmptyDirs(const char* dir)
{
    CALL_STACK_MESSAGE2("RemoveEmptyDirs(%s)", dir);
    EnvSetCurrentDirectoryA(gEnvironment, dir); // so it deletes better (system likes cur-dir)
    if (strlen(dir) < SAL_MAX_LONG_PATH)
        _RemoveEmptyDirs(dir);
    SetCurrentDirectoryToSystem(); // must leave it, otherwise it won't be deletable

    ClearReadOnlyAttr(dir);
    SalLPRemoveDirectory(dir);
}

// ****************************************************************************

BOOL CheckAndCreateDirectory(const char* dir, HWND parent, BOOL quiet, char* errBuf,
                             int errBufSize, char* newDir, BOOL noRetryButton,
                             BOOL manualCrDir)
{
    CALL_STACK_MESSAGE2("CheckAndCreateDirectory(%s)", dir);
AGAIN:
    if (parent == NULL)
        parent = MainWindow->HWindow;
    if (newDir != NULL)
        newDir[0] = 0;
    int dirLen = (int)strlen(dir);
    if (dirLen >= SAL_MAX_LONG_PATH) // too long name (use extended path limit)
    {
        if (errBuf != NULL)
            strncpy_s(errBuf, errBufSize, LoadStr(IDS_TOOLONGNAME), _TRUNCATE);
        else
            gPrompter->ShowError(LoadStrW(IDS_ERRORTITLE), LoadStrW(IDS_TOOLONGNAME));
        return FALSE;
    }
    DWORD attrs = SalGetFileAttributes(dir);
    CPathBuffer buf; // for error messages (truncation OK)
    CPathBuffer name;
    if (attrs == 0xFFFFFFFF) // probably doesn't exist, allow creating it
    {
        CPathBuffer root;  // Heap-allocated for long path support
        GetRootPath(root, dir);
        if (dirLen <= (int)strlen(root)) // dir is root directory
        {
            sprintf(buf, LoadStr(IDS_CREATEDIRFAILED), dir);
            if (errBuf != NULL)
                strncpy_s(errBuf, errBufSize, buf, _TRUNCATE);
            else
                gPrompter->ShowError(LoadStrW(IDS_ERRORTITLE), AnsiToWide(buf).c_str());
            return FALSE;
        }
        int msgBoxRet = IDCANCEL;
        if (!quiet)
        {
            // if user hasn't suppressed it, show info about directory non-existence
            if (Configuration.CnfrmCreateDir)
            {
                std::wstring msg = FormatStrW(LoadStrW(IDS_CREATEDIRECTORY), AnsiToWide(dir).c_str());
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
            strcpy(name, dir);
            char* s;
            while (1) // find first existing directory
            {
                s = strrchr(name, '\\');
                if (s == NULL)
                {
                    sprintf(buf, LoadStr(IDS_CREATEDIRFAILED), dir);
                    if (errBuf != NULL)
                        strncpy_s(errBuf, errBufSize, buf, _TRUNCATE);
                    else
                        gPrompter->ShowError(LoadStrW(IDS_ERRORTITLE), AnsiToWide(buf).c_str());
                    return FALSE;
                }
                if (s - name > (int)strlen(root))
                    *s = 0;
                else
                {
                    strcpy(name, root);
                    break; // we're already at root directory
                }
                attrs = SalGetFileAttributes(name);
                if (attrs != 0xFFFFFFFF) // name exists
                {
                    if (attrs & FILE_ATTRIBUTE_DIRECTORY)
                        break; // we'll build from this directory
                    else       // it's a file, that wouldn't work ...
                    {
                        sprintf(buf, LoadStr(IDS_NAMEUSEDFORFILE), name.Get());
                        if (errBuf != NULL)
                            strncpy_s(errBuf, errBufSize, buf, _TRUNCATE);
                        else
                        {
                            if (noRetryButton)
                            {
                                CFileErrorDlg dlg(parent, LoadStr(IDS_ERRORCREATINGDIR), dir, GetErrorText(ERROR_ALREADY_EXISTS), FALSE, IDD_ERROR3);
                                dlg.Execute();
                            }
                            else
                            {
                                CFileErrorDlg dlg(parent, LoadStr(IDS_ERRORCREATINGDIR), dir, GetErrorText(ERROR_ALREADY_EXISTS), TRUE);
                                if (dlg.Execute() == IDRETRY)
                                    goto AGAIN;
                                // SalMessageBox(parent, buf, LoadStr(IDS_ERRORTITLE), MB_OK | MB_ICONEXCLAMATION);
                            }
                        }
                        return FALSE;
                    }
                }
            }
            s = name + strlen(name) - 1;
            if (*s != '\\')
            {
                *++s = '\\';
                *++s = 0;
            }
            const char* st = dir + strlen(name);
            if (*st == '\\')
                st++;
            int len = (int)strlen(name);
            BOOL first = TRUE;
            while (*st != 0)
            {
                BOOL invalidName = manualCrDir && *st <= ' '; // spaces at the beginning of created directory name are undesirable only during manual creation (Windows can handle it, but it's potentially dangerous)
                const char* slash = strchr(st, '\\');
                if (slash == NULL)
                    slash = st + strlen(st);
                memcpy(name + len, st, slash - st);
                name[len += (int)(slash - st)] = 0;
                if (name[len - 1] <= ' ' || name[len - 1] == '.')
                    invalidName = TRUE; // spaces and dots at the end of created directory name are undesirable
            AGAIN2:
                if (invalidName || !SalLPCreateDirectory(name, NULL))
                {
                    DWORD lastErr = invalidName ? ERROR_INVALID_NAME : GetLastError();
                    // ERROR_ALREADY_EXISTS is not a failure - the directory is there, which is what we want
                    if (lastErr != ERROR_ALREADY_EXISTS)
                    {
                        sprintf(buf, LoadStr(IDS_CREATEDIRFAILED), name.Get());
                        if (errBuf != NULL)
                            strncpy_s(errBuf, errBufSize, buf, _TRUNCATE);
                        else
                        {
                            if (noRetryButton)
                            {
                                CFileErrorDlg dlg(parent, LoadStr(IDS_ERRORCREATINGDIR), dir, GetErrorText(lastErr), FALSE, IDD_ERROR3);
                                dlg.Execute();
                            }
                            else
                            {
                                CFileErrorDlg dlg(parent, LoadStr(IDS_ERRORCREATINGDIR), dir, GetErrorText(lastErr), TRUE);
                                if (dlg.Execute() == IDRETRY)
                                    goto AGAIN2;
                                //              SalMessageBox(parent, buf, LoadStr(IDS_ERRORTITLE), MB_OK | MB_ICONEXCLAMATION);
                            }
                        }
                        return FALSE;
                    }
                }
                else
                {
                    if (first && newDir != NULL)
                        strcpy(newDir, name.Get());
                    first = FALSE;
                }
                name[len++] = '\\';
                if (*slash == '\\')
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
        sprintf(buf, LoadStr(IDS_NAMEUSEDFORFILE), dir);
        if (errBuf != NULL)
            strncpy_s(errBuf, errBufSize, buf, _TRUNCATE);
        else
        {
            if (noRetryButton)
            {
                CFileErrorDlg dlg(parent, LoadStr(IDS_ERRORCREATINGDIR), dir, GetErrorText(ERROR_ALREADY_EXISTS), FALSE, IDD_ERROR3);
                dlg.Execute();
            }
            else
            {
                CFileErrorDlg dlg(parent, LoadStr(IDS_ERRORCREATINGDIR), dir, GetErrorText(ERROR_ALREADY_EXISTS), TRUE);
                if (dlg.Execute() == IDRETRY)
                    goto AGAIN;
                //        SalMessageBox(parent, buf, LoadStr(IDS_ERRORTITLE), MB_OK | MB_ICONEXCLAMATION);
            }
        }
        return FALSE;
    }
}

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

CPathHistoryItem::CPathHistoryItem(int type, const char* pathOrArchiveOrFSName,
                                   const char* archivePathOrFSUserPart, HICON hIcon,
                                   CPluginFSInterfaceAbstract* pluginFS)
{
    Type = type;
    HIcon = hIcon;
    PluginFS = NULL;

    TopIndex = -1;
    FocusedName = NULL;

    if (Type == 0) // disk
    {
        CPathBuffer root;  // Heap-allocated for long path support
        GetRootPath(root, pathOrArchiveOrFSName);
        const char* e = pathOrArchiveOrFSName + strlen(pathOrArchiveOrFSName);
        if ((int)strlen(root) < e - pathOrArchiveOrFSName || // it's not a root path
            pathOrArchiveOrFSName[0] == '\\')                // it's a UNC path
        {
            if (*(e - 1) == '\\')
                e--;
            PathOrArchiveOrFSName = (char*)malloc((e - pathOrArchiveOrFSName) + 1);
            if (PathOrArchiveOrFSName != NULL)
            {
                memcpy(PathOrArchiveOrFSName, pathOrArchiveOrFSName, e - pathOrArchiveOrFSName);
                PathOrArchiveOrFSName[e - pathOrArchiveOrFSName] = 0;
            }
        }
        else // it's a normal root path (c:\)
        {
            PathOrArchiveOrFSName = DupStr(root);
        }
        if (PathOrArchiveOrFSName == NULL)
        {
            TRACE_E(LOW_MEMORY);
            if (PathOrArchiveOrFSName != NULL)
                free(PathOrArchiveOrFSName);
            PathOrArchiveOrFSName = NULL;
            HIcon = NULL;
        }
        ArchivePathOrFSUserPart = NULL;
    }
    else
    {
        if (Type == 1 || Type == 2) // archive or FS (just copy of both strings)
        {
            if (Type == 2)
                PluginFS = pluginFS;
            PathOrArchiveOrFSName = DupStr(pathOrArchiveOrFSName);
            ArchivePathOrFSUserPart = DupStr(archivePathOrFSUserPart);
            if (PathOrArchiveOrFSName == NULL || ArchivePathOrFSUserPart == NULL)
            {
                TRACE_E(LOW_MEMORY);
                if (PathOrArchiveOrFSName != NULL)
                    free(PathOrArchiveOrFSName);
                if (ArchivePathOrFSUserPart != NULL)
                    free(ArchivePathOrFSUserPart);
                PathOrArchiveOrFSName = NULL;
                ArchivePathOrFSUserPart = NULL;
                HIcon = NULL;
            }
        }
        else
            TRACE_E("CPathHistoryItem::CPathHistoryItem(): unknown 'type'");
    }
}

CPathHistoryItem::~CPathHistoryItem()
{
    if (FocusedName != NULL)
        free(FocusedName);
    if (PathOrArchiveOrFSName != NULL)
        free(PathOrArchiveOrFSName);
    if (ArchivePathOrFSUserPart != NULL)
        free(ArchivePathOrFSUserPart);
    if (HIcon != NULL)
        HANDLES(DestroyIcon(HIcon));
}

void CPathHistoryItem::ChangeData(int topIndex, const char* focusedName)
{
    TopIndex = topIndex;
    if (FocusedName != NULL)
    {
        if (focusedName != NULL && strcmp(FocusedName, focusedName) == 0)
            return; // no change -> end
        free(FocusedName);
    }
    if (focusedName != NULL)
        FocusedName = DupStr(focusedName);
    else
        FocusedName = NULL;
}

void CPathHistoryItem::GetPath(char* buffer, int bufferSize)
{
    char* origBuffer = buffer;
    if (bufferSize == 0)
        return;
    if (PathOrArchiveOrFSName == NULL)
    {
        buffer[0] = 0;
        return;
    }
    int l = (int)strlen(PathOrArchiveOrFSName) + 1;
    if (l > bufferSize)
        l = bufferSize;
    memcpy(buffer, PathOrArchiveOrFSName, l - 1);
    buffer[l - 1] = 0;
    if (Type == 1 || Type == 2) // archive or FS
    {
        buffer += l - 1;
        bufferSize -= l - 1;
        char* s = ArchivePathOrFSUserPart;
        if (*s != 0 || Type == 2)
        {
            if (bufferSize >= 2) // add '\\' or ':'
            {
                *buffer++ = Type == 1 ? '\\' : ':';
                *buffer = 0;
                bufferSize--;
            }
            l = (int)strlen(s) + 1;
            if (l > bufferSize)
                l = bufferSize;
            memcpy(buffer, s, l - 1);
            buffer[l - 1] = 0;
        }
    }

    // must double all '&' otherwise they'll become underlines
    DuplicateAmpersands(origBuffer, bufferSize);
}

HICON
CPathHistoryItem::GetIcon()
{
    return HIcon;
}

BOOL DuplicateAmpersands(char* buffer, int bufferSize, BOOL skipFirstAmpersand)
{
    if (buffer == NULL)
    {
        TRACE_E("Unexpected situation (1) in DuplicateAmpersands()");
        return FALSE;
    }
    char* s = buffer;
    int l = (int)strlen(buffer);
    if (l >= bufferSize)
    {
        TRACE_E("Unexpected situation (2) in DuplicateAmpersands()");
        return FALSE;
    }
    BOOL ret = TRUE;
    BOOL first = TRUE;
    while (*s != 0)
    {
        if (*s == '&')
        {
            if (!(skipFirstAmpersand && first))
            {
                if (l + 1 < bufferSize)
                {
                    memmove(s + 1, s, l - (s - buffer) + 1); // double '&'
                    l++;
                    s++;
                }
                else // doesn't fit, trim buffer
                {
                    ret = FALSE;
                    memmove(s + 1, s, l - (s - buffer)); // double '&', trim by one character
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

void RemoveAmpersands(char* text)
{
    if (text == NULL)
    {
        TRACE_E("Unexpected situation in RemoveAmpersands().");
        return;
    }
    char* s = text;
    while (*s != 0 && *s != '&')
        s++;
    if (*s != 0)
    {
        char* d = s;
        while (*s != 0)
        {
            if (*s != '&')
                *d++ = *s++;
            else
            {
                if (*(s + 1) == '&')
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
    CPathBuffer errBuf;
    if (PathOrArchiveOrFSName != NULL) // valid data
    {
        int failReason;
        BOOL clear = TRUE;
        if (Type == 0) // disk
        {
            if (!panel->ChangePathToDisk(panel->HWindow, PathOrArchiveOrFSName, TopIndex, FocusedName, NULL,
                                         TRUE, FALSE, FALSE, &failReason))
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
                if (!panel->ChangePathToArchive(PathOrArchiveOrFSName, ArchivePathOrFSUserPart, TopIndex,
                                                FocusedName, FALSE, NULL, TRUE, &failReason, FALSE, FALSE, TRUE))
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
                            std::wstring msg = FormatStrW(LoadStrW(IDS_PATHINARCHIVENOTFOUND), AnsiToWide(ArchivePathOrFSUserPart).c_str());
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
                                if (!panel->ChangePathToDetachedFS(i, TopIndex, FocusedName, TRUE, &failReason,
                                                                   PathOrArchiveOrFSName, ArchivePathOrFSUserPart))
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
                             !panel->IsPathFromActiveFS(PathOrArchiveOrFSName, ArchivePathOrFSUserPart,
                                                        fsNameIndex, convertPathToInternalDummy)))
                    {
                        CDetachedFSList* list = MainWindow->DetachedFSList;
                        int i;
                        for (i = 0; i < list->Count; i++)
                        {
                            if (list->At(i)->IsPathFromThisFS(PathOrArchiveOrFSName, ArchivePathOrFSUserPart))
                            {
                                done = TRUE;
                                // try changing to requested path, also reconnect detached FS
                                if (!panel->ChangePathToDetachedFS(i, TopIndex, FocusedName, TRUE, &failReason,
                                                                   PathOrArchiveOrFSName, ArchivePathOrFSUserPart))
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
                        if (!panel->ChangePathToPluginFS(PathOrArchiveOrFSName, ArchivePathOrFSUserPart, TopIndex,
                                                         FocusedName, FALSE, 2, NULL, TRUE, &failReason))
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
    CPathBuffer buf1;
    CPathBuffer buf2;
    if (Type == item.Type)
    {
        if (Type == 0) // disk
        {
            GetPath(buf1, buf1.Size());
            item.GetPath(buf2, buf2.Size());
            if (StrICmp(buf1, buf2) == 0)
                return TRUE;
        }
        else
        {
            if (Type == 1) // archivee
            {
                if (StrICmp(PathOrArchiveOrFSName, item.PathOrArchiveOrFSName) == 0 &&  // archive file is "case-insensitive"
                    strcmp(ArchivePathOrFSUserPart, item.ArchivePathOrFSUserPart) == 0) // path in archive is "case-sensitive"
                {
                    return TRUE;
                }
            }
            else
            {
                if (Type == 2) // FS
                {
                    if (StrICmp(PathOrArchiveOrFSName, item.PathOrArchiveOrFSName) == 0) // fs-name is "case-insensitive"
                    {
                        if (strcmp(ArchivePathOrFSUserPart, item.ArchivePathOrFSUserPart) == 0) // fs-user-part is "case-sensitive"
                            return TRUE;
                        if (curPluginFS != NULL && // also handle case when both fs-user-parts are identical because FS returns TRUE from IsCurrentPath for them (generally we would need to introduce method for comparing two fs-user-parts, but I don't want to do it just for histories, maybe later...)
                            StrICmp(PathOrArchiveOrFSName, curPluginFS->GetPluginFSName()) == 0)
                        {
                            int fsNameInd = curPluginFS->GetPluginFSNameIndex();
                            if (curPluginFS->IsCurrentPath(fsNameInd, fsNameInd, ArchivePathOrFSUserPart) &&
                                curPluginFS->IsCurrentPath(fsNameInd, fsNameInd, item.ArchivePathOrFSUserPart))
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
    CPathBuffer buffer;

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
                Paths[i]->GetPath(buffer, buffer.Size());
                mii.String = buffer;
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
            Paths[i]->GetPath(buffer, buffer.Size());
            mii.String = buffer;
            mii.ID = id++;
            popup->InsertItem(-1, TRUE, &mii);
        }
    }
}

void CPathHistory::FillHistoryPopupMenu(CMenuPopup* popup, DWORD firstID, int maxCount,
                                        BOOL separator)
{
    CPathBuffer buffer;

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
        Paths[i]->GetPath(buffer, buffer.Size());
        mii.String = buffer;
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
        AddPathUnique(NewItem->Type, NewItem->PathOrArchiveOrFSName, NewItem->ArchivePathOrFSUserPart,
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

void CPathHistory::ChangeActualPathData(int type, const char* pathOrArchiveOrFSName,
                                        const char* archivePathOrFSUserPart,
                                        CPluginFSInterfaceAbstract* pluginFS,
                                        CPluginFSInterfaceEncapsulation* curPluginFS,
                                        int topIndex, const char* focusedName)
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

void CPathHistory::RemoveActualPath(int type, const char* pathOrArchiveOrFSName,
                                    const char* archivePathOrFSUserPart,
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

void CPathHistory::AddPath(int type, const char* pathOrArchiveOrFSName, const char* archivePathOrFSUserPart,
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

void CPathHistory::AddPathUnique(int type, const char* pathOrArchiveOrFSName, const char* archivePathOrFSUserPart,
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

void CPathHistory::SaveToRegistry(HKEY hKey, const char* name, BOOL onlyClear)
{
    HKEY historyKey;
    if (CreateKey(hKey, name, historyKey))
    {
        ClearKey(historyKey);

        if (!onlyClear) // if key should not just be cleared, save values from history
        {
            int index = 0;
            char buf[10];
            CPathBuffer path;
            int i;
            for (i = 0; i < Paths.Count; i++)
            {
                CPathHistoryItem* item = Paths[i];
                switch (item->Type)
                {
                case 0: // disk
                {
                    strcpy(path, item->PathOrArchiveOrFSName);
                    break;
                }

                // archive & FS: use ':' character as separator of two path parts
                // during load we'll determine path type based on this character
                case 1: // archive
                case 2: // FS
                {
                    lstrcpyn(path, item->PathOrArchiveOrFSName, path.Size());
                    StrNCat(path, ":", path.Size());
                    if (item->ArchivePathOrFSUserPart != NULL)
                        StrNCat(path, item->ArchivePathOrFSUserPart, path.Size());
                    break;
                }
                default:
                {
                    TRACE_E("CPathHistory::SaveToRegistry() uknown path type");
                    continue;
                }
                }
                itoa(index + 1, buf, 10);
                SetValue(historyKey, buf, REG_SZ, path, (DWORD)strlen(path) + 1);
                index++;
            }
        }
        CloseKey(historyKey);
    }
}

void CPathHistory::LoadFromRegistry(HKEY hKey, const char* name)
{
    ClearHistory();
    HKEY historyKey;
    if (OpenKey(hKey, name, historyKey))
    {
        CPathBuffer path;
        CPathBuffer fsName;
        const char* pathOrArchiveOrFSName = path;
        const char* archivePathOrFSUserPart = NULL;
        char buf[10];
        int type;
        int i;
        for (i = 0;; i++)
        {
            itoa(i + 1, buf, 10);
            if (GetValue(historyKey, buf, REG_SZ, path, path.Size()))
            {
                if (strlen(path) >= 2)
                {
                    // path can be of type
                    // 0 (disk): "C:\???" or "\\server\???"
                    // 1 (archive): "C:\???:" or "\\server\???:"
                    // 2 (FS): "XY:???"
                    type = -1; // don't add
                    if ((path[0] == '\\' && path[1] == '\\') || path[1] == ':')
                    {
                        // it's type==0 (disk) or type==1 (archive)
                        pathOrArchiveOrFSName = path;
                        char* separator = strchr(path + 2, ':');
                        if (separator == NULL)
                        {
                            type = 0;
                            archivePathOrFSUserPart = NULL;
                        }
                        else
                        {
                            *separator = 0;
                            type = 1;
                            archivePathOrFSUserPart = separator + 1;
                        }
                    }
                    else
                    {
                        // candidate for FS path
                        if (IsPluginFSPath(path, fsName, &archivePathOrFSUserPart))
                        {
                            pathOrArchiveOrFSName = fsName;
                            type = 2;
                        }
                    }
                    if (type != -1)
                        AddPath(type, pathOrArchiveOrFSName, archivePathOrFSUserPart, NULL, NULL);
                    else
                        TRACE_E("CPathHistory::LoadFromRegistry() invalid path: " << path);
                }
            }
            else
                break;
        }
        CloseKey(historyKey);
    }
}

//
// ****************************************************************************
// CUserMenuIconData
//

CUserMenuIconData::CUserMenuIconData(const char* fileName, DWORD iconIndex, const char* umCommand)
{
    lstrcpyn(FileName.Get(), fileName, SAL_MAX_LONG_PATH);
    IconIndex = iconIndex;
    lstrcpyn(UMCommand.Get(), umCommand, SAL_MAX_LONG_PATH);
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
    FileName[0] = 0;
    IconIndex = -1;
    UMCommand[0] = 0;
    LoadedIcon = NULL;
}

//
// ****************************************************************************
// CUserMenuIconDataArr
//

HICON
CUserMenuIconDataArr::GiveIconForUMI(const char* fileName, DWORD iconIndex, const char* umCommand)
{
    CALL_STACK_MESSAGE1("CUserMenuIconDataArr::GiveIconForUMI(, ,)");
    for (int i = 0; i < Count; i++)
    {
        CUserMenuIconData* item = At(i);
        if (item->IconIndex == iconIndex &&
            strcmp(item->FileName, fileName) == 0 &&
            strcmp(item->UMCommand, umCommand) == 0)
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
    SetThreadNameInVCAndTrace("UMIconReader");
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
        if (item->FileName[0] != 0 &&
            SalGetFileAttributes(item->FileName) != INVALID_FILE_ATTRIBUTES && // accessibility test (instead of CheckPath)
            ExtractIconEx(item->FileName, item->IconIndex, NULL, &umIcon, 1) == 1)
        {
            HANDLES_ADD(__htIcon, __hoLoadImage, umIcon); // pridame handle na 'umIcon' do HANDLES
        }
        else
        {
            umIcon = NULL;
            if (item->UMCommand[0] != 0)
            { // in case previous method failed - try to get icon from system
                DWORD attrs = SalGetFileAttributes(item->UMCommand);
                if (attrs != INVALID_FILE_ATTRIBUTES) // accessibility test (instead of CheckPath)
                {
                    umIcon = GetFileOrPathIconAux(item->UMCommand, FALSE,
                                                  (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY)));
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

CUserMenuItem::CUserMenuItem(char* name, char* umCommand, char* arguments, char* initDir, char* icon,
                             int throughShell, int closeShell, int useWindow, int showInToolbar, CUserMenuItemType type,
                             CUserMenuIconDataArr* bkgndReaderData)
{
    UMIcon = NULL;
    ItemName = UMCommand = Arguments = InitDir = Icon = NULL;
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
    ItemName = UMCommand = Arguments = InitDir = Icon = NULL;
    ThroughShell = TRUE;
    CloseShell = TRUE;
    UseWindow = TRUE;
    ShowInToolbar = TRUE;
    Type = umitItem;
    static char emptyBuffer[] = "";
    static char nameBuffer[] = "\"$(Name)\"";
    static char fullPathBuffer[] = "$(FullPath)";
    Set(emptyBuffer, emptyBuffer, nameBuffer, fullPathBuffer, emptyBuffer);
}

CUserMenuItem::CUserMenuItem(CUserMenuItem& item, CUserMenuIconDataArr* bkgndReaderData)
{
    UMIcon = NULL;
    ItemName = UMCommand = Arguments = InitDir = Icon = NULL;
    ThroughShell = item.ThroughShell;
    CloseShell = item.CloseShell;
    UseWindow = item.UseWindow;
    ShowInToolbar = item.ShowInToolbar;
    Type = item.Type;
    Set(item.ItemName, item.UMCommand, item.Arguments, item.InitDir, item.Icon);
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
    if (ItemName != NULL)
        free(ItemName);
    if (UMCommand != NULL)
        free(UMCommand);
    if (Arguments != NULL)
        free(Arguments);
    if (InitDir != NULL)
        free(InitDir);
    if (Icon != NULL)
        free(Icon);
}

BOOL CUserMenuItem::Set(char* name, char* umCommand, char* arguments, char* initDir, char* icon)
{
    char* itemName = (char*)malloc(strlen(name) + 1);
    char* commandName = (char*)malloc(strlen(umCommand) + 1);
    char* argumentsName = (char*)malloc(strlen(arguments) + 1);
    char* initDirName = (char*)malloc(strlen(initDir) + 1);
    char* iconName = (char*)malloc(strlen(icon) + 1);
    if (itemName == NULL || commandName == NULL ||
        argumentsName == NULL || initDirName == NULL || iconName == NULL)
    {
        TRACE_E(LOW_MEMORY);
        return FALSE;
    }

    strcpy(itemName, name);
    strcpy(commandName, umCommand);
    strcpy(argumentsName, arguments);
    strcpy(initDirName, initDir);
    strcpy(iconName, icon);

    if (ItemName != NULL)
        free(ItemName);
    if (UMCommand != NULL)
        free(UMCommand);
    if (Arguments != NULL)
        free(Arguments);
    if (InitDir != NULL)
        free(InitDir);
    if (Icon != NULL)
        free(Icon);

    ItemName = itemName;
    UMCommand = commandName;
    Arguments = argumentsName;
    InitDir = initDirName;
    Icon = iconName;
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
    CPathBuffer fileName; // Heap-allocated for long path support
    fileName[0] = 0;
    DWORD iconIndex = -1;
    if (MainWindow != NULL && Icon != NULL && Icon[0] != 0)
    {
        // Icon is in format "filename,resID"
        // perform decomposition
        char* iterator = Icon + strlen(Icon) - 1;
        while (iterator > Icon && *iterator != ',')
            iterator--;
        if (iterator > Icon && *iterator == ',')
        {
            strncpy(fileName, Icon, iterator - Icon);
            fileName[iterator - Icon] = 0;
            iterator++;
            iconIndex = atoi(iterator);
        }
    }

    if (bkgndReaderData == NULL && fileName[0] != 0 && // read icons right here
        MainWindow->GetActivePanel() != NULL &&
        MainWindow->GetActivePanel()->CheckPath(FALSE, fileName) == ERROR_SUCCESS &&
        ExtractIconEx(fileName, iconIndex, NULL, &UMIcon, 1) == 1)
    {
        HANDLES_ADD(__htIcon, __hoLoadImage, UMIcon); // add 'UMIcon' handle to HANDLES
        return TRUE;
    }

    // in case previous method failed - try to get icon from system
    CPathBuffer umCommand; // Heap-allocated for long path support
    if (MainWindow != NULL && UMCommand != NULL && UMCommand[0] != 0 &&
        ExpandCommand(MainWindow->HWindow, UMCommand, umCommand, umCommand.Size(), TRUE))
    {
        while (strlen(umCommand) > 2 && CutDoubleQuotesFromBothSides(umCommand))
            ;
    }
    else
        umCommand[0] = 0;

    if (bkgndReaderData == NULL && umCommand[0] != 0 && // read icons right here
        MainWindow->GetActivePanel() != NULL &&
        MainWindow->GetActivePanel()->CheckPath(FALSE, umCommand) == ERROR_SUCCESS)
    {
        DWORD attrs = SalGetFileAttributes(umCommand);
        UMIcon = GetFileOrPathIconAux(umCommand, FALSE,
                                      (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY)));
        if (UMIcon != NULL)
            return TRUE;
    }

    if (bkgndReaderData != NULL)
    {
        if (getIconsFromReader) // icons are already loaded, just take the right one
        {
            UMIcon = bkgndReaderData->GiveIconForUMI(fileName, iconIndex, umCommand);
            if (UMIcon != NULL)
                return TRUE;
        }
        else // request loading of needed icon
            bkgndReaderData->Add(new CUserMenuIconData(fileName, iconIndex, umCommand));
    }

    // extract default icon from shell32.dll
    UMIcon = SalLoadImage(2, 1, IconSizes[ICONSIZE_16], IconSizes[ICONSIZE_16], IconLRFlags);
    return TRUE;
}

BOOL CUserMenuItem::GetHotKey(char* key)
{
    if (ItemName == NULL || Type == umitSeparator)
        return FALSE;
    char* iterator = ItemName;
    while (*iterator != 0)
    {
        if (*iterator == '&' && *(iterator + 1) != 0 && *(iterator + 1) != '&')
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
        char className[101];
        className[0] = 0;
        if (GetClassName(hWindow, className, 100) != 0)
        {
            // some versions of synaptics touchpad (for example on HP notebooks) show their window with scrolling symbol under cursor
            // in such case we won't try to route to "correct" window under cursor, because
            // touchpad will handle it itself
            // https://forum.altap.cz/viewtopic.php?f=24&t=6039
            if (strcmp(className, "SynTrackCursorWindowClass") == 0 || strcmp(className, "Syn Visual Class") == 0)
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
            TRACE_E("GetClassName() failed!");
            hWindow = pMSG->hwnd;
        }
        // if it's a ScrollBar with a parent, post message to parent.
        // Scrollbars in panels aren't subclassed, so this is currently the only way
        // panel can learn about wheel rotation when cursor is over scrollbar.
        className[0] = 0;
        if (GetClassName(hWindow, className, 100) == 0 || StrICmp(className, "scrollbar") == 0)
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
    DosFileName = FileName = SourcePath = ZIPRoot = NULL;
    memset(&LastWrite, 0, sizeof(LastWrite));
    FileSize = CQuadWord(0, 0);
    Attr = 0;
}

CFileTimeStampsItem::~CFileTimeStampsItem()
{
    if (ZIPRoot != NULL)
        free(ZIPRoot);
    if (SourcePath != NULL)
        free(SourcePath);
    if (FileName != NULL)
        free(FileName);
    if (DosFileName != NULL)
        free(DosFileName);
    DosFileName = FileName = SourcePath = ZIPRoot = NULL;
}

BOOL CFileTimeStampsItem::Set(const char* zipRoot, const char* sourcePath, const char* fileName,
                              const char* dosFileName, const FILETIME& lastWrite, const CQuadWord& fileSize,
                              DWORD attr)
{
    if (*zipRoot == '\\')
        zipRoot++;
    ZIPRoot = DupStr(zipRoot);
    if (ZIPRoot != NULL) // zip-root has no '\\' at beginning or end
    {
        int l = (int)strlen(ZIPRoot);
        if (l > 0 && ZIPRoot[l - 1] == '\\')
            ZIPRoot[l - 1] = 0;
    }
    SourcePath = DupStr(sourcePath);
    if (SourcePath != NULL) // source-path has no '\\' at end
    {
        int l = (int)strlen(SourcePath);
        if (l > 0 && SourcePath[l - 1] == '\\')
            SourcePath[l - 1] = 0;
    }
    FileName = DupStr(fileName);
    if (dosFileName[0] != 0)
        DosFileName = DupStr(dosFileName);
    LastWrite = lastWrite;
    FileSize = fileSize;
    Attr = attr;
    return ZIPRoot != NULL && SourcePath != NULL && FileName != NULL &&
           (DosFileName != NULL || dosFileName[0] == 0);
}

//
// *****************************************************************************
// CFileTimeStamps
//

BOOL CFileTimeStamps::AddFile(const char* zipFile, const char* zipRoot, const char* sourcePath,
                              const char* fileName, const char* dosFileName,
                              const FILETIME& lastWrite, const CQuadWord& fileSize, DWORD attr)
{
    if (ZIPFile[0] == 0)
        strcpy(ZIPFile, zipFile);
    else
    {
        if (strcmp(zipFile, ZIPFile) != 0)
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
    int i;
    for (i = 0; i < List.Count; i++)
    {
        CFileTimeStampsItem* item2 = List[i];
        if (StrICmp(item->FileName, item2->FileName) == 0 &&
            StrICmp(item->SourcePath, item2->SourcePath) == 0)
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

const char* WINAPI FileTimeStampsEnum2(HWND parent, int enumFiles, const char** dosName, BOOL* isDir,
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
            *dosName = (item->DosFileName == NULL) ? item->FileName : item->DosFileName;
        if (isDir != NULL)
            *isDir = FALSE;
        if (size != NULL)
            *size = item->FileSize;
        if (attr != NULL)
            *attr = item->Attr;
        if (lastWrite != NULL)
            *lastWrite = item->LastWrite;
        return item->FileName;
    }
    else
        return NULL;
}

void CFileTimeStamps::AddFilesToListBox(HWND list)
{
    int i;
    for (i = 0; i < List.Count; i++)
    {
        CPathBuffer buf; // Heap-allocated for long path support
        strcpy(buf, List[i]->ZIPRoot);
        SalPathAppend(buf, List[i]->FileName, buf.Size());
        SendMessage(list, LB_ADDSTRING, 0, (LPARAM)buf.Get());
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

BOOL CDynamicStringImp::Add(const char* str, int len)
{
    if (len == -1)
        len = (int)strlen(str);
    else
    {
        if (len == -2)
            len = (int)strlen(str) + 1;
    }
    if (Length + len >= Allocated)
    {
        char* text = (char*)realloc(Text, Length + len + 100);
        if (text == NULL)
        {
            TRACE_E(LOW_MEMORY);
            return FALSE;
        }
        Allocated = Length + len + 100;
        Text = text;
    }
    memcpy(Text + Length, str, len);
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

void CFileTimeStamps::CopyFilesTo(HWND parent, int* indexes, int count, const char* initPath)
{
    CALL_STACK_MESSAGE3("CFileTimeStamps::CopyFilesTo(, , %d, %s)", count, initPath);
    CPathBuffer path; // Heap-allocated for long path support
    if (count > 0 &&
        GetTargetDirectory(parent, parent, LoadStr(IDS_BROWSEARCUPDATE),
                           LoadStr(IDS_BROWSEARCUPDATETEXT), path, FALSE, initPath))
    {
        CDynamicStringImp fromStr, toStr;
        BOOL ok = TRUE;
        BOOL tooLongName = FALSE;
        int i;
        for (i = 0; i < count; i++)
        {
            int index = indexes[i];
            if (index < List.Count && index >= 0) // just for safety
            {
                CFileTimeStampsItem* item = List[index];
                CPathBuffer name; // Heap-allocated for long path support
                strcpy(name, item->SourcePath);
                tooLongName |= !SalPathAppend(name, item->FileName, name.Size());
                ok &= fromStr.Add(name, (int)strlen(name) + 1);

                strcpy(name, path);
                tooLongName |= !SalPathAppend(name, item->ZIPRoot, name.Size());
                tooLongName |= !SalPathAppend(name, item->FileName, name.Size());
                ok &= toStr.Add(name, (int)strlen(name) + 1);
            }
        }
        fromStr.Add("\0", 2); // for safety add two more nulls at the end (no Add, also ok)
        toStr.Add("\0", 2);   // for safety add two more nulls at the end (no Add, also ok)

        if (ok && !tooLongName)
        {
            CShellExecuteWnd shellExecuteWnd;
            SHFILEOPSTRUCT fo;
            fo.hwnd = shellExecuteWnd.Create(parent, "SEW: CFileTimeStamps::CopyFilesTo");
            fo.wFunc = FO_COPY;
            fo.pFrom = fromStr.Text;
            fo.pTo = toStr.Text;
            fo.fFlags = FOF_SIMPLEPROGRESS | FOF_NOCONFIRMMKDIR | FOF_MULTIDESTFILES;
            fo.fAnyOperationsAborted = FALSE;
            fo.hNameMappings = NULL;
            char title[100];
            lstrcpyn(title, LoadStr(IDS_BROWSEARCUPDATE), 100); // better to backup, LoadStr is used by other threads too
            fo.lpszProgressTitle = title;
            // perform the actual copying - amazingly easy, unfortunately it crashes sometimes ;-)
            CALL_STACK_MESSAGE1("CFileTimeStamps::CopyFilesTo::SHFileOperation");
            SHFileOperation(&fo);
        }
        else
        {
            if (tooLongName)
            {
                gPrompter->ShowError(LoadStrW(IDS_ERRORTITLE), LoadStrW(IDS_TOOLONGNAME));
            }
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
    CPathBuffer buf;
    WIN32_FIND_DATA data;
    int i;
    for (i = List.Count - 1; i >= 0; i--)
    {
        CFileTimeStampsItem* item = List[i];
        sprintf(buf, "%s\\%s", item->SourcePath, item->FileName);
        BOOL kill = TRUE;
        HANDLE find = SalFindFirstFileH(buf, &data);
        if (find != INVALID_HANDLE_VALUE)
        {
            HANDLES(FindClose(find));
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
                        char *r1, *s1;
                        if (item1 != NULL)
                        {
                            r1 = item1->ZIPRoot;
                            s1 = item1->SourcePath;
                            packList.Add(item1);
                            List.Detach(0);
                        }
                        for (i = List.Count - 1; i >= 0; i--) // quadratic complexity hopefully won't be a problem here
                        {                                     // going from back, because Detach is "simpler" that way
                            CFileTimeStampsItem* item2 = List[i];
                            char* r2 = item2->ZIPRoot;
                            char* s2 = item2->SourcePath;
                            if (strcmp(r1, r2) == 0 && // matching zip-root (case-sensitive comparison necessary - update test\A.txt and Test\b.txt must not happen at once)
                                StrICmp(s1, s2) == 0)  // matching source-path
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
                            EnvSetCurrentDirectoryA(gEnvironment, s1);
                            if (Panel->CheckPath(TRUE, NULL, ERROR_SUCCESS, TRUE, parent) == ERROR_SUCCESS &&
                                PackCompress(parent, Panel, ZIPFile, r1, FALSE, s1, FileTimeStampsEnum2, &data2))
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
    ZIPFile[0] = 0;
    EndStopRefresh();
}

//****************************************************************************
//
// CTopIndexMem
//

void CTopIndexMem::Push(const char* path, int topIndex)
{
    // check if path follows Path (path==Path+"\\name")
    const char* s = path + strlen(path);
    if (s > path && *(s - 1) == '\\')
        s--;
    BOOL ok;
    if (s == path)
        ok = FALSE;
    else
    {
        if (s > path && *s == '\\')
            s--;
        while (s > path && *s != '\\')
            s--;

        int l = (int)strlen(Path);
        if (l > 0 && Path[l - 1] == '\\')
            l--;
        ok = s - path == l && StrNICmp(path, Path, l) == 0;
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
        strcpy(Path, path);
        TopIndexes[TopIndexesCount++] = topIndex;
    }
    else // doesn't follow -> first top-index in sequence
    {
        strcpy(Path, path);
        TopIndexesCount = 1;
        TopIndexes[0] = topIndex;
    }
}

BOOL CTopIndexMem::FindAndPop(const char* path, int& topIndex)
{
    // check if path matches Path (path==Path)
    int l1 = (int)strlen(path);
    if (l1 > 0 && path[l1 - 1] == '\\')
        l1--;
    int l2 = (int)strlen(Path);
    if (l2 > 0 && Path[l2 - 1] == '\\')
        l2--;
    if (l1 == l2 && StrNICmp(path, Path, l1) == 0)
    {
        if (TopIndexesCount > 0)
        {
            char* s = Path + strlen(Path);
            if (s > Path && *(s - 1) == '\\')
                s--;
            if (s > Path && *s == '\\')
                s--;
            while (s > Path && *s != '\\')
                s--;
            *s = 0;
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

BOOL CFileHistory::AddFile(CFileHistoryItemTypeEnum type, DWORD handlerID, const char* fileName)
{
    CALL_STACK_MESSAGE4("CFileHistory::AddFile(%d, %u, %s)", type, handlerID, fileName);

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
    CPathBuffer name;
    MENU_ITEM_INFO mii;
    mii.Mask = MENU_MASK_TYPE | MENU_MASK_ID | MENU_MASK_ICON | MENU_MASK_STRING;
    mii.Type = MENU_TYPE_STRING;
    mii.String = name;
    int i;
    for (i = 0; i < Files.Count; i++)
    {
        CFileHistoryItem* item = Files[i];

        // separate name from path with '\t' character - it will be in separate column
        lstrcpy(name, item->FileName);
        char* ptr = strrchr(name, '\\');
        if (ptr == NULL)
            return FALSE;
        memmove(ptr + 1, ptr, lstrlen(ptr) + 1);
        *(ptr + 1) = '\t';
        const char* text = "";
        // double '&' so it doesn't display as underline
        DuplicateAmpersands(name, 2 * MAX_PATH);

        mii.HIcon = item->HIcon;
        switch (item->Type)
        {
        case fhitView:
            text = LoadStr(IDS_FILEHISTORY_VIEW);
            break;
        case fhitEdit:
            text = LoadStr(IDS_FILEHISTORY_EDIT);
            break;
        case fhitOpen:
            text = LoadStr(IDS_FILEHISTORY_OPEN);
            break;
        default:
            TRACE_E("Unknown Type=" << item->Type);
        }
        sprintf(name + lstrlen(name), "\t(%s)", text); // append way file is opened
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

BOOL SetEditOrComboText(HWND hWnd, const char* text)
{
    char className[31];
    className[0] = 0;
    if (GetClassName(hWnd, className, 30) == 0)
    {
        TRACE_E("GetClassName failed on hWnd=0x" << hWnd);
        return FALSE;
    }

    HWND hEdit;
    if (StrICmp(className, "edit") != 0)
    {
        hEdit = GetWindow(hWnd, GW_CHILD);
        if (hEdit == NULL ||
            GetClassName(hEdit, className, 30) == 0 ||
            StrICmp(className, "edit") != 0)
        {
            TRACE_E("Edit window was not found hWnd=0x" << hWnd);
            return FALSE;
        }
    }
    else
        hEdit = hWnd;

    SendMessage(hEdit, WM_SETTEXT, 0, (LPARAM)text);
    SendMessage(hEdit, EM_SETSEL, 0, lstrlen(text));
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
    mii.String = LoadStr(IDS_PATHMENU_BROWSE);
    popup.InsertItem(0xFFFFFFFF, TRUE, &mii);

    //  mii.ID = 2;
    //  mii.String = "Tree...\tCtrl+T";
    //  popup.InsertItem(0xFFFFFFFF, TRUE, &mii);

    popup.InsertItem(0xFFFFFFFF, TRUE, &miiSep);

    mii.ID = DIRECTORY_COMMAND_LEFT;
    mii.String = LoadStr(IDS_PATHMENU_LEFT);
    popup.InsertItem(0xFFFFFFFF, TRUE, &mii);

    mii.ID = DIRECTORY_COMMAND_RIGHT;
    mii.String = LoadStr(IDS_PATHMENU_RIGHT);
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
                char className[30];
                GetClassName(hChild, className, 29);
                className[29] = 0;
                BOOL combo = (stricmp(className, "combobox") == 0);
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

void OnDirectoryButton(HWND hDialog, int editID, int editBufSize, int buttonID, WPARAM wParam, LPARAM lParam)
{
    BOOL selectMenuItem = LOWORD(lParam);
    DWORD cmd = TrackDirectoryMenu(hDialog, buttonID, selectMenuItem);
    InvokeDirectoryMenuCommand(cmd, hDialog, editID, editBufSize);
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

void InvokeDirectoryMenuCommand(DWORD cmd, HWND hDialog, int editID, int editBufSize)
{
    CPathBuffer path;
    BOOL setPathToEdit = FALSE;
    switch (cmd)
    {
    case 0:
    {
        return;
    }

    case DIRECTORY_COMMAND_BROWSE:
    {
        // browse
        GetDlgItemText(hDialog, editID, path, MAX_PATH);
        char caption[100];
        GetWindowText(hDialog, caption, 100); // will have same caption as dialog
        if (GetTargetDirectory(hDialog, hDialog, caption, LoadStr(IDS_BROWSETARGETDIRECTORY), path, FALSE, path))
            setPathToEdit = TRUE;
        break;
    }

        //    case 2:
        //    {
        //      // tree
        //      break;
        //    }

    case DIRECTORY_COMMAND_LEFT:
    case DIRECTORY_COMMAND_RIGHT:
    {
        // left/right panel directory
        CFilesWindow* panel = (cmd == DIRECTORY_COMMAND_LEFT) ? MainWindow->LeftPanel : MainWindow->RightPanel;
        if (panel != NULL)
        {
            panel->GetGeneralPath(path, path.Size(), TRUE);
            setPathToEdit = TRUE;
        }
        break;
    }

    default:
    {
        // hot path
        if (cmd >= DIRECTORY_COMMAND_HOTPATHF && cmd <= DIRECTORY_COMMAND_HOTPATHL)
        {
            if (MainWindow->GetExpandedHotPath(hDialog, cmd - DIRECTORY_COMMAND_HOTPATHF, path, path.Size()))
                setPathToEdit = TRUE;
        }
        else
            TRACE_E("Unknown cmd=" << cmd);
    }
    }
    if (setPathToEdit)
    {
        if ((int)strlen(path) >= editBufSize)
        {
            TRACE_E("InvokeDirectoryMenuCommand(): too long path! len=" << (int)strlen(path));
            path[editBufSize - 1] = 0;
        }
        SetEditOrComboText(GetDlgItem(hDialog, editID), path);
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
    char className[31];
    className[0] = 0;
    if (GetClassName(hWindow, className, 30) == 0 || StrICmp(className, "edit") != 0)
    {
        // it might be a combobox, try to reach for inner edit
        hWindow = GetWindow(hWindow, GW_CHILD);
        if (hWindow == NULL || GetClassName(hWindow, className, 30) == 0 || StrICmp(className, "edit") != 0)
        {
            TRACE_E("CreateKeyForwarder: edit window was not found ClassName is " << className);
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
