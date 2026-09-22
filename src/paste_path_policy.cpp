// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

// SALLY_PASTE_PATH_POLICY_STANDALONE lets the headless test compile this real translation
// unit without Sally's precompiled header.
#ifndef SALLY_PASTE_PATH_POLICY_STANDALONE
#include "precomp.h"
#endif

#include "paste_path_policy.h"
#include "common/SalPathWide.h"

namespace sally
{


bool ResolvePastedFilePathW(const wchar_t* fullPath, DWORD attrs,
                            std::wstring& directoryOut, std::wstring& focusNameOut)
{
    if (fullPath == nullptr || fullPath[0] == 0)
        return false;
    if (attrs == INVALID_FILE_ATTRIBUTES)
        return false; // does not exist - leave the existing error reporting alone
    if ((attrs & FILE_ATTRIBUTE_DIRECTORY) != 0)
        return false; // a directory is listed directly, same as the ANSI path does

    std::wstring directory(fullPath);
    std::wstring name;
    if (!CutDirectoryW(directory, &name) || name.empty())
        return false; // no parent to list ("C:\", "\server\share")

    directoryOut = directory;
    focusNameOut = name;
    return true;
}

} // namespace sally
