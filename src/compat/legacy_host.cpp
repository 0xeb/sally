// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

// legacy_host — see legacy_host.h.
//
// Never include precomp.h here. The host owns facades whose frozen sdk107 types
// must coexist with the live SDK in this translation unit.

#define NOMINMAX
#include <windows.h>

#include <stdexcept>
#include <utility>

#include "compat/legacy_host_api.h"
#include "compat/legacy_host.h"

namespace sally::compat
{
namespace
{

template <typename Map, typename Legacy, typename Factory>
auto ResolveChildNoThrow(Map& facades, Legacy* legacy,
                         Factory&& factory) noexcept
    -> typename Map::mapped_type::element_type*
{
    if (legacy == nullptr)
        return nullptr;
    const auto found = facades.find(legacy);
    if (found != facades.end())
        return found->second.get();
    try
    {
        auto facade = factory();
        const auto inserted = facades.emplace(legacy, std::move(facade));
        return inserted.first->second.get();
    }
    catch (const std::bad_alloc&)
    {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return nullptr;
    }
    catch (const std::length_error&)
    {
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return nullptr;
    }
    catch (...)
    {
        SetLastError(ERROR_INVALID_DATA);
        return nullptr;
    }
}

} // namespace

void CLegacyPluginHostDeleter::operator()(CLegacyPluginHost* host) const noexcept
{
    delete host;
}

CLegacyPluginHostPtr CreateLegacyPluginHost(
    ::CSalamanderPluginEntryAbstract& wideEntry,
    ::CSalamanderDebugAbstract& wideDebug,
    ::CSalamanderGeneralAbstract& wideGeneral,
    ::CSalamanderSafeFileAbstract& wideSafeFile,
    ::CSalamanderGUIAbstract& wideGUI,
    int builtForVersion) noexcept
{
    try
    {
        return CLegacyPluginHostPtr(new CLegacyPluginHost(
            wideEntry, wideDebug, wideGeneral, wideSafeFile, wideGUI,
            builtForVersion));
    }
    catch (...)
    {
        return {};
    }
}

::CPluginInterfaceAbstract* InvokeLegacyPluginEntry(
    CLegacyPluginHost& host, FARPROC entryAddress) noexcept
{
    if (entryAddress == nullptr)
        return nullptr;
    try
    {
        return host.InvokeLegacyEntry(
            reinterpret_cast<sdk107::FSalamanderPluginEntry>(entryAddress));
    }
    catch (...)
    {
        return nullptr;
    }
}

CLegacyPluginHost::CLegacyPluginHost(
    ::CSalamanderPluginEntryAbstract& wideEntry,
    ::CSalamanderDebugAbstract& wideDebug,
    ::CSalamanderGeneralAbstract& wideGeneral,
    ::CSalamanderSafeFileAbstract& wideSafeFile,
    ::CSalamanderGUIAbstract& wideGUI,
    int builtForVersion)
    : Debug(wideDebug),
      SafeFile(wideSafeFile),
      GUI(wideGUI),
      BuiltForVersion(builtForVersion),
      PluginFS(std::make_unique<CLegacyPluginFSOwner>(PluginData, builtForVersion)),
      General(std::make_unique<CLegacySalamanderGeneral>(
          wideGeneral, PluginData, *PluginFS, builtForVersion)),
      Entry(wideEntry, {&Debug, General.get(), &GUI, &SafeFile})
{
}

CLegacyPluginHost::CLegacyPluginHost(::CSalamanderPluginEntryAbstract& wideEntry,
                                     ::CSalamanderDebugAbstract& wideDebug,
                                     ::CSalamanderSafeFileAbstract& wideSafeFile,
                                     ::CSalamanderGUIAbstract& wideGUI,
                                     const CLegacyEntryFacadeSet& facades)
    : Debug(wideDebug),
      SafeFile(wideSafeFile),
      GUI(wideGUI),
      BuiltForVersion(0),
      PluginFS(std::make_unique<CLegacyPluginFSOwner>(PluginData, BuiltForVersion)),
      Entry(wideEntry, {&Debug, facades.General, &GUI, &SafeFile})
{
}

CLegacySalamanderGeneral::CLegacySalamanderGeneral(
    ::CSalamanderGeneralAbstract& wideGeneral,
    CLegacyPluginDataResolver& pluginDataResolver,
    CLegacyPluginFSResolver& pluginFSResolver,
    int builtForVersion)
    : WideGeneral(wideGeneral),
      PluginDataResolver(pluginDataResolver),
      PluginFSResolver(pluginFSResolver),
      BuiltForVersion(builtForVersion),
      State(CreateState(), &CLegacySalamanderGeneral::DestroyState)
{
}

CLegacySalamanderGeneral::~CLegacySalamanderGeneral() = default;

sdk107::CSalamanderPluginEntryAbstract* CLegacyPluginHost::EntryFacade()
{
    return &Entry;
}

sdk107::CSalamanderDebugAbstract* CLegacyPluginHost::DebugFacade()
{
    return &Debug;
}

sdk107::CSalamanderSafeFileAbstract* CLegacyPluginHost::SafeFileFacade()
{
    return &SafeFile;
}

sdk107::CSalamanderGUIAbstract* CLegacyPluginHost::GUIFacade()
{
    return &GUI;
}

sdk107::CSalamanderGeneralAbstract* CLegacyPluginHost::GeneralFacade()
{
    return General.get();
}

CLegacyPluginDataOwner& CLegacyPluginHost::PluginDataFacades()
{
    return PluginData;
}

CLegacyPluginFSOwner* CLegacyPluginHost::PluginFSFacades()
{
    return PluginFS.get();
}

::CPluginInterfaceAbstract* CLegacyPluginHost::InvokeLegacyEntry(
    sdk107::FSalamanderPluginEntry entry)
{
    if (entry == nullptr || Plugin != nullptr)
        return nullptr;

    sdk107::CPluginInterfaceAbstract* legacy = entry(EntryFacade());
    if (legacy == nullptr)
        return nullptr;

    try
    {
        Plugin = std::make_unique<CLegacyPluginInterface>(
            *legacy, PluginData, *this, GUI);
        return Plugin.get();
    }
    catch (const std::bad_alloc&)
    {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return nullptr;
    }
    catch (...)
    {
        SetLastError(ERROR_INVALID_DATA);
        return nullptr;
    }
}

::CPluginInterfaceAbstract* CLegacyPluginHost::PluginFacade()
{
    return Plugin.get();
}

::CPluginInterfaceForArchiverAbstract* CLegacyPluginHost::ResolveArchiver(
    sdk107::CPluginInterfaceForArchiverAbstract* legacy)
{
    return ResolveChildNoThrow(
        Archivers, legacy,
        [&]() {
            return std::make_unique<CLegacyPluginInterfaceForArchiver>(
                *legacy, PluginData, BuiltForVersion);
        });
}

::CPluginInterfaceForViewerAbstract* CLegacyPluginHost::ResolveViewer(
    sdk107::CPluginInterfaceForViewerAbstract* legacy)
{
    return ResolveChildNoThrow(
        Viewers, legacy,
        [&]() {
            return std::make_unique<CLegacyPluginInterfaceForViewer>(*legacy);
        });
}

::CPluginInterfaceForMenuExtAbstract* CLegacyPluginHost::ResolveMenuExt(
    sdk107::CPluginInterfaceForMenuExtAbstract* legacy)
{
    return ResolveChildNoThrow(
        MenuExts, legacy,
        [&]() {
            return std::make_unique<CLegacyPluginInterfaceForMenuExt>(
                *legacy, GUI);
        });
}

::CPluginInterfaceForFSAbstract* CLegacyPluginHost::ResolveFS(
    sdk107::CPluginInterfaceForFSAbstract* legacy)
{
    if (PluginFS == nullptr)
        return nullptr;
    return ResolveChildNoThrow(
        FileSystems, legacy,
        [&]() {
            return std::make_unique<CLegacyPluginInterfaceForFS>(
                *legacy, *PluginFS);
        });
}

::CPluginInterfaceForThumbLoaderAbstract*
CLegacyPluginHost::ResolveThumbLoader(
    sdk107::CPluginInterfaceForThumbLoaderAbstract* legacy)
{
    return ResolveChildNoThrow(
        ThumbLoaders, legacy,
        [&]() {
            return std::make_unique<CLegacyPluginInterfaceForThumbLoader>(
                *legacy);
        });
}

} // namespace sally::compat
