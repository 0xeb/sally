// SPDX-FileCopyrightText: 2025-2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <windows.h>

#include <algorithm>
#include <cstdlib>
#include <cwchar>
#include <cstring>
#include <limits>
#include <new>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "WideTextRange.h"

namespace sally::unicode
{

struct WideVarEntry
{
    const wchar_t* Name;
    std::wstring (*Execute)(void* param);
};

enum class WideVarResolveResult
{
    Found,
    NotFound,
    Failed,
};

enum class WideEnvironmentReadResult
{
    Found,
    NotFound,
    Failed,
};

inline WideEnvironmentReadResult ReadWideEnvironmentVariable(
    const wchar_t* name, std::wstring& value)
{
    if (name == nullptr)
        return WideEnvironmentReadResult::Failed;
    try
    {
        SetLastError(ERROR_SUCCESS);
        DWORD required = GetEnvironmentVariableW(name, nullptr, 0);
        if (required == 0)
        {
            const DWORD error = GetLastError();
            if (error == ERROR_ENVVAR_NOT_FOUND)
                return WideEnvironmentReadResult::NotFound;
            if (error == ERROR_SUCCESS)
            {
                value.clear();
                return WideEnvironmentReadResult::Found;
            }
            return WideEnvironmentReadResult::Failed;
        }

        std::vector<wchar_t> buffer(required, L'\0');
        for (;;)
        {
            SetLastError(ERROR_SUCCESS);
            const DWORD written = GetEnvironmentVariableW(
                name, buffer.data(), static_cast<DWORD>(buffer.size()));
            if (written == 0)
            {
                const DWORD error = GetLastError();
                if (error == ERROR_ENVVAR_NOT_FOUND)
                    return WideEnvironmentReadResult::NotFound;
                if (error == ERROR_SUCCESS)
                {
                    value.clear();
                    return WideEnvironmentReadResult::Found;
                }
                return WideEnvironmentReadResult::Failed;
            }
            if (written < buffer.size())
            {
                value.assign(buffer.data(), written);
                return WideEnvironmentReadResult::Found;
            }
            if (written == (std::numeric_limits<DWORD>::max)())
                return WideEnvironmentReadResult::Failed;
            buffer.resize(static_cast<std::size_t>(written) + 1, L'\0');
        }
    }
    catch (const std::bad_alloc&)
    {
        return WideEnvironmentReadResult::Failed;
    }
    catch (const std::length_error&)
    {
        return WideEnvironmentReadResult::Failed;
    }
}

enum class WideVarErrorKind
{
    None,
    InvalidArguments,
    UnmatchedParenthesis,
    InvalidVariableWidth,
    VariableNotFound,
    VariableCallbackFailed,
    UnmatchedBracket,
    EnvironmentNotFound,
    EnvironmentTooLarge,
    UnexpectedCharacter,
    TrailingDollar,
    OutputTooSmall,
};

struct WideVarError
{
    WideVarErrorKind Kind = WideVarErrorKind::None;
    int Position1 = 0;
    int Position2 = 0;
    std::wstring Argument;
};

inline void SetWideVarError(WideVarError* error, WideVarErrorKind kind,
                            int position1, int position2,
                            const std::wstring& argument = std::wstring())
{
    if (error == nullptr)
        return;
    error->Kind = kind;
    error->Position1 = position1;
    error->Position2 = position2;
    error->Argument = argument;
}

inline bool SegmentEqualsNoCase(const wchar_t* segment, int segmentLen,
                                const wchar_t* name)
{
    if (segment == nullptr || name == nullptr || segmentLen < 0 ||
        static_cast<std::size_t>(segmentLen) != std::wcslen(name))
        return false;
    for (int index = 0; index < segmentLen; ++index)
    {
        wchar_t left = segment[index];
        wchar_t right = name[index];
        if (left >= L'A' && left <= L'Z')
            left = left - L'A' + L'a';
        if (right >= L'A' && right <= L'Z')
            right = right - L'A' + L'a';
        if (left != right)
            return false;
    }
    return true;
}

inline const WideVarEntry* FindWideVarEntry(const WideVarEntry* entries,
                                            const wchar_t* name, int nameLen)
{
    if (entries == nullptr)
        return nullptr;
    for (const WideVarEntry* entry = entries; entry->Name != nullptr; ++entry)
    {
        if (SegmentEqualsNoCase(name, nameLen, entry->Name))
            return entry;
    }
    return nullptr;
}

// One UTF-16 grammar walker serves native core values, the live SDK, and the isolated
// frozen-v107 adapter. The resolver applies its contract's :num/:max width semantics and
// returns UTF-16 text plus the unformatted measurement width used by a :max pass.
template <class ResolveVariable, class HandleEnvironmentError>
inline bool ExpandWideVarStringCore(const wchar_t* varText, bool validateOnly,
                                    ResolveVariable&& resolveVariable,
                                    std::wstring* output,
                                    std::vector<WideTextRange>* varPlacements,
                                    bool detectMaxVarWidths,
                                    int* maxVarWidths, int maxVarWidthsCount,
                                    std::size_t outputCapacity, WideVarError* error,
                                    HandleEnvironmentError&& handleEnvironmentError)
{
    if (error != nullptr)
        *error = {};
    if (varText == nullptr ||
        (maxVarWidthsCount > 0 && maxVarWidths == nullptr))
    {
        SetWideVarError(error, WideVarErrorKind::InvalidArguments, 0, 0);
        return false;
    }

    const wchar_t* s = varText;
    int currentMaxVarIndex = 0;
    std::wstring result;
    std::vector<WideTextRange> resultPlacements;

    const auto canAppend = [&](std::size_t count) {
        if (output == nullptr || outputCapacity == (std::numeric_limits<std::size_t>::max)())
            return true;
        return count < outputCapacity && result.size() <= outputCapacity - count - 1;
    };

    while (*s != 0)
    {
        if (*s != L'$')
        {
            if (!validateOnly && output != nullptr)
            {
                if (!canAppend(1))
                {
                    SetWideVarError(error, WideVarErrorKind::OutputTooSmall,
                                    static_cast<int>(s - varText),
                                    static_cast<int>(s - varText + 1));
                    return false;
                }
                result.push_back(*s);
            }
            ++s;
            continue;
        }

        ++s;
        if (*s == 0)
        {
            SetWideVarError(error, WideVarErrorKind::TrailingDollar,
                            static_cast<int>(s - varText - 1),
                            static_cast<int>(s - varText));
            return false;
        }

        std::wstring value;
        bool detectMax = false;
        int requestedWidth = 0;
        int measurementWidth = 0;

        if (*s == L'$')
        {
            value = L"$";
            ++s;
        }
        else if (*s == L'(')
        {
            const wchar_t* var = s + 1;
            while (*s != L')' && *s != 0)
                ++s;
            if (*s == 0)
            {
                SetWideVarError(error, WideVarErrorKind::UnmatchedParenthesis,
                                static_cast<int>(var - varText - 2),
                                static_cast<int>(s - varText));
                return false;
            }

            int varLen = (int)(s - var);
            const wchar_t* s2 = var;
            while (s2 < s)
            {
                if (*s2 == L':' && s2 + 1 < s && s2[1] == L':')
                {
                    s2 += 2;
                    continue;
                }
                if (*s2 == L':')
                {
                    const wchar_t* width = s2 + 1;
                    int tmpLen = static_cast<int>(s - width);
                    bool validMax = tmpLen == 3 &&
                                    (width[0] == L'm' || width[0] == L'M') &&
                                    (width[1] == L'a' || width[1] == L'A') &&
                                    (width[2] == L'x' || width[2] == L'X');
                    detectMax = validMax && detectMaxVarWidths;
                    bool validNum = false;
                    if (!validMax && tmpLen > 0 && tmpLen <= 4)
                    {
                        const wchar_t* digit = width;
                        int parsedWidth = 0;
                        while (digit < s && *digit >= L'0' && *digit <= L'9')
                        {
                            parsedWidth = parsedWidth * 10 + (*digit - L'0');
                            ++digit;
                        }
                        if (digit == s)
                        {
                            requestedWidth = parsedWidth;
                            validNum = requestedWidth >= 1;
                        }
                    }
                    if (!validMax && !validNum)
                    {
                        SetWideVarError(error, WideVarErrorKind::InvalidVariableWidth,
                                        static_cast<int>(width - varText),
                                        static_cast<int>(s - varText));
                        return false;
                    }
                    if (!validateOnly && validMax && !detectMax)
                    {
                        if (currentMaxVarIndex < maxVarWidthsCount)
                            requestedWidth = maxVarWidths[currentMaxVarIndex++];
                    }
                    varLen -= static_cast<int>(s - width) + 1;
                    break;
                }
                ++s2;
            }

            const WideVarResolveResult resolved = resolveVariable(
                var, varLen, !validateOnly, requestedWidth, value,
                measurementWidth);
            if (resolved == WideVarResolveResult::NotFound)
            {
                SetWideVarError(error, WideVarErrorKind::VariableNotFound,
                                static_cast<int>(var - varText - 2),
                                static_cast<int>(s - varText + 1),
                                std::wstring(var, static_cast<std::size_t>(varLen > 0 ? varLen : 0)));
                return false;
            }
            if (resolved == WideVarResolveResult::Failed)
            {
                SetWideVarError(error, WideVarErrorKind::VariableCallbackFailed,
                                static_cast<int>(var - varText - 2),
                                static_cast<int>(s - varText + 1));
                return false;
            }
            ++s;
        }
        else if (*s == L'[')
        {
            const wchar_t* var = s + 1;
            while (*s != L']' && *s != 0)
                ++s;
            if (*s == 0)
            {
                SetWideVarError(error, WideVarErrorKind::UnmatchedBracket,
                                static_cast<int>(var - varText - 2),
                                static_cast<int>(s - varText));
                return false;
            }

            if (!validateOnly)
            {
                const std::wstring envName(var, static_cast<std::size_t>(s - var));
                WideVarErrorKind environmentError = WideVarErrorKind::None;
                const WideEnvironmentReadResult environmentResult =
                    ReadWideEnvironmentVariable(envName.c_str(), value);
                if (environmentResult == WideEnvironmentReadResult::NotFound)
                    environmentError = WideVarErrorKind::EnvironmentNotFound;
                else if (environmentResult == WideEnvironmentReadResult::Failed)
                    environmentError = WideVarErrorKind::EnvironmentTooLarge;
                if (environmentError != WideVarErrorKind::None)
                {
                    WideVarError environment = {};
                    SetWideVarError(&environment, environmentError,
                                    static_cast<int>(var - varText),
                                    static_cast<int>(s - varText), envName);
                    if (!handleEnvironmentError(environment))
                    {
                        if (error != nullptr)
                            *error = environment;
                        return false;
                    }
                    value.clear();
                }
            }
            ++s;
        }
        else
        {
            SetWideVarError(error, WideVarErrorKind::UnexpectedCharacter,
                            static_cast<int>(s - varText),
                            static_cast<int>(s - varText + 1));
            return false;
        }

        if (!validateOnly && detectMax)
        {
            if (currentMaxVarIndex < maxVarWidthsCount)
            {
                if (maxVarWidths[currentMaxVarIndex] < measurementWidth)
                    maxVarWidths[currentMaxVarIndex] = measurementWidth;
                ++currentMaxVarIndex;
            }
        }

        if (!validateOnly && output != nullptr)
        {
            if (!canAppend(value.size()))
            {
                SetWideVarError(error, WideVarErrorKind::OutputTooSmall,
                                static_cast<int>(s - varText),
                                static_cast<int>(s - varText));
                return false;
            }
            if (varPlacements != nullptr)
                resultPlacements.push_back({result.length(), value.length()});
            result.append(value);
        }
    }

    if (!validateOnly && varPlacements != nullptr)
        *varPlacements = std::move(resultPlacements);
    if (!validateOnly && output != nullptr)
        *output = result;
    return true;
}

inline bool ExpandWideVarString(const wchar_t* varText,
                                const WideVarEntry* variables, void* param,
                                std::wstring* output,
                                std::vector<WideTextRange>* varPlacements = nullptr,
                                bool detectMaxVarWidths = false,
                                int* maxVarWidths = nullptr,
                                int maxVarWidthsCount = 0,
                                std::size_t outputCapacity =
                                    (std::numeric_limits<std::size_t>::max)(),
                                WideVarError* error = nullptr)
{
    const auto resolve = [&](const wchar_t* name, int nameLength, bool execute,
                             int requestedWidth, std::wstring& value,
                             int& measurementWidth) {
        const WideVarEntry* entry = FindWideVarEntry(variables, name, nameLength);
        if (entry == nullptr)
            return WideVarResolveResult::NotFound;
        if (!execute)
            return WideVarResolveResult::Found;
        if (entry->Execute == nullptr)
            return WideVarResolveResult::Failed;

        value = entry->Execute(param);
        measurementWidth = static_cast<int>(value.size());
        if (requestedWidth > 0)
        {
            if (value.size() > static_cast<std::size_t>(requestedWidth))
                value.resize(static_cast<std::size_t>(requestedWidth));
            else if (value.size() < static_cast<std::size_t>(requestedWidth))
                value.append(static_cast<std::size_t>(requestedWidth) - value.size(), L' ');
        }
        return WideVarResolveResult::Found;
    };
    const auto ignoreEnvironmentError = [](const WideVarError&) { return true; };
    return ExpandWideVarStringCore(
        varText, false, resolve, output, varPlacements,
        detectMaxVarWidths, maxVarWidths, maxVarWidthsCount, outputCapacity,
        error, ignoreEnvironmentError);
}

inline std::wstring BuildCommandLineDirectoryPrefixW(const std::wstring& dir)
{
    std::wstring result = dir;
    result.push_back(L'>');
    return result;
}

inline size_t ExtensionOffsetAfterDotW(const std::wstring& name, bool isDir)
{
    if (isDir || name.empty())
        return std::wstring::npos;
    size_t dot = name.find_last_of(L'.');
    if (dot == std::wstring::npos)
        return name.length();
    return dot + 1;
}

inline std::wstring FileNamePartFromExtensionOffsetW(const std::wstring& formattedName, size_t extOffset)
{
    if (extOffset != std::wstring::npos && extOffset > 0 && extOffset <= formattedName.length())
        return formattedName.substr(0, extOffset - 1);
    return formattedName;
}

inline std::wstring FileExtensionFromExtensionOffsetW(const std::wstring& formattedName, size_t extOffset)
{
    if (extOffset != std::wstring::npos && extOffset < formattedName.length())
        return formattedName.substr(extOffset);
    return std::wstring();
}

} // namespace sally::unicode
