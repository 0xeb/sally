// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"

#include "config.h"
#include "../registry_names.h"

const wchar_t* DB_ROOT_KEY = SAL_REG_KEY_BUG_REPORTER_DB_W;
const wchar_t* CONFIG_EMAIL_REG = L"Email";

CConfiguration Config;

//*****************************************************************************
//
// CConfiguration
//

void CConfiguration::ForgetEmail()
{
    HKEY hKey;
    if (HANDLES_Q(RegOpenKeyExW(HKEY_CURRENT_USER, DB_ROOT_KEY, 0, KEY_SET_VALUE, &hKey)) == ERROR_SUCCESS)
    {
        RegDeleteValueW(hKey, CONFIG_EMAIL_REG);
        HANDLES(RegCloseKey(hKey));
    }
}
