// SPDX-FileCopyrightText: 2025-2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "common/configuration/ConfigurationSearch.h"
#include "common/configuration/DialogTemplate.h"

namespace sally::configuration
{
    struct SearchResult
    {
        int Page = -1;
        std::uint32_t Control = 0;
        std::wstring Label;
    };

    class SearchIndex
    {
    public:
        void Clear() { Pages.clear(); }
        void AddPage(int page, const DialogTemplate& resource, const std::wstring& keywords = {});
        void AddOption(int page, std::uint32_t control, const std::wstring& label,
                       const std::wstring& keywords = {});
        void AddOptionValue(int page, std::uint32_t control, const std::wstring& value);
        void AddKeywords(int page, std::uint32_t control, const std::wstring& text);
        std::vector<SearchResult> Find(const SearchQuery& query) const;
        std::vector<SearchResult> FindSubstring(const std::wstring& text) const;

    private:
        struct Option
        {
            SearchResult Result;
            std::wstring Document;
            std::wstring DisplayLabel;
            bool IsValue = false;
        };
        struct Page
        {
            SearchResult Result;
            std::wstring Keywords;
            std::wstring Document;
            std::vector<Option> Options;
        };
        std::vector<Page> Pages;
    };
} // namespace sally::configuration
