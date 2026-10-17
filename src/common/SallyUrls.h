// SPDX-FileCopyrightText: 2025-2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

namespace Sally::Urls
{
    inline constexpr wchar_t Website[] = L"https://get-sally.app/";
    inline constexpr wchar_t WebsiteHost[] = L"get-sally.app";
    inline constexpr wchar_t Support[] = L"https://get-sally.app/support-sally/";
    inline constexpr wchar_t LiveMessages[] = L"https://get-sally.app/update/messages.json";
    inline constexpr wchar_t LiveMessagesSignature[] = L"https://get-sally.app/update/messages.json.sig";
    inline constexpr wchar_t LatestReleaseFeed[] = L"https://api.github.com/repos/0xeb/sally/releases/latest";
    inline constexpr wchar_t Repository[] = L"https://github.com/0xeb/sally";
    inline constexpr wchar_t Releases[] = L"https://github.com/0xeb/sally/releases";
    inline constexpr wchar_t Discussions[] = L"https://github.com/0xeb/sally/discussions";
    inline constexpr wchar_t Issues[] = L"https://github.com/0xeb/sally/issues";

    // UTF-8 addresses for callers that compose byte-oriented reports or metadata.
    inline constexpr char RepositoryUtf8[] = "https://github.com/0xeb/sally";
    inline constexpr char ReleasesUtf8[] = "https://github.com/0xeb/sally/releases";
    inline constexpr char NewIssueUtf8[] = "https://github.com/0xeb/sally/issues/new";
}
