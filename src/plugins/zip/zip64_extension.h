// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "typecons.h"

enum class Zip64ExtensionResult
{
    Absent,
    Plain,
    Unsupported,
    ReadError,
};

// The extensible record is smaller than the basic record. The reader must use
// its own type's size, and must leave subsequent locator/EOCD bytes untouched.
template <typename Reader>
Zip64ExtensionResult ReadZip64Extension(QWORD recordedSize, Reader&& read)
{
    constexpr QWORD minimumSize = sizeof(CZip64EOCentrDirRecord) +
                                  sizeof(CZip64EOCentrDirRecordVer2) -
                                  sizeof(CZip64EOCentrDirRecord::Signature) -
                                  sizeof(CZip64EOCentrDirRecord::Size);
    if (recordedSize < minimumSize)
        return Zip64ExtensionResult::Absent;
    CZip64EOCentrDirRecordVer2 extension = {};
    if (!read(&extension, static_cast<unsigned>(sizeof(extension))))
        return Zip64ExtensionResult::ReadError;
    return extension.Method == CM_STORED && extension.AlgID == 0
               ? Zip64ExtensionResult::Plain
               : Zip64ExtensionResult::Unsupported;
}
