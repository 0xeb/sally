// Helpers for edit controls that hold a password or key passphrase.
#pragma once

#include <vector>

#include "secret.h"

// The edit text (UTF-16) as UTF-8 CSecret; the temporary UTF-16 buffer is wiped.
inline CSecret ReadSecretFromEdit(HWND edit)
{
    CSecret secret;
    const int length = GetWindowTextLengthW(edit);
    if (length <= 0)
        return secret;
    std::vector<wchar_t> wide((size_t)length + 1, L'\0');
    GetWindowTextW(edit, wide.data(), length + 1);
    const int bytes = WideCharToMultiByte(CP_UTF8, 0, wide.data(), length, nullptr, 0, nullptr, nullptr);
    if (bytes > 0)
    {
        secret.resize((size_t)bytes);
        WideCharToMultiByte(CP_UTF8, 0, wide.data(), length, secret.data(), bytes, nullptr, nullptr);
    }
    SecureZeroMemory(wide.data(), wide.size() * sizeof(wchar_t));
    return secret;
}

// Best effort: the control's own buffer is replaced by an empty text.
inline void WipeEdit(HWND edit)
{
    if (edit != nullptr)
        SetWindowTextW(edit, L"");
}
