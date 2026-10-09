// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

// What the Change Attributes dialog asks for, as attribute masks.
//
// Every attribute check box of the dialog is tri-state: 0 clears the attribute,
// 1 sets it, 2 (the mixed state of a selection whose items differ) leaves it as
// each item has it. Compression and encryption are not plain attribute bits: the
// worker has to compress/uncompress or encrypt/decrypt the item, so a 0 or 1 in
// either box also asks for that work.
//
// Header-only and free of panel state, so the mapping can be reasoned about on
// its own.

#pragma once

#include <windows.h>

namespace sally
{
namespace attrs
{

enum : int
{
    kClear = 0,
    kSet = 1,
    kKeep = 2,
};

struct ChangeAttrsChoice
{
    int Archive = kKeep;
    int ReadOnly = kKeep;
    int Hidden = kKeep;
    int System = kKeep;
    int Compressed = kKeep;
    int Encrypted = kKeep;
    BOOL RecurseSubDirs = FALSE;
};

struct ChangeAttrsRequest
{
    DWORD AttrAnd = 0xFFFFFFFF;
    DWORD AttrOr = 0;
    BOOL SubDirs = FALSE;
    BOOL ChangeCompression = FALSE;
    BOOL ChangeEncryption = FALSE;
    // The dialog offered a combination NTFS cannot hold (compressed and encrypted
    // together); NormalizeCompressionAndEncryption corrected it.
    bool CorrectedCompressionEncryption = false;
};

// A file cannot be both compressed and encrypted. Encryption wins, as it always
// has in this dialog: setting it forces Compressed off, and setting Compressed
// forces Encrypted off. Returns true when it had to correct a contradictory pair
// (the dialog's own check box logic should already prevent one).
inline bool NormalizeCompressionAndEncryption(ChangeAttrsChoice& choice)
{
    bool corrected = false;
    if (choice.Encrypted == kSet)
    {
        corrected = choice.Compressed != kClear;
        choice.Compressed = kClear;
    }
    else if (choice.Compressed == kSet)
    {
        corrected = choice.Encrypted != kClear;
        choice.Encrypted = kClear;
    }
    return corrected;
}

inline void ApplyTriState(int state, DWORD attribute, DWORD& attrAnd, DWORD& attrOr)
{
    if (state == kClear)
        attrAnd &= ~attribute;
    else if (state == kSet)
        attrOr |= attribute;
}

// Maps the dialog's answer to the masks the operation script applies:
// NewAttrs = (CurrentAttrs & AttrAnd) | AttrOr.
inline ChangeAttrsRequest MapChangeAttrsChoice(ChangeAttrsChoice choice)
{
    ChangeAttrsRequest request;
    request.CorrectedCompressionEncryption = NormalizeCompressionAndEncryption(choice);
    request.SubDirs = choice.RecurseSubDirs;

    ApplyTriState(choice.Archive, FILE_ATTRIBUTE_ARCHIVE, request.AttrAnd, request.AttrOr);
    ApplyTriState(choice.ReadOnly, FILE_ATTRIBUTE_READONLY, request.AttrAnd, request.AttrOr);
    ApplyTriState(choice.Hidden, FILE_ATTRIBUTE_HIDDEN, request.AttrAnd, request.AttrOr);
    ApplyTriState(choice.System, FILE_ATTRIBUTE_SYSTEM, request.AttrAnd, request.AttrOr);
    ApplyTriState(choice.Compressed, FILE_ATTRIBUTE_COMPRESSED, request.AttrAnd, request.AttrOr);
    ApplyTriState(choice.Encrypted, FILE_ATTRIBUTE_ENCRYPTED, request.AttrAnd, request.AttrOr);

    request.ChangeCompression = choice.Compressed != kKeep;
    request.ChangeEncryption = choice.Encrypted != kKeep;
    return request;
}

} // namespace attrs
} // namespace sally
