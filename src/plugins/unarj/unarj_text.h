// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <cstddef>
#include <string>
#include <vector>

bool DecodeArjMemberName(const char* bytes, size_t length, std::wstring& name);
bool TryBuildNextArjVolumePath(const std::wstring& current, std::wstring& next);
std::vector<std::wstring> SplitArjMasks(const wchar_t* masks);
