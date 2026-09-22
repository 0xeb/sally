// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

/*
	Network Plugin for Open Salamander
	
	Copyright (c) 2008-2023 Milan Kase <manison@manison.cz>
	
	TODO:
	Open-source license goes here...

	Network cache.
*/

#include "precomp.h"

#include <algorithm>
#include <new>
#include <vector>
#include "nethood.h"
#include "nethoodfs.h"
#include "cache.h"
#include "globals.h"
#include "resource_enum_buffer.h"
#include "nethood.rh"
#include "nethood.rh2"
#include "lang\lang.rh"

#if defined(_DEBUG) && 0
/// If nonzero, artificially prolongs enumeration time by the specified
/// number of milliseconds.
#define DBG_PROLONG_ENUMERATION 5000
#endif

#if defined(_DEBUG) && 0
/// If nonzero, results of every n-th enumeration will be truncated to
/// contain just one item.
#define DBG_TRUNCATE_EVERY_NTH_ENUM 4
#endif

#if defined(_DEBUG) && 0
/// If nonzero, every n-th enumeration will fail with error code defined
/// by the DBG_FAIL_ENUM_CODE.
#define DBG_FAIL_EVERY_NTH_ENUM 5
#endif

#define DBG_FAIL_ENUM_CODE ERROR_BAD_NETPATH

#ifndef DBG_PROLONG_ENUMERATION
#define DBG_PROLONG_ENUMERATION 0
#endif

#ifndef DBG_TRUNCATE_EVERY_NTH_ENUM
#define DBG_TRUNCATE_EVERY_NTH_ENUM 0
#endif

#ifndef DBG_FAIL_EVERY_NTH_ENUM
#define DBG_FAIL_EVERY_NTH_ENUM 0
#endif

/// Defines whether to include hidden system shares (C$ etc) into
/// the enumeration.
#define ENUM_HIDDEN_SHARES 1

//------------------------------------------------------------------------------
// CNethoodCacheNode

CNethoodCacheNode::CNethoodCacheNode()
{
    m_status = StatusUnknown;
    m_revertStatus = StatusUnknown;
    m_pszName = NULL;
    m_pszDisplayName = NULL;
    m_pszComment = NULL;
    m_pszProvider = NULL;
    m_pszExplicitDisplayName = NULL;
    m_dwLastEnumerationResult = -1;
    m_cConsumers = 0;
    m_cInternalConsumers = 0;
    m_hint = HintNone;
    m_pCache = NULL;
    m_myIterator = NULL;
    memset(&m_sNetResource, 0, sizeof(m_sNetResource));
    m_bNetResourceValid = false;
    m_type = TypeGeneric;
    m_uStandbyTimeout = 0;
    m_uStandbyTimestamp = 0;
    m_bImmortal = false;
    m_bImmortalSticky = false;
    m_bHidden = false;
    m_bShortcut = false;
    m_cRef = 0;
}

CNethoodCacheNode::~CNethoodCacheNode()
{
    TRACE_IW(L"Deleting node " << (m_pszDisplayName != NULL ? m_pszDisplayName : L"(null)") << L" [" << m_myIterator << L"]");

    // Notify everyone that we are vanishing.
    if (m_status != StatusDead)
    {
        m_status = StatusDead;
        NotifyNodeUpdated();
    }

    assert(m_cConsumers == 0);
    assert(m_cInternalConsumers == 0);
    //assert(m_cRef == 0);

    delete[] m_pszName;
    delete[] m_pszComment;
    delete[] m_pszProvider;
    delete[] m_pszExplicitDisplayName;
}

void CNethoodCacheNode::SetName(PCWSTR pszName, int nLen)
{
    if (m_pszName != NULL)
    {
        delete[] m_pszName;
        m_pszName = NULL;
    }

    if (pszName == NULL)
    {
        m_pszName = NULL;
    }
    else
    {
        if (nLen < 0)
        {
            nLen = static_cast<int>(wcslen(pszName));
        }

        m_pszName = new wchar_t[nLen + 1];
        StringCchCopyW(m_pszName, nLen + 1, pszName);

        m_pszDisplayName = const_cast<PWSTR>(GetDisplayName(m_pszName, m_pszComment));
    }
}

void CNethoodCacheNode::SetComment(__in PCWSTR pszComment)
{
    if (m_pszComment != NULL)
    {
        delete[] m_pszComment;
        m_pszComment = NULL;
    }

    if (pszComment == NULL)
    {
        m_pszComment = NULL;
    }
    else
    {
        size_t nLen = wcslen(pszComment);

        m_pszComment = new wchar_t[nLen + 1];
        StringCchCopyW(m_pszComment, nLen + 1, pszComment);

        m_pszDisplayName = const_cast<PWSTR>(GetDisplayName(m_pszName, m_pszComment));
    }
}

void CNethoodCacheNode::SetProvider(__in PCWSTR pszProvider)
{
    if (m_pszProvider != NULL)
    {
        delete[] m_pszProvider;
        m_pszProvider = NULL;
    }

    if (pszProvider == NULL)
    {
        m_pszProvider = NULL;
    }
    else
    {
        size_t nLen = wcslen(pszProvider);

        m_pszProvider = new wchar_t[nLen + 1];
        StringCchCopyW(m_pszProvider, nLen + 1, pszProvider);
    }
}

void CNethoodCacheNode::SetDisplayName(__in PCWSTR pszDisplayName)
{
    assert(m_type == TypeTSCVolume);
    assert(pszDisplayName != NULL && *pszDisplayName != L'\0');

    if (m_pszExplicitDisplayName != NULL)
    {
        delete[] m_pszExplicitDisplayName;
        m_pszExplicitDisplayName = NULL;
    }

    size_t nLen = wcslen(pszDisplayName);

    m_pszExplicitDisplayName = new wchar_t[nLen + 1];
    StringCchCopyW(m_pszExplicitDisplayName, nLen + 1, pszDisplayName);

    // Redirect the display name to point to the now formed explicit name.
    m_pszDisplayName = m_pszExplicitDisplayName;
}

unsigned CNethoodCacheNode::AddConsumer(
    __in CNethoodCacheEventConsumer* pConsumer)
{
    assert(pConsumer != NULL);
    assert(!CNethoodCacheListEntry::Search(&m_consumers, pConsumer));
    assert(pConsumer->pNext == pConsumer->pPrev);

    CNethoodCacheListEntry::InsertAtTail(&m_consumers, pConsumer);
    return ++m_cConsumers;
}

unsigned CNethoodCacheNode::RemoveConsumer(
    __in CNethoodCacheEventConsumer* pConsumer)
{
    assert(pConsumer != NULL);
    assert(CNethoodCacheListEntry::Search(&m_consumers, pConsumer));
    assert(m_cConsumers > 0);

    CNethoodCacheListEntry::Remove(pConsumer);
    --m_cConsumers;
    assert((m_cConsumers + m_cInternalConsumers > 0) || CNethoodCacheListEntry::IsEmpty(&m_consumers));
    return m_cConsumers;
}

void CNethoodCacheNode::AddInternalConsumer(
    __in CNethoodCacheEventConsumer* pConsumer)
{
    assert(pConsumer != NULL);
    assert(pConsumer->pNext == pConsumer->pPrev);
    assert(!CNethoodCacheListEntry::Search(&m_consumers, pConsumer));

    CNethoodCacheListEntry::InsertAtTail(&m_consumers, pConsumer);
    ++m_cInternalConsumers;
}

void CNethoodCacheNode::RemoveInternalConsumer(
    __in CNethoodCacheEventConsumer* pConsumer)
{
    assert(pConsumer != NULL);
    assert(CNethoodCacheListEntry::Search(&m_consumers, pConsumer));
    assert(m_cInternalConsumers > 0);

    CNethoodCacheListEntry::Remove(pConsumer);
    --m_cInternalConsumers;
    assert((m_cConsumers + m_cInternalConsumers > 0) || CNethoodCacheListEntry::IsEmpty(&m_consumers));
}

void CNethoodCacheNode::Create(
    __in CNethoodCache* pCache,
    __in void* iterator)
{
    assert(m_pCache == NULL);
    assert(m_myIterator == NULL);
    assert(pCache != NULL);
    assert(iterator != NULL);

    m_pCache = pCache;
    m_myIterator = iterator;
}

void CNethoodCacheNode::NotifyNodeUpdated()
{
    assert(m_pCache != NULL);
    assert(m_myIterator != NULL);

    CNethoodCacheListEntry* pConsumerEntry;
    CNethoodCacheListEntry* pNextEntry;

    m_pCache->LockCache();

    pConsumerEntry = m_consumers.pNext;
    while (pConsumerEntry != &m_consumers)
    {
        // Fetch the next entry before invoking the callback in case the
        // consumer removes itself from the list during the notification.
        pNextEntry = pConsumerEntry->pNext;

        // Invoke the consumer's callback.
        static_cast<CNethoodCacheEventConsumer*>(pConsumerEntry)->OnCacheNodeUpdated(reinterpret_cast<CNethoodCache::Node>(m_myIterator));

        // Move on.
        pConsumerEntry = pNextEntry;
    }

    m_pCache->UnlockCache();
}

void CNethoodCacheNode::SetNetResource(__in const NETRESOURCEW* pNetResource)
{
    m_sNetResource.dwScope = pNetResource->dwScope;
    m_sNetResource.dwType = pNetResource->dwType;
    m_sNetResource.dwDisplayType = pNetResource->dwDisplayType;
    m_sNetResource.dwUsage = pNetResource->dwUsage;
    m_sNetResource.lpLocalName = NULL;
    SetName(pNetResource->lpRemoteName);
    m_sNetResource.lpRemoteName = m_pszName;
    SetComment(pNetResource->lpComment);
    m_sNetResource.lpComment = m_pszComment;
    SetProvider(pNetResource->lpProvider);
    m_sNetResource.lpProvider = m_pszProvider;
    m_pszDisplayName = const_cast<PWSTR>(GetDisplayNameFromNetResource(&m_sNetResource));
    m_type = static_cast<Type>(m_sNetResource.dwDisplayType);
    m_bNetResourceValid = true;
}

PCWSTR CNethoodCacheNode::GetDisplayNameFromNetResource(
    __in const NETRESOURCEW* pNetResource)
{
    return GetDisplayName(
        (pNetResource->dwType == RESOURCETYPE_SHORTCUT) ? NULL : pNetResource->lpRemoteName,
        pNetResource->lpComment);
}

PCWSTR CNethoodCacheNode::GetDisplayName(
    __in PCWSTR pszName,
    __in PCWSTR pszComment)
{
    PCWSTR pszDisplayName;

    if (pszName == NULL)
    {
        if (pszComment == NULL)
        {
            pszDisplayName = L"";
        }
        else
        {
            pszDisplayName = pszComment;
        }
    }
    else
    {
        PCWSTR pszBackSlash = wcsrchr(pszName, L'\\');

        if (pszBackSlash == NULL)
        {
            pszDisplayName = pszName;
        }
        else
        {
            pszDisplayName = pszBackSlash + 1;
            assert(*pszDisplayName != L'\0');
        }
    }

    return pszDisplayName;
}

void CNethoodCacheNode::Invalidate(__in bool bRecursive)
{
    m_status = StatusInvalid;

    if (bRecursive)
    {
        CNethoodCache::Node node;

        node = m_pCache->m_oTree.GetFirstChild(
            reinterpret_cast<CNethoodCache::Node>(m_myIterator));
        while (node != NULL)
        {
            m_pCache->m_oTree.GetAt(node).Invalidate(true);
            node = m_pCache->m_oTree.GetNextSibling(node);
        }
    }

    NotifyNodeUpdated();
}

void CNethoodCacheNode::SetStandbyTimeout(__in UINT uTimeout)
{
    m_uStandbyTimeout = uTimeout;
    m_uStandbyTimestamp = (uTimeout != 0) ? GetTickCount() : 0;
}

bool CNethoodCacheNode::IsStandbyPeriodOver()
{
    assert(m_uStandbyTimestamp != 0);
    assert(m_uStandbyTimeout != 0);
    return (GetTickCount() - m_uStandbyTimestamp) >= m_uStandbyTimeout;
}

void CNethoodCacheNode::SetStatus(__in Status status)
{
    assert(m_status != StatusDead);
    assert(status != m_status);

    m_revertStatus = m_status;
    m_status = status;
}

void CNethoodCacheNode::RevertStatus()
{
    assert(m_status == StatusStandby);
    assert(m_revertStatus == StatusOk || m_revertStatus == StatusInvalid);
    m_status = m_revertStatus;
}

#if defined(_DEBUG) || defined(TRACE_ENABLE)
bool CNethoodCacheNode::CanKill() const
{
    //assert(m_status == StatusDead);
    assert(m_pCache->IsLocked());
#ifdef TRACE_ENABLE
    const wchar_t* pszName = GetDisplayName();
    if (pszName == NULL)
    {
        pszName = L"\\";
    }
    if (m_cRef == 0)
    {
        TRACE_IW(L"No outstanding references to dead node " << pszName << L", can kill=yes");
    }
    else
    {
        // assert(*pszName != L'\\');
        // why was this here?
        // should it rather be assert(strcmp(pszName, "\\")) => the root node must not be killed
        TRACE_IW(m_cRef << L" outstanding references to dead node " << pszName << L", can kill=no");
    }
#endif
    return (m_cRef == 0);
}
#endif

//------------------------------------------------------------------------------
// CUncPathParser

CUncPathParser::CUncPathParser(__in_opt PCWSTR pszUncPath)
{
    Initialize(pszUncPath);
}

bool CUncPathParser::NextToken()
{
    const wchar_t* pchStart;
    const wchar_t* pch;
    UINT uPrevClassification;

    if (m_pchData == NULL)
    {
        pchStart = m_pszPath;
    }
    else
    {
        pchStart = m_pchData + m_nLength;
    }

    uPrevClassification = m_uClassification;
    m_uClassification = 0;

    if (pchStart >= m_pchMax)
    {
        m_pchData = NULL;
        m_nLength = 0;
        m_uError = ERROR_NO_MORE_ITEMS;
        return false;
    }

    if (*pchStart != L'\\')
    {
        m_pchData = NULL;
        m_nLength = 0;
        m_uError = ERROR_INVALID_NETNAME;
        return false;
    }

    ++pchStart;
    if (*pchStart == L'\\')
    {
        if (m_pchData == NULL)
        {
            ++pchStart;
            m_uClassification = TokenServer;
        }
        else
        {
            m_pchData = NULL;
            m_nLength = 0;
            m_uError = ERROR_INVALID_NETNAME;
            return false;
        }
    }

    pch = pchStart;
    if (pch > m_pchMax)
    {
        m_pchData = NULL;
        m_nLength = 0;
        m_uError = ((m_uClassification & TokenServer) != 0) ? ERROR_INVALID_NETNAME : ERROR_NO_MORE_ITEMS;
        return false;
    }

    while (pch < m_pchMax)
    {
        if (*pch == L'\\')
        {
            break;
        }
        ++pch;
    }

    m_pchData = pchStart;
    m_nLength = pch - pchStart;

    if (m_nLength == 0)
    {
        m_pchData = NULL;
        m_nLength = 0;
        m_uError = ((m_uClassification & TokenServer) != 0) ? ERROR_INVALID_NETNAME : ERROR_NO_MORE_ITEMS;
        return false;
    }

    if ((uPrevClassification & TokenServer) != 0)
    {
        m_uClassification = TokenShare;
    }
    return true;
}

void CUncPathParser::Initialize(__in PCWSTR pszUncPath)
{
    m_pszPath = pszUncPath;
    m_pchData = NULL;
    m_nLength = 0;
    m_pchMax = (m_pszPath == NULL) ? NULL : (m_pszPath + wcslen(m_pszPath));
    m_uClassification = 0;
    m_uError = NO_ERROR;
}

void CUncPathParser::Reset()
{
    assert(m_pszPath != NULL);
    m_pchData = NULL;
    m_nLength = 0;
    m_pchMax = m_pszPath + wcslen(m_pszPath);
    m_uClassification = 0;
    m_uError = NO_ERROR;
}

UINT CUncPathParser::Validate(__out_opt int* pcTokens)
{
    UINT uError;
    int nLevel = 0;
    bool bUnc = false;

    Reset();

    while (NextToken())
    {
        if ((m_uClassification & TokenServer) != 0)
        {
            bUnc = true;
        }
        ++nLevel;
    }

    uError = GetLastError();

    Reset();

    if (uError == ERROR_NO_MORE_ITEMS)
    {
        uError = NO_ERROR;
    }

    if (uError == NO_ERROR && bUnc && nLevel >= 2)
    {
        // The path seems to be full UNC path in the form
        // \\server\share\directory\...
        // Such path should not be cached. Instead it should
        // be handled natively by Salamander.
        uError = ERROR_NETHOODCACHE_FULL_UNC_PATH;
    }

    if (pcTokens != NULL)
    {
        *pcTokens = nLevel;
    }

    return uError;
}

/*static*/ UINT CUncPathParser::Validate(__in PCWSTR pszUncPath)
{
    CUncPathParser oParser(pszUncPath);
    return oParser.Validate();
}

std::wstring CUncPathParser::GetPathUpToCurrentToken() const
{
    const size_t length = (m_pchData - m_pszPath) + m_nLength;
    return std::wstring(m_pszPath, length);
}

//------------------------------------------------------------------------------
// CNethoodCache

CNethoodCache::CNethoodCache() : m_oThreadQueue("Nethood Enums")
{
    // Create cache root node.
    Node nodeRoot = InsertNode(NULL, NULL);
    CNethoodCacheNode& data = m_oTree.GetAt(nodeRoot);
    data.SetHint(CNethoodCacheNode::HintRoot);

    m_displaySystemShares = SysShareNone;
    m_bNetworkShortcuts = true;

    m_displayTscVolumes = TSCDisplayNone;
    m_nodeTscFolder = NULL;

    m_cStandbyNodes = 0;
    m_hMgmtThread = NULL;
    m_hWakeMgmtEvent = NULL;

    m_hQuitEvent = HANDLES(CreateEvent(NULL, TRUE, FALSE, NULL));

    // Initialize the critical section lock with spin count.
    InitializeCriticalSectionAndSpinCount(&m_csLock, 4000);

    m_hWtsApi32 = NULL;
    m_pfnWTSRegisterSessionNotification = NULL;
    m_pfnWTSUnRegisterSessionNotification = NULL;
    m_pfnWTSGetActiveConsoleSessionId = NULL;
}

CNethoodCache::~CNethoodCache()
{
    // Explicitly call the Destroy here, because it may have not been
    // called if the plugin was not initialized properly (e.g. missing
    // language file). The TTree destructor would otherwise release
    // the root node that would in turn invoke the notifications
    // while the cache lock is already gone.
    Destroy();
    DeleteCriticalSection(&m_csLock);
}

void CNethoodCache::Destroy()
{
    if (m_hQuitEvent != NULL)
    {
        SetEvent(m_hQuitEvent);

        if (m_hMgmtThread != NULL)
        {
            m_oThreadQueue.WaitForExit(m_hMgmtThread);

            assert(m_hWakeMgmtEvent != NULL);
            HANDLES(CloseHandle(m_hWakeMgmtEvent));
            m_hWakeMgmtEvent = NULL;
        }

        m_oThreadQueue.KillAll(TRUE, 0, 0);

        HANDLES(CloseHandle(m_hQuitEvent));
        m_hQuitEvent = NULL;
    }

    if (m_hWtsApi32 != NULL && m_hWtsApi32 != (HMODULE)INVALID_HANDLE_VALUE)
    {
        m_pfnWTSRegisterSessionNotification = NULL;
        m_pfnWTSUnRegisterSessionNotification = NULL;
        m_pfnWTSGetActiveConsoleSessionId = NULL;
        HANDLES(FreeLibrary(m_hWtsApi32));
        m_hWtsApi32 = NULL;
    }

    assert(m_cStandbyNodes == 0);

    // Purge the cache tree.
    Node nodeRoot = m_oTree.GetRoot();
    if (nodeRoot != NULL)
    {
        LockCache();
        m_oTree.Remove(nodeRoot);
        UnlockCache();
    }

    m_nodeTscFolder = NULL;
}

UINT CNethoodCache::GetPathStatus(
    __in PCWSTR pszPath,
    __in_opt CNethoodCacheEventConsumer* pEventConsumer,
    __out Node* node,
    __in unsigned uFlags,
    __out_opt std::wstring* targetPath)
{
    Node nodeParent = m_oTree.GetRoot();
    Node nodeChild;
    Node nodeFind = NULL;
    UINT uError = NO_ERROR;
    CUncPathParser oParser(pszPath);
    bool bPending = false;
    UINT uTokenClassification;
    bool bStandby = false;
    UINT uDelay;
    bool bRecovered = false;
    bool bForce = !!(uFlags & GPSF_FORCE);
    int cTokens;
    int iToken = 0;

    if (targetPath != NULL)
        targetPath->clear();

    // Consumer cannot be specified without also returning ther node.
    assert(node || !pEventConsumer);

    if (node)
    {
        *node = NULL;
    }

    // Validate whether the path is syntactically correct.
    uError = oParser.Validate(&cTokens);
    if (uError != NO_ERROR)
    {
        return uError;
    }

    uDelay = bForce ? 0 : REENUMERATION_DELAY;

    LockCache();

    while (uError == NO_ERROR && (oParser.NextToken() ||
                                  (nodeParent == m_oTree.GetRoot() &&
                                   oParser.GetLastError() == ERROR_NO_MORE_ITEMS)))
    {

        if (oParser.GetLastError() == ERROR_NO_MORE_ITEMS)
        {
            uError = ERROR_NO_MORE_ITEMS;
            nodeFind = nodeParent;
            uTokenClassification = 0;
        }
        else
        {
            uTokenClassification = oParser.GetTokenClassification();

            nodeFind = NULL;
            nodeChild = m_oTree.GetFirstChild(nodeParent);
            if (nodeChild != NULL)
            {
                nodeFind = FindNextSiblingNode(nodeChild, oParser);
            }

            if (nodeFind == NULL)
            {
                nodeFind = NewNode(nodeParent, oParser);
            }
        }

        CNethoodCacheNode& data = m_oTree.GetAt(nodeFind);
        if ((uTokenClassification & CUncPathParser::TokenServer) != 0)
        {
            data.SetHint(CNethoodCacheNode::HintServer);
        }

        bStandby = false;

        if (data.GetType() == CNethoodCacheNode::TypeShare)
        {
            uError = ERROR_NETHOODCACHE_FULL_UNC_PATH;
            if (targetPath != NULL)
            {
                GetUncPath(nodeFind, *targetPath);

                // Append remainder of the path to the
                // resulting UNC path.
                while (oParser.NextToken())
                {
                    targetPath->push_back(L'\\');
                    targetPath->append(oParser.GetTokenData(), oParser.GetTokenLength());
                }
            }
            break;
        }
        else if (data.GetType() == CNethoodCacheNode::TypeTSCVolume)
        {
            uError = ERROR_NETHOODCACHE_FULL_UNC_PATH;
            if (targetPath != NULL)
            {
                targetPath->assign(data.GetName());
                // Append remainder of the path to the
                // resulting UNC path.
                while (oParser.NextToken())
                {
                    targetPath->push_back(L'\\');
                    targetPath->append(oParser.GetTokenData(), oParser.GetTokenLength());
                }
            }
            break;
        }
        else if (data.GetType() == CNethoodCacheNode::TypeServer &&
                 iToken == cTokens - 1 &&
                 !(uFlags & GPSF_DONT_RESOLVE_SYMLINK) &&
                 m_displayTscVolumes != TSCDisplayNone)
        {
            if (oParser.GetTokenLength() == 8 &&
                _wcsicmp(oParser.GetTokenData(), L"tsclient") == 0)
            {
                uError = ERROR_NETHOODCACHE_SYMLINK;
                if (targetPath != NULL)
                {
                    targetPath->assign(L"\\");
                    if (m_displayTscVolumes == TSCDisplayFolder)
                    {
                        targetPath->append(m_oTree.GetAt(m_nodeTscFolder).GetDisplayName());
                    }
                }

                break;
            }
        }

        switch (data.GetStatus())
        {
        case CNethoodCacheNode::StatusUnknown:
            bPending = true;
            if (uFlags & GPSF_MANUAL)
            {
                data.SetStickyImmortal();
            }
            ScheduleEnumeration(nodeFind, nodeParent, 0);
            break;

        case CNethoodCacheNode::StatusOk:
            bStandby = true;
            break;

        case CNethoodCacheNode::StatusPending:
            bPending = true;
            break;

        case CNethoodCacheNode::StatusInvalid:
            uError = data.GetLastEnumerationResult();
            assert(uError != -1 && uError != NO_ERROR && uError != ERROR_NO_MORE_ITEMS);
            if (IsRecoverableError(uError))
            {
                bStandby = true;
                bRecovered = true;
                uError = NO_ERROR;
                uDelay = 0;
            }
            break;

        case CNethoodCacheNode::StatusStandby:
            break;

        case CNethoodCacheNode::StatusInvisible:
        case CNethoodCacheNode::StatusDead:
            uError = ERROR_BAD_NETPATH;
            break;

        default:
            assert(0);
        }

        nodeParent = nodeFind;
        ++iToken;
    }

    if (uError == NO_ERROR || uError == ERROR_NO_MORE_ITEMS)
    {
        uError = oParser.GetLastError();
        if (uError == ERROR_NO_MORE_ITEMS)
        {
            assert(nodeFind != NULL);
            if (node)
            {
                *node = nodeFind;
            }
            CNethoodCacheNode& nodeData = m_oTree.GetAt(nodeFind);
            uError = nodeData.GetLastEnumerationResult();

            if (bPending)
            {
                if (uError == -1)
                {
                    // The node currently holds no data yet.
                    uError = ERROR_IO_PENDING;
                }
            }

            assert(uError != -1);
            if (uError == ERROR_NO_MORE_ITEMS)
            {
                uError = NO_ERROR;
            }

            if (pEventConsumer != NULL)
            {
                nodeData.AddConsumer(pEventConsumer);
            }
        }
    }

    if (bStandby && nodeFind != m_nodeTscFolder)
    {
        ScheduleEnumeration(nodeFind, nodeParent, uDelay);
    }

    UnlockCache();

    if (bRecovered)
    {
        assert(uError != NO_ERROR && uError != -1);
        uError = NO_ERROR;
    }

    return uError;
}

void CNethoodCache::UnregisterConsumerWorker(
    __in Node node,
    __in CNethoodCacheEventConsumer* pEventConsumer,
    __in bool bInternal)
{
    unsigned cConsumers;
    bool bWakeUp = false;

    LockCache();

    CNethoodCacheNode& nodeData = m_oTree.GetAt(node);
    if (bInternal)
    {
        nodeData.RemoveInternalConsumer(pEventConsumer);
    }
    else
    {
        cConsumers = nodeData.RemoveConsumer(pEventConsumer);
        if ((cConsumers == 0) && (nodeData.GetStatus() == CNethoodCacheNode::StatusStandby))
        {
            // The node is on the stand-by list but nobody is interested
            // in the node anymore. Remove the node from the stand-by list.
            bWakeUp = RemoveNodeFromStandbyList(node);
            assert(bWakeUp);

            nodeData.RevertStatus();
        }
    }

    UnlockCache();

    if (bWakeUp)
    {
        assert(m_hWakeMgmtEvent != NULL);
        SetEvent(m_hWakeMgmtEvent);
    }
}

CNethoodCache::Node CNethoodCache::FindNextSiblingNode(
    __in Node startNode,
    __in const CUncPathParser& token)
{
    Node node;
    int iCompare = 0;

    node = startNode;
    while ((node != NULL) && (iCompare = CompareNode(node, token)) != 0)
    {
        node = m_oTree.GetNextSibling(node);
    }

    if (iCompare == 0)
    {
        return node;
    }

    return NULL;
}

CNethoodCache::Node CNethoodCache::FindNextSiblingNode(
    __in Node startNode,
    __in PCWSTR pszDisplayName)
{
    Node node;
    int iCompare = 0;

    node = startNode;
    while ((node != NULL) && (iCompare = CompareNode(node, pszDisplayName)) != 0)
    {
        node = m_oTree.GetNextSibling(node);
    }

    if (iCompare == 0)
    {
        return node;
    }

    return NULL;
}

CNethoodCache::Node CNethoodCache::NewNode(
    __in Node parentNode,
    __in const CUncPathParser& token)
{
    Node node;

    node = InsertNode(parentNode, NULL);
    CNethoodCacheNode& nodeData = m_oTree.GetAt(node);

    if ((token.GetTokenClassification() & CUncPathParser::TokenServer) != 0)
    {
        const std::wstring remoteName = token.GetPathUpToCurrentToken();
        nodeData.SetType(CNethoodCacheNode::TypeServer);
        nodeData.SetName(remoteName.c_str());
    }
    else if ((token.GetTokenClassification() & CUncPathParser::TokenShare) != 0)
    {
        const std::wstring shareName = token.GetPathUpToCurrentToken();
        nodeData.SetType(CNethoodCacheNode::TypeShare);
        nodeData.SetName(shareName.c_str());
    }
    else
    {
        nodeData.SetName(token.GetTokenData(),
                         static_cast<int>(token.GetTokenLength()));
    }

    // Notify parent that we added child node.
    assert(parentNode != NULL);
    m_oTree.GetAt(parentNode).NotifyNodeUpdated();

    return node;
}

int CNethoodCache::CompareNode(
    __in Node node,
    __in const CUncPathParser& token)
{
    const CNethoodCacheNode& nodeData = m_oTree.GetAt(node);

    return nodeData.Compare(token.GetTokenData(), token.GetTokenLength());
}

int CNethoodCache::CompareNode(
    __in Node node,
    __in PCWSTR pszDisplayName)
{
    const CNethoodCacheNode& nodeData = m_oTree.GetAt(node);

    return nodeData.Compare(pszDisplayName);
}

bool CNethoodCache::ScheduleEnumeration(
    __in Node node,
    __in Node nodeDependsOn,
    __in UINT uDelay)
{
    bool bImmediate = true;
    bool bWithConsumer = false;
    CNethoodCacheBeginDependentEnumerationConsumer* pConsumer;
    CNethoodCacheHitmanConsumer* pConsumerKiller;
    Node nodeToEnumerate = node;

    assert(node != NULL);
    assert(nodeDependsOn != NULL);

    if (uDelay == 0)
    {
        TRACE_IW(L"Scheduling enumeration of " << DbgGetNodeName(node));
    }

    TRACE_I("node=" << node << "[" << m_oTree.GetAt(node).GetStatus() << "], nodeDependsOn=" << nodeDependsOn << "[" << m_oTree.GetAt(nodeDependsOn).GetStatus() << "], uDelay=" << uDelay);

    if (nodeDependsOn != node)
    {
        CNethoodCacheNode& dataDependsOn = m_oTree.GetAt(nodeDependsOn);
        switch (dataDependsOn.GetStatus())
        {
        case CNethoodCacheNode::StatusDead:
        case CNethoodCacheNode::StatusInvalid:
            assert(0);
            return false;

        case CNethoodCacheNode::StatusUnknown:
            bImmediate = true;
            bWithConsumer = true;
            nodeToEnumerate = nodeDependsOn;
            break;

        case CNethoodCacheNode::StatusOk:
        case CNethoodCacheNode::StatusStandby:
            bImmediate = true;
            bWithConsumer = false;
            break;

        case CNethoodCacheNode::StatusPending:
            bImmediate = false;
            bWithConsumer = true;
            break;

        default:
            assert(0);
            return false;
        }
    }

    if (bImmediate)
    {
        if (uDelay != 0)
        {
            CNethoodCacheNode& nodeData = m_oTree.GetAt(node);

            assert(nodeToEnumerate == node);
            assert(nodeData.GetStatus() == CNethoodCacheNode::StatusOk || nodeData.GetStatus() == CNethoodCacheNode::StatusInvalid);

            nodeData.SetStatus(CNethoodCacheNode::StatusStandby);
            nodeData.NotifyNodeUpdated();
            nodeData.SetStandbyTimeout(uDelay);

            if (PutNodeOnStandbyList(node))
            {
                // TODO: Move the management-thread wake-up after the
                //       cache is unlocked.
                assert(m_hWakeMgmtEvent != NULL);
                SetEvent(m_hWakeMgmtEvent);
            }
        }
        else if (!BeginEnumerationThread(nodeToEnumerate))
        {
            TRACE_E("Failed to create enumeration thread");
            assert(0);
            return false;
        }
    }

    if (bWithConsumer)
    {
        assert(nodeDependsOn != node);

        pConsumer = new CNethoodCacheBeginDependentEnumerationConsumer(this, node);
        m_oTree.GetAt(node).SetStatus(CNethoodCacheNode::StatusPending);
        m_oTree.GetAt(node).NotifyNodeUpdated();
        m_oTree.GetAt(nodeDependsOn).AddInternalConsumer(pConsumer);

        pConsumerKiller = new CNethoodCacheHitmanConsumer(this, nodeDependsOn, pConsumer);
        m_oTree.GetAt(node).AddInternalConsumer(pConsumerKiller);

        pConsumer->SetHitman(pConsumerKiller);
    }

    return true;
}

CNethoodCache::Node CNethoodCache::InsertNode(
    __in_opt Node nodeParent,
    __in_opt Node nodeInsertAfter)
{
    Node node;

    node = m_oTree.Insert(nodeParent, nodeInsertAfter);
    CNethoodCacheNode& data = m_oTree.GetAt(node);

    data.Create(this, node);

    return node;
}

bool CNethoodCache::BeginEnumerationThread(__in Node node)
{
    CNethoodCacheEnumerationThread* pEnumThread;

    pEnumThread = new CNethoodCacheEnumerationThread(this, node);
    if (pEnumThread->Create(m_oThreadQueue) == NULL)
    {
        delete pEnumThread;
        return false;
    }

    CNethoodCacheNode& nodeData = m_oTree.GetAt(node);
    if (nodeData.GetStatus() != CNethoodCacheNode::StatusPending)
    {
        nodeData.SetStatus(CNethoodCacheNode::StatusPending);
        nodeData.NotifyNodeUpdated();
    }
    return true;
}

bool CNethoodCache::GetPathWorker(
    __in Node node,
    __out std::wstring& path,
    __in PathAssemblyType type)
{
    PCWSTR pszName;
    CNethoodCacheNode::Type nodeType;

    assert(node != NULL);
    path.clear();

    /*assert((type == PathUnc && m_oTree.GetAt(node).GetType() == CNethoodCacheNode::TypeShare) || type == PathFull);
	if (type == PathUnc && m_oTree.GetAt(node).GetType() != CNethoodCacheNode::TypeShare)
	{
		return false;
	}*/

    nodeType = m_oTree.GetAt(node).GetType();
    if (type == PathUnc)
    {
        if (nodeType == CNethoodCacheNode::TypeTSCVolume)
        {
            path.assign(m_oTree.GetAt(node).GetName());
            return true;
        }
        else if (nodeType == CNethoodCacheNode::TypeTSCFolder)
        {
            path.assign(L"\\\\tsclient");
            return true;
        }
    }

    while (node != NULL)
    {
        CNethoodCacheNode& nodeData = m_oTree.GetAt(node);

        pszName = nodeData.GetDisplayName();
        if (pszName == NULL)
        {
            if (path.empty())
            {
                // Root node.
                path.assign(L"\\");
                return true;
            }
            break;
        }

        path.insert(0, pszName);
        path.insert(path.begin(), L'\\');

        node = m_oTree.GetParent(node);

        if ((nodeData.GetType() == CNethoodCacheNode::TypeServer) &&
            (((node != NULL) && m_oTree.GetAt(node).GetHint() == CNethoodCacheNode::HintRoot) ||
             (type == PathUnc)))
        {
            path.insert(path.begin(), L'\\');

            if (type == PathUnc)
            {
                break;
            }
        }
    }

    return true;
}

bool CNethoodCache::PutNodeOnStandbyList(__in Node node)
{
    bool res = true;

    TRACE_IW(L"Putting " << DbgGetNodeName(node) << L" on stand-by list");

    LockCache();

    CNethoodCacheNode& nodeData = m_oTree.GetAt(node);
    assert(nodeData.m_standbyListEntry.pNext == nodeData.m_standbyListEntry.pPrev);

    CNethoodCacheListEntry::InsertAtTail(&m_standbyNodes, &nodeData.m_standbyListEntry);
    if (m_cStandbyNodes++ == 0 && m_hWakeMgmtEvent == NULL)
    {
        // First node placed on the stand-by list.
        // Create the management thread.

        res = CreateMgmtThread();
        if (!res)
        {
            CNethoodCacheListEntry::Remove(&nodeData.m_standbyListEntry);
            --m_cStandbyNodes;
        }
    }

    UnlockCache();

    return res;
}

bool CNethoodCache::CreateMgmtThread()
{
    bool res = true;
    CNethoodCacheManagementThread* pMgmtThread;

    assert(m_hWakeMgmtEvent == NULL);

    m_hWakeMgmtEvent = HANDLES(CreateEvent(NULL, FALSE, FALSE, NULL));
    if (m_hWakeMgmtEvent == NULL)
    {
        res = false;
    }
    else
    {
        pMgmtThread = new CNethoodCacheManagementThread(this);
        m_hMgmtThread = pMgmtThread->Create(m_oThreadQueue);

        if (m_hMgmtThread == NULL)
        {
            delete pMgmtThread;
            HANDLES(CloseHandle(m_hWakeMgmtEvent));
            m_hWakeMgmtEvent = NULL;
            res = false;
        }
    }

    return res;
}

bool CNethoodCache::RemoveNodeFromStandbyList(__in Node node)
{
    TRACE_IW(L"Removing " << DbgGetNodeName(node) << L" from stand-by list");

    LockCache();

    CNethoodCacheNode& nodeData = m_oTree.GetAt(node);

    assert(nodeData.GetStatus() == CNethoodCacheNode::StatusStandby);
    assert(CNethoodCacheListEntry::Search(&m_standbyNodes, &nodeData.m_standbyListEntry));
    assert(m_cStandbyNodes > 0);
    CNethoodCacheListEntry::Remove(&nodeData.m_standbyListEntry);
    --m_cStandbyNodes;

    assert(m_cStandbyNodes > 0 || CNethoodCacheListEntry::IsEmpty(&m_standbyNodes));

    UnlockCache();

    return true;
}

CNethoodCache::Node CNethoodCache::FindValidItem(
    __in Node node,
    __in_opt CNethoodCacheEventConsumer* pEventConsumer)
{
    Node nodeParent;

    LockCache();

    // The returned node should be an ancestor of the node passed as the
    // parameter. We must never return the same node; otherwise we can
    // cause an infinite loop. We only violate this rule for the root node,
    // but the root node should always be valid and no one should question
    // that.
    nodeParent = m_oTree.GetParent(node);
    assert(nodeParent != NULL);
    if (nodeParent != NULL)
    {
        node = nodeParent;
    }

    while (node != NULL)
    {
        CNethoodCacheNode& nodeData = m_oTree.GetAt(node);
        CNethoodCacheNode::Status status = nodeData.GetStatus();

        if (status == CNethoodCacheNode::StatusOk ||
            status == CNethoodCacheNode::StatusStandby ||
            (status == CNethoodCacheNode::StatusPending && nodeData.GetLastEnumerationResult() != -1))
        {
            if (pEventConsumer != NULL)
            {
                nodeData.AddConsumer(pEventConsumer);
            }

            break;
        }

        node = m_oTree.GetParent(node);
    }

    UnlockCache();

    assert(node != NULL);

    return node;
}

bool CNethoodCache::FindAccessiblePath(
    __in Node node,
    __out std::wstring& path)
{
    bool bRet = true;
    Node nodeValid;

    LockCache();

    nodeValid = FindValidItem(node, NULL);
    if (nodeValid != NULL)
    {
        bRet = GetFullPath(nodeValid, path);
    }
    else
    {
        path.assign(L"\\");
    }

    UnlockCache();

    return bRet;
}

void CNethoodCache::SpreadError(
    __in Node node,
    __in UINT uError,
    __in SpreadMode mode)
{
    Node nodeChild;
    Node nodeNext;

    nodeChild = m_oTree.GetFirstChild(node);
    while (nodeChild != NULL)
    {
        nodeNext = m_oTree.GetNextSibling(nodeChild);
        SpreadError(nodeChild, uError,
                    //(mode == SpreadRemoveChildren) ? SpreadRemoveAll : mode);
                    SpreadOnly);
        nodeChild = nodeNext;
    }

    CNethoodCacheNode& data = m_oTree.GetAt(node);
    data.SetLastEnumerationResult(uError);
    data.SetStatus(CNethoodCacheNode::StatusInvalid);
    data.NotifyNodeUpdated();

    if (mode & _SpreadRemoveSelf)
    {
        RemoveItem(node);
    }
}

UINT CNethoodCache::EnsurePathExists(__in PCWSTR pszPath)
{
    CUncPathParser oParser(pszPath);
    Node nodeRoot = m_oTree.GetRoot();
    Node nodeChild, nodeServer, nodeShare;
    UINT uError;
    bool bTsc;

    // Process the server part.

    if (!oParser.NextToken())
    {
        uError = oParser.GetLastError();
        if (uError == ERROR_NO_MORE_ITEMS)
        {
            uError = NO_ERROR;
        }
        return uError;
    }

    if ((oParser.GetTokenClassification() & CUncPathParser::TokenServer) == 0)
    {
        return ERROR_INVALID_PARAMETER;
    }

    bTsc = (oParser.GetTokenLength() == 8) &&
           _wcsnicmp(oParser.GetTokenData(), L"tsclient", 8) == 0 &&
           (m_displayTscVolumes != TSCDisplayNone);

    LockCache();

    if (bTsc)
    {
        if (m_nodeTscFolder != NULL)
        {
            CNethoodCacheNode& nodeTscFolder = m_oTree.GetAt(m_nodeTscFolder);
            if (nodeTscFolder.GetStatus() == CNethoodCacheNode::StatusInvisible)
            {
                nodeTscFolder.SetStatus(CNethoodCacheNode::StatusOk);
            }
            nodeServer = m_nodeTscFolder;
        }
        else
        {
            assert(m_displayTscVolumes != TSCDisplayRoot);
            nodeServer = nodeRoot;
        }
    }
    else
    {
        nodeChild = m_oTree.GetFirstChild(nodeRoot);
        nodeServer = FindNextSiblingNode(nodeChild, oParser);
        if (nodeServer == NULL)
        {
            nodeServer = NewNode(nodeRoot, oParser);
            m_oTree.GetAt(nodeServer).SetImmortality();
        }
    }

    // Process the share part.

    if (!oParser.NextToken())
    {
        uError = oParser.GetLastError();
        if (uError == ERROR_NO_MORE_ITEMS)
        {
            uError = NO_ERROR;
        }
        UnlockCache();
        return uError;
    }

    if ((oParser.GetTokenClassification() & CUncPathParser::TokenShare) == 0)
    {
        UnlockCache();
        return ERROR_INVALID_PARAMETER;
    }

    nodeChild = m_oTree.GetFirstChild(nodeServer);
    if (bTsc)
    {
        CTsClientName oClientName(this);
        CTsNameFormatter oFormatter;
        wchar_t szTrueDisplayName[128];
        wchar_t szVolumeName[16];

        StringCchCopyNW(szVolumeName, COUNTOF(szVolumeName),
                       oParser.GetTokenData(), oParser.GetTokenLength());

        oFormatter.Format(m_displayTscVolumes, szVolumeName,
                          &oClientName, szTrueDisplayName, COUNTOF(szTrueDisplayName));

        nodeShare = FindNextSiblingNode(nodeChild, szTrueDisplayName);
        if (nodeShare == NULL)
        {
            nodeShare = NewNode(nodeServer, oParser);
            CNethoodCacheNode& nodeTscVolume = m_oTree.GetAt(nodeShare);
            nodeTscVolume.SetType(CNethoodCacheNode::TypeTSCVolume);
            nodeTscVolume.SetDisplayName(szTrueDisplayName);
            nodeTscVolume.SetImmortality();
        }

        // Refresh TS volumes (enumerated as a part of the root).
        GetPathStatus(L"\\", NULL, NULL);
    }
    else
    {
        nodeShare = FindNextSiblingNode(nodeChild, oParser);
        if (nodeShare == NULL)
        {
            nodeShare = NewNode(nodeServer, oParser);
            m_oTree.GetAt(nodeShare).SetImmortality();
        }
    }

    // Ignore the rest of the path.

    UnlockCache();

    return NO_ERROR;
}

void CNethoodCache::AddRefNode(__in Node node)
{
    LockCache();

    assert(m_oTree.GetAt(node).GetStatus() != CNethoodCacheNode::StatusDead);
    LONG cRef = m_oTree.GetAt(node).AddRef();
    TRACE_IW(L"The node " << DbgGetNodeName(node) << L" was referenced, cRef=" << cRef);

    UnlockCache();
}

void CNethoodCache::ReleaseNode(__in Node node)
{
#ifdef TRACE_ENABLE
    wchar_t szNodeName[64];
    StringCchCopyW(szNodeName, COUNTOF(szNodeName), DbgGetNodeName(node));
#endif

    LockCache();

    LONG cRef = m_oTree.GetAt(node).Release();
    TRACE_IW(L"The node " << szNodeName << L" was DEreferenced, cRef=" << cRef);

    if (cRef == 0 && m_oTree.GetAt(node).GetStatus() == CNethoodCacheNode::StatusDead)
    {
        TRACE_IW(L"Last reference to the dead node " << szNodeName << L" was released and is going to be destroyed.");
        m_oTree.Remove(node);
    }

    UnlockCache();
}

void CNethoodCache::RemoveItem(__in Node node)
{
    assert(IsLocked());
    assert(m_oTree.GetAt(node).GetStatus() != CNethoodCacheNode::StatusDead);

#if 0
	Node nodeChild, nodeNext;
	
        // Walk every child manually to dispatch notifications and release
        // them. Simply calling TTree::Remove would skip notifications and
        // would forcibly remove the child nodes without respecting the
        // reference counts.

	nodeChild = m_oTree.GetFirstChild(node);
	while (nodeChild != NULL)
	{
		nodeNext = m_oTree.GetNextSibling(nodeChild);
		RemoveItem(nodeChild);
		nodeChild = nodeNext;
	}
#endif

    // Notify everyone that the node is vanishing.
    m_oTree.GetAt(node).SetStatus(CNethoodCacheNode::StatusDead);
    m_oTree.GetAt(node).NotifyNodeUpdated();

    m_oTree.Remove(node);
}

void CNethoodCache::SetDisplayTSClientVolumes(TSCDisplayMode mode)
{
    m_displayTscVolumes = mode;
    if (mode != TSCDisplayNone)
    {
        if (!EnsureWtsApiInitialized())
        {
            m_displayTscVolumes = TSCDisplayNone;
        }
    }

    if (mode == TSCDisplayFolder && m_nodeTscFolder == NULL)
    {
        m_nodeTscFolder = InsertNode(m_oTree.GetRoot(), NULL);
        CNethoodCacheNode& nodeTscFolder = m_oTree.GetAt(m_nodeTscFolder);
        nodeTscFolder.SetHint(CNethoodCacheNode::HintTSCFolder);
        NETRESOURCEW sNetResource = {
            0,
        };
        // Named local: SPLLoadStrOwned returns by value, so taking .c_str()
        // straight into lpRemoteName left it dangling at the end of that
        // statement - before SetNetResource() below ever read it.
        std::wstring tscFolderNameW =
            SPLLoadStrOwned(SalamanderGeneral, GetLangInstance(), IDS_TSFOLDER);
        sNetResource.lpRemoteName = const_cast<LPWSTR>(tscFolderNameW.c_str());
        sNetResource.dwUsage = RESOURCEUSAGE_CONTAINER;
        nodeTscFolder.SetNetResource(&sNetResource);
        nodeTscFolder.SetType(CNethoodCacheNode::TypeTSCFolder);
        nodeTscFolder.SetStatus(CNethoodCacheNode::StatusInvisible);
        nodeTscFolder.SetLastEnumerationResult(NO_ERROR);
    }
    else if (mode != TSCDisplayFolder && m_nodeTscFolder != NULL)
    {
        CNethoodCacheNode& nodeTscFolder = m_oTree.GetAt(m_nodeTscFolder);
        nodeTscFolder.SetStatus(CNethoodCacheNode::StatusInvisible);
    }
}

bool CNethoodCache::EnsureWtsApiInitialized()
{
    if (m_hWtsApi32 == (HMODULE)INVALID_HANDLE_VALUE)
    {
        TRACE_E("Not trying to initialize WTSAPI because the previous try was not successful.");
        return false;
    }

    if (m_hWtsApi32 != NULL)
    {
        // Already successfully initialized, nothing to do.
        return true;
    }

    HMODULE hWtsApi32 = HANDLES(LoadLibraryW(L"wtsapi32.dll"));
    if (hWtsApi32 != NULL)
    {
        m_pfnWTSRegisterSessionNotification = (PFN_WTSRegisterSessionNotification)
            GetProcAddress(hWtsApi32, "WTSRegisterSessionNotification"); // Min: XP

        m_pfnWTSUnRegisterSessionNotification = (PFN_WTSUnRegisterSessionNotification)
            GetProcAddress(hWtsApi32, "WTSUnRegisterSessionNotification"); // Min: XP

        m_pfnWTSGetActiveConsoleSessionId = (PFN_WTSGetActiveConsoleSessionId)
            GetProcAddress(hWtsApi32, "WTSGetActiveConsoleSessionId"); // Min: XP
    }

    if (hWtsApi32 == NULL ||
        m_pfnWTSRegisterSessionNotification == NULL ||
        m_pfnWTSUnRegisterSessionNotification == NULL)
    {
        TRACE_E("Unable to load WTSAPI.");

        // Mark failure.
        m_hWtsApi32 = (HMODULE)INVALID_HANDLE_VALUE;

        // Sanity clean the function pointers.
        m_pfnWTSRegisterSessionNotification = NULL;
        m_pfnWTSUnRegisterSessionNotification = NULL;
        m_pfnWTSGetActiveConsoleSessionId = NULL;
    }

    if (m_hWakeMgmtEvent == NULL)
    {
        // Create the management thread that will take care of
        // the notify window message loop.

        LockCache();

        CreateMgmtThread();

        UnlockCache();
    }

    if (m_hWakeMgmtEvent != NULL)
    {
        SetEvent(m_hWakeMgmtEvent);
    }

    m_hWtsApi32 = hWtsApi32;

    return true;
}

void CNethoodCache::WTSSessionChange(DWORD dwSessionId, int nStatus)
{
    static const char* STATUS_NAMES[] = {"??? (0)", "CONSOLE_CONNECT",
                                         "CONSOLE_DISCONNECT", "REMOTE_CONNECT", "REMOTE_DISCONNECT",
                                         "SESSION_LOGON", "SESSION_LOGOFF", "SESSION_LOCK",
                                         "SESSION_UNLOCK", "SESSION_REMOTE_CONTROL"};

    TRACE_I("WTS session change, session id=" << dwSessionId << ", status=" << (nStatus < COUNTOF(STATUS_NAMES) ? STATUS_NAMES[nStatus] : "???"));

    LPWSTR pszClientName;
    DWORD cbReturned;

    if (WTSQuerySessionInformation(
            WTS_CURRENT_SERVER_HANDLE,
            dwSessionId,
            WTSClientName,
            &pszClientName,
            &cbReturned))
    {
        TRACE_IW(L"Client name=" << pszClientName);

        WTSFreeMemory(pszClientName);
    }
}

bool CNethoodCache::AreTSAvailable()
{
    // TODO: Cache result of this method.

    bool bAvailable = false;

    SC_HANDLE hScManager;

    hScManager = OpenSCManager(NULL, SERVICES_ACTIVE_DATABASE, GENERIC_READ);
    if (hScManager != NULL)
    {
        SC_HANDLE hTsService;

        hTsService = OpenServiceW(hScManager, L"TermService", SERVICE_QUERY_STATUS);
        if (hTsService != NULL)
        {
            SERVICE_STATUS status;

            if (QueryServiceStatus(hTsService, &status))
            {
                bAvailable = (status.dwCurrentState == SERVICE_RUNNING);
            }

            CloseServiceHandle(hTsService);
        }

        CloseServiceHandle(hScManager);
    }

    return bAvailable;
}

//------------------------------------------------------------------------------
// CNethoodCacheEnumerationThread

CNethoodCacheEnumerationThread::CNethoodCacheEnumerationThread(
    __in CNethoodCache* pCache,
    __in CNethoodCache::Node node) : _baseClass()
{
    Name = SPLFormatStringOwned(L"NethoodCacheEnum:%p", node);

    m_pCache = pCache;
    m_node = node;
}

CNethoodCacheEnumerationThread::~CNethoodCacheEnumerationThread()
{
}

unsigned CNethoodCacheEnumerationThread::Body()
{
    DWORD dwError;
    CNethoodCacheNode& data = m_pCache->m_oTree.GetAt(m_node);
    HANDLE hEnum;
    DWORD dwScope;
    DWORD dwType;
    DWORD dwUsage;
    NETRESOURCEW* pNetResource = NULL;
    NETRESOURCEW sNetResource = {
        0,
    };
    NethoodResourceBuffer enumStorage;
    DWORD cEntries = 0;

#if DBG_TRUNCATE_EVERY_NTH_ENUM || DBG_FAIL_EVERY_NTH_ENUM
    static LONG nStaticDbgEnumCounter;
    LONG nDbgEnumCounter = InterlockedIncrement(&nStaticDbgEnumCounter);
#endif

    TRACE_IW(L"Starting enumeration of " << m_pCache->DbgGetNodeName(m_node));

    if (data.GetHint() == CNethoodCacheNode::HintRoot)
    {
        dwScope = RESOURCE_CONTEXT;
        dwType = RESOURCETYPE_ANY;
        dwUsage = RESOURCEUSAGE_ALL;
        pNetResource = NULL;

        m_pCache->LockCache();
        BeginEnumeration(PhaseShortcuts);
        if (m_pCache->GetDisplayNetworkShortcuts())
        {
            EnumNetworkShortcuts();
            data.NotifyNodeUpdated();
        }
        m_pCache->UnlockCache();
    }
    else
    {
        dwScope = RESOURCE_GLOBALNET;
        dwType = RESOURCETYPE_DISK;
        dwUsage = RESOURCEUSAGE_ALL;
        if (data.GetNetResource(sNetResource))
        {
            pNetResource = &sNetResource;
        }
        else if (data.GetHint() == CNethoodCacheNode::HintServer)
        {
            sNetResource.lpRemoteName = const_cast<PWSTR>(data.GetName());
#if 0
			DWORD cbBuffer = 1024;
			PWSTR pszSystem;

			void *pBuffer = _alloca(cbBuffer);

			sNetResource.dwType = RESOURCETYPE_DISK;
			dwError = WNetGetResourceInformation(&sNetResource, pBuffer, &cbBuffer, &pszSystem);
			if (dwError == NO_ERROR)
			{
				pNetResource = (NETRESOURCEW *)pBuffer;
			}
#endif
            pNetResource = &sNetResource;
        }
        else
        {
            sNetResource.lpRemoteName = const_cast<PWSTR>(data.GetName());
            pNetResource = &sNetResource;
            //assert(0);
        }
    }

#if DBG_PROLONG_ENUMERATION > 0
    Sleep(DBG_PROLONG_ENUMERATION);
#endif

#if DBG_FAIL_EVERY_NTH_ENUM
    if (nDbgEnumCounter % DBG_FAIL_EVERY_NTH_ENUM == 0)
    {
        dwError = DBG_FAIL_ENUM_CODE;
    }
    else
    {
#endif

        dwError = OpenEnum(dwScope, dwType, dwUsage, pNetResource, &hEnum);
        dwError = ExtendWNetError(dwError);

#if DBG_FAIL_EVERY_NTH_ENUM
    }
#endif

    if (dwError == NO_ERROR)
    {
        m_pCache->LockCache();
        BeginEnumeration(PhaseNetwork);

        for (;;)
        {
            NETRESOURCEW* resources = NULL;
            dwError = NethoodEnumerateResourceBatch(hEnum, cEntries, enumStorage, resources);
            dwError = ExtendWNetError(dwError);
            if (dwError == NO_ERROR && cEntries > 0)
            {
#if DBG_TRUNCATE_EVERY_NTH_ENUM
                if (cEntries > 1 && nDbgEnumCounter % DBG_TRUNCATE_EVERY_NTH_ENUM == 0)
                {
                    cEntries = 1;
                    break;
                }
#endif

                ProcessEnumeration(resources, cEntries, 0);
            }
            else
            {
                break;
            }
        }

        if (dwError == ERROR_NO_MORE_ITEMS)
        {
            dwError = NO_ERROR;
        }

        CloseEnum(hEnum);
        hEnum = NULL;

        if (dwError == NO_ERROR &&
            m_pCache->GetDisplaySystemShares() != CNethoodCache::SysShareNone &&
            (data.GetHint() == CNethoodCacheNode::HintServer ||
             (pNetResource != NULL && pNetResource->dwDisplayType == RESOURCEDISPLAYTYPE_SERVER)))
        {
            m_pCache->UnlockCache();
            EnumHiddenShares(pNetResource->lpRemoteName);
            m_pCache->LockCache();
        }

        if (m_pCache->m_nodeTscFolder != NULL)
        {
            InvalidateNetResources(m_pCache->m_nodeTscFolder, PhaseTerminalServices);
        }

        if (dwError == NO_ERROR &&
            m_pCache->GetDisplayTSClientVolumes() != CNethoodCache::TSCDisplayNone &&
            data.GetHint() == CNethoodCacheNode::HintRoot &&
            SalamanderGeneral->IsRemoteSession())
        {
            // "commit" results of previous enumeration
            data.NotifyNodeUpdated();

            if (m_pCache->m_nodeTscFolder != NULL)
            {
                CNethoodCacheNode& nodeTscFolder = m_pCache->m_oTree.GetAt(m_pCache->m_nodeTscFolder);
                if (nodeTscFolder.GetStatus() != CNethoodCacheNode::StatusInvisible)
                {
                    nodeTscFolder.SetStatus(CNethoodCacheNode::StatusPending);
                    nodeTscFolder.NotifyNodeUpdated();
                }
            }

            m_pCache->UnlockCache();
            EnumTSClientVolumes();
            m_pCache->LockCache();
        }

        // Show/hide virtual folder based on the fact, whether
        // remote volumes were enumerated or not.
        if (m_pCache->m_nodeTscFolder != NULL)
        {
            bool bHasChildren = (m_pCache->GetFirstItem(m_pCache->m_nodeTscFolder) != NULL);
            CNethoodCacheNode& nodeTscFolder = m_pCache->m_oTree.GetAt(m_pCache->m_nodeTscFolder);
            if (bHasChildren && (nodeTscFolder.GetStatus() == CNethoodCacheNode::StatusInvisible ||
                                 nodeTscFolder.GetStatus() == CNethoodCacheNode::StatusPending))
            {
                nodeTscFolder.SetStatus(CNethoodCacheNode::StatusOk);
                nodeTscFolder.NotifyNodeUpdated();
            }
            else if (!bHasChildren && nodeTscFolder.GetStatus() != CNethoodCacheNode::StatusInvisible)
            {
                nodeTscFolder.SetStatus(CNethoodCacheNode::StatusInvisible);
                nodeTscFolder.NotifyNodeUpdated();
            }

            RemoveInvalidNetResources(m_pCache->m_nodeTscFolder);
        }

        data.SetLastEnumerationResult(dwError);
        EndEnumeration(dwError);

        m_pCache->UnlockCache();
    }
    else
    {
        TRACE_IW(L"Enumeration of " << m_pCache->DbgGetNodeName(m_node) << L" failed (WNet error=" << dwError << L")");

        m_pCache->LockCache();
        /*data.SetLastEnumerationResult(dwError);
		EndProcessEnumeration(dwError);*/
        m_pCache->SpreadError(m_node, dwError, CNethoodCache::SpreadOnly);
        m_pCache->UnlockCache();
    }

    return 0;
}

void CNethoodCacheEnumerationThread::ProcessEnumeration(
    __in CNethoodCache::Node nodeParent,
    __in const NETRESOURCEW* pNetResource,
    __in DWORD cElements,
    __in UINT uFlags,
    __in_opt const CTsClientName* poClientName)
{
    CNethoodCache::Node nodeUpdate;
    CTsNameFormatter oTsNameFormatter;

    while (cElements)
    {
        nodeUpdate = FindNode(nodeParent, pNetResource);
        if (nodeUpdate == NULL)
        {
            if (m_pCache->GetDisplayTSClientVolumes() != CNethoodCache::TSCDisplayNone &&
                m_pCache->m_oTree.GetAt(nodeParent).IsRoot() &&
                pNetResource->lpRemoteName &&
                _wcsicmp(pNetResource->lpRemoteName, L"\\\\tsclient") == 0)
            {
                // Hide \\tsclient from the root view.
                --cElements;
                ++pNetResource;
                continue;
            }
            else
            {
                // Create a new node.
                nodeUpdate = m_pCache->InsertNode(nodeParent, NULL);
            }
        }
        else
        {
            // Update the existing node.
        }

        CNethoodCacheNode& dataUpdate = m_pCache->m_oTree.GetAt(nodeUpdate);
        dataUpdate.SetShortcut((uFlags & ProcessShortcut) != 0); // SetShortcut must be called before SetNetResource!
        dataUpdate.SetNetResource(pNetResource);
        dataUpdate.ResetImmortality();
        dataUpdate.SetAttributes((uFlags & ProcessHidden) != 0);
        if (uFlags & ProcessTSCVolume)
        {
            assert(poClientName != NULL);
            dataUpdate.SetType(CNethoodCacheNode::TypeTSCVolume);

            if (oTsNameFormatter.NeedsExtraFormatting(m_pCache->GetDisplayTSClientVolumes()))
            {
                wchar_t szNewDisplayName[64];

                oTsNameFormatter.Format(
                    m_pCache->GetDisplayTSClientVolumes(),
                    dataUpdate.GetDisplayName(),
                    poClientName,
                    szNewDisplayName,
                    COUNTOF(szNewDisplayName));

                dataUpdate.SetDisplayName(szNewDisplayName);
            }
        }

        CNethoodCache::Node nP = m_pCache->m_oTree.GetParent(nodeUpdate);
        if (nP)
        {
            CNethoodCacheNode& dataParent = m_pCache->m_oTree.GetAt(nP);
            if (dataParent.TestAndResetStickyImmortal())
            {
                dataParent.SetImmortality();
            }
        }

        --cElements;
        ++pNetResource;
    }
}

void CNethoodCacheEnumerationThread::ProcessShareInfo(
    __in PCWSTR pszServerName,
    __in const SHARE_INFO_1& sShareInfo)
{
    NETRESOURCEW sNetResource = {
        0,
    };
    std::wstring remoteName = pszServerName;
    if (!remoteName.empty() && remoteName.back() != L'\\')
        remoteName.push_back(L'\\');
    remoteName.append(sShareInfo.shi1_netname);

    sNetResource.dwScope = RESOURCE_GLOBALNET;
    sNetResource.dwType = RESOURCETYPE_DISK;
    sNetResource.dwDisplayType = RESOURCEDISPLAYTYPE_SHARE;
    sNetResource.dwUsage = RESOURCEUSAGE_CONNECTABLE;

    // NETRESOURCE is NETRESOURCEW throughout this file now, so this
    // member is always wide - the #ifdef _UNICODE/#else split that used to exist here (one
    // genuinely wide branch, one building a narrow szRemoteName for a narrow sNetResource)
    // no longer has a narrow case to handle. Collapsed to the wide-only logic; this does NOT
    // define UNICODE (that is a separate, deliberately-deferred flip).
    sNetResource.lpComment = sShareInfo.shi1_remark;
    sNetResource.lpRemoteName = const_cast<PWSTR>(remoteName.c_str());

    ProcessEnumeration(&sNetResource, 1,
                       m_pCache->GetDisplaySystemShares() == CNethoodCache::SysShareDisplayHidden ? ProcessHidden : 0);
}

void CNethoodCacheEnumerationThread::BeginEnumeration(__in EnumerationPhase phase)
{
    // At the beginning of the process invalidate
    // all the nodes. After the process ends, delete
    // nodes that were not updated during the process.
    InvalidateNetResources(phase);
}

void CNethoodCacheEnumerationThread::EndEnumeration(__in DWORD dwResult)
{
    if (dwResult == NO_ERROR)
    {
        CNethoodCacheNode& data = m_pCache->m_oTree.GetAt(m_node);

        // Remove all the nodes that were not updated.
        RemoveInvalidNetResources();

        data.SetStatus(CNethoodCacheNode::StatusOk);

        // Children were added or removed and the status of the node
        // changed at least, notify the parent about this.
        data.NotifyNodeUpdated();
    }
    else
    {
        // Invalidate this node and all of its children and send
        // notifications to the registered consumers.
        Invalidate();

        // Do not remove items that fail to enumerate, since
        // Windows insists on keeping those items online. As a result
        // the items are hiding and showing again and again as the
        // paths are being reenumerated which is very distracting.
        // So just give Windows some time to accept that the item is gone.
#if 0		
		// If the node failed to enumerate and it was not an access
		// control related problem then remove the node and notify the
		// parent about it so it can refresh the panel.
		if (!IsLogonFailure(dwResult) && dwResult != ERROR_CANCELLED && // The user might dismissed the logon dialog.
			dwResult != ERROR_SESSION_CREDENTIAL_CONFLICT)
		{
			CNethoodCache::Node nodeParent = m_pCache->m_oTree.GetParent(m_node);

			m_pCache->RemoveItem(m_node);
			assert(nodeParent != NULL);
			if (nodeParent != NULL)
			{
				m_pCache->m_oTree.GetAt(nodeParent).NotifyNodeUpdated();
			}
		}
#endif
    }
}

void CNethoodCacheEnumerationThread::Invalidate()
{
    CNethoodCacheNode& data = m_pCache->m_oTree.GetAt(m_node);
    data.Invalidate();
}

CNethoodCache::Node CNethoodCacheEnumerationThread::FindNode(
    __in CNethoodCache::Node nodeParent,
    __in const NETRESOURCEW* pNetResource)
{
    CNethoodCache::Node nodeFirstChild;
    CNethoodCache::Node nodeFind = NULL;

    nodeFirstChild = m_pCache->GetFirstItem(nodeParent);
    if (nodeFirstChild != NULL)
    {
        nodeFind = m_pCache->FindNextSiblingNode(
            nodeFirstChild,
            CNethoodCacheNode::GetDisplayNameFromNetResource(pNetResource));
    }

    return nodeFind;
}

void CNethoodCacheEnumerationThread::InvalidateNetResources(
    __in CNethoodCache::Node nodeParent,
    __in EnumerationPhase phase)
{
    CNethoodCache::Node nodeChild;

    nodeChild = m_pCache->GetFirstItem(nodeParent);

    while (nodeChild != NULL)
    {
        bool bShortcut = m_pCache->m_oTree.GetAt(nodeChild).IsShortcut();
        if (bShortcut == (phase == PhaseShortcuts))
        {
            m_pCache->InvalidateNetResource(nodeChild);
        }
        nodeChild = m_pCache->GetNextItem(nodeChild);
    }
}

int CNethoodCacheEnumerationThread::RemoveInvalidNetResources(
    __in CNethoodCache::Node nodeParent)
{
    CNethoodCache::Node nodeChild;
    CNethoodCache::Node nodeNext;
    int cRemoved = 0;

    nodeChild = m_pCache->GetFirstItem(nodeParent);

    while (nodeChild != NULL)
    {
        if (!m_pCache->GetItemData(nodeChild).IsNetResourceValid())
        {
            nodeNext = m_pCache->GetNextItem(nodeChild);
            if (m_pCache->GetItemData(nodeChild).GetStatus() == CNethoodCacheNode::StatusPending)
            {
                // We cannot free the node while it is being enumerated.
            }
            else
            {
                if (!m_pCache->m_oTree.GetAt(nodeChild).IsImmortal())
                {
                    m_pCache->RemoveItem(nodeChild);
                    ++cRemoved;
                }
            }
            nodeChild = nodeNext;
        }
        else
        {
            nodeChild = m_pCache->GetNextItem(nodeChild);
        }
    }

    return cRemoved;
}

DWORD CNethoodCacheEnumerationThread::ExtendWNetError(__in DWORD dwError)
{
    if (dwError == ERROR_EXTENDED_ERROR)
    {
        DWORD dwExtendedError;
        // Widened: this buffer is purely internal (never
        // returned to a caller), so switching to the W form loses no data and
        // needs no signature change. Genuinely isolated from the NETRESOURCEW/
        // Node chain that blocks the rest of this file's widening.
        wchar_t szDescription[128];
        wchar_t szProvider[128];

        if (WNetGetLastErrorW(
                &dwExtendedError,
                szDescription,
                COUNTOF(szDescription),
                szProvider,
                COUNTOF(szProvider)) == NO_ERROR)
        {
            dwError = dwExtendedError;
        }
    }

    return dwError;
}

DWORD CNethoodCacheEnumerationThread::OpenEnum(
    __in DWORD dwScope,
    __in DWORD dwType,
    __in DWORD dwUsage,
    __in NETRESOURCEW* pNetResource,
    __out HANDLE* phEnum)
{
    DWORD dwError;

    dwError = WNetOpenEnumW(dwScope, dwType, dwUsage, pNetResource, phEnum);
    if (IsLogonFailure(dwError))
    {
        // Access to the network resource was denied; try to
        // authenticate the user.

        // SalWNetAddConnection2Interactive is a shared SDK method
        // (spl_gen.h) whose LPNETRESOURCE parameter is the ambiguous wchar_t-generic macro, not
        // genuinely narrow-only as this comment previously claimed - it resolves LPNETRESOURCEW
        // under _UNICODE, exactly matching pNetResource's own always-wide NETRESOURCEW* type
        //. ProjectNetResourceFieldToAnsiExact's refuse-if-lossy dance below is only
        // needed for the narrow build; under _UNICODE pNetResource is passed straight through.
        dwError = SalamanderGeneral->SalWNetAddConnection2Interactive(pNetResource);
        /*
                // It seems that the CONNECT_TEMPORARY flag vanished from
                // recent MSDN documentation, so let's see what an older SDK
                // says about this flag:
                // "CONNECT_TEMPORARY: The connection is being established
                // for browsing purposes and can be released quickly."
                // Since the flag is still declared in the header file,
                // it still does no harm.
		dwError = WNetAddConnection2(
			pNetResource,
			NULL,
			NULL,
			CONNECT_INTERACTIVE | CONNECT_TEMPORARY);
*/
        if (dwError == NO_ERROR)
        {
            // Try once more.
            dwError = WNetOpenEnumW(dwScope, dwType, dwUsage,
                                   pNetResource, phEnum);
        }
    }

    return dwError;
}

DWORD CNethoodCacheEnumerationThread::EnumHiddenSharesNt(__in PCWSTR pszServerName)
{
    SHARE_INFO_1* pShareInfo;
    DWORD cRead;
    DWORD cTotal;
    DWORD res;

    // pszServerName is PCWSTR now (cache.h widened) - the
    // #ifndef _UNICODE narrow->wide conversion this used to need is obsolete, NetShareEnum
    // (always wide-native) takes it directly. This does NOT define UNICODE (deferred).
    res = NetShareEnum(
        const_cast<LPWSTR>(pszServerName),
        1, reinterpret_cast<BYTE**>(&pShareInfo),
        MAX_PREFERRED_LENGTH, &cRead, &cTotal, NULL);
    if (res == ERROR_SUCCESS || res == ERROR_MORE_DATA)
    {
        m_pCache->LockCache();

        DWORD i;
        for (i = 0; i < cRead; i++)
        {
            if (pShareInfo[i].shi1_type == STYPE_SPECIAL)
            {
                ProcessShareInfo(pszServerName, pShareInfo[i]);
            }
        }

        m_pCache->UnlockCache();

        NetApiBufferFree(pShareInfo);
    }

    return res;
}

DWORD CNethoodCacheEnumerationThread::EnumHiddenShares(__in PCWSTR pszServerName)
{
#if ENUM_HIDDEN_SHARES
    DWORD res;

    res = EnumHiddenSharesNt(pszServerName);

    return res;
#else
    return NO_ERROR;
#endif
}

BOOL CNethoodCacheEnumerationThread::GetShortcutsDir(std::wstring& path)
{
    PWSTR knownPath = NULL;
    const HRESULT result = SHGetKnownFolderPath(FOLDERID_NetHood, KF_FLAG_DEFAULT, NULL, &knownPath);
    if (FAILED(result) || knownPath == NULL)
        return FALSE;
    path.assign(knownPath);
    CoTaskMemFree(knownPath);
    return TRUE;
}

BOOL CNethoodCacheEnumerationThread::AddNetworkShortcut(
    __in PCWSTR pszName,
    __in PCWSTR pszPath)
{
    std::wstring resolvedPath(pszPath);
    if (ResolveNetShortcut(resolvedPath))
    {
        NETRESOURCEW sNetResource = {
            0,
        };

        sNetResource.dwDisplayType = RESOURCEDISPLAYTYPE_SHARE;
        sNetResource.dwType = CNethoodCacheNode::RESOURCETYPE_SHORTCUT;
        sNetResource.lpComment = const_cast<PWSTR>(pszName);
        sNetResource.lpRemoteName = resolvedPath.data();
        ProcessEnumeration(&sNetResource, 1, ProcessShortcut);
    }

    return FALSE;
}

BOOL CNethoodCacheEnumerationThread::ResolveNetShortcut(
    std::wstring& path)
{
    if (path.empty() || path[0] == L'\\')
        return FALSE; // UNC path -> not a NetHood location

    wchar_t root[] = {path[0], L':', L'\\', L'\0'};
    if (GetDriveTypeW(root) != DRIVE_FIXED)
        return FALSE; // not a local fixed path -> not a NetHood location

    BOOL tryTarget = FALSE; // if TRUE, it is worth trying to find the "target.lnk" file
    std::wstring name = path;
    if (!name.empty() && name.back() != L'\\')
        name.push_back(L'\\');
    name.append(L"desktop.ini");
    {
        HANDLE hFile = HANDLES_Q(CreateFileW(name.c_str(), GENERIC_READ,
                                            FILE_SHARE_WRITE | FILE_SHARE_READ, NULL,
                                            OPEN_EXISTING,
                                            FILE_FLAG_SEQUENTIAL_SCAN,
                                            NULL));
        if (hFile != INVALID_HANDLE_VALUE)
        {
            if (GetFileSize(hFile, NULL) <= 1000) // so far all had 92 bytes, so 1000 bytes should be more than enough
            {
                char buf[1000];
                DWORD read;
                if (ReadFile(hFile, buf, 1000, &read, NULL) && read != 0) // Load the file into memory.
                {
                    char* s = buf;
                    char* end = buf + read;
                    while (s < end) // Look for the "folder shortcut" CLSID in the file.
                    {
                        if (*s == '{')
                        {
                            s++;
                            char* beg = s;
                            while (s < end && *s != '}')
                                s++;
                            if (s < end)
                            {
                                const char* folderShortcutCLSID = "0AFACED1-E828-11D1-9187-B532F1E9575D";
                                if (_strnicmp(beg, folderShortcutCLSID, s - beg) == 0)
                                {
                                    tryTarget = TRUE;
                                    break;
                                }
                            }
                        }
                        else
                            s++;
                    }
                }
            }
            HANDLES(CloseHandle(hFile));
        }
    }

    BOOL ok = FALSE;

    if (tryTarget)
    {
        name = path;
        if (!name.empty() && name.back() != L'\\')
            name.push_back(L'\\');
        name.append(L"target.lnk");
        {
            WIN32_FIND_DATAW data;
            HANDLE find = HANDLES_Q(FindFirstFileW(name.c_str(), &data));
            if (find != INVALID_HANDLE_VALUE) // The file exists and we already retrieved its metadata.
            {
                HANDLES(FindClose(find));

                IShellLinkW* link;
                if (CoCreateInstance(CLSID_ShellLink, NULL,
                                     CLSCTX_INPROC_SERVER, IID_IShellLinkW,
                                     (LPVOID*)&link) == S_OK)
                {
                    IPersistFile* fileInt;
                    if (link->QueryInterface(IID_IPersistFile, (LPVOID*)&fileInt) == S_OK)
                    {
                        if (fileInt->Load(name.c_str(), STGM_READ) == S_OK)
                        {
                            std::vector<wchar_t> target(256, L'\0');
                            for (;;)
                            {
                                std::fill(target.begin(), target.end(), L'\0');
                                if (link->GetPath(target.data(), static_cast<int>(target.size()),
                                                  &data, SLGP_UNCPRIORITY) != NOERROR)
                                    break;
                                const size_t length = wcsnlen_s(target.data(), target.size());
                                if (length + 1 < target.size())
                                {
                                    path.assign(target.data(), length);
                                    ok = TRUE;
                                    break;
                                }
                                if (target.size() > static_cast<size_t>(INT_MAX) / 2)
                                    break;
                                target.resize(target.size() * 2, L'\0');
                            }
                            // Skip Resolve; it is not critical here and would slow things down.
                        }
                        fileInt->Release();
                    }
                    link->Release();
                }
            }
        }
    }

    return ok;
}

DWORD CNethoodCacheEnumerationThread::EnumNetworkShortcuts()
{
    std::wstring shortcutsPath;
    std::wstring findMask;
    std::wstring shortcut;
    HANDLE hFind;
    WIN32_FIND_DATAW wfd;
    BOOL res;

    if (!GetShortcutsDir(shortcutsPath))
        return NO_ERROR;

    findMask = shortcutsPath;
    if (!findMask.empty() && findMask.back() != L'\\')
        findMask.push_back(L'\\');
    findMask.push_back(L'*');

    hFind = HANDLES_Q(FindFirstFileW(findMask.c_str(), &wfd));
    if (hFind == INVALID_HANDLE_VALUE)
    {
        return NO_ERROR;
    }

    if (SUCCEEDED(CoInitialize(NULL)))
    {
        res = TRUE;
        while (res)
        {
            if ((wfd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
                wfd.cFileName[0] != 0 &&
                (wfd.cFileName[0] != L'.' || wfd.cFileName[1] != 0 &&
                                                      (wfd.cFileName[1] != L'.' || wfd.cFileName[2] != 0)))
            { // directories except empty names, "." and ".."
                shortcut = shortcutsPath;
                if (!shortcut.empty() && shortcut.back() != L'\\')
                    shortcut.push_back(L'\\');
                shortcut.append(wfd.cFileName);
                AddNetworkShortcut(wfd.cFileName, shortcut.c_str());
            }

            res = FindNextFileW(hFind, &wfd);
        }

        CoUninitialize();
    }

    HANDLES(FindClose(hFind));

    return NO_ERROR;
}

DWORD CNethoodCacheEnumerationThread::EnumTSClientVolumes()
{
    NETRESOURCEW sNetResource = {
        0,
    };
    HANDLE hEnum;
    NethoodResourceBuffer enumStorage;
    DWORD dwError;
    DWORD cEntries = 0;
    CNethoodCache::Node nodeParent;
    CTsClientName oClientName(m_pCache);

    if (m_pCache->GetDisplayTSClientVolumes() == CNethoodCache::TSCDisplayRoot)
    {
        nodeParent = m_node;
    }
    else
    {
        assert(m_pCache->GetDisplayTSClientVolumes() == CNethoodCache::TSCDisplayFolder);
        nodeParent = m_pCache->m_nodeTscFolder;
    }

    assert(nodeParent != NULL);

    static wchar_t remoteName[] = L"\\\\tsclient";
    sNetResource.lpRemoteName = remoteName;

    TRACE_IW(L"Opening TS client enumeration, client name " << (PCWSTR)oClientName);

    dwError = OpenEnum(RESOURCE_GLOBALNET, RESOURCETYPE_DISK, RESOURCEUSAGE_ALL, &sNetResource, &hEnum);
    dwError = ExtendWNetError(dwError);

    if (dwError == NO_ERROR)
    {
        NETRESOURCEW* resources = NULL;
        dwError = NethoodEnumerateResourceBatch(hEnum, cEntries, enumStorage, resources);
        dwError = ExtendWNetError(dwError);

        TRACE_I("Enumerating TC client resources returned error " << dwError << ", " << cEntries << " entries available.");

        if (dwError == NO_ERROR)
        {
            ProcessEnumeration(nodeParent, resources, cEntries, ProcessTSCVolume, &oClientName);
        }
        CloseEnum(hEnum);
    }

    TRACE_I("TS client enumeration returned " << dwError);

    if (dwError == 0 && cEntries > 0 &&
        m_pCache->GetDisplayTSClientVolumes() == CNethoodCache::TSCDisplayFolder)
    {
        int nTscFolderName;
        CNethoodCacheNode& nodeTscFolderData = m_pCache->m_oTree.GetAt(m_pCache->m_nodeTscFolder);
        wchar_t szComment[64];

        memset(&sNetResource, 0, sizeof(sNetResource));

        if (SalamanderGeneral->IsRemoteSession())
        {
            // Remote Desktop Session
            nTscFolderName = IDS_TSFOLDER;
        }
        else
        {
            // Windows 7 XP mode
            nTscFolderName = IDS_TSHOST;
        }

        const std::wstring remoteName =
            SPLLoadStrOwned(SalamanderGeneral, GetLangInstance(), nTscFolderName);
        sNetResource.lpRemoteName = const_cast<LPWSTR>(remoteName.c_str());
        sNetResource.dwUsage = RESOURCEUSAGE_CONTAINER;

        if (oClientName.Length() > 0)
        {
            StringCchPrintfW(szComment, COUNTOF(szComment),
                            SPLLoadStrOwned(SalamanderGeneral,
                                GetLangInstance(),
                                IDS_TSFOLDERCOMMENT).c_str(),
                            (PCWSTR)oClientName);

            sNetResource.lpComment = szComment;
        }

        nodeTscFolderData.SetNetResource(&sNetResource);
        nodeTscFolderData.SetType(CNethoodCacheNode::TypeTSCFolder);
    }

    return dwError;
}

bool CNethoodCacheEnumerationThread::IsLogonFailure(__in DWORD dwError)
{
    switch (dwError)
    {
    case WN_BAD_USER:
    case WN_BAD_PASSWORD:
    case WN_ACCESS_DENIED:
    case WN_NOT_AUTHENTICATED:
    case WN_NOT_LOGGED_ON:
    case WN_NOT_VALIDATED:
    case ERROR_LOGON_FAILURE:
    case ERROR_ACCOUNT_RESTRICTION:
    case ERROR_INVALID_LOGON_HOURS:
    case ERROR_INVALID_WORKSTATION:
    case ERROR_PASSWORD_EXPIRED:
    case ERROR_PASSWORD_MUST_CHANGE:
    case ERROR_ACCOUNT_DISABLED:
    case ERROR_LOGON_NOT_GRANTED:
    case ERROR_TRUST_FAILURE:
    case ERROR_NOLOGON_INTERDOMAIN_TRUST_ACCOUNT:
    case ERROR_NOLOGON_WORKSTATION_TRUST_ACCOUNT:
    case ERROR_NOLOGON_SERVER_TRUST_ACCOUNT:
    case ERROR_ILL_FORMED_PASSWORD:
    case ERROR_INVALID_PASSWORDNAME:
    case ERROR_NO_SUCH_USER:
    case ERROR_DOWNGRADE_DETECTED:
    case ERROR_EXTENDED_ERROR: // we do not know specific error, but "password expired" error uses ERROR_EXTENDED_ERROR, so we get them all
        return true;

    default:
        return false;
    }
}

//------------------------------------------------------------------------------
// CNethoodCacheBeginDependentEnumerationConsumer

void CNethoodCacheBeginDependentEnumerationConsumer::OnCacheNodeUpdated(
    __in CNethoodCache::Node node)
{
    bool bKillDependentNode = false;
    const CNethoodCacheNode& data = m_pCache->GetItemData(node);

    if (data.GetStatus() == CNethoodCacheNode::StatusPending)
    {
        return;
    }
    else if (data.GetStatus() == CNethoodCacheNode::StatusOk)
    {
        const CNethoodCacheNode& dataToEnum = m_pCache->GetItemData(m_node);
        if (dataToEnum.GetType() == CNethoodCacheNode::TypeShare)
        {
            TRACE_IW(L"Node " << m_pCache->DbgGetNodeName(m_node) << L" is UNC path, spreading error to child nodes.");
            m_pCache->SpreadError(m_node, ERROR_NETHOODCACHE_FULL_UNC_PATH, CNethoodCache::SpreadRemoveChildren);
            bKillDependentNode = false;
        }
        else
        {
            TRACE_IW(L"Starting dependent enumeration of " << m_pCache->DbgGetNodeName(m_node));
            assert(dataToEnum.IsContainer() || dataToEnum.GetLastEnumerationResult() == -1);
            m_pCache->BeginEnumerationThread(m_node);
        }
    }
    else
    {
        //assert(0);
        bKillDependentNode = true;
    }

    if (bKillDependentNode)
    {
        TRACE_IW(L"Killing node " << m_pCache->DbgGetNodeName(m_node) << L" and its children.");

        // This will kill ourselves through the
        // CNethoodCacheHitmanConsumer.
        // !!! Do not touch 'this' pointer after the following line !!!
        m_pCache->RemoveItem(m_node);
    }
    else
    {
        if (m_pHitmanConsumer != NULL)
        {
            // Recall the hitman. We don't need it to kill us anymore.
            m_pCache->UnregisterInternalConsumer(m_node, m_pHitmanConsumer);
            delete m_pHitmanConsumer;
            m_pHitmanConsumer = NULL;
        }

        // Commit a suicide...
        m_pCache->UnregisterInternalConsumer(node, this);
        delete this;
    }
}

//------------------------------------------------------------------------------
// CNethoodCacheHitmanConsumer

void CNethoodCacheHitmanConsumer::OnCacheNodeUpdated(__in CNethoodCache::Node node)
{
    if (m_pCache->GetItemData(node).GetStatus() == CNethoodCacheNode::StatusDead)
    {
        m_pConsumerToKill->SetHitman(NULL);

        // Consumer hired to kill another consumer to prevent memory leaks.
        m_pCache->UnregisterInternalConsumer(m_nodeToKill, m_pConsumerToKill);
        delete m_pConsumerToKill;

        // Commit a suicide...
        m_pCache->UnregisterInternalConsumer(node, this);
        delete this;
    }
}

//------------------------------------------------------------------------------
// CNethoodCacheManagementThread

CNethoodCacheManagementThread::CNethoodCacheManagementThread(
    __in CNethoodCache* pCache) : _baseClass(L"NethoodCacheManagement")
{
    m_pCache = pCache;
    m_hwndWtsNotify = NULL;
    m_wndWtsNotifyCls = NULL;
}

CNethoodCacheManagementThread::~CNethoodCacheManagementThread()
{
}

unsigned CNethoodCacheManagementThread::Body()
{
    HANDLE ahWait[2];
    DWORD dwWait;
    DWORD dwTimeout = INFINITE;

    ahWait[0] = m_pCache->m_hQuitEvent;
    ahWait[1] = m_pCache->m_hWakeMgmtEvent;

#if defined(_DEBUG) && 0
    Sleep(5000);
    m_pCache->LockCache();
    CNethoodCache::Node node = m_pCache->m_oTree.GetRoot();
    node = m_pCache->m_oTree.GetFirstChild(node);
    node = m_pCache->m_oTree.GetNextSibling(node);
    m_pCache->RemoveItem(node);
    m_pCache->UnlockCache();
#else

    for (;;)
    {
        /*
                        Try to avoid creating the message loop if possible, because
                        once we do, both the kernel and user stacks for the
                        thread grow and plenty of additional overhead appears.
                */

        if (m_hwndWtsNotify)
        {
            dwWait = MsgWaitForMultipleObjects(COUNTOF(ahWait), ahWait,
                                               FALSE, dwTimeout, QS_ALLINPUT);
        }
        else
        {
            dwWait = WaitForMultipleObjects(COUNTOF(ahWait), ahWait,
                                            FALSE, dwTimeout);
        }

        if (dwWait == WAIT_OBJECT_0)
        {
            // Quit.

            if (m_hwndWtsNotify)
            {
                DestroyWindow(m_hwndWtsNotify);
                DrainMsgQueue(true);

                if (!UnregisterClassW(MAKEINTATOM(m_wndWtsNotifyCls), g_hInstance))
                    TRACE_E("UnregisterClass(m_wndWtsNotifyCls) has failed");

                m_hwndWtsNotify = NULL;
                m_wndWtsNotifyCls = NULL;
            }

            break;
        }
        else if (dwWait == WAIT_OBJECT_0 + 1)
        {
            // Node was put on or removed from the stand-by list.
            dwTimeout = WalkStandbyList();

            if (m_pCache->m_displayTscVolumes != CNethoodCache::TSCDisplayNone &&
                m_hwndWtsNotify == NULL)
            {
                CreateWtsNotifyWindow();
            }
        }
        else if (dwWait == WAIT_TIMEOUT)
        {
            // Walk the stand-by list and eventually start
            // the re-enumeration.
            dwTimeout = WalkStandbyList();
        }
        else if (dwWait == WAIT_OBJECT_0 + COUNTOF(ahWait) && m_hwndWtsNotify != NULL)
        {
            DrainMsgQueue();
        }
        else
        {
            // Some error?
            assert(0);
            break;
        }
    }
#endif
    return 0;
}

DWORD CNethoodCacheManagementThread::WalkStandbyList()
{
    // Wake the management thread every 100 ms.
    DWORD dwTimeout = 100;
    CNethoodCacheListEntry* pListEntry;
    CNethoodCacheListEntry* pNextEntry;
    CNethoodCacheNode* pNode;

    m_pCache->LockCache();

    pListEntry = m_pCache->m_standbyNodes.pNext;
    while (pListEntry != &m_pCache->m_standbyNodes)
    {
        // Since the entry can be removed during this cycle,
        // fetch the next entry before the potential removal.
        pNextEntry = pListEntry->pNext;

        // Based on the list entry get the pointer to
        // the node object.
        pNode = reinterpret_cast<CNethoodCacheNode*>(
            reinterpret_cast<char*>(pListEntry) -
            offsetof(CNethoodCacheNode, m_standbyListEntry));
        assert(pNode->GetStatus() == CNethoodCacheNode::StatusStandby);

        if (pNode->IsStandbyPeriodOver())
        {
            CNethoodCache::Node node;
            node = reinterpret_cast<CNethoodCache::Node>(pNode->GetIterator());
            m_pCache->RemoveNodeFromStandbyList(node);
            m_pCache->BeginEnumerationThread(node);
        }

        // Move on.
        pListEntry = pNextEntry;
    }

    if (m_pCache->m_cStandbyNodes == 0)
    {
        // There is nothing on the stand-by list,
        // the management thread can sleep indefinitely.
        dwTimeout = INFINITE;
    }

    m_pCache->UnlockCache();

    return dwTimeout;
}

void CNethoodCacheManagementThread::DrainMsgQueue(bool bQuitting)
{
    MSG msg;
    int res;

    for (;;)
    {
        if (bQuitting)
        {
            res = GetMessageW(&msg, NULL, 0, 0);
        }
        else
        {
            res = PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE);
        }

        if (res <= 0) /* GetMessage may return -1 on error */
        {
            break;
        }

        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
}

void CNethoodCacheManagementThread::CreateWtsNotifyWindow()
{
    try
    {
        const std::wstring className = SPLFormatStringOwned(L"NethoodWtsNotify:%p", this);
        WNDCLASSEXW wc = {0};
        wc.cbSize = sizeof(WNDCLASSEXW);
        wc.lpfnWndProc = &CNethoodCacheManagementThread::WtsNotifyWndProc;
        wc.hInstance = g_hInstance;
        wc.lpszClassName = className.c_str();

        m_wndWtsNotifyCls = RegisterClassExW(&wc);
        assert(m_wndWtsNotifyCls);

        m_hwndWtsNotify = CreateWindowExW(
            0, // exStyle
            className.c_str(),
            NULL, // name
            WS_POPUP,
            0,
            0,
            0,
            0,
            HWND_MESSAGE,
            NULL,
            g_hInstance,
            this);
        assert(m_hwndWtsNotify);
    }
    catch (const std::bad_alloc&)
    {
        TRACE_E("Unable to allocate the WTS notification window class name.");
        return;
    }

    DrainMsgQueue();
}

LRESULT WINAPI CNethoodCacheManagementThread::WtsNotifyWndProc(
    HWND hwnd,
    UINT uMsg,
    WPARAM wParam,
    LPARAM lParam)
{
    CNethoodCacheManagementThread* that = NULL;

    switch (uMsg)
    {
    case WM_CREATE:
    {
        CREATESTRUCT* pCreateStruct = reinterpret_cast<CREATESTRUCT*>(lParam);
        that = reinterpret_cast<CNethoodCacheManagementThread*>(pCreateStruct->lpCreateParams);
        assert(that);

        SetInstanceToHwnd(hwnd, that);

        if (!that->m_pCache->m_pfnWTSRegisterSessionNotification(
                hwnd, NOTIFY_FOR_ALL_SESSIONS))
        {
            TRACE_E("WTSRegisterSessionNotification failed, error " << GetLastError());
        }
        else
        {
            TRACE_I("WTS session notification successfully registered.");
        }

        break;
    }

    case WM_DESTROY:
    {
        that = GetInstanceFromHwnd(hwnd);
        assert(that);

        that->m_pCache->m_pfnWTSUnRegisterSessionNotification(hwnd);

        PostQuitMessage(0);

        break;
    }

    case WM_WTSSESSION_CHANGE:
    {
        that = GetInstanceFromHwnd(hwnd);
        assert(that);

        that->m_pCache->WTSSessionChange((DWORD)lParam, (int)wParam); // FIXME_X64 - verify the cast to (DWORD)

        break;
    }
    }

    return DefWindowProcW(hwnd, uMsg, wParam, lParam);
}

//------------------------------------------------------------------------------
// CTsClientName

CTsClientName::CTsClientName(CNethoodCache* pCache)
{
    DWORD cbReturned;

    assert(pCache != NULL);

    m_pCache = pCache;

    if (WTSQuerySessionInformationW(
            WTS_CURRENT_SERVER_HANDLE,
            WTS_CURRENT_SESSION,
            WTSClientName,
            &m_pszClientName, &cbReturned))
    {
        m_cchClientName = (cbReturned / sizeof(wchar_t)) - 1; // exclude nul-terminator
        assert(m_cchClientName >= 0);
    }
    else
    {
        // Either an error occurred or this is not a remote session.
        TRACE_E("Failed to retrieve client name, error " << GetLastError());

        m_pszClientName = NULL;
        m_cchClientName = 0;
    }
}

CTsClientName::~CTsClientName()
{
    if (m_pszClientName != NULL)
    {
        WTSFreeMemory(m_pszClientName);
    }
}

//------------------------------------------------------------------------------
// CTsNameFormatter

CTsNameFormatter::CTsNameFormatter() = default;

void CTsNameFormatter::Format(
    __in CNethoodCache::TSCDisplayMode displayMode,
    __in PCWSTR pszVolumeName,
    __in const CTsClientName* poClientName,
    __out_ecount(cchMax) PWSTR pszDisplayName,
    __in size_t cchMax)
{
    assert(pszVolumeName != NULL);
    assert(pszDisplayName != NULL);
    assert(cchMax > 0);

    if (!NeedsExtraFormatting(displayMode) ||
        poClientName == NULL ||
        poClientName->Length() == 0)
    {
        StringCchCopyW(pszDisplayName, cchMax, pszVolumeName);
        return;
    }

    if (m_format.empty())
    {
        m_format = SPLLoadStrOwned(SalamanderGeneral, GetLangInstance(), IDS_TSCLIENT);
        assert(!m_format.empty());
    }

    // Use FormatMessage to take advantage of the indexed insertion
    // points, since position of the volume name and client name may
    // differ in different languages.

    DWORD_PTR args[2];
    args[0] = reinterpret_cast<DWORD_PTR>(pszVolumeName);
    args[1] = reinterpret_cast<DWORD_PTR>((PCWSTR)(*poClientName));

    FormatMessageW(
        FORMAT_MESSAGE_FROM_STRING | FORMAT_MESSAGE_ARGUMENT_ARRAY, // flags
        m_format.c_str(),                                           // source
        0,                                                          // message id
        0,                                                          // lang id
        pszDisplayName,                                             // buffer
        static_cast<DWORD>(cchMax),                                 // buffer size
        reinterpret_cast<va_list*>(args));
}
