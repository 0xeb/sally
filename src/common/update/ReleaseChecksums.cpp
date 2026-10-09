// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#include "update/ReleaseChecksums.h"

#include <bcrypt.h>

#include <vector>

#pragma comment(lib, "bcrypt.lib")

namespace Sally::Update
{

namespace
{

bool NormalizeHex(const std::string& text, std::string& hex)
{
    if (text.size() != 64)
        return false;
    hex.clear();
    for (char ch : text)
    {
        if (ch >= '0' && ch <= '9')
            hex.push_back(ch);
        else if (ch >= 'a' && ch <= 'f')
            hex.push_back(ch);
        else if (ch >= 'A' && ch <= 'F')
            hex.push_back(static_cast<char>(ch - 'A' + 'a'));
        else
            return false;
    }
    return true;
}

} // namespace

bool ParseSha256Sums(const std::string& text, std::vector<ChecksumEntry>& entries, std::string& error)
{
    entries.clear();
    error.clear();
    size_t start = 0;
    int lineNumber = 0;
    while (start < text.size())
    {
        size_t end = text.find('\n', start);
        if (end == std::string::npos)
            end = text.size();
        std::string line = text.substr(start, end - start);
        start = end + 1;
        ++lineNumber;
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        if (line.empty())
            continue;
        // "<hex>  <name>" (text mode) or "<hex> *<name>" (binary mode), as sha256sum writes.
        if (line.size() < 67 || line[64] != ' ' || (line[65] != ' ' && line[65] != '*'))
        {
            error = "line " + std::to_string(lineNumber) + " is not a checksum line";
            return false;
        }
        ChecksumEntry entry;
        if (!NormalizeHex(line.substr(0, 64), entry.Sha256))
        {
            error = "line " + std::to_string(lineNumber) + " has an invalid checksum";
            return false;
        }
        entry.FileName = line.substr(66);
        if (entry.FileName.empty() || entry.FileName.find_first_of("/\\") != std::string::npos)
        {
            error = "line " + std::to_string(lineNumber) + " has an invalid file name";
            return false;
        }
        if (FindChecksum(entries, entry.FileName) != nullptr)
        {
            error = "the file " + entry.FileName + " is listed twice";
            return false;
        }
        entries.push_back(entry);
    }
    if (entries.empty())
    {
        error = "no checksums";
        return false;
    }
    return true;
}

const ChecksumEntry* FindChecksum(const std::vector<ChecksumEntry>& entries, const std::string& fileName)
{
    for (const ChecksumEntry& entry : entries)
    {
        if (entry.FileName == fileName)
            return &entry;
    }
    return nullptr;
}

bool ParseGitHubSha256Digest(const std::string& digest, std::string& sha256)
{
    const std::string prefix = "sha256:";
    if (digest.compare(0, prefix.size(), prefix) != 0)
        return false;
    return NormalizeHex(digest.substr(prefix.size()), sha256);
}

bool ComputeFileSha256(const std::wstring& path, std::string& sha256, DWORD& error)
{
    sha256.clear();
    error = 0;
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                              FILE_FLAG_SEQUENTIAL_SCAN, NULL);
    if (file == INVALID_HANDLE_VALUE)
    {
        error = GetLastError();
        return false;
    }
    BCRYPT_ALG_HANDLE algorithm = NULL;
    BCRYPT_HASH_HANDLE hash = NULL;
    bool ok = BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, NULL, 0)) &&
              BCRYPT_SUCCESS(BCryptCreateHash(algorithm, &hash, NULL, 0, NULL, 0, 0));
    std::vector<unsigned char> buffer(256 * 1024);
    while (ok)
    {
        DWORD read = 0;
        if (!ReadFile(file, buffer.data(), static_cast<DWORD>(buffer.size()), &read, NULL))
        {
            error = GetLastError();
            ok = false;
            break;
        }
        if (read == 0)
            break;
        ok = BCRYPT_SUCCESS(BCryptHashData(hash, buffer.data(), read, 0));
    }
    unsigned char digest[32];
    if (ok)
        ok = BCRYPT_SUCCESS(BCryptFinishHash(hash, digest, sizeof(digest), 0));
    if (hash != NULL)
        BCryptDestroyHash(hash);
    if (algorithm != NULL)
        BCryptCloseAlgorithmProvider(algorithm, 0);
    CloseHandle(file);
    if (!ok)
    {
        if (error == 0)
            error = ERROR_INVALID_FUNCTION;
        return false;
    }
    static const char kHex[] = "0123456789abcdef";
    for (unsigned char byte : digest)
    {
        sha256.push_back(kHex[byte >> 4]);
        sha256.push_back(kHex[byte & 0x0F]);
    }
    return true;
}

} // namespace Sally::Update
