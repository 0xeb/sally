// "Properties" dialog (Alt+Enter): one item (name, path, type, size, times, owner, group, permissions, link target) or
// several (counts, the sum of the file sizes, the permissions they share). Read-only; the FS passes the facts.
#pragma once

#include <cstdint>
#include <string>

#include "selection_summary.h"

struct CPropertiesInput
{
    int itemCount = 1;
    std::wstring location; // the folder that holds the item(s)

    // one item
    std::wstring name;
    bool isDir = false; // a directory, or a link to one
    bool isLink = false;
    uint64_t size = 0;
    int64_t mtime = 0; // Unix seconds, 0 = not known
    int64_t atime = 0;
    std::wstring owner; // numeric ids as the server reports them; empty = not reported
    std::wstring group;
    uint32_t mode = 0; // st_mode bits, 0 = not reported
    bool hasLinkTarget = false;
    std::wstring linkTarget;

    // several items
    CSelectionSummary summary;
};

// Modal; returns when closed. Never throws.
void ShowPropertiesDialog(HWND parent, const CPropertiesInput& input);
