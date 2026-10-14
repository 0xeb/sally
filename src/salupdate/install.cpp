// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

// Installing a release: --prepare checks and unpacks a downloaded package, --apply replaces the
// files of the Sally folder (common/update/UpdateFiles.h), and an update started by hand with a
// downloaded zip does both after asking.

#include <windows.h>
#include <shellapi.h>

#include <algorithm>
#include <string>
#include <vector>

#include "ExtendedLengthPath.h"
#include "salupdate.h"
#include "salupdate.rh"
#include "Win32UpdateFileOps.h"
#include "update/ReleaseChecksums.h"
#include "update/UpdateFiles.h"
#include "update/UpdatePackage.h"
#include "update/UpdateProcesses.h"
#include "update/UpdaterCommand.h"
#include "zip/ZipReader.h"

using namespace Sally::Update;

namespace salupdate
{

namespace
{

// How long Sally gets to close (it may still be saving its configuration), then how long
// other programs started from its folder get after that.
const DWORD kSallyExitTimeoutMs = 60 * 1000;
const DWORD kFolderIdleTimeoutMs = 30 * 1000;

bool Finish(const std::wstring& resultFile, const UpdateResult& result)
{
    Log(L"result: " + std::to_wstring(static_cast<int>(result.Status)) +
        (result.Message.empty() ? std::wstring() : L" - " + result.Message) +
        (result.FailedPath.empty() ? std::wstring() : L" (" + result.FailedPath + L", " + std::to_wstring(result.Code) + L")"));
    if (!WriteUpdateResult(resultFile, result))
        Log(L"cannot write the result file " + resultFile);
    return result.Status == UpdateStatus::Prepared || result.Status == UpdateStatus::Updated;
}

} // namespace

bool PrepareRelease(const UpdaterCommand& command, UpdateResult& result)
{
    result = UpdateResult();
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
        return false;
    }
    if (actual != expected)
    {
        result.FailedPath = command.Package;
        result.Message = L"The downloaded package does not match its published SHA-256 checksum.";
        return false;
    }

    const std::wstring files = StagedFilesFolder(command.Staging);
    if (!RemoveTree(files))
    {
        result.FailedPath = files;
        result.Code = GetLastError();
        result.Message = L"Cannot clear the folder for the new version.";
        return false;
    }
    CreateDirectoryW(Sally::ToExtendedLengthPath(command.Staging).c_str(), NULL);
    if (!CreateDirectoryW(Sally::ToExtendedLengthPath(files).c_str(), NULL))
    {
        result.FailedPath = files;
        result.Code = GetLastError();
        result.Message = L"Cannot create the folder for the new version.";
        return false;
    }

    Sally::Zip::ZipReader reader;
    if (!reader.Open(command.Package) || !reader.ExtractAll(files))
    {
        result.FailedPath = command.Package;
        result.Code = reader.ErrorCode();
        result.Message = L"The package cannot be unpacked: " + reader.ErrorText();
        RemoveTree(files);
        return false;
    }
    reader.Close();

    std::wstring problem;
    if (!CheckStagedRelease(files, command.Version, command.Arch, problem))
    {
        result.FailedPath = command.Package;
        result.Message = problem;
        RemoveTree(files);
        return false;
    }

    // The install step runs from this copy, so that the installed salupdate.exe can be
    // replaced like every other file.
    const std::wstring copy = command.Staging + L"\\salupdate.exe";
    if (!CopyFileW(Sally::ToExtendedLengthPath(ModulePath()).c_str(), Sally::ToExtendedLengthPath(copy).c_str(), FALSE))
    {
        result.FailedPath = copy;
        result.Code = GetLastError();
        result.Message = L"Cannot copy the updater.";
        return false;
    }
    result.Status = UpdateStatus::Prepared;
    return true;
}

// --prepare: verify, extract and check the release, and put a copy of this program next to it.
int Prepare(const UpdaterCommand& command)
{
    UpdateResult result;
    PrepareRelease(command, result);
    return Finish(command.ResultFile, result) ? 0 : 1;
}

namespace
{

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

} // namespace

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

namespace
{

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

} // namespace

// Every program started from the folder must end; Sally is asked to close, the bug reporter
// beside each Sally ends with it, anything else must be closed by the user.
ProgramsInFolder FindProgramsInFolder(const std::wstring& installRoot)
{
    ProgramsInFolder found;
    for (const RunningProcess& process : FindProcessesInFolder(installRoot, GetCurrentProcessId()))
    {
        const std::wstring name = FileNameOf(process.Image);
        if (CompareStringOrdinal(name.c_str(), -1, L"sally.exe", -1, TRUE) == CSTR_EQUAL)
            found.SallyPids.push_back(process.Pid);
        else if (CompareStringOrdinal(name.c_str(), -1, L"salmon.exe", -1, TRUE) != CSTR_EQUAL)
            found.Others += L"\n" + process.Image;
    }
    return found;
}

void CloseSallyWindows(const std::vector<DWORD>& pids)
{
    SallyWindows windows;
    windows.Pids = pids;
    EnumWindows(CollectSallyWindow, reinterpret_cast<LPARAM>(&windows));
    for (HWND window : windows.Windows)
        PostMessageW(window, WM_CLOSE, 0, 0);
}

bool StartApply(const UpdaterCommand& apply, const std::wstring& updaterCopy, DWORD& error)
{
    DeleteFileW(Sally::ToExtendedLengthPath(apply.ResultFile).c_str());
    std::wstring commandLine = QuoteCommandLineArgument(updaterCopy) + L" " + FormatUpdaterCommand(apply);
    STARTUPINFOW startup = {};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process = {};
    if (!CreateProcessW(updaterCopy.c_str(), &commandLine[0], NULL, NULL, FALSE, 0, NULL, apply.Staging.c_str(),
                        &startup, &process))
    {
        error = GetLastError();
        return false;
    }
    AllowSetForegroundWindow(process.dwProcessId);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return true;
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

    const ProgramsInFolder running = FindProgramsInFolder(install);
    const std::vector<DWORD>& sallyPids = running.SallyPids;
    if (!running.Others.empty())
    {
        Tell(Text(IDS_UPDATER_OTHERS_RUNNING, running.Others.c_str()), MB_ICONWARNING);
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
    DWORD startError = 0;
    if (!StartApply(apply, copy, startError))
    {
        Tell(Text(IDS_UPDATER_BAD_PACKAGE, packageName.c_str(), SystemErrorText(startError).c_str()), MB_ICONERROR);
        return 1;
    }
    CloseSallyWindows(sallyPids);
    Log(L"installing " + to + L" over " + from + (sallyPids.empty() ? L"" : L", closing Sally"));
    return 0;
}

} // namespace salupdate
