// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

// Small helpers for texts and message boxes that every fs/*.cpp file needs (one copy instead of one per file).

#pragma once

#include <string>

#include "../ui/error_text.h" // DescribeError, used by every fs file
#include "site.h"
#include "ssh_types.h"

// An OK message box with the exclamation icon and the plugin name as the title.
void ShowMessage(HWND parent, const std::wstring& text);
// The same, with Sally's current message box parent as the owner.
void ShowMessage(const std::wstring& text);

// "host:port" as the operation window names the server.
std::wstring HostTextOf(const CSite& site);
