// SPDX-FileCopyrightText: 2025-2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#include "release_check.h"

namespace checkver
{

    bool BuildReleaseCheckResult(const char* payload, size_t payloadSize,
                                 const std::string& installedVersionText,
                                 GitHubAssetPlatform platform,
                                 ReleaseCheckResult& result, std::string& error)
    {
        result = ReleaseCheckResult();
        error.clear();

        if (payload == nullptr || payloadSize == 0)
        {
            error = "GitHub release payload is empty";
            return false;
        }

        GitHubReleaseInfo release;
        if (!ParseGitHubLatestReleaseJson(payload, payloadSize, release, error))
            return false;

        result.InstalledVersion = NormalizeVersionTag(installedVersionText);
        result.LatestVersion = NormalizeVersionTag(release.TagName);
        result.ReleasePageUrl = release.HtmlUrl;
        result.PrimaryUrl = SelectReleaseAssetUrl(release, platform,
                                                  &result.PrimaryAssetName);
        result.UsedDirectAssetLink = !result.PrimaryUrl.empty();
        if (!result.UsedDirectAssetLink)
            result.PrimaryUrl = result.ReleasePageUrl;

        result.UpdateAvailable =
            CompareVersionTags(result.InstalledVersion, result.LatestVersion) < 0;

        const GitHubReleaseAsset* package = FindPlatformAsset(release, platform);
        const GitHubReleaseAsset* checksums = FindChecksumsAsset(release);
        if (package != nullptr)
        {
            result.PrimaryAssetSize = package->Size;
            result.PrimaryAssetDigest = package->Digest;
        }
        if (checksums != nullptr)
            result.ChecksumsUrl = checksums->DownloadUrl;
        auto isHttps = [](const std::string& url) { return url.compare(0, 8, "https://") == 0; };
        result.CanInstall = result.UpdateAvailable && !release.Prerelease && package != nullptr &&
                            checksums != nullptr && isHttps(package->DownloadUrl) &&
                            isHttps(checksums->DownloadUrl);
        result.HasCorrectData = true;
        return true;
    }

} // namespace checkver
