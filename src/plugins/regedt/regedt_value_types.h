// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <windows.h>
#include <cwchar>

struct CRegTypeText
{
    const wchar_t* Text;
    DWORD Type;
    unsigned CanCreate : 1;
    unsigned CanEdit : 1;
};

enum CValDlgType
{
    vdtNewValDialog,
    vdtEditValDialog,
    vdtRawEditValDialog
};

inline bool PopulateRegistryTypeCombo(HWND combo, const CRegTypeText* types, CValDlgType mode, DWORD current)
{
    SendMessageW(combo, CB_RESETCONTENT, 0, 0);
    bool includedCurrent = false;
    const auto add = [&](const wchar_t* text, DWORD type)
    {
        const LRESULT index = SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(text));
        return index != CB_ERR && index != CB_ERRSPACE &&
               SendMessageW(combo, CB_SETITEMDATA, index, static_cast<LPARAM>(type)) != CB_ERR;
    };
    for (const auto* entry = types; entry->Text; ++entry)
    {
        const bool include = mode == vdtEditValDialog ? entry->CanEdit : entry->CanCreate || (mode == vdtRawEditValDialog && entry->Type == current);
        if (include)
        {
            if (!add(entry->Text, entry->Type))
                return false;
            includedCurrent |= entry->Type == current;
        }
    }
    if (mode == vdtRawEditValDialog && !includedCurrent)
    {
        // A DWORD type has exactly eight hexadecimal digits. Unknown types must
        // remain selectable without interpreting their opaque bytes as a string.
        wchar_t text[11];
        swprintf_s(text, L"0x%08X", current);
        if (!add(text, current))
            return false;
    }
    const LRESULT count = SendMessageW(combo, CB_GETCOUNT, 0, 0);
    for (LRESULT index = 0; index < count; ++index)
        if (static_cast<DWORD>(SendMessageW(combo, CB_GETITEMDATA, index, 0)) == current)
            return SendMessageW(combo, CB_SETCURSEL, index, 0) != CB_ERR;
    return mode == vdtNewValDialog && SendMessageW(combo, CB_SETCURSEL, 0, 0) != CB_ERR;
}
