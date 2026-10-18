// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

// Wide display/validation name helpers. Extracted from sally_path_validation.cpp,
// files_window_delete_email.cpp, and sally_strings_waitwindow.cpp so production
// and the private tests compile the same translation unit
//.

#ifdef SALLY_WORKER_CORE_STANDALONE
#include "common/WorkerCoreStandalone.h"
#else
#include "precomp.h"
#endif

#include <string>
#include <limits>

#include "common/PathDisplayUtils.h"

// Wide version - returns path with trailing backslash added if needed
// (to prevent Windows from trimming trailing spaces/dots)
std::wstring MakeCopyWithBackslashIfNeededW(const wchar_t* name)
{
    if (name == nullptr || *name == L'\0')
        return std::wstring();

    std::wstring result(name);
    size_t len = result.length();

    // If name ends with space or dot, append backslash
    if (len > 0 && (result[len - 1] <= L' ' || result[len - 1] == L'.'))
    {
        result += L'\\';
    }

    return result;
}

// Wide version of NameEndsWithBackslash
BOOL NameEndsWithBackslashW(const wchar_t* name)
{
    if (name == nullptr || *name == L'\0')
        return FALSE;
    size_t len = wcslen(name);
    return len > 0 && name[len - 1] == L'\\';
}

// Wide version - checks if path contains components ending with space or dot
// Returns FALSE if invalid component found (and optionally truncates at that point)
BOOL PathContainsValidComponents(const wchar_t* path)
{
    const wchar_t* s = path;
    while (*s != 0)
    {
        const wchar_t* slash = wcschr(s, L'\\');
        if (slash == NULL)
            s += wcslen(s);
        else
            s = slash;
        if (s > path && (*(s - 1) <= L' ' || *(s - 1) == L'.'))
        {
            return FALSE;
        }
        if (slash != NULL)
            s++;
    }
    return TRUE;
}

namespace
{
// Map complete UTF-16 strings, independent of the CRT/process locale. Windows
// owns the Unicode tables; query output length before allocating writable space.
std::wstring MapDisplayCase(const std::wstring& input, DWORD mode)
{
    if (input.empty() || input.size() > static_cast<size_t>((std::numeric_limits<int>::max)()))
        return input;
    const int length = static_cast<int>(input.size());
    const int needed = LCMapStringW(LOCALE_INVARIANT, mode, input.data(), length, nullptr, 0);
    if (needed <= 0)
        return input;
    std::wstring result(static_cast<size_t>(needed), L'\0');
    if (LCMapStringW(LOCALE_INVARIANT, mode, input.data(), length, result.data(), needed) != needed)
        return input;
    return result;
}

std::wstring CapitalizeDisplayWords(const std::wstring& input, bool dotSeparates)
{
    std::wstring result;
    size_t position = 0;
    while (position < input.size())
    {
        if (input[position] == L' ' || (dotSeparates && input[position] == L'.'))
        {
            result += input[position++];
            continue;
        }
        const size_t end = input.find_first_of(dotSeparates ? L" ." : L" ", position);
        const size_t count = (end == std::wstring::npos ? input.size() : end) - position;
        std::wstring word = MapDisplayCase(input.substr(position, count), LCMAP_LOWERCASE);
        size_t first = 1;
        if (word.size() > 1 && word[0] >= 0xd800 && word[0] <= 0xdbff &&
            word[1] >= 0xdc00 && word[1] <= 0xdfff)
            first = 2;
        result += MapDisplayCase(word.substr(0, first), LCMAP_UPPERCASE);
        result += word.substr(first);
        position += count;
    }
    return result;
}

std::wstring FormatDisplayPart(const std::wstring& input, int format)
{
    if (format == 1)
        return CapitalizeDisplayWords(input, true);
    if (format == 2 || format == 3)
        return MapDisplayCase(input, format == 2 ? LCMAP_LOWERCASE : LCMAP_UPPERCASE);
    return input;
}
} // namespace

// Formats display text and explicit change-case operation names. The caller
// decides whether to display the result or use it in a requested file operation.
std::wstring AlterFileNameW(const wchar_t* filename, int format, int change, bool isDir)
{
    CALL_STACK_MESSAGE_NONE
    const std::wstring input = filename != nullptr ? filename : L"";
    if (format == 6)
        format = isDir ? 3 : 2;
    if (format == 7 && change != 0)
        format = change == 1 ? 1 : 2;
    const size_t extension = input.rfind(L'.');
    if (change != 0 && format != 5 && format != 7)
    {
        if (extension != std::wstring::npos)
        {
            if (change == 1)
                return FormatDisplayPart(input.substr(0, extension), format) + input.substr(extension);
            return input.substr(0, extension + 1) + FormatDisplayPart(input.substr(extension + 1), format);
        }
        if (change == 2)
            return input;
    }
    if (format == 5)
    {
        const size_t dot = input.find(L'.');
        const size_t nameLength = dot == std::wstring::npos ? input.size() : dot;
        if (nameLength > 8 || (dot != std::wstring::npos &&
            (input.size() - dot - 1 > 3 || input.find(L'.', dot + 1) != std::wstring::npos)) ||
            MapDisplayCase(input, LCMAP_UPPERCASE) != input)
            return input;
        return CapitalizeDisplayWords(input, false);
    }
    if (format == 7)
    {
        const size_t nameEnd = extension == std::wstring::npos ? input.size() : extension;
        return CapitalizeDisplayWords(input.substr(0, nameEnd), false) +
               MapDisplayCase(input.substr(nameEnd), LCMAP_LOWERCASE);
    }
    return FormatDisplayPart(input, format);
}
