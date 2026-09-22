// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"
#include "checkver.h"
#include "checkver.rh"
#include "checkver.rh2"
#include "checkver_text.h"
#include "release_check.h"
#include "lang\lang.rh"

#include <string>
#include <new>

SYSTEMTIME LastCheckTime;       // when the check was last performed
SYSTEMTIME NextOpenOrCheckTime; // the earliest time the plugin window should open automatically and optionally perform a check
int ErrorsSinceLastCheck = 0;   // how many times we have already failed to perform the automatic check

std::vector<BYTE> LoadedScript;

namespace
{

checkver::ReleaseCheckResult ReleaseState;

checkver::GitHubAssetPlatform GetCurrentPlatform()
{
#ifdef _WIN64
#ifdef _M_ARM64
    return checkver::GitHubAssetPlatform::ARM64;
#else
    return checkver::GitHubAssetPlatform::X64;
#endif
#else
    return checkver::GitHubAssetPlatform::X86;
#endif
}

void ApplyFixedReleaseSettings(CDataDefaults& data)
{
    data.CheckBetaVersion = FALSE;
    data.CheckPBVersion = FALSE;
    data.CheckReleaseVersion = TRUE;
}

void ResetReleaseState()
{
    ReleaseState = checkver::ReleaseCheckResult();
}

bool BuildReleaseState()
{
    std::string error;
    if (!checkver::BuildReleaseCheckResult(
            reinterpret_cast<const char*>(LoadedScript.data()), LoadedScript.size(),
            SalamanderTextVersion.c_str(), GetCurrentPlatform(), ReleaseState, error))
    {
        TRACE_E("Unable to build GitHub release state: " << error.c_str());
        return false;
    }
    return true;
}

void AddVersionLine(int stringId, const std::string& version)
{
    const std::wstring versionText = checkver::Utf8ToWideOrEmpty(version);
    const std::wstring line = SPLFormatStringOwned(LangStr(stringId).c_str(), versionText.c_str());
    AddLogLine(line.c_str(), FALSE);
}

void AddPrimaryLinkLine()
{
    const std::wstring latestVersion = checkver::Utf8ToWideOrEmpty(ReleaseState.LatestVersion);
    std::wstring label;
    if (ReleaseState.UsedDirectAssetLink)
    {
        const std::wstring platform = checkver::Utf8ToWideOrEmpty(
            checkver::GetPlatformLabel(GetCurrentPlatform()));
        label = SPLFormatStringOwned(LangStr(IDS_DOWNLOAD_RELEASE).c_str(),
                                     latestVersion.c_str(), platform.c_str());
    }
    else
    {
        label = SPLFormatStringOwned(LangStr(IDS_OPEN_RELEASE_PAGE).c_str(),
                                     latestVersion.c_str());
    }

    // The \tu ... \tl ... \tn markup is FlexWriteText's, not a user string - see logwnd.cpp.
    const std::wstring url = checkver::Utf8ToWideOrEmpty(ReleaseState.PrimaryUrl);
    const std::wstring line = L"   \tu" + label + L"\tl" + url + L"\tn";
    AddLogLine(line.c_str(), FALSE);
}

void AddReleasePageLine()
{
    if (ReleaseState.ReleasePageUrl.empty() || ReleaseState.ReleasePageUrl == ReleaseState.PrimaryUrl)
        return;

    const std::wstring url = checkver::Utf8ToWideOrEmpty(ReleaseState.ReleasePageUrl);
    const std::wstring line = L"   \tu" + LangStr(IDS_RELEASE_PAGE_LINK) + L"\tl" + url + L"\tn";
    AddLogLine(line.c_str(), FALSE);
}

void FillReleaseLog()
{
    if (!ReleaseState.HasCorrectData)
    {
        AddLogLine(LangStr(IDS_INFO_CORRUPTED).c_str(), TRUE);
        return;
    }

    if (ReleaseState.UpdateAvailable)
    {
        AddLogLine(LangStr(IDS_NEWREL_MODULES).c_str(), FALSE);
        AddVersionLine(IDS_CURRENT_VERSION, ReleaseState.InstalledVersion);
        AddVersionLine(IDS_LATEST_VERSION, ReleaseState.LatestVersion);
        AddPrimaryLinkLine();
        AddReleasePageLine();
        AddLogLine(L"", FALSE);
        AddLogLine(LangStr(IDS_LOGWNDHELP1).c_str(), FALSE);
        AddLogLine(LangStr(IDS_LOGWNDHELP2).c_str(), FALSE);
    }
    else
    {
        AddLogLine(LangStr(IDS_NONEW_MODULES).c_str(), FALSE);
        AddVersionLine(IDS_CURRENT_VERSION, ReleaseState.InstalledVersion);
        AddVersionLine(IDS_LATEST_VERSION, ReleaseState.LatestVersion);
    }
    AddLogLine(L"", FALSE);
}

} // namespace

CDataDefaults DataDefaults[inetCount] =
    {
        {achmMonth, FALSE, FALSE, FALSE, FALSE, TRUE},
        {achmWeek, TRUE, TRUE, FALSE, FALSE, TRUE},
        {achmNever, FALSE, FALSE, FALSE, FALSE, TRUE},
};

CInternetConnection InternetConnection = inetLAN;
CInternetProtocol InternetProtocol = inetpHTTP;
CDataDefaults Data = DataDefaults[inetLAN];
const wchar_t* CONFIG_AUTOCHECKMODE = L"AutoCheckMode";
const wchar_t* CONFIG_AUTOCONNECT = L"AutoConnect";
const wchar_t* CONFIG_AUTOCLOSE = L"AutoClose";
const wchar_t* CONFIG_CHECKBETA = L"CheckBetaVersions";
const wchar_t* CONFIG_CHECKPB = L"CheckPBVersions";
const wchar_t* CONFIG_CHECKRELEASE = L"CheckReleaseVersions";
const wchar_t* CONFIG_CONNECTION = L"IneternetConnection";
const wchar_t* CONFIG_PROTOCOL = L"InternetProtocol";
const wchar_t* CONFIG_TIMESTAMP_KEY = L"TimeStamp.hidden";
const wchar_t* CONFIG_LASTCHECK = L"LastOpen";
const wchar_t* CONFIG_NEXTOPEN = L"NextOpen";
const wchar_t* CONFIG_NUMOFERRORS = L"NumOfErrors";

int ConfigVersion = 0;
#define CURRENT_CONFIG_VERSION 5
#define LOAD_ONLY_CONFIG_VERSION 5
const wchar_t* CONFIG_VERSION = L"Version";

unsigned __int64
GetDaysCount(const SYSTEMTIME* time)
{
    FILETIME ft;
    if (SystemTimeToFileTime(time, &ft))
    {
        unsigned __int64 t = ft.dwLowDateTime + (((unsigned __int64)ft.dwHighDateTime) << 32);
        return t / ((unsigned __int64)10000000 * 60 * 60 * 24);
    }
    return 0;
}

DWORD
GetWaitDays()
{
    DWORD days = 0;
    switch (Data.AutoCheckMode)
    {
    case achmDay:
        days = 1;
        break;
    case achmWeek:
        days = 7;
        break;
    case achmMonth:
        days = 30;
        break;
    case achm3Month:
        days = 3 * 30;
        break;
    case achm6Month:
        days = 6 * 30;
        break;
    default:
        break;
    }
    return days;
}

BOOL IsTimeExpired(const SYSTEMTIME* time)
{
    if (GetWaitDays() == 0)
        return FALSE;

    SYSTEMTIME currentTime;
    GetLocalTime(&currentTime);
    return GetDaysCount(time) <= GetDaysCount(&currentTime);
}

void GetFutureTime(SYSTEMTIME* tgtTime, const SYSTEMTIME* time, DWORD days)
{
    FILETIME ft;
    if (SystemTimeToFileTime(time, &ft))
    {
        unsigned __int64 t = ft.dwLowDateTime + (((unsigned __int64)ft.dwHighDateTime) << 32);
        t += days * ((unsigned __int64)10000000 * 60 * 60 * 24);
        ft.dwLowDateTime = (DWORD)(t & 0xffffffff);
        ft.dwHighDateTime = (DWORD)(t >> 32);
        if (FileTimeToSystemTime(&ft, tgtTime))
            return;
    }
    GetLocalTime(tgtTime);
}

void GetFutureTime(SYSTEMTIME* tgtTime, DWORD days)
{
    SYSTEMTIME currentTime;
    GetLocalTime(&currentTime);
    GetFutureTime(tgtTime, &currentTime, days);
}

void LoadConfig(HKEY regKey, CSalamanderRegistryAbstract* registry)
{
    CALL_STACK_MESSAGE1("LoadConfig(, ,)");

    if (regKey != NULL && !registry->GetValue(regKey, CONFIG_VERSION, REG_DWORD, &ConfigVersion, sizeof(DWORD)))
        ConfigVersion = 0;

    InternetConnection = inetLAN;
    InternetProtocol = inetpHTTP;
    Data = DataDefaults[InternetConnection];
    ApplyFixedReleaseSettings(Data);
    if (regKey != NULL && ConfigVersion >= LOAD_ONLY_CONFIG_VERSION)
    {
        registry->GetValue(regKey, CONFIG_AUTOCHECKMODE, REG_DWORD, &Data.AutoCheckMode, sizeof(DWORD));
        registry->GetValue(regKey, CONFIG_AUTOCONNECT, REG_DWORD, &Data.AutoConnect, sizeof(DWORD));
        registry->GetValue(regKey, CONFIG_AUTOCLOSE, REG_DWORD, &Data.AutoClose, sizeof(DWORD));
        ApplyFixedReleaseSettings(Data);

        HKEY actKey;
        BOOL timeStampLoaded = FALSE;
        if (registry->OpenKey(regKey, CONFIG_TIMESTAMP_KEY, actKey))
        {
            if (registry->GetValue(actKey, CONFIG_LASTCHECK, REG_BINARY, &LastCheckTime, sizeof(LastCheckTime)) &&
                registry->GetValue(actKey, CONFIG_NEXTOPEN, REG_BINARY, &NextOpenOrCheckTime, sizeof(NextOpenOrCheckTime)) &&
                registry->GetValue(actKey, CONFIG_NUMOFERRORS, REG_DWORD, &ErrorsSinceLastCheck, sizeof(DWORD)))
            {
                timeStampLoaded = TRUE;
            }
            registry->CloseKey(actKey);
        }

        if (LoadedOnSalamanderStart && timeStampLoaded)
        {
            if (IsTimeExpired(&NextOpenOrCheckTime))
                SalGeneral->PostMenuExtCommand(CM_AUTOCHECK_VERSION, TRUE);
            else
                SalGeneral->PostUnloadThisPlugin();
        }
    }
    else
    {
        if (LoadedOnSalInstall)
        {
            SalGeneral->PostMenuExtCommand(CM_FIRSTCHECK_VERSION, TRUE);
            LoadedOnSalInstall = FALSE;
        }
        else if (LoadedOnSalamanderStart && Data.AutoCheckMode != achmNever)
        {
            SalGeneral->PostMenuExtCommand(CM_AUTOCHECK_VERSION, TRUE);
        }
    }
}

void OnSaveTimeStamp(HKEY regKey, CSalamanderRegistryAbstract* registry)
{
    CALL_STACK_MESSAGE1("OnSaveTimeStamp(, ,)");
    HKEY actKey;
    if (registry->CreateKey(regKey, CONFIG_TIMESTAMP_KEY, actKey))
    {
        registry->SetValue(actKey, CONFIG_LASTCHECK, REG_BINARY, &LastCheckTime, sizeof(LastCheckTime));
        registry->SetValue(actKey, CONFIG_NEXTOPEN, REG_BINARY, &NextOpenOrCheckTime, sizeof(NextOpenOrCheckTime));
        registry->SetValue(actKey, CONFIG_NUMOFERRORS, REG_DWORD, &ErrorsSinceLastCheck, sizeof(DWORD));
        registry->CloseKey(actKey);
    }
}

void SaveConfig(HKEY regKey, CSalamanderRegistryAbstract* registry)
{
    CALL_STACK_MESSAGE1("SaveConfig(, ,)");

    ApplyFixedReleaseSettings(Data);
    InternetConnection = inetLAN;
    InternetProtocol = inetpHTTP;

    DWORD version = CURRENT_CONFIG_VERSION;
    registry->SetValue(regKey, CONFIG_VERSION, REG_DWORD, &version, sizeof(DWORD));
    registry->SetValue(regKey, CONFIG_AUTOCHECKMODE, REG_DWORD, &Data.AutoCheckMode, sizeof(DWORD));
    registry->SetValue(regKey, CONFIG_AUTOCONNECT, REG_DWORD, &Data.AutoConnect, sizeof(DWORD));
    registry->SetValue(regKey, CONFIG_AUTOCLOSE, REG_DWORD, &Data.AutoClose, sizeof(DWORD));
    registry->SetValue(regKey, CONFIG_CHECKBETA, REG_DWORD, &Data.CheckBetaVersion, sizeof(DWORD));
    registry->SetValue(regKey, CONFIG_CHECKPB, REG_DWORD, &Data.CheckPBVersion, sizeof(DWORD));
    registry->SetValue(regKey, CONFIG_CHECKRELEASE, REG_DWORD, &Data.CheckReleaseVersion, sizeof(DWORD));
    registry->SetValue(regKey, CONFIG_CONNECTION, REG_DWORD, &InternetConnection, sizeof(DWORD));
    registry->SetValue(regKey, CONFIG_PROTOCOL, REG_DWORD, &InternetProtocol, sizeof(DWORD));

    SalGeneral->SetFlagLoadOnSalamanderStart(Data.AutoCheckMode != achmNever);
}

BOOL LoadScripDataFromFile(const wchar_t* fileName)
{
    HANDLE hFile = CreateFileW(fileName, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE)
    {
        const std::wstring message = SPLFormatStringOwned(LangStr(IDS_FILE_OPENERROR).c_str(), fileName);
        AddLogLine(message.c_str(), TRUE);
        return FALSE;
    }

    LARGE_INTEGER fileSize = {};
    if (!GetFileSizeEx(hFile, &fileSize) || fileSize.QuadPart < 0 ||
        static_cast<ULONGLONG>(fileSize.QuadPart) > CHECKVER_MAX_RELEASE_RESPONSE_BYTES)
    {
        CloseHandle(hFile);
        const std::wstring message = SPLFormatStringOwned(LangStr(IDS_FILE_READERROR).c_str(), fileName);
        AddLogLine(message.c_str(), TRUE);
        return FALSE;
    }

    std::vector<BYTE> loaded;
    try
    {
        loaded.resize(static_cast<size_t>(fileSize.QuadPart));
    }
    catch (const std::bad_alloc&)
    {
        CloseHandle(hFile);
        const std::wstring message = SPLFormatStringOwned(LangStr(IDS_FILE_READERROR).c_str(), fileName);
        AddLogLine(message.c_str(), TRUE);
        return FALSE;
    }

    DWORD bytesRead = 0;
    const DWORD requested = static_cast<DWORD>(loaded.size());
    BOOL ok = requested == 0 || ReadFile(hFile, loaded.data(), requested, &bytesRead, NULL);
    CloseHandle(hFile);
    if (!ok || bytesRead != requested)
    {
        const std::wstring message = SPLFormatStringOwned(LangStr(IDS_FILE_READERROR).c_str(), fileName);
        AddLogLine(message.c_str(), TRUE);
        return FALSE;
    }

    LoadedScript.swap(loaded);
    const std::wstring message = SPLFormatStringOwned(LangStr(IDS_FILE_OPENED).c_str(), fileName);
    AddLogLine(message.c_str(), FALSE);
    return TRUE;
}

void ModulesCreateLog(BOOL* moduleWasFound, BOOL rereadModules)
{
    if (moduleWasFound != NULL)
        *moduleWasFound = FALSE;

    if (rereadModules || !ReleaseState.HasCorrectData)
    {
        if (!BuildReleaseState())
        {
            AddLogLine(LangStr(IDS_INFO_CORRUPTED).c_str(), TRUE);
            return;
        }
    }

    FillReleaseLog();
    if (moduleWasFound != NULL)
        *moduleWasFound = ReleaseState.UpdateAvailable ? TRUE : FALSE;
}

BOOL ModulesHasCorrectData()
{
    return ReleaseState.HasCorrectData ? TRUE : FALSE;
}

void ModulesCleanup()
{
    ResetReleaseState();
}

void ModulesChangeShowDetails(int index)
{
    (void)index;
}

void EnumSalModules()
{
}
