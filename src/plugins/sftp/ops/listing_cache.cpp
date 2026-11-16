#include "listing_cache.h"

CListingCache::ItemList::iterator CListingCache::Find(const std::string& path)
{
    for (ItemList::iterator it = items_.begin(); it != items_.end(); ++it)
        if (it->path == path)
            return it;
    return items_.end();
}

CListingCache::ListingPtr CListingCache::Get(const std::string& path, int maxAgeSeconds, Clock::time_point now)
{
    const ItemList::iterator it = Find(path);
    if (it == items_.end())
        return nullptr;
    if (now - it->readAt > std::chrono::seconds(maxAgeSeconds))
    {
        items_.erase(it);
        return nullptr;
    }
    items_.splice(items_.begin(), items_, it); // most recently used
    return items_.front().entries;
}

void CListingCache::Put(const std::string& path, ListingPtr entries, Clock::time_point now)
{
    if (entries == nullptr)
        return;
    const ItemList::iterator it = Find(path);
    if (it != items_.end())
        items_.erase(it);
    CItem item;
    item.path = path;
    item.entries = std::move(entries);
    item.readAt = now;
    items_.push_front(std::move(item));
    while (items_.size() > MaxFolders)
        items_.pop_back();
}

void CListingCache::Invalidate(const std::string& path)
{
    const ItemList::iterator it = Find(path);
    if (it != items_.end())
        items_.erase(it);
}

void CListingCache::InvalidateTree(const std::string& path)
{
    std::string base = path;
    while (!base.empty() && base.back() == '/')
        base.pop_back();
    if (base.empty()) // the root: every absolute path is below it
    {
        items_.clear();
        return;
    }
    const std::string below = base + "/";
    for (ItemList::iterator it = items_.begin(); it != items_.end();)
    {
        if (it->path == base || it->path.compare(0, below.size(), below) == 0)
            it = items_.erase(it);
        else
            ++it;
    }
}

void CListingCache::Clear()
{
    items_.clear();
}

bool MayUseListingCache(bool cacheEnabled, bool forceRefresh)
{
    return cacheEnabled && !forceRefresh;
}
