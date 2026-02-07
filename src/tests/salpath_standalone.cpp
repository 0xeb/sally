// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-License-Identifier: GPL-2.0-or-later
// CommentsTranslationProject: TRANSLATED

//****************************************************************************
//
// Standalone extraction of wide path helpers for testing
// (no precomp.h dependency)
//
// Functions extracted from salamdr3.cpp and salamdr1.cpp
//
//****************************************************************************

#include <windows.h>
#include <string>
#include <algorithm>

#include "../common/widepath.h"

//****************************************************************************
// From salamdr3.cpp: SalPath*W functions
//****************************************************************************

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

// Wide version - ensures path ends with backslash
void SalPathAddBackslashW(std::wstring& path)
{
    if (!path.empty() && path.back() != L'\\')
        path += L'\\';
}

// Wide version - removes trailing backslash
void SalPathRemoveBackslashW(std::wstring& path)
{
    if (!path.empty() && path.back() == L'\\')
        path.pop_back();
}

// Wide version - strips path leaving just filename
// "C:\foo\bar.txt" -> "bar.txt", "bar.txt" -> "bar.txt"
void SalPathStripPathW(std::wstring& path)
{
    size_t pos = path.rfind(L'\\');
    if (pos != std::wstring::npos)
        path = path.substr(pos + 1);
}

// Wide version - finds filename portion of path
// Returns pointer within the string to the filename part
const wchar_t* SalPathFindFileNameW(const wchar_t* path)
{
    if (path == nullptr)
        return nullptr;

    const wchar_t* result = path;
    for (const wchar_t* p = path; *p != L'\0'; p++)
    {
        if (*p == L'\\')
            result = p + 1;
    }
    return result;
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

// Wide version - adds extension if not already present
// Returns true if extension was added or already exists
bool SalPathAddExtensionW(std::wstring& path, const wchar_t* extension)
{
    if (extension == nullptr)
        return false;

    size_t len = path.length();
    for (size_t i = len; i > 0; i--)
    {
        if (path[i - 1] == L'.')
            return true; // Extension already exists
        if (path[i - 1] == L'\\')
            break; // No extension, add it
    }
    path += extension;
    return true;
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

//****************************************************************************
// From salamdr1.cpp: CutDirectoryW
//****************************************************************************

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

//****************************************************************************
// CWidePathBuffer implementation (from common/widepath.cpp)
//****************************************************************************

CWidePathBuffer::CWidePathBuffer()
    : m_buffer(NULL)
{
    m_buffer = (wchar_t*)malloc(SAL_MAX_LONG_PATH * sizeof(wchar_t));
    if (m_buffer != NULL)
    {
        m_buffer[0] = L'\0';
    }
}

CWidePathBuffer::CWidePathBuffer(const wchar_t* initialPath)
    : m_buffer(NULL)
{
    m_buffer = (wchar_t*)malloc(SAL_MAX_LONG_PATH * sizeof(wchar_t));
    if (m_buffer != NULL)
    {
        if (initialPath != NULL)
        {
            wcsncpy(m_buffer, initialPath, SAL_MAX_LONG_PATH - 1);
            m_buffer[SAL_MAX_LONG_PATH - 1] = L'\0';
        }
        else
        {
            m_buffer[0] = L'\0';
        }
    }
}

CWidePathBuffer::~CWidePathBuffer()
{
    if (m_buffer != NULL)
    {
        free(m_buffer);
    }
}

BOOL CWidePathBuffer::Append(const wchar_t* name)
{
    if (m_buffer == NULL || name == NULL)
        return FALSE;

    size_t currentLen = wcslen(m_buffer);
    size_t nameLen = wcslen(name);

    // Add backslash if path doesn't end with one and isn't empty
    BOOL needsBackslash = (currentLen > 0 && m_buffer[currentLen - 1] != L'\\');
    size_t totalLen = currentLen + (needsBackslash ? 1 : 0) + nameLen;

    if (totalLen >= SAL_MAX_LONG_PATH)
        return FALSE; // Would overflow

    if (needsBackslash)
    {
        m_buffer[currentLen] = L'\\';
        currentLen++;
    }

    wcscpy(m_buffer + currentLen, name);
    return TRUE;
}

BOOL CWidePathBuffer::Append(const char* name)
{
    if (m_buffer == NULL || name == NULL)
        return FALSE;

    // Convert ANSI to wide
    int wideLen = MultiByteToWideChar(CP_ACP, 0, name, -1, NULL, 0);
    if (wideLen <= 0)
        return FALSE;

    // Allocate temp buffer for converted name
    wchar_t* wideName = (wchar_t*)malloc(wideLen * sizeof(wchar_t));
    if (wideName == NULL)
        return FALSE;

    if (MultiByteToWideChar(CP_ACP, 0, name, -1, wideName, wideLen) == 0)
    {
        free(wideName);
        return FALSE;
    }

    // Use the wide version of Append
    BOOL result = Append(wideName);
    free(wideName);
    return result;
}
