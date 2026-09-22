// SPDX-FileCopyrightText: 2025-2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#include "IPathService.h"

#include <algorithm>
#include <cwctype>
#include <limits>
#include <new>
#include <stdexcept>

static bool HasLongPrefix(const wchar_t* path)
{
    if (path == NULL)
        return false;
    size_t len = wcslen(path);
    return len >= 4 && wcsncmp(path, L"\\\\?\\", 4) == 0;
}

static bool HasDevicePrefix(const wchar_t* path)
{
    return path != NULL && wcsncmp(path, L"\\\\.\\", 4) == 0;
}

static bool IsUNCPath(const wchar_t* path)
{
    return path != NULL &&
           path[0] == L'\\' && path[1] == L'\\' &&
           !HasLongPrefix(path);
}

static bool IsDriveAbsolutePath(const wchar_t* path)
{
    return path != NULL &&
           ((path[0] >= L'A' && path[0] <= L'Z') || (path[0] >= L'a' && path[0] <= L'z')) &&
           path[1] == L':' && (path[2] == L'\\' || path[2] == L'/');
}

static void NormalizeSeparators(std::wstring& path)
{
    std::replace(path.begin(), path.end(), L'/', L'\\');
}

static bool HasRedundantSeparator(const std::wstring& path)
{
    // Keep the leading UNC pair intact, but do not pass redundant separators through to the
    // extended-length namespace: unlike ordinary Win32 paths, \\?\ paths are not normalized by
    // the system and a harmless-looking C:\\dir spelling otherwise fails with
    // ERROR_INVALID_NAME.
    const size_t begin = IsUNCPath(path.c_str()) ? 2 : 1;
    for (size_t i = begin; i < path.size(); ++i)
    {
        const bool separator = path[i] == L'\\' || path[i] == L'/';
        const bool previousSeparator = path[i - 1] == L'\\' || path[i - 1] == L'/';
        if (separator && previousSeparator)
            return true;
    }
    return false;
}

static bool HasDotSegment(const std::wstring& path)
{
    size_t componentStart = 0;
    while (componentStart < path.size())
    {
        while (componentStart < path.size() &&
               (path[componentStart] == L'\\' || path[componentStart] == L'/'))
            ++componentStart;
        size_t componentEnd = componentStart;
        while (componentEnd < path.size() &&
               path[componentEnd] != L'\\' && path[componentEnd] != L'/')
            ++componentEnd;
        size_t length = componentEnd - componentStart;
        if ((length == 1 && path[componentStart] == L'.') ||
            (length == 2 && path[componentStart] == L'.' && path[componentStart + 1] == L'.'))
            return true;
        componentStart = componentEnd;
    }
    return false;
}

static bool NextCapacity(DWORD current, DWORD suggested, DWORD& next)
{
    unsigned long long candidate = suggested;
    if (candidate <= current)
    {
        candidate = static_cast<unsigned long long>(current) * 2;
    }
    if (candidate <= current || candidate > (std::numeric_limits<DWORD>::max)())
        return false;
    next = static_cast<DWORD>(candidate);
    return true;
}

static bool HasPatternInFinalComponent(const std::wstring& path)
{
    const size_t separator = path.find_last_of(L"\\/");
    const size_t component = separator == std::wstring::npos ? 0 : separator + 1;
    return path.find_first_of(L"*?", component) != std::wstring::npos;
}

static bool IsReservedDosDeviceLeaf(const std::wstring& path)
{
    const size_t separator = path.find_last_of(L"\\/");
    const size_t component = separator == std::wstring::npos ? 0 : separator + 1;
    std::wstring leaf = path.substr(component);
    if (leaf == L"." || leaf == L"..")
        return false;

    const size_t stream = leaf.find(L':');
    if (stream != std::wstring::npos)
        leaf.resize(stream);
    while (!leaf.empty() && (leaf.back() == L' ' || leaf.back() == L'.'))
        leaf.pop_back();
    const size_t extension = leaf.find(L'.');
    if (extension != std::wstring::npos)
        leaf.resize(extension);
    std::transform(leaf.begin(), leaf.end(), leaf.begin(),
                   [](wchar_t value) { return static_cast<wchar_t>(towupper(value)); });

    if (leaf == L"CON" || leaf == L"PRN" || leaf == L"AUX" || leaf == L"NUL")
        return true;
    return leaf.size() == 4 &&
           ((leaf.compare(0, 3, L"COM") == 0) || (leaf.compare(0, 3, L"LPT") == 0)) &&
           leaf[3] >= L'1' && leaf[3] <= L'9';
}

static bool NeedsLiteralLeafResolution(const std::wstring& path)
{
    const size_t separator = path.find_last_of(L"\\/");
    const std::wstring leaf = path.substr(separator == std::wstring::npos ? 0 : separator + 1);
    if (leaf.empty() || leaf == L"." || leaf == L"..")
        return false;
    return leaf.back() == L' ' || leaf.back() == L'.' || IsReservedDosDeviceLeaf(path);
}

static bool ResizeBuffer(std::wstring& buffer, DWORD capacity)
{
    try
    {
        buffer.resize(static_cast<size_t>(capacity));
        return true;
    }
    catch (const std::bad_alloc&)
    {
        return false;
    }
    catch (const std::length_error&)
    {
        return false;
    }
}

class Win32PathService : public IPathService
{
public:
    PathResult PrepareForIo(const wchar_t* path, std::wstring& outPath) override
    {
        if (path == NULL || path[0] == L'\0')
            return PathResult::Error(ERROR_INVALID_PARAMETER);

        try
        {
            std::wstring prepared(path);
            if (HasDevicePrefix(prepared.c_str()) || HasLongPrefix(prepared.c_str()))
            {
                outPath.swap(prepared);
                return PathResult::Ok();
            }

            const bool absolute = IsDriveAbsolutePath(prepared.c_str()) || IsUNCPath(prepared.c_str());
            if (!absolute || HasDotSegment(prepared) || HasRedundantSeparator(prepared))
            {
                std::wstring absolutePath;
                if (NeedsLiteralLeafResolution(prepared))
                {
                    const size_t separator = prepared.find_last_of(L"\\/");
                    const std::wstring leaf = prepared.substr(
                        separator == std::wstring::npos ? 0 : separator + 1);
                    std::wstring parent;
                    if (separator == std::wstring::npos)
                        parent = L".";
                    else if (separator == 2 && prepared[1] == L':')
                        parent = prepared.substr(0, 3);
                    else
                        parent = prepared.substr(0, separator);
                    if (parent.empty())
                        parent = L".";

                    PathResult parentResult = GetFullPathName(parent.c_str(), absolutePath);
                    if (!parentResult.success)
                        return parentResult;
                    if (!absolutePath.empty() && absolutePath.back() != L'\\' &&
                        absolutePath.back() != L'/')
                        absolutePath.push_back(L'\\');
                    absolutePath.append(leaf);
                }
                else
                {
                    PathResult fullResult = GetFullPathName(prepared.c_str(), absolutePath);
                    if (!fullResult.success)
                        return fullResult;
                }
                prepared.swap(absolutePath);
            }

            if (HasDevicePrefix(prepared.c_str()) || HasLongPrefix(prepared.c_str()))
            {
                outPath.swap(prepared);
                return PathResult::Ok();
            }

            NormalizeSeparators(prepared);
            if (IsUNCPath(prepared.c_str()))
                prepared = L"\\\\?\\UNC\\" + prepared.substr(2);
            else
                prepared = L"\\\\?\\" + prepared;
            outPath.swap(prepared);
            return PathResult::Ok();
        }
        catch (const std::bad_alloc&)
        {
            return PathResult::Error(ERROR_NOT_ENOUGH_MEMORY);
        }
        catch (const std::length_error&)
        {
            return PathResult::Error(ERROR_NOT_ENOUGH_MEMORY);
        }
    }

    PathResult PrepareEnumerationPattern(const wchar_t* path, const wchar_t* pattern,
                                         std::wstring& outPath) override
    {
        if (path == NULL || path[0] == L'\0')
            return PathResult::Error(ERROR_INVALID_PARAMETER);

        try
        {
            std::wstring combined(path);
            if (!HasPatternInFinalComponent(combined))
            {
                if (!combined.empty() && combined.back() != L'\\' && combined.back() != L'/')
                    combined.push_back(L'\\');
                combined.append(pattern != NULL && pattern[0] != L'\0' ? pattern : L"*");
            }

            const size_t separator = combined.find_last_of(L"\\/");
            const std::wstring leaf = combined.substr(
                separator == std::wstring::npos ? 0 : separator + 1);
            std::wstring directory;
            if (separator == std::wstring::npos)
                directory = L".";
            else if (separator == 2 && combined[1] == L':')
                directory = combined.substr(0, 3);
            else
                directory = combined.substr(0, separator);
            if (directory.empty())
                directory = L".";

            std::wstring preparedDirectory;
            PathResult directoryResult = PrepareForIo(directory.c_str(), preparedDirectory);
            if (!directoryResult.success)
                return directoryResult;
            if (!preparedDirectory.empty() && preparedDirectory.back() != L'\\')
                preparedDirectory.push_back(L'\\');
            preparedDirectory.append(leaf);
            outPath.swap(preparedDirectory);
            return PathResult::Ok();
        }
        catch (const std::bad_alloc&)
        {
            return PathResult::Error(ERROR_NOT_ENOUGH_MEMORY);
        }
        catch (const std::length_error&)
        {
            return PathResult::Error(ERROR_NOT_ENOUGH_MEMORY);
        }
    }

    PathResult GetCurrentDirectory(std::wstring& outPath) override
    {
        DWORD capacity = 256;
        for (;;)
        {
            std::wstring buffer;
            if (!ResizeBuffer(buffer, capacity))
                return PathResult::Error(ERROR_NOT_ENOUGH_MEMORY);

            DWORD len = ::GetCurrentDirectoryW(capacity, &buffer[0]);
            if (len == 0)
                return PathResult::Error(GetLastError());

            if (len < capacity)
            {
                buffer.resize(len);
                outPath.swap(buffer);
                return PathResult::Ok();
            }

            DWORD next;
            DWORD suggested = len == (std::numeric_limits<DWORD>::max)() ? len : len + 1;
            if (!NextCapacity(capacity, suggested, next))
                return PathResult::Error(ERROR_FILENAME_EXCED_RANGE);
            capacity = next;
        }
    }

    PathResult GetModuleFileName(HMODULE module, std::wstring& outPath) override
    {
        DWORD capacity = 256;
        for (;;)
        {
            std::wstring buffer;
            if (!ResizeBuffer(buffer, capacity))
                return PathResult::Error(ERROR_NOT_ENOUGH_MEMORY);

            SetLastError(ERROR_SUCCESS);
            DWORD len = ::GetModuleFileNameW(module, &buffer[0], capacity);
            if (len == 0)
                return PathResult::Error(GetLastError());

            DWORD err = GetLastError();
            bool truncated = (len >= capacity) || (len == capacity - 1 && err == ERROR_INSUFFICIENT_BUFFER);
            if (!truncated)
            {
                buffer.resize(len);
                outPath.swap(buffer);
                return PathResult::Ok();
            }

            DWORD next;
            DWORD suggested = capacity == (std::numeric_limits<DWORD>::max)() ? capacity : capacity + 1;
            if (!NextCapacity(capacity, suggested, next))
                return PathResult::Error(ERROR_FILENAME_EXCED_RANGE);
            capacity = next;
        }
    }

    PathResult GetTempPath(std::wstring& outPath) override
    {
        DWORD capacity = 256;
        for (;;)
        {
            std::wstring buffer;
            if (!ResizeBuffer(buffer, capacity))
                return PathResult::Error(ERROR_NOT_ENOUGH_MEMORY);

            DWORD len = ::GetTempPathW(capacity, &buffer[0]);
            if (len == 0)
                return PathResult::Error(GetLastError());

            if (len < capacity)
            {
                buffer.resize(len);
                outPath.swap(buffer);
                return PathResult::Ok();
            }

            DWORD next;
            DWORD suggested = len == (std::numeric_limits<DWORD>::max)() ? len : len + 1;
            if (!NextCapacity(capacity, suggested, next))
                return PathResult::Error(ERROR_FILENAME_EXCED_RANGE);
            capacity = next;
        }
    }

    PathResult GetFullPathName(const wchar_t* inputPath, std::wstring& outPath) override
    {
        if (inputPath == NULL || inputPath[0] == L'\0')
            return PathResult::Error(ERROR_INVALID_PARAMETER);

        DWORD capacity = 256;
        for (;;)
        {
            std::wstring buffer;
            if (!ResizeBuffer(buffer, capacity))
                return PathResult::Error(ERROR_NOT_ENOUGH_MEMORY);

            DWORD len = ::GetFullPathNameW(inputPath, capacity, &buffer[0], NULL);
            if (len == 0)
                return PathResult::Error(GetLastError());

            if (len < capacity)
            {
                buffer.resize(len);
                outPath.swap(buffer);
                return PathResult::Ok();
            }

            DWORD next;
            DWORD suggested = len == (std::numeric_limits<DWORD>::max)() ? len : len + 1;
            if (!NextCapacity(capacity, suggested, next))
                return PathResult::Error(ERROR_FILENAME_EXCED_RANGE);
            capacity = next;
        }
    }
};

static Win32PathService g_win32PathService;
IPathService* gPathService = &g_win32PathService;

IPathService* GetWin32PathService()
{
    return &g_win32PathService;
}
