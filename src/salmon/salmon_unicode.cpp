// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "salmon_unicode.h"

#include <limits>
#include <windows.h>

namespace sally::salmon
{

bool ParseCommandLine(std::wstring_view commandLine, std::wstring& mappingName,
                      std::wstring& slgName) noexcept
{
    try
    {
        if (commandLine.empty() || commandLine.front() != L'"')
            return false;
        const size_t mappingEnd = commandLine.find(L'"', 1);
        if (mappingEnd == std::wstring_view::npos || mappingEnd + 2 >= commandLine.size() ||
            commandLine[mappingEnd + 1] != L' ' || commandLine[mappingEnd + 2] != L'"')
            return false;
        const size_t slgBegin = mappingEnd + 3;
        const size_t slgEnd = commandLine.find(L'"', slgBegin);
        if (slgEnd == std::wstring_view::npos)
            return false;

        std::wstring parsedMapping(commandLine.substr(1, mappingEnd - 1));
        std::wstring parsedSlg(commandLine.substr(slgBegin, slgEnd - slgBegin));
        mappingName.swap(parsedMapping);
        slgName.swap(parsedSlg);
        return true;
    }
    catch (...)
    {
        return false;
    }
}

bool ReadFrozenWideString(const wchar_t* value, size_t capacity, std::wstring& output) noexcept
{
    if (value == nullptr || capacity == 0)
        return false;
    const size_t length = wcsnlen_s(value, capacity);
    if (length == capacity)
        return false;
    try
    {
        std::wstring parsed(value, length);
        output.swap(parsed);
        return true;
    }
    catch (...)
    {
        return false;
    }
}

bool EncodeUtf8(std::wstring_view text, std::string& encoded) noexcept
{
    try
    {
        if (text.empty())
        {
            encoded.clear();
            return true;
        }
        if (text.size() > static_cast<size_t>((std::numeric_limits<int>::max)()))
            return false;
        const int sourceChars = static_cast<int>(text.size());
        const int bytes = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), sourceChars,
                                              nullptr, 0, nullptr, nullptr);
        if (bytes <= 0)
            return false;
        std::string result(static_cast<size_t>(bytes), '\0');
        if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), sourceChars,
                                result.data(), bytes, nullptr, nullptr) != bytes)
            return false;
        encoded.swap(result);
        return true;
    }
    catch (...)
    {
        return false;
    }
}

}
