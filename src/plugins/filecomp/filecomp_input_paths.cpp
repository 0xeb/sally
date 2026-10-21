// SPDX-FileCopyrightText: 2025-2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later
#include "precomp.h"
#include "filecomp_input_paths.h"

namespace
{
    bool DriveLetter(wchar_t value)
    {
        return (value >= L'A' && value <= L'Z') || (value >= L'a' && value <= L'z');
    }
    size_t DriveIndex(wchar_t value)
    {
        return static_cast<size_t>((value >= L'a' ? value - L'a' : value - L'A'));
    }
    bool NamespacePath(const std::wstring& path)
    {
        return path.compare(0, 4, L"\\\\?\\") == 0 || path.compare(0, 4, L"\\\\.\\") == 0;
    }
    bool Qualified(const std::wstring& path)
    {
        return (path.size() >= 3 && DriveLetter(path[0]) && path[1] == L':' && path[2] == L'\\') ||
               path.compare(0, 2, L"\\\\") == 0;
    }
    std::wstring LogicalPath(const std::wstring& path)
    {
        if (path.size() >= 8 && path.compare(0, 4, L"\\\\?\\") == 0 &&
            (path[4] == L'U' || path[4] == L'u') && (path[5] == L'N' || path[5] == L'n') &&
            (path[6] == L'C' || path[6] == L'c') && path[7] == L'\\')
            return L"\\\\" + path.substr(8);
        if (path.size() >= 7 && path.compare(0, 4, L"\\\\?\\") == 0 && DriveLetter(path[4]) && path[5] == L':')
            return path.substr(4);
        return path;
    }
    std::wstring Join(const std::wstring& directory, const std::wstring& relative)
    {
        std::wstring result = directory;
        if (!result.empty() && result.back() != L'\\')
            result += L'\\';
        result += relative;
        return result;
    }
} // namespace

std::wstring FilecompInitialDirectory(const std::wstring& file)
{
    std::wstring normalized = file;
    std::replace(normalized.begin(), normalized.end(), L'/', L'\\');
    if (!Qualified(normalized))
        return {};
    const size_t separator = normalized.find_last_of(L'\\');
    return separator == std::wstring::npos ? std::wstring() : normalized.substr(0, separator + 1);
}

CFilecompPathContext CaptureFilecompPathContext(CSalamanderGeneralAbstract& general, IPathService& paths,
                                                const std::wstring& baseDirectory)
{
    CFilecompPathContext context;
    std::wstring base = baseDirectory, prepared;
    if (base.empty() && !paths.GetCurrentDirectory(base).success)
        return context;
    if (!paths.PrepareForIo(base.c_str(), prepared).success)
        return context;
    context.BaseDirectory = LogicalPath(prepared);
    SPLGetRootPathOwned(&general, context.BaseDirectory.c_str(), context.BaseRoot);
    for (size_t drive = 0; drive < context.DriveDirectories.size(); ++drive)
    {
        std::wstring query(1, static_cast<wchar_t>(L'A' + drive));
        query += L":.";
        std::wstring directory;
        if (paths.GetFullPathName(query.c_str(), directory).success)
            context.DriveDirectories[drive] = LogicalPath(directory);
    }
    if (context.BaseDirectory.size() >= 3 && DriveLetter(context.BaseDirectory[0]) && context.BaseDirectory[1] == L':')
        context.DriveDirectories[DriveIndex(context.BaseDirectory[0])] = context.BaseDirectory;
    return context;
}

CFilecompPathContext CaptureFilecompSourceContext(CSalamanderGeneralAbstract& general, IPathService& paths)
{
    std::wstring base;
    int type = 0;
    if (!SPLGetPanelPathOwned(&general, PANEL_SOURCE, base, &type) || type != PATH_TYPE_WINDOWS)
        base.clear();
    return CaptureFilecompPathContext(general, paths, base);
}

bool ResolveFilecompInputPath(IPathService& paths, const CFilecompPathContext& context,
                              const std::wstring& input, std::wstring& resolved) noexcept
{
    try
    {
        if (input.empty() || input.find(L'\0') != std::wstring::npos)
            return false;
        std::wstring candidate = input;
        std::replace(candidate.begin(), candidate.end(), L'/', L'\\');
        if (NamespacePath(candidate))
        {
            resolved.swap(candidate);
            return true;
        }
        if (!Qualified(candidate))
        {
            if (candidate.size() >= 2 && DriveLetter(candidate[0]) && candidate[1] == L':')
            {
                const auto& directory = context.DriveDirectories[DriveIndex(candidate[0])];
                if (directory.empty())
                    return false;
                candidate = Join(directory, candidate.substr(2));
            }
            else if (candidate[0] == L'\\')
            {
                if (context.BaseRoot.empty())
                    return false;
                candidate = Join(context.BaseRoot, candidate.substr(1));
            }
            else
            {
                if (context.BaseDirectory.empty())
                    return false;
                candidate = Join(context.BaseDirectory, candidate);
            }
        }
        if (!Qualified(candidate))
            return false;
        std::wstring prepared;
        if (!paths.PrepareForIo(candidate.c_str(), prepared).success)
            return false;
        std::wstring logical = LogicalPath(prepared);
        resolved.swap(logical);
        return true;
    }
    catch (...)
    {
        return false;
    }
}

bool FilecompFileExists(CSalamanderGeneralAbstract& general, const std::wstring& file) noexcept
{
    try
    {
        const DWORD attributes = general.SalGetFileAttributes(file.c_str());
        if (attributes != INVALID_FILE_ATTRIBUTES)
            return (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
        // An ADS may have no independently queryable attributes. Confirm a finite
        // target through the SDK's read-only size query before calling it missing.
        CQuadWord size;
        DWORD error = ERROR_SUCCESS;
        if (general.SalGetFileSize2(file.c_str(), size, &error))
            return true;
        // These errors cannot prove absence; retain the worker's useful open error.
        return error == ERROR_ACCESS_DENIED || error == ERROR_SHARING_VIOLATION;
    }
    catch (...)
    {
        return false;
    }
}
