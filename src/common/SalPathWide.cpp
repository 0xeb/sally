// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

// Wide path-string helpers. Extracted from sally_path_utils.cpp and
// sally_entry_lifecycle.cpp so production and the private tests compile the
// same translation unit.

#ifdef SALLY_WORKER_CORE_STANDALONE
#include "common/WorkerCoreStandalone.h"
#else
#include "precomp.h"
#endif

#include <cstring>
#include <cwchar>
#include <string>

#include "common/SalPathWide.h"

size_t AppendNextPathComponentW(const std::wstring& source, size_t& sourceEnd,
                                std::wstring& result)
{
    const size_t insertion = result.size();
    if (sourceEnd >= source.size())
        return insertion;

    const size_t start = sourceEnd;
    sourceEnd = source.find(L'\\', start + 1);
    if (sourceEnd == std::wstring::npos)
        sourceEnd = source.size();
    result.append(source, start, sourceEnd - start);
    return insertion;
}

void ReplacePathComponentW(std::wstring& path, size_t componentSeparator,
                           const wchar_t* correctedName)
{
    if (componentSeparator >= path.size() || correctedName == nullptr)
        return;
    path.resize(componentSeparator + 1);
    path.append(correctedName);
}

// Wide version - appends name to path (modifies path in-place)
// Handles leading/trailing backslashes properly
void SalPathAppendW(std::wstring& path, const wchar_t* name)
{
    if (name == nullptr)
        return;

    // Skip leading backslash in name
    if (*name == L'\\')
        name++;

    // Remove trailing backslash from path
    if (!path.empty() && path.back() == L'\\')
        path.pop_back();

    // Append name if non-empty
    if (*name != L'\0')
    {
        if (!path.empty())
            path += L'\\';
        path += name;
    }
}

// Raw-buffer overload for in-place path manipulation
BOOL SalPathAppendW(wchar_t* path, const wchar_t* name, int pathSize)
{
    if (path == nullptr || name == nullptr || pathSize <= 0)
        return FALSE;

    if (*name == L'\\')
        ++name;

    const std::size_t capacity = static_cast<std::size_t>(pathSize);
    std::size_t length = std::wcslen(path);
    if (length >= capacity)
        return FALSE;
    if (length > 0 && path[length - 1] == L'\\')
        --length;

    if (*name != L'\0')
    {
        const std::size_t nameLength = std::wcslen(name);
        // Preserve the frozen helper's strict '< pathSize' check. For an empty
        // path this intentionally requires one spare character beyond the
        // appended name and terminator.
        if (capacity - length <= 1 ||
            nameLength >= capacity - length - 1)
            return FALSE;

        std::size_t destination = 0;
        if (length != 0)
        {
            path[length] = L'\\';
            destination = length + 1;
        }
        std::memmove(path + destination, name,
                     (nameLength + 1) * sizeof(wchar_t));
    }
    else
        path[length] = L'\0';
    return TRUE;
}

// Wide version - ensures path ends with backslash
void SalPathAddBackslashW(std::wstring& path)
{
    if (!path.empty() && path.back() != L'\\')
        path += L'\\';
}

// Raw-buffer overload for in-place path manipulation
BOOL SalPathAddBackslashW(wchar_t* path, int pathSize)
{
    if (path == nullptr || pathSize <= 0)
        return FALSE;

    const std::size_t capacity = static_cast<std::size_t>(pathSize);
    const std::size_t length = std::wcslen(path);
    if (length >= capacity)
        return FALSE;

    if (length > 0 && path[length - 1] != L'\\')
    {
        if (capacity - length <= 1)
            return FALSE;
        path[length] = L'\\';
        path[length + 1] = L'\0';
    }
    return TRUE;
}

// Wide version - removes trailing backslash
void SalPathRemoveBackslashW(std::wstring& path)
{
    if (!path.empty() && path.back() == L'\\')
        path.pop_back();
}

// Raw-buffer overload for in-place path manipulation
void SalPathRemoveBackslashW(wchar_t* path)
{
    if (path == nullptr)
        return;

    const std::size_t length = std::wcslen(path);
    if (length > 0 && path[length - 1] == L'\\')
        path[length - 1] = L'\0';
}

// Wide version - strips path leaving just filename
// "C:\foo\bar.txt" -> "bar.txt", "bar.txt" -> "bar.txt"
void SalPathStripPathW(std::wstring& path)
{
    size_t pos = path.rfind(L'\\');
    if (pos != std::wstring::npos)
        path = path.substr(pos + 1);
}

// Raw-buffer adapter; the std::wstring overload remains the path-policy owner.
void SalPathStripPathW(wchar_t* path)
{
    if (path == nullptr)
        return;

    std::wstring pathW(path);
    SalPathStripPathW(pathW);
    std::wmemcpy(path, pathW.c_str(), pathW.size() + 1);
}

// Wide version - finds filename portion of path
// Returns pointer within the string to the filename part
//
// SEMANTICS CORRECTED to match the narrow SalPathFindFileName and
// the SDK contract, which specifies that this "ignores backslash at end of
// 'path'".
//
// The previous implementation scanned FORWARD and returned the text after the
// LAST backslash, so "C:\dir\" yielded L"" where the narrow form yields
// L"dir\". The two had silently diverged - the wide one was added later and
// nothing ever compared them, because no test covered a trailing separator.
// Scanning backward from len-2 (i.e. skipping one trailing separator) is what
// both the narrow code and the documentation specify.
const wchar_t* SalPathFindFileNameW(const wchar_t* path)
{
    if (path == nullptr)
        return nullptr;

    const size_t len = wcslen(path);
    if (len < 2)
        return path; // nothing sits before a possible trailing separator

    const wchar_t* iterator = path + len - 2;
    while (iterator >= path)
    {
        if (*iterator == L'\\')
            return iterator + 1;
        iterator--;
    }
    return path;
}

// Wide version - removes extension from path
// "C:\foo\bar.txt" -> "C:\foo\bar"
void SalPathRemoveExtensionW(std::wstring& path)
{
    size_t len = path.length();
    for (size_t i = len; i > 0; i--)
    {
        if (path[i - 1] == L'.')
        {
            path.resize(i - 1);
            return;
        }
        if (path[i - 1] == L'\\')
            return; // No extension found
    }
}

// Raw-buffer adapter; the std::wstring overload remains the path-policy owner.
void SalPathRemoveExtensionW(wchar_t* path)
{
    if (path == nullptr)
        return;

    std::wstring pathW(path);
    SalPathRemoveExtensionW(pathW);
    std::wmemcpy(path, pathW.c_str(), pathW.size() + 1);
}

static bool SalPathAddExtensionCoreW(std::wstring& path, const wchar_t* extension,
                                     bool* extensionAlreadyPresent)
{
    if (extensionAlreadyPresent != nullptr)
        *extensionAlreadyPresent = false;
    if (extension == nullptr)
        return false;

    size_t len = path.length();
    for (size_t i = len; i > 0; i--)
    {
        if (path[i - 1] == L'.')
        {
            if (extensionAlreadyPresent != nullptr)
                *extensionAlreadyPresent = true;
            return true; // Extension already exists
        }
        if (path[i - 1] == L'\\')
            break; // No extension, add it
    }
    path += extension;
    return true;
}

// Wide version - adds extension if not already present
// Returns true if extension was added or already exists
bool SalPathAddExtensionW(std::wstring& path, const wchar_t* extension)
{
    return SalPathAddExtensionCoreW(path, extension, nullptr);
}

// Raw-buffer overload preserves the frozen no-op capacity quirk while keeping
// extension detection in the std::wstring policy owner.
BOOL SalPathAddExtensionW(wchar_t* path, const wchar_t* extension, int pathSize)
{
    if (path == nullptr || extension == nullptr)
        return FALSE;

    std::wstring pathW(path);
    bool extensionAlreadyPresent = false;
    if (!SalPathAddExtensionCoreW(
            pathW, extension, &extensionAlreadyPresent))
        return FALSE;
    if (extensionAlreadyPresent)
        return TRUE;
    if (pathSize <= 0 ||
        pathW.size() >= static_cast<std::size_t>(pathSize))
        return FALSE;

    std::wmemcpy(path, pathW.c_str(), pathW.size() + 1);
    return TRUE;
}

// Wide version - replaces extension (or adds if none)
// "C:\foo\bar.txt" + ".bak" -> "C:\foo\bar.bak"
bool SalPathRenameExtensionW(std::wstring& path, const wchar_t* extension)
{
    if (extension == nullptr)
        return false;

    size_t len = path.length();
    for (size_t i = len; i > 0; i--)
    {
        if (path[i - 1] == L'.')
        {
            path.resize(i - 1);
            break;
        }
        if (path[i - 1] == L'\\')
            break; // No existing extension
    }
    path += extension;
    return true;
}

// Raw-buffer overload adapts capacity and publication around the string owner.
BOOL SalPathRenameExtensionW(wchar_t* path, const wchar_t* extension, int pathSize)
{
    if (path == nullptr || extension == nullptr || pathSize <= 0)
        return FALSE;

    std::wstring pathW(path);
    if (!SalPathRenameExtensionW(pathW, extension) ||
        pathW.size() >= static_cast<std::size_t>(pathSize))
        return FALSE;

    std::wmemcpy(path, pathW.c_str(), pathW.size() + 1);
    return TRUE;
}

// Trims leading/trailing whitespace (chars <= ' ') in place.
// Returns TRUE if the string changed.
BOOL CutSpacesFromBothSidesW(wchar_t* path)
{
    BOOL ch = FALSE;
    wchar_t* n = path;
    while (*n != 0 && *n <= L' ')
        n++;
    if (n > path)
    {
        memmove(path, n, (wcslen(n) + 1) * sizeof(wchar_t));
        ch = TRUE;
    }
    n = path + wcslen(path);
    while (n > path && (*(n - 1) <= L' '))
        n--;
    if (*n != 0)
    {
        *n = 0;
        ch = TRUE;
    }
    return ch;
}

// Wide sibling of MakeValidFileName (files_window_view_edit.cpp) - trims
// leading spaces and trailing spaces/dots, same Explorer-parity rule
// (https://forum.altap.cz/viewtopic.php?f=16&t=5891), which RenameFileInternal never
// applied: a Unicode rename could leave a trailing dot/space Explorer itself would strip.
BOOL MakeValidFileNameW(std::wstring& name)
{
    BOOL ch = FALSE;
    size_t begin = 0;
    while (begin < name.length() && name[begin] <= L' ')
        begin++;
    if (begin > 0)
    {
        name.erase(0, begin);
        ch = TRUE;
    }
    size_t end = name.length();
    while (end > 0 && (name[end - 1] <= L' ' || name[end - 1] == L'.'))
        end--;
    if (end < name.length())
    {
        name.resize(end);
        ch = TRUE;
    }
    return ch;
}

// Wide version - cuts last directory from path
// Returns false if path cannot be shortened (e.g., "C:\" or "\\server\share")
// If cutDir is provided, it receives the cut directory name
bool CutDirectoryW(std::wstring& path, std::wstring* cutDir)
{
    if (path.empty())
    {
        if (cutDir)
            cutDir->clear();
        return false;
    }

    // Remove trailing backslash for processing
    size_t len = path.length();
    if (len > 0 && path[len - 1] == L'\\')
        len--;

    // Find last backslash
    size_t lastBS = path.rfind(L'\\', len - 1);
    if (lastBS == std::wstring::npos)
    {
        if (cutDir)
            cutDir->clear();
        return false; // No backslash found
    }

    // Find second-to-last backslash
    size_t prevBS = (lastBS > 0) ? path.rfind(L'\\', lastBS - 1) : std::wstring::npos;

    // Check for root path cases
    if (prevBS == std::wstring::npos)
    {
        // "C:\somedir" case - cut to "C:\"
        if (cutDir)
            *cutDir = path.substr(lastBS + 1, len - lastBS - 1);
        path.resize(lastBS + 1); // Keep the backslash: "C:\"
        return true;
    }

    // Check for UNC root "\\server\share"
    if (path.length() >= 2 && path[0] == L'\\' && path[1] == L'\\' && prevBS <= 2)
    {
        if (cutDir)
            cutDir->clear();
        return false; // Cannot shorten UNC root
    }

    // Normal case: "C:\dir1\dir2" -> "C:\dir1"
    if (cutDir)
        *cutDir = path.substr(lastBS + 1, len - lastBS - 1);
    path.resize(lastBS);
    return true;
}
