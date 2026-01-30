// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-License-Identifier: GPL-2.0-or-later
// CommentsTranslationProject: TRANSLATED

#pragma once

extern CPathBuffer Command;
extern CPathBuffer Arguments;
extern CPathBuffer InitDir;

extern CSalamanderVarStrEntry ExpCommandVariables[];
extern CSalamanderVarStrEntry ExpArgumentsVariables[];
extern CSalamanderVarStrEntry ExpInitDirVariables[];

BOOL ExecuteEditor(const char* tempFile);
