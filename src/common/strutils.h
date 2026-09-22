// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

// macro SAFE_ALLOC removes code that tests whether memory allocation succeeded (see allochan.*)

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
