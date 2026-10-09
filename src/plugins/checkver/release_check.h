// SPDX-FileCopyrightText: 2025-2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "github_release.h"

#include <cstddef>
#include <string>

namespace checkver
{

    struct ReleaseCheckResult
    {
        bool HasCorrectData = false;
        bool UpdateAvailable = false;
        bool UsedDirectAssetLink = false;
        std::string InstalledVersion;
        std::string LatestVersion;
        std::string ReleasePageUrl;
        std::string PrimaryUrl;
        std::string PrimaryAssetName;
        unsigned long long PrimaryAssetSize = 0;
        std::string PrimaryAssetDigest; // "sha256:<hex>" or empty
        std::string ChecksumsUrl;       // the release's SHA256SUMS file, or empty
        // A newer release whose package and checksum file can be fetched over HTTPS, so the
        // updater can install it.
        bool CanInstall = false;
    };

    // UI-free production workflow: parse a GitHub response, compare versions, and
    // select the platform download. On failure, result is reset and error explains why.
    bool BuildReleaseCheckResult(const char* payload, size_t payloadSize,
                                 const std::string& installedVersionText,
                                 GitHubAssetPlatform platform,
                                 ReleaseCheckResult& result, std::string& error);

} // namespace checkver
