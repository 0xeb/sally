// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "common/Win32TextCodec.h"

#include <string>

namespace sally::plugin_catalog
{

// CMake writes plugins.ver as UTF-8. Keep its bytes byte-owned through row parsing and decode a
// complete path field exactly once; malformed input is refused without publishing a partial name.
inline bool DecodePath(const std::string& bytes, std::wstring& path) noexcept
{
    return static_cast<bool>(Win32DecodeText(CP_UTF8, bytes, path));
}

} // namespace sally::plugin_catalog
