#include "explored_dirs.h"

bool CExploredDirs::Insert(const std::string& realPath)
{
    std::lock_guard<std::mutex> lock(mutex_);
    return paths_.insert(realPath).second;
}

bool CExploredDirs::Contains(const std::string& realPath) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return paths_.count(realPath) != 0;
}
