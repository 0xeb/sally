// SPDX-FileCopyrightText: 2025-2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

// Sally's releases as GitHub's "latest release" API describes them: the release's tag, page,
// notes and assets, and what that means for the Sally that asks (is the release newer, which
// package fits this processor, can it be installed). The response is UTF-8 and stays so here;
// it is decoded only where it is shown.

#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace Sally::Update
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
    std::string Name; // the release's title, may be empty
    std::string Body; // the release notes (Markdown), may be empty
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

// The platform this program was built for.
constexpr GitHubAssetPlatform BuildPlatform()
{
#if defined(_M_ARM64)
    return GitHubAssetPlatform::ARM64;
#elif defined(_WIN64)
    return GitHubAssetPlatform::X64;
#else
    return GitHubAssetPlatform::X86;
#endif
}

struct ReleaseCheckResult
{
    bool HasCorrectData = false;
    bool UpdateAvailable = false;
    bool UsedDirectAssetLink = false;
    std::string InstalledVersion;
    std::string LatestVersion;
    std::string ReleasePageUrl;
    std::string ReleaseName;
    std::string ReleaseNotes;
    std::string PrimaryUrl;
    std::string PrimaryAssetName;
    unsigned long long PrimaryAssetSize = 0;
    std::string PrimaryAssetDigest; // "sha256:<hex>" or empty
    std::string ChecksumsUrl;       // the release's SHA256SUMS file, or empty
    std::string MetadataUrl;        // optional display content, independent of CanInstall
    std::string MetadataSignatureUrl;
    // A newer release whose package and checksum file can be fetched over HTTPS, so the
    // updater can install it.
    bool CanInstall = false;
};

// Parse a GitHub response, compare versions, and select the platform download. On failure,
// result is reset and error explains why.
bool BuildReleaseCheckResult(const char* payload, size_t payloadSize, const std::string& installedVersionText,
                             GitHubAssetPlatform platform, ReleaseCheckResult& result, std::string& error);

} // namespace Sally::Update
