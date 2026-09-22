// SPDX-FileCopyrightText: 2025-2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#ifndef SALLY_WINDOWS_TERMINAL_STANDALONE
#include "precomp.h"
#endif

#include "WindowsTerminalService.h"
#include "Win32Utf8.h"

#pragma push_macro("free")
#undef free
#include "yyjson/yyjson.h"
#pragma pop_macro("free")

#include <algorithm>
#include <cstring>
#include <cwctype>
#include <new>
#include <set>

namespace
{
    constexpr uint64_t kMaximumSettingsSize = 16ULL * 1024ULL * 1024ULL;

    std::wstring FoldCase(const std::wstring& value)
    {
        std::wstring folded(value);
        std::transform(folded.begin(), folded.end(), folded.begin(),
                       [](wchar_t ch)
                       { return static_cast<wchar_t>(towlower(ch)); });
        return folded;
    }

    bool SameFileTime(const FILETIME& left, const FILETIME& right)
    {
        return left.dwLowDateTime == right.dwLowDateTime &&
               left.dwHighDateTime == right.dwHighDateTime;
    }

    std::wstring JoinPath(const std::wstring& directory, const wchar_t* relativePath)
    {
        std::wstring path(directory);
        if (!path.empty() && path.back() != L'\\' && path.back() != L'/')
            path.push_back(L'\\');
        while (relativePath != nullptr && (*relativePath == L'\\' || *relativePath == L'/'))
            relativePath++;
        if (relativePath != nullptr)
            path.append(relativePath);
        return path;
    }

    bool GetParentPath(const std::wstring& path, std::wstring& parent)
    {
        parent.clear();
        size_t separator = path.find_last_of(L"\\/");
        if (separator == std::wstring::npos)
            return false;
        parent.assign(path, 0, separator);
        return !parent.empty();
    }

    bool IsPackageFamilyName(const std::wstring& value)
    {
        if (value.empty())
            return false;
        for (wchar_t ch : value)
        {
            const bool alphaNumeric = (ch >= L'a' && ch <= L'z') ||
                                      (ch >= L'A' && ch <= L'Z') ||
                                      (ch >= L'0' && ch <= L'9');
            if (!alphaNumeric && ch != L'.' && ch != L'-' && ch != L'_')
                return false;
        }
        return true;
    }

    bool ReadJsonString(yyjson_val* object, const char* key, std::wstring& value)
    {
        yyjson_val* member = yyjson_obj_get(object, key);
        return yyjson_is_str(member) &&
               Win32StrictUtf8ToWide(yyjson_get_str(member), yyjson_get_len(member), value);
    }

    bool ReadOptionalBool(yyjson_val* object, const char* key, bool fallback)
    {
        yyjson_val* member = yyjson_obj_get(object, key);
        return yyjson_is_bool(member) ? yyjson_get_bool(member) : fallback;
    }

    std::wstring QuoteArgument(const std::wstring& value)
    {
        std::wstring quoted(L"\"");
        size_t slashCount = 0;
        for (wchar_t ch : value)
        {
            if (ch == L'\\')
            {
                slashCount++;
                continue;
            }

            if (ch == L'\"')
            {
                quoted.append(slashCount * 2 + 1, L'\\');
                quoted.push_back(L'\"');
            }
            else
            {
                quoted.append(slashCount, L'\\');
                quoted.push_back(ch);
            }
            slashCount = 0;
        }
        quoted.append(slashCount * 2, L'\\');
        quoted.push_back(L'\"');
        return quoted;
    }
} // namespace

CWindowsTerminalService::CWindowsTerminalService(IExecutableLocator* executableLocator,
                                                 IEnvironment* environment,
                                                 IFileSystem* fileSystem,
                                                 IProcess* process)
    : ExecutableLocator(executableLocator),
      Environment(environment),
      FileSystem(fileSystem),
      Process(process)
{
}

IExecutableLocator* CWindowsTerminalService::ResolveExecutableLocator() const
{
    return ExecutableLocator != nullptr ? ExecutableLocator : gExecutableLocator;
}

IEnvironment* CWindowsTerminalService::ResolveEnvironment() const
{
    return Environment != nullptr ? Environment : gEnvironment;
}

IFileSystem* CWindowsTerminalService::ResolveFileSystem() const
{
    return FileSystem != nullptr ? FileSystem : gFileSystem;
}

IProcess* CWindowsTerminalService::ResolveProcess() const
{
    return Process != nullptr ? Process : gProcess;
}

bool CWindowsTerminalService::FindSettingsFile(const ExecutableLocation& executable,
                                               std::wstring& path, FileInfo& info) const
{
    path.clear();
    info = FileInfo{};
    IFileSystem* fileSystem = ResolveFileSystem();
    if (fileSystem == nullptr || executable.kind == ExecutableLocationKind::Unresolved)
        return false;

    if (executable.kind == ExecutableLocationKind::Direct)
    {
        const std::wstring& targetPath = !executable.targetPath.empty()
                                             ? executable.targetPath
                                             : executable.executablePath;
        std::wstring installDirectory;
        if (GetParentPath(targetPath, installDirectory))
        {
            std::wstring markerPath = JoinPath(installDirectory, L".portable");
            FileInfo markerInfo{};
            if (fileSystem->GetFileInfo(markerPath.c_str(), markerInfo).success &&
                !markerInfo.isDirectory)
            {
                path = JoinPath(installDirectory, L"settings\\settings.json");
                FileInfo settingsInfo{};
                if (fileSystem->GetFileInfo(path.c_str(), settingsInfo).success &&
                    !settingsInfo.isDirectory)
                {
                    info = settingsInfo;
                    return true;
                }
                return false;
            }
        }
    }

    IEnvironment* environment = ResolveEnvironment();
    std::wstring localAppData;
    if (environment == nullptr ||
        !environment->GetVariable(L"LOCALAPPDATA", localAppData).success ||
        localAppData.empty())
        return false;

    if (executable.kind == ExecutableLocationKind::AppExecutionAlias)
    {
        if (!IsPackageFamilyName(executable.packageFamilyName))
            return false;
        path = JoinPath(localAppData, L"Packages");
        path = JoinPath(path, executable.packageFamilyName.c_str());
        path = JoinPath(path, L"LocalState\\settings.json");
    }
    else
    {
        path = JoinPath(localAppData, L"Microsoft\\Windows Terminal\\settings.json");
    }

    FileInfo settingsInfo{};
    if (!fileSystem->GetFileInfo(path.c_str(), settingsInfo).success ||
        settingsInfo.isDirectory)
        return false;
    info = settingsInfo;
    return true;
}

bool CWindowsTerminalService::ReadSettingsFile(const std::wstring& path,
                                               std::vector<char>& bytes,
                                               DWORD& errorCode) const
{
    bytes.clear();
    IFileSystem* fileSystem = ResolveFileSystem();
    if (fileSystem == nullptr)
    {
        errorCode = ERROR_INVALID_PARAMETER;
        return false;
    }

    HANDLE file = fileSystem->OpenFileForRead(path.c_str(),
                                              FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE);
    if (file == INVALID_HANDLE_VALUE)
    {
        errorCode = GetLastError();
        return false;
    }

    uint64_t size = 0;
    FileResult sizeResult = fileSystem->GetHandleFileSize(file, &size);
    if (!sizeResult.success || size > kMaximumSettingsSize)
    {
        errorCode = sizeResult.success ? ERROR_FILE_TOO_LARGE : sizeResult.errorCode;
        fileSystem->CloseFileHandle(file);
        return false;
    }

    bytes.resize(static_cast<size_t>(size));
    size_t offset = 0;
    while (offset < bytes.size())
    {
        DWORD chunk = static_cast<DWORD>(std::min<size_t>(bytes.size() - offset, 1024 * 1024));
        DWORD read = 0;
        FileResult readResult = fileSystem->ReadFromHandle(file, bytes.data() + offset, chunk, &read);
        if (!readResult.success || read == 0)
        {
            errorCode = readResult.success ? ERROR_HANDLE_EOF : readResult.errorCode;
            fileSystem->CloseFileHandle(file);
            bytes.clear();
            return false;
        }
        offset += read;
    }

    fileSystem->CloseFileHandle(file);
    errorCode = ERROR_SUCCESS;
    return true;
}

bool CWindowsTerminalService::ParseSettingsJson(const char* json, size_t size,
                                                WindowsTerminalCatalog& catalog,
                                                std::wstring& error)
{
    catalog.profiles.clear();
    catalog.defaultProfileGuid.clear();
    error.clear();

    yyjson_read_err readError{};
    const yyjson_read_flag flags = YYJSON_READ_ALLOW_COMMENTS |
                                   YYJSON_READ_ALLOW_TRAILING_COMMAS |
                                   YYJSON_READ_ALLOW_BOM;
    const char* input = json != nullptr ? json : "";
    const size_t inputSize = json != nullptr ? size : 0;
    yyjson_doc* document = yyjson_read_opts(const_cast<char*>(input), inputSize, flags,
                                            nullptr, &readError);
    if (document == nullptr)
    {
        std::wstring detail;
        if (readError.msg != nullptr)
            Win32StrictUtf8ToWide(readError.msg, strlen(readError.msg), detail);
        error = detail.empty() ? L"Invalid Windows Terminal settings JSON." : detail;
        return false;
    }

    yyjson_val* root = yyjson_doc_get_root(document);
    if (!yyjson_is_obj(root))
    {
        yyjson_doc_free(document);
        error = L"Windows Terminal settings must contain a JSON object.";
        return false;
    }

    ReadJsonString(root, "defaultProfile", catalog.defaultProfileGuid);
    yyjson_val* profilesNode = yyjson_obj_get(root, "profiles");
    yyjson_val* listNode = profilesNode;
    bool defaultHidden = false;
    if (yyjson_is_obj(profilesNode))
    {
        yyjson_val* defaultsNode = yyjson_obj_get(profilesNode, "defaults");
        if (yyjson_is_obj(defaultsNode))
            defaultHidden = ReadOptionalBool(defaultsNode, "hidden", false);
        listNode = yyjson_obj_get(profilesNode, "list");
    }

    std::set<std::wstring> seenGuids;
    if (yyjson_is_arr(listNode))
    {
        size_t index;
        size_t count;
        yyjson_val* value;
        yyjson_arr_foreach(listNode, index, count, value)
        {
            if (!yyjson_is_obj(value) || ReadOptionalBool(value, "hidden", defaultHidden))
                continue;

            WindowsTerminalProfile profile;
            if (!ReadJsonString(value, "name", profile.name) || profile.name.empty())
                continue;
            ReadJsonString(value, "guid", profile.guid);

            std::wstring foldedGuid = FoldCase(profile.guid);
            if (!foldedGuid.empty() && seenGuids.find(foldedGuid) != seenGuids.end())
                continue;

            if (!foldedGuid.empty())
                seenGuids.insert(std::move(foldedGuid));
            profile.isDefault = !profile.guid.empty() &&
                                FoldCase(profile.guid) == FoldCase(catalog.defaultProfileGuid);
            catalog.profiles.push_back(std::move(profile));
        }
    }

    yyjson_doc_free(document);
    return true;
}

const WindowsTerminalCatalog& CWindowsTerminalService::Refresh(bool force)
{
    IExecutableLocator* locator = ResolveExecutableLocator();
    ExecutableLocation executable;
    ExecutableLocationResult executableResult = locator != nullptr
                                                    ? locator->FindOnPath(L"wt.exe", executable)
                                                    : ExecutableLocationResult::Error(ERROR_INVALID_PARAMETER);
    if (!executableResult.success)
    {
        Catalog = WindowsTerminalCatalog{};
        CachedSettingsWriteTime = FILETIME{};
        CachedSettingsFilePresent = false;
        CacheInitialized = true;
        return Catalog;
    }

    std::wstring settingsPath;
    FileInfo settingsInfo{};
    const bool settingsFound = FindSettingsFile(executable, settingsPath, settingsInfo);
    if (!force && CacheInitialized && Catalog.installed &&
        Catalog.executablePath == executable.executablePath &&
        Catalog.settingsPath == settingsPath &&
        CachedSettingsFilePresent == settingsFound &&
        (!settingsFound || SameFileTime(CachedSettingsWriteTime, settingsInfo.lastWriteTime)))
        return Catalog;

    Catalog = WindowsTerminalCatalog{};
    Catalog.installed = true;
    Catalog.executablePath = executable.executablePath;
    Catalog.settingsPath = settingsPath;
    CachedSettingsFilePresent = settingsFound;
    CachedSettingsWriteTime = settingsFound ? settingsInfo.lastWriteTime : FILETIME{};
    CacheInitialized = true;
    if (!settingsFound)
        return Catalog;

    std::vector<char> bytes;
    DWORD errorCode = ERROR_SUCCESS;
    if (!ReadSettingsFile(settingsPath, bytes, errorCode))
    {
        Catalog.parseError = L"Unable to read Windows Terminal settings.";
        return Catalog;
    }

    std::wstring parseError;
    if (!ParseSettingsJson(bytes.data(), bytes.size(), Catalog, parseError))
    {
        Catalog.parseError = parseError;
        return Catalog;
    }
    Catalog.settingsAvailable = true;
    return Catalog;
}

bool ResolveWindowsTerminalTarget(const WindowsTerminalCatalog& catalog,
                                  const ShellTarget& requested,
                                  ShellTarget& resolved)
{
    resolved = requested;
    if (requested.kind != ShellTargetKind::WindowsTerminalProfile)
        return requested.kind == ShellTargetKind::WindowsTerminalDefault ||
               requested.kind == ShellTargetKind::ComSpec;

    if (!requested.profileGuid.empty())
    {
        std::wstring wanted = FoldCase(requested.profileGuid);
        for (const WindowsTerminalProfile& profile : catalog.profiles)
        {
            if (!profile.guid.empty() && FoldCase(profile.guid) == wanted)
            {
                resolved.profileGuid = profile.guid;
                resolved.profileName = profile.name;
                return true;
            }
        }
    }

    if (!requested.profileName.empty())
    {
        std::wstring wanted = FoldCase(requested.profileName);
        for (const WindowsTerminalProfile& profile : catalog.profiles)
        {
            if (FoldCase(profile.name) == wanted)
            {
                resolved.profileGuid = profile.guid;
                resolved.profileName = profile.name;
                return true;
            }
        }
    }
    return false;
}

bool CWindowsTerminalService::ResolveTarget(const ShellTarget& requested,
                                            ShellTarget& resolved) const
{
    return ResolveWindowsTerminalTarget(Catalog, requested, resolved);
}

std::wstring CWindowsTerminalService::BuildCommandLine(
    const std::wstring& executablePath, const ShellTarget& target,
    const WindowsTerminalLaunchRequest& request)
{
    std::wstring commandLine = QuoteArgument(executablePath);
    commandLine.append(L" -w new");
    if (request.usePosition)
    {
        commandLine.append(L" --pos ");
        commandLine.append(std::to_wstring(request.x));
        commandLine.push_back(L',');
        commandLine.append(std::to_wstring(request.y));
    }
    commandLine.append(L" new-tab");
    if (target.kind == ShellTargetKind::WindowsTerminalProfile)
    {
        // wt.exe's -p selector matches by profile NAME, not GUID, and dynamically
        // generated profiles (WSL, PowerShell) are only reachable by name. Prefer the
        // resolved name; the GUID stays a defensive fallback. (Duplicate names resolve
        // to wt's first match — wt has no CLI GUID selector to disambiguate them.)
        const std::wstring& selector = !target.profileName.empty()
                                           ? target.profileName
                                           : target.profileGuid;
        if (!selector.empty())
        {
            commandLine.append(L" -p ");
            commandLine.append(QuoteArgument(selector));
        }
    }
    if (!request.workingDirectory.empty())
    {
        commandLine.append(L" -d ");
        commandLine.append(QuoteArgument(request.workingDirectory));
    }
    return commandLine;
}

CommandShellResult CWindowsTerminalService::Launch(
    const ShellTarget& target, const WindowsTerminalLaunchRequest& request)
try
{
    const WindowsTerminalCatalog& catalog = Refresh(false);
    if (!catalog.installed)
        return CommandShellResult::Error(ERROR_FILE_NOT_FOUND);

    ShellTarget resolved;
    if (!ResolveTarget(target, resolved) || resolved.kind == ShellTargetKind::ComSpec)
        return CommandShellResult::Error(ERROR_NOT_FOUND);

    IProcess* process = ResolveProcess();
    if (process == nullptr)
        return CommandShellResult::Error(ERROR_INVALID_PARAMETER);

    std::wstring commandLine = BuildCommandLine(catalog.executablePath, resolved, request);
    ProcessStartInfo info;
    info.applicationName = catalog.executablePath;
    info.commandLine = commandLine;
    info.workingDirectory = request.workingDirectory;
    info.creationFlags = CREATE_DEFAULT_ERROR_MODE | NORMAL_PRIORITY_CLASS;
    HPROCESS launched = process->CreateProcess(info);
    if (launched == INVALID_HPROCESS)
    {
        DWORD errorCode = GetLastError();
        return CommandShellResult::Error(errorCode != ERROR_SUCCESS ? errorCode : ERROR_GEN_FAILURE);
    }
    return CommandShellResult::Ok(launched, process->GetProcessId(launched), process);
}
catch (const std::bad_alloc&)
{
    return CommandShellResult::Error(ERROR_NOT_ENOUGH_MEMORY);
}

static CWindowsTerminalService g_defaultWindowsTerminalService;
CWindowsTerminalService* gWindowsTerminalService = &g_defaultWindowsTerminalService;
