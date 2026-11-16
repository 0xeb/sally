// The symlink loop guard of an operation: the resolved (real) remote paths of the directories already explored.
// A link that points back to a directory above it resolves to a path that is already in the set.
// Known limit: two different links to the same directory in one selection also count as a repeat.
#pragma once

#include <mutex>
#include <set>
#include <string>

class CExploredDirs
{
public:
    // True when the path was new (now remembered); false when it was already there. Thread safe.
    bool Insert(const std::string& realPath);
    bool Contains(const std::string& realPath) const;

private:
    mutable std::mutex mutex_;
    std::set<std::string> paths_;
};
