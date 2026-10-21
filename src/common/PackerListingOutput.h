// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <string>
#include <string_view>

namespace sally::pack
{
    inline bool IsModernRarListing(std::string_view banner)
    {
        if (banner.size() <= 4 || banner.substr(0, 4) != "RAR " || banner[4] < '0' || banner[4] > '9')
            return false;
        unsigned major = 0;
        for (size_t i = 4; i < banner.size() && banner[i] >= '0' && banner[i] <= '9'; ++i)
        {
            // Only the >=5 decision matters; saturation also handles banners
            // with arbitrarily many version digits without integer overflow.
            if (major < 5)
            {
                major = major * 10 + static_cast<unsigned>(banner[i] - '0');
                if (major > 5)
                    major = 5;
            }
        }
        return major >= 5;
    }

    class PackerListingLines
    {
        std::string Partial;

        template <class AcceptLine>
        bool Emit(AcceptLine& acceptLine)
        {
            if (!Partial.empty() && Partial.back() == '\r')
                Partial.pop_back();
            Partial.push_back('\n');
            if (!acceptLine(std::string_view(Partial)))
                return false;
            Partial.clear();
            return true;
        }

    public:
        template <class AcceptLine>
        bool Append(std::string_view bytes, AcceptLine&& acceptLine)
        {
            while (!bytes.empty())
            {
                const size_t end = bytes.find('\n');
                if (end == std::string_view::npos)
                {
                    Partial.append(bytes.data(), bytes.size());
                    return true;
                }
                Partial.append(bytes.data(), end);
                if (!Emit(acceptLine))
                    return false;
                bytes.remove_prefix(end + 1);
            }
            return true;
        }

        template <class AcceptLine>
        bool Finish(AcceptLine&& acceptLine)
        {
            return Partial.empty() || Emit(acceptLine);
        }
    };

    template <class ListingTable>
    bool ApplyRarListingBanner(std::string_view banner, ListingTable& table)
    {
        if (!IsModernRarListing(banner))
            return false;
        table.LinesPerFile = 1;
        table.NameIdx = 8;
        table.AttrIdx = 1;
        return true;
    }
} // namespace sally::pack
