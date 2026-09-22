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

/**
	Cache of network.
*/
extern CNethoodCache g_oNethoodCache;

CNethoodPluginInterfaceForFS::CNethoodPluginInterfaceForFS()
{
    m_cActiveFS = 0;
}

CNethoodPluginInterfaceForFS::~CNethoodPluginInterfaceForFS()
{
    assert(m_cActiveFS == 0);
}

CPluginFSInterfaceAbstract* WINAPI
CNethoodPluginInterfaceForFS::OpenFS(
    __in const wchar_t* fsName,
    __in int fsNameIndex)
{
    ++m_cActiveFS;
    return new CNethoodFSInterface();
}

void WINAPI
CNethoodPluginInterfaceForFS::CloseFS(
    __in CPluginFSInterfaceAbstract* pluginFS)
{
    --m_cActiveFS;

    // Typecast to call the correct destructor.
    CNethoodFSInterface* fs = static_cast<CNethoodFSInterface*>(pluginFS);

    if (fs != NULL)
    {
        delete fs;
    }
}

void WINAPI
CNethoodPluginInterfaceForFS::ExecuteChangeDriveMenuItem(
    __in int panel)
{
    CALL_STACK_MESSAGE2("CNethoodPluginInterfaceForFS::ExecuteChangeDriveMenuItem(%d)", panel);

    BOOL changeRes;
    int failReason;

    changeRes = SalamanderGeneral->ChangePanelPathToPluginFS(
        panel,
        g_assignedFSName.c_str(),
        L"",
        &failReason);
}

BOOL WINAPI
CNethoodPluginInterfaceForFS::ChangeDriveMenuItemContextMenu(
    __in HWND parent,
    __in int panel,
    __in int x,
    __in int y,
    __in CPluginFSInterfaceAbstract* pluginFS,
    __in const wchar_t* pluginFSName,
    __in int pluginFSNameIndex,
    __in BOOL isDetachedFS,
    __out BOOL& refreshMenu,
    __out BOOL& closeMenu,
    __out int& postCmd,
    __out void*& postCmdParam)
{
    return FALSE;
}

void WINAPI
CNethoodPluginInterfaceForFS::ExecuteChangeDrivePostCommand(
    __in int panel,
    __in int postCmd,
    __in void* postCmdParam)
{
}

void WINAPI
CNethoodPluginInterfaceForFS::ExecuteOnFS(
    __in int panel,
    __in CPluginFSInterfaceAbstract* pluginFS,
    __in const wchar_t* pluginFSName,
    __in int pluginFSNameIndex,
    __in CFileData& file,
    __in int isDir)
{
    // GetCurrentPathW is CNethoodFSInterface's real implementation now
    // (that class's wide-FS-interface-ABI conversion); calling it directly here instead of
    // the narrow GetCurrentPath wrapper removes every narrow-then-rewiden bridge this
    // function used to need.
    std::wstring newPath;
    CNethoodFSInterface* pFSInterface;
    CNethoodCache::Node node;

    pFSInterface = reinterpret_cast<CNethoodFSInterface*>(pluginFS);
    node = pFSInterface->GetNodeFromFileData(file);
    assert(node != NULL || isDir == 2);

    if (!pFSInterface->GetCurrentPathOwned(newPath))
        return;

    if (isDir == 2)
    {
        // It's the up-dir.

        // Trim off the last path component...
        if (newPath.size() >= 2 && newPath[0] == L'\\' && newPath[1] == L'\\')
        {
            // Is a UNC path - go back to root.
            //pFSInterface->GetRootPath(szNewPath);
            pFSInterface = NULL; // Pointer may be invalid after ChangePanelPathToXxx
            SalamanderGeneral->ChangePanelPathToPluginFS(
                panel, pluginFSName, L"", NULL, -1, newPath.c_str() + 2);
        }
        else
        {
            const size_t slash = newPath.find_last_of(L'\\');
            if (slash != std::wstring::npos)
            {
                const std::wstring cutDir = newPath.substr(slash + 1);
                newPath.erase(slash);
                // ...and change the path.
                pFSInterface = NULL; // Pointer may be invalid after ChangePanelPathToXxx
                SalamanderGeneral->ChangePanelPathToPluginFS(
                    panel, pluginFSName, newPath.c_str(), NULL,
                    -1, cutDir.c_str());
            }
        }
    }
    else
    {
        // It's a subdirectory (isDir == 1) or a "file"
        // (ie. a server or a share) (isDir == 0).

        const CNethoodCacheNode& nodeData = g_oNethoodCache.GetItemData(node);

        if (nodeData.GetType() == CNethoodCacheNode::TypeShare ||
            nodeData.GetType() == CNethoodCacheNode::TypeTSCVolume)
        {
            // It's a share. Change path to Salamander.
            pFSInterface = NULL; // Pointer may be invalid after ChangePanelPathToXxx

            // SalamanderGeneral->SetUserWorkedOnPanelPath(panel);  // Petr Solin: I think this is not the reason to add current path to List Of Working Directories (Alt+F12)

            if (!SalamanderGeneral->ChangePanelPathToDisk(panel, nodeData.GetName()))
            {
                // May be access denied or so...
                // Do nothing - stay on the current nethood path.
            }
        }
        else if (nodeData.GetType() == CNethoodCacheNode::TypeServer)
        {
            // It's a server.

            if (pFSInterface->IsRootPath(newPath.c_str()))
            {
                pFSInterface = NULL; // Pointer may be invalid after ChangePanelPathToXxx
                SalamanderGeneral->ChangePanelPathToPluginFS(
                    panel, pluginFSName, nodeData.GetName());
            }
            else
            {
                // Combine the path...
                if (!newPath.empty() && newPath.back() != L'\\')
                    newPath.push_back(L'\\');
                newPath.append(file.Name);
                pFSInterface = NULL; // Pointer may be invalid after ChangePanelPathToXxx
                SalamanderGeneral->ChangePanelPathToPluginFS(
                    panel, pluginFSName, newPath.c_str());
            }
        }
        else
        {
            // Combine the path...
            if (!newPath.empty() && newPath.back() != L'\\')
                newPath.push_back(L'\\');
            newPath.append(file.Name);
            pFSInterface = NULL; // Pointer may be invalid after ChangePanelPathToXxx
            SalamanderGeneral->ChangePanelPathToPluginFS(
                panel, pluginFSName, newPath.c_str());
        }
    }
}

BOOL WINAPI
CNethoodPluginInterfaceForFS::DisconnectFS(
    __in HWND parent,
    __in BOOL isInPanel,
    __in int panel,
    __in CPluginFSInterfaceAbstract* pluginFS,
    __in const wchar_t* pluginFSName,
    __in int pluginFSNameIndex)
{
    BOOL ret = FALSE;

    CALL_STACK_MESSAGE4("CNethoodPluginInterfaceForFS::DisconnectFS(, %d, %d, , , %d)",
                        isInPanel, panel, pluginFSNameIndex);

    //((CPluginFSInterface *)pluginFS)->CalledFromDisconnectDialog = TRUE; // suppress unnecessary prompts (the user issued a disconnect command, we just perform it)

    if (isInPanel)
    {
        SalamanderGeneral->DisconnectFSFromPanel(parent, panel);
        ret = SalamanderGeneral->GetPanelPluginFS(panel) != pluginFS;
    }
    else
    {
        ret = SalamanderGeneral->CloseDetachedFS(parent, pluginFS);
    }

    if (!ret)
    {
        //((CPluginFSInterface *)pluginFS)->CalledFromDisconnectDialog = FALSE; // stop suppressing unnecessary prompts
    }

    return ret;
}

BOOL WINAPI
CNethoodPluginInterfaceForFS::ConvertPathToInternal(
    __in const wchar_t* fsName,
    __in int fsNameIndex,
    __inout CSalamanderStringBuffer* fsUserPart)
{
    return fsUserPart != NULL &&
           sally::plugin_abi::IsValidStringBuffer(*fsUserPart);
}

BOOL WINAPI
CNethoodPluginInterfaceForFS::ConvertPathToExternal(
    __in const wchar_t* fsName,
    __in int fsNameIndex,
    __inout CSalamanderStringBuffer* fsUserPart)
{
    return fsUserPart != NULL &&
           sally::plugin_abi::IsValidStringBuffer(*fsUserPart);
}

void WINAPI
CNethoodPluginInterfaceForFS::EnsureShareExistsOnServer(
    __in int iPanel,
    __in const wchar_t* server,
    __in const wchar_t* share)
{
    UINT uError;

    assert(server != NULL);

    // EnsurePathExists is wide now (cache.h widened) - the narrow
    // ToNarrowDisplay bridge this comment used to describe is obsolete, use server/share
    // directly.
    std::wstring uncPath = L"\\\\";
    uncPath.append(server);
    if (share != NULL)
    {
        uncPath.push_back(L'\\');
        uncPath.append(share);
    }

    uError = g_oNethoodCache.EnsurePathExists(uncPath.c_str());
    assert(uError == NO_ERROR);

    g_iFocusSharePanel = iPanel;
    // Shift iPanel to zero-based value.
    iPanel -= PANEL_LEFT;
    assert(iPanel >= 0 && iPanel < 2);
    g_focusShareNames[iPanel] = share != NULL ? share : L"";
}
