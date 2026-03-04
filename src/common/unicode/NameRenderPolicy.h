// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <wchar.h>

namespace sally::unicode
{
inline int GetWideNameLengthForNameColumn(const wchar_t* nameW,
                                          bool isDir,
                                          bool sortDirsByExt,
                                          bool extensionInSeparateColumn)
{
    if (nameW == NULL)
        return 0;

    int fullLen = (int)wcslen(nameW);
    if (!extensionInSeparateColumn || (isDir && !sortDirsByExt))
        return fullLen;

    const wchar_t* dot = wcsrchr(nameW, L'.');
    if (dot == NULL || dot <= nameW)
        return fullLen; // ".htaccess" and names without extension stay in Name column

    return (int)(dot - nameW);
}

inline const wchar_t* GetWideExtensionStart(const wchar_t* nameW)
{
    if (nameW == NULL)
        return NULL;

    const wchar_t* dot = wcsrchr(nameW, L'.');
    if (dot == NULL || dot <= nameW)
        return NULL; // no extension or ".htaccess" style name

    return dot + 1;
}
} // namespace sally::unicode
