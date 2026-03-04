// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <string>
#include <vector>

#include <windows.h>
#include <shlobj.h>

namespace sally
{
namespace clipboard
{
// Builds a CF_HDROP payload with Unicode paths.
inline bool BuildHDropWidePayload(const std::vector<std::wstring>& paths, std::vector<BYTE>& payload)
{
    payload.clear();
    if (paths.empty())
        return false;

    SIZE_T bytes = sizeof(DROPFILES) + sizeof(wchar_t); // struct + final list terminator
    for (size_t i = 0; i < paths.size(); i++)
        bytes += (paths[i].length() + 1) * sizeof(wchar_t);

    payload.resize((size_t)bytes, 0);

    DROPFILES* drop = reinterpret_cast<DROPFILES*>(payload.data());
    drop->pFiles = sizeof(DROPFILES);
    drop->fWide = TRUE;

    wchar_t* out = reinterpret_cast<wchar_t*>(payload.data() + sizeof(DROPFILES));
    for (size_t i = 0; i < paths.size(); i++)
    {
        lstrcpyW(out, paths[i].c_str());
        out += paths[i].length() + 1;
    }
    *out = L'\0';

    return true;
}
} // namespace clipboard
} // namespace sally
