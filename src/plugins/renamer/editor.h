// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <string>

inline const char* FindFileNameA(const char* path)
{
    int len = (int)strlen(path);
    if (len < 2)
        return path;
    const char* iterator = path + len - 1;
    while (iterator != path)
    {
        iterator--;
        if (*iterator == '\\')
            return iterator + 1;
    }
    return path;
}

extern std::wstring Command;
extern std::wstring Arguments;
extern std::wstring InitDir;

extern CSalamanderVarStrEntry ExpCommandVariables[];
extern CSalamanderVarStrEntry ExpArgumentsVariables[];
extern CSalamanderVarStrEntry ExpInitDirVariables[];

BOOL ExecuteEditor(const wchar_t* tempFile);
