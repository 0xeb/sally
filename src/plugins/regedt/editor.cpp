// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"
#include "common/unicode/WideVariableExpansion.h"

// Wide: the underlying registry facade (SetValueAux/GetValueAux) reads and
// writes REG_SZ as raw UTF-16LE bytes via RegQueryValueExW/RegSetValueExW, so
// a narrow buffer here silently received/sent garbled wide bytes - the -1
// dataSize convention even ran wcslen() over what it thought were narrow
// bytes reinterpreted as wchar_t*. See the sibling renamer plugin for the
// full write-up (this is regedt's tick, following the same
// already-wide-source correction found in fs_registry_io.cpp).
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

struct CExpDataW
{
    const wchar_t* LongName;
    const wchar_t* DosName;
};

static std::wstring WideDirWithBackslash(const wchar_t* path)
{
    std::wstring value(path != NULL ? path : L"");
    const size_t pos = value.find_last_of(L'\\');
    return pos == std::wstring::npos ? std::wstring() : value.substr(0, pos + 1);
}

static std::wstring WideRootOf(const wchar_t* path)
{
    std::wstring value;
    if (!SPLGetRootPathOwned(SG, path, value))
        return std::wstring();
    if (!value.empty() && value.back() == L'\\')
        value.pop_back();
    return value;
}

static std::wstring WideStripRoot(const wchar_t* path)
{
    const std::wstring root = WideRootOf(path);
    const std::wstring value(path != NULL ? path : L"");
    return root.size() <= value.size() ? value.substr(root.size()) : value;
}

static std::wstring WideFileName(const wchar_t* path)
{
    const std::wstring value(path != NULL ? path : L"");
    const size_t pos = value.find_last_of(L'\\');
    return pos == std::wstring::npos ? value : value.substr(pos + 1);
}

static std::wstring WideNamePart(const wchar_t* path)
{
    std::wstring value = WideFileName(path);
    const size_t pos = value.find_last_of(L'.');
    return pos == std::wstring::npos ? value : value.substr(0, pos);
}

static std::wstring WideExtPart(const wchar_t* path)
{
    const std::wstring value = WideFileName(path);
    const size_t pos = value.find_last_of(L'.');
    return pos == std::wstring::npos ? std::wstring() : value.substr(pos + 1);
}

static std::wstring ShortPathOf(const wchar_t* path)
{
    std::wstring value;
    return SPLGetShortPathNameOwned(path, value) ? value : std::wstring();
}

static std::wstring WindowsDirectory(BOOL shortName, BOOL trailingBackslash)
{
    std::wstring value;
    if (!SPLGetWindowsDirectoryOwned(value))
        return std::wstring();
    std::wstring result = shortName ? ShortPathOf(value.c_str()) : value;
    if (trailingBackslash && !result.empty() && result.back() != L'\\')
        result.push_back(L'\\');
    if (!trailingBackslash && result.size() > 1 && result.back() == L'\\')
        result.pop_back();
    return result;
}

static std::wstring SystemDirectory(BOOL shortName, BOOL trailingBackslash)
{
    std::wstring value;
    if (!SPLGetSystemDirectoryOwned(value))
        return std::wstring();
    std::wstring result = shortName ? ShortPathOf(value.c_str()) : value;
    if (trailingBackslash && !result.empty() && result.back() != L'\\')
        result.push_back(L'\\');
    if (!trailingBackslash && result.size() > 1 && result.back() == L'\\')
        result.pop_back();
    return result;
}

static std::wstring ExecuteFullNameW(void* p) { return ((CExpDataW*)p)->LongName; }
static std::wstring ExecuteDOSFullNameW(void* p) { return ((CExpDataW*)p)->DosName; }
static std::wstring ExecuteDriveW(void* p) { return WideRootOf(((CExpDataW*)p)->LongName); }
static std::wstring ExecuteDOSDriveW(void* p) { return WideRootOf(((CExpDataW*)p)->DosName); }
static std::wstring ExecutePathW(void* p) { return WideDirWithBackslash(WideStripRoot(((CExpDataW*)p)->LongName).c_str()); }
static std::wstring ExecuteDOSPathW(void* p) { return WideDirWithBackslash(WideStripRoot(((CExpDataW*)p)->DosName).c_str()); }
static std::wstring ExecuteNameW(void* p) { return WideFileName(((CExpDataW*)p)->LongName); }
static std::wstring ExecuteDOSNameW(void* p) { return WideFileName(((CExpDataW*)p)->DosName); }
static std::wstring ExecuteNamePartW(void* p) { return WideNamePart(((CExpDataW*)p)->LongName); }
static std::wstring ExecuteDOSNamePartW(void* p) { return WideNamePart(((CExpDataW*)p)->DosName); }
static std::wstring ExecuteExtPartW(void* p) { return WideExtPart(((CExpDataW*)p)->LongName); }
static std::wstring ExecuteDOSExtPartW(void* p) { return WideExtPart(((CExpDataW*)p)->DosName); }
static std::wstring ExecuteFullPathW(void* p) { return WideDirWithBackslash(((CExpDataW*)p)->LongName); }
static std::wstring ExecuteDOSFullPathW(void* p) { return WideDirWithBackslash(((CExpDataW*)p)->DosName); }
static std::wstring ExecuteWinDirW(void*) { return WindowsDirectory(FALSE, TRUE); }
static std::wstring ExecuteDOSWinDirW(void*) { return WindowsDirectory(TRUE, TRUE); }
static std::wstring ExecuteSysDirW(void*) { return SystemDirectory(FALSE, TRUE); }
static std::wstring ExecuteDOSSysDirW(void*) { return SystemDirectory(TRUE, TRUE); }
static std::wstring ExecutePath2W(void* p)
{
    // $(Path) in the Initial Directory table is fed a DIRECTORY that
    // ExecuteEditor has already cut, so it only loses its root and a trailing
    // backslash. ExecutePathW above is the one fed a FILE path and therefore
    // the one that has to cut back to the containing directory.
    std::wstring value = WideStripRoot(((CExpDataW*)p)->LongName);
    if (value.size() > 1 && value.back() == L'\\')
        value.pop_back();
    return value;
}
static std::wstring ExecuteFullPath2W(void* p) { return ((CExpDataW*)p)->LongName; }
static std::wstring ExecuteWinDir2W(void*) { return WindowsDirectory(FALSE, FALSE); }
static std::wstring ExecuteSysDir2W(void*) { return SystemDirectory(FALSE, FALSE); }
static std::wstring ExecuteSalDirW(void*)
{
    std::wstring module;
    return SPLGetModuleFileNameOwned(NULL, module) ? WideDirWithBackslash(module.c_str()) : L"";
}

static sally::unicode::WideVarEntry CommandExpArrayW[] = {
    {L"WinDir", ExecuteWinDirW}, {L"SysDir", ExecuteSysDirW}, {L"SalDir", ExecuteSalDirW}, {NULL, NULL}};
static sally::unicode::WideVarEntry ArgumentsExpArrayW[] = {
    {L"FullName", ExecuteFullNameW}, {L"Drive", ExecuteDriveW}, {L"Path", ExecutePathW},
    {L"Name", ExecuteNameW}, {L"NamePart", ExecuteNamePartW}, {L"ExtPart", ExecuteExtPartW},
    {L"FullPath", ExecuteFullPathW}, {L"WinDir", ExecuteWinDirW}, {L"SysDir", ExecuteSysDirW},
    {L"DOSFullName", ExecuteDOSFullNameW}, {L"DOSDrive", ExecuteDOSDriveW},
    {L"DOSPath", ExecuteDOSPathW}, {L"DOSName", ExecuteDOSNameW},
    {L"DOSNamePart", ExecuteDOSNamePartW}, {L"DOSExtPart", ExecuteDOSExtPartW},
    {L"DOSFullPath", ExecuteDOSFullPathW}, {L"DOSWinDir", ExecuteDOSWinDirW},
    {L"DOSSysDir", ExecuteDOSSysDirW}, {NULL, NULL}};
static sally::unicode::WideVarEntry InitDirExpArrayW[] = {
    {L"Drive", ExecuteDriveW}, {L"Path", ExecutePath2W}, {L"FullPath", ExecuteFullPath2W},
    {L"WinDir", ExecuteWinDir2W}, {L"SysDir", ExecuteSysDir2W}, {NULL, NULL}};

static BOOL ExpandWide(const wchar_t* text, const sally::unicode::WideVarEntry* variables,
                       CExpDataW* data, std::wstring& expanded)
{
    const auto resolve = [&](const wchar_t* name, int nameLength, bool execute,
                             int requestedWidth, std::wstring& value, int& measurementWidth) {
        const sally::unicode::WideVarEntry* entry =
            sally::unicode::FindWideVarEntry(variables, name, nameLength);
        if (entry == NULL)
            return sally::unicode::WideVarResolveResult::NotFound;
        if (!execute)
            return sally::unicode::WideVarResolveResult::Found;
        if (entry->Execute == NULL)
            return sally::unicode::WideVarResolveResult::Failed;
        value = entry->Execute(data);
        measurementWidth = (int)value.size();
        if (requestedWidth > 0)
        {
            const size_t width = (size_t)requestedWidth;
            if (value.size() > width)
                value.resize(width);
            else if (value.size() < width)
                value.append(width - value.size(), L' ');
        }
        return sally::unicode::WideVarResolveResult::Found;
    };
    const auto failOnEnvironmentError = [](const sally::unicode::WideVarError&) { return false; };
    sally::unicode::WideVarError error;
    if (!sally::unicode::ExpandWideVarStringCore(text, false, resolve, &expanded, NULL,
                                                 false, NULL, 0,
                                                 (std::numeric_limits<std::size_t>::max)(), &error,
                                                 failOnEnvironmentError))
        return FALSE;
    return TRUE;
}

static void RemoveDoubleBackslashesFromPath(std::wstring& text)
{
    if (text.size() < 3)
        return;
    size_t source = 2; // UNC paths start with "\\"
    size_t destination = source;
    while (source < text.size())
    {
        if (text[source] == L'\\' && source + 1 < text.size() && text[source + 1] == L'\\')
            source++;
        text[destination] = text[source];
        source++;
        destination++;
    }
    text.resize(destination);
}

static BOOL ExpandCommand(const wchar_t* varText, std::wstring& result)
{
    CALL_STACK_MESSAGE2("ExpandCommand(, %ls, , ,)", varText);
    CExpDataW data;
    data.LongName = NULL;
    data.DosName = NULL;
    if (ExpandWide(varText, CommandExpArrayW, &data, result))
    {
        // the EXECUTE_WINDIR, EXECUTE_SYSDIR, and EXECUTE_SALDIR variables end with a backslash
        // the user adds their own backslash, so the resulting path contains two of them
        RemoveDoubleBackslashesFromPath(result); // trim double backslashes down to one
        return TRUE;
    }
    else
        return FALSE;
}

static BOOL ExpandInitDir(const wchar_t* varText, const wchar_t* longName,
                          const wchar_t* dosName, std::wstring& directory)
{
    CALL_STACK_MESSAGE4("ExpandInitDir(%ls, , %ls, %ls)", varText, longName, dosName);
    CExpDataW data;
    data.LongName = longName;
    data.DosName = dosName;
    return ExpandWide(varText, InitDirExpArrayW, &data, directory);
}

static BOOL ExpandArguments(const wchar_t* varText, const wchar_t* longName,
                            const wchar_t* dosName, std::wstring& arguments)
{
    CALL_STACK_MESSAGE4("ExpandArguments(%ls, , %ls, %ls)", varText, longName,
                        dosName);
    CExpDataW data;
    data.LongName = longName;
    data.DosName = dosName;
    return ExpandWide(varText, ArgumentsExpArrayW, &data, arguments);
}

BOOL ExecuteEditor(const wchar_t* tempFile)
{
    CALL_STACK_MESSAGE2("ExecuteEditor(%ls)", tempFile);
    std::wstring command;
    std::wstring directory;
    std::wstring arguments;

    // expand the initdir
    std::wstring longNameW(tempFile);
    SPLCutDirectoryOwned(SG, longNameW);
    std::wstring dosNameW;
    SPLGetShortPathNameOwned(longNameW.c_str(), dosNameW);

    int e1, e2;
    if (!SG->ValidateVarString(GetParent(), Command.c_str(), e1, e2, ExpCommandVariables) ||
        !ExpandCommand(Command.c_str(), command))
        return FALSE;

    if (!SG->ValidateVarString(GetParent(), InitDir.c_str(), e1, e2, ExpInitDirVariables) ||
        !ExpandInitDir(InitDir.c_str(), longNameW.c_str(), dosNameW.c_str(), directory))
        return FALSE;

    // expand the arguments
    SPLGetShortPathNameOwned(tempFile, dosNameW);

    if (!SG->ValidateVarString(GetParent(), Arguments.c_str(), e1, e2, ExpArgumentsVariables) ||
        !ExpandArguments(Arguments.c_str(), tempFile, dosNameW.c_str(), arguments))
        return FALSE;

    // run the command
    if (command.empty())
        return Error(IDS_PROCESS);
    std::wstring cmdLine = L"\"" + command + L"\" " + arguments;

    STARTUPINFOW startupInfo;
    PROCESS_INFORMATION pi;
    memset(&startupInfo, 0, sizeof(STARTUPINFOW));
    startupInfo.cb = sizeof(STARTUPINFOW);
    startupInfo.lpTitle = NULL;
    startupInfo.dwFlags = STARTF_USESHOWWINDOW;
    startupInfo.wShowWindow = SW_SHOWNORMAL;

    if (!CreateProcessW(NULL, cmdLine.data(), NULL, NULL, FALSE, CREATE_DEFAULT_ERROR_MODE | NORMAL_PRIORITY_CLASS,
                        NULL, directory.empty() ? NULL : directory.c_str(), &startupInfo, &pi))
        return Error(IDS_PROCESS);

    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return TRUE;
}
