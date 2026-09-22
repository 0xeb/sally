// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#include "find_history.h"

#include <utility>

namespace sally::dbviewer
{

bool RememberFindText(FindHistoryEntries& history, const std::wstring& text) noexcept
{
    if (text.empty())
        return true;

    try
    {
        FindHistoryEntries staged = history;
        std::size_t existing = staged.size();
        for (std::size_t i = 0; i < staged.size(); ++i)
        {
            if (staged[i] == text)
            {
                existing = i;
                break;
            }
        }

        const std::size_t moveCount = existing < staged.size() ? existing : staged.size() - 1;
        for (std::size_t i = moveCount; i > 0; --i)
            staged[i] = std::move(staged[i - 1]);
        staged[0] = text;
        history.swap(staged);
        return true;
    }
    catch (...)
    {
        return false;
    }
}

} // namespace sally::dbviewer
