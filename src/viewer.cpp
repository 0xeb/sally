// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"
#include "common/unicode/LegacyByteCellMap.h"

#include <cstdint>

#include "ui/IPrompter.h"
#include "viewer.h"

#include "cfgdlg.h"
#include "mainwnd.h"
#include "codetbl.h"
#include "usermenu.h"
#include "execute.h"
#include "gui.h"
#include "darkmode.h"

const wchar_t* CVIEWERWINDOW_CLASSNAME = L"Salamander's Viewer Window";

wchar_t* ViewerHistory[VIEWER_HISTORY_SIZE];

HACCEL ViewerTable = NULL;
BOOL UseCustomViewerFont = FALSE;
LOGFONT ViewerLogFont;
HMENU ViewerMenu = NULL;
int CharWidth = 1,  // character width (in points); we divide by this value, so we will never set it to zero
    CharHeight = 1; // character height (in points); we divide by this value, so we will never set it to zero

CRITICAL_SECTION ViewerFontMeasureCS;
BOOL ViewerFontMeasured = FALSE;
wchar_t ViewerFontMapping[256];

void GetDefaultViewerLogFont(LOGFONT* lf)
{
    const int VIEWER_FONT_PTS = 10;
    memset(lf, 0, sizeof(*lf));
    lf->lfHeight = -(VIEWER_FONT_PTS * SystemDPI) / 72;
    lf->lfWeight = FW_NORMAL;
    lf->lfCharSet = UserCharset;
    lf->lfOutPrecision = OUT_DEFAULT_PRECIS;
    lf->lfClipPrecision = CLIP_DEFAULT_PRECIS;
    lf->lfQuality = DEFAULT_QUALITY;
    lf->lfPitchAndFamily = FIXED_PITCH | FF_DONTCARE;
    wcscpy_s(lf->lfFaceName, L"Consolas");
}

//
//*****************************************************************************

void HistoryComboBox(HWND hWindow, CTransferInfo& ti, int ctrlID, wchar_t* Text,
                     int textLen, BOOL hexMode, int historySize, wchar_t* history[],
                     BOOL changeOnlyHistory)
{
    CALL_STACK_MESSAGE6("HistoryComboBox(, , %d, , %d, %d, %d, , %d)",
                        ctrlID, textLen, hexMode, historySize, changeOnlyHistory);
    HWND hwnd;
    if (changeOnlyHistory || ti.GetControl(hwnd, ctrlID))
    {
        if (!changeOnlyHistory && ti.Type == ttDataToWindow)
        {
            SendMessage(hwnd, CB_RESETCONTENT, 0, 0);
            SendMessage(hwnd, CB_LIMITTEXT, textLen - 1, 0);
            SendMessage(hwnd, WM_SETTEXT, 0, (LPARAM)Text);
        }
        else
        {
            if (!changeOnlyHistory)
            {
                SendMessage(hwnd, WM_GETTEXT, textLen, (LPARAM)Text);
                SendMessage(hwnd, CB_RESETCONTENT, 0, 0);
                SendMessage(hwnd, CB_LIMITTEXT, textLen - 1, 0);
                SendMessage(hwnd, WM_SETTEXT, 0, (LPARAM)Text);
            }

            // hex mode handling
            if (hexMode)
            {
                wchar_t* s = Text;
                BOOL openedQuotes = FALSE;
                wchar_t* lastQuotes = NULL;
                while (*s != 0 && (openedQuotes || *s == ' ' || *s >= '0' && *s <= '9' ||
                                   LowerCase[*s] >= 'a' && LowerCase[*s] <= 'f' ||
                                   *s == '"'))
                {
                    if (*s == '"')
                    {
                        openedQuotes = !openedQuotes;
                        lastQuotes = s;
                    }
                    s++;
                }
                if (openedQuotes)
                    s = lastQuotes;
                if (*s != 0) // contains a non-hex character
                {
                    if (!changeOnlyHistory)
                    {
                        gPrompter->ShowError(LoadStrW(IDS_ERRORTITLE), LoadStrW(IDS_STRINGISNOTHEX));
                        SetFocus(hwnd);
                        SendMessage(hwnd, CB_SETEDITSEL, 0, MAKELPARAM(s - Text, 1 + (s - Text)));
                    }
                    ti.ErrorOn(ctrlID);
                }
            }
            // everything is fine; store it in the history
            if (ti.IsGood())
            {
                if (Text[0] != 0)
                {
                    BOOL insert = TRUE;
                    int i;
                    for (i = 0; i < historySize; i++)
                    {
                        if (history[i] != NULL)
                        {
                            if (wcscmp(history[i], Text) == 0) // already in the history
                            {                                  // move it to position 0
                                if (i > 0)
                                {
                                    wchar_t* swap = history[i];
                                    memmove(history + 1, history, i * sizeof(wchar_t*));
                                    history[0] = swap;
                                }
                                insert = FALSE;
                                break;
                            }
                        }
                        else
                            break;
                    }

                    if (insert)
                    {
                        wchar_t* newText = _wcsdup(Text);
                        if (newText != NULL)
                        {
                            if (history[historySize - 1] != NULL)
                                free(history[historySize - 1]);
                            memmove(history + 1, history,
                                    (historySize - 1) * sizeof(wchar_t*));
                            history[0] = newText;
                        }
                        else
                            TRACE_E(LOW_MEMORY);
                    }
                }
            }
        }

        if (!changeOnlyHistory)
        {
            int i;
            for (i = 0; i < historySize; i++) // fill the combo-box list
                if (history[i] != NULL)
                    SendMessage(hwnd, CB_ADDSTRING, 0, (LPARAM)history[i]);
                else
                    break;
        }
    }
}

void HistoryComboBox(HWND hWindow, CTransferInfo& ti, int ctrlID, std::wstring& text,
                     BOOL hexMode, int historySize, wchar_t* history[],
                     BOOL changeOnlyHistory)
{
    CALL_STACK_MESSAGE5("HistoryComboBox(, , %d, <dynamic>, %d, %d, , %d)",
                        ctrlID, hexMode, historySize, changeOnlyHistory);
    HWND hwnd = NULL;
    if (changeOnlyHistory || ti.GetControl(hwnd, ctrlID))
    {
        if (!changeOnlyHistory && ti.Type == ttDataToWindow)
        {
            SendMessageW(hwnd, CB_RESETCONTENT, 0, 0);
            SendMessageW(hwnd, WM_SETTEXT, 0, (LPARAM)text.c_str());
        }
        else
        {
            if (!changeOnlyHistory)
            {
                text = GetWindowTextStringW(hwnd);
                SendMessageW(hwnd, CB_RESETCONTENT, 0, 0);
                SendMessageW(hwnd, WM_SETTEXT, 0, (LPARAM)text.c_str());
            }

            if (hexMode)
            {
                size_t pos = 0;
                BOOL openedQuotes = FALSE;
                size_t lastQuotes = std::wstring::npos;
                while (pos < text.length() &&
                       (openedQuotes || text[pos] == L' ' ||
                        text[pos] >= L'0' && text[pos] <= L'9' ||
                        text[pos] >= L'a' && text[pos] <= L'f' ||
                        text[pos] >= L'A' && text[pos] <= L'F' ||
                        text[pos] == L'"'))
                {
                    if (text[pos] == L'"')
                    {
                        openedQuotes = !openedQuotes;
                        lastQuotes = pos;
                    }
                    pos++;
                }
                if (openedQuotes)
                    pos = lastQuotes;
                if (pos < text.length())
                {
                    if (!changeOnlyHistory)
                    {
                        gPrompter->ShowError(LoadStrW(IDS_ERRORTITLE), LoadStrW(IDS_STRINGISNOTHEX));
                        SetFocus(hwnd);
                        SendMessageW(hwnd, CB_SETEDITSEL, 0, MAKELPARAM(pos, pos + 1));
                    }
                    ti.ErrorOn(ctrlID);
                }
            }

            if (ti.IsGood() && !text.empty())
            {
                BOOL insert = TRUE;
                int i;
                for (i = 0; i < historySize; i++)
                {
                    if (history[i] == NULL)
                        break;
                    if (wcscmp(history[i], text.c_str()) == 0)
                    {
                        if (i > 0)
                        {
                            wchar_t* swap = history[i];
                            memmove(history + 1, history, i * sizeof(wchar_t*));
                            history[0] = swap;
                        }
                        insert = FALSE;
                        break;
                    }
                }
                if (insert)
                {
                    wchar_t* newText = _wcsdup(text.c_str());
                    if (newText != NULL)
                    {
                        if (history[historySize - 1] != NULL)
                            free(history[historySize - 1]);
                        memmove(history + 1, history,
                                (historySize - 1) * sizeof(wchar_t*));
                        history[0] = newText;
                    }
                    else
                        TRACE_E(LOW_MEMORY);
                }
            }
        }

        if (!changeOnlyHistory)
        {
            for (int i = 0; i < historySize && history[i] != NULL; i++)
                SendMessageW(hwnd, CB_ADDSTRING, 0, (LPARAM)history[i]);
        }
    }
}

//
//*****************************************************************************

void DoHexValidation(HWND edit)
{
    CALL_STACK_MESSAGE1("DoHexValidation()");
    int start, end;
    SendMessage(edit, CB_GETEDITSEL, (WPARAM)&start, (LPARAM)&end);
    std::wstring text = GetWindowTextStringW(edit);
    Sally::Unicode::NormalizeHexPatternInput(text, start, end);
    SendMessageW(edit, WM_SETTEXT, 0, (LPARAM)text.c_str());
    SendMessage(edit, CB_SETEDITSEL, 0, MAKELPARAM(start, end));
}

//
//*****************************************************************************

void CFindSetDialog::Transfer(CTransferInfo& ti)
{
    ti.CheckBox(IDC_FINDHEX, HexMode);
    ti.CheckBox(IDC_VIEWREGEXP, Regular);
    HistoryComboBox(HWindow, ti, IDC_FINDTEXT, Text, !Regular && HexMode,
                    VIEWER_HISTORY_SIZE, ViewerHistory);
    if (ti.Type == ttDataToWindow)
    { // initialize the search text based on the selection in the viewer (the parent of this dialog)
        CWindowsObject* win = WindowsManager.GetWindowPtr(Parent);
        if (win != NULL && win->Is(otViewerWindow)) // just to be sure, check that it is a viewer window
        {
            CViewerWindow* view = (CViewerWindow*)win;
            if (HexMode)
            {
                std::string bytes;
                if (view->GetFindBytes(bytes))
                {
                    std::wstring hexText;
                    if (Sally::Unicode::FormatHexPattern(
                            reinterpret_cast<const std::uint8_t*>(bytes.data()),
                            bytes.size(), hexText))
                        SetDlgItemTextW(HWindow, IDC_FINDTEXT, hexText.c_str());
                }
            }
            else
            {
                std::wstring seedW;
                if (!view->GetFindTextW(seedW) || seedW.empty())
                {
                    std::string bytes;
                    if (!view->GetFindBytes(bytes) ||
                        !sally::legacy_search::DecodeAcp(
                            bytes.data(), bytes.size(), seedW))
                        seedW.clear();
                }
                if (!seedW.empty())
                    SetDlgItemTextW(HWindow, IDC_FINDTEXT, seedW.c_str());
            }
        }
    }
    ti.RadioButton(IDC_SBACKWARD, 0, Forward);
    ti.RadioButton(IDC_SFORWARD, 1, Forward);
    ti.CheckBox(IDC_WHOLEWORDS, WholeWords);
    ti.CheckBox(IDC_CASESENSITIVE, CaseSensitive);
}

INT_PTR
CFindSetDialog::DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    CALL_STACK_MESSAGE4("CFindSetDialog::DialogProc(0x%X, 0x%IX, 0x%IX)", uMsg, wParam, lParam);
    switch (uMsg)
    {
    case WM_INITDIALOG:
    {
        CancelHexMode = HexMode;
        CancelRegular = Regular;
        EnableWindow(GetDlgItem(HWindow, IDC_FINDHEX), !Regular);
        if (Regular)
            CheckDlgButton(HWindow, IDC_FINDHEX, BST_UNCHECKED);
        ChangeToArrowButton(HWindow, IDC_REGEXP_BROWSE);

        CComboboxEdit* edit = new CComboboxEdit();
        if (edit != NULL)
        {
            HWND hCombo = GetDlgItem(HWindow, IDC_FINDTEXT);
            edit->AttachToWindow(GetWindow(hCombo, GW_CHILD));
        }

        break;
    }

    case WM_USER_CLEARHISTORY:
    {
        // we should clear the histories
        ClearComboboxListbox(GetDlgItem(HWindow, IDC_FINDTEXT));
        return 0;
    }

    case WM_COMMAND:
    {
        switch (LOWORD(wParam))
        {
        case IDCANCEL:
        {
            HexMode = CancelHexMode; // keep Cancel correct
            Regular = CancelRegular;
            break;
        }

        case IDC_REGEXP_BROWSE:
        {
            const CExecuteItem* item = TrackExecuteMenu(HWindow, IDC_REGEXP_BROWSE, IDC_FINDTEXT,
                                                        TRUE, RegularExpressionItems);
            if (item != NULL)
            {
                BOOL regular = (IsDlgButtonChecked(HWindow, IDC_VIEWREGEXP) == BST_CHECKED);
                if (item->Keyword == EXECUTE_HELP)
                {
                    // open the help page dedicated to regular expressions
                    OpenHtmlHelp(NULL, HWindow, HHCDisplayContext, IDH_REGEXP, FALSE);
                }
                if (item->Keyword != EXECUTE_HELP && !regular)
                {
                    // the user selected an expression, so tick the checkbox for regular search
                    CheckDlgButton(HWindow, IDC_VIEWREGEXP, BST_CHECKED);
                    PostMessage(HWindow, WM_COMMAND, MAKELPARAM(IDC_VIEWREGEXP, BN_CLICKED), 0);
                }
            }
            return 0;
        }

        case IDC_VIEWREGEXP:
        {
            Regular = (IsDlgButtonChecked(HWindow, IDC_VIEWREGEXP) != BST_UNCHECKED);
            EnableWindow(GetDlgItem(HWindow, IDC_FINDHEX), !Regular);
            if (Regular)
                CheckDlgButton(HWindow, IDC_FINDHEX, BST_UNCHECKED);
            break;
        }

        case IDC_FINDHEX:
        {
            if (HIWORD(wParam) == BN_CLICKED)
            {
                HexMode = (IsDlgButtonChecked(HWindow, IDC_FINDHEX) != BST_UNCHECKED);
                if (HexMode)
                    CheckDlgButton(HWindow, IDC_CASESENSITIVE, BST_CHECKED);
                return TRUE;
            }
            break;
        }

        case IDC_FINDTEXT:
        {
            if (!Regular && HexMode && HIWORD(wParam) == CBN_EDITUPDATE)
            {
                DoHexValidation((HWND)lParam);
                return TRUE;
            }
            break;
        }
        }
        break;
    }
    }
    return CCommonDialog::DialogProc(uMsg, wParam, lParam);
}

//
//*****************************************************************************
// CViewerGoToOffsetDialog
//

void CViewerGoToOffsetDialog::Validate(CTransferInfo& ti)
{
    int h;
    ti.CheckBox(IDC_VGTO_HEX, h);
    __int64 dummy;
    ti.EditLine(IDE_VGTO_OFFSET, dummy, TRUE, TRUE, h);
}

void CViewerGoToOffsetDialog::Transfer(CTransferInfo& ti)
{
    ti.CheckBox(IDC_VGTO_HEX, Configuration.GoToOffsetIsHex);
    ti.EditLine(IDE_VGTO_OFFSET, *Offset, TRUE, TRUE, Configuration.GoToOffsetIsHex);
}

INT_PTR
CViewerGoToOffsetDialog::DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    CALL_STACK_MESSAGE4("CViewerGoToOffsetDialog::DialogProc(0x%X, 0x%IX, 0x%IX)", uMsg, wParam, lParam);
    switch (uMsg)
    {
    case WM_COMMAND:
    {
        if (LOWORD(wParam) == IDC_VGTO_HEX && HIWORD(wParam) == BN_CLICKED)
        { // switching HEX = change the offset from decimal to hex and back
            BOOL h = IsDlgButtonChecked(HWindow, IDC_VGTO_HEX) != BST_UNCHECKED;
            CTransferInfo ti(HWindow, ttDataFromWindow);
            __int64 off;
            ti.EditLine(IDE_VGTO_OFFSET, off, TRUE, TRUE, !h, FALSE, TRUE); // do not show an error; just skip conversion
            if (ti.IsGood())
            {
                CTransferInfo ti2(HWindow, ttDataToWindow);
                ti2.EditLine(IDE_VGTO_OFFSET, off, FALSE, TRUE, h);
            }
        }
        break;
    }
    }
    return CCommonDialog::DialogProc(uMsg, wParam, lParam);
}

//
//*****************************************************************************
// CViewerWindow
//

CViewerWindow::CViewerWindow(const wchar_t* fileName, CViewType type, const wchar_t* caption,
                             BOOL wholeCaption, CObjectOrigin origin,
                             int enumFileNamesSourceUID, int enumFileNamesLastFileIndex)
    : CWindow(origin), LineOffset(300, 100),
      FindDialog(HLanguage, IDD_FINDSET, IDD_FINDSET)
{
    // GDI variables
    BkgndBrush = NULL;
    BkgndBrushSel = NULL;
    ViewerFont = NULL;

    Width = Height = 0;

    // dummy bitmap -- the correct size will be set in WM_SIZE
    if (!Bitmap.CreateBmp(NULL, 1, 1))
        TRACE_E("Unable to create bitmap or memory DC for viewer.");

    CreateViewerBrushs();
    SetViewerFont(); // uses Bitmap (must already be allocated to at least 1x1) and Width (must already be initialized to at least 0)

    // other variables
    HexOffsetLength = 0;
    CanSwitchToHex = TRUE;
    CanSwitchQuietlyToHex = FALSE;
    FindingSoDonotSwitchToHex = FALSE;
    WaitForViewerRefresh = FALSE;
    LastSeekY = 0;
    LastOriginX = 0;
    RepeatCmdAfterRefresh = -1;
    ExitTextMode = FALSE;
    ForceTextMode = FALSE;
    CodeType = 0;
    CodeTables.Init(MainWindow->HWindow);
    UseCodeTable = FALSE;
    TextEncoding = Sally::Unicode::BomEncoding::LegacyBytes;
    TextContentOffset = 0;
    ResetDecodedLineIndex();
    if (fileName == NULL)
        ClearViewedFile(); // error
    else
    {
        std::wstring name = fileName;
        if (SalGetFullNameW(name))
        {
            FileNameW = name;
        }
        else
            ClearViewedFile();
    }
    Buffer = (unsigned char*)malloc(VIEW_BUFFER_SIZE);
    Seek = 0;
    Loaded = 0;
    DefViewMode = Configuration.DefViewMode;
    Type = type;
    OriginX = SeekY = 0;
    MaxSeekY = -1;
    ViewSize = FileSize = 0;
    LastLineSize = FirstLineSize = 0;
    EnablePaint = TRUE;
    StartSelection = -1; // no selection yet
    EndSelection = -1;   // no selection yet
    TooBigSelAction = 0;
    EndSelectionRow = -1;
    EndSelectionPrefX = -1;
    WrapIsBeforeFirstLine = FALSE;
    MouseDrag = FALSE;
    ChangingSelWithShiftKey = FALSE;
    FindOffset = 0;
    ResetFindOffsetOnNextPaint = TRUE;
    SelectionIsFindResult = FALSE;
    ScrollScaleX = ScrollScaleY = 0;
    EnableSetScroll = TRUE;
    ScrollToSelection = FALSE;
    ToolTipOffset = -1;
    HToolTip = NULL;
    Lock = NULL;
    WrapText = Configuration.WrapText;
    CodePageAutoSelect = Configuration.CodePageAutoSelect;
    DefaultConvert = Configuration.DefaultConvert;
    LastFindSeekY = -1;
    LastFindOffset = -1;

    if (caption != NULL)
    {
        Caption = caption;
        WholeCaption = wholeCaption;
    }
    else
    {
        Caption.clear();
        WholeCaption = FALSE;
    }
    EnumFileNamesSourceUID = enumFileNamesSourceUID;
    EnumFileNamesLastFileIndex = enumFileNamesLastFileIndex;
    VScrollWParam = -1;

    ResetMouseWheelAccumulator();
}

CViewerWindow::~CViewerWindow()
{
    if (ViewerFont != NULL)
        HANDLES(DeleteObject(ViewerFont));
    ReleaseViewerBrushs();
    if (Lock != NULL)
    {
        SetEvent(Lock);
        Lock = NULL; // now it is up to the disk cache
    }
    if (Buffer != NULL)
        free(Buffer);
}

HANDLE
CViewerWindow::GetLockObject()
{
    if (Lock == NULL)
        Lock = HANDLES(CreateEvent(NULL, FALSE, FALSE, NULL));
    return Lock;
}

void CViewerWindow::CloseLockObject()
{
    if (Lock != NULL)
    {
        HANDLES(CloseHandle(Lock));
        Lock = NULL;
    }
}

void CViewerWindow::FindNewSeekY(__int64 newSeekY, BOOL& fatalErr)
{
    CALL_STACK_MESSAGE2("CViewerWindow::FindNewSeekY(%g,)", (double)newSeekY);
    fatalErr = FALSE;
    if (newSeekY >= MaxSeekY)
        SeekY = MaxSeekY;
    else
    {
        if (newSeekY == 0)
            SeekY = 0;
        else
        {
            newSeekY = FindBegin(newSeekY, fatalErr);
            if (!fatalErr && !ExitTextMode)
                SeekY = newSeekY;
        }
    }
}

int TranslateU2T(int u, BOOL left, int hexOffsetLength)
{
    int i = u - (62 - 8 + hexOffsetLength);
    return left ? (10 - 8 + hexOffsetLength + i * 3 + (i / 4)) : (9 - 8 + hexOffsetLength + i * 3 + ((i - 1) / 4));
}

int GetHexOffsetMode(unsigned __int64 fileSize, int& hexOffsetLength)
{
    if (fileSize == 0)
    {
        hexOffsetLength = 4;
        return 1; // at least 4 characters
    }
    --fileSize;                                              // the largest possible offset in the file is one less than the file size
    if (fileSize <= CQuadWord(0x0000FFFF, 0x00000000).Value) // 4 characters are enough
    {
        hexOffsetLength = 4;
        return 1;
    }
    else
    {
        if (fileSize <= CQuadWord(0xFFFFFFFF, 0x00000000).Value) // 8 characters are enough
        {
            hexOffsetLength = 9;
            return 2;
        }
        else
        {
            if (fileSize <= CQuadWord(0xFFFFFFFF, 0x0000FFFF).Value) // 12 characters are enough
            {
                hexOffsetLength = 14;
                return 3;
            }
            else // 16 characters are necessary
            {
                hexOffsetLength = 19;
                return 4;
            }
        }
    }
}

#define LOWORD64(qw) ((WORD)((qw) & 0xffff))

void PrintHexOffset(char* s, unsigned __int64 offset, int mode)
{
    switch (mode)
    {
    case 1:
        sprintf(s, "%04X", LOWORD64(offset));
        return; // 4 characters are enough
    case 2:
        sprintf(s, "%04X %04X", LOWORD64(offset >> 16), LOWORD64(offset));
        return; // 8 characters are enough
    case 3:
        sprintf(s, "%04X %04X %04X", LOWORD64(offset >> 32), LOWORD64(offset >> 16), LOWORD64(offset));
        return; // 12 characters are enough
    case 4:
        sprintf(s, "%04X %04X %04X %04X", LOWORD64(offset >> 48), LOWORD64(offset >> 32),
                LOWORD64(offset >> 16), LOWORD64(offset));
        return; // 16 characters are necessary
    }
    TRACE_E("Unexpected situation in PrintHexOffset().");
}

void DrawLegacyByteCells(HDC hdc, int nXStart, int nYStart,
                         const char* bytes, int byteCount) noexcept
{
#ifdef _DEBUG
    if (!ViewerFontMeasured)
        TRACE_E("DrawLegacyByteCells(): ViewerFontMeasured is FALSE!");
#endif // _DEBUG
    if (bytes == nullptr || byteCount <= 0)
        return;
    std::wstring text;
    if (sally::unicode::TryMapLegacyByteCells(
            bytes, static_cast<size_t>(byteCount), ViewerFontMapping, text))
        TextOutW(hdc, nXStart, nYStart, text.c_str(), byteCount);
    else
        TRACE_E("DrawLegacyByteCells(): insufficient memory for display text");
}

void DrawUnicodeCells(HDC hdc, int nXStart, int nYStart,
                      const wchar_t* text, int characterCount)
{
#ifdef _DEBUG
    if (!ViewerFontMeasured)
        TRACE_E("DrawUnicodeCells(): ViewerFontMeasured is FALSE!");
#endif // _DEBUG
    TextOutW(hdc, nXStart, nYStart, text, characterCount);
}

namespace
{

bool IsViewerDecodedEOL(std::uint32_t scalar)
{
    return scalar == L'\r' || scalar == L'\n' || scalar == 0;
}

void AppendVisualCell(Sally::Unicode::DecodedRun& visual, std::uint32_t scalar,
                      __int64 rawStart, __int64 rawEnd, int tabSize)
{
    if (scalar == L'\t')
    {
        int tab = (int)(tabSize - (visual.CellCount() % tabSize));
        if (tab <= 0)
            tab = 1;
        while (tab-- > 0)
            visual.AppendCell(L' ', rawStart, rawEnd);
    }
    else
        visual.AppendCell(scalar, rawStart, rawEnd);
}

// Same expansion as AppendVisualCell, but the run is optional: when 'visual' is NULL the cells
// are only counted. Tab width depends solely on the running cell count, so the counting and
// materialising modes cannot disagree - which is the property the line index relies on.
void AppendOrCountVisualCell(Sally::Unicode::DecodedRun* visual, __int64& cellCount,
                             std::uint32_t scalar, __int64 rawStart, __int64 rawEnd, int tabSize)
{
    if (scalar == L'\t')
    {
        int tab = (int)(tabSize - (cellCount % tabSize));
        if (tab <= 0)
            tab = 1;
        cellCount += tab;
        if (visual != NULL)
        {
            while (tab-- > 0)
                visual->AppendCell(L' ', rawStart, rawEnd);
        }
    }
    else
    {
        cellCount++;
        if (visual != NULL)
            visual->AppendCell(scalar, rawStart, rawEnd);
    }
}

std::size_t DecodedSelectionStartCell(const Sally::Unicode::DecodedRun& visual, __int64 offset)
{
    for (std::size_t i = 0; i < visual.CellCount(); ++i)
    {
        if (offset <= visual.RawStart[i])
            return i;
        if (offset < visual.RawEnd[i])
            return i;
    }
    return visual.CellCount();
}

std::size_t DecodedSelectionEndCell(const Sally::Unicode::DecodedRun& visual, __int64 offset)
{
    for (std::size_t i = 0; i < visual.CellCount(); ++i)
    {
        if (offset <= visual.RawStart[i])
            return i;
        if (offset <= visual.RawEnd[i])
            return i + 1;
    }
    return visual.CellCount();
}

void DrawDecodedCells(HDC dc, const Sally::Unicode::DecodedRun& visual, std::size_t cellStart,
                      std::size_t cellEnd, int xCell)
{
    if (cellStart >= cellEnd)
        return;
    std::size_t textStart = visual.TextIndexForCellEnd(cellStart);
    std::size_t textEnd = visual.TextIndexForCellEnd(cellEnd);
    if (textEnd > textStart)
        DrawUnicodeCells(dc, xCell * CharWidth, 0, visual.Text.c_str() + textStart, (int)(textEnd - textStart));
}

} // namespace

BOOL CViewerWindow::DecodeTextRange(HANDLE* hFile, __int64 start, __int64 end,
                                    Sally::Unicode::DecodedRun& run, BOOL& fatalErr, bool flush)
{
    run.Clear();
    fatalErr = FALSE;
    if (!HasDecodedTextEncoding())
        return FALSE;

    start = max(start, TextContentOffset);
    end = min(end, FileSize);
    start = Sally::Unicode::AlignToCodeUnit(TextEncoding, start, TextContentOffset);
    if (end <= start)
        return TRUE;

    __int64 off = start;
    while (off < end)
    {
        __int64 want = min((__int64)APROX_LINE_LEN + 8, end - off);
        __int64 len = Prepare(hFile, off, want, fatalErr);
        if (fatalErr)
            return FALSE;
        if (len <= 0)
            break;

        bool finalChunk = flush && off + len >= end;
        Sally::Unicode::DecodedRun part = Sally::Unicode::DecodeBytes(TextEncoding, Buffer + (off - Seek), (std::size_t)len, off, finalChunk);
        if (part.RawBytesConsumed == 0 && part.CellCount() == 0)
        {
            if (finalChunk)
                break;
            len = min((__int64)APROX_LINE_LEN + 16, FileSize - off);
            len = Prepare(hFile, off, len, fatalErr);
            if (fatalErr || len <= 0)
                return !fatalErr;
            part = Sally::Unicode::DecodeBytes(TextEncoding, Buffer + (off - Seek), (std::size_t)len, off, off + len >= FileSize);
            if (part.RawBytesConsumed == 0)
                break;
        }
        run.AppendRun(part);
        off += (__int64)part.RawBytesConsumed;
    }
    return TRUE;
}

BOOL CViewerWindow::ReadDecodedScalar(HANDLE* hFile, __int64 offset, Sally::Unicode::DecodedRun& scalar, BOOL& fatalErr)
{
    scalar.Clear();
    fatalErr = FALSE;
    if (!HasDecodedTextEncoding())
        return FALSE;
    offset = max(offset, TextContentOffset);
    offset = Sally::Unicode::AlignToCodeUnit(TextEncoding, offset, TextContentOffset);
    if (offset >= FileSize)
        return TRUE;

    __int64 len = Prepare(hFile, offset, min((__int64)8, FileSize - offset), fatalErr);
    if (fatalErr || len <= 0)
        return !fatalErr;
    scalar = Sally::Unicode::DecodeBytes(TextEncoding, Buffer + (offset - Seek), (std::size_t)len, offset, TRUE);
    return TRUE;
}

BOOL CViewerWindow::DecodeOrScanTextLine(HANDLE* hFile, __int64 lineOffset, __int64 maxCells,
                                         Sally::Unicode::DecodedRun* visualLine, __int64& cellCount,
                                         __int64& lineEnd, __int64& nextLineBegin, BOOL& eol,
                                         BOOL& wrapped, int& eolBytes, BOOL& fatalErr)
{
    if (visualLine != NULL)
        visualLine->Clear();
    cellCount = 0;
    fatalErr = FALSE;
    eol = FALSE;
    wrapped = FALSE;
    eolBytes = 0;

    if (!HasDecodedTextEncoding())
        return FALSE;

    __int64 off = max(lineOffset, TextContentOffset);
    off = Sally::Unicode::AlignToCodeUnit(TextEncoding, off, TextContentOffset);
    lineEnd = off;
    nextLineBegin = off;
    if (off >= FileSize)
        return TRUE;

    while (off < FileSize)
    {
        __int64 readEnd = min(FileSize, off + APROX_LINE_LEN + 8);
        Sally::Unicode::DecodedRun decoded;
        if (!DecodeTextRange(hFile, off, readEnd, decoded, fatalErr, readEnd >= FileSize))
            return FALSE;
        if (fatalErr)
            return FALSE;
        if (decoded.CellCount() == 0)
        {
            lineEnd = nextLineBegin = off;
            return TRUE;
        }

        for (std::size_t i = 0; i < decoded.CellCount(); ++i)
        {
            std::uint32_t scalar = decoded.Scalars[i];
            if (IsViewerDecodedEOL(scalar))
            {
                if (scalar == L'\r')
                {
                    if (Configuration.EOL_CRLF)
                    {
                        Sally::Unicode::DecodedRun nextScalar;
                        bool haveNext = false;
                        if (i + 1 < decoded.CellCount())
                        {
                            nextScalar.AppendCell(decoded.Scalars[i + 1], decoded.RawStart[i + 1], decoded.RawEnd[i + 1]);
                            haveNext = true;
                        }
                        else if (ReadDecodedScalar(hFile, decoded.RawEnd[i], nextScalar, fatalErr) && !fatalErr &&
                                 nextScalar.CellCount() > 0)
                            haveNext = true;
                        if (fatalErr)
                            return FALSE;
                        if (haveNext && nextScalar.Scalars[0] == L'\n')
                        {
                            lineEnd = decoded.RawStart[i];
                            nextLineBegin = nextScalar.RawEnd[0];
                            eol = TRUE;
                            eolBytes = (int)(nextLineBegin - lineEnd);
                            return TRUE;
                        }
                    }
                    if (Configuration.EOL_CR)
                    {
                        lineEnd = decoded.RawStart[i];
                        nextLineBegin = decoded.RawEnd[i];
                        eol = TRUE;
                        eolBytes = (int)(nextLineBegin - lineEnd);
                        return TRUE;
                    }
                }
                else if (scalar == L'\n')
                {
                    if (Configuration.EOL_LF)
                    {
                        lineEnd = decoded.RawStart[i];
                        nextLineBegin = decoded.RawEnd[i];
                        eol = TRUE;
                        eolBytes = (int)(nextLineBegin - lineEnd);
                        return TRUE;
                    }
                }
                else if (Configuration.EOL_NULL)
                {
                    lineEnd = decoded.RawStart[i];
                    nextLineBegin = decoded.RawEnd[i];
                    eol = TRUE;
                    eolBytes = (int)(nextLineBegin - lineEnd);
                    return TRUE;
                }
            }

            AppendOrCountVisualCell(visualLine, cellCount, scalar, decoded.RawStart[i],
                                    decoded.RawEnd[i], Configuration.TabSize);
            lineEnd = decoded.RawEnd[i];
            nextLineBegin = lineEnd;
            if (WrapText && maxCells > 0 && cellCount >= maxCells)
            {
                wrapped = TRUE;
                return TRUE;
            }
        }
        off += (__int64)decoded.RawBytesConsumed;
        if (decoded.RawBytesConsumed == 0)
            break;
    }
    lineEnd = nextLineBegin = max(lineEnd, off);
    return TRUE;
}

BOOL CViewerWindow::ReadDecodedTextLine(HANDLE* hFile, __int64 lineOffset, __int64 maxCells,
                                        Sally::Unicode::DecodedRun& visualLine, __int64& lineEnd,
                                        __int64& nextLineBegin, BOOL& eol, BOOL& wrapped,
                                        int& eolBytes, BOOL& fatalErr)
{
    __int64 cellCount = 0;
    return DecodeOrScanTextLine(hFile, lineOffset, maxCells, &visualLine, cellCount, lineEnd,
                                nextLineBegin, eol, wrapped, eolBytes, fatalErr);
}

BOOL CViewerWindow::ScanDecodedTextLine(HANDLE* hFile, __int64 lineOffset, __int64 maxCells,
                                        __int64& cellCount, __int64& lineEnd,
                                        __int64& nextLineBegin, BOOL& eol, BOOL& wrapped,
                                        int& eolBytes, BOOL& fatalErr)
{
    return DecodeOrScanTextLine(hFile, lineOffset, maxCells, NULL, cellCount, lineEnd,
                                nextLineBegin, eol, wrapped, eolBytes, fatalErr);
}

void CViewerWindow::PaintDecodedText(HDC dc, const RECT& fullLine, int lines, int columns,
                                     int clipFirstRow, int clipLastRow, BOOL& fatalErr,
                                     BOOL& setFindOffset)
{
    __int64 xRollLimit = (Width - BORDER_WIDTH) / CharWidth / 6;
    FirstLineSize = LastLineSize = 0;
    WrapIsBeforeFirstLine = FALSE;

    RECT r;
    RECT endRect = fullLine;
    __int64 lineOffset = max(SeekY, TextContentOffset);
    BOOL previousEOL = FALSE;
    for (int i = 0; i < lines; i++)
    {
        Sally::Unicode::DecodedRun visual;
        __int64 lineEnd = lineOffset;
        __int64 nextLineBegin = lineOffset;
        BOOL EOL = FALSE;
        BOOL lineEndIsWrapped = FALSE;
        int lineEOLSize = 0;

        if (lineOffset >= FileSize)
        {
            int redrI = i;
            if (redrI < clipFirstRow)
                redrI = clipFirstRow;
            r.left = BORDER_WIDTH;
            r.top = CharHeight * redrI;
            r.bottom = CharHeight * clipLastRow;
            if (r.bottom > Height)
                r.bottom = Height;
            if (r.top <= r.bottom)
                FillRect(dc, &r, BkgndBrush);

            if (previousEOL)
            {
                LineOffset.Add(lineOffset);
                LineOffset.Add(lineOffset);
                LineOffset.Add(0);
            }
            break;
        }

        __int64 maxCells = WrapText ? max(1, columns) : TEXT_MAX_LINE_LEN + 1;
        if (!ReadDecodedTextLine(NULL, lineOffset, maxCells, visual, lineEnd, nextLineBegin,
                                 EOL, lineEndIsWrapped, lineEOLSize, fatalErr))
            break;
        if (fatalErr)
            break;

        __int64 fullLineLen = max((__int64)0, nextLineBegin - lineOffset);
        __int64 lineLen = (__int64)visual.CellCount();
        LineOffset.Add(lineOffset);
        LineOffset.Add(lineEnd);
        LineOffset.Add(lineLen);

        __int64 startSel = min(StartSelection, EndSelection);
        if (startSel == -1)
            startSel = 0;
        __int64 endSel = max(StartSelection, EndSelection);
        if (endSel == -1)
            endSel = 0;
        if (startSel == endSel)
            startSel = endSel = 0;

        std::size_t selStartCell = DecodedSelectionStartCell(visual, startSel);
        std::size_t selEndCell = DecodedSelectionEndCell(visual, endSel);

        if (ScrollToSelection)
        {
            int len2 = (Width - BORDER_WIDTH) / CharWidth;
            if (len2 - 2 * xRollLimit < (__int64)selEndCell - (__int64)selStartCell)
            {
                xRollLimit = (len2 - ((__int64)selEndCell - (__int64)selStartCell)) / 2;
                if (xRollLimit < 0)
                    xRollLimit = 0;
            }
            __int64 left = OriginX;
            __int64 right = OriginX + len2;
            if ((__int64)selStartCell < lineLen)
            {
                __int64 originX = OriginX;
                if ((__int64)selStartCell < left)
                {
                    originX = (__int64)selStartCell - xRollLimit;
                    if (originX < 0)
                        originX = 0;
                }
                else if ((__int64)selStartCell >= right ||
                         ((__int64)selEndCell < lineLen && (__int64)selEndCell >= right))
                {
                    originX = (__int64)selStartCell - xRollLimit;
                    if (originX < 0)
                        originX = 0;
                    __int64 originX2 = (__int64)selEndCell - len2 + 1 + xRollLimit;
                    if (originX2 < 0)
                        originX2 = 0;
                    originX = min(originX, originX2);
                }
                if (originX != OriginX)
                {
                    setFindOffset = FALSE;
                    OriginX = originX;
                    InvalidateRect(HWindow, NULL, FALSE);
                    break;
                }
                else
                    ScrollToSelection = FALSE;
            }
        }

        if (i == 0)
            FirstLineSize = fullLineLen;
        if (i + 1 < lines)
        {
            ViewSize += fullLineLen;
            if (i + 2 == lines)
                LastLineSize = fullLineLen;
        }

        BOOL blackEnd = (lineEndIsWrapped ? startSel < lineEnd : startSel <= lineEnd) && endSel > lineEnd;
        if (OriginX < lineLen)
        {
            __int64 len2 = min((Width - BORDER_WIDTH) / CharWidth + 1, lineLen - OriginX);
            std::size_t left = (std::size_t)OriginX;
            std::size_t right = (std::size_t)(OriginX + len2);
            std::size_t u1 = len2, u2 = 0, u3 = 0;
            if (selStartCell <= left)
            {
                if (selEndCell > left)
                {
                    u1 = 0;
                    u2 = min(right, selEndCell) - left;
                    u3 = (std::size_t)len2 - u2;
                }
            }
            else if (selStartCell < right)
            {
                if (selEndCell > selStartCell)
                {
                    u1 = selStartCell - left;
                    u2 = min(right, selEndCell) - left - u1;
                    u3 = (std::size_t)len2 - u2 - u1;
                }
            }

            if (i >= clipFirstRow && i <= clipLastRow)
            {
                RECT myLine = fullLine;
                // CJK/complex-script fallback glyphs can draw wider than the viewer's
                // average CharWidth; clip decoded rows to the real viewport, not to
                // logical cell count.
                myLine.right = fullLine.right;

                if (blackEnd)
                {
                    endRect.left = 0;
                    endRect.right = (int)((u1 + u2 + u3) * CharWidth);
                    FillRect(Bitmap.HMemDC, &endRect, BkgndBrush);
                    endRect.left = endRect.right;
                    endRect.right = Width - BORDER_WIDTH;
                    FillRect(Bitmap.HMemDC, &endRect, BkgndBrushSel);
                }
                else
                    FillRect(Bitmap.HMemDC, &myLine, BkgndBrush);

                if (u3 > 0)
                    DrawDecodedCells(Bitmap.HMemDC, visual, left + u1 + u2, left + u1 + u2 + u3, (int)(u1 + u2));
                if (u2 > 0)
                {
                    SetBkColor(Bitmap.HMemDC, GetCOLORREF(ViewerColors[VIEWER_BK_SELECTED]));
                    SetTextColor(Bitmap.HMemDC, GetCOLORREF(ViewerColors[VIEWER_FG_SELECTED]));
                    SetBkMode(Bitmap.HMemDC, OPAQUE);
                    DrawDecodedCells(Bitmap.HMemDC, visual, left + u1, left + u1 + u2, (int)u1);
                    SetBkMode(Bitmap.HMemDC, TRANSPARENT);
                    SetTextColor(Bitmap.HMemDC, GetCOLORREF(ViewerColors[VIEWER_FG_NORMAL]));
                    SetBkColor(Bitmap.HMemDC, GetCOLORREF(ViewerColors[VIEWER_BK_NORMAL]));
                }
                if (u1 > 0)
                    DrawDecodedCells(Bitmap.HMemDC, visual, left, left + u1, 0);

                BitBlt(dc, BORDER_WIDTH, CharHeight * i, myLine.right,
                       CharHeight, Bitmap.HMemDC, 0, 0, SRCCOPY);

                if (myLine.right < fullLine.right)
                {
                    myLine.top = CharHeight * i;
                    myLine.bottom = myLine.top + CharHeight;
                    myLine.left = BORDER_WIDTH + myLine.right;
                    myLine.right = BORDER_WIDTH + fullLine.right;
                    FillRect(dc, &myLine, blackEnd ? BkgndBrushSel : BkgndBrush);
                }
            }
        }
        else if (i >= clipFirstRow && i <= clipLastRow)
        {
            r.left = BORDER_WIDTH;
            r.top = CharHeight * i;
            r.right = Width;
            r.bottom = r.top + CharHeight;
            FillRect(dc, &r, blackEnd ? BkgndBrushSel : BkgndBrush);
        }

        previousEOL = EOL;
        if (nextLineBegin <= lineOffset)
            break;
        lineOffset = nextLineBegin;
    }
}

void CViewerWindow::Paint(HDC dc)
{
    CALL_STACK_MESSAGE1("CViewerWindow::Paint()");
    RECT clientRect;
    GetClientRect(HWindow, &clientRect);
    FillRect(dc, &clientRect, BkgndBrush);

    if (EnablePaint && !ExitTextMode && !FileNameW.empty() && Width > 0 && Height > 0)
    {
        //    HCURSOR oldCursor = GetCursor();
        //    SetCursor(LoadCursor(NULL, IDC_WAIT));
        const int columns = (Width - BORDER_WIDTH) / CharWidth;
        const size_t visibleLineCapacity = static_cast<size_t>(max(columns, 0)) + 1;
        const size_t hexLineCapacity = static_cast<size_t>(max(HexOffsetLength, 0)) + 2 + 16 * 4 + 16 + 1;
        std::vector<char> lineStorage;
        try
        {
            lineStorage.resize(max(visibleLineCapacity, hexLineCapacity), '\0');
        }
        catch (...)
        {
            TRACE_E("CViewerWindow::Paint(): insufficient memory for the visible byte line");
            return;
        }
        //---
        HFONT oldFont = (HFONT)SelectObject(dc, ViewerFont);
        SetTextColor(dc, GetCOLORREF(ViewerColors[VIEWER_FG_NORMAL]));
        SetBkColor(dc, GetCOLORREF(ViewerColors[VIEWER_BK_NORMAL]));
        //---
        HFONT oldFont2 = (HFONT)SelectObject(Bitmap.HMemDC, ViewerFont);
        SetTextColor(Bitmap.HMemDC, GetCOLORREF(ViewerColors[VIEWER_FG_NORMAL]));
        SetBkColor(Bitmap.HMemDC, GetCOLORREF(ViewerColors[VIEWER_BK_NORMAL]));
        //---
        int oldMode = SetBkMode(Bitmap.HMemDC, TRANSPARENT);

        EnablePaint = FALSE;
        LineOffset.DestroyMembers();
        RECT r;
        r.left = 0;
        r.right = BORDER_WIDTH;
        r.top = 0;
        r.bottom = Height;
        FillRect(dc, &r, BkgndBrush); // clear the columns to the left of the text
        RECT fullLine;
        fullLine.left = 0;
        fullLine.top = 0;
        fullLine.right = Width - BORDER_WIDTH;
        fullLine.bottom = CharHeight /*Height*/; // j.r. W2K slowness patch

        r.right = Width;
        ViewSize = 0;
        int lines = Height / CharHeight + 1;
        char* line = lineStorage.data(); // explicit byte owner for legacy/hex parsing only
        char* s;
        BOOL fatalErr = FALSE;
            // determine which rows need to be repainted
            RECT clipRect;
            int clipRet = GetClipBox(dc, &clipRect);
            int clipFirstRow = 0;
            int clipLastRow = lines;
            if (clipRet == SIMPLEREGION || clipRet == COMPLEXREGION)
            {
                clipFirstRow = clipRect.top / CharHeight;
                clipLastRow = clipRect.bottom / CharHeight + 1;
            }

            BOOL setFindOffset = ResetFindOffsetOnNextPaint;
            switch (Type)
            {
            case vtHex:
            {
                FirstLineSize = LastLineSize = 16;
                int hexOffsetMode = GetHexOffsetMode(FileSize, HexOffsetLength);

                __int64 startSel = min(StartSelection, EndSelection);
                if (startSel == -1)
                    startSel = 0;
                __int64 endSel = max(StartSelection, EndSelection);
                if (endSel == -1)
                    endSel = 0;
                if (startSel == endSel)
                    startSel = endSel = 0;
                __int64 lineOffset = SeekY;
                int i;
                for (i = 0; i < lines; i++)
                {
                    __int64 len = Prepare(NULL, lineOffset, 16, fatalErr);
                    // if (fatalErr) FatalFileErrorOccured(); // see below
                    if (fatalErr)
                        break;
                    if (len == 0 && i + 1 != lines && SeekY != 0)
                    {
                        __int64 size = FileSize;
                        FileChanged(NULL, TRUE, fatalErr, FALSE);
                        // if (fatalErr) FatalFileErrorOccured(); // see below
                        if (fatalErr || ExitTextMode)
                            break;
                        if (size != FileSize)
                        {
                            setFindOffset = FALSE; // leave it for the next drawing pass
                            ViewSize = 0;
                            FindNewSeekY(SeekY, fatalErr);
                            if (fatalErr || ExitTextMode)
                                break;
                            FirstLineSize = LastLineSize = 0;
                            // when viewing a growing text file whose last line ended with a line break,
                            // scrolling down at the end of the file caused incorrect repainting;
                            // we expect the same in HEX view, so the following invalidate ensures a full repaint
                            InvalidateRect(HWindow, NULL, FALSE);
                            break;
                        }
                    }
                    if (i + 1 != lines)
                        ViewSize += len; // count only fully visible lines

                    s = line;
                    if (len != 0)
                    {
                        PrintHexOffset(s, lineOffset, hexOffsetMode); // line offset
                        s += HexOffsetLength;
                        *s++ = ':';
                        *s++ = ' ';

                        int j;
                        for (j = 0; j < 16; j++)
                        {
                            if (j < len)
                                if ((j % 4) == 3)
                                    s += sprintf(s, "%02X  ", (unsigned int)Buffer[lineOffset - Seek + j]);
                                else
                                    s += sprintf(s, "%02X ", (unsigned int)Buffer[lineOffset - Seek + j]);
                            else if ((j % 4) == 3)
                                s += sprintf(s, "    ");
                            else
                                s += sprintf(s, "   ");
                        }
                        memmove(s, Buffer + (lineOffset - Seek), (int)len);
                        s += len;
                    }

                    int lineLen = (int)(s - line); // length of the line to print
                    if (OriginX < lineLen)
                    {
                        int u1, u2;
                        if (startSel < lineOffset + len && endSel > lineOffset)
                        {
                            u1 = (int)(lineLen - len);
                            if (startSel > lineOffset)
                                u1 += (int)(startSel - lineOffset);
                            if (endSel >= lineOffset + len)
                                u2 = lineLen;
                            else
                                u2 = (int)(lineLen - (lineOffset + len - endSel));
                        }
                        else
                        {
                            u1 = lineLen;
                            u2 = lineLen;
                        }
                        int t1, t2;
                        t1 = TranslateU2T(u1, TRUE, HexOffsetLength);
                        t2 = TranslateU2T(u2, FALSE, HexOffsetLength);
                        if (t2 < t1)
                            t2 = t1;

                        if (i >= clipFirstRow && i <= clipLastRow)
                        {
                            RECT myLine = fullLine; // myLine is the text area that we will draw with the slower BitBlt path
                            myLine.right = min(myLine.right, (lineLen + 1) * CharWidth);
                            FillRect(Bitmap.HMemDC, &myLine, BkgndBrush);
                            if (lineLen > OriginX)
                            {
                                char* text = line + OriginX; // shift the text buffer according to OriginX
                                u1 -= (int)OriginX;
                                if (u1 < 0)
                                    u1 = 0;
                                u2 -= (int)OriginX;
                                if (u2 < 0)
                                    u2 = 0;
                                t1 -= (int)OriginX;
                                if (t1 < 0)
                                    t1 = 0;
                                t2 -= (int)OriginX;
                                if (t2 < 0)
                                    t2 = 0;
                                // render text backwards because of italics
                                // u2, lineLen - OriginX norm
                                if (u2 < lineLen - OriginX)
                                {
                                    DrawLegacyByteCells(Bitmap.HMemDC, u2 * CharWidth, 0, text + u2,
                                                        (int)(lineLen - OriginX - u2));
                                }
                                // u1, u2 sel
                                if (u1 < u2)
                                {
                                    SetBkColor(Bitmap.HMemDC, GetCOLORREF(ViewerColors[VIEWER_BK_SELECTED]));
                                    SetTextColor(Bitmap.HMemDC, GetCOLORREF(ViewerColors[VIEWER_FG_SELECTED]));
                                    SetBkMode(Bitmap.HMemDC, OPAQUE);
                                    DrawLegacyByteCells(Bitmap.HMemDC, u1 * CharWidth, 0, text + u1, u2 - u1);
                                    SetBkMode(Bitmap.HMemDC, TRANSPARENT);
                                    SetTextColor(Bitmap.HMemDC, GetCOLORREF(ViewerColors[VIEWER_FG_NORMAL]));
                                    SetBkColor(Bitmap.HMemDC, GetCOLORREF(ViewerColors[VIEWER_BK_NORMAL]));
                                }
                                // t2, u1 norm
                                if (t2 < u1)
                                {
                                    DrawLegacyByteCells(Bitmap.HMemDC, t2 * CharWidth, 0, text + t2, u1 - t2);
                                }
                                // t1, t2 select
                                if (t1 < t2)
                                {
                                    SetBkColor(Bitmap.HMemDC, GetCOLORREF(ViewerColors[VIEWER_BK_SELECTED]));
                                    SetTextColor(Bitmap.HMemDC, GetCOLORREF(ViewerColors[VIEWER_FG_SELECTED]));
                                    SetBkMode(Bitmap.HMemDC, OPAQUE);
                                    DrawLegacyByteCells(Bitmap.HMemDC, t1 * CharWidth, 0, text + t1, t2 - t1);
                                    SetBkMode(Bitmap.HMemDC, TRANSPARENT);
                                    SetTextColor(Bitmap.HMemDC, GetCOLORREF(ViewerColors[VIEWER_FG_NORMAL]));
                                    SetBkColor(Bitmap.HMemDC, GetCOLORREF(ViewerColors[VIEWER_BK_NORMAL]));
                                }
                                // 0, t1 norm
                                if (t1 > 0)
                                    DrawLegacyByteCells(Bitmap.HMemDC, 0, 0, text, t1);
                            }

                            // bitblt the entire row to the screen
                            BitBlt(dc, BORDER_WIDTH, CharHeight * i, (lineLen + 1) * CharWidth, // extend the line by one character so italics fit
                                   CharHeight, Bitmap.HMemDC, 0, 0, SRCCOPY);
                            // if needed, clear the space on the right
                            if (myLine.right < fullLine.right)
                            {
                                myLine.top = CharHeight * i;
                                myLine.bottom = myLine.top + CharHeight;
                                myLine.left = BORDER_WIDTH + myLine.right;
                                myLine.right = BORDER_WIDTH + fullLine.right;
                                FillRect(dc, &myLine, BkgndBrush);
                            }
                        }
                    }

                    lineOffset += len;
                    if (lineOffset == FileSize)
                    {
                        r.left = BORDER_WIDTH;
                        if (FileSize > 0) // JR: up to AS2.52b1 (inclusive) we rendered a 0-byte file in hex without clearing the first line (it appeared when resizing the window)
                            r.top = CharHeight * (i + 1);
                        else
                            r.top = 0;
                        r.bottom = Height;
                        if (r.top <= r.bottom)
                            FillRect(dc, &r, BkgndBrush);
                        break;
                    }
                }
                break;
            }

            case vtText:
            {
                if (HasDecodedTextMode())
                {
                    PaintDecodedText(dc, fullLine, lines, columns, clipFirstRow, clipLastRow, fatalErr, setFindOffset);
                    break;
                }
                __int64 xRollLimit = (Width - BORDER_WIDTH) / CharWidth / 6;
                FirstLineSize = LastLineSize = 0;

                WrapIsBeforeFirstLine = FALSE;
                if (WrapText && SeekY > 0)
                {
                    __int64 len = Prepare(NULL, SeekY - (SeekY > 1 ? 2 : 1), SeekY > 1 ? 2 : 1, fatalErr);
                    // if (fatalErr) FatalFileErrorOccured(); // see below
                    if (fatalErr)
                        break;

                    unsigned char* s = Buffer + (SeekY - Seek) - 1;
                    if (!(*s == '\n' && Configuration.EOL_LF ||
                          *s == '\r' && Configuration.EOL_CR ||
                          *s == 0 && Configuration.EOL_NULL ||
                          SeekY > 1 && *(s - 1) == '\r' && *s == '\n' && Configuration.EOL_CRLF))
                    {
                        WrapIsBeforeFirstLine = TRUE;
                    }
                }

                RECT endRect = fullLine;
                __int64 lineOffset = SeekY; // offset of the beginning of the line in bytes
                BOOL EOL = FALSE;           // TRUE if the previous line ended with an EOL (the next line exists, it may be empty)
                int lineEOLSize = 0;        // length of the EOL (CR=1, LF=1, CRLF=2, NULL=1)
                for (int i = 0; i < lines; i++)
                {
                    __int64 len = Prepare(NULL, lineOffset, APROX_LINE_LEN, fatalErr);
                    // if (fatalErr) FatalFileErrorOccured(); // see below
                    if (fatalErr)
                        break;
                    if (len == 0 && i + 1 != lines && SeekY != 0)
                    {
                        __int64 size = FileSize;
                        FileChanged(NULL, TRUE, fatalErr, FALSE);
                        // if (fatalErr) FatalFileErrorOccured(); // see below
                        if (fatalErr || ExitTextMode)
                            break;
                        if (size != FileSize)
                        {
                            setFindOffset = FALSE; // leave it for the next drawing pass
                            LineOffset.DestroyMembers();
                            ViewSize = 0;
                            FindNewSeekY(SeekY, fatalErr);
                            if (fatalErr || ExitTextMode)
                                break;
                            FirstLineSize = LastLineSize = 0;
                            // when viewing a growing text file whose last line ended with a line break,
                            // scrolling down at the end of the file caused incorrect repainting;
                            // this invalidate call ensures a full repaint
                            InvalidateRect(HWindow, NULL, FALSE);
                            break;
                        }
                    }

                    if (len == 0)
                    {
                        int redrI = i;
                        if (redrI < clipFirstRow)
                            redrI = clipFirstRow; // avoid repainting unnecessarily
                        r.left = BORDER_WIDTH;
                        r.top = CharHeight * redrI;
                        r.bottom = CharHeight * clipLastRow;
                        if (r.bottom > Height)
                            r.bottom = Height;
                        if (r.top <= r.bottom)
                            FillRect(dc, &r, BkgndBrush);

                        if (EOL) // add the last empty line to the LineOffset array -> the line cannot be ignored
                        {
                            LineOffset.Add(lineOffset);
                            LineOffset.Add(lineOffset);
                            LineOffset.Add(0);
                        }
                        break;
                    }

                    unsigned char* st;                                               // start of the buffer with the line content
                    unsigned char* s2;                                               // processed character from the buffer with the line content
                    __int64 lineLen = 0;                                             // line length in characters (tab != 1 character)
                    BOOL lineEndIsWrapped = FALSE;                                   // is the end of the line wrapped because wrap mode is enabled?
                    __int64 fullLineLen = 0;                                         // line length in bytes
                    __int64 endX = OriginX + (Width - BORDER_WIDTH) / CharWidth + 1; // screen edge offset in characters
                    BOOL onlyOne = (len == 1);                                       // last character of the file?
                    __int64 startSel = min(StartSelection, EndSelection);            // selection start - offset in bytes
                    if (startSel == -1)
                        startSel = 0;
                    __int64 endSel = max(StartSelection, EndSelection); // selection end - offset in bytes
                    if (endSel == -1)
                        endSel = 0;
                    if (startSel == endSel)
                        startSel = endSel = 0;
                    BOOL startSelDone = startSel <= lineOffset; // the selection start has already been drawn
                    BOOL endSelDone = endSel < lineOffset;      // the selection end has already been drawn

                    __int64 tabSpaces = 0; // shift caused by tabs
                    EOL = FALSE;
                    lineEOLSize = 0;
                    while (len != 0)
                    {
                        st = s2 = Buffer + (lineOffset + fullLineLen - Seek);
                        while (len--)
                        {
                            if (*s2 == '\r') // 'end of line \r' or '\r\n'
                            {
                                BOOL ok = FALSE;
                                if (len > 0)
                                {
                                    if (*(s2 + 1) == '\n' && Configuration.EOL_CRLF)
                                    {
                                        s2 += 2; // '\r\n'
                                        len--;
                                        ok = TRUE;
                                        EOL = TRUE;
                                        lineEOLSize = 2;
                                    }
                                    else if (Configuration.EOL_CR)
                                    {
                                        ok = TRUE;
                                        s2++; // '\r'
                                        EOL = TRUE;
                                        lineEOLSize = 1;
                                    }
                                }
                                else
                                {
                                    if (!onlyOne)
                                    { // the line has not ended yet; there may be '\n' beyond the boundary
                                        if (Configuration.EOL_CRLF)
                                        {
                                            len = -1;
                                            break;
                                        }
                                        else
                                        {
                                            if (Configuration.EOL_CR)
                                            {
                                                ok = TRUE;
                                                s2++; // '\r'
                                                EOL = TRUE;
                                                lineEOLSize = 1;
                                            }
                                        }
                                    }
                                    else
                                    {
                                        if (Configuration.EOL_CR)
                                        {
                                            ok = TRUE;
                                            s2++; // the last character of the file is '\r'
                                            EOL = TRUE;
                                            lineEOLSize = 1;
                                        }
                                    }
                                }

                                if (ok)
                                    break;
                                else
                                    goto COMMON_CHAR;
                            }
                            else
                            {
                                if (*s2 == '\n' || *s2 == 0) // end of line '\n' or 0
                                {
                                    if ((*s2 == '\n') ? Configuration.EOL_LF : Configuration.EOL_NULL)
                                    {
                                        s2++;
                                        EOL = TRUE;
                                        lineEOLSize = 1;
                                        break;
                                    }
                                    else
                                        goto COMMON_CHAR;
                                }
                                else
                                {
                                    if (*s2 == '\t')
                                    {
                                        __int64 curOff = lineOffset + fullLineLen + (s2 - st);
                                        if (!startSelDone && startSel <= curOff)
                                        {
                                            startSelDone = TRUE;
                                            startSel += tabSpaces;
                                        }
                                        if (!endSelDone && endSel <= curOff)
                                        {
                                            endSelDone = TRUE;
                                            endSel += tabSpaces;
                                        }
                                        int tab = (int)(Configuration.TabSize - (lineLen % Configuration.TabSize));
                                        if (WrapText && lineLen + tab - 1 >= columns)
                                        {
                                            tab = (int)max(1, columns - lineLen); // at most to the edge, at least 1 character
                                        }
                                        tabSpaces += tab - 1;
                                        if (lineLen > OriginX - tab && endX > lineLen)
                                            memset(line + max(0, lineLen - OriginX), ' ', min(tab, (int)(endX - lineLen)));

                                        lineLen += tab - 1;
                                    }
                                    else
                                    {
                                    COMMON_CHAR:

                                        if (lineLen >= OriginX && lineLen < endX)
                                            line[lineLen - OriginX] = *s2;
                                    }
                                }
                            }
                            if (WrapText && lineLen >= columns)
                            {
#ifdef _DEBUG
                                if (lineLen > columns)
                                    TRACE_E("something's wrong");
#endif // _DEBUG
                                lineEndIsWrapped = TRUE;
                                break; // premature end of line
                            }
                            s2++;
                            lineLen++;
                        }
                        fullLineLen += s2 - st;

                        // test the text line length (over 10000 characters we offer HEX mode)
                        if (CanSwitchToHex && !ForceTextMode && fullLineLen > TEXT_MAX_LINE_LEN)
                        {
                            if (!CanSwitchQuietlyToHex)
                                CanSwitchToHex = FALSE;
                            if (CanSwitchQuietlyToHex ||
                                gPrompter->AskYesNo(LoadStrW(IDS_VIEWERTITLE), LoadStrW(IDS_VIEWER_BINFILE)).type == PromptResult::kYes)
                            {
                                CanSwitchQuietlyToHex = FALSE;
                                ExitTextMode = TRUE;
                                PostMessage(HWindow, WM_COMMAND, CM_TO_HEX, 0);
                                break;
                            }
                            else
                            {
                                ForceTextMode = TRUE;
                            }
                        }

                        if (len == -1) // the line continues into a yet unread section
                        {
                            len = Prepare(NULL, lineOffset + fullLineLen, APROX_LINE_LEN, fatalErr);
                            // if (fatalErr) FatalFileErrorOccured(); // see below
                            if (fatalErr)
                                break;
                            onlyOne = (len == 1);
                        }
                        else
                            break; // the entire line has been loaded
                    }
                    if (fatalErr || ExitTextMode)
                        break;
                    LineOffset.Add(lineOffset);                             // line start offset
                    LineOffset.Add(lineOffset + fullLineLen - lineEOLSize); // line end offset (before EOL)
                    LineOffset.Add(lineLen);                                // line length in displayed characters (TAB is more characters)
                    if (!startSelDone)
                        startSel += tabSpaces;
                    if (!endSelDone)
                        endSel += tabSpaces;

                    if (ScrollToSelection)
                    {
                        int len2 = (Width - BORDER_WIDTH) / CharWidth;
                        if (len2 - 2 * xRollLimit < endSel - startSel)
                        { // try to show as much of long strings as possible, so set xRollLimit -> 0
                            xRollLimit = (len2 - (endSel - startSel)) / 2;
                            if (xRollLimit < 0)
                                xRollLimit = 0;
                        }
                        __int64 left = lineOffset + OriginX;
                        __int64 right = lineOffset + OriginX + len2;
                        if (startSel < lineOffset + lineLen)
                        {
                            __int64 originX = OriginX;
                            if (startSel < left) // the view is too far to the right
                            {
                                originX = startSel - lineOffset - xRollLimit;
                                if (originX < 0)
                                    originX = 0;
                            }
                            else
                            {
                                if (startSel >= right ||
                                    endSel < lineOffset + lineLen && endSel >= right)
                                { // the view is too far to the left
                                    originX = startSel - lineOffset - xRollLimit;
                                    if (originX < 0)
                                        originX = 0;
                                    __int64 originX2 = endSel - lineOffset - len2 + 1 + xRollLimit;
                                    if (originX2 < 0)
                                        originX2 = 0;
                                    originX = min(originX, originX2);
                                }
                            }
                            if (originX != OriginX) // there was a change
                            {
                                setFindOffset = FALSE; // resetting FindOffset probably is not needed here, but if it is, we will do it during the next drawing pass
                                OriginX = originX;
                                InvalidateRect(HWindow, NULL, FALSE);
                                break; // we need to start over
                            }
                            else
                                ScrollToSelection = FALSE; // not necessary
                        }
                    }

                    if (i == 0)
                        FirstLineSize = fullLineLen;
                    if (i + 1 < lines)
                    {
                        ViewSize += fullLineLen; // count only fully visible lines
                        if (i + 2 == lines)
                            LastLineSize = fullLineLen;
                    }

                    BOOL blackEnd;
                    if (OriginX < lineLen)
                    {
                        __int64 len2 = min((Width - BORDER_WIDTH) / CharWidth + 1, lineLen - OriginX);
                        __int64 left = lineOffset + OriginX;
                        __int64 right = lineOffset + OriginX + len2;
                        __int64 u1 = len2, u2 = 0, u3 = 0;
                        blackEnd = (lineEndIsWrapped ? startSel < right : startSel <= right) && endSel > right;
                        if (startSel <= left)
                        {
                            if (endSel > left)
                            {
                                u1 = 0;
                                u2 = min(right, endSel) - left;
                                u3 = len2 - u2;
                            }
                        }
                        else if (startSel < right)
                        {
                            if (endSel > startSel)
                            {
                                u1 = startSel - left;
                                u2 = min(right, endSel) - left - u1;
                                u3 = len2 - u2 - u1;
                            }
                        }

                        if (i >= clipFirstRow && i <= clipLastRow)
                        {
                            RECT myLine = fullLine;                                        // myLine is the text area that we will draw with the slower BitBlt path
                            myLine.right = min(myLine.right, (int)(len2 + 1) * CharWidth); // allow one extra character so italics fit

                            if (blackEnd)
                            {
                                endRect.left = 0;
                                endRect.right = (int)((u1 + u2 + u3) * CharWidth);
                                FillRect(Bitmap.HMemDC, &endRect, BkgndBrush);
                                endRect.left = endRect.right;
                                endRect.right = Width - BORDER_WIDTH;
                                FillRect(Bitmap.HMemDC, &endRect, BkgndBrushSel);
                            }
                            else
                                FillRect(Bitmap.HMemDC, &myLine, BkgndBrush);

                            if (lineLen > OriginX)
                            { // output text to Bitmap.HMemDC
                                if (u3 > 0)
                                    DrawLegacyByteCells(Bitmap.HMemDC, (int)((u1 + u2) * CharWidth), 0, line + u1 + u2, (int)u3);
                                if (u2 > 0)
                                {
                                    SetBkColor(Bitmap.HMemDC, GetCOLORREF(ViewerColors[VIEWER_BK_SELECTED]));
                                    SetTextColor(Bitmap.HMemDC, GetCOLORREF(ViewerColors[VIEWER_FG_SELECTED]));
                                    SetBkMode(Bitmap.HMemDC, OPAQUE);
                                    DrawLegacyByteCells(Bitmap.HMemDC, (int)(u1 * CharWidth), 0, line + u1, (int)u2);
                                    SetBkMode(Bitmap.HMemDC, TRANSPARENT);
                                    SetTextColor(Bitmap.HMemDC, GetCOLORREF(ViewerColors[VIEWER_FG_NORMAL]));
                                    SetBkColor(Bitmap.HMemDC, GetCOLORREF(ViewerColors[VIEWER_BK_NORMAL]));
                                }
                                if (u1 > 0)
                                    DrawLegacyByteCells(Bitmap.HMemDC, 0, 0, line, (int)u1);
                            }

                            // bitblt the entire row to the screen
                            BitBlt(dc, BORDER_WIDTH, CharHeight * i, myLine.right,
                                   CharHeight, Bitmap.HMemDC, 0, 0, SRCCOPY);

                            // if needed, clear the space on the right
                            if (myLine.right < fullLine.right)
                            {
                                myLine.top = CharHeight * i;
                                myLine.bottom = myLine.top + CharHeight;
                                myLine.left = BORDER_WIDTH + myLine.right;
                                myLine.right = BORDER_WIDTH + fullLine.right;
                                FillRect(dc, &myLine, blackEnd ? BkgndBrushSel : BkgndBrush);
                            }
                        }
                    }
                    else
                    {
                        if (i >= clipFirstRow && i <= clipLastRow)
                        {
                            blackEnd = (lineEndIsWrapped ? startSel < lineOffset + lineLen : startSel <= lineOffset + lineLen) &&
                                       endSel > lineOffset + lineLen;
                            r.left = BORDER_WIDTH;
                            r.top = CharHeight * i;
                            r.right = Width;
                            r.bottom = r.top + CharHeight;
                            FillRect(dc, &r, blackEnd ? BkgndBrushSel : BkgndBrush);
                        }
                    }
                    lineOffset += fullLineLen;
                }
                break;
            }
            }
            if (setFindOffset && !fatalErr && !ExitTextMode)
            {
                ResetFindOffsetOnNextPaint = FALSE;
                FindOffset = SeekY;
                if (!FindDialog.Forward)
                    FindOffset += ViewSize;
            }
        EnablePaint = TRUE;
        ScrollToSelection = FALSE;
        SetBkMode(Bitmap.HMemDC, oldMode);
        //---
        SelectObject(dc, oldFont);
        SelectObject(Bitmap.HMemDC, oldFont2);
        //---
        if (fatalErr)
            FatalFileErrorOccured();
        if (fatalErr || ExitTextMode)
            return;
        SetScrollBar();
        //    SetCursor(oldCursor);
    }
    else // at least clear the screen
    {
        RECT r;
        r.left = 0;
        r.right = Width;
        r.top = 0;
        r.bottom = Height;
        FillRect(dc, &r, BkgndBrush); // clear the column to the left of the text
        SetScrollBar();
    }
}

//
// ****************************************************************************

BOOL CViewerWindow::CreateViewerBrushs()
{
    BkgndBrush = HANDLES(CreateSolidBrush(GetCOLORREF(ViewerColors[VIEWER_BK_NORMAL])));
    if (BkgndBrush == NULL)
    {
        TRACE_E("Unable to create window background brush.");
        return FALSE;
    }
    BkgndBrushSel = HANDLES(CreateSolidBrush(GetCOLORREF(ViewerColors[VIEWER_BK_SELECTED])));
    if (BkgndBrushSel == NULL)
    {
        TRACE_E("Unable to create window selected text background brush.");
        return FALSE;
    }
    return TRUE;
}

void UpdateViewerColors(SALCOLOR* colors)
{
    DarkModeColors darkModeColors;
    DarkMode_GetColors(&darkModeColors);

    if (GetFValue(colors[VIEWER_FG_NORMAL]) & SCF_DEFAULT)
        SetRGBPart(&colors[VIEWER_FG_NORMAL], darkModeColors.ViewerText);
    if (GetFValue(colors[VIEWER_BK_NORMAL]) & SCF_DEFAULT)
        SetRGBPart(&colors[VIEWER_BK_NORMAL], darkModeColors.ViewerBackground);
    if (GetFValue(colors[VIEWER_FG_SELECTED]) & SCF_DEFAULT)
        SetRGBPart(&colors[VIEWER_FG_SELECTED], darkModeColors.ViewerSelectionText);
    if (GetFValue(colors[VIEWER_BK_SELECTED]) & SCF_DEFAULT)
        SetRGBPart(&colors[VIEWER_BK_SELECTED], darkModeColors.ViewerSelectionBackground);
}

BOOL InitializeViewer()
{
    int i;
    for (i = 0; i < VIEWER_HISTORY_SIZE; i++)
        ViewerHistory[i] = NULL;

    GetDefaultViewerLogFont(&ViewerLogFont);

    HANDLES(InitializeCriticalSection(&ViewerFontMeasureCS));

    UpdateViewerColors(ViewerColors);
    ViewerMenu = LoadMenuW(HLanguage, MAKEINTRESOURCEW(IDM_VIEWERMENU));
    if (ViewerMenu == NULL)
    {
        TRACE_E("Unable to load menu for viewer.");
        return FALSE;
    }
    MENUITEMINFOW mi;
    memset(&mi, 0, sizeof(mi));
    mi.cbSize = sizeof(mi);
    mi.fMask = MIIM_TYPE | MIIM_SUBMENU;
    mi.fType = MFT_STRING;
    mi.hSubMenu = CreatePopupMenu();
    std::wstring codingMenuText = LoadStrOwned(IDS_VIEWERCODINGMENU);
    mi.dwTypeData = codingMenuText.data();
    InsertMenuItemW(ViewerMenu, CODING_MENU_INDEX, TRUE, &mi);

    ViewerTable = HANDLES(LoadAcceleratorsW(HInstance, MAKEINTRESOURCEW(IDA_VIEWERACCELS)));
    if (ViewerTable == NULL)
    {
        TRACE_E("Unable to load accelerators for viewer.");
        return FALSE;
    }

    if (!CViewerWindow::RegisterUniversalClass(CS_DBLCLKS | CS_HREDRAW | CS_VREDRAW,
                                               0,
                                               0,
                                               HANDLES(LoadIconW(HInstance,
                                                                 MAKEINTRESOURCEW(IDI_VIEWER))),
                                               LoadCursorW(NULL, IDC_ARROW),
                                               (HBRUSH)(COLOR_WINDOW + 1),
                                               NULL,
                                               CVIEWERWINDOW_CLASSNAME,
                                               NULL))
    {
        TRACE_E("Unable to register window class for viewer.");
        return FALSE;
    }

    ViewerContinue = HANDLES(CreateEvent(NULL, FALSE, FALSE, NULL));
    if (ViewerContinue == NULL)
    {
        TRACE_E("Unable to create ViewerContinue event.");
        return FALSE;
    }

    return TRUE;
}

void CViewerWindow::ReleaseViewerBrushs()
{
    if (BkgndBrush != NULL)
    {
        HANDLES(DeleteObject(BkgndBrush));
        BkgndBrush = NULL;
    }
    if (BkgndBrushSel != NULL)
    {
        HANDLES(DeleteObject(BkgndBrushSel));
        BkgndBrushSel = NULL;
    }
}

void ClearViewerHistory(BOOL dataOnly)
{
    int i;
    for (i = 0; i < VIEWER_HISTORY_SIZE; i++)
    {
        if (ViewerHistory[i] != NULL)
        {
            free(ViewerHistory[i]);
            ViewerHistory[i] = NULL;
        }
    }

    if (!dataOnly)
    {
        // also clear the combobox in any open Find windows
        ViewerWindowQueue.BroadcastMessage(WM_USER_CLEARHISTORY, 0, 0);
    }
}

void ReleaseViewer()
{
    if (ViewerMenu != NULL)
        DestroyMenu(ViewerMenu);
    ClearViewerHistory(TRUE); // we only want to clear the data
    if (ViewerContinue != NULL)
        HANDLES(CloseHandle(ViewerContinue));
    HANDLES(DeleteCriticalSection(&ViewerFontMeasureCS));
}

void CViewerWindow::SetViewerFont()
{
    if (ViewerFont != NULL)
        HANDLES(DeleteObject(ViewerFont));
    LOGFONT lf;
    if (UseCustomViewerFont)
        lf = ViewerLogFont;
    else
        GetDefaultViewerLogFont(&lf);
    ViewerFont = HANDLES(CreateFontIndirect(&lf));
    if (ViewerFont == NULL)
    {
        TRACE_E("Unable to create ViewerFont.");
        return;
    }
    else
    {
        HDC dc = HANDLES(GetDC(NULL));
        HFONT old = (HFONT)SelectObject(dc, ViewerFont);
        TEXTMETRIC tm;
        BOOL ok = GetTextMetrics(dc, &tm);
        CharHeight = max(1, tm.tmHeight);
        CharWidth = max(1, tm.tmAveCharWidth);
        SelectObject(dc, old);
        HANDLES(ReleaseDC(NULL, dc));

        if (!ok)
        {
            TRACE_E("Unable to get text metrics for ViewerFont.");
            HANDLES(DeleteObject(ViewerFont));
            ViewerFont = NULL;
            return;
        }

        if (Bitmap.HBmp != NULL)
            Bitmap.ReCreateForScreenDC(Width, CharHeight);

        HANDLES(EnterCriticalSection(&ViewerFontMeasureCS));

        // The legacy/hex view still has one visual cell per source byte. Build
        // its byte-to-UTF-16 table once, measuring every resulting glyph so the
        // old fixed-font substitution behavior remains intact without GDI A.
        if (!ViewerFontMeasured)
        {
            HFONT oldFont = (HFONT)SelectObject(Bitmap.HMemDC, ViewerFont);
            int oldMode = SetBkMode(Bitmap.HMemDC, TRANSPARENT);

            RECT rect;
            wchar_t substChar = L'\xB7'; // middle dot
            int x;
            for (x = 0; x < 256; x++)
            {
                wchar_t character = sally::legacy_search::DecodeDisplayCellAcp(
                    static_cast<std::uint8_t>(x));
                ViewerFontMapping[x] = character;
                rect.left = 0;
                rect.right = Width;
                rect.top = 0;
                rect.bottom = CharHeight;
                if (DrawTextExW(Bitmap.HMemDC, &character, 1, &rect, DT_LEFT | DT_TOP | DT_CALCRECT | DT_NOPREFIX | DT_SINGLELINE, NULL))
                {
                    if (rect.right - rect.left != CharWidth)
                    {
                        if (x == 0xB7 /* middle dot */) // if the 'middle dot' is also incorrectly wide, substitute a space, which should be OK
                        {
                            substChar = ' ';
                            int z;
                            for (z = 0; z < x; z++)
                                if (ViewerFontMapping[z] == L'\xB7' /* middle dot */)
                                    ViewerFontMapping[z] = substChar;
                        }
                        ViewerFontMapping[x] = substChar;
                    }
                }
                else
                    TRACE_I("CViewerWindow::SetViewerFont(): DrawTextEx: error for: " << x);
            }

            SetBkMode(Bitmap.HMemDC, oldMode);
            SelectObject(Bitmap.HMemDC, oldFont);

            ViewerFontMeasured = TRUE;
        }

        HANDLES(LeaveCriticalSection(&ViewerFontMeasureCS));
    }
}
