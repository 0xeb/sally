// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"

#include <limits>
#include <vector>
#include "common/unicode/WideVariableExpansion.h"

// Wide: the shared registry facade (GetValueAux/SetValueAux, regwork.cpp) reads
// and writes REG_SZ as raw UTF-16LE bytes via RegQueryValueExW/RegSetValueExW,
// so a narrow buffer here silently received/sent garbled wide bytes - the -1
// dataSize convention even ran wcslen() over what it thought were narrow bytes
// reinterpreted as wchar_t*. Same corruption pattern as regedt's editor.cpp
// (see regedt's Command/Arguments/InitDir/LastExportPath fix).
std::wstring Command;
std::wstring Arguments;
std::wstring InitDir;

const wchar_t* EXP_FULLNAME = L"FullName";
const wchar_t* EXP_DRIVE = L"Drive";
const wchar_t* EXP_PATH = L"Path";
const wchar_t* EXP_NAME = L"Name";
const wchar_t* EXP_NAMEPART = L"NamePart";
const wchar_t* EXP_EXTPART = L"ExtPart";
const wchar_t* EXP_FULLPATH = L"FullPath";
const wchar_t* EXP_WINDIR = L"WinDir";
const wchar_t* EXP_SYSDIR = L"SysDir";
const wchar_t* EXP_SALDIR = L"SalDir";
const wchar_t* EXP_DOSFULLNAME = L"DOSFullName";
const wchar_t* EXP_DOSDRIVE = L"DOSDrive";
const wchar_t* EXP_DOSPATH = L"DOSPath";
const wchar_t* EXP_DOSNAME = L"DOSName";
const wchar_t* EXP_DOSNAMEPART = L"DOSNamePart";
const wchar_t* EXP_DOSEXTPART = L"DOSExtPart";
const wchar_t* EXP_DOSFULLPATH = L"DOSFullPath";
const wchar_t* EXP_DOSWINDIR = L"DOSWinDir";
const wchar_t* EXP_DOSSYSDIR = L"DOSSysDir";

// These live-SDK tables provide the validation and insert-variable vocabulary.
// Execute is NULL because expansion below uses the core-owned WideVarEntry tables.
CSalamanderVarStrEntry ExpCommandVariables[] =
    {
        {EXP_WINDIR, NULL},
        {EXP_SYSDIR, NULL},
        {EXP_SALDIR, NULL},
        {NULL, NULL}};

CSalamanderVarStrEntry ExpArgumentsVariables[] =
    {
        {EXP_FULLNAME, NULL},
        {EXP_DRIVE, NULL},
        {EXP_PATH, NULL},
        {EXP_NAME, NULL},
        {EXP_NAMEPART, NULL},
        {EXP_EXTPART, NULL},
        {EXP_FULLPATH, NULL},
        {EXP_WINDIR, NULL},
        {EXP_SYSDIR, NULL},
        {EXP_DOSFULLNAME, NULL},
        {EXP_DOSDRIVE, NULL},
        {EXP_DOSPATH, NULL},
        {EXP_DOSNAME, NULL},
        {EXP_DOSNAMEPART, NULL},
        {EXP_DOSEXTPART, NULL},
        {EXP_DOSFULLPATH, NULL},
        {EXP_DOSWINDIR, NULL},
        {EXP_DOSSYSDIR, NULL},
        {NULL, NULL}};

CSalamanderVarStrEntry ExpInitDirVariables[] =
    {
        {EXP_DRIVE, NULL},
        {EXP_PATH, NULL},
        {EXP_FULLPATH, NULL},
        {EXP_WINDIR, NULL},
        {EXP_SYSDIR, NULL},
        {NULL, NULL}};

// ****************************************************************************
// Wide expansion path (sally::unicode::WideVarEntry). ExecuteEditor is the only
// consumer - it needs the actual expanded text, computed end to end in UTF-16 with
// no ANSI round trip, unlike the narrow tables above which never execute.

const wchar_t* EXP_FULLNAME_W = L"FullName";
const wchar_t* EXP_DRIVE_W = L"Drive";
const wchar_t* EXP_PATH_W = L"Path";
const wchar_t* EXP_NAME_W = L"Name";
const wchar_t* EXP_NAMEPART_W = L"NamePart";
const wchar_t* EXP_EXTPART_W = L"ExtPart";
const wchar_t* EXP_FULLPATH_W = L"FullPath";
const wchar_t* EXP_WINDIR_W = L"WinDir";
const wchar_t* EXP_SYSDIR_W = L"SysDir";
const wchar_t* EXP_SALDIR_W = L"SalDir";
const wchar_t* EXP_DOSFULLNAME_W = L"DOSFullName";
const wchar_t* EXP_DOSDRIVE_W = L"DOSDrive";
const wchar_t* EXP_DOSPATH_W = L"DOSPath";
const wchar_t* EXP_DOSNAME_W = L"DOSName";
const wchar_t* EXP_DOSNAMEPART_W = L"DOSNamePart";
const wchar_t* EXP_DOSEXTPART_W = L"DOSExtPart";
const wchar_t* EXP_DOSFULLPATH_W = L"DOSFullPath";
const wchar_t* EXP_DOSWINDIR_W = L"DOSWinDir";
const wchar_t* EXP_DOSSYSDIR_W = L"DOSSysDir";

struct CExpDataW
{
    const wchar_t* LongName;
    const wchar_t* DosName;
};

// Everything up to and including the last backslash; empty if none found.
std::wstring WideDirWithBackslash(const std::wstring& path)
{
    size_t pos = path.find_last_of(L'\\');
    return pos == std::wstring::npos ? std::wstring() : path.substr(0, pos + 1);
}

// The root (drive or UNC share) of 'fullPath', without a trailing backslash.
std::wstring WideRootOf(const wchar_t* fullPath)
{
    std::wstring root;
    if (!SPLGetRootPathOwned(SG, fullPath, root))
        return std::wstring();
    if (!root.empty() && root.back() == L'\\')
        root.pop_back();
    return root;
}

static std::wstring QuerySystemDirectory(UINT(WINAPI* query)(LPWSTR, UINT))
{
    UINT capacity = 256;
    while (capacity < (std::numeric_limits<UINT>::max)() / 2)
    {
        std::vector<wchar_t> buffer(capacity, L'\0');
        const UINT length = query(buffer.data(), capacity);
        if (length == 0)
            return std::wstring();
        if (length < capacity)
            return std::wstring(buffer.data(), length);
        capacity = length + 1;
    }
    return std::wstring();
}

static std::wstring ShortPathOwned(const wchar_t* path)
{
    const DWORD needed = GetShortPathNameW(path, NULL, 0);
    if (needed == 0)
        return std::wstring();
    std::vector<wchar_t> buffer(static_cast<size_t>(needed) + 1, L'\0');
    const DWORD length = GetShortPathNameW(path, buffer.data(), static_cast<DWORD>(buffer.size()));
    return length > 0 && length < buffer.size() ? std::wstring(buffer.data(), length) : std::wstring();
}

// 'fullPath' with its root prefix removed, keeping the leading backslash.
std::wstring WideStripRoot(const wchar_t* fullPath)
{
    std::wstring root = WideRootOf(fullPath);
    std::wstring full(fullPath);
    return root.size() <= full.size() ? full.substr(root.size()) : full;
}

std::wstring WideFileName(const wchar_t* fullPath)
{
    std::wstring path(fullPath);
    size_t pos = path.find_last_of(L'\\');
    return pos == std::wstring::npos ? path : path.substr(pos + 1);
}

std::wstring WideNamePart(const wchar_t* fullPath)
{
    std::wstring name = WideFileName(fullPath);
    size_t dot = name.find_last_of(L'.');
    return dot == std::wstring::npos ? name : name.substr(0, dot);
}

std::wstring WideExtPart(const wchar_t* fullPath)
{
    std::wstring name = WideFileName(fullPath);
    size_t dot = name.find_last_of(L'.');
    return dot == std::wstring::npos ? std::wstring() : name.substr(dot + 1);
}

std::wstring ExecuteFullNameW(void* param)
{
    return ((CExpDataW*)param)->LongName;
}

std::wstring ExecuteDOSFullNameW(void* param)
{
    return ((CExpDataW*)param)->DosName;
}

std::wstring ExecuteDriveW(void* param)
{
    return WideRootOf(((CExpDataW*)param)->LongName);
}

std::wstring ExecuteDOSDriveW(void* param)
{
    return WideRootOf(((CExpDataW*)param)->DosName);
}

std::wstring ExecutePathW(void* param)
{
    return WideDirWithBackslash(WideStripRoot(((CExpDataW*)param)->LongName));
}

std::wstring ExecuteDOSPathW(void* param)
{
    return WideDirWithBackslash(WideStripRoot(((CExpDataW*)param)->DosName));
}

std::wstring ExecuteNameW(void* param)
{
    return WideFileName(((CExpDataW*)param)->LongName);
}

std::wstring ExecuteDOSNameW(void* param)
{
    return WideFileName(((CExpDataW*)param)->DosName);
}

std::wstring ExecuteNamePartW(void* param)
{
    return WideNamePart(((CExpDataW*)param)->LongName);
}

std::wstring ExecuteDOSNamePartW(void* param)
{
    return WideNamePart(((CExpDataW*)param)->DosName);
}

std::wstring ExecuteExtPartW(void* param)
{
    // ".cvspass" is an extension in Windows - WideExtPart matches that (last '.'
    // in the filename, same as the leading dot when there is only one).
    return WideExtPart(((CExpDataW*)param)->LongName);
}

std::wstring ExecuteDOSExtPartW(void* param)
{
    return WideExtPart(((CExpDataW*)param)->DosName);
}

std::wstring ExecuteFullPathW(void* param)
{
    return WideDirWithBackslash(((CExpDataW*)param)->LongName);
}

std::wstring ExecuteDOSFullPathW(void* param)
{
    return WideDirWithBackslash(((CExpDataW*)param)->DosName);
}

std::wstring ExecuteWinDirW(void* param)
{
    std::wstring dir = QuerySystemDirectory(GetWindowsDirectoryW);
    if (dir.empty() || dir.back() != L'\\')
        dir.push_back(L'\\');
    return dir;
}

std::wstring ExecuteDOSWinDirW(void* param)
{
    const std::wstring windowsDirectory = QuerySystemDirectory(GetWindowsDirectoryW);
    std::wstring dir = ShortPathOwned(windowsDirectory.c_str());
    if (dir.empty() || dir.back() != L'\\')
        dir.push_back(L'\\');
    return dir;
}

std::wstring ExecuteSysDirW(void* param)
{
    std::wstring dir = QuerySystemDirectory(GetSystemDirectoryW);
    if (dir.empty() || dir.back() != L'\\')
        dir.push_back(L'\\');
    return dir;
}

std::wstring ExecuteDOSSysDirW(void* param)
{
    const std::wstring systemDirectory = QuerySystemDirectory(GetSystemDirectoryW);
    std::wstring dir = ShortPathOwned(systemDirectory.c_str());
    if (dir.empty() || dir.back() != L'\\')
        dir.push_back(L'\\');
    return dir;
}

std::wstring ExecutePath2W(void* param)
{
    // Root-stripped, then a TRAILING backslash removed - not the last component.
    //
    // This borrowed WideDirWithBackslash from ExecutePathW, which is right there
    // and looks like the same job. It is not: that helper cuts back to the last
    // backslash, which pre-unicode's ExecutePath does and its ExecutePath2 never
    // did. ExecuteEditor feeds LongName through SPLCutDirectoryOwned, so it is
    // already a directory with no trailing backslash - and cutting it again lost
    // its last component, so "$(Path)" as an Initial Directory opened the parent
    // of the intended folder, or "\" outright for a first-level one.
    std::wstring dir = WideStripRoot(((CExpDataW*)param)->LongName);
    if (dir.size() > 1 && dir.back() == L'\\')
        dir.pop_back();
    return dir;
}

std::wstring ExecuteFullPath2W(void* param)
{
    return ((CExpDataW*)param)->LongName;
}

std::wstring ExecuteWinDir2W(void* param)
{
    std::wstring dir = ExecuteWinDirW(param);
    if (dir.size() > 1)
        dir.pop_back();
    return dir;
}

std::wstring ExecuteSysDir2W(void* param)
{
    std::wstring dir = ExecuteSysDirW(param);
    if (dir.size() > 1)
        dir.pop_back();
    return dir;
}

std::wstring ExecuteSalDirW(void* param)
{
    std::wstring module;
    // hInstance==NULL: we want the path to the EXE, not the DLL
    if (!SPLGetModuleFileNameOwned(NULL, module))
        return std::wstring();
    return WideDirWithBackslash(module.c_str());
}

sally::unicode::WideVarEntry CommandExpArrayW[] =
    {
        {EXP_WINDIR_W, ExecuteWinDirW},
        {EXP_SYSDIR_W, ExecuteSysDirW},
        {EXP_SALDIR_W, ExecuteSalDirW},
        {NULL, NULL}};

sally::unicode::WideVarEntry ArgumentsExpArrayW[] =
    {
        {EXP_FULLNAME_W, ExecuteFullNameW},
        {EXP_DRIVE_W, ExecuteDriveW},
        {EXP_PATH_W, ExecutePathW},
        {EXP_NAME_W, ExecuteNameW},
        {EXP_NAMEPART_W, ExecuteNamePartW},
        {EXP_EXTPART_W, ExecuteExtPartW},
        {EXP_FULLPATH_W, ExecuteFullPathW},
        {EXP_WINDIR_W, ExecuteWinDirW},
        {EXP_SYSDIR_W, ExecuteSysDirW},
        {EXP_DOSFULLNAME_W, ExecuteDOSFullNameW},
        {EXP_DOSDRIVE_W, ExecuteDOSDriveW},
        {EXP_DOSPATH_W, ExecuteDOSPathW},
        {EXP_DOSNAME_W, ExecuteDOSNameW},
        {EXP_DOSNAMEPART_W, ExecuteDOSNamePartW},
        {EXP_DOSEXTPART_W, ExecuteDOSExtPartW},
        {EXP_DOSFULLPATH_W, ExecuteDOSFullPathW},
        {EXP_DOSWINDIR_W, ExecuteDOSWinDirW},
        {EXP_DOSSYSDIR_W, ExecuteDOSSysDirW},
        {NULL, NULL}};

sally::unicode::WideVarEntry InitDirExpArrayW[] =
    {
        {EXP_DRIVE_W, ExecuteDriveW},
        {EXP_PATH_W, ExecutePath2W},
        {EXP_FULLPATH_W, ExecuteFullPath2W},
        {EXP_WINDIR_W, ExecuteWinDir2W},
        {EXP_SYSDIR_W, ExecuteSysDir2W},
        {NULL, NULL}};

BOOL RemoveDoubleBackslahesFromPath(wchar_t* text)
{
    if (text == NULL)
    {
        TRACE_E("Unexpected situation in RemoveDoubleBackslahesFromPath().");
        return FALSE;
    }
    int len = (int)wcslen(text);
    if (len < 3)
        return TRUE;
    wchar_t* source = text + 2; // UNC paths start with "\\"
    wchar_t* destination = source;
    while (*source != 0)
    {
        if (*source == L'\\' && *(source + 1) == L'\\')
            source++;
        *destination = *source;
        source++;
        destination++;
    }
    *destination = 0;
    return TRUE;
}

// Wide end to end - the flagged bug was ExecuteEditor's narrow longName/
// dosName scratch, built by narrowing a genuinely-wide temp-directory path via
// WideCharToMultiByte with no usedDefaultChar check. A non-ASCII TEMP path (e.g. a
// non-ASCII Windows username) silently corrupted the value these Expand*W calls then
// substitute into the external editor's command line and working directory. Staying
// wide throughout removes the narrowing step entirely instead of adding a check to it.
//
// sally::unicode::ExpandWideVarString's convenience wrapper hardcodes "ignore a missing
// $[env_var] and substitute empty, then keep going" - core's own ExpandVarString instead
// asks the user whether to continue (CSalamanderGeneralAbstract::ExpandVarString's
// ignoreEnvVarNotFoundOrTooLong=FALSE default, which is what every ExecuteEditor call
// site used). A plugin can't reach core's confirm-dialog machinery (gPrompter is
// core-internal), so approximate the safe side of that choice: fail outright on a
// missing/oversized environment variable rather than silently building a command line
// with a blank substitution in it. Calls ExpandWideVarStringCore directly (bypassing the
// convenience wrapper) to plug in that policy.
//
// Failing is only half of what core does, though: core's dialog TELLS the user which
// variable went wrong. Aborting without a word left the Edit-in-external-editor command
// looking like it had simply done nothing. So the error record is collected here and
// ReportEnvVarError below turns it into the same two messages core uses.
bool ExpandWideVarStringFailOnEnvError(const wchar_t* varText,
                                       const sally::unicode::WideVarEntry* variables,
                                       void* param, std::wstring* output,
                                       sally::unicode::WideVarError* error)
{
    const auto resolve = [&](const wchar_t* name, int nameLength, bool execute,
                             int requestedWidth, std::wstring& value, int& measurementWidth) {
        const sally::unicode::WideVarEntry* entry =
            sally::unicode::FindWideVarEntry(variables, name, nameLength);
        if (entry == nullptr)
            return sally::unicode::WideVarResolveResult::NotFound;
        if (!execute)
            return sally::unicode::WideVarResolveResult::Found;
        if (entry->Execute == nullptr)
            return sally::unicode::WideVarResolveResult::Failed;
        value = entry->Execute(param);
        measurementWidth = static_cast<int>(value.size());
        if (requestedWidth > 0)
        {
            if (value.size() > static_cast<std::size_t>(requestedWidth))
                value.resize(static_cast<std::size_t>(requestedWidth));
            else if (value.size() < static_cast<std::size_t>(requestedWidth))
                value.append(static_cast<std::size_t>(requestedWidth) - value.size(), L' ');
        }
        return sally::unicode::WideVarResolveResult::Found;
    };
    const auto failOnEnvironmentError = [](const sally::unicode::WideVarError&) { return false; };
    return sally::unicode::ExpandWideVarStringCore(
        varText, false, resolve, output, nullptr, false, nullptr, 0,
        (std::numeric_limits<std::size_t>::max)(), error, failOnEnvironmentError);
}

// The two messages core shows for the same two kinds, worded the same way. Anything else
// that can stop the walker here (an unmatched bracket, a bad width) has already been
// rejected by the SG->ValidateVarString call that precedes every expansion, so only the
// environment kinds are worth a message; the rest fall through silently as before.
static BOOL ReportEnvVarError(const sally::unicode::WideVarError& error)
{
    int resID;
    switch (error.Kind)
    {
    case sally::unicode::WideVarErrorKind::EnvironmentNotFound:
        resID = IDS_EXP_ENVVARNOTFOUND;
        break;
    case sally::unicode::WideVarErrorKind::EnvironmentTooLarge:
        resID = IDS_EXP_ENVVARTOOLARGE;
        break;
    default:
        return FALSE;
    }
    // Error() appends FormatMessage(GetLastError()) when it is not ERROR_SUCCESS, and
    // whatever is left over in it here has nothing to do with this failure.
    SetLastError(ERROR_SUCCESS);
    return Error(resID, error.Argument.c_str());
}

BOOL ExpandCommandW(const wchar_t* varText, std::wstring& expanded)
{
    CExpDataW data;
    data.LongName = NULL;
    data.DosName = NULL;
    sally::unicode::WideVarError error;
    if (!ExpandWideVarStringFailOnEnvError(varText, CommandExpArrayW, &data, &expanded, &error))
    {
        ReportEnvVarError(error);
        return FALSE;
    }
    // the WinDir/SysDir/SalDir variables end with a backslash; the user adds their own
    // backslash too, so the resulting path can contain two.
    std::vector<wchar_t> mutableText(expanded.begin(), expanded.end());
    mutableText.push_back(L'\0');
    RemoveDoubleBackslahesFromPath(mutableText.data()); // reduce double backslashes to one
    expanded.assign(mutableText.data());
    return TRUE;
}

BOOL ExpandInitDirW(const wchar_t* varText, std::wstring& directory,
                    const wchar_t* longName, const wchar_t* dosName)
{
    CExpDataW data;
    data.LongName = longName;
    data.DosName = dosName;
    sally::unicode::WideVarError error;
    if (!ExpandWideVarStringFailOnEnvError(varText, InitDirExpArrayW, &data, &directory, &error))
    {
        ReportEnvVarError(error);
        return FALSE;
    }
    return TRUE;
}

BOOL ExpandArgumentsW(const wchar_t* varText, std::wstring& arguments,
                      const wchar_t* longName, const wchar_t* dosName)
{
    CExpDataW data;
    data.LongName = longName;
    data.DosName = dosName;
    sally::unicode::WideVarError error;
    if (!ExpandWideVarStringFailOnEnvError(varText, ArgumentsExpArrayW, &data, &arguments, &error))
    {
        ReportEnvVarError(error);
        return FALSE;
    }
    return TRUE;
}

BOOL ExecuteEditor(const wchar_t* tempFile)
{
    CALL_STACK_MESSAGE1("ExecuteEditor()");
    std::wstring command, directory, arguments;

    // expand initdir
    std::wstring longNameW(tempFile);
    SPLCutDirectoryOwned(SG, longNameW);
    std::wstring dosNameW = ShortPathOwned(longNameW.c_str());

    int e1, e2;

    if (!SG->ValidateVarString(GetParent(), Command.c_str(), e1, e2, ExpCommandVariables) ||
        !ExpandCommandW(Command.c_str(), command))
        return FALSE;

    if (!SG->ValidateVarString(GetParent(), InitDir.c_str(), e1, e2, ExpInitDirVariables) ||
        !ExpandInitDirW(InitDir.c_str(), directory, longNameW.c_str(), dosNameW.c_str()))
        return FALSE;

    // expand arguments
    dosNameW = ShortPathOwned(tempFile);

    if (!SG->ValidateVarString(GetParent(), Arguments.c_str(), e1, e2, ExpArgumentsVariables) ||
        !ExpandArgumentsW(Arguments.c_str(), arguments, tempFile, dosNameW.c_str()))
        return FALSE;

    // run the command
    if (command.empty())
        return Error(IDS_PROCESS);
    std::wstring cmdLine = L"\"" + command + L"\" " + arguments;
    std::vector<wchar_t> mutableCmdLine(cmdLine.begin(), cmdLine.end());
    mutableCmdLine.push_back(L'\0');

    STARTUPINFOW startupInfo;
    PROCESS_INFORMATION pi;
    memset(&startupInfo, 0, sizeof(STARTUPINFOW));
    startupInfo.cb = sizeof(STARTUPINFOW);
    startupInfo.lpTitle = NULL;
    startupInfo.dwFlags = STARTF_USESHOWWINDOW;
    startupInfo.wShowWindow = SW_SHOWNORMAL;

    if (!CreateProcessW(NULL, mutableCmdLine.data(), NULL, NULL, FALSE, CREATE_DEFAULT_ERROR_MODE | NORMAL_PRIORITY_CLASS,
                        NULL, directory.empty() ? NULL : directory.c_str(), &startupInfo, &pi))
        return Error(IDS_ERRLAUNCHEDIT);

    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return TRUE;
}
