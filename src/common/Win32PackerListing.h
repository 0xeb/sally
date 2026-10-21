// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <windows.h>
#include <functional>
#include <string_view>

// External archiver output is an encoded byte protocol. Completed lines retain
// a single LF for the existing listing parsers. The caller owns the pipe.
// The callback consumes borrowed bytes synchronously; retained lines need copies.
DWORD Win32ReadPackerListingLines(HANDLE pipe, const std::function<bool(std::string_view)>& acceptLine);
