// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "common/DiagnosticTextEncoding.h"
#include <array>
#include <string>

namespace sally::diagnostic
{

// Prepare owned metadata before calling untrusted plugin code. The crash-report worker reads
// these complete byte lines from the suspended faulting thread without allocating or querying
// plugin objects that may be only partly initialized. This is context, not attribution of blame.
struct PluginLoadDiagnostic
{
    std::wstring Dll;
    std::wstring Version;
    int Sdk;
    std::array<std::string, 4> ReportLines;

    PluginLoadDiagnostic(const std::wstring& dll, const std::wstring& version, int sdk)
        : Dll(dll.substr(dll.find_last_of(L"\\/") == std::wstring::npos ? 0 : dll.find_last_of(L"\\/") + 1)),
          Version(version), Sdk(sdk)
    {
        // Do not let plugin-supplied text introduce report headings or control sequences.
        for (wchar_t& c : Dll)
        {
            if (c < L' ' || c == 0x7f)
                c = L'?';
        }
        // A display version can contain arbitrary plugin text. Publish only conventional
        // numeric versions; paths, descriptive text and unavailable versions stay unknown.
        bool numeric = !Version.empty() && Version.size() <= 40 && Version[0] >= L'0' && Version[0] <= L'9';
        for (wchar_t c : Version)
            numeric = numeric && ((c >= L'0' && c <= L'9') || c == L'.' || c == L'-' || c == L'_');
        if (!numeric)
            Version = L"unknown";
        ReportLines = {"Loading plugin DLL: " + EncodeAcpLossy(Dll),
                       "Loading plugin version: " + EncodeAcpLossy(Version),
                       "Loading plugin SDK: " + std::to_string(Sdk),
                       "Plugin phase: entry"};
    }
};

class ScopedPluginLoadDiagnostic
{
    const PluginLoadDiagnostic** Slot;
    const PluginLoadDiagnostic* Previous;

public:
    ScopedPluginLoadDiagnostic(const PluginLoadDiagnostic** slot, const PluginLoadDiagnostic& context)
        : Slot(slot), Previous(slot != nullptr ? *slot : nullptr)
    {
        if (Slot != nullptr)
            *Slot = &context;
    }

    ~ScopedPluginLoadDiagnostic()
    {
        if (Slot != nullptr)
            *Slot = Previous;
    }

    ScopedPluginLoadDiagnostic(const ScopedPluginLoadDiagnostic&) = delete;
    ScopedPluginLoadDiagnostic& operator=(const ScopedPluginLoadDiagnostic&) = delete;
};

} // namespace sally::diagnostic
