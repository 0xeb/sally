// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

// The SHA-256 checksums a Sally release publishes next to its packages
// (Sally-v<version>-SHA256SUMS.txt, one "<64 hex digits>  <file name>" line per package),
// and the digest GitHub reports for each release asset ("sha256:<64 hex digits>").

#pragma once

#include <windows.h>

#include <string>
#include <vector>

namespace Sally::Update
{

struct ChecksumEntry
{
    std::string FileName;
    std::string Sha256; // 64 lower-case hex digits
};

// Every line must be well formed; blank lines are allowed. A file named twice is refused.
bool ParseSha256Sums(const std::string& text, std::vector<ChecksumEntry>& entries, std::string& error);
const ChecksumEntry* FindChecksum(const std::vector<ChecksumEntry>& entries, const std::string& fileName);

// "sha256:<hex>" from GitHub's asset metadata; false for anything else.
bool ParseGitHubSha256Digest(const std::string& digest, std::string& sha256);

// Lower-case hex SHA-256 of a file.
bool ComputeFileSha256(const std::wstring& path, std::string& sha256, DWORD& error);

} // namespace Sally::Update
