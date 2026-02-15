// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-License-Identifier: GPL-2.0-or-later
// CommentsTranslationProject: TRANSLATED

#include "precomp.h"

#include "menu.h"
#include "ui/IPrompter.h"
#include "common/unicode/helpers.h"
#include "common/IEnvironment.h"
#include "cfgdlg.h"
#include "plugins.h"
#include "fileswnd.h"
#include "mainwnd.h"
#include "snooper.h"
#include "shellib.h"
#include "pack.h"
extern "C"
{
#include "shexreg.h"
}
#include "salshlib.h"
#include "tasklist.h"
//#include "drivelst.h"

//
// ****************************************************************************
// UseOwnRutine
//

BOOL UseOwnRutine(IDataObject* pDataObject)
{
    return DropSourcePanel != NULL || // either it's being dragged from us
           OurClipDataObject;         // or it's from us on the clipboard
}

//
// ****************************************************************************
// MouseConfirmDrop
//

BOOL MouseConfirmDrop(DWORD& effect, DWORD& defEffect, DWORD& grfKeyState)
{
    HMENU menu = CreatePopupMenu();
    if (menu != NULL)
    {
        /* used by export_mnu.py script which generates salmenu.mnu for Translator
   keep synchronized with AppendMenu() calls below...
MENU_TEMPLATE_ITEM MouseDropMenu1[] =
{
	{MNTT_PB, 0
	{MNTT_IT, IDS_DROPMOVE
	{MNTT_IT, IDS_DROPCOPY
	{MNTT_IT, IDS_DROPLINK
	{MNTT_IT, IDS_DROPCANCEL
	{MNTT_PE, 0
};
MENU_TEMPLATE_ITEM MouseDropMenu2[] =
{
	{MNTT_PB, 0
	{MNTT_IT, IDS_DROPUNKNOWN
	{MNTT_IT, IDS_DROPCANCEL
	{MNTT_PE, 0
};
*/
        DWORD cmd = 4;
        char *item1 = NULL, *item2 = NULL, *item3 = NULL, *item4 = NULL;
        if (effect & DROPEFFECT_MOVE)
            item1 = LoadStr(IDS_DROPMOVE);
        if (effect & DROPEFFECT_COPY)
            item2 = LoadStr(IDS_DROPCOPY);
        if (effect & DROPEFFECT_LINK)
            item3 = LoadStr(IDS_DROPLINK);
        if (item1 == NULL && item2 == NULL && item3 == NULL)
            item4 = LoadStr(IDS_DROPUNKNOWN);

        if ((item1 == NULL || AppendMenu(menu, MF_ENABLED | MF_STRING, 1, item1)) &&
            (item2 == NULL || AppendMenu(menu, MF_ENABLED | MF_STRING, 2, item2)) &&
            (item3 == NULL || AppendMenu(menu, MF_ENABLED | MF_STRING, 3, item3)) &&
            (item4 == NULL || AppendMenu(menu, MF_ENABLED | MF_STRING | MF_DEFAULT,
                                         4, item4)) &&
            AppendMenu(menu, MF_SEPARATOR, 0, NULL) &&
            AppendMenu(menu, MF_ENABLED | MF_STRING | MF_DEFAULT, 5, LoadStr(IDS_DROPCANCEL)))
        {
            int defItem = 0;
            if (item1 != NULL && (defEffect & DROPEFFECT_MOVE))
                defItem = 1;
            if (item2 != NULL && (defEffect & DROPEFFECT_COPY))
                defItem = 2;
            if (item3 != NULL && (defEffect & DROPEFFECT_LINK))
                defItem = 3;
            if (defItem != 0)
            {
                MENUITEMINFO item;
                memset(&item, 0, sizeof(item));
                item.cbSize = sizeof(item);
                item.fMask = MIIM_STATE;
                item.fState = MFS_DEFAULT | MFS_ENABLED;
                SetMenuItemInfo(menu, defItem, FALSE, &item);
            }
            POINT p;
            GetCursorPos(&p);
            cmd = TrackPopupMenuEx(menu, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_LEFTBUTTON, p.x, p.y, MainWindow->HWindow, NULL);
        }
        DestroyMenu(menu);
        switch (cmd)
        {
        case 1: // move
        {
            effect = defEffect = DROPEFFECT_MOVE;
            grfKeyState = 0;
            break;
        }

        case 2: // copy
        {
            effect = defEffect = DROPEFFECT_COPY;
            grfKeyState = 0;
            break;
        }

        case 3: // link
        {
            effect = defEffect = DROPEFFECT_LINK;
            grfKeyState = MK_SHIFT | MK_CONTROL;
            break;
        }

        case 0: // ESC
        case 5:
            return FALSE; // cancel
        }
    }
    return TRUE;
}

//
// ****************************************************************************
// DoCopyMove
//

BOOL DoCopyMove(BOOL copy, char* targetDir, CCopyMoveData* data, void* param)
{
    CFilesWindow* panel = (CFilesWindow*)param;

    CTmpDropData* tmp = new CTmpDropData;
    if (tmp != NULL)
    {
        tmp->Copy = copy;
        strcpy(tmp->TargetPath, targetDir);
        tmp->Data = data;
        PostMessage(panel->HWindow, WM_USER_DROPCOPYMOVE, (WPARAM)tmp, 0);
        return TRUE;
    }
    else
    {
        DestroyCopyMoveData(data);
        return FALSE;
    }
}

//
// ****************************************************************************
// DoDragDropOper
//

void DoDragDropOper(BOOL copy, BOOL toArchive, const char* archiveOrFSName, const char* archivePathOrUserPart,
                    CDragDropOperData* data, void* param)
{
    CFilesWindow* panel = (CFilesWindow*)param;
    CTmpDragDropOperData* tmp = new CTmpDragDropOperData;
    if (tmp != NULL)
    {
        tmp->Copy = copy;
        tmp->ToArchive = toArchive;
        BOOL ok = TRUE;
        if (archiveOrFSName == NULL)
        {
            if (toArchive)
            {
                if (panel->Is(ptZIPArchive))
                    archiveOrFSName = panel->GetZIPArchive();
                else
                {
                    TRACE_E("DoDragDropOper(): unexpected type of drop panel (should be archive)!");
                    ok = FALSE;
                }
            }
            else
            {
                if (panel->Is(ptPluginFS))
                    archiveOrFSName = panel->GetPluginFS()->GetPluginFSName();
                else
                {
                    TRACE_E("DoDragDropOper(): unexpected type of drop panel (should be FS)!");
                    ok = FALSE;
                }
            }
        }
        if (ok)
        {
            lstrcpyn(tmp->ArchiveOrFSName, archiveOrFSName, tmp->ArchiveOrFSName.Size());
            lstrcpyn(tmp->ArchivePathOrUserPart, archivePathOrUserPart, tmp->ArchivePathOrUserPart.Size());
            tmp->Data = data;
            PostMessage(panel->HWindow, WM_USER_DROPTOARCORFS, (WPARAM)tmp, 0);
            data = NULL;
            tmp = NULL;
        }
    }
    else
        TRACE_E(LOW_MEMORY);
    if (tmp != NULL)
        delete tmp;
    if (data != NULL)
        delete data;
}

//
// ****************************************************************************
// DoGetFSToFSDropEffect
//

void DoGetFSToFSDropEffect(const char* srcFSPath, const char* tgtFSPath,
                           DWORD allowedEffects, DWORD keyState,
                           DWORD* dropEffect, void* param)
{
    CFilesWindow* panel = (CFilesWindow*)param;
    DWORD orgEffect = *dropEffect;
    if (panel->Is(ptPluginFS) && panel->GetPluginFS()->NotEmpty())
    {
        panel->GetPluginFS()->GetDropEffect(srcFSPath, tgtFSPath, allowedEffects,
                                            keyState, dropEffect);
    }

    // if the FS didn't respond or returned nonsense, we prioritize Copy
    if (*dropEffect != DROPEFFECT_COPY && *dropEffect != DROPEFFECT_MOVE &&
        *dropEffect != DROPEFFECT_NONE)
    {
        *dropEffect = orgEffect;
        if ((*dropEffect & DROPEFFECT_COPY) != 0)
            *dropEffect = DROPEFFECT_COPY;
        else
        {
            if ((*dropEffect & DROPEFFECT_MOVE) != 0)
                *dropEffect = DROPEFFECT_MOVE;
            else
                *dropEffect = DROPEFFECT_NONE; // drop-target error
        }
    }
}

//
// ****************************************************************************
// GetCurrentDir
//

const char* GetCurrentDir(POINTL& pt, void* param, DWORD* effect, BOOL rButton, BOOL& isTgtFile,
                          DWORD keyState, int& tgtType, int srcType)
{
    CFilesWindow* panel = (CFilesWindow*)param;
    isTgtFile = FALSE; // not a drop target file yet -> we can handle the operation ourselves
    tgtType = idtttWindows;
    RECT r;
    GetWindowRect(panel->GetListBoxHWND(), &r);
    int index = panel->GetIndex(pt.x - r.left, pt.y - r.top);
    if (panel->Is(ptZIPArchive) || panel->Is(ptPluginFS))
    {
        if (panel->Is(ptZIPArchive))
        {
            int format = PackerFormatConfig.PackIsArchive(panel->GetZIPArchive());
            if (format != 0) // we found a supported archive
            {
                format--;
                if (PackerFormatConfig.GetUsePacker(format) &&
                        (*effect & (DROPEFFECT_MOVE | DROPEFFECT_COPY)) != 0 || // has edit? + effect is copy or move?
                    index == 0 && panel->Dirs->Count > 0 && strcmp(panel->Dirs->At(0).Name, "..") == 0 &&
                        (panel->GetZIPPath()[0] == 0 || panel->GetZIPPath()[0] == '\\' && panel->GetZIPPath()[1] == 0)) // drop to disk path
                {
                    tgtType = idtttArchive;
                    DWORD origEffect = *effect;
                    *effect &= (DROPEFFECT_MOVE | DROPEFFECT_COPY); // trim effect to copy+move

                    if (index >= 0 && index < panel->Dirs->Count) // drop on directory
                    {
                        panel->SetDropTarget(index);
                        int l = (int)strlen(panel->GetZIPPath());
                        memcpy(panel->DropPath.Get(), panel->GetZIPPath(), l);
                        if (index == 0 && strcmp(panel->Dirs->At(index).Name, "..") == 0)
                        {
                            if (l > 0 && panel->DropPath[l - 1] == '\\')
                                panel->DropPath[--l] = 0;
                            int backSlash = 0;
                            if (l == 0) // drop-path will be disk (".." leads out of archive)
                            {
                                tgtType = idtttWindows;
                                *effect = origEffect;
                                l = (int)strlen(panel->GetZIPArchive());
                                memcpy(panel->DropPath.Get(), panel->GetZIPArchive(), l);
                                backSlash = 1;
                            }
                            char* s = panel->DropPath + l;
                            while (--s >= (char*)panel->DropPath && *s != '\\')
                                ;
                            if (s > (char*)panel->DropPath)
                                *(s + backSlash) = 0;
                            else
                                panel->DropPath[0] = 0;
                        }
                        else
                        {
                            if (l > 0 && panel->DropPath[l - 1] != '\\')
                                panel->DropPath[l++] = '\\';
                            if (l + (int)panel->Dirs->At(index).NameLen >= panel->DropPath.Size())
                            {
                                TRACE_E("GetCurrentDir(): too long file name!");
                                tgtType = idtttWindows;
                                panel->SetDropTarget(-1); // hide marker
                                return NULL;
                            }
                            lstrcpyn(panel->DropPath + l, panel->Dirs->At(index).Name, panel->DropPath.Size() - l);
                        }
                        return panel->DropPath;
                    }
                    else
                    {
                        panel->SetDropTarget(-1); // hide marker
                        return panel->GetZIPPath();
                    }
                }
            }
        }
        else
        {
            if (panel->GetPluginFS()->NotEmpty())
            {
                if (srcType == 2 /* FS */) // drag&drop from FS to FS (any FS between each other, restrictions in CPluginFSInterfaceAbstract::CopyOrMoveFromFS)
                {
                    tgtType = idtttFullPluginFSPath;
                    int l = (int)strlen(panel->GetPluginFS()->GetPluginFSName());
                    memcpy(panel->DropPath.Get(), panel->GetPluginFS()->GetPluginFSName(), l);
                    panel->DropPath[l++] = ':';
                    if (index >= 0 && index < panel->Dirs->Count) // drop on directory
                    {
                        if (panel == DropSourcePanel) // drag&drop within one panel
                        {
                            if (panel->GetSelCount() == 0 &&
                                    index == panel->GetCaretIndex() ||
                                panel->GetSel(index) != 0)
                            {                             // directory into itself
                                panel->SetDropTarget(-1); // hide marker (copy will go to current directory, not to focused subdirectory)
                                if (!rButton && (keyState & (MK_CONTROL | MK_SHIFT | MK_ALT)) == 0)
                                {
                                    tgtType = idtttWindows;
                                    return NULL; // without modifier STOP cursor stays (prevents accidental copying to current directory)
                                }
                                if (effect != NULL)
                                    *effect &= ~DROPEFFECT_MOVE;
                                if (panel->GetPluginFS()->GetCurrentPath(panel->DropPath + l))
                                    return panel->DropPath;
                                else
                                {
                                    tgtType = idtttWindows;
                                    return NULL;
                                }
                            }
                        }

                        if (panel->GetPluginFS()->GetFullName(panel->Dirs->At(index),
                                                              (index == 0 && strcmp(panel->Dirs->At(0).Name, "..") == 0) ? 2 : 1,
                                                              panel->DropPath + l, panel->DropPath.Size() - l))
                        {
                            if (DropSourcePanel != NULL && DropSourcePanel->Is(ptPluginFS) &&
                                DropSourcePanel->GetPluginFS()->NotEmpty() && effect != NULL)
                            { // source FS can affect allowed drop-effects
                                DropSourcePanel->GetPluginFS()->GetAllowedDropEffects(1 /* drag-over-fs */, panel->DropPath,
                                                                                      effect);
                            }

                            panel->SetDropTarget(index);
                            return panel->DropPath;
                        }
                    }

                    panel->SetDropTarget(-1);                       // hide marker
                    if (panel == DropSourcePanel && effect != NULL) // drag&drop within one panel
                    {
                        if (!rButton && (keyState & (MK_CONTROL | MK_SHIFT | MK_ALT)) == 0)
                        {
                            tgtType = idtttWindows;
                            return NULL; // without modifier STOP cursor stays (prevents accidental copying to current directory)
                        }
                        *effect &= ~DROPEFFECT_MOVE;
                    }
                    if (panel->GetPluginFS()->GetCurrentPath(panel->DropPath + l))
                    {
                        if (DropSourcePanel != NULL && DropSourcePanel->Is(ptPluginFS) &&
                            DropSourcePanel->GetPluginFS()->NotEmpty() && effect != NULL)
                        { // source FS can influence the allowed drop effects
                            DropSourcePanel->GetPluginFS()->GetAllowedDropEffects(1 /* drag-over-fs */, panel->DropPath,
                                                                                  effect);
                        }
                        return panel->DropPath;
                    }
                    else
                    {
                        tgtType = idtttWindows;
                        return NULL;
                    }
                }

                DWORD posEff = 0;
                if (panel->GetPluginFS()->IsServiceSupported(FS_SERVICE_COPYFROMDISKTOFS))
                    posEff |= DROPEFFECT_COPY;
                if (panel->GetPluginFS()->IsServiceSupported(FS_SERVICE_MOVEFROMDISKTOFS))
                    posEff |= DROPEFFECT_MOVE;
                if ((*effect & posEff) != 0)
                {
                    tgtType = idtttPluginFS;
                    *effect &= posEff; // trim effect to FS capabilities

                    if (index >= 0 && index < panel->Dirs->Count) // drop on directory
                    {
                        if (panel->GetPluginFS()->GetFullName(panel->Dirs->At(index),
                                                              (index == 0 && strcmp(panel->Dirs->At(0).Name, "..") == 0) ? 2 : 1,
                                                              panel->DropPath, panel->DropPath.Size()))
                        {
                            panel->SetDropTarget(index);
                            return panel->DropPath;
                        }
                    }
                    panel->SetDropTarget(-1); // hide marker
                    if (panel->GetPluginFS()->GetCurrentPath(panel->DropPath))
                        return panel->DropPath;
                    else
                    {
                        tgtType = idtttWindows;
                        return NULL;
                    }
                }
            }
        }
        panel->SetDropTarget(-1); // hide marker
        return NULL;
    }

    if (index >= 0 && index < panel->Dirs->Count) // drop on directory
    {
        if (panel == DropSourcePanel) // drag&drop within one panel
        {
            if (panel->GetSelCount() == 0 &&
                    index == panel->GetCaretIndex() ||
                panel->GetSel(index) != 0)
            {                             // directory into itself
                panel->SetDropTarget(-1); // hide marker (copy/shortcut will go to current directory, not to focused subdirectory)
                if (!rButton && (keyState & (MK_CONTROL | MK_SHIFT | MK_ALT)) == 0)
                    return NULL; // without modifier STOP cursor stays (prevents accidental copying to current directory)
                if (effect != NULL)
                    *effect &= ~DROPEFFECT_MOVE;
                return panel->GetPath();
            }
        }

        panel->SetDropTarget(index);
        int l = (int)strlen(panel->GetPath());
        memcpy(panel->DropPath.Get(), panel->GetPath(), l);
        if (strcmp(panel->Dirs->At(index).Name, "..") == 0)
        {
            char* s = panel->DropPath + l;
            if (l > 0 && *(s - 1) == '\\')
                s--;
            while (--s > (char*)panel->DropPath && *s != '\\')
                ;
            if (s > (char*)panel->DropPath)
                *(s + 1) = 0;
        }
        else
        {
            if (panel->GetPath()[l - 1] != '\\')
                panel->DropPath[l++] = '\\';
            if (l + (int)panel->Dirs->At(index).NameLen >= panel->DropPath.Size())
            {
                TRACE_E("GetCurrentDir(): too long file name!");
                panel->SetDropTarget(-1); // hide marker
                return NULL;
            }
            lstrcpyn(panel->DropPath + l, panel->Dirs->At(index).Name, panel->DropPath.Size() - l);
        }
        return panel->DropPath;
    }
    else
    {
        if (index >= panel->Dirs->Count && index < panel->Dirs->Count + panel->Files->Count)
        {                                 // drop on file
            if (panel == DropSourcePanel) // drag&drop within one panel
            {
                if (panel->GetSelCount() == 0 &&
                        index == panel->GetCaretIndex() ||
                    panel->GetSel(index) != 0)
                {                             // file into itself
                    panel->SetDropTarget(-1); // hide marker (copy/shortcut will go to current directory, not to focused file)
                    if (!rButton && (keyState & (MK_CONTROL | MK_SHIFT | MK_ALT)) == 0)
                        return NULL; // without modifier STOP cursor stays (prevents accidental copying to current directory)
                    if (effect != NULL)
                        *effect &= ~DROPEFFECT_MOVE;
                    return panel->GetPath();
                }
            }
            CPathBuffer fullName; // Heap-allocated for long path support
            int l = (int)strlen(panel->GetPath());
            memcpy(fullName.Get(), panel->GetPath(), l);
            if (fullName[l - 1] != '\\')
                fullName[l++] = '\\';
            CFileData* file = &(panel->Files->At(index - panel->Dirs->Count));
            if (l + (int)file->NameLen >= (int)fullName.Size())
            {
                TRACE_E("GetCurrentDir(): too long file name!");
                panel->SetDropTarget(-1); // hide marker
                return NULL;
            }
            strcpy(fullName + l, file->Name);

            // if it's a shortcut, perform its analysis
            BOOL linkIsDir = FALSE;  // TRUE -> shortcut to directory -> ChangePathToDisk
            BOOL linkIsFile = FALSE; // TRUE -> shortcut to file -> archive test
            CPathBuffer linkTgt; // Heap-allocated for long path support
            linkTgt[0] = 0;
            if (StrICmp(file->Ext, "lnk") == 0) // is it a directory shortcut?
            {
                IShellLink* link;
                if (CoCreateInstance(CLSID_ShellLink, NULL,
                                     CLSCTX_INPROC_SERVER, IID_IShellLink,
                                     (LPVOID*)&link) == S_OK)
                {
                    IPersistFile* fileInt;
                    if (link->QueryInterface(IID_IPersistFile, (LPVOID*)&fileInt) == S_OK)
                    {
                        CWidePathBuffer oleName;
                        MultiByteToWideChar(CP_ACP, MB_PRECOMPOSED, fullName, -1, oleName, oleName.Size());
                        oleName[oleName.Size() - 1] = 0;
                        if (fileInt->Load(oleName, STGM_READ) == S_OK &&
                            link->GetPath(linkTgt, linkTgt.Size(), NULL, SLGP_UNCPRIORITY) == NOERROR)
                        {
                            DWORD attr = GetFileAttributesW(AnsiToWide(linkTgt).c_str());
                            if (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY))
                                linkIsDir = TRUE;
                            else
                                linkIsFile = TRUE;
                        }
                        fileInt->Release();
                    }
                    link->Release();
                }
            }
            if (linkIsDir) // link leads to directory, path is o.k., switch to it
            {
                panel->SetDropTarget(index);
                lstrcpyn(panel->DropPath, linkTgt, panel->DropPath.Size());
                return panel->DropPath;
            }

            int format = PackerFormatConfig.PackIsArchive(linkIsFile ? linkTgt : fullName);
            if (format != 0) // we found a supported archive
            {
                format--;
                if (PackerFormatConfig.GetUsePacker(format) && // ma edit?
                    (*effect & (DROPEFFECT_MOVE | DROPEFFECT_COPY)) != 0)
                {
                    tgtType = idtttArchiveOnWinPath;
                    *effect &= (DROPEFFECT_MOVE | DROPEFFECT_COPY); // trim effect to copy+move
                    panel->SetDropTarget(index);
                    lstrcpyn(panel->DropPath, linkIsFile ? linkTgt : fullName, panel->DropPath.Size());
                    return panel->DropPath;
                }
                panel->SetDropTarget(-1); // hide marker
                return NULL;
            }

            if (HasDropTarget(fullName))
            {
                isTgtFile = TRUE; // drop target file -> shell must handle it
                panel->SetDropTarget(index);
                lstrcpyn(panel->DropPath, fullName, panel->DropPath.Size());
                return panel->DropPath;
            }
        }
        panel->SetDropTarget(-1); // hide marker
    }

    if (panel == DropSourcePanel && effect != NULL) // drag&drop v ramci jednoho panelu
    {
        if (!rButton && (keyState & (MK_CONTROL | MK_SHIFT | MK_ALT)) == 0)
            return NULL; // without modifier STOP cursor stays (prevents accidental copying to current directory)
        *effect &= ~DROPEFFECT_MOVE;
    }
    return panel->GetPath();
}

const char* GetCurrentDirClipboard(POINTL& pt, void* param, DWORD* effect, BOOL rButton,
                                   BOOL& isTgtFile, DWORD keyState, int& tgtType, int srcType)
{ // jednodussi verze predchoziho pro "paste" z clipboardu
    CFilesWindow* panel = (CFilesWindow*)param;
    isTgtFile = FALSE;
    tgtType = idtttWindows;
    if (panel->Is(ptZIPArchive) || panel->Is(ptPluginFS)) // do archivu a FS zatim ne
    {
        //    if (panel->Is(ptZIPArchive)) tgtType = idtttArchive;
        //    else tgtType = idtttPluginFS;
        return NULL;
    }
    return panel->DropPath;
}

//
// ****************************************************************************
// DropEnd
//

int CountNumberOfItemsOnPath(const char* path)
{
    CPathBuffer s;
    lstrcpyn(s, path, s.Size());
    if (SalPathAppend(s, "*.*", s.Size()))
    {
        WIN32_FIND_DATAW fileData;
        HANDLE search = HANDLES_Q(FindFirstFileW(AnsiToWide(s).c_str(), &fileData));
        if (search != INVALID_HANDLE_VALUE)
        {
            int num = 0;
            do
            {
                num++;
            } while (FindNextFileW(search, &fileData));
            HANDLES(FindClose(search));
            return num;
        }
    }
    return 0;
}

void DropEnd(BOOL drop, BOOL shortcuts, void* param, BOOL ownRutine, BOOL isFakeDataObject, int tgtType)
{
    CFilesWindow* panel = (CFilesWindow*)param;
    if (drop && GetActiveWindow() == NULL)
        SetForegroundWindow(MainWindow->HWindow);
    if (drop)
        MainWindow->FocusPanel(panel);

    panel->SetDropTarget(-1); // hide marker
    if (tgtType == idtttWindows &&
        !isFakeDataObject && (!ownRutine || shortcuts) && drop && // refresh panels
        (!MainWindow->LeftPanel->AutomaticRefresh ||
         !MainWindow->RightPanel->AutomaticRefresh ||
         MainWindow->LeftPanel->GetNetworkDrive() ||
         MainWindow->RightPanel->GetNetworkDrive()))
    {
        BOOL again = TRUE; // as long as files keep coming, we load
        int numLeft = MainWindow->LeftPanel->NumberOfItemsInCurDir;
        int numRight = MainWindow->RightPanel->NumberOfItemsInCurDir;
        while (again)
        {
            again = FALSE;
            Sleep(shortcuts ? 333 : 1000); // they work in another thread, give them time

            if ((!MainWindow->LeftPanel->AutomaticRefresh || MainWindow->LeftPanel->GetNetworkDrive()) &&
                MainWindow->LeftPanel->Is(ptDisk))
            {
                int newNum = CountNumberOfItemsOnPath(MainWindow->LeftPanel->GetPath());
                again |= newNum != numLeft;
                numLeft = newNum;
            }
            if ((!MainWindow->RightPanel->AutomaticRefresh || MainWindow->RightPanel->GetNetworkDrive()) &&
                MainWindow->RightPanel->Is(ptDisk))
            {
                int newNum = CountNumberOfItemsOnPath(MainWindow->RightPanel->GetPath());
                again |= newNum != numRight;
                numRight = newNum;
            }
        }

        // let panels refresh
        HANDLES(EnterCriticalSection(&TimeCounterSection));
        int t1 = MyTimeCounter++;
        int t2 = MyTimeCounter++;
        HANDLES(LeaveCriticalSection(&TimeCounterSection));
        if (!MainWindow->LeftPanel->AutomaticRefresh || MainWindow->LeftPanel->GetNetworkDrive())
            PostMessage(MainWindow->LeftPanel->HWindow, WM_USER_REFRESH_DIR, 0, t1);
        if (!MainWindow->RightPanel->AutomaticRefresh || MainWindow->RightPanel->GetNetworkDrive())
            PostMessage(MainWindow->RightPanel->HWindow, WM_USER_REFRESH_DIR, 0, t2);
        MainWindow->RefreshDiskFreeSpace();
    }
}

void EnterLeaveDrop(BOOL enter, void* param)
{
    CFilesWindow* panel = (CFilesWindow*)param;
    if (enter)
        panel->DragEnter();
    else
        panel->DragLeave();
}

//
// ****************************************************************************
// SetClipCutCopyInfo
//

void SetClipCutCopyInfo(HWND hwnd, BOOL copy, BOOL salObject)
{
    UINT cfPrefDrop = RegisterClipboardFormat(CFSTR_PREFERREDDROPEFFECT);
    UINT cfSalDataObject = RegisterClipboardFormat(SALCF_IDATAOBJECT);
    HANDLE effect = NOHANDLES(GlobalAlloc(GMEM_MOVEABLE | GMEM_DDESHARE, sizeof(DWORD)));
    HANDLE effect2 = NOHANDLES(GlobalAlloc(GMEM_MOVEABLE | GMEM_DDESHARE, sizeof(DWORD)));
    if (effect != NULL && effect2 != NULL)
    {
        DWORD* ef = (DWORD*)HANDLES(GlobalLock(effect));
        if (ef != NULL)
        {
            *ef = copy ? (DROPEFFECT_COPY | DROPEFFECT_LINK) : DROPEFFECT_MOVE;
            HANDLES(GlobalUnlock(effect));
            if (OpenClipboard(hwnd))
            {
                if (SetClipboardData(cfPrefDrop, effect) == NULL)
                    NOHANDLES(GlobalFree(effect));
                if (!salObject || SetClipboardData(cfSalDataObject, effect2) == NULL)
                    NOHANDLES(GlobalFree(effect2));
                CloseClipboard();
            }
            else
            {
                TRACE_E("OpenClipboard() has failed!");
                NOHANDLES(GlobalFree(effect));
                NOHANDLES(GlobalFree(effect2));
            }
        }
        else
        {
            NOHANDLES(GlobalFree(effect));
            NOHANDLES(GlobalFree(effect2));
        }
    }
}

//
// ****************************************************************************
// ShellAction
//

const char* EnumFileNames(int index, void* param)
{
    CTmpEnumData* data = (CTmpEnumData*)param;
    if (data->Indexes[index] >= 0 &&
        data->Indexes[index] < data->Panel->Dirs->Count + data->Panel->Files->Count)
    {
        return (data->Indexes[index] < data->Panel->Dirs->Count) ? data->Panel->Dirs->At(data->Indexes[index]).Name : data->Panel->Files->At(data->Indexes[index] - data->Panel->Dirs->Count).Name;
    }
    else
        return NULL;
}

const char* EnumOneFileName(int index, void* param)
{
    return index == 0 ? (char*)param : NULL;
}

void AuxInvokeCommand2(CFilesWindow* panel, CMINVOKECOMMANDINFO* ici)
{
    CALL_STACK_MESSAGE_NONE

    // temporarily lower thread priority, so some confused shell extension doesn't eat CPU
    HANDLE hThread = GetCurrentThread(); // pseudo-handle, no need to release
    int oldThreadPriority = GetThreadPriority(hThread);
    SetThreadPriority(hThread, THREAD_PRIORITY_NORMAL);

    __try
    {
        panel->ContextSubmenuNew->GetMenu2()->InvokeCommand(ici);
    }
    __except (CCallStack::HandleException(GetExceptionInformation(), 17))
    {
        ICExceptionHasOccured++;
    }

    SetThreadPriority(hThread, oldThreadPriority);
}

void AuxInvokeCommand(CFilesWindow* panel, CMINVOKECOMMANDINFO* ici)
{ // POZOR: pouziva se i z CSalamanderGeneral::OpenNetworkContextMenu()
    CALL_STACK_MESSAGE_NONE

    // temporarily lower thread priority, so some confused shell extension doesn't eat CPU
    HANDLE hThread = GetCurrentThread(); // pseudo-handle, no need to release
    int oldThreadPriority = GetThreadPriority(hThread);
    SetThreadPriority(hThread, THREAD_PRIORITY_NORMAL);

    __try
    {
        panel->ContextMenu->InvokeCommand(ici);
    }
    __except (CCallStack::HandleException(GetExceptionInformation(), 18))
    {
        ICExceptionHasOccured++;
    }

    SetThreadPriority(hThread, oldThreadPriority);
}

void AuxInvokeAndRelease(IContextMenu2* menu, CMINVOKECOMMANDINFO* ici)
{
    CALL_STACK_MESSAGE_NONE

    // temporarily lower thread priority, so some confused shell extension doesn't eat CPU
    HANDLE hThread = GetCurrentThread(); // pseudo-handle, no need to release
    int oldThreadPriority = GetThreadPriority(hThread);
    SetThreadPriority(hThread, THREAD_PRIORITY_NORMAL);

    __try
    {
        menu->InvokeCommand(ici);
    }
    __except (CCallStack::HandleException(GetExceptionInformation(), 19))
    {
        ICExceptionHasOccured++;
    }

    SetThreadPriority(hThread, oldThreadPriority);

    __try
    {
        menu->Release();
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        RelExceptionHasOccured++;
    }
}

HRESULT AuxGetCommandString(IContextMenu2* menu, UINT_PTR idCmd, UINT uType, UINT* pReserved, LPSTR pszName, UINT cchMax)
{
    CALL_STACK_MESSAGE_NONE
    HRESULT ret = E_UNEXPECTED;
    __try
    {
        // for years we've been getting crashes when calling IContextMenu2::GetCommandString()
        // this call is not essential for program operation, so we wrap it in try/except block
        ret = menu->GetCommandString(idCmd, uType, pReserved, pszName, cchMax);
    }
    __except (CCallStack::HandleException(GetExceptionInformation(), 19))
    {
        ICExceptionHasOccured++;
    }
    return ret;
}

void ShellActionAux5(UINT flags, CFilesWindow* panel, HMENU h)
{ // POZOR: pouziva se i z CSalamanderGeneral::OpenNetworkContextMenu()
    CALL_STACK_MESSAGE_NONE

    // temporarily lower thread priority, so some confused shell extension doesn't eat CPU
    HANDLE hThread = GetCurrentThread(); // pseudo-handle, no need to release
    int oldThreadPriority = GetThreadPriority(hThread);
    SetThreadPriority(hThread, THREAD_PRIORITY_NORMAL);

    __try
    {
        panel->ContextMenu->QueryContextMenu(h, 0, 0, 4999, flags);
    }
    __except (CCallStack::HandleException(GetExceptionInformation(), 20))
    {
        QCMExceptionHasOccured++;
    }

    SetThreadPriority(hThread, oldThreadPriority);
}

void ShellActionAux6(CFilesWindow* panel)
{ // POZOR: pouziva se i z CSalamanderGeneral::OpenNetworkContextMenu()
    __try
    {
        if (panel->ContextMenu != NULL)
            panel->ContextMenu->Release();
        panel->ContextMenu = NULL;
        if (panel->ContextSubmenuNew->MenuIsAssigned())
            panel->ContextSubmenuNew->Release();
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        RelExceptionHasOccured++;
    }
}

void ShellActionAux7(IDataObject* dataObject, CImpIDropSource* dropSource)
{
    __try
    {
        if (dropSource != NULL)
            dropSource->Release(); // it's ours, hopefully it won't crash ;-)
        if (dataObject != NULL)
            dataObject->Release();
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        RelExceptionHasOccured++;
    }
}

void DoDragFromArchiveOrFS(CFilesWindow* panel, BOOL& dropDone, char* targetPath, int& operation,
                           char* realDraggedPath, DWORD allowedEffects,
                           int srcType, const char* srcFSPath, BOOL leftMouseButton)
{
    if (SalShExtSharedMemView != NULL) // shared memory is available (we can't handle drag&drop on error)
    {
        CALL_STACK_MESSAGE1("ShellAction::archive/FS::drag_files");

        // create "fake" directory
        CPathBuffer fakeRootDir; // Heap-allocated for long path support
        char* fakeName;
        if (SalGetTempFileName(NULL, "SAL", fakeRootDir, FALSE))
        {
            fakeName = fakeRootDir + strlen(fakeRootDir);
            // jr: Nasel jsem na netu zminku "Did implementing "IPersistStream" and providing the undocumented
            // "OleClipboardPersistOnFlush" format solve the problem?" -- pro pripad, ze bychom se potrebovali
            // zbavit DROPFAKE metody
            if (SalPathAppend(fakeRootDir, "DROPFAKE", fakeRootDir.Size()))
            {
                if (SalLPCreateDirectory(fakeRootDir, NULL))
                {
                    // vytvorime objekty pro drag&drop
                    *fakeName = 0;
                    IDataObject* dataObject = CreateIDataObject(MainWindow->HWindow, fakeRootDir,
                                                                1, EnumOneFileName, fakeName + 1);
                    BOOL dragFromPluginFSWithCopyAndMove = allowedEffects == (DROPEFFECT_MOVE | DROPEFFECT_COPY);
                    CImpIDropSource* dropSource = new CImpIDropSource(dragFromPluginFSWithCopyAndMove);
                    if (dataObject != NULL && dropSource != NULL)
                    {
                        CFakeDragDropDataObject* fakeDataObject = new CFakeDragDropDataObject(dataObject, realDraggedPath,
                                                                                              srcType, srcFSPath);
                        if (fakeDataObject != NULL)
                        {
                            // shared memory initialization
                            WaitForSingleObject(SalShExtSharedMemMutex, INFINITE);
                            BOOL sharedMemOK = SalShExtSharedMemView->Size >= sizeof(CSalShExtSharedMem);
                            if (sharedMemOK)
                            {
                                if (SalShExtSharedMemView->DoDragDropFromSalamander)
                                    TRACE_E("Drag&drop from archive/FS: SalShExtSharedMemView->DoDragDropFromSalamander is TRUE, this should never happen here!");
                                SalShExtSharedMemView->DoDragDropFromSalamander = TRUE;
                                *fakeName = '\\';
                                lstrcpyn(SalShExtSharedMemView->DragDropFakeDirName, fakeRootDir, MAX_PATH);
                                SalShExtSharedMemView->DropDone = FALSE;
                            }
                            ReleaseMutex(SalShExtSharedMemMutex);

                            if (sharedMemOK)
                            {
                                DWORD dwEffect;
                                HRESULT hr;
                                DropSourcePanel = panel;
                                LastWndFromGetData = NULL; // just in case, if fakeDataObject->GetData wasn't called
                                hr = DoDragDrop(fakeDataObject, dropSource, allowedEffects, &dwEffect);
                                DropSourcePanel = NULL;
                                // read drag&drop results
                                // Note: returns dwEffect == 0 for MOVE, so we use workaround via dropSource->LastEffect,
                                // reasons see "Handling Shell Data Transfer Scenarios" section "Handling Optimized Move Operations":
                                // http://msdn.microsoft.com/en-us/library/windows/desktop/bb776904%28v=vs.85%29.aspx
                                // (in short: optimized Move is performed, meaning no copy to target followed by deletion
                                //            of original, so source doesn't accidentally delete original (may not be moved yet), gets
                                //            operation result DROPEFFECT_NONE or DROPEFFECT_COPY)
                                if (hr == DRAGDROP_S_DROP && dropSource->LastEffect != DROPEFFECT_NONE)
                                {
                                    WaitForSingleObject(SalShExtSharedMemMutex, INFINITE);
                                    dropDone = SalShExtSharedMemView->DropDone;
                                    SalShExtSharedMemView->DoDragDropFromSalamander = FALSE;
                                    if (dropDone)
                                    {
                                        lstrcpyn(targetPath, SalShExtSharedMemView->TargetPath, 2 * MAX_PATH);
                                        if (leftMouseButton && dragFromPluginFSWithCopyAndMove)
                                            operation = (dropSource->LastEffect & DROPEFFECT_MOVE) ? SALSHEXT_MOVE : SALSHEXT_COPY;
                                        else // archives + FS with Copy or Move (not both) + FS with Copy+Move when dragging with right button, where result from right button menu isn't affected by mouse cursor change (trick with Copy cursor during Move effect), so we take the result from copy-hook (SalShExtSharedMemView->Operation)
                                            operation = SalShExtSharedMemView->Operation;
                                    }
                                    ReleaseMutex(SalShExtSharedMemMutex);

                                    if (!dropDone &&                 // copy-hook doesn't respond or user chose Cancel in drop-menu (shown during D&D with right button)
                                        dwEffect != DROPEFFECT_NONE) // Cancel detection: since copy-hook didn't trigger, returned drop-effect is valid, so we compare it to Cancel
                                    {
                                        gPrompter->ShowError(LoadStrW(IDS_ERRORTITLE), LoadStrW(IDS_SHEXT_NOTLOADEDYET));
                                    }
                                }
                                else
                                {
                                    WaitForSingleObject(SalShExtSharedMemMutex, INFINITE);
                                    SalShExtSharedMemView->DoDragDropFromSalamander = FALSE;
                                    ReleaseMutex(SalShExtSharedMemMutex);
                                }
                            }
                            else
                                TRACE_E("Shared memory is too small!");
                            fakeDataObject->Release(); // dataObject will be released later in ShellActionAux7
                        }
                        else
                            TRACE_E(LOW_MEMORY);
                    }

                    ShellActionAux7(dataObject, dropSource);
                }
                else
                    TRACE_E("Unable to create fake directory in TEMP for drag&drop from archive/FS: unable to create subdir!");
            }
            else
                TRACE_E("Unable to create fake directory in TEMP for drag&drop from archive/FS: too long name!");
            *fakeName = 0;
            RemoveTemporaryDir(fakeRootDir);
        }
        else
            TRACE_E("Unable to create fake directory in TEMP for drag&drop from archive/FS!");
    }
}

void GetLeftTopCornert(POINT* pt, BOOL posByMouse, BOOL useSelection, CFilesWindow* panel)
{
    if (posByMouse)
    {
        // souradnice dle pozice mysi
        DWORD pos = GetMessagePos();
        pt->x = GET_X_LPARAM(pos);
        pt->y = GET_Y_LPARAM(pos);
    }
    else
    {
        if (useSelection)
        {
            // dle pozice pro kontextove menu
            panel->GetContextMenuPos(pt);
        }
        else
        {
            // levy horni roh panelu
            RECT r;
            GetWindowRect(panel->GetListBoxHWND(), &r);
            pt->x = r.left;
            pt->y = r.top;
        }
    }
}

void RemoveUselessSeparatorsFromMenu(HMENU h)
{
    int miCount = GetMenuItemCount(h);
    MENUITEMINFO mi;
    int lastSep = -1;
    int i;
    for (i = miCount - 1; i >= 0; i--)
    {
        memset(&mi, 0, sizeof(mi));
        mi.cbSize = sizeof(mi);
        mi.fMask = MIIM_TYPE;
        if (GetMenuItemInfo(h, i, TRUE, &mi) && (mi.fType & MFT_SEPARATOR))
        {
            if (lastSep != -1 && lastSep == i + 1) // two consecutive separators, delete one, it's redundant
                DeleteMenu(h, i, MF_BYPOSITION);
            lastSep = i;
        }
    }
}

#define GET_WORD(ptr) (*(WORD*)(ptr))
#define GET_DWORD(ptr) (*(DWORD*)(ptr))

BOOL ResourceGetDialogName(WCHAR* buff, int buffSize, char* name, int nameMax)
{
    DWORD style = GET_DWORD(buff);
    buff += 2; // dlgVer + signature
    if (style != 0xffff0001)
    {
        TRACE_E("ResourceGetDialogName(): resource is not DLGTEMPLATEEX!");
        // reading classic DLGTEMPLATE, see altap translator, but probably won't need to be implemented
        return FALSE;
    }

    //  typedef struct {
    //    WORD dlgVer;     // Specifies the version number of the extended dialog box template. This member must be 1.
    //    WORD signature;  // Indicates whether a template is an extended dialog box template. If signature is 0xFFFF, this is an extended dialog box template.
    //    DWORD helpID;
    //    DWORD exStyle;
    //    DWORD style;
    //    WORD cDlgItems;
    //    short x;
    //    short y;
    //    short cx;
    //    short cy;
    //    sz_Or_Ord menu;
    //    sz_Or_Ord windowClass;
    //    WCHAR title[titleLen];
    //    WORD pointsize;
    //    WORD weight;
    //    BYTE italic;
    //    BYTE charset;
    //    WCHAR typeface[stringLen];
    //  } DLGTEMPLATEEX;

    buff += 2; // helpID
    buff += 2; // exStyle
    buff += 2; // style
    buff += 1; // cDlgItems
    buff += 1; // x
    buff += 1; // y
    buff += 1; // cx
    buff += 1; // cy

    // menu name
    switch (GET_WORD(buff))
    {
    case 0x0000:
    {
        buff++;
        break;
    }

    case 0xffff:
    {
        buff += 2;
        break;
    }

    default:
    {
        buff += wcslen(buff) + 1;
        break;
    }
    }

    // class name
    switch (GET_WORD(buff))
    {
    case 0x0000:
    {
        buff++;
        break;
    }

    case 0xffff:
    {
        buff += 2;
        break;
    }

    default:
    {
        buff += wcslen(buff) + 1;
        break;
    }
    }

    // window name
    WideCharToMultiByte(CP_ACP, WC_COMPOSITECHECK, buff, (int)wcslen(buff) + 1, name, nameMax, NULL, NULL);

    return TRUE;
}

// tries to load aclui.dll and extract dialog name stored with ID 103 (Security tab)
// on success fills dialog name into pageName and returns TRUE; otherwise returns FALSE
BOOL GetACLUISecurityPageName(char* pageName, int pageNameMax)
{
    BOOL ret = FALSE;

    HINSTANCE hModule = LoadLibraryEx("aclui.dll", NULL, LOAD_LIBRARY_AS_DATAFILE);

    if (hModule != NULL)
    {
        HRSRC hrsrc = FindResource(hModule, MAKEINTRESOURCE(103), RT_DIALOG); // 103 - security zalozka
        if (hrsrc != NULL)
        {
            int size = SizeofResource(hModule, hrsrc);
            if (size > 0)
            {
                HGLOBAL hglb = LoadResource(hModule, hrsrc);
                if (hglb != NULL)
                {
                    LPVOID data = LockResource(hglb);
                    if (data != NULL)
                        ret = ResourceGetDialogName((WCHAR*)data, size, pageName, pageNameMax);
                }
            }
            else
                TRACE_E("GetACLUISecurityPageName() invalid Security dialog box resource.");
        }
        else
            TRACE_E("GetACLUISecurityPageName() cannot find Security dialog box.");
        FreeLibrary(hModule);
    }
    else
        TRACE_E("GetACLUISecurityPageName() cannot load aclui.dll");

    return ret;
}

void ShellAction(CFilesWindow* panel, CShellAction action, BOOL useSelection,
                 BOOL posByMouse, BOOL onlyPanelMenu)
{
    CALL_STACK_MESSAGE5("ShellAction(, %d, %d, %d, %d)", action, useSelection, posByMouse, onlyPanelMenu);
    if (panel->QuickSearchMode)
        panel->EndQuickSearch();
    if (panel->Dirs->Count + panel->Files->Count == 0 && useSelection)
    { // without files and directories -> nothing to do
        return;
    }

    BOOL dragFiles = action == saLeftDragFiles || action == saRightDragFiles;
    if (panel->Is(ptZIPArchive) && action != saContextMenu &&
        (!dragFiles && action != saCopyToClipboard || !SalShExtRegistered))
    {
        if (dragFiles && !SalShExtRegistered)
        {
            TRACE_E("Drag&drop from archives is not possible, shell extension utils\\salextx86.dll or utils\\salextx64.dll is missing!");
        }
        if (action == saCopyToClipboard && !SalShExtRegistered)
            TRACE_E("Copy&paste from archives is not possible, shell extension utils\\salextx86.dll or utils\\salextx64.dll is missing!");
        // we do not support other archive operations yet
        return;
    }
    if (panel->Is(ptPluginFS) && dragFiles &&
        (!SalShExtRegistered ||
         !panel->GetPluginFS()->NotEmpty() ||
         !panel->GetPluginFS()->IsServiceSupported(FS_SERVICE_MOVEFROMFS) &&    // FS umi "move from FS"
             !panel->GetPluginFS()->IsServiceSupported(FS_SERVICE_COPYFROMFS))) // FS umi "copy from FS"
    {
        if (!SalShExtRegistered)
            TRACE_E("Drag&drop from file-systems is not possible, shell extension utils\\salextx86.dll or utils\\salextx64.dll is missing!");
        if (!panel->GetPluginFS()->NotEmpty())
            TRACE_E("Unexpected situation in ShellAction(): panel->GetPluginFS() is empty!");
        return;
    }

    //  MainWindow->ReleaseMenuNew();  // Windows nejsou staveny na vic kontextovych menu

    BeginStopRefresh(); // zadne refreshe nepotrebujeme

    std::unique_ptr<int[]> indexes; // RAII: auto-deleted when scope exits
    int index = 0;
    int count = 0;
    if (useSelection)
    {
        BOOL subDir;
        if (panel->Dirs->Count > 0)
            subDir = (strcmp(panel->Dirs->At(0).Name, "..") == 0);
        else
            subDir = FALSE;

        count = panel->GetSelCount();
        if (count != 0)
        {
            indexes = std::make_unique<int[]>(count);
            panel->GetSelItems(count, indexes.get(), action == saContextMenu); // we backed off from this (see GetSelItems): for context menus we start from focused item and end with item before focus (there's intermediate return to beginning of name list) (system does it too, see Add To Windows Media Player List on MP3 files)
        }
        else
        {
            index = panel->GetCaretIndex();
            if (subDir && index == 0)
            {
                EndStopRefresh();
                return;
            }
        }
    }
    else
        index = -1;

    CPathBuffer targetPath;
    targetPath[0] = 0;
    CPathBuffer realDraggedPath;
    realDraggedPath[0] = 0;
    if (panel->Is(ptZIPArchive) && SalShExtRegistered)
    {
        if (dragFiles)
        {
            // if dragging a single subdirectory of archive, determine which one (for changing path
            // in directory-line and inserting into command-line)
            int i = -1;
            if (count == 1)
                i = indexes[0];
            else if (count == 0)
                i = index;
            if (i >= 0 && i < panel->Dirs->Count)
            {
                realDraggedPath[0] = 'D';
                lstrcpyn(realDraggedPath + 1, panel->GetZIPArchive(), 2 * MAX_PATH);
                SalPathAppend(realDraggedPath, panel->GetZIPPath(), 2 * MAX_PATH);
                SalPathAppend(realDraggedPath, panel->Dirs->At(i).Name, 2 * MAX_PATH);
            }
            else
            {
                if (i >= 0 && i >= panel->Dirs->Count && i < panel->Dirs->Count + panel->Files->Count)
                {
                    realDraggedPath[0] = 'F';
                    lstrcpyn(realDraggedPath + 1, panel->GetZIPArchive(), 2 * MAX_PATH);
                    SalPathAppend(realDraggedPath, panel->GetZIPPath(), 2 * MAX_PATH);
                    SalPathAppend(realDraggedPath, panel->Files->At(i - panel->Dirs->Count).Name, 2 * MAX_PATH);
                }
            }

            BOOL dropDone = FALSE;
            int operation = SALSHEXT_NONE;
            DoDragFromArchiveOrFS(panel, dropDone, targetPath, operation, realDraggedPath,
                                  DROPEFFECT_COPY, 1 /* archiv */, NULL, action == saLeftDragFiles);
            // RAII: indexes auto-deleted when scope exits
            EndStopRefresh();

            if (dropDone) // let the operation be performed
            {
                char* p = DupStr(targetPath);
                if (p != NULL)
                    PostMessage(panel->HWindow, WM_USER_DROPUNPACK, (WPARAM)p, operation);
            }

            return;
        }
        else
        {
            if (action == saCopyToClipboard)
            {
                if (SalShExtSharedMemView != NULL) // shared memory is available (we can't handle copy&paste on error)
                {
                    CALL_STACK_MESSAGE1("ShellAction::archive::clipcopy_files");

                    // create "fake" directory
                    CPathBuffer fakeRootDir; // Heap-allocated for long path support
                    char* fakeName;
                    if (SalGetTempFileName(NULL, "SAL", fakeRootDir, FALSE))
                    {
                        BOOL delFakeDir = TRUE;
                        fakeName = fakeRootDir + strlen(fakeRootDir);
                        if (SalPathAppend(fakeRootDir, "CLIPFAKE", fakeRootDir.Size()))
                        {
                            if (SalLPCreateDirectory(fakeRootDir, NULL))
                            {
                                DWORD prefferedDropEffect = DROPEFFECT_COPY; // DROPEFFECT_MOVE (we used for debugging purposes)

                                // create objects for copy&paste
                                *fakeName = 0;
                                IDataObject* dataObject = CreateIDataObject(MainWindow->HWindow, fakeRootDir,
                                                                            1, EnumOneFileName, fakeName + 1);
                                if (dataObject != NULL)
                                {
                                    *fakeName = '\\';
                                    CFakeCopyPasteDataObject* fakeDataObject = new CFakeCopyPasteDataObject(dataObject, fakeRootDir);
                                    if (fakeDataObject != NULL)
                                    {
                                        UINT cfPrefDrop = RegisterClipboardFormat(CFSTR_PREFERREDDROPEFFECT);
                                        HANDLE effect = NOHANDLES(GlobalAlloc(GMEM_MOVEABLE | GMEM_DDESHARE, sizeof(DWORD)));
                                        if (effect != NULL)
                                        {
                                            DWORD* ef = (DWORD*)HANDLES(GlobalLock(effect));
                                            if (ef != NULL)
                                            {
                                                *ef = prefferedDropEffect;
                                                HANDLES(GlobalUnlock(effect));

                                                if (SalShExtPastedData.SetData(panel->GetZIPArchive(), panel->GetZIPPath(),
                                                                               panel->Files, panel->Dirs,
                                                                               panel->IsCaseSensitive(),
                                                                               (count == 0) ? &index : indexes.get(),
                                                                               (count == 0) ? 1 : count))
                                                {
                                                    BOOL clearSalShExtPastedData = TRUE;
                                                    if (OleSetClipboard(fakeDataObject) == S_OK)
                                                    { // pri uspesnem ulozeni system vola fakeDataObject->AddRef() +
                                                        // ulozime default drop-effect
                                                        if (OpenClipboard(MainWindow->HWindow))
                                                        {
                                                            if (SetClipboardData(cfPrefDrop, effect) != NULL)
                                                                effect = NULL;
                                                            CloseClipboard();
                                                        }
                                                        else
                                                            TRACE_E("OpenClipboard() has failed!");

                                                        // our data is already on clipboard (we must clear it on Salamander exit)
                                                        OurDataOnClipboard = TRUE;

                                                        // shared memory initialization
                                                        WaitForSingleObject(SalShExtSharedMemMutex, INFINITE);
                                                        BOOL sharedMemOK = SalShExtSharedMemView->Size >= sizeof(CSalShExtSharedMem);
                                                        if (sharedMemOK)
                                                        {
                                                            SalShExtSharedMemView->DoPasteFromSalamander = TRUE;
                                                            SalShExtSharedMemView->ClipDataObjLastGetDataTime = GetTickCount() - 60000; // initialize to 1 minute before creating the data object
                                                            *fakeName = '\\';
                                                            lstrcpyn(SalShExtSharedMemView->PasteFakeDirName, fakeRootDir, MAX_PATH);
                                                            SalShExtSharedMemView->SalamanderMainWndPID = GetCurrentProcessId();
                                                            SalShExtSharedMemView->SalamanderMainWndTID = GetCurrentThreadId();
                                                            SalShExtSharedMemView->SalamanderMainWnd = (UINT64)(DWORD_PTR)MainWindow->HWindow;
                                                            SalShExtSharedMemView->PastedDataID++;
                                                            SalShExtPastedData.SetDataID(SalShExtSharedMemView->PastedDataID);
                                                            clearSalShExtPastedData = FALSE;
                                                            SalShExtSharedMemView->PasteDone = FALSE;
                                                            lstrcpyn(SalShExtSharedMemView->ArcUnableToPaste1, LoadStr(IDS_ARCUNABLETOPASTE1), 300);
                                                            lstrcpyn(SalShExtSharedMemView->ArcUnableToPaste2, LoadStr(IDS_ARCUNABLETOPASTE2), 300);

                                                            delFakeDir = FALSE; // everything is OK, fake-dir will be used
                                                            fakeDataObject->SetCutOrCopyDone();
                                                        }
                                                        else
                                                            TRACE_E("Shared memory is too small!");
                                                        ReleaseMutex(SalShExtSharedMemMutex);

                                                        if (!sharedMemOK) // if it's not possible to establish communication with salextx86.dll or salextx64.dll, it makes no sense to leave data-object on clipboard
                                                        {
                                                            OleSetClipboard(NULL);
                                                            OurDataOnClipboard = FALSE; // theoretically unnecessary (should be set in Release() of fakeDataObject - a few lines below)
                                                        }
                                                        // clipboard changed, let's verify...
                                                        IdleRefreshStates = TRUE;  // force state variable check on next Idle
                                                        IdleCheckClipboard = TRUE; // also check clipboard

                                                        // on COPY clear CutToClip flag
                                                        if (panel->CutToClipChanged)
                                                            panel->ClearCutToClipFlag(TRUE);
                                                        CFilesWindow* anotherPanel = MainWindow->LeftPanel == panel ? MainWindow->RightPanel : MainWindow->LeftPanel;
                                                        // on COPY also clear CutToClip flag for the other panel
                                                        if (anotherPanel->CutToClipChanged)
                                                            anotherPanel->ClearCutToClipFlag(TRUE);
                                                    }
                                                    else
                                                        TRACE_E("Unable to set data object to clipboard (copy&paste from archive)!");
                                                    if (clearSalShExtPastedData)
                                                        SalShExtPastedData.Clear();
                                                }
                                            }
                                            if (effect != NULL)
                                                NOHANDLES(GlobalFree(effect));
                                        }
                                        else
                                            TRACE_E(LOW_MEMORY);
                                        fakeDataObject->Release(); // if fakeDataObject is on clipboard, it will be released at application end or when removed from clipboard
                                    }
                                    else
                                        TRACE_E(LOW_MEMORY);
                                }
                                ShellActionAux7(dataObject, NULL);
                            }
                            else
                                TRACE_E("Unable to create fake directory in TEMP for copy&paste from archive: unable to create subdir!");
                        }
                        else
                            TRACE_E("Unable to create fake directory in TEMP for copy&paste from archive: too long name!");
                        *fakeName = 0;
                        if (delFakeDir)
                            RemoveTemporaryDir(fakeRootDir);
                    }
                    else
                        TRACE_E("Unable to create fake directory in TEMP for copy&paste from archive!");
                }
                // RAII: indexes auto-deleted when scope exits
                EndStopRefresh();
                return;
            }
        }
    }

    if (panel->Is(ptPluginFS))
    {
        // lower thread priority to "normal" (so operations don't overload the machine)
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_NORMAL);

        int panelID = MainWindow->LeftPanel == panel ? PANEL_LEFT : PANEL_RIGHT;

        int selectedDirs = 0;
        if (count > 0)
        {
            // count how many directories are selected (the rest of selected items are files)
            int i;
            for (i = 0; i < panel->Dirs->Count; i++) // ".." can't be selected, test would be unnecessary
            {
                if (panel->Dirs->At(i).Selected)
                    selectedDirs++;
            }
        }

        if (action == saProperties && useSelection &&
            panel->GetPluginFS()->NotEmpty() &&
            panel->GetPluginFS()->IsServiceSupported(FS_SERVICE_SHOWPROPERTIES)) // show-properties
        {
            panel->GetPluginFS()->ShowProperties(panel->GetPluginFS()->GetPluginFSName(),
                                                 panel->HWindow, panelID,
                                                 count - selectedDirs, selectedDirs);
        }
        else
        {
            if (action == saContextMenu &&
                panel->GetPluginFS()->NotEmpty() &&
                panel->GetPluginFS()->IsServiceSupported(FS_SERVICE_CONTEXTMENU)) // context-menu
            {
                // calculate top-left corner of context menu
                POINT p;
                if (posByMouse)
                {
                    DWORD pos = GetMessagePos();
                    p.x = GET_X_LPARAM(pos);
                    p.y = GET_Y_LPARAM(pos);
                }
                else
                {
                    if (useSelection)
                    {
                        panel->GetContextMenuPos(&p);
                    }
                    else
                    {
                        RECT r;
                        GetWindowRect(panel->GetListBoxHWND(), &r);
                        p.x = r.left;
                        p.y = r.top;
                    }
                }

                if (useSelection) // menu for items in panel (click on item)
                {
                    panel->GetPluginFS()->ContextMenu(panel->GetPluginFS()->GetPluginFSName(),
                                                      panel->GetListBoxHWND(), p.x, p.y, fscmItemsInPanel,
                                                      panelID, count - selectedDirs, selectedDirs);
                }
                else
                {
                    if (onlyPanelMenu) // panel menu (click behind items in panel)
                    {
                        panel->GetPluginFS()->ContextMenu(panel->GetPluginFS()->GetPluginFSName(),
                                                          panel->GetListBoxHWND(), p.x, p.y, fscmPanel,
                                                          panelID, 0, 0);
                    }
                    else // menu for the current path (click on the change-drive button)
                    {
                        panel->GetPluginFS()->ContextMenu(panel->GetPluginFS()->GetPluginFSName(),
                                                          panel->GetListBoxHWND(), p.x, p.y, fscmPathInPanel,
                                                          panelID, 0, 0);
                    }
                }
            }
            else
            {
                if (dragFiles && SalShExtRegistered &&
                    panel->GetPluginFS()->NotEmpty() &&
                    (panel->GetPluginFS()->IsServiceSupported(FS_SERVICE_MOVEFROMFS) || // FS can do "move from FS"
                     panel->GetPluginFS()->IsServiceSupported(FS_SERVICE_COPYFROMFS)))  // FS can do "copy from FS"
                {
                    // if dragging a single subdirectory of FS, determine which one (for changing path
                    // in directory-line and inserting into command-line)
                    int i = -1;
                    if (count == 1)
                        i = indexes[0];
                    else if (count == 0)
                        i = index;
                    if (i >= 0 && i < panel->Dirs->Count)
                    {
                        realDraggedPath[0] = 'D';
                        strcpy(realDraggedPath + 1, panel->GetPluginFS()->GetPluginFSName());
                        strcat(realDraggedPath, ":");
                        int l = (int)strlen(realDraggedPath);
                        if (!panel->GetPluginFS()->GetFullName(panel->Dirs->At(i), 1, realDraggedPath + l, 2 * MAX_PATH - l))
                            realDraggedPath[0] = 0;
                    }
                    else
                    {
                        if (i >= 0 && i >= panel->Dirs->Count && i < panel->Dirs->Count + panel->Files->Count)
                        {
                            realDraggedPath[0] = 'F';
                            strcpy(realDraggedPath + 1, panel->GetPluginFS()->GetPluginFSName());
                            strcat(realDraggedPath, ":");
                            int l = (int)strlen(realDraggedPath);
                            if (!panel->GetPluginFS()->GetFullName(panel->Files->At(i - panel->Dirs->Count),
                                                                   0, realDraggedPath + l, 2 * MAX_PATH - l))
                            {
                                realDraggedPath[0] = 0;
                            }
                        }
                    }

                    BOOL dropDone = FALSE;
                    int operation = SALSHEXT_NONE;
                    DWORD allowedEffects = (panel->GetPluginFS()->IsServiceSupported(FS_SERVICE_MOVEFROMFS) ? DROPEFFECT_MOVE : 0) |
                                           (panel->GetPluginFS()->IsServiceSupported(FS_SERVICE_COPYFROMFS) ? DROPEFFECT_COPY : 0);
                    CPathBuffer srcFSPath;
                    lstrcpyn(srcFSPath, panel->GetPluginFS()->GetPluginFSName(), srcFSPath.Size());
                    strcat(srcFSPath, ":");
                    if (!panel->GetPluginFS()->GetCurrentPath(srcFSPath + strlen(srcFSPath)))
                        srcFSPath[0] = 0;
                    panel->GetPluginFS()->GetAllowedDropEffects(0 /* start */, NULL, &allowedEffects);
                    DoDragFromArchiveOrFS(panel, dropDone, targetPath, operation, realDraggedPath,
                                          allowedEffects, 2 /* FS */, srcFSPath, action == saLeftDragFiles);
                    panel->GetPluginFS()->GetAllowedDropEffects(2 /* end */, NULL, NULL);

                    if (dropDone) // let the operation be performed
                    {
                        char* p = DupStr(targetPath);
                        if (p != NULL)
                            PostMessage(panel->HWindow, WM_USER_DROPFROMFS, (WPARAM)p, operation);
                    }
                }
            }
        }

        // raise thread priority again, operation finished
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);
        // RAII: indexes auto-deleted when scope exits
        EndStopRefresh();
        return;
    }

    if (!panel->Is(ptDisk) && !panel->Is(ptZIPArchive))
    {
        // RAII: indexes auto-deleted when scope exits
        EndStopRefresh();
        return; // just to be safe, don't let other panel types through
    }

#ifndef _WIN64
    CPathBuffer redirectedDir; // Heap-allocated for long path support
    CPathBuffer msg;
#endif // _WIN64
    switch (action)
    {
    case saPermissions:
    case saProperties:
    {
        CALL_STACK_MESSAGE1("ShellAction::properties");
        if (useSelection)
        {
#ifndef _WIN64
            if (ContainsWin64RedirectedDir(panel, (count == 0) ? &index : indexes.get(), (count == 0) ? 1 : count, redirectedDir, TRUE))
            {
                std::wstring errMsg = FormatStrW(LoadStrW(IDS_ERROPENPROPSELCONTW64ALIAS), AnsiToWide(redirectedDir).c_str());
                gPrompter->ShowError(LoadStrW(IDS_ERRORTITLE), errMsg.c_str());
            }
            else
            {
#endif // _WIN64
                CTmpEnumData data;
                data.Indexes = (count == 0) ? &index : indexes.get();
                data.Panel = panel;
                IContextMenu2* menu = CreateIContextMenu2(MainWindow->HWindow, panel->GetPath(),
                                                          (count == 0) ? 1 : count,
                                                          EnumFileNames, &data);
                if (menu != NULL)
                {
                    CShellExecuteWnd shellExecuteWnd;
                    CMINVOKECOMMANDINFOEX ici;
                    ZeroMemory(&ici, sizeof(CMINVOKECOMMANDINFOEX));
                    ici.cbSize = sizeof(CMINVOKECOMMANDINFOEX);
                    ici.fMask = CMIC_MASK_PTINVOKE;
                    ici.hwnd = shellExecuteWnd.Create(MainWindow->HWindow, "SEW: ShellAction::properties");
                    ici.lpVerb = "properties";
                    char pageName[200];
                    if (action == saPermissions)
                    {
                        // force opening Security tab; unfortunately we need to pass string for given OS localization
                        ici.lpParameters = pageName;
                        if (!GetACLUISecurityPageName(pageName, 200))
                            lstrcpy(pageName, "Security"); // if we failed to get the name, use English "Security" and silently won't work in localized versions
                    }
                    ici.lpDirectory = panel->GetPath();
                    ici.nShow = SW_SHOWNORMAL;
                    GetLeftTopCornert(&ici.ptInvoke, posByMouse, useSelection, panel);

                    AuxInvokeAndRelease(menu, (CMINVOKECOMMANDINFO*)&ici);
                }
#ifndef _WIN64
            }
#endif // _WIN64
        }
        break;
    }

    case saCopyToClipboard:
    case saCutToClipboard:
    {
        CALL_STACK_MESSAGE1("ShellAction::copy_cut_clipboard");
        if (useSelection)
        {
#ifndef _WIN64
            if (action == saCutToClipboard &&
                ContainsWin64RedirectedDir(panel, (count == 0) ? &index : indexes.get(), (count == 0) ? 1 : count, redirectedDir, FALSE))
            {
                std::wstring errMsg = FormatStrW(LoadStrW(IDS_ERRCUTSELCONTW64ALIAS), AnsiToWide(redirectedDir).c_str());
                gPrompter->ShowError(LoadStrW(IDS_ERRORTITLE), errMsg.c_str());
            }
            else
            {
#endif // _WIN64
                CTmpEnumData data;
                data.Indexes = (count == 0) ? &index : indexes.get();
                data.Panel = panel;
                IContextMenu2* menu = CreateIContextMenu2(MainWindow->HWindow, panel->GetPath(), (count == 0) ? 1 : count,
                                                          EnumFileNames, &data);
                if (menu != NULL)
                {
                    CShellExecuteWnd shellExecuteWnd;
                    CMINVOKECOMMANDINFO ici;
                    ici.cbSize = sizeof(CMINVOKECOMMANDINFO);
                    ici.fMask = 0;
                    ici.lpVerb = (action == saCopyToClipboard) ? "copy" : "cut";
                    ici.hwnd = shellExecuteWnd.Create(MainWindow->HWindow, "SEW: ShellAction::copy_cut_clipboard verb=%s", ici.lpVerb);
                    ici.lpParameters = NULL;
                    ici.lpDirectory = panel->GetPath();
                    ici.nShow = SW_SHOWNORMAL;
                    ici.dwHotKey = 0;
                    ici.hIcon = 0;

                    AuxInvokeAndRelease(menu, &ici);

                    // clipboard changed, let's verify...
                    IdleRefreshStates = TRUE;  // force state variable check on next Idle
                    IdleCheckClipboard = TRUE; // also check clipboard

                    BOOL repaint = FALSE;
                    if (panel->CutToClipChanged)
                    {
                        // before CUT and COPY clear CutToClip flag
                        panel->ClearCutToClipFlag(FALSE);
                        repaint = TRUE;
                    }
                    CFilesWindow* anotherPanel = MainWindow->LeftPanel == panel ? MainWindow->RightPanel : MainWindow->LeftPanel;
                    BOOL samePaths = panel->Is(ptDisk) && anotherPanel->Is(ptDisk) &&
                                     IsTheSamePath(panel->GetPath(), anotherPanel->GetPath());
                    if (anotherPanel->CutToClipChanged)
                    {
                        // before CUT and COPY also clear CutToClip flag for the other panel
                        anotherPanel->ClearCutToClipFlag(!samePaths);
                    }

                    if (action != saCopyToClipboard)
                    {
                        // in CUT case set file's CutToClip bit (ghosted)
                        int idxCount = count;
                        int* idxs = (idxCount == 0) ? &index : indexes.get();
                        if (idxCount == 0)
                            idxCount = 1;
                        int i;
                        for (i = 0; i < idxCount; i++)
                        {
                            int idx = idxs[i];
                            CFileData* f = (idx < panel->Dirs->Count) ? &panel->Dirs->At(idx) : &panel->Files->At(idx - panel->Dirs->Count);
                            f->CutToClip = 1;
                            f->Dirty = 1;
                            if (samePaths) // mark file/directory in the other panel (quadratic complexity, we don't care...)
                            {
                                if (idx < panel->Dirs->Count) // searching among directories
                                {
                                    int total = anotherPanel->Dirs->Count;
                                    int k;
                                    for (k = 0; k < total; k++)
                                    {
                                        CFileData* f2 = &anotherPanel->Dirs->At(k);
                                        if (StrICmp(f->Name, f2->Name) == 0)
                                        {
                                            f2->CutToClip = 1;
                                            f2->Dirty = 1;
                                            break;
                                        }
                                    }
                                }
                                else // searching among files
                                {
                                    int total = anotherPanel->Files->Count;
                                    int k;
                                    for (k = 0; k < total; k++)
                                    {
                                        CFileData* f2 = &anotherPanel->Files->At(k);
                                        if (StrICmp(f->Name, f2->Name) == 0)
                                        {
                                            f2->CutToClip = 1;
                                            f2->Dirty = 1;
                                            break;
                                        }
                                    }
                                }
                            }
                        }
                        panel->CutToClipChanged = TRUE;
                        if (samePaths)
                            anotherPanel->CutToClipChanged = TRUE;
                        repaint = TRUE;
                    }

                    if (repaint)
                        panel->RepaintListBox(DRAWFLAG_DIRTY_ONLY | DRAWFLAG_SKIP_VISTEST);
                    if (samePaths)
                        anotherPanel->RepaintListBox(DRAWFLAG_DIRTY_ONLY | DRAWFLAG_SKIP_VISTEST);

                    // also set preferred drop effect + origin from Salamander
                    SetClipCutCopyInfo(panel->HWindow, action == saCopyToClipboard, TRUE);
                }
#ifndef _WIN64
            }
#endif // _WIN64
        }
        break;
    }

    case saLeftDragFiles:
    case saRightDragFiles:
    {
        CALL_STACK_MESSAGE1("ShellAction::drag_files");
        if (useSelection)
        {
            CTmpEnumData data;
            data.Indexes = (count == 0) ? &index : indexes.get();
            data.Panel = panel;
            IDataObject* dataObject = CreateIDataObject(MainWindow->HWindow, panel->GetPath(),
                                                        (count == 0) ? 1 : count,
                                                        EnumFileNames, &data);
            CImpIDropSource* dropSource = new CImpIDropSource(FALSE);

            if (dataObject != NULL && dropSource != NULL)
            {
                DWORD dwEffect;
                HRESULT hr;
                DropSourcePanel = panel;
                hr = DoDragDrop(dataObject, dropSource, DROPEFFECT_MOVE | DROPEFFECT_LINK | DROPEFFECT_COPY, &dwEffect);
                DropSourcePanel = NULL;
            }

            ShellActionAux7(dataObject, dropSource);
        }
        break;
    }

    case saContextMenu:
    {
        CALL_STACK_MESSAGE1("ShellAction::context_menu");

        // calculate top-left corner of context menu
        POINT pt;
        GetLeftTopCornert(&pt, posByMouse, useSelection, panel);

        if (panel->Is(ptZIPArchive))
        {
            if (useSelection) // only for items in panel (not for current path in panel)
            {
                // if command states need to be calculated, do it (ArchiveMenu.UpdateItemsState uses them)
                MainWindow->OnEnterIdle();

                // set states according to enablers and open menu
                ArchiveMenu.UpdateItemsState();
                DWORD cmd = ArchiveMenu.Track(MENU_TRACK_RETURNCMD | MENU_TRACK_RIGHTBUTTON,
                                              pt.x, pt.y, panel->GetListBoxHWND(), NULL);
                // send result to main window
                if (cmd != 0)
                    PostMessage(MainWindow->HWindow, WM_COMMAND, cmd, 0);
            }
            else
            {
                if (onlyPanelMenu) // context menu in panel (after items) -> just paste
                {
                    // if command states need to be calculated, do it (ArchivePanelMenu.UpdateItemsState uses them)
                    MainWindow->OnEnterIdle();

                    // set states according to enablers and open menu
                    ArchivePanelMenu.UpdateItemsState();

                    // If it's a paste of type "change directory", display it in Paste item
                    char text[220];
                    char tail[50];
                    tail[0] = 0;

                    strcpy(text, LoadStr(IDS_ARCHIVEMENU_CLIPPASTE));

                    if (EnablerPastePath &&
                        (!panel->Is(ptDisk) || !EnablerPasteFiles) && // PasteFiles has priority
                        !EnablerPasteFilesToArcOrFS)                  // PasteFilesToArcOrFS has priority
                    {
                        char* p = strrchr(text, '\t');
                        if (p != NULL)
                            strcpy(tail, p);
                        else
                            p = text + strlen(text);

                        sprintf(p, " (%s)%s", LoadStr(IDS_PASTE_CHANGE_DIRECTORY), tail);
                    }

                    MENU_ITEM_INFO mii;
                    mii.Mask = MENU_MASK_STRING;
                    mii.String = text;
                    ArchivePanelMenu.SetItemInfo(CM_CLIPPASTE, FALSE, &mii);

                    DWORD cmd = ArchivePanelMenu.Track(MENU_TRACK_RETURNCMD | MENU_TRACK_RIGHTBUTTON,
                                                       pt.x, pt.y, panel->GetListBoxHWND(), NULL);
                    // send result to main window
                    if (cmd != 0)
                        PostMessage(MainWindow->HWindow, WM_COMMAND, cmd, 0);
                }
            }
        }
        else
        {
            BOOL uncRootPath = FALSE;
            if (panel->ContextMenu != NULL) // we got a crash probably caused by recursive call via message-loop in contextPopup.Track (panel->ContextMenu was nulled, probably when leaving inner recursive call)
            {
                TRACE_E("ShellAction::context_menu: panel->ContextMenu must be NULL (probably forbidden recursive call)!");
            }
            else // ptDisk
            {
                HMENU h = CreatePopupMenu();

                UINT flags = CMF_NORMAL | CMF_EXPLORE;
                // handle pressed shift - extended context menu, under W2K there's e.g. Run as...
#define CMF_EXTENDEDVERBS 0x00000100 // rarely used verbs
                BOOL shiftPressed = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
                if (shiftPressed)
                    flags |= CMF_EXTENDEDVERBS;

                if (useSelection && count <= 1)
                    flags |= CMF_CANRENAME;

                BOOL alreadyHaveContextMenu = FALSE;

                if (onlyPanelMenu)
                {
#ifndef _WIN64
                    if (IsWin64RedirectedDir(panel->GetPath(), NULL, TRUE))
                    {
                        gPrompter->ShowError(LoadStrW(IDS_ERRORTITLE), LoadStrW(IDS_ERROPENMENUFORW64ALIAS));
                    }
                    else
                    {
#endif // _WIN64
                        panel->ContextMenu = CreateIContextMenu2(MainWindow->HWindow, panel->GetPath());
                        if (panel->ContextMenu != NULL && h != NULL)
                        {
                            // bypass buggy TortoiseHg shell-extension: it has a global with mapping of menu item IDs
                            // to THg commands, so in our case when two menus are obtained
                            // (panel->ContextMenu and panel->ContextSubmenuNew) the mapping gets overwritten
                            // by the later obtained menu (calling QueryContextMenu), so commands from the earlier
                            // obtained menu can't be invoked, in original version it was menu panel->ContextSubmenuNew,
                            // which contains all commands except Open and Explore for panel context menu:
                            // to work around this problem we use the fact that from menu panel->ContextMenu we take
                            // only Open and Explore, i.e. Windows commands not affected by this bug, so
                            // we just need to obtain menu (call QueryContextMenu) from panel->ContextSubmenuNew as
                            // second in order
                            // NOTE: we can't always do this, because if only New menu is added,
                            //       it's better to obtain menu panel->ContextMenu second,
                            //       so its commands work (e.g. THg doesn't add to New menu at all,
                            //       so no problem arises)
                            ShellActionAux5(flags, panel, h);
                            alreadyHaveContextMenu = TRUE;
                        }
                        GetNewOrBackgroundMenu(MainWindow->HWindow, panel->GetPath(), panel->ContextSubmenuNew, 5000, 6000, TRUE);
                        uncRootPath = IsUNCRootPath(panel->GetPath());
#ifndef _WIN64
                    }
#endif // _WIN64
                }
                else
                {
                    if (useSelection)
                    {
#ifndef _WIN64
                        if (ContainsWin64RedirectedDir(panel, (count == 0) ? &index : indexes.get(), (count == 0) ? 1 : count, redirectedDir, TRUE))
                        {
                            std::wstring errMsg = FormatStrW(LoadStrW(IDS_ERROPENMENUSELCONTW64ALIAS), AnsiToWide(redirectedDir).c_str());
                            gPrompter->ShowError(LoadStrW(IDS_ERRORTITLE), errMsg.c_str());
                        }
                        else
                        {
#endif // _WIN64
                            CTmpEnumData data;
                            data.Indexes = (count == 0) ? &index : indexes.get();
                            data.Panel = panel;
                            panel->ContextMenu = CreateIContextMenu2(MainWindow->HWindow, panel->GetPath(), (count == 0) ? 1 : count,
                                                                     EnumFileNames, &data);
#ifndef _WIN64
                        }
#endif // _WIN64
                    }
                    else
                    {
#ifndef _WIN64
                        if (IsWin64RedirectedDir(panel->GetPath(), NULL, TRUE))
                        {
                            gPrompter->ShowError(LoadStrW(IDS_ERRORTITLE), LoadStrW(IDS_ERROPENMENUFORW64ALIAS));
                        }
                        else
                        {
#endif // _WIN64
                            panel->ContextMenu = CreateIContextMenu2(MainWindow->HWindow, panel->GetPath());
                            GetNewOrBackgroundMenu(MainWindow->HWindow, panel->GetPath(), panel->ContextSubmenuNew, 5000, 6000, FALSE);
                            uncRootPath = IsUNCRootPath(panel->GetPath());
#ifndef _WIN64
                        }
#endif // _WIN64
                    }
                }

                BOOL clipCopy = FALSE;     // is it "our copy"?
                BOOL clipCut = FALSE;      // is it "our cut"?
                BOOL cmdDelete = FALSE;    // is it "our delete"?
                BOOL cmdMapNetDrv = FALSE; // is it "our Map Network Drive"? (only UNC root, we don't want to complicate things)
                DWORD cmd = 0;             // command number for context menu (10000 = "our paste")
                CPathBuffer pastePath; // Heap-allocated for long path support; buffer for path where "our paste" will be performed (if it happens)
                if (panel->ContextMenu != NULL && h != NULL)
                {
                    if (!alreadyHaveContextMenu)
                        ShellActionAux5(flags, panel, h);
                    RemoveUselessSeparatorsFromMenu(h);

                    char cmdName[2000]; // intentionally 2000 instead of 200, shell-extensions sometimes write double (reasoning: unicode = 2 * "character count"), etc.
                    if (onlyPanelMenu)
                    {
                        if (panel->ContextSubmenuNew->MenuIsAssigned())
                        {
                            HMENU bckgndMenu = panel->ContextSubmenuNew->GetMenu();
                            int bckgndMenuInsert = 0;
                            if (useSelection)
                                TRACE_E("Unexpected value in 'useSelection' (TRUE) in ShellAction(saContextMenu).");
                            int miCount = GetMenuItemCount(h);
                            MENUITEMINFO mi;
                            char itemName[500];
                            int i;
                            for (i = 0; i < miCount; i++)
                            {
                                memset(&mi, 0, sizeof(mi)); // necessary here
                                mi.cbSize = sizeof(mi);
                                mi.fMask = MIIM_STATE | MIIM_TYPE | MIIM_ID | MIIM_SUBMENU;
                                mi.dwTypeData = itemName;
                                mi.cch = 500;
                                if (GetMenuItemInfo(h, i, TRUE, &mi))
                                {
                                    if (mi.hSubMenu == NULL && (mi.fType & MFT_SEPARATOR) == 0) // not submenu nor separator
                                    {
                                        if (AuxGetCommandString(panel->ContextMenu, mi.wID, GCS_VERB, NULL, cmdName, 200) == NOERROR)
                                        {
                                            if (stricmp(cmdName, "explore") == 0 || stricmp(cmdName, "open") == 0)
                                            {
                                                InsertMenuItem(bckgndMenu, bckgndMenuInsert++, TRUE, &mi);
                                                if (bckgndMenuInsert == 2)
                                                    break; // we don't need more items from here
                                            }
                                        }
                                    }
                                }
                                else
                                {
                                    DWORD err = GetLastError();
                                    TRACE_E("Unable to get item information from menu: " << GetErrorText(err));
                                }
                            }
                            if (bckgndMenuInsert > 0) // separate Explore + Open from rest of menu
                            {
                                // separator
                                mi.cbSize = sizeof(mi);
                                mi.fMask = MIIM_TYPE;
                                mi.fType = MFT_SEPARATOR;
                                mi.dwTypeData = NULL;
                                InsertMenuItem(bckgndMenu, bckgndMenuInsert++, TRUE, &mi);
                            }

                            /* used by export_mnu.py script which generates salmenu.mnu for Translator
   keep synchronized with InsertMenuItem() calls below...
MENU_TEMPLATE_ITEM PanelBkgndMenu[] =
{
  {MNTT_PB, 0
  {MNTT_IT, IDS_MENU_EDIT_PASTE
  {MNTT_IT, IDS_PASTE_CHANGE_DIRECTORY
  {MNTT_IT, IDS_MENU_EDIT_PASTELINKS   
  {MNTT_PE, 0
};
*/

                            // add Paste command (if it's a paste of type "change directory", display it in Paste item)
                            char tail[50];
                            tail[0] = 0;
                            strcpy(itemName, LoadStr(IDS_MENU_EDIT_PASTE));
                            if (EnablerPastePath && !EnablerPasteFiles) // PasteFiles has priority
                            {
                                char* p = strrchr(itemName, '\t');
                                if (p != NULL)
                                    strcpy(tail, p);
                                else
                                    p = itemName + strlen(itemName);

                                sprintf(p, " (%s)%s", LoadStr(IDS_PASTE_CHANGE_DIRECTORY), tail);
                            }
                            mi.cbSize = sizeof(mi);
                            mi.fMask = MIIM_STATE | MIIM_ID | MIIM_TYPE;
                            mi.fType = MFT_STRING;
                            mi.fState = EnablerPastePath || EnablerPasteFiles ? MFS_ENABLED : MFS_DISABLED;
                            mi.dwTypeData = itemName;
                            mi.wID = 10000;
                            InsertMenuItem(bckgndMenu, bckgndMenuInsert++, TRUE, &mi);

                            // add Paste Shortcuts command
                            mi.cbSize = sizeof(mi);
                            mi.fMask = MIIM_STATE | MIIM_ID | MIIM_TYPE;
                            mi.fType = MFT_STRING;
                            mi.fState = EnablerPasteLinksOnDisk ? MFS_ENABLED : MFS_DISABLED;
                            mi.dwTypeData = LoadStr(IDS_MENU_EDIT_PASTELINKS);
                            mi.wID = 10001;
                            InsertMenuItem(bckgndMenu, bckgndMenuInsert++, TRUE, &mi);

                            // if not already there, insert separator
                            MENUITEMINFO mi2;
                            memset(&mi2, 0, sizeof(mi2));
                            mi2.cbSize = sizeof(mi);
                            mi2.fMask = MIIM_TYPE;
                            if (!GetMenuItemInfo(bckgndMenu, bckgndMenuInsert, TRUE, &mi2) ||
                                (mi2.fType & MFT_SEPARATOR) == 0)
                            {
                                mi.cbSize = sizeof(mi);
                                mi.fMask = MIIM_TYPE;
                                mi.fType = MFT_SEPARATOR;
                                mi.dwTypeData = NULL;
                                InsertMenuItem(bckgndMenu, bckgndMenuInsert++, TRUE, &mi);
                            }

                            DestroyMenu(h);
                            h = bckgndMenu;
                        }
                    }
                    else
                    {
                        // originally adding New item was called before ShellActionAux5, but
                        // under Windows XP calling ShellActionAux5 caused deletion of New item
                        // (in case Edit/Copy operation was performed first)
                        if (panel->ContextSubmenuNew->MenuIsAssigned())
                        {
                            MENUITEMINFO mi;

                            // separator
                            mi.cbSize = sizeof(mi);
                            mi.fMask = MIIM_TYPE;
                            mi.fType = MFT_SEPARATOR;
                            mi.dwTypeData = NULL;
                            InsertMenuItem(h, -1, TRUE, &mi);

                            // New submenu
                            mi.cbSize = sizeof(mi);
                            mi.fMask = MIIM_STATE | MIIM_SUBMENU | MIIM_TYPE;
                            mi.fType = MFT_STRING;
                            mi.fState = MFS_ENABLED;
                            mi.hSubMenu = panel->ContextSubmenuNew->GetMenu();
                            mi.dwTypeData = LoadStr(IDS_MENUNEWTITLE);
                            InsertMenuItem(h, -1, TRUE, &mi);
                        }
                    }

                    if (GetMenuItemCount(h) > 0) // protection against completely stripped menu
                    {
                        CMenuPopup contextPopup;
                        contextPopup.SetTemplateMenu(h);
                        cmd = contextPopup.Track(MENU_TRACK_RETURNCMD | MENU_TRACK_RIGHTBUTTON,
                                                 pt.x, pt.y, panel->GetListBoxHWND(), NULL);
                        //            for testing only -- show menu via Windows API
                        //            cmd = TrackPopupMenuEx(h, TPM_RETURNCMD | TPM_LEFTALIGN |
                        //                                   TPM_LEFTBUTTON, pt.x, pt.y, panel->GetListBoxHWND(), NULL);
                    }
                    else
                        cmd = 0;
                    if (cmd != 0)
                    {
                        CALL_STACK_MESSAGE1("ShellAction::context_menu::exec0");
                        if (cmd < 5000)
                        {
                            if (AuxGetCommandString(panel->ContextMenu, cmd, GCS_VERB, NULL, cmdName, 200) != NOERROR)
                            {
                                cmdName[0] = 0;
                            }
                        }
                        if (cmd == 10000 || cmd == 10001)
                            strcpy(pastePath, panel->GetPath());
                        if (cmd < 5000 && stricmp(cmdName, "paste") == 0 && count <= 1)
                        {
                            if (useSelection) // paste into subdirectory of panel->GetPath()
                            {
                                int specialIndex;
                                if (count == 1) // select
                                {
                                    panel->GetSelItems(1, &specialIndex);
                                }
                                else
                                    specialIndex = panel->GetCaretIndex(); // focus
                                if (specialIndex >= 0 && specialIndex < panel->Dirs->Count)
                                {
                                    const char* subdir = panel->Dirs->At(specialIndex).Name;
                                    strcpy(pastePath, panel->GetPath());
                                    char* s = pastePath + strlen(pastePath);
                                    if (s > pastePath && *(s - 1) != '\\')
                                        *s++ = '\\';
                                    strcpy(s, subdir);
                                    cmd = 10000; // command will be executed elsewhere
                                }
                            }
                            else // paste into panel->GetPath()
                            {
                                strcpy(pastePath, panel->GetPath());
                                cmd = 10000; // command will be executed elsewhere
                            }
                        }
                        clipCopy = (cmd < 5000 && stricmp(cmdName, "copy") == 0);
                        clipCut = (cmd < 5000 && stricmp(cmdName, "cut") == 0);
                        cmdDelete = useSelection && (cmd < 5000 && stricmp(cmdName, "delete") == 0);

                        // Map Network Drive command is 40 under XP, 43 under W2K, and only under Vista it has defined cmdName
                        cmdMapNetDrv = uncRootPath && (stricmp(cmdName, "connectNetworkDrive") == 0 ||
                                                       !WindowsVistaAndLater && cmd == 40);

                        if (cmd != 10000 && cmd != 10001 && !clipCopy && !clipCut && !cmdDelete && !cmdMapNetDrv)
                        {
                            if (cmd < 5000 && stricmp(cmdName, "rename") == 0)
                            {
                                int specialIndex;
                                if (count == 1) // select
                                {
                                    panel->GetSelItems(1, &specialIndex);
                                }
                                else
                                    specialIndex = -1;           // focus
                                panel->RenameFile(specialIndex); // only disk is considered (enabling "Rename" is not needed)
                            }
                            else
                            {
                                BOOL releaseLeft = FALSE;                  // disconnect left panel from disk?
                                BOOL releaseRight = FALSE;                 // disconnect right panel from disk?
                                if (!useSelection && cmd < 5000 &&         // it's a context menu for directory
                                    stricmp(cmdName, "properties") != 0 && // not necessary for properties
                                    stricmp(cmdName, "find") != 0 &&       // not necessary for find
                                    stricmp(cmdName, "open") != 0 &&       // not necessary for open
                                    stricmp(cmdName, "explore") != 0 &&    // not necessary for explore
                                    stricmp(cmdName, "link") != 0)         // not necessary for create-short-cut
                                {
                                    CPathBuffer root;  // Heap-allocated for long path support
                                    GetRootPath(root, panel->GetPath());
                                    if (strlen(root) >= strlen(panel->GetPath())) // menu for entire disk - due to commands like
                                    {                                             // for "format..." we must "hands off" the media
                                        CFilesWindow* win;
                                        int i;
                                        for (i = 0; i < 2; i++)
                                        {
                                            win = i == 0 ? MainWindow->LeftPanel : MainWindow->RightPanel;
                                            if (HasTheSameRootPath(win->GetPath(), root)) // stejny disk (UNC i normal)
                                            {
                                                if (i == 0)
                                                    releaseLeft = TRUE;
                                                else
                                                    releaseRight = TRUE;
                                            }
                                        }
                                    }
                                }

                                CALL_STACK_MESSAGE1("ShellAction::context_menu::exec1");
                                if (!useSelection || count == 0 && index < panel->Dirs->Count ||
                                    count == 1 && indexes[0] < panel->Dirs->Count)
                                {
                                    SetCurrentDirectoryToSystem(); // so disk from panel can be unmapped
                                }
                                else
                                {
                                    EnvSetCurrentDirectoryA(gEnvironment, panel->GetPath()); // for files with spaces in name: so Open With works for Microsoft Paint too (failed under W2K - wrote "d:\documents.bmp was not found" for file "D:\Documents and Settings\petr\My Documents\example.bmp")
                                }

                                DWORD disks = GetLogicalDrives();

                                CShellExecuteWnd shellExecuteWnd;
                                CMINVOKECOMMANDINFOEX ici;
                                ZeroMemory(&ici, sizeof(CMINVOKECOMMANDINFOEX));
                                ici.cbSize = sizeof(CMINVOKECOMMANDINFOEX);
                                ici.fMask = CMIC_MASK_PTINVOKE;
                                if (CanUseShellExecuteWndAsParent(cmdName))
                                    ici.hwnd = shellExecuteWnd.Create(MainWindow->HWindow, "SEW: ShellAction::context_menu cmd=%d", cmd);
                                else
                                    ici.hwnd = MainWindow->HWindow;
                                if (cmd < 5000)
                                    ici.lpVerb = MAKEINTRESOURCE(cmd);
                                else
                                    ici.lpVerb = MAKEINTRESOURCE(cmd - 5000);
                                ici.lpDirectory = panel->GetPath();
                                ici.nShow = SW_SHOWNORMAL;
                                ici.ptInvoke = pt;

                                panel->FocusFirstNewItem = TRUE; // both for WinZip and its archives, and for New menu (works well only with panel autorefresh)
                                if (cmd < 5000)
                                {
                                    BOOL changeToFixedDrv = cmd == 35; // "format" is not modal, change to fixed drive necessary
                                    if (releaseLeft)
                                    {
                                        if (changeToFixedDrv)
                                        {
                                            MainWindow->LeftPanel->ChangeToFixedDrive(MainWindow->LeftPanel->HWindow);
                                        }
                                        else
                                            MainWindow->LeftPanel->HandsOff(TRUE);
                                    }
                                    if (releaseRight)
                                    {
                                        if (changeToFixedDrv)
                                        {
                                            MainWindow->RightPanel->ChangeToFixedDrive(MainWindow->RightPanel->HWindow);
                                        }
                                        else
                                            MainWindow->RightPanel->HandsOff(TRUE);
                                    }

                                    AuxInvokeCommand(panel, (CMINVOKECOMMANDINFO*)&ici);

                                    // we catch cut/copy/paste, but to be safe we still refresh clipboard enablers
                                    IdleRefreshStates = TRUE;  // force state variable check on next Idle
                                    IdleCheckClipboard = TRUE; // also check clipboard

                                    if (releaseLeft && !changeToFixedDrv)
                                        MainWindow->LeftPanel->HandsOff(FALSE);
                                    if (releaseRight && !changeToFixedDrv)
                                        MainWindow->RightPanel->HandsOff(FALSE);

                                    //---  refresh non-automatically refreshed directories
                                    // report change in current directory and its subdirectories (just to be safe, who knows what was launched)
                                    MainWindow->PostChangeOnPathNotification(panel->GetPath(), TRUE);
                                }
                                else
                                {
                                    if (panel->ContextSubmenuNew->MenuIsAssigned()) // exception could have occurred
                                    {
                                        AuxInvokeCommand2(panel, (CMINVOKECOMMANDINFO*)&ici);

                                        //---  refresh non-automatically refreshed directories
                                        // report change in current directory (new file/directory can probably only be created in it)
                                        MainWindow->PostChangeOnPathNotification(panel->GetPath(), FALSE);
                                    }
                                }

                                if (GetLogicalDrives() < disks) // odmapovani
                                {
                                    if (MainWindow->LeftPanel->CheckPath(FALSE) != ERROR_SUCCESS)
                                        MainWindow->LeftPanel->ChangeToRescuePathOrFixedDrive(MainWindow->LeftPanel->HWindow);
                                    if (MainWindow->RightPanel->CheckPath(FALSE) != ERROR_SUCCESS)
                                        MainWindow->RightPanel->ChangeToRescuePathOrFixedDrive(MainWindow->RightPanel->HWindow);
                                }
                            }
                        }
                    }
                }
                {
                    CALL_STACK_MESSAGE1("ShellAction::context_menu::release");
                    ShellActionAux6(panel);
                    if (h != NULL)
                        DestroyMenu(h);
                }

                if (cmd == 10000) // our own "paste" to pastePath
                {
                    if (!panel->ClipboardPaste(FALSE, FALSE, pastePath))
                        panel->ClipboardPastePath(); // classic paste failed, we probably just need to change current path
                }
                else
                {
                    if (cmd == 10001) // our own "paste shortcuts" to pastePath
                    {
                        panel->ClipboardPaste(TRUE, FALSE, pastePath);
                    }
                    else
                    {
                        if (clipCopy) // our own "copy"
                        {
                            panel->ClipboardCopy(); // recursive call to ShellAction
                        }
                        else
                        {
                            if (clipCut) // our own "cut"
                            {
                                panel->ClipboardCut(); // recursive call to ShellAction
                            }
                            else
                            {
                                if (cmdDelete)
                                {
                                    PostMessage(MainWindow->HWindow, WM_COMMAND, CM_DELETEFILES, 0);
                                }
                                else
                                {
                                    if (cmdMapNetDrv) // is it "our Map Network Drive"? (only UNC root, we don't want to complicate things)
                                    {
                                        panel->ConnectNet(TRUE);
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
        break;
    }
    }
    // RAII: indexes auto-deleted when scope exits
    EndStopRefresh();
}

const char* ReturnNameFromParam(int, void* param)
{
    return (const char*)param;
}

void ExecuteAssociationAux(IContextMenu2* menu, CMINVOKECOMMANDINFO& ici)
{
    CALL_STACK_MESSAGE_NONE

    // temporarily lower thread priority, so some confused shell extension doesn't eat CPU
    HANDLE hThread = GetCurrentThread(); // pseudo-handle, no need to release
    int oldThreadPriority = GetThreadPriority(hThread);
    SetThreadPriority(hThread, THREAD_PRIORITY_NORMAL);

    __try
    {
        menu->InvokeCommand(&ici);
    }
    __except (CCallStack::HandleException(GetExceptionInformation(), 21))
    {
        ICExceptionHasOccured++;
    }

    SetThreadPriority(hThread, oldThreadPriority);
}

void ExecuteAssociationAux2(IContextMenu2* menu, HMENU h, DWORD flags)
{
    CALL_STACK_MESSAGE_NONE

    // temporarily lower thread priority, so some confused shell extension doesn't eat CPU
    HANDLE hThread = GetCurrentThread(); // pseudo-handle, no need to release
    int oldThreadPriority = GetThreadPriority(hThread);
    SetThreadPriority(hThread, THREAD_PRIORITY_NORMAL);

    __try
    {
        menu->QueryContextMenu(h, 0, 0, -1, flags);
    }
    __except (CCallStack::HandleException(GetExceptionInformation(), 22))
    {
        QCMExceptionHasOccured++;
    }

    SetThreadPriority(hThread, oldThreadPriority);
}

void ExecuteAssociationAux3(IContextMenu2* menu)
{
    __try
    {
        menu->Release();
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        RelExceptionHasOccured++;
    }
}

extern DWORD ExecuteAssociationTlsIndex; // allows only one call at a time (prevents recursion) in each thread

// Wide version for Unicode filenames - uses ShellExecuteExW directly
void ExecuteAssociationW(HWND hWindow, const char* path, const wchar_t* nameW)
{
    CALL_STACK_MESSAGE2("ExecuteAssociationW(, %s, <wide>)", path);

    if (ExecuteAssociationTlsIndex == TLS_OUT_OF_INDEXES ||
        TlsGetValue(ExecuteAssociationTlsIndex) == 0)
    {
        if (ExecuteAssociationTlsIndex != TLS_OUT_OF_INDEXES)
            TlsSetValue(ExecuteAssociationTlsIndex, (void*)1);

        // Build wide full path
        wchar_t fullPathW[SAL_MAX_LONG_PATH];
        wchar_t pathW[SAL_MAX_LONG_PATH];

        // Convert ANSI path to wide
        MultiByteToWideChar(CP_ACP, 0, path, -1, pathW, SAL_MAX_LONG_PATH);

        // Build full path: path + nameW
        wcscpy(fullPathW, pathW);
        int len = (int)wcslen(fullPathW);
        if (len > 0 && fullPathW[len - 1] != L'\\')
        {
            fullPathW[len] = L'\\';
            fullPathW[len + 1] = L'\0';
        }
        wcscat(fullPathW, nameW);

        // Use ShellExecuteExW for Unicode filenames
        SHELLEXECUTEINFOW sei = {0};
        sei.cbSize = sizeof(sei);
        sei.fMask = SEE_MASK_FLAG_NO_UI;
        sei.hwnd = hWindow;
        sei.lpVerb = NULL; // default verb (open)
        sei.lpFile = fullPathW;
        sei.lpDirectory = pathW;
        sei.nShow = SW_SHOWNORMAL;
        ShellExecuteExW(&sei);

        if (ExecuteAssociationTlsIndex != TLS_OUT_OF_INDEXES)
            TlsSetValue(ExecuteAssociationTlsIndex, (void*)0);
    }
}

void ExecuteAssociation(HWND hWindow, const char* path, const char* name)
{
    CALL_STACK_MESSAGE3("ExecuteAssociation(, %s, %s)", path, name);

    if (ExecuteAssociationTlsIndex == TLS_OUT_OF_INDEXES || // TLS not allocated (always false)
        TlsGetValue(ExecuteAssociationTlsIndex) == 0)       // not a recursive call
    {
        if (ExecuteAssociationTlsIndex != TLS_OUT_OF_INDEXES) // new call is not possible
            TlsSetValue(ExecuteAssociationTlsIndex, (void*)1);

        //  MainWindow->ReleaseMenuNew();  // Windows aren't designed for multiple context menus

        if (Configuration.UseSalOpen)
        {
            // try to open association via salopen.exe
            CPathBuffer execName;
            strcpy(execName, path);
            if (SalPathAppend(execName, name, execName.Size()) && SalOpenExecute(hWindow, execName))
            {
                if (ExecuteAssociationTlsIndex != TLS_OUT_OF_INDEXES) // new call is now possible
                    TlsSetValue(ExecuteAssociationTlsIndex, (void*)0);
                return; // done, it started in salopen.exe process
            }

            // if salopen.exe fails, we start the classic way (danger of open handles in directory)
        }

        IContextMenu2* menu = CreateIContextMenu2(hWindow, path, 1,
                                                  ReturnNameFromParam, (void*)name);
        if (menu != NULL)
        {
            CALL_STACK_MESSAGE1("ExecuteAssociation::1");
            HMENU h = CreatePopupMenu();
            if (h != NULL)
            {
                DWORD flags = CMF_DEFAULTONLY | ((GetKeyState(VK_SHIFT) & 0x8000) ? CMF_EXPLORE : 0);
                ExecuteAssociationAux2(menu, h, flags);

                UINT cmd = GetMenuDefaultItem(h, FALSE, GMDI_GOINTOPOPUPS);
                if (cmd == -1) // we didn't find default item -> try searching only among verbs
                {
                    DestroyMenu(h);
                    h = CreatePopupMenu();
                    if (h != NULL)
                    {
                        ExecuteAssociationAux2(menu, h, CMF_VERBSONLY | CMF_DEFAULTONLY);

                        cmd = GetMenuDefaultItem(h, FALSE, GMDI_GOINTOPOPUPS);
                        if (cmd == -1)
                            cmd = 0; // try "default verb" (index 0)
                    }
                }
                if (cmd != -1)
                {
                    CShellExecuteWnd shellExecuteWnd;
                    CMINVOKECOMMANDINFO ici;
                    ici.cbSize = sizeof(CMINVOKECOMMANDINFO);
                    ici.fMask = 0;
                    ici.hwnd = shellExecuteWnd.Create(hWindow, "SEW: ExecuteAssociation cmd=%d", cmd);
                    ici.lpVerb = MAKEINTRESOURCE(cmd);
                    ici.lpParameters = NULL;
                    ici.lpDirectory = path;
                    ici.nShow = SW_SHOWNORMAL;
                    ici.dwHotKey = 0;
                    ici.hIcon = 0;

                    CALL_STACK_MESSAGE1("ExecuteAssociation::2");
                    ExecuteAssociationAux(menu, ici);
                }
                DestroyMenu(h);
            }
            CALL_STACK_MESSAGE1("ExecuteAssociation::3");
            ExecuteAssociationAux3(menu);
        }
        else
        {
            // Shell IContextMenu doesn't support long paths (> MAX_PATH).
            // Fall back to ShellExecuteEx which may work on Windows 10+
            CPathBuffer fullPath;
            strcpy(fullPath, path);
            if (SalPathAppend(fullPath, name, fullPath.Size()))
            {
                SHELLEXECUTEINFO sei = {0};
                sei.cbSize = sizeof(sei);
                sei.fMask = SEE_MASK_FLAG_NO_UI;
                sei.hwnd = hWindow;
                sei.lpVerb = NULL; // default verb (open)
                sei.lpFile = fullPath;
                sei.lpDirectory = path;
                sei.nShow = SW_SHOWNORMAL;
                ShellExecuteEx(&sei);
            }
        }

        if (ExecuteAssociationTlsIndex != TLS_OUT_OF_INDEXES) // new call is now possible
            TlsSetValue(ExecuteAssociationTlsIndex, (void*)0);
    }
    else
    {
        // TRACE_E("Attempt to call ExecuteAssociation() recursively! (skipping this call...)");
        // ask whether Salamander should continue or generate bug report
        if (SalMessageBox(hWindow, LoadStr(IDS_SHELLEXTBREAK4), SALAMANDER_TEXT_VERSION,
                          MSGBOXEX_CONTINUEABORT | MB_ICONINFORMATION | MSGBOXEX_SETFOREGROUND) == IDABORT)
        { // we break
            strcpy(BugReportReasonBreak, "Attempt to call ExecuteAssociation() recursively.");
            TaskList.FireEvent(TASKLIST_TODO_BREAK, GetCurrentProcessId());
            // freeze this thread
            while (1)
                Sleep(1000);
        }
    }
}

// returns TRUE if it's "safe" to provide shell extension a special invisible window as parent,
// which shell extension can then e.g. destroy via DestroyWindow (which normally closes Explorer, but crashed Salamander)
// there are exceptions when main Salamander window must be passed as parent
BOOL CanUseShellExecuteWndAsParent(const char* cmdName)
{
    // for Map Network Drive we can't use shellExecuteWnd, otherwise it hangs (MainWindows->HWindow gets disabled and Map Network Drive window doesn't open)
    if (WindowsVistaAndLater && stricmp(cmdName, "connectNetworkDrive") == 0)
        return FALSE;

    // under Windows 8 Open With was problematic - when choosing custom program, Open dialog didn't appear
    // https://forum.altap.cz/viewtopic.php?f=16&t=6730 and https://forum.altap.cz/viewtopic.php?t=6782
    // the problem is that code returns from invoke, but later MS accesses the parent window which we already destroyed
    // TODO: a solution would be to keep ShellExecuteWnd alive (child window, stretched over entire Salamander area, completely in its background)
    // we would just verify it's alive (that someone didn't destroy it) before passing it
    // TODO2: I tried the proposal as exercise and under W8 with Open With it doesn't work, Open dialog is not modal to our main window (or Find window)
    // for now we'll pass main Salamander window in this case
    if (Windows8AndLater && stricmp(cmdName, "openas") == 0)
        return FALSE;

    // for other cases (majority) ShellExecuteWnd can be used
    return TRUE;
}

/*
//const char *EnumFileNamesFunction_OneFile(int index, void *param)
//{
//  return (const char *)param;
//}

BOOL MakeFileAvailOfflineIfOneDriveOnWin81(HWND parent, const char *name)
{
  CALL_STACK_MESSAGE2("MakeFileAvailOfflineIfOneDriveOnWin81(, %s)", name);

  BOOL ret = TRUE;    // WARNING: support for OneDriveBusinessStorages is missing, add if needed !!!
  if (Windows8_1AndLater && OneDrivePath[0] != 0)
  {
    CPathBuffer path; // Heap-allocated for long path support
    char *cutName;
    strcpy_s(path, path.Size(), name);
    if (CutDirectory(path, &cutName) && SalPathIsPrefix(OneDrivePath, path)) // we handle this only under OneDrive folder
    {
      BOOL makeOffline = FALSE;
      WIN32_FIND_DATAW findData;
      HANDLE hFind = HANDLES_Q(FindFirstFileW(AnsiToWide(name).c_str(), &findData));
      if (hFind != INVALID_HANDLE_VALUE)
      {
        makeOffline = IsFilePlaceholderW(&findData);
        FindClose(hFind);
      }

      if (makeOffline)  // convert file to offline
      {
        // this stupid approach doesn't work, it's asynchronous and the offline conversion (download from network)
        // can take up to a minute or not happen at all, we have no control over it,
        // we'll wait until it works somehow via Win32 API
//        IContextMenu2 *menu = CreateIContextMenu2(parent, path, 1, EnumFileNamesFunction_OneFile, cutName);
//        if (menu != NULL)
//        {
//          CShellExecuteWnd shellExecuteWnd;
//          CMINVOKECOMMANDINFO ici;
//          ici.cbSize = sizeof(CMINVOKECOMMANDINFO);
//          ici.fMask = 0;
//          ici.hwnd = shellExecuteWnd.Create(parent, "SEW: MakeFileAvailOfflineIfOneDriveOnWin81");
//          ici.lpVerb = "MakeAvailableOffline";
//          ici.lpParameters = NULL;
//          ici.lpDirectory = path;
//          ici.nShow = SW_SHOWNORMAL;
//          ici.dwHotKey = 0;
//          ici.hIcon = 0;
//
//          TRACE_I("SafeInvokeCommand");
//          ret = SafeInvokeCommand(menu, ici);
//
//          menu->Release();
//        }
      }
    }
  }
  return ret;
}
*/
