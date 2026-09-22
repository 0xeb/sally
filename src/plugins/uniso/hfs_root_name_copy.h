// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <string>

// extracted from CHFS::GetRootName so its buffer-fit decision can be
// unit-tested directly (same shape as udf_ostacompress.h/renamer_name_narrow.h - zero dependency
// on precomp.h/the Salamander SDK).
//
// CHFS::GetRootName previously narrowed the HFS+ root catalog key's already-correctly-decoded
// wide name (HFSPlusCatalogKey::nodeName, a real HFSUniStr255 Unicode string per the HFS+ spec -
// not restricted-charset disk metadata) via an unguarded WideCharToMultiByte(CP_ACP,
// WC_COMPOSITECHECK, ...) - WC_COMPOSITECHECK does not prevent best-fit substitution, the same
// non-guarding flag mistake already fixed in unrar's ReadHeader. The corrupted narrow name was
// then re-widened by udfiso.cpp's CUDFISO::ListDirectory to build the virtual "\HFS (name)"
// directory a user navigates into - a real, navigable panel path component, not just diagnostic
// text. Fixed by keeping the name wide throughout: this helper just replaces the narrowing call
// with a bounds-checked copy of the name the caller already decoded correctly.

// Copies 'nameLen' wide code units from 'decodedName' (NOT NUL-terminated - HFSPlusCatalogKey's
// nodeName carries an explicit length) into dynamic ownership. Publication is transactional so an
// invalid input or allocation failure leaves the caller's previous value untouched.
inline bool CopyHfsRootNameOwned(const wchar_t* decodedName, int nameLen, std::wstring& result) noexcept
{
    if (decodedName == nullptr || nameLen < 0)
        return false;

    try
    {
        std::wstring staged(decodedName, static_cast<size_t>(nameLen));
        result.swap(staged);
        return true;
    }
    catch (...)
    {
        return false;
    }
}
