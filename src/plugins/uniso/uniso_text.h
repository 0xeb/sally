// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <string>
#include <string_view>

#include <windows.h>

bool DecodeUnisoLegacyText(std::string_view bytes, std::wstring& text) noexcept;
bool EncodeUnisoReportText(std::wstring_view text, std::string& bytes) noexcept;
bool CopyUnisoReportField(std::string_view bytes, std::string& field) noexcept;
bool FormatUnisoReportSystemTime(const SYSTEMTIME& time, std::string& text) noexcept;
