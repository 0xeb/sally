// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <cstddef>
#include <string_view>

// The embedded UnRAR engine allocates this many wchar_t elements before it
// invokes UCM_CHANGEVOLUMEW. This is an external callback-storage contract,
// not a limit on Sally's dynamically owned path strings.
inline constexpr std::size_t UNRAR_CHANGE_VOLUME_BUFFER_CHARS = 0x10000;

inline constexpr bool UnrarChangeVolumeFits(std::wstring_view path) noexcept
{
    return path.size() < UNRAR_CHANGE_VOLUME_BUFFER_CHARS;
}
