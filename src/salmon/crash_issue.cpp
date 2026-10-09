// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#include "crash_issue.h"

#include <algorithm>

namespace sally::salmon
{

namespace
{

constexpr size_t kMaxFrames = 64;
constexpr size_t kMaxFrameName = 100;

bool IsAsciiLetter(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

bool IsAsciiDigit(char c)
{
    return c >= '0' && c <= '9';
}

bool IsHexDigit(char c)
{
    return IsAsciiDigit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

bool StartsWith(std::string_view text, std::string_view prefix)
{
    return text.size() >= prefix.size() && text.compare(0, prefix.size(), prefix) == 0;
}

bool EndsWith(std::string_view text, std::string_view suffix)
{
    return text.size() >= suffix.size() && text.compare(text.size() - suffix.size(), suffix.size(), suffix) == 0;
}

std::string_view TrimSpaces(std::string_view text)
{
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t'))
        text.remove_prefix(1);
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t'))
        text.remove_suffix(1);
    return text;
}

// Keeps the letters, digits and the given punctuation; anything else becomes '?'.
std::string Restrict(std::string_view text, std::string_view punctuation, size_t maxLength)
{
    std::string out;
    for (char c : TrimSpaces(text))
    {
        if (out.size() == maxLength)
            break;
        const bool allowed = IsAsciiLetter(c) || IsAsciiDigit(c) || punctuation.find(c) != std::string_view::npos;
        out.push_back(allowed ? c : '?');
    }
    return out;
}

std::vector<std::string_view> SplitLines(std::string_view text)
{
    std::vector<std::string_view> lines;
    while (!text.empty())
    {
        size_t end = text.find('\n');
        std::string_view line = text.substr(0, end);
        if (!line.empty() && line.back() == '\r')
            line.remove_suffix(1);
        lines.push_back(line);
        if (end == std::string_view::npos)
            break;
        text.remove_prefix(end + 1);
    }
    return lines;
}

// "... execution address = 0x00007FF6 (sally.exe: 0x1A2B3)" -> "sally.exe+0x1A2B3"
std::string ParseLocation(std::string_view origin)
{
    if (origin.empty() || origin.back() != ')')
        return {};
    const size_t open = origin.rfind(" (");
    if (open == std::string_view::npos)
        return {};
    std::string_view inner = origin.substr(open + 2, origin.size() - open - 3);
    const size_t colon = inner.rfind(": 0x");
    if (colon == std::string_view::npos)
        return {};
    std::string_view module = inner.substr(0, colon);
    const size_t separator = module.find_last_of("\\/");
    if (separator != std::string_view::npos)
        module.remove_prefix(separator + 1);
    std::string_view offset = inner.substr(colon + 2);
    if (offset.size() < 3 || offset.size() > 18)
        return {};
    for (size_t i = 2; i < offset.size(); i++)
    {
        if (!IsHexDigit(offset[i]))
            return {};
    }
    std::string name = Restrict(module, "._- ", 64);
    if (name.empty())
        return {};
    return name + "+" + std::string(offset);
}

// The function name a call-stack message starts with: "CFilesWindow::ChangeDir(<path>, 1)" gives
// "CFilesWindow::ChangeDir". Every message format starts with literal code text, so the name never
// holds an argument; whatever follows it is dropped.
std::string FrameName(std::string_view line)
{
    size_t length = 0;
    while (length < line.size() && length <= kMaxFrameName)
    {
        const char c = line[length];
        if (!IsAsciiLetter(c) && !IsAsciiDigit(c) && c != '_' && c != ':' && c != '~' && c != '<' &&
            c != '>' && c != '-')
            break;
        length++;
    }
    std::string_view name = line.substr(0, length);
    while (!name.empty() && name.back() == ':')
        name.remove_suffix(1);
    if (name.empty() || length > kMaxFrameName || (!IsAsciiLetter(name[0]) && name[0] != '_' && name[0] != '~'))
        return "(other)";
    return std::string(name);
}

void AppendLine(std::string& body, std::string_view text)
{
    body.append(text);
    body.push_back('\n');
}

std::string_view ExceptionKind(const CrashSummary& summary)
{
    std::string_view kind = summary.Exception;
    return kind.substr(0, kind.find(':'));
}

// Cuts UTF-8 text to at most 'bytes' bytes without splitting a character.
std::string_view CutUtf8(std::string_view text, size_t bytes)
{
    if (text.size() <= bytes)
        return text;
    while (bytes > 0 && (static_cast<unsigned char>(text[bytes]) & 0xC0) == 0x80)
        bytes--;
    return text.substr(0, bytes);
}

} // namespace

// The report is a list of sections: a heading line ("Call Stacks:") followed by its own lines, which
// are indented. Only the sections and lines named here are read.
CrashSummary SummarizeBugReport(std::string_view report)
{
    CrashSummary summary;
    const std::vector<std::string_view> lines = SplitLines(report);
    std::string_view section;
    bool versionLine = true;
    for (size_t i = 0; i < lines.size(); i++)
    {
        const std::string_view line = TrimSpaces(lines[i]);
        if (line.empty())
            continue;
        if (lines[i][0] != ' ')
        {
            // The title is followed by the version; "Break was used." is followed by its reason.
            if (versionLine && EndsWith(line, "Bug Report File"))
                continue;
            if (versionLine)
                summary.Version = Restrict(line, " .()+-_", 80);
            else if (line == "Break was used.")
                summary.Break = true;
            versionLine = false;
            section = line;
            continue;
        }

        if (section == "Information About Exception:")
        {
            if (summary.Exception.empty() && StartsWith(line, "Exception: "))
                summary.Exception = Restrict(line.substr(11), " :.+-_", 120);
            else if (summary.Location.empty() && StartsWith(line, "Exception origin: "))
                summary.Location = ParseLocation(line);
        }
        else if (section == "System Version:")
        {
            if (summary.Windows.empty() && StartsWith(line, "SalGetVersionEx Version "))
            {
                std::string_view version = line.substr(24);
                const size_t close = version.find(')');
                if (close != std::string_view::npos)
                    version = version.substr(0, close + 1);
                summary.Windows = Restrict(version, " .()", 40);
            }
            else if (summary.Architecture.empty() && StartsWith(line, "GetNativeSystemInfo architecture "))
                summary.Architecture = Restrict(line.substr(33), " ()-", 30);
        }
        else if (section == "Call Stacks:" && summary.Frames.empty() && StartsWith(line, "Thread with Exception (ID: "))
        {
            const size_t plugin = line.find("): in ");
            if (plugin != std::string_view::npos)
                summary.Plugin = Restrict(line.substr(plugin + 6), " .+-_()", 60);

            std::vector<std::string> frames;
            for (size_t j = i + 1; j < lines.size(); j++)
            {
                const std::string_view frame = TrimSpaces(lines[j]);
                if (frame.empty() || lines[j][0] != ' ' || frame == "----" ||
                    StartsWith(frame, "Number of skipped records:") || StartsWith(frame, "Thread ID: "))
                    break;
                frames.push_back(FrameName(frame));
            }
            // The report lists the outermost call first.
            std::reverse(frames.begin(), frames.end());
            if (frames.size() > kMaxFrames)
                frames.resize(kMaxFrames);
            summary.Frames = std::move(frames);
        }
    }
    return summary;
}

std::string ComposeCrashIssueTitle(const CrashSummary& summary)
{
    std::string title;
    if (summary.Break)
        title = "Bug report";
    else if (!summary.Exception.empty())
        title = "Crash: " + std::string(ExceptionKind(summary));
    else
        title = "Crash";
    if (!summary.Location.empty())
        title += " at " + summary.Location;
    if (!summary.Version.empty())
        title += " in " + summary.Version;
    return title;
}

std::string ComposeCrashIssueBody(const CrashSummary& summary, std::string_view lastAction,
                                  int otherReports, size_t frameCount)
{
    std::string body;
    AppendLine(body, "### What were you doing when Sally stopped?");
    AppendLine(body, "");
    std::string action;
    for (char c : TrimSpaces(lastAction))
    {
        if (c != '\r')
            action.push_back(c);
    }
    AppendLine(body, action.empty() ? "(please describe it here)" : action);
    AppendLine(body, "");

    AppendLine(body, "### Crash summary");
    AppendLine(body, "");
    AppendLine(body, "- Sally: " + (summary.Version.empty() ? std::string("unknown") : summary.Version));
    std::string windows = summary.Windows.empty() ? std::string("unknown") : summary.Windows;
    if (!summary.Architecture.empty())
        windows += ", " + summary.Architecture;
    AppendLine(body, "- Windows: " + windows);
    if (summary.Break)
        AppendLine(body, "- Made on request (Break), not by an exception");
    if (!summary.Exception.empty())
        AppendLine(body, "- Exception: " + summary.Exception);
    if (!summary.Location.empty())
        AppendLine(body, "- Location: " + summary.Location);
    if (!summary.Plugin.empty())
        AppendLine(body, "- Plugin: " + summary.Plugin);
    if (otherReports > 0)
        AppendLine(body, "- Other crash reports on this computer: " + std::to_string(otherReports));

    frameCount = (std::min)(frameCount, summary.Frames.size());
    if (frameCount > 0)
    {
        AppendLine(body, "");
        AppendLine(body, "### Call stack (innermost first, arguments removed)");
        AppendLine(body, "");
        AppendLine(body, "```");
        for (size_t i = 0; i < frameCount; i++)
            AppendLine(body, summary.Frames[i]);
        if (frameCount < summary.Frames.size())
            AppendLine(body, "... " + std::to_string(summary.Frames.size() - frameCount) + " more");
        AppendLine(body, "```");
    }

    AppendLine(body, "");
    AppendLine(body, "### Crash files");
    AppendLine(body, "");
    body.append("The full crash report and memory dump stay on my computer. They can contain file names "
                "and memory contents, so they are not attached. A maintainer may ask me to share them privately.\n");
    return body;
}

std::string EncodeQueryValue(std::string_view text)
{
    static const char hex[] = "0123456789ABCDEF";
    std::string out;
    out.reserve(text.size() * 3);
    for (char c : text)
    {
        const unsigned char byte = static_cast<unsigned char>(c);
        if (IsAsciiLetter(c) || IsAsciiDigit(c) || c == '-' || c == '.' || c == '_' || c == '~')
            out.push_back(c);
        else if (c == ' ')
            out.push_back('+');
        else
        {
            out.push_back('%');
            out.push_back(hex[byte >> 4]);
            out.push_back(hex[byte & 0x0F]);
        }
    }
    return out;
}

std::string BuildCrashIssueAddress(const CrashSummary& summary, std::string_view lastAction,
                                   int otherReports, size_t maxLength)
{
    const std::string prefix = std::string(kNewIssueAddress) + "?title=" +
                               EncodeQueryValue(ComposeCrashIssueTitle(summary)) + "&body=";
    auto address = [&](std::string_view action, size_t frames)
    {
        return prefix + EncodeQueryValue(ComposeCrashIssueBody(summary, action, otherReports, frames));
    };

    // Fewer frames first, down to the innermost few.
    const size_t minFrames = (std::min)(summary.Frames.size(), static_cast<size_t>(5));
    for (size_t frames = summary.Frames.size();; frames--)
    {
        std::string candidate = address(lastAction, frames);
        if (candidate.size() <= maxLength)
            return candidate;
        if (frames == minFrames)
            break;
    }

    // Then the end of the user's text, marked as cut.
    for (size_t frames = minFrames;; frames--)
    {
        size_t low = 0;
        size_t high = lastAction.size();
        std::string best;
        while (low <= high)
        {
            const size_t middle = low + (high - low) / 2;
            std::string action(CutUtf8(lastAction, middle));
            if (!action.empty())
                action += " [...]";
            std::string candidate = address(action, frames);
            if (candidate.size() <= maxLength)
            {
                best = std::move(candidate);
                low = middle + 1;
            }
            else if (middle == 0)
                break;
            else
                high = middle - 1;
        }
        if (!best.empty())
            return best;
        if (frames == 0)
            break;
    }

    // Even the bare summary is too long for the limit: open the empty form.
    return std::string(kNewIssueAddress);
}

} // namespace sally::salmon
