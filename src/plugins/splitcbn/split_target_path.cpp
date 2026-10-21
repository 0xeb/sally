// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later
#include "split_target_path.h"

namespace sally::split
{
    TargetResult ResolveTarget(std::wstring& path, bool directory, const std::wstring& root,
                               bool activePreferred, ITargetServices& services)
    {
        services.UpdateDefaultDirectories(activePreferred);
        ParsedTarget parsed;
        if (!services.Parse(path, root, parsed))
            return TargetResult::ParseFailed;
        if (!parsed.Windows)
            return TargetResult::NotWindows;
        if (parsed.RemainingOffset > path.size())
            return TargetResult::InvalidResult;
        if (!parsed.ExistingDirectory)
            return TargetResult::Valid;
        std::wstring_view remainder(path.data() + parsed.RemainingOffset, path.size() - parsed.RemainingOffset);
        if (!directory && remainder.empty())
            return TargetResult::NotFile;
        if (!directory)
        {
            const size_t firstSeparator = remainder.find(L'\\');
            if (firstSeparator == std::wstring_view::npos)
                return TargetResult::Valid;
        }
        if (!remainder.empty() && !services.ConfirmSubdirectories())
            return TargetResult::DeclinedCreation;
        return TargetResult::Valid;
    }
    std::wstring DefaultCombinedName(std::wstring_view companionBase)
    {
        std::wstring result(companionBase);
        if (!result.empty() && result.back() == L'.')
            result.pop_back();
        const size_t slash = result.find_last_of(L"\\/");
        const size_t filename = slash == std::wstring::npos ? 0 : slash + 1;
        if (filename == result.size())
            return {};
        if (result.find(L'.', filename) == std::wstring::npos)
            result += L".EXT";
        return result;
    }
} // namespace sally::split
