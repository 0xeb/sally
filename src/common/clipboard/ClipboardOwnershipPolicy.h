// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

namespace sally::clipboard
{
inline bool ShouldTagAsSalamanderObject(bool usedUnicodeHDropFallback)
{
    (void)usedUnicodeHDropFallback;
    return true;
}
} // namespace sally::clipboard

