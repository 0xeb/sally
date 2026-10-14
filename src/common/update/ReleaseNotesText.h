// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

// A release's notes, written in Markdown on GitHub, as plain text for the update window's
// read-only edit box: headings lose their #, list items get a bullet, emphasis and code marks
// go, a link keeps its text and shows its address. Lines end with CR LF, as the edit box needs.

#pragma once

#include <string>

namespace Sally::Update
{

namespace detail
{

inline std::wstring PlainMarkdownLine(const std::wstring& line)
{
    size_t indent = 0;
    while (indent < line.size() && line[indent] == L' ')
        ++indent;
    std::wstring rest = line.substr(indent);
    std::wstring prefix(indent, L' ');

    size_t hashes = 0;
    while (hashes < rest.size() && rest[hashes] == L'#')
        ++hashes;
    if (hashes > 0 && hashes <= 6 && hashes < rest.size() && rest[hashes] == L' ')
        rest = rest.substr(hashes + 1);
    else if (rest.size() >= 2 && (rest[0] == L'-' || rest[0] == L'*' || rest[0] == L'+') && rest[1] == L' ')
    {
        prefix += L"\u2022 ";
        rest = rest.substr(2);
    }

    std::wstring out;
    for (size_t i = 0; i < rest.size();)
    {
        const wchar_t ch = rest[i];
        if ((ch == L'*' || ch == L'_') && i + 1 < rest.size() && rest[i + 1] == ch)
        {
            i += 2; // **strong** / __strong__
            continue;
        }
        if (ch == L'`')
        {
            ++i;
            continue;
        }
        // [text](address) and ![alt](address)
        const bool image = ch == L'!' && i + 1 < rest.size() && rest[i + 1] == L'[';
        if (ch == L'[' || image)
        {
            const size_t open = image ? i + 1 : i;
            const size_t close = rest.find(L']', open + 1);
            if (close != std::wstring::npos && close + 1 < rest.size() && rest[close + 1] == L'(')
            {
                const size_t end = rest.find(L')', close + 2);
                if (end != std::wstring::npos)
                {
                    const std::wstring text = rest.substr(open + 1, close - open - 1);
                    const std::wstring address = rest.substr(close + 2, end - close - 2);
                    out += PlainMarkdownLine(text);
                    if (!image && !address.empty() && address != text)
                        out += L" (" + address + L")";
                    i = end + 1;
                    continue;
                }
            }
        }
        out.push_back(ch);
        ++i;
    }
    return prefix + out;
}

} // namespace detail

// At most maxChars characters; longer notes are cut at a line end and marked with an ellipsis.
inline std::wstring ReleaseNotesForDisplay(const std::wstring& markdown, size_t maxChars)
{
    std::wstring out;
    size_t start = 0;
    bool first = true;
    while (start <= markdown.size())
    {
        size_t end = markdown.find(L'\n', start);
        if (end == std::wstring::npos)
            end = markdown.size();
        std::wstring line = markdown.substr(start, end - start);
        if (!line.empty() && line.back() == L'\r')
            line.pop_back();
        const std::wstring plain = detail::PlainMarkdownLine(line);
        const size_t added = (first ? 0 : 2) + plain.size();
        if (out.size() + added > maxChars)
        {
            out += first ? L"\u2026" : L"\r\n\u2026";
            return out;
        }
        if (!first)
            out += L"\r\n";
        out += plain;
        first = false;
        start = end + 1;
    }
    while (out.size() >= 2 && out.compare(out.size() - 2, 2, L"\r\n") == 0)
        out.erase(out.size() - 2);
    return out;
}

} // namespace Sally::Update
