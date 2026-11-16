// What the properties dialog shows for several selected items: how many of each kind, the sum of the file sizes, and
// the permissions all of them share. Pure functions (no Sally, no network), so they are unit tested.
#pragma once

#include <cstdint>
#include <vector>

#include "file_mode.h"

// One selected item as the listing knows it.
struct CSummaryItem
{
    bool isDir = false;  // a directory, or a link to one
    bool isLink = false; // a symbolic link (also counted in files or dirs)
    uint64_t size = 0;
    uint32_t mode = 0; // st_mode bits; 0 = the server reported none
};

struct CSelectionSummary
{
    int files = 0;
    int dirs = 0;
    int links = 0;          // subset of files + dirs
    uint64_t totalSize = 0; // of the files only (a directory's own size is meaningless; nothing is walked into)
    // false when some item has no mode, or all are links (their own mode is not what chmod changes): not shown then.
    // Links are left out of the permissions otherwise.
    bool modeKnown = false;
    CModeState modeState;   // bits set in all items / mixed
};

CSelectionSummary SummarizeSelection(const std::vector<CSummaryItem>& items);
