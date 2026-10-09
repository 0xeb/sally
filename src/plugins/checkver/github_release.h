// SPDX-FileCopyrightText: 2025-2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace checkver
{

enum class GitHubAssetPlatform
{
    Unknown,
    X86,
    X64,
    ARM64,
};

struct GitHubReleaseAsset
{
    std::string Name;
    std::string DownloadUrl;
    unsigned long long Size = 0; // bytes, 0 when GitHub did not say
    std::string Digest;          // "sha256:<hex>" when GitHub reports one
};

struct GitHubReleaseInfo
{
    std::string TagName;
    std::string HtmlUrl;
    bool Prerelease = false;
    std::vector<GitHubReleaseAsset> Assets;
};

bool ParseGitHubLatestReleaseJson(const char* json, size_t size, GitHubReleaseInfo& release,
                                  std::string& error);

std::string NormalizeVersionTag(const std::string& rawVersionText);

// Returns -1 when lhs < rhs, 0 when lhs == rhs, and 1 when lhs > rhs.
// If either side cannot be parsed as a version, the raw normalized strings are
// compared lexicographically as a stable fallback.
int CompareVersionTags(const std::string& lhs, const std::string& rhs);

std::string SelectReleaseAssetUrl(const GitHubReleaseInfo& release, GitHubAssetPlatform platform,
                                  std::string* assetName = nullptr);

// The package for a platform, and the release's checksum file (Sally-v<version>-SHA256SUMS.txt);
// nullptr when the release has none.
const GitHubReleaseAsset* FindPlatformAsset(const GitHubReleaseInfo& release, GitHubAssetPlatform platform);
const GitHubReleaseAsset* FindChecksumsAsset(const GitHubReleaseInfo& release);

const char* GetPlatformLabel(GitHubAssetPlatform platform);

} // namespace checkver
