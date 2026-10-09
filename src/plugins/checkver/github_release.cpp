// SPDX-FileCopyrightText: 2025-2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#include "github_release.h"

#pragma push_macro("free")
#undef free
#include "yyjson/yyjson.h"
#pragma pop_macro("free")

#include <algorithm>
#include <cctype>
#include <sstream>
#include <string_view>
#include <utility>

#ifdef min
#undef min
#endif

#ifdef max
#undef max
#endif

namespace checkver
{
namespace
{

bool IsSpace(char ch)
{
    return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n';
}

std::string Trim(std::string_view text)
{
    size_t begin = 0;
    while (begin < text.size() && IsSpace(text[begin]))
        begin++;

    size_t end = text.size();
    while (end > begin && IsSpace(text[end - 1]))
        end--;

    return std::string(text.substr(begin, end - begin));
}

std::string ToLower(std::string_view text)
{
    std::string lowered;
    lowered.reserve(text.size());
    for (char ch : text)
        lowered.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
    return lowered;
}

bool EndsWith(std::string_view text, std::string_view suffix)
{
    return text.size() >= suffix.size() &&
           text.substr(text.size() - suffix.size()) == suffix;
}

bool ParseSemverParts(const std::string& normalizedTag, std::vector<int>& parts)
{
    parts.clear();

    std::string_view text(normalizedTag);
    if (!text.empty() && (text.front() == 'v' || text.front() == 'V'))
        text.remove_prefix(1);

    size_t pos = 0;
    while (pos < text.size())
    {
        if (!std::isdigit(static_cast<unsigned char>(text[pos])))
            return !parts.empty();

        int value = 0;
        while (pos < text.size() && std::isdigit(static_cast<unsigned char>(text[pos])))
        {
            value = value * 10 + (text[pos] - '0');
            pos++;
        }
        parts.push_back(value);

        if (pos >= text.size())
            return true;

        if (text[pos] == '.')
        {
            pos++;
            continue;
        }

        if (text[pos] == '-')
            return true;

        return false;
    }

    return !parts.empty();
}

} // namespace

bool ParseGitHubLatestReleaseJson(const char* json, size_t size, GitHubReleaseInfo& release,
                                  std::string& error)
{
    release = GitHubReleaseInfo();
    error.clear();

    const char* input = json != nullptr ? json : "";
    const size_t inputSize = json != nullptr ? size : 0;
    yyjson_read_err readError{};
    yyjson_doc* document = yyjson_read_opts(const_cast<char*>(input), inputSize, 0, nullptr, &readError);
    if (document == nullptr)
    {
        std::ostringstream message;
        message << (readError.msg != nullptr ? readError.msg : "invalid JSON")
                << " at byte " << readError.pos;
        error = message.str();
        return false;
    }

    yyjson_val* root = yyjson_doc_get_root(document);
    if (!yyjson_is_obj(root))
    {
        yyjson_doc_free(document);
        error = "expected root JSON object";
        return false;
    }

    auto ReadString = [](yyjson_val* object, const char* key, std::string& value) {
        yyjson_val* member = yyjson_obj_get(object, key);
        if (!yyjson_is_str(member))
            return false;
        value.assign(yyjson_get_str(member), yyjson_get_len(member));
        return true;
    };

    ReadString(root, "tag_name", release.TagName);
    ReadString(root, "html_url", release.HtmlUrl);

    yyjson_val* prerelease = yyjson_obj_get(root, "prerelease");
    if (yyjson_is_bool(prerelease))
        release.Prerelease = yyjson_get_bool(prerelease);

    yyjson_val* assets = yyjson_obj_get(root, "assets");
    if (yyjson_is_arr(assets))
    {
        size_t index;
        size_t count;
        yyjson_val* value;
        yyjson_arr_foreach(assets, index, count, value)
        {
            if (!yyjson_is_obj(value))
                continue;

            GitHubReleaseAsset asset;
            ReadString(value, "name", asset.Name);
            ReadString(value, "browser_download_url", asset.DownloadUrl);
            ReadString(value, "digest", asset.Digest);
            yyjson_val* size = yyjson_obj_get(value, "size");
            if (yyjson_is_uint(size))
                asset.Size = yyjson_get_uint(size);
            if (!asset.Name.empty() && !asset.DownloadUrl.empty())
                release.Assets.push_back(std::move(asset));
        }
    }

    yyjson_doc_free(document);

    if (release.TagName.empty())
    {
        error = "release payload is missing tag_name";
        return false;
    }
    if (release.HtmlUrl.empty())
    {
        error = "release payload is missing html_url";
        return false;
    }

    return true;
}

std::string NormalizeVersionTag(const std::string& rawVersionText)
{
    std::string value = Trim(rawVersionText);
    if (value.empty())
        return value;

    size_t paren = value.rfind(" (");
    if (paren != std::string::npos && EndsWith(value, ")"))
        value.erase(paren);

    size_t start = std::string::npos;
    for (size_t i = 0; i < value.size(); i++)
    {
        const unsigned char ch = static_cast<unsigned char>(value[i]);
        if ((value[i] == 'v' || value[i] == 'V') &&
            i + 1 < value.size() &&
            std::isdigit(static_cast<unsigned char>(value[i + 1])) != 0)
        {
            start = i;
            break;
        }
        if (std::isdigit(ch) != 0)
        {
            start = i;
            break;
        }
    }

    if (start == std::string::npos)
        return Trim(value);

    size_t end = start;
    while (end < value.size())
    {
        const unsigned char ch = static_cast<unsigned char>(value[end]);
        if (std::isalnum(ch) != 0 || value[end] == '.' || value[end] == '-' || value[end] == '_')
            end++;
        else
            break;
    }

    std::string normalized = value.substr(start, end - start);
    if (!normalized.empty() && std::isdigit(static_cast<unsigned char>(normalized[0])) != 0)
        normalized.insert(normalized.begin(), 'v');
    return normalized;
}

int CompareVersionTags(const std::string& lhs, const std::string& rhs)
{
    const std::string left = NormalizeVersionTag(lhs);
    const std::string right = NormalizeVersionTag(rhs);

    std::vector<int> leftParts;
    std::vector<int> rightParts;
    const bool leftValid = ParseSemverParts(left, leftParts);
    const bool rightValid = ParseSemverParts(right, rightParts);

    if (leftValid && rightValid)
    {
        const size_t maxParts = std::max(leftParts.size(), rightParts.size());
        for (size_t i = 0; i < maxParts; i++)
        {
            const int leftValue = i < leftParts.size() ? leftParts[i] : 0;
            const int rightValue = i < rightParts.size() ? rightParts[i] : 0;
            if (leftValue < rightValue)
                return -1;
            if (leftValue > rightValue)
                return 1;
        }
        return 0;
    }

    if (left < right)
        return -1;
    if (left > right)
        return 1;
    return 0;
}

const GitHubReleaseAsset* FindPlatformAsset(const GitHubReleaseInfo& release, GitHubAssetPlatform platform)
{
    const char* suffix = nullptr;
    switch (platform)
    {
    case GitHubAssetPlatform::X64:
        suffix = "-x64.zip";
        break;
    case GitHubAssetPlatform::X86:
        suffix = "-x86.zip";
        break;
    case GitHubAssetPlatform::ARM64:
        suffix = "-arm64.zip";
        break;
    default:
        break;
    }

    if (suffix == nullptr)
        return nullptr;

    for (const GitHubReleaseAsset& asset : release.Assets)
    {
        if (EndsWith(ToLower(asset.Name), suffix))
            return &asset;
    }
    return nullptr;
}

const GitHubReleaseAsset* FindChecksumsAsset(const GitHubReleaseInfo& release)
{
    for (const GitHubReleaseAsset& asset : release.Assets)
    {
        if (EndsWith(ToLower(asset.Name), "-sha256sums.txt"))
            return &asset;
    }
    return nullptr;
}

std::string SelectReleaseAssetUrl(const GitHubReleaseInfo& release, GitHubAssetPlatform platform,
                                  std::string* assetName)
{
    if (assetName != nullptr)
        assetName->clear();
    const GitHubReleaseAsset* asset = FindPlatformAsset(release, platform);
    if (asset == nullptr)
        return std::string();
    if (assetName != nullptr)
        *assetName = asset->Name;
    return asset->DownloadUrl;
}

const char* GetPlatformLabel(GitHubAssetPlatform platform)
{
    switch (platform)
    {
    case GitHubAssetPlatform::X86:
        return "x86";
    case GitHubAssetPlatform::X64:
        return "x64";
    case GitHubAssetPlatform::ARM64:
        return "ARM64";
    default:
        return "unknown";
    }
}

} // namespace checkver
