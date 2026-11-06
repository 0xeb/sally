// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

// The Find dialog's four comboboxes must all receive dark-theme skinning. The
// "Type:" combo (IDC_FIND_FILETYPE) was originally omitted, so it rendered as a stray
// white row in dark mode. This is the single source of truth for the themed-combo set,
// shared by the dialog code and the regression test.
//
// Returns the array of Find combobox control ids that ApplyFindComboSkins() must skin,
// and sets 'count' to its length.
const int* GetFindThemedComboIds(int& count);
