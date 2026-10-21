// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <string>
#include <cstdint>

bool DecodeFatOemName(const char* bytes, std::wstring& name);
bool DecodeFatShortName(const char* bytes, uint8_t caseFlags, std::wstring& name);
