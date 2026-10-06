// SPDX-FileCopyrightText: 2025-2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

// Which plugins Sally loads, decided from the SDK version a plugin reports.
//
// A plugin reports the oldest Sally it needs through SalamanderPluginGetReqVer and, optionally,
// the SDK it was compiled against through SalamanderPluginGetSDKVer. Sally serves exactly one
// plugin SDK: there is no compatibility layer for older SDKs, and a plugin built for a newer
// Sally would call services this build does not have.

#pragma once

namespace sally::plugin_sdk
{

// The SDK version a plugin was built for. 'reqVersion' is what SalamanderPluginGetReqVer
// returned (-1 when the plugin does not export it); 'sdkVersion' is what
// SalamanderPluginGetSDKVer returned (-1 when not exported). A plugin may report an old
// required version for compatibility while being compiled against a newer SDK; the newer
// SDK wins. Plugins below 'minimumReqVersion' keep their reported value, which is refused.
inline int ResolveBuiltForVersion(int reqVersion, int sdkVersion, int minimumReqVersion)
{
    if (reqVersion < minimumReqVersion)
        return reqVersion;
    return sdkVersion >= reqVersion ? sdkVersion : reqVersion;
}

// Only a plugin built for exactly the SDK this Sally serves is loaded.
inline bool IsLoadable(int builtForVersion, int currentSdkVersion)
{
    return builtForVersion == currentSdkVersion;
}

} // namespace sally::plugin_sdk
