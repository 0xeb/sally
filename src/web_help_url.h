// SPDX-FileCopyrightText: 2025-2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <cstddef>
#include <cwchar>
#include <cwctype>
#include <string>

// Sally's manual is published at https://sally-filemanager.app/manual/. Help requests (F1, What's
// This?, Help buttons, Help menu) open the matching page there.

struct WebHelpTopic
{
    const wchar_t* manual; // "sally" or a plugin manual, e.g. "7zip"
    unsigned long id;      // help context ID
    const wchar_t* page;   // page name without ".html"; "<manual>/<page>" points into another manual
};

enum class WebHelpRequest
{
    Contents, // the manual's start (Help > Contents)
    Search,   // the site search (Help > Index, Help > Search)
    Topic,    // the page for a help context ID
};

// "sally" for Sally itself (no help file name); otherwise the plugin's registered help file name
// without its folder and ".chm" extension, lower-cased: "7zip.chm" -> "7zip".
inline std::wstring WebHelpManualKey(const wchar_t* helpFileName)
{
    if (helpFileName == nullptr || *helpFileName == L'\0')
        return L"sally";
    std::wstring name(helpFileName);
    const size_t slash = name.find_last_of(L"\\/");
    if (slash != std::wstring::npos)
        name.erase(0, slash + 1);
    const size_t dot = name.find_last_of(L'.');
    if (dot != std::wstring::npos)
        name.erase(dot);
    for (wchar_t& c : name)
        c = static_cast<wchar_t>(std::towlower(c));
    return name.empty() ? std::wstring(L"sally") : name;
}

// The page for a help context ID in a manual, or nullptr when the manual has no such topic.
inline const wchar_t* FindWebHelpPage(const WebHelpTopic* topics, size_t count, const std::wstring& manual,
                                      unsigned long id)
{
    for (size_t i = 0; i < count; i++)
    {
        if (topics[i].id == id && manual == topics[i].manual)
            return topics[i].page;
    }
    return nullptr;
}

// The URL to open. A Topic request without a known page falls back to the manual's start.
inline std::wstring BuildWebHelpUrl(const wchar_t* siteRoot, const std::wstring& manual,
                                    WebHelpRequest request, const wchar_t* page)
{
    std::wstring url(siteRoot != nullptr ? siteRoot : L"");
    while (!url.empty() && url.back() == L'/')
        url.pop_back();
    url += L"/manual/";
    if (request == WebHelpRequest::Search)
        return url + L"?search";
    if (request == WebHelpRequest::Topic && page != nullptr && *page != L'\0')
    {
        if (std::wcschr(page, L'/') != nullptr) // a page of another manual
            return url + page + L".html";
        return url + manual + L"/" + page + L".html";
    }
    if (manual != L"sally")
        url += manual + L"/";
    return url;
}
