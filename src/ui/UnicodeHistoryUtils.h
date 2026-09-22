// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <windows.h>
#include <cstddef>
#include <string>

BOOL IsWideHistoryEmpty(wchar_t* history[], int count);
void AddValueToWideHistory(wchar_t** historyArr, int historyItemsCount,
                           const wchar_t* value, BOOL caseSensitiveValue);

// Dual-read precedence for a wide/ANSI value PAIR (hot paths today;
// the same shape as the ten histories).
//
// The question it answers: a profile may hold the wide values, the legacy ANSI
// ones, or both. Which does the loader believe?
//
// Rule: the wide pair wins if EITHER half was present, because a hot path may
// legitimately have a name and no path (or the reverse), and requiring both would
// silently fall back to the lossy ANSI values for a perfectly good half-filled
// entry. Absent both, the legacy values seed the entry as stored - never "healed"
// into something they never said.
//
// 'gotWideName'/'gotWidePath' are the per-value read results.
BOOL ShouldUseWidePair(BOOL gotWideName, BOOL gotWidePath);

// Escapes literal '$' characters in a hot path before variable expansion.
void EscapeHotPathDollars(std::wstring& text);

// Appends one optionally quoted argument to a space-separated user-menu list.
// The update is transactional when the explicit execution limit would be exceeded.
BOOL AppendUserMenuArgument(std::wstring& list, const wchar_t* name, size_t nameLength,
                            size_t maxLength);

