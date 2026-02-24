// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later
// CommentsTranslationProject: TRANSLATED

#pragma once

// macro SAFE_ALLOC removes code that tests whether memory allocation succeeded (see allochan.*)

// conversion of Unicode string (UTF-16) to ANSI multibyte string; 'src' is Unicode string;
// 'srcLen' is length of Unicode string (without null terminator; if -1 is passed, the length is determined
// according to null terminator); 'bufSize' (must be greater than 0) is the size of target buffer
// 'buf' for ANSI string; if 'compositeCheck' is TRUE, it uses WC_COMPOSITECHECK flag
// (see MSDN), must not be used for file names (NTFS distinguishes names written as
// precomposed and composite, i.e., does not normalize names); 'codepage' is code page
// of ANSI string; returns number of characters written to 'buf' (including null terminator); on error
// returns zero (details see GetLastError()); always ensures null-terminated 'buf' (even on error);
// if 'buf' is small, the function returns zero, but at least part of the string is converted to 'buf'
int ConvertU2A(const WCHAR* src, int srcLen, char* buf, int bufSize,
               BOOL compositeCheck = FALSE, UINT codepage = CP_ACP);

// conversion of Unicode string (UTF-16) to allocated ANSI multibyte string (caller is
// responsible for deallocation of string); 'src' is Unicode string; 'srcLen' is length of Unicode
// string (without null terminator; if -1 is passed, the length is determined according to null terminator);
// if 'compositeCheck' is TRUE, it uses WC_COMPOSITECHECK flag (see MSDN), must not be used
// for file names (NTFS distinguishes names written as precomposed and composite, i.e.,
// does not normalize names); 'codepage' is code page of ANSI string; returns allocated
// ANSI string; on error returns NULL (details see GetLastError())
char* ConvertAllocU2A(const WCHAR* src, int srcLen, BOOL compositeCheck = FALSE, UINT codepage = CP_ACP);

// conversion of ANSI multibyte string to Unicode string (UTF-16); 'src' is ANSI string;
// 'srcLen' is length of ANSI string (without null terminator; if -1 is passed, the length is determined
// according to null terminator); 'bufSize' (must be greater than 0) is the size of target buffer
// 'buf' for Unicode string; 'codepage' is code page of ANSI string;
// returns number of characters written to 'buf' (including null terminator); on error returns zero
// (details see GetLastError()); always ensures null-terminated 'buf' (even on error);
// if 'buf' is small, the function returns zero, but at least part of the string is converted to 'buf'
int ConvertA2U(const char* src, int srcLen, WCHAR* buf, int bufSizeInChars,
               UINT codepage = CP_ACP);

// conversion of ANSI multibyte string to allocated (caller is responsible for deallocation
// of string) Unicode string (UTF-16); 'src' is ANSI string; 'srcLen' is length of ANSI
// string (without null terminator; if -1 is passed, the length is determined according to null terminator);
// 'codepage' is code page of ANSI string; returns allocated Unicode string; on
// error returns NULL (details see GetLastError())
WCHAR* ConvertAllocA2U(const char* src, int srcLen, UINT codepage = CP_ACP);

// copies string 'txt' to newly allocated string, NULL = not enough memory (can only happen if
// allochan.* is not used) or 'txt'==NULL
WCHAR* DupStr(const WCHAR* txt);

// holds pointer to allocated memory, takes care of its deallocation when overwritten by another pointer to
// allocated memory and during its destruction
template <class PTR_TYPE>
class CAllocP
{
public:
    PTR_TYPE* Ptr;

public:
    CAllocP(PTR_TYPE* ptr = NULL) { Ptr = ptr; }
    ~CAllocP()
    {
        if (Ptr != NULL)
            free(Ptr);
    }

    PTR_TYPE* GetAndClear()
    {
        PTR_TYPE* p = Ptr;
        Ptr = NULL;
        return p;
    }

    operator PTR_TYPE*() { return Ptr; }
    PTR_TYPE* operator=(PTR_TYPE* p)
    {
        if (Ptr != NULL)
            free(Ptr);
        return Ptr = p;
    }
};

// holds allocated string, takes care of deallocation when overwritten by another string (also allocated)
// and during its destruction
typedef CAllocP<WCHAR> CStrP;
