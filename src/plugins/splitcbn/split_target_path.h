// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <string>
#include <string_view>

namespace sally::split
{
    struct ParsedTarget
    {
        bool Windows = false;
        bool ExistingDirectory = false;
        size_t RemainingOffset = std::wstring::npos;
    };
    enum class TargetResult
    {
        Valid,
        ParseFailed,
        NotWindows,
        NotFile,
        DeclinedCreation,
        InvalidResult
    };
    class ITargetServices
    {
    public:
        virtual ~ITargetServices() = default;
        virtual void UpdateDefaultDirectories(bool activePreferred) = 0;
        virtual bool Parse(std::wstring& path, const std::wstring& root, ParsedTarget& parsed) = 0;
        virtual bool ConfirmSubdirectories() = 0;
    };
    TargetResult ResolveTarget(std::wstring& path, bool directory, const std::wstring& root,
                               bool activePreferred, ITargetServices& services);
    std::wstring DefaultCombinedName(std::wstring_view companionBase);
} // namespace sally::split
