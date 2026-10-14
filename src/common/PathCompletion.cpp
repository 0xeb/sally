// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#include "PathCompletion.h"

#include "IFileEnumerator.h"

#include <algorithm>
#include <cwctype>

namespace Sally::PathCompletion
{
namespace
{

bool IsSeparator(wchar_t c)
{
    return c == L'\\' || c == L'/';
}

bool IsDriveLetter(wchar_t c)
{
    return (c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z');
}

// Characters a Windows path component cannot hold ("\\?\" prefixes are not typed by users).
bool HasInvalidCharacter(const std::wstring& text, size_t from)
{
    for (size_t i = from; i < text.size(); i++)
    {
        const wchar_t c = text[i];
        if (c < 32 || c == L'<' || c == L'>' || c == L'"' || c == L'|' || c == L'?' || c == L'*' || c == L':')
            return true;
    }
    return false;
}

std::wstring WithoutTrailingSeparators(std::wstring path)
{
    while (path.size() > 3 && IsSeparator(path.back()))
        path.pop_back();
    return path;
}

std::wstring Join(const std::wstring& base, const std::wstring& relative)
{
    if (base.empty())
        return relative;
    if (IsSeparator(base.back()))
        return base + relative;
    return base + L"\\" + relative;
}

int CompareNoCase(const std::wstring& a, const std::wstring& b)
{
    const size_t n = a.size() < b.size() ? a.size() : b.size();
    for (size_t i = 0; i < n; i++)
    {
        const wchar_t x = static_cast<wchar_t>(std::towlower(a[i]));
        const wchar_t y = static_cast<wchar_t>(std::towlower(b[i]));
        if (x != y)
            return x < y ? -1 : 1;
    }
    return a.size() == b.size() ? 0 : (a.size() < b.size() ? -1 : 1);
}

} // namespace

Target Resolve(const std::wstring& typedHead, const std::wstring& baseDirectory)
{
    Target target;
    target.Head = typedHead;
    const std::wstring& h = typedHead;

    if (h.empty())
    {
        // Nothing typed up to a separator yet: complete names in the base folder.
        if (baseDirectory.empty())
            return target;
        target.Directory = WithoutTrailingSeparators(baseDirectory);
        target.Complete = true;
        return target;
    }
    if (!IsSeparator(h.back()))
        return target; // the autocomplete always passes the part up to a separator

    if (h.size() >= 2 && IsDriveLetter(h[0]) && h[1] == L':')
    {
        if (h.size() < 3 || !IsSeparator(h[2]))
            return target; // "C:dir\": relative to the drive's current folder, which Sally does not track
        if (HasInvalidCharacter(h, 3))
            return target;
        target.Directory = WithoutTrailingSeparators(h);
        target.Complete = true;
        return target;
    }
    if (h.size() >= 2 && IsSeparator(h[0]) && IsSeparator(h[1]))
    {
        // UNC: "\\server\share\..." lists; "\\server\" would need a share listing, so nothing.
        size_t server = 2;
        while (server < h.size() && !IsSeparator(h[server]))
            server++;
        size_t share = server + 1;
        while (share < h.size() && !IsSeparator(h[share]))
            share++;
        if (server == 2 || share >= h.size() || share == server + 1)
            return target;
        if (HasInvalidCharacter(h, 2))
            return target;
        target.Directory = WithoutTrailingSeparators(h);
        target.Complete = true;
        return target;
    }
    if (HasInvalidCharacter(h, 0))
        return target; // also ':' in "ftp://" and other plugin paths
    if (baseDirectory.empty())
        return target;
    if (IsSeparator(h[0]))
    {
        // "\dir\": from the root of the base folder's drive
        if (baseDirectory.size() >= 2 && IsDriveLetter(baseDirectory[0]) && baseDirectory[1] == L':')
        {
            target.Directory = WithoutTrailingSeparators(baseDirectory.substr(0, 2) + h);
            target.Complete = true;
        }
        return target;
    }
    target.Directory = WithoutTrailingSeparators(Join(baseDirectory, h));
    target.Complete = true;
    return target;
}

std::vector<std::wstring> Candidates(const std::wstring& head, std::vector<std::wstring> names, size_t maxCount)
{
    names.erase(std::remove_if(names.begin(), names.end(),
                               [](const std::wstring& n) { return n.empty() || n == L"." || n == L".."; }),
                names.end());
    std::sort(names.begin(), names.end(), [](const std::wstring& a, const std::wstring& b) {
        return CompareNoCase(a, b) < 0;
    });
    if (names.size() > maxCount)
        names.resize(maxCount);
    std::vector<std::wstring> result;
    result.reserve(names.size());
    for (const std::wstring& name : names)
        result.push_back(head + name);
    return result;
}

std::vector<std::wstring> ListFolderCandidates(IFileEnumerator& enumerator, const Target& target, size_t maxCount)
{
    if (!target.Complete || target.Directory.empty())
        return {};
    HENUM handle = enumerator.StartEnum(target.Directory.c_str(), L"*");
    if (handle == INVALID_HENUM)
        return {};
    std::vector<std::wstring> names;
    FileEnumEntry entry;
    // Read a little past the cap, so the sorted result is the alphabetically first ones of a
    // large folder more often than not; a huge folder is never read to the end.
    const size_t readLimit = maxCount * 4;
    while (names.size() < readLimit)
    {
        const EnumResult r = enumerator.NextFile(handle, entry);
        if (!r.success || r.noMoreFiles)
            break;
        if (entry.IsDirectory())
            names.push_back(entry.name);
    }
    enumerator.EndEnum(handle);
    return Candidates(target.Head, std::move(names), maxCount);
}

} // namespace Sally::PathCompletion
