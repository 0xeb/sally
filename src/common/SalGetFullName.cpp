// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

// SalGetFullNameW + SalRemovePointsFromPath. Extracted from
// sally_path_utils.cpp so production and the private tests compile the same
// translation unit.
//
// Host seam: SalGetDefaultDirForDrive (see SalGetFullName.h) — production
// implements it over the DefaultDir table (sally_path_utils.cpp); tests
// provide a stub.

#ifdef SALLY_WORKER_CORE_STANDALONE
#include "common/WorkerCoreStandalone.h"
#include "texts.rh2"
#else
#include "precomp.h"
#endif

#include <cwctype>
#include <string>
#include <vector>

#include "common/SalGetFullName.h"
#include "common/unicode/helpers.h"

// A second definition of SalRemovePointsFromPath stood here,
// spelled wchar_t* where the surviving one below is spelled WCHAR* - the same
// signature, so the pair was a redefinition. It was also the wrong body: its
// two memmove calls sized themselves with strlen() on a wchar_t* and omitted
// sizeof(wchar_t) entirely. The WCHAR* body below is the correct one and is
// what every caller was already meant to reach.
BOOL SalRemovePointsFromPath(WCHAR* afterRoot)
{
    WCHAR* d = afterRoot; // pointer after root path
    while (*d != 0)
    {
        while (*d != 0 && *d != L'.')
            d++;
        if (*d == L'.')
        {
            if (d == afterRoot || d > afterRoot && *(d - 1) == L'\\') // '.' after root path or "\."
            {
                if (*(d + 1) == L'.' && (*(d + 2) == L'\\' || *(d + 2) == 0)) // ".."
                {
                    WCHAR* l = d - 1;
                    while (l > afterRoot && *(l - 1) != L'\\')
                        l--;
                    if (l >= afterRoot) // remove directory + ".."
                    {
                        if (*(d + 2) == 0)
                            *l = 0;
                        else
                            memmove(l, d + 3, sizeof(WCHAR) * (lstrlenW(d + 3) + 1));
                        d = l;
                    }
                    else
                        return FALSE; // ".." cannot be removed
                }
                else
                {
                    if (*(d + 1) == L'\\' || *(d + 1) == 0) // "."
                    {
                        if (*(d + 1) == 0)
                            *d = 0;
                        else
                            memmove(d, d + 2, sizeof(WCHAR) * (lstrlenW(d + 2) + 1));
                    }
                    else
                        d++;
                }
            }
            else
                d++;
        }
    }
    return TRUE;
}

// Wide version of SalGetFullName
BOOL SalGetFullNameW(std::wstring& name, int* errTextID, const wchar_t* curDir,
                     std::wstring* nextFocus, BOOL* callNethood,
                     BOOL allowRelPathWithSpaces)
{
    int err = 0;

    size_t rootOffset = 3; // offset of directory part start (3 for "c:\path")
    size_t sOff = 0;    // offset into name (replaces pointer arithmetic)

    // Skip leading whitespace
    while (sOff < name.length() && name[sOff] >= 1 && name[sOff] <= L' ')
        sOff++;

    if (sOff < name.length() && name[sOff] == L'\\' && sOff + 1 < name.length() && name[sOff + 1] == L'\\')
    {
        // UNC (\\server\share\...)
        if (sOff > 0)
            name.erase(0, sOff);
        sOff = 2;
        if (sOff < name.length() && (name[sOff] == L'.' || name[sOff] == L'?'))
            err = IDS_PATHISINVALID;
        else
        {
            if (sOff >= name.length() || name[sOff] == L'\\')
            {
                if (callNethood != NULL)
                    *callNethood = (sOff >= name.length());
                err = IDS_SERVERNAMEMISSING;
            }
            else
            {
                while (sOff < name.length() && name[sOff] != L'\\')
                    sOff++; // skip servername
                if (sOff < name.length() && name[sOff] == L'\\')
                    sOff++;
                if (sOff >= name.length() || name[sOff] == L'\\')
                {
                    if (callNethood != NULL)
                        *callNethood = (sOff >= name.length()) &&
                                       (sOff < 2 || name[sOff - 1] != L'.' || name[sOff - 2] != L'\\') &&
                                       (sOff < 3 || name[sOff - 1] != L'\\' || name[sOff - 2] != L'.' || name[sOff - 3] != L'\\');
                    err = IDS_SHARENAMEMISSING;
                }
                else
                {
                    while (sOff < name.length() && name[sOff] != L'\\')
                        sOff++; // skip sharename
                    if (sOff < name.length() && name[sOff] == L'\\')
                        sOff++;
                }
            }
        }
    }
    else // path specified using drive (c:\...)
    {
        if (sOff < name.length())
        {
            wchar_t ch = name[sOff];
            if (sOff + 1 < name.length() && name[sOff + 1] == L':') // "c:..."
            {
                if (sOff + 2 < name.length() && name[sOff + 2] == L'\\') // "c:\..."
                {
                    if (sOff > 0)
                        name.erase(0, sOff);
                }
                else // "c:path..."
                {
                    std::wstring remainder = name.substr(sOff + 2);
                    wchar_t lower = (wchar_t)towlower(ch);
                    if (lower >= L'a' && lower <= L'z')
                    {
                        std::wstring head;
                        if (curDir != NULL && (wchar_t)towlower(curDir[0]) == lower)
                            head = curDir;
                        else
                            head = SalGetDefaultDirForDrive(lower);
                        if (!head.empty() && head.back() != L'\\')
                            head += L'\\';
                        name = head + remainder;
                    }
                    else
                        err = IDS_INVALIDDRIVE;
                }
            }
            else
            {
                if (curDir != NULL)
                {
                    if (allowRelPathWithSpaces && sOff < name.length() && name[sOff] != L'\\')
                        sOff = 0;
                    std::wstring tail = name.substr(sOff);

                    if (!tail.empty() && tail[0] == L'\\') // "\path...."
                    {
                        std::wstring curDirW = curDir;
                        if (curDirW.length() >= 2 && curDirW[0] == L'\\' && curDirW[1] == L'\\') // UNC
                        {
                            size_t root = 2;
                            while (root < curDirW.length() && curDirW[root] != L'\\')
                                root++;
                            root++; // '\\'
                            while (root < curDirW.length() && curDirW[root] != L'\\')
                                root++;
                            name = curDirW.substr(0, root) + tail;
                            rootOffset = root + 1;
                        }
                        else
                        {
                            name.clear();
                            name += curDirW[0];
                            name += L':';
                            name += tail;
                        }
                    }
                    else // "path..."
                    {
                        if (nextFocus != NULL)
                        {
                            size_t bsPos = tail.find(L'\\');
                            if (bsPos == std::wstring::npos)
                                *nextFocus = tail;
                        }

                        std::wstring curDirW = curDir;
                        if (!curDirW.empty() && curDirW.back() != L'\\')
                            curDirW += L'\\';
                        name = curDirW + tail;
                    }
                }
                else
                    err = IDS_INCOMLETEFILENAME;
            }
            sOff = rootOffset;
        }
        else
        {
            name.clear();
            err = IDS_EMPTYNAMENOTALLOWED;
        }
    }

    if (err == 0) // eliminate '.' and '..' in path
    {
        // SalRemovePointsFromPath works in-place on a null-terminated buffer.
        size_t len = name.length();
        std::vector<wchar_t> buffer(name.begin(), name.end());
        buffer.push_back(L'\0');
        if (sOff <= len)
        {
            if (!SalRemovePointsFromPath(buffer.data() + sOff))
                err = IDS_PATHISINVALID;
            else
                name = buffer.data();
        }
    }

    if (err == 0) // remove any unwanted backslash from end of string
    {
        size_t l = name.length();
        if (l > 1 && name[1] == L':') // path type "c:\path"
        {
            if (l > 3) // not a root path
            {
                if (name[l - 1] == L'\\')
                    name.resize(l - 1);
            }
            else
            {
                name.resize(3);
                name[2] = L'\\'; // root path, backslash required ("c:\")
            }
        }
        else
        {
            if (l >= 7 && name[0] == L'\\' && name[1] == L'\\' && name[2] == L'.' && name[3] == L'\\' &&
                name[4] != 0 && name[5] == L':') // path type "\\.\C:\"
            {
                if (l > 7) // not a root path
                {
                    if (name[l - 1] == L'\\')
                        name.resize(l - 1);
                }
                else
                {
                    name.resize(7);
                    name[6] = L'\\'; // root path, backslash required ("\\.\C:\")
                }
            }
            else // UNC path
            {
                if (l > 0 && name[l - 1] == L'\\')
                    name.resize(l - 1);
            }
        }
    }

    if (errTextID != NULL)
        *errTextID = err;

    return err == 0;
}
