// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <string>
#include <string_view>

namespace sally::salmon
{

bool ParseCommandLine(std::wstring_view commandLine, std::wstring& mappingName,
                      std::wstring& slgName) noexcept;
bool ReadFrozenWideString(const wchar_t* value, size_t capacity, std::wstring& output) noexcept;
bool EncodeUtf8(std::wstring_view text, std::string& encoded) noexcept;

}
