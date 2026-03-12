// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "github_release.h"

#include <algorithm>
#include <cctype>
#include <cstring>
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

class JsonCursor
{
public:
    JsonCursor(const char* json, size_t size)
        : Text(json != nullptr ? json : ""),
          Size(size)
    {
    }

    void SkipWhitespace()
    {
        while (Pos < Size && IsSpace(Text[Pos]))
            Pos++;
    }

    bool Consume(char expected)
    {
        SkipWhitespace();
        if (Pos < Size && Text[Pos] == expected)
        {
            Pos++;
            return true;
        }
        return false;
    }

    bool ParseString(std::string& out, std::string& error)
    {
        SkipWhitespace();
        if (Pos >= Size || Text[Pos] != '"')
        {
            error = "expected JSON string";
            return false;
        }

        Pos++;
        out.clear();
        while (Pos < Size)
        {
            char ch = Text[Pos++];
            if (ch == '"')
                return true;

            if (ch != '\\')
            {
                out.push_back(ch);
                continue;
            }

            if (Pos >= Size)
            {
                error = "unterminated escape sequence";
                return false;
            }

            char escaped = Text[Pos++];
            switch (escaped)
            {
            case '"':
            case '\\':
            case '/':
                out.push_back(escaped);
                break;
            case 'b':
                out.push_back('\b');
                break;
            case 'f':
                out.push_back('\f');
                break;
            case 'n':
                out.push_back('\n');
                break;
            case 'r':
                out.push_back('\r');
                break;
            case 't':
                out.push_back('\t');
                break;
            case 'u':
            {
                if (Pos + 4 > Size)
                {
                    error = "incomplete unicode escape";
                    return false;
                }
                unsigned value = 0;
                for (int i = 0; i < 4; i++)
                {
                    char hex = Text[Pos++];
                    value <<= 4;
                    if (hex >= '0' && hex <= '9')
                        value |= static_cast<unsigned>(hex - '0');
                    else if (hex >= 'a' && hex <= 'f')
                        value |= static_cast<unsigned>(hex - 'a' + 10);
                    else if (hex >= 'A' && hex <= 'F')
                        value |= static_cast<unsigned>(hex - 'A' + 10);
                    else
                    {
                        error = "invalid unicode escape";
                        return false;
                    }
                }
                if (value <= 0x7F)
                    out.push_back(static_cast<char>(value));
                else
                    out.push_back('?');
                break;
            }
            default:
                error = "unsupported escape sequence";
                return false;
            }
        }

        error = "unterminated JSON string";
        return false;
    }

    bool ParseBool(bool& out, std::string& error)
    {
        SkipWhitespace();
        if (MatchLiteral("true"))
        {
            out = true;
            return true;
        }
        if (MatchLiteral("false"))
        {
            out = false;
            return true;
        }

        error = "expected JSON boolean";
        return false;
    }

    bool SkipValue(std::string& error)
    {
        SkipWhitespace();
        if (Pos >= Size)
        {
            error = "unexpected end of JSON";
            return false;
        }

        switch (Text[Pos])
        {
        case '"':
        {
            std::string ignored;
            return ParseString(ignored, error);
        }

        case '{':
            return SkipObject(error);

        case '[':
            return SkipArray(error);

        case 't':
            return MatchLiteral("true") ? true : (error = "invalid literal", false);

        case 'f':
            return MatchLiteral("false") ? true : (error = "invalid literal", false);

        case 'n':
            return MatchLiteral("null") ? true : (error = "invalid literal", false);

        default:
            if (Text[Pos] == '-' || std::isdigit(static_cast<unsigned char>(Text[Pos])) != 0)
                return SkipNumber(error);
            error = "unsupported JSON token";
            return false;
        }
    }

private:
    bool MatchLiteral(const char* literal)
    {
        const size_t literalLength = std::strlen(literal);
        if (Pos + literalLength > Size)
            return false;
        if (std::strncmp(Text + Pos, literal, literalLength) != 0)
            return false;
        Pos += literalLength;
        return true;
    }

    bool SkipNumber(std::string& error)
    {
        size_t start = Pos;
        if (Text[Pos] == '-')
            Pos++;

        if (Pos >= Size || !std::isdigit(static_cast<unsigned char>(Text[Pos])))
        {
            error = "invalid number";
            return false;
        }

        if (Text[Pos] == '0')
        {
            Pos++;
        }
        else
        {
            while (Pos < Size && std::isdigit(static_cast<unsigned char>(Text[Pos])))
                Pos++;
        }

        if (Pos < Size && Text[Pos] == '.')
        {
            Pos++;
            if (Pos >= Size || !std::isdigit(static_cast<unsigned char>(Text[Pos])))
            {
                error = "invalid number fraction";
                return false;
            }
            while (Pos < Size && std::isdigit(static_cast<unsigned char>(Text[Pos])))
                Pos++;
        }

        if (Pos < Size && (Text[Pos] == 'e' || Text[Pos] == 'E'))
        {
            Pos++;
            if (Pos < Size && (Text[Pos] == '+' || Text[Pos] == '-'))
                Pos++;
            if (Pos >= Size || !std::isdigit(static_cast<unsigned char>(Text[Pos])))
            {
                error = "invalid number exponent";
                return false;
            }
            while (Pos < Size && std::isdigit(static_cast<unsigned char>(Text[Pos])))
                Pos++;
        }

        return Pos > start;
    }

    bool SkipArray(std::string& error)
    {
        if (!Consume('['))
        {
            error = "expected '['";
            return false;
        }

        SkipWhitespace();
        if (Consume(']'))
            return true;

        while (true)
        {
            if (!SkipValue(error))
                return false;

            SkipWhitespace();
            if (Consume(']'))
                return true;
            if (!Consume(','))
            {
                error = "expected ',' or ']'";
                return false;
            }
        }
    }

    bool SkipObject(std::string& error)
    {
        if (!Consume('{'))
        {
            error = "expected '{'";
            return false;
        }

        SkipWhitespace();
        if (Consume('}'))
            return true;

        while (true)
        {
            std::string key;
            if (!ParseString(key, error))
                return false;
            if (!Consume(':'))
            {
                error = "expected ':'";
                return false;
            }
            if (!SkipValue(error))
                return false;

            SkipWhitespace();
            if (Consume('}'))
                return true;
            if (!Consume(','))
            {
                error = "expected ',' or '}'";
                return false;
            }
        }
    }

public:
    const char* Text = nullptr;
    size_t Size = 0;
    size_t Pos = 0;
};

bool ParseAssetObject(JsonCursor& cursor, GitHubReleaseAsset& asset, std::string& error)
{
    if (!cursor.Consume('{'))
    {
        error = "expected asset object";
        return false;
    }

    cursor.SkipWhitespace();
    if (cursor.Consume('}'))
        return true;

    while (true)
    {
        std::string key;
        if (!cursor.ParseString(key, error))
            return false;
        if (!cursor.Consume(':'))
        {
            error = "expected ':' in asset object";
            return false;
        }

        if (key == "name")
        {
            if (!cursor.ParseString(asset.Name, error))
                return false;
        }
        else if (key == "browser_download_url")
        {
            if (!cursor.ParseString(asset.DownloadUrl, error))
                return false;
        }
        else
        {
            if (!cursor.SkipValue(error))
                return false;
        }

        cursor.SkipWhitespace();
        if (cursor.Consume('}'))
            return true;
        if (!cursor.Consume(','))
        {
            error = "expected ',' or '}' in asset object";
            return false;
        }
    }
}

bool ParseAssetsArray(JsonCursor& cursor, std::vector<GitHubReleaseAsset>& assets,
                      std::string& error)
{
    if (!cursor.Consume('['))
    {
        error = "expected assets array";
        return false;
    }

    cursor.SkipWhitespace();
    if (cursor.Consume(']'))
        return true;

    while (true)
    {
        GitHubReleaseAsset asset;
        if (!ParseAssetObject(cursor, asset, error))
            return false;
        if (!asset.Name.empty() && !asset.DownloadUrl.empty())
            assets.push_back(std::move(asset));

        cursor.SkipWhitespace();
        if (cursor.Consume(']'))
            return true;
        if (!cursor.Consume(','))
        {
            error = "expected ',' or ']' in assets array";
            return false;
        }
    }
}

bool ParseRootObject(JsonCursor& cursor, GitHubReleaseInfo& release, std::string& error)
{
    if (!cursor.Consume('{'))
    {
        error = "expected root JSON object";
        return false;
    }

    cursor.SkipWhitespace();
    if (cursor.Consume('}'))
    {
        error = "release payload is empty";
        return false;
    }

    while (true)
    {
        std::string key;
        if (!cursor.ParseString(key, error))
            return false;
        if (!cursor.Consume(':'))
        {
            error = "expected ':' in release object";
            return false;
        }

        if (key == "tag_name")
        {
            if (!cursor.ParseString(release.TagName, error))
                return false;
        }
        else if (key == "html_url")
        {
            if (!cursor.ParseString(release.HtmlUrl, error))
                return false;
        }
        else if (key == "prerelease")
        {
            if (!cursor.ParseBool(release.Prerelease, error))
                return false;
        }
        else if (key == "assets")
        {
            if (!ParseAssetsArray(cursor, release.Assets, error))
                return false;
        }
        else
        {
            if (!cursor.SkipValue(error))
                return false;
        }

        cursor.SkipWhitespace();
        if (cursor.Consume('}'))
            return true;
        if (!cursor.Consume(','))
        {
            error = "expected ',' or '}' in release object";
            return false;
        }
    }
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

    JsonCursor cursor(json, size);
    if (!ParseRootObject(cursor, release, error))
        return false;

    cursor.SkipWhitespace();
    if (cursor.Pos != cursor.Size)
    {
        error = "unexpected trailing JSON content";
        return false;
    }

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

std::string SelectReleaseAssetUrl(const GitHubReleaseInfo& release, GitHubAssetPlatform platform,
                                  std::string* assetName)
{
    if (assetName != nullptr)
        assetName->clear();

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
        return std::string();

    for (const GitHubReleaseAsset& asset : release.Assets)
    {
        const std::string loweredName = ToLower(asset.Name);
        if (EndsWith(loweredName, suffix))
        {
            if (assetName != nullptr)
                *assetName = asset.Name;
            return asset.DownloadUrl;
        }
    }

    return std::string();
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
