// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <string>

// structure passed to the minidump thread, used to transfer input/output parameters
struct CMinidumpParams
{
    BOOL Result = FALSE;
    std::wstring ErrorMessage;
};

BOOL StartMinidumpThread(CMinidumpParams* params);
BOOL IsMinidumpThreadRunning();
