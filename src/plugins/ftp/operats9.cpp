// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"

#include <utility>


template <typename TObject, typename... TArgs>
static TObject* NewUploadCacheObject(TArgs&&... args) noexcept
{
    try
    {
        return new TObject(std::forward<TArgs>(args)...);
    }
    catch (...)
    {
        TRACE_E(LOW_MEMORY);
        return NULL;
    }
}

static BOOL StoreUploadListingItemName(CUploadListingItem& item,
                                       const CFtpTextCodec& codec,
                                       const char* name) noexcept
{
    if (name == NULL || !FtpStoreProtocolBytes(name, item.Name))
        return FALSE;
    switch (codec.DecodeForComparison(item.Name.data(), item.Name.size(), item.NameText))
    {
    case CFtpTextDecodeStatus::Success:
        item.NameTextValid = TRUE;
        return TRUE;

    case CFtpTextDecodeStatus::InvalidInput:
        item.NameText.clear();
        item.NameTextValid = FALSE;
        return TRUE;

    default:
        return FALSE;
    }
}

static int CompareUploadListingByteNames(std::string_view first,
                                         std::string_view second) noexcept
{
    const size_t commonLength = first.size() < second.size() ? first.size() : second.size();
    const int prefix = commonLength == 0 ? 0 : memcmp(first.data(), second.data(), commonLength);
    if (prefix != 0)
        return prefix;
    return first.size() < second.size() ? -1 : first.size() > second.size() ? 1 : 0;
}

static int CompareUploadListingNamesNoCase(const CUploadListingItem& first,
                                           const CUploadListingItem& second) noexcept
{
    if (first.NameTextValid != second.NameTextValid)
        return first.NameTextValid ? -1 : 1;
    if (!first.NameTextValid)
        return CompareUploadListingByteNames(first.Name, second.Name);
    const int result = CompareStringOrdinal(first.NameText.c_str(), -1,
                                            second.NameText.c_str(), -1, TRUE);
    return result == CSTR_LESS_THAN ? -1 : result == CSTR_GREATER_THAN ? 1 : 0;
}

static int CompareUploadListingNameNoCase(const CUploadListingItem& item,
                                          std::string_view name,
                                          const std::wstring& nameText,
                                          BOOL nameTextValid) noexcept
{
    if (item.NameTextValid != nameTextValid)
        return item.NameTextValid ? -1 : 1;
    if (!item.NameTextValid)
        return CompareUploadListingByteNames(item.Name, name);
    const int result = CompareStringOrdinal(item.NameText.c_str(), -1,
                                            nameText.c_str(), -1, TRUE);
    return result == CSTR_LESS_THAN ? -1 : result == CSTR_GREATER_THAN ? 1 : 0;
}

#ifdef _DEBUG
int CUploadListingsOnServer::FoundPathIndexesInCache = 0; // how many searched paths the cache has caught
int CUploadListingsOnServer::FoundPathIndexesTotal = 0;   // total number of searched paths
#endif

//
// ****************************************************************************
// CUploadListingCache
//

CUploadListingCache::CUploadListingCache() : ListingsOnServer(5, 5)
{
    HANDLES(InitializeCriticalSection(&UploadLstCacheCritSect));
}

CUploadListingCache::~CUploadListingCache()
{
    HANDLES(DeleteCriticalSection(&UploadLstCacheCritSect));
}

BOOL CUploadListingCache::AddOrUpdateListing(const wchar_t* user, const wchar_t* host, unsigned short port,
                                             const char* path, CFTPServerPathType pathType,
                                             const char* pathListing, int pathListingLen,
                                             const CFTPDate& pathListingDate, DWORD listingStartTime,
                                             BOOL onlyUpdate, const char* welcomeReply,
                                             const char* systReply, const char* suggestedListingServerType,
                                             const CFtpTextCodec& textCodec)
{
    if (host == NULL)
    {
        TRACE_E("Unexpected situation in CUploadListingCache::AddOrUpdateListing()!");
        return FALSE;
    }
    BOOL ret = TRUE;
    if (user != NULL && wcscmp(user, L"anonymous") == 0)
        user = NULL;
    HANDLES(EnterCriticalSection(&UploadLstCacheCritSect));
    CUploadListingsOnServer* foundServer = FindServer(user, host, port, NULL);
    if (foundServer == NULL && !onlyUpdate) // the cache does not contain any path from this server yet; create an entry for the server
    {
        foundServer = NewUploadCacheObject<CUploadListingsOnServer>(user, host, port);
        if (foundServer != NULL && foundServer->IsGood())
        {
            ListingsOnServer.Add(foundServer);
            if (!ListingsOnServer.IsGood())
            {
                ListingsOnServer.ResetState();
                delete foundServer;
                foundServer = NULL;
                ret = FALSE;
            }
        }
        else
        {
            if (foundServer != NULL)
            {
                delete foundServer;
                foundServer = NULL;
            }
            else
                TRACE_E(LOW_MEMORY);
            ret = FALSE;
        }
    }

    if (foundServer != NULL)
    {
        ret = foundServer->AddOrUpdateListing(path, pathType, pathListing, pathListingLen,
                                              pathListingDate, listingStartTime, onlyUpdate,
                                              welcomeReply, systReply, suggestedListingServerType,
                                              textCodec);
    }
    HANDLES(LeaveCriticalSection(&UploadLstCacheCritSect));
    return ret;
}

void CUploadListingCache::RemoveServer(const wchar_t* user, const wchar_t* host, unsigned short port)
{
    HANDLES(EnterCriticalSection(&UploadLstCacheCritSect));
    int index;
    if (FindServer(user, host, port, &index) != NULL)
        ListingsOnServer.Delete(index);
    HANDLES(LeaveCriticalSection(&UploadLstCacheCritSect));
}

void CUploadListingCache::RemoveNotAccessibleListings(const wchar_t* user, const wchar_t* host, unsigned short port)
{
    HANDLES(EnterCriticalSection(&UploadLstCacheCritSect));
    CUploadListingsOnServer* foundServer = FindServer(user, host, port, NULL);
    if (foundServer != NULL)
        foundServer->RemoveNotAccessibleListings();
    HANDLES(LeaveCriticalSection(&UploadLstCacheCritSect));
}

CUploadListingsOnServer*
CUploadListingCache::FindServer(const wchar_t* user, const wchar_t* host, unsigned short port, int* index)
{
    if (index != NULL)
        *index = -1;
    if (host == NULL)
    {
        TRACE_E("Unexpected situation in CUploadListingCache::FindServer()!");
        return FALSE;
    }
    if (user != NULL && wcscmp(user, L"anonymous") == 0)
        user = NULL;
    int i;
    for (i = 0; i < ListingsOnServer.Count; i++)
    {
        CUploadListingsOnServer* server = ListingsOnServer[i];
        if (CompareStringOrdinal(server->Host.c_str(), -1, host, -1, TRUE) == CSTR_EQUAL &&
            (server->Anonymous && user == NULL ||
             user != NULL && !server->Anonymous && server->User == user) &&
            server->Port == port) // server found
        {
            if (index != NULL)
                *index = i;
            return server;
        }
    }
    return NULL;
}

void CUploadListingCache::ReportCreateDirs(const wchar_t* user, const wchar_t* host, unsigned short port,
                                           const char* workPath, CFTPServerPathType pathType, const char* newDirs,
                                           BOOL unknownResult)
{
    HANDLES(EnterCriticalSection(&UploadLstCacheCritSect));
    CUploadListingsOnServer* server = FindServer(user, host, port, NULL);
    if (server != NULL)
        server->ReportCreateDirs(workPath, pathType, newDirs, unknownResult);
    HANDLES(LeaveCriticalSection(&UploadLstCacheCritSect));
}

void CUploadListingCache::ReportRename(const wchar_t* user, const wchar_t* host, unsigned short port,
                                       const char* workPath, CFTPServerPathType pathType,
                                       const char* fromName, const char* newName, BOOL unknownResult)
{
    HANDLES(EnterCriticalSection(&UploadLstCacheCritSect));
    CUploadListingsOnServer* server = FindServer(user, host, port, NULL);
    if (server != NULL)
        server->ReportRename(workPath, pathType, fromName, newName, unknownResult);
    HANDLES(LeaveCriticalSection(&UploadLstCacheCritSect));
}

void CUploadListingCache::ReportDelete(const wchar_t* user, const wchar_t* host, unsigned short port,
                                       const char* workPath, CFTPServerPathType pathType, const char* name,
                                       BOOL unknownResult)
{
    HANDLES(EnterCriticalSection(&UploadLstCacheCritSect));
    CUploadListingsOnServer* server = FindServer(user, host, port, NULL);
    if (server != NULL)
        server->ReportDelete(workPath, pathType, name, unknownResult);
    HANDLES(LeaveCriticalSection(&UploadLstCacheCritSect));
}

void CUploadListingCache::ReportStoreFile(const wchar_t* user, const wchar_t* host, unsigned short port,
                                          const char* workPath, CFTPServerPathType pathType, const char* name)
{
    HANDLES(EnterCriticalSection(&UploadLstCacheCritSect));
    CUploadListingsOnServer* server = FindServer(user, host, port, NULL);
    if (server != NULL)
        server->ReportStoreFile(workPath, pathType, name);
    HANDLES(LeaveCriticalSection(&UploadLstCacheCritSect));
}

void CUploadListingCache::ReportFileUploaded(const wchar_t* user, const wchar_t* host, unsigned short port,
                                             const char* workPath, CFTPServerPathType pathType, const char* name,
                                             const CQuadWord& fileSize, BOOL unknownResult)
{
    HANDLES(EnterCriticalSection(&UploadLstCacheCritSect));
    CUploadListingsOnServer* server = FindServer(user, host, port, NULL);
    if (server != NULL)
        server->ReportFileUploaded(workPath, pathType, name, fileSize, unknownResult);
    HANDLES(LeaveCriticalSection(&UploadLstCacheCritSect));
}

void CUploadListingCache::ReportUnknownChange(const wchar_t* user, const wchar_t* host, unsigned short port,
                                              const char* workPath, CFTPServerPathType pathType)
{
    HANDLES(EnterCriticalSection(&UploadLstCacheCritSect));
    CUploadListingsOnServer* server = FindServer(user, host, port, NULL);
    if (server != NULL)
        server->ReportUnknownChange(workPath, pathType);
    HANDLES(LeaveCriticalSection(&UploadLstCacheCritSect));
}

void CUploadListingCache::InvalidatePathListing(const wchar_t* user, const wchar_t* host, unsigned short port,
                                                const char* path, CFTPServerPathType pathType)
{
    HANDLES(EnterCriticalSection(&UploadLstCacheCritSect));
    CUploadListingsOnServer* server = FindServer(user, host, port, NULL);
    if (server != NULL)
        server->InvalidatePathListing(path, pathType);
    HANDLES(LeaveCriticalSection(&UploadLstCacheCritSect));
}

BOOL CUploadListingCache::IsListingFromPanel(const wchar_t* user, const wchar_t* host, unsigned short port,
                                             const char* path, CFTPServerPathType pathType)
{
    HANDLES(EnterCriticalSection(&UploadLstCacheCritSect));
    CUploadListingsOnServer* server = FindServer(user, host, port, NULL);
    BOOL ret = FALSE;
    if (server != NULL)
        ret = server->IsListingFromPanel(path, pathType);
    HANDLES(LeaveCriticalSection(&UploadLstCacheCritSect));
    return ret;
}

BOOL CUploadListingCache::GetListing(const wchar_t* user, const wchar_t* host, unsigned short port,
                                     const char* path, CFTPServerPathType pathType, int workerMsg,
                                     int workerUID, BOOL* listingInProgress, BOOL* notAccessible,
                                     BOOL* getListing, const char* name, CUploadListingItem** existingItem,
                                     BOOL* nameExists, const CFtpTextCodec& textCodec)
{
    BOOL ret = TRUE;
    *listingInProgress = FALSE;
    *notAccessible = FALSE;
    *getListing = FALSE;
    if (existingItem != NULL)
        *existingItem = NULL;
    if (nameExists != NULL)
        *nameExists = FALSE;
    HANDLES(EnterCriticalSection(&UploadLstCacheCritSect));
    CUploadListingsOnServer* server = FindServer(user, host, port, NULL);
    if (server == NULL) // the cache does not contain any path from this server yet; create an entry for the server
    {
        server = NewUploadCacheObject<CUploadListingsOnServer>(user, host, port);
        if (server != NULL && server->IsGood())
        {
            ListingsOnServer.Add(server);
            if (!ListingsOnServer.IsGood())
            {
                ListingsOnServer.ResetState();
                delete server;
                server = NULL;
                ret = FALSE;
            }
        }
        else
        {
            if (server != NULL)
            {
                delete server;
                server = NULL;
            }
            else
                TRACE_E(LOW_MEMORY);
            ret = FALSE;
        }
    }
    if (server != NULL)
    {
        ret = server->GetListing(path, pathType, workerMsg, workerUID, listingInProgress,
                                 notAccessible, getListing, name, existingItem, nameExists,
                                 textCodec);
    }
    HANDLES(LeaveCriticalSection(&UploadLstCacheCritSect));
    return ret;
}

void CUploadListingCache::ListingFailed(const wchar_t* user, const wchar_t* host, unsigned short port,
                                        const char* path, CFTPServerPathType pathType,
                                        BOOL listingIsNotAccessible,
                                        CUploadWaitingWorker** uploadFirstWaitingWorker,
                                        BOOL* listingOKErrorIgnored)
{
    if (listingOKErrorIgnored != NULL)
        *listingOKErrorIgnored = FALSE;
    HANDLES(EnterCriticalSection(&UploadLstCacheCritSect));
    CUploadListingsOnServer* server = FindServer(user, host, port, NULL);
    if (server != NULL)
    {
        server->ListingFailed(path, pathType, listingIsNotAccessible,
                              uploadFirstWaitingWorker, listingOKErrorIgnored);
    }
    else
        TRACE_E("CUploadListingCache::ListingFailed(): server not found!");
    HANDLES(LeaveCriticalSection(&UploadLstCacheCritSect));
}

BOOL CUploadListingCache::ListingFinished(const wchar_t* user, const wchar_t* host, unsigned short port,
                                          const char* path, CFTPServerPathType pathType,
                                          const char* pathListing, int pathListingLen,
                                          const CFTPDate& pathListingDate, const char* welcomeReply,
                                          const char* systReply, const char* suggestedListingServerType,
                                          const CFtpTextCodec& textCodec)
{
    BOOL ret = TRUE;
    HANDLES(EnterCriticalSection(&UploadLstCacheCritSect));
    CUploadListingsOnServer* server = FindServer(user, host, port, NULL);
    if (server != NULL)
    {
        ret = server->ListingFinished(path, pathType, pathListing, pathListingLen,
                                      pathListingDate, welcomeReply, systReply,
                                      suggestedListingServerType, textCodec);
    }
    else
        TRACE_E("CUploadListingCache::ListingFinished(): server not found!");
    HANDLES(LeaveCriticalSection(&UploadLstCacheCritSect));
    return ret;
}

//
// ****************************************************************************
// CUploadListingsOnServer
//

CUploadListingsOnServer::CUploadListingsOnServer(const wchar_t* user, const wchar_t* host,
                                                 unsigned short port) : Listing(50, 100)
{
    if (user != NULL && wcscmp(user, L"anonymous") == 0)
        user = NULL;
    Anonymous = user == NULL;
    Valid = host != NULL && FtpStoreWideText(host, Host) &&
            (Anonymous || FtpStoreWideText(user, User));
    Port = port;
    memset(FoundPathIndexes, -1, sizeof(FoundPathIndexes));
}

CUploadListingsOnServer::~CUploadListingsOnServer()
{
    int i;
    for (i = 0; i < Listing.Count; i++)
    {
        CUploadListingState state = Listing[i]->ListingState;
        if (state != ulsReady && state != ulsNotAccessible)
            TRACE_E("CUploadListingsOnServer::~CUploadListingsOnServer(): listing in forbidden state (in progress): " << state);
    }
}

BOOL CUploadListingsOnServer::AddOrUpdateListing(const char* path, CFTPServerPathType pathType,
                                                 const char* pathListing, int pathListingLen,
                                                 const CFTPDate& pathListingDate,
                                                 DWORD listingStartTime, BOOL onlyUpdate,
                                                 const char* welcomeReply, const char* systReply,
                                                 const char* suggestedListingServerType,
                                                 const CFtpTextCodec& textCodec)
{
    BOOL ret = TRUE;
    int index;
    if (FindPath(path, pathType, index)) // the path is already in the cache
    {
        CUploadPathListing* listing = Listing[index];
        DWORD listingTime;
        if (listing->ListingState == ulsInProgress)
            listingTime = listing->ListingStartTime; // the changes are currently only in the queue, so they can also be used on a listing older than LatestChangeTime
        else
            listingTime = max(listing->LatestChangeTime, listing->ListingStartTime);
        if (listingTime < listingStartTime) // if this is a newer listing, perform an update
        {
            listing->ClearListingItems();
            if (listing->ParseListing(pathListing, pathListingLen, pathListingDate, pathType,
                                      welcomeReply, systReply, suggestedListingServerType, NULL,
                                      textCodec))
            {
                if (listing->ListingState == ulsInProgress)
                {
                    CUploadListingChange* change = listing->FirstChange;
                    while (change != NULL)
                    {
                        if (change->ChangeTime > listingStartTime) // if the change also concerns the new listing, apply it
                        {
                            if (!listing->CommitChange(change))
                                break;
                        }
                        change = change->NextChange;
                    }
                    if (change != NULL)               // error while applying changes to the new listing
                        listing->ClearListingItems(); // discard the new listing; everything stays with the old one (as if the new listing could not be used)
                    else                              // the changes (if there were any) are applied to the new listing
                    {
                        listing->ListingState = ulsInProgressButObsolete;
                        listing->ListingStartTime = listingStartTime;

                        // discard the collected changes; they are no longer useful
                        listing->ClearListingChanges();
                        if (listing->LatestChangeTime < listingStartTime) // otherwise we must not reset listing->LatestChangeTime - it contains
                            listing->LatestChangeTime = 0;                // the time of the last change in the listing
                    }
                }
                else
                {
                    listing->ListingStartTime = listingStartTime;
                    listing->LatestChangeTime = 0; // reset because it is always less than 'listingStartTime' (so it is useless)
                    if (listing->ListingState == ulsNotAccessible)
                        listing->ListingState = ulsReady;
                    if (listing->ListingState == ulsInProgressButMayBeOutdated)
                        listing->ListingState = ulsInProgressButObsolete;
                }
            }
            else // error parsing the new listing or memory is low
            {
                if (listing->ListingState == ulsReady)
                {
                    Listing.Delete(index);
                    if (!Listing.IsGood())
                        Listing.ResetState(); // Delete cannot fail; it only reports an error when shrinking the array
                    ret = FALSE;
                }
                else
                {
                    listing->ClearListingItems();                          // discard the new listing
                    if (listing->ListingState == ulsInProgressButObsolete) // it is impossible to determine whether the transition to ulsInProgressButObsolete was preceded by an "unknown listing change", so go to the error state of the listing
                        listing->ListingState = ulsInProgressButMayBeOutdated;
                }
            }
        }
    }
    else // the path is not in the cache yet
    {
        if (!onlyUpdate) // if it is possible to add the listing, add it
        {
            CUploadPathListing* listing = NewUploadCacheObject<CUploadPathListing>(textCodec, path, pathType, ulsReady, listingStartTime, TRUE /* listing being added = listing from the panel */);
            if (listing != NULL && listing->IsGood())
            {
                if (listing->ParseListing(pathListing, pathListingLen, pathListingDate, pathType,
                                          welcomeReply, systReply, suggestedListingServerType, NULL,
                                          textCodec))
                {
                    Listing.Add(listing);
                    if (!Listing.IsGood())
                    {
                        Listing.ResetState();
                        delete listing;
                        ret = FALSE;
                    }
                }
                else // the listing cannot be parsed or memory is low
                {
                    delete listing;
                    ret = FALSE;
                }
            }
            else
            {
                if (listing != NULL)
                    delete listing;
                else
                    TRACE_E(LOW_MEMORY);
                ret = FALSE;
            }
        }
    }
    return ret;
}

void CUploadListingsOnServer::RemoveNotAccessibleListings()
{
    int i;
    for (i = Listing.Count - 1; i >= 0; i--)
    {
        CUploadPathListing* listing = Listing[i];
        if (listing->ListingState == ulsNotAccessible)
        {
            Listing.Delete(i);
            if (!Listing.IsGood())
                Listing.ResetState(); // Delete cannot fail; it only reports an error when shrinking the array
        }
    }
}

CUploadPathListing*
CUploadListingsOnServer::AddEmptyListing(const char* path, const char* dirName, CFTPServerPathType pathType,
                                         CUploadListingState listingState, BOOL doNotCheckIfPathIsKnown,
                                         const CFtpTextCodec& textCodec)
{
    CUploadPathListing* ret = NULL;
    std::string dir;
    if (!FtpStoreProtocolBytes(path != NULL ? path : "", dir))
        return NULL;
    if (dirName == NULL || FTPPathAppend(pathType, dir, dirName, TRUE))
    {
        int index;
        if (doNotCheckIfPathIsKnown || !FindPath(dir.c_str(), pathType, index)) // the path is not in the cache yet
        {
            CUploadPathListing* listing = NewUploadCacheObject<CUploadPathListing>(textCodec, dir.c_str(), pathType, listingState, IncListingCounter(), FALSE);
            if (listing != NULL && listing->IsGood())
            {
                Listing.Add(listing);
                if (Listing.IsGood())
                    ret = listing;
                else
                {
                    Listing.ResetState();
                    delete listing;
                }
            }
            else
            {
                if (listing != NULL)
                    delete listing;
                else
                    TRACE_E(LOW_MEMORY);
            }
        }
    }
    return ret;
}

BOOL CUploadListingsOnServer::FindPath(const char* path, CFTPServerPathType pathType, int& index)
{
    if (path == NULL)
    {
        TRACE_E("Unexpected situation in CUploadListingsOnServer::FindPath()!");
        return FALSE;
    }
#ifdef _DEBUG
    FoundPathIndexesTotal++;
#endif
    index = -1;
    int i;
    for (i = 0; i < FOUND_PATH_IND_CACHE_SIZE; i++) // first try indexes that succeeded in the past
    {
        int ind = FoundPathIndexes[i];
        if (ind >= 0 && ind < Listing.Count &&
            FTPIsTheSameServerPath(pathType, Listing[ind]->Path.c_str(), path))
        {
            index = ind;
            if (i > 0) // bubble the found index up to the first position in the lookup cache (giving it the longest lifetime)
            {
                memmove(FoundPathIndexes + 1, FoundPathIndexes, sizeof(int) * i);
                FoundPathIndexes[0] = i;
            }
#ifdef _DEBUG
            FoundPathIndexesInCache++;
#endif
            return TRUE;
        }
    }
    for (i = 0; i < Listing.Count; i++) // the cache did not help, search the entire array sequentially
    {
        if (FTPIsTheSameServerPath(pathType, Listing[i]->Path.c_str(), path))
        {
            // put the found index into the lookup cache at the first position and shift the others down
            memmove(FoundPathIndexes + 1, FoundPathIndexes, sizeof(int) * (FOUND_PATH_IND_CACHE_SIZE - 1));
            FoundPathIndexes[0] = i;

            index = i;
            return TRUE;
        }
    }
    return FALSE;
}

void CUploadListingsOnServer::ReportCreateDirs(const char* workPath, CFTPServerPathType pathType,
                                               const char* newDirs, BOOL unknownResult)
{
    std::string cutDir;
    std::string path;
    if (FTPIsPathRelative(pathType, newDirs))
    { // call ReportCreateDir sequentially for all directories being created
        std::string relPath;
        if (!FtpStoreProtocolBytes(newDirs != NULL ? newDirs : "", relPath) ||
            !FtpStoreProtocolBytes(workPath != NULL ? workPath : "", path))
            return;
        while (FTPCutFirstDirFromRelativePath(pathType, relPath, cutDir))
        {
            if (cutDir != ".") // assumption: "." is the current directory or is meaningless
            {
                if (cutDir == "..")
                    FTPCutDirectory(pathType, path, NULL, NULL); // assumption: ".." is the parent directory or is meaningless
                else
                {
                    int index;
                    if (FindPath(path.c_str(), pathType, index)) // find the path in the cache
                    {
                        if (unknownResult)
                            InvalidateListing(index);
                        else
                        {
                            BOOL dirCreated, lowMem;
                            Listing[index]->ReportCreateDir(cutDir.c_str(), &dirCreated, &lowMem);
                            if (lowMem)
                                InvalidateListing(index);
                            if (dirCreated)
                                AddEmptyListing(path.c_str(), cutDir.c_str(), pathType, ulsReady, FALSE,
                                                Listing[index]->TextCodec); // add an empty listing to the listing cache for the newly created directory
                        }
                    }
                    if (!FTPPathAppend(pathType, path, cutDir.c_str(), TRUE))
                        break;
                }
            }
        }
    }
    else // newDirs is an absolute path, so trim it up to the root and call ReportCreateDir for all subdirectories (we do not know how many subdirectories were created)
    {
        if (!FtpStoreProtocolBytes(newDirs != NULL ? newDirs : "", path) ||
            !FTPCompleteAbsolutePath(pathType, path, workPath != NULL ? workPath : "") ||
            !FTPRemovePointsFromPath(path, pathType))
            return;
        while (FTPCutDirectory(pathType, path, &cutDir, NULL))
        {
            int index;
            if (FindPath(path.c_str(), pathType, index)) // find the path in the cache
            {
                if (unknownResult)
                    InvalidateListing(index);
                else
                {
                    BOOL dirCreated, lowMem;
                    Listing[index]->ReportCreateDir(cutDir.c_str(), &dirCreated, &lowMem);
                    if (lowMem)
                        InvalidateListing(index);
                    if (dirCreated)
                        AddEmptyListing(path.c_str(), cutDir.c_str(), pathType, ulsReady, FALSE,
                                        Listing[index]->TextCodec); // add an empty listing to the listing cache for the newly created directory
                }
            }
        }
    }
}

void CUploadListingsOnServer::ReportRename(const char* workPath, CFTPServerPathType pathType,
                                           const char* fromName, const char* newName,
                                           BOOL unknownResult)
{
    // fromName is always a name (no path) on workPath - the change on workPath is certain;
    // newName is provided by the user and can be anything - for example, on VMS it is quite complicated to determine
    // what actually happened (a move within the server, changes just to the name/extension ("a." and ".a"),
    // etc.) - for now we simplify it to only a change on workPath - 99% of cases will be OK
    // (used only in Quick Rename for now, where it should not even occur to the user to enter a path)
    int index;
    if (FindPath(workPath, pathType, index))
        InvalidateListing(index);
}

void CUploadListingsOnServer::ReportDelete(const char* workPath, CFTPServerPathType pathType,
                                           const char* name, BOOL unknownResult)
{
    int index;
    BOOL invalidateNameDir = unknownResult;
    if (FindPath(workPath, pathType, index)) // find the path in the cache
    {
        if (unknownResult)
            InvalidateListing(index);
        else
        {
            BOOL lowMem;
            Listing[index]->ReportDelete(name, &invalidateNameDir, &lowMem);
            if (lowMem)
                InvalidateListing(index);
        }
    }

    if (invalidateNameDir)
    {
        // just in case 'name' is a directory or a link to a directory, invalidate
        // the listing of the path to that directory
        std::string path;
        if (FtpStoreProtocolBytes(workPath != NULL ? workPath : "", path) &&
            FTPPathAppend(pathType, path, name, TRUE))
        {
            if (FindPath(path.c_str(), pathType, index)) // find the path in the cache (if it was a file, the path cannot be found)
                InvalidateListing(index);
        }
    }
}

void CUploadListingsOnServer::ReportStoreFile(const char* workPath, CFTPServerPathType pathType, const char* name)
{
    int index;
    if (FindPath(workPath, pathType, index)) // find the path in the cache
    {
        BOOL lowMem;
        Listing[index]->ReportStoreFile(name, &lowMem);
        if (lowMem)
            InvalidateListing(index);
    }
}

void CUploadListingsOnServer::ReportFileUploaded(const char* workPath, CFTPServerPathType pathType, const char* name,
                                                 const CQuadWord& fileSize, BOOL unknownResult)
{
    int index;
    if (FindPath(workPath, pathType, index)) // find the path in the cache
    {
        if (unknownResult)
            InvalidateListing(index);
        else
        {
            BOOL lowMem;
            Listing[index]->ReportFileUploaded(name, fileSize, &lowMem);
            if (lowMem)
                InvalidateListing(index);
        }
    }
}

void CUploadListingsOnServer::InvalidatePathListing(const char* path, CFTPServerPathType pathType)
{
    int index;
    if (FindPath(path, pathType, index))
        InvalidateListing(index);
}

BOOL CUploadListingsOnServer::IsListingFromPanel(const char* path, CFTPServerPathType pathType)
{
    int index;
    if (FindPath(path, pathType, index))
        return Listing[index]->FromPanel;
    return FALSE;
}

void CUploadListingsOnServer::ReportUnknownChange(const char* workPath, CFTPServerPathType pathType)
{
    int index;
    if (FindPath(workPath, pathType, index))
        InvalidateListing(index);
}

void CUploadListingsOnServer::InvalidateListing(int index)
{
    CUploadPathListing* listing = Listing[index];
    if (listing->ListingState == ulsReady) // remove the listing from the array because it can no longer be trusted
    {
        Listing.Delete(index);
        if (!Listing.IsGood())
            Listing.ResetState(); // Delete cannot fail; it only reports an error when shrinking the array
    }
    else
    {
        if (listing->ListingState != ulsNotAccessible)
        {
            if (listing->ListingState == ulsInProgressButObsolete) // discard the newer listing; it will no longer be useful
                listing->ClearListingItems();
            if (listing->ListingState == ulsInProgress) // discard the accumulated changes; they are no longer useful
                listing->ClearListingChanges();
            listing->ListingState = ulsInProgressButMayBeOutdated;
        }
        listing->LatestChangeTime = IncListingCounter();
        listing->FromPanel = FALSE;
    }
}

BOOL CUploadListingsOnServer::GetListing(const char* path, CFTPServerPathType pathType, int workerMsg,
                                         int workerUID, BOOL* listingInProgress, BOOL* notAccessible,
                                         BOOL* getListing, const char* name, CUploadListingItem** existingItem,
                                         BOOL* nameExists, const CFtpTextCodec& textCodec)
{
    BOOL ret = TRUE;
    int index;
    if (FindPath(path, pathType, index))
    {
        CUploadPathListing* listing = Listing[index];
        if (listing->ListingState == ulsNotAccessible) // the listing is "unobtainable"
            *notAccessible = TRUE;
        else
        {
            if (listing->ListingState == ulsInProgress ||
                listing->ListingState == ulsInProgressButObsolete ||
                listing->ListingState == ulsInProgressButMayBeOutdated) // the listing is being fetched; add a worker to report completion/error
            {
                *listingInProgress = TRUE;
                ret = listing->AddWaitingWorker(workerMsg, workerUID);
            }
            else
            {
                if (listing->ListingState == ulsReady) // the listing is ready
                {
                    int index2;
                    BOOL lowMemory = FALSE;
                    if (listing->FindItem(name, index2, &lowMemory)) // item 'name' found; copy its data into 'existingItem' and set 'nameExists' to TRUE
                    {
                        if (nameExists != NULL)
                            *nameExists = TRUE;
                        if (existingItem != NULL)
                        {
                            CUploadListingItem* item = listing->ListingItem[index2];
                            *existingItem = NewUploadCacheObject<CUploadListingItem>();
                            if (*existingItem != NULL)
                            {
                                if (FtpStoreProtocolBytes(item->Name, (*existingItem)->Name) &&
                                    (!item->NameTextValid || FtpStoreWideText(item->NameText, (*existingItem)->NameText)))
                                {
                                    (*existingItem)->NameTextValid = item->NameTextValid;
                                    (*existingItem)->ItemType = item->ItemType;
                                    (*existingItem)->ByteSize = item->ByteSize;
                                }
                                else
                                {
                                    ret = FALSE;
                                    delete *existingItem;
                                    *existingItem = NULL;
                                }
                            }
                            else
                                ret = FALSE;
                            if (!ret)
                                TRACE_E(LOW_MEMORY);
                        }
                    }
                    else if (lowMemory)
                    {
                        TRACE_E(LOW_MEMORY);
                        ret = FALSE;
                    }
                }
                else
                    TRACE_E("CUploadListingsOnServer::GetListing(): Unexpected state of listing: " << listing->ListingState);
            }
        }
    }
    else // the path is not in the cache yet
    {
        CUploadPathListing* listing = AddEmptyListing(path, NULL, pathType, ulsInProgress, TRUE,
                                                      textCodec);
        if (listing != NULL)
        {
            *getListing = TRUE;
            *listingInProgress = TRUE;
        }
        else
            ret = FALSE;
    }
    return ret;
}

void CUploadListingsOnServer::ListingFailed(const char* path, CFTPServerPathType pathType,
                                            BOOL listingIsNotAccessible,
                                            CUploadWaitingWorker** uploadFirstWaitingWorker,
                                            BOOL* listingOKErrorIgnored)
{
    int index;
    if (FindPath(path, pathType, index))
    {
        CUploadPathListing* listing = Listing[index];
        if (listing->ListingState == ulsInProgress ||
            listing->ListingState == ulsInProgressButObsolete ||
            listing->ListingState == ulsInProgressButMayBeOutdated)
        {
            listing->InformWaitingWorkers(uploadFirstWaitingWorker);
            if (listing->ListingState == ulsInProgressButObsolete) // we already have the listing, so we do not mind that the worker reports a listing error
            {
                listing->ListingState = ulsReady;
                if (listingOKErrorIgnored != NULL)
                    *listingOKErrorIgnored = TRUE;
            }
            else
            {
                if (listingIsNotAccessible)
                {
                    listing->ClearListingItems();
                    listing->ClearListingChanges();
                    listing->LatestChangeTime = 0;
                    listing->ListingStartTime = IncListingCounter(); // only a new listing can update it
                    listing->ListingState = ulsNotAccessible;
                }
                else
                {
                    Listing.Delete(index);
                    if (!Listing.IsGood())
                        Listing.ResetState(); // Delete cannot fail; it only reports an error when shrinking the array
                }
            }
        }
        else
            TRACE_E("CUploadListingsOnServer::ListingFailed(): listing is not in progress!");
    }
    else
        TRACE_E("CUploadListingsOnServer::ListingFailed(): path was not found!");
}

BOOL CUploadListingsOnServer::ListingFinished(const char* path, CFTPServerPathType pathType,
                                              const char* pathListing, int pathListingLen,
                                              const CFTPDate& pathListingDate, const char* welcomeReply,
                                              const char* systReply, const char* suggestedListingServerType,
                                              const CFtpTextCodec& textCodec)
{
    BOOL ret = TRUE;
    int index;
    if (FindPath(path, pathType, index))
    {
        CUploadPathListing* listing = Listing[index];
        if (listing->ListingState == ulsInProgress ||
            listing->ListingState == ulsInProgressButObsolete ||
            listing->ListingState == ulsInProgressButMayBeOutdated)
        {
            listing->InformWaitingWorkers(NULL);
            if (listing->ListingState == ulsInProgressButObsolete) // we already have a newer listing than this one, so ignore this listing (stay with the newer one)
                listing->ListingState = ulsReady;
            else
            {
                if (listing->ListingState == ulsInProgressButMayBeOutdated) // we know this listing is probably invalid (and we have no other), so remove the path from the cache (the listing will be downloaded again)
                {
                    Listing.Delete(index);
                    if (!Listing.IsGood())
                        Listing.ResetState(); // Delete cannot fail; it only reports an error when shrinking the array
                }
                else // ulsInProgress: process the new listing and commit the changes to it
                {
                    listing->ClearListingItems(); // probably unnecessary, just to be safe
                    BOOL lowMem;
                    if (listing->ParseListing(pathListing, pathListingLen, pathListingDate, pathType,
                                              welcomeReply, systReply, suggestedListingServerType, &lowMem,
                                              textCodec))
                    {
                        CUploadListingChange* change = listing->FirstChange;
                        while (change != NULL)
                        {
                            if (change->ChangeTime > listing->ListingStartTime) // probably "always true"
                            {
                                if (!listing->CommitChange(change))
                                    break;
                            }
                            change = change->NextChange;
                        }
                        if (change != NULL) // error while applying changes to the new listing - it can only be a lack of memory - temporary error - remove the path from the cache; the listing will be downloaded again later
                        {
                            Listing.Delete(index);
                            if (!Listing.IsGood())
                                Listing.ResetState(); // Delete cannot fail; it only reports an error when shrinking the array
                            ret = FALSE;
                        }
                        else // the changes (if there were any) are applied to the new listing
                        {
                            listing->ListingState = ulsReady;
                            listing->ClearListingChanges(); // discard the accumulated changes; they are no longer useful
                        }
                    }
                    else
                    {
                        if (lowMem) // lack of memory = temporary error - remove the path from the cache; the listing will be downloaded again later
                        {
                            Listing.Delete(index);
                            if (!Listing.IsGood())
                                Listing.ResetState(); // Delete cannot fail; it only reports an error when shrinking the array
                            ret = FALSE;
                        }
                        else // parsing error of the new listing = permanent error - mark the listing for this path as "unobtainable"
                        {
                            listing->ListingState = ulsNotAccessible;
                            listing->ClearListingChanges();
                            listing->ClearListingItems();
                        }
                    }
                }
            }
        }
        else
            TRACE_E("CUploadListingsOnServer::ListingFinished(): listing is not in progress!");
    }
    else
        TRACE_E("CUploadListingsOnServer::ListingFinished(): path was not found!");
    return ret;
}

//
// ****************************************************************************
// CUploadPathListing
//

CUploadPathListing::CUploadPathListing(const CFtpTextCodec& textCodec, const char* path, CFTPServerPathType pathType,
                                       CUploadListingState listingState, DWORD listingStartTime,
                                       BOOL fromPanel)
    : TextCodec(textCodec), ListingItem(50, 200, dtNoDelete)
{
    Valid = path != NULL && FtpStoreProtocolBytes(path, Path);
    PathType = pathType;

    ListingState = listingState;
    ListingStartTime = listingStartTime;
    FirstChange = NULL;
    LastChange = NULL;
    LatestChangeTime = 0;
    FirstWaitingWorker = NULL;
    FromPanel = fromPanel;

}

CUploadPathListing::~CUploadPathListing()
{
    if (FirstWaitingWorker != NULL)
        TRACE_E("CUploadPathListing::~CUploadPathListing(): FirstWaitingWorker is not NULL!");
    ClearListingItems();
    ClearListingChanges();
}

void CUploadPathListing::ClearListingItems()
{
    int i;
    for (i = 0; i < ListingItem.Count; i++)
    {
        CUploadListingItem* item = ListingItem[i];
        delete item;
    }
    ListingItem.DetachMembers();
}

void CUploadPathListing::ClearListingChanges()
{
    CUploadListingChange* change = FirstChange;
    while (change != NULL)
    {
        CUploadListingChange* del = change;
        change = change->NextChange;
        delete del;
    }
    FirstChange = LastChange = NULL;
}

BOOL CUploadPathListing::AddItemDoNotSort(CUploadListingItemType itemType, const char* name, const CQuadWord& byteSize)
{
    if (name == NULL)
        TRACE_E("Unexpected situation in CUploadPathListing::AddItemDoNotSort()!");
    else
    {
        CUploadListingItem* item = NewUploadCacheObject<CUploadListingItem>();
        if (item != NULL)
        {
            if (StoreUploadListingItemName(*item, TextCodec, name))
            {
                item->ItemType = itemType;
                item->ByteSize = byteSize;

                ListingItem.Add(item);
                if (ListingItem.IsGood())
                    return TRUE;
                else
                    ListingItem.ResetState();
            }
            delete item;
        }
        else
            TRACE_E(LOW_MEMORY);
    }
    return FALSE;
}

BOOL CUploadPathListing::InsertNewItem(int index, CUploadListingItemType itemType, const char* name,
                                       const CQuadWord& byteSize)
{
    if (name == NULL)
        TRACE_E("Unexpected situation in CUploadPathListing::InsertNewItem()!");
    else
    {
        CUploadListingItem* item = NewUploadCacheObject<CUploadListingItem>();
        if (item != NULL)
        {
            if (StoreUploadListingItemName(*item, TextCodec, name))
            {
                item->ItemType = itemType;
                item->ByteSize = byteSize;

                ListingItem.Insert(index, item);
                if (ListingItem.IsGood())
                    return TRUE;
                else
                    ListingItem.ResetState();
            }
            delete item;
        }
        else
            TRACE_E(LOW_MEMORY);
    }
    return FALSE;
}

void UploadListingSortItemsAux(TIndirectArray<CUploadListingItem>* listingItem, int left, int right)
{

LABEL_UploadListingSortItemsAux:

    int i = left, j = right;
    const char* pivot = listingItem->At((i + j) / 2)->Name.c_str();

    do
    {
        while (strcmp(listingItem->At(i)->Name.c_str(), pivot) < 0 && i < right)
            i++;
        while (strcmp(pivot, listingItem->At(j)->Name.c_str()) < 0 && j > left)
            j--;

        if (i <= j)
        {
            CUploadListingItem* swap = listingItem->At(i);
            listingItem->At(i) = listingItem->At(j);
            listingItem->At(j) = swap;
            i++;
            j--;
        }
    } while (i <= j);

    // the following "nice" code was replaced with code that significantly saves stack usage (maximum log(N) recursion depth)
    //  if (left < j) UploadListingSortItemsAux(listingItem, left, j);
    //  if (i < right) UploadListingSortItemsAux(listingItem, i, right);

    if (left < j)
    {
        if (i < right)
        {
            if (j - left < right - i) // both "halves" need to be sorted, so recurse into the smaller one and process the other via "goto"
            {
                UploadListingSortItemsAux(listingItem, left, j);
                left = i;
                goto LABEL_UploadListingSortItemsAux;
            }
            else
            {
                UploadListingSortItemsAux(listingItem, i, right);
                right = j;
                goto LABEL_UploadListingSortItemsAux;
            }
        }
        else
        {
            right = j;
            goto LABEL_UploadListingSortItemsAux;
        }
    }
    else
    {
        if (i < right)
        {
            left = i;
            goto LABEL_UploadListingSortItemsAux;
        }
    }
}

void UploadListingSortItemsCaseInsensitiveAux(TIndirectArray<CUploadListingItem>* listingItem, int left, int right)
{

LABEL_UploadListingSortItemsCaseInsensitiveAux:

    int i = left, j = right;
    const CUploadListingItem* pivot = listingItem->At((i + j) / 2);

    do
    {
        while (CompareUploadListingNamesNoCase(*listingItem->At(i), *pivot) < 0 && i < right)
            i++;
        while (CompareUploadListingNamesNoCase(*pivot, *listingItem->At(j)) < 0 && j > left)
            j--;

        if (i <= j)
        {
            CUploadListingItem* swap = listingItem->At(i);
            listingItem->At(i) = listingItem->At(j);
            listingItem->At(j) = swap;
            i++;
            j--;
        }
    } while (i <= j);

    // the following "nice" code was replaced with code that significantly saves stack usage (maximum log(N) recursion depth)
    //  if (left < j) UploadListingSortItemsCaseInsensitiveAux(listingItem, left, j);
    //  if (i < right) UploadListingSortItemsCaseInsensitiveAux(listingItem, i, right);

    if (left < j)
    {
        if (i < right)
        {
            if (j - left < right - i) // both "halves" need to be sorted, so recurse into the smaller one and process the other via "goto"
            {
                UploadListingSortItemsCaseInsensitiveAux(listingItem, left, j);
                left = i;
                goto LABEL_UploadListingSortItemsCaseInsensitiveAux;
            }
            else
            {
                UploadListingSortItemsCaseInsensitiveAux(listingItem, i, right);
                right = j;
                goto LABEL_UploadListingSortItemsCaseInsensitiveAux;
            }
        }
        else
        {
            right = j;
            goto LABEL_UploadListingSortItemsCaseInsensitiveAux;
        }
    }
    else
    {
        if (i < right)
        {
            left = i;
            goto LABEL_UploadListingSortItemsCaseInsensitiveAux;
        }
    }
}

void CUploadPathListing::SortItems()
{
    if (ListingItem.Count > 1)
    {
        if (FTPIsCaseSensitive(PathType))
            UploadListingSortItemsAux(&ListingItem, 0, ListingItem.Count - 1);
        else
            UploadListingSortItemsCaseInsensitiveAux(&ListingItem, 0, ListingItem.Count - 1);
    }
}

BOOL CUploadPathListing::FindItem(const char* name, int& index, BOOL* lowMemory) noexcept
{
    if (lowMemory != NULL)
        *lowMemory = FALSE;
    if (name == NULL)
    {
        index = 0;
        return FALSE;
    }
    if (ListingItem.Count == 0)
    {
        index = 0;
        return FALSE;
    }

    BOOL caseSensitive = FTPIsCaseSensitive(PathType);
    const std::string_view nameBytes(name, strlen(name));
    std::wstring nameText;
    BOOL nameTextValid = FALSE;
    if (!caseSensitive)
    {
        switch (TextCodec.DecodeForComparison(nameBytes.data(), nameBytes.size(), nameText))
        {
        case CFtpTextDecodeStatus::Success:
            nameTextValid = TRUE;
            break;

        case CFtpTextDecodeStatus::InvalidInput:
            break;

        default:
            index = 0;
            if (lowMemory != NULL)
                *lowMemory = TRUE;
            return FALSE;
        }
    }
    int l = 0, r = ListingItem.Count - 1, m;
    while (1)
    {
        m = (l + r) / 2;
        int res;
        if (caseSensitive)
            res = CompareUploadListingByteNames(ListingItem[m]->Name, nameBytes);
        else
            res = CompareUploadListingNameNoCase(*ListingItem[m], nameBytes, nameText,
                                                 nameTextValid);
        if (res == 0) // found
        {
            index = m;
            return TRUE;
        }
        else if (res > 0)
        {
            if (l == r || l > m - 1) // not found
            {
                index = m; // it should be at this position
                return FALSE;
            }
            r = m - 1;
        }
        else
        {
            if (l == r) // not found
            {
                index = m + 1; // it should be right after this position
                return FALSE;
            }
            l = m + 1;
        }
    }
}

BOOL CUploadPathListing::ParseListing(const char* pathListing, int pathListingLen, const CFTPDate& pathListingDate,
                                      CFTPServerPathType pathType, const char* welcomeReply, const char* systReply,
                                      const char* suggestedListingServerType, BOOL* lowMemory,
                                      const CFtpTextCodec& textCodec)
{
    CALL_STACK_MESSAGE2("CUploadPathListing::ParseListing(, %d,)", pathListingLen);

    if (lowMemory != NULL)
        *lowMemory = FALSE;
    if (pathListing == NULL || welcomeReply == NULL || systReply == NULL)
    {
        TRACE_E("CUploadPathListing::ParseListing(): pathListing == NULL or welcomeReply == NULL or systReply == NULL!");
        return FALSE;
    }
    TextCodec = textCodec;

    BOOL isVMS = pathType == ftpsptOpenVMS;
    BOOL needSimpleListing = TRUE;
    std::string listingServerType;
    if (suggestedListingServerType != NULL &&
        !FtpStoreLocalTextBytes(suggestedListingServerType, listingServerType))
    {
        if (lowMemory != NULL)
            *lowMemory = TRUE;
        return FALSE;
    }

    // reset the helper variable used to determine which server type has already been tested (unsuccessfully)
    CServerTypeList* serverTypeList = Config.LockServerTypeList();
    int serverTypeListCount = serverTypeList->Count;
    int j;
    for (j = 0; j < serverTypeListCount; j++)
        serverTypeList->At(j)->ParserAlreadyTested = FALSE;

    BOOL err = FALSE;
    CServerType* serverType = NULL;
    if (!listingServerType.empty()) // this is not autodetection; find listingServerType
    {
        int i;
        for (i = 0; i < serverTypeListCount; i++)
        {
            serverType = serverTypeList->At(i);
            const char* s = serverType->TypeName;
            if (*s == '*')
                s++;
            const CFtpTextCompareStatus comparison = FtpCompareLocalTextNoCase(listingServerType, s);
            if (comparison == CFtpTextCompareStatus::Failure)
            {
                err = TRUE;
                if (lowMemory != NULL)
                    *lowMemory = TRUE;
                break;
            }
            if (comparison == CFtpTextCompareStatus::Equal)
            {
                // serverType has been selected; try its parser on the listing
                serverType->ParserAlreadyTested = TRUE;
                if (ParseListingToArray(pathListing, pathListingLen, pathListingDate, serverType, &err, isVMS, textCodec))
                    needSimpleListing = FALSE; // successfully parsed the listing
                if (err && lowMemory != NULL)
                    *lowMemory = TRUE;
                break; // found the requested server type, done
            }
        }
        if (i == serverTypeListCount)
            listingServerType.clear(); // listingServerType does not exist -> perform autodetection
    }

    // autodetection - select the server type whose autodetection condition is satisfied
    if (!err && needSimpleListing && listingServerType.empty())
    {
        int welcomeReplyLen = (int)strlen(welcomeReply);
        int systReplyLen = (int)strlen(systReply);
        int i;
        for (i = 0; i < serverTypeListCount; i++)
        {
            serverType = serverTypeList->At(i);
            if (!serverType->ParserAlreadyTested) // only if we have not tried it yet
            {
                if (serverType->CompiledAutodetCond == NULL)
                {
                    serverType->CompiledAutodetCond = CompileAutodetectCond(HandleNULLStr(serverType->AutodetectCond),
                                                                            NULL, NULL, NULL, NULL);
                    if (serverType->CompiledAutodetCond == NULL) // can only be a memory shortage error
                    {
                        err = TRUE;
                        if (lowMemory != NULL)
                            *lowMemory = TRUE;
                        break;
                    }
                }
                if (serverType->CompiledAutodetCond->Evaluate(welcomeReply, welcomeReplyLen,
                                                              systReply, systReplyLen))
                {
                    // serverType has been selected; try its parser on the listing
                    serverType->ParserAlreadyTested = TRUE;
                    if (ParseListingToArray(pathListing, pathListingLen, pathListingDate, serverType, &err, isVMS, textCodec) || err)
                    {
                        if (err && lowMemory != NULL)
                            *lowMemory = TRUE;
                        if (!err)
                        {
                            const char* s = serverType->TypeName;
                            if (*s == '*')
                                s++;
                            if (!FtpStoreLocalTextBytes(s, listingServerType))
                            {
                                err = TRUE;
                                if (lowMemory != NULL)
                                    *lowMemory = TRUE;
                            }
                        }
                        needSimpleListing = err; // either successfully parsed the listing or ran into a memory shortage error, finish
                        break;
                    }
                }
            }
        }

        // autodetection - selection of the remaining server types
        if (!err && needSimpleListing)
        {
            int k;
            for (k = 0; k < serverTypeListCount; k++)
            {
                serverType = serverTypeList->At(k);
                if (!serverType->ParserAlreadyTested) // only if we have not tried it yet
                {
                    // serverType has been selected; try its parser on the listing
                    // serverType->ParserAlreadyTested = TRUE;  // unnecessary, not used later
                    if (ParseListingToArray(pathListing, pathListingLen, pathListingDate, serverType, &err, isVMS, textCodec) || err)
                    {
                        if (err && lowMemory != NULL)
                            *lowMemory = TRUE;
                        if (!err)
                        {
                            const char* s = serverType->TypeName;
                            if (*s == '*')
                                s++;
                            if (!FtpStoreLocalTextBytes(s, listingServerType))
                            {
                                err = TRUE;
                                if (lowMemory != NULL)
                                    *lowMemory = TRUE;
                            }
                        }
                        needSimpleListing = err; // either successfully parsed the listing or ran into a memory shortage error, finish
                        break;
                    }
                }
            }
        }
    }
    Config.UnlockServerTypeList();
    return !err && !needSimpleListing;
}

BOOL CUploadPathListing::ParseListingToArray(const char* pathListing, int pathListingLen,
                                             const CFTPDate& pathListingDate,
                                             CServerType* serverType, BOOL* lowMem, BOOL isVMS,
                                             const CFtpTextCodec& textCodec)
{
    BOOL ret = FALSE;
    *lowMem = FALSE;
    ClearListingItems();

    // compute the 'validDataMask' mask
    DWORD validDataMask = VALID_DATA_HIDDEN | VALID_DATA_ISLINK; // Name + NameLen + Hidden + IsLink
    int i;
    for (i = 0; i < serverType->Columns.Count; i++)
    {
        switch (serverType->Columns[i]->Type)
        {
        case stctExt:
            validDataMask |= VALID_DATA_EXTENSION;
            break;
        case stctSize:
            validDataMask |= VALID_DATA_SIZE;
            break;
        case stctDate:
            validDataMask |= VALID_DATA_DATE;
            break;
        case stctTime:
            validDataMask |= VALID_DATA_TIME;
            break;
        case stctType:
            validDataMask |= VALID_DATA_TYPE;
            break;
        }
    }

    BOOL err = FALSE;
    CFTPListingPluginDataInterface* dataIface = new CFTPListingPluginDataInterface(&(serverType->Columns), FALSE,
                                                                                   validDataMask, isVMS,
                                                                                   textCodec);
    if (dataIface != NULL)
    {
        DWORD* emptyCol = new DWORD[serverType->Columns.Count]; // helper preallocated array for GetNextItemFromListing
        if (dataIface->IsGood() && emptyCol != NULL)
        {
            validDataMask |= dataIface->GetPLValidDataMask();
            CFTPParser* parser = serverType->CompiledParser;
            if (parser == NULL)
            {
                parser = CompileParsingRules(HandleNULLStr(serverType->RulesForParsing), &(serverType->Columns),
                                             NULL, NULL, NULL);
                serverType->CompiledParser = parser; // do not deallocate 'parser'; it is already stored in 'serverType'
            }
            if (parser != NULL)
            {
                CFileData file;
                const char* listing = pathListing;
                const char* listingEnd = pathListing + pathListingLen;
                BOOL isDir = FALSE;

                int rightsCol = dataIface->FindRightsColumn(); // index of the column with rights (used for link detection)

                parser->BeforeParsing(listing, listingEnd, pathListingDate.Year, pathListingDate.Month,
                                      pathListingDate.Day, FALSE); // initialize the parser
                while (parser->GetNextItemFromListing(&file, &isDir, dataIface, &(serverType->Columns), &listing,
                                                      listingEnd, NULL, &err, emptyCol, textCodec))
                {
                    if (!isDir || file.NameLen > 2 ||
                        file.Name[0] != '.' || (file.Name[1] != 0 && file.Name[1] != '.')) // not the directories "." and ".."
                    {
                        CUploadListingItemType itemType;
                        if (rightsCol != -1 && IsUNIXLink(dataIface->GetStringFromColumn(file, rightsCol)))
                            itemType = ulitLink;
                        else
                            itemType = isDir ? ulitDirectory : ulitFile;

                        BOOL sizeInBytes; // TRUE = 'size' is in bytes
                        CQuadWord size;   // variable for the size of the current file
                        if (itemType != ulitFile || !dataIface->GetSize(file, size, sizeInBytes) || !sizeInBytes)
                            size = UPLOADSIZE_UNKNOWN; // the size of the file in bytes is unknown

                        std::string itemName;
                        err = !dataIface->GetWireName(file, textCodec, itemName) ||
                              !AddItemDoNotSort(itemType, itemName.c_str(), size);
                    }
                    // release the data for the file or directory
                    dataIface->ReleasePluginData(file, isDir);
                    SalamanderGeneral->Free(file.Name);

                    if (err)
                        break;
                }
                if (!err && listing == listingEnd)
                    ret = TRUE; // parsing completed successfully
            }
            else
                err = TRUE; // can only be a memory shortage
        }
        else
        {
            if (emptyCol == NULL)
                TRACE_E(LOW_MEMORY);
            err = TRUE; // low memory
        }
        if (emptyCol != NULL)
            delete[] emptyCol;
        if (dataIface != NULL)
            delete dataIface;
    }
    else
    {
        TRACE_E(LOW_MEMORY);
        err = TRUE; // low memory
    }
    *lowMem = err;
    if (ret)
        SortItems();
    return ret;
}

void CUploadPathListing::ReportCreateDir(const char* newDir, BOOL* dirCreated, BOOL* lowMem)
{
    LatestChangeTime = IncListingCounter(); // record the time of this change
    *lowMem = FALSE;
    *dirCreated = FALSE;
    int index;
    switch (ListingState)
    {
    case ulsReady:
    case ulsInProgressButObsolete:
    {
        if (!FindItem(newDir, index, lowMem)) // the name is free; it is possible to create the directory
        {
            if (*lowMem)
                break;
            if (InsertNewItem(index, ulitDirectory, newDir, UPLOADSIZE_UNKNOWN))
                *dirCreated = TRUE;
            else
                *lowMem = TRUE;
        }
        break;
    }

    case ulsInProgress:
    {
        CUploadListingChange* ch = NewUploadCacheObject<CUploadListingChange>(LatestChangeTime, ulctCreateDir, newDir);
        if (ch != NULL && ch->IsGood())
            AddChange(ch);
        else
        {
            if (ch != NULL)
                delete ch;
            else
                TRACE_E(LOW_MEMORY);
            *lowMem = TRUE;
        }
        break;
    }

        // case ulsInProgressButMayBeOutdated:   // nothing to do
        // case ulsNotAccessible:                // nothing to do
    }
}

void CUploadPathListing::ReportDelete(const char* name, BOOL* invalidateNameDir, BOOL* lowMem)
{
    LatestChangeTime = IncListingCounter(); // record the time of this change
    *lowMem = FALSE;
    *invalidateNameDir = TRUE; // we do not know whether it is a directory or a link, so try to invalidate the directory listing (if it is a file, it will be ignored)
    int index;
    switch (ListingState)
    {
    case ulsReady:
    case ulsInProgressButObsolete:
    {
        if (FindItem(name, index, lowMem))
        {
            CUploadListingItem* item = ListingItem[index];
            if (item->ItemType == ulitFile)
                *invalidateNameDir = FALSE; // file = no need to invalidate the directory listing
            delete item;
            ListingItem.Detach(index);
            if (!ListingItem.IsGood())
                ListingItem.ResetState();
        }
        else if (!*lowMem)
            TRACE_E("CUploadPathListing::ReportDelete(): delete for unknown name reported: " << name); // warning only
        break;
    }

    case ulsInProgress:
    {
        CUploadListingChange* ch = NewUploadCacheObject<CUploadListingChange>(LatestChangeTime, ulctDelete, name);
        if (ch != NULL && ch->IsGood())
            AddChange(ch);
        else
        {
            if (ch != NULL)
                delete ch;
            else
                TRACE_E(LOW_MEMORY);
            *lowMem = TRUE;
        }
        break;
    }

        // case ulsInProgressButMayBeOutdated:   // nothing to do
        // case ulsNotAccessible:                // nothing to do
    }
}

void CUploadPathListing::ReportStoreFile(const char* name, BOOL* lowMem)
{
    LatestChangeTime = IncListingCounter(); // record the time of this change
    *lowMem = FALSE;
    int index;
    switch (ListingState)
    {
    case ulsReady:
    case ulsInProgressButObsolete:
    {
        if (!FindItem(name, index, lowMem)) // the name is free; create the file
        {
            if (*lowMem)
                break;
            if (!InsertNewItem(index, ulitFile, name, UPLOADSIZE_NEEDUPDATE))
                *lowMem = TRUE;
        }
        else // the name already exists; if it is a file, invalidate its size (a link has no size)
        {
            CUploadListingItem* item = ListingItem[index];
            if (item->ItemType == ulitFile)
                item->ByteSize = UPLOADSIZE_NEEDUPDATE;
        }
        break;
    }

    case ulsInProgress:
    {
        CUploadListingChange* ch = NewUploadCacheObject<CUploadListingChange>(LatestChangeTime, ulctStoreFile, name);
        if (ch != NULL && ch->IsGood())
            AddChange(ch);
        else
        {
            if (ch != NULL)
                delete ch;
            else
                TRACE_E(LOW_MEMORY);
            *lowMem = TRUE;
        }
        break;
    }

        // case ulsInProgressButMayBeOutdated:   // nothing to do
        // case ulsNotAccessible:                // nothing to do
    }
}

void CUploadPathListing::ReportFileUploaded(const char* name, const CQuadWord& fileSize, BOOL* lowMem)
{
    LatestChangeTime = IncListingCounter(); // record the time of this change
    *lowMem = FALSE;
    int index;
    switch (ListingState)
    {
    case ulsReady:
    case ulsInProgressButObsolete:
    {
        if (FindItem(name, index, lowMem)) // if it is a file, set its new size
        {
            CUploadListingItem* item = ListingItem[index];
            if (item->ItemType == ulitFile)
                item->ByteSize = fileSize;
        }
        break;
    }

    case ulsInProgress:
    {
        CUploadListingChange* ch = NewUploadCacheObject<CUploadListingChange>(LatestChangeTime, ulctFileUploaded, name, &fileSize);
        if (ch != NULL && ch->IsGood())
            AddChange(ch);
        else
        {
            if (ch != NULL)
                delete ch;
            else
                TRACE_E(LOW_MEMORY);
            *lowMem = TRUE;
        }
        break;
    }

        // case ulsInProgressButMayBeOutdated:   // nothing to do
        // case ulsNotAccessible:                // nothing to do
    }
}

BOOL CUploadPathListing::CommitChange(CUploadListingChange* change)
{
    int index;
    BOOL lowMemory;
    switch (change->Type)
    {
    case ulctDelete:
    {
        if (FindItem(change->Name.c_str(), index, &lowMemory))
        {
            CUploadListingItem* item = ListingItem[index];
            delete item;
            ListingItem.Detach(index);
            if (!ListingItem.IsGood())
                ListingItem.ResetState();
        }
        else if (lowMemory)
            return FALSE;
        else
            TRACE_I("CUploadPathListing::CommitChange(): delete for unknown name reported: " << change->Name); // warning only
        return TRUE;
    }

    case ulctCreateDir:
    {
        if (!FindItem(change->Name.c_str(), index, &lowMemory)) // the name is free; it is possible to create the directory
        {
            if (lowMemory)
                return FALSE;
            return InsertNewItem(index, ulitDirectory, change->Name.c_str(), UPLOADSIZE_UNKNOWN);
        }
        return TRUE;
    }

    case ulctStoreFile:
    {
        if (!FindItem(change->Name.c_str(), index, &lowMemory)) // the name is free; create the file
        {
            if (lowMemory)
                return FALSE;
            return InsertNewItem(index, ulitFile, change->Name.c_str(), UPLOADSIZE_NEEDUPDATE);
        }
        else // the name already exists; if it is a file, invalidate its size (a link has no size)
        {
            CUploadListingItem* item = ListingItem[index];
            if (item->ItemType == ulitFile)
                item->ByteSize = UPLOADSIZE_NEEDUPDATE;
        }
        return TRUE;
    }

    case ulctFileUploaded:
    {
        if (FindItem(change->Name.c_str(), index, &lowMemory)) // if it is a file, set its new size
        {
            CUploadListingItem* item = ListingItem[index];
            if (item->ItemType == ulitFile)
                item->ByteSize = change->FileSize;
        }
        else if (lowMemory)
            return FALSE;
        return TRUE;
    }

    default:
        TRACE_E("CUploadPathListing::CommitChange(): unknown type of change!");
        break;
    }
    return FALSE;
}

void CUploadPathListing::AddChange(CUploadListingChange* ch)
{
    if (LastChange != NULL)
    {
        LastChange->NextChange = ch;
        LastChange = ch;
    }
    else
        FirstChange = LastChange = ch;
}

BOOL CUploadPathListing::AddWaitingWorker(int workerMsg, int workerUID)
{
    CUploadWaitingWorker* worker = new CUploadWaitingWorker;
    if (worker == NULL)
    {
        TRACE_E(LOW_MEMORY);
        return FALSE;
    }
    worker->NextWorker = FirstWaitingWorker;
    worker->WorkerMsg = workerMsg;
    worker->WorkerUID = workerUID;
    FirstWaitingWorker = worker;
    return TRUE;
}

void CUploadPathListing::InformWaitingWorkers(CUploadWaitingWorker** uploadFirstWaitingWorker)
{
    if (uploadFirstWaitingWorker != NULL) // should add the waiting workers to the list
    {
        if (FirstWaitingWorker != NULL)
        {
            if (*uploadFirstWaitingWorker != NULL)
            {
                CUploadWaitingWorker* worker = FirstWaitingWorker;
                while (worker->NextWorker != NULL)
                    worker = worker->NextWorker;
                worker->NextWorker = *uploadFirstWaitingWorker;
            }
            *uploadFirstWaitingWorker = FirstWaitingWorker;
        }
    }
    else // we need to post WORKER_TGTPATHLISTINGFINISHED to the waiting workers
    {
        CUploadWaitingWorker* worker = FirstWaitingWorker;
        while (worker != NULL)
        {
            // because we are already in CSocketsThread::CritSect, this call is possible (no deadlock risk)
            SocketsThread->PostSocketMessage(worker->WorkerMsg, worker->WorkerUID, WORKER_TGTPATHLISTINGFINISHED, NULL);

            CUploadWaitingWorker* del = worker;
            worker = worker->NextWorker;
            delete del;
        }
    }
    FirstWaitingWorker = NULL;
}

//
// ****************************************************************************
// CUploadListingChange
//

CUploadListingChange::CUploadListingChange(DWORD changeTime, CUploadListingChangeType type,
                                           const char* name, const CQuadWord* fileSize)
{
    Valid = name != NULL && FtpStoreProtocolBytes(name, Name);
    if (fileSize != NULL)
        FileSize = *fileSize;
    else
        FileSize = UPLOADSIZE_UNKNOWN;
    Type = type;
    ChangeTime = changeTime;
    NextChange = NULL;
}

//
// ****************************************************************************
// CFTPOpenedFiles
//

BOOL CFTPOpenedFile::Set(const CFtpTextCodec& codec, int myUID, const wchar_t* user, const wchar_t* host,
                         unsigned short port, const char* path, CFTPServerPathType pathType,
                         const char* name, CFTPFileAccessType accessType) noexcept
{
    try
    {
        std::wstring stagedUser;
        std::wstring stagedHost;
        std::wstring stagedPath;
        std::wstring stagedName;
        const char* pathBytes = path != NULL ? path : "";
        const char* nameBytes = name != NULL ? name : "";
        if (!FtpStoreWideText(user != NULL ? user : L"", stagedUser) ||
            !FtpStoreWideText(host != NULL ? host : L"", stagedHost) ||
            !codec.Decode(pathBytes, strlen(pathBytes), stagedPath) ||
            !codec.Decode(nameBytes, strlen(nameBytes), stagedName))
            return FALSE;

        User.swap(stagedUser);
        Host.swap(stagedHost);
        Path.swap(stagedPath);
        Name.swap(stagedName);
        UID = myUID;
        AccessType = accessType;
        Port = port;
        PathType = pathType;
        return TRUE;
    }
    catch (...)
    {
        return FALSE;
    }
}

BOOL CFTPOpenedFile::IsSameFile(const CFtpTextCodec& codec, const wchar_t* user, const wchar_t* host,
                                unsigned short port, const char* path,
                                CFTPServerPathType pathType, const char* name) noexcept
{
    try
    {
        std::wstring otherUser;
        std::wstring otherHost;
        std::wstring otherPath;
        std::wstring otherName;
        const char* pathBytes = path != NULL ? path : "";
        const char* nameBytes = name != NULL ? name : "";
        if (!FtpStoreWideText(user != NULL ? user : L"", otherUser) ||
            !FtpStoreWideText(host != NULL ? host : L"", otherHost) ||
            !codec.Decode(pathBytes, strlen(pathBytes), otherPath) ||
            !codec.Decode(nameBytes, strlen(nameBytes), otherName))
            return FALSE;

        return User == otherUser &&
               CompareStringOrdinal(Host.c_str(), -1, otherHost.c_str(), -1, TRUE) == CSTR_EQUAL &&
               Port == port &&
               FTPIsPrefixOfServerPathW(pathType, Path.c_str(), otherPath.c_str(), TRUE) &&
               (FTPIsCaseSensitive(pathType) ? Name == otherName :
                                                CompareStringOrdinal(Name.c_str(), -1, otherName.c_str(), -1, TRUE) == CSTR_EQUAL);
    }
    catch (...)
    {
        return FALSE;
    }
}

BOOL CFTPOpenedFile::IsInConflictWith(CFTPFileAccessType accessType)
{
    return AccessType != accessType || // two different operations on a single file are not possible
           accessType == ffatWrite;    // two uploads of one file are not possible
                                       // two downloads at the same time do no harm,
                                       // two deletions do not matter either - the second ends with a "not found" error (the outcome is certain, independent of the server implementation)
                                       // two renames do not matter either and, moreover, cannot happen (Quick Rename can run only one at a time), but renaming to a name that differs only in letter casing is possible (the same source and target of the rename are two names in the array)
}

CFTPOpenedFiles::CFTPOpenedFiles() : OpenedFiles(50, 100), AllocatedObjects(50, 100)
{
    HANDLES(InitializeCriticalSection(&FTPOpenedFilesCritSect));
    NextOpenedFileUID = 1;
}

CFTPOpenedFiles::~CFTPOpenedFiles()
{
    HANDLES(DeleteCriticalSection(&FTPOpenedFilesCritSect));
    if (OpenedFiles.Count > 0)
        TRACE_E("CFTPOpenedFiles::~CFTPOpenedFiles(): unexpected situation: OpenedFiles.Count > 0!");
}

BOOL CFTPOpenedFiles::OpenFile(const CFtpTextCodec& codec, const wchar_t* user, const wchar_t* host, unsigned short port,
                               const char* path, CFTPServerPathType pathType,
                               const char* name, int* newUID, CFTPFileAccessType accessType)
{
    BOOL ret = TRUE;
    HANDLES(EnterCriticalSection(&FTPOpenedFilesCritSect));
    int i;
    for (i = 0; i < OpenedFiles.Count; i++)
    {
        CFTPOpenedFile* file = OpenedFiles[i];
        if (file->IsSameFile(codec, user, host, port, path, pathType, name) &&
            file->IsInConflictWith(accessType))
        {
            ret = FALSE;
            break;
        }
    }
    if (ret)
    {
        int uid = NextOpenedFileUID;
        CFTPOpenedFile* n;
        if (AllocatedObjects.Count > 0) // if we have something preallocated, reuse it
        {
            n = AllocatedObjects[AllocatedObjects.Count - 1];
            if (n->Set(codec, uid, user, host, port, path, pathType, name, accessType))
            {
                AllocatedObjects.Detach(AllocatedObjects.Count - 1);
                if (!AllocatedObjects.IsGood())
                    AllocatedObjects.ResetState(); // detaching cannot fail
            }
            else
                n = NULL;
        }
        else
        {
            try
            {
                n = new CFTPOpenedFile;
            }
            catch (...)
            {
                n = NULL;
            }
            if (n != NULL && !n->Set(codec, uid, user, host, port, path, pathType, name, accessType))
            {
                delete n;
                n = NULL;
            }
        }
        if (n != NULL)
        {
            OpenedFiles.Add(n);
            if (OpenedFiles.IsGood())
            {
                *newUID = uid;
                NextOpenedFileUID++;
            }
            else
            {
                OpenedFiles.ResetState();
                ret = FALSE;
                delete n;
            }
        }
        else
        {
            TRACE_E(LOW_MEMORY);
            ret = FALSE;
        }
    }
    HANDLES(LeaveCriticalSection(&FTPOpenedFilesCritSect));
    return ret;
}

void CFTPOpenedFiles::CloseFile(int UID)
{
    HANDLES(EnterCriticalSection(&FTPOpenedFilesCritSect));
    BOOL found = FALSE;
    int i;
    for (i = 0; i < OpenedFiles.Count; i++)
    {
        if (OpenedFiles[i]->IsUID(UID))
        {
            AllocatedObjects.Add(OpenedFiles[i]);
            if (AllocatedObjects.IsGood())
                OpenedFiles.Detach(i);
            else
            {
                AllocatedObjects.ResetState();
                OpenedFiles.Delete(i);
            }
            if (!OpenedFiles.IsGood())
                OpenedFiles.ResetState(); // detach or delete cannot fail; only the array compaction did not succeed
            found = TRUE;
            break;
        }
    }
    if (!found)
        TRACE_E("CFTPOpenedFiles::CloseFile(): unable to find file with UID: " << UID);
    HANDLES(LeaveCriticalSection(&FTPOpenedFilesCritSect));
}
