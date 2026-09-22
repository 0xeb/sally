// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <string>

// extracted from GetNetworkDrivesBody so it can be unit-tested directly without a
// live network connection (same shape as udf_ostacompress.h/hfs_root_name_copy.h - zero
// dependency on precomp.h/the Salamander SDK). The former narrow enumeration
// (WNetEnumResourceA) had Windows best-fit-substitute any remote share name outside CP_ACP
// before this code ever saw it. This helper is the pure per-entry decision the native-wide
// GetNetworkDrivesBody applies to each WNetEnumResourceW result, so the decision (which drive
// letter and what exact remote path) can be verified without
// a real network resource.
//
// Returns true and fills 'driveIndexOut'/'remotePathOut' when 'localName' names a drive letter
// (A-Z, case-insensitive) followed by ':' - the same shape GetNetworkDrivesBody's narrow loop
// checks (name[1] == ':'). Returns false for any other resource (a printer, a UNC-only bookmark
// with no assigned drive letter, etc.), leaving the outputs untouched.
inline bool ExtractNetworkDriveEntryW(const wchar_t* localName, const wchar_t* remoteName,
                                      int& driveIndexOut, std::wstring& remotePathOut)
{
    if (localName == nullptr || localName[0] == 0 || localName[1] != L':')
        return false;

    wchar_t drv = localName[0];
    if (drv >= L'A' && drv <= L'Z')
        drv = (wchar_t)(drv - L'A' + L'a');
    if (drv < L'a' || drv > L'z')
        return false;

    driveIndexOut = drv - L'a';

    remotePathOut = remoteName != nullptr ? remoteName : L"";
    return true;
}
