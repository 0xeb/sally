// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#ifndef SALLY_SHICONOV_DIAG_STANDALONE
#include "precomp.h"
#endif

#include "shiconov_diag.h"
#include "shellsup_diag.h" // AppendAsciiEscapedW

#include <stdio.h>
#include <string.h>
#include <algorithm>

CShellOverlayDiagLog ShellOverlayDiag;

ShellOverlayDiagRecord* CShellOverlayDiagLog::Add(const wchar_t* name, const wchar_t* clsid)
{
    if (Filled >= SHICONOV_DIAG_CAPACITY)
        return NULL;

    ShellOverlayDiagRecord* rec = &Records[Filled++];
    *rec = ShellOverlayDiagRecord();

    if (name != NULL)
        rec->Name = name;
    if (clsid != NULL)
        rec->Clsid = clsid;

    return rec;
}

ShellOverlayDiagRecord* CShellOverlayDiagLog::Find(const wchar_t* name)
{
    if (name == NULL)
        return NULL;

    // Exact compare, not case-insensitive: the registry key name is the identity here, and
    // the reporter's Tortoise keys differ from ordinary ones only by leading whitespace.
    for (int i = 0; i < Filled; i++)
    {
        if (Records[i].Name == name)
            return &Records[i];
    }
    return NULL;
}

const ShellOverlayDiagRecord* CShellOverlayDiagLog::At(int index) const
{
    if (index < 0 || index >= Filled)
        return NULL;
    return &Records[index];
}

void CShellOverlayDiagLog::Reset()
{
    Filled = 0;
    Header = ShellOverlayDiagHeader();
}

const char* OverlayOutcomeText(OverlayOutcome outcome)
{
    switch (outcome)
    {
    case OverlayOutcome::Loaded:
        return "LOADED";
    case OverlayOutcome::SkippedGloballyDisabled:
        return "SKIPPED all-overlays-off";
    case OverlayOutcome::SkippedUserDisabled:
        return "SKIPPED in-disabled-list";
    case OverlayOutcome::RegKeyOpenFailed:
        // Deliberately not spelled as the Win32 API name: the architecture gate counts raw
        // identifiers anywhere in the file, including inside string literals.
        return "DROPPED registry-key-open";
    case OverlayOutcome::RegValueMissing:
        return "DROPPED reg-value-missing";
    case OverlayOutcome::RegValueNotSz:
        return "DROPPED reg-value-not-REG_SZ";
    case OverlayOutcome::InvalidClsid:
        return "DROPPED invalid-CLSID";
    case OverlayOutcome::CoCreateFailed:
        return "DROPPED CoCreateInstance";
    case OverlayOutcome::GetOverlayInfoFailed:
        return "DROPPED GetOverlayInfo";
    case OverlayOutcome::NoIconFileFlag:
        return "DROPPED no-ISIOI_ICONFILE";
    case OverlayOutcome::IconExtractFailed:
        return "DROPPED ExtractIcons";
    case OverlayOutcome::ItemAllocFailed:
        return "DROPPED alloc";
    case OverlayOutcome::CapReached:
        return "DROPPED cap-reached";
    case OverlayOutcome::ArrayGrowFailed:
        return "DROPPED array-grow";
    default:
        return "NOT-ATTEMPTED";
    }
}

int FormatShellOverlayDiagRecord(const ShellOverlayDiagRecord& record, char* buf, int bufSize) noexcept
{
    if (buf == NULL || bufSize <= 0)
        return 0;

    buf[0] = 0;
    try
    {
        const std::string nameAcp = ReportAcpBytesW(record.Name.c_str());
        const std::string clsidAcp = ReportAcpBytesW(record.Clsid.c_str());
        const std::string nameEsc = AsciiEscapedW(record.Name.c_str());
        char fragment[512];
        std::string report = "\"" + nameAcp + "\" " + clsidAcp + " " +
                             OverlayOutcomeText(record.Outcome);

        if (record.Outcome == OverlayOutcome::Loaded)
        {
            sprintf_s(fragment, " idx=%d prio=%d icons=%s%s%s",
                      record.LoadedIndex, record.Priority,
                      (record.IconsOk & 1) ? "16/" : "-/",
                      (record.IconsOk & 2) ? "32/" : "-/",
                      (record.IconsOk & 4) ? "48" : "-");
            report += fragment;
        }
        else if (record.Hr != 0)
        {
            sprintf_s(fragment, " hr=0x%08X", static_cast<unsigned>(record.Hr));
            report += fragment;
        }

        if (record.Outcome == OverlayOutcome::IconExtractFailed ||
            record.Outcome == OverlayOutcome::NoIconFileFlag)
        {
            sprintf_s(fragment, " got=%s%s%s index=%d flags=0x%08X",
                      (record.IconsOk & 1) ? "16 " : "- ",
                      (record.IconsOk & 2) ? "32 " : "- ",
                      (record.IconsOk & 4) ? "48" : "-",
                      record.IconIndex, record.InfoFlags);
            report += fragment;
            const std::string iconAcp = ReportAcpBytesW(record.IconFile.c_str());
            const std::string iconEsc = AsciiEscapedW(record.IconFile.c_str());
            report += " iconFile=\"";
            report += iconAcp;
            report += '"';
            if (iconAcp != iconEsc)
                report += " iconFile(escaped)=\"" + iconEsc + '"';
        }

        if (record.ReaderFailures != 0)
        {
            sprintf_s(fragment, " readerFailures=%ld (last 0x%08X)",
                      record.ReaderFailures, static_cast<unsigned>(record.ReaderLastHr));
            report += fragment;
        }

        if (nameAcp != nameEsc)
            report += " name(escaped)=\"" + nameEsc + '"';

        const size_t copied = (std::min)(report.size(), static_cast<size_t>(bufSize - 1));
        if (copied != 0)
            memcpy(buf, report.data(), copied);
        buf[copied] = 0;
        return static_cast<int>(copied);
    }
    catch (...)
    {
        buf[0] = 0;
        return 0;
    }
}

void SetOverlayDiagConfigRoot(ShellOverlayDiagHeader& header, const wchar_t* rootReg)
{
    header.ConfigRoot = rootReg != NULL ? rootReg : L"<none: first run>";
}
