// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"

#include <windows.h>

#include "strutils.h"

WCHAR* DupStr(const WCHAR* txt)
{
    if (txt == NULL)
        return NULL;
    int len = lstrlenW(txt) + 1;
    WCHAR* ret = (WCHAR*)malloc(len * sizeof(WCHAR));
#ifndef SAFE_ALLOC
    if (ret == NULL)
        return NULL;
#endif // SAFE_ALLOC
    return (WCHAR*)memcpy(ret, txt, len * sizeof(WCHAR));
}
