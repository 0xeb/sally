// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

BOOL SplitDialog(const wchar_t* fileName, // [in]     name of the file being split
                 CQuadWord& fileSize,    // [in]     its size
                 std::wstring& targetDir, // [in/out] target directory
                 CQuadWord* partialSize, // [out]    size of a single part
                 HWND hParent);

BOOL CombineDialog(TIndirectArray<wchar_t>& files, // [in/out] array of partial file names
                   std::wstring& targetName,        // [in/out] name of the target file
                   BOOL bOrigCrcFound,          // [in]     flag indicating whether the original CRC was found
                   UINT32 origCrc,              // [in]     original CRC
                   HWND hParent, CSalamanderForOperationsAbstract* salamander);

void ConfigDialog(HWND hParent);

void CRCDialog(TIndirectArray<wchar_t>& files, BOOL bCrcFound, UINT32 origCrc,
               HWND parent, CSalamanderForOperationsAbstract* salamander);
