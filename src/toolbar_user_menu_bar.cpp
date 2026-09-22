// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"

#include "mainwnd.h"
#include "usermenu.h"
#include "toolbar.h"
#include "shellib.h"
#include "cfgdlg.h"
#include "execute.h"
#include "plugins.h"
extern "C"
{
#include "shexreg.h"
}
#include "salshlib.h"
#include "common/clipboard/HDropSelection.h"
#include "ui/UnicodeHistoryUtils.h"

//****************************************************************************
//
// CUMDropTarget
//

class CUMDropTarget : public IDropTarget
{
private:
    long RefCount;             // object lifetime
    IDataObject* DataObject;   // IDataObject that entered the drag
    CUserMenuBar* UserMenuBar; // bar we are associated with
    IDropTarget* DropTarget;
    std::wstring DropTargetFileName;

public:
    CUMDropTarget(CUserMenuBar* userMenuBar)
    {
        RefCount = 1;
        DataObject = NULL;
        UserMenuBar = userMenuBar;
        DropTarget = NULL;
        DropTargetFileName.clear();
    }

    virtual ~CUMDropTarget()
    {
        if (RefCount != 0)
            TRACE_E("Preliminary destruction of this object.");
    }

    STDMETHOD(QueryInterface)
    (REFIID refiid, void FAR * FAR * ppv)
    {
        if (refiid == IID_IUnknown || refiid == IID_IDropTarget)
        {
            *ppv = this;
            AddRef();
            return NOERROR;
        }
        else
        {
            *ppv = NULL;
            return E_NOINTERFACE;
        }
    }

    STDMETHOD_(ULONG, AddRef)
    (void) { return ++RefCount; }
    STDMETHOD_(ULONG, Release)
    (void)
    {
        if (--RefCount == 0)
        {
            delete this;
            return 0; // must not touch the object, it no longer exists
        }
        return RefCount;
    }

    void HitTest(POINTL pt, int& insertIndex, BOOL& after, int& pasteIndex,
                 std::wstring& fileName, BOOL insert)
    {
        fileName.clear();
        POINT p;
        p.x = pt.x;
        p.y = pt.y;
        ScreenToClient(UserMenuBar->HWindow, &p);

        int total = UserMenuBar->GetItemCount();
        int hitIndex = UserMenuBar->HitTest(p.x, p.y);

        int imIndex;
        int imAfter;
        pasteIndex = -1;
        if (!UserMenuBar->InsertMarkHitTest(p.x, p.y, imIndex, imAfter))
        {
            imIndex = -1;
            if (hitIndex >= 0)
                pasteIndex = hitIndex;
        }
        else
        {
            if (imIndex == -1)
                imIndex = 0;
        }

        if (!insert)
        {
            imIndex = -1;
            imAfter = FALSE;
            if (hitIndex >= 0)
                pasteIndex = hitIndex;
        }

        insertIndex = imIndex;
        after = imAfter;
        if (after)
            insertIndex++;

        if (pasteIndex != -1)
        {
            insertIndex = -1;
            // verify that it is a target
            TLBI_ITEM_INFO2 tii;
            tii.Mask = TLBI_MASK_ID;
            if (UserMenuBar->GetItemInfo2(pasteIndex, TRUE, &tii))
            {
                CUserMenuItem* item = MainWindow->UserMenuItems->At(tii.ID - CM_USERMENU_MIN);
                if (ExpandCommand(MainWindow->HWindow, item->UMCommand.c_str(), fileName, TRUE))
                {
                    if (!HasDropTarget(fileName.c_str()))
                        pasteIndex = -1;
                }
                else
                    pasteIndex = -1;
            }
        }

        UserMenuBar->SetInsertMark(imIndex, imAfter);
        UserMenuBar->SetHotItem(pasteIndex);
    }

    BOOL GetPathFromDataObject(IDataObject* pDataObject, std::wstring& path)
    {
        path.clear();
        if (IsFakeDataObject(pDataObject, NULL, NULL))
            return FALSE;

        FORMATETC formatEtc;
        formatEtc.cfFormat = CF_HDROP;
        formatEtc.ptd = NULL;
        formatEtc.dwAspect = DVASPECT_CONTENT;
        formatEtc.lindex = -1;
        formatEtc.tymed = TYMED_HGLOBAL;

        STGMEDIUM stgMedium;
        stgMedium.tymed = TYMED_HGLOBAL;
        stgMedium.hGlobal = NULL;
        stgMedium.pUnkForRelease = NULL;

        BOOL ret = FALSE;
        if (pDataObject->GetData(&formatEtc, &stgMedium) == S_OK)
        {
            if (stgMedium.tymed == TYMED_HGLOBAL && stgMedium.hGlobal != NULL)
            {
                DROPFILES* data = (DROPFILES*)HANDLES(GlobalLock(stgMedium.hGlobal));
                if (data != NULL)
                {
                    if (sally::clipboard::TryGetSingleHDropPath(
                            data, GlobalSize(stgMedium.hGlobal), path))
                    {
                        ret = TRUE;
                    }

                    HANDLES(GlobalUnlock(stgMedium.hGlobal));
                }
            }
            ReleaseStgMedium(&stgMedium);
        }
        if (ret && !FileExistsW(path.c_str()))
            ret = FALSE;
        return ret;
    }

    STDMETHOD(DragEnter)
    (IDataObject* pDataObject, DWORD grfKeyState,
     POINTL pt, DWORD* pdwEffect)
    {
        if (ImageDragging)
            ImageDragEnter(pt.x, pt.y);
        if (DataObject != NULL)
            DataObject->Release();
        DataObject = pDataObject;
        DataObject->AddRef();

        if (DataObject != NULL)
        {
            // perform hit test
            int insertIndex = -1;
            BOOL after;
            int pasteIndex = -1;
            std::wstring fileName;
            std::wstring buff;
            BOOL insert = GetPathFromDataObject(pDataObject, buff);

            HitTest(pt, insertIndex, after, pasteIndex, fileName, insert);

            if (insertIndex != -1)
            {
                if (insert)
                    *pdwEffect = DROPEFFECT_LINK;
                else
                    *pdwEffect = DROPEFFECT_NONE;
            }
            else
            {
                if (pasteIndex != -1)
                {
                    if (fileName != DropTargetFileName &&
                        !IsFakeDataObject(pDataObject, NULL, NULL))
                    {
                        if (DropTarget != NULL)
                        {
                            DropTarget->DragLeave();
                            DropTarget->Release();
                            DropTargetFileName.clear();
                        }
                        DropTarget = CreateIDropTargetW(UserMenuBar->HWindow, fileName.c_str());
                        if (DropTarget != NULL)
                            DropTargetFileName = fileName;
                    }
                    if (DropTarget != NULL)
                    {
                        return DropTarget->DragEnter(pDataObject, grfKeyState, pt, pdwEffect);
                    }
                }
                *pdwEffect = DROPEFFECT_NONE;
            }
        }
        return S_OK;
    }

    STDMETHOD(DragOver)
    (DWORD grfKeyState, POINTL pt, DWORD* pdwEffect)
    {
        if (ImageDragging)
            ImageDragMove(pt.x, pt.y);
        if (DataObject != NULL)
        {
            // perform hit test
            int insertIndex = -1;
            BOOL after;
            int pasteIndex = -1;
            std::wstring fileName;
            std::wstring buff;
            BOOL insert = GetPathFromDataObject(DataObject, buff);

            HitTest(pt, insertIndex, after, pasteIndex, fileName, insert);

            if (insertIndex != -1)
            {
                if (insert)
                    *pdwEffect = DROPEFFECT_LINK;
                else
                    *pdwEffect = DROPEFFECT_NONE;
            }
            else
            {
                if (pasteIndex != -1)
                {
                    if (fileName != DropTargetFileName &&
                        !IsFakeDataObject(DataObject, NULL, NULL))
                    {
                        if (DropTarget != NULL)
                        {
                            DropTarget->DragLeave();
                            DropTarget->Release();
                            DropTargetFileName.clear();
                        }
                        DropTarget = CreateIDropTargetW(UserMenuBar->HWindow, fileName.c_str());
                        if (DropTarget != NULL)
                        {
                            DropTargetFileName = fileName;
                            DropTarget->DragEnter(DataObject, grfKeyState, pt, pdwEffect);
                        }
                    }
                    if (DropTarget != NULL)
                    {
                        return DropTarget->DragOver(grfKeyState, pt, pdwEffect);
                    }
                }
                *pdwEffect = DROPEFFECT_NONE;
            }
        }
        return S_OK;
    }

    STDMETHOD(DragLeave)
    ()
    {
        if (ImageDragging)
            ImageDragLeave();
        if (DropTarget != NULL)
        {
            DropTarget->DragLeave();
            DropTarget->Release();
            DropTarget = NULL;
            DropTargetFileName.clear();
        }
        if (DataObject != NULL)
        {
            DataObject->Release();
            DataObject = NULL;
        }

        // clear the insert mark
        UserMenuBar->SetInsertMark(-1, FALSE);
        // clear the hot item
        UserMenuBar->SetHotItem(-1);

        return E_UNEXPECTED;
    }

    STDMETHOD(Drop)
    (IDataObject* pDataObject, DWORD grfKeyState, POINTL pt,
     DWORD* pdwEffect)
    {
        HRESULT ret = E_UNEXPECTED;

        if (ImageDragging)
            ImageDragLeave();
        if (DataObject != NULL)
        {
            // perform hit test
            int insertIndex = -1;
            BOOL after;
            int pasteIndex = -1;
            std::wstring fileName;
            std::wstring buff;
            BOOL insert = GetPathFromDataObject(pDataObject, buff);

            HitTest(pt, insertIndex, after, pasteIndex, fileName, insert);

            if (insertIndex != -1)
            {
                // clear the insert mark
                UserMenuBar->SetInsertMark(-1, FALSE);

                if (insert)
                {
                    *pdwEffect = DROPEFFECT_LINK;
                    const size_t slash = buff.find_last_of(L"\\/");
                    const std::wstring name = slash == std::wstring::npos ? buff : buff.substr(slash + 1);
                    BOOL shell = FALSE;

                    // CMainWindow::UserMenu was changed so that if it does not launch via Shell,
                    // it calls ShellExecuteEx instead of CreateProcess; therefore quotes are no longer needed
                    /*
            // if it is not an executable file (.exe, .com, .bat, .pif),
            // wrap the name in quotes and run it through the shell
            wchar_t *dot = strrchr(buff, '.');
            if (dot != NULL && *(dot + 1) != 0)
            {
              dot++;
              BOOL executable = FALSE;
              if (stricmp(dot, "exe") == 0 ||
                  stricmp(dot, "com") == 0 ||
                  stricmp(dot, "bat") == 0 ||
                  stricmp(dot, "pif") == 0)
                executable = TRUE;

              if (!executable)
              {
                strcpy(tmp, buff);
                sprintf(buff, "\"%s\"", tmp);
                shell = TRUE;
              }
            }
*/

                    EscapeHotPathDollars(buff);

                    static wchar_t emptyBuffer[] = L"";
                    static wchar_t fullPathBuffer[] = L"$(FullPath)";
                    CUserMenuItem* item = new CUserMenuItem(name.c_str(), buff.c_str(), emptyBuffer, fullPathBuffer, emptyBuffer,
                                                            shell, FALSE, FALSE, TRUE, umitItem, NULL);

                    // find the place where the item should be inserted
                    int count = 0;
                    int i;
                    for (i = 0; i < MainWindow->UserMenuItems->Count; i++)
                    {
                        CUserMenuItem* item2 = MainWindow->UserMenuItems->At(i);
                        if (count >= insertIndex)
                            break;

                        if (item2->Type == umitSubmenuBegin)
                        {
                            int endIndex = MainWindow->UserMenuItems->GetSubmenuEndIndex(i);
                            if (endIndex != -1)
                                i = endIndex;
                        }

                        if (item2->ShowInToolbar)
                            count++;
                    }
                    MainWindow->UserMenuItems->Insert(i, item);

                    if (UserMenuIconBkgndReader.IsReadingIcons()) // icon loading in progress = must restart it, item count in user menu changed (side effect: drops just-loaded icon of the dropped file, but we ignore that)
                    {
                        CUserMenuIconDataArr* bkgndReaderData = new CUserMenuIconDataArr();
                        for (int i2 = 0; i2 < MainWindow->UserMenuItems->Count; i2++)
                            MainWindow->UserMenuItems->At(i2)->GetIconHandle(bkgndReaderData, FALSE);
                        UserMenuIconBkgndReader.StartBkgndReadingIcons(bkgndReaderData); // WARNING: frees 'bkgndReaderData'
                    }

                    MainWindow->UMToolBar->CreateButtons();
                }
                else
                    *pdwEffect = DROPEFFECT_NONE;
            }
            else
            {
                if (pasteIndex != -1)
                {
                    if (fileName != DropTargetFileName &&
                        !IsFakeDataObject(pDataObject, NULL, NULL))
                    {
                        if (DropTarget != NULL)
                        {
                            DropTarget->DragLeave();
                            DropTarget->Release();
                            DropTargetFileName.clear();
                        }
                        DropTarget = CreateIDropTargetW(UserMenuBar->HWindow, fileName.c_str());
                        if (DropTarget != NULL)
                            DropTargetFileName = fileName;
                    }
                    if (DropTarget != NULL)
                    {
                        ret = DropTarget->Drop(pDataObject, grfKeyState, pt, pdwEffect);
                    }
                }
            }
        }

        if (DataObject != NULL)
        {
            DataObject->Release();
            DataObject = NULL;
        }

        if (DropTarget != NULL)
        {
            DropTarget->Release();
            DropTarget = NULL;
            DropTargetFileName.clear();
        }

        return ret;
    }
};

//*****************************************************************************
//
// CUserMenuBar
//

CUserMenuBar::CUserMenuBar(HWND hNotifyWindow, CObjectOrigin origin)
    : CToolBar(hNotifyWindow, origin){
          CALL_STACK_MESSAGE_NONE}

      BOOL CUserMenuBar::CreateButtons()
{
    CALL_STACK_MESSAGE1("CUserMenuBar::CreateButtons()");
    if (HWindow == NULL)
        return FALSE;

    RemoveAllItems();
    SetStyle(TLB_STYLE_IMAGE | (Configuration.UserMenuToolbarLabels ? TLB_STYLE_TEXT : 0));

    // load icons into own toolbar
    // insert only items and submenus from the top level; the rest will expand as submenus
    int level = 0;
    TLBI_ITEM_INFO2 tii;
    int i;
    for (i = 0; i < MainWindow->UserMenuItems->Count; i++)
    {
        CUserMenuItem* item = MainWindow->UserMenuItems->At(i);
        switch (item->Type)
        {
        case umitSubmenuEnd:
        {
            level--;
            break;
        }

        case umitSeparator:
        {
            if (level == 0 && item->ShowInToolbar)
            {
                tii.Mask = TLBI_MASK_STYLE;
                tii.Style = TLBI_STYLE_SEPARATOR;
                InsertItem2(0xFFFFFFFF, TRUE, &tii);
            }
            break;
        }

        case umitItem:
        case umitSubmenuBegin:
        {
            if (level == 0 && item->ShowInToolbar)
            {
                tii.Mask = TLBI_MASK_STYLE | TLBI_MASK_TEXT | TLBI_MASK_ICON |
                           TLBI_MASK_ID | TLBI_MASK_ENABLER;
                tii.Style = TLBI_STYLE_SHOWTEXT | TLBI_STYLE_NOPREFIX;
                if (item->Type == umitSubmenuBegin)
                    tii.Style |= TLBI_STYLE_WHOLEDROPDOWN | TLBI_STYLE_DROPDOWN;
                std::wstring text = item->ItemName;
                RemoveAmpersands(text.data());
                text.resize(wcslen(text.c_str()));
                tii.Text = text.data();
                tii.HIcon = item->UMIcon;
                tii.ID = CM_USERMENU_MIN + i;
                tii.Enabler = &EnablerOnDisk;
                InsertItem2(0xFFFFFFFF, TRUE, &tii);
            }
            if (item->Type == umitSubmenuBegin)
                level++;
            break;
        }
        }
    }

    UpdateItemsState();

    return TRUE;
}

void CUserMenuBar::ToggleLabels()
{
    CALL_STACK_MESSAGE1("CUserMenuBar::ToggleLabels()");
    // set the style
    DWORD style = GetStyle();
    if (Configuration.UserMenuToolbarLabels)
        style &= ~TLB_STYLE_TEXT;
    else
        style |= TLB_STYLE_TEXT;
    Configuration.UserMenuToolbarLabels = !Configuration.UserMenuToolbarLabels;
    SetStyle(style);
}

int CUserMenuBar::GetNeededHeight()
{
    CALL_STACK_MESSAGE_NONE
    // even if we hold no icon, return the correct height
    int height = CToolBar::GetNeededHeight();
    int iconSize = GetIconSizeForSystemDPI(ICONSIZE_16);
    int minH = 3 + iconSize + 3;
    if (height < minH)
        height = minH;
    return height;
}

void CUserMenuBar::Customize()
{
    CALL_STACK_MESSAGE_NONE
    // let it open the UserMenu page and edit the item index
    PostMessage(MainWindow->HWindow, WM_USER_CONFIGURATION, 2, 0);
}

void CUserMenuBar::SetInsertMark(int index, BOOL after)
{
    CALL_STACK_MESSAGE3("CUserMenuBar::SetInsertMark(%d, %d)", index, after);
    if (InserMarkIndex == index && InserMarkAfter == after)
        return;
    if (ImageDragging)
        ImageDragShow(FALSE);
    CToolBar::SetInsertMark(index, after);
    if (ImageDragging)
        ImageDragShow(TRUE);
}

int CUserMenuBar::SetHotItem(int index)
{
    CALL_STACK_MESSAGE2("CUserMenuBar::SetHotItem(%d)", index);
    if (HotIndex == index)
        return index;

    if (ImageDragging)
        ImageDragShow(FALSE);
    int ret = CToolBar::SetHotItem(index);
    if (ImageDragging)
        ImageDragShow(TRUE);
    return ret;
}

void CUserMenuBar::OnGetToolTip(LPARAM lParam)
{
    CALL_STACK_MESSAGE2("CUserMenuBar::OnGetToolTip(0x%IX)", lParam);
    TOOLBAR_TOOLTIP* tt = (TOOLBAR_TOOLTIP*)lParam;
    if (tt->ID - CM_USERMENU_MIN < (DWORD)MainWindow->UserMenuItems->Count)
    {
        CUserMenuItem* item = MainWindow->UserMenuItems->At(tt->ID - CM_USERMENU_MIN);
        if (Configuration.UserMenuToolbarLabels)
        {
            std::wstring umCommand;
            if (ExpandCommand(MainWindow->HWindow, item->UMCommand.c_str(), umCommand, TRUE))
                lstrcpynW(tt->Buffer, umCommand.c_str(), TOOLTIP_TEXT_MAX);
        }
        else
            lstrcpynW(tt->Buffer, item->ItemName.c_str(), TOOLTIP_TEXT_MAX);
    }
}

LRESULT
CUserMenuBar::WindowProc(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    CALL_STACK_MESSAGE4("CUserMenuBar::WindowProc(0x%X, 0x%IX, 0x%IX)", uMsg, wParam, lParam);
    switch (uMsg)
    {
    case WM_CREATE:
    {
        CUMDropTarget* dropTarget = new CUMDropTarget(this);
        if (dropTarget != NULL)
        {
            if (HANDLES(RegisterDragDrop(HWindow, dropTarget)) != S_OK)
            {
                TRACE_E("RegisterDragDrop error.");
            }
            dropTarget->Release(); // RegisterDragDrop called AddRef()
        }
        break;
    }

    case WM_DESTROY:
    {
        HANDLES(RevokeDragDrop(HWindow));
        break;
    }
    }
    return CToolBar::WindowProc(uMsg, wParam, lParam);
}
