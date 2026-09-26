// SPDX-FileCopyrightText: 2025-2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#ifndef SALLY_SALEXT_CLEANUP_STANDALONE
#include "precomp.h"
#endif

#include "salext_cleanup.h"

#include <cstring>
#include <cstdio>
#include <string>

namespace
{
// Every past Sally/Salamander build registered its shell extension under one CLSID in this
// family, so the sweep has to probe the whole printable-ASCII range of the low byte.
const unsigned kClsidLowFirst = 0x30;
const unsigned kClsidLowLast = 0x7f;

std::wstring ClsidKey(unsigned low, bool inprocServer)
{
    wchar_t buf[128];
    swprintf_s(buf, L"CLSID\\{c78b61%02x-f3ea-11d2-94a1-00e0292a01e3}%s", low,
               inprocServer ? L"\\InProcServer32" : L"");
    return buf;
}

const wchar_t* BaseName(const wchar_t* path)
{
    const wchar_t* base = path;
    for (const wchar_t* p = path; *p != L'\0'; ++p)
        if (*p == L'\\' || *p == L'/')
            base = p + 1;
    return base;
}
} // namespace

bool IsStaleSalextRegistration(const wchar_t* currentSalextPath, const wchar_t* registeredPath)
{
    if (registeredPath == nullptr || registeredPath[0] == L'\0')
        return false;

    // Only ever act on our own shell-extension DLL by exact basename — never a arbitrary CLSID.
    const wchar_t* base = BaseName(registeredPath);
    if (_wcsicmp(base, L"salextx64.dll") != 0 &&
        _wcsicmp(base, L"salextx86.dll") != 0 &&
        _wcsicmp(base, L"salextARM64.dll") != 0)
        return false;

    // The current install's own registration is not stale.
    if (currentSalextPath != nullptr && _wcsicmp(currentSalextPath, registeredPath) == 0)
        return false;

    return true;
}

SalextCleanupStats ReclaimStaleSalextRegistrations(const wchar_t* currentSalextPath,
                                                   IRegistry* registry,
                                                   IFileSystem* fileSystem)
{
    SalextCleanupStats stats;
    if (registry == nullptr || fileSystem == nullptr)
        return stats;

    for (unsigned low = kClsidLowFirst; low <= kClsidLowLast; low++)
    {
        HKEY key = nullptr;
        if (!registry->OpenKeyRead(HKEY_CLASSES_ROOT, ClsidKey(low, true).c_str(), key).success)
            continue;

        // The DLL path is the key's default value.
        std::wstring registeredPath;
        RegistryResult read = registry->GetString(key, nullptr, registeredPath);
        registry->CloseKey(key);
        if (!read.success)
            continue;

        if (!IsStaleSalextRegistration(currentSalextPath, registeredPath.c_str()))
            continue;
        if (!fileSystem->FileExists(registeredPath.c_str()))
            continue; // old DLL already gone

        stats.stale++;
        if (fileSystem->ScheduleDeleteOnReboot(registeredPath.c_str()).success)
            stats.scheduled++;

        if (registry->DeleteKeyRecursive(HKEY_CLASSES_ROOT, ClsidKey(low, false).c_str()).success)
            stats.keysRemoved++;
    }
    return stats;
}
