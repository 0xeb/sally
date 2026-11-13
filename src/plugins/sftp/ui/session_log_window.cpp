#include "precomp.h"

#include <commdlg.h>

#include <vector>

#include "session_log_window.h"

#include "centered_dialog.h"
#include "dark_controls.h"
#include "global_config.h"

namespace
{
// Messages the host posts to the window (WM_APP range: private to this window class).
enum
{
    WM_APP_SHOW = WM_APP + 1, // wParam: id of the log to select (0 = keep)
    WM_APP_CLOSE
};

const UINT_PTR TIMER_POLL = 1;
const UINT POLL_MS = 500;

// How a control follows the size of the window.
struct CAnchor
{
    int id;
    bool moveX;    // follows the right edge
    bool moveY;    // follows the bottom edge
    bool stretchX; // grows with the width
    bool stretchY; // grows with the height
};
const CAnchor Anchors[] = {
    {IDC_LOG_COMBO, false, false, true, false},
    {IDE_LOG_TEXT, false, false, true, true},
    {IDB_LOG_COPY, false, true, false, false},
    {IDB_LOG_SAVE, false, true, false, false},
    {IDB_LOG_CLEAR, false, true, false, false},
    {IDCANCEL, true, true, false, false},
};
const int AnchorCount = static_cast<int>(sizeof(Anchors) / sizeof(Anchors[0]));

bool SameLists(const std::vector<CSessionLogInfo>& a, const std::vector<CSessionLogInfo>& b)
{
    if (a.size() != b.size())
        return false;
    for (size_t i = 0; i < a.size(); i++)
        if (a[i].id != b[i].id || a[i].open != b[i].open || !(a[i].title == b[i].title))
            return false;
    return true;
}
} // namespace

std::wstring FormatSessionLogTitle(const CSessionLogInfo& info)
{
    std::wstring text;
    if (info.title.kind == CSessionLogTitle::Kind::Panel)
        text = SPLFormatStringOwned(LangStr(IDS_LOG_TITLE_PANEL).c_str(), info.title.target.c_str());
    else if (info.title.operation == 0)
        text = SPLFormatStringOwned(LangStr(IDS_LOG_TITLE_WORKER_NO_OPERATION).c_str(), info.title.target.c_str(),
                                    info.title.worker);
    else
        text = SPLFormatStringOwned(LangStr(IDS_LOG_TITLE_WORKER).c_str(), info.title.target.c_str(),
                                    static_cast<int>(info.title.operation), info.title.worker);
    if (!info.open)
        text = SPLFormatStringOwned(LangStr(IDS_LOG_CLOSED_SUFFIX).c_str(), text.c_str());
    return text;
}

// ---- CSessionLogWindow -----------------------------------------------------------------------------------------
// The dialog. Lives on the window thread only (a local variable of WindowThreadMain), so it is "static" for WinLib
// (ooStatic): WinLib does not delete it when the window is destroyed.

class CSessionLogWindow : public CCenteredDialog
{
public:
    ~CSessionLogWindow() override
    {
        if (font_ != nullptr)
            DeleteObject(font_); // the window is gone by now
    }

    explicit CSessionLogWindow(CSessionLogWindowHost& host)
        : CCenteredDialog(HLanguage, IDD_SESSION_LOG, IDH_SESSION_LOG, nullptr), host_(host) // no owner: it would couple the input queues
    {
        SetObjectOrigin(ooStatic);
    }

    // Creates the window, runs its message loop and returns after the window was destroyed.
    void Run();

protected:
    INT_PTR DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam) override;

private:
    INT_PTR HandleMessage(UINT uMsg, WPARAM wParam, LPARAM lParam);
    void InitControls();
    void SetFixedFont(HWND edit);
    void CaptureLayout();
    void ApplyLayout();
    void OnCommand(int id, int code);
    void ShowAndFront();

    void SelectLog(unsigned id);
    void Refresh(bool force);
    void RebuildCombo(const std::vector<CSessionLogInfo>& list);
    void ShowText(const std::wstring& text, bool force);
    bool IsAtEnd(HWND edit) const;
    void UpdateButtons();

    void CopyToClipboard();
    void SaveToFile();

    CSessionLogWindowHost& host_;
    HFONT font_ = nullptr; // fixed-width font of the log edit; owned here

    unsigned shownLog_ = 0; // the log of the combo's selection (0 = none)
    std::vector<CSessionLogInfo> shownList_;
    std::wstring shownText_;
    unsigned long lastCounter_ = 0;
    bool counterValid_ = false;

    // layout: the control rectangles in client coordinates and the client size at creation
    RECT initialRects_[AnchorCount] = {};
    SIZE initialClient_ = {};
    SIZE minTrack_ = {};
    bool layoutReady_ = false;
};

void CSessionLogWindow::Run()
{
    HWND window = Create();
    if (window == nullptr)
        return;

    unsigned startLog = 0;
    if (!host_.OnWindowCreated(window, startLog))
        DestroyWindow(window); // closed while it was being created; WM_DESTROY ends the loop below
    else
    {
        SelectLog(startLog);
        ShowAndFront();
    }

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0)
    {
        if (HWindow == nullptr || !IsDialogMessageW(HWindow, &msg))
        {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    if (HWindow != nullptr) // the loop ended without WM_DESTROY (GetMessage error)
        DestroyWindow(HWindow);
}

INT_PTR CSessionLogWindow::DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    try
    {
        return HandleMessage(uMsg, wParam, lParam);
    }
    catch (...)
    {
        TRACE_E("CSessionLogWindow: unexpected exception in a window message.");
        return FALSE; // no exception may cross the Win32 boundary
    }
}

INT_PTR CSessionLogWindow::HandleMessage(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
    case WM_INITDIALOG:
    {
        const INT_PTR result = CCenteredDialog::DialogProc(uMsg, wParam, lParam);
        InitControls();
        return result;
    }

    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK) // Enter: nothing to confirm here
            return TRUE;
        if (LOWORD(wParam) == IDCANCEL) // Close and Esc
        {
            DestroyWindow(HWindow);
            return TRUE;
        }
        OnCommand(LOWORD(wParam), HIWORD(wParam));
        return TRUE;

    case WM_CLOSE:
        DestroyWindow(HWindow);
        return TRUE;

    case WM_TIMER:
        if (wParam == TIMER_POLL)
            Refresh(false);
        return TRUE;

    case WM_SIZE:
        if (wParam != SIZE_MINIMIZED)
            ApplyLayout();
        break;

    case WM_GETMINMAXINFO:
        if (layoutReady_)
        {
            MINMAXINFO* info = reinterpret_cast<MINMAXINFO*>(lParam);
            info->ptMinTrackSize.x = minTrack_.cx;
            info->ptMinTrackSize.y = minTrack_.cy;
        }
        return TRUE;

    case WM_APP_SHOW:
        SelectLog(static_cast<unsigned>(wParam));
        ShowAndFront();
        return TRUE;

    case WM_APP_CLOSE:
        DestroyWindow(HWindow);
        return TRUE;

    case WM_DESTROY:
        KillTimer(HWindow, TIMER_POLL);
        host_.OnWindowDestroyed(); // from here on nobody posts to this handle any more
        PostQuitMessage(0);        // ends the loop in Run
        break;
    }
    return CCenteredDialog::DialogProc(uMsg, wParam, lParam);
}

// ---- setup -------------------------------------------------------------------------------------------------------

void CSessionLogWindow::InitControls()
{
    ApplyDarkButtons(HWindow);
    HWND edit = GetDlgItem(HWindow, IDE_LOG_TEXT);
    SendMessageW(edit, EM_LIMITTEXT, 0, 0); // an edit control holds 32 KB by default; a log is up to 16 MiB
    SetFixedFont(edit);
    CaptureLayout();
    SetTimer(HWindow, TIMER_POLL, POLL_MS, nullptr);
}

// Same size and weight as the dialog font, but Consolas (a fixed-width face) so the columns of the log line up. A system
// without Consolas gets another face from CreateFontIndirect; that is acceptable for a log.
void CSessionLogWindow::SetFixedFont(HWND edit)
{
    LOGFONTW lf = {};
    if (GetObjectW(reinterpret_cast<HFONT>(SendMessageW(edit, WM_GETFONT, 0, 0)), sizeof(lf), &lf) == 0)
        return; // keep the dialog font
    lf.lfPitchAndFamily = FIXED_PITCH | FF_MODERN;
    wcscpy_s(lf.lfFaceName, L"Consolas");
    font_ = CreateFontIndirectW(&lf);
    if (font_ != nullptr)
        SendMessageW(edit, WM_SETFONT, reinterpret_cast<WPARAM>(font_), TRUE);
}

void CSessionLogWindow::CaptureLayout()
{
    RECT client;
    GetClientRect(HWindow, &client);
    initialClient_.cx = client.right;
    initialClient_.cy = client.bottom;
    RECT window;
    GetWindowRect(HWindow, &window);
    minTrack_.cx = window.right - window.left;
    minTrack_.cy = window.bottom - window.top;
    for (int i = 0; i < AnchorCount; i++)
    {
        HWND control = GetDlgItem(HWindow, Anchors[i].id);
        GetWindowRect(control, &initialRects_[i]);
        MapWindowPoints(nullptr, HWindow, reinterpret_cast<POINT*>(&initialRects_[i]), 2);
    }
    layoutReady_ = true;
}

void CSessionLogWindow::ApplyLayout()
{
    if (!layoutReady_)
        return;
    RECT client;
    GetClientRect(HWindow, &client);
    const int dx = client.right - initialClient_.cx;
    const int dy = client.bottom - initialClient_.cy;
    for (int i = 0; i < AnchorCount; i++)
    {
        const CAnchor& a = Anchors[i];
        const RECT& r = initialRects_[i];
        const int x = r.left + (a.moveX ? dx : 0);
        const int y = r.top + (a.moveY ? dy : 0);
        const int w = (r.right - r.left) + (a.stretchX ? dx : 0);
        const int h = (r.bottom - r.top) + (a.stretchY ? dy : 0);
        SetWindowPos(GetDlgItem(HWindow, a.id), nullptr, x, y, w > 0 ? w : 0, h > 0 ? h : 0,
                     SWP_NOZORDER | SWP_NOACTIVATE);
    }
}

void CSessionLogWindow::ShowAndFront()
{
    ShowWindow(HWindow, IsIconic(HWindow) ? SW_RESTORE : SW_SHOW);
    SetForegroundWindow(HWindow); // may only flash the taskbar button when Sally is not in the foreground
}

void CSessionLogWindow::OnCommand(int id, int code)
{
    switch (id)
    {
    case IDC_LOG_COMBO:
        if (code == CBN_SELCHANGE)
        {
            const LRESULT index = SendDlgItemMessageW(HWindow, IDC_LOG_COMBO, CB_GETCURSEL, 0, 0);
            const LRESULT data = index == CB_ERR ? 0 : SendDlgItemMessageW(HWindow, IDC_LOG_COMBO, CB_GETITEMDATA, index, 0);
            shownLog_ = static_cast<unsigned>(data);
            Refresh(true);
        }
        break;
    case IDB_LOG_COPY:
        CopyToClipboard();
        break;
    case IDB_LOG_SAVE:
        SaveToFile();
        break;
    case IDB_LOG_CLEAR:
        if (shownLog_ != 0)
        {
            SessionLogStore().Clear(shownLog_);
            Refresh(true);
        }
        break;
    }
}

// ---- content -----------------------------------------------------------------------------------------------------

// Makes this log the selected one (0 = keep the current choice) and reloads everything.
void CSessionLogWindow::SelectLog(unsigned id)
{
    if (id != 0)
        shownLog_ = id;
    Refresh(true);
}

void CSessionLogWindow::RebuildCombo(const std::vector<CSessionLogInfo>& list)
{
    HWND combo = GetDlgItem(HWindow, IDC_LOG_COMBO);
    SendMessageW(combo, WM_SETREDRAW, FALSE, 0);
    SendMessageW(combo, CB_RESETCONTENT, 0, 0);
    int selected = -1;
    for (size_t i = 0; i < list.size(); i++)
    {
        const std::wstring title = FormatSessionLogTitle(list[i]);
        const LRESULT index = SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(title.c_str()));
        if (index == CB_ERR)
            continue;
        SendMessageW(combo, CB_SETITEMDATA, index, static_cast<LPARAM>(list[i].id));
        if (list[i].id == shownLog_)
            selected = static_cast<int>(index);
    }
    const int count = static_cast<int>(SendMessageW(combo, CB_GETCOUNT, 0, 0));
    if (selected < 0 && count > 0)
        selected = count - 1; // the log that was shown is gone: the newest one
    SendMessageW(combo, CB_SETCURSEL, selected, 0);
    shownLog_ = selected >= 0 ? static_cast<unsigned>(SendMessageW(combo, CB_GETITEMDATA, selected, 0)) : 0;
    SendMessageW(combo, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(combo, nullptr, TRUE);
}

void CSessionLogWindow::Refresh(bool force)
{
    const unsigned long counter = SessionLogStore().ChangeCounter();
    if (!force && counterValid_ && counter == lastCounter_)
        return;
    lastCounter_ = counter;
    counterValid_ = true;

    const std::vector<CSessionLogInfo> list = SessionLogStore().List();
    const unsigned before = shownLog_;
    if (force || !SameLists(list, shownList_))
    {
        RebuildCombo(list);
        shownList_ = list;
    }
    std::wstring text;
    if (shownLog_ != 0)
        SessionLogStore().GetText(shownLog_, text);
    else
        text = LangStr(IDS_LOG_NO_LOGS);
    if (shownLog_ != 0 && text.empty() && GlobalConfigStore().Get().logLevel == SessionLogLevel::Off)
        text = LangStr(IDS_LOG_OFF_HINT);
    ShowText(text, force || shownLog_ != before);
    UpdateButtons();
}

// True when the edit shows its last line (or has no scroll bar): then a log that grows keeps following the end.
bool CSessionLogWindow::IsAtEnd(HWND edit) const
{
    SCROLLINFO si = {};
    si.cbSize = sizeof(si);
    si.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
    if (!GetScrollInfo(edit, SB_VERT, &si))
        return true;
    return si.nMax <= 0 || si.nPos + static_cast<int>(si.nPage) >= si.nMax - 1;
}

// Puts the text into the edit control. A text that only grew at the end is appended, which keeps the selection and the
// scroll position; anything else replaces the text. A window at the end of the text stays at the end, otherwise the
// first visible line is kept.
void CSessionLogWindow::ShowText(const std::wstring& text, bool force)
{
    if (!force && text == shownText_)
        return;
    HWND edit = GetDlgItem(HWindow, IDE_LOG_TEXT);
    const bool atEnd = force || IsAtEnd(edit);
    const int firstLine = static_cast<int>(SendMessageW(edit, EM_GETFIRSTVISIBLELINE, 0, 0));
    DWORD selStart = 0;
    DWORD selEnd = 0;
    SendMessageW(edit, EM_GETSEL, reinterpret_cast<WPARAM>(&selStart), reinterpret_cast<LPARAM>(&selEnd));
    const bool appended = !force && !shownText_.empty() && text.size() > shownText_.size() &&
                          text.compare(0, shownText_.size(), shownText_) == 0;

    SendMessageW(edit, WM_SETREDRAW, FALSE, 0);
    if (appended)
    {
        const int length = GetWindowTextLengthW(edit);
        SendMessageW(edit, EM_SETSEL, length, length);
        SendMessageW(edit, EM_REPLACESEL, FALSE, reinterpret_cast<LPARAM>(text.c_str() + shownText_.size()));
    }
    else
        SetWindowTextW(edit, text.c_str());

    if (atEnd)
    {
        const int length = GetWindowTextLengthW(edit);
        SendMessageW(edit, EM_SETSEL, length, length);
        SendMessageW(edit, EM_SCROLLCARET, 0, 0);
    }
    else
    {
        if (appended)
            SendMessageW(edit, EM_SETSEL, selStart, selEnd);
        const int nowFirst = static_cast<int>(SendMessageW(edit, EM_GETFIRSTVISIBLELINE, 0, 0));
        if (nowFirst != firstLine)
            SendMessageW(edit, EM_LINESCROLL, 0, firstLine - nowFirst);
    }
    SendMessageW(edit, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(edit, nullptr, TRUE);
    shownText_ = text;
}

void CSessionLogWindow::UpdateButtons()
{
    const BOOL enable = shownLog_ != 0;
    EnableWindow(GetDlgItem(HWindow, IDB_LOG_COPY), enable);
    EnableWindow(GetDlgItem(HWindow, IDB_LOG_SAVE), enable);
    EnableWindow(GetDlgItem(HWindow, IDB_LOG_CLEAR), enable);
}

// ---- buttons -----------------------------------------------------------------------------------------------------

// The whole log text of the selected connection as Unicode text (own Win32 code: this runs on the window thread).
void CSessionLogWindow::CopyToClipboard()
{
    std::wstring text;
    if (shownLog_ == 0 || !SessionLogStore().GetText(shownLog_, text))
        return;
    const size_t bytes = (text.size() + 1) * sizeof(wchar_t);
    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (memory == nullptr)
        return;
    void* locked = GlobalLock(memory);
    if (locked == nullptr)
    {
        GlobalFree(memory);
        return;
    }
    memcpy(locked, text.c_str(), bytes);
    GlobalUnlock(memory);
    if (!OpenClipboard(HWindow))
    {
        GlobalFree(memory);
        return;
    }
    EmptyClipboard();
    if (SetClipboardData(CF_UNICODETEXT, memory) == nullptr)
        GlobalFree(memory); // on success the clipboard owns the memory
    CloseClipboard();
}

// Asks for a file name and writes the log as UTF-8 text with a byte order mark (Notepad shows it right).
void CSessionLogWindow::SaveToFile()
{
    std::wstring text;
    if (shownLog_ == 0 || !SessionLogStore().GetText(shownLog_, text))
        return;

    // the filter is "name\0pattern\0name\0pattern\0\0"
    std::wstring filter = LangStr(IDS_LOG_SAVE_FILTER_TEXT);
    filter.push_back(L'\0');
    filter += L"*.txt";
    filter.push_back(L'\0');
    filter += LangStr(IDS_LOG_SAVE_FILTER_ALL);
    filter.push_back(L'\0');
    filter += L"*.*";
    filter.push_back(L'\0'); // the second terminator comes from the string itself

    wchar_t file[MAX_PATH] = L"sftp-log.txt";
    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = HWindow;
    ofn.lpstrFilter = filter.c_str();
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrDefExt = L"txt";
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY | OFN_NOCHANGEDIR;
    if (!GetSaveFileNameW(&ofn))
        return;

    std::string bytes = "\xEF\xBB\xBF";
    if (!text.empty())
    {
        const int length = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
        if (length > 0)
        {
            std::string utf8(static_cast<size_t>(length), '\0');
            WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), &utf8[0], length, nullptr, nullptr);
            bytes += utf8;
        }
    }
    DWORD error = ERROR_SUCCESS;
    HANDLE handle = CreateFileW(file, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE)
        error = GetLastError();
    else
    {
        DWORD written = 0;
        if (!WriteFile(handle, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr) || written != bytes.size())
            error = GetLastError() != ERROR_SUCCESS ? GetLastError() : ERROR_WRITE_FAULT;
        CloseHandle(handle);
    }
    if (error != ERROR_SUCCESS)
        SalamanderGeneral->SalMessageBox(HWindow, SPLFormatStringOwned(LangStr(IDS_LOG_SAVE_FAILED).c_str(), static_cast<int>(error)).c_str(),
                                         LangStr(IDS_PLUGINNAME).c_str(), MB_OK | MB_ICONEXCLAMATION);
}

// ---- CSessionLogWindowHost -------------------------------------------------------------------------------------

CSessionLogWindowHost& SessionLogWindow()
{
    static CSessionLogWindowHost host;
    return host;
}

CSessionLogWindowHost::~CSessionLogWindowHost()
{
    try
    {
        Close(); // normally done by Release already: nothing is joined then
    }
    catch (...)
    {
    }
}

bool CSessionLogWindowHost::Show(unsigned logId)
{
    std::lock_guard<std::mutex> lifecycle(lifecycleMutex_);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (running_)
        {
            if (hwnd_ != nullptr)
                PostMessageW(hwnd_, WM_APP_SHOW, logId, 0);
            else if (logId != 0)
                startLog_ = logId; // the thread is still starting and selects it when the window exists
            return true;
        }
    }

    try
    {
        if (thread_.joinable())
            thread_.join(); // an earlier window that closed itself
        {
            std::lock_guard<std::mutex> lock(mutex_);
            closing_ = false;
            running_ = true;
            startLog_ = logId;
        }
        thread_ = std::thread([this] { WindowThreadMain(); });
    }
    catch (...)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        running_ = false;
        return false;
    }
    return true;
}

void CSessionLogWindowHost::Close()
{
    std::lock_guard<std::mutex> lifecycle(lifecycleMutex_);
    if (thread_.joinable() && thread_.get_id() == std::this_thread::get_id())
        return; // called from the window itself: joining oneself would hang
    {
        std::lock_guard<std::mutex> lock(mutex_);
        closing_ = true; // a window being created destroys itself
        if (hwnd_ != nullptr)
            PostMessageW(hwnd_, WM_APP_CLOSE, 0, 0);
    }
    if (thread_.joinable())
        thread_.join();
    std::lock_guard<std::mutex> lock(mutex_);
    closing_ = false;
    running_ = false;
    hwnd_ = nullptr;
}

bool CSessionLogWindowHost::IsOpen() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return running_;
}

void CSessionLogWindowHost::WindowThreadMain()
{
    try
    {
        TraceAttachCurrentThread();
        SetThreadNameInVCAndTrace(L"SFTP session log window");
        CSessionLogWindow window(*this);
        window.Run();
    }
    catch (...)
    {
        TRACE_E("CSessionLogWindowHost: the window thread ended with an exception.");
    }
    std::lock_guard<std::mutex> lock(mutex_);
    hwnd_ = nullptr;
    running_ = false;
}

bool CSessionLogWindowHost::OnWindowCreated(HWND window, unsigned& startLog)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (closing_)
        return false;
    hwnd_ = window;
    startLog = startLog_;
    return true;
}

void CSessionLogWindowHost::OnWindowDestroyed()
{
    std::lock_guard<std::mutex> lock(mutex_);
    hwnd_ = nullptr;
    running_ = false; // a Show from now on starts a new window (after joining this thread)
}
