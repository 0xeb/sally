// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <string>
#include <vector>

bool DecodeLhaName(const std::string& bytes, std::wstring& name);
std::vector<std::wstring> SplitLhaMasks(const wchar_t* masks);
