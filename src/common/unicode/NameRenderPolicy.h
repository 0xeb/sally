// SPDX-FileCopyrightText: 2025-2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <wchar.h>

namespace sally::unicode
{
enum class NameColumnViewMode
{
    Brief,
    Detailed,
};

struct NameWidthMeasurementPlan
{
    int NameLength = 0;
    int ExtensionLength = 0;
};

inline bool ShouldUseSeparateExtensionColumn(bool isDir,
                                             bool sortDirsByExt,
                                             bool extensionInSeparateColumn,
                                             NameColumnViewMode viewMode)
{
    return viewMode == NameColumnViewMode::Detailed &&
           extensionInSeparateColumn &&
           (!isDir || sortDirsByExt);
}

inline int GetWideNameLengthForNameColumn(const wchar_t* nameW,
                                          bool isDir,
                                          bool sortDirsByExt,
                                          bool extensionInSeparateColumn,
                                          NameColumnViewMode viewMode = NameColumnViewMode::Detailed)
{
    if (nameW == NULL)
        return 0;

    int fullLen = (int)wcslen(nameW);
    if (!ShouldUseSeparateExtensionColumn(isDir, sortDirsByExt, extensionInSeparateColumn, viewMode))
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

// This took a fifth argument 'const wchar_t* nameW' and set
// plan.UseWide = (nameW != NULL) to choose between two branches. That predicate died with
// CFileData::NameW (see spl_com.h: "two wide names cannot disagree usefully"), so it was
// always false and every caller silently took the branch below. Both branches were measured
// and computed the SAME quantities: the wide one found the last dot with wcsrchr and took
// wcslen past it, this one derives the same split from Ext and NameLen. They agree because
// Ext points into Name just after that same last dot and NameLen == wcslen(Name).
//
// 'name'/'nameLen'/'ext' are CFileData's Name/NameLen/Ext, all wide since P1.3 - they were
// called nameA/nameLenA/extA while the wide mirror still existed to distinguish them from it.
inline NameWidthMeasurementPlan BuildNameWidthMeasurementPlan(const wchar_t* name,
                                                              int nameLen,
                                                              const wchar_t* ext,
                                                              bool isDir,
                                                              bool sortDirsByExt,
                                                              bool extensionInSeparateColumn,
                                                              NameColumnViewMode viewMode = NameColumnViewMode::Detailed)
{
    NameWidthMeasurementPlan plan;
    if (name == NULL)
        return plan;

    const bool splitExtension = ShouldUseSeparateExtensionColumn(isDir, sortDirsByExt, extensionInSeparateColumn, viewMode);

    plan.NameLength = nameLen;
    if (splitExtension && ext != NULL && ext[0] != 0 && ext > name + 1)
    {
        plan.NameLength = (int)(ext - name - 1);
        plan.ExtensionLength = nameLen - (int)(ext - name);
    }
    return plan;
}
} // namespace sally::unicode
