// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#include "ShellProfileSettings.h"
#include <commctrl.h>
#include <cwchar>

namespace Sally::ShellProfiles
{
namespace
{
std::wstring ValueName(size_t slot, const wchar_t* suffix)
{
    return L"Command Shell Shortcut " + std::to_wstring(slot + 1) + suffix;
}
}

Shortcuts EmptyShortcuts()
{
    Shortcuts result;
    for (auto& target : result)
        target.kind = ShellTargetKind::Unassigned;
    return result;
}

bool SameTarget(const ShellTarget& left, const ShellTarget& right)
{
    if (left.kind != right.kind)
        return false;
    if (left.kind != ShellTargetKind::WindowsTerminalProfile)
        return true;
    if (!left.profileGuid.empty() || !right.profileGuid.empty())
        return !left.profileGuid.empty() && !right.profileGuid.empty() &&
               _wcsicmp(left.profileGuid.c_str(), right.profileGuid.c_str()) == 0;
    return _wcsicmp(left.profileName.c_str(), right.profileName.c_str()) == 0;
}

bool Available(const WindowsTerminalCatalog& catalog, const ShellTarget& target)
{
    if (target.kind == ShellTargetKind::ComSpec || target.kind == ShellTargetKind::Unassigned)
        return true;
    ShellTarget resolved;
    return catalog.installed && ResolveWindowsTerminalTarget(catalog, target, resolved);
}

int ShortcutForKey(unsigned key, bool control, bool shift, bool alt)
{
    if (key != VK_DIVIDE || static_cast<int>(control) + static_cast<int>(shift) + static_cast<int>(alt) != 1)
        return -1;
    return control ? 0 : shift ? 1 : 2;
}

WORD ShortcutHotKey(size_t slot, bool extended)
{
    if (slot >= 3)
        return 0;
    BYTE modifiers = slot == 0 ? HOTKEYF_CONTROL : slot == 1 ? HOTKEYF_SHIFT : HOTKEYF_ALT;
    if (extended)
        modifiers |= HOTKEYF_EXT;
    return MAKEWORD(VK_DIVIDE, modifiers);
}

Shortcuts LoadShortcuts(IRegistry* registry, HKEY key)
{
    Shortcuts result = EmptyShortcuts();
    if (registry == nullptr)
        return result;
    for (size_t slot = 0; slot < result.size(); ++slot)
    {
        DWORD kind = static_cast<DWORD>(ShellTargetKind::Unassigned);
        if (!registry->GetDWord(key, ValueName(slot, L" Kind").c_str(), kind).success || kind > 2)
            continue;
        ShellTarget target;
        target.kind = static_cast<ShellTargetKind>(kind);
        if (target.kind == ShellTargetKind::WindowsTerminalProfile)
        {
            const auto guid = registry->GetString(key, ValueName(slot, L" GUID").c_str(), target.profileGuid);
            const auto name = registry->GetString(key, ValueName(slot, L" Name").c_str(), target.profileName);
            // A genuinely absent GUID permits a name-only dynamic profile. A
            // malformed/unreadable identity must never degrade to name matching.
            if ((!guid.success && guid.errorCode != ERROR_FILE_NOT_FOUND) ||
                (!name.success && name.errorCode != ERROR_FILE_NOT_FOUND) ||
                (target.profileGuid.empty() && target.profileName.empty()))
                continue;
        }
        result[slot] = std::move(target);
    }
    return result;
}

RegistryResult SaveShortcuts(IRegistry* registry, HKEY key, const Shortcuts& shortcuts)
{
    if (registry == nullptr)
        return RegistryResult::Error(ERROR_INVALID_HANDLE);
    for (size_t slot = 0; slot < shortcuts.size(); ++slot)
    {
        const auto& target = shortcuts[slot];
        auto result = registry->SetDWord(key, ValueName(slot, L" Kind").c_str(), static_cast<DWORD>(target.kind));
        if (!result.success)
            return result;
        result = registry->SetString(key, ValueName(slot, L" GUID").c_str(), target.profileGuid.c_str());
        if (!result.success)
            return result;
        result = registry->SetString(key, ValueName(slot, L" Name").c_str(), target.profileName.c_str());
        if (!result.success)
            return result;
    }
    return RegistryResult::Ok();
}
}
