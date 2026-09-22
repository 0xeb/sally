// SPDX-FileCopyrightText: 2025-2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "registry_names.h"

namespace sally::salmon
{
inline constexpr bool kBugReporterRegistryMutexIsGlobalByPolicy = true;

inline const wchar_t* BugReporterRegistryMutexName()
{
    return SAL_REG_MUTEX_GLOBAL_BUG_REPORTER_W;
}
} // namespace sally::salmon
