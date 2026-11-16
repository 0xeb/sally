#include "file_mode.h"

std::wstring FormatOctal(uint32_t mode)
{
    mode &= kModeAll;
    std::wstring text(4, L'0');
    for (int i = 3; i >= 0; --i)
    {
        text[static_cast<size_t>(i)] = static_cast<wchar_t>(L'0' + (mode & 7));
        mode >>= 3;
    }
    return text;
}

bool ParseOctal(const std::wstring& text, uint32_t& mode)
{
    if (text.empty() || text.size() > 4)
        return false;
    uint32_t value = 0;
    for (wchar_t c : text)
    {
        if (c < L'0' || c > L'7')
            return false;
        value = value * 8 + static_cast<uint32_t>(c - L'0');
    }
    mode = value;
    return true;
}

namespace
{
// One position of the text. 'special' is the setuid/setgid/sticky bit that shares the x position.
wchar_t SymbolicChar(uint32_t mode, uint32_t mixed, uint32_t bit, wchar_t on, uint32_t special = 0, wchar_t specialOn = L's')
{
    if ((mixed & bit) != 0 || (special != 0 && (mixed & special) != 0))
        return L'?';
    const bool set = (mode & bit) != 0;
    if (special != 0 && (mode & special) != 0)
        return set ? specialOn : static_cast<wchar_t>(specialOn - L'a' + L'A'); // s/S, t/T
    return set ? on : L'-';
}
} // namespace

std::wstring FormatSymbolic(uint32_t mode, uint32_t mixed)
{
    std::wstring text;
    text += SymbolicChar(mode, mixed, 0400, L'r');
    text += SymbolicChar(mode, mixed, 0200, L'w');
    text += SymbolicChar(mode, mixed, 0100, L'x', 04000);
    text += SymbolicChar(mode, mixed, 0040, L'r');
    text += SymbolicChar(mode, mixed, 0020, L'w');
    text += SymbolicChar(mode, mixed, 0010, L'x', 02000);
    text += SymbolicChar(mode, mixed, 0004, L'r');
    text += SymbolicChar(mode, mixed, 0002, L'w');
    text += SymbolicChar(mode, mixed, 0001, L'x', 01000, L't');
    return text;
}

CModeState StateOfModes(const std::vector<uint32_t>& modes)
{
    CModeState state;
    if (modes.empty())
        return state;
    uint32_t all = kModeAll; // set in every item
    uint32_t any = 0;        // set in at least one item
    for (uint32_t mode : modes)
    {
        all &= mode;
        any |= mode;
    }
    state.checked = all & kModeAll;
    state.mixed = (any & ~all) & kModeAll;
    return state;
}

CModeState StateOfMode(uint32_t mode)
{
    CModeState state;
    state.checked = mode & kModeAll;
    return state;
}

CModeChange ChangeOfState(const CModeState& state)
{
    CModeChange change;
    change.setBits = state.checked & ~state.mixed & kModeAll;
    change.clearBits = ~state.checked & ~state.mixed & kModeAll;
    return change;
}

uint32_t ApplyModeChange(uint32_t mode, const CModeChange& change)
{
    return ((mode | change.setBits) & ~change.clearBits) & kModeAll;
}

CModeState StateOfItemModes(const std::vector<CItemMode>& items)
{
    std::vector<uint32_t> modes;
    for (const CItemMode& item : items)
    {
        if (!item.known)
        {
            CModeState unknown;
            unknown.mixed = kModeAll;
            return unknown;
        }
        modes.push_back(PermissionBitsOf(item.mode));
    }
    return StateOfModes(modes);
}

bool ApplyToItemMode(const CItemMode& item, const CModeChange& change, uint32_t& result)
{
    if (item.known)
    {
        result = ApplyModeChange(PermissionBitsOf(item.mode), change);
        return true;
    }
    if (((change.setBits | change.clearBits) & kModeAll) != kModeAll)
        return false;
    result = ApplyModeChange(0, change);
    return true;
}
