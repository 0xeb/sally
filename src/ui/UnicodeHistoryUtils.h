// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <windows.h>

BOOL IsWideHistoryEmpty(wchar_t* history[], int count);
void SeedWideHistoryFromAnsi(wchar_t* historyW[], int historyWCount, char* historyA[], int historyACount);
void AddValueToWideHistory(wchar_t** historyArr, int historyItemsCount,
                           const wchar_t* value, BOOL caseSensitiveValue);

