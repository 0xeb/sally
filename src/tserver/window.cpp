// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"

#include <crtdbg.h>
#include <ostream>
#include <stdio.h>
#include <commctrl.h>
#include <limits.h>
#include <algorithm>
#include <array>
#include <limits>
#include <string>
#include <utility>
#include <vector>

#include "lstrfix.h"
#include "trace.h"
#include "messages.h"
#include "handles.h"
#include "array.h"
#include "str.h"
#include "winlib.h"
#include "sheets.h"
#include "tablist.h"
#include "trace_export.h"
#include "tserver.h"
#include "dialog.h"
#include "registry.h"
#include "config.h"

#include "tserver.rh"
#include "tserver.rh2"

// ID of the small icon on the taskbar
#define ICON_ID 1

#define FLUSH_MESSAGES_CACHE_TIMER_ID 1
#define FLUSH_MESSAGES_CACHE_TIMER_TO 100

#define RESET_DATA_ACCEPT_EVENT_TIMER_ID 2
#define RESET_DATA_ACCEPT_EVENT_TIMER_TO 1000

// if TRUE, WM_CLOSE will exit the program
BOOL QuitProgram = FALSE;

//****************************************************************************
//
// CMainWindow
//

CMainWindow::CMainWindow()
{
    TabList = NULL;
    HasHotKey = FALSE;
    HasHotKeyClear = FALSE;
    TaskbarRestartMsg = 0;
}

void CMainWindow::FlushMessagesCache(BOOL& ErrorMessage)
{
    ErrorMessage = FALSE;
    Data.MessagesCache.BlockArray();
    int newCount = Data.MessagesCache.GetCount();
    int firstNew = Data.Messages.Count;

    // if necessary, remove the first X items from the Data.Messages array
    BOOL needRepaint = FALSE;
    if (UseMaxMessagesCount)
    {
        if (firstNew + newCount > MaxMessagesCount)
        {
            int needDelete = firstNew + newCount - MaxMessagesCount;
            if (needDelete > firstNew)
                needDelete = firstNew;
            for (int i = 0; i < needDelete; i++)
                free(Data.Messages[i].File);
            if (needDelete < firstNew)
            {
                memmove(&Data.Messages[0], &Data.Messages[needDelete],
                        (firstNew - needDelete) * sizeof(CGlobalDataMessage));
            }
            Data.Messages.Count = firstNew - needDelete;
            firstNew -= needDelete;
            needRepaint = TRUE;
        }
    }

    int i;
    for (i = 0; i < newCount; i++)
    {
        Data.MessagesCache[i].Index = CGlobalDataMessage::StaticIndex++;
        Data.Messages.Add(Data.MessagesCache[i]);
        if (IsErrorMsg(Data.MessagesCache[i].Type))
            ErrorMessage = TRUE;
    }
    Data.MessagesCache.DestroyMembers(); // does not call destructors
    Data.MessagesCache.UnBlockArray();

    Data.MessagesFlushInProgress = FALSE;
    SetEvent(MessagesFlushDoneEvent);

    // insert new messages into the array
    if (newCount > 0)
    {
        if (firstNew == 0 && newCount > 1)
            firstNew = 1;
        if (firstNew != 0)
        {
            while (firstNew < Data.Messages.Count)
            {
                i = firstNew - 1;
                while (i >= 0 && Data.Messages[firstNew] < Data.Messages[i])
                    i--;
                if (++i < firstNew)
                {
                    CGlobalDataMessage tmp = Data.Messages[firstNew];
                    memmove(&Data.Messages[i + 1], &Data.Messages[i],
                            (firstNew - i) * sizeof(CGlobalDataMessage));
                    Data.Messages[i] = tmp;
                }
                firstNew++;
            }
        }
    }
    if (newCount > 0)
        TabList->SetCount(Data.Messages.Count);
}

void CMainWindow::Activate()
{
    if (!IconControlEnable)
    {
        MessageBeep(MB_ICONEXCLAMATION);
        return;
    }
    if (!ConfigData.UseToolbarCaption)
    {
        // if the main window is not visible, show it
        if (IsIconic(HWindow))
        {
            ShowWindow(HWindow, SW_RESTORE);
            SetForegroundWindow(HWindow);
        }
        else
        {
            if (GetActiveWindow() == HWindow)
            {
                // if the window is active, minimize it
                ShowWindow(HWindow, SW_MINIMIZE);
            }
            else
            {
                // otherwise activate it
                SetForegroundWindow(HWindow);
            }
        }
    }
    else
    {
        // if the main window is not visible, show it
        if (!IsWindowVisible(HWindow))
        {
            ShowWindow(HWindow, SW_SHOW);
            SetForegroundWindow(HWindow);
        }
        else
        {
            if (GetActiveWindow() == HWindow)
            {
                // if the window is active, hide it
                ShowWindow(HWindow, SW_HIDE);
            }
            else
            {
                // otherwise activate it
                SetForegroundWindow(HWindow);
            }
        }
    }
}

void CMainWindow::OnErrorMessage()
{
    // if we are restored, do not bother the user
    if (!IsWindowVisible(HWindow) || IsIconic(HWindow))
    {
        MessageBeep(0);
        if (IsIconic(HWindow))
            ShowWindow(HWindow, SW_RESTORE);
        ShowWindow(HWindow, SW_SHOW);
        SetForegroundWindow(HWindow);
    }
}

void CMainWindow::ClearAllMessages()
{
    Data.MessagesCache.BlockArray();
    for (int i = 0; i < Data.MessagesCache.GetCount(); i++)
    {
        if (Data.MessagesCache[i].File != NULL)
            free(Data.MessagesCache[i].File);
    }
    Data.MessagesCache.DestroyMembers();
    Data.MessagesCache.UnBlockArray();
    for (int i = 0; i < Data.Messages.Count; i++)
    {
        if (Data.Messages[i].File != NULL)
            free(Data.Messages[i].File);
    }
    Data.Messages.DestroyMembers(); // does not call destructors
    TabList->SetCount(0);
}

void CMainWindow::ShowMessageDetails()
{
    int index = TabList->GetSelectedIndex();
    if (index != -1)
    {
        // temporarily suppress the topmost flag so the dialog can be placed under MSVC
        if (ConfigData.AlwaysOnTop)
            SetWindowPos(MainWindow->HWindow, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);

        CDetailsDialog dialog(HWindow, &Data.Messages[index]);
        dialog.Execute();

        if (ConfigData.AlwaysOnTop)
            SetWindowPos(MainWindow->HWindow, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
    }
}

static BOOL WriteWideText(HANDLE file, const std::wstring& text)
{
    if (text.size() > ((std::numeric_limits<size_t>::max)() / sizeof(wchar_t)))
        return FALSE;
    const BYTE* bytes = reinterpret_cast<const BYTE*>(text.data());
    size_t remaining = text.size() * sizeof(wchar_t);
    const DWORD maximumChunk = MAXDWORD & ~static_cast<DWORD>(sizeof(wchar_t) - 1);
    while (remaining != 0)
    {
        const DWORD chunk = static_cast<DWORD>((std::min<size_t>)(remaining, maximumChunk));
        DWORD written = 0;
        if (!WriteFile(file, bytes, chunk, &written, NULL) || written != chunk)
            return FALSE;
        bytes += written;
        remaining -= written;
    }
    return TRUE;
}

static BOOL SelectLogFileOwned(HWND owner, std::wstring& fileName)
{
    const size_t limit = (std::numeric_limits<DWORD>::max)();
    if (fileName.size() >= limit)
        return FALSE;
    static const WCHAR filter[] = L"Log file (*.log)\0*.log\0";
    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = owner;
    ofn.lpstrFilter = filter;
    ofn.nFilterIndex = 1;
    ofn.lpstrDefExt = L"log";
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_HIDEREADONLY | OFN_OVERWRITEPROMPT | OFN_EXPLORER;

    std::vector<WCHAR> buffer((std::max<size_t>)(fileName.size() + 1, 2), L'\0');
    for (;;)
    {
        std::copy(fileName.begin(), fileName.end(), buffer.begin());
        buffer[fileName.size()] = L'\0';
        ofn.lpstrFile = buffer.data();
        ofn.nMaxFile = static_cast<DWORD>(buffer.size());
        if (GetSaveFileNameW(&ofn))
        {
            fileName.assign(buffer.data());
            return TRUE;
        }
        if (CommDlgExtendedError() != FNERR_BUFFERTOOSMALL)
            return FALSE;
        size_t required = *reinterpret_cast<const WORD*>(buffer.data());
        size_t doubled = buffer.size() <= limit / 2 ? buffer.size() * 2 : limit;
        size_t next = (std::max)(doubled, required + 1);
        if (next <= buffer.size() || next > limit)
            return FALSE;
        buffer.assign(next, L'\0');
    }
}

static BOOL GetFullPathOwned(const std::wstring& path, std::wstring& fullPath)
{
    DWORD capacity = GetFullPathNameW(path.c_str(), 0, NULL, NULL);
    if (capacity == 0)
        return FALSE;
    std::vector<WCHAR> buffer(capacity, L'\0');
    for (;;)
    {
        DWORD length = GetFullPathNameW(path.c_str(), static_cast<DWORD>(buffer.size()), buffer.data(), NULL);
        if (length == 0)
            return FALSE;
        if (length < buffer.size())
        {
            fullPath.assign(buffer.data(), length);
            return TRUE;
        }
        if (length == (std::numeric_limits<DWORD>::max)())
            return FALSE;
        buffer.assign(static_cast<size_t>(length) + 1, L'\0');
    }
}

void CMainWindow::ExportAllMessages()
{
    std::wstring fileName = L"tserver.log";
    if (SelectLogFileOwned(HWindow, fileName))
    {
        std::wstring fullName;
        if (GetFullPathOwned(fileName, fullName))
        {
            HANDLE file = HANDLES_Q(CreateFileW(fullName.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                                                FILE_ATTRIBUTE_ARCHIVE, NULL));
            if (file == INVALID_HANDLE_VALUE)
                return;

            const TraceServerExport::Row headers = {
                L"PID", L"UPID", L"PName", L"TID", L"UTID", L"TName",
                L"Date", L"Time", L"Counter [ms]", L"Module", L"Line", L"Message"};
            std::vector<TraceServerExport::Row> rows;
            rows.reserve(static_cast<size_t>(Data.Messages.Count));
            for (int i = 0; i < Data.Messages.Count; i++)
            {
                TraceServerExport::Row row;
                for (size_t column = 0; column < row.size(); column++)
                    row[column] = TabList->GetTextOwned(i, static_cast<int>(column + 1), FALSE);
                rows.emplace_back(std::move(row));
            }
            const TraceServerExport::Widths widths =
                TraceServerExport::CalculateWidths(headers, rows);
            const std::wstring separator = TraceServerExport::BuildSeparator(widths);

            BOOL writeSucceeded = WriteWideText(file, L"\xFEFF" L"Trace Server Log File\r\n\r\n") &&
                                  WriteWideText(file, separator) &&
                                  WriteWideText(file, TraceServerExport::BuildLine(headers, widths, L" |")) &&
                                  WriteWideText(file, separator);
            for (size_t row = 0; writeSucceeded && row < rows.size(); row++)
            {
                const wchar_t* prefix = IsErrorMsg(Data.Messages[static_cast<int>(row)].Type) ? L"E|" : L"I|";
                writeSucceeded = WriteWideText(file, TraceServerExport::BuildLine(rows[row], widths, prefix));
            }
            if (writeSucceeded)
                WriteWideText(file, separator);
            HANDLES(CloseHandle(file));
        }
    }
}

void CMainWindow::GetWindowPos()
{
    ConfigData.MainWindowPlacement.length = sizeof(ConfigData.MainWindowPlacement);
    GetWindowPlacement(HWindow, &ConfigData.MainWindowPlacement);
    ConfigData.MainWindowHidden = FALSE;
    if (ConfigData.UseToolbarCaption && !IsWindowVisible(HWindow))
        ConfigData.MainWindowHidden = TRUE;
}

LRESULT
CMainWindow::WindowProc(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
    case WM_COMMAND:
    {
        switch (LOWORD(wParam))
        {
        case CM_EXIT:
        {
            QuitProgram = TRUE;
            PostMessage(HWindow, WM_CLOSE, 0, 0);
            return 0;
        }

        case CM_EXPORT:
        {
            ExportAllMessages();
            return 0;
        }

        case CM_CLEAR:
        {
            ClearAllMessages();
            return 0;
        }

        case CM_ABOUT:
        {
            CAboutDialog dialog(HInstance, IDD_ABOUT, HWindow);
            dialog.Execute();
            return 0;
        }

        case CM_COPY:
        {
            TabList->CopyLineToClipboard(HWindow);
            return 0;
        }

        case CM_DETAILS:
        {
            ShowMessageDetails();
            return 0;
        }

        case CM_DIFFTIME:
        {
            TabList->SwitchDeltaMode();
            return 0;
        }

        case CM_SHOWINMSVC:
        {
            int index = TabList->GetSelectedIndex();
            if (index != -1)
                Data.GotoEditor(index);
            return 0;
        }

        case CM_CONFIGURATION:
        {
            DoSetupDialog(HWindow);
            return 0;
        }
        }
        break;
    }

    case WM_INITMENUPOPUP:
    {
        CheckMenuItem((HMENU)wParam, CM_DIFFTIME, MF_BYCOMMAND | (TabList->GetDeltaMode() ? MF_CHECKED : MF_UNCHECKED));
        break;
    }

    case WM_HOTKEY:
    {
        if ((int)wParam == HOT_KEY_ID)
            Activate();
        if ((int)wParam == HOT_KEYCLEAR_ID)
            ClearAllMessages();
        break;
    }

    case WM_ACTIVATEAPP:
    {
        if ((BOOL)wParam)
        {
            if (TabList != NULL)
            {
                PostMessage(HWindow, WM_USER_ACTIVATE_APP, 0, 0);
            }
        }
        break;
    }

    case WM_ACTIVATE:
    {
        if (wParam == WA_ACTIVE || wParam == WA_CLICKACTIVE)
        {
            PostMessage(HWindow, WM_USER_ACTIVATE_APP, 0, 0);
            return 0;
        }
        break;
    }

    case WM_SETFOCUS:
    {
        if (TabList != NULL && TabList->HWindow != NULL)
        {
            PostMessage(HWindow, WM_USER_ACTIVATE_APP, 0, 0);
            return 0;
        }
        break;
    }

    case WM_ERASEBKGND:
    {
        return TRUE;
    }

    case WM_CREATE:
    {
        RECT r;
        GetClientRect(HWindow, &r);
        TabList = new CTabList();
        if (TabList != NULL)
        {
            if (TabList->Create(WC_TABLIST,
                                L"",
                                WS_VISIBLE | WS_CHILD | WS_CLIPCHILDREN,
                                0,
                                0,
                                r.right,
                                r.bottom,
                                HWindow,
                                NULL,
                                HInstance,
                                TabList))
            {
                // start a timer for loading the cache
                SetTimer(HWindow, FLUSH_MESSAGES_CACHE_TIMER_ID, FLUSH_MESSAGES_CACHE_TIMER_TO, NULL);

                if (ConfigData.HotKey != 0)
                    HasHotKey = RegisterHotKey(HWindow, HOT_KEY_ID, HIBYTE(ConfigData.HotKey), LOBYTE(ConfigData.HotKey));
                if (ConfigData.HotKeyClear != 0)
                    HasHotKeyClear = RegisterHotKey(HWindow, HOT_KEYCLEAR_ID, HIBYTE(ConfigData.HotKeyClear), LOBYTE(ConfigData.HotKeyClear));
            }
            else
            {
                TRACE_EW(L"Error while creating TabListu.");
            }
        }
        else
        {
            TRACE_EW(L"Out of memory.");
        }

        // start a timer to reset ConnectDataAcceptedEvent, which older clients must have
        // reset before connecting, otherwise the connect would collapse (since version 7 clients reset it
        // in advance and this is not needed); scenario: if the client does not receive the server response,
        // it ends the connect with a timeout, and when the server finishes the action and prepares the response,
        // it signals ConnectDataAcceptedEvent, and nobody resets the event, so it stays signaled for the next
        // connect (which then fails unless it is a version 7 or newer client)
        SetTimer(HWindow, RESET_DATA_ACCEPT_EVENT_TIMER_ID, RESET_DATA_ACCEPT_EVENT_TIMER_TO, NULL);

        TaskbarRestartMsg = RegisterWindowMessageW(L"TaskbarCreated");

        break;
    }

    case WM_QUERYENDSESSION:
    {
        GetWindowPos();
        Registry.Save();
        return TRUE;
    }

    case WM_CLOSE:
    {
        if (QuitProgram)
        {
            GetWindowPos();
            DestroyWindow(HWindow);
        }

        if (!ConfigData.UseToolbarCaption)
        {
            if (!IsIconic(HWindow))
                ShowWindow(HWindow, SW_MINIMIZE);
        }
        else
        {
            if (IsWindowVisible(HWindow))
                ShowWindow(HWindow, SW_HIDE);
        }
        return 0;
    }

    case WM_DESTROY:
    {
        if (HasHotKey)
            UnregisterHotKey(HWindow, HOT_KEY_ID);
        if (HasHotKeyClear)
            UnregisterHotKey(HWindow, HOT_KEYCLEAR_ID);
        KillTimer(HWindow, FLUSH_MESSAGES_CACHE_TIMER_ID);
        KillTimer(HWindow, RESET_DATA_ACCEPT_EVENT_TIMER_ID);
        TaskBarRemoveIcon();
        PostQuitMessage(0);
        return 0;
    }

    case WM_MOVE:
    {
        GetWindowPos();
        break;
    }

    case WM_SIZE:
    {
        // save the main window position to the configuration
        GetWindowPos();

        // position the TabList
        if (TabList != NULL)
        {
            RECT r;
            GetClientRect(HWindow, &r);
            SetWindowPos(TabList->HWindow, NULL, 0, 0, r.right, r.bottom, SWP_NOMOVE);
        }
        break;
    }

    case WM_TIMER:
    {
        if (wParam == FLUSH_MESSAGES_CACHE_TIMER_ID)
        {
            BOOL errorMessage;
            FlushMessagesCache(errorMessage);
            if (errorMessage && ConfigData.ShowOnErrorMessage)
                OnErrorMessage();
        }
        else
        {
            if (wParam == RESET_DATA_ACCEPT_EVENT_TIMER_ID && ConnectDataAcceptedEventMayBeSignaled)
            {
                if (WaitForSingleObject(OpenConnectionMutex, 0) == WAIT_OBJECT_0) // so nobody waits on ConnectDataAcceptedEvent
                {
                    ConnectDataAcceptedEventMayBeSignaled = FALSE; // it will be reset even if the previous client skipped it
                    ResetEvent(ConnectDataAcceptedEvent);
                    ReleaseMutex(OpenConnectionMutex); // make connects available to clients again
                }
            }
        }
        return 0;
    }

    case WM_USER_ACTIVATE_APP:
    {
        SetForegroundWindow(TabList->GetListViewHWND());
        SetFocus(TabList->GetListViewHWND());
        return 0;
    }

    case WM_KEYDOWN:
    {
        switch (wParam)
        {
        case VK_ESCAPE:
        {
            Activate();
            break;
        }
        }
        break;
    }

    case WM_USER_PROCESS_CONNECTED:
    {
        if (ConfigData.AutoClear)
            ClearAllMessages();
        return 0;
    }

    case WM_USER_CT_OPENCONNECTION:
    {
        ReleaseMutex(OpenConnectionMutex); // the connection thread starts operating
        return 0;
    }

    case WM_USER_CT_TERMINATED:
    {
        WaitForSingleObject(ConnectingThread, 5000); // wait until it finishes

        DWORD exitCode;
        if (GetExitCodeThread(ConnectingThread, &exitCode))
        {
            switch (exitCode)
            {
            case CT_SUCCESS:
                break;

            case CT_UNABLE_TO_CREATE_FILE_MAPPING:
            {
                MESSAGE_EW(NULL, L"Unable to create file mapping in connecting thread.", MB_OK);
                break;
            }

            case CT_UNABLE_TO_MAP_VIEW_OF_FILE:
            {
                MESSAGE_EW(NULL, L"Unable to map view of file in connecting thread.", MB_OK);
                break;
            }

            default: // STILL_ACTIVE and other values...
            {
                MESSAGE_EW(NULL, L"Unexpected exit code of connecting thread.", MB_OK);
                break;
            }
            }
        }
        else
            MESSAGE_EW(NULL, L"Unable to get exit code of terminated connecting thread.", MB_OK);
        return 0;
    }

    case WM_USER_SHOWERROR:
    {
        switch (wParam)
        {
        case EC_CANNOT_CREATE_READ_PIPE_THREAD:
        {
            MESSAGE_EW(NULL, L"Unable to create read pipe thread, connection failed.",
                       MB_OK);
            break;
        }

        case EC_LOW_MEMORY:
        {
            MESSAGE_EW(NULL, L"Low memory.", MB_OK);
            QuitProgram = TRUE;
            PostMessage(HWindow, WM_CLOSE, 0, 0);
            break;
        }

        case EC_UNKNOWN_MESSAGE_TYPE:
        {
            MESSAGE_EW(NULL, L"Unknown message type.\nConnection failed.", MB_OK);
            break;
        }

        default:
        {
            MESSAGE_EW(NULL, L"Unexpected error code.", MB_OK);
            break;
        }
        }
        return 0;
    }

    case WM_USER_PROCESSES_CHANGE:
    {
        if (TabList != NULL)
            TabList->RedrawListView();
        return 0;
    }

    case WM_USER_THREADS_CHANGE:
    {
        if (TabList != NULL)
            TabList->RedrawListView();
        return 0;
    }

    case WM_USER_PROCESS_DISCONNECTED:
    {
        TRACE_I("Odpojil se client - PID: " << wParam);
        return 0;
    }

    case WM_USER_INCORRECT_VERSION:
    {
        MESSAGE_EW(NULL, L"Incorrect version of client, connection refused." << L"\nClient PID: " << lParam << L"\nClient version: " << wParam << L"\nServer version: " << (DWORD)TRACE_SERVER_VERSION, MB_OK);
        return 0;
    }

    case WM_USER_SHOWSYSTEMERROR:
    {
        MESSAGE_EW(NULL, L"Error during reading client->server pipe:\n"
                             << errW((DWORD)wParam),
                   MB_OK);
        return 0;
    }

    case WM_USER_FLUSH_MESSAGES_CACHE:
    {
        BOOL errorMessage;
        FlushMessagesCache(errorMessage);
        if (errorMessage && ConfigData.ShowOnErrorMessage)
            OnErrorMessage();
        return 0;
    }

    case WM_USER_ICON_NOTIFY:
    {
        switch (lParam)
        {
        case WM_LBUTTONDBLCLK:
        case WM_LBUTTONDOWN:
        {
            Activate();
            break;
        }

        case WM_RBUTTONDOWN:
        {
            if (!IconControlEnable)
            {
                MessageBeep(MB_ICONEXCLAMATION);
                break;
            }
            HMENU hMenu = LoadMenuW(HInstance, MAKEINTRESOURCEW(IDM_ICON_POPUP));
            HMENU hPopupMenu = GetSubMenu(hMenu, 0);
            POINT p;
            GetCursorPos(&p);

            SetForegroundWindow(HWindow);
            TrackPopupMenu(hPopupMenu, TPM_LEFTBUTTON | TPM_RIGHTBUTTON, p.x,
                           p.y, 0, HWindow, NULL);
            PostMessage(HWindow, WM_USER, 0, 0);
            DestroyMenu(hMenu);
            break;
        }
        }
        return 0;
    }

    default:
    {
        if (uMsg == TaskbarRestartMsg)
            TaskBarAddIcon();
        break;
    }
    }

    return CWindow::WindowProc(uMsg, wParam, lParam);
}

BOOL CMainWindow::TaskBarAddIcon()
{
    BOOL res;
    NOTIFYICONDATA tnid;

    tnid.cbSize = sizeof(NOTIFYICONDATA);
    tnid.hWnd = HWindow;
    tnid.uID = ICON_ID;
    tnid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    tnid.uCallbackMessage = WM_USER_ICON_NOTIFY;
    tnid.hIcon = HANDLES(LoadIcon(HInstance, MAKEINTRESOURCE(IC_TSERVER_1)));

    lstrcpyn(tnid.szTip, MAINWINDOW_NAME, sizeof(tnid.szTip));

    res = Shell_NotifyIcon(NIM_ADD, &tnid);

    if (tnid.hIcon != NULL)
        HANDLES(DestroyIcon(tnid.hIcon));

    return res;
}

BOOL CMainWindow::TaskBarRemoveIcon()
{
    NOTIFYICONDATA tnid;

    tnid.cbSize = sizeof(NOTIFYICONDATA);
    tnid.hWnd = HWindow;
    tnid.uID = ICON_ID;

    return Shell_NotifyIcon(NIM_DELETE, &tnid);
}
