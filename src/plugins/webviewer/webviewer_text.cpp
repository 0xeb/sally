// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#include "webviewer_text.h"

#include "common/Win32TextCodec.h"

#include <string_view>

namespace
{
bool IsAsciiPathByteSafe(unsigned char value)
{
    return (value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z') ||
           (value >= '0' && value <= '9') || value == '-' || value == '.' || value == '_' ||
           value == '~' || value == '/' || value == ':';
}

bool BuildFileUrlUtf8(const std::wstring& originalPath, std::string& url)
{
    std::wstring_view path(originalPath);
    bool unc = path.size() >= 2 && (path[0] == L'\\' || path[0] == L'/') &&
               (path[1] == L'\\' || path[1] == L'/');

    if (path.size() >= 8 && path.substr(0, 8) == L"\\\\?\\UNC\\")
    {
        path.remove_prefix(8);
        unc = true;
    }
    else if (path.size() >= 4 && path.substr(0, 4) == L"\\\\?\\")
    {
        path.remove_prefix(4);
        unc = false;
    }
    else if (unc)
    {
        path.remove_prefix(2);
    }

    std::string encodedPath;
    if (!Win32EncodeText(CP_UTF8, path.data(), path.size(), encodedPath))
        return false;

    static constexpr char Hex[] = "0123456789ABCDEF";
    std::string staged = unc ? "file://" : "file:///";
    staged.reserve(staged.size() + encodedPath.size());
    for (unsigned char value : encodedPath)
    {
        if (value == '\\')
            staged.push_back('/');
        else if (IsAsciiPathByteSafe(value))
            staged.push_back(static_cast<char>(value));
        else
        {
            staged.push_back('%');
            staged.push_back(Hex[value >> 4]);
            staged.push_back(Hex[value & 0x0F]);
        }
    }
    url.swap(staged);
    return true;
}
} // namespace

bool WebViewerBuildFileUrl(const std::wstring& filePath, std::wstring& url)
{
    std::string encodedUrl;
    if (!BuildFileUrlUtf8(filePath, encodedUrl))
        return false;

    std::wstring staged(encodedUrl.begin(), encodedUrl.end());
    url.swap(staged);
    return true;
}

bool WebViewerBuildMarkdownBaseHref(const std::wstring& filePath, std::string& baseHref)
{
    const size_t separator = filePath.find_last_of(L"\\/");
    const std::wstring directory = separator == std::wstring::npos
                                       ? L".\\"
                                       : filePath.substr(0, separator + 1);
    return BuildFileUrlUtf8(directory, baseHref);
}

bool WebViewerDecodeHtmlUtf8(const std::string& html, std::wstring& wideHtml)
{
    return Win32DecodeText(CP_UTF8, html, wideHtml).Succeeded();
}
