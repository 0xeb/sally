// SPDX-FileCopyrightText: 2025-2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once
#include <string>
#include <string_view>

// Transport and presentation stay in main-thread adapters. The complete copy
// must outlive asynchronous associations and Sally itself.
class IFtpFileOpenHost
{
public:
    virtual ~IFtpFileOpenHost() = default;
    virtual bool CreateTemporaryDirectory(std::wstring& directory) = 0;
    virtual bool DownloadCompleteCopy(const std::wstring& directory, const std::wstring& name) = 0;
    virtual void LaunchAssociation(const std::wstring& directory, const std::wstring& name) = 0;
    virtual void RemoveIncompleteCopy(const std::wstring& directory) noexcept = 0;
};
bool FtpOpenDownloadedCopy(std::wstring_view localName, IFtpFileOpenHost& host) noexcept;

enum class CFtpLinkActivation
{
    Directory,
    File,
    Failed
};
CFtpLinkActivation FtpClassifyLinkActivation(bool commandCompleted, bool directoryAccepted,
                                             std::string_view reply) noexcept;
