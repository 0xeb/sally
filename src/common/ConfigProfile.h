// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

// -profile <name>: Sally keeps its configuration in a separate registry key,
//   HKCU\Software\Sally\Profiles\<name>\1.0
// instead of HKCU\Software\Sally\1.0. A new name starts with Sally's defaults; the same name again
// finds the same configuration. The usual configuration is not read, changed or offered for import,
// so a profile is a clean, disposable Sally (to try a setting, a new release, or a fresh start)
// beside the one in daily use. Delete the key to remove a profile.
//
// The switch is applied before anything reads the configuration: Sally parses its command line
// (common/CommandLineParser.h) for it first, then again at the usual place.

#pragma once

#include <string>
#include <wchar.h>

namespace sally::profile
{

// 1 to 64 letters, digits, '.', '_' or '-'; not "." or "..".
inline bool IsValidProfileName(const std::wstring& name)
{
    if (name.empty() || name.size() > 64 || name == L"." || name == L"..")
        return false;
    for (wchar_t ch : name)
    {
        const bool ok = (ch >= L'a' && ch <= L'z') || (ch >= L'A' && ch <= L'Z') || (ch >= L'0' && ch <= L'9') ||
                        ch == L'.' || ch == L'_' || ch == L'-';
        if (!ok)
            return false;
    }
    return true;
}

// currentRoot is the usual root ("Software\Sally\1.0", or "... 1.0 Debug" in a debug build): the
// profile lives beside it under Profiles, with the same last part.
inline std::wstring ProfileRegistryRoot(const std::wstring& currentRoot, const std::wstring& name)
{
    const size_t slash = currentRoot.find_last_of(L'\\');
    if (slash == std::wstring::npos)
        return L"Profiles\\" + name + L"\\" + currentRoot;
    return currentRoot.substr(0, slash) + L"\\Profiles\\" + name + L"\\" + currentRoot.substr(slash + 1);
}

} // namespace sally::profile
