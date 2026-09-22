// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later
//
// The two decisions the delete loop makes before it destroys anything.
//
// The delete loop itself cannot be extracted usefully - it is interleaved with a progress dialog,
// a cancel check, and a retry/skip/skip-all prompt per item, none of which is algorithm. But two
// decisions inside it are pure, and both are consequential:
//
//   1. IS THIS A NON-EMPTY TOP-LEVEL DIRECTORY? If it is, the user gets "this directory is not
//      empty, delete anyway?". Answer wrongly in one direction and the prompt never appears, so a
//      directory full of files is deleted silently on a single confirmation. Answer wrongly in the
//      other and the user is asked about every empty directory.
//
//   2. WHERE DOES THE BLOCK END? When the user answers no, the loop must skip the whole block -
//      the directory AND everything the walk listed beneath it. Miscount that and it either
//      deletes some of the contents it was just told to leave alone, or skips past unrelated
//      items that were never asked about.
//
// Both are index arithmetic over the walk's output, with a look-ahead and a sentinel value - the
// shape that is easy to get subtly wrong and impossible to notice, because the failure is a
// missing prompt rather than an error. Neither needs a device, so both are pinned here.
//
// The walk emits items grouped into BLOCKS: one block per top-level item the user selected, with
// the directory itself last in its block (see wmobile_treewalk_core's directoriesFirst == false
// ordering, which delete relies on). A block holding more than one item therefore means a
// directory with contents.

#pragma once

#include <cstddef>

namespace wmobile
{

// Just the fields these two decisions read. Mirrors CFileInfo.
struct DeleteCandidate
{
    int block = 0;
    bool isDirectory = false;
};

// True when 'index' begins a block that holds more than one item - i.e. a top-level directory the
// walk found contents beneath. 'previousBlock' is the block id of the item before it, which is how
// the loop recognises a block boundary.
bool StartsNonEmptyDirectoryBlock(const DeleteCandidate* items,
                                  size_t count,
                                  size_t index,
                                  int previousBlock);

// Index of the LAST item in the block starting at 'index' - the directory itself. The loop jumps
// here to skip a block the user declined. Returns 'index' when the block holds one item.
size_t LastIndexOfBlock(const DeleteCandidate* items, size_t count, size_t index);

} // namespace wmobile
