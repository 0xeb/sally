// SPDX-FileCopyrightText: 2026 Open Salamander Authors
// SPDX-License-Identifier: GPL-2.0-or-later

// Standalone implementations for SalGetFullNameW testing.
// Provides: SalRemovePointsFromPath(WCHAR*), AnsiToWide, DefaultDir stub,
// SalGetFullNameW, and required constants.

#include <windows.h>
#include <string>
#include <cwctype>

// Constants matching the main codebase
#define SAL_MAX_LONG_PATH 32767

// Error IDs (must match salamand.rh / texts.rh2)
#define IDS_PATHISINVALID 5501
#define IDS_SERVERNAMEMISSING 5502
#define IDS_SHARENAMEMISSING 5503
#define IDS_INVALIDDRIVE 5504
#define IDS_INCOMLETEFILENAME 5505
#define IDS_TOOLONGPATH 5506
#define IDS_EMPTYNAMENOTALLOWED 5507

// Stub DefaultDir — per-drive default directory
char DefaultDir['z' - 'a' + 1][SAL_MAX_LONG_PATH];

// AnsiToWide helper
std::wstring AnsiToWide(const char* ansi)
{
    if (ansi == NULL || *ansi == 0)
        return std::wstring();
    int len = MultiByteToWideChar(CP_ACP, 0, ansi, -1, NULL, 0);
    if (len <= 0)
        return std::wstring();
    std::wstring result(len - 1, L'\0');
    MultiByteToWideChar(CP_ACP, 0, ansi, -1, &result[0], len);
    return result;
}

// Wide SalRemovePointsFromPath — removes . and .. from path
BOOL SalRemovePointsFromPath(WCHAR* afterRoot)
{
    WCHAR* d = afterRoot;
    while (*d != 0)
    {
        while (*d != 0 && *d != L'.')
            d++;
        if (*d == L'.')
        {
            if (d == afterRoot || d > afterRoot && *(d - 1) == L'\\')
            {
                if (*(d + 1) == L'.' && (*(d + 2) == L'\\' || *(d + 2) == 0))
                {
                    WCHAR* l = d - 1;
                    while (l > afterRoot && *(l - 1) != L'\\')
                        l--;
                    if (l >= afterRoot)
                    {
                        if (*(d + 2) == 0)
                            *l = 0;
                        else
                            memmove(l, d + 3, sizeof(WCHAR) * (lstrlenW(d + 3) + 1));
                        d = l;
                    }
                    else
                        return FALSE;
                }
                else
                {
                    if (*(d + 1) == L'\\' || *(d + 1) == 0)
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

// SalGetFullNameW — the function under test
BOOL SalGetFullNameW(std::wstring& name, int* errTextID, const wchar_t* curDir,
                     std::wstring* nextFocus, BOOL* callNethood,
                     BOOL allowRelPathWithSpaces)
{
    int err = 0;

    int rootOffset = 3;
    size_t sOff = 0;

    while (sOff < name.length() && name[sOff] >= 1 && name[sOff] <= L' ')
        sOff++;

    if (sOff < name.length() && name[sOff] == L'\\' && sOff + 1 < name.length() && name[sOff + 1] == L'\\')
    {
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
                    sOff++;
                if (sOff < name.length() && name[sOff] == L'\\')
                    sOff++;
                if (sOff > SAL_MAX_LONG_PATH - 1)
                    err = IDS_SERVERNAMEMISSING;
                else
                {
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
                            sOff++;
                        if (sOff + 1 > SAL_MAX_LONG_PATH - 1)
                            err = IDS_SHARENAMEMISSING;
                        if (sOff < name.length() && name[sOff] == L'\\')
                            sOff++;
                    }
                }
            }
        }
    }
    else
    {
        if (sOff < name.length())
        {
            wchar_t ch = name[sOff];
            if (sOff + 1 < name.length() && name[sOff + 1] == L':')
            {
                if (sOff + 2 < name.length() && name[sOff + 2] == L'\\')
                {
                    if (sOff > 0)
                        name.erase(0, sOff);
                }
                else
                {
                    std::wstring remainder = name.substr(sOff + 2);
                    wchar_t lower = (wchar_t)towlower(ch);
                    if (lower >= L'a' && lower <= L'z')
                    {
                        std::wstring head;
                        if (curDir != NULL && (wchar_t)towlower(curDir[0]) == lower)
                            head = curDir;
                        else
                            head = AnsiToWide(DefaultDir[lower - L'a']);
                        if (!head.empty() && head.back() != L'\\')
                            head += L'\\';
                        if (head.length() + remainder.length() >= SAL_MAX_LONG_PATH)
                            err = IDS_TOOLONGPATH;
                        else
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

                    if (!tail.empty() && tail[0] == L'\\')
                    {
                        std::wstring curDirW = curDir;
                        if (curDirW.length() >= 2 && curDirW[0] == L'\\' && curDirW[1] == L'\\')
                        {
                            size_t root = 2;
                            while (root < curDirW.length() && curDirW[root] != L'\\')
                                root++;
                            root++;
                            while (root < curDirW.length() && curDirW[root] != L'\\')
                                root++;
                            if (tail.length() + root >= SAL_MAX_LONG_PATH)
                                err = IDS_TOOLONGPATH;
                            else
                                name = curDirW.substr(0, root) + tail;
                            rootOffset = (int)root + 1;
                        }
                        else
                        {
                            if (tail.length() + 2 >= SAL_MAX_LONG_PATH)
                                err = IDS_TOOLONGPATH;
                            else
                            {
                                name.clear();
                                name += curDirW[0];
                                name += L':';
                                name += tail;
                            }
                        }
                    }
                    else
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
                        if (tail.length() + curDirW.length() >= SAL_MAX_LONG_PATH)
                            err = IDS_TOOLONGPATH;
                        else
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

    if (err == 0)
    {
        size_t len = name.length();
        wchar_t* buf = new wchar_t[len + 1];
        memcpy(buf, name.c_str(), (len + 1) * sizeof(wchar_t));
        if (sOff <= len)
        {
            if (!SalRemovePointsFromPath(buf + sOff))
                err = IDS_PATHISINVALID;
            else
                name = buf;
        }
        delete[] buf;
    }

    if (err == 0)
    {
        size_t l = name.length();
        if (l > 1 && name[1] == L':')
        {
            if (l > 3)
            {
                if (name[l - 1] == L'\\')
                    name.resize(l - 1);
            }
            else
            {
                name.resize(3);
                name[2] = L'\\';
            }
        }
        else
        {
            if (l >= 7 && name[0] == L'\\' && name[1] == L'\\' && name[2] == L'.' && name[3] == L'\\' &&
                name[4] != 0 && name[5] == L':')
            {
                if (l > 7)
                {
                    if (name[l - 1] == L'\\')
                        name.resize(l - 1);
                }
                else
                {
                    name.resize(7);
                    name[6] = L'\\';
                }
            }
            else
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
