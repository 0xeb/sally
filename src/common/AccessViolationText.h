// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <windows.h>

namespace Sally::Diagnostics
{

// The operation an access violation reports in ExceptionInformation[0]: 0 for a read,
// 1 for a write and 8 for a data-execution fault, which is also what a call through a
// pointer into an unloaded module produces.
inline const char* AccessViolationOperation(ULONG_PTR kind)
{
    switch (kind)
    {
    case 0:
        return "read";
    case 1:
        return "write";
    case 8:
        return "execute";
    default:
        return "access";
    }
}

} // namespace Sally::Diagnostics
