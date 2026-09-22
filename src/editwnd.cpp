// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"

#include "cfgdlg.h"
#include "mainwnd.h"
#include "ui/IPrompter.h"
#include "ui/UnicodeHistoryUtils.h" // AddValueToWideHistory
#include "common/unicode/PanelPathPolicy.h" // EffectiveItemNameW
#include "plugins.h"
#include "fileswnd.h"
#include "editwnd.h"
#include "stswnd.h"
#include "darkmode.h"
#include "common/CommandShellService.h"
#include "common/DiagnosticTextEncoding.h"
#include "common/clipboard/ClipboardTextPayload.h"
#include "common/clipboard/HDropSelection.h"
#include "common/fsutil.h"
#include "common/PathDisplayUtils.h" // MakeCompactPathBuffer
#include "common/unicode/helpers.h"
#include "common/unicode/WideVariableExpansion.h"
#include "salshlib.h"
#include <uxtheme.h>

#include <shlwapi.h>

namespace
{
const COLORREF EDITWND_DARK_BG = RGB(45, 45, 48);
const COLORREF EDITWND_DARK_INPUT_BG = RGB(30, 30, 30);
const COLORREF EDITWND_DARK_TEXT = RGB(232, 232, 232);
const COLORREF EDITWND_DARK_DISABLED_TEXT = RGB(140, 140, 140);
const COLORREF EDITWND_DARK_BORDER_OUTER = RGB(45, 45, 48);
const COLORREF EDITWND_DARK_BORDER_INNER = RGB(62, 62, 66);
const COLORREF EDITWND_DARK_BUTTON_BG = RGB(52, 52, 56);

static void FillRectSolid(HDC hDC, const RECT* rect, COLORREF color)
{
    HGDIOBJ oldBrush = SelectObject(hDC, GetStockObject(DC_BRUSH));
    COLORREF oldColor = SetDCBrushColor(hDC, color);
    FillRect(hDC, rect, (HBRUSH)GetStockObject(DC_BRUSH));
    SetDCBrushColor(hDC, oldColor);
    SelectObject(hDC, oldBrush);
}

static void DrawDarkComboFrame(HWND hwnd, HDC hDC)
{
    RECT r;
    GetWindowRect(hwnd, &r);
    OffsetRect(&r, -r.left, -r.top);

    HGDIOBJ oldPen = SelectObject(hDC, GetStockObject(DC_PEN));
    HGDIOBJ oldBrush = SelectObject(hDC, GetStockObject(NULL_BRUSH));

    SetDCPenColor(hDC, EDITWND_DARK_BORDER_OUTER);
    Rectangle(hDC, r.left, r.top, r.right, r.bottom);

    if (r.right - r.left > 3 && r.bottom - r.top > 3)
    {
        SetDCPenColor(hDC, EDITWND_DARK_BORDER_INNER);
        Rectangle(hDC, r.left + 1, r.top + 1, r.right - 1, r.bottom - 1);
    }

    SelectObject(hDC, oldBrush);
    SelectObject(hDC, oldPen);
}

static void ExcludeChildWindowFromClip(HWND parent, HWND child, HDC hDC)
{
    if (child == NULL || !IsWindow(child))
        return;

    RECT r;
    GetWindowRect(child, &r);
    MapWindowPoints(NULL, parent, (POINT*)&r, 2);
    ExcludeClipRect(hDC, r.left, r.top, r.right, r.bottom);
}

static void RedrawChildWindowNow(HWND child)
{
    if (child != NULL && IsWindow(child))
        RedrawWindow(child, NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_UPDATENOW);
}

} // namespace

//*****************************************************************************
//
// InstallWordBreakProc
//
// ensures the cursor stops at backslashes in paths
//
// Implementation hack to work around stupid Windows behavior.
// Extracted and rewritten in C from IE 5.5 / BROWSEUI.DLL.
//

BOOL IsCharacterDelimiter(char ch)
{
    return ch == ' ' || ch == '/' || ch == '\\' || ch == ';' || ch == ',' || ch == '.';
}

// The same delimiter set, which is entirely ASCII - so the wide form is a
// direct transliteration and cannot disagree with the narrow one about any character.
BOOL IsCharacterDelimiterW(wchar_t ch)
{
    return ch == L' ' || ch == L'/' || ch == L'\\' || ch == L';' || ch == L',' || ch == L'.';
}

// The narrow-native half of the EM_SETWORDBREAKPROC A/W pair (see EditWordBreakCoreW's
// comment above EditWordBreakProcUNICODE) - fixed to char*/CharPrevA/CharNextA rather than
// LPWSTR/CharPrev/CharNext so its width never depends on this project's own UNICODE define;
// this is the proc installed pre-common-controls-6, which always hands narrow text.
int CALLBACK
EditWordBreakProc(char* text, int current, int textLen, int code)
{
    CALL_STACK_MESSAGE5("EditWordBreakProc(%s, %d, %d, %d)", text, current, textLen, code);
    if (textLen == 0)
        return 0;
    static BOOL gRightBreak = FALSE;
    BOOL ebp_8 = FALSE;
    char* ebp_10 = NULL;
    char* esi = text + current;
    switch (code)
    {
    case WB_LEFT:
    {
        do
        {
            esi = CharPrevA(text, esi);
            if (esi == text)
                break;
            if (!IsCharacterDelimiter(*esi))
            {
                gRightBreak = FALSE;
                ebp_8 = TRUE;
                continue;
            }
            if (gRightBreak)
                break;
            if (ebp_8)
                break;
        } while (1);
        if (esi - text <= 0)
            return 0;
        if (esi - text >= textLen)
            return (int)(esi - text);
        return (int)(esi - text + 1);
    }

    case WB_RIGHT:
    {
        gRightBreak = FALSE;
        BOOL edi = !IsCharacterDelimiter(*esi);
        ebp_10 = text + textLen;
        if (esi == ebp_10)
            return (int)(esi - text);
        do
        {
            esi = CharNextA(esi);
            if (esi == ebp_10)
                return (int)(esi - text);

            if (IsCharacterDelimiter(*esi))
                edi = FALSE;
            else if (!edi)
                return (int)(esi - text);
        } while (1);
        return 0;
    }

    case WB_ISDELIMITER:
    {
        gRightBreak = TRUE;
        return IsCharacterDelimiter(text[current]);
    }
    }
    return textLen;
}

// The wide-native core. EM_SETWORDBREAKPROC's signature is fixed by Win32 as
// int(CALLBACK*)(LPWSTR, int, int, int), so the LPWSTR lives in the shim below and
// everything real happens here, in wchar_t, with no cast at any call site.
int EditWordBreakCoreW(const wchar_t* wtext, int current, int textLen, int code)
{
    // This is installed on common controls 6+, where USER32 hands the callback
    // UTF-16 text and expects a UTF-16 CHARACTER index back. It used to immediately do
    // WideCharToMultiByte(CP_ACP, ...) into a char[10000], walk the mirror, and return an
    // index into THAT - a narrow index used by the caller as a wide one.
    //
    // Be precise about when that actually breaks, because the obvious answer is wrong and
    // was measured wrong here first: on a SINGLE-BYTE ACP the two index spaces coincide,
    // including across non-BMP characters - WideCharToMultiByte substitutes the default
    // character per wchar_t, so a surrogate pair becomes "??" rather than "?" and the
    // lengths still match. The divergence is real on a DBCS ACP (932/936/949/950), where
    // one wchar_t narrows to two bytes and every index past the first multi-byte character
    // is wrong; Ctrl+Backspace then selects and deletes the wrong range. So this was a
    // LATENT bug on Western installs and a live one on CJK ones.
    //
    // Now native: same algorithm, wide throughout, no mirror. The delimiter set is ASCII,
    // so this cannot disagree with the narrow proc about any character it also sees.
    CALL_STACK_MESSAGE4("EditWordBreakCoreW(, %d, %d, %d)", current, textLen, code);
    if (textLen == 0)
        return 0;

    static BOOL gRightBreak = FALSE;
    BOOL sawNonDelimiter = FALSE;
    const wchar_t* esi = wtext + current;
    switch (code)
    {
    case WB_LEFT:
    {
        do
        {
            esi = CharPrevW(wtext, esi);
            if (esi == wtext)
                break;
            if (!IsCharacterDelimiterW(*esi))
            {
                gRightBreak = FALSE;
                sawNonDelimiter = TRUE;
                continue;
            }
            if (gRightBreak)
                break;
            if (sawNonDelimiter)
                break;
        } while (1);
        if (esi - wtext <= 0)
            return 0;
        if (esi - wtext >= textLen)
            return (int)(esi - wtext);
        return (int)(esi - wtext + 1);
    }

    case WB_RIGHT:
    {
        gRightBreak = FALSE;
        BOOL inWord = !IsCharacterDelimiterW(*esi);
        const wchar_t* endPtr = wtext + textLen;
        if (esi == endPtr)
            return (int)(esi - wtext);
        do
        {
            esi = CharNextW(esi);
            if (esi == endPtr)
                return (int)(esi - wtext);

            if (IsCharacterDelimiterW(*esi))
                inWord = FALSE;
            else if (!inWord)
                return (int)(esi - wtext);
        } while (1);
        return 0;
    }

    case WB_ISDELIMITER:
    {
        gRightBreak = TRUE;
        return IsCharacterDelimiterW(wtext[current]);
    }
    }
    return textLen;
}

int CALLBACK
EditWordBreakProcUNICODE(LPWSTR text, int current, int textLen, int code)
{
    return EditWordBreakCoreW((const wchar_t*)text, current, textLen, code);
}

const char* BACKSPACE_SUBCLASSPROC = "SALBSSubClass";
int CALLBACK EditWordBreakProc(char* text, int current, int textLen, int code);

LRESULT CALLBACK
BSHandlerSubclassProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    // BACKSPACE_SUBCLASSPROC is a fixed internal narrow identifier, not user-facing text -
    // explicit GetPropA/RemovePropA/SetPropA rather than the wchar_t macros, which would
    // otherwise resolve to the W forms under the msvc-unicode-canary probe.
    WNDPROC OldWndProc = (WNDPROC)GetPropA(hwnd, BACKSPACE_SUBCLASSPROC);
    if (OldWndProc == NULL)
    {
        TRACE_E("BSHandlerSubclassProc: OldWndProc == NULL");
        return 0;
    }
    switch (message)
    {
    case WM_CHAR:
    {
        if (wParam == 127) // discard the "Ctrl+Backspace" character
            return 0;      // we handled it
        break;
    }

    case WM_KEYDOWN:
    {
        // handles Ctrl+Backspace for deleting a word
        if (wParam == VK_BACK)
        {
            BOOL controlPressed = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
            BOOL altPressed = (GetKeyState(VK_MENU) & 0x8000) != 0;
            BOOL shiftPressed = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
            if (controlPressed && !altPressed && !shiftPressed)
            {
                int iStart, iEnd;
                SendMessage(hwnd, EM_GETSEL, (WPARAM)&iStart, (LPARAM)&iEnd);

                // if a selection exists, cancel it and move the cursor to the end
                if (iStart != iEnd)
                {
                    SendMessage(hwnd, EM_SETSEL, iEnd, iEnd);
                    iStart = iEnd;
                }
                //          if (iStart == iEnd) // nothing can't be selected
                //          {
                // iStart/iEnd come from EM_GETSEL as CHARACTER indices. This
                // used to read the text narrow and hand the mirror to the narrow word-break
                // proc, so the boundary it computed indexed a different string from the one
                // EM_SETSEL then selects - Ctrl+Backspace deleted the wrong range. Read wide
                // and use the wide proc, so index space is the same on both sides.
                wchar_t buffW[10000];
                int len = GetWindowTextLengthW(hwnd);
                if (len >= 10000 - 1)
                    break;
                buffW[0] = 0;
                GetWindowTextW(hwnd, buffW, 10000);

                // delete the word
                iStart = EditWordBreakCoreW(buffW, iStart, iStart + 1, WB_LEFT);
                SendMessage(hwnd, EM_SETSEL, iStart, iEnd);
                SendMessageW(hwnd, EM_REPLACESEL, TRUE, (LPARAM)L"");
                //          }
                return 0; // we handled it
            }
        }
        break;
    }

    case WM_DESTROY:
    {
        // clean up the stored OldWndProc
        // W forms, matching AttachBackspaceHandler's install.
        WNDPROC currentWndProc = (WNDPROC)GetWindowLongPtrW(hwnd, GWLP_WNDPROC);
        SetWindowLongPtrW(hwnd, GWLP_WNDPROC, (LONG_PTR)OldWndProc);

        RemovePropA(hwnd, BACKSPACE_SUBCLASSPROC);
        break;
    }
    }
    // CallWindowProcW, not the A form: this proc is installed with
    // SetWindowLongPtrW, so the message it received is wide. Forwarding it through
    // the ANSI variant would convert the very text this subclass exists to preserve.
    return CallWindowProcW(OldWndProc, hwnd, message, wParam, lParam);
}

// we don't use WinLib's subclass so we don't step on its toes
// (some windows we need to attach may already be or will be under WinLib)
// Subclass with the WIDE form.
//
// This is the second subclass that lands on a combo's inner EDIT (the first is
// CKeyForwarder, via CWindow::AttachToWindow). Installing with the ANSI
// SetWindowLongPtr converts a Unicode control into an ANSI one, and thereafter
// every SendMessageW / GetWindowTextW on that edit round-trips through CP_ACP -
// the damage the "Unicode overlay" in dialogs_file_transforms.cpp exists to dodge.
//
// The two subclasses CHAIN on the same control, so both must be wide; one narrow
// link is enough to narrow the text passing through it.
//
// Safe unconditionally: BSHandlerSubclassProc's only character test is
// wParam == 127 (the Ctrl+Backspace DEL), a control code identical under A and W,
// and its text handling is already wide (GetWindowTextW + SendMessageW
// EM_REPLACESEL) precisely so the indices it computes match the string EM_SETSEL
// then acts on.
BOOL AttachBackspaceHandler(HWND hwndEdit)
{
    WNDPROC oldWndProc = (WNDPROC)GetWindowLongPtrW(hwndEdit, GWLP_WNDPROC);
    if (SetPropA(hwndEdit, BACKSPACE_SUBCLASSPROC, (HANDLE)oldWndProc))
    {
        SetWindowLongPtrW(hwndEdit, GWLP_WNDPROC, (LONG_PTR)BSHandlerSubclassProc);
        return TRUE;
    }
    return FALSE;
}

BOOL InstallWordBreakProc(HWND hWindow)
{
    CALL_STACK_MESSAGE2("InstallWordBreakProc(0x%p)", hWindow);

    if (hWindow == NULL)
    {
        TRACE_E("InstallWordBreakProc: hWindow == NULL");
        return FALSE;
    }

    wchar_t className[31];
    className[0] = 0;
    if (GetClassNameW(hWindow, className, 30) == 0 || StrICmpW(className, L"edit") != 0)
    {
        // might be a combobox, so try grabbing its internal edit control
        hWindow = GetWindow(hWindow, GW_CHILD);
        if (hWindow == NULL || GetClassNameW(hWindow, className, 30) == 0 || StrICmpW(className, L"edit") != 0)
        {
            TRACE_EW(L"InstallWordBreakProc: edit window was not found ClassName is " << className);
            return FALSE;
        }
    }
    // Under Windows XP and .NET with common controls 6, EditWordBreakProc receives UNICODE text
    if (CCVerMajor >= 6)
        SendMessage(hWindow, EM_SETWORDBREAKPROC, NULL, (LPARAM)EditWordBreakProcUNICODE);
    else
        SendMessage(hWindow, EM_SETWORDBREAKPROC, NULL, (LPARAM)EditWordBreakProc);

    // enable Ctrl+Backspace deletion by words
    if (!AttachBackspaceHandler(hWindow))
        TRACE_E("AttachBackspaceHandler on hWnd=0x" << hWindow);

    return TRUE;
}

// returns TRUE if this is the "cd *" command
BOOL IsChangeDirAttempt(const wchar_t* text)
{
    while (*text == L' ')
        text++;
    return _wcsnicmp(text, L"cd ", 3) == 0;
}

int GetCmdLineLimit()
{
    /*
  Measured limits when launching via COMSPEC:
    (4094 + length of the exe string)  W2K (not dependent on COMSPEC length)
    (8190 + length of the exe string)  XP (not dependent on COMSPEC length)
    8156                             Vista + Win7 with COMSPEC=C:\Windows\system32\cmd.exe (depends on COMSPEC lenght: longer COMSPEC = smaller limit)
*/

#if SALCMDLINE_MAXLEN != 8192 // maximum value that GetCmdLineLimit() can return
#pragma message(__FILE__ " ERROR: SALCMDLINE_MAXLEN != 8192. SALCMDLINE_MAXLEN and GetCmdLineLimit() must contain the same maximal value!")
#endif

    if (WindowsXP64AndLater) // XP64 + Vista + Win7 + ...
    {
        CommandShellRequest request;
        CommandShellPolicyResult policy = gCommandShellService != NULL
                                              ? gCommandShellService->GetPolicyInfo(request)
                                              : CommandShellPolicyResult::Error(ERROR_INVALID_PARAMETER);
        int comspecLen = policy.success ? (int)policy.info.quotedComspec.length() : 0;
        return 8191 - comspecLen - 6; // 6 = strlen(" /K ") + 2 (two quotation marks around the command itself)
    }
    else
        return 8192; // XP
}

//
// ****************************************************************************
// CEditLine
//

CEditLine::CEditLine()
    : CWindow(ooStatic) // see CEditWindow::Create
{
    SkipCharacter = FALSE;
    SelChangeDisabled = FALSE;
}

// Wide insertion. The command line is a Unicode window with a wide execute
// path, so text inserted here is the last place a '?' can still enter a command.
void CEditLine::InsertTextW(const wchar_t* s)
{
    SendMessageW(HWindow, EM_REPLACESEL, TRUE, (LPARAM)s);
}

BOOL SkipNextSysCharacter = FALSE;

LRESULT
CEditLine::WindowProc(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    SLOW_CALL_STACK_MESSAGE4("CEditLine::WindowProc(0x%X, 0x%IX, 0x%IX)", uMsg, wParam, lParam);
    switch (uMsg)
    {
    case WM_CHAR:
    {
        if (MainWindow->HasLockedUI())
            return 0;
        if (MainWindow->EditWindow->Dropped())
            break;

        if (SkipCharacter)
            return 0;
        switch ((wchar_t)wParam)
        {
        case '\t': // change panel
        {
            MainWindow->ChangePanel();
            return 0;
        }

        case '\r':
        {
            if (SendMessage(HWindow, WM_GETTEXTLENGTH, 0, 0) == 0)
                MainWindow->GetActivePanel()->CtrlPageDnOrEnter(VK_RETURN);
            else
            {
                const int commandLength = GetWindowTextLengthW(HWindow);
                std::wstring cmdLineW(static_cast<size_t>(commandLength) + 1, L'\0');
                const int copiedCommandLength =
                    GetWindowTextW(HWindow, cmdLineW.data(), commandLength + 1);
                cmdLineW.resize(static_cast<size_t>((std::max)(copiedCommandLength, 0)));

                MainWindow->SetDefaultDirectories();

                std::wstring command;
                int selFrom = 0;
                int selTo = 0;

                BOOL executed = FALSE;
                CFilesWindow* panel = MainWindow->GetActivePanel();
                if (panel->Is(ptDisk)) // running commands on disk -> executed in DOS Prompt
                {
                    // users coming from TC and other file managers tend to change the panel path via the command line
                    // we'll try to break this habit
                    if (IsChangeDirAttempt(cmdLineW.c_str()))
                    {
                        if (Configuration.CnfrmChangeDirTC)
                        {
                            bool dontShow = !Configuration.CnfrmChangeDirTC;
                            gPrompter->ShowInfoWithCheckbox(LoadStrW(IDS_INFOTITLE), LoadStrW(IDS_CHANGEDIR_TC_HINT),
                                                            LoadStrW(IDS_DONTSHOWAGAIN2), &dontShow);
                            Configuration.CnfrmChangeDirTC = !dontShow;
                        }
                        // let the command fall through to the shell so the message box text stays simple
                    }

                    CommandShellRequest policyRequest;
                    CommandShellPolicyResult policy = gCommandShellService != NULL
                                                          ? gCommandShellService->GetPolicyInfo(policyRequest)
                                                          : CommandShellPolicyResult::Error(ERROR_INVALID_PARAMETER);
                    const wchar_t* shellPolicyName = policy.success ? policy.info.executableNameForPolicy.c_str() : L"";

                    if (SystemPolicies.GetMyRunRestricted() &&
                        (!SystemPolicies.GetMyCanRun(shellPolicyName) || !SystemPolicies.GetMyCanRun(cmdLineW.c_str())))
                    {
                        gPrompter->ShowErrorWithHelp(LoadStrW(IDS_POLICIESRESTRICTION_TITLE), LoadStrW(IDS_POLICIESRESTRICTION), IDH_GROUPPOLICY);
                        return 0;
                    }

                    panel->UserWorkedOnThisPath = TRUE;

                    BOOL setWait = (GetCursor() != LoadCursor(NULL, IDC_WAIT)); // is it already waiting?
                    HCURSOR oldCur;
                    if (setWait)
                        oldCur = SetCursor(LoadCursor(NULL, IDC_WAIT));

                    CommandShellRequest request;
                    request.keepOpen = !((Configuration.CloseShell != 0) ^ ((GetKeyState(VK_MENU) & 0x8000) != 0));
                    request.workingDirectory = panel->GetPathW();
                    request.windowTitle = LoadStrW(IDS_COMMANDSHELL);
                    request.useShowWindow = true;
                    request.showWindow = SW_SHOWNORMAL;
                    POINT p;
                    if (MultiMonGetDefaultWindowPos(MainWindow->HWindow, &p))
                    {
                        // if the main window is on another monitor, we should open
                        // the created window there as well, ideally at the default position (same as on the primary)
                        request.usePosition = true;
                        request.x = p.x;
                        request.y = p.y;
                    }

                    // The service has always taken a wide command. The former narrow mirror
                    // round trip lost characters before launch; workingDirectory above was
                    // already GetPathW(), so the command was the last narrow link in the chain.
                    request.command = cmdLineW;

                    CommandShellResult result = gCommandShellService != NULL
                                                    ? gCommandShellService->LaunchCommand(request)
                                                    : CommandShellResult::Error(ERROR_INVALID_PARAMETER);
                    if (!result.success)
                    {
                        gPrompter->ShowError(LoadStrW(IDS_ERROREXECCMDLINE),
                                             result.errorCode == ERROR_FILENAME_EXCED_RANGE ? LoadStrW(IDS_TOOLONGPATH) : GetErrorTextOwned(result.errorCode).c_str());
                    }
                    else
                    {
                        result.CloseProcess();
                        executed = TRUE;
                    }
                    if (setWait)
                        SetCursor(oldCur);
                }
                else
                {
                    if (panel->Is(ptPluginFS) && panel->GetPluginFS()->NotEmpty() &&
                        panel->GetPluginFS()->IsServiceSupported(FS_SERVICE_COMMANDLINE))
                    { // executing commands from FS
                        command = cmdLineW;

                        panel->UserWorkedOnThisPath = TRUE;

                        // lower the thread priority to "normal" (so the operations do not burden the machine too much)
                        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_NORMAL);

                        if (panel->GetPluginFS()->ExecuteCommandLine(HWindow, command, selFrom, selTo))
                        {
                            executed = TRUE;
                        }

                        // increase the thread priority again once the operation has finished
                        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);
                    }
                }

                // also add the command to history
                if (executed)
                {
                    if (Configuration.EnableCmdLineHistory)
                    {
                        AddValueToWideHistory(Configuration.EditHistory, EDIT_HISTORY_SIZE,
                                              cmdLineW.c_str(), TRUE /*caseSensitiveValue*/);
                    }
                    MainWindow->EditWindow->FillHistory();

                    int l = static_cast<int>((std::min)(
                        command.size(), static_cast<size_t>((std::numeric_limits<int>::max)())));
                    if (selFrom < 0)
                        selFrom = 0;
                    if (selFrom > l)
                        selFrom = l;
                    if (selTo < 0)
                        selTo = 0;
                    if (selTo > l)
                        selTo = l;
                    SendMessageW(HWindow, WM_SETTEXT, 0, (LPARAM)command.c_str());
                    SendMessage(HWindow, EM_SETSEL, selFrom, selTo);
                }
            }
            return 0;
        }
        }
        break;
    }

    case WM_COPY:
    case WM_CUT:
    case WM_DESTROYCLIPBOARD:
    {
        // clipboard changed -> trigger recalculation of clipboard function enablers
        IdleRefreshStates = TRUE;  // force a status variable check on the next Idle
        IdleCheckClipboard = TRUE; // request clipboard check as well
        break;
    }

    case WM_KILLFOCUS:
    {
        SelChangeDisabled = TRUE;
        LRESULT res = CWindow::WindowProc(uMsg, wParam, lParam);
        SelChangeDisabled = FALSE;
        return res;
    }

    case WM_SETFOCUS:
    {
        SelChangeDisabled = TRUE;
        LRESULT res = CWindow::WindowProc(uMsg, wParam, lParam);
        SelChangeDisabled = FALSE;
        return res;
    }

    case EM_SETSEL:
    {
        if (SelChangeDisabled)
            return 0;
        break;
    }

    case WM_LBUTTONDOWN:
    case WM_RBUTTONDOWN:
    case WM_MBUTTONDOWN:
    case WM_LBUTTONDBLCLK:
    case WM_LBUTTONUP:
    case WM_RBUTTONUP:
    case WM_MBUTTONUP:
    case WM_SETCURSOR:
    {
        if (MainWindow->HasLockedUI())
            return 0;
        break;
    }

    case WM_MOUSEHWHEEL:
    case WM_MOUSEWHEEL:
    {
        if (MainWindow->HasLockedUI())
            return 0;
        // 7.10.2009 - AS253_B1_IB34: Manison reported that horizontal scrolling didn't work under Windows Vista.
        // It worked for me (via this method). After installing IntelliPoint drivers v7 (previously I had no special drivers
        // on Vista x64) WM_MOUSEHWHEEL messages stopped passing through the hook and flowed directly
        // to the focused window; I disabled the hook and now we must catch the messages in windows that may get focus
        // so the forwarding continues.
        // 30.11.2012 - someone on the forum reported that WM_MOUSEHWHEEL no longer flows through the message hook (the same as
        // Manison previously had with WM_MOUSEHWHEEL): https://forum.altap.cz/viewtopic.php?f=24&t=6039
        // so now we also catch the message in each window where it can flow to (depends on focus)
        // and then route it so it reaches the window under the cursor as we've always done

        // if the message arrived "recently" through the other channel, ignore this one
        if (MouseWheelMSGThroughHook && MouseWheelMSGTime != 0 && (GetTickCount() - MouseWheelMSGTime < MOUSEWHEELMSG_VALID))
            return 0;
        MouseWheelMSGThroughHook = FALSE;
        MouseWheelMSGTime = GetTickCount();

        MSG msg;
        DWORD pos = GetMessagePos();
        msg.pt.x = GET_X_LPARAM(pos);
        msg.pt.y = GET_Y_LPARAM(pos);
        msg.lParam = lParam;
        msg.wParam = wParam;
        msg.hwnd = HWindow;
        msg.message = uMsg;
        PostMouseWheelMessage(&msg);
        return 0;
    }

    case WM_USER_MOUSEWHEEL:
    {
        // suppress default processing
        return 0;
    }

    case WM_SYSKEYUP:
    case WM_KEYUP:
    {
        if (MainWindow->HasLockedUI())
            return 0;
        if (MainWindow->EditWindow->Dropped())
            break;
        SkipCharacter = FALSE;
        break;
    }

    case WM_SYSCOMMAND:
    {
        if (MainWindow->HasLockedUI())
            return 0;
        if (MainWindow->EditWindow->Dropped())
            break;
        if (SkipCharacter)
            return 0;
        break;
    }

    case WM_SYSCHAR:
    {
        if (MainWindow->HasLockedUI())
            return 0;
        if (SkipNextSysCharacter)
        {
            SkipNextSysCharacter = FALSE;
            return FALSE;
        }
        return TRUE;
    }

    case WM_SYSKEYDOWN:
    case WM_KEYDOWN:
    {
        if (MainWindow->HasLockedUI())
            return 0;
        SkipCharacter = FALSE;
        BOOL controlPressed = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
        BOOL altPressed = (GetKeyState(VK_MENU) & 0x8000) != 0;
        BOOL shiftPressed = (GetKeyState(VK_SHIFT) & 0x8000) != 0;

        if (!IsWindowEnabled(MainWindow->HWindow))
            return 0;

        // set the panel variable SelectedItems so that selection via Shift+arrows works
        // when the focus is here in the edit line
        CFilesWindow* panel = MainWindow->GetActivePanel();
        BOOL firstPress = (lParam & 0x40000000) == 0;
        // j.r.: Dusek found an issue where UP wasn't paired with DOWN
        // therefore I introduce a check for the first press of the SHIFT key
        if (wParam == VK_SHIFT && firstPress && panel->Dirs->Count + panel->Files->Count > 0)
        {
            panel->SelectItems = !panel->GetSel(panel->FocusedIndex);
        }

        if (MainWindow->EditWindow->Dropped())
            break;
        else
        {
            if (wParam == VK_UP || wParam == VK_DOWN)
            {
                // Alt - let the listbox pop up
                if (!controlPressed && altPressed && !shiftPressed)
                    break;
                // Control - scroll without popping up the listbox
                if (controlPressed && !altPressed && !shiftPressed)
                    break;
            }
        }

        if (altPressed && !controlPressed && !shiftPressed)
        {
            // change panel mode
            if (wParam >= '0' && wParam <= '9')
            {
                int index = (int)(wParam - '0');
                if (index == 0)
                    index = 9;
                else
                    index--;
                if (MainWindow->GetActivePanel()->IsViewTemplateValid(index))
                    MainWindow->GetActivePanel()->SelectViewTemplate(index, TRUE, FALSE);
                SkipNextSysCharacter = TRUE; // prevent a beep
                return TRUE;
            }
        }

        if (controlPressed && !shiftPressed && !altPressed)
        {
            // starting with Windows Vista, SelectAll works natively, so we leave it to them
            if (!WindowsVistaAndLater)
            {
                if (wParam == 'A')
                {
                    SendMessage(HWindow, EM_SETSEL, 0, -1);
                    SkipCharacter = TRUE; // prevent a beep
                    return TRUE;
                }
            }
        }

        /*
      if (shiftPressed && controlPressed && !altPressed)
      {
        if (wParam >= 'A' && wParam <= 'Z')
        {
          SkipCharacter = TRUE;
          MainWindow->HandleCtrlLetter((char)wParam);
          return 0;
        }
      }
*/

        if (wParam >= '0' && wParam <= '9')
        {
            BOOL exit = FALSE;
            // define hot path
            if (shiftPressed && controlPressed && !altPressed)
            {
                MainWindow->GetActivePanel()->SetUnescapedHotPath((char)wParam == '0' ? 9 : (char)wParam - '1');
                if (!Configuration.HotPathAutoConfig)
                    MainWindow->GetActivePanel()->DirectoryLine->FlashText();
                exit = TRUE;
            }

            // go to hot path

            // I cannot type the characters @, L, $, {, [, ], } on the command line in
            // Open Salamander. With my Danish keyboard these characters all require me
            // to press AltGr+<a digit> (= Ctrl+Alt+<digit>), but to Salamander this has a
            // special meaning, which seems to be: "go to hot path in the non-focused
            // panel". I am not interested in this special Alt-Ctrl-functionality in
            // Salamander, I am definitely more interested in being able to type the
            // mentioned characters on the command line.
            if ((controlPressed && !shiftPressed && !altPressed) /* || // Shift+number from the edit line won't work (you need to type '*' and others)
            (Configuration.ShiftForHotPaths && !controlPressed && shiftPressed) */
            )
            {
                MainWindow->GetActivePanel()->GotoHotPath((char)wParam == '0' ? 9 : (char)wParam - '1');
                /*
          if (altPressed)
            MainWindow->GetNonActivePanel()->GotoHotPath((char)wParam == '0' ? 9 : (char)wParam - '1');
          else
            MainWindow->GetActivePanel()->GotoHotPath((char)wParam == '0' ? 9 : (char)wParam - '1');
          */
                exit = TRUE;
            }

            if (exit)
            {
                SkipCharacter = TRUE;
                return 0;
            }
        }

        if (wParam == VK_BACK && (!controlPressed && !altPressed && shiftPressed))
        { // Shift+backspace
            SkipCharacter = TRUE;
            MainWindow->GetActivePanel()->GotoRoot();
            return 0;
        }

        if (controlPressed && !altPressed)
        {
            if (!shiftPressed)
            {
                if (wParam == 0xBF) // Ctrl+'/'
                {
                    SkipCharacter = TRUE;
                    PostMessage(MainWindow->HWindow, WM_COMMAND, CM_DOSSHELL, 0);
                    return 0;
                }

                if (wParam == 0xBB) // Ctrl+'+'
                {
                    SkipCharacter = TRUE;
                    PostMessage(MainWindow->HWindow, WM_COMMAND, CM_ACTIVESELECT, 0);
                    return 0;
                }

                if (wParam == 0xBD) // Ctrl+'-'
                {
                    SkipCharacter = TRUE;
                    PostMessage(MainWindow->HWindow, WM_COMMAND, CM_ACTIVEUNSELECT, 0);
                    return 0;
                }

                if (wParam == VK_BACKSLASH) // Ctrl+'\\'
                {
                    SkipCharacter = TRUE;
                    MainWindow->GetActivePanel()->GotoRoot();
                    return 0;
                }
            }
        }

        switch (wParam)
        {
        case VK_RETURN:
        {
            if (controlPressed && !altPressed) // filename of the selected file to the command line
            {
                SkipCharacter = TRUE;
                std::wstring nameW;
                CFilesWindow* p = MainWindow->GetActivePanel();
                if (p->FocusedIndex >= 0 &&
                    p->FocusedIndex < p->Files->Count + p->Dirs->Count)
                {
                    CFileData* file = (p->FocusedIndex < p->Dirs->Count) ? &p->Dirs->At(p->FocusedIndex) : &p->Files->At(p->FocusedIndex - p->Dirs->Count);
                    // NameW when the name needs it; DosName is 8.3 and ASCII
                    // by definition, so it is faithful as-is.
                    if (shiftPressed) // DOS name
                    {
                        // A DOS 8.3 name is ASCII by construction, so widen it byte-for-byte
                        // rather than through a code-page conversion - there is nothing for
                        // CP_ACP to decide, and saying so keeps the conversion count honest.
                        nameW = (file->DosName == NULL)
                                    ? file->Name
                                    : std::wstring(file->DosName, file->DosName + wcslen(file->DosName));
                    }
                    else
                    {
                        nameW = file->Name;
                    }
                }
                else
                    return 0;
                nameW += L' ';
                InsertTextW(nameW.c_str());
                return 0;
            }
            else
            {
                if (shiftPressed)
                {
                    SkipCharacter = TRUE;
                    MainWindow->GetActivePanel()->CtrlPageDnOrEnter(wParam);
                    return 0;
                }
                else
                {
                    if (altPressed)
                    {
                        SendMessage(HWindow, WM_CHAR, '\r', 0);
                        SkipCharacter = TRUE;
                        return 0;
                    }
                }
            }
            break;
        }

        case VK_INSERT:
        {
            if (!shiftPressed && !controlPressed && altPressed ||
                shiftPressed && !controlPressed && altPressed)
            { // clipboard: (full) name of focused item
                CCopyFocusedNameModeEnum mode;
                if (!shiftPressed && !controlPressed && altPressed)
                    mode = cfnmFull;
                else
                    mode = cfnmShort;
                MainWindow->GetActivePanel()->CopyFocusedNameToClipboard(mode);
                return 0;
            }
            if (!shiftPressed && controlPressed && altPressed)
            { // clipboard: current full path
                MainWindow->GetActivePanel()->CopyCurrentPathToClipboard();
                return 0;
            }
            if (shiftPressed && controlPressed && !altPressed)
            { // clipboard: (full) UNC name of focused item
                MainWindow->GetActivePanel()->CopyFocusedNameToClipboard(cfnmUNC);
                return 0;
            }
            if (controlPressed || shiftPressed)
                break;
            MainWindow->GetActivePanel()->SelectFocusedIndex();
            return 0;
        }

        case VK_ESCAPE:
        case VK_TAB:
        {
            if (wParam == VK_ESCAPE || controlPressed)
            {
                if (wParam == VK_ESCAPE)
                {
                    // people want compatibility with WinCmd, NC, FAR -- delete the contents on Escape
                    SetWindowTextW(HWindow, L"");
                }
                SkipCharacter = TRUE;
                MainWindow->FocusPanel(MainWindow->GetActivePanel());
                return 0;
            }
            break;
        }

        case VK_LBRACKET:
        case VK_RBRACKET:
        case VK_SPACE:
        {
            if (controlPressed && !altPressed)
            {
                SkipCharacter = TRUE;
                // Ctrl+[ / Ctrl+] / Ctrl+Space insert a panel path into the
                // command line. Taken from the CP_ACP mirror they inserted a path that may
                // name nothing - and the command line now runs exactly what it holds.
                const wchar_t* s = NULL;
                switch (wParam)
                {
                case VK_LBRACKET:
                    s = MainWindow->LeftPanel->Is(ptDisk) ? MainWindow->LeftPanel->GetPathW() : NULL;
                    break;
                case VK_RBRACKET:
                    s = MainWindow->RightPanel->Is(ptDisk) ? MainWindow->RightPanel->GetPathW() : NULL;
                    break;
                default:
                    s = MainWindow->GetActivePanel()->Is(ptDisk) ? MainWindow->GetActivePanel()->GetPathW() : NULL;
                    break;
                }
                if (s != NULL)
                {
                    std::wstring pathW(s);
                    if (shiftPressed) // DOS path
                    {
                        std::wstring shortPath = GetShortPathW(s);
                        if (!shortPath.empty())
                            pathW = std::move(shortPath);
                    }
                    SalPathAddBackslashW(pathW);
                    InsertTextW(pathW.c_str());
                }
                return 0;
            }
            break;
        }

        case VK_UP:
        case VK_DOWN:
        case VK_PRIOR:
        case VK_NEXT:
        {
            CFilesWindow* activePanel = MainWindow->GetActivePanel();
            LRESULT dummy;
            activePanel->OnSysKeyDown(WM_SYSKEYDOWN, wParam, lParam, &dummy);
            return 0;
        }
        }

        // give plugins a chance (we do the same in the panels)
        if (Plugins.HandleKeyDown(wParam, lParam, MainWindow->GetActivePanel(), MainWindow->HWindow))
        {
            return 0;
        }

        break;
    }
    }
    return CWindow::WindowProc(uMsg, wParam, lParam);
}

class CEditDropTarget : public IDropTarget
{
private:
    long RefCount;                    // object lifetime
    IDataObject* DataObject;          // IDataObject that entered the drag
    IDataObject* ForbiddenDataObject; // IDataObject we ignore (we are its source)
    BOOL UseUnicode;                  // is Unicode text in DataObject? (otherwise we try ANSI text)
    CEditLine* EditLine;              // edit line we operate on
    int EditWidth;
    int EditHeight;
    // Wide: TextLen is a CHARACTER count used to index the control and to
    // measure the last glyph. On the CP_ACP mirror both the count and the measured
    // width belong to a different string, so the drag insert-mark lands in the wrong
    // place on any command line the code page cannot spell.
    std::wstring TextBuff;
    int TextLen;
    int OldIsertMarkX;

public:
    CEditDropTarget(CEditLine* editLine)
    {
        RefCount = 1;
        DataObject = NULL;
        ForbiddenDataObject = NULL;
        EditLine = editLine;
        OldIsertMarkX = -1;
        UseUnicode = TRUE;
    }

    virtual ~CEditDropTarget()
    {
        if (!TextBuff.empty())
        {
            TRACE_E("~CEditDropTarget(): unexpected situation: TextBuff != NULL");
            TextBuff.clear();
        }
        if (RefCount != 0)
            TRACE_E("Preliminary destruction of object!");
    }

    void DrawInsertMark(int x)
    {
        HDC hDC = HANDLES(GetDC(EditLine->HWindow));
        int oldRop = SetROP2(hDC, R2_NOT);
        MoveToEx(hDC, x, 0, NULL);
        LineTo(hDC, x, EditHeight);
        MoveToEx(hDC, x - 2, 0, NULL);
        LineTo(hDC, x + 3, 0);
        MoveToEx(hDC, x - 2, EditHeight - 1, NULL);
        LineTo(hDC, x + 3, EditHeight - 1);
        SetROP2(hDC, oldRop);
        HANDLES(ReleaseDC(EditLine->HWindow, hDC));
    }

    void SetInsertMark(int x)
    {
        if (x != OldIsertMarkX)
        {
            if (ImageDragging)
                ImageDragShow(FALSE);
            if (OldIsertMarkX != -1)
                DrawInsertMark(OldIsertMarkX); // erase
            if (x != -1)
                DrawInsertMark(x); // draw
            OldIsertMarkX = x;
            if (ImageDragging)
                ImageDragShow(TRUE);
        }
    }

    BOOL HitTest(POINTL pt, BOOL showInsertMark, int* xPos)
    {
        POINT p;
        p.x = pt.x;
        p.y = pt.y;
        ScreenToClient(EditLine->HWindow, &p);
        if (!TextBuff.empty() && p.x >= 0 && p.x <= EditWidth && p.y >= 0 && p.y <= EditHeight)
        {
            // the EM_POSFROMCHAR message is unreliable - work around it
            LRESULT pos = SendMessage(EditLine->HWindow, EM_CHARFROMPOS, 0, MAKELPARAM(p.x, p.y));
            int myPos = *xPos = LOWORD(pos);
            BOOL byPass = FALSE;
            if (TextLen > 0 && myPos == TextLen)
            {
                myPos--;
                byPass = TRUE;
            }
            pos = SendMessage(EditLine->HWindow, EM_POSFROMCHAR, myPos, 0);
            short x = (short)LOWORD(pos);
            if (x < 0)
                x = 0;
            if (x > EditWidth)
                x = EditWidth;
            if (byPass)
            {
                HFONT hFont = (HFONT)SendMessage(EditLine->HWindow, WM_GETFONT, 0, 0);
                HDC hDC = HANDLES(GetDC(EditLine->HWindow));
                HFONT hOldFont = (HFONT)SelectObject(hDC, hFont);
                SIZE sz;
                GetTextExtentPoint32W(hDC, TextBuff.c_str() + TextLen - 1, 1, &sz);
                SelectObject(hDC, hOldFont);
                HANDLES(ReleaseDC(EditLine->HWindow, hDC));
                x += (short)sz.cx;
            }
            if (showInsertMark)
                SetInsertMark(x);
            return TRUE;
        }
        return FALSE;
    }

    // Wide sibling. The drop data is CF_UNICODETEXT - already wide - and the
    // narrow path below only exists for the legacy CF_HDROP branch.
    BOOL InsertTextW(POINTL pt, const wchar_t* text)
    {
        int xPos;
        if (HitTest(pt, FALSE, &xPos))
        {
            SetInsertMark(-1);
            std::wstring buff(text != NULL ? text : L"");
            const wchar_t* start = buff.c_str();
            if ((GetKeyState(VK_MENU) & 0x8000) != 0)
            {
                // we do not want the whole path - trim it
                size_t len = buff.length();
                if (len > 2)
                {
                    if (buff[len - 1] == L'\\')
                    {
                        buff.resize(len - 1);
                        len--;
                    }
                    size_t slash = buff.find_last_of(L'\\');
                    if (slash != std::wstring::npos)
                        start = buff.c_str() + slash + 1;
                }
            }
            if (ImageDragging)
                ImageDragShow(FALSE);
            SendMessage(EditLine->HWindow, EM_SETSEL, xPos, xPos);
            SendMessageW(EditLine->HWindow, EM_REPLACESEL, TRUE, (LPARAM)start);
            UpdateWindow(EditLine->HWindow);
            if (ImageDragging)
                ImageDragShow(TRUE);
            return TRUE;
        }
        return FALSE;
    }

    void SetForbiddenDataObject(IDataObject* forbiddenDataObject)
    {
        ForbiddenDataObject = forbiddenDataObject;
    }

    // Returns the exact directory or file from a single-item data object.
    BOOL GetNameFromDataObject(IDataObject* pDataObject, std::wstring* path = NULL)
    {
        std::wstring exactPath;
        BOOL fakeFormatPresent = FALSE;
        if (GetFakeDataObjectRealPath(pDataObject, exactPath, NULL, &fakeFormatPresent))
        {
            if (path != NULL)
                *path = std::move(exactPath);
            return TRUE;
        }
        if (fakeFormatPresent)
        {
            // Ours, and deliberately carrying no real path - shellsup.cpp leaves it empty for any
            // drag of more than one item. Falling through to CF_HDROP here would insert Sally's
            // internal ...\Temp\DROPFAKE\<id> path into the command line, which names nothing the
            // user dragged and which they might then run.
            return FALSE;
        }

        FORMATETC formatEtc = {};
        formatEtc.cfFormat = CF_HDROP;
        formatEtc.ptd = NULL;
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
            return 0; // must not touch the object, it no longer exists
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

        SetInsertMark(-1);

        // if our panel is also the source, disable paste
        if (DataObject == ForbiddenDataObject)
        {
            *pdwEffect = DROPEFFECT_NONE;
            return S_OK;
        }

        RECT r;
        GetClientRect(EditLine->HWindow, &r);
        EditWidth = r.right;
        EditHeight = r.bottom;

        if (!TextBuff.empty())
        {
            TRACE_E("CEditDropTarget::DragEnter: Unexpected situation: TextBuff != NULL");
            TextBuff.clear();
        }
        TextLen = GetWindowTextLengthW(EditLine->HWindow);
        TextBuff.resize(TextLen + 1);
        TextLen = GetWindowTextW(EditLine->HWindow, TextBuff.data(), TextLen + 1);
        TextBuff.resize(TextLen);

        // check whether there is text on the clipboard
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
        if (textRes == S_OK || GetNameFromDataObject(DataObject))
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
            // if our panel is also the source, disable paste
            if (DataObject == ForbiddenDataObject)
            {
                *pdwEffect = DROPEFFECT_NONE;
                return S_OK;
            }
            // check whether there is text on the clipboard
            FORMATETC formatEtc;
            ZeroMemory(&formatEtc, sizeof(formatEtc));
            formatEtc.cfFormat = UseUnicode ? CF_UNICODETEXT : CF_TEXT;
            formatEtc.dwAspect = DVASPECT_CONTENT;
            formatEtc.lindex = -1;
            formatEtc.tymed = TYMED_HGLOBAL;
            if (DataObject->QueryGetData(&formatEtc) == S_OK || GetNameFromDataObject(DataObject))
            {
                int xPosDummy;
                if (HitTest(pt, TRUE, &xPosDummy))
                {
                    *pdwEffect = DROPEFFECT_COPY;
                    return S_OK;
                }
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
        SetInsertMark(-1);
        TextBuff.clear();
        return S_OK;
    }

    STDMETHOD(Drop)
    (IDataObject* pDataObject, DWORD grfKeyState, POINTL pt,
     DWORD* pdwEffect)
    {
        // attempt to extract text from the DataObject
        FORMATETC formatEtc;
        ZeroMemory(&formatEtc, sizeof(formatEtc));
        formatEtc.cfFormat = UseUnicode ? CF_UNICODETEXT : CF_TEXT;
        formatEtc.dwAspect = DVASPECT_CONTENT;
        formatEtc.lindex = -1;
        formatEtc.tymed = TYMED_HGLOBAL;

        STGMEDIUM stgMedium;
        ZeroMemory(&stgMedium, sizeof(stgMedium));
        stgMedium.tymed = TYMED_HGLOBAL;

        if (ImageDragging)
            ImageDragLeave();
        SetInsertMark(-1);

        if (pDataObject->GetData(&formatEtc, &stgMedium) == S_OK)
        {
            const SIZE_T byteSize = stgMedium.tymed == TYMED_HGLOBAL && stgMedium.hGlobal != NULL
                                        ? GlobalSize(stgMedium.hGlobal)
                                        : 0;
            const void* payload = byteSize != 0 ? HANDLES(GlobalLock(stgMedium.hGlobal)) : NULL;
            if (payload != NULL)
            {
                std::wstring text;
                const DWORD decodeError = UseUnicode
                                              ? sally::clipboard::DecodeUnicodeClipboardPayload(
                                                    payload, byteSize, text)
                                              : sally::clipboard::DecodeAnsiClipboardPayload(
                                                    payload, byteSize, GetACP(), text);
                if (decodeError == ERROR_SUCCESS && !text.empty())
                    InsertTextW(pt, text.c_str());
                HANDLES(GlobalUnlock(stgMedium.hGlobal));
            }
            ReleaseStgMedium(&stgMedium);
        }
        else
        {
            std::wstring path;
            if (GetNameFromDataObject(pDataObject, &path))
            {
                if (!IsPluginFSPath(path.c_str()))
                {
                    if (!path.empty() && path.back() == L'\\')
                        path.pop_back(); // trailing '\\' is not welcomed
                    if (path.length() == 2 && path[0] != L'\\')
                        path.push_back(L'\\'); // a non-UNC root path must end with '\\'
                }
                else
                {
                    PluginFSConvertPathToExternal(path);
                }
                InsertTextW(pt, path.c_str());
            }
        }

        if (DataObject != NULL)
        {
            DataObject->Release();
            DataObject = NULL;
        }
        *pdwEffect = DROPEFFECT_NONE;
        TextBuff.clear();
        return S_OK;
    }
};

void CEditLine::RegisterDragDrop()
{
    CALL_STACK_MESSAGE1("CEditLine::RegisterDragDrop()");
    CEditDropTarget* dropTarget = new CEditDropTarget(this);
    if (dropTarget != NULL)
    {
        if (HANDLES(RegisterDragDrop(HWindow, dropTarget)) != S_OK)
        {
            TRACE_E("RegisterDragDrop error.");
        }
        //    else
        //      IDropTargetPtr = dropTarget;
        dropTarget->Release(); // RegisterDragDrop called AddRef()
    }
}

void CEditLine::RevokeDragDrop()
{
    HANDLES(RevokeDragDrop(HWindow));
}

//
// ****************************************************************************
// CInnerText
//

CInnerText::CInnerText(CEditWindow* editWindow)
    : CWindow(ooStatic)
{
    Width = 0;
    Height = 0;
    EditWindow = editWindow;
}

CInnerText::~CInnerText()
{
}

LRESULT
CInnerText::WindowProc(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    CALL_STACK_MESSAGE4("CInnerText::WindowProc(0x%X, 0x%IX, 0x%IX)", uMsg, wParam, lParam);
    switch (uMsg)
    {
    case WM_SIZE:
    {
        Width = LOWORD(lParam);
        Height = HIWORD(lParam);
        ItemBitmap.Enlarge(Width, Height); // allocation of bitmap in ItemBitmap.HMemDC
        return 0;
    }

    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HANDLES(BeginPaint(HWindow, &ps));

        HDC dc = ItemBitmap.HMemDC;
        BOOL useDark = DarkMode_ShouldUseDark();
        COLORREF bkColor = useDark ? EDITWND_DARK_INPUT_BG
                                   : GetSysColor(EditWindow->Enabled ? COLOR_WINDOW : COLOR_BTNFACE);
        COLORREF txColor = useDark ? (EditWindow->Enabled ? EDITWND_DARK_TEXT : EDITWND_DARK_DISABLED_TEXT)
                                   : GetSysColor(EditWindow->Enabled ? COLOR_WINDOWTEXT : COLOR_BTNSHADOW);
        RECT r;
        GetClientRect(HWindow, &r);
        FillRectSolid(dc, &r, bkColor);

        if (!Message.empty())
        {
            int oldColor = SetTextColor(dc, txColor);
            HFONT oldFont = (HFONT)SelectObject(dc, EnvFont);
            int oldBkMode = SetBkMode(dc, TRANSPARENT);
            r.right -= TXEL_SPACE - 1; // bold fonts make the text overflow - hence this correction

            // PathCompactPath() works better than combining DT_PATH_ELLIPSIS with DT_END_ELLIPSIS (because the last character misbehaves)
            std::vector<wchar_t> buff = MakeCompactPathBuffer(Message);
            PathCompactPathW(dc, buff.data(), r.right - r.left);

            DrawTextW(dc, buff.data(), -1, &r,
                      /*DT_END_ELLIPSIS | DT_PATH_ELLIPSIS | */ DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
            SetBkMode(dc, oldBkMode);
            SetTextColor(dc, oldColor);
            SelectObject(dc, oldFont);
        }

        BitBlt(ps.hdc, 0, 0, Width, Height, dc, 0, 0, SRCCOPY);

        HANDLES(EndPaint(HWindow, &ps));
        return 0;
    }
    }
    return CWindow::WindowProc(uMsg, wParam, lParam);
}

void CInnerText::UpdateControl()
{
    if (HWindow != NULL)
        InvalidateRect(HWindow, NULL, FALSE);
}

BOOL CInnerText::SetText(const wchar_t* txt)
{
    std::wstring newMessage = sally::unicode::BuildCommandLineDirectoryPrefixW(txt != NULL ? txt : L"");
    CALL_STACK_MESSAGE2("CInnerText::SetText(%s)", sally::diagnostic::EncodeAcpLossy(newMessage).c_str());
    if (Message == newMessage)
        return FALSE;
    Message = newMessage;
    return TRUE;
}

int CInnerText::GetNeededWidth()
{
    if (Message.empty())
        return 0;

    HDC dc = HANDLES(GetDC(NULL));
    if (dc != NULL)
    {
        HFONT old = (HFONT)SelectObject(dc, EnvFont);
        SIZE s;
        GetTextExtentPoint32W(dc, Message.c_str(), (int)Message.size(), &s);
        SelectObject(dc, old);
        HANDLES(ReleaseDC(NULL, dc));
        return s.cx + TXEL_SPACE;
    }
    return 0;
}

//
// ****************************************************************************
// CEditWindow
//

CEditWindow::CEditWindow()
    : CWindow(ooStatic) // see Create()
{
    EditLine = new CEditLine();
    Text = new CInnerText(this);
    Enabled = TRUE;
    Tracking = FALSE;
}

CEditWindow::~CEditWindow()
{
    if (EditLine != NULL)
        delete EditLine;
    if (Text != NULL)
        delete Text;
    ResetStoredContent();
}

BOOL CEditWindow::Create(HWND hParent, int childID)
{
    CALL_STACK_MESSAGE2("CEditWindow::Create(, %d)", childID);
    // Created wide, and both CEditWindow and CEditLine are constructed with
    // unicodeWnd=TRUE so winlib subclasses them through SetWindowLongPtrW. All three parts
    // are needed together: the W creation makes USER32 treat the combo (and the edit child
    // CEditLine attaches to) as a Unicode window, and only then does WM_CHAR arrive as
    // UTF-16 - so a character the code page cannot spell can be TYPED into the command
    // line, not merely displayed. Reading it back wide (see the '\r' handler) is what
    // makes it reach the shell intact.
    HWND hWnd = CreateEx(0,
                          L"ComboBox",
                          L"",
                          WS_CHILD | WS_VSCROLL | WS_CLIPSIBLINGS |
                              CBS_AUTOHSCROLL | CBS_HASSTRINGS | CBS_DROPDOWN,
                          0, 0, 0, 0,
                          hParent,
                          (HMENU)(UINT_PTR)childID,
                          HInstance,
                          this);
    if (hWnd != NULL)
    {
        if (EditLine != NULL)
        {
            EditLine->AttachToWindow(GetWindow(HWindow, GW_CHILD));
            EditLine->RegisterDragDrop();
            InstallWordBreakProc(EditLine->HWindow);
        }
        if (Text != NULL)
        {
            if (!Text->Create(L"STATIC",
                              L"",
                              WS_VISIBLE | WS_CHILD | SS_RIGHT | SS_NOPREFIX,
                              0, 0, 0, 0,
                              HWindow,
                              (HMENU)IDC_DIRTEXT,
                              HInstance,
                              Text))
            {
                TRACE_E("Unable to create text control.");
            }
        }
    }
    SetFont();
    SendMessage(HWindow, CB_LIMITTEXT, GetCmdLineLimit(), 0);
    FillHistory();
    EnableWindow(HWindow, Enabled);
    return hWnd != NULL;
}

void CEditWindow::FillHistory()
{
    SendMessage(HWindow, CB_RESETCONTENT, 0, 0);
    if (Configuration.EnableCmdLineHistory)
    {
        // The dropdown is filled from the WIDE history. The combo is a Unicode
        // window (see Create), so SendMessageW hands the strings over verbatim; the ANSI
        // array would have gone through USER32's A->W conversion and shown '?' for every
        // command the code page cannot spell - including ones the user can now type.
        int i;
        for (i = 0; i < EDIT_HISTORY_SIZE; i++)
            if (Configuration.EditHistory[i] != NULL)
                SendMessageW(HWindow, CB_ADDSTRING, 0, (LPARAM)Configuration.EditHistory[i]);
    }
}

BOOL CEditWindow::Dropped()
{
    return (BOOL)SendMessage(HWindow, CB_GETDROPPEDSTATE, 0, 0);
}

void CEditWindow::SetFont()
{
    if (EditLine != NULL && EditLine->HWindow != NULL &&
        Text != NULL && Text->HWindow != NULL)
    {
        SendMessage(HWindow, WM_SETFONT, (WPARAM)EnvFont, 0);
        SendMessage(EditLine->HWindow, EM_SETMARGINS, (WPARAM)EC_LEFTMARGIN | EC_RIGHTMARGIN, 0);

        RECT r;
        GetClientRect(HWindow, &r);
        ResizeChilds(r.right - r.left, r.bottom - r.top, FALSE);
        Text->UpdateControl();
    }
}

void CEditWindow::SetDirectoryW(const wchar_t* dir)
{
    BOOL changed = Text == NULL || Text->SetText(dir);
    if (!changed && !DarkMode_ShouldUseDark())
        return;
    if (HWindow != NULL)
    {
        RECT r;
        GetClientRect(HWindow, &r);
        ResizeChilds(r.right - r.left, r.bottom - r.top, FALSE);
        if (Text != NULL)
            Text->UpdateControl();
        if (DarkMode_ShouldUseDark())
            RedrawWindow(HWindow, NULL, NULL, RDW_INVALIDATE | RDW_UPDATENOW);
        else
            UpdateWindow(HWindow);
        if (Text != NULL)
            RedrawChildWindowNow(Text->HWindow);
        if (EditLine != NULL)
            RedrawChildWindowNow(EditLine->HWindow);
    }
}

#define EL_XBORDER 4
#define EL_YBORDER 1

int CEditWindow::GetNeededHeight()
{
    return 2 * (EL_YBORDER + 3) + EnvFontCharHeight;
}

void CEditWindow::ResizeChilds(int cx, int cy, BOOL repaint)
{
    int textWidth;
    if (Text != NULL && Text->HWindow != NULL)
    {
        textWidth = Text->GetNeededWidth();
        int pulka = (cx - 2 * EL_XBORDER) / 2;
        if (textWidth > pulka)
            textWidth = pulka;
        MoveWindow(Text->HWindow, EL_XBORDER, 4, textWidth,
                   EnvFontCharHeight, repaint);
    }
    else
        textWidth = 0;

    if (EditLine != NULL && EditLine->HWindow != NULL)
        MoveWindow(EditLine->HWindow, EL_XBORDER + textWidth, 4,
                   cx - 2 * EL_XBORDER - textWidth + 1 - GetSystemMetrics(SM_CXVSCROLL),
                   EnvFontCharHeight, repaint);
}

LRESULT
CEditWindow::WindowProc(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    SLOW_CALL_STACK_MESSAGE4("CEditWindow::WindowProc(0x%X, 0x%IX, 0x%IX)", uMsg, wParam, lParam);
    switch (uMsg)
    {
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX:
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN:
    {
        HBRUSH hBrush = DarkMode_GetDialogCtlColorBrush(uMsg, (HDC)wParam, (HWND)lParam);
        if (hBrush != NULL)
            return (LRESULT)hBrush;
        break;
    }

    case WM_SIZE:
    {
        LRESULT result = CWindow::WindowProc(uMsg, wParam, lParam);
        ResizeChilds(LOWORD(lParam), HIWORD(lParam), TRUE);
        return result;
    }

    case WM_NCPAINT:
    {
        if (DarkMode_ShouldUseDark())
        {
            HDC hDC = HANDLES(GetWindowDC(HWindow));
            if (hDC != NULL)
            {
                DrawDarkComboFrame(HWindow, hDC);
                HANDLES(ReleaseDC(HWindow, hDC));
            }
            return 0;
        }
        break;
    }

    case WM_DESTROY:
    {
        if (EditLine != NULL)
        {
            EditLine->RevokeDragDrop();
            EditLine->DetachWindow();
        }
        if (Text != NULL)
            Text->DetachWindow();
        break;
    }

    case WM_LBUTTONDOWN:
    {
        int sbWidth = GetSystemMetrics(SM_CXVSCROLL);
        RECT cr;
        GetClientRect(HWindow, &cr);
        if (!SendMessage(HWindow, CB_GETDROPPEDSTATE, 0, 0) &&
            LOWORD(lParam) > cr.right - 3 - sbWidth &&
            LOWORD(lParam) < cr.right - 2 &&
            HIWORD(lParam) > cr.top + 1 &&
            HIWORD(lParam) < cr.bottom - 2)
        {
            Tracking = TRUE;
        }
        break;
    }

    case WM_CAPTURECHANGED:
    case WM_LBUTTONUP:
    {
        Tracking = FALSE;
        break;
    }

    case WM_PAINT:
    {
        RECT cr;
        GetClientRect(HWindow, &cr);
        BOOL useDark = DarkMode_ShouldUseDark();

        if (useDark)
        {
            PAINTSTRUCT ps;
            HDC hDC = HANDLES(BeginPaint(HWindow, &ps));
            if (hDC != NULL)
            {
                int savedDC = SaveDC(hDC);
                if (savedDC != 0)
                {
                    if (Text != NULL)
                        ExcludeChildWindowFromClip(HWindow, Text->HWindow, hDC);
                    if (EditLine != NULL)
                        ExcludeChildWindowFromClip(HWindow, EditLine->HWindow, hDC);
                }

                FillRectSolid(hDC, &cr, EDITWND_DARK_BG);

                int sbWidth = GetSystemMetrics(SM_CXVSCROLL);
                RECT editArea = cr;
                editArea.left = EL_XBORDER - 1;
                editArea.top = 3;
                editArea.right = cr.right - sbWidth - 1;
                editArea.bottom = cr.bottom - 3;
                FillRectSolid(hDC, &editArea, EDITWND_DARK_INPUT_BG);

                RECT btnArea;
                btnArea.left = cr.right - 2 - sbWidth + 1;
                btnArea.top = 3;
                btnArea.right = cr.right - 3;
                btnArea.bottom = cr.bottom - 3;
                if (btnArea.right > btnArea.left && btnArea.bottom > btnArea.top)
                {
                    FillRectSolid(hDC, &btnArea, EDITWND_DARK_BUTTON_BG);

                    // The default combo WM_PAINT no longer runs in this branch,
                    // so the dropdown arrow glyph must be drawn here.
                    int centerX = (btnArea.left + btnArea.right) / 2;
                    int centerY = (btnArea.top + btnArea.bottom) / 2;
                    POINT arrow[3] = {
                        {centerX - 3, centerY - 1},
                        {centerX + 4, centerY - 1},
                        {centerX, centerY + 3},
                    };
                    HPEN hArrowPen = HANDLES(CreatePen(PS_SOLID, 1, EDITWND_DARK_TEXT));
                    HBRUSH hArrowBrush = HANDLES(CreateSolidBrush(EDITWND_DARK_TEXT));
                    HGDIOBJ oldArrowPen = SelectObject(hDC, hArrowPen);
                    HGDIOBJ oldArrowBrush = SelectObject(hDC, hArrowBrush);
                    Polygon(hDC, arrow, 3);
                    SelectObject(hDC, oldArrowBrush);
                    SelectObject(hDC, oldArrowPen);
                    HANDLES(DeleteObject(hArrowBrush));
                    HANDLES(DeleteObject(hArrowPen));
                }

                HGDIOBJ oldPen = SelectObject(hDC, GetStockObject(DC_PEN));
                HGDIOBJ oldBrush = SelectObject(hDC, GetStockObject(NULL_BRUSH));
                COLORREF oldPenColor = SetDCPenColor(hDC, EDITWND_DARK_BORDER_OUTER);
                Rectangle(hDC, cr.left, cr.top, cr.right, cr.bottom);

                SetDCPenColor(hDC, EDITWND_DARK_BORDER_INNER);
                Rectangle(hDC, editArea.left, editArea.top, editArea.right, editArea.bottom);

                SetDCPenColor(hDC, EDITWND_DARK_BORDER_OUTER);
                MoveToEx(hDC, cr.right - 2 - sbWidth, cr.top + 2, NULL);
                LineTo(hDC, cr.right - 2 - sbWidth, cr.bottom - 2);

                SetDCPenColor(hDC, oldPenColor);
                SelectObject(hDC, oldBrush);
                SelectObject(hDC, oldPen);
                if (savedDC != 0)
                    RestoreDC(hDC, savedDC);
            }
            HANDLES(EndPaint(HWindow, &ps));
            if (Text != NULL)
                RedrawChildWindowNow(Text->HWindow);
            if (EditLine != NULL)
                RedrawChildWindowNow(EditLine->HWindow);
            return 0;
        }

        // ensure the 2-pixel frame around the combo is not cleared during painting
        RECT r;
        r.left = 0;
        r.top = 0;
        r.right = cr.right;
        r.bottom = 2;
        ValidateRect(HWindow, &r);
        r.left = 0;
        r.top = cr.bottom - 2;
        r.right = cr.right;
        r.bottom = cr.bottom;
        ValidateRect(HWindow, &r);
        r.left = 0;
        r.top = 2;
        r.right = 2;
        r.bottom = cr.bottom - 2;
        ValidateRect(HWindow, &r);
        r.left = cr.right - 2;
        r.top = 2;
        r.right = cr.right;
        r.bottom = cr.bottom - 2;
        ValidateRect(HWindow, &r);

        // we will draw our own (the outer gray and the inner sunken)
        HDC hDC = HANDLES(GetDC(HWindow));
        HPEN hOldPen = (HPEN)SelectObject(hDC, BtnFacePen);
        SelectObject(hDC, HANDLES(GetStockObject(NULL_BRUSH)));
        Rectangle(hDC, cr.left, cr.top, cr.right, cr.bottom);
        SelectObject(hDC, BtnShadowPen);
        MoveToEx(hDC, cr.left + 1, cr.bottom - 2, NULL);
        LineTo(hDC, cr.left + 1, cr.top + 1);
        LineTo(hDC, cr.right - 2, cr.top + 1);
        SelectObject(hDC, BtnHilightPen);
        MoveToEx(hDC, cr.right - 2, cr.top + 1, NULL);
        LineTo(hDC, cr.right - 2, cr.bottom - 2);
        LineTo(hDC, cr.left, cr.bottom - 2);

        if (WindowsVistaAndLater)
        {
            // Vista finally fixed the combobox flickering during resize
            // so we must manually clear the area between child windows and the
            // combobox edge, otherwise garbage remains there
            (HPEN) SelectObject(hDC, WndPen);
            Rectangle(hDC, cr.left + EL_XBORDER - 1, cr.top + 4 - 1,
                      cr.right - GetSystemMetrics(SM_CXVSCROLL) - 1, cr.bottom - 4 + 1);
        }

        // if a visual style is active, we won't decorate the button
        if (IsAppThemed())
        {
            SelectObject(hDC, hOldPen);
            HANDLES(ReleaseDC(HWindow, hDC));
            break;
        }

        // ensure the 2-pixel frame around the button is not cleared during painting
        int sbWidth = GetSystemMetrics(SM_CXVSCROLL);
        r.left = cr.right - 2 - sbWidth;
        r.top = 2;
        r.right = cr.right - 2;
        r.bottom = 4;
        ValidateRect(HWindow, &r);
        r.left = cr.right - 2 - sbWidth;
        r.top = cr.bottom - 4;
        r.right = cr.right - 2;
        r.bottom = cr.bottom - 2;
        ValidateRect(HWindow, &r);
        r.left = cr.right - 2 - sbWidth;
        r.top = 2;
        r.right = cr.right - 2 - sbWidth + 2;
        r.bottom = cr.bottom - 2;
        ValidateRect(HWindow, &r);
        r.left = cr.right - 4;
        r.top = 2;
        r.right = cr.right - 2;
        r.bottom = cr.bottom - 2;
        ValidateRect(HWindow, &r);

        // draw a custom frame around the button
        HPEN leftTopPen;
        HPEN bottomRightPen;
        POINT pos;
        GetCursorPos(&pos);
        ScreenToClient(HWindow, &pos);
        if (Tracking && pos.x > cr.right - 3 - sbWidth &&
            pos.x < cr.right - 2 &&
            pos.y > cr.top + 1 &&
            pos.y < cr.bottom - 2)
        {
            // draw a sunken frame
            leftTopPen = BtnShadowPen;
            //bottomRightPen = BtnHilightPen;
            bottomRightPen = BtnShadowPen;
        }
        else
        {
            // draw a normal frame
            leftTopPen = BtnHilightPen;
            bottomRightPen = BtnShadowPen;
        }
        SelectObject(hDC, BtnFacePen);
        SelectObject(hDC, HANDLES(GetStockObject(NULL_BRUSH)));
        Rectangle(hDC, cr.right - 2 - sbWidth + 1, 3, cr.right - 3, cr.bottom - 3);
        SelectObject(hDC, leftTopPen);
        MoveToEx(hDC, cr.right - 2 - sbWidth, cr.bottom - 4, NULL);
        LineTo(hDC, cr.right - 2 - sbWidth, cr.top + 2);
        LineTo(hDC, cr.right - 3, cr.top + 2);
        SelectObject(hDC, bottomRightPen);
        MoveToEx(hDC, cr.right - 3, cr.top + 2, NULL);
        LineTo(hDC, cr.right - 3, cr.bottom - 3);
        LineTo(hDC, cr.right - 2 - sbWidth - 1, cr.bottom - 3);

        SelectObject(hDC, hOldPen);
        HANDLES(ReleaseDC(HWindow, hDC));
        break;
    }
    }
    return CWindow::WindowProc(uMsg, wParam, lParam);
}

BOOL CEditWindow::HideCaret()
{
    if (EditLine->HWindow == NULL)
        return FALSE;
    if (GetFocus() != EditLine->HWindow)
        return FALSE;
    return ::HideCaret(EditLine->HWindow);
}

void CEditWindow::ShowCaret()
{
    if (EditLine->HWindow != NULL)
        ::ShowCaret(EditLine->HWindow);
}

void CEditWindow::Enable(BOOL enable)
{
    if (Enabled != enable)
    {
        Enabled = enable;
        EnableWindow(HWindow, Enabled);
    }
}

void CEditWindow::StoreContent()
{
    if (HWindow == NULL)
        return;
    ResetStoredContent();
    int textLen = GetWindowTextLengthW(EditLine->HWindow);
    if (textLen > 0)
    {
        LastText.resize(textLen + 1);
        GetWindowTextW(EditLine->HWindow, LastText.data(), textLen + 1);
        LastText.resize(textLen);
        SendMessage(EditLine->HWindow, EM_GETSEL, (WPARAM)&LastSelStart, (LPARAM)&LastSelEnd);
    }
}

void CEditWindow::RestoreContent()
{
    if (Enabled && !LastText.empty())
    {
        // if the old window state (contents and selection) was saved, we restore it
        SetWindowTextW(HWindow, LastText.c_str());
        SendMessage(EditLine->HWindow, EM_SETSEL, (WPARAM)LastSelStart, (LPARAM)LastSelEnd);
    }
}

void CEditWindow::ResetStoredContent()
{
    LastText.clear();
}
