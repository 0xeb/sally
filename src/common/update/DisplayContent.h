// SPDX-FileCopyrightText: 2025-2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "supporter/Supporter.h"
#include <map>

namespace Sally::Update
{
    struct LocalizedContent
    {
        std::string Title, Markdown;
    };
    using ContentLocales = std::map<std::string, LocalizedContent>;
    struct Notice
    {
        std::string Id, Target = "*";
        uint64_t From = 0, Until = 0;
        ContentLocales Locales;
    };
    struct DisplayContent
    {
        std::string Version;
        ContentLocales Notes;
        std::vector<Notice> Notices;
        uint64_t Published = 0, Until = 0;
    };
    bool ParseReleaseMetadata(std::string_view bytes, std::string_view expectedVersion, DisplayContent& result);
    bool ParseLiveMessages(std::string_view bytes, uint64_t now, DisplayContent& result);
    bool VerifyContent(std::string_view bytes, std::string_view signature, const std::vector<Supporter::PublicKey>& keys);
    LocalizedContent SelectReleaseNotes(const DisplayContent& model, const std::string& tag, const std::string& githubBody = {});
    std::vector<LocalizedContent> ResolveNotices(const DisplayContent& release, const DisplayContent* live,
                                                 const std::string& version, const std::string& tag, uint64_t now);
    bool ValidContentLink(std::string_view url);
    struct MarkdownRun
    {
        size_t Start = 0, Length = 0;
        bool Bold = false, Italic = false, Code = false;
        std::string Link;
    };
    struct MarkdownDisplay
    {
        std::wstring Text;
        std::vector<MarkdownRun> Runs;
        bool Truncated = false;
        bool Visible = false;
    };
    MarkdownDisplay BuildMarkdownDisplay(std::string_view markdown);
    // RichEdit positions count UTF-16 units and one unit per paragraph break.
    constexpr size_t ReadingTextLimit = 2 * 65536 + 2 * 512 + 4;
    struct ReadingDisplay
    {
        MarkdownDisplay Content;
        size_t NotesStart = std::wstring::npos;
        size_t MaintainerStart = std::wstring::npos;
    };
    ReadingDisplay BuildReadingDisplay(const MarkdownDisplay& notes, std::wstring_view notesHeading,
                                       const MarkdownDisplay& maintainer, std::wstring_view maintainerHeading);
    size_t RemapReadingPosition(const ReadingDisplay& previous, const ReadingDisplay& current, size_t position);
} // namespace Sally::Update
