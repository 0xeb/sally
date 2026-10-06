// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

// Plugins write CALL_STACK_MESSAGE formats as narrow (ACP) printf strings; the call stack
// records them through the wide formatter. This converts such a format once, keeping its
// va_list meaning.

#pragma once

#include <windows.h>

#include <cstring>
#include <string>

namespace sally::debug
{

inline bool IsNarrowPrintfConversion(char value)
{
    return std::strchr("diouxXfFeEgGaAcCsSpnZ%", value) != nullptr;
}

inline bool WidenNarrowPrintfText(const char* text, int length,
                                  std::wstring& result)
{
    result.clear();
    if (text == nullptr || length < 0)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return false;
    }
    if (length == 0)
        return true;
    const DWORD flags = 0;
    const int needed = MultiByteToWideChar(CP_ACP, flags, text, length,
                                           nullptr, 0);
    if (needed <= 0)
    {
        SetLastError(ERROR_NO_UNICODE_TRANSLATION);
        return false;
    }
    try
    {
        result.resize(static_cast<std::size_t>(needed));
    }
    catch (const std::bad_alloc&)
    {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return false;
    }
    catch (const std::length_error&)
    {
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return false;
    }
    if (MultiByteToWideChar(CP_ACP, flags, text, length, result.data(),
                            needed) != needed)
    {
        result.clear();
        SetLastError(ERROR_NO_UNICODE_TRANSLATION);
        return false;
    }
    return true;
}

// Translate a narrow printf format for a wide formatter while preserving
// its va_list. Microsoft wide printf reverses the implicit character/string
// widths, so only unmodified c/C/s/S conversions change case. Explicit h/l/w
// modifiers retain their original meaning.
inline bool WidenNarrowPrintfFormat(const char* format, std::wstring& result)
{
    result.clear();
    if (format == nullptr)
        return true;

    try
    {
        const std::string source(format);
        if (source.size() > static_cast<std::size_t>(INT_MAX))
        {
            SetLastError(ERROR_INSUFFICIENT_BUFFER);
            return false;
        }
        std::wstring staged;
        std::size_t literalStart = 0;
        std::size_t i = 0;
        while (i < source.size())
        {
            if (source[i] != '%')
            {
                i++;
                continue;
            }

            if (i > literalStart)
            {
                std::wstring literal;
                if (!WidenNarrowPrintfText(
                        source.data() + literalStart,
                        static_cast<int>(i - literalStart), literal))
                    return false;
                staged += literal;
            }

            std::size_t conversion = i + 1;
            while (conversion < source.size() &&
                   !IsNarrowPrintfConversion(source[conversion]))
                conversion++;

            if (conversion == source.size())
            {
                std::wstring literal;
                if (!WidenNarrowPrintfText(
                        source.data() + i,
                        static_cast<int>(source.size() - i), literal))
                    return false;
                staged += literal;
                result.swap(staged);
                return true;
            }

            bool explicitCharacterWidth = false;
            for (std::size_t modifier = i + 1; modifier < conversion;
                 modifier++)
            {
                if (source[modifier] == 'h' || source[modifier] == 'l' ||
                    source[modifier] == 'w')
                    explicitCharacterWidth = true;
            }

            for (std::size_t part = i; part < conversion; part++)
                staged.push_back(static_cast<unsigned char>(source[part]));

            char type = source[conversion];
            if (!explicitCharacterWidth)
            {
                if (type == 's')
                    type = 'S';
                else if (type == 'S')
                    type = 's';
                else if (type == 'c')
                    type = 'C';
                else if (type == 'C')
                    type = 'c';
            }
            staged.push_back(static_cast<unsigned char>(type));
            i = conversion + 1;
            literalStart = i;
        }

        if (literalStart < source.size())
        {
            std::wstring literal;
            if (!WidenNarrowPrintfText(
                    source.data() + literalStart,
                    static_cast<int>(source.size() - literalStart), literal))
                return false;
            staged += literal;
        }
        result.swap(staged);
        return true;
    }
    catch (const std::bad_alloc&)
    {
        result.clear();
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return false;
    }
    catch (const std::length_error&)
    {
        result.clear();
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return false;
    }
}

inline std::wstring WidenNarrowPrintfFormat(const char* format)
{
    std::wstring result;
    WidenNarrowPrintfFormat(format, result);
    return result;
}

} // namespace sally::debug
