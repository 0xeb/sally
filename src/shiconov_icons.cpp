// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#ifndef SALLY_SHICONOV_ICONS_STANDALONE
#include "precomp.h"
#endif

#include "shiconov_icons.h"

#include <shlobj.h>

int LoadShellOverlayIcons(const char* iconFile, int iconIndex, const int* sizes, int sizeCount,
                          HICON* icons, ShellIconExtractFn extract)
{
    if (icons == NULL || sizes == NULL || sizeCount <= 0)
        return 0;

    for (int i = 0; i < sizeCount; i++)
        icons[i] = NULL;

    if (iconFile == NULL || iconFile[0] == 0)
        return 0;

    if (extract == NULL)
        extract = &SHDefExtractIconA;

    int loaded = 0;
    for (int i = 0; i < sizeCount; i++)
    {
        // One size per call, with the small-icon slot unused.
        //
        // nIconSize is MAKELONG(largeSize, smallSize); passing a bare size asks for that
        // one size only. The previous code packed two sizes into this argument and read
        // both icons back, which is where issue #90 came from - see the header.
        //
        // Note SHDefExtractIcon can return S_FALSE when it cannot produce the exact size
        // requested, so success must be tested with == S_OK rather than SUCCEEDED().
        HICON hIcon = NULL;
        if (extract(iconFile, iconIndex, 0, &hIcon, NULL, (UINT)sizes[i]) == S_OK &&
            hIcon != NULL)
        {
            icons[i] = hIcon;
            loaded++;
        }
    }
    return loaded;
}

// wide: same contract as LoadShellOverlayIcons above (see shiconov_icons.h) - a
// genuine parallel implementation, not a template, since production calls it with a wide COM
// result (IShellIconOverlayIdentifier::GetOverlayInfo) that must never be narrowed first.
int LoadShellOverlayIconsW(const wchar_t* iconFile, int iconIndex, const int* sizes, int sizeCount,
                           HICON* icons, ShellIconExtractFnW extract)
{
    if (icons == NULL || sizes == NULL || sizeCount <= 0)
        return 0;

    for (int i = 0; i < sizeCount; i++)
        icons[i] = NULL;

    if (iconFile == NULL || iconFile[0] == 0)
        return 0;

    if (extract == NULL)
        extract = &SHDefExtractIconW;

    int loaded = 0;
    for (int i = 0; i < sizeCount; i++)
    {
        HICON hIcon = NULL;
        if (extract(iconFile, iconIndex, 0, &hIcon, NULL, (UINT)sizes[i]) == S_OK &&
            hIcon != NULL)
        {
            icons[i] = hIcon;
            loaded++;
        }
    }
    return loaded;
}
