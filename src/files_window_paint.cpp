// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"

#include "cfgdlg.h"
#include "mainwnd.h"
#include "plugins.h"
#include "fileswnd.h"
#include "filesbox.h"
#include "shiconov.h"
#include "common/PanelTextPainter.h"
#include "common/text/CaseFolding.h"
#include "common/unicode/NameRenderPolicy.h"
#include "common/unicode/helpers.h" // FoldCharW
#include "darkmode.h"
#include "sal_colors.h"

//****************************************************************************
//
// Panel paint
//
//  - Brief mode
//  - Detailed mode
//  - Icons mode
//  - Thumbnails mode
//

//****************************************************************************
//
// CFilesWindow::SetFontAndColors
//

void CFilesWindow::SetFontAndColors(HDC hDC, CHighlightMasksItem* highlightMasksItem, CFileData* f,
                                    BOOL isItemFocusedOrEditMode, int itemIndex)
{
    // local scope enum
    enum ColorModeEnum
    {
        cmeNormal,
        cmeFocused,
        cmeSelected,
        cmeFocSel
    };

    int colorMode;

    if (!f->Selected == 1 && !isItemFocusedOrEditMode)
        colorMode = cmeNormal;
    else if (f->Selected == 0 && isItemFocusedOrEditMode)
        colorMode = cmeFocused;
    else if (f->Selected == 1 && !isItemFocusedOrEditMode)
        colorMode = cmeSelected;
    else
        colorMode = cmeFocSel;

    BOOL useDark = DarkMode_ShouldUseDark();

    if (TrackingSingleClick && SingleClickIndex == itemIndex)
    {
        SelectObject(hDC, FontUL);
        SetTextColor(hDC, GetCOLORREF(CurrentColors[HOT_PANEL]));
    }
    else
    {
        SelectObject(hDC, Font);
        // text color
        SALCOLOR* fgColor;
        if (highlightMasksItem == NULL)
        {
            if (colorMode == cmeNormal)
                fgColor = &CurrentColors[ITEM_FG_NORMAL];
            else if (colorMode == cmeFocused)
                fgColor = &CurrentColors[ITEM_FG_FOCUSED];
            else if (colorMode == cmeSelected)
                fgColor = &CurrentColors[ITEM_FG_SELECTED];
            else
                fgColor = &CurrentColors[ITEM_FG_FOCSEL];
        }
        else
        {
            if (colorMode == cmeNormal)
                fgColor = &highlightMasksItem->NormalFg;
            else if (colorMode == cmeFocused)
                fgColor = &highlightMasksItem->FocusedFg;
            else if (colorMode == cmeSelected)
                fgColor = &highlightMasksItem->SelectedFg;
            else
                fgColor = &highlightMasksItem->FocSelFg;
        }
        COLORREF fg = GetCOLORREF(*fgColor);
        // Base (non-mask) selection/focused-selection text is stored flag-0; resolve its dark
        // value here at render time so the schemes keep their light RGB and a dark->light
        // switch restores the light colors (issue #81 follow-up). fgColor pointing into
        // CurrentColors identifies the base path (masks point into highlightMasksItem).
        if (fgColor == &CurrentColors[ITEM_FG_SELECTED])
            fg = ResolveDarkBaseColor(ITEM_FG_SELECTED, fg, useDark);
        else if (fgColor == &CurrentColors[ITEM_FG_FOCSEL])
            fg = ResolveDarkBaseColor(ITEM_FG_FOCSEL, fg, useDark);
        // In dark mode, mask text that is too dark for the dark background must fall back to
        // the panel foreground. We adjust at render time (not in UpdateDefaultColors) to
        // avoid corrupting saved mask data.
        else if (useDark && highlightMasksItem != NULL)
        {
            BYTE gray = GetGrayscaleFromRGB(GetRValue(fg), GetGValue(fg), GetBValue(fg));
            if (gray < 96)
            {
                if (colorMode == cmeNormal)
                    fg = GetCOLORREF(CurrentColors[ITEM_FG_NORMAL]);
                else if (colorMode == cmeFocused)
                    fg = GetCOLORREF(CurrentColors[ITEM_FG_FOCUSED]);
                else if (colorMode == cmeSelected)
                    fg = ResolveDarkBaseColor(ITEM_FG_SELECTED, GetCOLORREF(CurrentColors[ITEM_FG_SELECTED]), useDark);
                else
                    fg = ResolveDarkBaseColor(ITEM_FG_FOCSEL, GetCOLORREF(CurrentColors[ITEM_FG_FOCSEL]), useDark);
            }
        }
        SetTextColor(hDC, fg);
    }

    // set background color
    SALCOLOR* bkColor;
    if (highlightMasksItem == NULL)
    {
        if (colorMode == cmeNormal)
            bkColor = &CurrentColors[ITEM_BK_NORMAL];
        else if (colorMode == cmeFocused)
            bkColor = &CurrentColors[ITEM_BK_FOCUSED];
        else if (colorMode == cmeSelected)
            bkColor = &CurrentColors[ITEM_BK_SELECTED];
        else
            bkColor = &CurrentColors[ITEM_BK_FOCSEL];
    }
    else
    {
        if (colorMode == cmeNormal)
            bkColor = &highlightMasksItem->NormalBk;
        else if (colorMode == cmeFocused)
            bkColor = &highlightMasksItem->FocusedBk;
        else if (colorMode == cmeSelected)
            bkColor = &highlightMasksItem->SelectedBk;
        else
            bkColor = &highlightMasksItem->FocSelBk;
    }

    if (itemIndex == DropTargetIndex)
    {
        if (colorMode == cmeSelected)
            bkColor = &CurrentColors[ITEM_BK_SELECTED];
        else if (colorMode == cmeFocSel)
            bkColor = &CurrentColors[ITEM_BK_FOCSEL];
        else
            bkColor = &CurrentColors[ITEM_BK_FOCUSED];
    }
    COLORREF bk = GetCOLORREF(*bkColor);
    // Base/drop-target focus backgrounds are stored flag-0; resolve their dark value here so
    // the schemes keep their light RGB across a dark->light switch (issue #81 follow-up).
    // bkColor pointing into CurrentColors covers both the base path and the drop-target
    // override; masks (which point into highlightMasksItem) fall through to the branch below.
    if (bkColor == &CurrentColors[ITEM_BK_FOCUSED])
        bk = ResolveDarkBaseColor(ITEM_BK_FOCUSED, bk, useDark);
    else if (bkColor == &CurrentColors[ITEM_BK_FOCSEL])
        bk = ResolveDarkBaseColor(ITEM_BK_FOCSEL, bk, useDark);
    // In dark mode, mask backgrounds that are too light look wrong on the dark panel.
    // Normalize to the panel background for the current state.
    else if (useDark && highlightMasksItem != NULL && itemIndex != DropTargetIndex)
    {
        BYTE gray = GetGrayscaleFromRGB(GetRValue(bk), GetGValue(bk), GetBValue(bk));
        if (gray > 160)
        {
            if (colorMode == cmeNormal)
                bk = GetCOLORREF(CurrentColors[ITEM_BK_NORMAL]);
            else if (colorMode == cmeFocused)
                bk = ResolveDarkBaseColor(ITEM_BK_FOCUSED, GetCOLORREF(CurrentColors[ITEM_BK_FOCUSED]), useDark);
            else if (colorMode == cmeSelected)
                bk = GetCOLORREF(CurrentColors[ITEM_BK_SELECTED]);
            else
                bk = ResolveDarkBaseColor(ITEM_BK_FOCSEL, GetCOLORREF(CurrentColors[ITEM_BK_FOCSEL]), useDark);
        }
    }
    SetBkColor(hDC, bk);
}

//****************************************************************************
//
// CFilesWindow::DrawIcon
//

void CFilesWindow::DrawIcon(HDC hDC, CFileData* f, BOOL isDir, BOOL isItemUpDir,
                            BOOL isItemFocusedOrEditMode, int x, int y, CIconSizeEnum iconSize,
                            const RECT* overlayRect, DWORD drawFlags)
{
    BOOL drawSimpleSymbol = FALSE;
    int symbolIndex;                   // index in the Symbols bitmap...
    DWORD iconState = 0;               // flags for drawing the icon
    std::wstring lowerExtension; // extension in lowercase

    if (!(drawFlags & DRAWFLAG_NO_STATE))
    {
        if ((f->Hidden == 1 || f->CutToClip == 1) && !isItemUpDir)
        {
            if (f->CutToClip == 0 && f->Selected == 1)
                iconState |= IMAGE_STATE_SELECTED; //hidden items will have the selected state (not cut)
            else
                iconState |= IMAGE_STATE_HIDDEN;
        }
        else
        {
            if (isItemFocusedOrEditMode)
                iconState |= IMAGE_STATE_FOCUSED;
            if (!isItemUpDir)
                if (f->Selected == 1)
                    iconState |= IMAGE_STATE_SELECTED;
        }
    }

    if (drawFlags & DRAWFLAG_MASK)
        iconState |= IMAGE_STATE_MASK;

    if (f->IsLink)
        iconState |= IMAGE_STATE_SHORTCUT;

    if (f->IsOffline)
        iconState |= IMAGE_STATE_OFFLINE;

    if (!isDir)
    {
        // convert extension characters to lowercase
        // FoldCharW, not LowerCase[]: that is a 256-entry BYTE table
        // (common/str.h:23) and a wchar_t index read out of bounds above U+00FF. This buffer
        // is the LOOKUP key for Associations.GetIndex, whose STORED keys are folded by the
        // same table at icncache.cpp - so both folds move in this one commit, and the array
        // is rebuilt in memory by ReadAssociations() on every start, which is why no
        // migration or re-sort is needed. NOT towlower(): see the note on FoldCharW itself.
        // ⚠ The *(DWORD*)dstExt = 0 below is load-bearing - CompareDWORDS reads past
        // lengthBytes in DWORD steps, so the two-wide-NUL pad keeps that read defined.
        lowerExtension = sally::text::Fold(f->Ext);
        lowerExtension.append(2, L'\0'); // CompareDWORDS reads through DWORD alignment.

        // _wcsicmp: a DWORD spans four narrow characters but only TWO
        // wide ones, so *(DWORD*)lowerExtension == *(DWORD*)"exe" could never match once
        // the extension went wide - every executable drew the wrong icon. The compiler
        // never complained, because both sides are still DWORDs.
        if (_wcsicmp(lowerExtension.c_str(), L"exe") == 0 ||
            _wcsicmp(lowerExtension.c_str(), L"bat") == 0 ||
            _wcsicmp(lowerExtension.c_str(), L"pif") == 0 ||
            _wcsicmp(lowerExtension.c_str(), L"com") == 0 ||
            _wcsicmp(lowerExtension.c_str(), L"scf") == 0 ||
            _wcsicmp(lowerExtension.c_str(), L"scr") == 0 ||
            _wcsicmp(lowerExtension.c_str(), L"cmd") == 0)
            symbolIndex = symbolsExecutable;
        else
            symbolIndex = (f->Association) ? (f->Archive ? symbolsArchive : symbolsAssociated) : symbolsNonAssociated;
    }
    else
    {
        if (isItemUpDir)
        {
            symbolIndex = symbolsUpDir;
        }
        else
        {
            symbolIndex = symbolsDirectory;
            if (f->Shared)
                iconState |= IMAGE_STATE_SHARED;
        }
    }

    BOOL iconOverlayFromPlugin = Is(ptZIPArchive) || Is(ptPluginFS);
    int pluginIconOverlaysCount = 0;
    HICON* pluginIconOverlays = NULL;
    if (iconOverlayFromPlugin && f->IconOverlayIndex != ICONOVERLAYINDEX_NOTUSED)
    {
        CPluginData* plugin = GetPluginDataForPluginIface();
        if (plugin != NULL && plugin->IconOverlaysCount > 0)
        {
            pluginIconOverlaysCount = plugin->IconOverlaysCount;
            pluginIconOverlays = plugin->IconOverlays;
        }
    }

    // TemporarilySimpleIcons==TRUE is set while switching panel modes
    // so we don't use the not-yet-prepared IconCache for drawing icons
    if (!TemporarilySimpleIcons && UseSystemIcons)
    {
        if (symbolIndex != symbolsUpDir && symbolIndex != symbolsArchive)
        {
            CIconList* iconList = NULL;
            int iconListIndex = -1; // close it if not set
            std::wstring fileName;

            if (GetPluginIconsType() != pitFromPlugin || !Is(ptPluginFS))
            {
                if (isDir) // it's a directory
                {
                    int icon;
                    fileName.assign(f->Name, f->NameLen);

                    if (!IconCache->GetIndex(fileName.c_str(), icon, NULL, NULL) ||                     // the icon-thread isn't loading it
                        IconCache->At(icon).GetFlag() != 1 && IconCache->At(icon).GetFlag() != 2 ||     // neither new nor old icon is loaded
                        !IconCache->GetIcon(IconCache->At(icon).GetIndex(), &iconList, &iconListIndex)) // failed to obtain its icon
                    {                                                                                   // we will display a simple symbol
                        if (!Associations.GetIcon(ASSOC_ICON_SOME_DIR, &iconList, &iconListIndex, iconSize))
                        {
                            iconList = NULL;
                            drawSimpleSymbol = TRUE;
                        }
                    }
                }
                else // it's a file
                {
                    int index;
                    // _wcsicmp - see the note at the top of this function.
                    BOOL exceptions = _wcsicmp(lowerExtension.c_str(), L"scr") == 0 || // icons in the file,
                                      _wcsicmp(lowerExtension.c_str(), L"pif") == 0 || // even though it isn't visible
                                      _wcsicmp(lowerExtension.c_str(), L"lnk") == 0;   // in the Registry

                    if (exceptions || Associations.GetIndex(lowerExtension.c_str(), index)) // the extension has an icon (association)
                    {
                        if (!exceptions)
                            TransferAssocIndex = index;                               // remember the valid index in Associations
                        if (exceptions || Associations[index].GetIndex(iconSize) < 0) // dynamic icon (from the file) or a loaded static icon
                        {                                                             // icon in the file
                            int icon;
                            fileName.assign(f->Name, f->NameLen);
                            if (!IconCache->GetIndex(fileName.c_str(), icon, NULL, NULL) ||                 // the icon-thread isn't loading it
                                IconCache->At(icon).GetFlag() != 1 && IconCache->At(icon).GetFlag() != 2 || // neither new nor old icon is loaded
                                !IconCache->GetIcon(IconCache->At(icon).GetIndex(),
                                                    &iconList, &iconListIndex)) // failed to obtain loaded icon
                            {                                                   // we will display a simple symbol
                                // _wcsicmp - same defect as above.
                                if (_wcsicmp(lowerExtension.c_str(), L"pif") == 0 ||
                                    _wcsicmp(lowerExtension.c_str(), L"exe") == 0 ||
                                    _wcsicmp(lowerExtension.c_str(), L"com") == 0 ||
                                    _wcsicmp(lowerExtension.c_str(), L"bat") == 0 ||
                                    _wcsicmp(lowerExtension.c_str(), L"scf") == 0 ||
                                    _wcsicmp(lowerExtension.c_str(), L"scr") == 0 ||
                                    _wcsicmp(lowerExtension.c_str(), L"cmd") == 0)
                                    icon = ASSOC_ICON_SOME_EXE;
                                else
                                    icon = (exceptions || Associations[index].GetFlag() != 0) ? ASSOC_ICON_SOME_FILE : ASSOC_ICON_NO_ASSOC;
                                if (!Associations.GetIcon(icon, &iconList, &iconListIndex, iconSize))
                                {
                                    iconList = NULL;
                                    drawSimpleSymbol = TRUE;
                                }
                            }
                            index = -2; // a dynamic icon will be displayed
                        }
                        // else -> index is the icon index shared by files with this extension
                    }
                    else
                    {
                        index = -1;              // the file has no special icon
                        TransferAssocIndex = -1; // remember that this extension isn't in Associations
                    }
                    if (index != -2)     // icon in the file, first pass -> don't read
                    {                    // try to draw the icon,
                        if (index != -1) // index==-1 -> simple-symbol
                        {
                            int i = Associations.At(index).GetIndex(iconSize);
                            if (i >= 0)
                                index = i;
                            else
                            {
                                TRACE_E("Unexpected situation.");
                                drawSimpleSymbol = TRUE;
                            }
                        }
                        else
                            index = ASSOC_ICON_NO_ASSOC;
                        if (!drawSimpleSymbol && !Associations.GetIcon(index, &iconList, &iconListIndex, iconSize))
                        {
                            iconList = NULL;
                            drawSimpleSymbol = TRUE;
                        }
                    }
                }
            }
            else // FS, pitFromPlugin
            {
                int icon;
                //        memmove(fileName, f->Name, f->NameLen);
                //        *(DWORD *)(fileName + f->NameLen) = 0;

                if (!IconCache->GetIndex(NULL /*fileName*/, icon, &PluginData, f) ||                // the icon-thread isn't loading it
                    IconCache->At(icon).GetFlag() != 1 && IconCache->At(icon).GetFlag() != 2 ||     // neither new nor old icon is loaded
                    !IconCache->GetIcon(IconCache->At(icon).GetIndex(), &iconList, &iconListIndex)) // failed to obtain its icon
                {                                                                                   // we will display a simple symbol from the plug-in
                    // the simple icon index will be obtained via the GetPluginIconIndex callback
                    TransferFileData = f;
                    TransferIsDir = isDir ? (isItemUpDir ? 2 : 1) : 0;
                    // The v107 View adapter selects the stable transfer
                    // scratch through this reverse identity. Column drawing already
                    // initializes it below; the simple-icon callback needs the same
                    // current-panel identity even though native callbacks historically
                    // consumed only TransferFileData and TransferIsDir.
                    TransferPluginDataIface = PluginData.GetInterface();

                    iconList = SimplePluginIcons;
                    iconListIndex = GetPluginIconIndex();
                }
            }

            if (iconList != NULL && !drawSimpleSymbol)
            {
                BOOL leaveSection;
                if (!IconCacheValid)
                {
                    HANDLES(EnterCriticalSection(&ICSectionUsingIcon));
                    leaveSection = TRUE;
                }
                else
                    leaveSection = FALSE;

                StateImageList_Draw(iconList, iconListIndex, hDC, x, y, iconState, iconSize,
                                    f->IconOverlayIndex, overlayRect, (drawFlags & DRAWFLAG_OVERLAY_ONLY) != 0,
                                    iconOverlayFromPlugin, pluginIconOverlaysCount, pluginIconOverlays);

                if (leaveSection)
                {
                    HANDLES(LeaveCriticalSection(&ICSectionUsingIcon));
                }
            }
        }
        else
            drawSimpleSymbol = TRUE; // the ".." directory
    }
    else
        drawSimpleSymbol = TRUE;

    if (drawSimpleSymbol)
    { // simple symbols
        StateImageList_Draw(SimpleIconLists[iconSize], symbolIndex, hDC, x, y, iconState, iconSize,
                            f->IconOverlayIndex, overlayRect, (drawFlags & DRAWFLAG_OVERLAY_ONLY) != 0,
                            iconOverlayFromPlugin, pluginIconOverlaysCount, pluginIconOverlays);
    }

    // I don't understand why, but the current bitmap drawing doesn't flicker.
    // probably in the case of blending the icons are drawn via memory dc
    //    BitBlt(hDC, iconRect.left, iconRect.top,
    //           iconRect.right - iconRect.left,
    //           iconRect.bottom - iconRect.top,
    //           ItemMemDC, 0, 0, SRCCOPY);
}

//****************************************************************************
//
// FillIntersectionRegion
//
// using FillRect fills the areas between
// oRect (outer) and iRect (inner) rectangles
//

void FillIntersectionRegion(HDC hDC, const RECT* oRect, const RECT* iRect)
{
    RECT r;

    // area to the left of innerRect
    r = *oRect;
    r.right = iRect->left;
    if (r.left < r.right && r.top < r.bottom)
        FillRect(hDC, &r, HNormalBkBrush);

    // area to the right of iRect
    r = *oRect;
    r.left = iRect->right;
    if (r.left < r.right && r.top < r.bottom)
        FillRect(hDC, &r, HNormalBkBrush);

    // area above iRect
    r = *iRect;
    r.top = oRect->top;
    r.bottom = iRect->top;
    if (r.left < r.right && r.top < r.bottom)
        FillRect(hDC, &r, HNormalBkBrush);

    // area below iRect
    r = *iRect;
    r.top = iRect->bottom;
    r.bottom = oRect->bottom;
    if (r.left < r.right && r.top < r.bottom)
        FillRect(hDC, &r, HNormalBkBrush);
}

//****************************************************************************
//
// DrawFocusRect
//
// Draws a solid or dashed focus around the item
//

void DrawFocusRect(HDC hDC, const RECT* r, BOOL selected, BOOL editMode)
{
    HPEN hPen;
    if (editMode)
    {
        COLORREF bkColor;
        if (selected)
        {
            hPen = HInactiveSelectedPen;
            bkColor = GetCOLORREF(CurrentColors[FOCUS_BK_INACTIVE_SELECTED]);
        }
        else
        {
            hPen = HInactiveNormalPen;
            bkColor = GetCOLORREF(CurrentColors[FOCUS_BK_INACTIVE_NORMAL]);
        }
        SetBkColor(hDC, bkColor);
        SetBkMode(hDC, OPAQUE);
    }
    else
    {
        if (selected)
            hPen = HActiveSelectedPen;
        else
            hPen = HActiveNormalPen;
    }

    SelectObject(hDC, hPen);
    SelectObject(hDC, HANDLES(GetStockObject(NULL_BRUSH)));
    Rectangle(hDC, r->left, r->top, r->right, r->bottom);
}

//****************************************************************************
//
// CFilesWindow::DrawBriefDetailedItem
//
// Draws an item in Brief and Detailed modes
//

//
// changes in item painting (layout) must be reflected in these locations:
//
// CFilesWindow::DrawItem(WPARAM wParam, LPARAM lParam)
// CFilesBox::GetIndex(int x, int y)
// CFilesMap::CreateMap()
//

wchar_t DrawItemBuff[1024];     // destination buffer for strings
wchar_t DrawItemBuffW[1024]; // destination buffer for wide strings (Unicode filenames)
int DrawItemAlpDx[1024];     // for width calculations of columns with FixedWidth bit + elastic columns with smart mode

void CFilesWindow::DrawBriefDetailedItem(HDC hTgtDC, int itemIndex, RECT* itemRect, DWORD drawFlags)
{
    CALL_STACK_MESSAGE_NONE
    if (itemIndex < 0 || itemIndex >= Dirs->Count + Files->Count)
    {
        TRACE_E("itemIndex = " << itemIndex);
        return;
    }

    if (!CanDrawItems)
        return;

    BOOL isDir = itemIndex < Dirs->Count;
    CFileData* f = isDir ? &Dirs->At(itemIndex) : &Files->At(itemIndex - Dirs->Count);

    if ((drawFlags & DRAWFLAG_DIRTY_ONLY) && f->Dirty == 0)
        return;

    BOOL isItemUpDir = FALSE; // is the drawn item the ".." directory?

    if (itemIndex == 0 && isDir && *f->Name == '.' && *(f->Name + 1) == '.' &&
        *(f->Name + 2) == 0) // "up-dir" can only be first
    {
        if (drawFlags & DRAWFLAG_DIRTY_ONLY)
            return;
        isItemUpDir = TRUE;
    }

    //  TRACE_I("DrawingSmall itemIndex="<<dec<<itemIndex<<" y="<<itemRect->top);

    BOOL isItemFocusedOrEditMode = FALSE;
    if (FocusedIndex == itemIndex) // drawing the cursor
    {
        if (FocusVisible || Parent->EditMode && Parent->GetActivePanel() == this) // switched in the command-line
            isItemFocusedOrEditMode = TRUE;
    }
    if (drawFlags & DRAWFLAG_NO_FRAME)
        isItemFocusedOrEditMode = FALSE;

    BOOL fullRowHighlight = FALSE;
    if (GetViewMode() == vmDetailed && !Configuration.FullRowSelect && Configuration.FullRowHighlight)
        fullRowHighlight = TRUE;

    int xOffset = ListBox->XOffset;

    RECT rect = *itemRect; // item position shifted by xOffset
    rect.left -= xOffset;
    rect.right -= xOffset;

    HDC hDC;
    int cacheValidWidth; // meaningful only when using the cache and specifies the number of pixels (width),
                         // which are rendered in the bitmap and must be transferred to the screen
    if (drawFlags & DRAWFLAG_SELFOC_CHANGE)
    {
        //    TRACE_I("drawing index="<<itemIndex);
        // if it is only the cursor position change, skip visibility tests
        drawFlags |= DRAWFLAG_SKIP_VISTEST;

        // we are drawing only a small area and have plenty of time - so using the cache is fine
        hDC = ItemBitmap.HMemDC;

        // the bitmap is empty for now
        cacheValidWidth = 0;

        // we must adjust the rect variable
        rect.top -= itemRect->top;
        rect.bottom -= itemRect->top;
        rect.left -= itemRect->left;
        rect.right -= itemRect->left;
    }
    else
        hDC = hTgtDC; // this is a large paint; skip the cache and draw directly to the screen

    // if not prohibited, clear the flag
    if (!(drawFlags & DRAWFLAG_KEEP_DIRTY))
        f->Dirty = 0;

    TransferAssocIndex = -2; // we haven't looked up the extension for the drawn item in Associations yet

    //*****************************************
    //
    // drawing the icon
    //

    RECT iconRect = rect;
    iconRect.right = iconRect.left + 1 + IconSizes[ICONSIZE_16] + 1;

    if (drawFlags & DRAWFLAG_SKIP_VISTEST || RectVisible(hDC, &iconRect))
    {
        RECT innerRect;
        innerRect.left = iconRect.left + 1;                                                           // iconX
        innerRect.top = iconRect.top + (iconRect.bottom - iconRect.top - IconSizes[ICONSIZE_16]) / 2; // iconY
        innerRect.right = innerRect.left + IconSizes[ICONSIZE_16];
        innerRect.bottom = innerRect.top + IconSizes[ICONSIZE_16];

        // clear the area around the icon
        if ((drawFlags & DRAWFLAG_MASK) == 0) // when drawing the mask (b&w), the background color must not be painted
            FillIntersectionRegion(hDC, &iconRect, &innerRect);

        /*
    SHFILEINFO shi;
    HIMAGELIST hl = (HIMAGELIST)SHGetFileInfo("c:\\z\\cross_i.cur", 0, &shi, sizeof(shi),
                  SHGFI_SYSICONINDEX | SHGFI_SMALLICON | SHGFI_SHELLICONSIZE);
//    DrawIconEx(hDC, innerRect.left, innerRect.top, shi.hIcon, 16, 16, 0, NULL,
//               DI_NORMAL);
    ImageList_Draw(hl, shi.iIcon, hDC, innerRect.left, innerRect.top, ILD_NORMAL);
*/

        DrawIcon(hDC, f, isDir, isItemUpDir, isItemFocusedOrEditMode,
                 innerRect.left, innerRect.top, ICONSIZE_16, NULL, drawFlags);

        if (drawFlags & DRAWFLAG_SELFOC_CHANGE)
            cacheValidWidth += 1 + IconSizes[ICONSIZE_16] + 1;
    }

    BOOL showCaret = FALSE;
    if (!(drawFlags & DRAWFLAG_ICON_ONLY))
    {
        // Wide name/extension, not the narrow f->Name/f->Ext mirror - see
        // CHighlightMasks::AgreeMasks uses the exact wide name and extension
        // (or mis-match) a file whose true name isn't representable in the code page.
        std::wstring highlightNameW(f->Name);
        const wchar_t* highlightExtW = isDir ? NULL : sally::unicode::GetWideExtensionStart(highlightNameW.c_str());
        CHighlightMasksItem* highlightMasksItem = MainWindow->HighlightMasks->AgreeMasks(highlightNameW.c_str(), highlightExtW, f->Attr);

        sally::unicode::NameColumnViewMode nameColumnMode =
            GetViewMode() == vmBrief ? sally::unicode::NameColumnViewMode::Brief : sally::unicode::NameColumnViewMode::Detailed;
        sally::unicode::NameWidthMeasurementPlan namePlan =
            sally::unicode::BuildNameWidthMeasurementPlan(
                f->Name,
                f->NameLen,
                f->Ext,
                isDir != FALSE,
                Configuration.SortDirsByExt != FALSE,
                IsExtensionInSeparateColumn() != FALSE,
                nameColumnMode);
        int nameLen = namePlan.NameLength;

        // Apply the user's configured name-display format. Computed here (not
        // gated on the name column's own RectVisible check) because the extension column -
        // drawn separately below, with its own independent visibility check - also needs it.
        // AlterFileNameW's per-character case mapping is length-preserving, so
        // namePlan.NameLength (the dot position found in the original f->Name) stays a valid
        // name/extension split point against this altered copy.
        // Was gated on namePlan.UseWide over f->NameW. That member is gone, so
        // the guard was permanently false and every row silently got an EMPTY alteredNameW;
        // only the narrow arms below were doing any work. Unconditional over f->Name now.
        std::wstring alteredNameW = AlterFileNameW(f->Name, Configuration.FileNameFormat, 0, isDir);

        // set the the applied font, background color and text color
        SetFontAndColors(hDC, highlightMasksItem, f, isItemFocusedOrEditMode, itemIndex);

        RECT r = rect;
        int x = r.left;
        int y = (r.top + r.bottom - FontCharHeight) / 2;

        SIZE textSize;
        SetTextAlign(hDC, TA_TOP | TA_LEFT | TA_NOUPDATECP);

        // valid if the focus is shortened
        BOOL focusFrameRightValid = FALSE;
        int focusFrameRight;

        if (QuickSearchMode && itemIndex == FocusedIndex)
        {
            HideCaret(ListBox->HWindow);
            showCaret = TRUE;
        }

        int nameWidth = Columns[0].Width;

        r.left = x + 1 + IconSizes[ICONSIZE_16] + 1;
        r.right = x + nameWidth;

        // if the item is framed, draw only its interior
        BOOL forFrameAdjusted = FALSE;
        RECT adjR = r;
        if ((itemIndex == DropTargetIndex || isItemFocusedOrEditMode))
        {
            forFrameAdjusted = TRUE;
            adjR.top++;
            adjR.bottom--;
        }

        //*****************************************
        //
        // text drawing
        //
        // !!! Warning !!!
        // For smooth rendering of the whole line the GdiBatchLimit is used
        // Ensure text drawing does not interrupt buffering
        // (GDI functions do not return BOOL they call GdiFlush())
        //
        //    GdiFlush();

        if (drawFlags & DRAWFLAG_MASK)
        {
            SetTextColor(hDC, RGB(0, 0, 0)); // we want black text on black background because of aliased fonts
            SetBkColor(hDC, RGB(0, 0, 0));
        }
        if (drawFlags & DRAWFLAG_DRAGDROP)
        {
            SetTextColor(hDC, GetCOLORREF(CurrentColors[ITEM_FG_FOCUSED])); // focused text and background
            SetBkColor(hDC, ResolveDarkBaseColor(ITEM_BK_FOCUSED, GetCOLORREF(CurrentColors[ITEM_BK_FOCUSED]), DarkMode_ShouldUseDark()));
        }

        if (drawFlags & DRAWFLAG_SKIP_VISTEST || RectVisible(hDC, &r))
        {
            if (GetViewMode() == vmBrief && Configuration.FullRowSelect)
            {
                r.right -= 10; // adjustment - we must create space for dragging the frame
                focusFrameRightValid = TRUE;
                focusFrameRight = r.right;
            }

            if (isItemUpDir)
            {
                nameLen = 0;
                if (GetViewMode() == vmBrief && !Configuration.FullRowSelect)
                {
                    adjR.right -= 10; // 10 - so we don't stretch across the full width
                    r.right -= 10;
                    focusFrameRightValid = TRUE;
                    focusFrameRight = adjR.right;
                }
                else if (!Configuration.FullRowSelect)
                {
                    focusFrameRightValid = TRUE;
                    focusFrameRight = r.right;
                }
            }
            CColumn* column = &Columns[0];
            SIZE fnSZ;
            int fitChars;
            int textWidth;
            if (!isItemUpDir && !Configuration.FullRowSelect)
            {
                // measure the actual text length
                if (GetViewMode() == vmDetailed && (column->FixedWidth == 1 || NarrowedNameColumn))
                {
                    textWidth = nameWidth - 1 - IconSizes[ICONSIZE_16] - 1 - 2 - SPACE_WIDTH;
                    GetTextExtentExPointW(hDC, alteredNameW.c_str(), nameLen, textWidth,
                                          &fitChars, DrawItemAlpDx, &fnSZ);
                    int newWidth = 1 + IconSizes[ICONSIZE_16] + 1 + 2 + fnSZ.cx + 3;
                    if (newWidth > nameWidth)
                        newWidth = nameWidth;
                    adjR.right = r.right = rect.left + newWidth;
                }
                else
                {
                    GetTextExtentPoint32W(hDC, alteredNameW.c_str(), nameLen, &fnSZ);
                    adjR.right = r.right = rect.left + 1 + IconSizes[ICONSIZE_16] + 1 + 2 + fnSZ.cx + 3;
                }

                // focus will also be shorter
                focusFrameRightValid = TRUE;
                focusFrameRight = r.right;
            }
            if (!isItemUpDir && GetViewMode() == vmDetailed && (column->FixedWidth == 1 || NarrowedNameColumn))
            {
                if (Configuration.FullRowSelect)
                {
                    // no width measured yet - let's measure it now
                    // the string may be longer than the available space and must end with "..."
                    textWidth = nameWidth - 1 - IconSizes[ICONSIZE_16] - 1 - 2 - SPACE_WIDTH;
                    GetTextExtentExPointW(hDC, alteredNameW.c_str(), nameLen, textWidth,
                                          &fitChars, DrawItemAlpDx, &fnSZ);
                }
                if (fitChars < nameLen)
                {
                    // search from the end for the character after which we can copy "..." and it fits in the column
                    while (fitChars > 0 && DrawItemAlpDx[fitChars - 1] + TextEllipsisWidth > textWidth)
                        fitChars--;
                    // copy a part of the original string to another buffer
                    // The deleted narrow arm carried the SplitText defect:
                    // memmove(DrawItemBuff, TransferBuffer, fitChars) counted BYTES over two
                    // wchar_t[1024] buffers, and memmove(..., "...", 3) wrote three narrow
                    // bytes into a wide buffer. It compiled clean. The wide arm below is the
                    // correct one, and is now the only one.
                    int totalCount;
                    if (fitChars > 0)
                    {
                        wmemmove(DrawItemBuffW, alteredNameW.c_str(), fitChars);
                        // and append "..."
                        wmemmove(DrawItemBuffW + fitChars, L"...", 3);
                        totalCount = fitChars + 3;
                    }
                    else
                    {
                        DrawItemBuffW[0] = alteredNameW[0];
                        DrawItemBuffW[1] = L'.';
                        totalCount = 2;
                    }
                    // DRAWFLAG_MASK: hack, under XP some stuff is added in font of the text in the mask while drawing short texts; not an issue if text is not drawn
                    sally::ui::DrawPanelTextW(hDC, r.left + 2, y, ETO_OPAQUE, &adjR, DrawItemBuffW, (drawFlags & DRAWFLAG_MASK) ? 0 : totalCount, NULL);
                    goto SKIP1;
                }
            }
            // DRAWFLAG_MASK: hack, under XP some stuff is added in font of the text in the mask while drawing short texts; not an issue if text is not drawn
            // nameLen, not the old nameLenW: isItemUpDir sets nameLen = 0
            // above but never touched nameLenW, so the wide arm would have started painting
            // ".." text where the arm that actually ships paints only the background.
            sally::ui::DrawPanelTextW(hDC, r.left + 2, y, ETO_OPAQUE, &adjR, alteredNameW.c_str(), (drawFlags & DRAWFLAG_MASK) ? 0 : nameLen, NULL);
        SKIP1:
            if (!Configuration.FullRowSelect || GetViewMode() == vmBrief)
            {
                if (forFrameAdjusted)
                {
                    adjR.top--;
                    adjR.bottom++;
                    forFrameAdjusted = FALSE;
                }
                if (r.right < x + nameWidth)
                {
                    // clear the remainder of the name
                    adjR.left = r.left = r.right;
                    adjR.right = r.right = x + nameWidth;
                    SALCOLOR* bkColor = (highlightMasksItem == NULL) ? &CurrentColors[ITEM_BK_NORMAL] : &highlightMasksItem->NormalBk;
                    if (fullRowHighlight && isItemFocusedOrEditMode)
                        bkColor = (highlightMasksItem == NULL) ? &CurrentColors[ITEM_BK_HIGHLIGHT] : &highlightMasksItem->HighlightBk;
                    COLORREF bkClr = GetCOLORREF(*bkColor);
                    if (DarkMode_ShouldUseDark() && highlightMasksItem != NULL)
                    {
                        BYTE gray = GetGrayscaleFromRGB(GetRValue(bkClr), GetGValue(bkClr), GetBValue(bkClr));
                        if (gray > 160)
                            bkClr = GetCOLORREF((fullRowHighlight && isItemFocusedOrEditMode) ? CurrentColors[ITEM_BK_HIGHLIGHT] : CurrentColors[ITEM_BK_NORMAL]);
                    }
                    SetBkColor(hDC, bkClr);
                    if (drawFlags & DRAWFLAG_MASK) // mask is b&w; we must not paint a colored background into it
                        SetBkColor(hDC, RGB(255, 255, 255));
                    sally::ui::DrawPanelTextW(hDC, 0, 0, ETO_OPAQUE, &r, L"", 0, NULL);
                }
            }
        }
        x += nameWidth;
        if (drawFlags & DRAWFLAG_SELFOC_CHANGE)
            cacheValidWidth = nameWidth;
        // if FullRowSelect is off and we draw only dirty items or only focus/select changed,
        // skip drawing the remaining columns - nothing changed there
        if (GetViewMode() == vmDetailed && (fullRowHighlight || Configuration.FullRowSelect ||
                                            !(drawFlags & DRAWFLAG_DIRTY_ONLY) && !(drawFlags & DRAWFLAG_SELFOC_CHANGE)))
        {
            if (!Configuration.FullRowSelect)
            {
                if (TrackingSingleClick && SingleClickIndex == itemIndex)
                    SelectObject(hDC, Font); // return to the normal font
                if (forFrameAdjusted)
                {
                    adjR.top--;
                    adjR.bottom++;
                    forFrameAdjusted = FALSE;
                }
                // set colors for the remaining columns
                SALCOLOR* fgColor = (highlightMasksItem == NULL) ? &CurrentColors[ITEM_FG_NORMAL] : &highlightMasksItem->NormalFg;
                SALCOLOR* bkColor = (highlightMasksItem == NULL) ? &CurrentColors[ITEM_BK_NORMAL] : &highlightMasksItem->NormalBk;
                if (fullRowHighlight && isItemFocusedOrEditMode)
                {
                    fgColor = (highlightMasksItem == NULL) ? &CurrentColors[ITEM_FG_HIGHLIGHT] : &highlightMasksItem->HighlightFg;
                    bkColor = (highlightMasksItem == NULL) ? &CurrentColors[ITEM_BK_HIGHLIGHT] : &highlightMasksItem->HighlightBk;
                }
                COLORREF fgClr = GetCOLORREF(*fgColor);
                COLORREF bkClr = GetCOLORREF(*bkColor);
                if (DarkMode_ShouldUseDark() && highlightMasksItem != NULL)
                {
                    BYTE fgGray = GetGrayscaleFromRGB(GetRValue(fgClr), GetGValue(fgClr), GetBValue(fgClr));
                    if (fgGray < 96)
                        fgClr = GetCOLORREF((fullRowHighlight && isItemFocusedOrEditMode) ? CurrentColors[ITEM_FG_HIGHLIGHT] : CurrentColors[ITEM_FG_NORMAL]);
                    BYTE bkGray = GetGrayscaleFromRGB(GetRValue(bkClr), GetGValue(bkClr), GetBValue(bkClr));
                    if (bkGray > 160)
                        bkClr = GetCOLORREF((fullRowHighlight && isItemFocusedOrEditMode) ? CurrentColors[ITEM_BK_HIGHLIGHT] : CurrentColors[ITEM_BK_NORMAL]);
                }
                SetTextColor(hDC, fgClr);
                SetBkColor(hDC, bkClr);
                if (drawFlags & DRAWFLAG_MASK) // mask is b&w; we must not paint a colored background into it
                    SetBkColor(hDC, RGB(255, 255, 255));
            }

            // the order of additional columns is optional => their content will be obtained using
            // callback that fill the buffer with text and then we draw the column
            TransferPluginDataIface = PluginData.GetInterface();
            TransferFileData = f;
            TransferIsDir = isDir ? (isItemUpDir ? 2 : 1) : 0;
            TransferRowData = 0; // this variable must be zero for each row;
                                 // it is used for column drawing optimizations
                                 // within a single row

            int deltaX;
            // skip column Name (i==0) - it is already drawn
            int i;
            for (i = 1; i < Columns.Count; i++)
            {
                CColumn* column = &Columns[i];

                adjR.left = r.left = x;
                adjR.right = r.right = x + column->Width;

                // check whether the column area is at least partially visible
                if (drawFlags & DRAWFLAG_SKIP_VISTEST || RectVisible(hDC, &r))
                {
                    // retrieve the text
                    if (column->ID != COLUMN_ID_EXTENSION)
                    {
                        TransferActCustomData = column->CustomData;
                        column->GetText();
                    }
                    else
                    {
                        // extension is an exception - it always follows Name and we handle it explicitly
                        if (isDir && !Configuration.SortDirsByExt || f->Ext[0] == 0 || f->Ext <= f->Name + 1) // empty value in the Ext column (exception for names like ".htaccess", they appear in the Name column even though they are extensions)
                        {
                            TransferLen = 0;
                        }
                        else
                        {
                            // Slice the extension from the same format-altered wide copy as the
                            // name column, so a "lowercase extension" style applies consistently.
                            const wchar_t* extPosW = sally::unicode::GetWideExtensionStart(alteredNameW.c_str());
                            if (extPosW != NULL)
                            {
                                TransferLen = (int)wcslen(extPosW);
                                // Copy extension to wide buffer and draw it below.
                                wmemcpy(DrawItemBuffW, extPosW, TransferLen + 1);
                            }
                            else
                                TransferLen = 0;
                        }
                    }

                    if (TransferLen == 0)
                        sally::ui::DrawPanelTextW(hDC, r.left, y, ETO_OPAQUE, &adjR, L"", 0, NULL); // just clearing
                    else if (column->ID == COLUMN_ID_EXTENSION)
                    {
                        // Draw the formatted wide extension extracted above.
                        if (column->FixedWidth == 1)
                        {
                            int fitChars;
                            int textWidth = r.right - r.left - SPACE_WIDTH;
                            GetTextExtentExPointW(hDC, DrawItemBuffW, TransferLen, textWidth,
                                                  &fitChars, DrawItemAlpDx, &textSize);
                            if (fitChars < TransferLen)
                            {
                                while (fitChars > 0 && DrawItemAlpDx[fitChars - 1] + TextEllipsisWidth > textWidth)
                                    fitChars--;
                                int totalCount;
                                if (fitChars > 0)
                                {
                                    wmemmove(DrawItemBuffW + fitChars, L"...", 3);
                                    totalCount = fitChars + 3;
                                }
                                else
                                {
                                    DrawItemBuffW[1] = L'.';
                                    totalCount = 2;
                                }
                                sally::ui::DrawPanelTextW(hDC, r.left + SPACE_WIDTH / 2, y, ETO_OPAQUE, &adjR, DrawItemBuffW, totalCount, NULL);
                            }
                            else
                            {
                                if (column->LeftAlignment == 0)
                                {
                                    deltaX = r.right - r.left - SPACE_WIDTH / 2 - textSize.cx;
                                    if (deltaX < SPACE_WIDTH / 2)
                                        deltaX = SPACE_WIDTH / 2;
                                }
                                else
                                    deltaX = SPACE_WIDTH / 2;
                                sally::ui::DrawPanelTextW(hDC, r.left + deltaX, y, ETO_OPAQUE, &adjR, DrawItemBuffW, TransferLen, NULL);
                            }
                        }
                        else
                        {
                            if (column->LeftAlignment == 0)
                            {
                                GetTextExtentPoint32W(hDC, DrawItemBuffW, TransferLen, &textSize);
                                deltaX = r.right - r.left - SPACE_WIDTH / 2 - textSize.cx;
                                if (deltaX < SPACE_WIDTH / 2)
                                    deltaX = SPACE_WIDTH / 2;
                            }
                            else
                                deltaX = SPACE_WIDTH / 2;
                            sally::ui::DrawPanelTextW(hDC, r.left + deltaX, y, ETO_OPAQUE, &adjR, DrawItemBuffW, TransferLen, NULL);
                        }
                    }
                    else
                    {
                        if (column->FixedWidth == 1) // NarrowedNameColumn does not apply here (not Name column)
                        {
                            int fitChars;
                            // for fixed-width columns we must check whether the entire text fits
                            int textWidth = r.right - r.left - SPACE_WIDTH;
                            GetTextExtentExPointW(hDC, TransferBuffer, TransferLen, textWidth,
                                                  &fitChars, DrawItemAlpDx, &textSize);
                            if (fitChars < TransferLen)
                            {
                                // search from the end for the character after which we can copy "..." and it fits in the column
                                while (fitChars > 0 && DrawItemAlpDx[fitChars - 1] + TextEllipsisWidth > textWidth)
                                    fitChars--;
                                // copy part of the original string to another buffer
                                int totalCount;
                                if (fitChars > 0)
                                {
                                    // wmemmove, not memmove: DrawItemBuff and
                                    // TransferBuffer are both wchar_t, so the byte count copied
                                    // half the characters, and "..." wrote three narrow bytes
                                    // where three WCHARs were meant.
                                    wmemmove(DrawItemBuff, TransferBuffer, fitChars);
                                    // and append "..."
                                    wmemmove(DrawItemBuff + fitChars, L"...", 3);
                                    totalCount = fitChars + 3;
                                }
                                else
                                {
                                    DrawItemBuff[0] = TransferBuffer[0];
                                    DrawItemBuff[1] = '.';
                                    totalCount = 2;
                                }
                                sally::ui::DrawPanelTextW(hDC, r.left + SPACE_WIDTH / 2, y, ETO_OPAQUE, &adjR, DrawItemBuff, totalCount, NULL);
                            }
                            else
                            {
                                if (column->LeftAlignment == 0)
                                {
                                    deltaX = r.right - r.left - SPACE_WIDTH / 2 - textSize.cx;
                                    if (deltaX < SPACE_WIDTH / 2)
                                        deltaX = SPACE_WIDTH / 2;
                                }
                                else
                                    deltaX = SPACE_WIDTH / 2;
                                sally::ui::DrawPanelTextW(hDC, r.left + deltaX, y, ETO_OPAQUE, &adjR, TransferBuffer, TransferLen, NULL);
                            }
                        }
                        else
                        {
                            // it's a flexible column and the contents definitely fit
                            if (column->LeftAlignment == 0)
                            {
                                // if the column is right-aligned, measure the text width
                                GetTextExtentPoint32W(hDC, TransferBuffer, TransferLen, &textSize);
                                deltaX = r.right - r.left - SPACE_WIDTH / 2 - textSize.cx;
                                if (deltaX < SPACE_WIDTH / 2)
                                    deltaX = SPACE_WIDTH / 2;
                            }
                            else
                                deltaX = SPACE_WIDTH / 2;

                            sally::ui::DrawPanelTextW(hDC, r.left + deltaX, y, ETO_OPAQUE, &adjR, TransferBuffer, TransferLen, NULL);
                        }
                    }
                }
                x += column->Width;
                if (drawFlags & DRAWFLAG_SELFOC_CHANGE)
                    cacheValidWidth += column->Width;
            }
        }
        if (forFrameAdjusted)
        {
            adjR.top--; // restore the correct dimensions
            adjR.bottom++;
            forFrameAdjusted = FALSE;
        }
        if (GetViewMode() == vmDetailed && !(drawFlags & DRAWFLAG_DIRTY_ONLY) &&
            !(drawFlags & DRAWFLAG_SELFOC_CHANGE))
        {
            if (r.right < rect.right)
            {
                // clear the background
                adjR.left = r.left = r.right;
                adjR.right = r.right = rect.right;
                SetBkColor(hDC, GetCOLORREF(CurrentColors[ITEM_BK_NORMAL]));
                if (drawFlags & DRAWFLAG_MASK) // mask is b&w; we must not paint a colored background into it
                    SetBkColor(hDC, RGB(255, 255, 255));
                sally::ui::DrawPanelTextW(hDC, r.left, r.top, ETO_OPAQUE, &adjR, L"", 0, NULL);
            }
        }

        //*****************************************
        //
        // drawing the focus frame
        //
        // if FullRowSelect is off and the name wasn't drawn, we won't even bother with
        // the frame

        if ((itemIndex == DropTargetIndex || isItemFocusedOrEditMode) &&
            (Configuration.FullRowSelect || focusFrameRightValid))
        {
            r = rect;
            r.left += 1 + IconSizes[ICONSIZE_16] + 1;
            r.right = x;
            if (focusFrameRightValid)
                r.right = focusFrameRight;

            DrawFocusRect(hDC, &r, (f->Selected == 1), Parent->EditMode);
            /*
      // for debugging the dual cursor issue we need to distinguish normal focus and drop target
      if (itemIndex == DropTargetIndex)
      {
        MoveToEx(hDC, r.left, r.top + 3, NULL);
        LineTo(hDC, r.left+3, r.top);
      }
*/
        }
    }

    if (drawFlags & DRAWFLAG_SELFOC_CHANGE)
    {
        // we used the cache - blit it to the screen
        int width = cacheValidWidth - xOffset;
        if (width > 0)
            BitBlt(hTgtDC, itemRect->left, itemRect->top,
                   width, itemRect->bottom - itemRect->top,
                   hDC, 0, 0, SRCCOPY);
    }

    if (showCaret)
        ShowCaret(ListBox->HWindow);
}

//****************************************************************************
//
// SplitTextW
//
// Uses the 'DrawItemAlpDx' array
//
// text      [IN]  input string which we'll split
// textLen   [IN]  number of characters in string 'text' (without terminator)
// maxWidth  [IN]  maximum width in pixels which a longer line may have
//           [OUT] actual maximum width
// out1      [OUT] first output line without terminator will copy here
// out1Len   [IN]  maximum number of characters that can be written to 'out1'
//           [OUT] number of characters copied to 'out1'
// out1Width [OUT] width of 'out1' in pixels
// out2            same as out1 but for the second line
// out2Len
// out2Width
//

void SplitTextW(HDC hDC, const wchar_t* text, int textLen, int* maxWidth,
                wchar_t* out1, int* out1Len, int* out1Width,
                wchar_t* out2, int* out2Len, int* out2Width)
{
    SIZE sz;
    // measure the width of every character
    GetTextExtentExPointW(hDC, text, textLen, 0, NULL, DrawItemAlpDx, &sz);

    if (sz.cx > *maxWidth)
    {
        // if the text length exceeds the maximum width,
        // try to split it into two lines at a space
        // anything that still exceeds is replaced with "..."

        // find the last character that still fits on the first line
        // while also tracking the index of the last space
        int lastSpaceIndex = -1;
        int maxW = *maxWidth;
        int w = 0;
        int index = 0;
        while (index < maxW) // this condition should not be applied
        {
            if (text[index] == L' ')
                lastSpaceIndex = index;
            if (DrawItemAlpDx[index] <= maxW)
                index++;
            else
                break;
        }

        if (lastSpaceIndex != -1)
        {
            // if we found a space, break the first line there
            // (the space is omitted to save room)
            if (lastSpaceIndex > 0)
            {
                *out1Len = min(*out1Len, lastSpaceIndex);
                *out1Width = DrawItemAlpDx[lastSpaceIndex - 1];
                wmemmove(out1, text, *out1Len);
            }
            else
            {
                *out1Len = 0;
                *out1Width = 0;
            }

            // move the pointer past the space
            index = lastSpaceIndex + 1;
        }
        else
        {
            // no space encountered yet, so we must break with an ellipsis

            // append "..." at the end of the string
            int backTrackIndex = index - 1;
            while (DrawItemAlpDx[backTrackIndex] + TextEllipsisWidth > maxW && backTrackIndex > 0)
                backTrackIndex--;

            *out1Len = min(*out1Len, backTrackIndex + 3);
            *out1Width = DrawItemAlpDx[backTrackIndex - 1] + TextEllipsisWidth;
            wmemmove(out1, text, *out1Len - 3);
            if (*out1Len >= 3)
                wmemmove(out1 + *out1Len - 3, L"...", 3);

            // look for a space where we can continue to the next line
            while (index < textLen)
            {
                if (text[index++] == L' ')
                    break;
            }
        }

        if (index < textLen)
        {
            // process the second line
            int oldIndex = index;
            int offsetX;

            if (index > 0)
                offsetX = DrawItemAlpDx[index - 1]; // width of the first line including the separating space
            else
                offsetX = 0;

            while (index < textLen)
            {
                if (DrawItemAlpDx[index] - offsetX < maxW)
                    index++;
                else
                    break;
            }

            if (index < textLen)
            {
                // the second line didn't fit completely; append an ellipsis
                int backTrackIndex = index - 1;
                while (DrawItemAlpDx[backTrackIndex] - offsetX + TextEllipsisWidth > maxW &&
                       backTrackIndex > oldIndex)
                    backTrackIndex--;

                *out2Len = min(*out2Len, backTrackIndex - oldIndex + 3);
                *out2Width = DrawItemAlpDx[backTrackIndex - 1] - offsetX + TextEllipsisWidth;
                wmemmove(out2, text + oldIndex, *out2Len - 3);
                if (*out2Len >= 3)
                    wmemmove(out2 + *out2Len - 3, L"...", 3);
            }
            else
            {
                // the second line fit completely
                wmemmove(out2, text + oldIndex, index - oldIndex);
                *out2Len = index - oldIndex;
                *out2Width = DrawItemAlpDx[index - 1] - offsetX;
            }
        }
        else
        {
            // nothing to put on the second line
            *out2Len = 0;
            *out2Width = 0;
        }
    }
    else
    {
        // the first line will contain everything
        *out1Len = min(*out1Len, textLen);
        *out1Width = sz.cx;
        wmemmove(out1, text, *out1Len);

        // the second line will be empty
        *out2Len = 0;
        *out2Width = 0;
    }
    // maximum width
    *maxWidth = max(*out1Width, *out2Width);
}

//****************************************************************************
//
// DrawIconThumbnailItem
//
// Draws an item in Icons and Thumbnails modes
//

void CFilesWindow::DrawIconThumbnailItem(HDC hTgtDC, int itemIndex, RECT* itemRect,
                                         DWORD drawFlags, CIconSizeEnum iconSize)
{
    CALL_STACK_MESSAGE_NONE
    if (itemIndex < 0 || itemIndex >= Dirs->Count + Files->Count)
    {
        TRACE_E("itemIndex = " << itemIndex);
        return;
    }
#ifdef _DEBUG
    if (GetViewMode() != vmIcons && GetViewMode() != vmThumbnails)
    {
        TRACE_E("GetViewMode() != vmIcons && GetViewMode() != vmThumbnails");
        return;
    }
#endif // _DEBUG

    if (!CanDrawItems)
        return;

    BOOL isDir = itemIndex < Dirs->Count;
    CFileData* f = isDir ? &Dirs->At(itemIndex) : &Files->At(itemIndex - Dirs->Count);

    if ((drawFlags & DRAWFLAG_DIRTY_ONLY) && f->Dirty == 0)
        return;

    BOOL isItemUpDir = FALSE; // is the drawn item the ".." directory?

    if (itemIndex == 0 && isDir && *f->Name == '.' && *(f->Name + 1) == '.' &&
        *(f->Name + 2) == 0) // "up-dir" can only be first
    {
        if (drawFlags & DRAWFLAG_DIRTY_ONLY)
            return;
        isItemUpDir = TRUE;
    }

    //  TRACE_I("DrawingLarge itemIndex="<<dec<<itemIndex<<" y="<<itemRect->top);

    BOOL isItemFocusedOrEditMode = FALSE;
    if (FocusedIndex == itemIndex) // drawing the cursor
    {
        if (FocusVisible || Parent->EditMode && Parent->GetActivePanel() == this) // switched in the command-line
            isItemFocusedOrEditMode = TRUE;
    }

    RECT rect = *itemRect; // item position shifted by xOffset
                           //  rect.left -= xOffset;
                           //  rect.right -= xOffset;

    HDC hDC;
    /*
  int cacheValidWidth; // meaningful only when using the cache and specifies the number of pixels (the width),
                       // which are rendered in the bitmap and must be transferred to the screen
  if (drawFlags & DRAWFLAG_SELFOC_CHANGE)
  {
//    TRACE_I("drawing index="<<itemIndex);
    // if it is only the cursor position change, skip visibility tests
    drawFlags |= DRAWFLAG_SKIP_VISTEST;

    // we are drawing only a small area and have plenty of time, so using the cache is fine
    hDC = ItemMemDC;

    // the bitmap is empty for now
    cacheValidWidth = 0;

    // we must adjust the rect variable
    rect.top -= itemRect->top;
    rect.bottom -= itemRect->top;
    rect.left -= itemRect->left;
    rect.right -= itemRect->left;
  }
  else
*/
    hDC = hTgtDC; // this is a large paint; skip the cache and draw directly to the screen

    // if not prohibited, clear the flag
    if (!(drawFlags & DRAWFLAG_KEEP_DIRTY))
        f->Dirty = 0;

    TransferAssocIndex = -2; // we haven't looked up the extension for the drawn item in Associations yet

    //*****************************************
    //
    // drawing the icon
    //

    if (drawFlags & DRAWFLAG_SKIP_VISTEST || RectVisible(hDC, &rect))
    {
        // icon/thumbnail size
        int iconW = IconSizes[iconSize];
        int iconH = IconSizes[iconSize];

        HBITMAP hScaled = NULL; // if not NULL, draw the thumbnail; otherwise draw the icon
        if (GetViewMode() == vmThumbnails)
        {
            // thumbnails are supported only on disk
            if (Is(ptDisk) && !isDir)
            {
                int icon;
                const std::wstring fileName(f->Name, f->NameLen);

                if (IconCache->GetIndex(fileName.c_str(), icon, NULL, NULL))
                {
                    DWORD flag = IconCache->At(icon).GetFlag();
                    if (flag == 5 || flag == 6) // o.k. || old version
                    {

                        BOOL leaveSection;
                        if (!IconCacheValid)
                        {
                            HANDLES(EnterCriticalSection(&ICSectionUsingThumb));
                            leaveSection = TRUE;
                        }
                        else
                            leaveSection = FALSE;

                        CThumbnailData* thumbnailData;
                        if (IconCache->GetThumbnail(IconCache->At(icon).GetIndex(), &thumbnailData))
                        {
                            // reconstruction of the bitmap
                            hScaled = HANDLES(CreateBitmap(thumbnailData->Width,
                                                           thumbnailData->Height,
                                                           thumbnailData->Planes,
                                                           thumbnailData->BitsPerPixel,
                                                           thumbnailData->Bits));

                            if (hScaled != NULL)
                            {
                                iconW = thumbnailData->Width;
                                iconH = thumbnailData->Height;
                            }
                            else
                                TRACE_E("Error creating bitmap!");
                        }

                        if (leaveSection)
                        {
                            HANDLES(LeaveCriticalSection(&ICSectionUsingThumb));
                        }
                    }
                }
            }
        }

        int iconX = rect.left + (rect.right - rect.left - iconW) / 2;
        int iconY;
        if (GetViewMode() == vmThumbnails)
            iconY = rect.top + (rect.bottom - Configuration.IconSpacingVert - rect.top - iconH) / 2 + 3;
        else
            iconY = rect.top + 2;

        // outer rectangle from which we clear towards the inner one
        RECT outerRect = rect;

        // rectangle we will clear to
        RECT innerRect;
        BOOL thickFrame = FALSE; // for Thumbnails only -- should the frame be doubled?
        if (GetViewMode() == vmThumbnails)
        {
            // for Thumbnails it reaches the frame around the thumbnail
            innerRect = rect;
            innerRect.left += (rect.right - rect.left - (ListBox->ThumbnailWidth + 2)) / 2;
            innerRect.right = innerRect.left + ListBox->ThumbnailWidth + 2;
            innerRect.top += 3;
            innerRect.bottom = innerRect.top + ListBox->ThumbnailHeight + 2;

            // pen for the frame
            HPEN hPen;
            if (!f->Selected == 1 && !isItemFocusedOrEditMode)
                hPen = HThumbnailNormalPen;
            else
            {
                thickFrame = TRUE;
                InflateRect(&innerRect, 1, 1);
                if (f->Selected == 0 && isItemFocusedOrEditMode)
                    hPen = HThumbnailFucsedPen;
                else if (f->Selected == 1 && !isItemFocusedOrEditMode)
                    hPen = HThumbnailSelectedPen;
                else
                    hPen = HThumbnailFocSelPen;
            }
            SelectObject(hDC, hPen);
            SelectObject(hDC, HANDLES(GetStockObject(NULL_BRUSH)));
            outerRect.bottom = innerRect.bottom;
        }
        else
        {
            // for Icons it extends to the icon
            innerRect.left = iconX;
            innerRect.top = iconY;
            innerRect.right = iconX + iconW;
            innerRect.bottom = iconY + iconH;
            outerRect.bottom = innerRect.bottom;
        }

        // clear the background around the icon or frame (for Thumbnails)
        if ((drawFlags & DRAWFLAG_MASK) == 0) // when drawing the mask (b&w), the background color must not be painted
            FillIntersectionRegion(hDC, &outerRect, &innerRect);

        RECT overlayRect;
        overlayRect = innerRect;
        InflateRect(&overlayRect, -1, -1);
        if (GetViewMode() == vmThumbnails && thickFrame)
            InflateRect(&overlayRect, -1, -1);

        if (GetViewMode() == vmThumbnails)
        {
            if ((drawFlags & DRAWFLAG_SKIP_FRAME) == 0)
            {
                // draw the frame around the thumbnail
                Rectangle(hDC, innerRect.left, innerRect.top, innerRect.right, innerRect.bottom);
                if (thickFrame)
                {
                    InflateRect(&innerRect, -1, -1);
                    Rectangle(hDC, innerRect.left, innerRect.top, innerRect.right, innerRect.bottom);
                }
                InflateRect(&innerRect, -1, -1);

                // clear the area between the frame and the icon
                RECT iiRect;
                iiRect.left = iconX;
                iiRect.top = iconY;
                iiRect.right = iiRect.left + iconW;
                iiRect.bottom = iiRect.top + iconH;
                if ((drawFlags & DRAWFLAG_MASK) == 0) // when drawing the mask (b&w), the background color must not be painted
                    FillIntersectionRegion(hDC, &innerRect, &iiRect);
            }
        }

        if (GetViewMode() == vmThumbnails)
            drawFlags |= DRAWFLAG_NO_STATE;

        if (hScaled == NULL)
        {
            // no thumbnail available -> draw the icon
            DrawIcon(hDC, f, isDir, isItemUpDir, isItemFocusedOrEditMode,
                     iconX, iconY, iconSize, (GetViewMode() == vmThumbnails ? &overlayRect : NULL),
                     drawFlags);
        }
        else
        {
            // draw the thumbnail
            HDC hTmpDC = HANDLES(CreateCompatibleDC(hDC));
            HBITMAP hOldBitmap = (HBITMAP)SelectObject(hTmpDC, hScaled);

            if ((drawFlags & DRAWFLAG_MASK) != 0)
            {
                // draw the mask for the overlay (the overlay must be drawn first because we cannot draw it transparently)
                DrawIcon(hDC, f, isDir, isItemUpDir, isItemFocusedOrEditMode,
                         iconX, iconY, iconSize, (GetViewMode() == vmThumbnails ? &overlayRect : NULL),
                         drawFlags | DRAWFLAG_OVERLAY_ONLY);
            }

            // draw the bitmap itself
            BitBlt(hDC, iconX, iconY, iconW, iconH, hTmpDC, 0, 0,
                   (drawFlags & DRAWFLAG_MASK) == 0 ? SRCCOPY : SRCERASE);

            if ((drawFlags & DRAWFLAG_MASK) == 0)
            {
                // draw the overlay (during normal drawing it must be drawn after the bitmap)
                DrawIcon(hDC, f, isDir, isItemUpDir, isItemFocusedOrEditMode,
                         iconX, iconY, iconSize, (GetViewMode() == vmThumbnails ? &overlayRect : NULL),
                         drawFlags | DRAWFLAG_OVERLAY_ONLY);
            }

            SelectObject(hTmpDC, hOldBitmap);
            HANDLES(DeleteDC(hTmpDC));
            HANDLES(DeleteObject(hScaled));
        }
    }

    //*****************************************
    //
    // text drawing
    //

    if (!(drawFlags & DRAWFLAG_ICON_ONLY))
    {
        // select the appropriate font
        SelectObject(hDC, Font);

        // will we later draw a frame around the item?
        BOOL drawFocusFrame = (itemIndex == DropTargetIndex || isItemFocusedOrEditMode) &&
                              (drawFlags & DRAWFLAG_NO_FRAME) == 0;

        // color detection
        // Wide name/extension.
        std::wstring highlightNameW(f->Name);
        const wchar_t* highlightExtW = isDir ? NULL : sally::unicode::GetWideExtensionStart(highlightNameW.c_str());
        CHighlightMasksItem* highlightMasksItem = MainWindow->HighlightMasks->AgreeMasks(highlightNameW.c_str(), highlightExtW, f->Attr);

        // set the applied font, background color and text color
        SetFontAndColors(hDC, highlightMasksItem, f, isItemFocusedOrEditMode, itemIndex);

        if (drawFlags & DRAWFLAG_MASK)
        {
            SetTextColor(hDC, RGB(0, 0, 0)); // we want black text on black background because of aliased fonts
            SetBkColor(hDC, RGB(0, 0, 0));
        }
        if (drawFlags & DRAWFLAG_DRAGDROP)
        {
            SetTextColor(hDC, GetCOLORREF(CurrentColors[ITEM_FG_FOCUSED])); // focused text and background
            SetBkColor(hDC, ResolveDarkBaseColor(ITEM_BK_FOCUSED, GetCOLORREF(CurrentColors[ITEM_BK_FOCUSED]), DarkMode_ShouldUseDark()));
        }

        int itemWidth = rect.right - rect.left; // item width

        // maximum width available for the text
        int maxWidth = itemWidth - 4 - 1; // -1 so they don't touch
        int out1Len = 512;
        int out1Width;
        int out2Len = 512;
        int out2Width;

        // One arm. DrawItemBuff and DrawItemBuffW are BOTH wchar_t[1024]
        // (see their definitions), so out1/out2 were never the "ANSI buffers" the old comment
        // claimed - the pair differed only in which wide buffer it wrote to. The earlier
        // format fix lived in the wide arm only, so that is the arm that survives.
        wchar_t* out1W = DrawItemBuffW;
        wchar_t* out2W = DrawItemBuffW + 512;

        std::wstring alteredNameW = AlterFileNameW(f->Name, Configuration.FileNameFormat, 0, isDir);
        SplitTextW(hDC, alteredNameW.c_str(), (int)alteredNameW.length(), &maxWidth,
                   out1W, &out1Len, &out1Width,
                   out2W, &out2Len, &out2Width);

        if (isItemUpDir)
        {
            out1Len = 0;
            maxWidth = (GetViewMode() == vmThumbnails) ? ListBox->ThumbnailWidth : 32;
            out1Width = maxWidth;
            out2Len = 0;
        }

        // outer rectangle from which we clear towards the inner one
        int y; // text position
        RECT outerRect = rect;
        if (GetViewMode() == vmThumbnails)
        {
            outerRect.top += 3 + ListBox->ThumbnailHeight + 2;
            y = outerRect.top + 4;
            if (f->Selected == 1 || isItemFocusedOrEditMode)
                outerRect.top++; // thickFrame
        }
        else
        {
            outerRect.top += 2 + IconSizes[ICONSIZE_32];
            y = outerRect.top + 4;
        }

        // inner rectangle we clear towards
        RECT r;
        r.left = rect.left + (itemWidth - maxWidth) / 2 - 2;
        r.top = y - 2;
        r.right = r.left + maxWidth + 4;
        r.bottom = y + FontCharHeight + 2;
        if (out2Len > 0)
            r.bottom += FontCharHeight;

        // clear the background around the text
        if ((drawFlags & DRAWFLAG_MASK) == 0) // when drawing the mask (b&w), the background color must not be painted
            FillIntersectionRegion(hDC, &outerRect, &r);

        if (drawFocusFrame) // if there won't be a frame displayed, reduce the area by it
            InflateRect(&r, -1, -1);

        int oldRTop = r.top;

        // display the centered first line; also clear background of the second line
        // DRAWFLAG_MASK: hack, under XP some stuff is added in font of the text in the mask while drawing short texts; not an issue if text is not drawn
        sally::ui::DrawPanelTextW(hDC, rect.left + (itemWidth - out1Width) / 2, y,
                                  ETO_OPAQUE, &r, out1W, (drawFlags & DRAWFLAG_MASK) ? 0 : out1Len, NULL);

        // display the centered second line
        if (out2Len > 0)
        {
            // DRAWFLAG_MASK: hack, under XP some stuff is added in font of the text in the mask while drawing short texts; not an issue if text is not drawn
            sally::ui::DrawPanelTextW(hDC, rect.left + (itemWidth - out2Width) / 2, y += FontCharHeight,
                                      0, &r, out2W, (drawFlags & DRAWFLAG_MASK) ? 0 : out2Len, NULL);
        }

        //*****************************************
        //
        // drawing the focus frame
        //
        if (drawFocusFrame)
        {
            InflateRect(&r, 1, 1);
            DrawFocusRect(hDC, &r, (f->Selected == 1), Parent->EditMode);
        }
    }
    /*
  SelectObject(hDC, HInactiveNormalPen);
  SelectObject(hDC, HANDLES(GetStockObject(NULL_BRUSH)));
  Rectangle(hDC, rect.left, rect.top, rect.right, rect.bottom);
*/
}

//****************************************************************************
//
// DrawTileItem
//
// Draws an item in Tiles mode
//

void TruncateSringToFitWidthW(HDC hDC, wchar_t* buffer, int* bufferLen, int maxTextWidth, int* widthNeeded)
{
    if (*bufferLen == 0)
        return;

    SIZE fnSZ;
    int fitChars;
    GetTextExtentExPointW(hDC, buffer, *bufferLen, maxTextWidth,
                          &fitChars, DrawItemAlpDx, &fnSZ);
    if (fitChars < *bufferLen)
    {
        if (*widthNeeded < maxTextWidth)
            *widthNeeded = maxTextWidth;
        // search from the end for the character after which we can copy "..." and it fits in the column
        while (fitChars > 0 && DrawItemAlpDx[fitChars - 1] + TextEllipsisWidth > maxTextWidth)
            fitChars--;
        // copy part of the original string to another buffer
        if (fitChars > 0)
        {
            // and append "..."
            wmemmove(buffer + fitChars, L"...", 3);
            *bufferLen = fitChars + 3;
        }
        else
        {
            buffer[1] = L'.';
            *bufferLen = 2;
        }
    }
    else
    {
        if (*widthNeeded < fnSZ.cx)
            *widthNeeded = fnSZ.cx;
    }
}

void GetTileTexts(CFileData* f, int isDir,
                  HDC hDC, int maxTextWidth, int* widthNeeded,
                  wchar_t* out0, int* out0Len,
                  wchar_t* out1, int* out1Len,
                  wchar_t* out2, int* out2Len, DWORD validFileData,
                  CPluginDataInterfaceEncapsulation* pluginData,
                  BOOL isDisk)
{
    // format the name to the user-defined form
    // AlterFileNameW returns a std::wstring and drops both the out-buffer
    // and filenameLen, so this is a reshape rather than a suffix swap. The copy is unbounded
    // exactly as the AlterFileName it replaces was - GetTileTexts is never told out0's
    // capacity - so the pre-existing risk is unchanged, not newly introduced.
    std::wstring alteredName = AlterFileNameW(f->Name, Configuration.FileNameFormat, 0, isDir != 0);
    // 1st line: NAME
    *out0Len = (int)alteredName.length();
    wmemcpy(out0, alteredName.c_str(), *out0Len + 1);
    // the string may be longer than available space and may need to be shortened with "..."
    *widthNeeded = 0;
    TruncateSringToFitWidthW(hDC, out0, out0Len, maxTextWidth, widthNeeded);

    // 2nd line: SIZE (if known)
    CQuadWord plSize;
    BOOL plSizeValid = FALSE;
    if ((validFileData & VALID_DATA_PL_SIZE) &&
        pluginData->NotEmpty() &&
        pluginData->GetByteSize(f, isDir != 0, &plSize))
    {
        plSizeValid = TRUE;
    }
    if (!plSizeValid && ((validFileData & VALID_DATA_SIZE) == 0 || isDir && !f->SizeValid))
        *out1 = 0;
    else
    {
        const std::wstring sizeText = PrintDiskSize(plSizeValid ? plSize : f->Size, 0);
        const size_t copyLength = (std::min<size_t>)(sizeText.size(), 511);
        wmemcpy(out1, sizeText.data(), copyLength);
        out1[copyLength] = L'\0';
    }
    *out1Len = (int)wcslen(out1);
    // the string may be longer than available space and may need to be shortened with "..."
    TruncateSringToFitWidthW(hDC, out1, out1Len, maxTextWidth, widthNeeded);

    // 3rd line DATE TIME (if known)
    SYSTEMTIME st;
    FILETIME ft;
    BOOL validDate = FALSE;
    BOOL validTime = FALSE;
    BOOL invalidDate = FALSE;
    BOOL invalidTime = FALSE;
    if (validFileData & (VALID_DATA_DATE | VALID_DATA_TIME)) // at least something is in LastWrite
    {
        if (!FileTimeToLocalFileTime(&f->LastWrite, &ft) ||
            !FileTimeToSystemTime(&ft, &st))
        {
            if (validFileData & VALID_DATA_DATE)
                invalidDate = TRUE;
            if (validFileData & VALID_DATA_TIME)
                invalidTime = TRUE;
        }
        else
        {
            if (isDir != 2 /* UP-DIR */ || !isDisk || st.wYear != 1602 || st.wMonth != 1 || st.wDay != 1 ||
                st.wHour != 0 || st.wMinute != 0 || st.wSecond != 0 || st.wMilliseconds != 0)
            {
                if (validFileData & VALID_DATA_DATE)
                    validDate = TRUE;
                if (validFileData & VALID_DATA_TIME)
                    validTime = TRUE;
            }
        }
    }
    if ((validFileData & VALID_DATA_PL_DATE) &&
        pluginData->NotEmpty() &&
        pluginData->GetLastWriteDate(f, isDir != 0, &st))
    {
        validDate = TRUE;
    }
    if ((validFileData & VALID_DATA_PL_TIME) &&
        pluginData->NotEmpty() &&
        pluginData->GetLastWriteTime(f, isDir != 0, &st))
    {
        validTime = TRUE;
    }
    if (!validDate) // no date set, so we set one...
    {
        st.wYear = 2000;
        st.wMonth = 12;
        st.wDay = 24;
        st.wDayOfWeek = 0; // Sunday
    }
    if (!validTime) // no time set, so we set one...
    {
        st.wHour = 0;
        st.wMinute = 0;
        st.wSecond = 0;
        st.wMilliseconds = 0;
    }

    out2[0] = 0;
    int out2LenA = 0;
    int out2LenB = 0;
    if (validDate || invalidDate)
    {
        if (validDate)
        {
            out2LenA = GetDateFormatW(LOCALE_USER_DEFAULT, DATE_SHORTDATE, &st, NULL, out2, 500) - 1;
            if (out2LenA < 0)
                out2LenA = _snwprintf_s(out2, 500, _TRUNCATE, L"%u.%u.%u", st.wDay, st.wMonth, st.wYear);
        }
        else
            out2LenA = _snwprintf_s(out2, 500, _TRUNCATE, L"%s", LoadStrW(IDS_INVALID_DATEORTIME));
        if (validTime || invalidTime)
        {
            out2[out2LenA] = L' ';
            out2LenA++;
        }
    }
    if (validTime || invalidTime)
    {
        if (validTime)
        {
            out2LenB = GetTimeFormatW(LOCALE_USER_DEFAULT, 0, &st, NULL, out2 + out2LenA, 500 - out2LenA) - 1;
            if (out2LenB < 0)
                out2LenB = _snwprintf_s(out2 + out2LenA, 500 - out2LenA, _TRUNCATE, L"%u:%02u:%02u", st.wHour, st.wMinute, st.wSecond);
        }
        else
            out2LenB = _snwprintf_s(out2 + out2LenA, 500 - out2LenA, _TRUNCATE, L"%s", LoadStrW(IDS_INVALID_DATEORTIME));
    }
    *out2Len = out2LenA + out2LenB;
    // the string may be longer than available space and may need to be shortened with "..."
    TruncateSringToFitWidthW(hDC, out2, out2Len, maxTextWidth, widthNeeded);
}

void CFilesWindow::DrawTileItem(HDC hTgtDC, int itemIndex, RECT* itemRect, DWORD drawFlags,
                                CIconSizeEnum iconSize)
{
    CALL_STACK_MESSAGE_NONE
    if (itemIndex < 0 || itemIndex >= Dirs->Count + Files->Count)
    {
        TRACE_E("itemIndex = " << itemIndex);
        return;
    }
#ifdef _DEBUG
    if (GetViewMode() != vmTiles)
    {
        TRACE_E("GetViewMode() != vmTiles");
        return;
    }
#endif // _DEBUG
    if (!CanDrawItems)
        return;

    BOOL isDir = itemIndex < Dirs->Count;

    CFileData* f = isDir ? &Dirs->At(itemIndex) : &Files->At(itemIndex - Dirs->Count);

    if ((drawFlags & DRAWFLAG_DIRTY_ONLY) && f->Dirty == 0)
        return;

    BOOL isItemUpDir = FALSE; // is the drawn item the ".." directory?

    if (itemIndex == 0 && isDir && *f->Name == '.' && *(f->Name + 1) == '.' &&
        *(f->Name + 2) == 0) // "up-dir" can only be first
    {
        if (drawFlags & DRAWFLAG_DIRTY_ONLY)
            return;
        isItemUpDir = TRUE;
    }

    //  TRACE_I("DrawTileItem itemIndex="<<dec<<itemIndex<<" y="<<itemRect->top);

    BOOL isItemFocusedOrEditMode = FALSE;
    if (FocusedIndex == itemIndex) // drawing the cursor
    {
        if (FocusVisible || Parent->EditMode && Parent->GetActivePanel() == this) // switched in the command-line
            isItemFocusedOrEditMode = TRUE;
    }

    RECT rect = *itemRect; // item position shifted by xOffset

    HDC hDC = hTgtDC; // this is a large paint; skip the cache and draw directly to the screen

    // if not prohibited, clear the flag
    if (!(drawFlags & DRAWFLAG_KEEP_DIRTY))
        f->Dirty = 0;

    TransferAssocIndex = -2; // we haven't looked up the extension for the drawn item in Associations yet

    //*****************************************
    //
    // drawing the icon
    //

    if (drawFlags & DRAWFLAG_SKIP_VISTEST || RectVisible(hDC, &rect))
    {
        // icon size
        int iconW = IconSizes[iconSize];
        int iconH = IconSizes[iconSize];

        // icon position
        int iconX = rect.left + TILE_LEFT_MARGIN;
        int iconY = rect.top + (rect.bottom - rect.top - iconH) / 2; // center it

        // outer rectangle from which we clear towards the inner one
        RECT outerRect = rect;
        outerRect.right = iconX + iconW;

        // rectangle we will clear towards
        RECT innerRect;
        BOOL thickFrame = FALSE; // for Thumbnails only -- should the frame be doubled?

        innerRect.left = iconX;
        innerRect.top = iconY;
        innerRect.right = iconX + iconW;
        innerRect.bottom = iconY + iconH;

        // clear the background around the icon or frame (for Thumbnails)
        if ((drawFlags & DRAWFLAG_MASK) == 0) // when drawing the mask (b&w), the background color must not be painted
            FillIntersectionRegion(hDC, &outerRect, &innerRect);
        // no thumbnail available -> draw the icon
        DrawIcon(hDC, f, isDir, isItemUpDir, isItemFocusedOrEditMode,
                 iconX, iconY, iconSize, NULL, drawFlags);
    }

    //*****************************************
    //
    // text drawing
    //

    if (!(drawFlags & DRAWFLAG_ICON_ONLY))
    {
        // select the appropriate font
        SelectObject(hDC, Font);

        // will we later draw a frame around the item?
        BOOL drawFocusFrame = (itemIndex == DropTargetIndex || isItemFocusedOrEditMode) &&
                              (drawFlags & DRAWFLAG_NO_FRAME) == 0;

        // colors detection
        // Wide name/extension.
        std::wstring highlightNameW(f->Name);
        const wchar_t* highlightExtW = isDir ? NULL : sally::unicode::GetWideExtensionStart(highlightNameW.c_str());
        CHighlightMasksItem* highlightMasksItem = MainWindow->HighlightMasks->AgreeMasks(highlightNameW.c_str(), highlightExtW, f->Attr);

        // set the applied font, background color and text color
        SetFontAndColors(hDC, highlightMasksItem, f, isItemFocusedOrEditMode, itemIndex);

        if (drawFlags & DRAWFLAG_MASK)
        {
            SetTextColor(hDC, RGB(0, 0, 0)); // we want black text on black background because of aliased fonts
            SetBkColor(hDC, RGB(0, 0, 0));
        }
        if (drawFlags & DRAWFLAG_DRAGDROP)
        {
            SetTextColor(hDC, GetCOLORREF(CurrentColors[ITEM_FG_FOCUSED])); // focused text and background
            SetBkColor(hDC, ResolveDarkBaseColor(ITEM_BK_FOCUSED, GetCOLORREF(CurrentColors[ITEM_BK_FOCUSED]), DarkMode_ShouldUseDark()));
        }

        int nameLen = f->NameLen;
        int itemWidth = rect.right - rect.left; // item width

        // texts must not exceed this length in pixels
        int maxTextWidth = itemWidth - TILE_LEFT_MARGIN - IconSizes[iconSize] - TILE_LEFT_MARGIN - 4;
        int widthNeeded = 0;

        wchar_t* out0 = TransferBuffer;
        int out0Len;
        wchar_t* out1 = DrawItemBuff;
        int out1Len;
        wchar_t* out2 = DrawItemBuff + 512;
        int out2Len;

        // One call. This used to fork on f->UseWideName() and, in the wide
        // arm, build a SECOND wide copy of the name in out0W before calling GetTileTexts
        // anyway and discarding its out0. The predicate died with CFileData::NameW, so only
        // the arm below has ever run - and GetTileTexts now applies AlterFileNameW itself,
        // so that format fix lives there instead of being duplicated here.
        GetTileTexts(f, isDir ? (isItemUpDir ? 2 /* UP-DIR */ : 1) : 0, hDC, maxTextWidth, &widthNeeded,
                     out0, &out0Len, out1, &out1Len, out2, &out2Len,
                     ValidFileData, &PluginData, Is(ptDisk));

        if (isItemUpDir)
        {
            out0Len = 0;
        }

        // outer rectangle from which I clear towards the inner one
        RECT outerRect = rect;
        outerRect.left += TILE_LEFT_MARGIN + IconSizes[iconSize];

        //int oldRTop = r.top;

        // inner rectangle towards which I clear
        RECT r;
        r.left = outerRect.left + 2;
        r.right = r.left + 2 + widthNeeded + 2 + 1;

        int visibleLines = 1; // name is definitely visible
        if (out1[0] != 0)
            visibleLines++;
        if (out2[0] != 0)
            visibleLines++;
        int textH = visibleLines * FontCharHeight + 4;
        int textX = r.left + 2;
        int textY = rect.top + (rect.bottom - rect.top - textH) / 2; // we center

        r.top = textY;
        r.bottom = textY + textH;
        RECT focusR = r; // we save for focus drawing

        if (drawFocusFrame)
        {
            r.left++;
            r.right--;
            r.top++;
        }

        textY += 2;

        // clear the background around the text
        if ((drawFlags & DRAWFLAG_MASK) == 0) // when drawing the mask (b&w), the background color must not be painted
            FillIntersectionRegion(hDC, &outerRect, &r);

        //    if (drawFocusFrame) // if there won't be a frame displayed, reduce the area by it
        //      InflateRect(&r, -1, -1);

        // display the first line and also clear the background of the area above it
        r.bottom = textY + FontCharHeight;
        if (out1[0] == 0 && out2[0] == 0) // if this is the last line, clear the background of the area below it
        {
            r.bottom += 2;
            if (drawFocusFrame)
                r.bottom--;
        }
        // DRAWFLAG_MASK: hack, under XP some stuff is added in font of the text in the mask while drawing short texts; not an issue if text is not drawn
        sally::ui::DrawPanelTextW(hDC, textX, textY, ETO_OPAQUE, &r, out0, (drawFlags & DRAWFLAG_MASK) ? 0 : out0Len, NULL);

        // display the second line
        if (out1[0] != 0)
        {
            textY += FontCharHeight;
            r.top = r.bottom;
            r.bottom = r.top + FontCharHeight;
            if (out2[0] == 0) // if this is the last line, clear the background of the area below it
            {
                r.bottom += 2;
                if (drawFocusFrame)
                    r.bottom--;
            }
            // DRAWFLAG_MASK: hack, under XP some stuff is added in font of the text in the mask while drawing short texts; not an issue if text is not drawn
            sally::ui::DrawPanelTextW(hDC, textX, textY, ETO_OPAQUE, &r, out1, (drawFlags & DRAWFLAG_MASK) ? 0 : out1Len, NULL);
        }
        // display the third line and clear the background of the area below it
        if (out2[0] != 0)
        {
            r.top = r.bottom;
            r.bottom = r.top + FontCharHeight + 2;
            if (drawFocusFrame)
                r.bottom--;
            textY += FontCharHeight;
            // DRAWFLAG_MASK: hack, under XP some stuff is added in font of the text in the mask while drawing short texts; not an issue if text is not drawn
            sally::ui::DrawPanelTextW(hDC, textX, textY, ETO_OPAQUE, &r, out2, (drawFlags & DRAWFLAG_MASK) ? 0 : out2Len, NULL);
        }

        //*****************************************
        //
        // drawing the focus frame
        //
        if (drawFocusFrame)
        {
            DrawFocusRect(hDC, &focusR, (f->Selected == 1), Parent->EditMode);
        }
    }
}

//****************************************************************************
//
// StateImageList_Draw
//

BOOL StateImageList_Draw(CIconList* iconList, int imageIndex, HDC hDC, int xDst, int yDst,
                         DWORD state, CIconSizeEnum iconSize, DWORD iconOverlayIndex,
                         const RECT* overlayRect, BOOL overlayOnly, BOOL iconOverlayFromPlugin,
                         int pluginIconOverlaysCount, HICON* pluginIconOverlays)
{
    COLORREF rgbFg = CLR_DEFAULT;
    BOOL blend = FALSE;
    if (Configuration.UseIconTincture)
    {
        if (state & IMAGE_STATE_FOCUSED && state & IMAGE_STATE_SELECTED)
            rgbFg = ResolveDarkBaseColor(ICON_BLEND_FOCSEL, GetCOLORREF(CurrentColors[ICON_BLEND_FOCSEL]), DarkMode_ShouldUseDark());
        else if (state & IMAGE_STATE_FOCUSED)
            rgbFg = ResolveDarkBaseColor(ICON_BLEND_FOCUSED, GetCOLORREF(CurrentColors[ICON_BLEND_FOCUSED]), DarkMode_ShouldUseDark());
        else if (state & IMAGE_STATE_SELECTED)
            rgbFg = GetCOLORREF(CurrentColors[ICON_BLEND_SELECTED]);
        else if (state & IMAGE_STATE_HIDDEN)
            rgbFg = CLR_NONE;
        blend = state & IMAGE_STATE_FOCUSED || state & IMAGE_STATE_SELECTED || state & IMAGE_STATE_HIDDEN;
    }
    DWORD flags;
    COLORREF rgbBk;
    if (blend)
    {
        rgbBk = GetCOLORREF(CurrentColors[ITEM_BK_NORMAL]);
        flags = ILD_NORMAL | ILD_BLEND50;
    }
    else
    {
        rgbBk = CLR_DEFAULT;
        flags = ILD_NORMAL;
    }

    // the overlay may (in case of a thumbnail) lie outside the icon (in the thumbnail's lower left corner)
    int xOverlayDst = xDst;
    int yOverlayDst = yDst;

    // on Vista a 48x48 icon uses overlay ICONSIZE_32 and thumbnails use overlay ICONSIZE_48
    if (iconSize == ICONSIZE_48 && overlayRect == NULL)
    {
        iconSize = ICONSIZE_32;
        yOverlayDst += 48 - 32;
    }

    int iconW = IconSizes[iconSize];
    int iconH = IconSizes[iconSize];

    // for a thumbnail overlayRect != NULL and we move the overlay to its lower left corner
    if (overlayRect != NULL)
    {
        xOverlayDst = overlayRect->left;
        yOverlayDst = overlayRect->bottom - iconH;
    }

    if (state & IMAGE_STATE_MASK)
    {
        // we must swap the order because DrawIconEx doesn't draw the mask transparently
        if (iconOverlayIndex != ICONOVERLAYINDEX_NOTUSED) // if this overlay is loaded it has higher priority than the overlays below
        {
            if (iconOverlayFromPlugin)
            {
                if ((int)iconOverlayIndex < pluginIconOverlaysCount)
                {
                    DrawIconEx(hDC, xOverlayDst, yOverlayDst, pluginIconOverlays[3 * iconOverlayIndex + iconSize],
                               iconW, iconH, 0, NULL, DI_MASK);
                }
                else
                    TRACE_E("StateImageList_Draw(): invalid icon-overlay index: " << iconOverlayIndex << ", max = " << pluginIconOverlaysCount);
            }
            else
            {
                DrawIconEx(hDC, xOverlayDst, yOverlayDst, ShellIconOverlays.GetIconOverlay(iconOverlayIndex, iconSize),
                           iconW, iconH, 0, NULL, DI_MASK);
            }
        }
        else
        {
            if (state & IMAGE_STATE_SHARED)
                DrawIconEx(hDC, xOverlayDst, yOverlayDst, HSharedOverlays[iconSize],
                           iconW, iconH, 0, NULL, DI_MASK);
            else
            {
                if (state & IMAGE_STATE_SHORTCUT)
                    DrawIconEx(hDC, xOverlayDst, yOverlayDst, HShortcutOverlays[iconSize],
                               iconW, iconH, 0, NULL, DI_MASK);
                else
                {
                    if (state & IMAGE_STATE_OFFLINE)
                        DrawIconEx(hDC, xOverlayDst, yOverlayDst, HSlowFileOverlays[iconSize],
                                   iconW, iconH, 0, NULL, DI_MASK);
                }
            }
        }
        if (!overlayOnly)
            iconList->Draw(imageIndex, hDC, xDst, yDst, rgbFg, IL_DRAW_MASK);
    }
    else
    {
        if (!overlayOnly)
            iconList->Draw(imageIndex, hDC, xDst, yDst, rgbFg, blend ? IL_DRAW_BLEND : 0);
        if (iconOverlayIndex != ICONOVERLAYINDEX_NOTUSED) // if this overlay is loaded it has higher priority than the overlays below
        {
            if (iconOverlayFromPlugin)
            {
                if ((int)iconOverlayIndex < pluginIconOverlaysCount)
                {
                    DrawIconEx(hDC, xOverlayDst, yOverlayDst, pluginIconOverlays[3 * iconOverlayIndex + iconSize],
                               iconW, iconH, 0, NULL, DI_NORMAL);
                }
                else
                    TRACE_E("StateImageList_Draw(): invalid icon-overlay index: " << iconOverlayIndex << ", max = " << pluginIconOverlaysCount);
            }
            else
            {
                DrawIconEx(hDC, xOverlayDst, yOverlayDst, ShellIconOverlays.GetIconOverlay(iconOverlayIndex, iconSize),
                           iconW, iconH, 0, NULL, DI_NORMAL);
            }
        }
        else
        {
            if (state & IMAGE_STATE_SHARED)
                DrawIconEx(hDC, xOverlayDst, yOverlayDst, HSharedOverlays[iconSize],
                           iconW, iconH, 0, NULL, DI_NORMAL);
            else
            {
                if (state & IMAGE_STATE_SHORTCUT)
                    DrawIconEx(hDC, xOverlayDst, yOverlayDst, HShortcutOverlays[iconSize],
                               iconW, iconH, 0, NULL, DI_NORMAL);
                else
                {
                    if (state & IMAGE_STATE_OFFLINE)
                        DrawIconEx(hDC, xOverlayDst, yOverlayDst, HSlowFileOverlays[iconSize],
                                   iconW, iconH, 0, NULL, DI_NORMAL);
                }
            }
        }
    }

    return TRUE;
}
