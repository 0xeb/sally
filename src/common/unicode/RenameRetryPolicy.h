// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <windows.h>

namespace sally::unicode
{
inline bool ShouldRetryUnicodeRenameAfterError(DWORD error)
{
    return error != ERROR_SUCCESS;
}
} // namespace sally::unicode
