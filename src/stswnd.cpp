// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"

#include "toolbar.h"
#include "cfgdlg.h"
#include "mainwnd.h"
#include "stswnd.h"
#include "plugins.h"
#include "fileswnd.h"
#include "shellib.h"
#include "svg.h"
#include "darkmode.h"
#include "common/Win32TextCodec.h"
#include "common/clipboard/ClipboardTextPayload.h"
#include "common/clipboard/HDropSelection.h"
#include "common/fsutil.h"
#include "common/IFileSystem.h"
#include "common/unicode/helpers.h"
#include "salshlib.h"

static COLORREF GetStatusBkColor(BOOL activeCaption, BOOL showPanelCaption)
{
    if (!DarkMode_ShouldUseDark())
    {
        if (showPanelCaption)
            return GetCOLORREF(CurrentColors[activeCaption ? ACTIVE_CAPTION_BK : INACTIVE_CAPTION_BK]);
        return GetSysColor(COLOR_BTNFACE);
    }
    if (showPanelCaption)
        return activeCaption ? RGB(45, 45, 48) : RGB(37, 37, 38);
    return RGB(45, 45, 48);
}

static COLORREF GetStatusTextColor(BOOL activeCaption, BOOL showPanelCaption, BOOL hot)
{
    if (!DarkMode_ShouldUseDark())
    {
        if (hot)
        {
            if (showPanelCaption)
                return GetCOLORREF(CurrentColors[activeCaption ? HOT_ACTIVE : HOT_INACTIVE]);
            return GetCOLORREF(CurrentColors[HOT_PANEL]);
        }
        if (showPanelCaption)
            return GetCOLORREF(CurrentColors[activeCaption ? ACTIVE_CAPTION_FG : INACTIVE_CAPTION_FG]);
        return GetSysColor(COLOR_BTNTEXT);
    }

    if (hot)
        return activeCaption ? RGB(170, 200, 255) : RGB(140, 170, 220);
    if (showPanelCaption)
        return activeCaption ? RGB(235, 235, 235) : RGB(180, 180, 180);
    return RGB(232, 232, 232);
}

static void FillRectSolid(HDC hDC, const RECT* rect, COLORREF color)
{
    HGDIOBJ oldBrush = SelectObject(hDC, GetStockObject(DC_BRUSH));
    SetDCBrushColor(hDC, color);
    FillRect(hDC, rect, (HBRUSH)GetStockObject(DC_BRUSH));
    SelectObject(hDC, oldBrush);
}

//
// ****************************************************************************
// CStatusWindow
//

CStatusWindow::CStatusWindow(CFilesWindow* filesWindow, int border, CObjectOrigin origin) : CWindow(origin), HotTrackItems(10, 5)
{
    CALL_STACK_MESSAGE_NONE
    TextAssigned = FALSE;
    PathLen = -1;
    TextLen = 0;
    Border = border;
    SizeAssigned = FALSE;
    Hidden = FALSE;
    History = FALSE;
    ShowThrobber = FALSE;
    DelayedThrobber = FALSE;
    DelayedThrobberShowTime = 0;
    Throbber = FALSE;
    ThrobberTooltip.clear();
    ThrobberID = -1;
    Security = sisNone;
    SecurityTooltip.clear();
    HiddenFilesCount = 0;
    HiddenDirsCount = 0;
    Left = TRUE; // dummy
    ToolBar = NULL;
    ToolBarWidth = 0;
    EllipsedChars = -1;
    EllipsedWidth = -1;
    NeedToInvalidate = FALSE;
    Width = 0;
    Height = 0;
    HotItem = NULL;
    LastHotItem = NULL;
    HotSize = FALSE;
    HotHistory = FALSE;
    HotZoom = FALSE;
    HotHidden = FALSE;
    HotSecurity = FALSE;
    MouseCaptured = FALSE;
    FilesWindow = filesWindow;
    LButtonDown = FALSE;
    RButtonDown = FALSE;
    IDropTargetPtr = NULL;
}

CStatusWindow::~CStatusWindow()
{
    CALL_STACK_MESSAGE1("CStatusWindow::~CStatusWindow()");
    // UTF-16 text and measurement storage are dynamically owned containers.
    if (ToolBar != NULL)
    {
        if (ToolBar->HWindow != NULL)
            ToolBar->DetachWindow();
        delete ToolBar;
    }
}

BOOL CStatusWindow::SetSubTexts(const sally::unicode::WideTextRange* subTexts,
                                size_t subTextsCount)
{
    CALL_STACK_MESSAGE2("CStatusWindow::SetSubTexts(, %Iu)", subTextsCount);
    HotItem = NULL;
    LastHotItem = NULL;

    if (subTextsCount != 0 && subTexts == NULL)
        return FALSE;
    if (subTextsCount == 0)
    {
        SubTexts.clear();
        return TRUE;
    }

    try
    {
        std::vector<sally::unicode::WideTextRange> staged(
            subTexts, subTexts + subTextsCount);
        SubTexts.swap(staged);
    }
    catch (const std::bad_alloc&)
    {
        TRACE_E(LOW_MEMORY);
        return FALSE;
    }
    catch (const std::length_error&)
    {
        TRACE_E(LOW_MEMORY);
        return FALSE;
    }

    // build array for cursor tracking
    BuildHotTrackItems();

    return TRUE;
}

BOOL CStatusWindow::SetText(const wchar_t* txt, int pathLen)
{
    std::wstring newText = txt != NULL ? txt : L"";
    CALL_STACK_MESSAGE2("CStatusWindow::SetText(, %d)", pathLen);
    if (newText.size() >= static_cast<size_t>((std::numeric_limits<int>::max)()))
    {
        SetLastError(ERROR_FILENAME_EXCED_RANGE);
        return FALSE;
    }
    if (TextAssigned && TextW == newText)
    {
        PathLen = pathLen;
        return TRUE;
    }

    try
    {
        AlpDX.resize(newText.empty() ? 1 : newText.size() + 1);
    }
    catch (const std::bad_alloc&)
    {
        TRACE_E(LOW_MEMORY);
        return FALSE;
    }
    catch (const std::length_error&)
    {
        TRACE_E(LOW_MEMORY);
        return FALSE;
    }

    HotTrackItemsMeasured = FALSE;
    HotItem = NULL;
    LastHotItem = NULL;

    TextW.swap(newText);
    TextAssigned = TRUE;

    PathLen = pathLen;
    TextLen = (int)TextW.size();

    SubTexts.clear();

    BuildHotTrackItems();

    if (MouseCaptured)
        WindowProc(WM_MOUSELEAVE, 0, 0);

    if (HWindow != NULL)
        InvalidateRect(HWindow, NULL, FALSE);
    return TRUE;
}

void CStatusWindow::BuildHotTrackItems()
{
    CALL_STACK_MESSAGE1("CStatusWindow::BuildHotTrackItems()");

    HDC dc = HANDLES(GetDC(HWindow));
    HFONT oldFont = (HFONT)SelectObject(dc, EnvFont);

    HotItem = NULL;
    LastHotItem = NULL;
    if (Border == blTop)
    {
        // populate HotTrackItems
        CHotTrackItem item;
        HotTrackItems.DestroyMembers();
        {
            int pathLen = (PathLen != -1) ? PathLen : (int)TextW.size();
            SIZE s;
            GetTextExtentExPointW(dc, TextW.c_str(), TextLen, 0, NULL, AlpDX.data(), &s);

            if (FilesWindow->Is(ptDisk) || FilesWindow->Is(ptZIPArchive))
            {
                int chars;
                if (TextW.size() >= 2 && TextW[0] == L'\\' && TextW[1] == L'\\' &&
                    (TextW.size() < 6 || TextW[2] != L'.' || TextW[3] != L'\\' || TextW[4] == 0 || TextW[5] != L':') &&
                    Plugins.GetFirstNethoodPluginFSName())
                {
                    chars = 2;
                }
                else
                {
                    std::wstring rootPathW = GetRootPath(TextW.c_str());
                    chars = (int)rootPathW.size();

                    BOOL isDotDriveFormat = TextW.size() >= 6 && TextW[0] == L'\\' && TextW[1] == L'\\' &&
                                            TextW[2] == L'.' && TextW[3] == L'\\' && TextW[4] != 0 && TextW[5] == L':';
                    if (chars > pathLen || !isDotDriveFormat && chars > 3)
                        chars--;
                }

                BOOL exit;
                do
                {
                    item.Offset = 0;
                    item.PixelsOffset = 0;
                    item.Chars = chars;
                    item.Pixels = TextWidthBefore(chars);
                    HotTrackItems.Add(item);

                    if (chars < pathLen && TextW[chars] == L'\\')
                        chars++;

                    exit = TRUE;
                    while (chars < pathLen)
                    {
                        exit = FALSE;
                        if (TextW[chars] == L'\\')
                            break;
                        chars++;
                    }
                } while (!exit);
            }
            else if (FilesWindow->Is(ptPluginFS))
            {
                // RESTORED, not preserved. This branch existed only in the narrow
                // arm below, which has been unreachable since the wide SetText began forcing
                // UseWideText - so directory-line hot-path tracking on a plugin FS (FTP and
                // friends) has been DEAD in shipping builds. The wide arm handled ptDisk and
                // ptZIPArchive and had no else at all.
                //
                // GetNextDirectoryLineHotPath is wide in the SDK (plugins/shared/spl_fs.h), so
                // the AnsiToWide(Text) hop the old code needed is gone with the mirror.
                int chars = 0;
                while (1)
                {
                    int lastChars = chars;
                    if (!FilesWindow->GetPluginFS()->GetNextDirectoryLineHotPath(TextW.c_str(), pathLen, chars))
                    {
                        chars = pathLen;
                    }
                    if (chars == lastChars)
                        chars++; // would be an infinite loop, better to handle it...
                    if (chars > pathLen)
                        chars = pathLen;

                    item.Offset = 0;
                    item.PixelsOffset = 0;
                    item.Chars = chars;
                    item.Pixels = TextWidthBefore(chars);
                    HotTrackItems.Add(item);

                    if (chars == pathLen)
                        break;
                }
            }
            HotTrackItemsMeasured = TRUE;
        }
        // The `else if (Text != NULL)` arm that stood here is deleted. It was
        // unreachable - Text was written only by SetText, which set UseWideText = TRUE in
        // the same breath - and it was LONGER than the wide arm because it held work the
        // wide side never got. That work is ported above, not dropped.
    }
    if (Border == blBottom)
    {
        // populate HotTrackItems
        CHotTrackItem item;
        HotTrackItems.DestroyMembers();
        {
            SIZE s;
            GetTextExtentExPointW(dc, TextW.c_str(), TextLen, 0, NULL, AlpDX.data(), &s);

            DWORD len = TextLen;
            SIZE sOffset;
            SIZE sSub;
            for (size_t i = 0; i < SubTexts.size(); ++i)
            {
                const size_t rangeOffset = SubTexts[i].Offset;
                const size_t rangeLength = SubTexts[i].Length;
                if (rangeOffset > static_cast<size_t>(len) ||
                    rangeLength > static_cast<size_t>(len) - rangeOffset)
                {
                    TRACE_E("charOffset + charLen >= len");
                    continue;
                }
                const int charOffset = static_cast<int>(rangeOffset);
                const int charLen = static_cast<int>(rangeLength);
                GetTextExtentPoint32W(dc, TextW.c_str(), charOffset, &sOffset);
                GetTextExtentPoint32W(dc, TextW.c_str() + charOffset, charLen, &sSub);
                item.PixelsOffset = sOffset.cx;
                item.Pixels = sSub.cx;
                item.Offset = charOffset;
                item.Chars = charLen;
                HotTrackItems.Add(item);
                HotTrackItemsMeasured = TRUE;
            }
        }
        // The `else if (Text != NULL)` arm that stood here is deleted. It was
        // unreachable - Text was written only by SetText, which set UseWideText = TRUE in
        // the same breath - and it was LONGER than the wide arm because it held work the
        // wide side never got. That work is ported above, not dropped.
    }
    SelectObject(dc, oldFont);
    HANDLES(ReleaseDC(HWindow, dc));
}

void CStatusWindow::DestroyWindow()
{
    CALL_STACK_MESSAGE1("CStatusWindow::DestroyWindow()");
    if (ToolBar != NULL)
    {
        if (ToolBar->HWindow != NULL)
            ToggleToolBar();
        delete ToolBar;
        ToolBar = NULL;
    }
    if (Throbber || DelayedThrobber)
        SetThrobber(FALSE, 0, TRUE); // we need to kill the timer

    ::DestroyWindow(HWindow);
}

void CStatusWindow::SetHidden(int hiddenFilesCount, int hiddenDirsCount)
{
    CALL_STACK_MESSAGE_NONE
    BOOL hidden = hiddenFilesCount != 0 || hiddenDirsCount != 0;
    HiddenFilesCount = hiddenFilesCount;
    HiddenDirsCount = hiddenDirsCount;
    if (Hidden != hidden)
    {
        Hidden = hidden;
        if (HWindow != NULL)
        {
            NeedToInvalidate = TRUE;
            InvalidateIfNeeded();
        }
    }
}

void CStatusWindow::SetHistory(BOOL history)
{
    CALL_STACK_MESSAGE_NONE
    if (History != history)
    {
        History = history;
        if (HWindow != NULL)
        {
            NeedToInvalidate = TRUE;
            InvalidateIfNeeded();
        }
    }
}

void CStatusWindow::SetThrobber(BOOL show, int delay, BOOL calledFromDestroyWindow)
{
    CALL_STACK_MESSAGE_NONE
    if (!calledFromDestroyWindow)
        ShowThrobber = show;
    if (show)
    {
        if (DelayedThrobber) // waiting to be shown
        {
            if (HWindow == NULL)
                TRACE_E("Unexpected situation in CStatusWindow::SetThrobber(): DelayedThrobber is TRUE but HWindow is NULL");
            if (Throbber)
                TRACE_E("Unexpected situation in CStatusWindow::SetThrobber(): DelayedThrobber and Throbber are both TRUE");
            KillTimer(HWindow, IDT_DELAYEDTHROBBER);
            if (Throbber /* just correcting inconsistent state */ ||
                delay <= 0 || !SetTimer(HWindow, IDT_DELAYEDTHROBBER, delay, NULL))
            {
                DelayedThrobber = FALSE; // should be shown immediately or timer setting failed, so show it now
                DelayedThrobberShowTime = 0;
            }
            else
            {
                DelayedThrobberShowTime = GetTickCount() + delay;
                if (DelayedThrobberShowTime == 0)
                    DelayedThrobberShowTime++; // 0 is invalid value
            }
        }
        else
        {
            if (!Throbber && delay > 0)
            {
                if (HWindow != NULL && SetTimer(HWindow, IDT_DELAYEDTHROBBER, delay, NULL))
                    DelayedThrobber = TRUE; // not shown + should be shown with delay + window is visible (if not, only DelayedThrobberShowTime is calculated)
                DelayedThrobberShowTime = GetTickCount() + delay;
                if (DelayedThrobberShowTime == 0)
                    DelayedThrobberShowTime++; // 0 is invalid value
            }
        }
    }
    else
    {
        if (DelayedThrobber) // waiting to be shown, but throbber should be hidden, end waiting
        {
            if (HWindow == NULL)
                TRACE_E("Unexpected situation 2 in CStatusWindow::SetThrobber(): DelayedThrobber is TRUE but HWindow is NULL");
            KillTimer(HWindow, IDT_DELAYEDTHROBBER);
            DelayedThrobber = FALSE;
        }
        if (!calledFromDestroyWindow)
            DelayedThrobberShowTime = 0;
    }
    if (HWindow == NULL && Throbber)
    {
        Throbber = FALSE;
        TRACE_E("Unexpected situation in CStatusWindow::SetThrobber(): Throbber is TRUE but HWindow is NULL");
    }
    if (HWindow != NULL && !DelayedThrobber && Throbber != show)
    {
        Throbber = show;

        if (Throbber)
        {
            ThrobberFrame = 0; // starting -> will animate from the first frame
            SetTimer(HWindow, IDT_THROBBER, IDT_THROBBER_DELAY, NULL);
        }
        else
        {
            KillTimer(HWindow, IDT_THROBBER);
        }

        if (StopStatusbarRepaint == 0)
        {
            NeedToInvalidate = TRUE;
            InvalidateIfNeeded();
        }
        else
        {
            PostStatusbarRepaint = TRUE;
        }
    }
}

void CStatusWindow::SetThrobberTooltipW(const wchar_t* throbberTooltip)
{
    ThrobberTooltip = throbberTooltip ? throbberTooltip : L"";
}

void CStatusWindow::SetSecurity(CSecurityIconState iconState)
{
    CALL_STACK_MESSAGE_NONE
    if (Security != iconState)
    {
        Security = iconState;
        if (HWindow != NULL)
        {
            NeedToInvalidate = TRUE;
            InvalidateIfNeeded();
        }
    }
}

void CStatusWindow::SetSecurityTooltipW(const wchar_t* tooltip)
{
    SecurityTooltip = tooltip ? tooltip : L"";
}

int CStatusWindow::ChangeThrobberID()
{
    static int NewID = 0; // throbber ID must be unique (i.e., single counter for both panels)
    ThrobberID = NewID++;
    if (ThrobberID == -1)
        ThrobberID = NewID++;
    return ThrobberID;
}

void CStatusWindow::HideThrobberAndSecurityIcon()
{
    SetThrobber(FALSE);
    SetThrobberTooltipW(NULL);
    SetSecurity(sisNone);
    SetSecurityTooltipW(NULL);
}

void CStatusWindow::InvalidateIfNeeded()
{
    CALL_STACK_MESSAGE_NONE
    if (NeedToInvalidate)
    {
        NeedToInvalidate = FALSE;
        if (HWindow != NULL)
            InvalidateRect(HWindow, NULL, TRUE);
    }
}

int CStatusWindow::GetNeededHeight()
{
    CALL_STACK_MESSAGE_NONE
    int height = 2 + EnvFontCharHeight + 2;
    if (Border & blTop)
    {
        height += 2 + 2;
        //    int needed = ToolBar->GetNeededHeight();
        int needed = 3 + 16 + 3;
        if (height < needed)
            height = needed;
    }
    if (Border & blBottom)
        height++;
    return height;
}

void CStatusWindow::SetSize(const CQuadWord& size)
{
    CALL_STACK_MESSAGE_NONE
    try
    {
        std::wstring newSize;
        if (size != CQuadWord(-1, -1))
            newSize = PrintDiskSize(size, 0);
        if (SizeAssigned && Size == newSize)
            return;
        Size.swap(newSize);
        SizeAssigned = TRUE;
    }
    catch (const std::bad_alloc&)
    {
        TRACE_E(LOW_MEMORY);
        return;
    }
    catch (const std::length_error&)
    {
        TRACE_E(LOW_MEMORY);
        return;
    }
    if (HWindow != NULL)
        InvalidateRect(HWindow, NULL, FALSE);
}

void CStatusWindow::SetLeftPanel(BOOL left)
{
    CALL_STACK_MESSAGE_NONE
    Left = left;
    if (ToolBar != NULL)
    {
        ToolBar->SetType(Left ? mtbtLeft : mtbtRight);
        ToolBar->Load((Left ? Configuration.LeftToolBar : Configuration.RightToolBar).c_str());
    }
}

BOOL CStatusWindow::ToggleToolBar()
{
    CALL_STACK_MESSAGE1("CStatusWindow::ToggleToolBar()");
    if (ToolBar == NULL)
        return FALSE;
    if (ToolBar->HWindow != NULL)
    {
        ::DestroyWindow(ToolBar->HWindow);
        return TRUE;
    }
    else
    {
        if (!ToolBar->CreateWnd(HWindow))
            return FALSE;
        ToolBar->SetImageList(HGrayToolBarImageList);
        ToolBar->SetHotImageList(HHotToolBarImageList);
        ToolBar->SetStyle(TLB_STYLE_IMAGE | TLB_STYLE_ADJUSTABLE);
        ToolBar->Load((Left ? Configuration.LeftToolBar : Configuration.RightToolBar).c_str());
        SendMessage(ToolBar->HWindow, TB_SETPARENT, (WPARAM)MainWindow->HWindow, 0);
        ShowWindow(ToolBar->HWindow, SW_SHOW);
        return TRUE;
    }
    return TRUE;
}

BOOL CStatusWindow::SetDriveIcon(HICON hIcon)
{
    CALL_STACK_MESSAGE_NONE
    if (ToolBar != NULL && ToolBar->HWindow != NULL)
        ToolBar->ReplaceImage(Left ? CM_LCHANGEDRIVE : CM_RCHANGEDRIVE, FALSE, hIcon, TRUE, TRUE);
    return TRUE;
}

void CStatusWindow::SetDrivePressed(BOOL pressed)
{
    CALL_STACK_MESSAGE_NONE
    if (ToolBar != NULL && ToolBar->HWindow != NULL)
    {
        TLBI_ITEM_INFO2 tii;
        tii.Mask = TLBI_MASK_STATE;
        tii.State = pressed ? TLBI_STATE_PRESSED : 0;
        ToolBar->SetItemInfo2(Left ? CM_LCHANGEDRIVE : CM_RCHANGEDRIVE, FALSE, &tii);
        UpdateWindow(ToolBar->HWindow);
    }
}

void CStatusWindow::LayoutWindow()
{
    CALL_STACK_MESSAGE_NONE
    SendMessage(HWindow, WM_SIZE, 0, 0);
    InvalidateRect(HWindow, NULL, TRUE);
    UpdateWindow(HWindow);
}

void CStatusWindow::GetHotTextW(std::wstring& buffer)
{
    CALL_STACK_MESSAGE_NONE
    if (HotItem != NULL)
    {
        buffer = TextW.substr(HotItem->Offset, HotItem->Chars);
        if ((Border & blTop) && FilesWindow->Is(ptPluginFS) && FilesWindow->GetPluginFS()->NotEmpty())
            FilesWindow->GetPluginFS()->CompleteDirectoryLineHotPathW(buffer);
    }
    else
        buffer.clear();
}

BOOL CStatusWindow::FindHotTrackItem(int xPos, int& index)
{
    CALL_STACK_MESSAGE_NONE
    int i;
    for (i = 0; i < HotTrackItems.Count; i++)
    {
        CHotTrackItem* item = &HotTrackItems[i];

        // if there's an ellipsis after root folder, we must adjust xPos for it
        if (i == 1 && EllipsedWidth != -1)
            xPos += EllipsedWidth - TextEllipsisWidthEnv;

        if (xPos >= item->PixelsOffset && xPos < item->PixelsOffset + item->Pixels)
        {
            index = i;
            return TRUE;
        }
    }
    return FALSE;
}

void CStatusWindow::FlashText(BOOL hotTrackOnly)
{
    CALL_STACK_MESSAGE_NONE
    Repaint(TRUE, hotTrackOnly);
    Sleep(100);
    Repaint(FALSE, hotTrackOnly);
}

void PaintSymbol(HDC hDC, HDC hMemDC, HBITMAP hBitmap, int xOffset, int width, int height, const RECT* rect, BOOL hot, BOOL activeCaption)
{
    HBITMAP hOldBitmap = (HBITMAP)SelectObject(hMemDC, hBitmap);
    COLORREF textColor = GetStatusTextColor(activeCaption, Configuration.ShowPanelCaption, hot);
    COLORREF bkColor = GetStatusBkColor(activeCaption, Configuration.ShowPanelCaption);
    int oldTextColor = SetTextColor(hDC, textColor);
    int oldBkColor = SetBkColor(hDC, bkColor);
    int x = (rect->left + rect->right) / 2 - width / 2;
    int y = (rect->top + rect->bottom) / 2 - height / 2;
    BitBlt(hDC, x, y, width, height, hMemDC, xOffset, 0, SRCCOPY);
    SetBkColor(hDC, oldBkColor);
    SetTextColor(hDC, oldTextColor);
    SelectObject(hMemDC, hOldBitmap);
}

void CStatusWindow::PaintThrobber(HDC hDC)
{
    if ((Border & blTop) == 0)
        return;
    BOOL activeCaption = (FilesWindow == MainWindow->GetActivePanel()) && MainWindow->CaptionIsActive;
    RECT r = ThrobberRect;
    r.left += 2;
    r.right -= 2;
    if (DarkMode_ShouldUseDark())
        FillRectSolid(hDC, &r, GetStatusBkColor(activeCaption, TRUE));
    else
        FillRect(hDC, &r, activeCaption ? HActiveCaptionBrush : HInactiveCaptionBrush);
    int x = (ThrobberRect.left + ThrobberRect.right) / 2 - THROBBER_WIDTH / 2;
    int y = (ThrobberRect.top + ThrobberRect.bottom) / 2 - THROBBER_HEIGHT / 2;
    COLORREF fgClr = GetStatusTextColor(activeCaption, Configuration.ShowPanelCaption, FALSE);

    ThrobberFrames->Draw(ThrobberFrame, hDC, x, y, fgClr, IL_DRAW_ASALPHA);
}

void CStatusWindow::PaintSecurity(HDC hDC)
{
    if (Security == sisNone || (Border & blTop) == 0)
        return;
    BOOL activeCaption = (FilesWindow == MainWindow->GetActivePanel()) && MainWindow->CaptionIsActive;
    RECT r = SecurityRect;
    //  r.left += 2;
    //  r.right -= 2;
    if (DarkMode_ShouldUseDark())
        FillRectSolid(hDC, &r, GetStatusBkColor(activeCaption, TRUE));
    else
        FillRect(hDC, &r, activeCaption ? HActiveCaptionBrush : HInactiveCaptionBrush);
    int x = (SecurityRect.left + SecurityRect.right) / 2 - LOCK_WIDTH / 2;
    int y = (SecurityRect.top + SecurityRect.bottom) / 2 - LOCK_HEIGHT / 2 - 1;
    COLORREF fgClr = GetStatusTextColor(activeCaption, Configuration.ShowPanelCaption, HotSecurity);

    LockFrames->Draw(DWORD(Security - 1), hDC, x, y, fgClr, IL_DRAW_ASALPHA /*IL_DRAW_TRANSPARENT*/);
}

#define FILTER_WIDTH 9
#define FILTER_HEIGHT 8

#define ZOOM_WIDTH 9
#define ZOOM_HEIGHT 8

void CStatusWindow::Paint(HDC hdc, BOOL highlightText, BOOL highlightHotTrackOnly)
{
    CALL_STACK_MESSAGE3("CStatusWindow::Paint(, %d, %d)", highlightText, highlightHotTrackOnly);
    HDC dc = ItemBitmap.HMemDC;

    BOOL isDirectoryLine = (Border & blTop) != 0;
    DarkModeMainFramePalette palette;
    BOOL useDark = DarkMode_GetMainFramePalette(&palette);

    RECT r;
    r.left = 0;
    r.top = 0;
    r.right = Width;
    r.bottom = Height;
    if (useDark)
        FillRectSolid(dc, &r, RGB(45, 45, 48));
    else
        FillRect(dc, &r, HDialogBrush);

    GetClientRect(HWindow, &r);
    if (Border & blBottom)
        r.bottom--;

    // make room for toolbar please
    if (isDirectoryLine)
        r.left += ToolBarWidth + 1;

    BOOL activeCaption = (FilesWindow == MainWindow->GetActivePanel()) && MainWindow->CaptionIsActive;
    if (isDirectoryLine && Configuration.ShowPanelCaption)
    {
        // frame around text
        RECT textR = r;
        textR.top += 2;
        textR.bottom -= 2;
        if (useDark)
            DarkMode_DrawSunkenFrame(dc, &textR, palette);
        else
            DrawEdge(dc, &textR, BDR_SUNKENOUTER, BF_RECT);

        // fill area under text (active/inactive)
        textR.left++;
        textR.top++;
        textR.right--;
        textR.bottom--;
        if (useDark)
            FillRectSolid(dc, &textR, GetStatusBkColor(activeCaption, TRUE));
        else
            FillRect(dc, &textR, activeCaption ? HActiveCaptionBrush : HInactiveCaptionBrush);
    }

    // text
    EllipsedChars = -1;
    EllipsedWidth = -1;
    // Was `if (Text != NULL)`. Read carefully before changing this: the pointer
    // meant "SetText has been called at least once", NOT "the text is non-empty" - SetText(L"")
    // allocated a 1-byte buffer, so the guard passed. `!TextW.empty()` is NOT the same test, and
    // the difference is visible: this block paints the throbber, zoom, hidden and security
    // chrome as well as the text, and files_window_directory_read.cpp:350 really does set the
    // status line to L"". Swapping in the empty test would make that chrome disappear whenever
    // the text is blank. TextAssigned carries the bit the pointer used to carry implicitly.
    if (TextAssigned)
    {
        BOOL truncateEnd = TRUE; // truncating end (TRUE) or after root folder (FALSE)
        int visibleChars = 0;

        SetBkMode(dc, TRANSPARENT);
        HFONT oldFont = (HFONT)SelectObject(dc, EnvFont);
        auto drawTextAt = [&](int x, int y, int start, int count)
        {
            if (count <= 0)
                return;
            ExtTextOutW(dc, x, y, 0, NULL, TextW.c_str() + start, count, NULL);
        };

        SIZE s;
        RECT tmpR;
        tmpR.left = r.left + 2;
        tmpR.right = r.right - 2;
        tmpR.top = r.top + 3;
        tmpR.bottom = r.bottom - 3;

        WholeTextVisible = FALSE;
        // set all rectangles to zero size
        SecurityRect = tmpR;
        SecurityRect.right = SecurityRect.left;

        TextRect = tmpR;
        TextRect.right = TextRect.left;

        HiddenRect = tmpR;
        HiddenRect.right = HiddenRect.left;

        ThrobberRect = tmpR;
        ThrobberRect.right = ThrobberRect.left;

        SizeRect = tmpR;
        SizeRect.left = SizeRect.right;

        HistoryRect = tmpR;
        HistoryRect.left = HistoryRect.right;

        ZoomRect = tmpR;
        ZoomRect.left = ZoomRect.right;

        // determine which items (text/history/size/zoom) fit in available space
        if (isDirectoryLine)
        {
            if (Configuration.ShowPanelZoom)
            {
                if (tmpR.right - tmpR.left < ZOOM_WIDTH + 4)
                    goto SKIP_MEASURING; // zoom button doesn't fit - skip measuring

                ZoomRect.left = tmpR.right - ZOOM_WIDTH - 6;
                ZoomRect.right = tmpR.right;
                tmpR.right -= 6 + ZOOM_WIDTH;
            }

            if (SizeAssigned)
            {
                GetTextExtentPoint32W(dc, Size.c_str(), (int)Size.size(), &s);
                if (tmpR.right - tmpR.left < s.cx)
                    goto SKIP_MEASURING; // even size doesn't fit - skip measuring

                SizeRect.left = tmpR.right - s.cx;
                SizeRect.right = tmpR.right;
                tmpR.right -= 2 + s.cx;
            }

            if (History)
            {
                if (tmpR.right - tmpR.left < SVGArrowDropDown.GetWidth())
                    goto SKIP_MEASURING; // drop arrow doesn't fit - skip measuring

                HistoryRect.left = tmpR.right - SVGArrowDropDown.GetWidth() - 2;
                HistoryRect.right = tmpR.right + 2;
                tmpR.right -= 2 + SVGArrowDropDown.GetWidth();
            }

            if (Hidden)
            {
                if (tmpR.right - tmpR.left < FILTER_WIDTH)
                    goto SKIP_MEASURING; // filter symbol doesn't fit - skip measuring
                HiddenRect.left = tmpR.right - FILTER_WIDTH - 2;
                HiddenRect.right = tmpR.right + 2;
                tmpR.right -= 2 + FILTER_WIDTH;
            }

            if (Throbber)
            {
                if (tmpR.right - tmpR.left < THROBBER_WIDTH)
                    goto SKIP_MEASURING; // throbber doesn't fit - skip measuring
                ThrobberRect.left = tmpR.right - THROBBER_WIDTH - 2;
                ThrobberRect.right = tmpR.right + 2;
                tmpR.right -= 2 + THROBBER_WIDTH;
            }

            if (Security != sisNone)
            {
                if (tmpR.right - tmpR.left < LOCK_WIDTH + 5)
                    goto SKIP_MEASURING; // lock doesn't fit - skip measuring
                SecurityRect.left = tmpR.left;
                SecurityRect.right = tmpR.left + 2 + LOCK_WIDTH + 3;
                tmpR.left += 2 + LOCK_WIDTH + 3;
            }
        }

        if (tmpR.right > tmpR.left + TextEllipsisWidthEnv)
        {
            visibleChars = TextLen;
            int textWidth = tmpR.right - tmpR.left;
            if (textWidth < TextWidthBefore(TextLen))
            {
                // text doesn't fit in requested width completely -> we must truncate
                if (isDirectoryLine && HotTrackItems.Count > 1 &&
                    HotTrackItems[0].Pixels + TextEllipsisWidthEnv <= textWidth)
                {
                    // for upper directory line, truncate after root folder of path
                    EllipsedChars = 0;
                    EllipsedWidth = 0;

                    int len = TextWidthBefore(TextLen);
                    int iter = HotTrackItems[0].Chars;
                    while (len > textWidth - TextEllipsisWidthEnv && iter < TextLen)
                    {
                        int charWidth = AlpDX[iter] - TextWidthBefore(iter);
                        len -= charWidth;
                        iter++;

                        EllipsedChars++;
                        EllipsedWidth += charWidth;
                    }
                    visibleChars = TextLen - iter;
                    truncateEnd = FALSE; // truncating from inside
                }
                else
                {
                    // for bottom infoline, search backwards for character
                    // after which we can insert "..."
                    while (visibleChars > 0 &&
                           TextWidthBefore(visibleChars) + TextEllipsisWidthEnv > textWidth)
                        visibleChars--;
                }
            }
            else
                WholeTextVisible = TRUE;

            // `TextLen > 1` here also zeroed the width of a legitimate single-character text,
            // collapsing TextRect so nothing was drawn. TextWidthBefore handles 0 correctly.
            int realWidth = TextWidthBefore(TextLen);
            if (realWidth != 0 && EllipsedWidth != -1)
                realWidth = realWidth - EllipsedWidth + TextEllipsisWidthEnv;
            TextRect.left = tmpR.left;
            TextRect.right = TextRect.left + realWidth;
            if (TextRect.right > tmpR.right)
                TextRect.right = tmpR.right;
            else
            {
                int leftMax = TextRect.right;
                if (Hidden)
                {
                    HiddenRect.left = TextRect.right + 2;
                    HiddenRect.right = HiddenRect.left + FILTER_WIDTH + 2;
                    leftMax = HiddenRect.right;
                }
                if (Throbber)
                {
                    ThrobberRect.left = Hidden ? HiddenRect.right : TextRect.right + 2;
                    ThrobberRect.right = ThrobberRect.left + THROBBER_WIDTH + 2;
                    leftMax = ThrobberRect.right;
                }
                if (History)
                {
                    int mid = (leftMax + SizeRect.left) / 2;
                    HistoryRect.left = mid - SVGArrowDropDown.GetWidth() - 2;
                    HistoryRect.right = mid + SVGArrowDropDown.GetWidth() + 2;
                }
            }
        }

    SKIP_MEASURING:

        int myYOffset = 0;
        if (isDirectoryLine && !Configuration.ShowPanelCaption)
            myYOffset = 1;
        int textY = (tmpR.top + tmpR.bottom - EnvFontCharHeight + myYOffset) / 2;

        // draw main text if we have any space for it
        if (TextRect.right > TextRect.left)
        {
            // determine in advance which part of text should be drawn highlighted, due to cleartype
            // we must exclude it from drawing normal text
            CHotTrackItem* hotItem = NULL;
            BOOL showFlashText = (highlightText && highlightHotTrackOnly && LastHotItem != NULL);
            if (HotItem != NULL)
                hotItem = HotItem;
            if (showFlashText)
                hotItem = LastHotItem;

            if (isDirectoryLine && Configuration.ShowPanelCaption)
            {
                SetTextColor(dc, GetStatusTextColor(activeCaption, TRUE, FALSE));
                if (highlightText && !highlightHotTrackOnly)
                    SetTextColor(dc, GetStatusTextColor(activeCaption, TRUE, TRUE));
            }
            else
            {
                SetTextColor(dc, GetStatusTextColor(activeCaption, FALSE, FALSE));
                if (highlightText && !highlightHotTrackOnly)
                    SetTextColor(dc, useDark ? GetStatusTextColor(activeCaption, FALSE, TRUE)
                                             : GetSysColor(COLOR_HIGHLIGHTTEXT));
            }

            int firstClipChar = 2 * TextLen;
            int lastClipChar = 2 * TextLen;
            if (hotItem != NULL)
            {
                firstClipChar = hotItem->Offset;
                lastClipChar = hotItem->Offset + hotItem->Chars;
            }

            // draw first part of text (up to hotItem, if text starts with hotItem, we won't draw anything)
            if (firstClipChar != 0)
            {
                if (truncateEnd)
                { // without truncation or truncated end
                    drawTextAt(TextRect.left, textY, 0, min(visibleChars, firstClipChar));
                    if (visibleChars < min(TextLen, firstClipChar)) // if end was truncated -> append "..."
                    {
                        int offset = TextWidthBefore(visibleChars);
                        ExtTextOutW(dc, TextRect.left + offset, textY, 0, NULL, L"...", 3, NULL);
                    }
                }
                else
                { // truncated part after root folder
                    // root part
                    int rootChars = HotTrackItems[0].Chars;
                    drawTextAt(TextRect.left, textY, 0, rootChars);
                    // "..."
                    ExtTextOutW(dc, TextRect.left + TextWidthBefore(rootChars), textY, 0, NULL, L"...", 3, NULL);
                    // remainder
                    drawTextAt(TextRect.left + TextWidthBefore(rootChars) + TextEllipsisWidthEnv,
                               textY, TextLen - visibleChars, visibleChars);
                }
            }

            // draw second part of text (after hotItem onwards) -- truncation at end
            if (hotItem != NULL && truncateEnd && lastClipChar <= visibleChars)
            {
                // without truncation or truncated end
                int visibleChars2 = visibleChars - lastClipChar;
                drawTextAt(TextRect.left + TextWidthBefore(lastClipChar), textY, lastClipChar, visibleChars2);
                if (visibleChars < TextLen) // if end was truncated -> append "..."
                {
                    int offset = TextWidthBefore(visibleChars);
                    ExtTextOutW(dc, TextRect.left + offset, textY, 0, NULL, L"...", 3, NULL);
                }
            }
            // draw second part of text (after hotItem onwards) -- truncation in middle
            // only paths in directory line are truncated this way (!truncateEnd condition applies)
            if (hotItem != NULL && !truncateEnd && lastClipChar <= TextLen)
            { // truncated part after root folder
                int rootChars = HotTrackItems[0].Chars;
                int firstChar = hotItem->Chars;

                if (lastClipChar <= rootChars)
                {
                    ExtTextOutW(dc, TextRect.left + TextWidthBefore(rootChars), textY, 0, NULL, L"...", 3, NULL); // "..."
                    firstChar += EllipsedChars;                                                           // move forward over elided characters
                }
                else
                {
                    if (firstChar < rootChars + EllipsedChars) // need to skip possible backslash that would fall into ellipsis
                        firstChar = rootChars + EllipsedChars;
                }
                drawTextAt(TextRect.left + TextWidthBefore(firstChar) - EllipsedWidth + TextEllipsisWidthEnv,
                           textY, firstChar, TextLen - firstChar);
            }

            // display hot track item
            if (hotItem != NULL)
            {
                COLORREF oldColor;
                if (isDirectoryLine && Configuration.ShowPanelCaption)
                {
                    oldColor = SetTextColor(dc, GetStatusTextColor(activeCaption, TRUE, TRUE));
                }
                else
                {
                    oldColor = SetTextColor(dc, GetStatusTextColor(activeCaption, FALSE, TRUE));
                    if (showFlashText)
                        SetTextColor(dc, useDark ? GetStatusTextColor(activeCaption, FALSE, TRUE)
                                                 : GetSysColor(COLOR_HIGHLIGHTTEXT));
                }
                HFONT hOldFont = NULL;
                if (Configuration.SingleClick && HotItem != NULL)
                    hOldFont = (HFONT)SelectObject(dc, EnvFontUL);

                if (truncateEnd)
                { // without truncation or truncated end
                    int showChars = hotItem->Chars;
                    if (hotItem->Offset + showChars > visibleChars)
                    {
                        showChars = visibleChars - hotItem->Offset;
                        int offset = TextWidthBefore(visibleChars);
                        ExtTextOutW(dc, TextRect.left + offset, textY, 0, NULL, L"...", 3, NULL);
                    }
                    if (showChars > 0)
                    {
                        drawTextAt(TextRect.left + hotItem->PixelsOffset, textY, hotItem->Offset, showChars);
                    }
                }
                else
                { // truncated part after root folder
                    int showChars = hotItem->Chars;

                    int rootChars = HotTrackItems[0].Chars;
                    drawTextAt(TextRect.left, textY, 0, rootChars);
                    if (showChars > rootChars)
                    {
                        // "..."
                        ExtTextOutW(dc, TextRect.left + TextWidthBefore(rootChars), textY, 0, NULL, L"...", 3, NULL);
                        if (showChars - rootChars - EllipsedChars > 0)
                        {
                            // remainder
                            drawTextAt(TextRect.left + TextWidthBefore(rootChars) + TextEllipsisWidthEnv,
                                       textY, rootChars + EllipsedChars, showChars - rootChars - EllipsedChars);
                        }
                    }
                }

                if (hOldFont != NULL)
                    SelectObject(dc, hOldFont);
                SetTextColor(dc, oldColor);
            }
        }

        HDC hMemDC = HANDLES(CreateCompatibleDC(NULL));

        // zoom symbol
        if (ZoomRect.left < ZoomRect.right)
        {
            BOOL zoomed;
            if (MainWindow->LeftPanel == FilesWindow)
                zoomed = MainWindow->IsPanelZoomed(TRUE);
            else
                zoomed = MainWindow->IsPanelZoomed(FALSE);
            PaintSymbol(dc, hMemDC, HZoomBitmap, zoomed ? ZOOM_WIDTH : 0, ZOOM_WIDTH, ZOOM_HEIGHT, &ZoomRect, HotZoom, activeCaption);
        }

        // disk free
        if (SizeRect.left < SizeRect.right)
        {
            if (HotSize)
            {
                if (isDirectoryLine && Configuration.ShowPanelCaption)
                    SetTextColor(dc, GetStatusTextColor(activeCaption, TRUE, TRUE));
                else
                    SetTextColor(dc, GetStatusTextColor(activeCaption, FALSE, TRUE));
            }
            else
            {
                if (isDirectoryLine && Configuration.ShowPanelCaption)
                    SetTextColor(dc, GetStatusTextColor(activeCaption, TRUE, FALSE));
                else
                    SetTextColor(dc, GetStatusTextColor(activeCaption, FALSE, FALSE));
            }

            HFONT hOldFont = NULL;
            if (Configuration.SingleClick && HotSize)
                hOldFont = (HFONT)SelectObject(dc, EnvFontUL);
            ExtTextOutW(dc, SizeRect.left, textY, 0, NULL, Size.c_str(), (UINT)Size.size(), NULL);
            if (hOldFont != NULL)
                SelectObject(dc, hOldFont);
        }
        SelectObject(dc, oldFont);

        // arrow for directory history
        if (HistoryRect.left < HistoryRect.right)
        {
            // JRYFIXME: proper HOT color support, see PaintSymbol, for now just hack via SVGSTATE_DISABLED
            //PaintSymbol(dc, hMemDC, HDropDownBitmap, 0, SVGArrowDropDown.GetWidth(), SVGArrowDropDown.GetHeight(), &HistoryRect, HotHistory, activeCaption);
            SVGArrowDropDown.AlphaBlend(dc,
                                        HistoryRect.left,
                                        HistoryRect.top + (HistoryRect.bottom - HistoryRect.top - SVGArrowDropDown.GetHeight()) / 2,
                                        -1, -1,
                                        HotHistory ? SVGSTATE_DISABLED : SVGSTATE_ENABLED);
        }

        // filter symbol
        if (HiddenRect.left < HiddenRect.right)
            PaintSymbol(dc, hMemDC, HFilter, 0, FILTER_WIDTH, FILTER_HEIGHT, &HiddenRect, HotHidden, activeCaption);

        // throbber
        if (ThrobberRect.left < ThrobberRect.right)
            PaintThrobber(dc);

        // security
        if (SecurityRect.left < SecurityRect.right)
            PaintSecurity(dc);

        HANDLES(DeleteDC(hMemDC));
    }

    int delta = 0;
    if (Border & blBottom)
        delta = 1;

    BitBlt(hdc, delta + ToolBarWidth, 0, Width - ToolBarWidth - 2 * delta, Height - delta,
           dc, ToolBarWidth, 0, SRCCOPY);
}

void CStatusWindow::Repaint(BOOL flashText, BOOL hotTrackOnly)
{
    CALL_STACK_MESSAGE_NONE
    if (HWindow == NULL)
        return;
    HDC hdc = HANDLES(GetDC(HWindow));
    Paint(hdc, flashText, hotTrackOnly);
    HANDLES(ReleaseDC(HWindow, hdc));
}
/*
void
CStatusWindow::RepaintThrobber()
{
  CALL_STACK_MESSAGE_NONE
  if (HWindow == NULL)
    return;
  HDC hdc = HANDLES(GetDC(HWindow));
  PaintThrobber(hdc);
  HANDLES(ReleaseDC(HWindow, hdc));
}
*/

void CStatusWindow::InvalidateAndUpdate(BOOL update)
{
    CALL_STACK_MESSAGE_NONE
    if (HWindow == NULL)
        return;
    InvalidateRect(HWindow, NULL, FALSE);
    if (update)
        UpdateWindow(HWindow);
}

class CTextDropTarget : public IDropTarget
{
private:
    long RefCount;                    // object lifetime
    IDataObject* DataObject;          // IDataObject that entered the drag
    IDataObject* ForbiddenDataObject; // IDataObject we don't accept (we are its source)
    BOOL UseUnicode;                  // is there unicode text in DataObject? (otherwise try ANSI text)
    CFilesWindow* FilesWindow;        // panel we are associated with

    static void NormalizeDroppedDirectory(std::wstring& path)
    {
        if (IsPluginFSPath(path.c_str()))
            return;

        const size_t length = path.length();
        if ((length != 2 || path[0] != L'\\' || path[1] != L'\\') &&
            length > 0 && path.back() == L'\\')
        {
            path.pop_back();
        }
        if (path.length() == 2 && path[0] != L'\\')
            path.push_back(L'\\');
        if (path.length() == 6 && path[0] == L'\\' && path[1] == L'\\' &&
            path[2] == L'.' && path[3] == L'\\' && path[5] == L':')
        {
            path.push_back(L'\\');
        }
    }

    void PostChangeDirectory(std::wstring path, BOOL postProcessUserText)
    {
        NormalizeDroppedDirectory(path);
        std::unique_ptr<std::wstring> payload(new std::wstring(std::move(path)));
        if (payload == NULL)
        {
            TRACE_E(LOW_MEMORY);
            return;
        }
        if (PostMessage(FilesWindow->HWindow, WM_USER_CHANGEDIRW,
                        postProcessUserText, reinterpret_cast<LPARAM>(payload.get())))
        {
            payload.release();
        }
    }

public:
    CTextDropTarget(CFilesWindow* filesWindow)
    {
        RefCount = 1;
        DataObject = NULL;
        ForbiddenDataObject = NULL;
        UseUnicode = TRUE;
        FilesWindow = filesWindow;
    }

    virtual ~CTextDropTarget()
    {
        if (RefCount != 0)
            TRACE_E("Preliminary destruction of this object.");
    }

    void SetForbiddenDataObject(IDataObject* forbiddenDataObject)
    {
        ForbiddenDataObject = forbiddenDataObject;
    }

    // Returns the exact directory from a single-item data object.
    BOOL GetDirFromDataObject(IDataObject* pDataObject, std::wstring* path = NULL)
    {
        std::wstring exactPath;
        wchar_t itemKind = L'\0';
        BOOL fakeFormatPresent = FALSE;
        if (GetFakeDataObjectRealPath(pDataObject, exactPath, &itemKind, &fakeFormatPresent))
        {
            if (itemKind != L'D')
                return FALSE;
            if (path != NULL)
                *path = std::move(exactPath);
            return TRUE;
        }
        if (fakeFormatPresent)
            return FALSE; // ours, but no real path - see GetFakeDataObjectRealPath's contract

        FORMATETC formatEtc = {};
        formatEtc.cfFormat = CF_HDROP;
        formatEtc.dwAspect = DVASPECT_CONTENT;
        formatEtc.lindex = -1;
        formatEtc.tymed = TYMED_HGLOBAL;

        STGMEDIUM stgMedium = {};
        BOOL ret = FALSE;
        if (pDataObject->GetData(&formatEtc, &stgMedium) == S_OK)
        {
            if (stgMedium.tymed == TYMED_HGLOBAL && stgMedium.hGlobal != NULL)
            {
                const SIZE_T dataSize = GlobalSize(stgMedium.hGlobal);
                const DROPFILES* data = static_cast<const DROPFILES*>(HANDLES(GlobalLock(stgMedium.hGlobal)));
                if (data != NULL)
                {
                    ret = sally::clipboard::TryGetSingleHDropPath(data, dataSize, exactPath);
                    HANDLES(GlobalUnlock(stgMedium.hGlobal));
                }
            }
            ReleaseStgMedium(&stgMedium);
        }
        if (ret)
        {
            IFileSystem* fs = gFileSystem != NULL ? gFileSystem : GetWin32FileSystem();
            const DWORD attrs = fs->GetFileAttributes(exactPath.c_str());
            ret = attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY) != 0;
        }
        if (ret && path != NULL)
            *path = std::move(exactPath);
        return ret;
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
            return 0; // must not touch object, it no longer exists
        }
        return RefCount;
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

        // if our panel is also the source, forbid paste
        if (DataObject == ForbiddenDataObject)
        {
            *pdwEffect = DROPEFFECT_NONE;
            return S_OK;
        }

        // zjistime jestli je na clipboardu text
        FORMATETC formatEtc;
        ZeroMemory(&formatEtc, sizeof(formatEtc));
        formatEtc.cfFormat = CF_UNICODETEXT;
        formatEtc.dwAspect = DVASPECT_CONTENT;
        formatEtc.lindex = -1;
        formatEtc.tymed = TYMED_HGLOBAL;
        UseUnicode = TRUE;
        HRESULT textRes;
        if ((textRes = pDataObject->QueryGetData(&formatEtc)) != S_OK)
        {
            formatEtc.cfFormat = CF_TEXT;
            UseUnicode = FALSE;
            textRes = pDataObject->QueryGetData(&formatEtc);
        }
        if (textRes == S_OK)
        {
            *pdwEffect = DROPEFFECT_COPY;
            return S_OK;
        }
        if (GetDirFromDataObject(DataObject))
        {
            *pdwEffect = DROPEFFECT_COPY;
            return S_OK;
        }

        *pdwEffect = DROPEFFECT_NONE;
        return S_OK;
    }

    STDMETHOD(DragOver)
    (DWORD grfKeyState, POINTL pt, DWORD* pdwEffect)
    {
        if (ImageDragging)
            ImageDragMove(pt.x, pt.y);
        if (DataObject != NULL)
        {
            // if our panel is also the source, forbid paste
            if (DataObject == ForbiddenDataObject)
            {
                *pdwEffect = DROPEFFECT_NONE;
                return S_OK;
            }
            // zjistime jestli je na clipboardu text
            FORMATETC formatEtc;
            ZeroMemory(&formatEtc, sizeof(formatEtc));
            formatEtc.cfFormat = UseUnicode ? CF_UNICODETEXT : CF_TEXT;
            formatEtc.dwAspect = DVASPECT_CONTENT;
            formatEtc.lindex = -1;
            formatEtc.tymed = TYMED_HGLOBAL;
            if (DataObject->QueryGetData(&formatEtc) == S_OK)
            {
                *pdwEffect = DROPEFFECT_COPY;
                return S_OK;
            }
            if (GetDirFromDataObject(DataObject))
            {
                *pdwEffect = DROPEFFECT_COPY;
                return S_OK;
            }
        }
        *pdwEffect = DROPEFFECT_NONE;
        return S_OK;
    }

    STDMETHOD(DragLeave)
    ()
    {
        if (ImageDragging)
            ImageDragLeave();
        if (DataObject != NULL)
        {
            DataObject->Release();
            DataObject = NULL;
        }
        return S_OK;
    }

    STDMETHOD(Drop)
    (IDataObject* pDataObject, DWORD grfKeyState, POINTL pt,
     DWORD* pdwEffect)
    {
        if (ImageDragging)
            ImageDragLeave();

        // try to extract text from DataObject
        FORMATETC formatEtc = {};
        formatEtc.cfFormat = UseUnicode ? CF_UNICODETEXT : CF_TEXT;
        formatEtc.dwAspect = DVASPECT_CONTENT;
        formatEtc.lindex = -1;
        formatEtc.tymed = TYMED_HGLOBAL;

        STGMEDIUM stgMedium = {};
        if (pDataObject->GetData(&formatEtc, &stgMedium) == S_OK)
        {
            if (stgMedium.tymed == TYMED_HGLOBAL && stgMedium.hGlobal != NULL)
            {
                const SIZE_T byteSize = GlobalSize(stgMedium.hGlobal);
                const void* payload = HANDLES(GlobalLock(stgMedium.hGlobal));
                if (payload != NULL)
                {
                    std::wstring path;
                    bool decoded = false;
                    if (UseUnicode)
                    {
                        const size_t capacity = byteSize / sizeof(wchar_t);
                        const wchar_t* text = static_cast<const wchar_t*>(payload);
                        const wchar_t* terminator = byteSize % sizeof(wchar_t) == 0 && capacity != 0
                                                        ? static_cast<const wchar_t*>(wmemchr(text, L'\0', capacity))
                                                        : NULL;
                        if (terminator != NULL)
                        {
                            path.assign(text, terminator);
                            decoded = true;
                        }
                    }
                    else
                    {
                        decoded = sally::clipboard::DecodeAnsiClipboardPayload(
                                      payload, byteSize, GetACP(), path) == ERROR_SUCCESS;
                    }
                    if (decoded && !path.empty())
                        PostChangeDirectory(std::move(path), TRUE);
                    HANDLES(GlobalUnlock(stgMedium.hGlobal));
                }
            }
            ReleaseStgMedium(&stgMedium);
        }
        else
        {
            std::wstring path;
            if (GetDirFromDataObject(pDataObject, &path))
                PostChangeDirectory(std::move(path), FALSE);
        }

        if (DataObject != NULL)
        {
            DataObject->Release();
            DataObject = NULL;
        }
        *pdwEffect = DROPEFFECT_NONE;
        return S_OK;
    }
};

void CStatusWindow::RegisterDragDrop()
{
    CALL_STACK_MESSAGE1("CStatusWindow::RegisterDragDrop()");
    CTextDropTarget* dropTarget = new CTextDropTarget(FilesWindow);
    if (dropTarget != NULL)
    {
        if (HANDLES(RegisterDragDrop(HWindow, dropTarget)) != S_OK)
        {
            TRACE_E("RegisterDragDrop error.");
        }
        else
            IDropTargetPtr = dropTarget;
        dropTarget->Release(); // RegisterDragDrop called AddRef()
    }
}

void CStatusWindow::RevokeDragDrop()
{
    CALL_STACK_MESSAGE_NONE
    HANDLES(RevokeDragDrop(HWindow));
}

#define BUTTON_OFFSET 0

LRESULT
CStatusWindow::WindowProc(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    SLOW_CALL_STACK_MESSAGE4("CStatusWindow::WindowProc(0x%X, 0x%IX, 0x%IX)", uMsg, wParam, lParam);
    switch (uMsg)
    {
    case WM_CREATE:
    {
        HotItem = NULL;
        LastHotItem = NULL;
        HotSize = FALSE;
        HotHistory = FALSE;
        HotZoom = FALSE;
        HotHidden = FALSE;
        HotSecurity = FALSE;
        MouseCaptured = FALSE;

        if (Border & blTop)
        {
            ToolBar = new CMainToolBar(MainWindow->HWindow, Left ? mtbtLeft : mtbtRight);
            if (ToolBar == NULL)
            {
                TRACE_E(LOW_MEMORY);
                return -1;
            }
            ToggleToolBar();
            RegisterDragDrop();
        }

        if (ShowThrobber)
        {
            int ti = DelayedThrobberShowTime == 0 /* invalid value */ ? 0 : DelayedThrobberShowTime - GetTickCount();
            if (ti < 0)
                ti = 0;
            DelayedThrobberShowTime = 0; // we no longer need this value, if needed SetThrobber must set it again, assign invalid value
            SetThrobber(TRUE, ti);       // we should also turn on throbber
        }

        return 0;
    }

    case WM_DESTROY:
    {
        if (Throbber || DelayedThrobber)
            SetThrobber(FALSE, 0, TRUE); // we need to kill the timer
        if (Border & blTop)
            RevokeDragDrop();
        if (ToolBar != NULL)
        {
            ToolBar->DetachWindow();
            delete ToolBar;
            ToolBar = NULL;
        }
        return 0;
    }

    case WM_SIZE:
    {
        RECT r;
        GetClientRect(HWindow, &r);

        if (ToolBar != NULL && ToolBar->HWindow != NULL)
        {
            ToolBarWidth = ToolBar->GetNeededWidth();
            SetWindowPos(ToolBar->HWindow, 0, 0, 0, ToolBarWidth, r.bottom, SWP_NOACTIVATE | SWP_NOZORDER);
        }
        if (Width != r.right || Height != r.bottom)
        {
            Width = r.right;
            Height = r.bottom;
            ItemBitmap.Enlarge(Width, Height); // alokace bitmapy v ItemBitmap.HMemDC
        }

        break;
    }

    case WM_USER_TTGETTEXTW:
    {
        DWORD id = (DWORD)wParam; // FIXME_X64 - overit pretypovani na (DWORD)
        wchar_t* text = (wchar_t*)lParam;
        switch (id)
        {
        case 0:
        {
            break;
        }

        case 2:
        {
            lstrcpyW(text, LoadStrW(IDS_PANELFILTER));
            break;
        }

        case 3:
        {
            const std::wstring tooltip = ExpandPluralFilesDirsTextW(
                HiddenFilesCount, HiddenDirsCount, epfdmHidden, FALSE);
            lstrcpynW(text, tooltip.c_str(), TOOLTIP_TEXT_MAX);
            break;
        }

        case 4:
        {
            // TextW is the authoritative copy of the status text; the narrow Text member is
            // its CP_ACP rendering. This is the drag path, so taking it from TextW is what
            // stops a non-ANSI directory from reaching the tooltip as '?'.
            const wchar_t* str;
            if (Border == blTop && WholeTextVisible)
                str = LoadStrW(IDS_TRIM_DRAG_PATH);
            else if (Border == blBottom && WholeTextVisible)
                str = LoadStrW(IDS_COPY_DRAG_TEXT);
            else
                str = TextW.empty() ? NULL : TextW.c_str();
            if (str == NULL)
                text[0] = 0;
            else
                lstrcpynW(text, str, TOOLTIP_TEXT_MAX);
            break;
        }

        case 5:
        {
            lstrcpyW(text, LoadStrW(IDS_DIRHISTORY));
            break;
        }

        case 6:
        {
            lstrcpyW(text, LoadStrW(IDS_FREESPACE));
            break;
        }

        case 7:
        {
            lstrcpyW(text, LoadStrW(IDS_ZOOMPANEL));
            break;
        }

        case 8:
        {
            lstrcpynW(text, ThrobberTooltip.c_str(), TOOLTIP_TEXT_MAX);
            break;
        }

        case 9:
        {
            lstrcpynW(text, SecurityTooltip.c_str(), TOOLTIP_TEXT_MAX);
            break;
        }

        default:
            TRACE_E("Unknown ID:" << id);
            break;
        }
        return 0;
    }

    case WM_MOUSEMOVE:
    {
        short xPos = LOWORD(lParam);
        short yPos = HIWORD(lParam);

        CHotTrackItem* newHotItem = NULL;
        BOOL newHotSize = FALSE;
        BOOL newHotHistory = FALSE;
        BOOL newHotZoom = FALSE;
        BOOL newHotHidden = FALSE;
        BOOL newHotSecurity = FALSE;

        DWORD toolTipID = 0;

        if (xPos >= TextRect.left && xPos < TextRect.right && yPos >= TextRect.top && yPos < TextRect.bottom)
            toolTipID = 4;

        BOOL isInHistory = History && xPos >= HistoryRect.left && xPos < HistoryRect.right &&
                           yPos >= HistoryRect.top && yPos < HistoryRect.bottom;
        if (isInHistory)
            toolTipID = 5;

        BOOL isInRect = xPos >= TextRect.left && xPos < TextRect.right &&
                        yPos >= TextRect.top && yPos < TextRect.bottom;
        BOOL isInSizeRect = xPos >= SizeRect.left && xPos < SizeRect.right &&
                            yPos >= SizeRect.top && yPos < SizeRect.bottom;
        BOOL isInZoomRect = xPos >= ZoomRect.left && xPos < ZoomRect.right &&
                            yPos >= ZoomRect.top && yPos < ZoomRect.bottom;
        BOOL isInHiddenRect = xPos >= HiddenRect.left && xPos < HiddenRect.right &&
                              yPos >= HiddenRect.top && yPos < HiddenRect.bottom;
        BOOL isInThrobberRect = xPos >= ThrobberRect.left && xPos < ThrobberRect.right &&
                                yPos >= ThrobberRect.top && yPos < ThrobberRect.bottom;
        BOOL isInSecurityRect = xPos >= SecurityRect.left && xPos < SecurityRect.right &&
                                yPos >= SecurityRect.top && yPos < SecurityRect.bottom;
        if (isInSizeRect)
            toolTipID = 6;
        if (isInZoomRect)
            toolTipID = 7;
        if (isInHiddenRect)
            toolTipID = 3;
        if (isInThrobberRect)
            toolTipID = 8;
        if (isInSecurityRect)
            toolTipID = 9;

        if (wParam & (MK_LBUTTON | MK_MBUTTON | MK_RBUTTON))
            toolTipID = 0;
        SetCurrentToolTip(HWindow, toolTipID);

        // handle text tear-off and drag&drop initiation
        if (MouseCaptured && (LButtonDown || RButtonDown) && HotTrackItems.Count > 0)
        {
            int x = abs(LButtonDownPoint.x - (short)LOWORD(lParam));
            int y = abs(LButtonDownPoint.y - (short)HIWORD(lParam));
            if (x > GetSystemMetrics(SM_CXDRAG) || y > GetSystemMetrics(SM_CYDRAG))
            {
                int index;
                if (FindHotTrackItem(LButtonDownPoint.x - TextRect.left, index))
                {
                    // BUG FIX. This read `UseWideText && FilesWindow->Is(ptDisk)`,
                    // so ARCHIVE and PLUGIN-FS panels took the narrow arm below and tore off a
                    // path built from the lossy ANSI mirror - a non-ANSI archive or FTP path
                    // dragged out as '?'. Only disk panels ever got the wide text.
                    //
                    // Consequence worth stating because it leaves the process: the drag payload
                    // for those panels changes from CF_TEXT to CF_UNICODETEXT (see
                    // CTextDataObject below). That is the point - an external drop target now
                    // receives the real path instead of a degraded one.
                    //
                    // The separate `bufferW` is gone too. It was a second copy that the
                    // plugin-FS adjustment below did NOT update - it rewrites 'buffer' - so
                    // taking the wide arm for a plugin FS would have silently dropped
                    // CompleteDirectoryLineHotPath's work from the payload. One buffer cannot
                    // disagree with itself.
                    std::wstring buffer;
                    int hotChars = HotTrackItems[index].Chars;
                    int hotOffset = HotTrackItems[index].Offset;
                    if (hotOffset >= 0 && hotOffset < (int)TextW.length())
                    {
                        if (hotOffset + hotChars > (int)TextW.length())
                            hotChars = (int)TextW.length() - hotOffset;
                        buffer.assign(TextW, static_cast<size_t>(hotOffset), static_cast<size_t>(hotChars));
                    }
                    // for Directory Line with plugin FS, allow plugin to make final path adjustments (adding ']' for VMS paths in FTP)
                    if ((Border & blTop) && FilesWindow->Is(ptPluginFS) && FilesWindow->GetPluginFS()->NotEmpty())
                    {
                        FilesWindow->GetPluginFS()->CompleteDirectoryLineHotPathW(buffer);
                        const size_t colon = buffer.find(L':');
                        if (colon != std::wstring::npos)
                        {
                            std::wstring userPart = buffer.substr(colon + 1);
                            FilesWindow->GetPluginFS()->GetPluginInterfaceForFS()->ConvertPathToExternalW(
                                FilesWindow->GetPluginFS()->GetPluginFSName(),
                                FilesWindow->GetPluginFS()->GetPluginFSNameIndex(), userPart);
                            buffer.replace(colon + 1, std::wstring::npos, userPart);
                        }
                    }
                    hotChars = static_cast<int>(buffer.size());

                    WindowProc(WM_MOUSELEAVE, 0, 0);
                    MouseCaptured = FALSE;
                    LButtonDown = FALSE;
                    RButtonDown = FALSE;

                    SIZE_T dataSize = ((SIZE_T)hotChars + 1) * sizeof(wchar_t);
                    HGLOBAL h = NOHANDLES(GlobalAlloc(GMEM_MOVEABLE | GMEM_DDESHARE, dataSize));
                    if (h != NULL)
                    {
                        void* s = HANDLES(GlobalLock(h));
                        if (s != NULL)
                        {
                            memcpy(s, buffer.c_str(), dataSize);
                            HANDLES(GlobalUnlock(h));
                        }

                        CImpIDropSource* dropSource = new CImpIDropSource(FALSE);
                        IDataObject* dataObject = new CTextDataObject(h, TRUE /* wide */);
                        if (IDropTargetPtr != NULL)
                            ((CTextDropTarget*)IDropTargetPtr)->SetForbiddenDataObject(dataObject);
                        if (dataObject != NULL && dropSource != NULL)
                        {
                            DWORD dwEffect;

                            HIMAGELIST hDragIL = NULL;
                            int dxHotspot, dyHotspot;
                            int imgWidth, imgHeight;
                            // the payload is exact wide text; the preview is built
                            // from the same buffer so image and payload cannot diverge.
                            hDragIL = CreateDragImage(buffer.c_str(), dxHotspot, dyHotspot, imgWidth, imgHeight);
                            ImageList_BeginDrag(hDragIL, 0, dxHotspot, dyHotspot);
                            ImageDragBegin(imgWidth, imgHeight, dxHotspot, dyHotspot);

                            DoDragDrop(dataObject, dropSource, DROPEFFECT_COPY, &dwEffect);

                            ImageDragEnd();
                            ImageList_EndDrag();
                            ImageList_Destroy(hDragIL);

                            isInRect = FALSE;
                            isInSizeRect = FALSE;
                            isInHistory = FALSE;
                            isInZoomRect = FALSE;
                            isInHiddenRect = FALSE;
                            isInThrobberRect = FALSE;
                            isInSecurityRect = FALSE;
                        }
                        if (IDropTargetPtr != NULL)
                            ((CTextDropTarget*)IDropTargetPtr)->SetForbiddenDataObject(NULL);
                        if (dataObject != NULL)
                            dataObject->Release();
                        if (dropSource != NULL)
                            dropSource->Release();
                    }
                }
            }
        }

        BOOL repaint = FALSE;

        // handle ending capture mode
        if (MouseCaptured)
        {
            POINT p;
            GetCursorPos(&p);
            if (!isInRect && !isInSizeRect && !isInHistory && !isInZoomRect &&
                    !isInHiddenRect && !isInSecurityRect ||
                WindowFromPoint(p) != HWindow)
            {
                WindowProc(WM_MOUSELEAVE, 0, 0);
                MouseCaptured = FALSE;
                repaint = TRUE;
            }
        }
        else
        {
            // initiate capture mode
            if (!MouseCaptured && (isInRect || isInSizeRect || isInHistory ||
                                   isInZoomRect || isInHiddenRect || isInSecurityRect))
            {
                TRACKMOUSEEVENT tme;
                tme.cbSize = sizeof(tme);
                tme.dwFlags = TME_LEAVE;
                tme.hwndTrack = HWindow;
                TrackMouseEvent(&tme);
                MouseCaptured = TRUE;
                repaint = TRUE;
            }
        }

        // handle highlighting
        if (MouseCaptured)
        {
            if (isInRect)
            {
                int index;
                if (FindHotTrackItem(xPos - TextRect.left, index))
                {
                    newHotItem = &HotTrackItems[index];
                    if (Configuration.SingleClick)
                        SetHandCursor();
                }
                else
                {
                    if (Configuration.SingleClick)
                        SetCursor(LoadCursor(NULL, IDC_ARROW));
                }
            }
            if (isInHistory)
            {
                newHotHistory = TRUE;
                if (Configuration.SingleClick)
                    SetHandCursor();
            }
            if (isInZoomRect)
            {
                newHotZoom = TRUE;
                if (Configuration.SingleClick)
                    SetHandCursor();
            }
            if (isInHiddenRect)
            {
                newHotHidden = TRUE;
                if (Configuration.SingleClick)
                    SetHandCursor();
            }
            if (isInSizeRect)
            {
                // drive-info works only if it's not an FS that doesn't support drive-info
                if (FilesWindow->Is(ptDisk) || FilesWindow->Is(ptZIPArchive) ||
                    FilesWindow->Is(ptPluginFS) && FilesWindow->GetPluginFS()->NotEmpty() &&
                        FilesWindow->GetPluginFS()->IsServiceSupported(FS_SERVICE_SHOWINFO))
                {
                    newHotSize = TRUE;
                    if (Configuration.SingleClick)
                        SetHandCursor();
                }
            }
            if (isInSecurityRect)
            {
                if (FilesWindow->Is(ptPluginFS) && FilesWindow->GetPluginFS()->NotEmpty() &&
                    FilesWindow->GetPluginFS()->IsServiceSupported(FS_SERVICE_SHOWSECURITYINFO))
                {
                    newHotSecurity = TRUE;
                    if (Configuration.SingleClick)
                        SetHandCursor();
                }
            }
        }

        if (repaint || newHotItem != HotItem || newHotSize != HotSize ||
            newHotHistory != HotHistory || newHotZoom != HotZoom ||
            newHotHidden != HotHidden || newHotSecurity != HotSecurity)
        {
            HotItem = newHotItem;
            if (HotItem != NULL)
                LastHotItem = HotItem;
            HotSize = newHotSize;
            HotHistory = newHotHistory;
            HotZoom = newHotZoom;
            HotHidden = newHotHidden;
            HotSecurity = newHotSecurity;
            Repaint();
        }

        break;
    }

    case WM_SETCURSOR:
    {
        if (MouseCaptured)
            return TRUE;
        break;
    }

    case WM_MOUSELEAVE:
    case WM_CANCELMODE:
    {
        SetCurrentToolTip(NULL, 0);
        if (MouseCaptured)
        {
            if (GetCapture() == HWindow)
                ReleaseCapture();
            LButtonDown = FALSE;
            RButtonDown = FALSE;
            MouseCaptured = FALSE;
            if (Configuration.SingleClick)
                SetCursor(LoadCursor(NULL, IDC_ARROW));
        }
        if (HotItem != NULL || HotSize || HotHistory || HotZoom || HotHidden || HotSecurity)
        {
            HotItem = NULL;
            HotHistory = FALSE;
            HotSize = FALSE;
            HotZoom = FALSE;
            HotHidden = FALSE;
            HotSecurity = FALSE;
            Repaint();
        }
        break;
    }

    case WM_RBUTTONDOWN:
    case WM_LBUTTONDOWN:
    case WM_LBUTTONDBLCLK:
    {
        MainWindow->CancelPanelsUI(); // cancel QuickSearch and QuickEdit
        SetCurrentToolTip(NULL, 0);

        if (HotHistory && MainWindow->GetActivePanel() != FilesWindow)
            MainWindow->ChangePanel();

        if (!MouseCaptured && (uMsg == WM_LBUTTONDOWN || uMsg == WM_LBUTTONDBLCLK))
        {
            if (MainWindow->GetActivePanel() != FilesWindow)
                MainWindow->ChangePanel();
            if (uMsg == WM_LBUTTONDBLCLK && (Border & blTop))
                SendMessage(MainWindow->HWindow, WM_COMMAND, MAKEWPARAM(CM_ACTIVE_CHANGEDIR, 0), 0);
        }
        if (MouseCaptured)
        {
            SetCapture(HWindow);
            LButtonDownPoint.x = LOWORD(lParam);
            LButtonDownPoint.y = HIWORD(lParam);
            LButtonDown = (uMsg == WM_LBUTTONDOWN);
            RButtonDown = (uMsg == WM_RBUTTONDOWN);

            if ((uMsg == WM_LBUTTONDOWN || uMsg == WM_LBUTTONDBLCLK) && HotHistory)
            {
                FilesWindow->OpenDirHistory();
            }

            if ((uMsg == WM_LBUTTONDOWN || uMsg == WM_LBUTTONDBLCLK) && HotHidden)
            {
                FilesWindow->OpenStopFilterMenu();
            }

            if (uMsg == WM_LBUTTONDBLCLK && HotItem != NULL && (Border & blTop))
            {
                SendMessage(MainWindow->HWindow, WM_COMMAND, MAKEWPARAM(CM_ACTIVE_CHANGEDIR, 0), 0);
            }
        }
        break;
    }

    case WM_LBUTTONUP:
    case WM_RBUTTONUP:
    {
        SetCurrentToolTip(NULL, 0);
        if (MouseCaptured && uMsg == WM_LBUTTONUP && (HotItem != NULL || HotSize || HotZoom || HotSecurity))
        {
            if (GetCapture() == HWindow)
                ReleaseCapture();
            int x = abs(LButtonDownPoint.x - (short)LOWORD(lParam));
            int y = abs(LButtonDownPoint.y - (short)HIWORD(lParam));
            if (x <= GetSystemMetrics(SM_CXDRAG) && y <= GetSystemMetrics(SM_CYDRAG))
            {
                if (HotItem != NULL)
                {
                    if (Border & blTop)
                    {
                        if (MainWindow->GetActivePanel() != FilesWindow)
                            MainWindow->ChangePanel();

                        CHotTrackItem* lastItem = NULL;
                        if (HotTrackItems.Count > 0)
                            lastItem = &HotTrackItems[HotTrackItems.Count - 1];
                        //if (HotItem->Chars != (int)TextLen) // this condition failed when filter was attached
                        if (HotItem != lastItem)
                        {
                            if (FilesWindow->Is(ptDisk))
                            {
                                std::wstring pathW = TextW.substr(0, HotItem->Chars);
                                if (FilesWindow->ChangePathToDisk(FilesWindow->HWindow, pathW.c_str(), -1, NULL, NULL, TRUE))
                                {
                                    FilesWindow->DirectoryLine->InvalidateAndUpdate(TRUE);
                                    UpdateWindow(MainWindow->HWindow);
                                }
                            }
                            else
                            {
                                // path truncation
                                // GetHotTextW already implements the correct
                                // narrow/wide-aware slicing (fixed for the sibling blBottom/
                                // context-menu callers earlier/184): Offset is always 0
                                // for blTop (directory-line breadcrumb) items, so this is an
                                // exact drop-in for the old strncpy(path, Text, HotItem->Chars)
                                // truncation, minus the buffer/units mismatch that made Text
                                // (a narrow, potentially lossy mirror) wrong once UseWideText
                                // items land here (Offset/Chars are wide-character indices
                                // computed against TextW in BuildHotTrackItems).
                                //
                                // CORRECTED. This used to claim
                                // CompleteDirectoryLineHotPath was "still applied via
                                // GetHotTextW's narrow branch" - it was not: that branch was
                                // unreachable, so the plugin-FS path adjustment never ran.
                                // GetHotTextW forwards to GetHotText now, which applies it
                                // unconditionally, so the claim is true by construction.
                                std::wstring pathW;
                                GetHotTextW(pathW);

                                if (FilesWindow->ChangeDir(pathW.c_str(), -1, NULL, 2 /* as back/forward in history*/, NULL, FALSE))
                                {
                                    FilesWindow->DirectoryLine->InvalidateAndUpdate(TRUE);
                                    UpdateWindow(MainWindow->HWindow);
                                }
                            }
                        }
                        else
                        {
                            // click on last component -- change dir
                            SendMessage(MainWindow->HWindow, WM_COMMAND, MAKEWPARAM(CM_ACTIVE_CHANGEDIR, 0), 0);
                        }
                    }
                    if (Border & blBottom)
                    {
                        // HotItem->Offset/Chars are wide-character indices
                        // computed against TextW (BuildHotTrackItems). GetHotTextW does the
                        // slicing; there is no narrow mirror left to slice by mistake.
                        std::wstring hotTextW;
                        GetHotTextW(hotTextW);
                        if (CopyTextToClipboardW(hotTextW.c_str()))
                            FlashText(TRUE);
                    }
                }
                if (HotSize)
                {
                    // HotSize
                    FilesWindow->DriveInfo();
                }

                if (HotZoom)
                {
                    SendMessage(MainWindow->HWindow, WM_COMMAND,
                                MainWindow->LeftPanel == FilesWindow ? CM_LEFTZOOMPANEL : CM_RIGHTZOOMPANEL, 0);
                    UpdateWindow(MainWindow->HWindow); // so next WM_MOUSEMOVE comes to already redrawn window
                }

                if (HotSecurity)
                {
                    if (FilesWindow->Is(ptPluginFS) && FilesWindow->GetPluginFS()->NotEmpty())
                        FilesWindow->GetPluginFS()->ShowSecurityInfo(FilesWindow->HWindow);
                }
            }
        }
        LButtonDown = FALSE;
        RButtonDown = FALSE;
        if (!MainWindow->HelpMode && GetActiveWindow() == NULL)
            SetForegroundWindow(MainWindow->HWindow);
        break;
    }

    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HANDLES(BeginPaint(HWindow, &ps));
        Paint(ps.hdc);
        HANDLES(EndPaint(HWindow, &ps));
        return 0;
    }

    case WM_ERASEBKGND:
    {
        HotTrackItemsMeasured = FALSE; // font might have changed - let's measure again
        RECT r;
        GetClientRect(HWindow, &r);
        if (Width != r.right || Height != r.bottom)
        {
            Width = r.right;
            Height = r.bottom;
            ItemBitmap.Enlarge(Width, Height); // alokace bitmapy v ItemBitmap.HMemDC
        }
        HDC dc = (HDC)wParam;
        if (Border != blNone)
        {
            DarkModeMainFramePalette palette;
            BOOL useDark = DarkMode_GetMainFramePalette(&palette);
            HGDIOBJ oldPen = SelectObject(dc, useDark ? GetStockObject(DC_PEN) : BtnShadowPen);
            if (Border & blBottom)
            {
                if (useDark)
                    SetDCPenColor(dc, palette.LineDark);
                MoveToEx(dc, r.left, r.top, NULL);
                LineTo(dc, r.left, r.bottom);
                if (useDark)
                    SetDCPenColor(dc, palette.Border);
                else
                    SelectObject(dc, BtnHilightPen);
                MoveToEx(dc, r.right - 1, r.top, NULL);
                LineTo(dc, r.right - 1, r.bottom);
                MoveToEx(dc, r.left, r.bottom - 1, NULL);
                LineTo(dc, r.right, r.bottom - 1);
                r.bottom--;
            }
            SelectObject(dc, oldPen);
        }

        // j.r. all statusbars pull through one CBitmap cache

        return TRUE;
    }

    case WM_TIMER:
    {
        if (wParam == IDT_THROBBER)
        {
            if (StopStatusbarRepaint == 0)
            {
                ThrobberFrame++;
                if (ThrobberFrame >= THROBBER_COUNT)
                    ThrobberFrame = 0;
                NeedToInvalidate = TRUE;
                InvalidateIfNeeded();
                //        RepaintThrobber();  // j.r. FIXME RepaintThrobber() would be better, but misbehaves during window resize
            }
            else
                PostStatusbarRepaint = TRUE;
        }
        if (wParam == IDT_DELAYEDTHROBBER)
        {
            if (DelayedThrobber)
                SetThrobber(TRUE);
            else
            {
                KillTimer(HWindow, IDT_DELAYEDTHROBBER);
                TRACE_E("CStatusWindow::WindowProc(): Unexpected timer: IDT_DELAYEDTHROBBER");
            }
        }
        break;
    }
    }

    return CWindow::WindowProc(uMsg, wParam, lParam);
}

HIMAGELIST
CStatusWindow::CreateDragImage(const wchar_t* text, int& dxHotspot, int& dyHotspot, int& imgWidth, int& imgHeight)
{
    CALL_STACK_MESSAGE6("CStatusWindow::CreateDragImage(%S, %d, %d, %d, %d)",
                        text, dxHotspot, dyHotspot, imgWidth, imgHeight);
    int textLen = (int)wcslen(text);
    HDC hDC = ItemBitmap.HMemDC;
    HFONT hOldFont = (HFONT)SelectObject(hDC, Font);
    SIZE sz;
    GetTextExtentPoint32W(hDC, text, textLen, &sz);
    ItemBitmap.Enlarge(sz.cx, sz.cy); // alokace bitmapy v ItemBitmap.HMemDC
    // podmazu pozadi
    RECT r;
    r.left = 0;
    r.top = 0;
    r.right = sz.cx;
    r.bottom = sz.cy;
    FillRect(hDC, &r, HNormalBkBrush);
    int oldBkMode = SetBkMode(hDC, TRANSPARENT);
    int oldTextColor = SetTextColor(hDC, GetCOLORREF(CurrentColors[ITEM_FG_NORMAL]));
    DrawTextW(hDC, text, textLen, &r, DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
    SetTextColor(hDC, oldTextColor);
    SetBkMode(hDC, oldBkMode);
    SelectObject(hDC, hOldFont);

    dxHotspot = -15;
    dyHotspot = 0;

    imgWidth = sz.cx;
    imgHeight = sz.cy;
    HIMAGELIST himl = ImageList_Create(sz.cx, sz.cy, ILC_COLORDDB | ILC_MASK, 1, 0);
    SelectObject(ItemBitmap.HMemDC, ItemBitmap.HOldBmp); // temporarily release bitmap from HMemDC
    ImageList_AddMasked(himl, ItemBitmap.HBmp, GetCOLORREF(CurrentColors[ITEM_BK_NORMAL]));
    SelectObject(ItemBitmap.HMemDC, ItemBitmap.HBmp); // select it again
    return himl;
}

BOOL CStatusWindow::GetTextFrameRect(RECT* r)
{
    CALL_STACK_MESSAGE_NONE
    if (HWindow == NULL)
        return FALSE;
    GetWindowRect(HWindow, r);
    //  r->left += TextRect.left - 2;
    r->top += 2;
    r->bottom -= 2;
    return TRUE;
}

BOOL CStatusWindow::GetFilterFrameRect(RECT* r)
{
    CALL_STACK_MESSAGE_NONE
    if (HWindow == NULL)
        return FALSE;

    if (!Hidden)
        return GetTextFrameRect(r);

    *r = HiddenRect;
    MapWindowPoints(HWindow, NULL, (POINT*)r, 2);
    return TRUE;
}

void CStatusWindow::OnColorsChanged()
{
    ItemBitmap.ReCreateForScreenDC();

    if (ToolBar != NULL)
    {
        ToolBar->OnColorsChanged();
        if (ToolBar->HWindow != NULL)
        {
            InvalidateRect(ToolBar->HWindow, NULL, TRUE);
            UpdateWindow(ToolBar->HWindow);
        }
    }

    if (HWindow != NULL)
    {
        InvalidateRect(HWindow, NULL, TRUE);
        UpdateWindow(HWindow);
    }
}

void CStatusWindow::SetFont()
{
    // font size might have changed
    InvalidateRect(HWindow, NULL, TRUE);
    BuildHotTrackItems();
}
