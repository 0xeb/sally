// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

/*
	Automation Plugin for Open Salamander
	
	Copyright (c) 2009-2023 Milan Kase <manison@manison.cz>
	Copyright (c) 2010-2023 Open Salamander Authors
	
	cfgdlg.cpp
	Configuration dialog.
*/

#include "precomp.h"
#include "automationplug.h"
#include "cfgdlg.h"
#include "lang\lang.rh"

extern HINSTANCE g_hLangInst;
extern CAutomationPluginInterface g_oAutomationPlugin;
extern CSalamanderGeneralAbstract* SalamanderGeneral;

const char CAutomationConfigDialog::SublassPropName[] = "AutCfgDlg";

namespace
{
std::wstring GetListViewItemTextOwned(HWND list, int item)
{
    size_t capacity = 2;
    const size_t limit = static_cast<size_t>((std::numeric_limits<int>::max)());
    for (;;)
    {
        std::vector<wchar_t> buffer(capacity, L'\0');
        LVITEMW entry = {};
        entry.iSubItem = 0;
        entry.pszText = buffer.data();
        entry.cchTextMax = static_cast<int>(buffer.size());
        const LRESULT copied = SendMessageW(
            list, LVM_GETITEMTEXTW, item, reinterpret_cast<LPARAM>(&entry));
        if (copied <= 0)
            return std::wstring();
        if (copied < static_cast<LRESULT>(buffer.size() - 1))
            return std::wstring(buffer.data(), static_cast<size_t>(copied));
        if (capacity > limit / 2)
            return std::wstring();
        capacity *= 2;
    }
}
}

CAutomationConfigDialog::CAutomationConfigDialog(HWND hwndParent)
    : CDialog(g_hLangInst, IDD_CONFIG, IDD_CONFIG, hwndParent)
{
    m_hwndSubclassedEdit = NULL;
    m_pfnSubclassedEdit = NULL;
    m_pHeader = NULL;
    m_nBrowseBtnState = PBS_NORMAL;
    m_bMouseCaptured = false;
    m_bBrowsing = false;
}

INT_PTR CAutomationConfigDialog::DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
    case WM_INITDIALOG:
    {
        // Transfer variables first.
        INT_PTR res = CDialog::DialogProc(uMsg, wParam, lParam);
        OnInitDialog();
        return res;
    }

    case WM_NOTIFY:
    {
        NMHDR* pnmhdr = reinterpret_cast<NMHDR*>(lParam);
        if (pnmhdr->idFrom == IDC_DIRLIST)
        {
            return OnDirListNotify(pnmhdr);
        }
        break;
    }

    case WM_COMMAND:
    {
        HWND hwndFrom = reinterpret_cast<HWND>(lParam);
        HWND hwndList = GetDlgItem(HWindow, IDC_DIRLIST);
        if (hwndFrom == hwndList || GetParent(hwndFrom) == hwndList)
        {
            // command forwarded from the list view,
            // do not pass it to the CDialog::DialogProc
            // since if it is IDOK the dialog will close
            return FALSE;
        }

        if (LOWORD(wParam) == IDC_DIRLISTHEADER)
        {
            if (OnHeaderCommand(HIWORD(wParam)))
            {
                return TRUE;
            }
        }

        break;
    }

    case WM_USER_RECALCWIDTH:
        // this is posted as the result of end-label-edit
        // notification which may require change of the column
        // width if the new text is longer than the previous one
        ListView_SetColumnWidth(GetDlgItem(HWindow, IDC_DIRLIST),
                                0, LVSCW_AUTOSIZE);
        break;
    }

    return CDialog::DialogProc(uMsg, wParam, lParam);
}

void CAutomationConfigDialog::Transfer(CTransferInfo& ti)
{
    int bEnableDebugger;
    int cDirs;
    int iDir;
    LVITEMW lvi = {
        0,
    };
    HWND hwndList = GetDlgItem(HWindow, IDC_DIRLIST);
    _ASSERTE(hwndList);
    ListView_SetUnicodeFormat(hwndList, TRUE);

    if (ti.Type == ttDataToWindow)
    {
        bEnableDebugger = g_oAutomationPlugin.IsDebuggerEnabled();

        LVCOLUMNW lvcol = {
            0,
        };
        SendMessageW(hwndList, LVM_INSERTCOLUMNW, 0, reinterpret_cast<LPARAM>(&lvcol));

        cDirs = g_oAutomationPlugin.GetScriptDirectoryCount();
        lvi.mask = LVIF_TEXT;
        for (iDir = 0; iDir < cDirs; iDir++)
        {
            lvi.iItem = iDir;
            lvi.pszText = const_cast<PWSTR>(g_oAutomationPlugin.GetScriptDirectoryRaw(iDir));
            SendMessageW(hwndList, LVM_INSERTITEMW, 0, reinterpret_cast<LPARAM>(&lvi));
        }

        ListView_SetColumnWidth(hwndList, 0, LVSCW_AUTOSIZE);
    }

    ti.CheckBox(IDC_ENABLEDBG, bEnableDebugger);

    if (ti.Type == ttDataFromWindow)
    {
        g_oAutomationPlugin.EnableDebugger(!!bEnableDebugger);

        g_oAutomationPlugin.RemoveAllScriptDirectories();
        cDirs = ListView_GetItemCount(hwndList);
        lvi.mask = LVIF_TEXT;
        for (iDir = 0; iDir < cDirs; iDir++)
        {
            const std::wstring directory = GetListViewItemTextOwned(hwndList, iDir);
            g_oAutomationPlugin.AddScriptDirectory(directory.c_str());
        }
    }
}

void CAutomationConfigDialog::OnInitDialog()
{
    if (Parent != NULL)
    {
        SalamanderGeneral->MultiMonCenterWindow(HWindow, Parent, TRUE);
    }

    m_pHeader = SalamanderGUI->AttachToolbarHeader(HWindow,
                                                   IDC_DIRLISTHEADER, GetDlgItem(HWindow, IDC_DIRLIST),
                                                   TLBHDRMASK_NEW | TLBHDRMASK_MODIFY | TLBHDRMASK_DELETE |
                                                       TLBHDRMASK_UP | TLBHDRMASK_DOWN);
    // enable/disable toolbar buttons
    DirSelChanged();
}

BOOL CAutomationConfigDialog::OnDirListNotify(NMHDR* pnmhdr)
{
    switch (pnmhdr->code)
    {
    case LVN_KEYDOWN:
        return OnDirListKeyDown(reinterpret_cast<NMLVKEYDOWN*>(pnmhdr));

    case LVN_BEGINLABELEDITW:
        return OnDirListBeginLabelEdit(reinterpret_cast<NMLVDISPINFOW*>(pnmhdr));

    case LVN_ENDLABELEDITW:
        return OnDirListEndLabelEdit(reinterpret_cast<NMLVDISPINFOW*>(pnmhdr));

    case LVN_ITEMCHANGED:
        return OnDirListItemChanged(reinterpret_cast<NMLISTVIEW*>(pnmhdr));
    }

    return FALSE;
}

BOOL CAutomationConfigDialog::OnDirListKeyDown(NMLVKEYDOWN* pnmkey)
{
    switch (pnmkey->wVKey)
    {
    case VK_F2:
        return EditDirectory();

    case VK_DELETE:
        return DeleteDirectory();

    case VK_INSERT:
        return NewDirectory();

    case VK_UP:
    case VK_DOWN:
    {
        if (GetKeyState(VK_MENU) & 0x8000) // Alt key
        {
            return MoveDirectory(pnmkey->wVKey == VK_UP ? -1 : +1);
        }
        break;
    }
    }

    return FALSE;
}

BOOL CAutomationConfigDialog::EditDirectory()
{
    int iSel;
    HWND hwndList;

    hwndList = GetDlgItem(HWindow, IDC_DIRLIST);

    iSel = ListView_GetNextItem(hwndList, -1, LVNI_ALL | LVNI_SELECTED);
    if (iSel >= 0)
    {
        SendMessageW(hwndList, LVM_EDITLABELW, iSel, 0);
        return TRUE;
    }

    return FALSE;
}

BOOL CAutomationConfigDialog::DeleteDirectory()
{
    int iSel;
    HWND hwndList;

    hwndList = GetDlgItem(HWindow, IDC_DIRLIST);

    iSel = ListView_GetNextItem(hwndList, -1, LVNI_ALL | LVNI_SELECTED);
    if (iSel >= 0)
    {
        ListView_DeleteItem(hwndList, iSel);

        if (ListView_GetItemCount(hwndList) > 0)
        {
            if (iSel > 0)
            {
                --iSel;
            }

            ListView_SetItemState(hwndList, iSel, LVIS_SELECTED, LVIS_SELECTED);
        }

        return TRUE;
    }

    return FALSE;
}

BOOL CAutomationConfigDialog::MoveDirectory(int iDelta)
{
    int iSel, iNew;
    HWND hwndList;
    LVITEMW lvi1 = {
        0,
    };
    LVITEMW lvi2 = {
        0,
    };
    wchar_t szItemText1[1024];
    wchar_t szItemText2[1024];

    hwndList = GetDlgItem(HWindow, IDC_DIRLIST);

    iSel = ListView_GetNextItem(hwndList, -1, LVNI_ALL | LVNI_SELECTED);
    if (iSel < 0)
    {
        return FALSE;
    }

    iNew = iSel + iDelta;
    if (iNew < 0 || iNew >= ListView_GetItemCount(hwndList))
    {
        return FALSE;
    }

    lvi1.mask = LVIF_TEXT | LVIF_STATE;
    lvi1.stateMask = ~0u;
    lvi1.iItem = iSel;
    lvi1.pszText = szItemText1;
    lvi1.cchTextMax = _countof(szItemText1);
    SendMessageW(hwndList, LVM_GETITEMW, 0, reinterpret_cast<LPARAM>(&lvi1));

    lvi2.mask = LVIF_TEXT | LVIF_STATE;
    lvi2.stateMask = ~0u;
    lvi2.iItem = iNew;
    lvi2.pszText = szItemText2;
    lvi2.cchTextMax = _countof(szItemText2);
    SendMessageW(hwndList, LVM_GETITEMW, 0, reinterpret_cast<LPARAM>(&lvi2));

    lvi1.iItem = iNew;
    lvi2.iItem = iSel;
    lvi1.mask = lvi2.mask = LVIF_TEXT | LVIF_STATE;
    lvi1.stateMask = lvi2.stateMask = ~0u;

    // swap the items
    SendMessageW(hwndList, LVM_SETITEMW, 0, reinterpret_cast<LPARAM>(&lvi1));
    SendMessageW(hwndList, LVM_SETITEMW, 0, reinterpret_cast<LPARAM>(&lvi2));

    return TRUE;
}

BOOL CAutomationConfigDialog::NewDirectory()
{
    LVITEMW lvi = {
        0,
    };
    int iSel;
    HWND hwndList;

    hwndList = GetDlgItem(HWindow, IDC_DIRLIST);

    lvi.mask = LVIF_STATE /*LVIF_TEXT*/;
    lvi.iItem = INT_MAX;
    lvi.state = LVIS_SELECTED;
    iSel = static_cast<int>(SendMessageW(hwndList, LVM_INSERTITEMW, 0, reinterpret_cast<LPARAM>(&lvi)));
    ListView_EnsureVisible(hwndList, iSel, FALSE);
    SendMessageW(hwndList, LVM_EDITLABELW, iSel, 0);

    return TRUE;
}

BOOL CAutomationConfigDialog::OnDirListBeginLabelEdit(NMLVDISPINFOW* nmlv)
{
    HWND hwndEdit = ListView_GetEditControl(nmlv->hdr.hwndFrom);
    _ASSERTE(IsWindow(hwndEdit));

    // autocomplete must be attached before we subclass
    // the edit control to not mess the subclass chain
    // while unsubclassing
    SHAutoComplete(hwndEdit, SHACF_FILESYS_DIRS);

    SubclassLabelEdit(hwndEdit);

    m_pHeader->EnableToolbar(0);

    return TRUE;
}

BOOL CAutomationConfigDialog::OnDirListEndLabelEdit(NMLVDISPINFOW* nmlv)
{
    UnsubclassLabelEdit();

    if (nmlv->item.pszText == NULL)
    {
        if (!m_bBrowsing)
        {
            // the edit was cancelled,
            // if the text is empty (e.g. new directory insertion
            // was cancelled), delete the item
            if (GetListViewItemTextOwned(nmlv->hdr.hwndFrom,
                                         nmlv->item.iItem)
                    .empty())
                DeleteDirectory();
        }

        DirSelChanged();

        return TRUE;
    }
    else if (nmlv->item.pszText[0] == L'\0')
    {
        // the text was deleted, remove the item
        DeleteDirectory();
        return TRUE;
    }

    // TODO: validate the path

    // accept the edited text
    SetWindowLongPtr(HWindow, DWLP_MSGRESULT, TRUE);

    // the width of the column might be changed,
    // defer recalcing the width once the new text will
    // be set after we return from this notification handler
    PostMessage(HWindow, WM_USER_RECALCWIDTH, 0, 0);

    DirSelChanged();

    return TRUE;
}

BOOL CAutomationConfigDialog::OnDirListItemChanged(NMLISTVIEW* nmlv)
{
    DirSelChanged();
    return TRUE;
}

void CAutomationConfigDialog::DirSelChanged()
{
    DWORD dwEnable;
    int iSel;
    HWND hwndList;

    hwndList = GetDlgItem(HWindow, IDC_DIRLIST);
    dwEnable = TLBHDRMASK_NEW;

    iSel = ListView_GetNextItem(hwndList, -1, LVNI_ALL | LVNI_SELECTED);
    if (iSel >= 0)
    {
        dwEnable |= TLBHDRMASK_MODIFY | TLBHDRMASK_DELETE;

        if (iSel != 0)
        {
            dwEnable |= TLBHDRMASK_UP;
        }

        int cItems = ListView_GetItemCount(hwndList);
        _ASSERTE(cItems > 0);
        if (iSel != cItems - 1)
        {
            dwEnable |= TLBHDRMASK_DOWN;
        }
    }

    m_pHeader->EnableToolbar(dwEnable);
}

BOOL CAutomationConfigDialog::OnHeaderCommand(int nId)
{
    switch (nId)
    {
    case TLBHDR_MODIFY:
        return EditDirectory();

    case TLBHDR_NEW:
        return NewDirectory();

    case TLBHDR_DELETE:
        return DeleteDirectory();

    case TLBHDR_UP:
        return MoveDirectory(-1);

    case TLBHDR_DOWN:
        return MoveDirectory(1);

    default:
        _ASSERTE(0);
    }

    return FALSE;
}

void CAutomationConfigDialog::SubclassLabelEdit(HWND hwndEdit)
{
    if (!SetPropA(hwndEdit, SublassPropName, this))
    {
        return;
    }

    m_hwndSubclassedEdit = hwndEdit;
    m_pfnSubclassedEdit = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(
        hwndEdit, GWLP_WNDPROC,
        reinterpret_cast<LONG_PTR>(&LabelEditSubclassProc)));

    m_nBrowseBtnState = PBS_NORMAL;
    m_bMouseCaptured = false;
}

void CAutomationConfigDialog::UnsubclassLabelEdit()
{
    if (IsWindow(m_hwndSubclassedEdit))
    {
        SetWindowLongPtrW(m_hwndSubclassedEdit, GWLP_WNDPROC,
                         reinterpret_cast<LONG_PTR>(m_pfnSubclassedEdit));
        SetPropA(m_hwndSubclassedEdit, SublassPropName, NULL);
    }

    m_hwndSubclassedEdit = NULL;
    m_pfnSubclassedEdit = NULL;
}

LRESULT CALLBACK CAutomationConfigDialog::LabelEditSubclassProc(
    HWND hwnd,
    UINT uMsg,
    WPARAM wParam,
    LPARAM lParam)
{
    LRESULT res;
    CAutomationConfigDialog* pDialog;
    pDialog = reinterpret_cast<CAutomationConfigDialog*>(GetPropA(hwnd, SublassPropName));
    _ASSERTE(pDialog);

    switch (uMsg)
    {
    case WM_WINDOWPOSCHANGING:
    {
        WINDOWPOS* wp = reinterpret_cast<WINDOWPOS*>(lParam);
        if (!(wp->flags & SWP_NOSIZE))
        {
            HWND hwndList = GetDlgItem(pDialog->HWindow, IDC_DIRLIST);
            RECT rc = {
                0,
            };
#if 0				
				rc.left = LVIR_BOUNDS;
				SendMessage(hwndList, LVM_GETITEMRECT, 0,
					reinterpret_cast<LPARAM>(&rc));

#else
            GetClientRect(hwndList, &rc);
            rc.left += 4;
#endif
            wp->cx = rc.right - rc.left;
        }
        break;
    }

    case WM_NCCALCSIZE:
    {
        NCCALCSIZE_PARAMS* nccs;

        res = CallWindowProcW(pDialog->m_pfnSubclassedEdit,
                             hwnd, uMsg, wParam, lParam);

        nccs = reinterpret_cast<NCCALCSIZE_PARAMS*>(lParam);
        pDialog->m_rcBrowseBtn = nccs->rgrc[0];
        nccs->rgrc[0].right -= BROWSEBTN_WIDTH;
        pDialog->m_rcBrowseBtn.left = nccs->rgrc[0].right;
        if (wParam)
        {
            // convert button coordinates from parent
            // client coordinates to control window
            // coordinates
            OffsetRect(&pDialog->m_rcBrowseBtn,
                       -nccs->rgrc[1].left,
                       -nccs->rgrc[1].top);
        }

        // normalize rect
        LONG tmp;
        if (pDialog->m_rcBrowseBtn.left > pDialog->m_rcBrowseBtn.right)
        {
            tmp = pDialog->m_rcBrowseBtn.left;
            pDialog->m_rcBrowseBtn.left = pDialog->m_rcBrowseBtn.right;
            pDialog->m_rcBrowseBtn.right = tmp;
        }
        if (pDialog->m_rcBrowseBtn.top > pDialog->m_rcBrowseBtn.bottom)
        {
            tmp = pDialog->m_rcBrowseBtn.top;
            pDialog->m_rcBrowseBtn.top = pDialog->m_rcBrowseBtn.bottom;
            pDialog->m_rcBrowseBtn.bottom = tmp;
        }

        // FIXME
        OffsetRect(&pDialog->m_rcBrowseBtn, -1, -pDialog->m_rcBrowseBtn.top);

        return res;
    }

    case WM_NCPAINT:
    {
        res = CallWindowProcW(pDialog->m_pfnSubclassedEdit,
                             hwnd, uMsg, wParam, lParam);

        pDialog->DrawBrowseBtn();

        return res;
    }

    case WM_NCHITTEST:
    {
        res = CallWindowProcW(pDialog->m_pfnSubclassedEdit,
                             hwnd, uMsg, wParam, lParam);

        if (res == HTNOWHERE && pDialog->ScreenPointInButtonRect(MAKEPOINTS(lParam)))
        {
            res = HTMENU;
        }

        return res;
    }

    case WM_NCLBUTTONDOWN:
    {
        res = CallWindowProcW(pDialog->m_pfnSubclassedEdit,
                             hwnd, uMsg, wParam, lParam);

        if (pDialog->ScreenPointInButtonRect(MAKEPOINTS(lParam)))
        {
            SetCapture(hwnd);
            pDialog->m_bMouseCaptured = true;
            pDialog->m_nBrowseBtnState = PBS_PRESSED;
            pDialog->DrawBrowseBtn();
        }

        return res;
    }

    case WM_LBUTTONUP:
    {
        res = CallWindowProcW(pDialog->m_pfnSubclassedEdit,
                             hwnd, uMsg, wParam, lParam);

        if (pDialog->m_bMouseCaptured)
        {
            POINT pt;

            pt.x = GET_X_LPARAM(lParam);
            pt.y = GET_Y_LPARAM(lParam);

            ReleaseCapture();
            pDialog->m_bMouseCaptured = false;

            if (pDialog->m_nBrowseBtnState != PBS_NORMAL)
            {
                pDialog->m_nBrowseBtnState = PBS_NORMAL;
                pDialog->DrawBrowseBtn();
            }

            ClientToScreen(hwnd, &pt);
            if (pDialog->ScreenPointInButtonRect(pt))
            {
                pDialog->OnBrowseBtnClicked();
            }
        }

        return res;
    }

    case WM_NCDESTROY:
        SetPropA(hwnd, SublassPropName, NULL);
        break;
    }

    return CallWindowProcW(pDialog->m_pfnSubclassedEdit, hwnd, uMsg, wParam, lParam);
}

void CAutomationConfigDialog::DrawBrowseBtn()
{
    bool bFallback = true;
    HDC hDC = GetWindowDC(m_hwndSubclassedEdit);

    HTHEME hTheme;

    hTheme = OpenThemeData(m_hwndSubclassedEdit, L"button");
    if (hTheme != NULL)
    {
        bFallback = false;
        DrawThemeBackground(hTheme, hDC, BP_PUSHBUTTON, m_nBrowseBtnState, &m_rcBrowseBtn, NULL);
        CloseThemeData(hTheme);
    }

    if (bFallback)
    {
        UINT uState = 0;

        if (m_nBrowseBtnState == PBS_HOT)
        {
            uState = DFCS_HOT;
        }
        else if (m_nBrowseBtnState == PBS_PRESSED)
        {
            uState = DFCS_PUSHED;
        }

        DrawFrameControl(hDC, &m_rcBrowseBtn, DFC_BUTTON, DFCS_BUTTONPUSH | uState);
    }

    int width = m_rcBrowseBtn.right - m_rcBrowseBtn.left;
    int height = m_rcBrowseBtn.bottom - m_rcBrowseBtn.top;
    HDC hMemDC = CreateCompatibleDC(hDC);
    HBITMAP hMaskBmp = CreateBitmap(width, height, 1, 1, NULL);
    HGDIOBJ hOldBmp = SelectObject(hMemDC, hMaskBmp);

    RECT rcArrow;
    rcArrow.left = rcArrow.top = 0;
    rcArrow.right = width;
    rcArrow.bottom = height;
    DrawFrameControl(hMemDC, &rcArrow, DFC_MENU, DFCS_MENUARROW);

    SetTextColor(hDC, GetSysColor(COLOR_BTNTEXT));
    BitBlt(hDC, m_rcBrowseBtn.left + 1, m_rcBrowseBtn.top + 1, width, height, hMemDC, 0, 0, SRCAND);
    SelectObject(hMemDC, hOldBmp);
    DeleteDC(hMemDC);

    ReleaseDC(m_hwndSubclassedEdit, hDC);
}

bool CAutomationConfigDialog::ScreenPointInButtonRect(POINT pt)
{
    RECT rcEdit;

    GetWindowRect(m_hwndSubclassedEdit, &rcEdit);
    pt.x -= rcEdit.left;
    pt.y -= rcEdit.top;
    return !!PtInRect(&m_rcBrowseBtn, pt);
}

void CAutomationConfigDialog::OnBrowseBtnClicked()
{
    HMENU hMenu;
    HMENU hPopup;
    int nId;
    RECT rcBtnScreen;

    hMenu = LoadMenu(g_hLangInst, MAKEINTRESOURCE(IDM_BROWSE));
    _ASSERTE(hMenu);

    hPopup = GetSubMenu(hMenu, 0);
    _ASSERTE(hPopup);

    rcBtnScreen = m_rcBrowseBtn;
    MapWindowPoints(m_hwndSubclassedEdit, NULL, (POINT*)&rcBtnScreen, 2);

    nId = TrackPopupMenuEx(hPopup, TPM_NONOTIFY | TPM_RETURNCMD,
                           rcBtnScreen.right, rcBtnScreen.top,
                           m_hwndSubclassedEdit, NULL);

    DestroyMenu(hMenu);

    switch (nId)
    {
    case ID_BROWSE:
    {
        const std::wstring initialDirectory =
            SPLGetWindowTextOwned(m_hwndSubclassedEdit);

        m_bBrowsing = true;

        HWND hwndList = GetDlgItem(HWindow, IDC_DIRLIST);
        int iSel = ListView_GetNextItem(hwndList, -1, LVNI_ALL | LVNI_SELECTED);

        std::wstring selectedDirectory;
        if (SPLGetTargetDirectoryOwned(
                SalamanderGeneral, HWindow, HWindow,
                SPLLoadStrOwned(SalamanderGeneral, g_hLangInst, IDS_ADDDIRTITLE).c_str(),
                SPLLoadStrOwned(SalamanderGeneral, g_hLangInst, IDS_ADDDIRDESCR).c_str(),
                selectedDirectory, FALSE, initialDirectory.c_str()))
        {
            LVITEMW lvi = {
                0,
            };
            lvi.iSubItem = 0;
            lvi.pszText = const_cast<wchar_t*>(selectedDirectory.c_str());
            SendMessageW(hwndList, LVM_SETITEMTEXTW, iSel, reinterpret_cast<LPARAM>(&lvi));
            ListView_SetColumnWidth(hwndList, 0, LVSCW_AUTOSIZE);
        }
        else
        {
            if (GetListViewItemTextOwned(hwndList, iSel).empty())
                DeleteDirectory();
        }

        m_bBrowsing = false;

        DirSelChanged();

        break;
    }

    case ID_SALDIR:
        InsertVariable(L"$(SalDir)");
        break;

    case ID_ENVVAR:
        InsertVariable(L"$[]", 2);
        break;
    }
}

void CAutomationConfigDialog::InsertVariable(PCWSTR pszVar, int nCaretPos)
{
    int iStart, iEnd, nLen;

    nLen = GetWindowTextLengthW(m_hwndSubclassedEdit);
    SendMessageW(m_hwndSubclassedEdit, EM_GETSEL, (WPARAM)&iStart, (LPARAM)&iEnd);
    if (iStart == 0 && iEnd == nLen)
    {
        iStart = nLen;
        SendMessageW(m_hwndSubclassedEdit, EM_SETSEL, iStart, iStart);
    }

    SendMessageW(m_hwndSubclassedEdit, EM_REPLACESEL, TRUE, (LPARAM)pszVar);

    if (nCaretPos < 0)
    {
        iStart += (int)wcslen(pszVar);
    }
    else
    {
        iStart += nCaretPos;
    }

    SendMessageW(m_hwndSubclassedEdit, EM_SETSEL, iStart, iStart);
}
