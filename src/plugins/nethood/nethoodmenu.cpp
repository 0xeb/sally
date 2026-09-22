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
#include "nethoodfs.h"
#include "cache.h"
#include "nethood_fs_operations.h"
#include "globals.h"
#include "nethoodmenu.h"
#include "nethood.rh"
#include "nethood.rh2"
#include "lang\lang.rh"

/// Global network cache instance.
extern CNethoodCache g_oNethoodCache;

DWORD WINAPI
CNethoodPluginInterfaceForMenuExt::GetMenuItemState(
    __in int id,
    __in DWORD eventMask)
{
    UNREFERENCED_PARAMETER(id);
    UNREFERENCED_PARAMETER(eventMask);

    return MENU_ITEM_STATE_HIDDEN;
}

BOOL WINAPI
CNethoodPluginInterfaceForMenuExt::ExecuteMenuItem(
    __in CSalamanderForOperationsAbstract* salamander,
    __in HWND parent,
    __in int id,
    __in DWORD eventMask)
{
    UNREFERENCED_PARAMETER(salamander);
    UNREFERENCED_PARAMETER(parent);
    UNREFERENCED_PARAMETER(eventMask);

    switch (id)
    {
    case MENUCMD_REDIRECT_LEFT:
    case MENUCMD_REDIRECT_RIGHT:
    {
        int iPanel;

        iPanel = id - MENUCMD_REDIRECT_BASE + PANEL_LEFT;
        assert(iPanel == PANEL_LEFT || iPanel == PANEL_RIGHT);

        // Paranoid safety check (should always be true).
        if (SalamanderGeneral->GetPanelPluginFS(iPanel) != NULL)
        {
            // Remove the current path from the directory history
            // and from the List of Working Directories (Alt+F12).
            SalamanderGeneral->RemoveCurrentPathFromHistory(iPanel);
        }

        // The redirect path is wide now - the narrow bridge this
        // comment used to describe is obsolete.
        if (!SalamanderGeneral->ChangePanelPath(
                iPanel,
                g_redirectPaths[id - MENUCMD_REDIRECT_BASE].c_str()))
        {
            // FIXME: Go back to last cache path
            assert(0);
            SalamanderGeneral->ChangePanelPathToRescuePathOrFixedDrive(iPanel);
        }

        return FALSE;
    }

    case MENUCMD_START_THROBBER:
    {
        int iPop;
        CNethoodCache::Node node;
        CNethoodFSInterface *pNethoodLeft, *pNethoodRight;
        std::wstring toolTip;

        g_oNethoodCache.LockCache();

        iPop = (g_iPostedThrobberQueue > 0) ? (g_iPostedThrobberQueue - 1) : (POSTED_THROBBER_QUEUE_LEN - 1);
        node = reinterpret_cast<CNethoodCache::Node>(g_adwPostedThrobberQueue[iPop]);

        assert(node != NULL);

        // Check whether the specified node is still displayed
        // in the left and/or right panel.
        pNethoodLeft = CheckThrobberForPanel(PANEL_LEFT, node);
        pNethoodRight = CheckThrobberForPanel(PANEL_RIGHT, node);

        g_oNethoodCache.ReleaseNode(node);

        g_oNethoodCache.UnlockCache();

        if (pNethoodLeft || pNethoodRight)
        {
            // LoadStr is wide-native; the narrow LoadStrNarrow ->
            // ToWideArg round-trip this used to do was pure unmigrated debt (same shape
            // as the SetRedirectPath chain fixed earlier), not narrow-by-design.
            toolTip = SPLLoadStrOwned(SalamanderGeneral, GetLangInstance(), IDS_REFRESHING);

            if (pNethoodLeft)
            {
                SalamanderGeneral->StartThrobber(
                    PANEL_LEFT,
                    toolTip.c_str(),
                    THROBBER_GRACE_PERIOD);
            }

            if (pNethoodRight)
            {
                SalamanderGeneral->StartThrobber(
                    PANEL_RIGHT,
                    toolTip.c_str(),
                    THROBBER_GRACE_PERIOD);
            }
        }

        return FALSE;
    }

    case MENUCMD_FOCUS_SHARE_LEFT:
    case MENUCMD_FOCUS_SHARE_RIGHT:
    {
        int iPanel;
        int iName;
        wchar_t szTrueDisplayName[128];
        CTsClientName oClientName(&g_oNethoodCache);
        CTsNameFormatter oFormatter;
        CNethoodCache::TSCDisplayMode displayMode;

        iName = id - MENUCMD_FOCUS_SHARE_BASE;
        iPanel = iName + PANEL_LEFT;
        assert(iPanel == PANEL_LEFT || iPanel == PANEL_RIGHT);

        if (!g_focusShareNames[iName].empty())
        {
            displayMode = g_oNethoodCache.GetDisplayTSClientVolumes();
            assert(displayMode != CNethoodCache::TSCDisplayNone);

            oFormatter.Format(
                displayMode,
                g_focusShareNames[iName].c_str(),
                &oClientName,
                szTrueDisplayName,
                COUNTOF(szTrueDisplayName));

            // szTrueDisplayName is wide now (CTsNameFormatter::Format
            // widened along with cache.h) - the narrow-bridge this comment used to describe
            // is obsolete, pass it straight through.
            FocusItemInPanel(iPanel, szTrueDisplayName);

            g_focusShareNames[iName].clear();
        }

        return FALSE;
    }

    default:
        assert(0);
    }

    return FALSE;
}

BOOL WINAPI
CNethoodPluginInterfaceForMenuExt::HelpForMenuItem(
    __in HWND parent,
    __in int id)
{
    UNREFERENCED_PARAMETER(parent);
    UNREFERENCED_PARAMETER(id);

    return FALSE;
}

void WINAPI
CNethoodPluginInterfaceForMenuExt::BuildMenu(
    __in HWND parent,
    __in CSalamanderBuildMenuAbstract* salamander)
{
    UNREFERENCED_PARAMETER(parent);
    UNREFERENCED_PARAMETER(salamander);
}

CNethoodFSInterface* CNethoodPluginInterfaceForMenuExt::CheckThrobberForPanel(
    __in int iPanel,
    __in CNethoodCache::Node node)
{
    CNethoodFSInterface* pNethood;

    pNethood = static_cast<CNethoodFSInterface*>(SalamanderGeneral->GetPanelPluginFS(iPanel));

    if (pNethood != NULL)
    {
        if (pNethood->GetCurrentNode() != node)
        {
            pNethood = NULL;
        }
    }

    return pNethood;
}

bool CNethoodPluginInterfaceForMenuExt::FocusItemInPanel(
    __in int iPanel,
    __in const wchar_t* pszItemName)
{
    const CFileData* pFile;
    int iFile = 0;

    while ((pFile = SalamanderGeneral->GetPanelItem(iPanel, &iFile, NULL)) != NULL)
    {
        if (wcscmp(pFile->Name, pszItemName) == 0)
        {
            SalamanderGeneral->SetPanelFocusedItem(iPanel, pFile, FALSE);
            return true;
        }
    }

    return false;
}
