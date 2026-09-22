// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "../shared/plugin_text_encoding.h"

#include <string>

namespace sally::automation_text
{

// Automation script files historically use the active Windows code page. Keep those bytes
// explicit until the complete mapped file is decoded into the UTF-16 Active Scripting input.
inline bool DecodeScriptSource(const char* bytes, size_t byteCount,
                               std::wstring& source) noexcept
{
    return sally::plugin_text::DecodeAcp(bytes, byteCount, source);
}

} // namespace sally::automation_text
