// SPDX-FileCopyrightText: 2025-2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <windows.h>

#include <new>
#include <stdexcept>
#include <string>

namespace sally::path
{
inline bool BuildModuleRelativePath(const wchar_t* modulePath,
                                    const wchar_t* relativePath,
                                    std::wstring& result)
{
    if (modulePath == nullptr || modulePath[0] == 0 || relativePath == nullptr)
        return false;

    try
    {
        std::wstring path(modulePath);
        const size_t slash = path.find_last_of(L"\\/");
        if (slash == std::wstring::npos)
            return false;
        path.resize(slash + 1);
        while (*relativePath == L'\\' || *relativePath == L'/')
            ++relativePath;
        path.append(relativePath);
        result.swap(path);
        return true;
    }
    catch (const std::bad_alloc&)
    {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return false;
    }
    catch (const std::length_error&)
    {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return false;
    }
}
} // namespace sally::path
