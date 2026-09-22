// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <string>

// structure passed to the compression thread, used to transfer input/output parameters
struct CCompressParams
{
    BOOL Result = FALSE;
    std::wstring ErrorMessage;
};

BOOL StartCompressThread(CCompressParams* params);
BOOL IsCompressThreadRunning();
