// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <string>

namespace Sally::Unicode
{

inline void AppendShellExtensionWindowTitle(std::wstring& reason, const std::wstring& title)
{
    if (title.empty())
        return;

    reason.append(L"\r\n");
    reason.append(title);
}

} // namespace Sally::Unicode
