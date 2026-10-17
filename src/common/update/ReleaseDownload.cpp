// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#include "update/ReleaseDownload.h"
#include "SallyUrls.h"

#include "Win32TextCodec.h"
#include "update/ReleaseChecksums.h"

#include <vector>

namespace Sally::Update
{

const wchar_t* const kDefaultReleaseFeedUrl = Sally::Urls::LatestReleaseFeed;
const unsigned long long kMaxReleaseFeedBytes = 1024 * 1024;
const unsigned long long kMaxChecksumsBytes = 64 * 1024;
const unsigned long long kMaxPackageBytes = 300ull * 1024 * 1024;

namespace
{

const wchar_t* const kGitHubApiHeaders = L"Accept: application/vnd.github+json\r\n"
                                         L"X-GitHub-Api-Version: 2022-11-28\r\n";

std::wstring Widen(const std::string& utf8)
{
    std::wstring wide;
    Win32DecodeText(CP_UTF8, utf8, wide);
    return wide;
}

bool NetworkFailure(ReleaseFailure& failure, const HttpsFailure& network)
{
    failure.What = network.Cancelled ? ReleaseFailure::Kind::Cancelled : ReleaseFailure::Kind::Network;
    failure.Network = network;
    return false;
}

// Collects a small body (the feed, the checksums) in memory.
HttpsSink Collect(std::string& text)
{
    return [&text](const BYTE* data, DWORD size, DWORD&) {
        text.append(reinterpret_cast<const char*>(data), size);
        return true;
    };
}

} // namespace

bool FetchLatestRelease(IHttpsClient& client, const std::wstring& feedUrl, const std::string& installedVersion,
                        GitHubAssetPlatform platform, ReleaseCheckResult& release, ReleaseFailure& failure)
{
    release = ReleaseCheckResult();
    failure = ReleaseFailure();
    std::string payload;
    HttpsFailure network;
    if (!client.Get(feedUrl, kGitHubApiHeaders, kMaxReleaseFeedBytes, Collect(payload), network))
        return NetworkFailure(failure, network);
    if (!BuildReleaseCheckResult(payload.data(), payload.size(), installedVersion, platform, release, failure.Detail))
    {
        failure.What = ReleaseFailure::Kind::BadFeed;
        return false;
    }
    return true;
}

bool DownloadReleasePackage(IHttpsClient& client, const ReleaseCheckResult& release, IPackageSink& sink,
                            const DownloadProgress& progress, std::string& sha256, ReleaseFailure& failure)
{
    sha256.clear();
    failure = ReleaseFailure();
    auto refuse = [&](ReleaseFailure::Kind what) {
        sink.Discard();
        failure.What = what;
        return false;
    };
    if (!release.CanInstall)
        return refuse(ReleaseFailure::Kind::NoChecksum);

    // The checksums the release publishes; the package must match its line, and GitHub's
    // digest for the package must say the same.
    std::string sumsText;
    HttpsFailure network;
    if (!client.Get(Widen(release.ChecksumsUrl), NULL, kMaxChecksumsBytes, Collect(sumsText), network))
    {
        sink.Discard();
        return NetworkFailure(failure, network);
    }
    std::vector<ChecksumEntry> sums;
    std::string parseError;
    const ChecksumEntry* entry = NULL;
    if (ParseSha256Sums(sumsText, sums, parseError))
        entry = FindChecksum(sums, release.PrimaryAssetName);
    if (entry == NULL)
        return refuse(ReleaseFailure::Kind::NoChecksum);
    std::string digest;
    if (!release.PrimaryAssetDigest.empty() &&
        (!ParseGitHubSha256Digest(release.PrimaryAssetDigest, digest) || digest != entry->Sha256))
        return refuse(ReleaseFailure::Kind::ChecksumMismatch);

    const unsigned long long expected = release.PrimaryAssetSize;
    const unsigned long long limit = expected > 0 && expected < kMaxPackageBytes ? expected : kMaxPackageBytes;
    unsigned long long received = 0;
    DWORD writeError = 0;
    const bool downloaded = client.Get(
        Widen(release.PrimaryUrl), NULL, limit,
        [&](const BYTE* data, DWORD size, DWORD& error) {
            if (!sink.Write(data, size, error))
            {
                writeError = error;
                return false;
            }
            received += size;
            if (progress)
                progress(received, expected);
            return true;
        },
        network);
    if (!downloaded)
    {
        sink.Discard();
        if (writeError != 0)
        {
            failure.What = ReleaseFailure::Kind::WriteFailed;
            failure.Code = writeError;
            return false;
        }
        return NetworkFailure(failure, network);
    }
    if (expected > 0 && received != expected)
        return refuse(ReleaseFailure::Kind::Incomplete);
    std::string actual;
    DWORD error = 0;
    if (!sink.Finish(actual, error))
    {
        sink.Discard();
        failure.What = ReleaseFailure::Kind::WriteFailed;
        failure.Code = error;
        return false;
    }
    if (actual != entry->Sha256)
        return refuse(ReleaseFailure::Kind::ChecksumMismatch);
    sha256 = actual;
    return true;
}

} // namespace Sally::Update
