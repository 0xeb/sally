// SPDX-FileCopyrightText: 2025-2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#include "common/configuration/ConfigurationSearchIndex.h"
#include "common/text/CaseFolding.h"
#include <utility>

namespace sally::configuration
{
    namespace
    {
        std::wstring LabelText(const std::wstring& text)
        {
            std::wstring label;
            for (std::size_t i = 0; i < text.size(); ++i)
            {
                if (text[i] == L'&')
                {
                    if (i + 1 == text.size() || text[i + 1] != L'&')
                        continue;
                    ++i;
                }
                label.push_back(text[i]);
            }
            return label;
        }
        bool ClassIs(const DialogControl& control, std::uint16_t ordinal, const wchar_t* name)
        {
            return control.ClassOrdinal == ordinal || sally::text::CompareFolded(control.ClassName.c_str(), name) == 0;
        }
    } // namespace

    void SearchIndex::AddPage(int page, const DialogTemplate& resource, const std::wstring& keywords)
    {
        Page entry;
        entry.Result = {page, 0, LabelText(resource.Title)};
        entry.Keywords = LabelText(keywords);
        entry.Document = entry.Result.Label + L" " + entry.Keywords;
        Pages.push_back(std::move(entry));
        std::wstring pendingLabel;
        std::wstring groupLabel;
        for (const auto& control : resource.Controls)
        {
            // Edit/combo initial contents are values rather than option names. Never
            // index them, even when a resource happens to contain a default value.
            const bool valueControl = ClassIs(control, 0x81, L"Edit") || ClassIs(control, 0x85, L"ComboBox");
            const bool isStatic = ClassIs(control, 0x82, L"Static");
            const bool isButton = ClassIs(control, 0x80, L"Button");
            const bool group = isButton && (control.Style & 0xf) == 7;
            const bool tabStop = (control.Style & 0x00010000) != 0;
            const std::wstring label = valueControl ? std::wstring() : LabelText(control.Text);
            if (!label.empty())
                Pages.back().Document += L" " + label;
            if (group)
            {
                groupLabel = label;
                pendingLabel.clear();
                continue;
            }
            if (isStatic && !tabStop && !label.empty())
                pendingLabel = label;
            else if (tabStop && !group && control.Id != 0 && control.Id != 0xffffffff)
            {
                const std::wstring option = label.empty() ? pendingLabel : label;
                if (!option.empty())
                    AddOption(page, control.Id, option, groupLabel);
                pendingLabel.clear();
            }
        }
    }

    void SearchIndex::AddKeywords(int page, std::uint32_t control, const std::wstring& text)
    {
        for (auto& entry : Pages)
        {
            if (entry.Result.Page != page)
                continue;
            entry.Document += L" " + text;
            for (auto& option : entry.Options)
                if (option.Result.Control == control)
                    option.Document += L" " + text;
            return;
        }
    }

    void SearchIndex::AddOptionValue(int page, std::uint32_t control, const std::wstring& value)
    {
        for (auto& entry : Pages)
        {
            if (entry.Result.Page != page)
                continue;
            for (const auto& option : entry.Options)
            {
                if (option.Result.Control != control || option.IsValue)
                    continue;
                const std::wstring clean = LabelText(value);
                const std::wstring label = option.Result.Label + L" \u2014 " + clean;
                const std::wstring document = option.Document + L" " + value;
                const std::wstring display = option.DisplayLabel + L" \u2014 " + clean;
                entry.Document += L" " + value;
                entry.Options.push_back({{page, control, label}, document, display, true});
                return;
            }
            const std::wstring clean = LabelText(value);
            const std::wstring label = entry.Result.Label + L" \u2014 " + clean;
            const std::wstring document = entry.Result.Label + L" " + clean + L" " + entry.Keywords;
            entry.Document += L" " + clean;
            entry.Options.push_back({{page, control, label}, document, clean, true});
            return;
        }
    }

    void SearchIndex::AddOption(int page, std::uint32_t control, const std::wstring& label,
                                const std::wstring& keywords)
    {
        for (auto& entry : Pages)
        {
            if (entry.Result.Page != page)
                continue;
            const std::wstring clean = LabelText(label);
            const std::wstring document = entry.Result.Label + L" " + clean + L" " + entry.Keywords + L" " + keywords;
            entry.Document += L" " + clean + L" " + keywords;
            entry.Options.push_back({{page, control, entry.Result.Label + L" \u2014 " + clean}, document, clean});
            return;
        }
    }

    std::vector<SearchResult> SearchIndex::Find(const SearchQuery& query) const
    {
        std::vector<SearchResult> result;
        if (!query.IsValid() || query.IsEmpty())
            return result;
        for (const auto& page : Pages)
        {
            bool optionFound = false;
            for (const auto& option : page.Options)
            {
                if (!query.Matches(option.Document))
                    continue;
                // Dynamic combo choices can share a control ID but retain their own
                // translated labels in the results.
                result.push_back(option.Result);
                optionFound = true;
            }
            if (!optionFound && query.Matches(page.Document))
                result.push_back(page.Result);
        }
        return result;
    }

    std::vector<SearchResult> SearchIndex::FindSubstring(const std::wstring& text) const
    {
        std::vector<SearchResult> results;
        const auto first = text.find_first_not_of(L" \t\r\n");
        if (first == std::wstring::npos)
            return results;
        const auto last = text.find_last_not_of(L" \t\r\n");
        const std::wstring needle = sally::text::Fold(text.substr(first, last - first + 1));
        const auto matches = [&](const std::wstring& value)
        {
            return sally::text::Fold(value).find(needle) != std::wstring::npos;
        };
        for (const auto& page : Pages)
        {
            // A page-name match is one destination, not every option on that page.
            if (matches(page.Result.Label))
            {
                results.push_back(page.Result);
                continue;
            }
            for (const auto& option : page.Options)
            {
                if (!matches(option.Document))
                    continue;
                const std::wstring label = option.DisplayLabel + L" (" + page.Result.Label + L")";
                results.push_back({option.Result.Page, option.Result.Control, label});
            }
        }
        return results;
    }
} // namespace sally::configuration
