// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"

#include "dialogs.h"
#include "stswnd.h"
#include "plugins.h"
#include "fileswnd.h"
#include "mainwnd.h"
#include "gui.h"
#include "logo.h"
#include "darkmode.h"
#include "common/unicode/helpers.h"

static void FillRectWithColor(HDC hDC, const RECT* rect, COLORREF color)
{
    HBRUSH hBrush = CreateSolidBrush(color);
    if (hBrush != NULL)
    {
        FillRect(hDC, rect, hBrush);
        DeleteObject(hBrush);
    }
}

// helper object for sending Ctrl+C to the parent via the WM_COPY message
class CKeyForwarderWindow : public CWindow
{
public:
    CKeyForwarderWindow(HWND hDlg, int ctrlID) : CWindow(hDlg, ctrlID)
    {
    }

protected:
    virtual LRESULT WindowProc(UINT uMsg, WPARAM wParam, LPARAM lParam)
    {
        if (uMsg == WM_KEYDOWN)
        {
            BOOL controlPressed = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
            BOOL altPressed = (GetKeyState(VK_MENU) & 0x8000) != 0;
            BOOL shiftPressed = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
            if (!shiftPressed && controlPressed && !altPressed)
            {
                if (wParam == 'C')
                {
                    HWND hParent = GetParent(HWindow);
                    if (hParent != NULL)
                        PostMessage(hParent, WM_COPY, 0, 0);
                }
            }
        }
        return CWindow::WindowProc(uMsg, wParam, lParam);
    }
};

//****************************************************************************
//
// CMessageBox
//

CMessageBox::CMessageBox(HWND parent, DWORD flags, const wchar_t* title, const wchar_t* text,
                         const wchar_t* checkText, BOOL* check, HICON hOwnIcon,
                         DWORD contextHelpId, MSGBOXEX_CALLBACK helpCallback,
                         const wchar_t* aliasBtnNames, const wchar_t* url, const wchar_t* urlText)
    : CCommonDialog(HInstance, IDD_MSGBOX, parent, ooStandard, NULL)
{
    Flags = flags;
    Title = title ? title : LoadStrW(IDS_ERRORTITLE);
    Text.SetW(text, NULL);
    if (checkText != NULL)
    {
        if (check == NULL)
        {
            TRACE_E("CMessageBox::CMessageBox: check parameter is NULL.");
        }
        else
            CheckText = checkText;
    }
    Check = check;
    HOwnIcon = hOwnIcon;
    if (Flags & MSGBOXEX_HELP)
        SetHelpID(contextHelpId);
    HelpCallback = helpCallback;
    AliasBtnNames = aliasBtnNames ? aliasBtnNames : L"";
    URL = url ? url : L"";
    URLText = urlText ? urlText : L"";
    BackgroundSeparator = 0;
}

CMessageBox::CMessageBox(HWND parent, DWORD flags, const wchar_t* title, CTruncatedString* text,
                         const wchar_t* checkText, BOOL* check, HICON hOwnIcon,
                         DWORD contextHelpId, MSGBOXEX_CALLBACK helpCallback,
                         const wchar_t* aliasBtnNames, const wchar_t* url, const wchar_t* urlText)
    : CCommonDialog(HInstance, IDD_MSGBOX, parent, ooStandard, NULL)
{
    Flags = flags;
    Title = title ? title : LoadStrW(IDS_ERRORTITLE);
    Text.CopyFrom(text);
    if (checkText != NULL)
    {
        if (check == NULL)
        {
            TRACE_E("CMessageBox::CMessageBox: check parameter is NULL.");
        }
        else
            CheckText = checkText;
    }
    Check = check;
    HOwnIcon = hOwnIcon;
    if (Flags & MSGBOXEX_HELP)
        SetHelpID(contextHelpId);
    HelpCallback = helpCallback;
    AliasBtnNames = aliasBtnNames ? aliasBtnNames : L"";
    URL = url ? url : L"";
    URLText = urlText ? urlText : L"";
    BackgroundSeparator = 0;
}

CMessageBox::~CMessageBox()
{
    // std::string members auto-destroyed
}

void CMessageBox::Transfer(CTransferInfo& ti)
{
    CALL_STACK_MESSAGE1("CMessageBox::Transfer()");
    if (!CheckText.empty() && Check != NULL)
        ti.CheckBox(IDS_MSGBOX_CHECK, *Check);
}

int CMessageBox::Execute()
{
    SplashScreenCloseIfExist();

    HWND mainWnd = GetWndToFlash(Parent);

    // this patch caused many crashes in SS2.5RC1, so we are abandoning it
    // and fixing FileComparator so WM_USER_ACTIVATEWINDOW behaves less
    // aggressively
    /*
  // first deliver messages (a standard MessageBox behaves the same way)
  // for example, File Comparator has a postponed WM_USER_ACTIVATEWINDOW message in the queue
  // which without this pump would be delivered only after the message box activation
  // so diffwnd would steal the activation afterwards
  MSG msg;
  while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE))
  {
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }
  */

    int ret = (int)CCommonDialog::Execute();
    if (mainWnd != NULL)
        FlashWindow(mainWnd, FALSE);

    return ret;
}

// move a child window by dx and dy
void OffsetChildWindow(HWND hDialog, int resID, int dx, int dy)
{
    HWND hWnd = GetDlgItem(hDialog, resID);
    RECT windowR;
    GetWindowRect(hWnd, &windowR);
    POINT p;
    p.x = windowR.left;
    p.y = windowR.top;
    ScreenToClient(hDialog, &p);
    SetWindowPos(hWnd, NULL, p.x + dx, p.y + dy, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
}

BOOL CMessageBox::EscapeEnabled()
{
    DWORD flagsType = Flags & MSGBOXEX_TYPEMASK;
    if (flagsType == MSGBOXEX_ABORTRETRYIGNORE)
        return (Flags & MSGBOXEX_ESCAPEENABLED) != 0;
    if (flagsType == MSGBOXEX_YESNO)
        return (Flags & MSGBOXEX_ESCAPEENABLED) != 0;
    else
        return TRUE;
}

// returns a copy of 'src' into which 'n' characters are inserted so that the resulting maximum
// width does not exceed 'maxWidth'. Assumes that the hDC has the correct font selected.
// returns NULL in case of failure

wchar_t* DuplicateStrAndInsertEOLs(const wchar_t* src, HDC hDC, int maxWidth)
{
    if (src == NULL || *src == 0)
        return NULL;

    int srcLen = (int)wcslen(src);

    // array for storing partial lengths in the string
    int* alpDx = (int*)malloc((1 + srcLen + 1) * sizeof(int)); // one extra slot for safety
    if (alpDx == NULL)
    {
        TRACE_E(LOW_MEMORY);
        return NULL;
    }
    alpDx[0] = 0;

    SIZE sz;
    if (!GetTextExtentExPointW(hDC, src, srcLen, 0, NULL, alpDx + 1, &sz))
    {
        free(alpDx);
        return NULL;
    }

    int breakCount = 0;
    int lineLen = 0;
    int i;
    for (i = 0; i < srcLen; i++)
    {
        if (src[i] == L'\n')
        {
            // end of line
            lineLen = 0;
        }
        else
        {
            if (lineLen + (alpDx[i + 1] - alpDx[i]) > maxWidth)
            {
                alpDx[breakCount] = i;
                breakCount++;
                lineLen = 0;
            }
            else
            {
                lineLen += alpDx[i + 1] - alpDx[i];
            }
        }
    }

    if (breakCount > 0)
    {
        wchar_t* text = (wchar_t*)malloc((srcLen + breakCount + 1) * sizeof(wchar_t));
        if (text != NULL)
        {
            memcpy(text, src, (srcLen + 1) * sizeof(wchar_t));
            int i2;
            for (i2 = 0; i2 < breakCount; i2++)
            {
                memmove(text + alpDx[i2] + 1, text + alpDx[i2], (srcLen - alpDx[i2] + 1 + i2) * sizeof(wchar_t));
                text[alpDx[i2]] = L'\n';
            }
            free(alpDx);
            return text;
        }
    }

    free(alpDx);
    return NULL;
}

wchar_t* DuplicateStrAndInsertEOLsW(const wchar_t* src, HDC hDC, int maxWidth)
{
    if (src == NULL || *src == 0)
        return NULL;

    int srcLen = (int)wcslen(src);

    // array for storing partial lengths in the string
    int* alpDx = (int*)malloc((1 + srcLen + 1) * sizeof(int)); // one extra slot for safety
    if (alpDx == NULL)
    {
        TRACE_E(LOW_MEMORY);
        return NULL;
    }
    alpDx[0] = 0;

    SIZE sz;
    if (!GetTextExtentExPointW(hDC, src, srcLen, 0, NULL, alpDx + 1, &sz))
    {
        free(alpDx);
        return NULL;
    }

    int breakCount = 0;
    int lineLen = 0;
    int i;
    for (i = 0; i < srcLen; i++)
    {
        if (src[i] == '\n')
        {
            // end of line
            lineLen = 0;
        }
        else
        {
            if (lineLen + (alpDx[i + 1] - alpDx[i]) > maxWidth)
            {
                alpDx[breakCount] = i;
                breakCount++;
                lineLen = 0;
            }
            else
            {
                lineLen += alpDx[i + 1] - alpDx[i];
            }
        }
    }

    if (breakCount > 0)
    {
        wchar_t* text = (wchar_t*)malloc((srcLen + breakCount + 1) * sizeof(wchar_t));
        if (text != NULL)
        {
            wmemcpy(text, src, srcLen + 1);
            int i2;
            for (i2 = 0; i2 < breakCount; i2++)
            {
                wmemmove(text + alpDx[i2] + 1, text + alpDx[i2], srcLen - alpDx[i2] + 1 + i2);
                text[alpDx[i2]] = L'\n';
            }
            free(alpDx);
            return text;
        }
    }

    free(alpDx);
    return NULL;
}

BOOL CMessageBox::CopyToClipboard()
{
    const wchar_t* separator = L"---------------------------\r\n";

    // compute the total buffer size
    const wchar_t* text = Text.Get();
    int urlTextLen = 0;
    if (!URL.empty())
    {
        urlTextLen += (int)URL.size() + 4;
        if (!URLText.empty())
            urlTextLen += (int)URLText.size() + 4;
    }
    int buffSize = 4 * (int)wcslen(separator) +
                   (int)Title.size() +
                   2 * (int)wcslen(text) + // every character may be '\n'; we'll convert them to "\r\n"
                   urlTextLen +
                   MESSAGEBOX_MAXBUTTONS * (100 + 2 + 4) + // max number of characters in a button we're willing to handle
                   50;                                     // extra space for "\r\n"
    if (!CheckText.empty())
        buffSize += 300 + 2 + (int)wcslen(separator);

    wchar_t* buff = (wchar_t*)malloc(buffSize * sizeof(wchar_t));
    if (buff == NULL)
    {
        TRACE_E(LOW_MEMORY);
        return FALSE;
    }

    DWORD written = wsprintfW(buff, L"%s%s\r\n%s", separator, Title.c_str(), separator);
    wchar_t* ptr = buff + written;

    // convert '\n' -> "\r\n"
    while (*text != 0)
    {
        if (*text == L'\n')
            *ptr++ = L'\r';
        *ptr++ = *text++;
    }

    if (!URL.empty())
    {
        ptr += wsprintfW(ptr, L"\r\n");
        if (!URLText.empty())
            ptr += wsprintfW(ptr, L"%s ", URLText.c_str());
        ptr += wsprintfW(ptr, L"%s", URL.c_str());
    }

    written = wsprintfW(ptr, L"\r\n%s", separator);
    ptr += written;

    // append list of buttons, remove '&'
    int i;
    for (i = 0; i < MESSAGEBOX_MAXBUTTONS; i++)
    {
        if (ButtonsID[i] != 0)
        {
            wchar_t btnText[100];
            GetDlgItemTextW(HWindow, ButtonsID[i], btnText, 100);
            btnText[99] = 0;
            RemoveAmpersands(btnText);

            written = wsprintfW(ptr, L"[%s]", btnText);
            ptr += written;
            if (i < MESSAGEBOX_MAXBUTTONS - 1 && ButtonsID[i + 1] != 0)
            {
                *ptr++ = L' ';
                *ptr++ = L' ';
                *ptr++ = L' ';
            }
        }
    }

    written = wsprintfW(ptr, L"\r\n%s", separator);
    ptr += written;

    // if a checkbox exists, append its text (strip the '&')
    if (!CheckText.empty())
    {
        wchar_t chkText[300];
        GetDlgItemTextW(HWindow, IDS_MSGBOX_CHECK, chkText, 300);
        chkText[299] = 0;
        RemoveAmpersands(chkText);
        written = wsprintfW(ptr, L"[ ] %s\r\n%s", chkText, separator);
        BOOL checked = IsDlgButtonChecked(HWindow, IDS_MSGBOX_CHECK) == BST_CHECKED;
        if (checked)
            ptr[1] = L'x';
        ptr += written;
    }

    *ptr = 0; // terminator

    BOOL ret = CopyTextToClipboardW(buff);
    free(buff);

    return ret;
}

INT_PTR
CMessageBox::DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
    case WM_INITDIALOG:
    {
        // unmask individual parts of the flag
        DWORD flagsType = Flags & MSGBOXEX_TYPEMASK;
        DWORD flagsIcon = Flags & MSGBOXEX_ICONMASK;
        DWORD flagsDef = Flags & MSGBOXEX_DEFMASK;
        DWORD flagsMode = Flags & MSGBOXEX_MODEMASK;
        DWORD flagsMisc = Flags & MSGBOXEX_MISCMASK;
        DWORD flagsEx = Flags & MSGBOXEX_EXMASK;

        if (!EscapeEnabled())
            EnableMenuItem(GetSystemMenu(HWindow, FALSE), SC_CLOSE, MF_BYCOMMAND | MF_GRAYED);

        // set the window title and body text
        SetWindowTextW(HWindow, Title.c_str());
        if (Text.NeedTruncate())
            Text.TruncateText(GetDlgItem(HWindow, IDS_MSGBOX_TEXT), TRUE);
        SetDlgItemTextW(HWindow, IDS_MSGBOX_TEXT, Text.GetW());

        if (!URL.empty())
        {
            CHyperLink* hl = new CHyperLink(HWindow, IDS_MSGBOX_URL);
            hl->SetActionOpen(URL.c_str());
            const wchar_t* urlTextW = !URLText.empty() ? URLText.c_str() : URL.c_str();
            SetDlgItemTextW(HWindow, IDS_MSGBOX_URL, urlTextW);
        }
        else
            DestroyWindow(GetDlgItem(HWindow, IDS_MSGBOX_URL));

        // space between the buttons and the horizontal line / bottom of the dialog
        RECT lineR;
        GetWindowRect(GetDlgItem(HWindow, IDC_MSGBOX_LINE), &lineR);
        RECT btnR;
        GetWindowRect(GetDlgItem(HWindow, IDC_MSGBOX_1), &btnR);
        int btnBottomMargin = lineR.top - btnR.bottom;

        // text for the checkbox
        int checkLineH = 0;
        BOOL hintVisible = FALSE;
        wchar_t* hintLabel = NULL;
        if (!CheckText.empty())
        {
            if (flagsEx & MSGBOXEX_HINT)
            {
                // parse CheckText and locate two '\t' characters
                // after the first '\t' is the "clickable" text, which we display next to the checkbox
                // after the second '\t' is the actual hint, which is shown when the clickable text is clicked
                hintLabel = &CheckText[0];
                while (*hintLabel != L'\t' && *hintLabel != 0)
                    hintLabel++;
                wchar_t* hintText = hintLabel;
                if (*hintText == L'\t')
                    hintText++;
                while (*hintText != L'\t' && *hintText != 0)
                    hintText++;
                if (hintLabel > &CheckText[0] && *hintLabel != 0 &&
                    hintText > &CheckText[0] && *hintText != 0 &&
                    hintText > hintLabel)
                {
                    *hintLabel = 0;
                    hintLabel++;
                    *hintText = 0;
                    hintText++;

                    CHyperLink* hl = new CHyperLink(HWindow, IDS_MSGBOX_HINT, STF_DOTUNDERLINE);
                    if (hl != NULL)
                    {
                        SetDlgItemTextW(HWindow, IDS_MSGBOX_HINT, hintLabel);
                        // hintText is already wide (a pointer into CheckText);
                        // narrowing via WideToAnsi then re-widening inside SetActionShowHint
                        // could corrupt a non-ASCII hint. SetActionShowHint takes it directly.
                        hl->SetActionShowHint(hintText);
                        hintVisible = TRUE;
                    }
                }
                else
                    TRACE_E("CMessageBox: MSGBOXEX_HINT was specified, but has CheckText invalid syntax.");
            }
            else
            {
                // TAB has no place in a checkbox (W2K shows a vertical bar, XP shows nothing)
                wchar_t* p = &CheckText[0];
                while (*p != L'\t' && *p != 0)
                    p++;
                if (*p == L'\t')
                {
                    TRACE_E("CMessageBox: CheckText contains TAB character while MSGBOXEX_HINT was not specified. Trimming.");
                    *p = 0;
                }
            }

            SetDlgItemTextW(HWindow, IDS_MSGBOX_CHECK, CheckText.c_str());
        }
        else
        {
            if (flagsEx & MSGBOXEX_HINT)
                TRACE_E("CMessageBox: MSGBOXEX_HINT has sense only if CheckText is specified");

            RECT r1;
            GetWindowRect(GetDlgItem(HWindow, IDC_MSGBOX_LINE), &r1);
            ScreenToClient(HWindow, (LPPOINT)&r1);
            RECT r2;
            GetClientRect(HWindow, &r2);
            checkLineH = r2.bottom - r1.top + 1;

            DestroyWindow(GetDlgItem(HWindow, IDC_MSGBOX_LINE));
            DestroyWindow(GetDlgItem(HWindow, IDS_MSGBOX_CHECK));
        }
        if (!hintVisible)
            DestroyWindow(GetDlgItem(HWindow, IDS_MSGBOX_HINT));

        // select the desired icon
        LPCWSTR iconID = 0;
        DWORD beepID = MB_OK;

        HICON hIcon;
        if (HOwnIcon != NULL)
        {
            if (flagsType == MSGBOXEX_YESNO || flagsType == MSGBOXEX_YESNOCANCEL)
                beepID = MB_ICONQUESTION;
            else
                beepID = MB_ICONASTERISK;
            hIcon = HOwnIcon;
        }
        else
        {
            switch (flagsIcon)
            {
            // IDI_HAND/IDI_QUESTION/IDI_EXCLAMATION/IDI_INFORMATION are already
            // MAKEINTRESOURCE(...)-encoded pointers (no UNICODE #define in this build, so that
            // resolves through MAKEINTRESOURCEA) - re-wrapping them in MAKEINTRESOURCEW
            // truncated that already-encoded pointer through (WORD) again (C4302). Numerically
            // harmless today only because every one of these IDs fits in 16 bits either way; a
            // plain pointer-type reinterpretation carries the same encoded value directly.
            case MSGBOXEX_ICONHAND:
            {
                iconID = (LPCWSTR)IDI_HAND;
                beepID = MB_ICONHAND;
                break;
            }

            case MSGBOXEX_ICONQUESTION:
            {
                iconID = (LPCWSTR)IDI_QUESTION;
                beepID = MB_ICONQUESTION;
                break;
            }

            case MSGBOXEX_ICONEXCLAMATION:
            {
                iconID = (LPCWSTR)IDI_EXCLAMATION;
                beepID = MB_ICONEXCLAMATION;
                break;
            }

            case MSGBOXEX_ICONINFORMATION:
            {
                iconID = (LPCWSTR)IDI_INFORMATION;
                beepID = MB_ICONASTERISK;
                break;
            }
            }

            hIcon = NULL;
            if (iconID != 0)
                hIcon = SalLoadIcon(NULL, iconID, ICONSIZE_32);
        }

        int iconWidth = 0;
        int topMargin = 0;
        int iconHeight = 0;
        if (hIcon != NULL)
        {
            RECT r;
            GetWindowRect(GetDlgItem(HWindow, IDI_MSGBOX_ICON), &r);
            POINT p;
            p.x = r.left;
            p.y = r.top;
            ScreenToClient(HWindow, &p);
            topMargin = p.y;
            iconHeight = r.bottom - r.top;
            SendDlgItemMessage(HWindow, IDI_MSGBOX_ICON, STM_SETICON, (WPARAM)hIcon, 0);
        }
        else
        {
            RECT r1, r2;
            GetWindowRect(GetDlgItem(HWindow, IDS_MSGBOX_TEXT), &r1);
            POINT p;
            p.x = r1.left;
            p.y = r1.top;
            ScreenToClient(HWindow, &p);
            topMargin = p.y;
            GetWindowRect(GetDlgItem(HWindow, IDI_MSGBOX_ICON), &r2);
            iconWidth = r1.left - r2.left;
            DestroyWindow(GetDlgItem(HWindow, IDI_MSGBOX_ICON));
        }

        // get the desktop rectangle where we will be positioned
        RECT clipRect;
        HWND hParent = Parent;
        if (hParent != NULL)
            hParent = GetTopVisibleParent(hParent);
        MultiMonGetClipRectByWindow(Parent, &clipRect, NULL);

        // measure the text size
        HDC hDC = HANDLES(GetDC(HWindow));
        HFONT hOldFont = (HFONT)SelectObject(hDC, (HFONT)SendMessage(HWindow, WM_GETFONT, 0, 0));
        RECT tR;
        GetClientRect(GetDlgItem(HWindow, IDS_MSGBOX_TEXT), &tR);
        RECT textR = tR;
        RECT tRCheck = tR;
        RECT tRHint = tR;

        // test the average character width of the font
        const wchar_t* FONT_TEST_TEXT = L"ABCDEabcde12345";
        RECT fontR = {0};
        DrawTextW(hDC, FONT_TEST_TEXT, -1, &fontR, DT_CALCRECT | DT_LEFT | DT_SINGLELINE | DT_NOPREFIX);
        int fontCharWidth = (fontR.right - fontR.left) / (int)wcslen(FONT_TEST_TEXT);
        int fontCharHeight = fontR.bottom - fontR.top;

        // maximum width relative to the desktop where the dialog will appear
        int maxTextWidth = (int)((clipRect.right - clipRect.left) / 1.8);
        if (maxTextWidth > fontCharWidth * 90) // since Windows Vista dialogs are narrower again - wrap at 90 average characters
            maxTextWidth = fontCharWidth * 90;

        tR.right = maxTextWidth;
        DrawTextW(hDC, Text.GetW(), -1, &tR, DT_CALCRECT | DT_LEFT | DT_WORDBREAK | DT_EXPANDTABS | DT_NOPREFIX);

        if (tR.right > maxTextWidth)
        {
            // text width exceeds maxTextWidth limit, so we create a new one
            // text into which we will insert hard line breaks
            const wchar_t* newText = DuplicateStrAndInsertEOLsW(Text.GetW(), hDC, maxTextWidth);
            if (newText != NULL)
            {
                tR.right = maxTextWidth;
                DrawTextW(hDC, newText, -1, &tR, DT_CALCRECT | DT_LEFT | DT_WORDBREAK | DT_EXPANDTABS | DT_NOPREFIX);
                SetDlgItemTextW(HWindow, IDS_MSGBOX_TEXT, newText);
                free((void*)newText);
            }
        }
        else
        {
            // decrease the text width until the lines are optimally filled
            // a bit time-consuming, but there's no need to rush
            RECT iterRect = tR;
            int goodRight;
            do
            {
                goodRight = iterRect.right;
                iterRect.right -= 5; // step by five pixels
                DrawTextW(hDC, Text.GetW(), -1, &iterRect, DT_CALCRECT | DT_LEFT | DT_WORDBREAK | DT_EXPANDTABS | DT_NOPREFIX);
                //        TRACE_I("Iter: right="<<iterRect.right);
            } while (iterRect.bottom == tR.bottom && iterRect.right < goodRight && iterRect.right >= 0);
            tR.right = goodRight;
        }

        // if there's a URL below the text, we add it to the text height for simplicity
        RECT urlR = {0};
        if (!URL.empty())
        {
            const wchar_t* urlTextW = !URLText.empty() ? URLText.c_str() : URL.c_str();
            DrawTextW(hDC, urlTextW, -1, &urlR, DT_CALCRECT | DT_LEFT | DT_SINGLELINE | DT_NOPREFIX);
            tR.bottom += urlR.bottom; // URL height
        }

        BOOL multiline = tR.bottom > textR.bottom;
        if (!CheckText.empty())
        {
            DrawTextW(hDC, CheckText.c_str(), -1, &tRCheck, DT_SINGLELINE | DT_CALCRECT | DT_LEFT | DT_EXPANDTABS);
            //tRCheck.right -= (3 * tRCheck.bottom) / 2;
        }
        else
            memset(&tRCheck, 0, sizeof(tRCheck));
        if (hintVisible)
            DrawTextW(hDC, hintLabel, -1, &tRHint, DT_SINGLELINE | DT_CALCRECT | DT_LEFT);
        else
            memset(&tRHint, 0, sizeof(tRHint));
        int hintWidth = tRHint.right - tRHint.left;
        SelectObject(hDC, hOldFont);
        HANDLES(ReleaseDC(HWindow, hDC));

        int deltaY = 0;
        if (tR.bottom > iconHeight)
            deltaY = tR.bottom - iconHeight;
        if (hIcon == NULL)
            deltaY -= 2 * topMargin;
        deltaY += fontCharHeight;

        // assign the buttons
        int btnText[MESSAGEBOX_MAXBUTTONS];
        int btnID[MESSAGEBOX_MAXBUTTONS];
        int btnCount = 0;
        switch (flagsType)
        {
        case MSGBOXEX_OKCANCEL:
        {
            btnText[btnCount] = IDS_BUTTON_OK;
            btnID[btnCount++] = DIALOG_OK;
            btnText[btnCount] = IDS_BUTTON_CANCEL;
            btnID[btnCount++] = DIALOG_CANCEL;
            break;
        }

        case MSGBOXEX_ABORTRETRYIGNORE:
        {
            btnText[btnCount] = IDS_BUTTON_ABORT;
            btnID[btnCount++] = DIALOG_ABORT;
            btnText[btnCount] = IDS_BUTTON_RETRY;
            btnID[btnCount++] = DIALOG_RETRY;
            btnText[btnCount] = IDS_BUTTON_IGNORE;
            btnID[btnCount++] = DIALOG_IGNORE;
            break;
        }

        case MSGBOXEX_YESNOCANCEL:
        {
            btnText[btnCount] = IDS_BUTTON_YES;
            btnID[btnCount++] = DIALOG_YES;
            btnText[btnCount] = IDS_BUTTON_NO;
            btnID[btnCount++] = DIALOG_NO;
            btnText[btnCount] = IDS_BUTTON_CANCEL;
            btnID[btnCount++] = DIALOG_CANCEL;
            break;
        }

        case MSGBOXEX_YESNO:
        {
            btnText[btnCount] = IDS_BUTTON_YES;
            btnID[btnCount++] = DIALOG_YES;
            btnText[btnCount] = IDS_BUTTON_NO;
            btnID[btnCount++] = DIALOG_NO;
            break;
        }

        case MSGBOXEX_RETRYCANCEL:
        {
            btnText[btnCount] = IDS_BUTTON_RETRY;
            btnID[btnCount++] = DIALOG_RETRY;
            btnText[btnCount] = IDS_BUTTON_CANCEL;
            btnID[btnCount++] = DIALOG_CANCEL;
            break;
        }

        case MSGBOXEX_CANCELTRYCONTINUE:
        {
            btnText[btnCount] = IDS_BUTTON_CANCEL;
            btnID[btnCount++] = DIALOG_CANCEL;
            btnText[btnCount] = IDS_BUTTON_TRY;
            btnID[btnCount++] = DIALOG_TRYAGAIN;
            btnText[btnCount] = IDS_BUTTON_CONTINUE;
            btnID[btnCount++] = DIALOG_CONTINUE;
            break;
        }

        case MSGBOXEX_CONTINUEABORT:
        {
            btnText[btnCount] = IDS_BUTTON_CONTINUE;
            btnID[btnCount++] = DIALOG_CONTINUE;
            btnText[btnCount] = IDS_BUTTON_ABORT;
            btnID[btnCount++] = DIALOG_ABORT;
            break;
        }

        case MSGBOXEX_YESNOOKCANCEL:
        {
            btnText[btnCount] = IDS_BUTTON_YES;
            btnID[btnCount++] = DIALOG_YES;
            btnText[btnCount] = IDS_BUTTON_NO;
            btnID[btnCount++] = DIALOG_NO;
            btnText[btnCount] = IDS_BUTTON_OK;
            btnID[btnCount++] = DIALOG_OK;
            btnText[btnCount] = IDS_BUTTON_CANCEL;
            btnID[btnCount++] = DIALOG_CANCEL;
            break;
        }

        default: //case MSGBOXEX_OK:
        {
            // if an unknown flag falls through, we will behave like the MSGBOXEX_OK variant
            if (flagsType != MSGBOXEX_OK)
                TRACE_E("CMessageBox: unknown flags: " << Flags);

            btnText[btnCount] = IDS_BUTTON_OK;
            btnID[btnCount++] = DIALOG_OK;
            break;
        }
        }
        if (flagsMisc & MSGBOXEX_HELP)
        {
            if (flagsType == MSGBOXEX_YESNOOKCANCEL)
            {
                TRACE_E("Invalid flags combination, message box can have only 4 buttons.");
            }
            else
            {
                btnText[btnCount] = IDS_BUTTON_HELP;
                btnID[btnCount++] = IDHELP;
            }
        }

        // detection of the button dimensions and the spacing between buttons
        RECT buttonR1;
        GetWindowRect(GetDlgItem(HWindow, IDC_MSGBOX_1), &buttonR1);
        RECT buttonR2;
        GetWindowRect(GetDlgItem(HWindow, IDC_MSGBOX_2), &buttonR2);
        int btnWidth = buttonR1.right - buttonR1.left;
        int btnHeight = buttonR1.bottom - buttonR1.top;
        int btnMargin = buttonR2.left - buttonR1.right;
        POINT p;
        p.x = buttonR1.left;
        p.y = buttonR1.top;
        ScreenToClient(HWindow, &p);
        int btnY = p.y + deltaY;

        // configure buttons (IDs, text, visibility)
        int origBtnID[MESSAGEBOX_MAXBUTTONS] = {IDC_MSGBOX_1, IDC_MSGBOX_2, IDC_MSGBOX_3, IDC_MSGBOX_4};
        HWND origBtnWnd[MESSAGEBOX_MAXBUTTONS];
        int btnAddedWidth = 0;
        int i;
        for (i = 0; i < MESSAGEBOX_MAXBUTTONS; i++)
        {
            HWND hButton = GetDlgItem(HWindow, origBtnID[i]);
            origBtnWnd[i] = hButton;
            if (i < btnCount)
            {
                // assign ID
                ButtonsID[i] = btnID[i];
                SetWindowLongPtr(hButton, GWLP_ID, btnID[i]);

                // assign text
                BOOL btnTextWasSet = FALSE;
                if (!AliasBtnNames.empty()) // alias button names
                {
                    wchar_t tmpBuff[1000];
                    const wchar_t* seps = L"\t";
                    if (i == 0 && AliasBtnNames.size() > 999)
                        TRACE_E("AliasBtnNames is too long");
                    lstrcpynW(tmpBuff, AliasBtnNames.c_str(), 1000);

                    wchar_t* nextTok = NULL;
                    wchar_t* aliasID = wcstok_s(tmpBuff, seps, &nextTok);
                    wchar_t* aliasName = wcstok_s(NULL, seps, &nextTok);
                    while (aliasID != NULL && aliasName != NULL)
                    {
                        int id = _wtoi(aliasID);
                        if (btnID[i] == id)
                        {
                            btnTextWasSet = TRUE;
                            SetWindowTextW(hButton, aliasName);

                            // measure whether the button needs to be expanded
                            wchar_t btnText2[300];
                            lstrcpynW(btnText2, aliasName, 300);
                            RemoveAmpersands(btnText2);
                            HFONT hFont = (HFONT)SendMessage(hButton, WM_GETFONT, 0, 0);
                            SIZE sz;
                            HDC hDC2 = HANDLES(GetDC(HWindow));
                            HFONT hOldFont2 = (HFONT)SelectObject(hDC2, hFont);
                            GetTextExtentPoint32W(hDC2, btnText2, (int)wcslen(btnText2), &sz);
                            SelectObject(hDC2, hOldFont2);
                            HANDLES(ReleaseDC(HWindow, hDC2));

                            int neededBtnWidth = sz.cx + btnWidth / 2;
                            if (neededBtnWidth > btnWidth)
                            {
                                btnAddedWidth += neededBtnWidth - btnWidth;
                                SetWindowPos(hButton, NULL, 0, 0, neededBtnWidth, btnHeight,
                                             SWP_NOACTIVATE | SWP_NOZORDER | SWP_NOMOVE);
                            }
                            break;
                        }
                        aliasID = wcstok_s(NULL, seps, &nextTok);
                        aliasName = wcstok_s(NULL, seps, &nextTok);
                    }
                }
                if (!btnTextWasSet)
                    SetWindowTextW(hButton, LoadStrW(btnText[i]));

                // request to receive Ctrl+C in the form of WM_COPY
                CKeyForwarderWindow* wnd = new CKeyForwarderWindow(HWindow, btnID[i]);
                if (wnd != NULL && wnd->HWindow == NULL)
                    delete wnd; // attaching failed - manually delete
            }
            else
            {
                ButtonsID[i] = 0;
                // remove unnecessary buttons
                DestroyWindow(hButton);
            }
        }

        // total width of buttons and gaps between them
        int totalBtnWidth = btnWidth * btnCount + btnMargin * (btnCount - 1) + btnAddedWidth;

        // adjust dialog dimensions
        RECT windowR;
        GetWindowRect(HWindow, &windowR);
        int width = windowR.right - windowR.left;
        int height = windowR.bottom - windowR.top - checkLineH;

        RECT clientR;
        GetClientRect(HWindow, &clientR);

        // adjust the dialog width: the text, checkbox and buttons must fit with margins
        int checkAndHint = tRCheck.right;
        if (hintVisible)
            checkAndHint += hintWidth;
        int deltaX = max(tR.right, checkAndHint) - textR.right - iconWidth;

        if (clientR.right + deltaX < totalBtnWidth + 2 * btnBottomMargin)
            deltaX = totalBtnWidth + 2 * btnBottomMargin - clientR.right;

        // reduce the text height
        GetWindowRect(GetDlgItem(HWindow, IDS_MSGBOX_TEXT), &textR);
        p.x = textR.left;
        ScreenToClient(HWindow, &p);
        // text is vertically centered relative to the icon
        p.y = topMargin + (iconHeight - tR.bottom - tR.top) / 2;
        // but it must not extend above it
        if (p.y < topMargin)
            p.y = topMargin;
        SetWindowPos(GetDlgItem(HWindow, IDS_MSGBOX_TEXT), NULL, p.x - iconWidth, p.y,
                     tR.right - tR.left, tR.bottom - tR.top,
                     SWP_NOZORDER);

        if (!URL.empty())
        {
            SetWindowPos(GetDlgItem(HWindow, IDS_MSGBOX_URL), NULL, p.x - iconWidth, p.y + tR.bottom - tR.top - (urlR.bottom - urlR.top),
                         tR.right - tR.left, urlR.bottom - urlR.top, SWP_NOZORDER);
        }

        // the actual dialog
        SetWindowPos(HWindow, NULL, 0, 0, width + deltaX, height + deltaY,
                     SWP_NOZORDER | SWP_NOMOVE);

        GetClientRect(HWindow, &clientR);

        // arrange the buttons
        int x = (clientR.right - totalBtnWidth) / 2;
        if (WindowsVistaAndLater)
            x = clientR.right - totalBtnWidth - btnBottomMargin;
        BackgroundSeparator = btnY - btnBottomMargin + 1;
        for (i = 0; i < btnCount; i++)
        {
            SetWindowPos(origBtnWnd[i], NULL, x, btnY, 0, 0,
                         SWP_NOACTIVATE | SWP_NOZORDER | SWP_NOSIZE);

            RECT buttonRect;
            GetWindowRect(origBtnWnd[i], &buttonRect);
            x += (buttonRect.right - buttonRect.left) + btnMargin;
        }

        int defIndex = 0;
        switch (flagsDef)
        {
        case MSGBOXEX_DEFBUTTON2:
            defIndex = 1;
            break;
        case MSGBOXEX_DEFBUTTON3:
            defIndex = 2;
            break;
        case MSGBOXEX_DEFBUTTON4:
            defIndex = 3;
            break;
        default:
        {
            if (flagsDef != MSGBOXEX_DEFBUTTON1)
                TRACE_E("CMessageBox: uknown flags: " << Flags);
            defIndex = 0;
        }
        }
        SendMessage(HWindow, DM_SETDEFID, btnID[defIndex], 0);
        SetFocus(GetDlgItem(HWindow, btnID[defIndex]));

// remove the following definitions once they are included...
#define BCM_FIRST 0x1600 // Button control messages
#define BCM_SETSHIELD (BCM_FIRST + 0x000C)

        if (WindowsVistaAndLater && (flagsEx & MSGBOXEX_SHIELDONDEFBTN))
        {
            SendMessage(GetDlgItem(HWindow, btnID[defIndex]), BCM_SETSHIELD, 0, TRUE);
        }

        // move the checkbox and line
        if (!CheckText.empty())
        {
            OffsetChildWindow(HWindow, IDC_MSGBOX_LINE, 0, deltaY);
            OffsetChildWindow(HWindow, IDS_MSGBOX_CHECK, 0, deltaY);

            RECT r;
            GetWindowRect(GetDlgItem(HWindow, IDC_MSGBOX_LINE), &r);
            p.x = r.left;
            p.y = r.top;
            ScreenToClient(HWindow, &p);
            SetWindowPos(GetDlgItem(HWindow, IDC_MSGBOX_LINE), NULL, p.x, p.y,
                         clientR.right - 2 * p.x, r.bottom - r.top, SWP_NOZORDER);

            GetWindowRect(GetDlgItem(HWindow, IDS_MSGBOX_CHECK), &r);
            p.x = r.left;
            p.y = r.top;
            ScreenToClient(HWindow, &p);
            int checkBoxHeight = r.bottom - r.top;
            SetWindowPos(GetDlgItem(HWindow, IDS_MSGBOX_CHECK), NULL, p.x, p.y,
                         tRCheck.right + 3 * p.x, r.bottom - r.top, SWP_NOZORDER);

            // forward Ctrl+C as a WM_COPY message
            CKeyForwarderWindow* wnd = new CKeyForwarderWindow(HWindow, IDS_MSGBOX_CHECK);
            if (wnd != NULL && wnd->HWindow == NULL)
                delete wnd; // attaching failed - manually delete

            if (hintVisible)
            {
                GetWindowRect(GetDlgItem(HWindow, IDS_MSGBOX_HINT), &r);
                int hintHeight = r.bottom - r.top;
                SetWindowPos(GetDlgItem(HWindow, IDS_MSGBOX_HINT), NULL,
                             clientR.right - hintWidth - p.x, p.y + (checkBoxHeight - hintHeight) / 2,
                             hintWidth, hintHeight, SWP_NOZORDER);
            }
        }

        //      MessageBox(Parent, Text.Get(), Title, Flags);  // experimental

        // beep - like a proper message box
        if ((Flags & MSGBOXEX_SILENT) == 0)
            MessageBeep(beepID);

        // let it be centered
        CCommonDialog::DialogProc(uMsg, wParam, lParam);

        // on Windows 2000, when launched via a shortcut with MAXIMIZED set
        // the dialog appeared maximized; SC_RESTORE fixes that
        SendMessage(HWindow, WM_SYSCOMMAND, SC_RESTORE, 0);

        if (Flags & MB_TASKMODAL)
            TRACE_E("MB_TASKMODAL flags is not implemented.");

        if ((Flags & MB_SYSTEMMODAL) || (Flags & MB_TOPMOST))
            SetWindowPos(HWindow, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOSIZE | SWP_NOMOVE);

        if (Flags & MSGBOXEX_SETFOREGROUND)
            SetForegroundWindow(HWindow);

        return FALSE; // prevent the system from setting the default keyboard focus
    }

    case WM_ERASEBKGND:
    {
        if (WindowsVistaAndLater)
        {
            HDC hDC = (HDC)wParam;
            RECT r;
            GetClientRect(HWindow, &r);
            RECT rOrig = r;
            int ySeparator = BackgroundSeparator;
            DarkModeColors colors;
            if (DarkMode_GetColors(&colors))
            {
                r.bottom = ySeparator;
                FillRectWithColor(hDC, &r, colors.DialogBackground);
                r = rOrig;
                r.top = ySeparator;
                r.bottom = r.top + 1;
                FillRectWithColor(hDC, &r, colors.Border);
                r = rOrig;
                r.top = ySeparator + 1;
                FillRectWithColor(hDC, &r, colors.DialogBackground);
                return TRUE;
            }
            r.bottom = ySeparator;
            FillRect(hDC, &r, (HBRUSH)(COLOR_WINDOW + 1));
            r = rOrig;
            r.top = ySeparator;
            r.bottom = r.top + 1;
            FillRect(hDC, &r, (HBRUSH)(COLOR_3DLIGHT + 1));
            r = rOrig;
            r.top = ySeparator + 1;
            FillRect(hDC, &r, (HBRUSH)(COLOR_BTNFACE + 1));
            return TRUE;
        }
        else
            break;
    }

    case WM_CTLCOLORSTATIC:
    {
        if (WindowsVistaAndLater)
        {
            HDC hdcStatic = (HDC)wParam;
            HWND hwndStatic = (HWND)lParam;
            int resID = GetWindowLong(hwndStatic, GWL_ID);
            if (resID == IDI_MSGBOX_ICON || resID == IDS_MSGBOX_TEXT || resID == IDS_MSGBOX_URL)
            {
                COLORREF textClr = GetSysColor(COLOR_WINDOWTEXT);
                HBRUSH hDarkBrush = DarkMode_GetDialogCtlColorBrush(uMsg, hdcStatic, hwndStatic);
                if (hDarkBrush != NULL)
                    return (INT_PTR)hDarkBrush;
                SetTextColor(hdcStatic, textClr);
                SetBkColor(hdcStatic, GetSysColor(COLOR_WINDOW));
                return (INT_PTR)(HBRUSH)(COLOR_WINDOW + 1);
            }
            break;
        }
        else
            break;
    }

    case WM_CTLCOLORBTN:
    {
        HBRUSH hDarkBrush = DarkMode_GetDialogCtlColorBrush(uMsg, (HDC)wParam, (HWND)lParam);
        if (hDarkBrush != NULL)
            return (INT_PTR)hDarkBrush;
        break;
    }

    case WM_HELP:
    {
        if (Flags & MSGBOXEX_HELP)
            DialogProc(WM_COMMAND, IDHELP, 0);
        return TRUE;
    }

    case WM_COPY:
    {
        CopyToClipboard();
        return 0;
    }

    case WM_COMMAND:
    {
        switch (LOWORD(wParam))
        {
        case IDHELP:
        {
            HELPINFO hi;
            memset(&hi, 0, sizeof(hi));
            hi.cbSize = sizeof(hi);
            hi.iContextType = HELPINFO_WINDOW;
            hi.dwContextId = HelpID;
            GetCursorPos(&hi.MousePos);
            // if we have a callback, invoke it
            if (HelpCallback != NULL)
                HelpCallback(&hi);
            else
            { // otherwise send WM_HELP
                if (Parent != NULL)
                    SendMessage(Parent, WM_HELP, 0, (LPARAM)&hi);
                else
                    TRACE_E("CMessageBox::DialogProc(): received IDHELP: unable to send WM_HELP to parent (this message-box has no parent)!");
            }
            return TRUE;
        }

        // only these buttons close the dialog (clicks on the checkbox also arrive here)
        case DIALOG_OK:
        case DIALOG_CANCEL:
        case DIALOG_ABORT:
        case DIALOG_RETRY:
        case DIALOG_IGNORE:
        case DIALOG_YES:
        case DIALOG_NO:
        case DIALOG_TRYAGAIN:
        case DIALOG_CONTINUE:
        {
            if (LOWORD(wParam) == DIALOG_CANCEL && !EscapeEnabled()) // block VK_ESCAPE for some button combinations
                return TRUE;

            TransferData(ttDataFromWindow); // force transfer even when DIALOG_CANCEL is used (MSDN: If users select the option and click Cancel, this option does take effect. This setting is a meta-option, so it doesn't follow the standard Cancel behavior of leaving no side effect.)
            if (Modal)
            {
                // message boxes MSGBOXEX_OK and MSGBOXEX_YESNO must not return CANCEL (whereas MSGBOXEX_ABORTRETRYIGNORE does return CANCEL)
                if (LOWORD(wParam) == DIALOG_CANCEL)
                {
                    DWORD flagsType = Flags & MSGBOXEX_TYPEMASK;
                    if (flagsType == MSGBOXEX_OK)
                        wParam = MAKELPARAM(DIALOG_OK, HIWORD(wParam)); // cancel -> ok
                    if (flagsType == MSGBOXEX_YESNO)
                        wParam = MAKELPARAM(DIALOG_NO, HIWORD(wParam)); // cancel -> no
                }
                EndDialog(HWindow, wParam);
            }
            else
            {
                TRACE_E("CMessageBox is not modal!");
                DestroyWindow(HWindow);
            }
            return TRUE;
        }
        }
        break;
    }
    }

    return CCommonDialog::DialogProc(uMsg, wParam, lParam);
}

//****************************************************************************
//
// SalMessageBox / SalMessageBoxEx
//

// The wide entry points are the real implementation; the ANSI
// pair below are thin forwarders kept for the frozen plugin ABI (spl_gen.h).
// They relocate into the compat facade.
int SalMessageBoxW(HWND hParent, const wchar_t* lpText, const wchar_t* lpCaption, UINT uType)
{
    CALL_STACK_MESSAGE2("SalMessageBoxW(0x%X)", uType);

    // limitations that SalMessageBox cannot handle
    if (uType & MSGBOXEX_HELP)
    {
        TRACE_E("SalMessageBoxW: use SalMessageBoxEx with MSGBOXEX_HELP flag");
        uType &= ~MSGBOXEX_HELP;
    }

    MSGBOXEX_PARAMS params;
    memset(&params, 0, sizeof(params));
    params.HParent = hParent;
    params.Text = lpText;
    params.Caption = lpCaption;
    params.Flags = uType;
    return SalMessageBoxEx(&params);
}

int SalMessageBoxEx(const MSGBOXEX_PARAMS* params)
{
    return CMessageBox(params->HParent,
                       params->Flags,
                       params->Caption,
                       params->Text,
                       params->CheckBoxText,
                       params->CheckBoxValue,
                       params->HIcon,
                       params->ContextHelpId,
                       params->HelpCallback,
                       params->AliasBtnNames,
                       params->URL,
                       params->URLText)
        .Execute();
}

// Narrow message-box ownership is intentionally absent from core. Compatibility conversion
// belongs only to the frozen v107 bridge.
