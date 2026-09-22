// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"

#include <shlobj.h>
#include <shellapi.h>
#include <sddl.h>
#include <algorithm>
#include <cstdarg>
#include <limits>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "common/unicode/helpers.h"
#include "common/DiagnosticTextEncoding.h"
#include "salmon_unicode.h"

HINSTANCE HLanguage = NULL;
CSalmonSharedMemory* SalmonSharedMemory = NULL;

std::wstring BugReportPath;
std::wstring CrashReportName;
std::vector<CBugReport> BugReports;
BOOL ReportOldBugs = TRUE;            // the user allowed uploading old reports as well

const wchar_t* APP_NAME = L"Sally Bug Reporter";

// ****************************************************************************

std::wstring LoadStr(int resID, HINSTANCE hInstance)
{
    if (hInstance == NULL)
        hInstance = HLanguage;
#ifdef _DEBUG
    // better ensure nobody calls us before the resource handle is initialized
    if (hInstance == NULL)
        TRACE_E("LoadStr: hInstance == NULL");
#endif // _DEBUG

    const wchar_t* resource = NULL;
    const int size = LoadStringW(hInstance, resID, reinterpret_cast<LPWSTR>(&resource), 0);
    if (size != 0 && resource != NULL) // error is NO_ERROR even when the string does not exist - unusable
    {
        return std::wstring(resource, static_cast<size_t>(size));
    }
    TRACE_E("Error in LoadStr(" << resID << ").");
    return L"ERROR LOADING STRING";
}

std::wstring LoadStrW(int resID, HINSTANCE hInstance)
{
    return LoadStr(resID, hInstance);
}

std::wstring FormatText(const wchar_t* format, ...)
{
    if (format == NULL)
        return std::wstring();
    va_list args;
    va_start(args, format);
    va_list measureArgs;
    va_copy(measureArgs, args);
    const int length = _vscwprintf(format, measureArgs);
    va_end(measureArgs);
    if (length < 0)
    {
        va_end(args);
        return std::wstring();
    }
    std::wstring result(static_cast<size_t>(length), L'\0');
    vswprintf_s(result.data(), result.size() + 1, format, args);
    va_end(args);
    return result;
}

static std::wstring JoinPath(const std::wstring& directory, const std::wstring& name)
{
    std::wstring path = directory;
    if (!path.empty() && path.back() != L'\\')
        path += L'\\';
    path += name;
    return path;
}

BOOL GetCurrentModulePath(std::wstring& path)
{
    std::vector<wchar_t> buffer(256);
    for (;;)
    {
        SetLastError(ERROR_SUCCESS);
        const DWORD length = GetModuleFileNameW(NULL, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (length == 0)
            return FALSE;
        if (length < buffer.size() && GetLastError() != ERROR_INSUFFICIENT_BUFFER)
        {
            path.assign(buffer.data(), length);
            return TRUE;
        }
        if (buffer.size() > MAXDWORD / 2)
            return FALSE;
        buffer.resize(buffer.size() * 2);
    }
}

//*****************************************************************************
//
// GetErrorText
//
char* GetErrorText(DWORD error)
{
    thread_local std::string text;
    char prefix[32];
    sprintf_s(prefix, (int)error < 0 ? "(%08X) " : "(%d) ", error);
    text = prefix;

    char* systemText = NULL;
    const DWORD length = FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_ALLOCATE_BUFFER,
                                        NULL, error, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                                        reinterpret_cast<char*>(&systemText), 0, NULL);
    if (length != 0 && systemText != NULL && *systemText != 0)
        text.append(systemText, length);
    else
        text += "System error " + std::to_string(error) + ", text description is not available.";
    if (systemText != NULL)
        LocalFree(systemText);
    return text.data();
}

//*****************************************************************************
//
// GetItemIdListForFileName
// OpenFolder
//

LPITEMIDLIST
GetItemIdListForFileName(LPSHELLFOLDER folder, const wchar_t* fileName,
                         BOOL addUNCPrefix = FALSE, BOOL useEnumForPIDLs = FALSE,
                         const wchar_t* enumNamePrefix = NULL)
{
    std::wstring olePath = addUNCPrefix ? L"\\\\" : L"";
    olePath += fileName;

    LPITEMIDLIST pidl;
    ULONG chEaten;
    HRESULT ret;
    if (SUCCEEDED((ret = folder->ParseDisplayName(NULL, NULL, olePath.data(), &chEaten,
                                                  &pidl, NULL))))
    {
        return pidl;
    }
    else
    {
        TRACE_E("ParseDisplayName error: 0x" << std::hex << ret << std::dec);
        return NULL;
    }
}

void OpenFolder(HWND hWnd, const wchar_t* szDir)
{
    LPITEMIDLIST pidl = NULL;
    LPSHELLFOLDER desktop;
    if (SUCCEEDED(SHGetDesktopFolder(&desktop)))
    {
        pidl = GetItemIdListForFileName(desktop, szDir);
        desktop->Release();
    }

    if (pidl != NULL)
    {
        SHELLEXECUTEINFOW se;
        memset(&se, 0, sizeof(se));
        se.cbSize = sizeof(SHELLEXECUTEINFO);
        se.fMask = SEE_MASK_IDLIST;
        se.lpVerb = L"explore"; // option whether to open with the tree view
        se.hwnd = hWnd;
        se.nShow = SW_SHOWNORMAL;
        se.lpIDList = pidl;
        ShellExecuteExW(&se);

        IMalloc* alloc;
        if (SUCCEEDED(CoGetMalloc(1, &alloc)))
        {
            if (pidl != NULL && alloc->DidAlloc(pidl) == 1)
                alloc->Free(pidl);
            alloc->Release();
        }
    }
}

//*****************************************************************************
//
// SalGetFileAttributes
//

DWORD SalGetFileAttributes(const wchar_t* fileName)
{
    const size_t fileNameLen = wcslen(fileName);
    // if the path ends with a space/dot, we must append '\\', otherwise GetFileAttributes trims
    // the spaces/dots and works with a different path; for files it does not work anyway,
    // but it is still better than getting attributes of another file/directory (for "c:\\file.txt   "
    // it works with the name "c:\\file.txt")
    if (fileNameLen > 0 && (fileName[fileNameLen - 1] <= ' ' || fileName[fileNameLen - 1] == '.'))
    {
        std::wstring decorated(fileName);
        decorated += L'\\';
        return GetFileAttributesW(decorated.c_str());
    }
    else // a plain path, nothing special to do, just call the Windows GetFileAttributes
    {
        return GetFileAttributesW(fileName);
    }
}

//*****************************************************************************
//
// DirExists
//

BOOL DirExists(const wchar_t* dirName)
{
    DWORD attr = SalGetFileAttributes(dirName);
    return (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY) != 0);
}

//*****************************************************************************
//
// ParseCommandLine
//

BOOL ParseCommandLine(const wchar_t* cmdLine, wchar_t* fileMappingName, size_t fileMappingNameCapacity,
                      std::wstring& slgName) noexcept
{
    std::wstring mappingName;
    std::wstring parsedSlgName;
    if (!sally::salmon::ParseCommandLine(cmdLine, mappingName, parsedSlgName) ||
        mappingName.size() >= fileMappingNameCapacity)
        return FALSE;
    try
    {
        memcpy(fileMappingName, mappingName.c_str(), (mappingName.size() + 1) * sizeof(wchar_t));
        slgName.swap(parsedSlgName);
        return TRUE;
    }
    catch (...)
    {
        return FALSE;
    }
}

//*****************************************************************************
//
// LoadSLG
//

HINSTANCE LoadSLG(const wchar_t* slgName)
{
    std::wstring path = JoinPath(L"lang", slgName);
    HINSTANCE hSLG = LoadLibraryW(path.c_str());
    if (hSLG == NULL)
    {
        // if loading the SLG failed, it might not exist or we were not given a valid name
        // try to find another suitable one based on priority
        const wchar_t* masks[] = {L"english.slg", L"czech.slg", L"german.slg", L"spanish.slg", L"*.slg", L""};
        for (int i = 0; *masks[i] != 0 && (hSLG == NULL); i++)
        {
            const std::wstring findPath = JoinPath(L"lang", masks[i]);
            WIN32_FIND_DATAW find;
            HANDLE hFind = HANDLES_Q(FindFirstFileW(findPath.c_str(), &find));
            if (hFind != INVALID_HANDLE_VALUE)
            {
                path = JoinPath(L"lang", find.cFileName);
                hSLG = LoadLibraryW(path.c_str());
                HANDLES(FindClose(hFind));
            }
        }
    }
    if (hSLG == NULL)
        // wide: English-only diagnostic, no LoadStr involved - same shape as
        // salmoncl.cpp's fix (194) in this same module family.
        MessageBoxW(NULL, L"Internal error: cannot load any language file. Please report at github.com/0xeb/sally/issues.", L"Sally Bug Reporter", MB_OK | MB_ICONEXCLAMATION | MB_SETFOREGROUND);
    return hSLG;
}

//------------------------------------------------------------------------------------------------
//
// RestartSalamander
//

BOOL RestartSalamander(HWND hParent)
{
    std::wstring modulePath;
    if (!GetCurrentModulePath(modulePath))
        return FALSE;
    const size_t utilitySeparator = modulePath.rfind(L'\\');
    if (utilitySeparator == std::wstring::npos)
        return FALSE;
    modulePath.resize(utilitySeparator); // strip salmon.exe
    const size_t rootSeparator = modulePath.rfind(L'\\');
    if (rootSeparator != std::wstring::npos)
    {
        const std::wstring initDir = modulePath.substr(0, rootSeparator);
        const std::wstring executable = JoinPath(initDir, L"sally.exe");
        SHELLEXECUTEINFOW se;
        memset(&se, 0, sizeof(se));
        se.cbSize = sizeof(SHELLEXECUTEINFO);
        se.nShow = SW_SHOWNORMAL;
        se.hwnd = hParent;
        se.lpDirectory = initDir.c_str();
        se.lpFile = executable.c_str();
        return ShellExecuteExW(&se);
    }
    return FALSE;
}

//------------------------------------------------------------------------------------------------
//
// CleanBugReportsDirectory
//

BOOL CleanBugReportsDirectory(BOOL keep7ZipArchives)
{
    if (BugReportPath.empty())
        return FALSE;
    for (const CBugReport& report : BugReports)
    {
        const std::wstring findPath = JoinPath(BugReportPath, report.Name + L".*");
        WIN32_FIND_DATAW find;
        HANDLE hFind = FindFirstFileW(findPath.c_str(), &find);
        if (hFind != INVALID_HANDLE_VALUE)
        {
            do
            {
                if ((find.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0)
                {
                    if (find.cFileName[0] != 0 && wcscmp(find.cFileName, L".") != 0 && wcscmp(find.cFileName, L"..") != 0)
                    {
                        BOOL skipDelete = FALSE;
                        if (keep7ZipArchives)
                        {
                            const wchar_t* ext = wcsrchr(find.cFileName, L'.');
                            if (ext != NULL && _wcsicmp(ext, L".7z") == 0)
                                skipDelete = TRUE;
                        }
                        if (!skipDelete)
                            DeleteFileW(JoinPath(BugReportPath, find.cFileName).c_str());
                    }
                }
            } while (FindNextFileW(hFind, &find));
            FindClose(hFind);
        }
    }
    return TRUE;
}

//------------------------------------------------------------------------------------------------
//
// GetBugReportNameIndexIgnoreExt()
//

// search the BugReports array (which contains names including extensions) and return the index of 'name'
// ignores extensions
int GetBugReportNameIndexIgnoreExt(const wchar_t* name)
{
    std::wstring strippedName(name);
    const size_t extension = strippedName.rfind(L'.');
    if (extension != std::wstring::npos)
        strippedName.resize(extension);
    for (size_t i = 0; i < BugReports.size(); i++)
    {
        std::wstring candidate = BugReports[i].Name;
        const size_t candidateExtension = candidate.rfind(L'.');
        if (candidateExtension != std::wstring::npos)
            candidate.resize(candidateExtension);
        if (_wcsicmp(candidate.c_str(), strippedName.c_str()) == 0)
            return static_cast<int>(i);
    }
    return -1;
}

//------------------------------------------------------------------------------------------------
//
// GetBugReportNames
//

BOOL GetBugReportNames()
{
    BugReports.clear();

    if (BugReportPath.empty())
        return FALSE;

    // if the directory does not exist, it cannot contain reports
    if (!DirExists(BugReportPath.c_str()))
        return FALSE;

    // look for reports by extension
    const wchar_t* extensions[] = {L"*.DMP", L"*.TXT", NULL};
    FILETIME latestFiletime;
    std::wstring latestFilename;
    for (int i = 0; extensions[i] != NULL; i++)
    {
        const std::wstring findPath = JoinPath(BugReportPath, extensions[i]);
        WIN32_FIND_DATAW find;
        HANDLE hFind = HANDLES_Q(FindFirstFileW(findPath.c_str(), &find));
        if (hFind != INVALID_HANDLE_VALUE)
        {
            do
            {
                CBugReport item = {find.cFileName};
                const wchar_t* ext = wcsrchr(item.Name.c_str(), L'.');

                BOOL skipFile = FALSE;
                if (ext != NULL && _wcsicmp(ext + 1, L"dmp") == 0)
                {
                    // delete minidumps over 200 MB because I do not believe they would pass to the server even after packing
                    if (find.nFileSizeHigh > 0 || find.nFileSizeLow > 200 * 1000 * 1024)
                    {
                        DeleteFileW(JoinPath(BugReportPath, find.cFileName).c_str());
                        skipFile = TRUE;
                    }
                }

                if (!skipFile)
                {
                    // store the newest name and timestamp
                    if (latestFilename.empty() || CompareFileTime(&latestFiletime, &find.ftLastWriteTime) == -1)
                    {
                        latestFilename = item.Name;
                        latestFiletime = find.ftLastWriteTime;
                    }

                    // add names that are missing from the list
                    if (GetBugReportNameIndexIgnoreExt(item.Name.c_str()) == -1)
                        BugReports.push_back(std::move(item));
                }
            } while (FindNextFileW(hFind, &find));
            HANDLES(FindClose(hFind));
        }
    }

    // bubble the newest report to index zero
    if (BugReports.size() > 1)
    {
        int index = GetBugReportNameIndexIgnoreExt(latestFilename.c_str());
        if (index > 0)
        {
            CBugReport item = std::move(BugReports[index]);
            BugReports.erase(BugReports.begin() + index);
            BugReports.insert(BugReports.begin(), std::move(item));
        }
    }

    // if these are crash dumps from WER, they lack the UID/version prefix - rename them
    wchar_t uid[17];
    swprintf_s(uid, L"%I64X", SalmonSharedMemory->UID);
    for (CBugReport& report : BugReports)
    {
        // if the file does not start with our UID, someone else (WER) generated it and we rename the item
        if (_wcsnicmp(report.Name.c_str(), uid, wcslen(uid)) != 0)
        {
            const std::wstring fullOrgName = JoinPath(BugReportPath, report.Name);
            const size_t extension = report.Name.rfind(L'.');
            const std::wstring extName = extension != std::wstring::npos ? report.Name.substr(extension) : L"";

            SYSTEMTIME lt;
            GetLocalTime(&lt);

            std::wstring tmpName = extension != std::wstring::npos ? report.Name.substr(0, extension) : report.Name;
            tmpName += L'-';
            tmpName += CrashReportName;
            std::wstring newName = GetReportBaseName(BugReportPath.c_str(), tmpName.c_str(), SalmonSharedMemory->UID, lt);
            newName += extName;

            // new name on disk
            const std::wstring fullNewName = JoinPath(BugReportPath, newName);
            if (MoveFileW(fullOrgName.c_str(), fullNewName.c_str()))
                report.Name = std::move(newName);
        }
    }

    // the extension is irrelevant, trim it off
    for (CBugReport& report : BugReports)
    {
        const size_t extension = report.Name.rfind(L'.');
        if (extension != std::wstring::npos)
            report.Name.resize(extension);
    }

    return !BugReports.empty();
}

// ignore 'oversize' reports that end with -1 to -99
int GetUniqueBugReportCount()
{
    std::vector<std::wstring> uniqueNames;
    for (const CBugReport& report : BugReports)
    {
        std::wstring name = report.Name;
        // remove trailing -1 to -99 from the name
        const size_t len = name.size();
        if (len > 3)
        {
            const bool oneDigitSuffix = name[len - 2] == L'-';
            const bool twoDigitSuffix = name[len - 3] == L'-';
            if (oneDigitSuffix)
                name.resize(len - 2);
            if (twoDigitSuffix)
                name.resize(len - 3);
        }
        if (std::find(uniqueNames.begin(), uniqueNames.end(), name) == uniqueNames.end())
            uniqueNames.push_back(std::move(name));
    }
    return static_cast<int>(uniqueNames.size());
}

//------------------------------------------------------------------------------------------------
//
// SaveDescriptionAndEmail
//

static BOOL WriteAll(HANDLE file, std::string_view bytes)
{
    while (!bytes.empty())
    {
        const DWORD chunk = static_cast<DWORD>(std::min<size_t>(bytes.size(), MAXDWORD));
        DWORD written = 0;
        if (!WriteFile(file, bytes.data(), chunk, &written, NULL) || written == 0)
            return FALSE;
        bytes.remove_prefix(written);
    }
    return TRUE;
}

BOOL SaveDescriptionAndEmail()
{
    if (BugReportPath.empty() || BugReports.empty())
        return FALSE;

    BOOL ret = FALSE;
    const std::wstring name = JoinPath(BugReportPath, BugReports[0].Name + L".INF");
    HANDLE hFile = CreateFileW(name.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile != INVALID_HANDLE_VALUE)
    {
        std::string email;
        std::string description;
        ret = sally::salmon::EncodeUtf8(Config.Email, email) &&
              sally::salmon::EncodeUtf8(Config.Description, description);
        if (ret)
        {
            std::string emailLine = "Email: " + email + "\r\n";
            ret = WriteAll(hFile, emailLine) && WriteAll(hFile, description);
        }
        CloseHandle(hFile);
    }
    return ret;
}

//------------------------------------------------------------------------------------------------
//
// LoadHLanguageVerbose
//

BOOL LoadHLanguageVerbose(const wchar_t* slgName)
{
    HINSTANCE hLanguage = LoadSLG(slgName);
    if (hLanguage == NULL)
    {
        // wide: same MessageBoxW shape as this file's other diagnostic (202);
        const std::wstring message = FormatText(L"Failed to load resources from %s.", slgName);
        MessageBoxW(NULL, message.c_str(), APP_NAME, MB_OK | MB_ICONEXCLAMATION | MB_SETFOREGROUND);
        return FALSE;
    }
    if (HLanguage != NULL)
        FreeLibrary(HLanguage);
    HLanguage = hLanguage;
    return TRUE;
}

//------------------------------------------------------------------------------------------------
//
// SID utilities
//

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

    // the caller must release the returned memory with LocalFree, see MSDN
    ConvertSidToStringSidW(pTokenUser->User.Sid, stringSid);

    free(pTokenUser);

    return TRUE;
}

#define SALMON_MAINDLG_MUTEX_NAME L"SallySalmonMainDialog"

class CMainDialogMutex
{
protected:
    HANDLE Mutex;

public:
    CMainDialogMutex()
    {
        // Init() calls HANDLES_Q(CreateMutexA(...)), which touches the global
        // __Handles object's CRITICAL_SECTION (common/handles.cpp). MainDialogMutex is itself a
        // global object, and C++ does not order global-constructor execution across translation
        // units - if MainDialogMutex's constructor ran before __Handles's, EnterCriticalSection
        // would fault on an uninitialized CRITICAL_SECTION. Previously invisible only because
        // HANDLES_Q(CreateMutexA(...)) could not even compile under Debug (C__Handles had no
        // CreateMutexA overload) - once the Debug-config fix added it, this static
        // initialization order fiasco became live and crashed on startup. Defer Init() to
        // wWinMain(), which runs strictly after every global constructor has completed.
        Mutex = NULL;
    }

    ~CMainDialogMutex()
    {
        // Deliberately does NOT close the handle - Done() does, from wWinMain.
        // This is the exact mirror of the construction-order problem the constructor comment
        // above describes, and it needs the symmetric answer. GetHandles() is a Meyer's singleton
        // (converted by the SIOF fix) that is first constructed lazily, and its first
        // caller in this process is Init() below - which runs INSIDE wWinMain, i.e. strictly
        // after this plain global object was constructed. Statics are destroyed in reverse order
        // of construction, so GetHandles() (constructed later) is torn down FIRST, and this
        // destructor would then call HANDLES(CloseHandle(...)) on an already-destroyed tracker:
        // EnterCriticalSection on a DeleteCriticalSection'd CRITICAL_SECTION. Worse, because the
        // handle was still open when ~C__Handles() ran, the tracker legitimately reported it as
        // leaked - the observed "Some monitored handles remained opened. Number of opened
        // handles: 1" dialog on exit, traced to salmon.cpp's CreateMutexA via a diagnostic dump
        // of the tracker's own remaining-handle list. Closing deterministically in wWinMain fixes
        // both halves at once: the handle is gone before the tracker tears down, so there is
        // nothing to report and nothing to call afterwards. A NULL check remains here only as a
        // backstop for paths that exit before Done() runs.
        if (Mutex != NULL)
        {
            // Not HANDLES(): by the time any static destructor runs, GetHandles() may already be
            // gone. Close the OS handle directly; the tracker entry dies with the tracker.
            CloseHandle(Mutex);
            Mutex = NULL;
        }
    }

    void Init();
    void Done();

    BOOL Enter();
    void Leave();
};

CMainDialogMutex MainDialogMutex; // mutex ensuring that we show only one dialog per user (even on the server)

void CMainDialogMutex::Init()
{
    LPWSTR sid = NULL;
    if (!GetStringSid(&sid))
        sid = NULL;

    wchar_t buff[1000];
    if (sid == NULL)
    {
        // failed to obtain the SID -- fall back to a degraded mode
        _snwprintf_s(buff, _TRUNCATE, L"%ls", SALMON_MAINDLG_MUTEX_NAME);
    }
    else
    {
        _snwprintf_s(buff, _TRUNCATE, L"Global\\%ls_%ls", SALMON_MAINDLG_MUTEX_NAME, sid);
        LocalFree(sid);
    }

    PSID psidEveryone;
    PACL paclNewDacl;
    SECURITY_ATTRIBUTES sa;
    SECURITY_DESCRIPTOR sd;
    SECURITY_ATTRIBUTES* saPtr = CreateAccessableSecurityAttributes(&sa, &sd, SYNCHRONIZE /*| MUTEX_MODIFY_STATE*/, &psidEveryone, &paclNewDacl);

    Mutex = HANDLES_Q(CreateMutexW(saPtr, FALSE, buff));
    if (Mutex == NULL)
    {
        Mutex = HANDLES_Q(OpenMutexW(SYNCHRONIZE, FALSE, buff));
        if (Mutex == NULL)
        {
            DWORD err = GetLastError();
            TRACE_I("CreateMainDialogMutex(): Unable to create/open mutex for the main dialog window! Error: " << GetErrorText(err));
        }
    }

    if (psidEveryone != NULL)
        FreeSid(psidEveryone);
    if (paclNewDacl != NULL)
        LocalFree(paclNewDacl);
}

// Deterministic counterpart to Init() - see the destructor's comment. Must run
// while GetHandles() is still alive, i.e. from wWinMain, not from static destruction.
void CMainDialogMutex::Done()
{
    if (Mutex != NULL)
    {
        HANDLES(CloseHandle(Mutex));
        Mutex = NULL;
    }
}

BOOL CMainDialogMutex::Enter()
{
    if (Mutex != NULL)
    {
        DWORD ret = WaitForSingleObject(Mutex, 0);
        if (ret == WAIT_FAILED)
            TRACE_E("CMainDialogMutex::Enter(): WaitForSingleObject() failed!");
        if (ret == WAIT_TIMEOUT)
            return FALSE; // another window is open, we cannot open ourselves
    }
    else
        TRACE_E("CMainDialogMutex::Enter(): the Mutex==NULL! Not initialized?");
    return TRUE;
}

void CMainDialogMutex::Leave()
{
    if (Mutex != NULL)
    {
        if (!ReleaseMutex(Mutex))
            TRACE_E("CMainDialogMutex::Enter(): ReleaseMutex() failed!");
    }
    else
        TRACE_E("CMainDialogMutex::Leave(): the Mutex==NULL! Not initialized?");
}

//------------------------------------------------------------------------------------------------
//
// OpenMainDialog()
//

BOOL OpenMainDialog(BOOL minidumpOnOpen)
{
    CMainDialog* mainDlg = new CMainDialog(HLanguage, IDD_SALMON_MAIN, minidumpOnOpen);
    if (mainDlg != NULL)
    {
        if (mainDlg->Create())
        {
            MSG msg;
            while (GetMessageW(&msg, NULL, 0, 0))
            {
                if (msg.message != WM_TIMER)
                    AppIsBusy = TRUE;
                if (!IsDialogMessage(mainDlg->HWindow, &msg))
                {
                    TranslateMessage(&msg);
                    DispatchMessageW(&msg);
                }
                AppIsBusy = FALSE;
            }
            DestroyWindow(mainDlg->HWindow);
        }
        mainDlg = NULL;
    }
    return TRUE;
}

//------------------------------------------------------------------------------------------------
//
// SetThreadNameInVCAndTrace
//

typedef struct tagTHREADNAME_INFO
{
    DWORD dwType;     // must be 0x1000
    LPCSTR szName;    // pointer to name (in user addr space)
    DWORD dwThreadID; // thread ID (-1=caller thread)
    DWORD dwFlags;    // reserved for future use, must be zero
} THREADNAME_INFO;

static void RaiseLegacyThreadNameInVC(const char* threadName)
{
    THREADNAME_INFO info;
    info.dwType = 0x1000;
    info.szName = threadName;
    info.dwThreadID = -1 /* caller thread */;
    info.dwFlags = 0;

    __try
    {
        RaiseException(0x406D1388, 0, sizeof(info) / sizeof(DWORD), (ULONG_PTR*)&info);
    }
    __except (EXCEPTION_CONTINUE_EXECUTION)
    {
    }
}

static void SetLegacyThreadNameInVC(const wchar_t* threadName) noexcept
{
    const std::string legacyThreadName =
        sally::diagnostic::EncodeAcpLossy(threadName != nullptr ? threadName : L"");
    RaiseLegacyThreadNameInVC(legacyThreadName.c_str());
}

void SetThreadNameInVC(const wchar_t* threadName)
{
    SetLegacyThreadNameInVC(threadName);
}

void SetThreadNameInVCAndTrace(const wchar_t* name)
{
    SetTraceThreadNameW(name);
    SetThreadNameInVC(name);
}

void ChechForBugs(CSalmonSharedMemory* mem, const wchar_t* slgName)
{
    if (DirExists(BugReportPath.c_str()))
    {
        if (MainDialogMutex.Enter())
        {
            if (GetBugReportNames())
            {
                // we need to display the GUI, we must load the SLG
                if (LoadHLanguageVerbose(slgName))
                {
                    if (GetUniqueBugReportCount() > 1)
                    {
                        // if multiple reports exist, ask whether to send them all
                        int res = MessageBoxW(NULL, LoadStr(IDS_SALMON_MORE_REPORTS, HLanguage).c_str(), LoadStr(IDS_SALMON_TITLE, HLanguage).c_str(), MB_YESNO | MB_ICONQUESTION | MB_SETFOREGROUND);
                        ReportOldBugs = (res == IDYES);
                    }
                    // with that decision made we can open the dialog
                    OpenMainDialog(FALSE);
                }
            }
            MainDialogMutex.Leave();
        }
    }

    ResetEvent(mem->CheckBugs);
    SetEvent(mem->Done); // let Salamander know we have taken over the SLG name
}

//------------------------------------------------------------------------------------------------
//
// WinMain
//

#define SALMON_RET_ERROR 0
#define SALMON_RET_OK 1

BOOL AppIsBusy = FALSE;

UINT PostponedMsg = 0;
WPARAM PostponedMsgWParam = 0;
LPARAM PostponedMsgLParam = 0;

int WINAPI
wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPWSTR cmdLine, int cmdShow)
{
    // in 99% of cases when Salamander does not crash, salmon.exe will run unnoticed in the background and should
    // consume as little memory/CPU as possible; therefore delay loading the SLG until something needs to be shown (a Salamander crash)

    SetTraceProcessName("Salmon");
    SetThreadNameInVCAndTrace(L"Main");
    TRACE_I("Begin");

    // we do not want critical errors such as "no disk in drive A:"
    SetErrorMode(SetErrorMode(0) | SEM_FAILCRITICALERRORS);

    HInstance = hInstance;

    // Deliberately not done in CMainDialogMutex's constructor - see the comment
    // there. wWinMain is guaranteed to run after every global object (including __Handles) has
    // finished constructing, so HANDLES_Q(CreateMutexA(...)) inside Init() is safe here.
    MainDialogMutex.Init();

    Config.Load();

    wchar_t fileMappingName[SALMON_FILEMAPPIN_NAME_SIZE] = {}; // frozen command/IPC rendezvous contract
    std::wstring slgName; // name of the SLG (e.g. "english.slg") to load into HLanguage; can be empty (then a default is loaded)

    if (!ParseCommandLine(cmdLine, fileMappingName, _countof(fileMappingName), slgName) || fileMappingName[0] == 0)
    {
        HINSTANCE hLanguage = LoadSLG(slgName.c_str()); // load the default SLG so that we can display possible errors
        if (hLanguage != NULL)
            MessageBoxW(NULL, LoadStr(IDS_SALMON_WRONG_CMDLINE, hLanguage).c_str(), LoadStr(IDS_SALMON_TITLE, hLanguage).c_str(), MB_OK | MB_ICONEXCLAMATION | MB_SETFOREGROUND);
        MainDialogMutex.Done(); // see Done()'s comment - must run before static destruction
        return SALMON_RET_ERROR;
    }

    CSalmonSharedMemory* mem = NULL;
    HANDLE fm = OpenFileMappingW(FILE_MAP_WRITE, FALSE, fileMappingName);
    if (fm != NULL)
        mem = (CSalmonSharedMemory*)MapViewOfFile(fm, FILE_MAP_WRITE, 0, 0, 0);
    if (mem == NULL)
    {
        if (fm != NULL)
            CloseHandle(fm);
        HINSTANCE hLanguage = LoadSLG(slgName.c_str()); // load the default SLG so that we can display possible errors
        if (hLanguage != NULL)
            MessageBoxW(NULL, LoadStr(IDS_SALMON_WRONG_CMDLINE, hLanguage).c_str(), LoadStr(IDS_SALMON_TITLE, hLanguage).c_str(), MB_OK | MB_ICONEXCLAMATION | MB_SETFOREGROUND);
        MainDialogMutex.Done(); // see Done()'s comment - must run before static destruction
        return SALMON_RET_ERROR;
    }

    if (mem->Version != SALMON_SHARED_MEMORY_VERSION)
    {
        UnmapViewOfFile(mem);
        CloseHandle(fm);
        HINSTANCE hLanguage = LoadSLG(slgName.c_str()); // load the default SLG so that we can display possible errors
        if (hLanguage != NULL)
            MessageBoxW(NULL, LoadStr(IDS_SALMON_WRONG_CMDLINE, hLanguage).c_str(), LoadStr(IDS_SALMON_TITLE, hLanguage).c_str(), MB_OK | MB_ICONEXCLAMATION | MB_SETFOREGROUND);
        MainDialogMutex.Done(); // see Done()'s comment - must run before static destruction
        return SALMON_RET_ERROR;
    }

    HANDLE arr[4];
    arr[0] = mem->Process;
    arr[1] = mem->Fire;
    arr[2] = mem->SetSLG;
    arr[3] = mem->CheckBugs;

    SalmonSharedMemory = mem; // set the global pointer so we do not have to thread it through parameters
    if (!sally::salmon::ReadFrozenWideString(mem->BugPath, _countof(mem->BugPath), BugReportPath))
    {
        SalmonSharedMemory = NULL;
        UnmapViewOfFile(mem);
        CloseHandle(fm);
        MainDialogMutex.Done();
        return SALMON_RET_ERROR;
    }
    if (!sally::salmon::ReadFrozenWideString(mem->BugName, _countof(mem->BugName), CrashReportName))
    {
        SalmonSharedMemory = NULL;
        UnmapViewOfFile(mem);
        CloseHandle(fm);
        MainDialogMutex.Done();
        return SALMON_RET_ERROR;
    }
    if (!BugReportPath.empty() && BugReportPath.back() != L'\\')
        BugReportPath += L'\\';

    BOOL run = TRUE;
    while (run)
    {
        // wait for one of the monitored events
        DWORD waitRet = MsgWaitForMultipleObjects(4, arr, FALSE, INFINITE, QS_ALLINPUT);
        switch (waitRet)
        {
        case WAIT_OBJECT_0 + 0: // sharedMemory->Process
        {
            // the parent process has terminated, so we exit as well

            // if we find any dumps, process them - Salamander could have crashed during init while loading
            // shell extensions and did not manage to open the main window and call CheckForBugs
            // or Salamander crashed before the exception handler was installed and the minidump was captured by WER,
            // which we have redirected to our bug report directory (Vista+)
            // Salamander could also have crashed in a way that bypassed the exception handler (typically caused by faulty shell extensions)
            ChechForBugs(mem, slgName.c_str());

            run = FALSE;
            break;
        }

        case WAIT_OBJECT_0 + 1: // sharedMemory->Fire
        {
            // the parent process wants us to generate a minidump
            if (LoadHLanguageVerbose(slgName.c_str())) // we need to display the GUI, we must load the SLG
            {
                // if we manage to lock the mutex, release it later; we do not want
                // additional processes started afterwards to pop up their windows during ours
                BOOL leave = MainDialogMutex.Enter();
                OpenMainDialog(TRUE);
                if (leave)
                    MainDialogMutex.Leave();
            }
            run = FALSE;
            break;
        }

        case WAIT_OBJECT_0 + 2: // sharedMemory->SetSLG
        {
            // Salamander loaded the “correct” SLG and lets us know we should switch to it
            // store its name; actively reading it now makes no sense yet
            if (!sally::salmon::ReadFrozenWideString(mem->SLGName, _countof(mem->SLGName), slgName))
                slgName.clear();
            ResetEvent(mem->SetSLG);
            SetEvent(mem->Done); // let Salamander know we have taken over the SLG name
            break;
        }

        case WAIT_OBJECT_0 + 3: // sharedMemory->CheckBugs
        {
            // Salamander informs us that the main window is open and it is time to check whether
            // there are old files in the bug report directory that we should send to the server
            ChechForBugs(mem, slgName.c_str());
            break;
        }

        case WAIT_OBJECT_0 + 4: // a message arrived in the message queue, pump it out
        {
            // salmon.exe uses the Win32 subsystem where Windows expects a message loop, which we do not have.
            // After starting salmon.exe a wait cursor was shown for about 5 seconds, see
            // https://forum.altap.cz/viewtopic.php?f=16&t=5572.
            // To get rid of it we had two options: switch to the "console" subsystem
            // or pump the message loop, which I chose as the cleaner solution (when launched by the user it shows no shell window, only a message box).
            MSG msg;
            while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE))
            {
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
            break;
        }
        }
    }

    SalmonSharedMemory = NULL;
    UnmapViewOfFile(mem);
    CloseHandle(fm);

    Config.Save();

    if (HLanguage != NULL)
    {
        FreeLibrary(HLanguage);
        HLanguage = NULL;
    }

    MainDialogMutex.Done(); // see Done()'s comment - must run before static destruction

    TRACE_I("End");
    return SALMON_RET_OK;
}
