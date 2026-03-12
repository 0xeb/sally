// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"

#include "checkver.h"
#include "checkver.rh"
#include "checkver.rh2"
#include "github_release.h"
#include "lang\lang.rh"

#include <string>

SYSTEMTIME LastCheckTime;       // when the check was last performed
SYSTEMTIME NextOpenOrCheckTime; // the earliest time the plugin window should open automatically and optionally perform a check
int ErrorsSinceLastCheck = 0;   // how many times we have already failed to perform the automatic check

BYTE LoadedScript[LOADED_SCRIPT_MAX];
DWORD LoadedScriptSize = 0;

namespace
{

struct CReleaseCheckState
{
    bool HasCorrectData = false;
    bool UpdateAvailable = false;
    bool UsedDirectAssetLink = false;
    std::string InstalledVersion;
    std::string LatestVersion;
    std::string ReleasePageUrl;
    std::string PrimaryUrl;
    std::string PrimaryAssetName;
};

CReleaseCheckState ReleaseState;

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
    ReleaseState = CReleaseCheckState();
}

std::string GetInstalledVersionTag()
{
    return checkver::NormalizeVersionTag(SalamanderTextVersion.Get());
}

bool BuildReleaseState()
{
    ResetReleaseState();

    if (LoadedScriptSize == 0)
    {
        TRACE_E("GitHub release payload is empty");
        return false;
    }

    checkver::GitHubReleaseInfo release;
    std::string error;
    if (!checkver::ParseGitHubLatestReleaseJson(reinterpret_cast<const char*>(LoadedScript), LoadedScriptSize,
                                                release, error))
    {
        TRACE_E("Unable to parse GitHub release JSON: " << error.c_str());
        return false;
    }

    ReleaseState.HasCorrectData = true;
    ReleaseState.InstalledVersion = GetInstalledVersionTag();
    ReleaseState.LatestVersion = checkver::NormalizeVersionTag(release.TagName);
    ReleaseState.ReleasePageUrl = release.HtmlUrl;
    ReleaseState.PrimaryUrl = checkver::SelectReleaseAssetUrl(release, GetCurrentPlatform(),
                                                              &ReleaseState.PrimaryAssetName);

    if (!ReleaseState.PrimaryUrl.empty())
    {
        ReleaseState.UsedDirectAssetLink = true;
    }
    else
    {
        ReleaseState.PrimaryUrl = release.HtmlUrl;
        ReleaseState.UsedDirectAssetLink = false;
    }

    ReleaseState.UpdateAvailable =
        checkver::CompareVersionTags(ReleaseState.InstalledVersion, ReleaseState.LatestVersion) < 0;
    return true;
}

void AddVersionLine(int stringId, const std::string& version)
{
    char buffer[1024];
    _snprintf_s(buffer, _TRUNCATE, LoadStr(stringId), version.c_str());
    AddLogLine(buffer, FALSE);
}

void AddPrimaryLinkLine()
{
    char buffer[1024];
    if (ReleaseState.UsedDirectAssetLink)
    {
        _snprintf_s(buffer, _TRUNCATE, LoadStr(IDS_DOWNLOAD_RELEASE),
                    ReleaseState.LatestVersion.c_str(),
                    checkver::GetPlatformLabel(GetCurrentPlatform()));
    }
    else
    {
        _snprintf_s(buffer, _TRUNCATE, LoadStr(IDS_OPEN_RELEASE_PAGE),
                    ReleaseState.LatestVersion.c_str());
    }

    char line[1600];
    _snprintf_s(line, _TRUNCATE, "   \tu%s\tl%s\tn", buffer, ReleaseState.PrimaryUrl.c_str());
    AddLogLine(line, FALSE);
}

void AddReleasePageLine()
{
    if (ReleaseState.ReleasePageUrl.empty() || ReleaseState.ReleasePageUrl == ReleaseState.PrimaryUrl)
        return;

    char line[1600];
    _snprintf_s(line, _TRUNCATE, "   \tu%s\tl%s\tn",
                LoadStr(IDS_RELEASE_PAGE_LINK), ReleaseState.ReleasePageUrl.c_str());
    AddLogLine(line, FALSE);
}

void FillReleaseLog()
{
    if (!ReleaseState.HasCorrectData)
    {
        AddLogLine(LoadStr(IDS_INFO_CORRUPTED), TRUE);
        return;
    }

    if (ReleaseState.UpdateAvailable)
    {
        AddLogLine(LoadStr(IDS_NEWREL_MODULES), FALSE);
        AddVersionLine(IDS_CURRENT_VERSION, ReleaseState.InstalledVersion);
        AddVersionLine(IDS_LATEST_VERSION, ReleaseState.LatestVersion);
        AddPrimaryLinkLine();
        AddReleasePageLine();
        AddLogLine("", FALSE);
        AddLogLine(LoadStr(IDS_LOGWNDHELP1), FALSE);
        AddLogLine(LoadStr(IDS_LOGWNDHELP2), FALSE);
    }
    else
    {
        AddLogLine(LoadStr(IDS_NONEW_MODULES), FALSE);
        AddVersionLine(IDS_CURRENT_VERSION, ReleaseState.InstalledVersion);
        AddVersionLine(IDS_LATEST_VERSION, ReleaseState.LatestVersion);
    }
    AddLogLine("", FALSE);
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
TDirectArray<char*> Filters(1, 1);

const char* CONFIG_AUTOCHECKMODE = "AutoCheckMode";
const char* CONFIG_AUTOCONNECT = "AutoConnect";
const char* CONFIG_AUTOCLOSE = "AutoClose";
const char* CONFIG_CHECKBETA = "CheckBetaVersions";
const char* CONFIG_CHECKPB = "CheckPBVersions";
const char* CONFIG_CHECKRELEASE = "CheckReleaseVersions";
const char* CONFIG_CONNECTION = "IneternetConnection";
const char* CONFIG_PROTOCOL = "InternetProtocol";
const char* CONFIG_TIMESTAMP_KEY = "TimeStamp.hidden";
const char* CONFIG_LASTCHECK = "LastOpen";
const char* CONFIG_NEXTOPEN = "NextOpen";
const char* CONFIG_NUMOFERRORS = "NumOfErrors";

int ConfigVersion = 0;
#define CURRENT_CONFIG_VERSION 5
#define LOAD_ONLY_CONFIG_VERSION 5
const char* CONFIG_VERSION = "Version";

void DestroyFilters()
{
    for (int i = 0; i < Filters.Count; i++)
        free(Filters[i]);
    Filters.DestroyMembers();
}

BOOL AddUniqueFilter(const char* itemName)
{
    if (itemName == NULL || *itemName == 0)
        return TRUE;

    for (int i = 0; i < Filters.Count; i++)
    {
        if (SalGeneral->StrICmp(Filters[i], itemName) == 0)
            return TRUE;
    }

    char* newItem = SalGeneral->DupStr(itemName);
    if (newItem == NULL)
        return FALSE;

    Filters.Add(newItem);
    if (!Filters.IsGood())
    {
        Filters.ResetState();
        free(newItem);
        return FALSE;
    }
    return TRUE;
}

void FiltersFillListBox(HWND hListBox)
{
    if (hListBox != NULL)
        SendMessage(hListBox, LB_RESETCONTENT, 0, 0);
}

void FiltersLoadFromListBox(HWND hListBox)
{
    (void)hListBox;
    DestroyFilters();
}

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
    DestroyFilters();

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

BOOL LoadScripDataFromFile(const char* fileName)
{
    HANDLE hFile = CreateFile(fileName, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE)
    {
        char buffer[1024];
        _snprintf_s(buffer, _TRUNCATE, LoadStr(IDS_FILE_OPENERROR), fileName);
        AddLogLine(buffer, TRUE);
        return FALSE;
    }

    DWORD fileSize = GetFileSize(hFile, NULL);
    if (fileSize == INVALID_FILE_SIZE || fileSize >= LOADED_SCRIPT_MAX)
    {
        CloseHandle(hFile);
        char buffer[1024];
        _snprintf_s(buffer, _TRUNCATE, LoadStr(IDS_FILE_READERROR), fileName);
        AddLogLine(buffer, TRUE);
        return FALSE;
    }

    DWORD bytesRead = 0;
    BOOL ok = ReadFile(hFile, LoadedScript, fileSize, &bytesRead, NULL);
    CloseHandle(hFile);
    if (!ok || bytesRead != fileSize)
    {
        char buffer[1024];
        _snprintf_s(buffer, _TRUNCATE, LoadStr(IDS_FILE_READERROR), fileName);
        AddLogLine(buffer, TRUE);
        return FALSE;
    }

    LoadedScriptSize = bytesRead;
    char buffer[1024];
    _snprintf_s(buffer, _TRUNCATE, LoadStr(IDS_FILE_OPENED), fileName);
    AddLogLine(buffer, FALSE);
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
            AddLogLine(LoadStr(IDS_INFO_CORRUPTED), TRUE);
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
