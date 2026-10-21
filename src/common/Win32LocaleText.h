// SPDX-FileCopyrightText: 2025-2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <windows.h>
#include <string>

// Empty on API failure; successful storage includes exactly the returned text.
std::wstring Win32FormatLocalDateTimePart(const SYSTEMTIME& value, bool date);
