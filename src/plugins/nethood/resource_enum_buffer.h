// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <windows.h>

#include <limits>
#include <vector>

using NethoodResourceBuffer = std::vector<ULONG_PTR>;

template <typename Enumerate>
DWORD NethoodEnumerateResourceBatch(HANDLE enumeration, DWORD& entries,
                                    NethoodResourceBuffer& storage,
                                    NETRESOURCEW*& resources,
                                    Enumerate&& enumerate) noexcept
{
    resources = NULL;
    try
    {
        if (storage.empty())
            storage.resize(4096 / sizeof(ULONG_PTR));

        for (;;)
        {
            entries = static_cast<DWORD>(-1);
            DWORD bytes = static_cast<DWORD>(storage.size() * sizeof(ULONG_PTR));
            const DWORD error = enumerate(enumeration, &entries, storage.data(), &bytes);
            if (error != ERROR_MORE_DATA)
            {
                if (error == NO_ERROR)
                    resources = reinterpret_cast<NETRESOURCEW*>(storage.data());
                return error;
            }

            size_t words = (static_cast<size_t>(bytes) + sizeof(ULONG_PTR) - 1) /
                           sizeof(ULONG_PTR);
            if (words <= storage.size())
            {
                if (storage.size() > (std::numeric_limits<DWORD>::max)() /
                                         (2 * sizeof(ULONG_PTR)))
                    return ERROR_NOT_ENOUGH_MEMORY;
                words = storage.size() * 2;
            }
            if (words > (std::numeric_limits<DWORD>::max)() / sizeof(ULONG_PTR))
                return ERROR_NOT_ENOUGH_MEMORY;
            storage.resize(words);
        }
    }
    catch (...)
    {
        return ERROR_NOT_ENOUGH_MEMORY;
    }
}

inline DWORD NethoodEnumerateResourceBatch(HANDLE enumeration, DWORD& entries,
                                           NethoodResourceBuffer& storage,
                                           NETRESOURCEW*& resources) noexcept
{
    return NethoodEnumerateResourceBatch(
        enumeration, entries, storage, resources,
        [](HANDLE handle, DWORD* count, void* buffer, DWORD* bytes) {
            return WNetEnumResourceW(handle, count, buffer, bytes);
        });
}
