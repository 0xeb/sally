// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <algorithm>
#include <string>

namespace sally
{
namespace unicode
{
// Restores original wide characters when UI text comes back with ANSI fallback
// placeholders ('?'). We only replace positions that still match the original
// lossy text to avoid overwriting user edits.
inline std::wstring RecoverWideCharsFromLossyInput(const std::wstring& edited,
                                                   const std::wstring& originalLossy,
                                                   const std::wstring& originalWide)
{
    if (edited.empty() || originalLossy.empty() || originalWide.empty())
        return edited;

    std::wstring recovered = edited;

    const size_t prefixMax = (std::min)(recovered.size(), (std::min)(originalLossy.size(), originalWide.size()));
    size_t prefix = 0;
    while (prefix < prefixMax && recovered[prefix] == originalLossy[prefix])
    {
        if (recovered[prefix] == L'?' && originalWide[prefix] != L'?')
            recovered[prefix] = originalWide[prefix];
        prefix++;
    }

    size_t suffix = 0;
    while (suffix < recovered.size() - prefix &&
           suffix < originalLossy.size() - prefix &&
           suffix < originalWide.size() - prefix)
    {
        const size_t recPos = recovered.size() - 1 - suffix;
        const size_t lossyPos = originalLossy.size() - 1 - suffix;
        const size_t widePos = originalWide.size() - 1 - suffix;

        if (recovered[recPos] != originalLossy[lossyPos])
            break;

        if (recovered[recPos] == L'?' && originalWide[widePos] != L'?')
            recovered[recPos] = originalWide[widePos];
        suffix++;
    }

    return recovered;
}
} // namespace unicode
} // namespace sally
