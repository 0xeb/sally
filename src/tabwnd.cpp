// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"

#include "cfgdlg.h"
#include "menu.h"
#include "mainwnd.h"
#include "plugins.h"
#include "fileswnd.h"
#include "darkmode.h"
#include "tabwnd.h"
#include "salshlib.h"
#include "common/IFileSystem.h"

using namespace sally::tabs;

namespace
{

// The directory line's caption colours (stswnd.cpp), so the active tab joins its directory line.
COLORREF TabBackground(BOOL activeTab, BOOL activePanel, BOOL hot)
{
    if (DarkMode_ShouldUseDark())
    {
        if (activeTab)
            return activePanel ? RGB(64, 64, 70) : RGB(46, 46, 50);
        return hot ? RGB(52, 52, 56) : RGB(30, 30, 31);
    }
    if (activeTab)
    {
        if (Configuration.ShowPanelCaption)
            return GetCOLORREF(CurrentColors[activePanel ? ACTIVE_CAPTION_BK : INACTIVE_CAPTION_BK]);
        return GetSysColor(COLOR_WINDOW);
    }
    return hot ? GetSysColor(COLOR_3DHILIGHT) : GetSysColor(COLOR_BTNFACE);
}

COLORREF TabText(BOOL activeTab, BOOL activePanel, BOOL dormant)
{
    if (DarkMode_ShouldUseDark())
    {
        if (dormant)
            return RGB(140, 140, 140);
        return activeTab ? (activePanel ? RGB(235, 235, 235) : RGB(190, 190, 190)) : RGB(200, 200, 200);
    }
    if (dormant)
        return GetSysColor(COLOR_GRAYTEXT);
    if (activeTab && Configuration.ShowPanelCaption)
        return GetCOLORREF(CurrentColors[activePanel ? ACTIVE_CAPTION_FG : INACTIVE_CAPTION_FG]);
    return GetSysColor(COLOR_BTNTEXT);
}

void FillSolid(HDC dc, const RECT& r, COLORREF color)
{
    COLORREF old = SetBkColor(dc, color);
    ExtTextOutW(dc, 0, 0, ETO_OPAQUE, &r, NULL, 0, NULL);
    SetBkColor(dc, old);
}

void DrawCross(HDC dc, const RECT& r, COLORREF color)
{
    HPEN pen = CreatePen(PS_SOLID, 1, color);
    HGDIOBJ old = SelectObject(dc, pen);
    const int inset = (r.right - r.left) / 4;
    MoveToEx(dc, r.left + inset, r.top + inset, NULL);
    LineTo(dc, r.right - inset, r.bottom - inset);
    MoveToEx(dc, r.right - inset - 1, r.top + inset, NULL);
    LineTo(dc, r.left + inset - 1, r.bottom - inset);
    SelectObject(dc, old);
    DeleteObject(pen);
}

// A small padlock where a locked tab would show its close cross.
void DrawLock(HDC dc, const RECT& r, COLORREF color)
{
    const int w = r.right - r.left;
    const int h = r.bottom - r.top;
    RECT body = {r.left + w / 6, r.top + h / 2 - 1, r.right - w / 6, r.bottom - 1};
    FillSolid(dc, body, color);
    HPEN pen = CreatePen(PS_SOLID, (std::max)(1, w / 8), color);
    HGDIOBJ oldPen = SelectObject(dc, pen);
    HGDIOBJ oldBrush = SelectObject(dc, GetStockObject(NULL_BRUSH));
    const int left = r.left + w / 3;
    const int right = r.right - w / 3;
    MoveToEx(dc, left, body.top, NULL);
    LineTo(dc, left, r.top + h / 4);
    LineTo(dc, right - 1, r.top + h / 4);
    LineTo(dc, right - 1, body.top);
    SelectObject(dc, oldBrush);
    SelectObject(dc, oldPen);
    DeleteObject(pen);
}

int CloseSize() { return (std::max)(9, EnvFontCharHeight * 2 / 3); }
int Padding() { return (std::max)(6, EnvFontCharHeight / 2); }

// Posted to the strip when folders were dropped on it: they open after the drag-and-drop loop
// (possibly Sally's own, from a panel) has returned.
UINT TabDropMessage()
{
    static const UINT message = RegisterWindowMessageW(L"SallyPanelTabStripDrop");
    return message;
}

// Folders (or the folders of files) dragged from Explorer or a panel open in new tabs. The drop
// never moves anything: it offers a link or copy effect only so the source keeps its files.
class CTabStripDropTarget : public IDropTarget
{
public:
    CTabStripDropTarget(CTabWindow* strip) : Strip(strip) {}

    STDMETHODIMP QueryInterface(REFIID riid, void** ppv)
    {
        if (riid == IID_IUnknown || riid == IID_IDropTarget)
        {
            *ppv = static_cast<IDropTarget*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = NULL;
        return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() { return (ULONG)InterlockedIncrement(&Refs); }
    STDMETHODIMP_(ULONG) Release()
    {
        const LONG refs = InterlockedDecrement(&Refs);
        if (refs == 0)
            delete this;
        return (ULONG)refs;
    }

    STDMETHODIMP DragEnter(IDataObject* data, DWORD, POINTL, DWORD* effect)
    {
        FORMATETC format = {CF_HDROP, NULL, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
        // An archive or plugin file system dragged from a panel offers a fake temporary folder
        // (DROPFAKE) until it is dropped on a real target: nothing to open here.
        CanDrop = data != NULL && data->QueryGetData(&format) == S_OK && !IsFakeDataObject(data, NULL, NULL) &&
                  (MainWindow == NULL || !MainWindow->HasLockedUI()) && !Strip->FilesWindow->Tabs.IsFull() &&
                  !Strip->FilesWindow->SwitchingTab;
        *effect = Effect(*effect);
        return S_OK;
    }
    STDMETHODIMP DragOver(DWORD, POINTL, DWORD* effect)
    {
        *effect = Effect(*effect);
        return S_OK;
    }
    STDMETHODIMP DragLeave()
    {
        CanDrop = FALSE;
        return S_OK;
    }
    STDMETHODIMP Drop(IDataObject* data, DWORD, POINTL, DWORD* effect)
    {
        *effect = Effect(*effect);
        if (!CanDrop || *effect == DROPEFFECT_NONE)
            return S_OK;
        CanDrop = FALSE;
        FORMATETC format = {CF_HDROP, NULL, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
        STGMEDIUM medium = {};
        if (data->GetData(&format, &medium) != S_OK)
            return S_OK;
        HDROP drop = (HDROP)GlobalLock(medium.hGlobal);
        if (drop != NULL)
        {
            const UINT count = DragQueryFileW(drop, 0xFFFFFFFF, NULL, 0);
            for (UINT i = 0; i < count; i++)
            {
                const UINT length = DragQueryFileW(drop, i, NULL, 0);
                std::vector<wchar_t> buffer(length + 1);
                if (DragQueryFileW(drop, i, buffer.data(), length + 1) == 0)
                    continue;
                std::wstring path(buffer.data());
                const DWORD attributes = gFileSystem->GetFileAttributes(path.c_str());
                if (attributes == INVALID_FILE_ATTRIBUTES)
                    continue;
                if ((attributes & FILE_ATTRIBUTE_DIRECTORY) == 0)
                {
                    const std::size_t slash = path.find_last_of(L'\\');
                    if (slash == std::wstring::npos)
                        continue;
                    path.erase(slash == 2 && path[1] == L':' ? 3 : slash); // a file: its folder
                }
                bool known = false;
                for (const std::wstring& pending : Strip->PendingDrops)
                    known = known || StrICmpW(pending.c_str(), path.c_str()) == 0;
                if (!known)
                    Strip->PendingDrops.push_back(path);
            }
            GlobalUnlock(medium.hGlobal);
        }
        ReleaseStgMedium(&medium);
        if (!Strip->PendingDrops.empty() && Strip->HWindow != NULL)
            PostMessage(Strip->HWindow, TabDropMessage(), 0, 0);
        return S_OK;
    }

private:
    LONG Refs = 1;
    CTabWindow* Strip;
    BOOL CanDrop = FALSE;

    DWORD Effect(DWORD allowed) const
    {
        if (!CanDrop)
            return DROPEFFECT_NONE;
        if (allowed & DROPEFFECT_LINK)
            return DROPEFFECT_LINK;
        return (allowed & DROPEFFECT_COPY) ? DROPEFFECT_COPY : DROPEFFECT_NONE;
    }
};

} // namespace

CTabWindow::CTabWindow(CFilesWindow* filesWindow) : CWindow(ooStatic)
{
    CALL_STACK_MESSAGE_NONE
    FilesWindow = filesWindow;
    HotIndex = PanelTabSet::npos;
    HotClose = FALSE;
    TrackingLeave = FALSE;
    LastEmptyClick = 0;
    LastEmptyPoint.x = LastEmptyPoint.y = -100;
    WheelDelta = 0;
    DragIndex = PanelTabSet::npos;
    DragId = 0;
    DragStart.x = DragStart.y = 0;
    Dragging = FALSE;
    DropGap = PanelTabSet::npos;
    DropTarget = NULL;
}

CTabWindow::~CTabWindow()
{
    CALL_STACK_MESSAGE1("CTabWindow::~CTabWindow()");
}

void CTabWindow::DestroyWindow()
{
    if (HWindow != NULL)
        ::DestroyWindow(HWindow);
}

int CTabWindow::GetNeededHeight()
{
    CALL_STACK_MESSAGE_NONE
    return 4 + EnvFontCharHeight + 6;
}

void CTabWindow::Repaint()
{
    if (HWindow != NULL)
    {
        InvalidateRect(HWindow, NULL, FALSE);
        UpdateWindow(HWindow);
    }
}

void CTabWindow::Measure(HDC hDC, int width)
{
    const PanelTabSet& tabs = FilesWindow->Tabs;
    HGDIOBJ oldFont = SelectObject(hDC, EnvFont);
    std::vector<int> preferred;
    preferred.reserve(tabs.Count());
    for (std::size_t i = 0; i < tabs.Count(); i++)
    {
        const std::wstring title = DisplayTitle(tabs.At(i));
        SIZE s = {};
        GetTextExtentPoint32W(hDC, title.c_str(), (int)title.size(), &s);
        preferred.push_back(s.cx + 3 * Padding() + CloseSize());
    }
    SelectObject(hDC, oldFont);
    StripMetrics metrics;
    metrics.Available = width;
    metrics.MinWidth = EnvFontCharHeight * 4;
    metrics.MaxWidth = EnvFontCharHeight * 16;
    metrics.OverflowWidth = EnvFontCharHeight + 8;
    Layout = LayOutStrip(preferred, tabs.Active(), metrics);
}

RECT CTabWindow::CloseRect(std::size_t index)
{
    RECT r = {0, 0, 0, 0};
    if (index >= Layout.Slots.size() || !Layout.Slots[index].Visible)
        return r;
    RECT client;
    GetClientRect(HWindow, &client);
    const int size = CloseSize();
    r.right = Layout.Slots[index].Right - Padding() / 2 - 1;
    r.left = r.right - size;
    r.top = (client.bottom + 2 - size) / 2;
    r.bottom = r.top + size;
    return r;
}

void CTabWindow::Paint(HDC hDC)
{
    CALL_STACK_MESSAGE1("CTabWindow::Paint()");
    RECT client;
    GetClientRect(HWindow, &client);
    if (client.right <= 0 || client.bottom <= 0)
        return;
    HDC dc = CreateCompatibleDC(hDC);
    HBITMAP bitmap = CreateCompatibleBitmap(hDC, client.right, client.bottom);
    HGDIOBJ oldBitmap = SelectObject(dc, bitmap);

    const BOOL dark = DarkMode_ShouldUseDark();
    FillSolid(dc, client, dark ? RGB(24, 24, 25) : GetSysColor(COLOR_BTNFACE));
    Measure(dc, client.right);

    const PanelTabSet& tabs = FilesWindow->Tabs;
    const BOOL activePanel = MainWindow != NULL && MainWindow->GetActivePanel() == FilesWindow && MainWindow->CaptionIsActive;
    HGDIOBJ oldFont = SelectObject(dc, EnvFont);
    SetBkMode(dc, TRANSPARENT);
    const COLORREF edge = dark ? RGB(62, 62, 66) : GetSysColor(COLOR_3DSHADOW);
    for (std::size_t i = 0; i < Layout.Slots.size(); i++)
    {
        const auto& slot = Layout.Slots[i];
        if (!slot.Visible)
            continue;
        const BOOL active = i == tabs.Active();
        const BOOL hot = i == HotIndex;
        RECT tab = {slot.Left, active ? 1 : 3, slot.Right - 1, client.bottom};
        FillSolid(dc, tab, TabBackground(active, activePanel, hot));
        if (active) // an accent on top: highlighted in the active panel, grey in the other
        {
            RECT accent = {tab.left, tab.top, tab.right, tab.top + 2};
            FillSolid(dc, accent, activePanel ? GetSysColor(COLOR_HIGHLIGHT) : (dark ? RGB(90, 90, 96) : GetSysColor(COLOR_GRAYTEXT)));
        }
        RECT side = {tab.right, tab.top + 2, tab.right + 1, tab.bottom - 1};
        FillSolid(dc, side, edge);

        const std::wstring title = DisplayTitle(tabs.At(i));
        RECT text = {tab.left + Padding(), tab.top, tab.right - Padding() - CloseSize(), tab.bottom};
        SetTextColor(dc, TabText(active, activePanel, tabs.At(i).Dormant));
        DrawTextW(dc, title.c_str(), (int)title.size(), &text, DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);

        if (tabs.At(i).Locked)
            DrawLock(dc, CloseRect(i), TabText(active, activePanel, tabs.At(i).Dormant));
        else if (tabs.Count() > 1 && (hot || active))
        {
            RECT cross = CloseRect(i);
            if (hot && HotClose)
                FillSolid(dc, cross, dark ? RGB(80, 80, 86) : GetSysColor(COLOR_3DSHADOW));
            DrawCross(dc, cross, TabText(active, activePanel, FALSE));
        }
    }
    if (Dragging && DropGap != PanelTabSet::npos)
    {
        // where the dragged tab would go: before tab DropGap, or after the last visible tab
        int x = -1;
        if (DropGap < Layout.Slots.size() && Layout.Slots[DropGap].Visible)
            x = Layout.Slots[DropGap].Left;
        else
        {
            for (const auto& slot : Layout.Slots)
                if (slot.Visible)
                    x = slot.Right - 1;
        }
        if (x >= 0)
        {
            RECT mark = {(std::max)(0, x - 1), 1, x + 2, client.bottom};
            FillSolid(dc, mark, GetSysColor(COLOR_HIGHLIGHT));
        }
    }
    if (Layout.Overflow)
    {
        RECT button = {Layout.OverflowLeft, 3, client.right, client.bottom};
        FillSolid(dc, button, TabBackground(FALSE, activePanel, FALSE));
        SetTextColor(dc, TabText(FALSE, activePanel, FALSE));
        // a small down triangle, escaped: the sally target is not compiled with /utf-8
        DrawTextW(dc, L"\u25BE", 1, &button, DT_SINGLELINE | DT_VCENTER | DT_CENTER | DT_NOPREFIX);
    }
    // the line the tabs stand on, open under the active tab
    RECT base = {0, client.bottom - 1, client.right, client.bottom};
    FillSolid(dc, base, edge);
    if (tabs.Active() < Layout.Slots.size() && Layout.Slots[tabs.Active()].Visible)
    {
        RECT open = {Layout.Slots[tabs.Active()].Left, client.bottom - 1, Layout.Slots[tabs.Active()].Right - 1, client.bottom};
        FillSolid(dc, open, TabBackground(TRUE, activePanel, FALSE));
    }

    SelectObject(dc, oldFont);
    BitBlt(hDC, 0, 0, client.right, client.bottom, dc, 0, 0, SRCCOPY);
    SelectObject(dc, oldBitmap);
    DeleteObject(bitmap);
    DeleteDC(dc);
}

void CTabWindow::ShowTabMenu(std::size_t index, POINT screen)
{
    CALL_STACK_MESSAGE1("CTabWindow::ShowTabMenu()");
    if (FilesWindow->SwitchingTab)
        return;
    PanelTabSet& tabs = FilesWindow->Tabs;
    const BOOL several = tabs.Count() > 1;
    BOOL closableOthers = FALSE; // Close Other Tabs has something to close
    for (std::size_t i = 0; i < tabs.Count(); i++)
        closableOthers = closableOthers || (i != index && !tabs.At(i).Locked);
    CMenuPopup menu;
    MENU_ITEM_INFO separator;
    separator.Mask = MENU_MASK_TYPE;
    separator.Type = MENU_TYPE_SEPARATOR;
    MENU_ITEM_INFO mii;
    mii.Mask = MENU_MASK_TYPE | MENU_MASK_ID | MENU_MASK_STATE | MENU_MASK_STRING;
    mii.Type = MENU_TYPE_STRING;
    auto add = [&](UINT text, DWORD id, DWORD state) {
        mii.String = LoadStrW(text);
        mii.ID = id;
        mii.State = state;
        menu.InsertItem(0xffffffff, TRUE, &mii);
    };
    add(IDS_MENU_TAB_NEW, CM_TAB_NEW, 0);
    add(IDS_MENU_TAB_DUPLICATE, CM_TAB_DUPLICATE, 0);
    menu.InsertItem(0xffffffff, TRUE, &separator);
    const BOOL locked = index < tabs.Count() && tabs.At(index).Locked;
    add(IDS_MENU_TAB_CLOSE, CM_TAB_CLOSE, several && !locked ? 0 : MENU_STATE_GRAYED);
    add(IDS_MENU_TAB_CLOSEOTHERS, CM_TAB_CLOSEOTHERS, closableOthers ? 0 : MENU_STATE_GRAYED);
    add(IDS_MENU_TAB_REOPEN, CM_TAB_REOPEN, tabs.CanReopen() ? 0 : MENU_STATE_GRAYED);
    menu.InsertItem(0xffffffff, TRUE, &separator);
    add(IDS_MENU_TAB_RENAME, CM_TAB_RENAME, 0);
    add(IDS_MENU_TAB_LOCK, CM_TAB_LOCK, locked ? MENU_STATE_CHECKED : 0);
    add(IDS_MENU_TAB_MOVELEFT, CM_TAB_MOVELEFT, index > 0 ? 0 : MENU_STATE_GRAYED);
    add(IDS_MENU_TAB_MOVERIGHT, CM_TAB_MOVERIGHT, index + 1 < tabs.Count() ? 0 : MENU_STATE_GRAYED);
    menu.InsertItem(0xffffffff, TRUE, &separator);
    add(IDS_TAB_COPYPATH, 1, 0);
    menu.InsertItem(0xffffffff, TRUE, &separator);
    add(IDS_MENU_TAB_ALWAYSSHOW, CM_TAB_TOGGLEBAR, Configuration.ShowTabBar ? MENU_STATE_CHECKED : 0);

    const int command = menu.Track(MENU_TRACK_RETURNCMD | MENU_TRACK_RIGHTBUTTON, screen.x, screen.y, HWindow, NULL);
    if (command == 0)
        return;
    if (command == 1)
    {
        if (index < tabs.Count())
        {
            if (index == tabs.Active() && !FilesWindow->SwitchingTab)
                FilesWindow->CaptureActiveTab();
            const std::wstring path = FullLocation(tabs.At(index));
            CopyTextToClipboardW(path.c_str(), -1, TRUE, FilesWindow->HWindow);
        }
        return;
    }
    // the menu acts on the clicked tab: make it the active one first, except for New Tab
    if (command == CM_TAB_CLOSE)
    {
        FilesWindow->CloseTab(index);
        return;
    }
    if (index < tabs.Count())
    {
        switch (command)
        {
        case CM_TAB_RENAME:
            FilesWindow->RenameTab(index);
            return;
        case CM_TAB_LOCK:
            FilesWindow->ToggleTabLock(index);
            return;
        case CM_TAB_MOVELEFT:
            if (index > 0)
                FilesWindow->MoveTab(index, index - 1);
            return;
        case CM_TAB_MOVERIGHT:
            if (index + 1 < tabs.Count())
                FilesWindow->MoveTab(index, index + 1);
            return;
        }
    }
    if (command == CM_TAB_CLOSEOTHERS || command == CM_TAB_DUPLICATE)
    {
        if (index != tabs.Active() && !FilesWindow->SwitchToTab(index))
            return;
    }
    FilesWindow->TabCommand(command);
}

const wchar_t* CTabWindow::WindowClassName()
{
    static const wchar_t* const name = L"SallyPanelTabStrip";
    static BOOL registered = FALSE;
    if (!registered)
    {
        // as CWINDOW_CLASSNAME2: double clicks, arrow cursor, no redraw on resize
        registered = CWindow::RegisterUniversalClass(CS_DBLCLKS, 0, 0, NULL, LoadCursorW(NULL, (LPCWSTR)IDC_ARROW),
                                                     (HBRUSH)(COLOR_WINDOW + 1), NULL, name, NULL);
        if (!registered && GetLastError() == ERROR_CLASS_ALREADY_EXISTS)
            registered = TRUE;
    }
    return registered ? name : CWINDOW_CLASSNAME2;
}

void CTabWindow::ResetPointerState()
{
    HotIndex = PanelTabSet::npos;
    HotClose = FALSE;
    TrackingLeave = FALSE;
    LastEmptyClick = 0;
    WheelDelta = 0;
    DragIndex = PanelTabSet::npos;
    Dragging = FALSE;
    DropGap = PanelTabSet::npos;
    Layout = StripLayout(); // measured again at the next paint
}

void CTabWindow::CancelDrag()
{
    if (DragIndex == PanelTabSet::npos)
        return;
    EndDrag();
    if (HWindow != NULL && GetCapture() == HWindow)
        ReleaseCapture();
}

void CTabWindow::EndDrag()
{
    const BOOL wasDragging = Dragging;
    DragIndex = PanelTabSet::npos;
    Dragging = FALSE;
    DropGap = PanelTabSet::npos;
    if (wasDragging && HWindow != NULL)
        InvalidateRect(HWindow, NULL, FALSE);
}

void CTabWindow::OpenPendingDrops()
{
    CALL_STACK_MESSAGE1("CTabWindow::OpenPendingDrops()");
    if (FilesWindow->SwitchingTab)
    {
        PostMessage(HWindow, TabDropMessage(), 0, 0); // after the path change that is under way
        return;
    }
    std::vector<std::wstring> paths;
    paths.swap(PendingDrops);
    if (paths.empty() || (MainWindow != NULL && MainWindow->HasLockedUI()))
        return;
    if (MainWindow != NULL && MainWindow->GetActivePanel() != FilesWindow)
        MainWindow->ChangePanel(); // the folders open in the panel they were dropped on
    for (const std::wstring& path : paths)
    {
        if (FilesWindow->Tabs.IsFull())
            break;
        FilesWindow->OpenPathInNewTab(path.c_str());
    }
}

void CTabWindow::ShowOverflowMenu()
{
    CALL_STACK_MESSAGE1("CTabWindow::ShowOverflowMenu()");
    PanelTabSet& tabs = FilesWindow->Tabs;
    CMenuPopup menu;
    MENU_ITEM_INFO mii;
    mii.Mask = MENU_MASK_TYPE | MENU_MASK_ID | MENU_MASK_STATE | MENU_MASK_STRING;
    mii.Type = MENU_TYPE_STRING;
    std::vector<std::wstring> titles;
    titles.reserve(tabs.Count());
    for (std::size_t i = 0; i < tabs.Count(); i++)
    {
        std::wstring title = DisplayTitle(tabs.At(i));
        std::wstring escaped;
        for (wchar_t ch : title) // '&' would become an access key
        {
            if (ch == L'&')
                escaped += L'&';
            escaped += ch;
        }
        titles.push_back(escaped);
    }
    for (std::size_t i = 0; i < tabs.Count(); i++)
    {
        mii.String = titles[i].data();
        mii.ID = (DWORD)(i + 1);
        mii.State = i == tabs.Active() ? MENU_STATE_CHECKED : 0;
        menu.InsertItem(0xffffffff, TRUE, &mii);
    }
    RECT client;
    GetClientRect(HWindow, &client);
    POINT pt = {Layout.OverflowLeft, client.bottom};
    ClientToScreen(HWindow, &pt);
    const int command = menu.Track(MENU_TRACK_RETURNCMD, pt.x, pt.y, HWindow, NULL);
    if (command > 0)
        FilesWindow->SwitchToTab((std::size_t)(command - 1));
}

LRESULT
CTabWindow::WindowProc(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    CALL_STACK_MESSAGE4("CTabWindow::WindowProc(0x%X, 0x%IX, 0x%IX)", uMsg, wParam, lParam);
    if (uMsg == TabDropMessage() && uMsg != 0)
    {
        OpenPendingDrops();
        return 0;
    }
    switch (uMsg)
    {
    case WM_CREATE:
    {
        CTabStripDropTarget* target = new CTabStripDropTarget(this);
        if (RegisterDragDrop(HWindow, target) == S_OK)
            DropTarget = target;
        else
            target->Release();
        break;
    }

    case WM_CANCELMODE: // Escape in a modal loop, a dialog opening: no drag
        CancelDrag();
        break;

    case WM_CAPTURECHANGED:
        if ((HWND)lParam != HWindow)
            EndDrag(); // Escape, another window took the mouse, or the button came up
        break;

    case WM_LBUTTONUP:
    {
        if (DragIndex != PanelTabSet::npos)
        {
            const std::size_t from = FilesWindow->Tabs.IndexOf(DragId);
            const std::size_t gap = DropGap;
            const BOOL drop = Dragging;
            EndDrag();
            if (GetCapture() == HWindow)
                ReleaseCapture();
            if (drop && gap != PanelTabSet::npos && from != PanelTabSet::npos)
                FilesWindow->MoveTabToGap(from, gap);
        }
        return 0;
    }

    case WM_ERASEBKGND:
        return TRUE;

    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC dc = HANDLES(BeginPaint(HWindow, &ps));
        Paint(dc);
        HANDLES(EndPaint(HWindow, &ps));
        return 0;
    }

    case WM_SIZE:
        InvalidateRect(HWindow, NULL, FALSE);
        break;

    case WM_MOUSEMOVE:
    {
        const int x = (short)LOWORD(lParam);
        const int y = (short)HIWORD(lParam);
        if (DragIndex != PanelTabSet::npos && GetCapture() == HWindow)
        {
            // reorder: past the drag threshold the gap the tab would go to is shown
            if (!Dragging && (abs(x - DragStart.x) > GetSystemMetrics(SM_CXDRAG) ||
                              abs(y - DragStart.y) > GetSystemMetrics(SM_CYDRAG)))
                Dragging = TRUE;
            if (Dragging)
            {
                const std::size_t gap = DropGapAt(Layout, x);
                if (gap != DropGap)
                {
                    DropGap = gap;
                    InvalidateRect(HWindow, NULL, FALSE);
                }
            }
            break;
        }
        const StripHit hit = HitTestStrip(Layout, x);
        std::size_t hot = hit.Kind == StripHitKind::Tab ? hit.Index : PanelTabSet::npos;
        BOOL hotClose = FALSE;
        if (hot >= FilesWindow->Tabs.Count())
            hot = PanelTabSet::npos;
        if (hot != PanelTabSet::npos && FilesWindow->Tabs.Count() > 1 && !FilesWindow->Tabs.At(hot).Locked)
        {
            const RECT cross = CloseRect(hot);
            POINT pt = {x, y};
            hotClose = PtInRect(&cross, pt);
        }
        if (hot != HotIndex || hotClose != HotClose)
        {
            HotIndex = hot;
            HotClose = hotClose;
            SetCurrentToolTip(hot != PanelTabSet::npos ? HWindow : NULL, (DWORD)(hot + 1));
            InvalidateRect(HWindow, NULL, FALSE);
        }
        if (!TrackingLeave)
        {
            TRACKMOUSEEVENT tme = {sizeof(tme)};
            tme.dwFlags = TME_LEAVE;
            tme.hwndTrack = HWindow;
            TrackingLeave = TrackMouseEvent(&tme);
        }
        break;
    }

    case WM_MOUSELEAVE:
    {
        TrackingLeave = FALSE;
        if (HotIndex != PanelTabSet::npos || HotClose)
        {
            HotIndex = PanelTabSet::npos;
            HotClose = FALSE;
            SetCurrentToolTip(NULL, 0);
            InvalidateRect(HWindow, NULL, FALSE);
        }
        break;
    }

    case WM_USER_TTGETTEXTW:
    {
        const std::size_t index = (std::size_t)wParam - 1;
        wchar_t* text = (wchar_t*)lParam;
        text[0] = 0;
        if (index < FilesWindow->Tabs.Count())
        {
            if (index == FilesWindow->Tabs.Active() && !FilesWindow->SwitchingTab)
                FilesWindow->CaptureActiveTab();
            const std::wstring path = FullLocation(FilesWindow->Tabs.At(index));
            wcsncpy_s(text, TOOLTIP_TEXT_MAX, path.c_str(), _TRUNCATE); // the tooltip's fixed buffer
        }
        return 0;
    }

    case WM_LBUTTONDOWN:
    case WM_LBUTTONDBLCLK:
    {
        if (MainWindow != NULL && MainWindow->HasLockedUI())
            return 0; // a running script owns the panel (Automation's LockMainWindow)
        if (MainWindow != NULL && MainWindow->GetActivePanel() != FilesWindow)
            MainWindow->ChangePanel(); // clicking a tab works in the panel it belongs to
        const int x = (short)LOWORD(lParam);
        const int y = (short)HIWORD(lParam);
        const StripHit hit = HitTestStrip(Layout, x);
        if (hit.Kind == StripHitKind::Tab && hit.Index < FilesWindow->Tabs.Count())
        {
            const RECT cross = CloseRect(hit.Index);
            POINT pt = {x, y};
            if (FilesWindow->Tabs.Count() > 1 && !FilesWindow->Tabs.At(hit.Index).Locked && PtInRect(&cross, pt))
                FilesWindow->CloseTab(hit.Index);
            else
            {
                const std::uint32_t id = FilesWindow->Tabs.At(hit.Index).Id;
                if (hit.Index != FilesWindow->Tabs.Active())
                    FilesWindow->SwitchToTab(hit.Index);
                // the button still down after the switch: a drag may reorder the tab
                const std::size_t index = FilesWindow->Tabs.IndexOf(id);
                if (index != PanelTabSet::npos && FilesWindow->Tabs.Count() > 1 &&
                    (GetKeyState(VK_LBUTTON) & 0x8000) != 0)
                {
                    DragIndex = index;
                    DragId = id;
                    DragStart = pt;
                    Dragging = FALSE;
                    DropGap = PanelTabSet::npos;
                    SetCapture(HWindow);
                }
            }
        }
        else if (hit.Kind == StripHitKind::Overflow)
            ShowOverflowMenu();
        else
        {
            // a double click on empty strip space opens a new tab
            const DWORD now = GetMessageTime();
            const BOOL doubleClick = uMsg == WM_LBUTTONDBLCLK ||
                                     (now - LastEmptyClick <= GetDoubleClickTime() &&
                                      abs(x - LastEmptyPoint.x) <= GetSystemMetrics(SM_CXDOUBLECLK) &&
                                      abs(y - LastEmptyPoint.y) <= GetSystemMetrics(SM_CYDOUBLECLK));
            if (doubleClick)
            {
                LastEmptyClick = 0;
                FilesWindow->NewTab(FALSE);
            }
            else
            {
                LastEmptyClick = now;
                LastEmptyPoint.x = x;
                LastEmptyPoint.y = y;
            }
        }
        return 0;
    }

    case WM_MBUTTONUP:
    {
        if (MainWindow != NULL && MainWindow->HasLockedUI())
            return 0;
        const StripHit hit = HitTestStrip(Layout, (short)LOWORD(lParam));
        if (hit.Kind == StripHitKind::Tab)
            FilesWindow->CloseTab(hit.Index);
        return 0;
    }

    case WM_RBUTTONUP:
    {
        if (MainWindow != NULL && MainWindow->HasLockedUI())
            return 0;
        const StripHit hit = HitTestStrip(Layout, (short)LOWORD(lParam));
        const std::size_t index = hit.Kind == StripHitKind::Tab ? hit.Index : FilesWindow->Tabs.Active();
        POINT pt = {(short)LOWORD(lParam), (short)HIWORD(lParam)};
        ClientToScreen(HWindow, &pt);
        ShowTabMenu(index, pt);
        return 0;
    }

    case WM_CONTEXTMENU:
        return 0; // handled in WM_RBUTTONUP (no panel context menu for the strip)

    case WM_MOUSEWHEEL:      // delivered when Windows scrolls inactive windows
    case WM_USER_MOUSEWHEEL: // Sally's wheel hook sends the window under the cursor this
    {
        if ((MainWindow != NULL && MainWindow->HasLockedUI()) || FilesWindow->Tabs.Count() < 2)
            return 0;
        // One tab per notch: high-resolution wheels and touchpads send smaller steps.
        const short delta = (short)HIWORD(wParam);
        if ((delta > 0) != (WheelDelta > 0))
            WheelDelta = 0; // the direction changed
        WheelDelta += delta;
        while (WheelDelta >= WHEEL_DELTA || WheelDelta <= -WHEEL_DELTA)
        {
            const BOOL back = WheelDelta > 0;
            WheelDelta += back ? -WHEEL_DELTA : WHEEL_DELTA;
            if (!FilesWindow->SwitchToTab(back ? FilesWindow->Tabs.PrevIndex() : FilesWindow->Tabs.NextIndex()))
            {
                WheelDelta = 0;
                break;
            }
        }
        return 0;
    }

    case WM_DESTROY:
        if (DropTarget != NULL)
        {
            RevokeDragDrop(HWindow);
            DropTarget->Release();
            DropTarget = NULL;
        }
        PendingDrops.clear();
        ResetPointerState(); // the strip can be recreated (Tab Bar off, 1 tab, then 2 again)
        break;
    }

    return CWindow::WindowProc(uMsg, wParam, lParam);
}
