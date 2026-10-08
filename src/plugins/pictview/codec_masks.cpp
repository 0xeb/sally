// SPDX-FileCopyrightText: 2025-2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#include "codec_masks.h"

#include <algorithm>
#include <cwctype>

namespace pictview
{
namespace
{

std::wstring Lower(std::wstring text)
{
    for (wchar_t& ch : text)
        ch = static_cast<wchar_t>(std::towlower(ch));
    return text;
}

bool IsPlainExtension(const std::wstring& extension)
{
    if (extension.empty() || extension.size() > 16)
        return false;
    return std::all_of(extension.begin(), extension.end(), [](wchar_t ch) {
        return (ch >= L'a' && ch <= L'z') || (ch >= L'0' && ch <= L'9') || ch == L'-' || ch == L'_';
    });
}

std::vector<std::wstring> Split(const std::wstring& text, wchar_t separator)
{
    std::vector<std::wstring> parts;
    size_t start = 0;
    while (start <= text.size())
    {
        const size_t end = text.find(separator, start);
        const size_t stop = end == std::wstring::npos ? text.size() : end;
        parts.push_back(text.substr(start, stop - start));
        if (end == std::wstring::npos)
            break;
        start = end + 1;
    }
    return parts;
}

std::wstring Trim(const std::wstring& text)
{
    size_t first = 0;
    while (first < text.size() && std::iswspace(text[first]))
        ++first;
    size_t last = text.size();
    while (last > first && std::iswspace(text[last - 1]))
        --last;
    return text.substr(first, last - first);
}

void SortUnique(std::vector<std::wstring>& values)
{
    std::sort(values.begin(), values.end());
    values.erase(std::unique(values.begin(), values.end()), values.end());
}

} // namespace

std::vector<std::wstring> ParseDecoderExtensions(const std::vector<std::wstring>& extensionLists)
{
    std::vector<std::wstring> extensions;
    for (const std::wstring& list : extensionLists)
    {
        for (std::wstring item : Split(list, L','))
        {
            item = Lower(Trim(item));
            if (!item.empty() && item[0] == L'.')
                item.erase(0, 1);
            if (IsPlainExtension(item))
                extensions.push_back(item);
        }
    }
    SortUnique(extensions);
    return extensions;
}

bool MasksCoverExtension(const std::wstring& masks, const std::wstring& extension)
{
    const std::wstring wanted = L"*." + Lower(extension);
    for (const std::wstring& mask : Split(masks, L';'))
    {
        if (Lower(Trim(mask)) == wanted)
            return true;
    }
    return false;
}

std::vector<std::wstring> ExtensionsBeyondMasks(const std::vector<std::wstring>& extensions, const std::wstring& masks)
{
    std::vector<std::wstring> result;
    for (const std::wstring& extension : extensions)
    {
        if (!MasksCoverExtension(masks, extension))
            result.push_back(extension);
    }
    return result;
}

std::wstring MasksFromExtensions(const std::vector<std::wstring>& extensions)
{
    std::wstring masks;
    for (const std::wstring& extension : extensions)
    {
        if (!masks.empty())
            masks += L';';
        masks += L"*." + extension;
    }
    return masks;
}

std::wstring JoinExtensions(const std::vector<std::wstring>& extensions)
{
    std::wstring joined;
    for (const std::wstring& extension : extensions)
    {
        if (!joined.empty())
            joined += L';';
        joined += extension;
    }
    return joined;
}

std::vector<std::wstring> SplitExtensions(const std::wstring& joined)
{
    std::vector<std::wstring> extensions;
    for (std::wstring item : Split(joined, L';'))
    {
        item = Lower(Trim(item));
        if (IsPlainExtension(item))
            extensions.push_back(item);
    }
    SortUnique(extensions);
    return extensions;
}

std::vector<std::wstring> ExtensionsNotOffered(const std::vector<std::wstring>& installed, const std::vector<std::wstring>& offered)
{
    std::vector<std::wstring> result;
    for (const std::wstring& extension : installed)
    {
        if (std::find(offered.begin(), offered.end(), extension) == offered.end())
            result.push_back(extension);
    }
    return result;
}

} // namespace pictview
