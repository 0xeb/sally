// SPDX-FileCopyrightText: 2025-2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "ModuleRelativePath.h"

#include <windows.h>
#include <functional>
#include <limits>
#include <new>
#include <stdexcept>
#include <string>

namespace sally::tip_of_day
{
inline constexpr const wchar_t* kTipsRelativePathW = L"help\\tips.txt";

inline bool BuildModuleRelativePathFromModulePathW(const wchar_t* modulePath,
                                                   const wchar_t* relativePath,
                                                   std::wstring& result)
{
    return sally::path::BuildModuleRelativePath(modulePath, relativePath, result);
}

inline bool BuildModuleRelativePathW(HINSTANCE module, const wchar_t* relativePath, std::wstring& result)
{
    DWORD capacity = 256;
    for (;;)
    {
        try
        {
            std::wstring modulePath(capacity, L'\0');
            SetLastError(ERROR_SUCCESS);
            const DWORD length = GetModuleFileNameW(module, modulePath.data(), capacity);
            if (length == 0)
                return false;
            const DWORD error = GetLastError();
            const bool truncated = length >= capacity ||
                                   (length == capacity - 1 && error == ERROR_INSUFFICIENT_BUFFER);
            if (!truncated)
            {
                modulePath.resize(length);
                return BuildModuleRelativePathFromModulePathW(modulePath.c_str(), relativePath, result);
            }
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
        if (capacity > (std::numeric_limits<DWORD>::max)() / 2)
        {
            SetLastError(ERROR_NOT_ENOUGH_MEMORY);
            return false;
        }
        capacity *= 2;
    }
}

using TipsFileOpener = std::function<HANDLE(const wchar_t*)>;

inline HANDLE OpenTipsFileForReadAtPathW(const wchar_t* fileName, const TipsFileOpener& openFile)
{
    return openFile(fileName);
}

inline HANDLE OpenTipsFileForReadW(HINSTANCE module, std::wstring& fileName,
                                   const TipsFileOpener& openFile)
{
    if (!BuildModuleRelativePathW(module, kTipsRelativePathW, fileName))
    {
        fileName = kTipsRelativePathW;
        SetLastError(ERROR_PATH_NOT_FOUND);
        return INVALID_HANDLE_VALUE;
    }
    return OpenTipsFileForReadAtPathW(fileName.c_str(), openFile);
}
} // namespace sally::tip_of_day
