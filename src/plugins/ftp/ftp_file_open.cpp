// SPDX-FileCopyrightText: 2025-2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"
#include "ftp_file_open.h"

bool FtpOpenDownloadedCopy(std::wstring_view localName, IFtpFileOpenHost& host) noexcept
{
    if (localName.empty() || localName == L"." || localName == L".." ||
        localName.find_first_of(std::wstring_view(L"\0/\\:", 4)) != std::wstring_view::npos)
        return false;
    std::wstring directory;
    bool ownsDirectory = false, complete = false;
    try
    {
        const std::wstring name(localName);
        ownsDirectory = host.CreateTemporaryDirectory(directory);
        if (!ownsDirectory)
            return false;
        complete = host.DownloadCompleteCopy(directory, name);
        if (complete)
        {
            // Association failure must not retire a successfully downloaded input.
            host.LaunchAssociation(directory, name);
            return true;
        }
    }
    catch (...)
    {
    }
    if (ownsDirectory && !complete)
        host.RemoveIncompleteCopy(directory);
    return false;
}

CFtpLinkActivation FtpClassifyLinkActivation(bool commandCompleted, bool directoryAccepted,
                                             std::string_view reply) noexcept
{
    if (!commandCompleted)
        return CFtpLinkActivation::Failed;
    if (directoryAccepted)
        return CFtpLinkActivation::Directory;
    if (reply.size() >= 4 && (reply[3] == ' ' || reply[3] == '-') &&
        (reply.substr(0, 3) == "550" || reply.substr(0, 3) == "553"))
        return CFtpLinkActivation::File;
    return CFtpLinkActivation::Failed;
}
