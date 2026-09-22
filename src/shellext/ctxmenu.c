// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#include <windows.h>
#include <windowsx.h>
#include <shlobj.h>

#include "lstrfix.h"
#include "..\shexreg.h"
#include "shellext.h"

#ifdef ENABLE_SH_MENU_EXT

DWORD SalGetFileAttributesOwned(const wchar_t* fileName)
{
    SIZE_T fileNameLen = (SIZE_T)lstrlenW(fileName);
    // if the path ends with a space/period we must append '\\', otherwise GetFileAttributes
    // trims the spaces/periods and works with a different path; with files it still does not
    // work, but it is better than retrieving attributes of another file/directory (for
    // "c:\\file.txt   " it works with the name "c:\\file.txt")
    if (fileNameLen > 0 && (fileName[fileNameLen - 1] <= L' ' || fileName[fileNameLen - 1] == L'.') &&
        fileNameLen <= (((SIZE_T)-1) / sizeof(wchar_t)) - 2)
    {
        DWORD result;
        wchar_t* fileNameCopy = (wchar_t*)GlobalAlloc(GMEM_FIXED, (fileNameLen + 2) * sizeof(wchar_t));
        SIZE_T i;
        if (fileNameCopy == NULL)
            return INVALID_FILE_ATTRIBUTES;
        for (i = 0; i < fileNameLen; i++)
            fileNameCopy[i] = fileName[i];
        fileNameCopy[fileNameLen] = L'\\';
        fileNameCopy[fileNameLen + 1] = 0;
        result = GetFileAttributesW(fileNameCopy);
        GlobalFree(fileNameCopy);
        return result;
    }
    else // a regular path, nothing to solve, just call the Windows GetFileAttributes
    {
        return GetFileAttributesW(fileName);
    }
}

BOOL IncFilesDirs(const wchar_t* path, int* files, int* dirs)
{
    DWORD attrs = SalGetFileAttributesOwned(path);
    if (attrs == INVALID_FILE_ATTRIBUTES)
        return FALSE;

    if (attrs & FILE_ATTRIBUTE_DIRECTORY)
        (*dirs)++;
    else
        (*files)++;

    return TRUE;
}

STDMETHODIMP SE_QueryContextMenu(THIS_
                                     HMENU hMenu,
                                 UINT indexMenu,
                                 UINT idCmdFirst,
                                 UINT idCmdLast,
                                 UINT uFlags)
{
    UINT idCmd = idCmdFirst;

    /*
  char buff1[1000];
  wsprintf(buff1, "SE_QueryContextMenu");
  MessageBox(NULL, buff1, "shellext.dll", MB_OK | MB_ICONINFORMATION);
*/

    if ((uFlags & 0x000F) == CMF_NORMAL || (uFlags & CMF_VERBSONLY) || (uFlags & CMF_EXPLORE))
    {
        HMENU hTmpMenu = hMenu;
        BOOL subMenu = FALSE;
        BOOL of;
        BOOL mf;
        BOOL od;
        BOOL md;

        LPDATAOBJECT pDataObj = ((ShellExt*)This)->m_pDataObj;

        int index = 0;
        int itemsCount = 0;
        CShellExtConfigItem* iterator = ShellExtConfigFirst;

        int filesCount = 0;
        int dirsCount = 0;

        FORMATETC formatEtc;
        STGMEDIUM stgMedium;
        formatEtc.cfFormat = CF_HDROP;
        formatEtc.ptd = NULL;
        formatEtc.dwAspect = DVASPECT_CONTENT;
        formatEtc.lindex = -1;
        formatEtc.tymed = TYMED_HGLOBAL;

        stgMedium.tymed = TYMED_HGLOBAL;
        stgMedium.hGlobal = NULL;
        stgMedium.pUnkForRelease = NULL;

        // fetch the list of files and directories that were clicked
        // walk it and find out how many files and how many directories it contains

        if (pDataObj->lpVtbl->GetData(pDataObj, &formatEtc, &stgMedium) == S_OK)
        {
            if (stgMedium.tymed == TYMED_HGLOBAL && stgMedium.hGlobal != NULL)
            {
                HDROP drop = (HDROP)stgMedium.hGlobal;
                UINT count = DragQueryFileW(drop, 0xFFFFFFFF, NULL, 0);
                UINT itemIndex;
                for (itemIndex = 0; itemIndex < count; itemIndex++)
                {
                    UINT length = DragQueryFileW(drop, itemIndex, NULL, 0);
                    if ((SIZE_T)length <= (((SIZE_T)-1) / sizeof(wchar_t)) - 1)
                    {
                        wchar_t* path = (wchar_t*)GlobalAlloc(GMEM_FIXED, ((SIZE_T)length + 1) * sizeof(wchar_t));
                        if (path != NULL)
                        {
                            if (DragQueryFileW(drop, itemIndex, path, length + 1) == length)
                                IncFilesDirs(path, &filesCount, &dirsCount);
                            GlobalFree(path);
                        }
                    }
                }
            }
            ReleaseStgMedium(&stgMedium);
        }

        // then dive into the context menu
        of = filesCount == 1;
        mf = filesCount > 1;
        od = dirsCount == 1;
        md = dirsCount > 1;

        if (ShellExtConfigSubmenu)
        {
            hTmpMenu = CreatePopupMenu();
            subMenu = TRUE;
            indexMenu = 0;
        }

        // iterate through all items and if they meet the condition, add them to the menu
        while (iterator != NULL)
        {
            // this condition is awful; the selection criteria in Salamander need a redesign
            if ((iterator->LogicalAnd && (iterator->OneFile == of) && (iterator->MoreFiles == mf) &&
                 (iterator->OneDirectory == od) && (iterator->MoreDirectories == md)) ||
                (!iterator->LogicalAnd && ((iterator->OneFile == of) || (iterator->MoreFiles == mf) ||
                                           (iterator->OneDirectory == od) || (iterator->MoreDirectories == md))))
            {
                InsertMenuW(hTmpMenu,
                            indexMenu++,
                            MF_STRING | MF_BYPOSITION,
                            idCmd++,
                            iterator->Name);
                iterator->Cmd = index;
                index++;
                itemsCount++;
            }
            iterator = iterator->Next;
        }

        // if there is something in the submenu, insert it into the ContextMenu
        // otherwise remove it
        if (ShellExtConfigSubmenu)
        {
            if (itemsCount > 0)
            {
                InsertMenuW(hMenu,
                            indexMenu,
                            MF_POPUP | MF_BYPOSITION,
                            (UINT_PTR)hTmpMenu,
                            ShellExtConfigSubmenuName);
            }
            else
            {
                // we must clean up after ourselves and destroy the submenu
                DestroyMenu(hMenu);
            }
        }

        //Must return number of menu items we added.
        return itemsCount;
    }
    return NOERROR;
}

STDMETHODIMP SE_InvokeCommand(THIS_
                                  LPCMINVOKECOMMANDINFO lpici)
{
    int index; // index into the linked list starting at ShellExtConfigFirst
    if (SECGetItemIndex(LOWORD(lpici->lpVerb), &index))
    {
        CShellExtConfigItem* item = SECGetItem(index);
        if (item != NULL)
        {
            char buff[1000];
            wsprintf(buff,
                     "SE_InvokeCommand index = %d\nitem ptr = 0x%p\nThe index has to be passed to Salamander so we can retrieve the pointer to the item through it.",
                     index, item);
            MessageBox(NULL, buff, "shellext.dll", MB_OK | MB_ICONINFORMATION);

            // this is where the communication with Salamander will go

            // the list of clicked files can be obtained by the same method
            // that I use in SE_QueryContextMenu
            // problem: it can arrive either in Unicode or ANSI
            // so I am converting it there and maybe it would be better to leave the conversion
            // to Salamander so that we have it in Unicode

            return NOERROR;
        }
    }
    return E_INVALIDARG;
}

STDMETHODIMP SE_GetCommandString(THIS_
                                     UINT idCmd,
                                 UINT uType,
                                 UINT* pwReserved,
                                 LPSTR pszName,
                                 UINT cchMax)
{
    if (pszName != NULL)
        *pszName = 0;

    // the string shown in the status bar when the context menu is invoked from Explorer
    // we would have to let it be edited per item - for now I am ignoring it

    return NOERROR;
}

#endif // ENABLE_SH_MENU_EXT
