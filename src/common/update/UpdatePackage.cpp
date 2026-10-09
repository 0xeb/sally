// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#include "update/UpdatePackage.h"

#include "ExtendedLengthPath.h"
#include "update/ReleaseChecksums.h"

#include <shlobj.h>

#include <algorithm>
#include <vector>

#include "Win32TextCodec.h"

#pragma comment(lib, "version.lib")

namespace Sally::Update
{

std::wstring UpdateWorkFolder()
{
    PWSTR localAppData = NULL;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_CREATE, NULL, &localAppData)))
        return std::wstring();
    std::wstring folder = localAppData;
    CoTaskMemFree(localAppData);
    folder += L"\\Sally";
    CreateDirectoryW(ToExtendedLengthPath(folder).c_str(), NULL);
    folder += L"\\Update";
    CreateDirectoryW(ToExtendedLengthPath(folder).c_str(), NULL);
    const DWORD attributes = GetFileAttributesW(ToExtendedLengthPath(folder).c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0)
        return std::wstring();
    return folder;
}

std::wstring StagingFolderFor(const std::wstring& workFolder, const std::wstring& version, const std::wstring& arch)
{
    std::wstring name = version;
    if (!name.empty() && (name[0] == L'v' || name[0] == L'V'))
        name.erase(0, 1);
    return workFolder + L"\\v" + name + L"-" + arch;
}

std::wstring StagedFilesFolder(const std::wstring& staging)
{
    return staging + L"\\files";
}

std::wstring UpdateResultPath(const std::wstring& workFolder)
{
    return workFolder + L"\\result.txt";
}

WORD MachineForArch(const std::wstring& arch)
{
    if (CompareStringOrdinal(arch.c_str(), -1, L"x64", -1, TRUE) == CSTR_EQUAL)
        return IMAGE_FILE_MACHINE_AMD64;
    if (CompareStringOrdinal(arch.c_str(), -1, L"x86", -1, TRUE) == CSTR_EQUAL)
        return IMAGE_FILE_MACHINE_I386;
    if (CompareStringOrdinal(arch.c_str(), -1, L"ARM64", -1, TRUE) == CSTR_EQUAL)
        return IMAGE_FILE_MACHINE_ARM64;
    return 0;
}

bool ReadPeMachine(const std::wstring& path, WORD& machine)
{
    machine = 0;
    HANDLE file = CreateFileW(ToExtendedLengthPath(path).c_str(), GENERIC_READ, FILE_SHARE_READ, NULL,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE)
        return false;
    unsigned char header[4096];
    DWORD read = 0;
    const bool ok = ReadFile(file, header, sizeof(header), &read, NULL) != FALSE;
    CloseHandle(file);
    if (!ok || read < 0x40 || header[0] != 'M' || header[1] != 'Z')
        return false;
    const DWORD peOffset = header[0x3C] | (header[0x3D] << 8) | (header[0x3E] << 16) | (header[0x3F] << 24);
    if (peOffset > read - 6 || header[peOffset] != 'P' || header[peOffset + 1] != 'E' ||
        header[peOffset + 2] != 0 || header[peOffset + 3] != 0)
        return false;
    machine = static_cast<WORD>(header[peOffset + 4] | (header[peOffset + 5] << 8));
    return true;
}

bool ReadFileVersionText(const std::wstring& path, std::wstring& version)
{
    version.clear();
    DWORD ignored = 0;
    const DWORD size = GetFileVersionInfoSizeW(path.c_str(), &ignored);
    if (size == 0)
        return false;
    std::vector<unsigned char> data(size);
    if (!GetFileVersionInfoW(path.c_str(), 0, size, data.data()))
        return false;
    VS_FIXEDFILEINFO* info = NULL;
    UINT infoSize = 0;
    if (!VerQueryValueW(data.data(), L"\\", reinterpret_cast<void**>(&info), &infoSize) || info == NULL ||
        infoSize < sizeof(VS_FIXEDFILEINFO) || info->dwSignature != VS_FFI_SIGNATURE)
        return false;
    version = std::to_wstring(HIWORD(info->dwFileVersionMS)) + L"." + std::to_wstring(LOWORD(info->dwFileVersionMS)) +
              L"." + std::to_wstring(HIWORD(info->dwFileVersionLS)) + L"." +
              std::to_wstring(LOWORD(info->dwFileVersionLS));
    return true;
}

bool VersionMatches(const std::wstring& actual, const std::wstring& expected)
{
    std::wstring wanted = expected;
    if (!wanted.empty() && (wanted[0] == L'v' || wanted[0] == L'V'))
        wanted.erase(0, 1);
    auto split = [](const std::wstring& text, std::vector<unsigned long>& parts) {
        parts.clear();
        size_t start = 0;
        while (start <= text.size())
        {
            size_t end = text.find(L'.', start);
            if (end == std::wstring::npos)
                end = text.size();
            const std::wstring part = text.substr(start, end - start);
            if (part.empty() || part.size() > 9 || part.find_first_not_of(L"0123456789") != std::wstring::npos)
                return false;
            parts.push_back(std::stoul(part));
            start = end + 1;
        }
        return true;
    };
    std::vector<unsigned long> have;
    std::vector<unsigned long> want;
    if (!split(actual, have) || !split(wanted, want) || want.empty() || want.size() > have.size())
        return false;
    for (size_t i = 0; i < want.size(); ++i)
    {
        if (have[i] != want[i])
            return false;
    }
    return true;
}

VersionOrder CompareVersionText(const std::wstring& installed, const std::wstring& package)
{
    auto split = [](const std::wstring& text, std::vector<unsigned long>& parts) {
        parts.clear();
        size_t start = 0;
        while (start <= text.size())
        {
            size_t end = text.find(L'.', start);
            if (end == std::wstring::npos)
                end = text.size();
            const std::wstring part = text.substr(start, end - start);
            if (part.empty() || part.size() > 9 || part.find_first_not_of(L"0123456789") != std::wstring::npos)
                return false;
            parts.push_back(std::stoul(part));
            start = end + 1;
        }
        return true;
    };
    std::vector<unsigned long> have;
    std::vector<unsigned long> offered;
    if (!split(installed, have) || !split(package, offered))
        return VersionOrder::Unknown;
    const size_t count = (std::max)(have.size(), offered.size());
    for (size_t i = 0; i < count; ++i)
    {
        const unsigned long a = i < have.size() ? have[i] : 0;
        const unsigned long b = i < offered.size() ? offered[i] : 0;
        if (a != b)
            return b > a ? VersionOrder::Newer : VersionOrder::Older;
    }
    return VersionOrder::Same;
}

std::wstring InstallRootOfUpdater(const std::wstring& updaterPath)
{
    const size_t file = updaterPath.find_last_of(L"\\/");
    if (file == std::wstring::npos || file == 0)
        return std::wstring();
    const std::wstring folder = updaterPath.substr(0, file);
    const size_t parent = folder.find_last_of(L"\\/");
    if (parent == std::wstring::npos)
        return std::wstring();
    const std::wstring name = folder.substr(parent + 1);
    if (CompareStringOrdinal(name.c_str(), -1, L"utils", -1, TRUE) != CSTR_EQUAL)
        return std::wstring();
    return folder.substr(0, parent);
}

bool FindChecksumNextTo(const std::wstring& packagePath, std::string& sha256, std::wstring& sumsPath)
{
    sha256.clear();
    sumsPath.clear();
    const size_t slash = packagePath.find_last_of(L"\\/");
    const std::wstring folder = slash == std::wstring::npos ? std::wstring(L".") : packagePath.substr(0, slash);
    const std::wstring packageName = slash == std::wstring::npos ? packagePath : packagePath.substr(slash + 1);
    std::string packageUtf8;
    if (!Win32EncodeText(CP_UTF8, packageName, packageUtf8))
        return false;

    WIN32_FIND_DATAW find;
    HANDLE handle = FindFirstFileW(ToExtendedLengthPath(folder + L"\\*SHA256SUMS*.txt").c_str(), &find);
    if (handle == INVALID_HANDLE_VALUE)
        return false;
    bool found = false;
    do
    {
        if ((find.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0 || find.nFileSizeHigh != 0 ||
            find.nFileSizeLow > 64 * 1024)
            continue;
        const std::wstring candidate = folder + L"\\" + find.cFileName;
        HANDLE file = CreateFileW(ToExtendedLengthPath(candidate).c_str(), GENERIC_READ, FILE_SHARE_READ, NULL,
                                  OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if (file == INVALID_HANDLE_VALUE)
            continue;
        std::string text(find.nFileSizeLow, '\0');
        DWORD read = 0;
        const bool ok = text.empty() || (ReadFile(file, &text[0], static_cast<DWORD>(text.size()), &read, NULL) &&
                                         read == text.size());
        CloseHandle(file);
        std::vector<ChecksumEntry> entries;
        std::string error;
        const ChecksumEntry* entry = NULL;
        if (ok && ParseSha256Sums(text, entries, error))
            entry = FindChecksum(entries, packageUtf8);
        if (entry != NULL)
        {
            sha256 = entry->Sha256;
            sumsPath = candidate;
            found = true;
        }
    } while (!found && FindNextFileW(handle, &find));
    FindClose(handle);
    return found;
}

bool CheckStagedRelease(const std::wstring& filesFolder, const std::wstring& version, const std::wstring& arch,
                        std::wstring& error)
{
    const std::wstring exe = filesFolder + L"\\sally.exe";
    WORD machine = 0;
    if (!ReadPeMachine(exe, machine))
    {
        error = L"The package has no valid sally.exe.";
        return false;
    }
    const WORD wanted = MachineForArch(arch);
    if (wanted == 0 || machine != wanted)
    {
        error = L"The package is for another processor architecture than " + arch + L".";
        return false;
    }
    std::wstring fileVersion;
    if (!ReadFileVersionText(exe, fileVersion) || !VersionMatches(fileVersion, version))
    {
        error = L"The package holds Sally " + (fileVersion.empty() ? std::wstring(L"?") : fileVersion) +
                L", not " + version + L".";
        return false;
    }
    return true;
}

} // namespace Sally::Update
