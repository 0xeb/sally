// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"

#include "menu.h"
#include "menu_item_text.h"
#include "ui/IPrompter.h"
#include "common/unicode/helpers.h"
#include "common/Win32TextCodec.h"
#include "common/unicode/PanelPathPolicy.h"
#include "common/clipboard/ClipboardOwnershipPolicy.h"
#include "common/clipboard/HDropWideDataObject.h"
#include "common/clipboard/ShellSelectionDataObject.h"
#include "common/widepath.h"
#include "common/fsutil.h"
#include "common/IEnvironment.h"
#include "common/IClipboard.h"
#include "common/IFileSystem.h"
#include "common/FixedUtf16Buffer.h"
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
#include "shellsup_diag.h"
//#include "drivelst.h"

#include <vector>

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
        const wchar_t *item1 = NULL, *item2 = NULL, *item3 = NULL, *item4 = NULL;
        if (effect & DROPEFFECT_MOVE)
            item1 = LoadStrW(IDS_DROPMOVE);
        if (effect & DROPEFFECT_COPY)
            item2 = LoadStrW(IDS_DROPCOPY);
        if (effect & DROPEFFECT_LINK)
            item3 = LoadStrW(IDS_DROPLINK);
        if (item1 == NULL && item2 == NULL && item3 == NULL)
            item4 = LoadStrW(IDS_DROPUNKNOWN);

        if ((item1 == NULL || AppendMenuW(menu, MF_ENABLED | MF_STRING, 1, item1)) &&
            (item2 == NULL || AppendMenuW(menu, MF_ENABLED | MF_STRING, 2, item2)) &&
            (item3 == NULL || AppendMenuW(menu, MF_ENABLED | MF_STRING, 3, item3)) &&
            (item4 == NULL || AppendMenuW(menu, MF_ENABLED | MF_STRING | MF_DEFAULT,
                                          4, item4)) &&
            AppendMenuW(menu, MF_SEPARATOR, 0, NULL) &&
            AppendMenuW(menu, MF_ENABLED | MF_STRING | MF_DEFAULT, 5, LoadStrW(IDS_DROPCANCEL)))
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
                MENUITEMINFOW item;
                memset(&item, 0, sizeof(item));
                item.cbSize = sizeof(item);
                item.fMask = MIIM_STATE;
                item.fState = MFS_DEFAULT | MFS_ENABLED;
                SetMenuItemInfoW(menu, defItem, FALSE, &item);
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

BOOL DoCopyMove(BOOL copy, const wchar_t* targetDir, CCopyMoveData* data, void* param)
{
    CFilesWindow* panel = (CFilesWindow*)param;

    CTmpDropData* tmp = new CTmpDropData;
    if (tmp != NULL)
    {
        tmp->Copy = copy;
        tmp->TargetPath = targetDir != NULL ? targetDir : L"";
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

void DoDragDropOper(BOOL copy, BOOL toArchive, const wchar_t* archiveOrFSName, const wchar_t* archivePathOrUserPart,
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
            tmp->ArchiveOrFSName = archiveOrFSName;
            tmp->ArchivePathOrUserPart = archivePathOrUserPart;
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

void DoGetFSToFSDropEffect(const wchar_t* srcFSPath, const wchar_t* tgtFSPath,
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

static const wchar_t* ReturnDropPath(CFilesWindow* panel)
{
    return panel->DropPathW.c_str();
}

const wchar_t* GetCurrentDir(POINTL& pt, void* param, DWORD* effect, BOOL rButton, BOOL& isTgtFile,
                             DWORD keyState, int& tgtType, int srcType)
{
    CFilesWindow* panel = (CFilesWindow*)param;
    isTgtFile = FALSE; // not a drop target file yet -> we can handle the operation ourselves
    tgtType = idtttWindows;

    panel->DropPathW.clear();

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
                    index == 0 && panel->Dirs->Count > 0 && wcscmp(panel->Dirs->At(0).Name, L"..") == 0 &&
                        (panel->GetZIPPath()[0] == 0 || panel->GetZIPPath()[0] == '\\' && panel->GetZIPPath()[1] == 0)) // drop to disk path
                {
                    tgtType = idtttArchive;
                    DWORD origEffect = *effect;
                    *effect &= (DROPEFFECT_MOVE | DROPEFFECT_COPY); // trim effect to copy+move

                    if (index >= 0 && index < panel->Dirs->Count) // drop on directory
                    {
                        panel->SetDropTarget(index);
                        panel->DropPathW = panel->GetZIPPath();
                        if (index == 0 && wcscmp(panel->Dirs->At(index).Name, L"..") == 0)
                        {
                            while (!panel->DropPathW.empty() && panel->DropPathW.back() == L'\\')
                                panel->DropPathW.pop_back();
                            if (panel->DropPathW.empty()) // drop-path will be disk (".." leads out of archive)
                            {
                                tgtType = idtttWindows;
                                *effect = origEffect;
                                panel->DropPathW = panel->GetZIPArchive();
                                if (CutDirectoryW(panel->DropPathW))
                                    SalPathAddBackslashW(panel->DropPathW);
                                else
                                    panel->DropPathW.clear();
                            }
                            else
                            {
                                if (!CutDirectoryW(panel->DropPathW))
                                    panel->DropPathW.clear();
                            }
                        }
                        else
                            SalPathAppendW(panel->DropPathW, panel->Dirs->At(index).Name);
                        return ReturnDropPath(panel);
                    }
                    else
                    {
                        panel->SetDropTarget(-1); // hide marker
                        panel->DropPathW = panel->GetZIPPath();
                        return ReturnDropPath(panel);
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
                    panel->DropPathW = panel->GetPluginFS()->GetPluginFSName();
                    panel->DropPathW += L':';
                    std::wstring pluginPath;
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
                                if (panel->GetPluginFS()->GetCurrentPathW(pluginPath))
                                {
                                    panel->DropPathW += pluginPath;
                                    return ReturnDropPath(panel);
                                }
                                else
                                {
                                    tgtType = idtttWindows;
                                    return NULL;
                                }
                            }
                        }

                        if (panel->GetPluginFS()->GetFullNameW(panel->Dirs->At(index),
                                                               (index == 0 && wcscmp(panel->Dirs->At(0).Name, L"..") == 0) ? 2 : 1,
                                                               pluginPath))
                        {
                            panel->DropPathW += pluginPath;
                            if (DropSourcePanel != NULL && DropSourcePanel->Is(ptPluginFS) &&
                                DropSourcePanel->GetPluginFS()->NotEmpty() && effect != NULL)
                            { // source FS can affect allowed drop-effects
                                DropSourcePanel->GetPluginFS()->GetAllowedDropEffects(1 /* drag-over-fs */, panel->DropPathW.c_str(),
                                                                                      effect);
                            }

                            panel->SetDropTarget(index);
                            return ReturnDropPath(panel);
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
                    if (panel->GetPluginFS()->GetCurrentPathW(pluginPath))
                    {
                        panel->DropPathW += pluginPath;
                        if (DropSourcePanel != NULL && DropSourcePanel->Is(ptPluginFS) &&
                            DropSourcePanel->GetPluginFS()->NotEmpty() && effect != NULL)
                        { // source FS can influence the allowed drop effects
                            DropSourcePanel->GetPluginFS()->GetAllowedDropEffects(1 /* drag-over-fs */, panel->DropPathW.c_str(),
                                                                                  effect);
                        }
                        return ReturnDropPath(panel);
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
                        std::wstring pluginPath;
                        if (panel->GetPluginFS()->GetFullNameW(panel->Dirs->At(index),
                                                               (index == 0 && wcscmp(panel->Dirs->At(0).Name, L"..") == 0) ? 2 : 1,
                                                               pluginPath))
                        {
                            panel->DropPathW = pluginPath;
                            panel->SetDropTarget(index);
                            return ReturnDropPath(panel);
                        }
                    }
                    panel->SetDropTarget(-1); // hide marker
                    std::wstring pluginPath;
                    if (panel->GetPluginFS()->GetCurrentPathW(pluginPath))
                    {
                        panel->DropPathW = pluginPath;
                        return ReturnDropPath(panel);
                    }
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
                return panel->GetPathW();
            }
        }

        panel->SetDropTarget(index);
        panel->DropPathW = panel->GetPathW();
        if (wcscmp(panel->Dirs->At(index).Name, L"..") == 0)
        {
            if (CutDirectoryW(panel->DropPathW))
            {
                SalPathAddBackslashW(panel->DropPathW);
            }
        }
        else
        {
            const CFileData& dropDir = panel->Dirs->At(index);
            panel->DropPathW = sally::unicode::BuildPanelChildPathW(panel->GetPathW(), dropDir.Name);
        }
        return ReturnDropPath(panel);
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
                    return panel->GetPathW();
                }
            }
            CFileData* file = &(panel->Files->At(index - panel->Dirs->Count));
            std::wstring fullName = sally::unicode::BuildPanelChildPathW(panel->GetPathW(), file->Name);

            // if it's a shortcut, perform its analysis
            BOOL linkIsDir = FALSE;  // TRUE -> shortcut to directory -> ChangePathToDisk
            BOOL linkIsFile = FALSE; // TRUE -> shortcut to file -> archive test
            std::wstring linkTarget;
            if (StrICmpW(file->Ext, L"lnk") == 0) // is it a directory shortcut?
            {
                // wide: request the wide COM interface directly - link->GetPath()
                // otherwise narrows the shortcut's resolved TARGET path via its own internal
                // CP_ACP conversion (this codebase's core code never defines UNICODE, so the
                // unqualified names resolved to the ANSI interface). Same defect and fix already
                // landed in the sibling Execute()/FocusShortcutTarget() shortcut-resolution
                // blocks.
                IShellLinkW* link;
                if (CoCreateInstance(CLSID_ShellLink, NULL,
                                     CLSCTX_INPROC_SERVER, IID_IShellLinkW,
                                     (LPVOID*)&link) == S_OK)
                {
                    IPersistFile* fileInt;
                    if (link->QueryInterface(IID_IPersistFile, (LPVOID*)&fileInt) == S_OK)
                    {
                        if (fileInt->Load(fullName.c_str(), STGM_READ) == S_OK)
                        {
                            GetShellLinkPathOwned(link, SLGP_UNCPRIORITY, linkTarget);
                            if (!linkTarget.empty())
                            {
                                IFileSystem* fs = gFileSystem != NULL ? gFileSystem : GetWin32FileSystem();
                                DWORD attr = fs->GetFileAttributes(linkTarget.c_str());
                                if (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY))
                                    linkIsDir = TRUE;
                                else
                                    linkIsFile = TRUE;
                            }
                        }
                        fileInt->Release();
                    }
                    link->Release();
                }
            }
            if (linkIsDir) // link leads to directory, path is o.k., switch to it
            {
                panel->SetDropTarget(index);
                panel->DropPathW = linkTarget;
                return ReturnDropPath(panel);
            }

            int format = PackerFormatConfig.PackIsArchive(linkIsFile ? linkTarget.c_str() : fullName.c_str());
            if (format != 0) // we found a supported archive
            {
                format--;
                if (PackerFormatConfig.GetUsePacker(format) && // ma edit?
                    (*effect & (DROPEFFECT_MOVE | DROPEFFECT_COPY)) != 0)
                {
                    tgtType = idtttArchiveOnWinPath;
                    *effect &= (DROPEFFECT_MOVE | DROPEFFECT_COPY); // trim effect to copy+move
                    panel->SetDropTarget(index);
                    panel->DropPathW = linkIsFile ? linkTarget : fullName;
                    return ReturnDropPath(panel);
                }
                panel->SetDropTarget(-1); // hide marker
                return NULL;
            }

            if (HasDropTarget(fullName.c_str()))
            {
                isTgtFile = TRUE; // drop target file -> shell must handle it
                panel->SetDropTarget(index);
                panel->DropPathW = fullName;
                return ReturnDropPath(panel);
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
    return panel->GetPathW();
}

const wchar_t* GetCurrentDirClipboard(POINTL& pt, void* param, DWORD* effect, BOOL rButton,
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
    return ReturnDropPath(panel);
}

//
// ****************************************************************************
// DropEnd
//

int CountNumberOfItemsOnPath(const wchar_t* path)
{
    std::wstring searchPath(path != NULL ? path : L"");
    SalPathAppendW(searchPath, L"*.*");
    WIN32_FIND_DATAW fileData;
    HANDLE search = SalFindFirstFileHW(searchPath.c_str(), &fileData);
    if (search != INVALID_HANDLE_VALUE)
    {
        int num = 0;
        do
        {
            num++;
        } while (SalLPFindNextFile(search, &fileData));
        SalLPFindClose(search);
        return num;
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
                int newNum = CountNumberOfItemsOnPath(MainWindow->LeftPanel->GetPathW());
                again |= newNum != numLeft;
                numLeft = newNum;
            }
            if ((!MainWindow->RightPanel->AutomaticRefresh || MainWindow->RightPanel->GetNetworkDrive()) &&
                MainWindow->RightPanel->Is(ptDisk))
            {
                int newNum = CountNumberOfItemsOnPath(MainWindow->RightPanel->GetPathW());
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

BOOL SetClipCutCopyInfo(HWND hwnd, BOOL copy, BOOL salObject)
{
    (void)hwnd;
    const DWORD effect = copy ? (DROPEFFECT_COPY | DROPEFFECT_LINK) : DROPEFFECT_MOVE;
    const UINT cfPrefDrop = gClipboard->RegisterFormat(L"Preferred DropEffect");
    if (cfPrefDrop == 0)
    {
        TRACE_E("Unable to register preferred clipboard drop effect.");
        gClipboard->Clear();
        return FALSE;
    }

    DWORD marker = 1;
    UINT cfSalDataObject = 0;
    if (salObject)
    {
        cfSalDataObject = gClipboard->RegisterFormat(L"SalIDataObject");
        if (cfSalDataObject == 0)
        {
            TRACE_E("Unable to register Sally clipboard ownership marker.");
            gClipboard->Clear();
            return FALSE;
        }
    }

    ClipboardRawData entries[2] = {
        {cfPrefDrop, &effect, sizeof(effect)},
        {cfSalDataObject, &marker, sizeof(marker)}};
    const ClipboardResult result = gClipboard->SetRawDataBatch(entries, salObject ? 2 : 1);
    if (!result.success)
    {
        TRACE_E("Unable to publish clipboard transfer metadata atomically.");
        gClipboard->Clear();
        return FALSE;
    }
    return TRUE;
}

//
// ****************************************************************************
// ShellAction
//

// Returns CFileData::Name, which is WCHAR* since P1.3 - only the
// return type lagged, and that is what made every caller fail on argument 4.
const wchar_t* EnumFileNames(int index, void* param)
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

// NameW is gone; CFileData::Name is WCHAR* and always exact now. hasWideName
// keeps its original diagnostic meaning ("this name would have needed the wide fallback under
// the old CP_ACP-primary design") by checking a real CP_ACP round trip, per
// shellsup_diag.h's own AnyNameNeedsWide comment.
static BOOL NameRoundTripsCPACP(const wchar_t* name)
{
    if (name == NULL || *name == 0)
        return TRUE;
    std::string encoded;
    return static_cast<bool>(Win32EncodeAcpExact(name, encoded));
}

// Collects the selected items' bare names in their wide form - the same source
// CollectSelectedPathsW() uses for the clipboard and for drag&drop, which the context
// menu never got.
static BOOL CollectSelectedNamesW(CFilesWindow* panel, const int* indexes, int indexCount,
                                  std::vector<std::wstring>& names, BOOL& hasWideName)
{
    names.clear();
    hasWideName = FALSE;

    if (panel == NULL || indexes == NULL || indexCount <= 0)
        return FALSE;

    names.reserve(indexCount);
    for (int i = 0; i < indexCount; i++)
    {
        int idx = indexes[i];
        if (idx < 0 || idx >= panel->Dirs->Count + panel->Files->Count)
            return FALSE;

        CFileData* file = (idx < panel->Dirs->Count) ? &panel->Dirs->At(idx) : &panel->Files->At(idx - panel->Dirs->Count);
        std::wstring nameW(file->Name);
        if (nameW.empty())
            return FALSE;

        if (!NameRoundTripsCPACP(file->Name))
            hasWideName = TRUE;

        names.push_back(nameW);
    }
    return TRUE;
}

static BOOL CollectSelectedPathsW(CFilesWindow* panel, const int* indexes, int indexCount,
                                  std::vector<std::wstring>& paths, BOOL& hasWideName)
{
    paths.clear();
    hasWideName = FALSE;

    if (panel == NULL || indexes == NULL || indexCount <= 0)
        return FALSE;

    // Disk panels only. Both call sites already guard with
    // `if (panel->Is(ptDisk))`, so the old `: AnsiToWide(panel->the removed ANSI mirror)`
    // fallback was unreachable - dead code that read like a considered decision and would
    // have quietly put a '?'-mangled base path on the clipboard if it ever became live.
    // These paths go out as HDROP, so another application acts on them.
    if (!panel->Is(ptDisk))
    {
        TRACE_E("CollectSelectedPathsW: panel is not ptDisk");
        return FALSE;
    }

    std::wstring basePathW = panel->GetPathW();
    if (!basePathW.empty() && basePathW.back() != L'\\')
        basePathW += L'\\';

    paths.reserve(indexCount);
    for (int i = 0; i < indexCount; i++)
    {
        int idx = indexes[i];
        if (idx < 0 || idx >= panel->Dirs->Count + panel->Files->Count)
            return FALSE;

        CFileData* file = (idx < panel->Dirs->Count) ? &panel->Dirs->At(idx) : &panel->Files->At(idx - panel->Dirs->Count);
        std::wstring nameW(file->Name);
        if (nameW.empty())
            return FALSE;

        if (!NameRoundTripsCPACP(file->Name))
            hasWideName = TRUE;

        paths.push_back(basePathW + nameW);
    }

    return TRUE;
}

static BOOL SetClipboardHDropW(HWND owner, const std::vector<std::wstring>& paths)
{
    (void)owner;
    sally::clipboard::HDropWideDataObject* dataObject =
        new sally::clipboard::HDropWideDataObject(paths);
    if (dataObject == NULL)
        return FALSE;

    BOOL ok = dataObject->IsValid() && gClipboard->SetDataObject(dataObject).success;
    dataObject->Release();
    return ok;
}

const wchar_t* EnumOneFileName(int index, void* param)
{
    // 'param' points into the caller-owned UTF-16 fake-directory string for the
    // duration of synchronous data-object construction.
    return index == 0 ? (const wchar_t*)param : NULL;
}

HRESULT AuxInvokeCommand2(CFilesWindow* panel, CMINVOKECOMMANDINFO* ici)
{
    CALL_STACK_MESSAGE_NONE

    // temporarily lower thread priority, so some confused shell extension doesn't eat CPU
    HANDLE hThread = GetCurrentThread(); // pseudo-handle, no need to release
    int oldThreadPriority = GetThreadPriority(hThread);
    SetThreadPriority(hThread, THREAD_PRIORITY_NORMAL);

    HRESULT ret = E_UNEXPECTED;
    __try
    {
        ret = panel->ContextSubmenuNew->GetMenu2()->InvokeCommand(ici);
    }
    __except (CCallStack::HandleException(GetExceptionInformation(), 17))
    {
        ICExceptionHasOccured++;
    }

    SetThreadPriority(hThread, oldThreadPriority);
    return ret;
}

HRESULT AuxInvokeCommand(CFilesWindow* panel, CMINVOKECOMMANDINFO* ici)
{ // POZOR: pouziva se i z CSalamanderGeneral::OpenNetworkContextMenu()
    CALL_STACK_MESSAGE_NONE

    // temporarily lower thread priority, so some confused shell extension doesn't eat CPU
    HANDLE hThread = GetCurrentThread(); // pseudo-handle, no need to release
    int oldThreadPriority = GetThreadPriority(hThread);
    SetThreadPriority(hThread, THREAD_PRIORITY_NORMAL);

    HRESULT ret = E_UNEXPECTED;
    __try
    {
        ret = panel->ContextMenu->InvokeCommand(ici);
    }
    __except (CCallStack::HandleException(GetExceptionInformation(), 18))
    {
        ICExceptionHasOccured++;
    }

    SetThreadPriority(hThread, oldThreadPriority);
    return ret;
}

HRESULT AuxInvokeAndRelease(IContextMenu2* menu, CMINVOKECOMMANDINFO* ici)
{
    CALL_STACK_MESSAGE_NONE

    // temporarily lower thread priority, so some confused shell extension doesn't eat CPU
    HANDLE hThread = GetCurrentThread(); // pseudo-handle, no need to release
    int oldThreadPriority = GetThreadPriority(hThread);
    SetThreadPriority(hThread, THREAD_PRIORITY_NORMAL);

    HRESULT ret = E_UNEXPECTED;
    __try
    {
        ret = menu->InvokeCommand(ici);
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
    return ret;
}

static HRESULT AuxGetCommandStringBuffer(IContextMenu2* menu, UINT_PTR idCmd, UINT uType,
                                         UINT* pReserved, LPWSTR pszName, UINT cchMax)
{
    CALL_STACK_MESSAGE_NONE
    HRESULT ret = E_UNEXPECTED;
    __try
    {
        // for years we've been getting crashes when calling IContextMenu2::GetCommandString()
        // this call is not essential for program operation, so we wrap it in try/except block
        // IContextMenu::GetCommandString's pszName parameter is always declared LPSTR by the
        // shell API regardless of the actual encoding; the buffer is treated as WCHAR* whenever
        // uType carries GCS_UNICODE (both call sites pass GCS_VERBW) - reinterpret per the
        // documented Win32 shell convention, not a real narrow buffer.
        ret = menu->GetCommandString(idCmd, uType, pReserved, (LPSTR)pszName, cchMax);
    }
    __except (CCallStack::HandleException(GetExceptionInformation(), 19))
    {
        ICExceptionHasOccured++;
    }
    return ret;
}

HRESULT AuxGetCommandString(IContextMenu2* menu, UINT_PTR idCmd, UINT uType,
                            UINT* pReserved, std::wstring& name)
{
    // The contract advertises 200 characters. A few historic shell extensions treated
    // that count as bytes and wrote twice as much; keep the padding inside this adapter
    // instead of making every semantic owner a 2,000-WCHAR array.
    constexpr UINT declaredCapacity = 200;
    std::vector<wchar_t> buffer(declaredCapacity * 2, L'\0');
    const HRESULT result = AuxGetCommandStringBuffer(menu, idCmd, uType, pReserved,
                                                     buffer.data(), declaredCapacity);
    if (result == NOERROR)
        name.assign(buffer.data(), wcsnlen(buffer.data(), buffer.size()));
    else
        name.clear();
    return result;
}

HRESULT ShellActionAux5(UINT flags, CFilesWindow* panel, HMENU h)
{ // POZOR: pouziva se i z CSalamanderGeneral::OpenNetworkContextMenu()
    CALL_STACK_MESSAGE_NONE

    // temporarily lower thread priority, so some confused shell extension doesn't eat CPU
    HANDLE hThread = GetCurrentThread(); // pseudo-handle, no need to release
    int oldThreadPriority = GetThreadPriority(hThread);
    SetThreadPriority(hThread, THREAD_PRIORITY_NORMAL);

    HRESULT ret = E_UNEXPECTED;
    __try
    {
        ret = panel->ContextMenu->QueryContextMenu(h, 0, 0, 4999, flags);
    }
    __except (CCallStack::HandleException(GetExceptionInformation(), 20))
    {
        QCMExceptionHasOccured++;
    }

    SetThreadPriority(hThread, oldThreadPriority);

    // On success HRESULT_CODE(ret) is the number of ids the extension claimed. Recording it
    // is what makes the 0..4999 / 5000..6000 range split verifiable instead of assumed - an
    // extension that claims ids past the split is silently dropped today (issue #13).
    ShellMenuDiagRecord* diag = ShellMenuDiag.Current();
    if (diag != NULL)
    {
        diag->QueryContextMenuHr = ret;
        diag->MenuItemCount = GetMenuItemCount(h);
    }
    return ret;
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

// wide: SalGetTempFileName's narrow wrapper best-fit-narrows the REAL wide temp
// path SalGetTempFileNameW returns, AFTER the underlying directory was already created on disk
// with its genuine Unicode name - if %TEMP% (or the user profile it sits under) isn't
// CP_ACP-representable, the caller's narrow mirror doesn't name what was actually just created,
// and a subsequent SalPathAppend/SalLPCreateDirectory then silently fails against a parent that
// doesn't exist (drag&drop from an archive/plugin-FS then breaks for that user). Build the whole
// "fake" directory chain in wide throughout. v7 serializes the complete logical path into an
// exact-size UTF-16 payload, so this owner no longer needs a short-path or fixed-field fallback.
static BOOL CreateFakeDragDropDir(const wchar_t* subDirName, std::wstring& fakeRootDir,
                                  size_t& fakeNamePos)
{
    std::wstring tempDirW = SalGetTempFileNameW(NULL, L"SAL", false);
    if (tempDirW.empty())
        return FALSE;

    std::wstring fullDirW = tempDirW;
    SalPathAppendW(fullDirW, subDirName);
    if (!SalLPCreateDirectory(fullDirW.c_str(), NULL))
    {
        RemoveTemporaryDirW(tempDirW.c_str());
        return FALSE;
    }

    fakeRootDir = tempDirW;
    SalPathAppendW(fakeRootDir, subDirName);
    fakeNamePos = tempDirW.length();
    return TRUE;
}

void DoDragFromArchiveOrFS(CFilesWindow* panel, BOOL& dropDone, std::wstring& targetPath, int& operation,
                           const std::wstring& realDraggedPath, DWORD allowedEffects,
                           int srcType, const wchar_t* srcFSPath, BOOL leftMouseButton)
{
    if (SalShExtSharedMemView != NULL) // shared memory is available (we can't handle drag&drop on error)
    {
        CALL_STACK_MESSAGE1("ShellAction::archive/FS::drag_files");

        // create "fake" directory
        // jr: Nasel jsem na netu zminku "Did implementing "IPersistStream" and providing the undocumented
        // "OleClipboardPersistOnFlush" format solve the problem?" -- pro pripad, ze bychom se potrebovali
        // zbavit DROPFAKE metody
        std::wstring fakeRootDir;
        size_t fakeNamePos;
        if (CreateFakeDragDropDir(L"DROPFAKE", fakeRootDir, fakeNamePos))
        {
            {
                {
                    // vytvorime objekty pro drag&drop
                    fakeRootDir[fakeNamePos] = 0;
                    IDataObject* dataObject = CreateIDataObjectW(MainWindow->HWindow, fakeRootDir.c_str(),
                                                                1, EnumOneFileName, (void*)(fakeRootDir.c_str() + fakeNamePos + 1));
                    BOOL dragFromPluginFSWithCopyAndMove = allowedEffects == (DROPEFFECT_MOVE | DROPEFFECT_COPY);
                    CImpIDropSource* dropSource = new CImpIDropSource(dragFromPluginFSWithCopyAndMove);
                    if (dataObject != NULL && dropSource != NULL)
                    {
                        CFakeDragDropDataObject* fakeDataObject = new CFakeDragDropDataObject(dataObject, realDraggedPath.c_str(),
                                                                                              srcType, srcFSPath);
                        if (fakeDataObject != NULL)
                        {
                            // shared memory initialization
                            WaitForSingleObject(SalShExtSharedMemMutex, INFINITE);
                            // Exact match, not >=: a LARGER block from a newer peer
                            // is equally unsafe here (this build would read fields at offsets
                            // the newer layout may have moved) - see SharedMemCompat.h's own
                            // documented rule. Only an exact size match means both sides
                            // compiled the same struct.
                            BOOL sharedMemOK = SALSHEXT_IsCompatibleControl(SalShExtSharedMemView);
                            if (sharedMemOK)
                            {
                                if ((SalShExtSharedMemView->StateFlags & SALSHEXT_STATE_DRAG_ACTIVE) != 0)
                                    TRACE_E("Drag&drop from archive/FS: SalShExtSharedMemView->DoDragDropFromSalamander is TRUE, this should never happen here!");
                                fakeRootDir[fakeNamePos] = L'\\';
                                sharedMemOK = SalShExtBeginDragRequestLocked(fakeRootDir);
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
                                    dropDone = (SalShExtSharedMemView->StateFlags & SALSHEXT_STATE_DROP_DONE) != 0;
                                    if (dropDone)
                                    {
                                        if (!SalShExtReadResponseLocked(targetPath))
                                            dropDone = FALSE;
                                        if (leftMouseButton && dragFromPluginFSWithCopyAndMove)
                                            operation = (dropSource->LastEffect & DROPEFFECT_MOVE) ? SALSHEXT_MOVE : SALSHEXT_COPY;
                                        else // archives + FS with Copy or Move (not both) + FS with Copy+Move when dragging with right button, where result from right button menu isn't affected by mouse cursor change (trick with Copy cursor during Move effect), so we take the result from copy-hook (SalShExtSharedMemView->Operation)
                                            operation = SalShExtSharedMemView->Operation;
                                    }
                                    SalShExtEndRequestLocked(SALSHEXT_STATE_DRAG_ACTIVE);
                                    ReleaseMutex(SalShExtSharedMemMutex);

                                    if (!dropDone &&                 // copy-hook doesn't respond or user chose Cancel in drop-menu (shown during D&D with right button)
                                        dwEffect != DROPEFFECT_NONE) // Cancel detection: since copy-hook didn't trigger, returned drop-effect is valid, so we compare it to Cancel
                                    {
                                        std::wstring diagnostic = LoadStrW(IDS_SHEXT_NOTLOADEDYET);
                                        diagnostic += L"\n\nThe shell extension did not answer the Unicode IPC v7 request. "
                                                      L"Explorer may still have an incompatible v6 DLL loaded; "
                                                      L"restart Explorer and retry.";
                                        gPrompter->ShowError(LoadStrW(IDS_ERRORTITLE), diagnostic.c_str());
                                    }
                                }
                                else
                                {
                                    WaitForSingleObject(SalShExtSharedMemMutex, INFINITE);
                                    SalShExtEndRequestLocked(SALSHEXT_STATE_DRAG_ACTIVE);
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
            }
            fakeRootDir[fakeNamePos] = 0;
            RemoveTemporaryDirW(fakeRootDir.c_str());
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

BOOL ResourceGetDialogName(WCHAR* buff, int buffSize, std::wstring& name)
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

    // Dialog resources store their title as UTF-16. Keep it in that native domain.
    name = buff;

    return TRUE;
}

// tries to load aclui.dll and extract dialog name stored with ID 103 (Security tab)
// on success fills dialog name into pageName and returns TRUE; otherwise returns FALSE
BOOL GetACLUISecurityPageName(std::wstring& pageName)
{
    BOOL ret = FALSE;

    HINSTANCE hModule = LoadLibraryExW(L"aclui.dll", NULL, LOAD_LIBRARY_AS_DATAFILE);

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
                        ret = ResourceGetDialogName((WCHAR*)data, size, pageName);
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
            TRACE_E("Drag&drop from archives is not possible because the native shell extension is missing!");
        }
        if (action == saCopyToClipboard && !SalShExtRegistered)
            TRACE_E("Copy&paste from archives is not possible because the native shell extension is missing!");
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
            TRACE_E("Drag&drop from file-systems is not possible because the native shell extension is missing!");
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
            subDir = (wcscmp(panel->Dirs->At(0).Name, L"..") == 0);
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

    std::wstring targetPath;
    std::wstring realDraggedPath;
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
                realDraggedPath = L'D';
                realDraggedPath += panel->GetZIPArchive();
                SalPathAppendW(realDraggedPath, panel->GetZIPPath());
                SalPathAppendW(realDraggedPath, panel->Dirs->At(i).Name);
            }
            else
            {
                if (i >= 0 && i >= panel->Dirs->Count && i < panel->Dirs->Count + panel->Files->Count)
                {
                    realDraggedPath = L'F';
                    realDraggedPath += panel->GetZIPArchive();
                    SalPathAppendW(realDraggedPath, panel->GetZIPPath());
                    SalPathAppendW(realDraggedPath, panel->Files->At(i - panel->Dirs->Count).Name);
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
                wchar_t* p = DupStr(targetPath.c_str());
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
                    std::wstring fakeRootDir;
                    size_t fakeNamePos;
                    if (CreateFakeDragDropDir(L"CLIPFAKE", fakeRootDir, fakeNamePos))
                    {
                        BOOL delFakeDir = TRUE;
                        {
                            {
                                DWORD prefferedDropEffect = DROPEFFECT_COPY; // DROPEFFECT_MOVE (we used for debugging purposes)

                                // create objects for copy&paste
                                fakeRootDir[fakeNamePos] = 0;
                                IDataObject* dataObject = CreateIDataObjectW(MainWindow->HWindow, fakeRootDir.c_str(),
                                                                            1, EnumOneFileName, (void*)(fakeRootDir.c_str() + fakeNamePos + 1));
                                if (dataObject != NULL)
                                {
                                    fakeRootDir[fakeNamePos] = L'\\';
                                    CFakeCopyPasteDataObject* fakeDataObject = new CFakeCopyPasteDataObject(dataObject, fakeRootDir.c_str());
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
                                                    if (gClipboard->SetDataObject(fakeDataObject).success)
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
                                                        // Exact match, not >=: see the sibling
                                                        // check above (drag&drop) for the full reasoning.
                                                        BOOL sharedMemOK = SALSHEXT_IsCompatibleControl(SalShExtSharedMemView);
                                                        if (sharedMemOK)
                                                        {
                                                            fakeRootDir[fakeNamePos] = L'\\';
                                                            sharedMemOK = SalShExtBeginPasteRequestLocked(
                                                                fakeRootDir, LoadStrW(IDS_ARCUNABLETOPASTE1),
                                                                LoadStrW(IDS_ARCUNABLETOPASTE2));
                                                            if (sharedMemOK)
                                                            {
                                                                SalShExtSharedMemView->ClipDataObjLastGetDataTime = GetTickCount() - 60000; // initialize to 1 minute before creating the data object
                                                                SalShExtSharedMemView->SalamanderMainWndPID = GetCurrentProcessId();
                                                                SalShExtSharedMemView->SalamanderMainWndTID = GetCurrentThreadId();
                                                                SalShExtSharedMemView->SalamanderMainWnd = (UINT64)(DWORD_PTR)MainWindow->HWindow;
                                                                SalShExtSharedMemView->PastedDataID++;
                                                                SalShExtPastedData.SetDataID(SalShExtSharedMemView->PastedDataID);
                                                                clearSalShExtPastedData = FALSE;
                                                                delFakeDir = FALSE; // everything is OK, fake-dir will be used
                                                                fakeDataObject->SetCutOrCopyDone();
                                                            }
                                                        }
                                                        else
                                                            TRACE_E("Shared memory is too small!");
                                                        ReleaseMutex(SalShExtSharedMemMutex);

                                                        if (!sharedMemOK) // if shell-extension communication is unavailable, it makes no sense to leave the data object on the clipboard
                                                        {
                                                            gClipboard->Clear();
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
                        }
                        fakeRootDir[fakeNamePos] = 0;
                        if (delFakeDir)
                            RemoveTemporaryDirW(fakeRootDir.c_str());
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
                        std::wstring fullName;
                        if (panel->GetPluginFS()->GetFullNameW(panel->Dirs->At(i), 1, fullName))
                        {
                            realDraggedPath = L'D';
                            realDraggedPath += panel->GetPluginFS()->GetPluginFSName();
                            realDraggedPath += L':';
                            realDraggedPath += fullName;
                        }
                        else
                            realDraggedPath.clear();
                    }
                    else
                    {
                        if (i >= 0 && i >= panel->Dirs->Count && i < panel->Dirs->Count + panel->Files->Count)
                        {
                            std::wstring fullName;
                            if (panel->GetPluginFS()->GetFullNameW(panel->Files->At(i - panel->Dirs->Count), 0, fullName))
                            {
                                realDraggedPath = L'F';
                                realDraggedPath += panel->GetPluginFS()->GetPluginFSName();
                                realDraggedPath += L':';
                                realDraggedPath += fullName;
                            }
                            else
                                realDraggedPath.clear();
                        }
                    }

                    BOOL dropDone = FALSE;
                    int operation = SALSHEXT_NONE;
                    DWORD allowedEffects = (panel->GetPluginFS()->IsServiceSupported(FS_SERVICE_MOVEFROMFS) ? DROPEFFECT_MOVE : 0) |
                                           (panel->GetPluginFS()->IsServiceSupported(FS_SERVICE_COPYFROMFS) ? DROPEFFECT_COPY : 0);
                    std::wstring currentPath;
                    std::wstring srcFSPath;
                    if (panel->GetPluginFS()->GetCurrentPathW(currentPath))
                    {
                        srcFSPath = panel->GetPluginFS()->GetPluginFSName();
                        srcFSPath += L':';
                        srcFSPath += currentPath;
                    }
                    panel->GetPluginFS()->GetAllowedDropEffects(0 /* start */, NULL, &allowedEffects);
                    DoDragFromArchiveOrFS(panel, dropDone, targetPath, operation, realDraggedPath,
                                          allowedEffects, 2 /* FS */, srcFSPath.c_str(), action == saLeftDragFiles);
                    panel->GetPluginFS()->GetAllowedDropEffects(2 /* end */, NULL, NULL);

                    if (dropDone) // let the operation be performed
                    {
                        wchar_t* p = DupStr(targetPath.c_str());
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
    std::wstring redirectedDir;
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
                std::wstring errMsg = FormatStrW(LoadStrW(IDS_ERROPENPROPSELCONTW64ALIAS), redirectedDir.c_str());
                gPrompter->ShowError(LoadStrW(IDS_ERRORTITLE), errMsg.c_str());
            }
            else
            {
#endif // _WIN64
                CTmpEnumData data;
                data.Indexes = (count == 0) ? &index : indexes.get();
                data.Panel = panel;
                const int selCount = (count == 0) ? 1 : count;

                // The same defect as the right-click menu in issue #79, reached by a
                // different command: the ANSI builder rebuilds every PIDL from
                // CFileData::Name, the lossy CP_ACP mirror, and CreateItemIdList() is
                // all-or-nothing. So one unrepresentable name suppressed the property
                // sheet for the entire selection, with no sheet and no error (audit A17).
                std::vector<std::wstring> selectedNamesW;
                BOOL selHasWideName = FALSE;
                CShellPidlResolveStats resolveStats;
                IContextMenu2* menu = NULL;
                if (panel->Is(ptDisk) &&
                    CollectSelectedNamesW(panel, data.Indexes, selCount, selectedNamesW, selHasWideName))
                {
                    menu = CreateIContextMenu2W(MainWindow->HWindow, panel->GetPathW(),
                                                selectedNamesW, &resolveStats);
                }
                if (menu == NULL)
                {
                    // Legacy construction still covers the "\\\\" and "\\\\server"
                    // namespace cases the wide path deliberately does not.
                    menu = CreateIContextMenu2(MainWindow->HWindow, panel->GetPathW(), selCount,
                                               EnumFileNames, &data);
                }
                if (menu != NULL)
                {
                    CShellExecuteWnd shellExecuteWnd;
                    CMINVOKECOMMANDINFOEX ici;
                    ZeroMemory(&ici, sizeof(CMINVOKECOMMANDINFOEX));
                    ici.cbSize = sizeof(CMINVOKECOMMANDINFOEX);
                    // CMIC_MASK_UNICODE tells the shell to read the W members. Without it
                    // lpDirectory alone reaches the handler, and for a non-ANSI panel path
                    // that is "D:\\???\\" - a working directory that does not exist.
                    ici.fMask = CMIC_MASK_PTINVOKE | CMIC_MASK_UNICODE;
                    ici.hwnd = shellExecuteWnd.Create(MainWindow->HWindow, L"SEW: ShellAction::properties");
                    ici.lpVerb = "properties";
                    ici.lpVerbW = L"properties";
                    std::wstring pageNameW;
                    if (action == saPermissions)
                    {
                        // force opening Security tab; unfortunately we need to pass string for given OS localization
                        if (!GetACLUISecurityPageName(pageNameW))
                            pageNameW = L"Security"; // if we failed to get the name, use English and silently won't work in localized versions
                        ici.lpParametersW = pageNameW.c_str();
                    }
                    const std::wstring dirW = panel->GetPathW();
                    ici.lpDirectoryW = dirW.c_str();
                    ici.nShow = SW_SHOWNORMAL;
                    GetLeftTopCornert(&ici.ptInvoke, posByMouse, useSelection, panel);

                    AuxInvokeAndRelease(menu, (CMINVOKECOMMANDINFO*)&ici);
                }
                else if (resolveStats.Requested > 0)
                {
                    // Never fail silently: this command used to return with nothing at all
                    // on screen, which reads as "Sally is broken", not "this name cannot be
                    // addressed".
                    gPrompter->ShowError(LoadStrW(IDS_ERRORTITLE),
                                         LoadStrW(IDS_SHELLMENU_NOSHELLITEMS));
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
                std::wstring errMsg = FormatStrW(LoadStrW(IDS_ERRCUTSELCONTW64ALIAS), redirectedDir.c_str());
                gPrompter->ShowError(LoadStrW(IDS_ERRORTITLE), errMsg.c_str());
            }
            else
            {
#endif // _WIN64
                int idxCount = (count == 0) ? 1 : count;
                int* idxs = (count == 0) ? &index : indexes.get();
                BOOL clipboardSet = FALSE;
                BOOL usedWideClipboardObject = FALSE;

                // For disk selections, prefer the Shell's wide data object so Paste Shortcut
                // receives both the shell ID list and CF_HDROP formats.
                if (panel->Is(ptDisk))
                {
                    std::vector<std::wstring> selectedPathsW;
                    BOOL hasWideName = FALSE;
                    if (CollectSelectedPathsW(panel, idxs, idxCount, selectedPathsW, hasWideName))
                    {
                        IDataObject* shellDataObject = NULL;
                        HRESULT createResult = sally::clipboard::CreateShellSelectionDataObject(
                            panel->GetPathW(), selectedPathsW, &shellDataObject);
                        if (SUCCEEDED(createResult) && shellDataObject != NULL)
                        {
                            clipboardSet = gClipboard->SetDataObject(shellDataObject).success;
                            shellDataObject->Release();
                            usedWideClipboardObject = clipboardSet;
                        }

                        if (!clipboardSet)
                        {
                            clipboardSet = SetClipboardHDropW(MainWindow->HWindow, selectedPathsW);
                            usedWideClipboardObject = clipboardSet;
                            if (!clipboardSet)
                                TRACE_E("Unable to place wide shell selection on clipboard, falling back to shell copy/cut.");
                        }
                    }
                }

                if (!clipboardSet)
                {
                    CTmpEnumData data;
                    data.Indexes = idxs;
                    data.Panel = panel;

                    // Reaching here means the wide clipboard object could not be
                    // built, so this is the shell copy/cut fallback. It used to go straight to
                    // the CP_ACP mirror, which makes the fallback lossy for exactly the
                    // selections most likely to have needed it. Try the wide construction
                    // first, the same way the properties/context menu above does, and keep the
                    // narrow form for the "\\" and "\\server" namespace cases the wide path
                    // deliberately does not cover.
                    std::vector<std::wstring> selectedNamesW;
                    BOOL selHasWideName = FALSE;
                    CShellPidlResolveStats resolveStats;
                    IContextMenu2* menu = NULL;
                    if (panel->Is(ptDisk) &&
                        CollectSelectedNamesW(panel, idxs, idxCount, selectedNamesW, selHasWideName))
                    {
                        menu = CreateIContextMenu2W(MainWindow->HWindow, panel->GetPathW(),
                                                    selectedNamesW, &resolveStats);
                    }
                    if (menu == NULL)
                    {
                        menu = CreateIContextMenu2(MainWindow->HWindow, panel->GetPathW(), idxCount,
                                                   EnumFileNames, &data);
                    }
                    if (menu != NULL)
                    {
                        CShellExecuteWnd shellExecuteWnd;
                        CMINVOKECOMMANDINFOEX ici;
                        ZeroMemory(&ici, sizeof(CMINVOKECOMMANDINFOEX));
                        ici.cbSize = sizeof(CMINVOKECOMMANDINFOEX);
                        // CMIC_MASK_UNICODE + lpDirectoryW, same as the
                        // properties/context-menu invocation above: without it, lpDirectory
                        // alone reaches the handler, and for a non-ANSI panel path that is
                        // "D:\???\" - a working directory that does not exist.
                        ici.fMask = CMIC_MASK_UNICODE;
                        ici.lpVerb = (action == saCopyToClipboard) ? "copy" : "cut";
                        ici.lpVerbW = (action == saCopyToClipboard) ? L"copy" : L"cut";
                        ici.hwnd = shellExecuteWnd.Create(MainWindow->HWindow, L"SEW: ShellAction::copy_cut_clipboard verb=%hs", ici.lpVerb);
                        ici.lpParameters = NULL;
                        const std::wstring dirW = panel->GetPathW();
                        ici.lpDirectoryW = dirW.c_str();
                        ici.nShow = SW_SHOWNORMAL;
                        ici.dwHotKey = 0;
                        ici.hIcon = 0;

                        AuxInvokeAndRelease(menu, (CMINVOKECOMMANDINFO*)&ici);
                        clipboardSet = TRUE;
                    }
                }

                if (clipboardSet)
                {
                    clipboardSet = SetClipCutCopyInfo(
                        panel->HWindow, action == saCopyToClipboard,
                        sally::clipboard::ShouldTagAsSalamanderObject(usedWideClipboardObject != FALSE));
                }

                if (clipboardSet)
                {
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
                    // Both sides are panel paths, so both have a wide form. On the
                    // CP_ACP mirrors two different unspellable directories compare EQUAL, and the
                    // other panel's CutToClip flag was then cleared without a repaint - the cut
                    // marks stayed on screen for files that are no longer cut.
                    BOOL samePaths = panel->Is(ptDisk) && anotherPanel->Is(ptDisk) &&
                                     IsTheSamePath(panel->GetPathW(), anotherPanel->GetPathW());
                    if (anotherPanel->CutToClipChanged)
                    {
                        // before CUT and COPY also clear CutToClip flag for the other panel
                        anotherPanel->ClearCutToClipFlag(!samePaths);
                    }

                    if (action != saCopyToClipboard)
                    {
                        // in CUT case set file's CutToClip bit (ghosted)
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
                                        // ANSI compare first (cheap), wide compare to
                                        // StrICmp is wide over the only name.
                                        if (StrICmpW(f->Name, f2->Name) == 0)
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
                                        if (StrICmpW(f->Name, f2->Name) == 0)
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
            int idxCount = (count == 0) ? 1 : count;
            int* idxs = (count == 0) ? &index : indexes.get();

            IDataObject* dataObject = NULL;
            if (panel->Is(ptDisk))
            {
                std::vector<std::wstring> selectedPathsW;
                BOOL hasWideName = FALSE;
                if (CollectSelectedPathsW(panel, idxs, idxCount, selectedPathsW, hasWideName))
                {
                    // #101: prefer the Shell's own data object, exactly as saCopyToClipboard does
                    // above for Paste Shortcut (#87).
                    //
                    // A CF_HDROP-only object cannot produce a shortcut: the shell folder drop
                    // target builds a link from the shell ID list (a PIDL array), not from file
                    // paths. With only CF_HDROP it answers DROPEFFECT_NONE for the link
                    // modifiers, which is the blocked cursor users see on Alt+drag and
                    // Ctrl+Shift+drag. Measured on a live drag before this change:
                    //
                    //   probe CF_HDROP            -> PRESENT
                    //   probe Shell IDList Array  -> absent
                    //   key=0x0021 ALT  allowedIn=[COPY MOVE LINK]  effectOut=[NONE]
                    //
                    // Copy and Move were unaffected because those the shell can do from
                    // CF_HDROP alone - which is why only the link modifiers appeared broken.
                    IDataObject* shellDataObject = NULL;
                    if (SUCCEEDED(sally::clipboard::CreateShellSelectionDataObject(
                            panel->GetPathW(), selectedPathsW, &shellDataObject)) &&
                        shellDataObject != NULL)
                    {
                        dataObject = shellDataObject; // reference passes to dataObject
                    }

                    if (dataObject == NULL)
                    { // fall back to the wide CF_HDROP object: no shortcuts, but non-ANSI names survive
                        sally::clipboard::HDropWideDataObject* wideDataObject = new sally::clipboard::HDropWideDataObject(selectedPathsW);
                        if (wideDataObject != NULL)
                        {
                            if (wideDataObject->IsValid())
                                dataObject = wideDataObject;
                            else
                                wideDataObject->Release();
                        }
                    }
                }
            }

            CTmpEnumData data;
            if (dataObject == NULL)
            {
                data.Indexes = idxs;
                data.Panel = panel;
                dataObject = CreateIDataObjectW(MainWindow->HWindow, panel->GetPathW(),
                                               idxCount, EnumFileNames, &data);
            }
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
                    std::wstring text = LoadStrW(IDS_ARCHIVEMENU_CLIPPASTE);

                    if (EnablerPastePath &&
                        (!panel->Is(ptDisk) || !EnablerPasteFiles) && // PasteFiles has priority
                        !EnablerPasteFilesToArcOrFS)                  // PasteFilesToArcOrFS has priority
                    {
                        text = DecorateMenuActionTextW(text.c_str(), LoadStrW(IDS_PASTE_CHANGE_DIRECTORY));
                    }

                    MENU_ITEM_INFO mii;
                    mii.Mask = MENU_MASK_STRING;
                    mii.String = text.data();
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

                // Open a diagnostic record for this right-click. GetPathW() is the wide
                // source of truth; the removed ANSI mirror is the lossy CP_ACP mirror and would hide
                // exactly the case issues #79/#90 turn on.
                ShellMenuDiag.Begin(panel->GetPathW(), useSelection ? (count == 0 ? 1 : count) : 0,
                                    onlyPanelMenu);

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
                    if (IsWin64RedirectedDir(panel->GetPathW(), NULL, TRUE))
                    {
                        gPrompter->ShowError(LoadStrW(IDS_ERRORTITLE), LoadStrW(IDS_ERROPENMENUFORW64ALIAS));
                    }
                    else
                    {
#endif // _WIN64
                        // Build wide-first, bundled the same way as the background-
                        // right-click-via-mouse sibling below: ContextMenu and ContextSubmenuNew
                        // are obtained together per attempt (wide, then narrow only if the wide
                        // ContextMenu didn't bind - covers "\\"/"\\server" the wide path doesn't
                        // resolve). On the CP_ACP mirror a non-ASCII panel directory came back as
                        // nothing, so a mouse click behind items in such a panel silently produced
                        // an empty menu. ShellActionAux5 must run after ContextMenu is obtained but
                        // before ContextSubmenuNew (see the TortoiseHg note below), in both branches.
                        if (panel->Is(ptDisk))
                        {
                            panel->ContextMenu = CreateIContextMenu2W(MainWindow->HWindow, panel->GetPathW());
                            if (panel->ContextMenu != NULL && h != NULL)
                            {
                                ShellActionAux5(flags, panel, h);
                                alreadyHaveContextMenu = TRUE;
                            }
                            GetNewOrBackgroundMenuW(MainWindow->HWindow, panel->GetPathW(), panel->ContextSubmenuNew, 5000, 6000, TRUE);
                        }
                        if (panel->ContextMenu == NULL)
                        {
                            // Legacy path still covers "\\" and "\\server", which
                            // SHParseDisplayName does not resolve to a bindable folder.
                            panel->ContextMenu = CreateIContextMenu2W(MainWindow->HWindow, panel->GetPathW());
                            if (panel->ContextMenu != NULL && h != NULL && !alreadyHaveContextMenu)
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
                            GetNewOrBackgroundMenuW(MainWindow->HWindow, panel->GetPathW(), panel->ContextSubmenuNew, 5000, 6000, TRUE);
                        }
                        // Gates the UNC-root branch of the context menu; on the
                        // CP_ACP mirror a share whose server or name the code page
                        // cannot spell stops being recognised as a root.
                        uncRootPath = IsUNCRootPathW(panel->GetPathW());
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
                            std::wstring errMsg = FormatStrW(LoadStrW(IDS_ERROPENMENUSELCONTW64ALIAS), redirectedDir.c_str());
                            gPrompter->ShowError(LoadStrW(IDS_ERRORTITLE), errMsg.c_str());
                        }
                        else
                        {
#endif // _WIN64
                            CTmpEnumData data;
                            data.Indexes = (count == 0) ? &index : indexes.get();
                            data.Panel = panel;
                            const int selCount = (count == 0) ? 1 : count;

                            // Build the menu from the wide names first (issue #79). The
                            // legacy path resolves each PIDL from CFileData::Name, the
                            // lossy CP_ACP mirror, and abandons the entire menu if any one
                            // name fails - which is why right-clicking a file with
                            // non-ANSI characters in its name did nothing at all.
                            std::vector<std::wstring> selectedNamesW;
                            BOOL selHasWideName = FALSE;
                            CShellPidlResolveStats resolveStats;
                            if (panel->Is(ptDisk) &&
                                CollectSelectedNamesW(panel, data.Indexes, selCount, selectedNamesW, selHasWideName))
                            {
                                panel->ContextMenu = CreateIContextMenu2W(MainWindow->HWindow, panel->GetPathW(),
                                                                          selectedNamesW, &resolveStats);
                                ShellMenuDiagRecord* diagRec = ShellMenuDiag.Current();
                                if (diagRec != NULL)
                                    diagRec->AnyNameNeedsWide = selHasWideName != FALSE;
                            }

                            if (panel->ContextMenu == NULL)
                            {
                                // Fall back to the legacy ANSI construction. Worth keeping:
                                // it handles the "\\\\" and "\\\\server" namespace cases the
                                // wide path deliberately does not.
                                panel->ContextMenu = CreateIContextMenu2(MainWindow->HWindow, panel->GetPathW(), selCount,
                                                                         EnumFileNames, &data);
                            }

                            if (panel->ContextMenu == NULL && resolveStats.Requested > 0)
                            {
                                // Never fail silently again. The Find dialog already warns
                                // when a row cannot be acted on through the ANSI shell API
                                // (CFindDialog::EnsureRowActionableViaAnsi); the panel was
                                // the outlier.
                                gPrompter->ShowError(LoadStrW(IDS_ERRORTITLE),
                                                     LoadStrW(IDS_SHELLMENU_NOSHELLITEMS));
                            }
#ifndef _WIN64
                        }
#endif // _WIN64
                    }
                    else
                    {
#ifndef _WIN64
                        if (IsWin64RedirectedDir(panel->GetPathW(), NULL, TRUE))
                        {
                            gPrompter->ShowError(LoadStrW(IDS_ERRORTITLE), LoadStrW(IDS_ERROPENMENUFORW64ALIAS));
                        }
                        else
                        {
#endif // _WIN64
                            // Background right-click. Both of these bind the panel's own
                            // folder, so both were being built for whatever folder the
                            // CP_ACP mirror named - in a folder the code page cannot spell
                            // that is nothing, and the menu came back empty.
                            if (panel->Is(ptDisk))
                            {
                                panel->ContextMenu = CreateIContextMenu2W(MainWindow->HWindow, panel->GetPathW());
                                GetNewOrBackgroundMenuW(MainWindow->HWindow, panel->GetPathW(), panel->ContextSubmenuNew, 5000, 6000, FALSE);
                            }
                            if (panel->ContextMenu == NULL)
                            {
                                // Legacy path still covers "\\" and "\\server", which
                                // SHParseDisplayName does not resolve to a bindable folder.
                                panel->ContextMenu = CreateIContextMenu2W(MainWindow->HWindow, panel->GetPathW());
                                GetNewOrBackgroundMenuW(MainWindow->HWindow, panel->GetPathW(), panel->ContextSubmenuNew, 5000, 6000, FALSE);
                            }
                            // Gates the UNC-root branch of the context menu; on the
                        // CP_ACP mirror a share whose server or name the code page
                        // cannot spell stops being recognised as a root.
                        uncRootPath = IsUNCRootPathW(panel->GetPathW());
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
                std::wstring pastePath;
                if (panel->ContextMenu != NULL && h != NULL)
                {
                    if (!alreadyHaveContextMenu)
                        ShellActionAux5(flags, panel, h);
                    RemoveUselessSeparatorsFromMenu(h);

                    std::wstring cmdName;
                    if (onlyPanelMenu)
                    {
                        if (panel->ContextSubmenuNew->MenuIsAssigned())
                        {
                            HMENU bckgndMenu = panel->ContextSubmenuNew->GetMenu();
                            int bckgndMenuInsert = 0;
                            if (useSelection)
                                TRACE_E("Unexpected value in 'useSelection' (TRUE) in ShellAction(saContextMenu).");
                            int miCount = GetMenuItemCount(h);
                            MENUITEMINFOW mi;
                            int i;
                            for (i = 0; i < miCount; i++)
                            {
                                memset(&mi, 0, sizeof(mi)); // necessary here
                                mi.cbSize = sizeof(mi);
                                mi.fMask = MIIM_STATE | MIIM_FTYPE | MIIM_ID | MIIM_SUBMENU;
                                std::wstring itemName;
                                if (GetMenuItemInfoW(h, i, TRUE, &mi) && ReadMenuItemTextW(h, i, itemName))
                                {
                                    if (mi.hSubMenu == NULL && (mi.fType & MFT_SEPARATOR) == 0) // not submenu nor separator
                                    {
                                        if (AuxGetCommandString(panel->ContextMenu, mi.wID, GCS_VERBW, NULL, cmdName) == NOERROR)
                                        {
                                            if (_wcsicmp(cmdName.c_str(), L"explore") == 0 || _wcsicmp(cmdName.c_str(), L"open") == 0)
                                            {
                                                mi.fMask |= MIIM_STRING;
                                                mi.dwTypeData = itemName.data();
                                                mi.cch = (UINT)itemName.size();
                                                InsertMenuItemW(bckgndMenu, bckgndMenuInsert++, TRUE, &mi);
                                                if (bckgndMenuInsert == 2)
                                                    break; // we don't need more items from here
                                            }
                                        }
                                    }
                                }
                                else
                                {
                                    DWORD err = GetLastError();
                                    TRACE_EW(L"Unable to get item information from menu: " << GetErrorTextOwned(err).c_str());
                                }
                            }
                            if (bckgndMenuInsert > 0) // separate Explore + Open from rest of menu
                            {
                                // separator
                                mi.cbSize = sizeof(mi);
                                mi.fMask = MIIM_TYPE;
                                mi.fType = MFT_SEPARATOR;
                                mi.dwTypeData = NULL;
                                InsertMenuItemW(bckgndMenu, bckgndMenuInsert++, TRUE, &mi);
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
                            std::wstring itemName = LoadStrW(IDS_MENU_EDIT_PASTE);
                            if (EnablerPastePath && !EnablerPasteFiles) // PasteFiles has priority
                                itemName = DecorateMenuActionTextW(itemName.c_str(), LoadStrW(IDS_PASTE_CHANGE_DIRECTORY));
                            mi.cbSize = sizeof(mi);
                            mi.fMask = MIIM_STATE | MIIM_ID | MIIM_TYPE;
                            mi.fType = MFT_STRING;
                            mi.fState = EnablerPastePath || EnablerPasteFiles ? MFS_ENABLED : MFS_DISABLED;
                            mi.dwTypeData = itemName.data();
                            mi.wID = 10000;
                            InsertMenuItemW(bckgndMenu, bckgndMenuInsert++, TRUE, &mi);

                            // add Paste Shortcuts command
                            mi.cbSize = sizeof(mi);
                            mi.fMask = MIIM_STATE | MIIM_ID | MIIM_TYPE;
                            mi.fType = MFT_STRING;
                            mi.fState = EnablerPasteLinksOnDisk ? MFS_ENABLED : MFS_DISABLED;
                            mi.dwTypeData = LoadStrW(IDS_MENU_EDIT_PASTELINKS);
                            mi.wID = 10001;
                            InsertMenuItemW(bckgndMenu, bckgndMenuInsert++, TRUE, &mi);

                            // if not already there, insert separator
                            MENUITEMINFOW mi2;
                            memset(&mi2, 0, sizeof(mi2));
                            mi2.cbSize = sizeof(mi2);
                            mi2.fMask = MIIM_TYPE;
                            if (!GetMenuItemInfoW(bckgndMenu, bckgndMenuInsert, TRUE, &mi2) ||
                                (mi2.fType & MFT_SEPARATOR) == 0)
                            {
                                mi.cbSize = sizeof(mi);
                                mi.fMask = MIIM_TYPE;
                                mi.fType = MFT_SEPARATOR;
                                mi.dwTypeData = NULL;
                                InsertMenuItemW(bckgndMenu, bckgndMenuInsert++, TRUE, &mi);
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
                            MENUITEMINFOW mi;

                            // separator
                            mi.cbSize = sizeof(mi);
                            mi.fMask = MIIM_TYPE;
                            mi.fType = MFT_SEPARATOR;
                            mi.dwTypeData = NULL;
                            InsertMenuItemW(h, -1, TRUE, &mi);

                            // New submenu
                            mi.cbSize = sizeof(mi);
                            mi.fMask = MIIM_STATE | MIIM_SUBMENU | MIIM_TYPE;
                            mi.fType = MFT_STRING;
                            mi.fState = MFS_ENABLED;
                            mi.hSubMenu = panel->ContextSubmenuNew->GetMenu();
                            mi.dwTypeData = LoadStrW(IDS_MENUNEWTITLE);
                            InsertMenuItemW(h, -1, TRUE, &mi);
                        }
                    }

                    if (GetMenuItemCount(h) > 0) // protection against completely stripped menu
                    {
                        CMenuPopup contextPopup;
                        contextPopup.SetTemplateMenu(h);
                        cmd = contextPopup.Track(MENU_TRACK_RETURNCMD | MENU_TRACK_RIGHTBUTTON,
                                                 pt.x, pt.y, panel->GetListBoxHWND(), NULL);
                    }
                    else
                        cmd = 0;

                    // Record what tracking returned before any interpretation. Note cmd == 0
                    // is ambiguous today: QueryContextMenu is called with idCmdFirst 0, so an
                    // extension's first command is indistinguishable from "user cancelled".
                    ShellMenuDiagRecord* diag = ShellMenuDiag.Current();
                    if (diag != NULL)
                    {
                        diag->TrackedCmd = cmd;
                        if (cmd == 0)
                            diag->Owner = ShellMenuOwner::Cancelled;
                    }

                    if (cmd != 0)
                    {
                        CALL_STACK_MESSAGE1("ShellAction::context_menu::exec0");
                        if (cmd < 5000)
                        {
                            HRESULT verbHr = AuxGetCommandString(panel->ContextMenu, cmd, GCS_VERBW, NULL, cmdName);
                            if (diag != NULL)
                            {
                                diag->VerbHr = verbHr;
                                diag->Verb = cmdName;
                            }
                        }
                        if (cmd == 10000 || cmd == 10001)
                        {
                            pastePath = panel->GetPathW();
                        }
                        if (cmd < 5000 && _wcsicmp(cmdName.c_str(), L"paste") == 0 && count <= 1)
                        {
                            if (useSelection) // paste into subdirectory of panel->GetPathW()
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
                                    const CFileData& subdirData = panel->Dirs->At(specialIndex);
                                    std::wstring targetPath =
                                        sally::unicode::BuildPanelChildPathW(panel->GetPathW(), subdirData.Name);
                                    pastePath = std::move(targetPath);
                                    cmd = 10000; // command will be executed elsewhere
                                }
                            }
                            else // paste into panel->GetPathW()
                            {
                                pastePath = panel->GetPathW();
                                cmd = 10000; // command will be executed elsewhere
                            }
                        }
                        clipCopy = (cmd < 5000 && _wcsicmp(cmdName.c_str(), L"copy") == 0);
                        clipCut = (cmd < 5000 && _wcsicmp(cmdName.c_str(), L"cut") == 0);
                        cmdDelete = useSelection && (cmd < 5000 && _wcsicmp(cmdName.c_str(), L"delete") == 0);

                        // Map Network Drive command is 40 under XP, 43 under W2K, and only under Vista it has defined cmdName
                        cmdMapNetDrv = uncRootPath && (_wcsicmp(cmdName.c_str(), L"connectNetworkDrive") == 0 ||
                                                       !WindowsVistaAndLater && cmd == 40);

                        if (cmd != 10000 && cmd != 10001 && !clipCopy && !clipCut && !cmdDelete && !cmdMapNetDrv)
                        {
                            if (cmd < 5000 && _wcsicmp(cmdName.c_str(), L"rename") == 0)
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
                                    _wcsicmp(cmdName.c_str(), L"properties") != 0 && // not necessary for properties
                                    _wcsicmp(cmdName.c_str(), L"find") != 0 &&       // not necessary for find
                                    _wcsicmp(cmdName.c_str(), L"open") != 0 &&       // not necessary for open
                                    _wcsicmp(cmdName.c_str(), L"explore") != 0 &&    // not necessary for explore
                                    _wcsicmp(cmdName.c_str(), L"link") != 0)         // not necessary for create-short-cut
                                {
                                    // This decides whether the OTHER panel must let go of
                                    // the medium before the verb runs - HandsOff(), or for "format..."
                                    // a forced jump to a fixed drive. Every operand has a wide form, so
                                    // none of this needs the CP_ACP mirror. On it two different UNC
                                    // servers whose names the code page cannot spell both render as
                                    // '?'-strings and compare EQUAL, and a panel on an unrelated volume
                                    // is thrown off its path. The length test is character-based here
                                    // rather than byte-based, which is what it always meant to ask.
                                    const wchar_t* panelPathW = panel->GetPathW();
                                    std::wstring rootW = GetRootPath(panelPathW);
                                    if (rootW.length() >= wcslen(panelPathW)) // menu for entire disk - due to commands like
                                    {                                             // for "format..." we must "hands off" the media
                                        CFilesWindow* win;
                                        int i;
                                        for (i = 0; i < 2; i++)
                                        {
                                            win = i == 0 ? MainWindow->LeftPanel : MainWindow->RightPanel;
                                            if (HasTheSameRootPath(win->GetPathW(), rootW.c_str())) // stejny disk (UNC i normal)
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
                                    gEnvironment->SetCurrentDirectory(panel->GetPathW()); // for files with spaces in name: so Open With works for Microsoft Paint too (failed under W2K - wrote "d:\documents.bmp was not found" for file "D:\Documents and Settings\petr\My Documents\example.bmp")
                                }

                                DWORD disks = GetLogicalDrives();

                                CShellExecuteWnd shellExecuteWnd;
                                CMINVOKECOMMANDINFOEX ici;
                                ZeroMemory(&ici, sizeof(CMINVOKECOMMANDINFOEX));
                                ici.cbSize = sizeof(CMINVOKECOMMANDINFOEX);
                                // Without CMIC_MASK_UNICODE only lpDirectory reaches the
                                // handler, so a verb that treats it as its working
                                // directory - which is most of the ones that touch the
                                // filesystem - was being pointed at "D:\???\" (audit A1).
                                // The W members exist on this struct precisely for that;
                                // nothing had ever set them.
                                ici.fMask = CMIC_MASK_PTINVOKE | CMIC_MASK_UNICODE;
                                if (CanUseShellExecuteWndAsParent(cmdName.c_str()))
                                    ici.hwnd = shellExecuteWnd.Create(MainWindow->HWindow, L"SEW: ShellAction::context_menu cmd=%d", cmd);
                                else
                                    ici.hwnd = MainWindow->HWindow;
                                // lpVerb (inherited from CMINVOKECOMMANDINFO) is always LPCSTR
                                // regardless of the Ex/wide fields alongside it - a genuine,
                                // permanent Windows Shell API contract.
                                if (cmd < 5000)
                                    ici.lpVerb = MAKEINTRESOURCEA(cmd);
                                else
                                    ici.lpVerb = MAKEINTRESOURCEA(cmd - 5000);
                                // MAKEINTRESOURCE and MAKEINTRESOURCEW carry the same
                                // value; the verb here is a menu id, not a string.
                                ici.lpVerbW = (LPCWSTR)ici.lpVerb;
                                const std::wstring invokeDirW = panel->GetPathW();
                                ici.lpDirectoryW = invokeDirW.c_str();
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

                                    HRESULT invokeHr = AuxInvokeCommand(panel, (CMINVOKECOMMANDINFO*)&ici);
                                    if (diag != NULL)
                                    {
                                        diag->Owner = ShellMenuOwner::ItemMenu;
                                        diag->InvokeHr = invokeHr;
                                        // ici.hwnd is a stack CShellExecuteWnd that dies when
                                        // this scope exits. A verb that keeps working after
                                        // InvokeCommand returns (the Compressed-folder handler
                                        // is the suspect for #20) is left with a dead parent.
                                        diag->ParentWasTransient = ici.hwnd != MainWindow->HWindow;
                                        diag->ParentAliveAfter = IsWindow(ici.hwnd) != FALSE;
                                    }

                                    // we catch cut/copy/paste, but to be safe we still refresh clipboard enablers
                                    IdleRefreshStates = TRUE;  // force state variable check on next Idle
                                    IdleCheckClipboard = TRUE; // also check clipboard

                                    if (releaseLeft && !changeToFixedDrv)
                                        MainWindow->LeftPanel->HandsOff(FALSE);
                                    if (releaseRight && !changeToFixedDrv)
                                        MainWindow->RightPanel->HandsOff(FALSE);

                                    //---  refresh non-automatically refreshed directories
                                    // report change in current directory and its subdirectories (just to be safe, who knows what was launched)
                                    // Wide: the ANSI mirror names a path the snooper never
                                    // matches, so the verb succeeded on disk and the panel
                                    // sat stale until Ctrl+R (audit A4).
                                    MainWindow->PostChangeOnPathNotificationW(panel->GetPathW(), TRUE);
                                }
                                else
                                {
                                    if (panel->ContextSubmenuNew->MenuIsAssigned()) // exception could have occurred
                                    {
                                        HRESULT invokeHr = AuxInvokeCommand2(panel, (CMINVOKECOMMANDINFO*)&ici);
                                        if (diag != NULL)
                                        {
                                            diag->Owner = ShellMenuOwner::NewMenu;
                                            diag->InvokeHr = invokeHr;
                                            diag->ParentWasTransient = ici.hwnd != MainWindow->HWindow;
                                            diag->ParentAliveAfter = IsWindow(ici.hwnd) != FALSE;
                                        }

                                        //---  refresh non-automatically refreshed directories
                                        // report change in current directory (new file/directory can probably only be created in it)
                                        MainWindow->PostChangeOnPathNotificationW(panel->GetPathW(), FALSE);
                                    }
                                    else if (diag != NULL)
                                    {
                                        // The silent discard behind issue #13: an extension that
                                        // claimed ids at or past 5000 lands here, and in the
                                        // selection case ContextSubmenuNew is never assigned, so
                                        // the command is dropped with no invoke and no error.
                                        diag->Owner = ShellMenuOwner::NewMenu;
                                        diag->InvokeHr = E_ABORT;
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
                    ShellMenuDiag.End();
                }

                if (cmd == 10000) // our own "paste" to pastePath
                {
                    if (!panel->ClipboardPaste(FALSE, FALSE, pastePath.c_str()))
                        panel->ClipboardPastePath(); // classic paste failed, we probably just need to change current path
                }
                else
                {
                    if (cmd == 10001) // our own "paste shortcuts" to pastePath
                    {
                        panel->ClipboardPaste(TRUE, FALSE, pastePath.c_str());
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

extern DWORD ExecuteAssociationTlsIndex; // allows only one call at a time (prevents recursion) in each thread

// QueryContextMenu gets its own function because a body containing __try/__except cannot
// also hold objects that need unwinding. Exception ID 22 is the one the deleted narrow
// ExecuteAssociation() used for this same call, so an old crash report still reads alike.
static void ExecuteAssociationQueryMenu(IContextMenu2* menu, HMENU h, DWORD flags)
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

// AuxInvokeAndRelease() covers the release on the path that invokes; this covers the one
// that does not (menu built, but no popup to read the default item out of).
static void ExecuteAssociationReleaseMenu(IContextMenu2* menu)
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

static const wchar_t* ExecuteAssociationSingleName(int, void* param)
{
    return (const wchar_t*)param;
}

// Wide version for Unicode filenames
void ExecuteAssociationW(HWND hWindow, const wchar_t* pathW, const wchar_t* nameW)
{
    CALL_STACK_MESSAGE1("ExecuteAssociationW()");

    if (pathW == NULL || nameW == NULL)
        return;

    if (ExecuteAssociationTlsIndex == TLS_OUT_OF_INDEXES ||
        TlsGetValue(ExecuteAssociationTlsIndex) == 0)
    {
        if (ExecuteAssociationTlsIndex != TLS_OUT_OF_INDEXES)
            TlsSetValue(ExecuteAssociationTlsIndex, (void*)1);

        // Invoke the default verb through IContextMenu2 with a throwaway CShellExecuteWnd as
        // the parent, so a shell extension that answers a double-click by destroying the
        // window it was handed destroys that throwaway and not the panel listbox.
        //
        // Widening deleted the narrow ExecuteAssociation(), and this whole branch went with
        // it - every association then opened through the bare ShellExecuteExW below with the
        // real panel HWND as sei.hwnd. That was the normal path for every ANSI-representable
        // name, so the shield was lost for everyone, not only for the users who had ticked
        // the (separately orphaned, now retired) salopen.exe option.
        IContextMenu2* menu = CreateIContextMenu2(hWindow, pathW, 1,
                                                  ExecuteAssociationSingleName, (void*)nameW);
        if (menu != NULL)
        {
            CALL_STACK_MESSAGE1("ExecuteAssociationW::1");
            HMENU h = CreatePopupMenu();
            UINT cmd = (UINT)-1;
            if (h != NULL)
            {
                DWORD flags = CMF_DEFAULTONLY | ((GetKeyState(VK_SHIFT) & 0x8000) ? CMF_EXPLORE : 0);
                ExecuteAssociationQueryMenu(menu, h, flags);

                cmd = GetMenuDefaultItem(h, FALSE, GMDI_GOINTOPOPUPS);
                if (cmd == (UINT)-1) // we didn't find default item -> try searching only among verbs
                {
                    DestroyMenu(h);
                    h = CreatePopupMenu();
                    if (h != NULL)
                    {
                        ExecuteAssociationQueryMenu(menu, h, CMF_VERBSONLY | CMF_DEFAULTONLY);

                        cmd = GetMenuDefaultItem(h, FALSE, GMDI_GOINTOPOPUPS);
                        if (cmd == (UINT)-1)
                            cmd = 0; // try "default verb" (index 0)
                    }
                }
            }
            if (cmd != (UINT)-1)
            {
                CShellExecuteWnd shellExecuteWnd;

                // CMIC_MASK_UNICODE + lpDirectoryW - the same shell-verb-invoke pattern core
                // already uses in files_window_clipboard_paths.cpp and drivelst.cpp: the base
                // CMINVOKECOMMANDINFO carries an LPCSTR directory only, so a path outside the
                // code page would narrow silently with no way to report the loss. lpDirectory
                // stays populated, but only when it round-trips exactly, as the documented
                // fallback for handlers that do not read the Ex struct.
                std::string pathA;
                const bool pathIsAcpExact = Win32EncodeAcpExact(pathW, pathA).Succeeded();

                CMINVOKECOMMANDINFOEX ici;
                ZeroMemory(&ici, sizeof(ici));
                ici.cbSize = sizeof(CMINVOKECOMMANDINFOEX);
                ici.fMask = CMIC_MASK_UNICODE;
                ici.hwnd = shellExecuteWnd.Create(hWindow, L"SEW: ExecuteAssociationW cmd=%d", cmd);
                ici.lpVerb = MAKEINTRESOURCEA(cmd);
                ici.lpVerbW = MAKEINTRESOURCEW(cmd);
                ici.lpParameters = NULL;
                ici.lpDirectory = pathIsAcpExact ? pathA.c_str() : NULL;
                ici.lpDirectoryW = pathW;
                ici.nShow = SW_SHOWNORMAL;
                ici.dwHotKey = 0;
                ici.hIcon = 0;

                CALL_STACK_MESSAGE1("ExecuteAssociationW::2");
                AuxInvokeAndRelease(menu, (CMINVOKECOMMANDINFO*)&ici);
            }
            else
            {
                CALL_STACK_MESSAGE1("ExecuteAssociationW::3");
                ExecuteAssociationReleaseMenu(menu);
            }
            if (h != NULL)
                DestroyMenu(h);
        }
        else
        {
            // The shell namespace would not bind this item. Fall back to ShellExecuteExW,
            // which is what the narrow version did here too.
            //
            // The directory arrives wide. It used to arrive as the panel's ANSI mirror and be
            // re-widened here with CP_ACP, which meant a folder outside the code page reached
            // the shell as "D:\???\" - so nothing opened, silently, no matter how the file
            // itself was named.
            std::wstring fullPathW(pathW);
            SalPathAppendW(fullPathW, nameW);

            SHELLEXECUTEINFOW sei = {0};
            sei.cbSize = sizeof(sei);
            sei.fMask = SEE_MASK_FLAG_NO_UI;
            sei.hwnd = hWindow;
            sei.lpVerb = NULL; // default verb (open)
            sei.lpFile = fullPathW.c_str();
            sei.lpDirectory = pathW;
            sei.nShow = SW_SHOWNORMAL;
            ShellExecuteExW(&sei);
        }

        if (ExecuteAssociationTlsIndex != TLS_OUT_OF_INDEXES)
            TlsSetValue(ExecuteAssociationTlsIndex, (void*)0);
    }
    else
    {
        // A recursive call: the previous one never finished. The narrow version
        // asked whether to continue or break for a bug report; widening it
        // dropped the whole branch, so the second double-click simply did
        // nothing and never said why.
        if (SalMessageBoxW(hWindow, LoadStrW(IDS_SHELLEXTBREAK4), SALAMANDER_TEXT_VERSIONW(),
                           MSGBOXEX_CONTINUEABORT | MB_ICONINFORMATION | MSGBOXEX_SETFOREGROUND) == IDABORT)
        { // we break
            SetBugReportReasonBreak(L"Attempt to call ExecuteAssociation() recursively.");
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
BOOL CanUseShellExecuteWndAsParent(const wchar_t* cmdName)
{
    // for Map Network Drive we can't use shellExecuteWnd, otherwise it hangs (MainWindows->HWindow gets disabled and Map Network Drive window doesn't open)
    if (WindowsVistaAndLater && _wcsicmp(cmdName, L"connectNetworkDrive") == 0)
        return FALSE;

    // under Windows 8 Open With was problematic - when choosing custom program, Open dialog didn't appear
    // https://forum.altap.cz/viewtopic.php?f=16&t=6730 and https://forum.altap.cz/viewtopic.php?t=6782
    // the problem is that code returns from invoke, but later MS accesses the parent window which we already destroyed
    // TODO: a solution would be to keep ShellExecuteWnd alive (child window, stretched over entire Salamander area, completely in its background)
    // we would just verify it's alive (that someone didn't destroy it) before passing it
    // TODO2: I tried the proposal as exercise and under W8 with Open With it doesn't work, Open dialog is not modal to our main window (or Find window)
    // for now we'll pass main Salamander window in this case
    if (Windows8AndLater && _wcsicmp(cmdName, L"openas") == 0)
        return FALSE;

    // for other cases (majority) ShellExecuteWnd can be used
    return TRUE;
}
