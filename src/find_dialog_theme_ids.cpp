// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

// SALLY_FIND_THEME_IDS_STANDALONE lets the headless test compile this real translation
// unit without Sally's precompiled header.
#ifndef SALLY_FIND_THEME_IDS_STANDALONE
#include "precomp.h"
#endif

#include "lang/lang.rh" // IDC_FIND_* control ids
#include "find_dialog_theme_ids.h"

// All four Find comboboxes, including the "Type:" combo IDC_FIND_FILETYPE that was
// previously missing and rendered white in dark mode.
static const int g_findThemedComboIds[] = {
    IDC_FIND_NAMED,
    IDC_FIND_LOOKIN,
    IDC_FIND_CONTAINING,
    IDC_FIND_FILETYPE,
};

const int* GetFindThemedComboIds(int& count)
{
    count = (int)(sizeof(g_findThemedComboIds) / sizeof(g_findThemedComboIds[0]));
    return g_findThemedComboIds;
}
