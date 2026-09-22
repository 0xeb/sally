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
#include "nethooddata.h"
#include "globals.h"
#include "icons.h"
#include "nethood.rh"
#include "nethood.rh2"
#include "lang\lang.rh"

#include <algorithm>
#include <vector>

// CNethoodFSInterface's own current/redirect/accessible paths
// are dynamically owned UTF-16 (this file's own wide-FS-interface-ABI conversion),
// so cache.h's path accessors (GetFullPath/FindAccessiblePath/GetUncPath/GetPathStatus/
// EnsurePathExists) are called directly - no narrow bridge needed for them any more.
// The *W methods below (GetCurrentPathW/GetFullNameW/GetRootPathW/IsCurrentPathW/
// IsOurPathW/ChangePathW) are the real implementation; the narrow forms required by
// CPluginFSInterfaceAbstract's mandatory ABI are now thin best-effort ANSI wrappers
// around them - same pattern as portables' CFxPluginFSInterface and undelete's
// CPluginFSInterface.

/// Cache of network.
extern CNethoodCache g_oNethoodCache;

/// Icon cache.
extern CNethoodIcons g_oIcons;

bool CNethoodFSInterface::s_bHideServersInRoot;

CNethoodFSInterface::CNethoodFSInterface()
{
    // Initial path is root path ("net:\")
    m_currentPath = L"\\";

    m_pathNode = NULL;
    m_pPathNodeEventConsumer = NULL;
    m_state = FSStateNormal;
    m_bIgnoreForceRefresh = false;
    m_dwEnumerationResult = ERROR_SUCCESS;
    m_bManualEntry = false;

    m_bShowThrobber = false;
    m_iThrobberID = -1;
}

CNethoodFSInterface::~CNethoodFSInterface()
{
    if (m_pPathNodeEventConsumer != NULL)
    {
        assert(m_pathNode != NULL);
        g_oNethoodCache.UnregisterConsumer(m_pathNode, m_pPathNodeEventConsumer);
        g_oNethoodCache.ReleaseNode(m_pathNode);
        delete m_pPathNodeEventConsumer;
        m_pPathNodeEventConsumer = NULL;
    }
}

BOOL WINAPI
CNethoodFSInterface::GetCurrentPath(CSalamanderStringBuffer* userPart)
{
    std::wstring currentPath;
    if (!GetCurrentPathOwned(currentPath))
        return FALSE;
    return userPart != NULL &&
           sally::plugin_abi::WriteStringBuffer(*userPart, currentPath);
}

BOOL CNethoodFSInterface::GetCurrentPathOwned(std::wstring& userPart)
{
    if (m_state == FSStateImmediateRefresh)
    {
        // The panel is about to be refreshed immediately.
        // That means the cache node was updated and the path may have changed.
        g_oNethoodCache.LockCache();
        g_oNethoodCache.GetFullPath(m_pathNode, m_currentPath);
        g_oNethoodCache.UnlockCache();
    }

    userPart = m_currentPath;
    return TRUE;
}

BOOL WINAPI
CNethoodFSInterface::GetFullName(
    __in CFileData& file,
    __in int isDir,
    CSalamanderStringBuffer* fullNameBuffer)
{
    std::wstring result = m_currentPath;

    if (isDir == 2)
    {
        // up-dir

        if (result.size() >= 2 && result[0] == L'\\' && result[1] == L'\\')
        {
            // UNC path, go to root
            result = L"\\";
        }
        else
        {
            const size_t slash = result.find_last_of(L'\\');
            if (slash == std::wstring::npos)
                return FALSE;
            result.erase(slash);
        }
    }
    else
    {
        if (m_currentPath == L"\\")
        {
            if (GetNodeTypeFromFileData(file) == CNethoodCacheNode::TypeServer)
                result = L"\\\\";
            else
                result = L"\\";
        }
        else if (!result.empty() && result.back() != L'\\')
            result.push_back(L'\\');
        result.append(file.Name);
    }

    return fullNameBuffer != NULL &&
           sally::plugin_abi::WriteStringBuffer(*fullNameBuffer, result);
}

BOOL WINAPI
CNethoodFSInterface::GetFullFSPath(
    HWND parent,
    const wchar_t* fsName,
    CSalamanderStringBuffer* path,
    BOOL& success)
{
    // FIXME
    success = FALSE;
    return FALSE;
}

BOOL WINAPI
CNethoodFSInterface::GetRootPath(CSalamanderStringBuffer* userPart)
{
    return userPart != NULL &&
           sally::plugin_abi::WriteStringBuffer(*userPart, std::wstring(L"\\"));
}

BOOL WINAPI
CNethoodFSInterface::IsCurrentPath(
    int currentFSNameIndex,
    int fsNameIndex,
    const wchar_t* userPart)
{
    return (currentFSNameIndex == fsNameIndex) &&
           SalamanderGeneral->IsTheSamePath(m_currentPath.c_str(), userPart);
}

BOOL WINAPI
CNethoodFSInterface::IsOurPath(
    int currentFSNameIndex,
    int fsNameIndex,
    const wchar_t* userPart)
{
    // It's always our path.
    return TRUE;
}

BOOL WINAPI
CNethoodFSInterface::ChangePath(
    int currentFSNameIndex,
    CSalamanderStringBuffer* fsName,
    int fsNameIndex,
    const wchar_t* userPart,
    CSalamanderStringBuffer* cutFileName,
    BOOL* pathWasCut,
    BOOL forceRefresh,
    int mode)
{
    UINT uError;
    std::wstring path;
    std::wstring correctedUserPart(userPart != NULL ? userPart : L"");
    std::wstring fsNameValue;
    if (fsName == NULL || !sally::plugin_abi::ReadStringBuffer(*fsName, fsNameValue))
        return FALSE;
    (void)fsNameValue;

    // mode parameter semantics:
    //   1 (refresh path) - shorten the path when necessary without reporting that it disappeared.
    //                      Still report other issues such as getting a file instead of a directory
    //                      or an inaccessible path.
    //   2 (ChangePanelPathToPluginFS call, history navigation, and similar) - shorten the path when
    //                      necessary and report every path-related error (file instead of directory,
    //                      a missing path, inaccessibility, and so on).
    //   3 (change-dir command) - shorten the path only when it points to a file or when the path cannot
    //                      be listed (ListCurrentPath returned FALSE). Suppress only the "file instead of
    //                      directory" warning by shortening silently and returning the file name; report
    //                      all other path errors (missing, inaccessible, and so on).
    // If mode is 1 or 2, return FALSE only when no path on this file system is accessible (for example,
    // when the connection is down). If mode is 3, return FALSE when the requested path or file is not
    // accessible (the path is shortened only when it points to a file).
    // When opening the file system is time-consuming (for example, connecting to an FTP server) and mode
    // is 3, treat it like an archive: shorten the path when necessary and return FALSE only when no path
    // on the file system is accessible; error reporting stays the same.

    CALL_STACK_MESSAGE4("CNethoodFSInterface::ChangePath(, , , %ls, , , %d, %d)", userPart, forceRefresh, mode);

    // Replace forward slashes with backslashes.
    std::replace(correctedUserPart.begin(), correctedUserPart.end(), L'/', L'\\');
    userPart = correctedUserPart.c_str();

    if (mode != 3 && (pathWasCut != NULL || cutFileName != NULL))
    {
        TRACE_E("Incorrect value of 'mode' in CPluginFSInterface::ChangePath().");
        mode = 3;
    }

    if (pathWasCut != NULL)
        *pathWasCut = FALSE;

    if (cutFileName != NULL &&
        !sally::plugin_abi::WriteStringBuffer(*cutFileName, std::wstring()))
        return FALSE;

    if (userPart[0] == L'\0')
    {
        m_currentPath = L"\\";
        return TRUE;
    }
    else if (userPart[0] == L'\\' && userPart[1] == L'\\' && userPart[2] == L'\0')
    {
        m_currentPath = L"\\";
        return TRUE;
    }
    else
    {
        if (userPart[0] != L'\\')
            path.push_back(L'\\');
        path.append(userPart);
    }

    if (m_state == FSStateDisplayError)
    {
        assert(m_dwEnumerationResult != ERROR_SUCCESS);
        assert(!m_accessiblePath.empty());

        path = m_accessiblePath;
        m_accessiblePath.clear();
        DisplayError(m_dwEnumerationResult);
        m_dwEnumerationResult = ERROR_SUCCESS;
        m_state = FSStateNormal;
    }
    else if (m_state == FSStateAsyncRedirect)
    {
        if (!PostRedirectPathToSalamander(m_accessiblePath.c_str()))
        {
            // The asynchronous redirect to the accessible path found by
            // the enumeration thread failed. Try to shorten
            // the path. Note that we cannot use FindAccessiblePath()
            // because the cache node is NULL!

            path = m_accessiblePath;
            const size_t lastSlash = path.find_last_of(L'\\');
            assert(lastSlash != std::wstring::npos && lastSlash > 2);
            if (lastSlash != std::wstring::npos)
                path.erase(lastSlash);

            m_state = FSStateNormal;
        }

        m_accessiblePath.clear();
    }
    else
    {
        uError = CUncPathParser::Validate(path.c_str());
        if (uError == ERROR_NETHOODCACHE_FULL_UNC_PATH)
        {
            if (!PostRedirectPathToSalamander(path.c_str()))
            {
                return FALSE;
            }
        }
        else if (uError != NO_ERROR)
        {
            m_state = FSStateFindAccessible;
            if (mode == 2 || mode == 3)
            {
                DisplayError(uError);
                if (mode == 3)
                {
                    return FALSE;
                }
            }
        }
    }

    if (m_state == FSStateFindAccessible)
    {
        if (m_pathNode != NULL && !IsRootPath(m_currentPath.c_str()))
        {
            g_oNethoodCache.LockCache();
            g_oNethoodCache.FindAccessiblePath(m_pathNode, m_currentPath);
            g_oNethoodCache.UnlockCache();
        }
        else
        {
            m_currentPath = L"\\";
        }

        if (pathWasCut != NULL)
        {
            *pathWasCut = TRUE;
        }
        if (cutFileName != NULL)
        {
            if (IsRootPath(m_currentPath.c_str()))
            {
                if (!sally::plugin_abi::WriteStringBuffer(*cutFileName, std::wstring()))
                    return FALSE;
            }
            else
            {
                if (!sally::plugin_abi::WriteStringBuffer(*cutFileName, m_currentPath))
                    return FALSE;
            }
        }

        m_state = FSStateNormal;

        TRACE_IW(L"Nethood: ChangePath: Found accessible path " << m_currentPath);

        return TRUE;
    }

    if (!IsRootPath(path.c_str()))
    {
        // Trim the backslash at the end of the path, but only
        // if it's not the root.
        if (!path.empty() && path.back() == L'\\')
            path.pop_back();
    }

    m_currentPath = std::move(path);

    TRACE_IW(L"Nethood: ChangePath: Path changed to " << m_currentPath);

    if (mode != 1 && m_state == FSStateImmediateRefresh)
    {
        m_state = FSStateNormal;
    }

    if (m_state == FSStateSymLink)
    {
        m_state = FSStateNormal;
    }

    m_bManualEntry = (mode == 3);

    return TRUE;
}

bool CNethoodFSInterface::NethoodNodeToFileData(
    __in CNethoodCache::Node node,
    __out CFileData& file)
{
    const CNethoodCacheNode& nodeData = g_oNethoodCache.GetItemData(node);

    if (nodeData.GetType() == CNethoodCacheNode::TypeServer &&
        s_bHideServersInRoot &&
        IsRootPath(m_currentPath.c_str()))
    {
        return false;
    }

    memset(&file, 0, sizeof(CFileData));
    // GetDisplayName() is wide now (cache.h widened) - the narrow-bridge
    // this comment used to describe is obsolete.
    file.Name = SalamanderGeneral->DupStr(nodeData.GetDisplayName());
    file.NameLen = static_cast<unsigned>(wcslen(file.Name));
    file.Ext = file.Name + file.NameLen;
    g_oNethoodCache.AddRefNode(node);
    file.PluginData = reinterpret_cast<DWORD_PTR>(node);
    assert(file.PluginData != 0);
    if (nodeData.IsContainer() && nodeData.GetType() != CNethoodCacheNode::TypeServer)
    {
        // We treat servers as "files" because Salamander offers limited
        // sorting options.
        file.Attr |= FILE_ATTRIBUTE_DIRECTORY;
    }

    if (nodeData.IsHidden())
    {
        file.Attr |= FILE_ATTRIBUTE_HIDDEN;
        file.Hidden = 1;
    }

#if 0
	if (nodeData.IsShortcut())
	{
		file.IsLink = 1;
	}
#endif

    return true;
}

CNethoodCache::Node CNethoodFSInterface::GetNodeFromFileData(
    __in const CFileData& file)
{
    return reinterpret_cast<CNethoodCache::Node>(static_cast<DWORD_PTR>(file.PluginData));
}

CNethoodCacheNode::Type CNethoodFSInterface::GetNodeTypeFromFileData(
    __in const CFileData& file)
{
    CNethoodCache::Node node;

    node = GetNodeFromFileData(file);
    if (node != NULL)
    {
        return g_oNethoodCache.GetItemData(node).GetType();
    }

    assert(0);
    return CNethoodCacheNode::TypeGeneric;
}

BOOL WINAPI
CNethoodFSInterface::ListCurrentPath(
    __in CSalamanderDirectoryAbstract* dir,
    __out CPluginDataInterfaceAbstract*& pluginData,
    __out int& iconsType,
    __in BOOL forceRefresh)
{
    m_bShowThrobber = false;

    UINT uError = NO_ERROR;
    bool bAddUpDir = false;
    CFileData fileData;
    CNethoodPluginDataInterface* pDataInterface;
    std::wstring targetPath;

    TRACE_IW(L"Nethood: ListCurrentPath (path=" << m_currentPath << L", state=" << m_state << L")");

    pDataInterface = new CNethoodPluginDataInterface();
    TRACE_I("dataInterface = " << pDataInterface);
    pluginData = pDataInterface;
    iconsType = pitFromPlugin;

    // This will invalidate the "file" extension and remove the Ext column.
    dir->SetValidData(VALID_DATA_ATTRIBUTES | VALID_DATA_HIDDEN | VALID_DATA_ISLINK);

    g_oNethoodCache.LockCache();

    if (m_bIgnoreForceRefresh)
    {
        forceRefresh = FALSE;
        m_bIgnoreForceRefresh = false;
    }

    if (m_state == FSStateRedirect)
    {
        m_state = FSStateNormal;

        // SetRedirectPath is wide now - the narrow-then-rewiden
        // round-trip this comment used to describe is obsolete (the current-path family
        // and the redirect-path consumer in nethoodmenu.cpp were already wide/bridging
        // back to wide, so this was pure unmigrated debt, not narrow-by-design).
        pDataInterface->SetRedirectPath(m_redirectPath);
        m_redirectPath.clear();

        g_oNethoodCache.UnlockCache();

        return TRUE;
    }
    else if (m_state == FSStateImmediateRefresh)
    {
        uError = g_oNethoodCache.GetItemData(m_pathNode).GetLastEnumerationResult();
        if (uError == -1 || g_oNethoodCache.GetItemData(m_pathNode).GetStatus() == CNethoodCacheNode::StatusPending)
        {
            uError = ERROR_IO_PENDING;
        }
        m_state = FSStateNormal;
        if (uError == NO_ERROR || uError == ERROR_IO_PENDING)
        {
            g_oNethoodCache.ReleaseNode(m_pathNode);
        }
    }
    else
    {
        if (m_pPathNodeEventConsumer != NULL)
        {
            assert(m_pathNode != NULL);
            g_oNethoodCache.UnregisterConsumer(m_pathNode, m_pPathNodeEventConsumer);
            g_oNethoodCache.ReleaseNode(m_pathNode);
            delete m_pPathNodeEventConsumer;
            m_pPathNodeEventConsumer = NULL;
        }

        unsigned uFlags = 0;

        if (forceRefresh)
        {
            uFlags |= CNethoodCache::GPSF_FORCE;
        }

        if (m_bManualEntry)
        {
            uFlags |= CNethoodCache::GPSF_MANUAL;
        }

        m_pPathNodeEventConsumer = new CNethoodFSCacheConsumer(this);
        uError = g_oNethoodCache.GetPathStatus(
            m_currentPath.c_str(),
            m_pPathNodeEventConsumer,
            &m_pathNode,
            uFlags,
            &targetPath);

        if (uError == ERROR_NETHOODCACHE_FULL_UNC_PATH)
        {
            delete m_pPathNodeEventConsumer;
            m_pPathNodeEventConsumer = NULL;

            if (!PostRedirectPathToSalamander(targetPath.c_str()))
            {
                m_state = FSStateFindAccessible;
                g_oNethoodCache.UnlockCache();
                return FALSE;
            }

            if (m_state == FSStateRedirect)
            {
                m_state = FSStateNormal;

                // SetRedirectPath is wide now - targetPath is
                // already wide, pass it straight through.
                pDataInterface->SetRedirectPath(targetPath);
                m_redirectPath.clear();
            }

            g_oNethoodCache.UnlockCache();

            return TRUE;
        }
        else if (uError == ERROR_NETHOODCACHE_SYMLINK)
        {
            delete m_pPathNodeEventConsumer;
            m_pPathNodeEventConsumer = NULL;

            m_state = FSStateSymLink;

            m_currentPath = targetPath;

            g_oNethoodCache.UnlockCache();

            SalamanderGeneral->PostMenuExtCommand(
                MENUCMD_FOCUS_SHARE_BASE + g_iFocusSharePanel - PANEL_LEFT,
                TRUE);
            g_iFocusSharePanel = 0;

            delete pDataInterface;
            pluginData = NULL;

            return FALSE;
        }
    }

    if (uError == NO_ERROR || uError == ERROR_IO_PENDING)
    {
        CNethoodCache::Node nodeItem;

        g_oNethoodCache.AddRefNode(m_pathNode);

        g_oNethoodCache.GetFullPath(m_pathNode, m_currentPath);

        assert(m_pathNode != NULL);
        nodeItem = g_oNethoodCache.GetFirstItem(m_pathNode);
        while (nodeItem != NULL)
        {
            if (NethoodNodeToFileData(nodeItem, fileData))
            {
                if ((fileData.Attr & FILE_ATTRIBUTE_DIRECTORY) != 0)
                {
                    dir->AddDir(NULL, fileData, NULL);
                }
                else
                {
                    dir->AddFile(NULL, fileData, NULL);
                }
            }

            nodeItem = g_oNethoodCache.GetNextItem(nodeItem);
        }

        m_state = FSStateNormal;
        bAddUpDir = true;

        if (uError == ERROR_IO_PENDING ||
            g_oNethoodCache.GetItemData(m_pathNode).GetStatus() == CNethoodCacheNode::StatusPending)
        {
            StartThrobber();
        }
    }
    else if (uError == ERROR_NO_MORE_ITEMS)
    {
        bAddUpDir = true;
    }
    else
    {
        if (m_pathNode != NULL)
        {
            g_oNethoodCache.UnregisterConsumer(m_pathNode, m_pPathNodeEventConsumer);
        }
        delete m_pPathNodeEventConsumer;
        m_pPathNodeEventConsumer = NULL;
        m_state = FSStateFindAccessible;

        g_oNethoodCache.UnlockCache();

        DisplayError(uError);

        delete pDataInterface;
        pluginData = NULL;

        return FALSE;
    }

    g_oNethoodCache.UnlockCache();

    if (bAddUpDir && !IsRootPath(m_currentPath.c_str()))
    {
        memset(&fileData, 0, sizeof(CFileData));
        fileData.Name = SalamanderGeneral->DupStr(L"..");
        fileData.NameLen = 2;
        fileData.Ext = fileData.Name + fileData.NameLen;
        fileData.Attr |= FILE_ATTRIBUTE_DIRECTORY;
        dir->AddDir(NULL, fileData, NULL);
    }

    return TRUE;
}

BOOL WINAPI
CNethoodFSInterface::TryCloseOrDetach(
    BOOL forceClose,
    BOOL canDetach,
    BOOL& detach,
    int reason)
{
    detach = FALSE;
    return TRUE;
}

void WINAPI
CNethoodFSInterface::Event(
    __in int nEvent,
    __in DWORD dwParam)
{
    if (nEvent == FSE_PATHCHANGED)
    {
        if (m_bShowThrobber)
        {
            m_iThrobberID = SalamanderGeneral->StartThrobber(
                dwParam,
                SPLLoadStrOwned(SalamanderGeneral,
                    GetLangInstance(), IDS_REFRESHING).c_str(),
                THROBBER_GRACE_PERIOD_IMMEDIATE);
        }
    }
}

void WINAPI
CNethoodFSInterface::ReleaseObject(
    __in HWND parent)
{
    UNREFERENCED_PARAMETER(parent);
}

DWORD WINAPI
CNethoodFSInterface::GetSupportedServices()
{
    return FS_SERVICE_CONTEXTMENU |
           FS_SERVICE_GETFSICON |
           FS_SERVICE_GETNEXTDIRLINEHOTPATH |
           FS_SERVICE_GETPATHFORMAINWNDTITLE;
}

BOOL WINAPI
CNethoodFSInterface::GetChangeDriveOrDisconnectItem(
    __in const wchar_t* fsName,
    __out_opt wchar_t*& title,
    __out_opt HICON& icon,
    __out BOOL& destroyIcon)
{
    UNREFERENCED_PARAMETER(fsName);
    UNREFERENCED_PARAMETER(title);
    UNREFERENCED_PARAMETER(icon);
    UNREFERENCED_PARAMETER(destroyIcon);

    return FALSE;
}

HICON WINAPI
CNethoodFSInterface::GetFSIcon(__out BOOL& destroyIcon)
{
    destroyIcon = TRUE;
    return g_oIcons.GetIcon(SALICONSIZE_16, CNethoodIcons::IconMain);
}

void WINAPI
CNethoodFSInterface::GetDropEffect(
    __in const wchar_t* srcFSPath,
    __in const wchar_t* tgtFSPath,
    __in DWORD allowedEffects,
    __in DWORD keyState,
    __out DWORD* dropEffect)
{
    UNREFERENCED_PARAMETER(srcFSPath);
    UNREFERENCED_PARAMETER(tgtFSPath);
    UNREFERENCED_PARAMETER(allowedEffects);
    UNREFERENCED_PARAMETER(keyState);
    UNREFERENCED_PARAMETER(dropEffect);
}

void WINAPI
CNethoodFSInterface::GetFSFreeSpace(
    __out CQuadWord* retValue)
{
    UNREFERENCED_PARAMETER(retValue);
}

BOOL WINAPI
CNethoodFSInterface::GetNextDirectoryLineHotPath(
    __in const wchar_t* text,
    __in int pathLen,
    __inout int& offset)
{
    const wchar_t* psz;
    const wchar_t* pszEnd;
    const wchar_t* pszRoot;

    pszRoot = text;

    while (*pszRoot != L'\0' && *pszRoot != L':')
    {
        ++pszRoot;
    }

    if (*pszRoot == L':')
    {
        ++pszRoot;

        // Skip root backslashes (net:\ or net:\\)
        while (*pszRoot == L'\\')
        {
            ++pszRoot;
        }
    }

    psz = text + offset;
    pszEnd = text + pathLen;

    if (psz >= pszEnd)
    {
        return FALSE;
    }

    if (psz < pszRoot)
    {
        offset = (int)(INT_PTR)(pszRoot - text);
    }
    else
    {
        if (*psz == L'\\')
        {
            ++psz;
        }

        while (psz < pszEnd && *psz != L'\\')
        {
            ++psz;
        }

        offset = (int)(INT_PTR)(psz - text);
    }

    return psz < pszEnd;
}

BOOL WINAPI
CNethoodFSInterface::CompleteDirectoryLineHotPath(
    CSalamanderStringBuffer* path)
{
    // FIXME
    return path != NULL && sally::plugin_abi::IsValidStringBuffer(*path)
               ? TRUE
               : FALSE;
}

BOOL WINAPI
CNethoodFSInterface::GetPathForMainWindowTitle(
    __in const wchar_t* fsName,
    __in int mode,
    CSalamanderStringBuffer* buf)
{
    UNREFERENCED_PARAMETER(fsName);
    UNREFERENCED_PARAMETER(mode);
    UNREFERENCED_PARAMETER(buf);

    // We return FALSE and Salamander will create the title for the main
    // window automatically through the GetNextDirectoryLineHotPath method.
    return FALSE;
}

void WINAPI
CNethoodFSInterface::ShowInfoDialog(
    __in const wchar_t* fsName,
    __in HWND parent)
{
    UNREFERENCED_PARAMETER(fsName);
    UNREFERENCED_PARAMETER(parent);
}

BOOL WINAPI
CNethoodFSInterface::ExecuteCommandLine(
    __in HWND parent,
    CSalamanderStringBuffer* command,
    __out int& selFrom,
    __out int& selTo)
{
    UNREFERENCED_PARAMETER(parent);
    UNREFERENCED_PARAMETER(command);
    UNREFERENCED_PARAMETER(selFrom);
    UNREFERENCED_PARAMETER(selTo);

    return FALSE;
}

BOOL WINAPI
CNethoodFSInterface::QuickRename(
    __in const wchar_t* fsName,
    __in int mode,
    __in HWND parent,
    __in CFileData& file,
    __in BOOL isDir,
    CSalamanderStringBuffer* newName,
    __out BOOL& cancel)
{
    UNREFERENCED_PARAMETER(fsName);
    UNREFERENCED_PARAMETER(mode);
    UNREFERENCED_PARAMETER(parent);
    UNREFERENCED_PARAMETER(file);
    UNREFERENCED_PARAMETER(isDir);
    UNREFERENCED_PARAMETER(newName);
    UNREFERENCED_PARAMETER(cancel);

    return FALSE;
}

void WINAPI
CNethoodFSInterface::AcceptChangeOnPathNotification(
    __in const wchar_t* fsName,
    __in const wchar_t* path,
    __in BOOL includingSubdirs)
{
    UNREFERENCED_PARAMETER(fsName);
    UNREFERENCED_PARAMETER(path);
    UNREFERENCED_PARAMETER(includingSubdirs);
}

BOOL WINAPI
CNethoodFSInterface::CreateDir(
    __in const wchar_t* fsName,
    __in int mode,
    __in HWND parent,
    CSalamanderStringBuffer* newName,
    __out BOOL& cancel)
{
    UNREFERENCED_PARAMETER(fsName);
    UNREFERENCED_PARAMETER(mode);
    UNREFERENCED_PARAMETER(parent);
    UNREFERENCED_PARAMETER(newName);
    UNREFERENCED_PARAMETER(cancel);

    return FALSE;
}

void WINAPI
CNethoodFSInterface::ViewFile(
    __in const wchar_t* fsName,
    __in HWND parent,
    __in CSalamanderForViewFileOnFSAbstract* salamander,
    __in CFileData& file)
{
    UNREFERENCED_PARAMETER(fsName);
    UNREFERENCED_PARAMETER(parent);
    UNREFERENCED_PARAMETER(salamander);
    UNREFERENCED_PARAMETER(file);
}

BOOL WINAPI
CNethoodFSInterface::Delete(
    __in const wchar_t* fsName,
    __in int mode,
    __in HWND parent,
    __in int panel,
    __in int selectedFiles,
    __in int selectedDirs,
    __out BOOL& cancelOrError)
{
    UNREFERENCED_PARAMETER(fsName);
    UNREFERENCED_PARAMETER(mode);
    UNREFERENCED_PARAMETER(parent);
    UNREFERENCED_PARAMETER(panel);
    UNREFERENCED_PARAMETER(selectedFiles);
    UNREFERENCED_PARAMETER(selectedDirs);
    UNREFERENCED_PARAMETER(cancelOrError);

    return FALSE;
}

BOOL WINAPI
CNethoodFSInterface::CopyOrMoveFromFS(
    __in BOOL copy,
    __in int mode,
    __in const wchar_t* fsName,
    __in HWND parent,
    __in int panel,
    __in int selectedFiles,
    __in int selectedDirs,
    CSalamanderStringBuffer* targetPath,
    __out BOOL& operationMask,
    __out BOOL& cancelOrHandlePath,
    __in HWND dropTarget)
{
    UNREFERENCED_PARAMETER(copy);
    UNREFERENCED_PARAMETER(mode);
    UNREFERENCED_PARAMETER(fsName);
    UNREFERENCED_PARAMETER(parent);
    UNREFERENCED_PARAMETER(panel);
    UNREFERENCED_PARAMETER(selectedFiles);
    UNREFERENCED_PARAMETER(selectedDirs);
    UNREFERENCED_PARAMETER(targetPath);
    UNREFERENCED_PARAMETER(operationMask);
    UNREFERENCED_PARAMETER(cancelOrHandlePath);
    UNREFERENCED_PARAMETER(dropTarget);

    return FALSE;
}

BOOL WINAPI
CNethoodFSInterface::CopyOrMoveFromDiskToFS(
    __in BOOL copy,
    __in int mode,
    __in const wchar_t* fsName,
    __in HWND parent,
    __in const wchar_t* sourcePath,
    __in SalEnumSelection2 next,
    __in void* nextParam,
    __in int sourceFiles,
    __in int sourceDirs,
    CSalamanderStringBuffer* targetPath,
    __out_opt BOOL* invalidPathOrCancel)
{
    UNREFERENCED_PARAMETER(copy);
    UNREFERENCED_PARAMETER(mode);
    UNREFERENCED_PARAMETER(fsName);
    UNREFERENCED_PARAMETER(parent);
    UNREFERENCED_PARAMETER(sourcePath);
    UNREFERENCED_PARAMETER(next);
    UNREFERENCED_PARAMETER(nextParam);
    UNREFERENCED_PARAMETER(sourceFiles);
    UNREFERENCED_PARAMETER(sourceDirs);
    UNREFERENCED_PARAMETER(targetPath);
    UNREFERENCED_PARAMETER(invalidPathOrCancel);

    return FALSE;
}

BOOL WINAPI
CNethoodFSInterface::ChangeAttributes(
    __in const wchar_t* fsName,
    __in HWND parent,
    __in int panel,
    __in int selectedFiles,
    __in int selectedDirs)
{
    UNREFERENCED_PARAMETER(fsName);
    UNREFERENCED_PARAMETER(parent);
    UNREFERENCED_PARAMETER(panel);
    UNREFERENCED_PARAMETER(selectedFiles);
    UNREFERENCED_PARAMETER(selectedDirs);

    return FALSE;
}

void WINAPI
CNethoodFSInterface::ShowProperties(
    __in const wchar_t* fsName,
    __in HWND parent,
    __in int panel,
    __in int selectedFiles,
    __in int selectedDirs)
{
    UNREFERENCED_PARAMETER(fsName);
    UNREFERENCED_PARAMETER(parent);
    UNREFERENCED_PARAMETER(panel);
    UNREFERENCED_PARAMETER(selectedFiles);
    UNREFERENCED_PARAMETER(selectedDirs);
}

void WINAPI
CNethoodFSInterface::ContextMenu(
    __in const wchar_t* fsName,
    __in HWND parent,
    __in int menuX,
    __in int menuY,
    __in int type,
    __in int panel,
    __in int selectedFiles,
    __in int selectedDirs)
{
    // Only ever fed by GetUncPath (wide now) and consumed by the
    // already-wide OpenNetworkContextMenu, so this local is wide directly - no round-trip.
    std::wstring path;
    // OpenNetworkContextMenu is wide; the mapped-drive letter comes back as a
    // wchar_t. This buffer only ever holds a drive root, so it is ASCII either way.
    wchar_t szMappedRootW[4] = L"\0:\\";
    bool bOk = false;

    if (type == fscmItemsInPanel)
    {
        // Open context menu for focused item or selected items in panel.

        if (!AreItemsSuitableForContextMenu(selectedFiles + selectedDirs > 0))
        {
            return;
        }

        if (IsRootForContextMenu())
        {
            SalamanderGeneral->OpenNetworkContextMenu(
                parent, panel, TRUE,
                menuX, menuY, L"\\\\",
                &szMappedRootW[0]);
        }
        else
        {
            g_oNethoodCache.LockCache();
            bOk = (m_pathNode != NULL) && g_oNethoodCache.GetUncPath(m_pathNode, path);
            g_oNethoodCache.UnlockCache();
            if (bOk)
            {
                SalamanderGeneral->OpenNetworkContextMenu(
                    parent, panel, TRUE,
                    menuX, menuY, path.c_str(),
                    &szMappedRootW[0]);
            }
        }
    }
    else
    {
        // Open context menu for incomplete UNC path in panel.

        assert(type == fscmPathInPanel || type == fscmPanel);

        if (IsRootPath(m_currentPath.c_str()))
        {
            SalamanderGeneral->OpenNetworkContextMenu(
                parent, panel, FALSE,
                menuX, menuY, L"\\\\",
                &szMappedRootW[0]);
        }
        else
        {
            g_oNethoodCache.LockCache();
            if (m_pathNode != NULL)
            {
                CNethoodCacheNode::Type nodeType;

                nodeType = g_oNethoodCache.GetItemData(m_pathNode).GetType();
                if (nodeType == CNethoodCacheNode::TypeServer ||
                    nodeType == CNethoodCacheNode::TypeShare ||
                    nodeType == CNethoodCacheNode::TypeTSCVolume)
                {
                    bOk = g_oNethoodCache.GetUncPath(m_pathNode, path);
                }
            }
            g_oNethoodCache.UnlockCache();

            if (bOk)
            {
                SalamanderGeneral->OpenNetworkContextMenu(
                    parent, panel, FALSE,
                    menuX, menuY, path.c_str(),
                    &szMappedRootW[0]);
            }
        }
    }

    if (szMappedRootW[0] != L'\0')
    {
        // New drive was mapped using Map Network Drive command,
        // let's open it in panel.

        // Store the path to redirect... PostRedirectPathToSalamander is wide now
        // (this file's own wide-FS-interface-ABI conversion), so szMappedRootW - already a
        // complete drive-root path - is passed straight through.
        PostRedirectPathToSalamander(szMappedRootW);
        // ...and refresh the panel that will perform the
        // actual redirection.
        SalamanderGeneral->PostRefreshPanelFS2(this);
    }
}

BOOL WINAPI
CNethoodFSInterface::OpenFindDialog(
    __in const wchar_t* fsName,
    __in int panel)
{
    UNREFERENCED_PARAMETER(fsName);
    UNREFERENCED_PARAMETER(panel);

    return FALSE;
}

void WINAPI
CNethoodFSInterface::OpenActiveFolder(
    __in const wchar_t* fsName,
    __in HWND parent)
{
    UNREFERENCED_PARAMETER(fsName);
    UNREFERENCED_PARAMETER(parent);
}

void WINAPI
CNethoodFSInterface::GetAllowedDropEffects(
    __in int mode,
    __in const wchar_t* tgtFSPath,
    __out DWORD* allowedEffects)
{
    UNREFERENCED_PARAMETER(mode);
    UNREFERENCED_PARAMETER(tgtFSPath);
    UNREFERENCED_PARAMETER(allowedEffects);
}

BOOL WINAPI
CNethoodFSInterface::HandleMenuMsg(
    __in UINT uMsg,
    __in WPARAM wParam,
    __in LPARAM lParam,
    __out LRESULT* plResult)
{
    UNREFERENCED_PARAMETER(uMsg);
    UNREFERENCED_PARAMETER(wParam);
    UNREFERENCED_PARAMETER(lParam);
    UNREFERENCED_PARAMETER(plResult);

    return FALSE;
}

BOOL WINAPI
CNethoodFSInterface::GetNoItemsInPanelText(
    CSalamanderStringBuffer* textBuf)
{
    if (textBuf == NULL ||
        !sally::plugin_abi::IsValidStringBuffer(*textBuf))
        return FALSE;
    bool bPending = false;

    g_oNethoodCache.LockCache();

    if (m_pathNode != NULL)
    {
        bPending = (g_oNethoodCache.GetItemData(m_pathNode).GetStatus() ==
                    CNethoodCacheNode::StatusPending);
    }

    g_oNethoodCache.UnlockCache();

    if (bPending)
    {
        // Enumeration pending...
        return sally::plugin_abi::WriteStringBuffer(
                   *textBuf,
                   SPLLoadStrOwned(SalamanderGeneral, GetLangInstance(), IDS_REFRESHING).c_str())
                   ? TRUE
                   : FALSE;
    }
#if 0
	else
	{
		// No items in the panel.
		LoadString(GetLangInstance(), IDS_NETWORK_UNAVAILABLE, textBuf, textBufSize);
	}

	return TRUE;
#endif

    return FALSE;
}

void CNethoodFSInterface::NotifyPathUpdated(__in CNethoodCache::Node node)
{
    bool bRefresh = false;
    bool bFindAccessible = false;
    DWORD dwEnumerationResult = ERROR_SUCCESS;

    const CNethoodCacheNode& nodeData = g_oNethoodCache.GetItemData(node);

    TRACE_I("Node " << node << " updated");

    if (node == m_pathNode)
    {
        if (nodeData.GetStatus() == CNethoodCacheNode::StatusInvalid)
        {
            dwEnumerationResult = nodeData.GetLastEnumerationResult();

            if (dwEnumerationResult == ERROR_NETHOODCACHE_FULL_UNC_PATH)
            {
                g_oNethoodCache.GetUncPath(node, m_accessiblePath);
                m_state = FSStateAsyncRedirect;

                g_oNethoodCache.UnregisterConsumer(node, m_pPathNodeEventConsumer);
                g_oNethoodCache.ReleaseNode(node);
                delete m_pPathNodeEventConsumer;
                m_pPathNodeEventConsumer = NULL;
                m_pathNode = NULL;
            }
            else
            {
                bFindAccessible = true;
            }
        }
        else if (nodeData.GetStatus() == CNethoodCacheNode::StatusDead)
        {
            bFindAccessible = true;
            dwEnumerationResult = ERROR_BAD_NETPATH;
        }
        else if (nodeData.GetStatus() == CNethoodCacheNode::StatusOk)
        {
            m_state = FSStateImmediateRefresh;
            TRACE_IW(L"Path " << m_currentPath << L" is valid. Will do immediate refresh.");
        }
        else if (nodeData.GetStatus() == CNethoodCacheNode::StatusPending)
        {
            if (nodeData.GetPreviousStatus() == CNethoodCacheNode::StatusStandby)
            {
                PostStartThrobber(node);
                return;
            }
            else
            {
                // Partial listing retrieved.
                m_state = FSStateImmediateRefresh;
            }
        }
        else
        {
            return;
        }

        if (bFindAccessible)
        {
            g_oNethoodCache.UnregisterConsumer(node, m_pPathNodeEventConsumer);
            g_oNethoodCache.ReleaseNode(node);
            delete m_pPathNodeEventConsumer;
            m_pPathNodeEventConsumer = NULL;
            g_oNethoodCache.FindAccessiblePath(m_pathNode, m_accessiblePath);
            m_pathNode = NULL;
            m_state = FSStateDisplayError;
            m_dwEnumerationResult = dwEnumerationResult;

            TRACE_IW(L"Path was invalid, new accessible path is " << m_accessiblePath << L". Will do full refresh.");
        }

        bRefresh = true;
    }

    if (bRefresh)
    {
        m_bIgnoreForceRefresh = true;

        if (!SalamanderGeneral->PostRefreshPanelFS2(this))
        {
            // Petr: Give the main thread some time to attach this FS to the panel.
            // FIXME: Milan: Not a good idea to sleep while holding the cache lock!
            Sleep(200);
            SalamanderGeneral->PostRefreshPanelFS2(this);
        }
    }
}

bool CNethoodFSInterface::IsRootPath(__in PCWSTR pszPath)
{
    return (pszPath[0] == L'\\') && (pszPath[1] == L'\0');
}

void CNethoodFSInterface::DisplayError(DWORD dwError)
{
    // FormatMessage/szErrorMessage narrowed a genuinely wide system
    // error string only to re-widen it via ToWideArg right before ShowMessageBox (wide-
    // native) - the same double-round-trip shape already fixed for SetRedirectPath and
    // ExecuteMenuItem's tooltip. FormatMessageW throughout instead.
    wchar_t szErrorMessage[512];
    void* pAllocatedErrorDescription;

    if (m_bShowThrobber)
    {
        SalamanderGeneral->StopThrobber(m_iThrobberID);
        m_bShowThrobber = false;
    }

    if (dwError == ERROR_CANCELLED)
    {
        // User cancelled something (the logon dialog most probably).
        // Do not report something the user already acknowledged.
        return;
    }

    if (FormatMessageW(
            FORMAT_MESSAGE_ALLOCATE_BUFFER |
                FORMAT_MESSAGE_FROM_SYSTEM |
                FORMAT_MESSAGE_IGNORE_INSERTS,
            NULL,
            dwError,
            MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
            reinterpret_cast<PWSTR>(&pAllocatedErrorDescription),
            0, NULL) > 0)
    {
        StringCchPrintfW(szErrorMessage, COUNTOF(szErrorMessage),
                         L"(%lu) ", dwError);
        StringCchCatW(szErrorMessage, COUNTOF(szErrorMessage),
                      static_cast<PWSTR>(pAllocatedErrorDescription));
        LocalFree(pAllocatedErrorDescription);
    }
    else
    {
        StringCchPrintfW(szErrorMessage, COUNTOF(szErrorMessage),
                         L"(%lu)", dwError);
    }

    // Preserve the prior behavior exactly: ToWideArg(NULL) returned an empty string, not
    // NULL, and MessageBox's caption defaults differently for NULL vs "" - use L"" here.
    SalamanderGeneral->ShowMessageBox(szErrorMessage, L"", MSGBOX_WARNING);
}

bool CNethoodFSInterface::PostRedirectPathToSalamander(
    __in PCWSTR pszPath)
{
    assert(m_state != FSStateRedirect);
    assert(pszPath != m_redirectPath.c_str()); // We REALLY want to compare the pointers!

    if (!SalamanderGeneral->SalCheckAndRestorePath(
            SalamanderGeneral->GetMainWindowHWND(),
            pszPath,
            TRUE))
    {
        return false;
    }

    // TODO: Exclude current path from the history.

    m_redirectPath.assign(pszPath);
    m_state = FSStateRedirect;

    return true;
}

void CNethoodFSInterface::StartThrobber()
{
    TRACE_I("Starting throbber.");

    m_bShowThrobber = true;
}

void CNethoodFSInterface::StopThrobber()
{
    TRACE_I("Stopping throbber.");

    m_bShowThrobber = false;
}

void CNethoodFSInterface::PostStartThrobber(__in CNethoodCache::Node node)
{
    g_oNethoodCache.AddRefNode(node);
    g_adwPostedThrobberQueue[g_iPostedThrobberQueue] = reinterpret_cast<DWORD_PTR>(node);
    if (++g_iPostedThrobberQueue >= POSTED_THROBBER_QUEUE_LEN)
    {
        g_iPostedThrobberQueue = 0;
    }
    SalamanderGeneral->PostMenuExtCommand(MENUCMD_START_THROBBER, FALSE);
}

bool CNethoodFSInterface::IsRootForContextMenu()
{
    bool bRoot;

    bRoot = IsRootPath(m_currentPath.c_str());
    if (!bRoot)
    {
        g_oNethoodCache.LockCache();
        if (m_pathNode != NULL)
        {
            CNethoodCacheNode::Type nodeType;
            nodeType = g_oNethoodCache.GetItemData(m_pathNode).GetType();
            bRoot = (nodeType == CNethoodCacheNode::TypeDomain) ||
                    (nodeType == CNethoodCacheNode::TypeGroup);
        }
        g_oNethoodCache.UnlockCache();
    }

    return bRoot;
}

bool CNethoodFSInterface::AreItemsSuitableForContextMenu(
    __in bool bSelected)
{
    const CFileData* file;
    CNethoodCacheNode::Type nodeType;
    bool bOk = false;

    g_oNethoodCache.LockCache();
    if (bSelected)
    {
        int iItem = 0;

        while ((file = SalamanderGeneral->GetPanelSelectedItem(PANEL_SOURCE, &iItem, NULL)) != NULL)
        {
            nodeType = GetNodeTypeFromFileData(*file);
            bOk = (nodeType == CNethoodCacheNode::TypeServer ||
                   nodeType == CNethoodCacheNode::TypeShare ||
                   nodeType == CNethoodCacheNode::TypeTSCVolume);
            if (!bOk)
            {
                break;
            }
        }
    }
    else
    {
        file = SalamanderGeneral->GetPanelFocusedItem(PANEL_SOURCE, NULL);
        if (file != NULL)
        {
            nodeType = GetNodeTypeFromFileData(*file);
            bOk = (nodeType == CNethoodCacheNode::TypeServer ||
                   nodeType == CNethoodCacheNode::TypeShare ||
                   nodeType == CNethoodCacheNode::TypeTSCVolume);
        }
    }
    g_oNethoodCache.UnlockCache();

    return bOk;
}

void CNethoodFSInterface::ConfigurationChanged()
{
    SalamanderGeneral->PostRefreshPanelFS2(this);
}

//------------------------------------------------------------------------------
//

void CNethoodFSCacheConsumer::OnCacheNodeUpdated(__in CNethoodCache::Node node)
{
#if defined(_DEBUG) && defined(SONIC_DEBUGGING)
    MessageBeep(MB_ICONINFORMATION);
#endif
    m_pChief->NotifyPathUpdated(node);
}
