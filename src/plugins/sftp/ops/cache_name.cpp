#include "cache_name.h"

std::wstring BuildCacheName(const std::wstring& fullFsPath, uint64_t size, int64_t mtime)
{
    return fullFsPath + L"|" + std::to_wstring(size) + L"|" + std::to_wstring(mtime);
}
