// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <cstdint>
#include <cstring>
#include <string>

// Raw DWORD-wise comparator over the packed sort keys of CIconCache (CIconData::NameAndData)
// and CAssociations (CAssociationData::ExtensionAndData). It is NOT lexicographic (it compares
// four bytes at a time as a little-endian DWORD) -- it is a consistent arbitrary total order,
// which is all the sort and the binary search need, provided both use it. 'lengthBytes' is the
// BYTE length of the first key, derived from wcslen().
//
// The loop reads a whole DWORD AT the end offset too, so for a key of an even number of
// characters it reads the terminator plus one more wchar_t. Stored keys are allocated
// DWORD-aligned with zero padding; LOOKUP keys must come from MakeDwordCompareKey.
inline int CompareDWORDS(const void* p1, const void* p2, int lengthBytes)
{
    const char* s1 = (const char*)p1;
    const char* s2 = (const char*)p2;
    const char* end = s1 + lengthBytes;
    while (s1 <= end)
    {
        std::uint32_t d1, d2;
        std::memcpy(&d1, s1, sizeof(d1));
        std::memcpy(&d2, s2, sizeof(d2));
        //    if ((res = d1 - d2) != 0) return res;  // this doesn't work (try 0x8 and 0x0 in 4-bit numbers)
        if (d1 > d2)
            return 1;
        if (d1 < d2)
            return -1;
        s1 += sizeof(std::uint32_t);
        s2 += sizeof(std::uint32_t);
    }
    return 0;
}

// #115: a lookup key for CIconCache::GetIndex or CAssociations::GetIndex. std::wstring
// guarantees only one wide NUL, so an even-length name left CompareDWORDS reading an
// uninitialized wchar_t past it: the lookup then failed at random and the panel painted the
// generic icon instead of the file's own, changing from one repaint to the next. Two wide NULs
// make that final DWORD zero, as the pre-Unicode code did with *(DWORD*)(name + len) = 0.
inline std::wstring MakeDwordCompareKey(const wchar_t* text, size_t length)
{
    std::wstring key(text, length);
    key.append(2, L'\0');
    return key;
}

inline std::wstring MakeDwordCompareKey(const std::wstring& text)
{
    return MakeDwordCompareKey(text.data(), text.size());
}
