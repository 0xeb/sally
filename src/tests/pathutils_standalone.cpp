// SPDX-FileCopyrightText: 2026 Open Salamander Authors
// SPDX-License-Identifier: GPL-2.0-or-later
// CommentsTranslationProject: TRANSLATED

//
// pathutils standalone implementation for tests (no precomp.h dependency)
//
// Extracted from salamdr5.cpp, fileswn8.cpp, salamdr2.cpp
//

#include <windows.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <wctype.h>

// Stub out call-stack macros used by the original code
#ifndef CALL_STACK_MESSAGE_NONE
#define CALL_STACK_MESSAGE_NONE
#endif

// --- MakeCopyWithBackslashIfNeededW (from salamdr5.cpp) ---

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

// --- NameEndsWithBackslashW (from salamdr5.cpp) ---

BOOL NameEndsWithBackslashW(const wchar_t* name)
{
    if (name == nullptr || *name == L'\0')
        return FALSE;
    size_t len = wcslen(name);
    return len > 0 && name[len - 1] == L'\\';
}

// --- PathContainsValidComponentsW (from fileswn8.cpp) ---

BOOL PathContainsValidComponentsW(const wchar_t* path)
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

// --- AlterFileNameW (from salamdr2.cpp) ---

std::wstring AlterFileNameW(const wchar_t* filename, int format, int change, bool isDir)
{
    CALL_STACK_MESSAGE_NONE

    if (format == 6)
        format = isDir ? 3 : 2; // VC display style
    if (format == 7 && change != 0)
        format = (change == 1) ? 1 : 2; // convert to mixed/lower case

    std::wstring result;
    std::wstring input = filename;

    // Find extension (last dot)
    size_t extPos = std::wstring::npos;
    if (change != 0 && format != 5 && format != 7)
    {
        extPos = input.rfind(L'.');
        if (extPos != std::wstring::npos)
        {
            if (change == 1) // change only name
            {
                // Process name part only, then append original extension
                std::wstring namePart = input.substr(0, extPos);
                std::wstring extPart = input.substr(extPos);

                switch (format)
                {
                case 1: // capitalize
                {
                    bool capital = true;
                    for (wchar_t c : namePart)
                    {
                        if (!capital)
                        {
                            result += (wchar_t)towlower(c);
                            if (c == L' ' || c == L'.')
                                capital = true;
                        }
                        else
                        {
                            result += (wchar_t)towupper(c);
                            if (c != L' ' && c != L'.')
                                capital = false;
                        }
                    }
                    break;
                }
                case 2: // lowercase
                    for (wchar_t c : namePart)
                        result += (wchar_t)towlower(c);
                    break;
                case 3: // uppercase
                    for (wchar_t c : namePart)
                        result += (wchar_t)towupper(c);
                    break;
                default:
                    result = namePart;
                    break;
                }
                result += extPart;
                return result;
            }
            else // change == 2, change only extension
            {
                // Copy name+dot, then process extension
                result = input.substr(0, extPos + 1);
                std::wstring extPart = input.substr(extPos + 1);

                switch (format)
                {
                case 1: // capitalize
                {
                    bool capital = true;
                    for (wchar_t c : extPart)
                    {
                        if (!capital)
                        {
                            result += (wchar_t)towlower(c);
                            if (c == L' ' || c == L'.')
                                capital = true;
                        }
                        else
                        {
                            result += (wchar_t)towupper(c);
                            if (c != L' ' && c != L'.')
                                capital = false;
                        }
                    }
                    break;
                }
                case 2: // lowercase
                    for (wchar_t c : extPart)
                        result += (wchar_t)towlower(c);
                    break;
                case 3: // uppercase
                    for (wchar_t c : extPart)
                        result += (wchar_t)towupper(c);
                    break;
                default:
                    result += extPart;
                    break;
                }
                return result;
            }
        }
        else if (change == 2) // no extension, nothing to change
        {
            return input;
        }
    }

    // Process entire filename
    switch (format)
    {
    case 5: // explorer style
    {
        // Check if it's 8.3 uppercase format
        const wchar_t* s = filename;
        int c = 8;
        while (c-- && *s != 0 && *s == towupper(*s) && *s != L'.')
            s++;
        if (*s == L'.')
            s++;
        else if (*s != 0)
            return input; // not 8.3 format, return as-is
        c = 3;
        while (c-- && *s != 0 && *s != L'.' && *s == towupper(*s))
            s++;
        if (*s != 0)
            return input; // not 8.3 format, return as-is

        // Convert to proper case
        bool capital = true;
        for (wchar_t ch : input)
        {
            if (!capital)
            {
                result += (wchar_t)towlower(ch);
                if (ch == L' ')
                    capital = true;
            }
            else
            {
                result += (wchar_t)towupper(ch);
                if (ch != L' ')
                    capital = false;
            }
        }
        break;
    }

    case 1: // capitalize
    {
        bool capital = true;
        for (wchar_t c : input)
        {
            if (!capital)
            {
                result += (wchar_t)towlower(c);
                if (c == L' ' || c == L'.')
                    capital = true;
            }
            else
            {
                result += (wchar_t)towupper(c);
                if (c != L' ' && c != L'.')
                    capital = false;
            }
        }
        break;
    }

    case 2: // lowercase
        for (wchar_t c : input)
            result += (wchar_t)towlower(c);
        break;

    case 3: // uppercase
        for (wchar_t c : input)
            result += (wchar_t)towupper(c);
        break;

    case 7: // name mixed case, extension lowercase
    {
        extPos = input.rfind(L'.');
        size_t nameEnd = (extPos != std::wstring::npos) ? extPos : input.length();

        // Name: mixed case
        bool capital = true;
        for (size_t i = 0; i < nameEnd; i++)
        {
            wchar_t c = input[i];
            if (!capital)
            {
                result += (wchar_t)towlower(c);
                if (c == L' ')
                    capital = true;
            }
            else
            {
                result += (wchar_t)towupper(c);
                if (c != L' ')
                    capital = false;
            }
        }
        // Extension: lowercase
        for (size_t i = nameEnd; i < input.length(); i++)
            result += (wchar_t)towlower(input[i]);
        break;
    }

    default:
        return input;
    }

    return result;
}
