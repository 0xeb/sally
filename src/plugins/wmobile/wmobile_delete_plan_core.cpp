// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#include "wmobile_delete_plan_core.h"

namespace wmobile
{

bool StartsNonEmptyDirectoryBlock(const DeleteCandidate* items,
                                  size_t count,
                                  size_t index,
                                  int previousBlock)
{
    if (items == nullptr || index + 1 >= count)
        return false; // the last item cannot have contents listed after it

    // A new block whose successor shares its id holds more than one item.
    return items[index].block != previousBlock &&
           items[index].block == items[index + 1].block;
}

size_t LastIndexOfBlock(const DeleteCandidate* items, size_t count, size_t index)
{
    if (items == nullptr || index >= count)
        return index;

    const int block = items[index].block;
    size_t last = index;
    while (last + 1 < count && items[last + 1].block == block)
        last++;
    return last;
}

} // namespace wmobile
