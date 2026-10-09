// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace sally::salmon
{

// The facts of a text bug report that are safe to publish: what failed and where in the code.
// The report also holds paths, the command line, memory and stack dumps, window titles, panel
// contents and call-stack arguments; none of that is copied here, and every copied field is
// limited to the characters its format can contain.
struct CrashSummary
{
    std::string Version;             // "Sally 1.0.35 (x64)"
    std::string Windows;             // "10.0 (Build 26300)"
    std::string Architecture;        // "64-bit (x64)"
    std::string Exception;           // "access violation: read on 0x0000000000000010"
    std::string Location;            // "sally.exe+0x1A2B3"
    std::string Plugin;              // the plugin the crashing thread was in, empty for Sally itself
    bool Break = false;              // the report was made on request, not by an exception
    std::vector<std::string> Frames; // the crashing thread's call stack, innermost first, names only
};

CrashSummary SummarizeBugReport(std::string_view report);

// Address of a new issue in Sally's public tracker.
inline constexpr std::string_view kNewIssueAddress = "https://github.com/0xeb/sally/issues/new";

// Longest address handed to the shell: longer ones are not opened reliably.
inline constexpr size_t kMaxIssueAddressLength = 2000;

std::string ComposeCrashIssueTitle(const CrashSummary& summary);

// 'lastAction' is the user's own UTF-8 text. 'frameCount' frames of the summary are listed.
std::string ComposeCrashIssueBody(const CrashSummary& summary, std::string_view lastAction,
                                  int otherReports, size_t frameCount);

// Percent-encodes UTF-8 text for a query value (a space becomes '+').
std::string EncodeQueryValue(std::string_view text);

// The new-issue address with title and body filled in, at most 'maxLength' characters long: the
// outermost frames go first, then the end of the user's text, then the remaining frames.
std::string BuildCrashIssueAddress(const CrashSummary& summary, std::string_view lastAction,
                                   int otherReports, size_t maxLength = kMaxIssueAddressLength);

} // namespace sally::salmon
