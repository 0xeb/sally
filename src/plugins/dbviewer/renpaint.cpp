// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"

#include "dbviewer.rh"
#include "dbviewer.rh2"
#include "lang\lang.rh"
#include "data.h"
#include "renderer.h"
#include "dialogs.h"
#include "dbviewer.h"
#include "plugindarkmode.h"
#include "common/PanelTextPainter.h"
#include "display_text.h"

//****************************************************************************
//
// CRendererWindow
//

void CRendererWindow::PaintTopMargin(HDC hDC, HRGN hUpdateRgn, const RECT* clipRect, BOOL selChangeOnly)
{
    RECT r;
    r.top = 0;
    r.bottom = RowHeight - 1;

    PluginDarkModeColors colors;
    PluginDarkMode_GetColors(&colors);
    COLORREF oldTextColor = SetTextColor(hDC, colors.DialogText);
    COLORREF normalBkColor = colors.DialogBackground;
    COLORREF selectionBkColor = colors.InactiveSelection;
    COLORREF oldBkColor = SetBkColor(hDC, normalBkColor);
    HPEN hOldPen = (HPEN)SelectObject(hDC, HGrayPen);

    BOOL leftExcluded = FALSE;
    int x = 0;

    if (!((x < clipRect->left && x + RowHeight <= clipRect->left) ||
          (x >= clipRect->right && x + RowHeight > clipRect->right)))
    {
        if (!selChangeOnly)
        {
            // draw the zeroth column
            SelectClipRgn(hDC, hUpdateRgn);
            r.left = x;
            r.right = x + RowHeight - 1;
            sally::ui::DrawPanelTextW(hDC, x + LeftTextMargin, TopTextMargin, ETO_OPAQUE,
                                      &r, L"", 0, NULL);
            // draw separator lines
            MoveToEx(hDC, r.left, r.bottom, NULL);
            LineTo(hDC, r.right, r.bottom);
            LineTo(hDC, r.right, r.top - 1);
            ExcludeClipRect(hDC, 0, 0, RowHeight, RowHeight);
            leftExcluded = TRUE;
        }
    }
    x = RowHeight - XOffset;

    int visibleIndex = 0;
    int i;
    for (i = 0; i < Database.GetColumnCount(); i++)
    {
        const CDatabaseColumn* column = Database.GetColumn(i);
        if (column->Visible)
        {
            if (!((x < clipRect->left && x + column->Width <= clipRect->left) ||
                  (x >= clipRect->right && x + column->Width > clipRect->right)))
            {
                if (!selChangeOnly || Selection.ChangedColumn(&OldSelection, visibleIndex))
                {
                    BOOL inSelection = FALSE;
                    if (Selection.ContainsColumn(visibleIndex))
                    {
                        SetBkColor(hDC, selectionBkColor);
                        SelectObject(hDC, HSelectionPen);
                        inSelection = TRUE;
                    }

                    // display the text

                    if (selChangeOnly && x < RowHeight && !leftExcluded)
                    {
                        ExcludeClipRect(hDC, 0, 0, RowHeight, RowHeight);
                        leftExcluded = TRUE;
                    }

                    r.left = x;
                    r.right = x + column->Width - 1;

                    if (!Database.GetIsUnicode())
                    {
                        std::wstring text;
                        sally::dbviewer::DecodeLegacyDisplayText(
                            column->Name, strlen(column->Name),
                            UseCodeTable ? reinterpret_cast<const unsigned char*>(CodeTable) : nullptr,
                            text);
                        sally::ui::DrawPanelTextW(hDC, x + LeftTextMargin, TopTextMargin, ETO_OPAQUE,
                                                  &r, text.c_str(), static_cast<UINT>(text.size()), NULL);
                    }
                    else
                    {
                        LPCWSTR colName = (LPCWSTR)column->Name;

                        sally::ui::DrawPanelTextW(hDC, x + LeftTextMargin, TopTextMargin, ETO_OPAQUE,
                                                  &r, colName, (UINT)wcslen(colName), NULL);
                    }

                    // draw separator lines
                    MoveToEx(hDC, r.left, r.bottom, NULL);
                    LineTo(hDC, r.right, r.bottom);
                    LineTo(hDC, r.right, r.top - 1);

                    if (inSelection)
                    {
                        SetBkColor(hDC, normalBkColor);
                        SelectObject(hDC, HGrayPen);
                    }
                }
            }
            x += column->Width;
            visibleIndex++;
        }
    }

    if (x < Width)
    {
        // fill the empty space behind the header
        r.left = x;
        r.right = Width;
        r.bottom++;
        SetBkColor(hDC, colors.InputBackground);
        sally::ui::DrawPanelTextW(hDC, 0, 0, ETO_OPAQUE, &r, L"", 0, NULL);
    }

    SelectObject(hDC, hOldPen);
    SetTextColor(hDC, oldTextColor);
    SetTextColor(hDC, oldBkColor);
}

void CRendererWindow::PaintBody(HDC hDC, HRGN hUpdateRgn, const RECT* clipRect, BOOL selChangeOnly)
{
    PluginDarkModeColors colors;
    PluginDarkMode_GetColors(&colors);
    COLORREF oldTextColor = SetTextColor(hDC, colors.InputText);
    COLORREF normalBkColor = colors.InputBackground;
    COLORREF selectionBkColor = colors.InactiveSelection;
    COLORREF oldBkColor = SetBkColor(hDC, normalBkColor);
    HPEN hOldPen = (HPEN)GetCurrentObject(hDC, OBJ_PEN);

    RECT r;
    r.top = RowHeight;
    r.bottom = 2 * RowHeight - 1;

    int fetchedIndex = -1;

    int i;
    for (i = TopIndex; i < Database.GetRowCount(); i++)
    {
        if (!((r.top <= clipRect->top && r.bottom < clipRect->top) ||
              (r.top > clipRect->bottom && r.bottom > clipRect->bottom)))
        {
            BOOL leftExcluded = FALSE;
            int x = 0;

            if (!((x < clipRect->left && x + RowHeight <= clipRect->left) ||
                  (x >= clipRect->right && x + RowHeight > clipRect->right)))
            {
                if (!selChangeOnly || Selection.ChangedRow(&OldSelection, i))
                {
                    // draw the left column
                    COLORREF oldBkColor2;
                    if (Selection.ContainsRow(i))
                    {
                        oldBkColor2 = SetBkColor(hDC, selectionBkColor);
                        SelectObject(hDC, HSelectionPen);
                    }
                    else
                    {
                        oldBkColor2 = SetBkColor(hDC, colors.DialogBackground);
                        SelectObject(hDC, HGrayPen);
                    }
                    SelectClipRgn(hDC, hUpdateRgn);
                    r.left = 0;
                    r.right = RowHeight - 1;
                    sally::ui::DrawPanelTextW(hDC, 0, r.top, ETO_OPAQUE, &r, L"", 0, NULL);
                    if (fetchedIndex != i)
                    {
                        if (!Database.FetchRecord(HWindow, i))
                        {
                            // Fetching the record failed -> do not draw anything
                            continue;
                        }
                        fetchedIndex = i;
                    }
                    if (Database.IsRecordDeleted())
                    {
                        if (HDeleteIcon == NULL)
                            HDeleteIcon = (HICON)LoadImage(DLLInstance, MAKEINTRESOURCE(IDI_DELETED), IMAGE_ICON, 16, 16, SalGeneral->GetIconLRFlags());
                        DrawIconEx(hDC, (RowHeight - 16) / 2 + 1, r.top + (RowHeight - 16) / 2 + 1,
                                   HDeleteIcon, 0, 0, 0, NULL, DI_NORMAL);
                    }
                    // draw separator lines
                    MoveToEx(hDC, r.left, r.bottom, NULL);
                    LineTo(hDC, r.right, r.bottom);
                    LineTo(hDC, r.right, r.top - 1);
                    SetBkColor(hDC, oldBkColor2);
                    ExcludeClipRect(hDC, 0, RowHeight, RowHeight, Height);
                    leftExcluded = TRUE;
                }
            }

            SelectObject(hDC, HLtGrayPen);
            x = RowHeight - XOffset;

            // draw visible columns
            int visibleIndex = 0;
            int j;
            for (j = 0; j < Database.GetColumnCount(); j++)
            {
                const CDatabaseColumn* column = Database.GetColumn(j);
                if (column->Visible)
                {
                    if (!((x < clipRect->left && x + column->Width <= clipRect->left) ||
                          (x >= clipRect->right && x + column->Width > clipRect->right)))
                    {
                        if (!selChangeOnly || Selection.Changed(&OldSelection, visibleIndex, i))
                        {
                            //              TRACE_I("Paint row="<<i<<" col="<<j);
                            // retrieve the text
                            if (fetchedIndex != i)
                            {
                                if (!Database.FetchRecord(HWindow, i))
                                {
                                    // Fetching the record failed -> do not draw anything
                                    continue;
                                }
                                fetchedIndex = i;
                            }
                            if (selChangeOnly && x < RowHeight && !leftExcluded)
                            {
                                ExcludeClipRect(hDC, 0, RowHeight, RowHeight, Height);
                                leftExcluded = TRUE;
                            }

                            r.left = x;
                            r.right = x + column->Width - 1;

                            size_t textLen;
                            SIZE sz;
                            LPCWSTR textW = L"";
                            std::wstring decodedText;
                            std::wstring localizedText;

                            if (Database.TryGetLocalizedCellText(column, localizedText))
                            {
                                textW = localizedText.c_str();
                                textLen = localizedText.size();
                            }
                            else if (!Database.GetIsUnicode())
                            {
                                const char* text = Database.GetCellText(column, &textLen);
                                sally::dbviewer::DecodeLegacyDisplayText(
                                    text, textLen,
                                    UseCodeTable ? reinterpret_cast<const unsigned char*>(CodeTable) : nullptr,
                                    decodedText);
                                textW = decodedText.c_str();
                                textLen = decodedText.size();
                            }
                            else
                            {
                                textW = Database.GetCellTextW(column, &textLen);
                            }
                            GetTextExtentPoint32W(hDC, textW, static_cast<int>(textLen), &sz);

                            // draw the text
                            int xOffset = 0;
                            if (!column->LeftAlign)
                                xOffset = r.right - r.left - 2 * LeftTextMargin - sz.cx;

                            BOOL inSelection = FALSE;
                            if (Selection.Contains(visibleIndex, i))
                            {
                                SetBkColor(hDC, selectionBkColor);
                                inSelection = TRUE;
                            }
                            sally::ui::DrawPanelTextW(hDC, x + LeftTextMargin + xOffset, r.top + TopTextMargin,
                                                      ETO_OPAQUE, &r, textW, static_cast<UINT>(textLen), NULL);
                            if (Bookmarks.IsMarked(visibleIndex, i))
                            {
                                if (HMarkedIcon == NULL)
                                    HMarkedIcon = (HICON)LoadImage(DLLInstance, MAKEINTRESOURCE(IDI_MARKED),
                                                                   IMAGE_ICON, 16, 16, SalGeneral->GetIconLRFlags());
                                DrawIconEx(hDC, r.right - 16, r.top,
                                           HMarkedIcon, 0, 0, 0, NULL, DI_NORMAL);
                            }

                            if (inSelection)
                            {
                                SetBkColor(hDC, normalBkColor);
                                if (Selection.IsFocus(visibleIndex, i))
                                {
                                    SelectObject(hDC, HBlackPen);
                                    MoveToEx(hDC, r.left, r.top, NULL);
                                    LineTo(hDC, r.left, r.bottom - 1);
                                    LineTo(hDC, r.right - 1, r.bottom - 1);
                                    LineTo(hDC, r.right - 1, r.top);
                                    LineTo(hDC, r.left, r.top);
                                    SelectObject(hDC, HLtGrayPen);
                                }
                            }

                            // draw separator lines
                            MoveToEx(hDC, r.left, r.bottom, NULL);
                            LineTo(hDC, r.right, r.bottom);
                            LineTo(hDC, r.right, r.top - 1);
                        }
                    }
                    x += column->Width;
                    visibleIndex++;
                }
            }

            if (x < Width)
            {
                // fill the remainder of the empty row
                RECT r2;
                r2 = r;
                r2.left = x;
                r2.right = Width;
                r2.bottom++;
                sally::ui::DrawPanelTextW(hDC, 0, 0, ETO_OPAQUE, &r2, L"", 0, NULL);
            }
        }

        r.top += RowHeight;
        r.bottom += RowHeight;

        if (r.top >= Height)
            break;
    }

    if (r.top < Height)
    {
        // fill the empty space below the table
        SelectClipRgn(hDC, hUpdateRgn);
        r.left = 0;
        r.right = Width;
        r.bottom = Height;
        sally::ui::DrawPanelTextW(hDC, 0, 0, ETO_OPAQUE, &r, L"", 0, NULL);
    }

    SelectObject(hDC, hOldPen);
    SetTextColor(hDC, oldTextColor);
    SetTextColor(hDC, oldBkColor);
}

void CRendererWindow::Paint(HDC hDC, HRGN hUpdateRgn, BOOL selChangeOnly)
{
    if (Database.IsOpened())
    {
        BOOL releaseDC = FALSE;
        if (hDC == NULL)
        {
            hDC = GetDC(HWindow);
            releaseDC = TRUE;

            if (hUpdateRgn != NULL)
                SelectClipRgn(hDC, hUpdateRgn);
        }

        RECT clipRect;
        int clipRectType = GetClipBox(hDC, &clipRect);
        if (clipRectType != SIMPLEREGION && clipRectType != COMPLEXREGION)
        {
            clipRect.left = 0;
            clipRect.top = 0;
            clipRect.right = Width;
            clipRect.bottom = Height;
        }

        HFONT hOldFont = (HFONT)SelectObject(hDC, HFont);
        PaintTopMargin(hDC, hUpdateRgn, &clipRect, selChangeOnly);
        PaintBody(hDC, hUpdateRgn, &clipRect, selChangeOnly);
        SelectObject(hDC, hOldFont);

        if (hUpdateRgn != NULL)
            SelectClipRgn(hDC, NULL); // remove the clip region if it was set

        if (releaseDC)
            ReleaseDC(HWindow, hDC);

        if (!selChangeOnly)
            ValidateRect(HWindow, NULL);
    }
}
