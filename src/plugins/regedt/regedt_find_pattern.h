// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <string>
#include <string_view>
#include <vector>

#include <windows.h>

bool EncodeRegedtSearchPattern(std::wstring_view pattern, std::string& bytes) noexcept;
bool EncodeRegedtSearchSubject(std::wstring_view text, std::string& bytes) noexcept;
bool MakeRegedtUtf16SearchBytes(std::wstring_view pattern, std::vector<char>& bytes) noexcept;
bool DecodeRegedtHexSearchPattern(std::wstring_view pattern, std::vector<char>& bytes) noexcept;
bool DecodeRegedtRegexError(const char* bytes, std::wstring& text) noexcept;
bool ParseRegedtFindTime(std::wstring_view text, bool maximum, SYSTEMTIME& time) noexcept;
