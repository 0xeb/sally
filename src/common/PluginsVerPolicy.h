// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <string>
#include <wchar.h>
#include <wctype.h>

// plugins\plugins.ver lists the plugins that ship with Sally, so a plugin added in a newer release
// reaches existing users too. Its first line is a number, and so is each plugin line: a profile
// remembers the highest number it has processed and takes only the lines above it.
//
// The number is derived from the release that built the file, so every release offers its
// plugins again; one already registered is skipped. A plugin the user removed in the Plugins
// Manager is recorded and not offered again, until the user adds it back.
namespace sally::plugins_ver
{

// 1.0.36 -> 10036. Must match sal_generate_plugins_ver() in cmake/sal_install.cmake.
inline int NumberForVersion(int major, int minorA, int minorB)
{
    return major * 10000 + minorA * 100 + minorB;
}

// The removed-plugins list: DLL names relative to the plugins folder, separated by ';',
// compared without regard to case.
inline bool ListContains(const std::wstring& list, const std::wstring& name)
{
    if (name.empty())
        return false;
    size_t start = 0;
    while (start <= list.size())
    {
        size_t end = list.find(L';', start);
        if (end == std::wstring::npos)
            end = list.size();
        if (end - start == name.size() && _wcsnicmp(list.c_str() + start, name.c_str(), name.size()) == 0)
            return true;
        start = end + 1;
    }
    return false;
}

inline std::wstring ListWith(const std::wstring& list, const std::wstring& name)
{
    if (name.empty() || name.find(L';') != std::wstring::npos || ListContains(list, name))
        return list;
    return list.empty() ? name : list + L";" + name;
}

inline std::wstring ListWithout(const std::wstring& list, const std::wstring& name)
{
    std::wstring kept;
    size_t start = 0;
    while (start <= list.size())
    {
        size_t end = list.find(L';', start);
        if (end == std::wstring::npos)
            end = list.size();
        const std::wstring item = list.substr(start, end - start);
        if (!item.empty() && !(item.size() == name.size() && _wcsicmp(item.c_str(), name.c_str()) == 0))
            kept = kept.empty() ? item : kept + L";" + item;
        start = end + 1;
    }
    return kept;
}

// Plugins a release retired: never registered or loaded again, even when a copy is still on disk
// (an update installed by an older updater leaves it until Sally removes it, see
// common/update/UpdateFiles.h). 1.0.37: CheckVer, replaced by utils\salupdate.exe.
inline bool IsRetiredPluginDll(const std::wstring& name)
{
    std::wstring relative = name;
    for (wchar_t& ch : relative)
        ch = ch == L'/' ? L'\\' : static_cast<wchar_t>(towlower(ch));
    // Also a full path inside the plugins folder.
    const size_t plugins = relative.rfind(L"\\plugins\\");
    if (plugins != std::wstring::npos)
        relative = relative.substr(plugins + 9);
    return relative == L"checkver\\checkver.dll";
}

// Whether a plugin line of plugins.ver is offered to this profile.
inline bool ShouldOffer(int lineNumber, int lastProcessedNumber, const std::wstring& name,
                        const std::wstring& removedList)
{
    return lineNumber > lastProcessedNumber && !name.empty() && !ListContains(removedList, name);
}

} // namespace sally::plugins_ver
