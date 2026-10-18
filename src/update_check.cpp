// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"

#include "update_check.h"
#include "common/ApplicationAppearance.h"
#include "common/supporter/SupporterState.h"

#include "cfgdlg.h"
#include "mainwnd.h"
#include "plugins.h"
#include "common/IFileSystem.h"
#include "common/unicode/helpers.h"
#include "ui/IPrompter.h"
#include "common/IProcess.h"
#include "common/IRegistry.h"
#include "common/PluginsVerPolicy.h"
#include "common/Win32UpdateFileOps.h"
#include "update/UpdateFiles.h"
#include "update/UpdatePackage.h"
#include "update/UpdateSchedule.h"
#include "update/UpdaterCommand.h"

#include <string>
#include <vector>

using namespace Sally::Update;

namespace
{

// In SALAMANDER_ROOT_REG\Configuration, beside Configuration.UpdateCheckMode ("Update Check
// Mode"). They are written when a check starts and ends, not with the rest of the configuration.
const wchar_t* const kModeValue = L"Update Check Mode";
const wchar_t* const kLastCheckValue = L"Update Check Last";
const wchar_t* const kNotBeforeValue = L"Update Check Not Before";
// Another release feed (an HTTPS mirror), set by hand; not in the user interface.
const wchar_t* const kFeedValue = L"Update Feed Url";

const wchar_t* const kCheckVerDll = L"checkver\\checkver.dll";
const DWORD kScheduledCheckDelayMs = 20 * 1000;
HANDLE ScheduledStop = NULL;
HANDLE ScheduledThread = NULL;

// What RetireCheckVer took over, written once the configuration has its final place.
bool HavePendingSettings = false;
ULONGLONG PendingLastCheck = 0;
std::wstring PendingFeedUrl;

ULONGLONG NowUtc()
{
    FILETIME now;
    GetSystemTimeAsFileTime(&now);
    return (static_cast<ULONGLONG>(now.dwHighDateTime) << 32) | now.dwLowDateTime;
}

std::wstring InstallFolder()
{
    std::wstring path;
    for (DWORD capacity = 512; capacity <= 65536; capacity *= 2)
    {
        path.assign(capacity, L'\0');
        const DWORD length = GetModuleFileNameW(NULL, &path[0], capacity);
        if (length == 0)
            return std::wstring();
        if (length < capacity)
        {
            path.resize(length);
            const size_t slash = path.find_last_of(L'\\');
            return slash == std::wstring::npos ? std::wstring() : path.substr(0, slash);
        }
    }
    return std::wstring();
}

std::wstring UpdaterPath()
{
    const std::wstring folder = InstallFolder();
    return folder.empty() ? std::wstring() : folder + L"\\utils\\salupdate.exe";
}

std::wstring ConfigurationKey()
{
    return SALAMANDER_ROOT_REG != NULL ? std::wstring(SALAMANDER_ROOT_REG) + L"\\" + SALAMANDER_CONFIG_REG
                                       : std::wstring();
}

ULONGLONG ReadTime(const wchar_t* name)
{
    const std::wstring path = ConfigurationKey();
    HKEY key = NULL;
    if (path.empty() || !gRegistry->OpenKeyRead(HKEY_CURRENT_USER, path.c_str(), key).success)
        return 0;
    uint64_t value = 0;
    if (!gRegistry->GetQWord(key, name, value).success)
        value = 0;
    gRegistry->CloseKey(key);
    return value;
}

void WriteTime(const wchar_t* name, ULONGLONG value)
{
    const std::wstring path = ConfigurationKey();
    HKEY key = NULL;
    if (path.empty() || !gRegistry->CreateKey(HKEY_CURRENT_USER, path.c_str(), key).success)
        return;
    if (value == 0)
        gRegistry->DeleteValue(key, name);
    else
        gRegistry->SetQWord(key, name, value);
    gRegistry->CloseKey(key);
}

std::wstring ReadFeedUrl()
{
    const std::wstring path = ConfigurationKey();
    HKEY key = NULL;
    std::wstring url;
    if (!path.empty() && gRegistry->OpenKeyRead(HKEY_CURRENT_USER, path.c_str(), key).success)
    {
        if (!gRegistry->GetString(key, kFeedValue, url).success)
            url.clear();
        gRegistry->CloseKey(key);
    }
    return url.size() > 8 && CompareStringOrdinal(url.c_str(), 8, L"https://", 8, TRUE) == CSTR_EQUAL ? url
                                                                                                         : std::wstring();
}

// CheckVer's settings, from the configuration being loaded.
CheckVerSettings ReadCheckVerSettings()
{
    CheckVerSettings old;
    old.RemovedByUser = sally::plugins_ver::ListContains(Configuration.RemovedBundledPlugins, kCheckVerDll);
    if (SALAMANDER_ROOT_REG == NULL)
        return old;
    const std::wstring path = std::wstring(SALAMANDER_ROOT_REG) + L"\\Plugins Configuration\\CHECKVER";
    HKEY key = NULL;
    if (!gRegistry->OpenKeyRead(HKEY_CURRENT_USER, path.c_str(), key).success)
        return old;
    old.Present = true;
    DWORD value = 0;
    if (gRegistry->GetDWord(key, L"Version", value).success)
        old.Version = value;
    if (gRegistry->GetDWord(key, L"AutoCheckMode", value).success)
    {
        old.HasAutoCheckMode = true;
        old.AutoCheckMode = value;
    }
    std::wstring feed;
    if (gRegistry->GetString(key, L"ReleaseFeedUrl", feed).success)
        old.ReleaseFeedUrl = feed;
    gRegistry->CloseKey(key);

    // When CheckVer would have opened next: a SYSTEMTIME in local time, zero for "at the next start".
    HKEY stamps = NULL;
    if (gRegistry->OpenKeyRead(HKEY_CURRENT_USER, (path + L"\\TimeStamp.hidden").c_str(), stamps).success)
    {
        std::vector<uint8_t> bytes;
        if (gRegistry->GetBinary(stamps, L"NextOpen", bytes).success && bytes.size() == sizeof(SYSTEMTIME))
        {
            SYSTEMTIME local;
            memcpy(&local, bytes.data(), sizeof(local));
            SYSTEMTIME utc;
            FILETIME time;
            if (local.wYear != 0 && TzSpecificLocalTimeToSystemTime(NULL, &local, &utc) &&
                SystemTimeToFileTime(&utc, &time))
                old.NextOpen = (static_cast<ULONGLONG>(time.dwHighDateTime) << 32) | time.dwLowDateTime;
        }
        gRegistry->CloseKey(stamps);
    }
    return old;
}

HPROCESS StartUpdater(const UpdaterCommand& command, DWORD& error)
{
    const std::wstring updater = UpdaterPath();
    if (updater.empty() || gFileSystem->GetFileAttributes(updater.c_str()) == INVALID_FILE_ATTRIBUTES)
    {
        error = ERROR_FILE_NOT_FOUND;
        return INVALID_HPROCESS;
    }
    ProcessStartInfo start;
    start.applicationName = updater;
    auto launch = command;
    if (MainWindow) launch.AppearanceOwner = reinterpret_cast<UINT_PTR>(MainWindow->HWindow);
    start.commandLine = QuoteCommandLineArgument(updater) + L" " + FormatUpdaterCommand(launch);
    start.workingDirectory = updater.substr(0, updater.find_last_of(L'\\'));
    HPROCESS process = gProcess->CreateProcess(start);
    if (process == INVALID_HPROCESS)
        error = GetLastError();
    return process;
}

UpdaterCommand CheckCommand(bool automatic)
{
    UpdaterCommand command;
    command.Mode = UpdaterMode::Check;
    command.Auto = automatic;
    command.InstallRoot = InstallFolder();
    command.FeedUrl = ReadFeedUrl();
    return command;
}

unsigned ScheduledCheckBody(void*)
{
    CALL_STACK_MESSAGE1("ScheduledCheckBody()");
    SetThreadNameInVCAndTrace(L"UpdateCheck");
    // Shutdown must wake the initial delay and join this worker before services die.
    if (WaitForSingleObject(ScheduledStop, kScheduledCheckDelayMs) != WAIT_TIMEOUT)
        return 0;

    const ULONGLONG started = NowUtc();
    if (!IsCheckDue(Configuration.UpdateCheckMode, ReadTime(kLastCheckValue), ReadTime(kNotBeforeValue), started))
        return 0;
    // Another Sally starting now does not check too.
    WriteTime(kNotBeforeValue, started + kFileTimeHour);
    DWORD error = 0;
    HPROCESS process = StartUpdater(CheckCommand(true), error);
    if (process == INVALID_HPROCESS)
    {
        TRACE_I("ScheduledCheck: cannot start the updater, error " << error);
        WriteTime(kNotBeforeValue, started + kFileTimeDay);
        return 0;
    }
    // The updater ends at once unless it shows a newer release; then when its window closes.
    WaitResult waited;
    do
    {
        if (WaitForSingleObject(ScheduledStop, 0) == WAIT_OBJECT_0)
        {
            gProcess->CloseProcess(process); // leave an already-open updater independent
            return 0;
        }
        waited = gProcess->WaitForProcess(process, 100);
    } while (waited == WaitResult::Timeout);
    DWORD exitCode = 2;
    gProcess->GetExitCode(process, exitCode);
    gProcess->CloseProcess(process);
    if (exitCode == 0 || exitCode == 1)
    {
        WriteTime(kLastCheckValue, started);
        WriteTime(kNotBeforeValue, 0);
    }
    else
    {
        TRACE_I("ScheduledCheck: GitHub could not be reached (" << exitCode << "), trying again tomorrow");
        WriteTime(kNotBeforeValue, NowUtc() + kFileTimeDay);
    }
    return 0;
}

unsigned ScheduledCheckEH(void* param)
{
#ifndef CALLSTK_DISABLE
    __try
    {
#endif // CALLSTK_DISABLE
        return ScheduledCheckBody(param);
#ifndef CALLSTK_DISABLE
    }
    __except (CCallStack::HandleException(GetExceptionInformation()))
    {
        TerminateProcess(GetCurrentProcess(), 1);
        return 1;
    }
#endif // CALLSTK_DISABLE
}

DWORD WINAPI ScheduledCheck(void* param)
{
#ifndef CALLSTK_DISABLE
    CCallStack stack;
#endif // CALLSTK_DISABLE
    return ScheduledCheckEH(param);
}

// An update result the updater left for this Sally folder.
bool HasUpdateResultFor(const std::wstring& installRoot, std::wstring& resultFile)
{
    const std::wstring work = UpdateWorkFolder();
    if (work.empty())
        return false;
    resultFile = UpdateResultPath(work);
    UpdateResult result;
    if (gFileSystem->GetFileAttributes(resultFile.c_str()) == INVALID_FILE_ATTRIBUTES ||
        !ReadUpdateResult(resultFile, result) || result.Status == UpdateStatus::Prepared)
        return false;
    std::wstring a = result.InstallRoot;
    std::wstring b = installRoot;
    while (!a.empty() && a.back() == L'\\')
        a.pop_back();
    while (!b.empty() && b.back() == L'\\')
        b.pop_back();
    return !a.empty() &&
           CompareStringOrdinal(a.c_str(), static_cast<int>(a.size()), b.c_str(), static_cast<int>(b.size()), TRUE) ==
               CSTR_EQUAL;
}

} // namespace

namespace UpdateCheck
{

void RemoveRetiredFiles()
{
    const std::wstring folder = InstallFolder();
    if (folder.empty())
        return;
    // Two Sallys starting together do not sweep at once.
    HANDLE mutex = HANDLES(CreateMutex(NULL, FALSE, L"Local\\SallyRetiredFiles"));
    if (mutex != NULL)
        WaitForSingleObject(mutex, 5000);
    Sally::Update::Win32UpdateFileOps ops;
    ObsoleteManifest manifest;
    if (ReadObsoleteManifest(folder, ops, manifest))
    {
        const ObsoleteSweep sweep = RemoveObsoleteFiles(folder, manifest, ops);
        if (sweep.Removed > 0 || !sweep.StillInUse.empty() || !sweep.Failures.empty())
            TRACE_I("RemoveRetiredFiles(): removed " << sweep.Removed << ", in use " << sweep.StillInUse.size()
                                                     << ", left " << sweep.Failures.size());
    }
    if (mutex != NULL)
    {
        ReleaseMutex(mutex);
        HANDLES(CloseHandle(mutex));
    }
}

BOOL RetireCheckVer(HWND parent, BOOL canDeletePluginKey)
{
    CALL_STACK_MESSAGE1("UpdateCheck::RetireCheckVer()");
    BOOL changed = FALSE;
    // Once: when this configuration has no update check of its own yet.
    const std::wstring path = ConfigurationKey();
    HKEY key = NULL;
    bool stored = false;
    if (!path.empty() && gRegistry->OpenKeyRead(HKEY_CURRENT_USER, path.c_str(), key).success)
    {
        stored = gRegistry->ValueExists(key, kModeValue);
        gRegistry->CloseKey(key);
    }
    if (!stored)
    {
        const MigratedUpdateSettings migrated = MigrateCheckVerSettings(ReadCheckVerSettings());
        Configuration.UpdateCheckMode = migrated.Mode;
        HavePendingSettings = true;
        PendingLastCheck = migrated.LastCheck;
        PendingFeedUrl = migrated.FeedUrl;
        changed = TRUE;
    }
    // Its registry key goes with it (its settings are taken over above).
    int index = -1;
    if (Plugins.FindDLL(kCheckVerDll, index) && Plugins.Remove(parent, index, canDeletePluginKey))
        changed = TRUE;
    return changed;
}

void OnStarted()
{
    CALL_STACK_MESSAGE1("UpdateCheck::OnStarted()");
    if (HavePendingSettings)
    {
        HavePendingSettings = false;
        if (PendingLastCheck != 0)
            WriteTime(kLastCheckValue, PendingLastCheck);
        if (!PendingFeedUrl.empty())
        {
            const std::wstring path = ConfigurationKey();
            HKEY key = NULL;
            if (!path.empty() && gRegistry->CreateKey(HKEY_CURRENT_USER, path.c_str(), key).success)
            {
                gRegistry->SetString(key, kFeedValue, PendingFeedUrl.c_str());
                gRegistry->CloseKey(key);
            }
        }
    }

    // Restarted by the updater: it shows how the update went (once, even when two Sallys start).
    const std::wstring installRoot = InstallFolder();
    std::wstring resultFile;
    if (HasUpdateResultFor(installRoot, resultFile))
    {
        UpdaterCommand report;
        report.Mode = UpdaterMode::Report;
        report.ResultFile = resultFile;
        report.InstallRoot = installRoot;
        DWORD error = 0;
        HPROCESS process = StartUpdater(report, error);
        if (process != INVALID_HPROCESS)
        {
            AllowSetForegroundWindow(gProcess->GetProcessId(process));
            gProcess->CloseProcess(process);
            return; // no scheduled check right after an update
        }
        TRACE_I("UpdateCheck::OnStarted(): cannot start the updater for the report, error " << error);
    }

    if (CheckInterval(Configuration.UpdateCheckMode) == 0)
        return;
    if (ScheduledThread != NULL)
        return;
    ScheduledStop = HANDLES(CreateEventW(NULL, TRUE, FALSE, NULL));
    if (ScheduledStop == NULL)
        return;
    DWORD id = 0;
    ScheduledThread = HANDLES(CreateThread(NULL, 0, ScheduledCheck, NULL, 0, &id));
    if (ScheduledThread == NULL)
    {
        HANDLES(CloseHandle(ScheduledStop));
        ScheduledStop = NULL;
    }
}

void OnStopping()
{
    if (ScheduledThread != NULL)
    {
        SetEvent(ScheduledStop);
        WaitForSingleObject(ScheduledThread, INFINITE);
        HANDLES(CloseHandle(ScheduledThread));
        ScheduledThread = NULL;
    }
    if (ScheduledStop != NULL)
    {
        HANDLES(CloseHandle(ScheduledStop));
        ScheduledStop = NULL;
    }
}

void ShowWhatsNew(HWND parent)
{
    DWORD error = 0;
    UpdaterCommand command = CheckCommand(false);
    command.Mode = UpdaterMode::WhatsNew;
    command.FeedUrl.clear();
    HPROCESS process = StartUpdater(command, error);
    if (process == INVALID_HPROCESS)
    {
        const std::wstring message = FormatStrW(LoadStrW(IDS_UPDATER_CANNOT_START), UpdaterPath().c_str(), GetErrorTextOwned(error).c_str());
        gPrompter->ShowError(LoadStrW(IDS_ERRORTITLE), message.c_str());
        return;
    }
    AllowSetForegroundWindow(gProcess->GetProcessId(process));
    gProcess->CloseProcess(process);
    (void)parent;
}

void CheckNow(HWND parent)
{
    CALL_STACK_MESSAGE1("UpdateCheck::CheckNow()");
    DWORD error = 0;
    HPROCESS process = StartUpdater(CheckCommand(false), error);
    if (process == INVALID_HPROCESS)
    {
        std::wstring reason = GetErrorTextOwned(error);
        while (!reason.empty() && (reason.back() == L'\n' || reason.back() == L'\r'))
            reason.pop_back();
        const std::wstring message = FormatStrW(LoadStrW(IDS_UPDATER_CANNOT_START), UpdaterPath().c_str(),
                                                reason.c_str());
        gPrompter->ShowError(LoadStrW(IDS_ERRORTITLE), message.c_str());
        return;
    }
    AllowSetForegroundWindow(gProcess->GetProcessId(process));
    gProcess->CloseProcess(process);
    (void)parent;
}

} // namespace UpdateCheck

namespace UpdateCheck
{
static Sally::Appearance::Snapshot CurrentAppearance()
{
    auto value = Sally::Appearance::Read(Sally::Supporter::ResolveProfileRoot(), InstallFolder(), *gRegistry);
    value.Icon = Configuration.GetMainWindowIconIndex();
    value.CustomFont = UseCustomPanelFont != FALSE;
    value.Font = LogFont;
    return value;
}
void AppearanceChanged(HWND parent) { Sally::Appearance::Broadcast(parent, CurrentAppearance()); }
LRESULT HandleAppearanceRequest(HWND parent, WPARAM receiver)
{
    Sally::Appearance::Send(parent, reinterpret_cast<HWND>(receiver), CurrentAppearance());
    return TRUE;
}
} // namespace UpdateCheck
