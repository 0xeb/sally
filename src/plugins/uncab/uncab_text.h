// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <string>
#include <vector>

// Cabinet.dll exposes byte strings. Keep that encoding boundary named and exact instead of
// letting byte ownership or ambient conversions leak into the plugin's UTF-16 UI/path code.
bool ProjectCabBytesToWide(const char* source, std::wstring& target);
bool DecodeCabMemberName(const char* source, bool utf8, std::wstring& target);
bool ProjectWideToCabBytes(const wchar_t* source, std::string& target);
bool EqualCabBytesIgnoringCase(const char* left, const char* right);
bool EqualCabMemberNames(const std::wstring& left, const std::wstring& right) noexcept;

// Sally's mask grammar uses a doubled semicolon for a literal semicolon. Returned masks are
// trimmed, dynamically owned UTF-16 and ready for host-side normalization.
std::vector<std::wstring> SplitUnCabMasks(const wchar_t* masks);
