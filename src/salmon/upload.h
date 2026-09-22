// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <string>

// structure passed to the upload thread, used to transfer input/output parameters
struct CUploadParams
{
    BOOL Result = FALSE;
    std::wstring ErrorMessage;
    std::wstring FileName;
};

BOOL StartUploadThread(CUploadParams* params);
BOOL IsUploadThreadRunning();
