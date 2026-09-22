// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

BOOL Error(HWND hParent, int lastErr, int title, int error);

void GetFirstWord(char* str, int& pos, int& len, char delimitChar = 0);
void GetLastWord(char* str, int& pos, int& len, char delimitChar = 0);
BYTE hex(char c);
BOOL IsHex(const char* str, int len);
