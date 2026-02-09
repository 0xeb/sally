// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-License-Identifier: GPL-2.0-or-later
// CommentsTranslationProject: TRANSLATED

#pragma once

// Long-path-safe buffer size for path variables (replaces MAX_PATH where safe)
#define SETUP_MAX_PATH 32768

void InitUtils();

int StrICmp(const char* s1, const char* s2);
void* mini_memcpy(void* out, const void* in, int len);
char* mystrstr(char* a, char* b);
BOOL GetFolderPath(int nFolder, LPTSTR pszPath); // pszPath must be at least MAX_PATH characters!
//BOOL Is64BitWindows();
