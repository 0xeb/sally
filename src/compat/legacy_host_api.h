// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <memory>
#include <windows.h>

class CPluginInterfaceAbstract;
class CSalamanderDebugAbstract;
class CSalamanderGeneralAbstract;
class CSalamanderGUIAbstract;
class CSalamanderPluginEntryAbstract;
class CSalamanderSafeFileAbstract;

namespace sally::compat
{

class CLegacyPluginHost;

struct CLegacyPluginHostDeleter
{
    void operator()(CLegacyPluginHost* host) const noexcept;
};

using CLegacyPluginHostPtr =
    std::unique_ptr<CLegacyPluginHost, CLegacyPluginHostDeleter>;

// This is the only frozen-plugin construction surface visible to the live
// loader. Frozen types and function signatures remain private to compat/.
CLegacyPluginHostPtr CreateLegacyPluginHost(
    CSalamanderPluginEntryAbstract& wideEntry,
    CSalamanderDebugAbstract& wideDebug,
    CSalamanderGeneralAbstract& wideGeneral,
    CSalamanderSafeFileAbstract& wideSafeFile,
    CSalamanderGUIAbstract& wideGUI,
    int builtForVersion) noexcept;

CPluginInterfaceAbstract* InvokeLegacyPluginEntry(
    CLegacyPluginHost& host, FARPROC entryAddress) noexcept;

} // namespace sally::compat
