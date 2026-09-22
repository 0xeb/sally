// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <cstddef>
#include <cwchar>
#include <string>
#include <utility>
#include <vector>

namespace sally::unicode
{

// Decodes OPENFILENAMEW's result buffer. A single selection is one full path;
// multiple selections are a directory followed by file names and a second NUL.
inline std::vector<std::wstring> DecodeOpenFileSelection(const wchar_t* buffer,
                                                         size_t capacity)
{
    std::vector<std::wstring> paths;
    if (buffer == nullptr || capacity == 0)
        return paths;

    const size_t firstLength = wcsnlen_s(buffer, capacity);
    if (firstLength == 0 || firstLength >= capacity)
        return paths;

    if (firstLength + 1 >= capacity || buffer[firstLength + 1] == L'\0')
    {
        paths.emplace_back(buffer, firstLength);
        return paths;
    }

    const std::wstring directory(buffer, firstLength);
    size_t offset = firstLength + 1;
    while (offset < capacity && buffer[offset] != L'\0')
    {
        const size_t length = wcsnlen_s(buffer + offset, capacity - offset);
        if (length >= capacity - offset)
            return {};

        std::wstring path = directory;
        if (!path.empty() && path.back() != L'\\' && path.back() != L'/')
            path += L'\\';
        path.append(buffer + offset, length);
        paths.push_back(std::move(path));
        offset += length + 1;
    }

    if (offset >= capacity)
        return {};
    return paths;
}

} // namespace sally::unicode
