// Unix permission bits for the "change attributes" (chmod) feature: octal text, the rwx text, and how a change is
// applied to a mode. Pure functions (no Sally, no network), so they are unit tested.
//
// Only the 12 permission bits (07777) are ever handled: rwx for owner/group/others (0777) plus setuid (04000), setgid
// (02000) and sticky (01000). The file type bits of a stat mode (directory, link, ...) are cut off and never sent.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

constexpr uint32_t kModeRwx = 0777;      // the nine rwx bits
constexpr uint32_t kModeSpecial = 07000; // setuid, setgid, sticky
constexpr uint32_t kModeAll = 07777;

// The permission bits of a stat mode (the file type is cut off).
inline uint32_t PermissionBitsOf(uint32_t statMode)
{
    return statMode & kModeAll;
}

// "0755": always four octal digits (the first one is the special bits). Bits above 07777 are ignored.
std::wstring FormatOctal(uint32_t mode);

// Parses what the user typed: 1 to 4 octal digits (0-7), nothing else (no spaces, no sign). A short text is a smaller
// number, so "644" is 0644. False when the text is not valid; 'mode' is then not changed.
bool ParseOctal(const std::wstring& text, uint32_t& mode);

// The text of ls -l without the file type: 9 characters like "rwxr-xr-x"; setuid/setgid replace the owner/group x with
// s (S when x is not set), sticky replaces the others x with t (T). A bit in 'mixed' is shown as '?'.
std::wstring FormatSymbolic(uint32_t mode, uint32_t mixed = 0);

// What a selection of several items looks like in the dialog: for every bit either "set in all", "clear in all" or
// "mixed" (set in some items only).
struct CModeState
{
    uint32_t checked = 0; // bits that are set (in all items, or the user checked them); never overlaps 'mixed'
    uint32_t mixed = 0;   // bits the items disagree about (the user has not decided them): left alone when applied
};

// The state for the current modes of the selected items. No items: nothing is set.
CModeState StateOfModes(const std::vector<uint32_t>& modes);

// The state after the user typed an octal number: every bit is decided.
CModeState StateOfMode(uint32_t mode);

// The octal field shows this: the set bits (a mixed bit counts as not set).
inline uint32_t ShownMode(const CModeState& state)
{
    return state.checked & ~state.mixed & kModeAll;
}

// What to do to each item's mode: bits to switch on and bits to switch off. A bit in neither is left as the item has it.
struct CModeChange
{
    uint32_t setBits = 0;
    uint32_t clearBits = 0;

    bool IsEmpty() const { return setBits == 0 && clearBits == 0; }
};

// The decided bits of the state become the change; the mixed ones stay out of it.
CModeChange ChangeOfState(const CModeState& state);

// (mode | setBits) & ~clearBits, permission bits only.
uint32_t ApplyModeChange(uint32_t mode, const CModeChange& change);

// The mode of one item, which the server may not have reported (permissions == 0 alone cannot tell).
struct CItemMode
{
    uint32_t mode = 0; // only meaningful when 'known'
    bool known = false;
};

// Like StateOfModes, but one unknown mode makes every bit "mixed": nothing is claimed about the items.
CModeState StateOfItemModes(const std::vector<CItemMode>& items);

// The mode an item gets. A known mode: ApplyModeChange. An unknown mode can only be set when the change decides all 12
// bits (then the old mode does not matter); false otherwise, 'result' is not changed.
bool ApplyToItemMode(const CItemMode& item, const CModeChange& change, uint32_t& result);
