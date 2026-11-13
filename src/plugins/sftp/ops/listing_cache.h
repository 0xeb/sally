// Directory listing cache of one panel connection (Sally-free). Used ONLY when the user comes back to a folder visited
// before (a non-forced ChangePath); Ctrl+R always reads the server and refreshes the entry.
//
// Main thread only: the file system object that owns it is used by Sally's main thread alone, so there is no lock.
// C#: a Dictionary<byte[], Entry> with a size limit, least recently used dropped first.
#pragma once

#include <chrono>
#include <list>
#include <memory>
#include <string>
#include <vector>

#include "ssh_types.h"

class CListingCache
{
public:
    using Clock = std::chrono::steady_clock;
    using ListingPtr = std::shared_ptr<const std::vector<CSftpEntry>>;

    static const size_t MaxFolders = 100;

    // The entries of 'path' (the raw, normalized remote path bytes) when they were read at most 'maxAgeSeconds' before
    // 'now'; an older entry is dropped. Null when there is none. A hit becomes the most recently used entry.
    ListingPtr Get(const std::string& path, int maxAgeSeconds, Clock::time_point now);

    // Stores (or replaces) the listing of 'path', read at 'now'. When the cache is full the least recently used folder
    // goes. A null listing is ignored.
    void Put(const std::string& path, ListingPtr entries, Clock::time_point now);

    void Invalidate(const std::string& path);

    // 'path' and everything below it: "/a" drops "/a" and "/a/b", never "/ab". "/" drops everything.
    void InvalidateTree(const std::string& path);

    void Clear();

    size_t Size() const { return items_.size(); }

private:
    struct CItem
    {
        std::string path;
        ListingPtr entries;
        Clock::time_point readAt;
    };
    using ItemList = std::list<CItem>;

    ItemList::iterator Find(const std::string& path);

    ItemList items_; // front = most recently used
};

// A listing request may be answered from the cache only when the cache is switched on and the user did not ask for a
// hard refresh (Ctrl+R).
bool MayUseListingCache(bool cacheEnabled, bool forceRefresh);
