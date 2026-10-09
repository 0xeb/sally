// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

// salupdate.exe: installs a downloaded Sally release into the folder Sally runs from.
// See common/update/UpdaterCommand.h for how Sally starts it (CheckVer's Install and Restart)
// and how to run it by hand with a downloaded release. Started by CheckVer it shows no window:
// what happened is left in the result file for the next Sally to report. Started by hand it asks
// and reports in Sally's language. Either way salupdate.log, next to the result file, keeps the
// details.

#include <windows.h>
#include <shellapi.h>

#include <algorithm>
#include <string>
#include <vector>

#include "ExtendedLengthPath.h"
#include "common/BuiltinLanguages.h"
#include "registry_names.h"
#include "salupdate.rh"
#include "Win32TextCodec.h"
#include "Win32UpdateFileOps.h"
#include "update/ReleaseChecksums.h"
#include "update/UpdateFiles.h"
#include "update/UpdatePackage.h"
#include "update/UpdateProcesses.h"
#include "update/UpdaterCommand.h"
#include "zip/ZipReader.h"

using namespace Sally::Update;

namespace
{

// How long Sally gets to close (it may still be saving its configuration), then how long
// other programs started from its folder get after that.
const DWORD kSallyExitTimeoutMs = 60 * 1000;
const DWORD kFolderIdleTimeoutMs = 30 * 1000;

std::wstring LogPath;

void Log(const std::wstring& text)
{
    if (LogPath.empty())
        return;
    SYSTEMTIME now;
    GetLocalTime(&now);
    wchar_t stamp[64];
    swprintf_s(stamp, L"%04u-%02u-%02u %02u:%02u:%02u [%lu] ", now.wYear, now.wMonth, now.wDay, now.wHour,
               now.wMinute, now.wSecond, GetCurrentProcessId());
    const std::wstring line = stamp + text + L"\r\n";
    std::string utf8;
    if (!Win32EncodeText(CP_UTF8, line, utf8))
        return;
    HANDLE file = CreateFileW(Sally::ToExtendedLengthPath(LogPath).c_str(), FILE_APPEND_DATA,
                              FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE)
        return;
    DWORD written = 0;
    WriteFile(file, utf8.data(), static_cast<DWORD>(utf8.size()), &written, NULL);
    CloseHandle(file);
}

std::wstring ParentFolder(const std::wstring& path)
{
    const size_t slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? std::wstring() : path.substr(0, slash);
}

std::wstring ModulePath()
{
    return ModuleFilePath(NULL);
}

bool Finish(const std::wstring& resultFile, const UpdateResult& result)
{
    Log(L"result: " + std::to_wstring(static_cast<int>(result.Status)) +
        (result.Message.empty() ? std::wstring() : L" - " + result.Message) +
        (result.FailedPath.empty() ? std::wstring() : L" (" + result.FailedPath + L", " + std::to_wstring(result.Code) + L")"));
    if (!WriteUpdateResult(resultFile, result))
        Log(L"cannot write the result file " + resultFile);
    return result.Status == UpdateStatus::Prepared || result.Status == UpdateStatus::Updated;
}

bool RemoveTree(const std::wstring& folder)
{
    WIN32_FIND_DATAW find;
    HANDLE handle = FindFirstFileExW(Sally::ToExtendedLengthPath(folder + L"\\*").c_str(), FindExInfoBasic, &find,
                                     FindExSearchNameMatch, NULL, 0);
    if (handle != INVALID_HANDLE_VALUE)
    {
        do
        {
            const std::wstring name = find.cFileName;
            if (name == L"." || name == L"..")
                continue;
            const std::wstring child = folder + L"\\" + name;
            const std::wstring extended = Sally::ToExtendedLengthPath(child);
            if ((find.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
            {
                // A link to a folder is removed, never followed.
                if ((find.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) == 0 && !RemoveTree(child))
                {
                    FindClose(handle);
                    return false;
                }
                if ((find.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0 && !RemoveDirectoryW(extended.c_str()))
                {
                    FindClose(handle);
                    return false;
                }
            }
            else
            {
                if ((find.dwFileAttributes & FILE_ATTRIBUTE_READONLY) != 0)
                    SetFileAttributesW(extended.c_str(), find.dwFileAttributes & ~FILE_ATTRIBUTE_READONLY);
                if (!DeleteFileW(extended.c_str()))
                {
                    FindClose(handle);
                    return false;
                }
            }
        } while (FindNextFileW(handle, &find));
        FindClose(handle);
    }
    const BOOL removed = RemoveDirectoryW(Sally::ToExtendedLengthPath(folder).c_str());
    return removed || GetLastError() == ERROR_FILE_NOT_FOUND || GetLastError() == ERROR_PATH_NOT_FOUND;
}

// --prepare: verify, extract and check the release, and put a copy of this program next to it.
int Prepare(const UpdaterCommand& command)
{
    UpdateResult result;
    result.ToVersion = command.Version;
    Log(L"prepare " + command.Package + L" -> " + command.Staging);

    std::string actual;
    DWORD error = 0;
    std::string expected;
    for (wchar_t ch : command.Sha256)
        expected.push_back(static_cast<char>(ch >= L'A' && ch <= L'F' ? ch - L'A' + L'a' : ch));
    if (!ComputeFileSha256(command.Package, actual, error))
    {
        result.FailedPath = command.Package;
        result.Code = error;
        result.Message = L"Cannot read the downloaded package.";
        return Finish(command.ResultFile, result) ? 0 : 1;
    }
    if (actual != expected)
    {
        result.FailedPath = command.Package;
        result.Message = L"The downloaded package does not match its published SHA-256 checksum.";
        return Finish(command.ResultFile, result) ? 0 : 1;
    }

    const std::wstring files = StagedFilesFolder(command.Staging);
    if (!RemoveTree(files))
    {
        result.FailedPath = files;
        result.Code = GetLastError();
        result.Message = L"Cannot clear the folder for the new version.";
        return Finish(command.ResultFile, result) ? 0 : 1;
    }
    CreateDirectoryW(Sally::ToExtendedLengthPath(command.Staging).c_str(), NULL);
    if (!CreateDirectoryW(Sally::ToExtendedLengthPath(files).c_str(), NULL))
    {
        result.FailedPath = files;
        result.Code = GetLastError();
        result.Message = L"Cannot create the folder for the new version.";
        return Finish(command.ResultFile, result) ? 0 : 1;
    }

    Sally::Zip::ZipReader reader;
    if (!reader.Open(command.Package) || !reader.ExtractAll(files))
    {
        result.FailedPath = command.Package;
        result.Code = reader.ErrorCode();
        result.Message = L"The package cannot be unpacked: " + reader.ErrorText();
        RemoveTree(files);
        return Finish(command.ResultFile, result) ? 0 : 1;
    }
    reader.Close();

    std::wstring problem;
    if (!CheckStagedRelease(files, command.Version, command.Arch, problem))
    {
        result.FailedPath = command.Package;
        result.Message = problem;
        RemoveTree(files);
        return Finish(command.ResultFile, result) ? 0 : 1;
    }

    // The install step runs from this copy, so that the installed salupdate.exe can be
    // replaced like every other file.
    const std::wstring copy = command.Staging + L"\\salupdate.exe";
    if (!CopyFileW(Sally::ToExtendedLengthPath(ModulePath()).c_str(), Sally::ToExtendedLengthPath(copy).c_str(), FALSE))
    {
        result.FailedPath = copy;
        result.Code = GetLastError();
        result.Message = L"Cannot copy the updater.";
        return Finish(command.ResultFile, result) ? 0 : 1;
    }
    result.Status = UpdateStatus::Prepared;
    return Finish(command.ResultFile, result) ? 0 : 1;
}

bool CanWriteTo(const std::wstring& folder, DWORD& error)
{
    const std::wstring probe = folder + L"\\sally-update-" + std::to_wstring(GetCurrentProcessId()) + L".probe";
    HANDLE file = CreateFileW(Sally::ToExtendedLengthPath(probe).c_str(), GENERIC_WRITE, 0, NULL, CREATE_NEW,
                              FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, NULL);
    if (file == INVALID_HANDLE_VALUE)
    {
        error = GetLastError();
        return false;
    }
    CloseHandle(file);
    return true;
}

void Relaunch(const std::wstring& installRoot)
{
    const std::wstring exe = installRoot + L"\\sally.exe";
    std::wstring commandLine = QuoteCommandLineArgument(exe);
    STARTUPINFOW startup = {};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process = {};
    if (CreateProcessW(exe.c_str(), &commandLine[0], NULL, NULL, FALSE, 0, NULL, installRoot.c_str(), &startup,
                       &process))
    {
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
        Log(L"started " + exe);
    }
    else
    {
        Log(L"cannot start " + exe + L": " + std::to_wstring(GetLastError()));
    }
}

// Runs the install step again with administrator rights, for a Sally folder that this
// user cannot change (Program Files), and waits for it.
bool RunElevated(const UpdaterCommand& command, DWORD& error)
{
    UpdaterCommand elevated = command;
    elevated.Elevated = true;
    elevated.Relaunch = false;
    elevated.WaitPids.clear();
    const std::wstring exe = ModulePath();
    const std::wstring parameters = FormatUpdaterCommand(elevated);
    SHELLEXECUTEINFOW info = {};
    info.cbSize = sizeof(info);
    info.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC | SEE_MASK_FLAG_NO_UI;
    info.lpVerb = L"runas";
    info.lpFile = exe.c_str();
    info.lpParameters = parameters.c_str();
    info.nShow = SW_HIDE;
    if (!ShellExecuteExW(&info) || info.hProcess == NULL)
    {
        error = GetLastError();
        return false;
    }
    WaitForSingleObject(info.hProcess, INFINITE);
    CloseHandle(info.hProcess);
    return true;
}

// --- the updater's own messages (an update started by hand) ------------------------------------

LANGID Language = sally::languages::kEnglishLangId;

std::wstring LoadTextFor(UINT id, LANGID language)
{
    HMODULE module = GetModuleHandleW(NULL);
    HRSRC found = FindResourceExW(module, RT_STRING, MAKEINTRESOURCEW(id / 16 + 1), language);
    if (found == NULL)
        return std::wstring();
    HGLOBAL loaded = LoadResource(module, found);
    const WCHAR* data = loaded != NULL ? static_cast<const WCHAR*>(LockResource(loaded)) : NULL;
    if (data == NULL)
        return std::wstring();
    const WCHAR* end = data + SizeofResource(module, found) / sizeof(WCHAR);
    for (UINT i = 0; i < id % 16; i++)
    {
        if (data >= end)
            return std::wstring();
        data += 1 + *data;
    }
    if (data >= end || data + 1 + *data > end)
        return std::wstring();
    return std::wstring(data + 1, *data);
}

std::wstring Text(UINT id)
{
    std::wstring text = LoadTextFor(id, Language);
    return text.empty() ? LoadTextFor(id, sally::languages::kEnglishLangId) : text;
}

template <typename... Args> std::wstring Text(UINT id, Args... args)
{
    const std::wstring format = Text(id);
    const int length = _scwprintf(format.c_str(), args...);
    if (length < 0)
        return format;
    std::wstring text(static_cast<size_t>(length) + 1, L'\0');
    swprintf_s(&text[0], text.size(), format.c_str(), args...);
    text.resize(static_cast<size_t>(length));
    return text;
}

// The language Sally is set to (its configuration in the registry), else the first of the user's
// languages Sally has, else English.
void ChooseLanguage()
{
    HKEY key = NULL;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, SAL_REG_ROOT_SALLY_1_0_W L"\\Configuration", 0, KEY_QUERY_VALUE, &key) ==
        ERROR_SUCCESS)
    {
        wchar_t name[64] = {};
        DWORD size = sizeof(name) - sizeof(wchar_t);
        DWORD type = 0;
        if (RegQueryValueExW(key, L"Language", NULL, &type, reinterpret_cast<BYTE*>(name), &size) == ERROR_SUCCESS &&
            type == REG_SZ)
        {
            const LANGID configured = sally::languages::LangIdFromPersistedName(name);
            if (configured != 0)
                Language = configured;
        }
        RegCloseKey(key);
        if (Language != sally::languages::kEnglishLangId)
            return;
    }
    std::vector<LANGID> available;
    for (const sally::languages::BuiltinLanguage& language : sally::languages::kBuiltinLanguages)
        available.push_back(language.LangId);
    std::vector<std::wstring> preferred;
    ULONG count = 0;
    ULONG chars = 0;
    if (GetUserPreferredUILanguages(MUI_LANGUAGE_NAME, &count, NULL, &chars) && chars > 0)
    {
        std::wstring names(chars, L'\0');
        if (GetUserPreferredUILanguages(MUI_LANGUAGE_NAME, &count, &names[0], &chars))
        {
            for (size_t start = 0; start < names.size() && names[start] != L'\0';)
            {
                const size_t end = names.find(L'\0', start);
                preferred.push_back(names.substr(start, end - start));
                start = end + 1;
            }
        }
    }
    Language = sally::languages::ChooseDefault(available, preferred, nullptr);
}

void Tell(const std::wstring& text, UINT icon)
{
    MessageBoxW(NULL, text.c_str(), Text(IDS_UPDATER_TITLE).c_str(), MB_OK | icon | MB_SETFOREGROUND | MB_TOPMOST);
}

bool Ask(const std::wstring& text)
{
    return MessageBoxW(NULL, text.c_str(), Text(IDS_UPDATER_TITLE).c_str(),
                       MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2 | MB_SETFOREGROUND | MB_TOPMOST) == IDYES;
}

std::wstring FileNameOf(const std::wstring& path)
{
    const size_t slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? path : path.substr(slash + 1);
}

std::wstring SystemErrorText(DWORD error)
{
    wchar_t* allocated = NULL;
    const DWORD count =
        FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                       NULL, error, Language, reinterpret_cast<wchar_t*>(&allocated), 0, NULL);
    std::wstring text = count > 0 && allocated != NULL ? std::wstring(allocated, count) : std::wstring();
    if (allocated != NULL)
        LocalFree(allocated);
    while (!text.empty() && (text.back() == L'\n' || text.back() == L'\r' || text.back() == L' '))
        text.pop_back();
    return text.empty() ? L"error " + std::to_wstring(error) : text;
}

// "1.0.36.188" -> "1.0.36", as Sally's About box and the release notes write it.
std::wstring ReleaseVersion(const std::wstring& fileVersion)
{
    size_t dots = 0;
    for (size_t i = 0; i < fileVersion.size(); ++i)
    {
        if (fileVersion[i] == L'.' && ++dots == 3)
            return fileVersion.substr(0, i);
    }
    return fileVersion;
}

void ShowOutcome(const UpdateResult& result)
{
    std::wstring detail = result.Message;
    if (!result.FailedPath.empty())
        detail += L"\n" + result.FailedPath;
    if (result.Code != 0)
        detail += L"\n" + SystemErrorText(result.Code);
    switch (result.Status)
    {
    case UpdateStatus::Updated:
        Tell(Text(IDS_UPDATER_DONE, result.FromVersion.c_str(), result.ToVersion.c_str()), MB_ICONINFORMATION);
        break;
    case UpdateStatus::Aborted:
        Tell(Text(IDS_UPDATER_ABORTED, result.ToVersion.c_str()), MB_ICONWARNING);
        break;
    default:
        Tell(result.RollbackComplete ? Text(IDS_UPDATER_FAILED, result.ToVersion.c_str(), detail.c_str())
                                     : Text(IDS_UPDATER_INCOMPLETE, result.ToVersion.c_str(), detail.c_str()),
             MB_ICONERROR);
        break;
    }
}

// The outcome of an update started by hand is shown here, not reported by the next Sally.
void Conclude(const UpdaterCommand& command, const UpdateResult* result)
{
    if (command.Relaunch && !command.Elevated)
        Relaunch(command.InstallRoot);
    if (!command.Notify || command.Elevated)
        return;
    UpdateResult written;
    if (result == NULL && ReadUpdateResult(command.ResultFile, written))
        result = &written;
    if (result != NULL)
        ShowOutcome(*result);
    DeleteFileW(Sally::ToExtendedLengthPath(command.ResultFile).c_str());
}

// --apply: wait for Sally to close, replace the files, start Sally again.
int Apply(const UpdaterCommand& command)
{
    UpdateResult result;
    result.InstallRoot = command.InstallRoot;
    result.FromVersion = command.FromVersion;
    result.ToVersion = command.ToVersion;
    Log(L"apply " + command.Staging + L" -> " + command.InstallRoot + (command.Elevated ? L" (elevated)" : L""));

    if (!command.Elevated)
    {
        if (!WaitForProcessesToExit(command.WaitPids, kSallyExitTimeoutMs))
        {
            // Sally is still running: the user cancelled closing it. Nothing is changed.
            result.Status = UpdateStatus::Aborted;
            result.Message = L"Sally did not close.";
            Finish(command.ResultFile, result);
            UpdaterCommand stay = command;
            stay.Relaunch = false;
            Conclude(stay, &result);
            return 3;
        }
        const ULONGLONG deadline = GetTickCount64() + kFolderIdleTimeoutMs;
        std::vector<RunningProcess> running = FindProcessesInFolder(command.InstallRoot, GetCurrentProcessId());
        while (!running.empty() && GetTickCount64() < deadline)
        {
            Sleep(250);
            running = FindProcessesInFolder(command.InstallRoot, GetCurrentProcessId());
        }
        if (!running.empty())
        {
            result.Status = UpdateStatus::Aborted;
            result.FailedPath = running.front().Image;
            result.Message = L"A program from the Sally folder is still running.";
            Finish(command.ResultFile, result);
            Conclude(command, &result);
            return 3;
        }

        DWORD error = 0;
        if (!CanWriteTo(command.InstallRoot, error))
        {
            Log(L"the Sally folder is not writable (" + std::to_wstring(error) + L"), asking for elevation");
            DWORD elevationError = 0;
            if (!RunElevated(command, elevationError))
            {
                result.Status = UpdateStatus::Failed;
                result.FailedPath = command.InstallRoot;
                result.Code = elevationError;
                result.Message = elevationError == ERROR_CANCELLED
                                     ? L"Administrator permission to change the Sally folder was not given."
                                     : L"The update could not be started with administrator rights.";
                Finish(command.ResultFile, result);
                Conclude(command, &result);
                return 1;
            }
            Conclude(command, NULL); // the elevated run wrote the result
            return 0;
        }
    }

    Win32UpdateFileOps ops;
    UpdatePlan plan;
    UpdateFailure failure;
    if (!BuildUpdatePlan(StagedFilesFolder(command.Staging), command.InstallRoot, ops, plan, failure))
    {
        result.Status = UpdateStatus::Failed;
        result.FailedPath = failure.Path;
        result.Code = failure.Code;
        result.Message = failure.What;
    }
    else
    {
        Log(L"replacing " + std::to_wstring(plan.Files.size()) + L" files");
        const ApplyOutcome outcome = ApplyUpdate(plan, ops);
        result.Status = outcome.Ok ? UpdateStatus::Updated : UpdateStatus::Failed;
        result.FailedPath = outcome.Failure.Path;
        result.Code = outcome.Failure.Code;
        result.Message = outcome.Failure.What;
        result.RolledBack = outcome.RolledBack;
        result.RollbackComplete = outcome.RollbackComplete;
        result.Leftovers = outcome.Leftovers;
        // A file still in use (the shell extension Explorer holds) stays renamed aside and listed;
        // Sally deletes it at a later start. Nothing is scheduled in the machine's registry.
        for (const std::wstring& leftover : outcome.Leftovers)
            Log(L"still in use, left for later: " + leftover);
    }
    Finish(command.ResultFile, result);
    Conclude(command, &result);
    return result.Status == UpdateStatus::Updated ? 0 : 1;
}

struct SallyWindows
{
    std::vector<DWORD> Pids;
    std::vector<HWND> Windows;
};

BOOL CALLBACK CollectSallyWindow(HWND window, LPARAM param)
{
    SallyWindows* search = reinterpret_cast<SallyWindows*>(param);
    DWORD pid = 0;
    GetWindowThreadProcessId(window, &pid);
    wchar_t className[64];
    if (std::find(search->Pids.begin(), search->Pids.end(), pid) != search->Pids.end() &&
        GetClassNameW(window, className, _countof(className)) > 0 &&
        wcscmp(className, L"SalamanderMainWindowVer25") == 0)
        search->Windows.push_back(window);
    return TRUE;
}

// salupdate [<zip>] [--yes] [--install <folder>]: an update started by hand, offline.
int Manual(const UpdaterCommand& command)
{
    std::wstring install = command.InstallRoot.empty() ? InstallRootOfUpdater(ModulePath()) : command.InstallRoot;
    while (!install.empty() && (install.back() == L'\\' || install.back() == L'/'))
        install.pop_back();
    const std::wstring installedExe = install + L"\\sally.exe";
    if (install.empty() || GetFileAttributesW(Sally::ToExtendedLengthPath(installedExe).c_str()) == INVALID_FILE_ATTRIBUTES)
    {
        Tell(Text(IDS_UPDATER_NOT_IN_SALLY, ModulePath().c_str()), MB_ICONERROR);
        return 2;
    }
    const std::wstring work = UpdateWorkFolder();
    if (!work.empty())
        LogPath = work + L"\\salupdate.log";
    Log(L"manual " + (command.Package.empty() ? std::wstring(L"(no package)") : command.Package) + L" -> " + install);

    // An update that was cut off (a power failure) is undone first.
    Win32UpdateFileOps ops;
    if (ops.Query(install + L"\\" + kJournalFileName) == PathKind::File)
    {
        UpdateFailure failure;
        if (RecoverInterruptedUpdate(install, ops, failure))
            Tell(Text(IDS_UPDATER_RECOVERED), MB_ICONINFORMATION);
        else
        {
            Tell(Text(IDS_UPDATER_RECOVER_FAILED, (failure.What + L"\n" + failure.Path).c_str()), MB_ICONERROR);
            return 1;
        }
        if (command.Package.empty())
            return 0;
    }
    if (command.Package.empty())
    {
        Tell(Text(IDS_UPDATER_USAGE), MB_ICONINFORMATION);
        return 0;
    }

    std::wstring package(32, L'\0');
    const DWORD needed = GetFullPathNameW(command.Package.c_str(), 0, NULL, NULL);
    package.resize(needed > 0 ? needed : 1);
    const DWORD written = GetFullPathNameW(command.Package.c_str(), static_cast<DWORD>(package.size()), &package[0], NULL);
    package.resize(written > 0 && written < package.size() ? written : 0);
    if (package.empty())
        package = command.Package;
    const std::wstring packageName = FileNameOf(package);

    // The checksum the release publishes, when its SHA256SUMS file lies beside the package.
    std::string published;
    std::wstring sumsPath;
    if (FindChecksumNextTo(package, published, sumsPath))
    {
        std::string actual;
        DWORD error = 0;
        if (!ComputeFileSha256(package, actual, error) || actual != published)
        {
            Tell(Text(IDS_UPDATER_CHECKSUM_MISMATCH, packageName.c_str(), FileNameOf(sumsPath).c_str()), MB_ICONERROR);
            return 1;
        }
        Log(L"checksum verified against " + sumsPath);
    }
    else if (!command.Yes && !Ask(Text(IDS_UPDATER_UNVERIFIED, packageName.c_str())))
    {
        return 1;
    }

    // Unpack and look at what the package holds.
    if (work.empty())
    {
        Tell(Text(IDS_UPDATER_BAD_PACKAGE, packageName.c_str(), L"%LOCALAPPDATA%"), MB_ICONERROR);
        return 1;
    }
    const std::wstring staging = work + L"\\manual";
    const std::wstring files = StagedFilesFolder(staging);
    RemoveTree(files);
    CreateDirectoryW(Sally::ToExtendedLengthPath(staging).c_str(), NULL);
    CreateDirectoryW(Sally::ToExtendedLengthPath(files).c_str(), NULL);
    Sally::Zip::ZipReader reader;
    if (!reader.Open(package) || !reader.ExtractAll(files))
    {
        Tell(Text(IDS_UPDATER_BAD_PACKAGE, packageName.c_str(), reader.ErrorText().c_str()), MB_ICONERROR);
        reader.Close();
        RemoveTree(files);
        return 1;
    }
    reader.Close();
    WORD installedMachine = 0;
    WORD packageMachine = 0;
    std::wstring installedVersion;
    std::wstring packageVersion;
    if (!ReadPeMachine(files + L"\\sally.exe", packageMachine) || !ReadFileVersionText(files + L"\\sally.exe", packageVersion))
    {
        Tell(Text(IDS_UPDATER_BAD_PACKAGE, packageName.c_str(), L"sally.exe"), MB_ICONERROR);
        RemoveTree(files);
        return 1;
    }
    ReadPeMachine(installedExe, installedMachine);
    ReadFileVersionText(installedExe, installedVersion);
    if (installedMachine != 0 && installedMachine != packageMachine)
    {
        Tell(Text(IDS_UPDATER_WRONG_PROCESSOR, packageName.c_str()), MB_ICONERROR);
        RemoveTree(files);
        return 1;
    }
    const std::wstring from = ReleaseVersion(installedVersion);
    const std::wstring to = ReleaseVersion(packageVersion);

    // Every program started from the folder must end; Sally is asked to close, the bug reporter
    // beside each Sally ends with it, anything else must be closed by the user.
    std::vector<DWORD> sallyPids;
    std::wstring others;
    for (const RunningProcess& process : FindProcessesInFolder(install, GetCurrentProcessId()))
    {
        const std::wstring name = FileNameOf(process.Image);
        if (CompareStringOrdinal(name.c_str(), -1, L"sally.exe", -1, TRUE) == CSTR_EQUAL)
            sallyPids.push_back(process.Pid);
        else if (CompareStringOrdinal(name.c_str(), -1, L"salmon.exe", -1, TRUE) != CSTR_EQUAL)
            others += L"\n" + process.Image;
    }
    if (!others.empty())
    {
        Tell(Text(IDS_UPDATER_OTHERS_RUNNING, others.c_str()), MB_ICONWARNING);
        return 1;
    }

    std::wstring question;
    switch (CompareVersionText(installedVersion, packageVersion))
    {
    case VersionOrder::Same:
        question = Text(IDS_UPDATER_CONFIRM_SAME, to.c_str(), packageName.c_str());
        break;
    case VersionOrder::Older:
        question = Text(IDS_UPDATER_CONFIRM_OLDER, packageName.c_str(), to.c_str(), from.c_str());
        break;
    default:
        question = Text(IDS_UPDATER_CONFIRM_NEWER, to.c_str(), packageName.c_str(), from.c_str());
        break;
    }
    if (!sallyPids.empty())
        question += L"\n\n" + Text(IDS_UPDATER_WILL_CLOSE, static_cast<int>(sallyPids.size()));
    if (!command.Yes && !Ask(question))
        return 1;

    // The install step runs from a copy outside the folder, which it replaces.
    const std::wstring copy = staging + L"\\salupdate.exe";
    if (!CopyFileW(Sally::ToExtendedLengthPath(ModulePath()).c_str(), Sally::ToExtendedLengthPath(copy).c_str(), FALSE))
    {
        Tell(Text(IDS_UPDATER_BAD_PACKAGE, packageName.c_str(), SystemErrorText(GetLastError()).c_str()), MB_ICONERROR);
        return 1;
    }
    UpdaterCommand apply;
    apply.Mode = UpdaterMode::Apply;
    apply.Staging = staging;
    apply.InstallRoot = install;
    apply.WaitPids = sallyPids;
    apply.WaitPids.push_back(GetCurrentProcessId());
    apply.FromVersion = from;
    apply.ToVersion = to;
    apply.ResultFile = UpdateResultPath(work);
    apply.Relaunch = !sallyPids.empty();
    apply.Notify = !command.Yes; // --yes runs unattended: the outcome stays in the result file and log
    DeleteFileW(Sally::ToExtendedLengthPath(apply.ResultFile).c_str());
    std::wstring commandLine = QuoteCommandLineArgument(copy) + L" " + FormatUpdaterCommand(apply);
    STARTUPINFOW startup = {};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process = {};
    if (!CreateProcessW(copy.c_str(), &commandLine[0], NULL, NULL, FALSE, 0, NULL, staging.c_str(), &startup, &process))
    {
        Tell(Text(IDS_UPDATER_BAD_PACKAGE, packageName.c_str(), SystemErrorText(GetLastError()).c_str()), MB_ICONERROR);
        return 1;
    }
    AllowSetForegroundWindow(process.dwProcessId);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);

    SallyWindows windows;
    windows.Pids = sallyPids;
    EnumWindows(CollectSallyWindow, reinterpret_cast<LPARAM>(&windows));
    for (HWND window : windows.Windows)
        PostMessageW(window, WM_CLOSE, 0, 0);
    Log(L"installing " + to + L" over " + from + (sallyPids.empty() ? L"" : L", closing Sally"));
    return 0;
}

} // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    std::vector<std::wstring> args;
    for (int i = 1; argv != NULL && i < argc; ++i)
        args.push_back(argv[i]);
    if (argv != NULL)
        LocalFree(argv);

    ChooseLanguage();
    UpdaterCommand command;
    std::wstring error;
    if (!ParseUpdaterCommand(args, command, error))
    {
        Tell(Text(IDS_UPDATER_USAGE) + L"\n\n" + error, MB_ICONWARNING);
        return 2;
    }
    if (command.Mode == UpdaterMode::Manual)
        return Manual(command);
    LogPath = ParentFolder(command.ResultFile) + L"\\salupdate.log";
    return command.Mode == UpdaterMode::Prepare ? Prepare(command) : Apply(command);
}
