// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#include "wmobile_treewalk_core.h"

#include "wmobile_path_core.h"

namespace wmobile
{

namespace
{

// "." and ".." - Windows Mobile does not report them, but the original code guarded anyway and so
// does this: a device that did report them would otherwise send the walk into an infinite loop.
bool IsSelfOrParent(const std::wstring& name)
{
    return name == L"." || name == L"..";
}

std::wstring Join(const std::wstring& base, const std::wstring& leaf)
{
    if (base.empty())
        return leaf;
    if (base.back() == L'\\')
        return base + leaf;
    return base + L'\\' + leaf;
}

bool WalkInto(DeviceEnumerator& device,
              const std::wstring& root,
              const std::wstring& relative,
              const wchar_t* pattern,
              const WalkOptions& options,
              int depth,
              std::vector<FoundItem>& found)
{
    if (depth > options.maxDepth)
        return false;

    const std::wstring searchPath = Join(Join(root, relative), pattern);
    if (options.maxPathChars != 0 && searchPath.size() >= options.maxPathChars)
        return false;

    std::vector<DeviceEntry> entries;
    if (!device.Enumerate(searchPath.c_str(), entries))
        return false;

    for (const DeviceEntry& entry : entries)
    {
        if (entry.name.empty() || IsSelfOrParent(entry.name))
            continue;

        FoundItem item;
        item.relativePath = Join(relative, entry.name);
        item.isDirectory = entry.isDirectory;
        item.sizeLow = entry.isDirectory ? 0 : entry.sizeLow;
        item.attributes = entry.attributes;

        if (options.maxPathChars != 0 && Join(root, item.relativePath).size() >= options.maxPathChars)
            return false;

        if (!entry.isDirectory)
        {
            found.push_back(item);
            continue;
        }

        // A directory is recorded either side of its contents depending on what the caller is
        // about to do with the list - see WalkOptions::directoriesFirst. Under
        // directoriesFirst it is recorded on BOTH sides: the pre-order entry is the
        // "create the target first" marker, the post-order one is what tells a Move
        // to remove the source directory after its contents are gone.
        if (options.directoriesFirst)
        {
            FoundItem marker = item;
            marker.isPreOrderMarker = true;
            found.push_back(marker);
        }

        // Always "*" below the top level: the caller's pattern selects what to START from, not
        // what to keep once inside a matched directory. Copying "*.jpg" must still copy a matched
        // directory whole.
        if (!WalkInto(device, root, item.relativePath, L"*", options, depth + 1, found))
            return false;

        found.push_back(item);
    }

    return true;
}

} // namespace

bool WalkDeviceTree(DeviceEnumerator& device,
                    const wchar_t* root,
                    const wchar_t* pattern,
                    const WalkOptions& options,
                    std::vector<FoundItem>& found)
{
    if (root == nullptr || pattern == nullptr)
        return false;
    return WalkInto(device, root, std::wstring(), pattern, options, 0, found);
}

} // namespace wmobile
