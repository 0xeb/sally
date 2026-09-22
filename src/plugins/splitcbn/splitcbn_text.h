// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <string>
#include <string_view>

#include <windows.h>

// Generated command files are an explicit OEM byte protocol consumed by cmd.exe.
// Semantic names remain UTF-16 and must be exactly representable at this boundary.
bool EncodeSplitBatchText(std::wstring_view text, std::string& bytes) noexcept;
bool DecodeSplitBatchText(std::string_view bytes, std::wstring& text) noexcept;
bool EscapeSplitBatchArgument(std::string_view bytes, std::string& escaped) noexcept;
bool EscapeSplitBatchEchoText(std::string_view bytes, std::string& escaped) noexcept;

bool FormatSplitLocalDateTime(const SYSTEMTIME& value, std::wstring& date,
                              std::wstring& time) noexcept;
