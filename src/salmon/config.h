// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <string>

//*****************************************************************************
//
// CConfiguration
//

class CConfiguration
{
public:
    std::wstring Description; // the user's "Last action" text, not stored

public:
    // Older versions asked for a contact e-mail address and kept it in the registry.
    void ForgetEmail();
};

extern CConfiguration Config;
