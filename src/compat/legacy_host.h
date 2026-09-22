// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

// legacy_host — per-plugin ownership for the frozen v107 facade graph.

#pragma once

#include <memory>
#include <unordered_map>

#include "compat/core_to_legacy.h"

namespace sally::compat
{

// Grows into the per-plugin owner of every legacy facade. Keeping ownership at
// the load boundary makes each facade pointer stable for the plugin lifetime.
class CLegacyPluginHost final : public CLegacyPluginChildResolver
{
public:
    // The production construction path. General and the reverse-FS owner are
    // lifetime-stable siblings, so General can resolve exactly the frozen FS
    // identities that the plugin's returned FS factory publishes.
    CLegacyPluginHost(::CSalamanderPluginEntryAbstract& wideEntry,
                      ::CSalamanderDebugAbstract& wideDebug,
                      ::CSalamanderGeneralAbstract& wideGeneral,
                      ::CSalamanderSafeFileAbstract& wideSafeFile,
                      ::CSalamanderGUIAbstract& wideGUI,
                      int builtForVersion);

    // Temporary test construction seam. Production routing must use the
    // owning overload above rather than injecting a General facade.
    CLegacyPluginHost(::CSalamanderPluginEntryAbstract& wideEntry,
                      ::CSalamanderDebugAbstract& wideDebug,
                      ::CSalamanderSafeFileAbstract& wideSafeFile,
                      ::CSalamanderGUIAbstract& wideGUI,
                      const CLegacyEntryFacadeSet& facades);

    sdk107::CSalamanderPluginEntryAbstract* EntryFacade();
    sdk107::CSalamanderDebugAbstract* DebugFacade();
    sdk107::CSalamanderSafeFileAbstract* SafeFileFacade();
    sdk107::CSalamanderGUIAbstract* GUIFacade();
    sdk107::CSalamanderGeneralAbstract* GeneralFacade();
    CLegacyPluginDataOwner& PluginDataFacades();
    CLegacyPluginFSOwner* PluginFSFacades();

    // Calls one frozen entry point through the host-owned parent facade, then
    // retains a live top-level wrapper and every returned child wrapper for the
    // lifetime of this host. The loader owns this host for as long as its DLL is
    // loaded, so no legacy vtable pointer escapes a call-scoped adapter.
    ::CPluginInterfaceAbstract* InvokeLegacyEntry(
        sdk107::FSalamanderPluginEntry entry);
    ::CPluginInterfaceAbstract* PluginFacade();

private:
    ::CPluginInterfaceForArchiverAbstract* ResolveArchiver(
        sdk107::CPluginInterfaceForArchiverAbstract* legacy) override;
    ::CPluginInterfaceForViewerAbstract* ResolveViewer(
        sdk107::CPluginInterfaceForViewerAbstract* legacy) override;
    ::CPluginInterfaceForMenuExtAbstract* ResolveMenuExt(
        sdk107::CPluginInterfaceForMenuExtAbstract* legacy) override;
    ::CPluginInterfaceForFSAbstract* ResolveFS(
        sdk107::CPluginInterfaceForFSAbstract* legacy) override;
    ::CPluginInterfaceForThumbLoaderAbstract* ResolveThumbLoader(
        sdk107::CPluginInterfaceForThumbLoaderAbstract* legacy) override;

    CLegacySalamanderDebug Debug;
    CLegacySalamanderSafeFile SafeFile;
    CLegacySalamanderGUI GUI;
    CLegacyPluginDataOwner PluginData;
    int BuiltForVersion;
    std::unique_ptr<CLegacyPluginFSOwner> PluginFS;
    std::unique_ptr<CLegacySalamanderGeneral> General;
    std::unordered_map<sdk107::CPluginInterfaceForArchiverAbstract*,
                       std::unique_ptr<CLegacyPluginInterfaceForArchiver>>
        Archivers;
    std::unordered_map<sdk107::CPluginInterfaceForViewerAbstract*,
                       std::unique_ptr<CLegacyPluginInterfaceForViewer>>
        Viewers;
    std::unordered_map<sdk107::CPluginInterfaceForMenuExtAbstract*,
                       std::unique_ptr<CLegacyPluginInterfaceForMenuExt>>
        MenuExts;
    std::unordered_map<sdk107::CPluginInterfaceForFSAbstract*,
                       std::unique_ptr<CLegacyPluginInterfaceForFS>>
        FileSystems;
    std::unordered_map<sdk107::CPluginInterfaceForThumbLoaderAbstract*,
                       std::unique_ptr<CLegacyPluginInterfaceForThumbLoader>>
        ThumbLoaders;
    std::unique_ptr<CLegacyPluginInterface> Plugin;
    CLegacySalamanderPluginEntry Entry;
};

} // namespace sally::compat
