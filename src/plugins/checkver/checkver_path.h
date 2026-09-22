// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <string>

namespace checkver
{

inline std::wstring ModuleDirectory(const std::wstring& modulePath)
{
    const size_t slash = modulePath.find_last_of(L'\\');
    return slash != std::wstring::npos ? modulePath.substr(0, slash) : std::wstring();
}

} // namespace checkver
