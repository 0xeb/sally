// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"
#include "plugindarkmode.h"

#include "dbviewer.rh"
#include "dbviewer.rh2"
#include "lang\lang.rh"
#include "data.h"
#include "renderer.h"
#include "dialogs.h"
#include "dbviewer.h"
#include "widefind.h"
#include "display_text.h"

#define GET_X_LPARAM(lp) ((int)(short)LOWORD(lp))
#define GET_Y_LPARAM(lp) ((int)(short)HIWORD(lp))

#define TIMER_SCROLL_ID 1

BOOL IsAlphaNumeric[256]; // TRUE/FALSE table for characters (FALSE = neither a letter nor a digit)
BOOL IsAlpha[256];

//****************************************************************************
//
// CSelection
//

CSelection::CSelection()
{
    FocusX = 0;
    FocusY = 0;
    AnchorX = 0;
    AnchorY = 0;
    Normalize();
}

CSelection&
CSelection::operator=(const CSelection& s)
{
    FocusX = s.FocusX;
    FocusY = s.FocusY;
    AnchorX = s.AnchorX;
    AnchorY = s.AnchorY;
    Rect = s.Rect;
    return *this;
}

void CSelection::Normalize()
{
    if (FocusX <= AnchorX)
    {
        Rect.left = FocusX;
        Rect.right = AnchorX;
    }
    else
    {
        Rect.left = AnchorX;
        Rect.right = FocusX;
    }
    if (FocusY <= AnchorY)
    {
        Rect.top = FocusY;
        Rect.bottom = AnchorY;
    }
    else
    {
        Rect.top = AnchorY;
        Rect.bottom = FocusY;
    }
}

//****************************************************************************
//
// CBookmarkList
//

CBookmarkList::CBookmarkList()
    : Bookmarks(10, 10)
{
}

void CBookmarkList::Toggle(int x, int y)
{
    int index;
    if (GetIndex(x, y, &index))
    {
        Bookmarks.Delete(index);
    }
    else
    {
        CBookmark bookmark;
        bookmark.X = x;
        bookmark.Y = y;
        Bookmarks.Add(bookmark);
    }
}

BOOL CBookmarkList::GetNext(int x, int y, int* newX, int* newY, BOOL next)
{
    int count = Bookmarks.Count;
    int index;
    if (!GetIndex(x, y, &index))
    {
        if (count > 0)
        {
            *newX = Bookmarks[next ? 0 : count - 1].X;
            *newY = Bookmarks[next ? 0 : count - 1].Y;
            return TRUE;
        }
        else
            return FALSE;
    }
    else
    {
        if (count < 2)
            return FALSE;
        int newIndex = index + (next ? 1 : -1);
        if (newIndex >= count)
            newIndex = 0;
        if (newIndex < 0)
            newIndex = count - 1;
        *newX = Bookmarks[newIndex].X;
        *newY = Bookmarks[newIndex].Y;
        return TRUE;
    }
}

BOOL CBookmarkList::IsMarked(int x, int y)
{
    int index;
    return GetIndex(x, y, &index);
}

void CBookmarkList::ClearAll()
{
    Bookmarks.DestroyMembers();
}

BOOL CBookmarkList::GetIndex(int x, int y, int* index)
{
    int count = Bookmarks.Count;
    int i;
    for (i = 0; i < count; i++)
    {
        CBookmark* bookmark = &Bookmarks[i];
        if (bookmark->X == x && bookmark->Y == y)
        {
            *index = i;
            return TRUE;
        }
    }
    return FALSE;
}

//****************************************************************************
//
// CRendererWindow
//

CRendererWindow::CRendererWindow(int enumFilesSourceUID, int enumFilesCurrentIndex)
    : CWindow(ooStatic),
      EnumFilesSourceUID(enumFilesSourceUID), EnumFilesCurrentIndex(enumFilesCurrentIndex)
{
    Database.SetRenderer(this);
    HGrayPen = NULL;
    HLtGrayPen = NULL;
    HSelectionPen = NULL;
    HBlackPen = NULL;
    HFont = NULL;
    HDeleteIcon = NULL;
    HMarkedIcon = NULL;
    RowHeight = 0;
    CharAvgWidth = 0;
    TopTextMargin = 0;
    LeftTextMargin = 0;
    Width = 0;
    Height = 0;
    TopIndex = 0;
    XOffset = 0;
    RowsOnPage = 1;
    DragMode = dsmNone;
    ScrollTimerID = 0;
    DragColumn = -1;

    AutoSelect = CfgAutoSelect;
    DefaultCoding = CfgDefaultCoding;
    UseCodeTable = FALSE;
    Coding.clear();

    Creating = TRUE;

    CreateGraphics();

    ResetMouseWheelAccumulator();
}

CRendererWindow::~CRendererWindow()
{
    ReleaseGraphics();
    Database.Close();
    if (HDeleteIcon != NULL)
    {
        DestroyIcon(HDeleteIcon);
        HDeleteIcon = NULL;
    }
    if (HMarkedIcon != NULL)
    {
        DestroyIcon(HMarkedIcon);
        HMarkedIcon = NULL;
    }
}

void CRendererWindow::OnFileOpen()
{
    OPENFILENAMEW ofn;
    memset(&ofn, 0, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = HWindow;
    ofn.nFilterIndex = 1;
    ofn.lpstrInitialDir = NULL;
    ofn.Flags = OFN_HIDEREADONLY | OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;

    const std::wstring filterOwner = LangStr(IDS_VIEWERFILTER);
    const wchar_t* filter = filterOwner.c_str();
    std::wstring filterW;
    for (const wchar_t* s = filter;; s++)
    {
        if (*s == L'|')
            filterW.push_back(L'\0');
        else if (*s == 0)
        {
            filterW.push_back(L'\0');
            break;
        }
        else
            filterW.push_back(*s);
    }
    filterW.push_back(L'\0');
    std::vector<std::wstring> files{std::wstring()};
    ofn.lpstrFilter = filterW.c_str();
    if (SPLSafeGetOpenFileNamesOwned(SalGeneral, &ofn, files))
    {
        EnumFilesSourceUID = -1;
        OpenFile(files[0].c_str(), TRUE);
    }
}

void CRendererWindow::OnFileReOpen()
{
    if (!Database.IsOpened())
        return;

    const std::wstring path = Database.GetFileName();
    OpenFile(path.c_str(), FALSE);
}

void CRendererWindow::OnGoto()
{
    if (Viewer->Enablers[vweDBOpened])
    {
        int x, y, count = Database.GetRowCount();

        Selection.GetFocus(&x, &y);
        CGoToDialog dlg(HWindow, &y, count);

        if ((dlg.Execute() == IDOK) && (y != count))
        {
            Selection.SetFocusAndAnchor(x, y);
            Viewer->UpdateRowNumberOnToolBar(y, count);
            EnsureRowIsVisible(y);
            Paint(NULL, NULL, FALSE);
        }
    }
}

void CRendererWindow::SetViewerTitle()
{
    std::wstring title;
    if (Database.IsOpened())
    {
        title = Database.GetFileName();
        title += L" - ";
        title += LangStr(IDS_PLUGINNAME).c_str();
        if (UseCodeTable || Database.GetIsUnicode())
        {
            title += L" - [";
            title += Coding;
            title += L"]";
        }
    }
    else
        title = LangStr(IDS_PLUGINNAME).c_str();

    SetWindowTextW(GetParent(HWindow), title.c_str());
}

BOOL CRendererWindow::OpenFile(const wchar_t* name, BOOL useDefaultConfig)
{
    CALL_STACK_MESSAGE3("CRendererWindow::OpenFile(%ls, %d)", name, useDefaultConfig);

    if (useDefaultConfig)
        Viewer->CfgCSV = CfgDefaultCSV;
    Bookmarks.ClearAll();
    // if OpenFile fails, the background will be filled
    InvalidateRect(HWindow, NULL, TRUE);

    BOOL ret = Database.Open(name);

    if (ret)
    {
        Viewer->UpdateEnablers(); // Update the Unicode flag
        if (AutoSelect || Database.GetIsUnicode())
            RecognizeCodePage();
        else
        {
            UseCodeTable = FALSE;
            Coding.clear();
            if (!DefaultCoding.empty())
            {
                char codeTable[256];
                if (SalGeneral->GetConversionTable(HWindow, codeTable, DefaultCoding.c_str()))
                {
                    memcpy(CodeTable, codeTable, 256);
                    UseCodeTable = TRUE;
                    Coding = DefaultCoding;
                }
            }
        }
    }

    TopIndex = 0;
    XOffset = 0;
    Selection.SetFocusAndAnchor(0, 0);
    // No row is active when there are no rows in the database
    Viewer->UpdateRowNumberOnToolBar(Database.GetRowCount() ? 0 : -1, Database.GetRowCount());
    OldSelection = Selection;

    SetViewerTitle();

    SetupScrollBars();
    InvalidateRect(HWindow, NULL, TRUE);

    Viewer->UpdateEnablers();

    return ret;
}

void CRendererWindow::OnToggleBookmark()
{
    int focusX, focusY;
    Selection.GetFocus(&focusX, &focusY);
    Bookmarks.Toggle(focusX, focusY);
    Paint(NULL, NULL, FALSE);
}

void CRendererWindow::OnNextBookmark(BOOL next)
{
    int focusX, focusY;
    Selection.GetFocus(&focusX, &focusY);
    int x, y;
    if (Bookmarks.GetNext(focusX, focusY, &x, &y, next))
    {
        OldSelection = Selection;
        Selection.SetFocusAndAnchor(x, y);
        Viewer->UpdateRowNumberOnToolBar(y, Database.GetRowCount());
        EnsureRowIsVisible(y);
        EnsureColumnIsVisible(x);
        Paint(NULL, NULL, FALSE);
    }
}

void CRendererWindow::OnClearBookmarks()
{
    if (Bookmarks.GetCount() < 1)
        return;
    Bookmarks.ClearAll();
    Paint(NULL, NULL, FALSE);
}

void CRendererWindow::RecognizeCodePage()
{
    if (!Database.IsOpened())
        return;

    UseCodeTable = FALSE;

    if (Database.GetIsUnicode())
    {
        Coding = Database.GetIsUTF8() ? L"UTF-8" : L"UTF-16";
        return;
    }

    Coding.clear();

    std::wstring winCodePage;
    SPLGetWindowsCodePageOwned(SalGeneral, HWindow, winCodePage);
    if (!winCodePage.empty()) // only if WindowsCodePage is known
    {
        char pattern[10000];
        size_t spaceLeft = 9999;
        char* iter = pattern;

        int i;
        for (i = 0; i < min(100, Database.GetRowCount()); i++)
        {
            if (!Database.FetchRecord(HWindow, i))
                goto STOP_FETCHING;
            int j;
            for (j = 0; j < Database.GetVisibleColumnCount(); j++)
            {
                const CDatabaseColumn* column = Database.GetVisibleColumn(j);
                size_t textLen;
                const char* text = Database.GetCellText(column, &textLen);
                size_t copyChars = min(textLen, spaceLeft);
                memcpy(iter, text, copyChars);
                spaceLeft -= copyChars;
                iter += copyChars;
                if (spaceLeft == 0)
                    goto STOP_FETCHING;
            }
        }

    STOP_FETCHING:

        if (iter > pattern)
        {
            *iter = 0;
            std::wstring codePage;
            SPLRecognizeFileTypeOwned(SalGeneral, HWindow, pattern,
                                      static_cast<int>(iter - pattern), TRUE,
                                      NULL, &codePage);
            if (!codePage.empty())
            {
                std::wstring conversion = codePage;
                conversion += L" - ";
                conversion += winCodePage;

                char codeTable[256];
                if (lstrcmpW(codePage.c_str(), winCodePage.c_str()) != 0 &&
                    SalGeneral->GetConversionTable(HWindow, codeTable, conversion.c_str()))
                {
                    Coding = codePage;
                    Coding += L" - ";
                    Coding += winCodePage;
                    memcpy(CodeTable, codeTable, 256);
                    UseCodeTable = TRUE;
                }
            }
        }
    }
} /* CRendererWindow::RecognizeCodePage */

void CRendererWindow::CodeCharacters(char* text, size_t textLen)
{
    if (UseCodeTable)
    {
        unsigned char* iter = (unsigned char*)text;
        while (textLen-- > 0)
        {
            *(iter++) = CodeTable[*iter];
        }
    }
}

void CRendererWindow::SelectConversion(const wchar_t* conversion)
{
    if (conversion == NULL)
    {
        UseCodeTable = FALSE;
        Coding.clear();
    }
    else
    {
        char codeTable[256];
        if (SalGeneral->GetConversionTable(HWindow, codeTable, conversion))
        {
            memcpy(CodeTable, codeTable, 256);
            UseCodeTable = TRUE;
            Coding = conversion;
        }
    }
    InvalidateRect(HWindow, NULL, TRUE);
    SetViewerTitle();
}

// In UTF-8 every byte of a multi-byte character is >= 0x80, so they must all count as
// word bytes - otherwise a whole-word match could be declared in the middle of a
// character. The ANSI table cannot answer this, because the same byte value means
// different things in the two encodings.
static bool IsWordByte(unsigned char b, bool utf8)
{
    if (utf8 && sally::dbviewer::IsUtf8WordByte(b))
        return true;
    return IsAlphaNumeric[b] != FALSE;
}

void CRendererWindow::Find(BOOL forward, BOOL wholeWords, BOOL caseSensitive,
                           FindMode mode, const std::wstring& pattern,
                           CSalamanderBMSearchData* bmSearchData,
                           CSalamanderREGEXPSearchData* regexpSearchData)
{
    const bool wideLiteral = mode == FindMode::WideLiteral;
    const bool encodedLiteral = mode == FindMode::EncodedLiteral;
    const bool utf8Regexp = mode == FindMode::Utf8RegularExpression;
    const bool encodedRegexp = mode == FindMode::EncodedRegularExpression || utf8Regexp;
    // Cells of a Unicode database are encoded through this code page before the byte
    // engine sees them. UTF-8 is lossless, so the "cannot be represented" refusal below
    // can only ever trigger on the ANSI path.
    const UINT cellCodePage = utf8Regexp ? CP_UTF8 : CP_ACP;
    if (pattern.empty() ||
        (wideLiteral && (bmSearchData != NULL || regexpSearchData != NULL)) ||
        (encodedLiteral && (bmSearchData == NULL || regexpSearchData != NULL)) ||
        (encodedRegexp && (bmSearchData != NULL || regexpSearchData == NULL)))
    {
        TRACE_E("Parameters mismatch");
        return;
    }

    HCURSOR hOldCursor = SetCursor(LoadCursor(NULL, IDC_WAIT));

    int row;
    int col;
    BOOL skip = TRUE; // skip the first match
    const size_t patLen = wideLiteral ? pattern.size() :
                                        encodedLiteral ? static_cast<size_t>(bmSearchData->GetLength()) : 0;
    Selection.GetFocus(&col, &row);
    std::string encodedCell;
    do
    {
        if (!Database.FetchRecord(HWindow, row))
            break;
        do
        {
            if (skip)
            {
                skip = FALSE;
            }
            else
            {
                const CDatabaseColumn* column = Database.GetVisibleColumn(col);

                // too narrow columns are not searched in non-regexp mode
                // other possible optimizations: do not search DBF_FTYPE_INT_V7,
                // DBF_FTYPE_TSTAMP, DBF_FTYPE_AUTOINC & DBF_FTYPE_DOUBLE columns if
                // the search pattern contains non-numeric characters
                if (!encodedLiteral || static_cast<size_t>(column->Length) >= patLen)
                {
                    int found; // -1=not found
                    size_t textLen;
                    int offset = 0;

                    if (wideLiteral)
                    {
                        LPCWSTR textW = Database.GetCellTextW(column, &textLen);
                        found = sally::dbviewer::FindWideSubstring(textW, textLen, pattern.data(), pattern.size(),
                                                                   caseSensitive != FALSE, wholeWords != FALSE, offset);
                    }
                    else
                    {
                        const char* text;
                        // Both 'continue' below used to jump to this outer
                        // do-while's condition, which skips the col++/col--
                        // advance at the bottom of the loop entirely - so an
                        // unrepresentable or oversized cell made Find re-search
                        // the SAME column forever. This flag lets a cell that
                        // cannot be searched fall through as "not found" and
                        // still reach the advance step, like every other miss.
                        bool cellUnsearchable = false;
                        if (!Database.GetIsUnicode())
                        {
                            text = Database.GetCellText(column, &textLen);

                            if (UseCodeTable)
                            {
                                encodedCell.assign(text, textLen);
                                CodeCharacters(encodedCell.data(), static_cast<int>(textLen));
                                text = encodedCell.data();
                            }
                        }
                        else
                        {
                            LPCWSTR textW = Database.GetCellTextW(column, &textLen);
                            // On the UTF-8 path this always succeeds, so every cell is
                            // searchable. On the ANSI path the engine is an encoded-byte
                            // interface: keep that compatibility boundary explicit and
                            // refuse substitution, since an unrepresentable Unicode cell
                            // cannot be searched faithfully and is skipped rather than
                            // collapsed to '?'.
                            if (!Win32EncodeText(cellCodePage, textW, textLen, encodedCell))
                                cellUnsearchable = true;
                            else
                            {
                                text = encodedCell.data();
                                textLen = encodedCell.size();
                            }
                        }
                        if (!cellUnsearchable &&
                            textLen > static_cast<size_t>((std::numeric_limits<int>::max)()))
                        {
                            cellUnsearchable = true;
                        }
                        if (cellUnsearchable)
                            found = -1;
                        else
                        {
                            do
                            {
                                int foundLen;
                                if (bmSearchData != NULL)
                                {
                                    found = bmSearchData->SearchForward(text, (int)textLen, offset);
                                }
                                else
                                {
                                    regexpSearchData->SetLine(text, text + textLen);
                                    found = regexpSearchData->SearchForward(offset, foundLen);
                                }
                                if (found == -1 || !wholeWords)
                                    break;
                                if (bmSearchData != NULL)
                                    foundLen = bmSearchData->GetLength();
                                if ((found == 0 || !IsWordByte(static_cast<unsigned char>(text[found - 1]), utf8Regexp)) &&
                                    (found + foundLen >= (int)textLen ||
                                     !IsWordByte(static_cast<unsigned char>(text[found + foundLen]), utf8Regexp)))
                                    break;
                                offset++;
                            } while (1);
                        }
                    }
                    if (found != -1)
                    {
                        SetCursor(hOldCursor);
                        OldSelection = Selection;
                        Selection.SetFocusAndAnchor(col, row);
                        Viewer->UpdateRowNumberOnToolBar(row, Database.GetRowCount());
                        EnsureRowIsVisible(row);
                        EnsureColumnIsVisible(col);
                        Paint(NULL, NULL, FALSE);
                        return;
                    }
                } // of if (!bmSearchData || (column->Length >= patLen))
            }

            if (forward)
            {
                col++;
                if (col >= Database.GetVisibleColumnCount())
                    break;
            }
            else
            {
                col--;
                if (col < 0)
                    break;
            }

        } while (1);

        if (forward)
        {
            row++;
            if (row >= Database.GetRowCount())
                break;
            col = 0;
        }
        else
        {
            row--;
            if (row < 0)
                break;
            col = Database.GetVisibleColumnCount() - 1;
        }
    } while (1);

    SetCursor(hOldCursor);

    const int messageID = encodedRegexp ? IDS_FIND_NOREGEXPMATCH : IDS_FIND_NOMATCH;
    const std::wstring text = SPLFormatStringOwned(
        SPLLoadStrOwned(SalGeneral, HLanguage, messageID).c_str(), pattern.c_str());
    SalGeneral->SalMessageBox(HWindow, text.c_str(), SPLLoadStrOwned(SalGeneral, HLanguage, IDS_FIND).c_str(), MB_ICONINFORMATION);
} /* CRendererWindow::Find */

void CRendererWindow::CreateGraphics()
{
    LOGFONT lf;
    if (CfgUseCustomFont)
        lf = CfgLogFont;
    else
        GetDefaultLogFont(&lf);
    HFont = CreateFontIndirect(&lf);

    HDC hDC = GetDC(NULL);
    HFONT oldFont = (HFONT)SelectObject(hDC, HFont);
    TEXTMETRIC tm;
    GetTextMetrics(hDC, &tm);
    RowHeight = tm.tmHeight + 4;
    TopTextMargin = 2;
    LeftTextMargin = 3;

    SIZE sz;
    GetTextExtentPoint32W(hDC, L"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz",
                          52, &sz);
    CharAvgWidth = (sz.cx / 26 + 1) / 2;

    SelectObject(hDC, oldFont);
    ReleaseDC(NULL, hDC);

    PluginDarkModeColors colors;
    PluginDarkMode_GetColors(&colors);
    HGrayPen = CreatePen(PS_SOLID, 0, colors.Border);
    HLtGrayPen = CreatePen(PS_SOLID, 0, colors.DialogBackground);
    HSelectionPen = CreatePen(PS_SOLID, 0, colors.Highlight);
    HBlackPen = CreatePen(PS_SOLID, 0, colors.DialogText);
}

void CRendererWindow::ReleaseGraphics()
{
    if (HFont != NULL)
    {
        DeleteObject(HFont);
        HFont = NULL;
    }
    if (HGrayPen != NULL)
    {
        DeleteObject(HGrayPen);
        HGrayPen = NULL;
    }
    if (HLtGrayPen != NULL)
    {
        DeleteObject(HLtGrayPen);
        HLtGrayPen = NULL;
    }
    if (HBlackPen != NULL)
    {
        DeleteObject(HBlackPen);
        HBlackPen = NULL;
    }
    if (HSelectionPen != NULL)
    {
        DeleteObject(HSelectionPen);
        HSelectionPen = NULL;
    }
}

void CRendererWindow::RebuildGraphics()
{
    ReleaseGraphics();
    CreateGraphics();
}

void CRendererWindow::SetupScrollBars(DWORD update)
{
    if (!Database.IsOpened())
        return;
    if (update & UPDATE_HORZ_SCROLL)
    {
        int rowWidth = Database.GetVisibleColumnsWidth();

        SCROLLINFO si;
        si.cbSize = sizeof(si);
        si.fMask = SIF_DISABLENOSCROLL | SIF_POS | SIF_RANGE | SIF_PAGE;
        si.nMin = 0;
        si.nMax = rowWidth - 1;
        si.nPage = Width - RowHeight; // exclude the left column
        si.nPos = XOffset;
        // update scrollbars
        SetScrollInfo(HWindow, SB_HORZ, &si, TRUE);
    }

    if (update & UPDATE_VERT_SCROLL)
    {
        int totalRows = Database.GetRowCount();
        SCROLLINFO si;
        si.cbSize = sizeof(SCROLLINFO);
        si.fMask = SIF_DISABLENOSCROLL | SIF_POS | SIF_RANGE | SIF_PAGE;
        si.nMin = 0;
        si.nMax = totalRows - 1;
        si.nPage = RowsOnPage;
        si.nPos = TopIndex;
        SetScrollInfo(HWindow, SB_VERT, &si, TRUE);
    }
}

BOOL CRendererWindow::GetColumnInfo(int visibleIndex, int* index, int* xPos)
{
    int visibleI = 0;
    int x = 0;
    int i;
    for (i = 0; i < Database.GetColumnCount(); i++)
    {
        const CDatabaseColumn* column = Database.GetColumn(i);
        if (column->Visible)
        {
            if (visibleI == visibleIndex)
            {
                *xPos = x;
                *index = i;
                return TRUE;
            }
            x += column->Width;
            visibleI++;
        }
    }
    return FALSE;
}

void CRendererWindow::EnsureColumnIsVisible(int x)
{
    if (x < 0 || x >= Database.GetVisibleColumnCount())
    {
        TRACE_E("Wrong column: x=" << x);
        return;
    }
    int newXOffset = XOffset;

    int colX = 0;
    int index = 0;
    if (!GetColumnInfo(x, &index, &colX))
        return;
    int colWidth = Database.GetColumn(index)->Width;
    if (colWidth > Width - RowHeight)
        colWidth = Width - RowHeight;

    if (colX < newXOffset)
        newXOffset = colX;
    else
    {
        if (colX + RowHeight + colWidth > XOffset + Width)
            newXOffset = colX - (Width - RowHeight - colWidth);
    }

    if (newXOffset != XOffset)
    {
        HRGN hUpdateRgn = CreateRectRgn(0, 0, 0, 0);
        RECT r;
        r.left = RowHeight;
        r.top = 0;
        r.right = Width;
        r.bottom = Height;
        ScrollWindowEx(HWindow, XOffset - newXOffset, 0,
                       &r, &r, hUpdateRgn, NULL, 0);
        XOffset = newXOffset;
        SetupScrollBars(UPDATE_HORZ_SCROLL);
        Paint(NULL, hUpdateRgn, FALSE);
        DeleteObject(hUpdateRgn);
    }
}

void CRendererWindow::EnsureRowIsVisible(int y)
{
    if (y < 0 || y >= Database.GetRowCount())
    {
        TRACE_E("Wrong row: y=" << y);
        return;
    }
    int newTopIndex = TopIndex;
    if (y < TopIndex)
        newTopIndex = y;
    else
    {
        if (y > TopIndex + RowsOnPage - 1)
        {
            newTopIndex = y - RowsOnPage + 1;
        }
    }
    if (newTopIndex != TopIndex)
    {
        HRGN hUpdateRgn = CreateRectRgn(0, 0, 0, 0);
        RECT r;
        r.left = 0;
        r.top = RowHeight;
        r.right = Width;
        r.bottom = Height;
        ScrollWindowEx(HWindow, 0, RowHeight * (TopIndex - newTopIndex),
                       &r, &r, hUpdateRgn, NULL, 0);
        TopIndex = newTopIndex;
        SetupScrollBars(UPDATE_VERT_SCROLL);
        Paint(NULL, hUpdateRgn, FALSE);
        DeleteObject(hUpdateRgn);
    }
}

BOOL CRendererWindow::HitTest(int x, int y, int* column, int* row, BOOL getNearest)
{
    int cellX = -1;
    int cellY = -1;

    int colX = 0;
    int visibleIndex = 0;
    int i;
    for (i = 0; i < Database.GetColumnCount(); i++)
    {
        const CDatabaseColumn* col = Database.GetColumn(i);
        if (col->Visible)
        {
            if ((x - RowHeight >= colX - XOffset) &&
                (x - RowHeight < colX - XOffset + col->Width))
            {
                cellX = visibleIndex;
                break;
            }
            colX += col->Width;
            visibleIndex++;
        }
    }
    cellY = TopIndex + (y - RowHeight) / RowHeight;

    if (!getNearest)
    {
        if (cellX == -1 || cellY < 0 || cellY >= Database.GetRowCount())
            return FALSE;
    }

    if (cellX == -1)
    {
        if (x < RowHeight)
            cellX = 0;
        else
            cellX = Database.GetVisibleColumnCount() - 1;
    }

    if (cellY < 0)
        cellY = 0;
    if (cellY >= Database.GetRowCount())
        cellY = Database.GetRowCount() - 1;

    *column = cellX;
    *row = cellY;

    return TRUE;
}

BOOL CRendererWindow::HitTestRow(int y, int* row, BOOL getNearest)
{
    int cellY = -1;
    cellY = TopIndex + (y - RowHeight) / RowHeight;

    if (!getNearest)
    {
        if (cellY < 0 || cellY >= Database.GetRowCount())
            return FALSE;
    }

    if (cellY < 0)
        cellY = 0;
    if (cellY >= Database.GetRowCount())
        cellY = Database.GetRowCount() - 1;

    *row = cellY;

    return TRUE;
}

BOOL CRendererWindow::HitTestColumn(int x, int* column, BOOL getNearest)
{
    int cellX = -1;

    int colX = 0;
    int visibleIndex = 0;
    int i;
    for (i = 0; i < Database.GetVisibleColumnCount(); i++)
    {
        const CDatabaseColumn* col = Database.GetVisibleColumn(i);
        if (col->Visible)
        {
            if ((x - RowHeight >= colX - XOffset) &&
                (x - RowHeight < colX - XOffset + col->Width))
            {
                cellX = visibleIndex;
                break;
            }
            colX += col->Width;
            visibleIndex++;
        }
    }

    if (!getNearest)
    {
        if (cellX == -1)
            return FALSE;
    }

    if (cellX == -1)
    {
        if (x < RowHeight)
            cellX = 0;
        else
            cellX = Database.GetVisibleColumnCount() - 1;
    }

    *column = cellX;

    return TRUE;
}

BOOL CRendererWindow::HitTestColumnSplit(int x, int* column, int* offset)
{
    int colX = RowHeight - XOffset;
    int count = Database.GetVisibleColumnCount();
    int i;
    for (i = 0; i <= count; i++)
    {
        if (i > 0 && x >= colX - 3 && x <= colX)
        {
            // if this is not the left edge of the first column and the point overlaps a divider, we found it
            if (column != NULL)
                *column = i - 1;
            if (offset != NULL)
                *offset = x - colX;
            return TRUE;
        }
        if (i < count) // must not access beyond the array
            colX += Database.GetVisibleColumn(i)->Width;
    }
    return FALSE;
}

/*
void
CRendererWindow::OnEditCell()
{
  int focusX, focusY;
  Selection.GetFocus(&focusX, &focusY);
  int cellX = 0;
  int colIndex = 0;
  if (!GetColumnInfo(focusX, &colIndex, &cellX))
    return;

  const CDbfColumn *col = Database.GetColumn(colIndex);

  if (!Database.FetchRecord(HWindow, focusY))
    return;

  char buff[66000];
  lstrcpyn(buff, Database.GetRecord() + col->PosInRecord, min(col->FieldSize + 1, 66000));

  cellX -= XOffset;
  int cellY = (focusY - TopIndex) * RowHeight;

  cellX += RowHeight;
  cellY += RowHeight;
  
  CWindow *EditWindow = new CWindow(ooAllocated);
  EditWindow->CreateEx(0,
                       "edit",
                       "",
                       WS_CHILD | ES_READONLY,
                       0, 0, 0, 0,
                       HWindow,
                       NULL,
                       DLLInstance,
                       EditWindow);
  SendMessage(EditWindow->HWindow, WM_SETFONT, (WPARAM)HFont, TRUE);
  SetWindowText(EditWindow->HWindow, buff);
  SendMessage(EditWindow->HWindow, EM_SETMARGINS, EC_LEFTMARGIN, 2);
  SendMessage(EditWindow->HWindow, EM_SETMARGINS, EC_LEFTMARGIN, 2);
  SetWindowPos(EditWindow->HWindow, NULL, cellX + 1, cellY + 1,
               col->Width - 3, RowHeight - 3,
               SWP_NOZORDER | SWP_SHOWWINDOW);
  InvalidateRect(EditWindow->HWindow, NULL, TRUE);
  SetFocus(EditWindow->HWindow);
  UpdateWindow(EditWindow->HWindow);
}
*/

void CRendererWindow::BeginSelectionDrag(CDragSelectionMode mode)
{
    if (DragMode != dsmNone)
    {
        TRACE_E("DragMode != dsmNone");
        return;
    }
    DragMode = mode;
    SetCapture(HWindow);
    ScrollTimerID = SetTimer(HWindow, TIMER_SCROLL_ID, 150, NULL);
}

void CRendererWindow::EndSelectionDrag()
{
    DragMode = dsmNone;
    if (GetCapture() == HWindow)
        ReleaseCapture();
    if (ScrollTimerID != 0)
    {
        KillTimer(HWindow, ScrollTimerID);
        ScrollTimerID = 0;
    }
}

void CRendererWindow::EndColumnDrag()
{
    DragColumn = -1;
    if (GetCapture() == HWindow)
        ReleaseCapture();
}

void CRendererWindow::OnTimer(WPARAM wParam)
{
    if (ScrollTimerID != 0 && wParam == ScrollTimerID)
    {
        DWORD msgPos = GetMessagePos();
        POINT p;
        p.x = GET_X_LPARAM(msgPos);
        p.y = GET_Y_LPARAM(msgPos);
        ScreenToClient(HWindow, &p);
        int xDelta = 0;
        int yDelta = 0;

        if (DragMode != dsmRows)
        {
            if (p.x < RowHeight)
                xDelta = -(RowHeight - p.x);

            if (p.x > Width)
                xDelta = (p.x - Width);
        }

        if (DragMode != dsmColumns)
        {
            if (p.y < RowHeight)
                yDelta = -(RowHeight - p.y) * 2;

            if (p.y > Height)
                yDelta = (p.y - Height) * 2;
        }

        if (xDelta != 0)
        {

            int mX;
            mX = xDelta;
            int nPos = XOffset + mX;
            OnHScroll(SB_THUMBPOSITION, nPos);
        }

        if (yDelta != 0)
        {
            int mY;
            mY = yDelta / 10;
            if (mY == 0)
                mY = yDelta < 0 ? -1 : 1;

            int nPos = TopIndex + mY;
            OnVScroll(SB_THUMBPOSITION, nPos);
        }

        if (xDelta != 0 || yDelta != 0)
            PostMessage(HWindow, WM_MOUSEMOVE, MK_LBUTTON | MK_RBUTTON, MAKEWPARAM(p.x, p.y));
    }
}

void CRendererWindow::OnVScroll(int scrollCode, int pos)
{
    int newTopIndex = TopIndex;
    switch (scrollCode)
    {
    case SB_LINEUP:
    {
        newTopIndex--;
        break;
    }

    case SB_LINEDOWN:
    {
        newTopIndex++;
        break;
    }

    case SB_PAGEUP:
    {
        int delta = RowsOnPage - 1;
        if (delta <= 0)
            delta = 1;
        newTopIndex -= delta;
        break;
    }

    case SB_PAGEDOWN:
    {
        int delta = RowsOnPage - 1;
        if (delta <= 0)
            delta = 1;
        newTopIndex += delta;
        break;
    }

    case SB_THUMBPOSITION:
    case SB_THUMBTRACK:
    {
        newTopIndex = pos;
        break;
    }
    }
    if (newTopIndex >= Database.GetRowCount() - RowsOnPage + 1)
        newTopIndex = Database.GetRowCount() - RowsOnPage;
    if (newTopIndex < 0)
        newTopIndex = 0;
    if (newTopIndex != TopIndex)
    {
        HRGN hUpdateRgn = CreateRectRgn(0, 0, 0, 0);
        RECT r;
        r.left = 0;
        r.top = RowHeight;
        r.right = Width;
        r.bottom = Height;
        ScrollWindowEx(HWindow, 0, RowHeight * (TopIndex - newTopIndex),
                       &r, &r, hUpdateRgn, NULL, 0);
        TopIndex = newTopIndex;
        Paint(NULL, hUpdateRgn, FALSE);
        DeleteObject(hUpdateRgn);
    }
    if (scrollCode != SB_THUMBTRACK)
        SetupScrollBars(UPDATE_VERT_SCROLL);
}

void CRendererWindow::OnHScroll(int scrollCode, int pos)
{
    int newXOffset = XOffset;
    switch (scrollCode)
    {
    case SB_LINEUP:
    {
        newXOffset -= RowHeight;
        break;
    }

    case SB_LINEDOWN:
    {
        newXOffset += RowHeight;
        break;
    }

    case SB_PAGEUP:
    {
        newXOffset -= Width - RowHeight;
        break;
    }

    case SB_PAGEDOWN:
    {
        newXOffset += Width - RowHeight;
        break;
    }

    case SB_THUMBPOSITION:
    case SB_THUMBTRACK:
    {
        newXOffset = pos;
        break;
    }
    }
    int columnsWidth = Database.GetVisibleColumnsWidth();
    if (newXOffset >= columnsWidth - (Width - RowHeight) + 1)
        newXOffset = columnsWidth - (Width - RowHeight);
    if (newXOffset < 0)
        newXOffset = 0;
    if (newXOffset != XOffset)
    {
        HRGN hUpdateRgn = CreateRectRgn(0, 0, 0, 0);
        RECT r;
        r.left = RowHeight;
        r.top = 0;
        r.right = Width;
        r.bottom = Height;
        ScrollWindowEx(HWindow, XOffset - newXOffset, 0,
                       &r, &r, hUpdateRgn, NULL, 0);
        XOffset = newXOffset;
        Paint(NULL, hUpdateRgn, FALSE);
        DeleteObject(hUpdateRgn);
    }
    if (scrollCode != SB_THUMBTRACK)
        SetupScrollBars(UPDATE_HORZ_SCROLL);
}

void CRendererWindow::CopySelectionToClipboard()
{
    RECT r;
    const BOOL bUnicode = Database.GetIsUnicode();
    Selection.GetNormalizedSelection(&r);

    try
    {
        std::string encoded;
        std::wstring text;
        for (int row = r.top; row <= r.bottom; ++row)
        {
            if (!Database.FetchRecord(HWindow, row))
                return;
            for (int columnIndex = r.left; columnIndex <= r.right; ++columnIndex)
            {
                const CDatabaseColumn* column = Database.GetVisibleColumn(columnIndex);
                size_t cellLength = 0;
                if (bUnicode)
                {
                    const wchar_t* cell = Database.GetCellTextW(column, &cellLength);
                    if (cell != NULL)
                        text.append(cell, cellLength);
                    if (columnIndex < r.right)
                        text.push_back(L'\t');
                }
                else
                {
                    const char* cell = Database.GetCellText(column, &cellLength);
                    if (cell != NULL)
                    {
                        const size_t begin = encoded.size();
                        encoded.append(cell, cellLength);
                        CodeCharacters(encoded.data() + begin, cellLength);
                    }
                    if (columnIndex < r.right)
                        encoded.push_back('\t');
                }
            }
            if (row < r.bottom)
            {
                if (bUnicode)
                    text.append(L"\r\n");
                else
                    encoded.append("\r\n");
            }
        }

        if (!bUnicode && !sally::dbviewer::DecodeLegacyDisplayText(
                             encoded.data(), encoded.size(), nullptr, text))
        {
            SalGeneral->SalMessageBox(HWindow, SPLLoadStrOwned(SalGeneral, HLanguage, IDS_DBFE_OOM).c_str(),
                                      SPLLoadStrOwned(SalGeneral, HLanguage, IDS_PLUGINNAME).c_str(), MB_OK | MB_ICONEXCLAMATION);
            return;
        }

        if (text.size() > static_cast<size_t>(INT_MAX))
            throw std::length_error("clipboard text exceeds host contract");
        SalGeneral->CopyTextToClipboard(text.c_str(), static_cast<int>(text.size()), FALSE, HWindow);
    }
    catch (const std::bad_alloc&)
    {
        SalGeneral->SalMessageBox(HWindow, SPLLoadStrOwned(SalGeneral, HLanguage, IDS_DBFE_OOM).c_str(),
                                  SPLLoadStrOwned(SalGeneral, HLanguage, IDS_PLUGINNAME).c_str(), MB_OK | MB_ICONEXCLAMATION);
    }
    catch (const std::length_error&)
    {
        SalGeneral->SalMessageBox(HWindow, SPLLoadStrOwned(SalGeneral, HLanguage, IDS_DBFE_OOM).c_str(),
                                  SPLLoadStrOwned(SalGeneral, HLanguage, IDS_PLUGINNAME).c_str(), MB_OK | MB_ICONEXCLAMATION);
    }
} /* CRendererWindow::CopySelectionToClipboard */

void CRendererWindow::SelectAll()
{
    OldSelection = Selection;
    Selection.SetFocus(0, 0);
    Viewer->UpdateRowNumberOnToolBar(0, Database.GetRowCount());
    Selection.SetAnchor(max(0, Database.GetVisibleColumnCount() - 1),
                        max(0, Database.GetRowCount() - 1));
    Paint(NULL, NULL, TRUE); // redraw the changes
}

void CRendererWindow::CheckAndCorrectBoundaries()
{
    if (!Database.IsOpened())
        return;
    if (Width - RowHeight > 0 && Height - RowHeight > 0)
    {
        // ensure scrolling is adjusted when the window grows, we are at the right or bottom edge, and further scrolling is possible
        int rowWidth = Database.GetVisibleColumnsWidth();
        int newXOffset = XOffset;
        if (newXOffset > 0 && rowWidth - newXOffset < (Width - RowHeight) + 1)
            newXOffset = rowWidth - (Width - RowHeight);
        if (rowWidth < (Width - RowHeight) + 1)
            newXOffset = 0;

        int rowCount = Database.GetRowCount();
        int newTopIndex = TopIndex;
        if (newTopIndex > 0 && rowCount - newTopIndex < RowsOnPage + 1)
            newTopIndex = rowCount - RowsOnPage;
        if (rowCount < RowsOnPage + 1)
            newTopIndex = 0;

        if (newXOffset != XOffset || newTopIndex != TopIndex)
        {
            XOffset = newXOffset;
            TopIndex = newTopIndex;
            Paint(NULL, NULL, FALSE);
        }
    }
}

void CRendererWindow::ColumnsWasChanged()
{
    // keep the selection within bounds
    int clipX = max(0, Database.GetVisibleColumnCount() - 1);
    Selection.Clip(clipX);
    OldSelection.Clip(clipX);
    Bookmarks.ClearAll();
    InvalidateRect(HWindow, NULL, TRUE);
    SetupScrollBars(UPDATE_HORZ_SCROLL);
    CheckAndCorrectBoundaries();
}

void CRendererWindow::GetContextMenuPos(POINT* p)
{
    int x, y;
    Selection.GetFocus(&x, &y);

    int colX = RowHeight - XOffset;
    int i;
    for (i = 0; i <= min(x, Database.GetVisibleColumnCount() - 1); i++)
        colX += Database.GetVisibleColumn(i)->Width;

    p->x = colX - 1;
    p->y = RowHeight + (y - TopIndex) * RowHeight + RowHeight - 1;
    if (p->x < RowHeight)
        p->x = RowHeight;
    if (p->y < RowHeight)
        p->y = RowHeight;
    if (p->x > Width)
        p->x = Width;
    if (p->y > Height)
        p->y = Height;
    ClientToScreen(HWindow, p);
}

void CRendererWindow::OnContextMenu(const POINT* p)
{
    CGUIMenuPopupAbstract* popup = SalamanderGUI->CreateMenuPopup();
    if (popup != NULL)
    {
        popup->LoadFromTemplate(HLanguage, PopupMenuTemplate, Viewer->Enablers, Viewer->HGrayToolBarImageList, Viewer->HHotToolBarImageList);
        popup->SetStyle(MENU_POPUP_UPDATESTATES);
        popup->Track(MENU_TRACK_RIGHTBUTTON, p->x, p->y, Viewer->HWindow, NULL);
        SalamanderGUI->DestroyMenuPopup(popup);
    }
}

void CRendererWindow::ResetMouseWheelAccumulatorHandler(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
    case WM_SYSKEYDOWN:
    case WM_KEYDOWN:
    {
        // if SHIFT is held, auto-repeat occurs, but we care only about the first press
        BOOL firstPress = (lParam & 0x40000000) == 0;
        if (!firstPress)
            break;
    }
    case WM_SETFOCUS:
    case WM_KILLFOCUS:
    case WM_LBUTTONDOWN:
    case WM_RBUTTONDOWN:
    case WM_NCLBUTTONDOWN:
    case WM_NCRBUTTONDOWN:
    case WM_SYSKEYUP:
    case WM_KEYUP:
    {
        ResetMouseWheelAccumulator();
        break;
    }
    }
}

#ifndef WM_MOUSEHWHEEL
#define WM_MOUSEHWHEEL 0x020E
#endif // WM_MOUSEHWHEEL

LRESULT
CRendererWindow::WindowProc(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    ResetMouseWheelAccumulatorHandler(uMsg, wParam, lParam);

    if (uMsg == WM_MOUSEWHEEL)
    {
        short zDelta = (short)HIWORD(wParam);
        if ((zDelta < 0 && MouseWheelAccumulator > 0) || (zDelta > 0 && MouseWheelAccumulator < 0))
            ResetMouseWheelAccumulator(); // when the wheel direction changes, the accumulator must be reset

        BOOL controlPressed = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
        BOOL altPressed = (GetKeyState(VK_MENU) & 0x8000) != 0;
        BOOL shiftPressed = (GetKeyState(VK_SHIFT) & 0x8000) != 0;

        // standard scrolling without modifier keys
        if (!controlPressed && !altPressed && !shiftPressed)
        {
            SCROLLINFO si;
            si.cbSize = sizeof(si);
            si.fMask = SIF_POS | SIF_RANGE | SIF_PAGE;
            GetScrollInfo(HWindow, SB_VERT, &si);

            DWORD wheelScroll = SalGeneral->GetMouseWheelScrollLines(); // can be up to WHEEL_PAGESCROLL(0xffffffff)
            wheelScroll = max(1, min(wheelScroll, si.nPage - 1));       // clamp to at most the page length

            MouseWheelAccumulator += 1000 * zDelta;
            int stepsPerLine = max(1, (1000 * WHEEL_DELTA) / wheelScroll);
            int linesToScroll = MouseWheelAccumulator / stepsPerLine;
            if (linesToScroll != 0)
            {
                MouseWheelAccumulator -= linesToScroll * stepsPerLine;
                OnVScroll(SB_THUMBPOSITION, si.nPos - linesToScroll);
            }
        }

        // SHIFT: horizontal scrolling
        if (!controlPressed && !altPressed && shiftPressed)
        {
            SCROLLINFO si;
            si.cbSize = sizeof(si);
            si.fMask = SIF_POS | SIF_RANGE | SIF_PAGE;
            GetScrollInfo(HWindow, SB_HORZ, &si);

            DWORD wheelScroll = RowHeight * SalGeneral->GetMouseWheelScrollLines(); // 'delta' can be up to WHEEL_PAGESCROLL(0xffffffff)
            wheelScroll = max(1, min(wheelScroll, si.nPage));                       // clamp to at most the page width

            MouseWheelAccumulator += 1000 * zDelta;
            int stepsPerLine = max(1, (1000 * WHEEL_DELTA) / wheelScroll);
            int linesToScroll = MouseWheelAccumulator / stepsPerLine;
            if (linesToScroll != 0)
            {
                MouseWheelAccumulator -= linesToScroll * stepsPerLine;
                OnHScroll(SB_THUMBPOSITION, si.nPos - linesToScroll);
            }
        }

        return 0;
    }

    if (uMsg == WM_MOUSEHWHEEL) // horizontall scroll, supported from Windows Vista
    {
        short zDelta = (short)HIWORD(wParam);
        if ((zDelta < 0 && MouseHWheelAccumulator > 0) || (zDelta > 0 && MouseHWheelAccumulator < 0))
            ResetMouseWheelAccumulator(); // when the wheel tilting direction changes, the accumulator must be reset

        SCROLLINFO si;
        si.cbSize = sizeof(si);
        si.fMask = SIF_POS | SIF_RANGE | SIF_PAGE;
        GetScrollInfo(HWindow, SB_HORZ, &si);

        DWORD wheelScroll = RowHeight * SalGeneral->GetMouseWheelScrollChars();
        wheelScroll = max(1, min(wheelScroll, si.nPage - 1)); // clamp to at most the page length

        MouseHWheelAccumulator += 1000 * zDelta;
        int stepsPerChar = max(1, (1000 * WHEEL_DELTA) / wheelScroll);
        int charsToScroll = MouseHWheelAccumulator / stepsPerChar;
        if (charsToScroll != 0)
        {
            MouseHWheelAccumulator -= charsToScroll * stepsPerChar;
            OnHScroll(SB_THUMBPOSITION, si.nPos + charsToScroll);
        }
        return TRUE; // event handled; skip emulating scrollbar clicks (would happen when returning FALSE)
    }

    switch (uMsg)
    {
    case WM_CREATE:
    {
        DragAcceptFiles(HWindow, TRUE);
        break;
    }

    case WM_DESTROY:
    {
        DragAcceptFiles(HWindow, FALSE);
        break;
    }

    case WM_DROPFILES:
    {
        const HDROP drop = (HDROP)wParam;
        const UINT drag = DragQueryFileW(drop, 0xFFFFFFFF, NULL, 0); // how many files were dropped
        if (drag > 0)
        {
            const UINT length = DragQueryFileW(drop, 0, NULL, 0);
            std::vector<wchar_t> path(static_cast<size_t>(length) + 1, L'\0');
            if (DragQueryFileW(drop, 0, path.data(), static_cast<UINT>(path.size())) != 0)
                OpenFile(path.data(), TRUE);
        }
        DragFinish(drop);
        break;
    }

    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hDC = BeginPaint(HWindow, &ps);
        if (hDC != NULL)
            Paint(hDC, NULL, FALSE);
        EndPaint(HWindow, &ps);
        return 0;
    }

    case WM_SIZE:
    {
        RECT r;
        GetClientRect(HWindow, &r);
        Width = r.right;
        Height = r.bottom;

        RowsOnPage = (Height - RowHeight) / RowHeight; // subtract the header line
        if (RowsOnPage < 1)
            RowsOnPage = 1;

        SetupScrollBars();
        CheckAndCorrectBoundaries();

        break;
    }

    case WM_ERASEBKGND:
    {
        if (Creating || Database.IsOpened())
            return 1;
        else
            break;
    }

    case WM_VSCROLL:
    {
        SCROLLINFO si;
        si.cbSize = sizeof(SCROLLINFO);
        si.fMask = SIF_TRACKPOS;
        GetScrollInfo(HWindow, SB_VERT, &si);
        int pos = si.nTrackPos;
        int scrollCode = (int)LOWORD(wParam);
        OnVScroll(scrollCode, pos);
        break;
    }

    case WM_HSCROLL:
    {
        SCROLLINFO si;
        si.cbSize = sizeof(SCROLLINFO);
        si.fMask = SIF_TRACKPOS;
        GetScrollInfo(HWindow, SB_HORZ, &si);
        int pos = si.nTrackPos;
        int scrollCode = (int)LOWORD(wParam);
        OnHScroll(scrollCode, pos);
        break;
    }

    case WM_SYSKEYDOWN:
    case WM_KEYDOWN:
    {
        if (DragMode != dsmNone || DragColumn != -1)
            break;

        if (wParam == VK_ESCAPE)
        {
            PostMessage(GetParent(HWindow), uMsg, wParam, lParam);
            break;
        }

        if (!Database.IsOpened())
            break;

        if (Database.GetVisibleColumnCount() == 0 || Database.GetRowCount() == 0)
            break;

        BOOL shiftPressed = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
        BOOL controlPressed = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
        BOOL resetSelection = FALSE;

        if ((wParam == VK_F10 && shiftPressed || wParam == VK_APPS))
        {
            POINT p;
            GetContextMenuPos(&p);
            OnContextMenu(&p);
            break;
        }

        int x, y;
        if (shiftPressed)
            Selection.GetAnchor(&x, &y);
        else
            Selection.GetFocus(&x, &y);
        int oldX = x;
        int oldY = y;
        int topIndex = TopIndex;
        switch (wParam)
        {
        case VK_RIGHT:
        {
            if (x < Database.GetVisibleColumnCount() - 1)
                x++;
            if (!shiftPressed)
                resetSelection = TRUE;
            break;
        }

        case VK_LEFT:
        {
            if (x > 0)
                x--;
            if (!shiftPressed)
                resetSelection = TRUE;
            break;
        }

        case VK_UP:
        {
            if (y > 0)
                y--;
            if (!shiftPressed)
                resetSelection = TRUE;
            break;
        }

        case VK_DOWN:
        {
            if (y < Database.GetRowCount() - 1)
                y++;
            if (!shiftPressed)
                resetSelection = TRUE;
            break;
        }

        case VK_PRIOR:
        {
            if (controlPressed)
                y = 0;
            else
            {
                topIndex -= RowsOnPage;
                if (topIndex < 0)
                    topIndex = 0;
                /*          if (shiftPressed)
          {*/
                y -= RowsOnPage;
                if (y < 0)
                    y = 0;
                /*          }
          else
          {
            if (y >= RowsOnPage)
              y -= RowsOnPage;
          }*/
            }
            if (!shiftPressed)
                resetSelection = TRUE;
            break;
        }

        case VK_NEXT:
        {
            if (controlPressed)
                y = Database.GetRowCount() - 1;
            else
            {
                topIndex += RowsOnPage;
                if (topIndex > Database.GetRowCount() - RowsOnPage)
                    topIndex = Database.GetRowCount() - RowsOnPage;
                if (topIndex < 0)
                    topIndex = 0;
                /*          if (shiftPressed)
          {*/
                y += RowsOnPage;
                if (y >= Database.GetRowCount())
                    //            if (y > Database.GetRowCount() - RowsOnPage)
                    y = Database.GetRowCount() - 1;
                if (y < 0)
                    y = 0;
                /*          }
          else
          {
            if (y < Database.GetRowCount() - RowsOnPage)
              y += RowsOnPage;
          }*/
            }
            if (!shiftPressed)
                resetSelection = TRUE;
            break;
        }

        case VK_HOME:
        {
            if (controlPressed)
                y = 0;
            else
                x = 0;
            break;
        }

        case VK_END:
        {
            if (controlPressed)
                y = Database.GetRowCount() - 1;
            else
                x = Database.GetVisibleColumnCount() - 1;
            break;
        }
        }
        if (x != oldX || y != oldY || topIndex != TopIndex ||
            (resetSelection && !Selection.FocusIsAnchor()))
        {
            OldSelection = Selection;
            if (shiftPressed)
                Selection.SetAnchor(x, y);
            else
            {
                Selection.SetFocusAndAnchor(x, y);
                Viewer->UpdateRowNumberOnToolBar(y, Database.GetRowCount());
            }
            BOOL selOnly = topIndex == TopIndex;
            TopIndex = topIndex;
            Paint(NULL, NULL, selOnly); // redraw the changes
            if (y < Database.GetRowCount())
                EnsureRowIsVisible(y);
            EnsureColumnIsVisible(x);
            SetupScrollBars(UPDATE_VERT_SCROLL | UPDATE_HORZ_SCROLL);
        }
        break;
    }

    case WM_LBUTTONDOWN:
    {
        if (!Database.IsOpened())
            break;

        int xPos = GET_X_LPARAM(lParam);
        int yPos = GET_Y_LPARAM(lParam);

        if (xPos > RowHeight && yPos < RowHeight)
        {
            // is this the start of column-width dragging?
            int colIndex;
            int offset;
            if (HitTestColumnSplit(xPos, &colIndex, &offset))
            {
                DragColumn = colIndex;
                DragColumnOffset = offset;
                SetCapture(HWindow);
                break;
            }
        }

        if (xPos < RowHeight && yPos < RowHeight)
        {
            // select all
            SelectAll();
            break;
        }

        if (xPos < RowHeight)
        {
            // selection across rows
            int y;
            if (HitTestRow(yPos, &y, FALSE))
            {
                OldSelection = Selection;
                Selection.SetFocus(0, y);
                Viewer->UpdateRowNumberOnToolBar(y, Database.GetRowCount());
                Selection.SetAnchor(Database.GetVisibleColumnCount() - 1, y);
                Paint(NULL, NULL, TRUE); // redraw the changes
                BeginSelectionDrag(dsmRows);
            }
            break;
        }

        if (yPos < RowHeight)
        {
            // selection across columns
            int x;
            if (HitTestColumn(xPos, &x, FALSE))
            {
                OldSelection = Selection;
                Selection.SetFocus(x, 0);
                Viewer->UpdateRowNumberOnToolBar(0, Database.GetRowCount());
                Selection.SetAnchor(x, Database.GetRowCount() - 1);
                Paint(NULL, NULL, TRUE); // redraw the changes
                BeginSelectionDrag(dsmColumns);
            }
            break;
        }

        if (xPos >= RowHeight && yPos >= RowHeight)
        {
            // select pres bunky
            int x, y;
            if (HitTest(xPos, yPos, &x, &y, FALSE))
            {
                OldSelection = Selection;
                Selection.SetFocusAndAnchor(x, y);
                Viewer->UpdateRowNumberOnToolBar(y, Database.GetRowCount());
                Paint(NULL, NULL, TRUE); // redraw the changes
                BeginSelectionDrag(dsmNormal);
            }
            break;
        }
        break;
    }

    case WM_RBUTTONDOWN:
    {
        if (!Database.IsOpened())
            break;

        int xPos = GET_X_LPARAM(lParam);
        int yPos = GET_Y_LPARAM(lParam);

        if (xPos >= RowHeight && yPos >= RowHeight)
        {
            // select pres bunky
            int x, y;
            if (HitTest(xPos, yPos, &x, &y, FALSE))
            {
                if (!Selection.Contains(x, y))
                {
                    OldSelection = Selection;
                    Selection.SetFocusAndAnchor(x, y);
                    Viewer->UpdateRowNumberOnToolBar(y, Database.GetRowCount());
                    Paint(NULL, NULL, TRUE); // redraw the changes
                }
                POINT p;
                GetCursorPos(&p);
                OnContextMenu(&p);
            }
            break;
        }
        break;
    }

    case WM_LBUTTONUP:
    {
        if (DragColumn != -1)
        {
            CheckAndCorrectBoundaries();
            SetupScrollBars(UPDATE_HORZ_SCROLL);
            EndColumnDrag();
        }
        if (DragMode != dsmNone)
            EndSelectionDrag();
        break;
    }

    case WM_CAPTURECHANGED:
    {
        if (DragColumn != -1)
        {
            CheckAndCorrectBoundaries();
            SetupScrollBars(UPDATE_HORZ_SCROLL);
            EndColumnDrag();
        }
        if (DragMode != dsmNone)
            EndSelectionDrag();
        break;
    }

    case WM_MOUSEMOVE:
    {
        if (!Database.IsOpened())
            break;
        int xPos = GET_X_LPARAM(lParam);
        int yPos = GET_Y_LPARAM(lParam);
        if (DragColumn != -1)
        {
            const CDatabaseColumn* column = Database.GetVisibleColumn(DragColumn);
            int x = Database.GetVisibleColumnX(DragColumn);
            if (column != NULL && x != -1)
            {
                int newWidth = xPos + XOffset - RowHeight - x - DragColumnOffset;
                if (newWidth < 10)
                    newWidth = 10;
                if (newWidth != column->Width)
                {
                    CDatabaseColumn col = *column;
                    col.Width = newWidth;
                    Database.SetVisibleColumn(DragColumn, &col);
                    // moved to the end of dragging to avoid rapid
                    // column-size changes when shrinking with right-edge scrolling
                    //            CheckAndCorrectBoundaries();
                    //            SetupScrollBars(UPDATE_HORZ_SCROLL);
                    Paint(NULL, NULL, FALSE); // causes a slight flicker
                }
            }
        }
        if (DragMode == dsmNormal)
        {
            int x, y;
            if (HitTest(xPos, yPos, &x, &y, TRUE))
            {
                OldSelection = Selection;
                Selection.SetAnchor(x, y);
                Paint(NULL, NULL, TRUE); // redraw the changes
            }
        }
        if (DragMode == dsmColumns)
        {
            int x;
            if (HitTestColumn(xPos, &x, TRUE))
            {
                OldSelection = Selection;
                Selection.SetAnchor(x, Database.GetRowCount() - 1);
                Paint(NULL, NULL, TRUE); // redraw the changes
            }
        }
        if (DragMode == dsmRows)
        {
            int y;
            if (HitTestRow(yPos, &y, TRUE))
            {
                OldSelection = Selection;
                Selection.SetAnchor(Database.GetVisibleColumnCount() - 1, y);
                Paint(NULL, NULL, TRUE); // redraw the changes
            }
        }

        break;
    }

    case WM_TIMER:
    {
        OnTimer(wParam);
        return 0;
    }

    case WM_SETCURSOR:
    {
        if (!Database.IsOpened())
            break;
        if (LOWORD(lParam) == HTCLIENT)
        {
            POINT p;
            GetCursorPos(&p);
            ScreenToClient(HWindow, &p);

            if (p.x - RowHeight >= Database.GetVisibleColumnsWidth() - XOffset ||
                p.y - RowHeight >= RowHeight * (Database.GetRowCount() - TopIndex))
            {
                // if the cursor is outside the valid data rectangle, show the arrow
                SetCursor(LoadCursor(NULL, IDC_ARROW));
            }
            else
            {
                // if it is within the data, check whether it is above a column divider in the header row
                BOOL hitSplit = FALSE;
                if (p.y < RowHeight && p.x > RowHeight)
                {
                    if (HitTestColumnSplit(p.x, NULL, NULL))
                        hitSplit = TRUE;
                }

                if (hitSplit)
                    SetCursor(LoadCursor(DLLInstance, MAKEINTRESOURCE(IDC_SPLIT)));
                else
                    SetCursor(LoadCursor(DLLInstance, MAKEINTRESOURCE(IDC_SELECT)));
            }
            return TRUE;
        }
        break;
    }
    }
    return CWindow::WindowProc(uMsg, wParam, lParam);
}
