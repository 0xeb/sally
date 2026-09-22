// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

/*
	Network Plugin for Open Salamander
	
	Copyright (c) 2008-2023 Milan Kase <manison@manison.cz>
	
	TODO:
	Open-source license goes here...
*/

#include "precomp.h"
#include "nethood.h"
#include "cache.h"
#include "nethoodfs.h"
#include "nethood_fs_operations.h"
#include "nethooddata.h"
#include "nethoodmenu.h"
#include "globals.h"
#include "config.h"
#include "icons.h"
#include "nethood.rh"
#include "nethood.rh2"
#include "lang\lang.rh"
#include "cfgdlg.h"

#define NETHOOD_WIDEN_IMPL(value) L##value
#define NETHOOD_WIDEN(value) NETHOOD_WIDEN_IMPL(value)

extern CNethoodCache g_oNethoodCache;
extern CNethoodIcons g_oIcons;
extern CNethoodPluginInterfaceForMenuExt g_oMenuExt;

static const wchar_t CONFIG_VERSION[] = L"Version";
#define CURRENT_CONFIG_VERSION 1
static const wchar_t CONFIG_SYSSHARES[] = L"ShowSystemShares";
static const wchar_t CONFIG_NETSHORTCUTS[] = L"ShowShortcuts";
static const wchar_t CONFIG_SHOWSERVERS[] = L"ShowServers";
static const wchar_t CONFIG_PRELOAD[] = L"Preload";
static const wchar_t CONFIG_TSCVOLUMES[] = L"ShowRDSVolumes"; // Remote Desktop Services

void WINAPI
CNethoodPluginInterface::About(
    __in HWND hwndParent)
{
    const std::wstring message = SPLFormatStringOwned(
        L"%s %s\n\n%s\n\n%s",
        SPLLoadStrOwned(SalamanderGeneral, GetLangInstance(), IDS_PLUGIN_NAME).c_str(),
        NETHOOD_WIDEN(VERSINFO_VERSION), NETHOOD_WIDEN(VERSINFO_COPYRIGHT),
        SPLLoadStrOwned(SalamanderGeneral, GetLangInstance(), IDS_DESCRIPTION).c_str());

    SalamanderGeneral->SalMessageBox(
        hwndParent,
        message.c_str(),
        SPLLoadStrOwned(SalamanderGeneral, GetLangInstance(), IDS_ABOUT).c_str(),
        MB_OK | MB_ICONINFORMATION);
}

BOOL WINAPI
CNethoodPluginInterface::Release(
    __in HWND parent,
    __in BOOL force)
{
    g_oNethoodCache.Destroy();
    ReleaseWinLib(g_hInstance);
    return TRUE;
}

void WINAPI
CNethoodPluginInterface::LoadConfiguration(
    __in HWND hwndParent,
    __in HKEY hKey,
    __in CSalamanderRegistryAbstract* registry)
{
    DWORD dwVersion;
    DWORD dwArbitrary;
    bool bDisplayShortcuts;

    CALL_STACK_MESSAGE1("CNethoodPluginInterface::LoadConfiguration(, ,)");

    if (!hKey || !registry->GetValue(hKey, CONFIG_VERSION, REG_DWORD, &dwVersion, sizeof(DWORD)))
        dwVersion = CURRENT_CONFIG_VERSION;

    if (hKey && registry->GetValue(hKey, CONFIG_SYSSHARES, REG_DWORD, &dwArbitrary, sizeof(DWORD)) &&
        dwArbitrary < 3)
    {
        g_oNethoodCache.SetDisplaySystemShares(static_cast<CNethoodCache::SystemSharesDisplayMode>(dwArbitrary));
    }
    else
    {
        g_oNethoodCache.SetDisplaySystemShares(CNethoodCache::SysShareNone);
    }

    if (hKey && registry->GetValue(hKey, CONFIG_NETSHORTCUTS, REG_DWORD, &dwArbitrary, sizeof(DWORD)))
    {
        bDisplayShortcuts = (dwArbitrary != FALSE);
    }
    else
    {
        // Display My Network Places on Win2K/XP/2003 by default.
        bDisplayShortcuts = (_winmajor == 5);
    }
    g_oNethoodCache.SetDisplayNetworkShortcuts(bDisplayShortcuts);

    if (hKey && registry->GetValue(hKey, CONFIG_SHOWSERVERS, REG_DWORD, &dwArbitrary, sizeof(DWORD)))
    {
        CNethoodFSInterface::SetHideServersInRoot(dwArbitrary == 0);
    }
    else
    {
        CNethoodFSInterface::SetHideServersInRoot(bDisplayShortcuts);
    }

    if (hKey && registry->GetValue(hKey, CONFIG_PRELOAD, REG_DWORD, &dwArbitrary, sizeof(DWORD)))
    {
        m_bPreload = (dwArbitrary != 0);
    }
    else
    {
        m_bPreload = false;
    }

    bool bTSAvailable = g_oNethoodCache.AreTSAvailable();
    if (hKey && registry->GetValue(hKey, CONFIG_TSCVOLUMES, REG_DWORD, &dwArbitrary, sizeof(DWORD)) && bTSAvailable && dwArbitrary < 3)
    {
        g_oNethoodCache.SetDisplayTSClientVolumes(static_cast<CNethoodCache::TSCDisplayMode>(dwArbitrary));
    }
    else
    {
        g_oNethoodCache.SetDisplayTSClientVolumes(bTSAvailable ? CNethoodCache::TSCDisplayFolder : CNethoodCache::TSCDisplayNone);
    }
}

void WINAPI
CNethoodPluginInterface::SaveConfiguration(
    __in HWND hwndParent,
    __in HKEY hKey,
    __in CSalamanderRegistryAbstract* registry)
{
    CALL_STACK_MESSAGE1("CNethoodPluginInterface::SaveConfiguration(, ,)");

    if (hKey != NULL)
    {
        DWORD dwArbitrary;

        dwArbitrary = CURRENT_CONFIG_VERSION;
        registry->SetValue(hKey, CONFIG_VERSION, REG_DWORD, &dwArbitrary, sizeof(DWORD));

        dwArbitrary = g_oNethoodCache.GetDisplaySystemShares();
        registry->SetValue(hKey, CONFIG_SYSSHARES, REG_DWORD, &dwArbitrary, sizeof(DWORD));

        dwArbitrary = g_oNethoodCache.GetDisplayNetworkShortcuts();
        registry->SetValue(hKey, CONFIG_NETSHORTCUTS, REG_DWORD, &dwArbitrary, sizeof(DWORD));

        dwArbitrary = !CNethoodFSInterface::GetHideServersInRoot();
        registry->SetValue(hKey, CONFIG_SHOWSERVERS, REG_DWORD, &dwArbitrary, sizeof(DWORD));

        dwArbitrary = m_bPreload;
        registry->SetValue(hKey, CONFIG_PRELOAD, REG_DWORD, &dwArbitrary, sizeof(DWORD));

        dwArbitrary = g_oNethoodCache.GetDisplayTSClientVolumes();
        registry->SetValue(hKey, CONFIG_TSCVOLUMES, REG_DWORD, &dwArbitrary, sizeof(DWORD));
    }
}

void WINAPI
CNethoodPluginInterface::Configuration(
    __in HWND hwndParent)
{
    CALL_STACK_MESSAGE1("CNethoodPluginInterface::Configuration()");

    CNethoodConfigDialog(hwndParent).Execute();
}

void WINAPI
CNethoodPluginInterface::Connect(
    __in HWND parent,
    __in CSalamanderConnectAbstract* salamander)
{
    std::wstring fileMenu = L",\t";
    int iIcon = -1;

    fileMenu.append(SPLLoadStrOwned(SalamanderGeneral, GetLangInstance(), IDS_MENUITEM).c_str());

    g_oIcons.Load();

    HICON hIcon = g_oIcons.GetIcon(SALICONSIZE_16, CNethoodIcons::IconMain);
    assert(hIcon != NULL);
    if (hIcon != NULL)
    {
        CGUIIconListAbstract* pIconList;

        pIconList = SalamanderGUI->CreateIconList();
        assert(pIconList != NULL);
        if (pIconList->Create(16, 16, 1))
        {
            if (pIconList->ReplaceIcon(0, hIcon))
            {
                salamander->SetIconListForGUI(pIconList);
                iIcon = 0;
            }
            else
            {
                assert(0);
                SalamanderGUI->DestroyIconList(pIconList);
            }
        }
        else
        {
            assert(0);
            SalamanderGUI->DestroyIconList(pIconList);
        }

        DestroyIcon(hIcon);
    }

    assert(iIcon >= 0);
    salamander->SetPluginIcon(iIcon);
    salamander->SetChangeDriveMenuItem(fileMenu.c_str(), iIcon);
    salamander->SetPluginMenuAndToolbarIcon(-1);

    if (m_bPreload)
    {
        g_oNethoodCache.GetPathStatus(L"\\", NULL, NULL);
    }
}

void WINAPI
CNethoodPluginInterface::ReleasePluginDataInterface(
    __in CPluginDataInterfaceAbstract* pluginData)
{
    CNethoodPluginDataInterface* pNethoodData;

    TRACE_I("ReleasePluginDataInterface(" << pluginData << ")");

    // Typecast to correct destructor be called.
    pNethoodData = static_cast<CNethoodPluginDataInterface*>(pluginData);
    delete pNethoodData;
}

CPluginInterfaceForArchiverAbstract* WINAPI
CNethoodPluginInterface::GetInterfaceForArchiver()
{
    return NULL;
}

CPluginInterfaceForViewerAbstract* WINAPI
CNethoodPluginInterface::GetInterfaceForViewer()
{
    return NULL;
}

CPluginInterfaceForMenuExtAbstract* WINAPI
CNethoodPluginInterface::GetInterfaceForMenuExt()
{
    return &g_oMenuExt;
}

CPluginInterfaceForFSAbstract* WINAPI
CNethoodPluginInterface::GetInterfaceForFS()
{
    return &g_oNethoodFS;
}

CPluginInterfaceForThumbLoaderAbstract* WINAPI
CNethoodPluginInterface::GetInterfaceForThumbLoader()
{
    return NULL;
}

void WINAPI
CNethoodPluginInterface::Event(
    __in int event,
    __in DWORD param)
{
}

void WINAPI
CNethoodPluginInterface::ClearHistory(
    __in HWND parent)
{
}

void WINAPI
CNethoodPluginInterface::AcceptChangeOnPathNotification(
    __in const wchar_t* path,
    __in BOOL includingSubdirs)
{
}

void WINAPI
CNethoodPluginInterface::PasswordManagerEvent(
    __in HWND parent,
    __in int event)
{
}

void CNethoodPluginInterface::SetPreloadFlag(bool bPreload)
{
    m_bPreload = bPreload;
    SalamanderGeneral->SetFlagLoadOnSalamanderStart(bPreload);
}
