// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#ifndef SALLY_SHELLSUP_DIAG_STANDALONE
#include "precomp.h"
#endif

#include "shellsup_diag.h"
#include "common/DiagnosticTextEncoding.h"

#include <stdio.h>
#include <string.h>
#include <algorithm>

CShellMenuDiagLog ShellMenuDiag;

ShellMenuDiagRecord* CShellMenuDiagLog::Begin(const wchar_t* dirPathW, int selCount, bool background)
{
    Head = (Head + 1) % SHELLMENU_DIAG_CAPACITY;
    if (Filled < SHELLMENU_DIAG_CAPACITY)
        Filled++;

    ShellMenuDiagRecord* rec = &Records[Head];
    *rec = ShellMenuDiagRecord();

    rec->Tick = GetTickCount();
    rec->SelCount = selCount;
    rec->Background = background;
    if (dirPathW != NULL)
        rec->DirPathW = dirPathW;

    Open = true;
    return rec;
}

const ShellMenuDiagRecord* CShellMenuDiagLog::At(int index) const
{
    if (index < 0 || index >= Filled)
        return NULL;

    // Oldest first: the slot after Head is the oldest once the ring has wrapped.
    int oldest = (Filled == SHELLMENU_DIAG_CAPACITY) ? (Head + 1) % SHELLMENU_DIAG_CAPACITY
                                                     : 0;
    return &Records[(oldest + index) % SHELLMENU_DIAG_CAPACITY];
}

void CShellMenuDiagLog::Reset()
{
    Head = -1;
    Filled = 0;
    Open = false;
}

namespace
{

int CopyReportBytes(const std::string& text, char* buf, int bufSize) noexcept
{
    if (buf == NULL || bufSize <= 0)
        return 0;

    const size_t copied = (std::min)(text.size(), static_cast<size_t>(bufSize - 1));
    if (copied != 0)
        memcpy(buf, text.data(), copied);
    buf[copied] = 0;
    return static_cast<int>(copied);
}

} // namespace

std::string ReportAcpBytesW(const wchar_t* text)
{
    if (text == NULL || *text == L'\0')
        return {};

    std::string bytes;
    if (!sally::diagnostic::EncodeAcpLossy(text, bytes))
        bytes.clear();
    return bytes;
}

std::string AsciiEscapedW(const wchar_t* text)
{
    std::string escaped;
    if (text == NULL)
        return escaped;

    for (const wchar_t* s = text; *s != 0; s++)
    {
        if (*s >= 0x20 && *s < 0x7f)
            escaped.push_back(static_cast<char>(*s));
        else
        {
            char fragment[7];
            sprintf_s(fragment, "\\u%04X", static_cast<unsigned>(*s));
            escaped += fragment;
        }
    }
    return escaped;
}

int AppendAsciiEscapedW(const wchar_t* text, char* buf, int bufSize) noexcept
{
    if (buf == NULL || bufSize <= 0)
        return 0;

    int written = 0;
    buf[0] = 0;
    if (text == NULL)
        return 0;

    for (const wchar_t* s = text; *s != 0; s++)
    {
        if (written + 7 > bufSize)
            break;

        if (*s >= 0x20 && *s < 0x7f)
            buf[written++] = static_cast<char>(*s);
        else
            written += sprintf_s(buf + written, bufSize - written, "\\u%04X",
                                 static_cast<unsigned>(*s));
        buf[written] = 0;
    }
    return written;
}

namespace
{

const char* OwnerText(ShellMenuOwner owner)
{
    switch (owner)
    {
    case ShellMenuOwner::ItemMenu:
        return "ItemMenu";
    case ShellMenuOwner::NewMenu:
        return "NewMenu";
    case ShellMenuOwner::SallyOwned:
        return "SallyOwned";
    case ShellMenuOwner::Cancelled:
        return "Cancelled";
    default:
        return "Unknown";
    }
}

} // namespace

int FormatShellMenuDiagRecord(const ShellMenuDiagRecord& record, char* buf, int bufSize) noexcept
{
    if (buf == NULL || bufSize <= 0)
        return 0;

    buf[0] = 0;
    try
    {
        const std::string pathAcp = ReportAcpBytesW(record.DirPathW.c_str());
        const std::string pathEsc = AsciiEscapedW(record.DirPathW.c_str());
        const std::string verbEsc = AsciiEscapedW(record.Verb.c_str());
        char fragment[512];
        std::string report;

        sprintf_s(fragment, "tick=%u dir=\"", record.Tick);
        report += fragment;
        report += pathAcp;
        sprintf_s(fragment, "\" sel=%d bg=%s wideNames=%s\n", record.SelCount,
                  record.Background ? "yes" : "no",
                  record.AnyNameNeedsWide ? "yes" : "no");
        report += fragment;

        if (pathAcp != pathEsc)
            report += "    dir(escaped)=\"" + pathEsc + "\"\n";

        sprintf_s(fragment, "    QueryContextMenu=0x%08X items=%d\n",
                  static_cast<unsigned>(record.QueryContextMenuHr), record.MenuItemCount);
        report += fragment;

        sprintf_s(fragment, "    cmd=%u topLevel=%s verb=\"", record.TrackedCmd,
                  record.TopLevel ? "yes" : "no");
        report += fragment;
        report += verbEsc;
        sprintf_s(fragment, "\" (GetCommandString=0x%08X)\n",
                  static_cast<unsigned>(record.VerbHr));
        report += fragment;

        sprintf_s(fragment,
                  "    owner=%s InvokeCommand=0x%08X parent=%s aliveAfter=%s\n",
                  OwnerText(record.Owner), static_cast<unsigned>(record.InvokeHr),
                  record.ParentWasTransient ? "transient" : "durable",
                  record.ParentAliveAfter ? "yes" : "no");
        report += fragment;
        return CopyReportBytes(report, buf, bufSize);
    }
    catch (...)
    {
        buf[0] = 0;
        return 0;
    }
}
