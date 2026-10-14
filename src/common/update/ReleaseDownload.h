// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

// Asking GitHub for Sally's latest release, and downloading its package so that nothing
// unverified reaches the updater: the package must match the line of the release's
// SHA256SUMS file, and GitHub's own digest for it when GitHub reports one.

#pragma once

#include <windows.h>

#include <functional>
#include <string>

#include "update/GitHubRelease.h"
#include "update/HttpsClient.h"

namespace Sally::Update
{

// The GitHub "latest release" API of Sally's repository.
extern const wchar_t* const kDefaultReleaseFeedUrl;
extern const unsigned long long kMaxReleaseFeedBytes;
extern const unsigned long long kMaxChecksumsBytes;
// A release package is about 12 MB; anything far beyond that is not one.
extern const unsigned long long kMaxPackageBytes;

struct ReleaseFailure
{
    enum class Kind
    {
        None,
        Network,          // see Network
        BadFeed,          // the feed's answer is not a release (Detail: why)
        NoChecksum,       // the release's SHA256SUMS file does not list the package
        ChecksumMismatch, // the package, or GitHub's digest, does not match that line
        Incomplete,       // fewer bytes arrived than the release lists
        WriteFailed,      // the package could not be saved (Code)
        Cancelled,
    };
    Kind What = Kind::None;
    HttpsFailure Network;
    DWORD Code = 0;
    std::string Detail;
};

// The release feed's answer for this Sally (installedVersion: its version text, platform: its
// processor).
bool FetchLatestRelease(IHttpsClient& client, const std::wstring& feedUrl, const std::string& installedVersion,
                        GitHubAssetPlatform platform, ReleaseCheckResult& release, ReleaseFailure& failure);

// Where the package goes while it downloads.
class IPackageSink
{
public:
    virtual ~IPackageSink() = default;
    virtual bool Write(const BYTE* data, DWORD size, DWORD& error) = 0;
    // Ends writing; sha256: lower-case hex SHA-256 of everything written.
    virtual bool Finish(std::string& sha256, DWORD& error) = 0;
    // Throws away what was written (a failed or refused download).
    virtual void Discard() = 0;
};

// Bytes received so far, and the package's size (0 when the release does not say).
using DownloadProgress = std::function<void(unsigned long long received, unsigned long long total)>;

// Downloads a release that BuildReleaseCheckResult found installable. True when the package is
// complete and verified; sha256 then holds its checksum. On failure the sink is discarded.
bool DownloadReleasePackage(IHttpsClient& client, const ReleaseCheckResult& release, IPackageSink& sink,
                            const DownloadProgress& progress, std::string& sha256, ReleaseFailure& failure);

} // namespace Sally::Update
