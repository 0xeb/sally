// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

extern std::wstring Command;
extern std::wstring Arguments;
extern std::wstring InitDir;

extern CSalamanderVarStrEntry ExpCommandVariables[];
extern CSalamanderVarStrEntry ExpArgumentsVariables[];
extern CSalamanderVarStrEntry ExpInitDirVariables[];

BOOL ExecuteEditor(const wchar_t* tempFile);
