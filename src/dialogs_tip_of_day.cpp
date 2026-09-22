// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"

#include <lm.h>

#include "ui/IPrompter.h"
#include "common/IFileEnumerator.h"
#include "common/TipOfDayResource.h"
#include "common/unicode/helpers.h"
#include "common/widepath.h"
#include "common/fsutil.h" // GetRootPathW - the FREE function, not the CPluginFSInterfaceAbstract method
#include "mainwnd.h"
#include "plugins.h"
#include "fileswnd.h"
#include "cfgdlg.h"
#include "dialogs.h"
#include "menu.h"
#include "gui.h"
#include "drivelst.h"
#include "shiconov.h"
#include "darkmode.h"

// this build doesn't define _UNICODE, so <commctrl.h>'s ListView_SetItemText
// macro resolves to the A form only; mirrors the same local macro already used by
// dialogs_viewer_editor_masks.cpp and the dbviewer/pictview plugins for the identical need.
#define ListView_SetItemTextW(hwndLV, i, iSubItem_, pszText_) \
    {                                                         \
        LV_ITEMW _ms_lvi;                                     \
        _ms_lvi.iSubItem = iSubItem_;                         \
        _ms_lvi.pszText = pszText_;                           \
        SNDMSG((hwndLV), LVM_SETITEMTEXTW, (WPARAM)(i), (LPARAM)(LV_ITEM*)&_ms_lvi); \
    }

/*
//****************************************************************************
//
// CTipOfTheDayWindow and CTipOfTheDayDialog
//
//

CTipOfTheDayWindow::CTipOfTheDayWindow()
{
  CALL_STACK_MESSAGE1("CTipOfTheDayWindow::CTipOfTheDayWindow()");

  LOGFONT srcLF;
  HFONT hSystemFont = (HFONT)HANDLES(GetStockObject(DEFAULT_GUI_FONT));
  GetObject(hSystemFont, sizeof(srcLF), &srcLF);

  LOGFONT lf;

  lf.lfHeight = (int)(srcLF.lfHeight * 1.9);
  lf.lfWidth = 0;
  lf.lfEscapement = 0;
  lf.lfOrientation = 0;
  lf.lfWeight = FW_BOLD;
  lf.lfItalic = 0;
  lf.lfUnderline = 0;
  lf.lfStrikeOut = 0;
  lf.lfCharSet = UserCharset;
  lf.lfOutPrecision = OUT_DEFAULT_PRECIS;
  lf.lfClipPrecision = CLIP_DEFAULT_PRECIS;
  lf.lfQuality = DEFAULT_QUALITY;
  lf.lfPitchAndFamily = VARIABLE_PITCH | FF_SWISS;
  lstrcpy(lf.lfFaceName, "Times New Roman");
  HHeadingFont = HANDLES(CreateFontIndirect(&lf));

  lf.lfHeight = (int)(srcLF.lfHeight * 1.2);
  lf.lfWeight = FW_NORMAL;
  lstrcpy(lf.lfFaceName, "Arial");
  HBodyFont = HANDLES(CreateFontIndirect(&lf));

}

CTipOfTheDayWindow::~CTipOfTheDayWindow()
{
  CALL_STACK_MESSAGE1("CTipOfTheDayWindow::~CTipOfTheDayWindow()");
  HANDLES(DeleteObject(HHeadingFont));
  HANDLES(DeleteObject(HBodyFont));
}

void
CTipOfTheDayWindow::PaintBodyText(HDC hDC)
{
  CALL_STACK_MESSAGE1("CTipOfTheDayWindow::PaintBodyText()");
  BOOL releaseDC = FALSE;
  if (hDC == NULL)
  {
    hDC = HANDLES(GetDC(HWindow));
    releaseDC = TRUE;
  }
  RECT clientR;
  GetClientRect(HWindow, &clientR);
  int leftWidth = clientR.right / 7;
  int topWidth = clientR.right / 8;

  RECT r;
  r = clientR;
  r.top = topWidth + 1;
  r.left = leftWidth;
  r.right--;
  r.bottom--;

  InflateRect(&r, -leftWidth / 8, -leftWidth / 8);

  FillRect(hDC, &r, (HBRUSH)(COLOR_WINDOW + 1));

  int tipIndex = Configuration.LastTipOfTheDay;
  if (tipIndex >= 0 && tipIndex < Parent->Tips.Count)
  {
    HFONT hOldFont = (HFONT)SelectObject(hDC, HBodyFont);
    int oldBkMode = SetBkMode(hDC, TRANSPARENT);
    COLORREF oldTextColor = SetTextColor(hDC, GetSysColor(COLOR_WINDOWTEXT));
    char *line = (char *)Parent->Tips[tipIndex];
    DrawText(hDC, line, -1, &r, DT_LEFT | DT_NOPREFIX | DT_WORDBREAK);
    SetTextColor(hDC, oldTextColor);
    SetBkMode(hDC, oldBkMode);
    SelectObject(hDC, hOldFont);
  }
  if (releaseDC)
    HANDLES(ReleaseDC(HWindow, hDC));
}

LRESULT
CTipOfTheDayWindow::WindowProc(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
  CALL_STACK_MESSAGE4("CTipOfTheDayWindow::WindowProc(0x%X, 0x%IX, 0x%IX)", uMsg, wParam, lParam);
  switch (uMsg)
  {
    case WM_ERASEBKGND:
    {
      return TRUE;
    }

    case WM_PAINT:
    {
      PAINTSTRUCT ps;
      HDC hDC = HANDLES(BeginPaint(HWindow, &ps));
      RECT clientR;
      GetClientRect(HWindow, &clientR);
      RECT r;
      int leftWidth = clientR.right / 7;
      int topWidth = clientR.right / 8;
      // border
      HPEN hOldPen = (HPEN)SelectObject(hDC, BtnShadowPen);
      MoveToEx(hDC, 0, clientR.bottom - 1, NULL);
      LineTo(hDC, 0, 0);
      LineTo(hDC, clientR.right - 1, 0);
      SelectObject(hDC, BtnHilightPen);
      MoveToEx(hDC, clientR.right - 1, 0, NULL);
      LineTo(hDC, clientR.right - 1, clientR.bottom - 1);
      LineTo(hDC, -1, clientR.bottom - 1);

      // gray stripe on the left
      r = clientR;
      r.left++;
      r.right = leftWidth;
      r.top++;
      r.bottom--;
      FillRect(hDC, &r, (HBRUSH)(COLOR_BTNSHADOW + 1));
      // place the icon stolen from Microsoft here - why does
      // everyone complain when they have such nice icons? ;-)
      DrawIcon(hDC, r.left + (r.right - r.left - 32) / 2, 16,
               HANDLES(LoadIcon(HInstance, MAKEINTRESOURCE(IDI_TIPOFTHEDAY))));

      // white stripe at the top
      r = clientR;
      r.top++;
      r.left = leftWidth;
      r.bottom = topWidth;
      r.right--;
      FillRect(hDC, &r, (HBRUSH)(COLOR_WINDOW + 1));
      r.top += leftWidth / 5;
      r.left += leftWidth / 8;
      HFONT hOldFont = (HFONT)SelectObject(hDC, HHeadingFont);
      int oldBkMode = SetBkMode(hDC, TRANSPARENT);
      DrawText(hDC, LoadStr(IDS_TOD_DIDYOUKNOW), -1, &r, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

      // separating gray line
      SelectObject(hDC, BtnShadowPen);
      MoveToEx(hDC, leftWidth, topWidth, NULL);
      LineTo(hDC, clientR.right - 1, topWidth);
      // white stripe at the bottom
      r = clientR;
      r.top = topWidth + 1;
      r.left = leftWidth;
      r.right--;
      r.bottom--;
      FillRect(hDC, &r, (HBRUSH)(COLOR_WINDOW + 1));

      PaintBodyText(hDC);

      SetBkMode(hDC, oldBkMode);
      SelectObject(hDC, hOldFont);
      SelectObject(hDC, hOldPen);
      HANDLES(EndPaint(HWindow, &ps));
      return 0;
    }
  }
  return CWindow::WindowProc(uMsg, wParam, lParam);
}

CTipOfTheDayDialog::CTipOfTheDayDialog(BOOL quiet)
 : CCommonDialog(HLanguage, IDD_TIPOFTHEDAY, NULL),
   Tips(200, 100)
{
  CALL_STACK_MESSAGE2("CTipOfTheDayDialog::CTipOfTheDayDialog(%d)", quiet);
  TipWindow.Parent = this;
  LoadTips(quiet);
}

CTipOfTheDayDialog::~CTipOfTheDayDialog()
{
  CALL_STACK_MESSAGE1("CTipOfTheDayDialog::~CTipOfTheDayDialog()");
  FreeTips();
}

BOOL
CTipOfTheDayDialog::LoadTips(BOOL quiet)
{
  CALL_STACK_MESSAGE2("CTipOfTheDayDialog::LoadTips(%d)", quiet);
  std::wstring fileNameW;
  HANDLE hFile = sally::tip_of_day::OpenTipsFileForReadW(
      HInstance, fileNameW,
      [](const wchar_t* path)
      {
          return gFileSystem->CreateFile(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                         NULL, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, NULL);
      });
  if (hFile == INVALID_HANDLE_VALUE)
  {
    if (!quiet)
    {
      std::wstring msg = FormatStrW(LoadStrW(IDS_FILEREADERROR), fileNameW.c_str());
      gPrompter->ShowError(LoadStrW(IDS_ERRORTITLE), msg.c_str());
    }
    return FALSE;
  }

  uint64_t size64 = 0;
  FileResult sizeResult = gFileSystem->GetHandleFileSize(hFile, &size64);
  if (!sizeResult.success || size64 == 0 || size64 > MAXDWORD)
  {
    if (!quiet)
    {
      std::wstring msg = FormatStrW(LoadStrW(IDS_FILEREADERROR), fileNameW.c_str());
      gPrompter->ShowError(LoadStrW(IDS_ERRORTITLE), msg.c_str());
    }
    gFileSystem->CloseFileHandle(hFile);
    return FALSE;
  }

  BYTE *data = (BYTE*)malloc(size);
  if (data == NULL)
  {
    TRACE_E(LOW_MEMORY);
    gFileSystem->CloseFileHandle(hFile);
    return FALSE;
  }

  DWORD size = (DWORD)size64;
  DWORD read = 0;
  if (!gFileSystem->ReadFromHandle(hFile, data, size, &read).success || read != size)
  {
    if (!quiet)
    {
      std::wstring msg = FormatStrW(LoadStrW(IDS_FILEREADERROR), fileNameW.c_str());
      gPrompter->ShowError(LoadStrW(IDS_ERRORTITLE), msg.c_str());
    }
    free(data);
    gFileSystem->CloseFileHandle(hFile);
    return FALSE;
  }


  BYTE *lineStart = data;
  DWORD count = 0;
  while (count < size)
  {
    BYTE *lineEnd = lineStart;
    while (*lineEnd != '\r' && *lineEnd != '\n' && count < size)
    {
      lineEnd++;
      count++;
    }
    int lineLen = lineEnd - lineStart;
    if (lineLen > 0)
    {
      BYTE *line = (BYTE*)malloc(lineLen + 1);
      if (line == NULL)
      {
        TRACE_E(LOW_MEMORY);
        free(data);
        gFileSystem->CloseFileHandle(hFile);
        return FALSE;
      }
      memmove(line, lineStart, lineLen);
      *(line + lineLen) = 0;
      Tips.Add((DWORD)line);
      if (!Tips.IsGood())
      {
        Tips.ResetState();
        free(line);
        free(data);
        gFileSystem->CloseFileHandle(hFile);
        return FALSE;
      }
    }
    while ((*lineEnd == '\r' || *lineEnd == '\n') && count < size)
    {
      lineEnd++;
      count++;
    }
    lineStart = lineEnd;
  }

  free(data);

  gFileSystem->CloseFileHandle(hFile);

  return TRUE;
}

void
CTipOfTheDayDialog::FreeTips()
{
  CALL_STACK_MESSAGE1("CTipOfTheDayDialog::FreeTips()");
  int count = Tips.Count;
  int i;
  for (i = 0; i < count; i++)
    free((char*)Tips[i]);
}

void
CTipOfTheDayDialog::Transfer(CTransferInfo &ti)
{
  CALL_STACK_MESSAGE1("CTipOfTheDayDialog::Transfer()");
  ti.CheckBox(IDC_TOD_SHOW, Configuration.ShowTipOfTheDay);
}

void
CTipOfTheDayDialog::IncrementTipIndex()
{
  Configuration.LastTipOfTheDay++;
  if (Configuration.LastTipOfTheDay >= Tips.Count)
    Configuration.LastTipOfTheDay = 0;
}

BOOL
CTipOfTheDayDialog::DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
  CALL_STACK_MESSAGE4("CTipOfTheDayWindow::DialogProc(0x%X, 0x%IX, 0x%IX)", uMsg, wParam, lParam);
  switch (uMsg)
  {
    case WM_INITDIALOG:
    {
      TipWindow.AttachToWindow(GetDlgItem(HWindow, IDC_TOD_TIP));

      // assign an icon to the window
      SendMessage(HWindow, WM_SETICON, ICON_BIG,
                  (LPARAM)HANDLES(LoadIcon(HInstance, MAKEINTRESOURCE(IDI_TIPOFTHEDAY))));

      break;
    }

    case WM_DESTROY:
    {
      TipWindow.DetachWindow();
      if (MainWindow != NULL)
        MainWindow->TipOfTheDayDialog = NULL;
      break;
    }

    case WM_COMMAND:
    {
      if (LOWORD(wParam) == IDCANCEL)
        wParam = IDOK;
      if (LOWORD(wParam) == IDOK)
        IncrementTipIndex();
      if (LOWORD(wParam) == IDC_TOD_NEXT)
      {
        IncrementTipIndex();
        TipWindow.PaintBodyText();
        return 0;
      }
      if (LOWORD(wParam) == IDC_TOD_SHOW)
        TransferData(ttDataFromWindow);
      break;
    }
  }
  return CCommonDialog::DialogProc(uMsg, wParam, lParam);
}
*/

//****************************************************************************
//
// CSharesDialog
//

CSharesDialog::CSharesDialog(HWND hParent)
    : CCommonDialog(HLanguage, IDD_SHARES, IDD_SHARES, hParent),
      SharedDirs(FALSE) // we want full shares
{
    HListView = NULL;
    SortBy = 0; // sorted by share
    FocusedIndex = -1;
}

HIMAGELIST
CSharesDialog::CreateImageList()
{
    HIMAGELIST himl = ImageList_Create(16, 16, GetImageListColorFlags() | ILC_MASK, 2, 0);
    //  ImageList_SetBkColor(himl, GetSysColor(COLOR_WINDOW)); // make transparent icons work under XP

    HICON hIcon = SalLoadImage(4, 4, IconSizes[ICONSIZE_16], IconSizes[ICONSIZE_16], IconLRFlags); // symbolsDirectory
    if (hIcon != NULL)
    {
        ImageList_ReplaceIcon(himl, -1, hIcon);
        HANDLES(DestroyIcon(hIcon));
    }
    else
        ImageList_SetImageCount(himl, 1);

    ImageList_ReplaceIcon(himl, -1, HSharedOverlays[ICONSIZE_16]);
    ImageList_SetOverlayImage(himl, 1, 1);
    return himl;
}

void CSharesDialog::InitColumns()
{
    CALL_STACK_MESSAGE1("CSharesDialog::InitColumns()");
    LVCOLUMNW lvc;
    int header[3] = {IDS_SHARES_NAME, IDS_SHARES_PATH, IDS_SHARES_COMMENT};

    lvc.mask = LVCF_FMT | LVCF_TEXT | LVCF_SUBITEM;
    lvc.fmt = LVCFMT_LEFT;
    int i;
    for (i = 0; i < 3; i++) // create columns
    {
        std::wstring headerText = LoadStrOwned(header[i]);
        lvc.pszText = headerText.data();
        lvc.iSubItem = i;
        SendMessageW(HListView, LVM_INSERTCOLUMNW, i, (LPARAM)&lvc);
    }
    ListView_SetColumnWidth(HListView, 0, LVSCW_AUTOSIZE_USEHEADER);
    int width = ListView_GetColumnWidth(HListView, 0);
    int scrollW = GetSystemMetrics(SM_CXHSCROLL);
    ListView_SetColumnWidth(HListView, 0, width + 15 + scrollW);
    ListView_SetColumnWidth(HListView, 1, width * 3);
    ListView_SetColumnWidth(HListView, 2, LVSCW_AUTOSIZE_USEHEADER); // the rest
    ListView_SetColumnWidth(HListView, 0, width + 15);               // space for the scrollbar
}

int CALLBACK
CSharesDialog::SortFunc(LPARAM lParam1, LPARAM lParam2, LPARAM lParamSort)
{
    int nRetVal = 0;

    CSharesDialog* dlg = (CSharesDialog*)lParamSort;

    // wide: sort by the genuine wide values (GetItemW) instead of their narrow
    // mirrors - a signed comparator can't bolt a confirmation onto a narrow tie, so use the wide
    // sibling outright.
    int index1 = (int)lParam1;
    const wchar_t* localPath1;
    const wchar_t* remoteName1;
    const wchar_t* comment1;
    dlg->SharedDirs.GetItemW(index1, &localPath1, &remoteName1, &comment1);

    int index2 = (int)lParam2;
    const wchar_t* localPath2;
    const wchar_t* remoteName2;
    const wchar_t* comment2;
    dlg->SharedDirs.GetItemW(index2, &localPath2, &remoteName2, &comment2);

    switch (dlg->SortBy)
    {
    case 1: // shared path
    {
        nRetVal = StrICmpW(localPath1, localPath2);
        break;
    }

    case 2: // comment
    {
        nRetVal = StrICmpW(comment1, comment2);
        break;
    }

    default: // share name
    {
        nRetVal = StrICmpW(remoteName1, remoteName2);
        break;
    }
    }
    return nRetVal;
}

void CSharesDialog::EnableControls()
{
    BOOL focused = GetFocusedIndex() != -1;
    EnableWindow(GetDlgItem(HWindow, IDC_SHARES_STOP), focused);
    EnableWindow(GetDlgItem(HWindow, IDOK), focused);
}

void CSharesDialog::Refresh()
{
    SharedDirs.Refresh();

    SendMessage(HListView, WM_SETREDRAW, FALSE, 0);
    ListView_DeleteAllItems(HListView);
    // wide: SharedDirs.GetItemW is already the correct source (CShares was fully
    // wide-fixed earlier, and SortFunc/GetFocusedPathW/DeleteShare in this same class
    // already use it) - this function, which actually populates what the user sees, was the one
    // consumer still reading the narrow GetItem, so a share name/path/comment outside the current
    // code page rendered '?'-mangled while sorting and deleting already operated on the real text.
    const wchar_t* localPath;
    const wchar_t* remoteName;
    const wchar_t* comment;
    int i;
    for (i = 0; i < SharedDirs.GetCount(); i++)
    {
        if (SharedDirs.GetItemW(i, &localPath, &remoteName, &comment))
        {
            LVITEM lvi;
            lvi.mask = LVIF_IMAGE | LVIF_STATE | LVIF_PARAM;
            lvi.iItem = i;
            lvi.iSubItem = 0;
            lvi.iImage = 0;
            lvi.state = INDEXTOOVERLAYMASK(1);
            lvi.lParam = i; // for later sorting
            int index = ListView_InsertItem(HListView, &lvi);
            ListView_SetItemTextW(HListView, index, 0, (LPWSTR)remoteName);
            ListView_SetItemTextW(HListView, index, 1, (LPWSTR)localPath);
            ListView_SetItemTextW(HListView, index, 2, (LPWSTR)comment);
        }
    }
    SortItems();
    DWORD state = LVIS_SELECTED | LVIS_FOCUSED;
    ListView_SetItemState(HListView, 0, state, state);
    SendMessage(HListView, WM_SETREDRAW, TRUE, 0);
    EnableControls();
}

void CSharesDialog::SortItems()
{
    ListView_SortItems(HListView, SortFunc, (LPARAM)this);
}

const wchar_t*
CSharesDialog::GetFocusedPathW()
{
    if (FocusedIndex == -1)
        return NULL;
    const wchar_t* localPathW;
    SharedDirs.GetItemW(FocusedIndex, &localPathW, NULL, NULL);
    return localPathW;
}

int CSharesDialog::GetFocusedIndex()
{
    int index = ListView_GetNextItem(HListView, -1, LVIS_SELECTED);
    if (index != -1)
    {
        LVITEM lvi;
        lvi.iItem = index;
        lvi.iSubItem = 0;
        lvi.mask = LVIF_PARAM;
        ListView_GetItem(HListView, &lvi);
        index = (int)lvi.lParam;
    }
    return index;
}

void CSharesDialog::DeleteShare(const wchar_t* shareName)
{
    // shareName is the exact wide name from NetShareEnum now - no ANSI
    // round trip before NetShareDel (a Unicode-only API to begin with), so a share
    // name CP_ACP cannot spell no longer gets narrowed, mangled, and rewidened wrong.
    NetShareDel(NULL, const_cast<LPWSTR>(shareName), 0);
}

void CSharesDialog::OnContextMenu(int x, int y)
{
    HWND hListView = HListView;
    DWORD selCount = ListView_GetSelectedCount(hListView);
    if (selCount < 1)
        return;

    // pull button texts and populate the context menu
    int ids[] = {-2, IDOK, // -2 -> next item will be default
                 IDC_SHARES_STOP,
                 -1, // -1 -> separator
                 IDC_SHARES_REFRESH,
                 0}; // 0 -> terminator
    CMenuPopup popup;
    FillContextMenuFromButtons(&popup, HWindow, ids);

    DWORD cmd = popup.Track(MENU_TRACK_RETURNCMD | MENU_TRACK_RIGHTBUTTON,
                            x, y, HWindow, NULL);
    if (cmd != 0)
        PostMessage(HWindow, WM_COMMAND, cmd, 0);
}

INT_PTR
CSharesDialog::DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
    case WM_INITDIALOG:
    {
        HListView = GetDlgItem(HWindow, IDC_SHARES_LIST);
        DWORD exFlags = LVS_EX_FULLROWSELECT;
        DWORD origFlags = ListView_GetExtendedListViewStyle(HListView);
        ListView_SetExtendedListViewStyle(HListView, origFlags | exFlags); // 4.71

        HIMAGELIST himl = CreateImageList();
        ListView_SetImageList(HListView, himl, LVSIL_SMALL);

        /*
      // insert a resize grip into the bottom-right corner
      // not inserted, it would be too much code
      RECT r;
      GetClientRect(HWindow, &r);
      CreateWindowEx(0,
                     "scrollbar",
                     "",
                     WS_CHILDWINDOW | WS_CLIPSIBLINGS | WS_CLIPCHILDREN | WS_VISIBLE |
                     WS_GROUP | SBS_SIZEBOX | SBS_SIZEGRIP | SBS_SIZEBOXBOTTOMRIGHTALIGN,
                     0, 0, r.right, r.bottom,
                     HWindow,
                     (HMENU)IDC_SHARES_GRIP,
                     HInstance,
                     NULL);
      */

        InitColumns();
        Refresh();
        break;
    }

    case WM_COMMAND:
    {
        switch (LOWORD(wParam))
        {
        case IDOK:
        {
            FocusedIndex = GetFocusedIndex();
            if (FocusedIndex == -1)
                return 0;
            break;
        }

        case IDC_SHARES_REFRESH:
        {
            Refresh();
            break;
        }

        case IDC_SHARES_STOP:
        {
            int index = GetFocusedIndex();
            if (index != -1)
            {
                const wchar_t* remoteNameW;
                if (SharedDirs.GetItemW(index, NULL, &remoteNameW, NULL))
                {
                    std::wstring msg = FormatStrW(LoadStrW(IDS_CONFIRM_STOPSHARE), remoteNameW);
                    if (gPrompter->ConfirmError(LoadStrW(IDS_QUESTION), msg.c_str()).type == PromptResult::kOk)
                    {
                        DeleteShare(remoteNameW);
                        Refresh();
                    }
                }
            }
            break;
        }
        }
        break;
    }

    case WM_NOTIFY:
    {
        if (wParam == IDC_SHARES_LIST)
        {
            switch (((LPNMHDR)lParam)->code)
            {
            case NM_DBLCLK:
            {
                PostMessage(HWindow, WM_COMMAND, MAKELPARAM(IDOK, BN_CLICKED), 0);
                break;
            }

            case NM_RCLICK:
            {
                DWORD pos = GetMessagePos();
                OnContextMenu(GET_X_LPARAM(pos), GET_Y_LPARAM(pos));
                return 0;
            }

            case LVN_KEYDOWN:
            {
                NMLVKEYDOWN* kd = (NMLVKEYDOWN*)lParam;
                BOOL shiftPressed = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
                if (shiftPressed && kd->wVKey == VK_F10 || kd->wVKey == VK_APPS)
                {
                    POINT p;
                    GetListViewContextMenuPos(HListView, &p);
                    OnContextMenu(p.x, p.y);
                }
                return 0;
            }

            case LVN_ITEMCHANGED:
            {
                EnableControls();
                return 0;
            }

            case LVN_COLUMNCLICK:
            {
                int subItem = ((NM_LISTVIEW*)lParam)->iSubItem;
                if (subItem >= 0 && subItem < 3)
                {
                    if (subItem != SortBy)
                    {
                        SortBy = subItem;
                        SortItems();
                    }
                }
                break;
            }
            }
        }
        break;
    }

    case WM_SYSCOLORCHANGE:
    {
        DarkMode_ApplyListTreeThemeRecursive(HListView);
        break;
    }
    }
    return CCommonDialog::DialogProc(uMsg, wParam, lParam);
}

//****************************************************************************
//
// CDisconnectDialog
//

CDisconnectDialog::CDisconnectDialog(CFilesWindow* panel)
    : CCommonDialog(HLanguage, IDD_DISCONNECT, IDD_DISCONNECT, panel->HWindow), Connections(10, 5)
{
    Panel = panel;
    HListView = NULL;
    HImageList = NULL;
    NoConncection = FALSE;
}

CDisconnectDialog::~CDisconnectDialog()
{
    DestroyConnections();
}

void CDisconnectDialog::Validate(CTransferInfo& ti)
{
    if (!OnDisconnect())
    {
        ti.ErrorOn(IDC_DISCONNECT_LIST);
        return;
    }
}

BOOL CDisconnectDialog::OnDisconnect()
{
    int index;
    int iStart = -1;
    while ((index = ListView_GetNextItem(HListView, iStart, LVNI_SELECTED)) != -1)
    {
        iStart = index;

        // just disconnect items but leave them in Connections array, which will be cleaned
        // in this dialog's destructor or during Refresh() before this method returns FALSE

        if (Connections[index].Type == citNetwork)
        {
            // NETWORK
            // DISCDLGSTRUCT/WNetDisconnectDialog1 are TCHAR-generic and would
            // resolve to the ANSI form until the UNICODE define flips; use the
            // explicit wide forms now since Name/Path are already wchar_t*.
            DISCDLGSTRUCTW conn;
            conn.cbStructure = sizeof(conn);
            conn.hwndOwner = HWindow;
            if (_wcsicmp(Connections[index].Name, LoadStrW(IDS_NETWORK_NONE)) == 0)
            {
                conn.lpLocalName = Connections[index].Path;
                conn.lpRemoteName = NULL;
            }
            else
            {
                conn.lpLocalName = Connections[index].Name;
                conn.lpRemoteName = Connections[index].Path;
            }
            conn.dwFlags = DISC_UPDATE_PROFILE;
            if (WNetDisconnectDialog1W(&conn) != NO_ERROR)
            {
                Refresh(); // we must rebuild the array and list view to remove already disconnected items
                // without invalidating and updating the main window, the area under the Disconnect dialog isn't redrawn after closing
                // (the dialog uses save-bits and its remembered background probably won't be invalidated,
                // likely a paint optimization)
                InvalidateRect(MainWindow->HWindow, NULL, FALSE);
                UpdateWindow(MainWindow->HWindow);
                return FALSE;
            }
        }
        else
        {
            if (Connections[index].Type == citPlugin)
            {
                // PLUGIN FILE SYSTEM
                CPluginFSInterfaceAbstract* pluginFS = Connections[index].PluginFS;
                if (pluginFS != NULL)
                {
                    BOOL isInPanel = FALSE;
                    int panel = 0;
                    CPluginFSInterfaceEncapsulation* fs = NULL; // find the encapsulation of the FS-object
                    if (MainWindow->LeftPanel->Is(ptPluginFS) &&
                        MainWindow->LeftPanel->GetPluginFS()->Contains(pluginFS))
                    {
                        fs = MainWindow->LeftPanel->GetPluginFS();
                        isInPanel = TRUE;
                        panel = PANEL_LEFT;
                    }
                    if (fs == NULL && MainWindow->RightPanel->Is(ptPluginFS) &&
                        MainWindow->RightPanel->GetPluginFS()->Contains(pluginFS))
                    {
                        fs = MainWindow->RightPanel->GetPluginFS();
                        isInPanel = TRUE;
                        panel = PANEL_RIGHT;
                    }
                    if (fs == NULL)
                    {
                        CDetachedFSList* list = MainWindow->DetachedFSList;
                        int j;
                        for (j = 0; j < list->Count; j++)
                        {
                            CPluginFSInterfaceEncapsulation* detachedFS = list->At(j);
                            if (detachedFS->Contains(pluginFS))
                            {
                                fs = detachedFS;
                                break;
                            }
                        }
                    }
                    if (fs != NULL)
                    {
                        CPluginInterfaceForFSEncapsulation* ifaceForFS = fs->GetPluginInterfaceForFS();
                        if (!ifaceForFS->DisconnectFS(HWindow, isInPanel, panel, pluginFS,
                                                      fs->GetPluginFSName(), fs->GetPluginFSNameIndex()))
                        {              // disconnect was rejected (probably by the user)
                            Refresh(); // we must rebuild the array and list view to remove already disconnected items
                            // without invalidating and updating the main window, the area under the Disconnect dialog isn't redrawn after closing
                            // (the dialog uses save-bits and its remembered background probably won't be invalidated,
                            // likely a paint optimization)
                            InvalidateRect(MainWindow->HWindow, NULL, FALSE);
                            UpdateWindow(MainWindow->HWindow);
                            return FALSE;
                        }
                    }
                    else
                        TRACE_E("CDisconnectDialog::OnDisconnect(): unexpected situation - unable to disconnect FS, because it not not found (most probably it has been already closed)!");
                }
                else
                    TRACE_E("CDisconnectDialog::OnDisconnect(): unexpected situation - CConnectionItem::PluginFS is NULL!");
            }
        }
    }
    // without invalidating and updating the main window, the area under the Disconnect dialog isn't redrawn after closing
    // (the dialog uses save-bits and its remembered background probably won't be invalidated,
    // likely a paint optimization)
    InvalidateRect(MainWindow->HWindow, NULL, FALSE);
    UpdateWindow(MainWindow->HWindow);
    return TRUE;
}

void CDisconnectDialog::EnableControls()
{
    int index;
    index = ListView_GetNextItem(HListView, -1, LVNI_SELECTED);
    EnableWindow(GetDlgItem(HWindow, IDOK), index != -1);
}

HIMAGELIST
CDisconnectDialog::CreateImageList()
{
    HIMAGELIST himl = ImageList_Create(16, 16, GetImageListColorFlags() | ILC_MASK, 4, 10);
    //  ImageList_SetBkColor(himl, GetSysColor(COLOR_WINDOW)); // make transparent icons work under XP

    // CONNECTION_ICON_NETWORK
    HICON hIcon = SalLoadImage(33, 10, IconSizes[ICONSIZE_16], IconSizes[ICONSIZE_16], IconLRFlags); // accessible network drive
    if (hIcon != NULL)
    {
        ImageList_ReplaceIcon(himl, -1, hIcon);
        HANDLES(DestroyIcon(hIcon));
    }
    else
        TRACE_E("Icon was not found!");

    // CONNECTION_ICON_PLUGIN
    hIcon = SalLoadIcon(HInstance, IDI_PLUGINFS, IconSizes[ICONSIZE_16]);
    ImageList_ReplaceIcon(himl, -1, hIcon);
    HANDLES(DestroyIcon(hIcon));

    // CONNECTION_ICON_ACCESSIBLE
    hIcon = SalLoadImage(33, 10, IconSizes[ICONSIZE_16], IconSizes[ICONSIZE_16], IconLRFlags); // accessible network drive
    if (hIcon != NULL)
    {
        ImageList_ReplaceIcon(himl, -1, hIcon);
        HANDLES(DestroyIcon(hIcon));
    }
    else
        TRACE_E("Icon was not found!");

    // CONNECTION_ICON_INACCESSIBLE
    hIcon = SalLoadImage(31, 11, IconSizes[ICONSIZE_16], IconSizes[ICONSIZE_16], IconLRFlags); // non-accessible network drive
    if (hIcon != NULL)
    {
        ImageList_ReplaceIcon(himl, -1, hIcon);
        HANDLES(DestroyIcon(hIcon));
    }
    else
        TRACE_E("Icon was not found!");

    return himl;
}

void CDisconnectDialog::InitColumns()
{
    CALL_STACK_MESSAGE1("CDisconnectDialog::InitColumns()");
    LVCOLUMNW lvc;
    int header[2] = {IDS_DISCONNECT_NAME, IDS_DISCONNECT_PATH};

    lvc.mask = LVCF_FMT | LVCF_TEXT | LVCF_SUBITEM;
    lvc.fmt = LVCFMT_LEFT;
    int i;
    for (i = 0; i < 2; i++) // create columns
    {
        std::wstring headerText = LoadStrOwned(header[i]);
        lvc.pszText = headerText.data();
        lvc.iSubItem = i;
        SendMessageW(HListView, LVM_INSERTCOLUMNW, i, (LPARAM)&lvc);
    }
    ListView_SetColumnWidth(HListView, 0, LVSCW_AUTOSIZE_USEHEADER);
    int width = ListView_GetColumnWidth(HListView, 0);
    int scrollW = GetSystemMetrics(SM_CXHSCROLL);
    ListView_SetColumnWidth(HListView, 0, width + 15 + scrollW);
    ListView_SetColumnWidth(HListView, 1, LVSCW_AUTOSIZE_USEHEADER); // the rest
    ListView_SetColumnWidth(HListView, 0, width + 15);               // space for the scrollbar
}

wchar_t* CreateIndexedPluginPathText(const wchar_t* pathText, int index)
{
    std::wstring newText = std::wstring(pathText) + L" [" + std::to_wstring(index) + L"]";
    return _wcsdup(newText.c_str());
}

void CDisconnectDialog::EnumConnections()
{
    CALL_STACK_MESSAGE1("CDisconnectDialog::EnumConnections()");

    // Network: CONNECTED resources

    wchar_t noneText[50]; // (none)
    lstrcpynW(noneText, LoadStrW(IDS_NETWORK_NONE), _countof(noneText));
    HANDLE hEnumNet;
    // WNetEnumResourceW, not a courtesy sibling: the enumeration is the
    // authority HasTheSameRootPath compares the panel path against (same defect
    // class as IsNetworkProviderDriveW, sally_path_validation.cpp) - narrowing it
    // would corrupt both sides of the comparison at once, so a mapped drive or share
    // whose name the code page cannot spell was never recognised as the panel's
    // current one and never shown pre-selected. Display stays narrow (unchanged
    // behavior for spellable names, best-effort otherwise) - only the
    // bold/default-item DECISION needs to be honest.
    DWORD err = WNetOpenEnumW(RESOURCE_CONNECTED, RESOURCETYPE_DISK, 0, NULL, &hEnumNet);
    if (err == ERROR_SUCCESS)
    {
        DWORD bufSize;
        DWORD entries = 0;
        BYTE buffer[10000];
        NETRESOURCEW* netSources = (NETRESOURCEW*)buffer;
        while (1)
        {
            DWORD e = 0xFFFFFFFF; // as many as possible
            bufSize = sizeof(buffer);
            err = WNetEnumResourceW(hEnumNet, &e, netSources, &bufSize);
            if (err == ERROR_SUCCESS && e > 0)
            {
                int i;
                for (i = 0; i < (int)e; i++) // process new data
                {
                    const wchar_t* nameW = netSources[i].lpLocalName;
                    BOOL isNone = (nameW == NULL);
                    const wchar_t* pathW = netSources[i].lpRemoteName;
                    if (pathW == NULL)
                        pathW = L"";
                    BOOL defaultItem = Panel->Is(ptDisk) &&
                                       HasTheSameRootPath(Panel->GetPathW(), isNone ? pathW : nameW);
                    InsertItem(-2, FALSE, citNetwork, CONNECTION_ICON_ACCESSIBLE, isNone ? noneText : nameW, pathW, defaultItem, NULL); // -2 -> insert alphabetically
                }
                entries += e;
            }
            else
                break;
        }
        WNetCloseEnum(hEnumNet);
    }
    else
    {
        if (err != ERROR_NO_NETWORK)
            gPrompter->ShowError(LoadStrW(IDS_NETWORKERROR), GetErrorTextOwned(err).c_str());
    }

    // Network: REMEMBERED resources

    // available drives
    DWORD mask = GetLogicalDrives();

    err = WNetOpenEnumW(RESOURCE_REMEMBERED, RESOURCETYPE_DISK, 0, NULL, &hEnumNet);
    if (err == ERROR_SUCCESS)
    {
        DWORD bufSize;
        DWORD entries = 0;
        BYTE buffer[10000];
        NETRESOURCEW* netSources = (NETRESOURCEW*)buffer;
        while (1)
        {
            DWORD e = 0xFFFFFFFF; // as many as possible
            bufSize = sizeof(buffer);
            err = WNetEnumResourceW(hEnumNet, &e, netSources, &bufSize);
            if (err == ERROR_SUCCESS && e > 0)
            {
                int i;
                for (i = 0; i < (int)e; i++) // process new data
                {
                    const wchar_t* nameW = netSources[i].lpLocalName;
                    if (nameW != NULL)
                    {
                        BOOL iconIndex = CONNECTION_ICON_ACCESSIBLE;
                        // a remembered drive-letter mapping's letter is always ASCII
                        // by construction; only the remote share name can be Unicode.
                        char drv = LowerCase[(char)nameW[0]];
                        if (drv >= 'a' && drv <= 'z' && (mask & (0x00000001 << (drv - 'a'))) == 0)
                            iconIndex = CONNECTION_ICON_INACCESSIBLE;

                        const wchar_t* pathW = netSources[i].lpRemoteName;
                        if (pathW == NULL)
                            pathW = L"";
                        BOOL defaultItem = Panel->Is(ptDisk) &&
                                           HasTheSameRootPath(Panel->GetPathW(), *nameW == 0 ? pathW : nameW);
                        InsertItem(-2, TRUE, citNetwork, iconIndex, nameW, pathW, defaultItem, NULL); // -2 -> insert alphabetically
                    }
                }
                entries += e;
            }
            else
                break;
        }
        WNetCloseEnum(hEnumNet);
    }
    else
    {
        if (err != ERROR_NO_NETWORK)
            gPrompter->ShowError(LoadStrW(IDS_NETWORKERROR), GetErrorTextOwned(err).c_str());
    }

    // if at least one network drive was inserted, add it in front of the first network group
    if (Connections.Count > 0)
        InsertItem(0, FALSE, citGroup, CONNECTION_ICON_NETWORK, LoadStrW(IDS_NETWORK_NETWORK), L"", FALSE, NULL);

    int pluginGroupIndex = Connections.Count;

    // Plugins: FILE SYSTEMS

    CDetachedFSList* list = MainWindow->DetachedFSList;
    CPluginFSInterfaceEncapsulation** fsList = (CPluginFSInterfaceEncapsulation**)malloc(sizeof(CPluginFSInterfaceEncapsulation*) * (list->Count + 2));
    if (fsList != NULL)
    {
        CPluginFSInterfaceEncapsulation** fsListItem = fsList;
        CPluginFSInterfaceEncapsulation* activePanelFS = NULL;
        if (Panel->Is(ptPluginFS))
        {
            activePanelFS = Panel->GetPluginFS();
            *fsListItem++ = activePanelFS;
        }
        CFilesWindow* otherPanel = MainWindow->LeftPanel == Panel ? MainWindow->RightPanel : MainWindow->LeftPanel;
        CPluginFSInterfaceEncapsulation* nonactivePanelFS = NULL;
        if (otherPanel->Is(ptPluginFS))
        {
            nonactivePanelFS = otherPanel->GetPluginFS();
            *fsListItem++ = nonactivePanelFS;
        }
        int i;
        for (i = 0; i < list->Count; i++)
            *fsListItem++ = list->At(i);
        int count = (int)(fsListItem - fsList);
        if (count > 1)
            SortPluginFSTimes(fsList, 0, count - 1);

        if (count > 0)
        {
            HWND oldPluginMsgBoxParent = PluginMsgBoxParent;
            PluginMsgBoxParent = HWindow;

            BOOL addFSItemForActivePanelFS = FALSE;
            BOOL addFSItemForNonactivePanelFS = FALSE;
            for (i = 0; i < count; i++)
            {
                CPluginFSInterfaceEncapsulation* fs = fsList[i];
                wchar_t* txt = NULL;
                HICON icon = NULL;
                BOOL destroyIcon = FALSE;
                if (fs->GetChangeDriveOrDisconnectItem(fs->GetPluginFSName(), txt, icon, destroyIcon))
                {
                    int iconIndex = -1;
                    if (icon != NULL)
                        iconIndex = ImageList_ReplaceIcon(HImageList, -1, icon);
                    if (destroyIcon && icon != NULL)
                        HANDLES(DestroyIcon(icon));
                    wchar_t* text = txt;
                    while (*text != 0 && *text != L'\t')
                        text++;
                    if (*text == L'\t')
                        text++;
                    wchar_t* s = text;
                    while (*s != 0 && *s != L'\t')
                        s++;
                    *s = 0;
                    InsertItem(-1, FALSE, citPlugin, iconIndex != -1 ? iconIndex : CONNECTION_ICON_PLUGIN,
                               L"", text, fs == activePanelFS, fs->GetInterface());
                    free(txt);
                }
                else // FS does not want to add any item
                {
                    if (fs == activePanelFS)
                        addFSItemForActivePanelFS = TRUE;
                    else if (fs == nonactivePanelFS)
                        addFSItemForNonactivePanelFS = TRUE;
                }
            }

            // check items text uniqueness and index duplicate items
            for (i = pluginGroupIndex; i < Connections.Count; i++)
            {
                BOOL freePluginPath = FALSE;
                wchar_t* pluginPath = Connections[i].Path;
                int currentIndex = 1;
                int x;
                for (x = i + 1; x < Connections.Count; x++)
                {
                    wchar_t* testedPath = Connections[x].Path;
                    if (StrICmpW(pluginPath, testedPath) == 0) // match -> the item must be indexed
                    {
                        if (!freePluginPath) // the first match found, index also the first duplicate item
                        {
                            currentIndex = GetIndexForDrvText(fsList, count, Connections[i].PluginFS, currentIndex);
                            Connections[i].Path = CreateIndexedPluginPathText(pluginPath, currentIndex++);
                            if (Connections[i].Path == NULL)
                                Connections[i].Path = pluginPath;
                            else
                                freePluginPath = TRUE;
                        }
                        currentIndex = GetIndexForDrvText(fsList, count, Connections[x].PluginFS, currentIndex);
                        Connections[x].Path = CreateIndexedPluginPathText(testedPath, currentIndex++);
                        if (Connections[x].Path == NULL)
                            Connections[x].Path = testedPath;
                        else
                            free(testedPath);
                    }
                }
                if (freePluginPath)
                    free(pluginPath);
            }

            // ensure addition for "unary" FS (RegEdit, WMobile, etc.) - they have no entries for open FS
            // and allow only one opened FS
            if (addFSItemForActivePanelFS || addFSItemForNonactivePanelFS)
            {
                int i2;
                for (i2 = 0; i2 < 2; i2++)
                {
                    if (i2 == 0 && addFSItemForActivePanelFS ||
                        i2 == 1 && addFSItemForNonactivePanelFS)
                    {
                        CPluginFSInterfaceEncapsulation* fs = i2 == 0 ? activePanelFS : nonactivePanelFS;
                        std::wstring userPart;
                        fs->GetRootPathW(userPart);
                        const std::wstring path = std::wstring(fs->GetPluginFSName()) + L":" + userPart;

                        BOOL destroyIcon = FALSE;
                        HICON icon = fs->GetFSIcon(destroyIcon);
                        int iconIndex = -1;
                        if (icon != NULL)
                            iconIndex = ImageList_ReplaceIcon(HImageList, -1, icon);
                        if (destroyIcon && icon != NULL)
                            HANDLES(DestroyIcon(icon));
                        InsertItem(-1, FALSE, citPlugin, iconIndex != -1 ? iconIndex : CONNECTION_ICON_PLUGIN,
                                   L"", path.c_str(), fs == activePanelFS, fs->GetInterface());
                    }
                }
            }

            PluginMsgBoxParent = oldPluginMsgBoxParent;
        }
        free(fsList);
    }

    // if at least one file system was inserted, add in front of the the first plugin group
    if (pluginGroupIndex < Connections.Count)
        InsertItem(pluginGroupIndex, FALSE, citGroup, CONNECTION_ICON_PLUGIN, LoadStrW(IDS_NETWORK_PLUGINS), L"", FALSE, NULL);
}

BOOL CDisconnectDialog::InsertItem(int index, BOOL ignoreDuplicate, CConnectionItemType type, int iconIndex, const wchar_t* name,
                                   const wchar_t* path, BOOL defaultItem, CPluginFSInterfaceAbstract* pluginFS)
{
    if (ignoreDuplicate)
    {
        int i;
        for (i = 0; i < Connections.Count; i++)
        {
            if (Connections[i].Type == type &&
                _wcsicmp(Connections[i].Name, name) == 0 &&
                _wcsicmp(Connections[i].Path, path) == 0)
            {
                return TRUE;
            }
        }
    }

    CConnectionItem item;
    ZeroMemory(&item, sizeof(item));

    item.Type = type;
    item.IconIndex = iconIndex;
    item.Name = DupStr(name);
    if (item.Name == NULL)
    {
        TRACE_E(LOW_MEMORY);
        return FALSE;
    }
    item.Default = defaultItem;
    item.PluginFS = pluginFS;

    item.Path = DupStr(path);
    if (item.Path == NULL)
    {
        free(item.Name);
        TRACE_E(LOW_MEMORY);
        return FALSE;
    }

    if (index == -2)
    {
        // the item should be inserted alphabetically (tche@thlsofts.be sent a screenshot showing
        // that enumerators return the list in random order)
        int i;
        for (i = 0; i < Connections.Count; i++)
        {
            if (Connections[i].Type == type &&
                _wcsicmp(Connections[i].Name, name) > 0)
            {
                index = i;
                break;
            }
        }
        if (index == -2)
            index = -1;
    }

    if (index == -1)
        Connections.Add(item);
    else
        Connections.Insert(index, item);

    if (!Connections.IsGood())
    {
        free(item.Name);
        free(item.Path);
        Connections.ResetState();
        TRACE_E(LOW_MEMORY);
        return FALSE;
    }

    TRACE_IW(L"ADDED: " << name << L"    " << path);
    return TRUE;
}

void CDisconnectDialog::DestroyConnections()
{
    int i;
    for (i = 0; i < Connections.Count; i++)
    {
        free(Connections[i].Name);
        free(Connections[i].Path);
    }
    Connections.DestroyMembers();
}

void CDisconnectDialog::Refresh()
{
    DestroyConnections();
    EnumConnections();

    SendMessage(HListView, WM_SETREDRAW, FALSE, 0);
    ListView_DeleteAllItems(HListView);

    int defaultIndex = -1;
    int i;
    for (i = 0; i < Connections.Count; i++)
    {
        // LVITEMW + LVM_INSERTITEMW explicitly: this build does not define _UNICODE, so
        // the ListView_InsertItem macro resolves to the A form, which would narrow
        // CConnectionItem::Name through the code page. Same reason as the local
        // ListView_SetItemTextW macro at the top of this file.
        LVITEMW lvi;
        lvi.mask = LVIF_TEXT | LVIF_IMAGE | LVIF_PARAM | LVIF_INDENT;
        lvi.iItem = i;
        lvi.iSubItem = 0;
        lvi.iImage = Connections[i].IconIndex;
        lvi.pszText = Connections[i].Name;
        lvi.lParam = i; // for later sorting
        lvi.iIndent = (Connections[i].Type == citGroup) ? 0 : 1;
        int index = (int)SendMessageW(HListView, LVM_INSERTITEMW, 0, (LPARAM)&lvi);
        ListView_SetItemTextW(HListView, index, 1, Connections[i].Path);
        if (Connections[i].Default)
        {
            if (defaultIndex == -1)
                defaultIndex = i;
            else
                TRACE_E("CDisconnectDialog::Refresh(): Only one item should have set Default==TRUE; ignoring others.");
        }
    }
    ListView_SetImageList(HListView, HImageList, LVSIL_SMALL);

    DWORD state = LVIS_SELECTED | LVIS_FOCUSED;
    if (defaultIndex == -1)
        defaultIndex = 1;
    ListView_SetItemState(HListView, defaultIndex, state, state);
    ListView_EnsureVisible(HListView, defaultIndex, FALSE);

    ListView_SetColumnWidth(HListView, 0, LVSCW_AUTOSIZE_USEHEADER);
    ListView_SetColumnWidth(HListView, 1, LVSCW_AUTOSIZE_USEHEADER);

    SendMessage(HListView, WM_SETREDRAW, TRUE, 0);
}

INT_PTR
CDisconnectDialog::DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
    case WM_INITDIALOG:
    {
        HListView = GetDlgItem(HWindow, IDC_DISCONNECT_LIST);
        DWORD exFlags = LVS_EX_FULLROWSELECT;
        DWORD origFlags = ListView_GetExtendedListViewStyle(HListView);
        ListView_SetExtendedListViewStyle(HListView, origFlags | exFlags); // 4.71

        HImageList = CreateImageList();
        ListView_SetImageList(HListView, HImageList, LVSIL_SMALL);

        InitColumns();
        Refresh();

        if (ListView_GetItemCount(HListView) == 0)
        {
            SendMessage(HWindow, WM_COMMAND, IDCANCEL, 0);
            NoConncection = TRUE;
            return 0;
        }
        break;
    }

    case WM_NOTIFY:
    {
        if (wParam == IDC_DISCONNECT_LIST)
        {
            switch (((LPNMHDR)lParam)->code)
            {
            case NM_DBLCLK:
            {
                if (IsWindowEnabled(GetDlgItem(HWindow, IDOK)))
                    PostMessage(HWindow, WM_COMMAND, MAKELPARAM(IDOK, BN_CLICKED), 0);
                break;
            }

            case LVN_ITEMCHANGED:
            {
                int i;
                for (i = 0; i < ListView_GetItemCount(HListView); i++)
                {
                    LVITEM lvi;
                    lvi.iItem = i;
                    lvi.iSubItem = 0;
                    lvi.mask = LVIF_PARAM;
                    ListView_GetItem(HListView, &lvi);
                    int index = (int)lvi.lParam;
                    if (index < 0 || index >= Connections.Count || // just in case
                        Connections[index].Type == citGroup)
                    {
                        // clear the selection
                        ListView_SetItemState(HListView, i, 0, LVIS_SELECTED);
                    }
                }

                EnableControls();
                return 0;
            }
            }
        }
        break;
    }

    case WM_SYSCOLORCHANGE:
    {
        DarkMode_ApplyListTreeThemeRecursive(HListView);
        break;
    }
    }
    return CCommonDialog::DialogProc(uMsg, wParam, lParam);
}

//****************************************************************************
//
// CSaveSelectionDialog
//

CSaveSelectionDialog::CSaveSelectionDialog(HWND hParent, BOOL* clipboard)
    : CCommonDialog(HLanguage, IDD_SAVESELECTION, IDD_SAVESELECTION, hParent)
{
    Clipboard = clipboard;
}

void CSaveSelectionDialog::Transfer(CTransferInfo& ti)
{
    ti.RadioButton(IDC_SAVESEL_MEMORY, FALSE, *Clipboard);
    ti.RadioButton(IDC_SAVESEL_CLIPBOARD, TRUE, *Clipboard);
}

//****************************************************************************
//
// CLoadSelectionDialog
//

CLoadSelectionDialog::CLoadSelectionDialog(HWND hParent, CLoadSelectionOperation* operation, BOOL* clipboard,
                                           BOOL clipboardValid, BOOL globalValid)
    : CCommonDialog(HLanguage, IDD_LOADSELECTION, IDD_LOADSELECTION, hParent)
{
    Operation = operation;
    Clipboard = clipboard;
    ClipboardValid = clipboardValid;
    GlobalValid = globalValid;
}

void CLoadSelectionDialog::Transfer(CTransferInfo& ti)
{
    if (ti.Type == ttDataToWindow)
    {
        if (!ClipboardValid)
            EnableWindow(GetDlgItem(HWindow, IDC_LOADSEL_CLIPBOARD), FALSE);
        if (!GlobalValid)
            EnableWindow(GetDlgItem(HWindow, IDC_LOADSEL_MEMORY), FALSE);
    }
    ti.RadioButton(IDC_LOADSEL_MEMORY, FALSE, *Clipboard);
    ti.RadioButton(IDC_LOADSEL_CLIPBOARD, TRUE, *Clipboard);
    int op = *Operation;
    ti.RadioButton(IDC_LOADSEL_COPY, lsoCOPY, op);
    ti.RadioButton(IDC_LOADSEL_OR, lsoOR, op);
    ti.RadioButton(IDC_LOADSEL_DIFF, lsoDIFF, op);
    ti.RadioButton(IDC_LOADSEL_AND, lsoAND, op);
    *Operation = (CLoadSelectionOperation)op;
}

//****************************************************************************
//
// CCompareDirsDialog
//

CCompareDirsDialog::CCompareDirsDialog(HWND hParent, BOOL enableByDateAndTime, BOOL enableBySize,
                                       BOOL enableByAttrs, BOOL enableByContent, BOOL enableSubdirs,
                                       BOOL enableCompAttrsOfSubdirs, CFilesWindow* leftPanel,
                                       CFilesWindow* rightPanel)
    : CCommonDialog(HLanguage, IDD_COMPAREDIRS, IDD_COMPAREDIRS, hParent)
{
    EnableByDateAndTime = enableByDateAndTime;
    EnableBySize = enableBySize;
    EnableByAttrs = enableByAttrs;
    EnableByContent = enableByContent;
    EnableSubdirs = enableSubdirs;
    EnableCompAttrsOfSubdirs = enableCompAttrsOfSubdirs;
    LeftPanel = leftPanel;
    RightPanel = rightPanel;
    Expanded = TRUE;
}

BOOL ValidateMask(HWND parent, CTransferInfo& ti, int checkbox, int editline)
{
    BOOL ignore;
    ti.CheckBox(checkbox, ignore);
    if (ignore)
    {
        const std::wstring buf = GetWindowTextStringW(GetDlgItem(parent, editline));
        CMaskGroup masks(buf.c_str());
        int errorPos;
        if (!masks.PrepareMasks(errorPos))
        {
            gPrompter->ShowError(LoadStrW(IDS_ERRORTITLE), LoadStrW(IDS_INCORRECTSYNTAX));
            SetFocus(GetDlgItem(parent, editline));
            SendMessage(GetDlgItem(parent, editline), EM_SETSEL, errorPos, errorPos + 1);
            ti.ErrorOn(editline);
            return FALSE;
        }
    }
    return TRUE;
}

void CCompareDirsDialog::Validate(CTransferInfo& ti)
{
    CALL_STACK_MESSAGE1("CCompareDirsDialog::Validate()");

    BOOL ret = TRUE;
    if (ret)
        ret &= ValidateMask(HWindow, ti, IDC_COMPARE_IGNORE_FILES, IDE_COMPARE_IGNORE_FILES);
    if (ret)
        ret &= ValidateMask(HWindow, ti, IDC_COMPARE_IGNORE_DIRS, IDE_COMPARE_IGNORE_DIRS);
}

void CCompareDirsDialog::Transfer(CTransferInfo& ti)
{
    if (EnableBySize)
        ti.CheckBox(IDC_COMPARE_BYSIZE, Configuration.CompareBySize);
    if (EnableByDateAndTime)
        ti.CheckBox(IDC_COMPARE_BYTIME, Configuration.CompareByTime);
    if (EnableByAttrs)
        ti.CheckBox(IDC_COMPARE_BYATTR, Configuration.CompareByAttr);
    if (EnableByContent)
        ti.CheckBox(IDC_COMPARE_BYCONTENT, Configuration.CompareByContent);
    if (EnableSubdirs)
    {
        ti.CheckBox(IDC_COMPARE_SUBDIRS, Configuration.CompareSubdirs);
        if (EnableCompAttrsOfSubdirs)
            ti.CheckBox(IDC_COMPARE_SUBDIRS_ATTR, Configuration.CompareSubdirsAttr);
    }

    ti.CheckBox(IDC_COMPARE_ONE_PANEL_DIRS, Configuration.CompareOnePanelDirs);
    ti.CheckBox(IDC_COMPARE_IGNORE_FILES, Configuration.CompareIgnoreFiles);
    if (ti.Type == ttDataToWindow || EnableSubdirs || Configuration.CompareOnePanelDirs)
        ti.CheckBox(IDC_COMPARE_IGNORE_DIRS, Configuration.CompareIgnoreDirs);

    if (ti.Type == ttDataToWindow)
    {
        SetDlgItemTextW(HWindow, IDE_COMPARE_IGNORE_FILES,
                        Configuration.CompareIgnoreFilesMasks.GetMasksString());
        SetDlgItemTextW(HWindow, IDE_COMPARE_IGNORE_DIRS,
                        Configuration.CompareIgnoreDirsMasks.GetMasksString());
    }
    else
    {
        const std::wstring fileMasks =
            GetWindowTextStringW(GetDlgItem(HWindow, IDE_COMPARE_IGNORE_FILES));
        const std::wstring dirMasks =
            GetWindowTextStringW(GetDlgItem(HWindow, IDE_COMPARE_IGNORE_DIRS));
        Configuration.CompareIgnoreFilesMasks.SetMasksString(fileMasks.c_str());
        Configuration.CompareIgnoreDirsMasks.SetMasksString(dirMasks.c_str());
    }

    if (ti.Type == ttDataToWindow)
    {
        EnableControls();
    }
    else
    {
        Configuration.CompareMoreOptions = Expanded;
    }
}

void CCompareDirsDialog::EnableControls()
{
    EnableWindow(GetDlgItem(HWindow, IDC_COMPARE_BYSIZE), EnableBySize);
    EnableWindow(GetDlgItem(HWindow, IDC_COMPARE_BYTIME), EnableByDateAndTime);
    EnableWindow(GetDlgItem(HWindow, IDC_COMPARE_BYATTR), EnableByAttrs);
    EnableWindow(GetDlgItem(HWindow, IDC_COMPARE_BYCONTENT), EnableByContent);
    EnableWindow(GetDlgItem(HWindow, IDC_COMPARE_SUBDIRS), EnableSubdirs);
    BOOL subdirs = EnableCompAttrsOfSubdirs &&
                   EnableSubdirs && IsDlgButtonChecked(HWindow, IDC_COMPARE_SUBDIRS) == BST_CHECKED;
    EnableWindow(GetDlgItem(HWindow, IDC_COMPARE_ONE_PANEL_DIRS), !subdirs);
    if (subdirs)
        CheckDlgButton(HWindow, IDC_COMPARE_ONE_PANEL_DIRS, BST_UNCHECKED);
    EnableWindow(GetDlgItem(HWindow, IDC_COMPARE_SUBDIRS_ATTR), subdirs);
    if (!subdirs)
        CheckDlgButton(HWindow, IDC_COMPARE_SUBDIRS_ATTR, BST_UNCHECKED);

    BOOL ignoreFiles = IsDlgButtonChecked(HWindow, IDC_COMPARE_IGNORE_FILES);
    EnableWindow(GetDlgItem(HWindow, IDE_COMPARE_IGNORE_FILES), ignoreFiles);

    BOOL onePanelDirs = IsDlgButtonChecked(HWindow, IDC_COMPARE_ONE_PANEL_DIRS) == BST_CHECKED;
    EnableWindow(GetDlgItem(HWindow, IDC_COMPARE_IGNORE_DIRS), subdirs || onePanelDirs);
    if (!subdirs && !onePanelDirs)
        CheckDlgButton(HWindow, IDC_COMPARE_IGNORE_DIRS, BST_UNCHECKED);

    BOOL ignoreDirs = IsDlgButtonChecked(HWindow, IDC_COMPARE_IGNORE_DIRS);
    EnableWindow(GetDlgItem(HWindow, IDE_COMPARE_IGNORE_DIRS), ignoreDirs);
}

HDWP CCompareDirsDialog::OffsetControl(HDWP hdwp, int id, int yOffset)
{
    HWND hCtrl = GetDlgItem(HWindow, id);
    RECT r;
    GetWindowRect(hCtrl, &r);
    ScreenToClient(HWindow, (LPPOINT)&r);

    hdwp = HANDLES(DeferWindowPos(hdwp, hCtrl, NULL, r.left, r.top + yOffset, 0, 0, SWP_NOSIZE | SWP_NOZORDER));
    return hdwp;
}

void CCompareDirsDialog::DisplayMore(BOOL more)
{
    // hidden controls must be hidden to remove them from the tab order
    int controls[] = {IDC_COMPARE_SUBDIRS_ATTR, IDC_COMPARE_SUBDIRS_ATTR_DESCR,
                      IDC_COMPARE_ONE_PANEL_DIRS, IDC_COMPARE_MORE_OPTIONS, IDC_COMPARE_MORE_OPTIONS_SEP,
                      IDC_COMPARE_IGNORE_FILES, IDE_COMPARE_IGNORE_FILES, IDC_COMPARE_IGNORE_DIRS,
                      IDE_COMPARE_IGNORE_DIRS, IDC_FILEMASK_HINT, -1};

    int wndHeight = OriginalHeight;
    if (!more)
        wndHeight -= SpacerHeight;
    SetWindowPos(HWindow, NULL, 0, 0, OriginalWidth, wndHeight,
                 SWP_NOZORDER | SWP_NOMOVE);

    HWND hFocus = GetFocus();
    int i;
    for (i = 0; controls[i] != -1; i++)
    {
        HWND hCtrl = GetDlgItem(HWindow, controls[i]);
        if (!more && hCtrl == hFocus)
        {
            SendMessage(HWindow, DM_SETDEFID, IDOK, 0);
            SetFocus(GetDlgItem(HWindow, IDOK));
        }
        ShowWindow(hCtrl, more ? SW_SHOW : SW_HIDE);
    }

    int yOffset = more ? SpacerHeight : -SpacerHeight;

    HDWP hdwp = HANDLES(BeginDeferWindowPos(5));
    if (hdwp != NULL)
    {
        hdwp = OffsetControl(hdwp, IDC_COMPARE_BUTTONS_SEP, yOffset);
        hdwp = OffsetControl(hdwp, IDOK, yOffset);
        hdwp = OffsetControl(hdwp, IDCANCEL, yOffset);
        hdwp = OffsetControl(hdwp, IDC_MORE, yOffset);
        hdwp = OffsetControl(hdwp, IDHELP, yOffset);
        HANDLES(EndDeferWindowPos(hdwp));
    }
    CheckDlgButton(HWindow, IDC_MORE, more ? BST_CHECKED : BST_UNCHECKED);
    if (!more)
    {
        CheckDlgButton(HWindow, IDC_COMPARE_SUBDIRS_ATTR, BST_UNCHECKED);
        CheckDlgButton(HWindow, IDC_COMPARE_ONE_PANEL_DIRS, BST_UNCHECKED);
        CheckDlgButton(HWindow, IDC_COMPARE_IGNORE_FILES, BST_UNCHECKED);
        CheckDlgButton(HWindow, IDC_COMPARE_IGNORE_DIRS, BST_UNCHECKED);
    }
    Expanded = more;
}

INT_PTR
CCompareDirsDialog::DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {

    case WM_INITDIALOG:
    {
        new CButton(HWindow, IDC_MORE, BTF_MORE | BTF_CHECKBOX);
        CheckDlgButton(HWindow, IDC_MORE, BST_CHECKED);

        // now we're at full size => measure the dialog
        RECT r;
        GetWindowRect(HWindow, &r);
        OriginalWidth = r.right - r.left;
        OriginalHeight = r.bottom - r.top;
        // original button positions
        GetWindowRect(GetDlgItem(HWindow, IDOK), &r);
        ScreenToClient(HWindow, (LPPOINT)&r);
        OriginalButtonsY = r.top;
        // height of the spacer
        GetWindowRect(GetDlgItem(HWindow, IDC_CM_SPACER), &r);
        SpacerHeight = r.bottom - r.top;

        CHyperLink* hl = new CHyperLink(HWindow, IDC_FILEMASK_HINT, STF_DOTUNDERLINE);
        hl->SetActionShowHint(LoadStrW(IDS_MASKS_HINT));

        if (!Configuration.CompareMoreOptions)
            DisplayMore(FALSE);

        break;
    }

    case WM_COMMAND:
    {
        if (HIWORD(wParam) == BN_CLICKED)
        {
            switch (LOWORD(wParam))
            {
            case IDC_MORE:
            {
                DisplayMore(!Expanded /*, FALSE*/);
                break;
            }

            case IDC_COMPARE_IGNORE_FILES:
            case IDC_COMPARE_IGNORE_DIRS:
            case IDC_COMPARE_ONE_PANEL_DIRS:
            case IDC_COMPARE_SUBDIRS_ATTR:
            {
                if (!Expanded)
                    DisplayMore(TRUE /*, FALSE*/);
                break;
            }
            }

            EnableControls();

            // if the user clicks the checkbox to enable the mask, he probably wants to edit it
            if (LOWORD(wParam) == IDC_COMPARE_IGNORE_FILES)
            {
                if (IsDlgButtonChecked(HWindow, IDC_COMPARE_IGNORE_FILES))
                    SendMessage(HWindow, WM_NEXTDLGCTL, FALSE, FALSE); // focus to the mask
            }
            if (LOWORD(wParam) == IDC_COMPARE_IGNORE_DIRS)
            {
                if (IsDlgButtonChecked(HWindow, IDC_COMPARE_IGNORE_DIRS))
                    SendMessage(HWindow, WM_NEXTDLGCTL, FALSE, FALSE); // focus to the mask
            }
        }
        break;
    }
    }
    return CCommonDialog::DialogProc(uMsg, wParam, lParam);
}

//****************************************************************************
//
// CCmpDirProgressDialog
//

CCmpDirProgressDialog::CCmpDirProgressDialog(HWND hParent, BOOL hasProgress, CITaskBarList3* taskBarList3)
    : CCommonDialog(HLanguage, hasProgress ? IDD_CMPDIR_PROGRESS : IDD_CMPDIR_PROGRESS2, hParent, ooStatic)
{
    HasProgress = hasProgress;
    DelayedSource.clear();
    DelayedTarget.clear();
    DelayedSourceDirty = FALSE;
    DelayedTargetDirty = FALSE;
    Source = NULL;
    Target = NULL;
    Progress = NULL;
    TotalProgress = NULL;
    FileSize = CQuadWord(1, 0); // protection against division by zero
    ActualFileSize = CQuadWord(0, 0);
    TotalSize = CQuadWord(1, 0); // protection against division by zero
    ActualTotalSize = CQuadWord(0, 0);
    Cancel = FALSE;
    LastTickCount = 0;
    TaskBarList3 = taskBarList3;
}

void CCmpDirProgressDialog::SetSource(const wchar_t* text)
{
    DelayedSource = text != NULL ? text : L"";
    DelayedSourceDirty = TRUE;
}

void CCmpDirProgressDialog::SetTarget(const wchar_t* text)
{
    DelayedTarget = text != NULL ? text : L"";
    DelayedTargetDirty = TRUE;
}

void CCmpDirProgressDialog::SetFileSize(const CQuadWord& size)
{
    FileSize = max(CQuadWord(1, 0), size); // protection against division by zero
}

void CCmpDirProgressDialog::SetActualFileSize(const CQuadWord& size)
{
    ActualFileSize = max(CQuadWord(0, 0), size);
    SizeIsDirty = TRUE;
}

void CCmpDirProgressDialog::SetTotalSize(const CQuadWord& size)
{
    TotalSize = max(CQuadWord(1, 0), size); // protection against division by zero
}

void CCmpDirProgressDialog::SetActualTotalSize(const CQuadWord& size)
{
    ActualTotalSize = max(CQuadWord(0, 0), size);
    SizeIsDirty = TRUE;
}

void CCmpDirProgressDialog::GetActualTotalSize(CQuadWord& size)
{
    size = ActualTotalSize;
}

void CCmpDirProgressDialog::AddSize(const CQuadWord& size)
{
    CALL_STACK_MESSAGE_NONE;
    if (size != CQuadWord(0, 0))
    {
        ActualFileSize += size;
        ActualTotalSize += size; //CQuadWord(size, 0)
        SizeIsDirty = TRUE;
    }
}

BOOL CCmpDirProgressDialog::Continue()
{
    MSG msg;
    while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) // give the user a moment ...
    {
        if (!IsWindow(HWindow) || !IsDialogMessage(HWindow, &msg))
        {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    // redraw modified data every 100 ms (text + progress bars)
    DWORD ticks = GetTickCount();
    if (ticks - LastTickCount > 100)
    {
        FlushDataToControls();
        LastTickCount = GetTickCount();
    }

    return !Cancel;
}

void CCmpDirProgressDialog::FlushDataToControls()
{
    // text
    if (DelayedSourceDirty && Source != NULL)
    {
        Source->SetText(DelayedSource.c_str());
        DelayedSourceDirty = FALSE;
    }

    // text
    if (DelayedTargetDirty && Target != NULL)
    {
        Target->SetText(DelayedTarget.c_str());
        DelayedTargetDirty = FALSE;
    }

    // progress bar
    if (SizeIsDirty)
    {
        if (Progress != NULL)
            Progress->SetProgress2(ActualFileSize, FileSize);
        if (TotalProgress != NULL)
            TotalProgress->SetProgress2(ActualTotalSize, TotalSize);
        if (TotalProgress != NULL)
            TaskBarList3->SetProgress2(ActualTotalSize, TotalSize);
        SizeIsDirty = FALSE;
    }
}

INT_PTR
CCmpDirProgressDialog::DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
    case WM_INITDIALOG:
    {
        if (HasProgress)
        {
            if ((Progress = new CProgressBar(HWindow, IDF_OPERATION)) == NULL)
                TRACE_E(LOW_MEMORY);
            if ((TotalProgress = new CProgressBar(HWindow, IDF_SUMMARY)) == NULL)
                TRACE_E(LOW_MEMORY);
        }
        if ((Source = new CStaticText(HWindow, IDS_SOURCE, STF_PATH_ELLIPSIS | STF_CACHED_PAINT)) == NULL)
            TRACE_E(LOW_MEMORY);
        if ((Target = new CStaticText(HWindow, IDS_TARGET, STF_PATH_ELLIPSIS | STF_CACHED_PAINT)) == NULL)
            TRACE_E(LOW_MEMORY);

        // when opening the dialog set msgbox parent for plug-ins to this dialog (main thread only)
        // PluginMsgBoxParent is restored in CCommonDialog::DialogProc on WM_DESTROY
        if (MainThreadID == GetCurrentThreadId())
        {
            HOldPluginMsgBoxParent = PluginMsgBoxParent;
            PluginMsgBoxParent = HWindow;
        }

        FlushDataToControls();
        break;
    }

    case WM_DESTROY:
    {
        TaskBarList3->SetProgressState(TBPF_NOPROGRESS);
        break;
    }

    case WM_COMMAND:
    {
        // if the user clicked Cancel and it hasn't been confirmed yet, ask again
        if (LOWORD(wParam) == IDCANCEL && !Cancel)
        {
            // the Cancel button must be enabled
            if (IsWindowEnabled(GetDlgItem(HWindow, IDCANCEL)))
            {
                // redraw explicitly before showing the messagebox
                FlushDataToControls();

                // ask the user if he wants to interrupt the operation
                Cancel = (gPrompter->AskYesNo(LoadStrW(IDS_QUESTION), LoadStrW(IDS_CANCELOPERATION)).type == PromptResult::kYes);
            }
        }
        // do not let the command fall through, or the dialog will close
        return TRUE;
    }
    }
    return CCommonDialog::DialogProc(uMsg, wParam, lParam);
}

//****************************************************************************
//
// CExitingOpenSal
//

CExitingOpenSal::CExitingOpenSal(HWND hParent)
    : CCommonDialog(HLanguage, IDD_EXITINGOPENSAL, hParent)
{
    NextOpenedDlgIndex = 0;
}

INT_PTR
CExitingOpenSal::DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
    case WM_INITDIALOG:
    {
        SetTimer(HWindow, 666, 200, NULL);
        PostMessage(HWindow, WM_TIMER, 666, 0);
        break;
    }

    case WM_TIMER:
    {
        if (wParam == 666)
        {
            int c = ProgressDlgArray.RemoveFinishedDlgs();
            if (c == 0)
                EndDialog(HWindow, IDOK); // ending Salamander is possible
            else
            {
                // Entirely narrow and internally consistent, so it compiles
                // today and does not appear in the error list - and the UNICODE flip would turn
                // both SetDlgItemText/GetDlgItemText wide over char buffers. All local to this
                // dialog, so it is ours to widen. The 50 is a CHARACTER count in both forms.
                wchar_t num[50];
                _itow(c, num, 10);
                wchar_t buf[50];
                GetDlgItemTextW(HWindow, IDT_RUNNINGOPERS, buf, 50);
                buf[49] = 0;
                if (wcscmp(buf, num) != 0)
                    SetDlgItemTextW(HWindow, IDT_RUNNINGOPERS, num);
            }
            return TRUE;
        }
        break;
    }

    case WM_COMMAND:
    {
        switch (LOWORD(wParam))
        {
        case IDB_CANCELALLOPERS:
        {
            ProgressDlgArray.PostCancelToAllDlgs();
            return TRUE;
        }

        case IDB_FOCUSNEXTOPER:
        {
            HWND dlg = ProgressDlgArray.GetNextOpenedDlg(&NextOpenedDlgIndex);
            if (dlg != NULL)
                PostMessage(dlg, WM_USER_FOCUSPROGRDLG, 0, 0);
            return TRUE;
        }
        }
        break;
    }

    case WM_DESTROY:
    {
        KillTimer(HWindow, 666);
        break;
    }
    }
    return CCommonDialog::DialogProc(uMsg, wParam, lParam);
}

//
// ****************************************************************************
// CConfirmADSLossDlg
//

CConfirmADSLossDlg::CConfirmADSLossDlg(HWND parent, BOOL isFile, const wchar_t* name,
                                       const wchar_t* streams, BOOL isMove) : CCommonDialog(HLanguage, IDD_CONFIRMADSLOSS, parent)
{
    IsFile = isFile;
    IsMove = isMove;
    Name = name;
    Streams = streams;
}

INT_PTR
CConfirmADSLossDlg::DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    CALL_STACK_MESSAGE4("CConfirmADSLossDlg::DialogProc(0x%X, 0x%IX, 0x%IX)", uMsg, wParam, lParam);
    switch (uMsg)
    {
    case WM_INITDIALOG:
    {
        CStaticText* name;
        if ((name = new CStaticText(HWindow, IDS_FILENAME, STF_PATH_ELLIPSIS)) != NULL)
        {
            name->SetTextToDblQuotesIfNeeded(Name);
        }
        else
            TRACE_E(LOW_MEMORY);

        SetDlgItemTextW(HWindow, IDE_ALTSTREAMS, Streams);

        if (IsFile)
            SetWindowTextW(GetDlgItem(HWindow, IDT_FILEORDIR), LoadStrW(IDS_FILETITLE));

        int resId;
        if (IsMove)
            resId = IsFile ? IDS_CONFADSLOSS_WARNING_MOVE_FILE : IDS_CONFADSLOSS_WARNING_MOVE_DIR;
        else
            resId = IsFile ? IDS_CONFADSLOSS_WARNING_COPY_FILE : IDS_CONFADSLOSS_WARNING_COPY_DIR;
        SetWindowTextW(GetDlgItem(HWindow, IDS_ERROR), LoadStrW(resId));
        break;
    }

    case WM_COMMAND:
    {
        if (LOWORD(wParam) == IDB_SKIP || LOWORD(wParam) == IDB_SKIPALL ||
            LOWORD(wParam) == IDB_ALL || LOWORD(wParam) == IDYES || LOWORD(wParam) == IDNO)
        {
            if (Modal)
                EndDialog(HWindow, LOWORD(wParam));
            else
                DestroyWindow(HWindow);
            return TRUE;
        }
        break;
    }
    }

    return CCommonDialog::DialogProc(uMsg, wParam, lParam);
}

//
// ****************************************************************************
// CConfirmLinkTgtCopyDlg
//

CConfirmLinkTgtCopyDlg::CConfirmLinkTgtCopyDlg(HWND parent, const wchar_t* name, const wchar_t* details) : CCommonDialog(HLanguage, IDD_CONFIRMLINKTGTCOPY, parent)
{
    Name = name;
    Details = details;
}

INT_PTR
CConfirmLinkTgtCopyDlg::DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    CALL_STACK_MESSAGE4("CConfirmLinkTgtCopyDlg::DialogProc(0x%X, 0x%IX, 0x%IX)", uMsg, wParam, lParam);
    switch (uMsg)
    {
    case WM_INITDIALOG:
    {
        CStaticText* name = new CStaticText(HWindow, IDS_FILENAME, STF_PATH_ELLIPSIS);
        name->SetTextToDblQuotesIfNeeded(Name);
        SetDlgItemTextW(HWindow, IDS_DETAILS, Details);
        break;
    }

    case WM_COMMAND:
    {
        if (LOWORD(wParam) == IDB_SKIP || LOWORD(wParam) == IDB_SKIPALL ||
            LOWORD(wParam) == IDB_ALL || LOWORD(wParam) == IDYES || LOWORD(wParam) == IDNO)
        {
            if (Modal)
                EndDialog(HWindow, LOWORD(wParam));
            else
                DestroyWindow(HWindow);
            return TRUE;
        }
        break;
    }
    }

    return CCommonDialog::DialogProc(uMsg, wParam, lParam);
}

//
// ****************************************************************************
// CConfirmEncryptionLossDlg
//

CConfirmEncryptionLossDlg::CConfirmEncryptionLossDlg(HWND parent, BOOL isFile, const wchar_t* name,
                                                     BOOL isMove) : CCommonDialog(HLanguage, IDD_CONFIRMENCRYPTLOSS, parent)
{
    IsFile = isFile;
    IsMove = isMove;
    Name = name;
}

INT_PTR
CConfirmEncryptionLossDlg::DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    CALL_STACK_MESSAGE4("CConfirmEncryptionLossDlg::DialogProc(0x%X, 0x%IX, 0x%IX)", uMsg, wParam, lParam);
    switch (uMsg)
    {
    case WM_INITDIALOG:
    {
        CStaticText* name;
        if ((name = new CStaticText(HWindow, IDS_FILENAME, STF_PATH_ELLIPSIS)) != NULL)
        {
            name->SetTextToDblQuotesIfNeeded(Name);
        }
        else
            TRACE_E(LOW_MEMORY);

        if (IsFile)
            SetWindowTextW(GetDlgItem(HWindow, IDT_FILEORDIR), LoadStrW(IDS_FILETITLE));

        int resId;
        if (IsMove)
            resId = IsFile ? IDS_CONFENCLOSS_WARNING_MOVE_FILE : IDS_CONFENCLOSS_WARNING_MOVE_DIR;
        else
            resId = IsFile ? IDS_CONFENCLOSS_WARNING_COPY_FILE : IDS_CONFENCLOSS_WARNING_COPY_DIR;
        SetWindowTextW(GetDlgItem(HWindow, IDS_ERROR), LoadStrW(resId));
        break;
    }

    case WM_COMMAND:
    {
        if (LOWORD(wParam) == IDB_SKIP || LOWORD(wParam) == IDB_SKIPALL ||
            LOWORD(wParam) == IDB_ALL || LOWORD(wParam) == IDYES || LOWORD(wParam) == IDNO)
        {
            if (Modal)
                EndDialog(HWindow, LOWORD(wParam));
            else
                DestroyWindow(HWindow);
            return TRUE;
        }
        break;
    }
    }

    return CCommonDialog::DialogProc(uMsg, wParam, lParam);
}

//
// ****************************************************************************
// CErrorReadingADSDlg
//

// unicodeWnd=TRUE is REQUIRED, not decorative: the title can now be wchar_t*
// (titleW), but SetWindowTextW on an ANSI-class dialog is re-narrowed by USER32. The opt-in
// creates it with DialogBoxParamW so the wide title actually reaches the screen. Same
// mechanism as CFileErrorDlg.
CErrorReadingADSDlg::CErrorReadingADSDlg(HWND parent, const wchar_t* file, const wchar_t* error,
                                         const wchar_t* title)
    : CCommonDialog(HLanguage, IDD_CANNOTGETADSINFO, parent, ooStandard, NULL)
{
    File = file;
    Error = error;
    Title = title;
}

INT_PTR
CErrorReadingADSDlg::DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    CALL_STACK_MESSAGE4("CErrorReadingADSDlg::DialogProc(0x%X, 0x%IX, 0x%IX)", uMsg, wParam, lParam);
    switch (uMsg)
    {
    case WM_INITDIALOG:
    {
        if (Title != NULL)
        {
            SetWindowTextW(HWindow, Title);
            Title = NULL; // comes from LoadStrW; its lifetime ends soon (it will be overwritten), so we null it out
        }

        CStaticText* name;
        if ((name = new CStaticText(HWindow, IDS_FILENAME, STF_PATH_ELLIPSIS)) != NULL)
        {
            name->SetTextToDblQuotesIfNeeded(File);
        }
        else
            TRACE_E(LOW_MEMORY);

        SetWindowTextW(GetDlgItem(HWindow, IDS_ERROR), Error);

        break;
    }

    case WM_COMMAND:
    {
        if (LOWORD(wParam) == IDB_IGNORE || LOWORD(wParam) == IDB_IGNOREALL ||
            LOWORD(wParam) == IDRETRY)
        {
            if (Modal)
                EndDialog(HWindow, LOWORD(wParam));
            else
                DestroyWindow(HWindow);
            return TRUE;
        }
        break;
    }
    }

    return CCommonDialog::DialogProc(uMsg, wParam, lParam);
}

//
// ****************************************************************************
// CErrorSettingAttrsDlg
//

CErrorSettingAttrsDlg::CErrorSettingAttrsDlg(HWND parent, const wchar_t* file, DWORD neededAttrs,
                                             DWORD currentAttrs) : CCommonDialog(HLanguage, IDD_CANNOTSETATTRSINFO, parent)
{
    File = file;
    NeededAttrs = neededAttrs;
    CurrentAttrs = currentAttrs;
}

void GetAttrsStringW(wchar_t* text, DWORD attrs)
{
    // if we support showing another attribute,
    // InternalGetAttr() and DISPLAYED_ATTRIBUTES mask need to be extended
    int l = 0;
    if (attrs & FILE_ATTRIBUTE_READONLY)
        text[l++] = L'R';
    if (attrs & FILE_ATTRIBUTE_HIDDEN)
        text[l++] = L'H';
    if (attrs & FILE_ATTRIBUTE_SYSTEM)
        text[l++] = L'S';
    if (attrs & FILE_ATTRIBUTE_ARCHIVE)
        text[l++] = L'A';
    if (attrs & FILE_ATTRIBUTE_TEMPORARY)
        text[l++] = L'T';
    if (attrs & FILE_ATTRIBUTE_COMPRESSED)
        text[l++] = L'C';
    if (attrs & FILE_ATTRIBUTE_ENCRYPTED)
        text[l++] = L'E';
    if (attrs & FILE_ATTRIBUTE_OFFLINE)
        text[l++] = L'O';
    text[l] = 0;
}

INT_PTR
CErrorSettingAttrsDlg::DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    CALL_STACK_MESSAGE4("CErrorSettingAttrsDlg::DialogProc(0x%X, 0x%IX, 0x%IX)", uMsg, wParam, lParam);
    switch (uMsg)
    {
    case WM_INITDIALOG:
    {
        CStaticText* name;
        if ((name = new CStaticText(HWindow, IDS_FILENAME, STF_PATH_ELLIPSIS)) != NULL)
        {
            name->SetTextToDblQuotesIfNeeded(File);
        }
        else
            TRACE_E(LOW_MEMORY);

        wchar_t text[20];
        GetAttrsStringW(text, NeededAttrs);
        SetWindowTextW(GetDlgItem(HWindow, IDS_NEEDEDATTRS), text);
        GetAttrsStringW(text, CurrentAttrs);
        SetWindowTextW(GetDlgItem(HWindow, IDS_CURRENTATTRS), text);
        break;
    }

    case WM_COMMAND:
    {
        if (LOWORD(wParam) == IDB_IGNORE || LOWORD(wParam) == IDB_IGNOREALL)
        {
            if (Modal)
                EndDialog(HWindow, LOWORD(wParam));
            else
                DestroyWindow(HWindow);
            return TRUE;
        }
        break;
    }
    }

    return CCommonDialog::DialogProc(uMsg, wParam, lParam);
}

//
// ****************************************************************************
// CErrorCopyingPermissionsDlg
//

CErrorCopyingPermissionsDlg::CErrorCopyingPermissionsDlg(HWND parent, const wchar_t* sourceFile,
                                                         const wchar_t* targetFile, DWORD error) : CCommonDialog(HLanguage, IDD_CANNOTCOPYPERMISSIONS, parent)
{
    SourceFile = sourceFile;
    TargetFile = targetFile;
    Error = error;
}

INT_PTR
CErrorCopyingPermissionsDlg::DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    CALL_STACK_MESSAGE4("CErrorCopyingPermissionsDlg::DialogProc(0x%X, 0x%IX, 0x%IX)", uMsg, wParam, lParam);
    switch (uMsg)
    {
    case WM_INITDIALOG:
    {
        CStaticText* name;
        if ((name = new CStaticText(HWindow, IDS_SOURCENAME, STF_PATH_ELLIPSIS)) != NULL)
        {
            name->SetTextToDblQuotesIfNeeded(SourceFile);
        }
        else
            TRACE_E(LOW_MEMORY);

        if ((name = new CStaticText(HWindow, IDS_TARGETNAME, STF_PATH_ELLIPSIS)) != NULL)
        {
            name->SetTextToDblQuotesIfNeeded(TargetFile);
        }
        else
            TRACE_E(LOW_MEMORY);

        SetWindowTextW(GetDlgItem(HWindow, IDS_ERROR),
                       Error != NO_ERROR ? GetErrorTextOwned(Error).c_str() : LoadStrW(IDS_VIEWER_UNKNOWNERR));
        break;
    }

    case WM_COMMAND:
    {
        if (LOWORD(wParam) == IDB_IGNORE || LOWORD(wParam) == IDB_IGNOREALL)
        {
            if (Modal)
                EndDialog(HWindow, LOWORD(wParam));
            else
                DestroyWindow(HWindow);
            return TRUE;
        }
        break;
    }
    }

    return CCommonDialog::DialogProc(uMsg, wParam, lParam);
}

//
// ****************************************************************************
// CErrorCopyingDirTimeDlg
//

CErrorCopyingDirTimeDlg::CErrorCopyingDirTimeDlg(HWND parent, const wchar_t* targetFile, DWORD error) : CCommonDialog(HLanguage, IDD_CANNOTCOPYDIRTIME, parent)
{
    TargetFile = targetFile;
    Error = error;
}

INT_PTR
CErrorCopyingDirTimeDlg::DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    CALL_STACK_MESSAGE4("CErrorCopyingDirTimeDlg::DialogProc(0x%X, 0x%IX, 0x%IX)", uMsg, wParam, lParam);
    switch (uMsg)
    {
    case WM_INITDIALOG:
    {
        CStaticText* name;
        if ((name = new CStaticText(HWindow, IDS_TARGETNAME, STF_PATH_ELLIPSIS)) != NULL)
        {
            name->SetTextToDblQuotesIfNeeded(TargetFile);
        }
        else
            TRACE_E(LOW_MEMORY);

        SetWindowTextW(GetDlgItem(HWindow, IDS_ERROR),
                       Error != NO_ERROR ? GetErrorTextOwned(Error).c_str() : LoadStrW(IDS_VIEWER_UNKNOWNERR));
        break;
    }

    case WM_COMMAND:
    {
        if (LOWORD(wParam) == IDB_IGNORE || LOWORD(wParam) == IDB_IGNOREALL)
        {
            if (Modal)
                EndDialog(HWindow, LOWORD(wParam));
            else
                DestroyWindow(HWindow);
            return TRUE;
        }
        break;
    }
    }

    return CCommonDialog::DialogProc(uMsg, wParam, lParam);
}

//
// ****************************************************************************
// CDriveSelectErrDlg
//

CDriveSelectErrDlg::CDriveSelectErrDlg(HWND parent, const wchar_t* errText, const wchar_t* drvPath) : CCommonDialog(HLanguage, IDD_DRIVESELECTERR, parent)
{
    ErrText = errText;
    DrvPath = drvPath != nullptr ? drvPath : L"";
    CounterForAllowedUseOfTimer = 5 * 60; // try for at most 5 minutes, then let the user press Retry (prevents drive hammering)
}

INT_PTR
CDriveSelectErrDlg::DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    CALL_STACK_MESSAGE4("CDriveSelectErrDlg::DialogProc(0x%X, 0x%IX, 0x%IX)", uMsg, wParam, lParam);
    switch (uMsg)
    {
    case WM_INITDIALOG:
    {
        LastDriveSelectErrDlgHWnd = HWindow;
        HICON hIcon = HANDLES(LoadIcon(NULL, IDI_EXCLAMATION));
        SendDlgItemMessage(HWindow, IDI_EXCLAMATIONICON, STM_SETICON, (WPARAM)hIcon, 0);
        SetDlgItemTextW(HWindow, IDT_ERRTEXT, ErrText);
        MessageBeep(MB_ICONEXCLAMATION);

        // check whether periodic drive readiness tests make sense (except for noisy floppies and network drives which may be slow)
        BOOL setTimer = TRUE;
        UINT drvType = MyGetDriveTypeW(DrvPath.c_str());
        // WARNING: unfortunately mountpoints return DRIVE_NO_ROOT_DIR when no media is inserted, unbelievable ... so
        // I had to keep periodic tests even when drvType == DRIVE_NO_ROOT_DIR
        if (drvType == DRIVE_REMOVABLE || drvType == DRIVE_CDROM || drvType == DRIVE_NO_ROOT_DIR)
        {
            std::wstring root = GetRootPath(DrvPath.c_str());
            switch (GetDriveTypeW(root.c_str()))
            {
            case DRIVE_REMOVABLE: // check whether it's a floppy disk (probably can't be in a mount point, so this is enough)
            {
                int drv = UpperCase[root[0]] - 'A' + 1;
                if (drv >= 1 && drv <= 26) // perform a range check just to be sure
                {
                    DWORD medium = GetDriveFormFactor(drv);
                    switch (medium)
                    {
                    case 1:
                    case 350:
                    case 525:
                    case 800:
                        setTimer = FALSE;
                        break; // it's a floppy
                    }
                }
                break;
            }

            case DRIVE_FIXED: // mount-point, determine the root of removable drive
            {
                std::wstring reparsePoint;
                if (!GetCurrentLocalReparsePointW(DrvPath.c_str(), reparsePoint))
                    setTimer = FALSE; // can't be a mount-point, no periodic tests
                // written on both paths: the wide form yields the plain root on failure,
                // exactly as the narrow one did, and 'root' is read either way below
                root = reparsePoint;
                break;
            }

            case DRIVE_NO_ROOT_DIR:
                setTimer = FALSE;
                break; // no idea what's going on, we better skip periodic tests
            }
            DrvPath = root;
        }
        else
            setTimer = FALSE; // most likely a network connection

        if (setTimer)
            SetTimer(HWindow, 3725, 1000, NULL);

        break;
    }

    case WM_TIMER:
    {
        if (wParam == 3725 && GetForegroundWindow() == HWindow) // check if the drive isn't accessible already
        {
            KillTimer(HWindow, 3725);

            // drive accessibility is tested using FindFirstFile because SalGetFileAttributes
            // always succeeds on junction-points (directory attributes are unrelated to content)
            BOOL ok = FALSE;
            IFileEnumerator* enumerator = gFileEnumerator != nullptr ? gFileEnumerator : GetWin32FileEnumerator();
            HENUM search = enumerator->StartEnum(DrvPath.c_str(), L"*");
            if (search == INVALID_HENUM)
            {
                DWORD err = GetLastError();
                if (err == ERROR_FILE_NOT_FOUND || err == ERROR_NO_MORE_FILES)
                    ok = TRUE;
            }
            else
            {
                ok = TRUE;
                enumerator->EndEnum(search);
            }

            if (ok)
                PostMessage(HWindow, WM_COMMAND, IDRETRY, 0);
            else
            {
                if (--CounterForAllowedUseOfTimer > 0)
                    SetTimer(HWindow, 3725, 1000, NULL);
            }
        }
        break;
    }

    case WM_COMMAND:
    {
        if (LOWORD(wParam) == IDRETRY)
        {
            if (Modal)
                EndDialog(HWindow, LOWORD(wParam));
            else
                DestroyWindow(HWindow);
            return TRUE;
        }
        break;
    }

    case WM_DESTROY:
    {
        KillTimer(HWindow, 3725);
        LastDriveSelectErrDlgHWnd = NULL;
        break;
    }
    }
    return CCommonDialog::DialogProc(uMsg, wParam, lParam);
}

//
// ****************************************************************************
// CCfgPageIconOvrls
//

CCfgPageIconOvrls::CCfgPageIconOvrls()
    : CCommonPropSheetPage(NULL, HLanguage, IDD_CFGPAGE_ICONOVRLS, IDD_CFGPAGE_ICONOVRLS, PSP_USETITLE, NULL)
{
    HListView = NULL;
}

void CCfgPageIconOvrls::Transfer(CTransferInfo& ti)
{
    BOOL oldEnableCustomIconOverlays = Configuration.EnableCustomIconOverlays;
    ti.CheckBox(IDC_ICONOVRLS_ENABLE, Configuration.EnableCustomIconOverlays);
    if (ti.Type == ttDataToWindow)
    {
        int i;
        for (i = 0; i < ListOfShellIconOverlays.Count; i++)
        {
            CShellIconOverlayItem2* item = ListOfShellIconOverlays[i];
            // LVITEMW + LVM_INSERTITEMW explicitly, same reason as the Connections list above:
            // this build does not define _UNICODE, so the plain macros resolve to the A form.
            LVITEMW lvi;
            lvi.mask = LVIF_TEXT;
            lvi.iItem = i;
            lvi.iSubItem = 0;
            lvi.pszText = const_cast<wchar_t*>(item->IconOverlayName.c_str());
            SendMessageW(HListView, LVM_INSERTITEMW, 0, (LPARAM)&lvi);

            ListView_SetItemTextW(HListView, i, 1, const_cast<wchar_t*>(item->IconOverlayDescr.c_str()));

            UINT state = INDEXTOSTATEIMAGEMASK((!IsNameInListOfDisabledCustomIconOverlays(item->IconOverlayName.c_str()) ? 2 : 1));
            ListView_SetItemState(HListView, i, state, LVIS_STATEIMAGEMASK);
        }
        // set column widths
        ListView_SetColumnWidth(HListView, 0, LVSCW_AUTOSIZE_USEHEADER);
        ListView_SetColumnWidth(HListView, 1, LVSCW_AUTOSIZE_USEHEADER);

        DWORD state = LVIS_SELECTED | LVIS_FOCUSED;
        ListView_SetItemState(HListView, 0, state, state);
        ListView_EnsureVisible(HListView, 0, FALSE);
        EnableControls();
    }
    else
    {
        wchar_t* oldDisabledCustomIconOverlays = Configuration.DisabledCustomIconOverlays;
        Configuration.DisabledCustomIconOverlays = NULL;
        int i;
        for (i = 0; i < ListOfShellIconOverlays.Count; i++)
        {
            if (ListView_GetItemState(HListView, i, LVIS_STATEIMAGEMASK) == INDEXTOSTATEIMAGEMASK(1))
            { // unchecked checkbox -> add the name to the list of disabled icon overlay handlers
                AddToListOfDisabledCustomIconOverlays(ListOfShellIconOverlays[i]->IconOverlayName.c_str());
            }
        }
        if (oldEnableCustomIconOverlays != Configuration.EnableCustomIconOverlays ||
            wcscmp(oldDisabledCustomIconOverlays != NULL ? oldDisabledCustomIconOverlays : L"",
                   Configuration.DisabledCustomIconOverlays != NULL ? Configuration.DisabledCustomIconOverlays : L"") != 0)
        { // configuration change -> notify that it takes effect after Salamander restarts
            gPrompter->ShowInfo(LoadStrW(IDS_INFOTITLE), LoadStrW(IDS_ICONOVRLS_CHANGE));
        }
        if (oldDisabledCustomIconOverlays != NULL)
            free(oldDisabledCustomIconOverlays);
    }
}

void CCfgPageIconOvrls::EnableControls()
{
    BOOL enable = IsDlgButtonChecked(HWindow, IDC_ICONOVRLS_ENABLE) == BST_CHECKED;
    if ((IsWindowEnabled(HListView) != 0) != enable)
        EnableWindow(HListView, enable);
}

INT_PTR
CCfgPageIconOvrls::DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    CALL_STACK_MESSAGE4("CCfgPageIconOvrls::DialogProc(0x%X, 0x%IX, 0x%IX)", uMsg, wParam, lParam);
    switch (uMsg)
    {
    case WM_INITDIALOG:
    {
        HListView = GetDlgItem(HWindow, IDC_ICONOVRLS_LIST);
        new CToolbarHeader(HWindow, IDC_ICONOVRLS_HEADER, HListView, 0);

        DWORD exFlags = LVS_EX_FULLROWSELECT | LVS_EX_CHECKBOXES;
        DWORD origFlags = ListView_GetExtendedListViewStyle(HListView);
        ListView_SetExtendedListViewStyle(HListView, origFlags | exFlags); // 4.71

        // Fill the list view with dynamically owned UTF-16 resource text.
        LVCOLUMNW lvc;
        lvc.mask = LVCF_TEXT | LVCF_FMT;
        std::wstring columnText = LoadStrOwned(IDS_ICONOVRLS_NAME);
        lvc.pszText = columnText.data();
        lvc.fmt = LVCFMT_LEFT;
        SendMessageW(HListView, LVM_INSERTCOLUMNW, 0, (LPARAM)&lvc);

        lvc.mask |= LVCF_SUBITEM;
        columnText = LoadStrOwned(IDS_ICONOVRLS_DESCR);
        lvc.pszText = columnText.data();
        lvc.iSubItem = 1;
        SendMessageW(HListView, LVM_INSERTCOLUMNW, 1, (LPARAM)&lvc);

        // dialog elements should stretch depending on its size, set split controls
        ElasticVerticalLayout(1, IDC_ICONOVRLS_LIST);

        break;
    }

    case WM_SYSCOLORCHANGE:
    {
        DarkMode_ApplyListTreeThemeRecursive(HListView);
        break;
    }

    case WM_COMMAND:
    {
        if (HIWORD(wParam) == BN_CLICKED && LOWORD(wParam) == IDC_ICONOVRLS_ENABLE)
            EnableControls();
        break;
    }
    }

    return CCommonPropSheetPage::DialogProc(uMsg, wParam, lParam);
}
