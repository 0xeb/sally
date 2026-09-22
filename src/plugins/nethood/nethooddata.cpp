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
#include "nethooddata.h"
#include "nethood_fs_operations.h"
#include "globals.h"
#include "icons.h"
#include "nethood.rh"
#include "nethood.rh2"
#include "lang\lang.rh"

extern CNethoodIcons g_oIcons;
extern CNethoodCache g_oNethoodCache;

CNethoodPluginDataInterface::CNethoodPluginDataInterface()
{
}

CNethoodPluginDataInterface::~CNethoodPluginDataInterface()
{
}

BOOL WINAPI
CNethoodPluginDataInterface::CallReleaseForFiles()
{
    return TRUE;
}

BOOL WINAPI
CNethoodPluginDataInterface::CallReleaseForDirs()
{
    return TRUE;
}

void WINAPI
CNethoodPluginDataInterface::ReleasePluginData(
    __in CFileData& file,
    __in BOOL isDir)
{
    CNethoodCache::Node node;

    node = CNethoodFSInterface::GetNodeFromFileData(file);
    if (node != NULL)
    {
        g_oNethoodCache.ReleaseNode(node);
        node = NULL;
    }
}

void WINAPI
CNethoodPluginDataInterface::GetFileDataForUpDir(
    __in const wchar_t* archivePath,
    __inout CFileData& upDir)
{
}

BOOL WINAPI
CNethoodPluginDataInterface::GetFileDataForNewDir(
    __in const wchar_t* dirName,
    __inout CFileData& dir)
{
    return FALSE;
}

HIMAGELIST WINAPI
CNethoodPluginDataInterface::GetSimplePluginIcons(
    __in int iconSize)
{
    return g_oIcons.GetImageList(iconSize);
}

BOOL WINAPI
CNethoodPluginDataInterface::HasSimplePluginIcon(
    CFileData& file,
    BOOL isDir)
{
    return TRUE;
}

HICON WINAPI
CNethoodPluginDataInterface::GetPluginIcon(
    __in const CFileData* file,
    __in int iconSize,
    __out BOOL& destroyIcon)
{
    assert(0);
    return NULL;
}

int WINAPI
CNethoodPluginDataInterface::CompareFilesFromFS(
    __in const CFileData* file1,
    __in const CFileData* file2)
{
    return wcscmp(file1->Name, file2->Name);
}

void WINAPI
CNethoodPluginDataInterface::SetupView(
    __in BOOL leftPanel,
    __in CSalamanderViewAbstract* view,
    __in const wchar_t* archivePath,
    __in const CFileData* upperDir)
{
    view->GetTransferVariables(
        g_transferFileData,
        g_transferIsDir,
        g_transferBuffer,
        g_transferLen,
        g_transferRowData,
        g_transferPluginDataIface,
        g_transferActCustomData);

    view->SetPluginSimpleIconCallback(&CNethoodPluginDataInterface::GetSimpleIconIndex);

    if (view->GetViewMode() == VIEW_MODE_DETAILED)
    {
        SetupColumns(view);
    }

    if (!m_redirectPath.empty())
    {
        RedirectUncPathToSalamander(
            leftPanel ? PANEL_LEFT : PANEL_RIGHT,
            m_redirectPath.c_str());

        m_redirectPath.clear();
    }
}

void WINAPI
CNethoodPluginDataInterface::ColumnFixedWidthShouldChange(
    __in BOOL leftPanel,
    __in const CColumn* column,
    __in int newFixedWidth)
{
}

void WINAPI
CNethoodPluginDataInterface::ColumnWidthWasChanged(
    __in BOOL leftPanel,
    __in const CColumn* column,
    __in int newWidth)
{
}

BOOL WINAPI
CNethoodPluginDataInterface::GetInfoLineContent(
    __in int panel,
    __in const CFileData* file,
    __in BOOL isDir,
    __in int selectedFiles,
    __in int selectedDirs,
    __in BOOL displaySize,
    __in const CQuadWord& selectedSize,
    CSalamanderStringBuffer* buffer,
    CSalamanderTextRangeBuffer* hotTexts)
{
    if (buffer == NULL || hotTexts == NULL)
        return FALSE;
    std::wstring text;

    if (file != NULL)
    {
        // Single node focused.

        CNethoodCache::Node node;

        node = CNethoodFSInterface::GetNodeFromFileData(*file);
        if (node != NULL)
        {
            g_oNethoodCache.LockCache();

            const CNethoodCacheNode& nodeData = g_oNethoodCache.GetItemData(node);
            PCWSTR pszName = nodeData.GetName();

            if (pszName != NULL)
            {
                // GetName() is wide now (cache.h widened) - the
                // narrow-bridge this comment used to describe is obsolete.
                text.push_back(L'<');
                for (const wchar_t* current = pszName; *current != L'\0'; ++current)
                {
                    if (*current == L'\\')
                        text.push_back(L'\\');
                    text.push_back(*current);
                }
                text.push_back(L'>');
            }

            g_oNethoodCache.UnlockCache();
        }
    }
    else
    {
        // Information Line for empty panel.
        if (selectedFiles == 0 && selectedDirs == 0)
        {
            // Use default text.
            return FALSE;
        }

        // Some nodes selected.

        CQuadWord cSelected;
        cSelected.SetUI64(selectedFiles + selectedDirs);
        const std::wstring expanded = SPLExpandPluralStringOwned(
            SalamanderGeneral,
            SPLLoadStrOwned(SalamanderGeneral, GetLangInstance(),
                                       IDS_INFOLINE_SELECTION_PLURAL).c_str(),
            1, &cSelected);
        text = SPLFormatStringOwned(expanded.c_str(),
                                    selectedFiles + selectedDirs);
    }

    std::vector<CSalamanderTextRange> ranges;
    if (!SPLLookForSubTextsOwned(SalamanderGeneral, text, ranges))
        return FALSE;
    return sally::plugin_abi::WriteTextAndRanges(
               *buffer, *hotTexts, text, ranges)
               ? TRUE
               : FALSE;
}

BOOL WINAPI
CNethoodPluginDataInterface::CanBeCopiedToClipboard()
{
    return FALSE;
}

BOOL WINAPI
CNethoodPluginDataInterface::GetByteSize(
    __in const CFileData* file,
    __in BOOL isDir,
    __out CQuadWord* size)
{
    return FALSE;
}

BOOL WINAPI
CNethoodPluginDataInterface::GetLastWriteDate(
    __in const CFileData* file,
    __in BOOL isDir,
    __out SYSTEMTIME* date)
{
    return FALSE;
}

BOOL WINAPI
CNethoodPluginDataInterface::GetLastWriteTime(
    __in const CFileData* file,
    __in BOOL isDir,
    __out SYSTEMTIME* time)
{
    return FALSE;
}

void CNethoodPluginDataInterface::SetupColumns(
    __in CSalamanderViewAbstract* pView)
{
    int cColumns;
    int iColumn;
    bool bHasDescription = false;
    const CColumn* pColumn;

    cColumns = pView->GetColumnsCount();
    for (iColumn = 0; iColumn < cColumns; iColumn++)
    {
        pColumn = pView->GetColumn(iColumn);

        if (pColumn->ID == COLUMN_ID_NAME)
        {
        }
        else if (pColumn->ID == COLUMN_ID_DESCRIPTION)
        {
            bHasDescription = true;
        }
        else
        {
            pView->DeleteColumn(iColumn);

            // Column deleted, fix the index.
            --iColumn;

            // Column count changed.
            cColumns = pView->GetColumnsCount();
        }
    }

    if (!bHasDescription)
    {
        AddDescriptionColumn(pView);
    }
}

void CNethoodPluginDataInterface::AddDescriptionColumn(
    __in CSalamanderViewAbstract* pView)
{
    CColumn column = {
        0,
    };

    LoadStringW(GetLangInstance(), IDS_COLUMN_COMMENTS, column.Name,
                COUNTOF(column.Name));
    column.GetText = &CNethoodPluginDataInterface::GetCommentColumnText;
    column.LeftAlignment = TRUE;
    column.CustomData = ColumnDataComment;
    column.ID = COLUMN_ID_CUSTOM;
    column.Width = 160;
    column.FixedWidth = FALSE;

    // Insert the "Comment" column at index 1 (right after the
    // Name column).
    pView->InsertColumn(1, &column);
}

void WINAPI CNethoodPluginDataInterface::GetCommentColumnText()
{
    CNethoodCache::Node node;

    g_transferBuffer[0] = L'\0';
    *g_transferLen = 0;

    node = CNethoodFSInterface::GetNodeFromFileData(**g_transferFileData);
    if (node != NULL)
    {
        PCWSTR pszComment;
        const CNethoodCacheNode& nodeData = g_oNethoodCache.GetItemData(node);

        pszComment = nodeData.GetComment();
        if (pszComment != NULL)
        {
            // GetComment() is wide now (cache.h widened) - the
            // narrow-bridge this comment used to describe is obsolete.
            StringCchCopyW(g_transferBuffer, TRANSFER_BUFFER_MAX, pszComment);
            *g_transferLen = static_cast<int>(wcslen(g_transferBuffer));
        }
    }
}

int WINAPI CNethoodPluginDataInterface::GetSimpleIconIndex()
{
    CNethoodCache::Node node;
    int iIcon = -1;

    node = CNethoodFSInterface::GetNodeFromFileData(**g_transferFileData);
    if (node != NULL)
    {
        CNethoodIcons::Icon icon = CNethoodIcons::_IconLast;
        const CNethoodCacheNode& nodeData = g_oNethoodCache.GetItemData(node);

        if (nodeData.IsShortcut())
        {
            /* Network shortcut can actually point to the local disk. */
            iIcon = CNethoodIcons::IconShare;

            PCWSTR pszPath = nodeData.GetName();
            if (pszPath && (*pszPath >= L'a' && *pszPath <= L'z' ||
                            *pszPath >= L'A' && *pszPath <= L'Z'))
            {
                /* The path starts with a drive letter. */
                wchar_t szRoot[4] = {*pszPath, L':', L'\\', L'\0'};
                if (GetDriveTypeW(szRoot) != DRIVE_REMOTE)
                {
                    iIcon = CNethoodIcons::IconSimpleDirectory;
                }
            }
        }
        else
        {
            iIcon = NodeTypeToIconIndex(nodeData.GetType());
        }
    }

    return iIcon;
}

int CNethoodPluginDataInterface::NodeTypeToIconIndex(
    __in CNethoodCacheNode::Type nodeType)
{
    int iIcon = -1;

    switch (nodeType)
    {
    case CNethoodCacheNode::TypeDomain:
    case CNethoodCacheNode::TypeGroup:
        iIcon = CNethoodIcons::IconDomainOrGroup;
        break;

    case CNethoodCacheNode::TypeServer:
    case CNethoodCacheNode::TypeTSCFolder:
        iIcon = CNethoodIcons::IconServer;
        break;

    case CNethoodCacheNode::TypeShare:
        iIcon = CNethoodIcons::IconShare;
        break;

    case CNethoodCacheNode::TypeNetwork:
        iIcon = CNethoodIcons::IconNetwork;
        break;

    case CNethoodCacheNode::TypeRoot:
        iIcon = CNethoodIcons::IconEntireNetwork;
        break;

    case CNethoodCacheNode::TypeTSCVolume:
        iIcon = CNethoodIcons::IconNetDrive;
        break;

    default:
        //iIcon = CNethoodIcons::IconSimpleFile;
        iIcon = CNethoodIcons::IconSimpleDirectory;
        //assert(0);
    }

    return iIcon;
}

void CNethoodPluginDataInterface::RedirectUncPathToSalamander(
    __in int iPanel,
    __in PCWSTR pszUncPath)
{
    assert(iPanel == PANEL_LEFT || iPanel == PANEL_RIGHT);

    // Shift iPanel to zero-based value.
    iPanel -= PANEL_LEFT;
    assert(iPanel >= 0 && iPanel < 2);

    g_redirectPaths[iPanel].assign(pszUncPath);
    SalamanderGeneral->PostMenuExtCommand(MENUCMD_REDIRECT_BASE + iPanel, TRUE);
}
