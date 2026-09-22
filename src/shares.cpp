// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"
#include <lm.h>

#include "common/SalGetFullName.h"    // SalGetFullNameW
#include "common/SalPathWide.h"       // SalPathAddBackslashW

//****************************************************************************
//
// CSharesItem
//

CSharesItem::CSharesItem(const wchar_t* localPath, const wchar_t* remoteName, const wchar_t* comment)
{
    Cleanup();

    if (localPath != NULL && localPath[0] != 0 && localPath[1] == L':')
    {
        std::wstring buff(localPath);
        SalPathAddBackslashW(buff);       // in case it's just L"c:", so that root is created
        if (SalGetFullNameW(buff))        // root "c:\\", others without '\\' at the end
        {
            LocalPathW = buff;
            RemoteNameW = remoteName != NULL ? remoteName : L"";
            CommentW = comment != NULL ? comment : L"";

            const size_t slash = LocalPathW.find_last_of(L'\\');
            if (slash == std::wstring::npos || slash + 1 >= LocalPathW.length())
                LocalNameOffsetW = 0; // root path; npos is just for safety, but can never occur
            else
                LocalNameOffsetW = slash + 1;
        }
        else
            TRACE_E("Unexpected path (1) in CSharesItem::CSharesItem()");
    }
    else
        TRACE_E("Unexpected path (2) in CSharesItem::CSharesItem()");
}

CSharesItem::~CSharesItem()
{
    // Nothing to release: the three std::wstrings own their storage.
    // Destroy() existed only to free the deleted second mirror's malloc'd copies.
}

void CSharesItem::Cleanup()
{
    LocalPathW.clear();
    RemoteNameW.clear();
    CommentW.clear();
    LocalNameOffsetW = 0;
}

//****************************************************************************
//
// CShares
//

/* Share loading section */

void CShares::Refresh()
{
    HANDLES(EnterCriticalSection(&CS));
    Wanted.DestroyMembers();
    Data.DestroyMembers();

    PSHARE_INFO_502 BufPtr, p;
    NET_API_STATUS res;
    DWORD er = 0, tr = 0, resume = 0, i;

    do
    {
        res = NetShareEnum(NULL, 502, (LPBYTE*)&BufPtr, -1, &er, &tr, &resume);
        if (res == ERROR_SUCCESS || res == ERROR_MORE_DATA)
        {
            p = BufPtr;
            for (i = 1; i <= er; i++)
            {
                // we don't want special shares because Explorer doesn't show them
                BOOL include = p->shi502_type == 0;
                if (!SubsetOnly && p->shi502_type == 0x80000000) // special
                    include = TRUE;
                // These three fields are LPWSTR - NetShareEnum has no ANSI form.
                // They used to be run through WideCharToMultiByte(CP_ACP, 0, ...) right here,
                // with best-fit on and the return value used only as a success flag, so a share
                // whose name or path the code page could not spell entered the list under a
                // '?'-mangled name. It then failed to match the real directory (no share overlay
                // in the panel) or matched the WRONG one, and Copy UNC Name built a UNC path
                // nobody can open. The wide value now goes in unmodified.
                if (include && p->shi502_netname != NULL && p->shi502_path != NULL)
                {
                    //              TRACE_I("Share: " << netname << " = " << path);
                    // adding the shared path to the Data array
                    CSharesItem* item = new CSharesItem(p->shi502_path, p->shi502_netname,
                                                        p->shi502_remark != NULL ? p->shi502_remark : L"");
                    if (item != NULL && item->IsGood())
                    {
                        Data.Add(item);
                        if (Data.IsGood())
                            item = NULL; // successfully added
                        else
                        {
                            delete item;
                            Data.ResetState();
                            Data.DestroyMembers();
                            break; // error, no point continuing with enum
                        }
                    }
                    if (item != NULL)
                        delete item;
                }
                p++;
            }
            NetApiBufferFree(BufPtr);
        }
        else
            TRACE_IW(L"Error getting shares: (" << res << L") " << GetErrorTextOwned(res).c_str());
    } while (res == ERROR_MORE_DATA);
    HANDLES(LeaveCriticalSection(&CS));
}

CShares::CShares(BOOL subsetOnly)
    : Data(10, 10), Wanted(10, 10, dtNoDelete)
{
    HANDLES(InitializeCriticalSection(&CS));
    SubsetOnly = subsetOnly;
}

CShares::~CShares()
{
    HANDLES(DeleteCriticalSection(&CS));
}

BOOL CShares::GetWantedIndexW(const wchar_t* name, int& index)
{
    if (Wanted.Count == 0)
    {
        index = 0;
        return FALSE;
    }

    int l = 0, r = Wanted.Count - 1, m;
    while (1)
    {
        m = (l + r) / 2;
        const wchar_t* hw = Wanted[m]->GetLocalNameW();
        int res = StrICmpW(hw, name);
        if (res == 0) // found
        {
            index = m;
            return TRUE;
        }
        else
        {
            if (res > 0)
            {
                if (l == r || l > m - 1) // not found
                {
                    index = m; // should be at this position
                    return FALSE;
                }
                r = m - 1;
            }
            else
            {
                if (l == r) // not found
                {
                    index = m + 1; // should be after this position
                    return FALSE;
                }
                l = m + 1;
            }
        }
    }
}

void CShares::PrepareSearchW(const wchar_t* path)
{
    HANDLES(EnterCriticalSection(&CS));
    // empty the Wanted array
    Wanted.DestroyMembers();

    // add only those shares that lie on the requested path
    std::wstring buff(path != NULL ? path : L"");
    if (!buff.empty())              // if searching for shares from this_computer we must not append backslash
        SalPathAddBackslashW(buff); // we want backslash at the end
    int pathLen = (int)buff.length();

    int i;
    for (i = 0; i < Data.Count; i++)
    {
        CSharesItem* item = Data[i];
        int itemNameLen = (int)item->LocalNameOffsetW;
        if (pathLen == itemNameLen && StrNICmpW(item->LocalPathW.c_str(), buff.c_str(), itemNameLen) == 0)
        {
            int index;
            if (!GetWantedIndexW(item->GetLocalNameW(), index)) // add matching share to Wanted array only if not already there
            {
                Wanted.Insert(index, item);
            }
        }
    }
    HANDLES(LeaveCriticalSection(&CS));
}

BOOL CShares::SearchW(const wchar_t* name)
{
    HANDLES(EnterCriticalSection(&CS));
    int index;
    BOOL ret = GetWantedIndexW(name, index);
    HANDLES(LeaveCriticalSection(&CS));
    return ret;
}

BOOL CShares::GetUNCPathW(const wchar_t* path, std::wstring& uncPath)
{
    HANDLES(EnterCriticalSection(&CS));
    const std::wstring original(path != NULL ? path : L"");
    std::wstring buff(original);
    SalPathAddBackslashW(buff); // we want backslash at the end

    int longestIndex = -1; // index into Data array where the longest matching share is located

    int i;
    for (i = 0; i < Data.Count; i++)
    {
        CSharesItem* item = Data[i];
        int itemNameLen = (int)item->LocalPathW.length();
        if (StrNICmpW(buff.c_str(), item->LocalPathW.c_str(), itemNameLen) == 0)
        {
            // look for the longest possible share that still matches the requested 'path'
            if (longestIndex == -1 || (int)Data[longestIndex]->LocalPathW.length() < itemNameLen)
                longestIndex = i;
        }
    }
    if (longestIndex != -1)
    {
        CSharesItem* item = Data[longestIndex];
        // insert the name of our computer
        std::wstring computer(64, L'\0');
        DWORD len = (DWORD)computer.size();
        bool gotComputerName = false;
        for (;;)
        {
            if (GetComputerNameW(computer.data(), &len))
            {
                gotComputerName = true;
                break;
            }
            if (GetLastError() != ERROR_BUFFER_OVERFLOW)
                break;
            computer.resize(len, L'\0');
            len = (DWORD)computer.size();
        }
        if (gotComputerName)
            computer.resize(len);
        else
            computer.clear();
        std::wstring unc = L"\\\\";
        unc += computer;
        unc += L'\\';
        // append the share name
        unc += item->RemoteNameW;
        SalPathAddBackslashW(unc); // we want backslash at the end
        // from the original path, append directories from the share onwards
        if (item->LocalPathW.length() < original.length())
        {
            const wchar_t* s = original.c_str() + item->LocalPathW.length();
            if (*s == L'\\')
                s++; // skip any backslash
            unc += s;
        }
        if (!SalGetFullNameW(unc)) // root "c:\\", others without '\\' at the end
        {
            TRACE_E("Unexpected path in CSharesItem::GetUNCPathW()");
            HANDLES(LeaveCriticalSection(&CS));
            return FALSE;
        }

        uncPath = unc;
        HANDLES(LeaveCriticalSection(&CS));
        return TRUE;
    }
    else
    {
        HANDLES(LeaveCriticalSection(&CS));
        return FALSE;
    }
}

BOOL CShares::GetItemW(int index, const wchar_t** localPath, const wchar_t** remoteName, const wchar_t** comment)
{
    HANDLES(EnterCriticalSection(&CS));
    if (index < 0 || index >= Data.Count)
    {
        TRACE_E("CShares::GetItemW index=" << index);
        HANDLES(LeaveCriticalSection(&CS));
        return FALSE;
    }
    CSharesItem* item = Data[index];
    if (localPath != NULL)
        *localPath = item->LocalPathW.c_str();
    if (remoteName != NULL)
        *remoteName = item->RemoteNameW.c_str();
    if (comment != NULL)
        *comment = item->CommentW.c_str();
    HANDLES(LeaveCriticalSection(&CS));
    return TRUE;
}
