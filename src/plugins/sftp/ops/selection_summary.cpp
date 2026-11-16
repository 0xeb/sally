#include "selection_summary.h"

CSelectionSummary SummarizeSelection(const std::vector<CSummaryItem>& items)
{
    CSelectionSummary summary;
    std::vector<uint32_t> modes;
    bool allHaveMode = true;
    for (const CSummaryItem& item : items)
    {
        if (item.isDir)
            summary.dirs++;
        else
        {
            summary.files++;
            // a sum that would overflow is capped instead of wrapping around
            summary.totalSize = item.size > UINT64_MAX - summary.totalSize ? UINT64_MAX : summary.totalSize + item.size;
        }
        if (item.isLink)
            summary.links++;
        if (item.isLink)
            continue; // the listing gives the mode of the link itself (always rwxrwxrwx), not of what it points to
        if (item.mode == 0)
            allHaveMode = false;
        modes.push_back(PermissionBitsOf(item.mode));
    }
    summary.modeKnown = allHaveMode && !modes.empty();
    if (allHaveMode)
        summary.modeState = StateOfModes(modes);
    return summary;
}
