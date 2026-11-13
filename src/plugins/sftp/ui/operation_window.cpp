#include "precomp.h"

#include <algorithm>
#include <chrono>
#include <map>

#include "operation_window.h"

#include "dark_controls.h"

#include "encoding.h"
#include "error_text.h"
#include "host_key_dialog.h"
#include "problem_text.h"
#include "progress_format.h"
#include "prompt_dialog.h"
#include "solve_error_dialog.h"
#include "text_util.h"

namespace
{
// Messages the host posts to the window (WM_APP range: private to this window class).
enum
{
    WM_APP_ITEMS = WM_APP + 1, // an item changed state
    WM_APP_PROGRESS,           // bytes moved
    WM_APP_STATE,              // state, pause flag, cancel flag or "finished" changed
    WM_APP_INTERACTION,        // a worker waits for an answer
    WM_APP_SHOW,
    WM_APP_HIDE,
    WM_APP_CLOSE
};

const UINT_PTR TIMER_SECOND = 1; // speed, ETA, elapsed time
const UINT_PTR TIMER_REFRESH = 2; // a repaint that was postponed to keep the 100 ms minimum distance
const long long MinRepaintMs = 100;
const long long MinAllItemsRebuildMs = 1000; // the "all items" list is bigger: rebuild it at most once a second
const size_t MaxListRows = 2000;

long long NowMs()
{
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}
} // namespace

// ---- COperationWindow ------------------------------------------------------------------------------------------
// The dialog. Lives on the window thread only (a local variable of COperationWindowHost::WindowThreadMain), so it is
// "static" for WinLib (ooStatic): WinLib does not delete it when the window is destroyed.

class COperationWindow : public CCenteredDialog
{
public:
    COperationWindow(COperationWindowHost& host, COperation& operation, const COperationWindowOptions& options)
        : CCenteredDialog(HLanguage, IDD_OPERATION, nullptr), // no owner: an owner on Sally's thread would couple the input queues
          host_(host), op_(operation), options_(options)
    {
        SetObjectOrigin(ooStatic);
        if (!options_.solveError)
            options_.solveError = &ShowSolveErrorDialog; // the owner may bring its own
    }

    // Creates the window, runs its message loop and returns after the window was destroyed.
    void Run();

protected:
    INT_PTR DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam) override;

private:
    INT_PTR HandleMessage(UINT uMsg, WPARAM wParam, LPARAM lParam);
    void InitControls();
    void InitList();
    const std::wstring& Str(int id);
    void SetTextIfChanged(int controlId, const std::wstring& text); // controlId 0: the window title
    void EnableIfChanged(int controlId, bool enable);

    void RefreshNow();
    void RequestRefresh();
    void UpdateSummary();
    void UpdateCurrentFile();
    void UpdateButtons(const CQueueSummary& summary, OperationStatus status);
    void UpdateSolveButton();
    void RebuildList();
    uint64_t SelectedUid();
    bool ErrorsOnly();
    int KindTitleId() const;

    void OnState();
    void OnCommand(int id, int code);
    void HideWindow();
    void CloseOrHide();
    void ShowAndFront();
    void TogglePause();
    void ConfirmCancel();
    void SolveSelected();
    void RequestDestroy();
    void FinishModal();
    bool CloseTopmostModal();

    void HandleInteractions();
    void AnswerInteraction(const CInteraction& in);
    HostKeyDecision AskHostKey(const CInteraction& in);
    bool AskPrompt(const CInteraction& in, CSecret& answer);

    COperationWindowHost& host_;
    COperation& op_;
    COperationWindowOptions options_;

    std::map<int, std::wstring> strings_;       // loaded once: every LangStr is a call into Sally
    std::map<int, std::wstring> shownTexts_;    // what the controls show now (0 = title)
    std::map<int, std::wstring> problemTexts_;  // by Problem

    long long lastRefreshMs_ = 0;
    long long lastListMs_ = 0;
    bool refreshTimerArmed_ = false;
    bool itemsDirty_ = true;
    bool rebuildingList_ = false; // the list sends selection notifications while it is filled
    int shownPercent_ = -1;

    int modalDepth_ = 0;           // > 0 while a message box or question dialog runs its own loop on this thread
    bool closeRequested_ = false;  // destroy the window as soon as no modal loop is open
    bool handlingInteractions_ = false;
    bool interactionOpen_ = false; // a question dialog is open now
};

void COperationWindow::Run()
{
    HWND window = Create();
    if (window == nullptr)
        return;

    bool visible = true;
    if (!host_.OnWindowCreated(window, visible))
        DestroyWindow(window); // closed while it was being created; WM_DESTROY ends the loop below
    else
    {
        if (visible)
            ShowAndFront();
        PostMessageW(window, WM_APP_INTERACTION, 0, 0); // a question may have arrived while the thread was starting
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

INT_PTR COperationWindow::DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    try
    {
        return HandleMessage(uMsg, wParam, lParam);
    }
    catch (...)
    {
        TRACE_E("COperationWindow: unexpected exception in a window message.");
        return FALSE; // no exception may cross the Win32 boundary
    }
}

INT_PTR COperationWindow::HandleMessage(UINT uMsg, WPARAM wParam, LPARAM lParam)
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
        if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL) // Enter and Esc: the base class would destroy the window
        {
            if (LOWORD(wParam) == IDCANCEL)
                CloseOrHide();
            return TRUE;
        }
        OnCommand(LOWORD(wParam), HIWORD(wParam));
        return TRUE;

    case WM_CLOSE: // the X button: hide while the operation matters, close it when nothing is left to do
        CloseOrHide();
        return TRUE;

    case WM_NOTIFY:
    {
        const NMHDR* header = reinterpret_cast<const NMHDR*>(lParam);
        if (header->idFrom == IDC_OP_LIST && header->code == LVN_ITEMCHANGED && !rebuildingList_)
            UpdateSolveButton();
        // double-click on an item that waits for a decision = the Solve button (SolveSelected needs a selected item);
        // a double-click on the empty area (iItem -1) does nothing
        else if (header->idFrom == IDC_OP_LIST && header->code == NM_DBLCLK &&
                 reinterpret_cast<const NMITEMACTIVATE*>(lParam)->iItem >= 0 &&
                 IsWindowEnabled(GetDlgItem(HWindow, IDB_OP_SOLVE)))
            SolveSelected();
        break;
    }

    case WM_TIMER:
        if (wParam == TIMER_REFRESH)
        {
            KillTimer(HWindow, TIMER_REFRESH);
            refreshTimerArmed_ = false;
            RefreshNow();
        }
        else if (wParam == TIMER_SECOND)
            RefreshNow();
        return TRUE;

    case WM_APP_ITEMS:
        host_.pendingItems_ = false;
        itemsDirty_ = true;
        RequestRefresh();
        return TRUE;

    case WM_APP_PROGRESS:
        host_.pendingProgress_ = false;
        RequestRefresh();
        return TRUE;

    case WM_APP_STATE:
        host_.pendingState_ = false;
        OnState();
        return TRUE;

    case WM_APP_INTERACTION:
        host_.pendingInteraction_ = false;
        HandleInteractions();
        return TRUE;

    case WM_APP_SHOW:
        ShowAndFront();
        return TRUE;

    case WM_APP_HIDE:
        ShowWindow(HWindow, SW_HIDE);
        return TRUE;

    case WM_APP_CLOSE:
        RequestDestroy();
        return TRUE;

    case WM_DESTROY:
        KillTimer(HWindow, TIMER_SECOND);
        KillTimer(HWindow, TIMER_REFRESH);
        host_.OnWindowDestroyed(); // from here on nobody posts to this handle any more
        PostQuitMessage(0);        // ends the loop in Run
        break;
    }
    return CCenteredDialog::DialogProc(uMsg, wParam, lParam);
}

// ---- setup and small helpers ------------------------------------------------------------------------------------

const std::wstring& COperationWindow::Str(int id)
{
    auto it = strings_.find(id);
    if (it == strings_.end())
        it = strings_.emplace(id, LangStr(id)).first;
    return it->second;
}

void COperationWindow::SetTextIfChanged(int controlId, const std::wstring& text)
{
    auto it = shownTexts_.find(controlId);
    if (it != shownTexts_.end() && it->second == text)
        return;
    shownTexts_[controlId] = text;
    if (controlId == 0)
        SetWindowTextW(HWindow, text.c_str());
    else
        SetDlgItemTextW(HWindow, controlId, text.c_str());
}

void COperationWindow::EnableIfChanged(int controlId, bool enable)
{
    HWND control = GetDlgItem(HWindow, controlId);
    if (control != nullptr && (IsWindowEnabled(control) != FALSE) != enable)
        EnableWindow(control, enable ? TRUE : FALSE);
}

int COperationWindow::KindTitleId() const
{
    const COperationSettings& settings = op_.Settings();
    if (settings.direction == TransferDirection::Delete)
        return IDS_OP_TITLE_DELETE;
    if (settings.direction == TransferDirection::Chmod)
        return IDS_OP_TITLE_CHMOD;
    if (settings.direction == TransferDirection::Download)
        return settings.move ? IDS_OP_TITLE_MOVE_DOWNLOAD : IDS_OP_TITLE_DOWNLOAD;
    return settings.move ? IDS_OP_TITLE_MOVE_UPLOAD : IDS_OP_TITLE_UPLOAD;
}

bool COperationWindow::ErrorsOnly()
{
    return IsDlgButtonChecked(HWindow, IDX_OP_ERRORS_ONLY) == BST_CHECKED;
}

void COperationWindow::InitControls()
{
    CheckDlgButton(HWindow, IDX_OP_ERRORS_ONLY, options_.errorsOnly ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(HWindow, IDX_OP_CLOSE_WHEN_DONE, options_.closeWhenDone ? BST_CHECKED : BST_UNCHECKED);
    SetDlgItemTextW(HWindow, IDC_OP_SOURCE, options_.source.c_str());
    SetDlgItemTextW(HWindow, IDC_OP_TARGET, options_.target.c_str());
    SendDlgItemMessageW(HWindow, IDC_OP_PROGRESS, PBM_SETRANGE32, 0, 100);
    InitList();
    SetTimer(HWindow, TIMER_SECOND, 1000, nullptr);
    RefreshNow();
}

void COperationWindow::InitList()
{
    HWND list = GetDlgItem(HWindow, IDC_OP_LIST);
    ListView_SetExtendedListViewStyle(list, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
    ApplyDarkListHeader(list);
    RECT rc;
    GetClientRect(list, &rc);
    const int width = rc.right - rc.left - GetSystemMetrics(SM_CXVSCROLL);
    static const int titles[4] = {IDS_OP_COL_STATE, IDS_OP_COL_NAME, IDS_OP_COL_PROBLEM, IDS_OP_COL_SIZE};
    static const int percents[4] = {14, 38, 36, 12};
    for (int i = 0; i < 4; i++)
    {
        std::wstring title = Str(titles[i]);
        LVCOLUMNW column = {};
        column.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
        column.pszText = const_cast<wchar_t*>(title.c_str());
        column.cx = width * percents[i] / 100;
        column.iSubItem = i;
        SendMessageW(list, LVM_INSERTCOLUMNW, i, reinterpret_cast<LPARAM>(&column));
    }
}

// ---- refreshing --------------------------------------------------------------------------------------------------

// At most one repaint per 100 ms: too early means "do it when the 100 ms are over" (one timer, no matter how many
// notifications arrive meanwhile).
void COperationWindow::RequestRefresh()
{
    const long long sinceLast = NowMs() - lastRefreshMs_;
    if (sinceLast >= MinRepaintMs)
        RefreshNow();
    else if (!refreshTimerArmed_)
    {
        refreshTimerArmed_ = SetTimer(HWindow, TIMER_REFRESH, static_cast<UINT>(MinRepaintMs - sinceLast + 1), nullptr) != 0;
    }
}

void COperationWindow::RefreshNow()
{
    lastRefreshMs_ = NowMs();
    UpdateSummary();
    UpdateCurrentFile();
    if (itemsDirty_ && (ErrorsOnly() || lastRefreshMs_ - lastListMs_ >= MinAllItemsRebuildMs))
        RebuildList();
}

void COperationWindow::UpdateSummary()
{
    const CQueueSummary summary = op_.Summary();
    const COperationStats stats = op_.Statistics();
    const OperationStatus status =
        DecideStatus(summary.state, op_.IsPaused(), op_.WasCancelled(), op_.IsFinished());
    const bool over = IsWorkOver(summary.state);

    // a delete or chmod has no bytes: the progress is the share of the items found so far that are done
    const TransferDirection direction = op_.Settings().direction;
    const bool deleting = direction == TransferDirection::Delete || direction == TransferDirection::Chmod;
    int percent = deleting ? PercentDone(static_cast<uint64_t>(stats.filesDone), static_cast<uint64_t>(stats.filesTotal))
                           : PercentDone(stats.bytesDone, stats.bytesTotal);
    if (status == OperationStatus::Finished)
        percent = 100;
    if (percent != shownPercent_)
    {
        shownPercent_ = percent;
        SendDlgItemMessageW(HWindow, IDC_OP_PROGRESS, PBM_SETPOS, percent, 0);
    }

    const std::wstring kindTitle = Str(KindTitleId());
    const bool active = status == OperationStatus::Running || status == OperationStatus::Paused;
    SetTextIfChanged(0, active ? SPLFormatStringOwned(Str(IDS_OP_TITLE_PERCENT).c_str(), percent, kindTitle.c_str())
                               : kindTitle);

    const long long now = NowMs();
    const long long end = host_.endMs_.load();
    const long long elapsedMs = (end != 0 ? end : now) - host_.startMs_.load();
    if (deleting)
        SetTextIfChanged(IDC_OP_STATS,
                         SPLFormatStringOwned(Str(IDS_OP_STATS_DELETE).c_str(), stats.filesDone, stats.filesTotal,
                                              FormatDuration(elapsedMs / 1000).c_str()));
    else
        SetTextIfChanged(IDC_OP_STATS,
                         SPLFormatStringOwned(Str(IDS_OP_STATS).c_str(), FormatBytes(stats.bytesDone).c_str(),
                                              FormatBytes(stats.bytesTotal).c_str(), stats.filesDone, stats.filesTotal,
                                              FormatSpeed(over ? 0 : stats.bytesPerSecond).c_str(),
                                              FormatEta(over ? 0 : stats.etaSeconds).c_str(),
                                              FormatDuration(elapsedMs / 1000).c_str()));

    std::wstring text;
    if (interactionOpen_)
        text = Str(IDS_OP_STATUS_ASKING);
    else
    {
        switch (status)
        {
        case OperationStatus::Running:
            text = Str(IDS_OP_STATUS_RUNNING);
            break;
        case OperationStatus::Paused:
        {
            const CSshError pauseError = op_.PauseError();
            // a login problem paused it
            const std::wstring reason = pauseError.IsError() ? DescribeError(pauseError) : op_.PauseReason();
            text = reason.empty() ? Str(IDS_OP_STATUS_PAUSED)
                                  : SPLFormatStringOwned(Str(IDS_OP_STATUS_PAUSED_REASON).c_str(), reason.c_str());
            break;
        }
        case OperationStatus::Cancelling:
            text = Str(IDS_OP_STATUS_CANCELLING);
            break;
        case OperationStatus::Cancelled:
            text = Str(IDS_OP_STATUS_CANCELLED);
            break;
        case OperationStatus::Finished:
            text = Str(IDS_OP_STATUS_FINISHED);
            break;
        case OperationStatus::FinishedWithSkips:
            text = SPLFormatStringOwned(Str(IDS_OP_STATUS_FINISHED_SKIPS).c_str(), summary.counters.skipped);
            break;
        case OperationStatus::FinishedWithErrors:
            text = SPLFormatStringOwned(Str(IDS_OP_STATUS_FINISHED_ERRORS).c_str(), summary.counters.failed);
            break;
        case OperationStatus::NeedsUser:
            text = SPLFormatStringOwned(Str(IDS_OP_STATUS_NEEDS_USER).c_str(), summary.counters.userInput);
            break;
        }
    }
    SetTextIfChanged(IDC_OP_STATUS, text);
    UpdateButtons(summary, status);
}

void COperationWindow::UpdateButtons(const CQueueSummary& summary, OperationStatus status)
{
    const bool cancelled = op_.WasCancelled();
    const bool paused = status == OperationStatus::Paused;
    SetTextIfChanged(IDB_OP_PAUSE, Str(paused ? IDS_OP_RESUME : IDS_OP_PAUSE));
    EnableIfChanged(IDB_OP_PAUSE, !cancelled && (paused || status == OperationStatus::Running));
    EnableIfChanged(IDB_OP_CANCEL, !cancelled);
    EnableIfChanged(IDB_OP_RETRY, !cancelled && summary.counters.failed > 0);

    UpdateSolveButton();
}

// Enabled for a selected item that waits for a decision.
void COperationWindow::UpdateSolveButton()
{
    bool canSolve = false;
    const uint64_t uid = SelectedUid();
    if (options_.solveError && !op_.WasCancelled() && uid != 0)
    {
        CQueueItem item;
        canSolve = op_.Queue().GetItem(uid, item) &&
                   (item.state == ItemState::Failed || item.state == ItemState::UserInputNeeded);
    }
    EnableIfChanged(IDB_OP_SOLVE, canSolve);
}

void COperationWindow::UpdateCurrentFile()
{
    std::wstring name;
    const std::vector<CQueueItem> working = op_.Queue().ProcessingItems(8);
    const CQueueItem* shown = working.empty() ? nullptr : &working.front();
    for (const CQueueItem& item : working)
    {
        if (IsTransferType(item.type))
        {
            shown = &item;
            break;
        }
    }
    if (shown != nullptr)
        name = !shown->localPath.empty() ? shown->localPath : CNameCodec::Utf8().Decode(shown->remotePath);
    SetTextIfChanged(IDC_OP_FILE, name);
}

uint64_t COperationWindow::SelectedUid()
{
    HWND list = GetDlgItem(HWindow, IDC_OP_LIST);
    const int index = ListView_GetNextItem(list, -1, LVNI_SELECTED);
    if (index < 0)
        return 0;
    LVITEMW item = {};
    item.mask = LVIF_PARAM;
    item.iItem = index;
    if (!ListView_GetItem(list, &item))
        return 0;
    return static_cast<uint64_t>(item.lParam);
}

void COperationWindow::RebuildList()
{
    HWND list = GetDlgItem(HWindow, IDC_OP_LIST);
    const std::vector<CQueueItem> items = op_.Queue().Items(ErrorsOnly());
    const uint64_t selected = SelectedUid();
    const size_t shown = (std::min)(items.size(), MaxListRows);

    rebuildingList_ = true;
    SendMessageW(list, WM_SETREDRAW, FALSE, 0);
    ListView_DeleteAllItems(list);
    ListView_SetItemCount(list, static_cast<int>(shown + 1));
    int selectIndex = -1;
    for (size_t i = 0; i < shown; i++)
    {
        const CQueueItem& item = items[i];
        std::wstring state;
        switch (item.state)
        {
        case ItemState::Waiting:
            state = Str(IDS_OP_ITEM_WAITING);
            break;
        case ItemState::Processing:
            state = Str(IDS_OP_ITEM_PROCESSING);
            break;
        case ItemState::Delayed:
            state = Str(IDS_OP_ITEM_DELAYED);
            break;
        case ItemState::Done:
            state = Str(IDS_OP_ITEM_DONE);
            break;
        case ItemState::Skipped:
            state = Str(IDS_OP_ITEM_SKIPPED);
            break;
        case ItemState::Failed:
            state = Str(IDS_OP_ITEM_FAILED);
            break;
        case ItemState::UserInputNeeded:
            state = Str(IDS_OP_ITEM_USERINPUT);
            break;
        case ItemState::ForcedToFail:
            state = Str(IDS_OP_ITEM_FORCEDFAIL);
            break;
        }
        const std::wstring name = !item.localPath.empty() ? item.localPath : CNameCodec::Utf8().Decode(item.remotePath);
        auto cached = problemTexts_.find(static_cast<int>(item.problem));
        if (cached == problemTexts_.end())
            cached = problemTexts_.emplace(static_cast<int>(item.problem), ProblemText(item.problem)).first;
        std::wstring problem = cached->second;
        const std::wstring details = ItemDetails(item);
        if (!details.empty())
            problem = problem.empty() ? details : problem + L": " + details;
        const std::wstring size = IsTransferType(item.type) ? FormatBytes(item.size) : std::wstring();

        LVITEMW row = {};
        row.mask = LVIF_TEXT | LVIF_PARAM;
        row.iItem = static_cast<int>(i);
        row.pszText = const_cast<wchar_t*>(state.c_str());
        row.lParam = static_cast<LPARAM>(item.uid);
        SendMessageW(list, LVM_INSERTITEMW, 0, reinterpret_cast<LPARAM>(&row));
        ListView_SetItemText(list, static_cast<int>(i), 1, const_cast<wchar_t*>(name.c_str()));
        ListView_SetItemText(list, static_cast<int>(i), 2, const_cast<wchar_t*>(problem.c_str()));
        ListView_SetItemText(list, static_cast<int>(i), 3, const_cast<wchar_t*>(size.c_str()));
        if (item.uid == selected)
            selectIndex = static_cast<int>(i);
    }
    if (items.size() > shown)
    {
        const std::wstring more = SPLFormatStringOwned(Str(IDS_OP_MORE).c_str(), static_cast<int>(items.size() - shown));
        LVITEMW row = {};
        row.mask = LVIF_TEXT | LVIF_PARAM;
        row.iItem = static_cast<int>(shown);
        row.pszText = const_cast<wchar_t*>(L"");
        row.lParam = 0;
        SendMessageW(list, LVM_INSERTITEMW, 0, reinterpret_cast<LPARAM>(&row));
        ListView_SetItemText(list, static_cast<int>(shown), 1, const_cast<wchar_t*>(more.c_str()));
    }
    if (selectIndex >= 0)
        ListView_SetItemState(list, selectIndex, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
    SendMessageW(list, WM_SETREDRAW, TRUE, 0);
    rebuildingList_ = false;
    UpdateSolveButton();

    itemsDirty_ = false;
    lastListMs_ = NowMs();
}

// ---- commands ---------------------------------------------------------------------------------------------------

void COperationWindow::OnCommand(int id, int code)
{
    switch (id)
    {
    case IDB_OP_PAUSE:
        TogglePause();
        break;
    case IDB_OP_CANCEL:
        ConfirmCancel();
        break;
    case IDB_OP_SOLVE:
        SolveSelected();
        break;
    case IDB_OP_RETRY:
        op_.RetryAllFailed();
        itemsDirty_ = true;
        RefreshNow();
        break;
    case IDB_OP_HIDE:
        HideWindow();
        break;
    case IDX_OP_ERRORS_ONLY:
        if (code == BN_CLICKED)
        {
            itemsDirty_ = true;
            lastListMs_ = 0; // the user asked: rebuild now
            RefreshNow();
        }
        break;
    case IDX_OP_CLOSE_WHEN_DONE:
        if (code == BN_CLICKED)
        {
            const bool on = IsDlgButtonChecked(HWindow, IDX_OP_CLOSE_WHEN_DONE) == BST_CHECKED;
            host_.closeWhenDone_ = on;
            if (options_.closeWhenDoneChanged)
                options_.closeWhenDoneChanged(on);
            OnState(); // already finished: close now
        }
        break;
    }
}

void COperationWindow::HideWindow()
{
    host_.SetWantVisible(false);
    ShowWindow(HWindow, SW_HIDE);
}

// X and Esc. A running (or parked) operation goes on in the background, so its window only hides. One that is over and
// needs nothing more is closed for good; the operations registry then drops it from the operations list.
void COperationWindow::CloseOrHide()
{
    if (op_.IsDisposable())
        RequestDestroy();
    else
        HideWindow();
}

void COperationWindow::ShowAndFront()
{
    host_.SetWantVisible(true);
    ShowWindow(HWindow, IsIconic(HWindow) ? SW_RESTORE : SW_SHOW);
    SetForegroundWindow(HWindow); // may only flash the taskbar button when Sally is not in the foreground
}

void COperationWindow::TogglePause()
{
    if (op_.IsPaused())
        op_.Resume();
    else
        op_.Pause();
    RefreshNow();
}

void COperationWindow::ConfirmCancel()
{
    ++modalDepth_;
    // Sally's message box (dark mode aware, callable from this window's thread)
    const int answer = SalamanderGeneral->SalMessageBox(HWindow, Str(IDS_OP_CANCEL_CONFIRM).c_str(),
                                                        Str(IDS_PLUGINNAME).c_str(),
                                                        MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2);
    --modalDepth_;
    if (answer == IDYES)
        op_.RequestCancel(); // the window stays until the workers have ended (OnState closes it)
    FinishModal();
    if (HWindow != nullptr)
        RefreshNow();
}

void COperationWindow::SolveSelected()
{
    const uint64_t uid = SelectedUid();
    if (!options_.solveError || uid == 0)
        return;
    ++modalDepth_;
    try
    {
        options_.solveError(HWindow, op_, uid);
    }
    catch (...)
    {
        TRACE_E("COperationWindow: the solve-error callback threw.");
    }
    --modalDepth_;
    FinishModal();
    if (HWindow != nullptr)
    {
        itemsDirty_ = true;
        RefreshNow();
    }
}

// A state message: refresh at once and close the window when its job is over.
void COperationWindow::OnState()
{
    itemsDirty_ = true;
    RefreshNow();
    const bool cancelled = op_.WasCancelled();
    if (cancelled)
    {
        if (op_.IsFinished()) // the workers have ended: nothing is left to watch
            RequestDestroy();
    }
    else if (op_.State() == OperationState::Finished && IsDlgButtonChecked(HWindow, IDX_OP_CLOSE_WHEN_DONE) == BST_CHECKED)
        RequestDestroy();
}

// Destroys the window now, or when the open message boxes / question dialogs have been closed (they are closed here, one
// after the other: "reject"/"no").
void COperationWindow::RequestDestroy()
{
    closeRequested_ = true;
    if (modalDepth_ > 0)
    {
        CloseTopmostModal();
        return;
    }
    DestroyWindow(HWindow);
}

namespace
{
struct CFindModal
{
    HWND mainWindow;
    HWND found;
};

// Of all windows of this thread only the topmost modal dialog is visible AND enabled: a modal dialog disables its owner.
BOOL CALLBACK FindEnabledDialogProc(HWND window, LPARAM param)
{
    CFindModal* find = reinterpret_cast<CFindModal*>(param);
    if (window != find->mainWindow && IsWindowVisible(window) && IsWindowEnabled(window))
    {
        find->found = window;
        return FALSE;
    }
    return TRUE;
}
} // namespace

// Ends the modal dialog or message box on top (not the window itself). Its owner code returns, decrements modalDepth_ and
// calls FinishModal, which ends the next one, until the window can be destroyed. False when none was found.
bool COperationWindow::CloseTopmostModal()
{
    CFindModal find = {HWindow, nullptr};
    EnumThreadWindows(GetCurrentThreadId(), FindEnabledDialogProc, reinterpret_cast<LPARAM>(&find));
    if (find.found == nullptr)
        return false;
    EndDialog(find.found, IDCANCEL); // works for message boxes and for our dialogs; we are on the thread that owns them
    return true;
}

void COperationWindow::FinishModal()
{
    if (!closeRequested_ || HWindow == nullptr)
        return;
    if (modalDepth_ == 0)
        DestroyWindow(HWindow);
    else
        CloseTopmostModal(); // a dialog below the one that just ended
}

// ---- questions from the workers ---------------------------------------------------------------------------------

// Answers every waiting question, one after the other. While a dialog is open this thread keeps dispatching messages, so
// this function can be entered again; the outer call picks up whatever arrived meanwhile.
void COperationWindow::HandleInteractions()
{
    if (handlingInteractions_)
        return;
    handlingInteractions_ = true;
    try
    {
        CInteraction in;
        while (HWindow != nullptr && op_.TryGetInteraction(in))
            AnswerInteraction(in);
    }
    catch (...)
    {
        TRACE_E("COperationWindow: unexpected exception while answering a question.");
    }
    handlingInteractions_ = false;
    FinishModal();
}

// ALWAYS answers (reject/cancel when the window is closing or anything fails), so no worker is left waiting.
void COperationWindow::AnswerInteraction(const CInteraction& in)
{
    HostKeyDecision decision = HostKeyDecision::Reject;
    bool ok = false;
    CSecret answer;
    if (!closeRequested_)
    {
        if (!IsWindowVisible(HWindow)) // hidden: surface the window, the user has to decide
        {
            host_.SetWantVisible(true);
            ShowWindow(HWindow, SW_SHOWNOACTIVATE);
            FLASHWINFO flash = {sizeof(flash), HWindow, FLASHW_ALL | FLASHW_TIMERNOFG, 3, 0};
            FlashWindowEx(&flash);
        }
        interactionOpen_ = true;
        RefreshNow();
        ++modalDepth_;
        try
        {
            if (in.kind == CInteraction::Kind::HostKey)
                decision = AskHostKey(in);
            else if (in.kind == CInteraction::Kind::Prompt)
                ok = AskPrompt(in, answer);
        }
        catch (...)
        {
            decision = HostKeyDecision::Reject;
            ok = false;
            answer.clear();
        }
        --modalDepth_;
        interactionOpen_ = false;
    }

    try
    {
        if (in.kind == CInteraction::Kind::HostKey)
            op_.AnswerHostKey(decision);
        else if (in.kind == CInteraction::Kind::Prompt)
            op_.AnswerPrompt(ok, ok ? std::move(answer) : CSecret());
    }
    catch (...)
    {
        // last resort: the worker must not stay blocked
        try
        {
            if (in.kind == CInteraction::Kind::HostKey)
                op_.AnswerHostKey(HostKeyDecision::Reject);
            else if (in.kind == CInteraction::Kind::Prompt)
                op_.AnswerPrompt(false, CSecret());
        }
        catch (...)
        {
        }
    }

    if (HWindow != nullptr)
        RefreshNow();
}

HostKeyDecision COperationWindow::AskHostKey(const CInteraction& in)
{
    const CNameCodec utf8 = CNameCodec::Utf8();
    CHostKeyDialog dialog(HWindow, utf8.Decode(FormatHostPort(in.host, in.port)), utf8.Decode(in.keyType),
                          utf8.Decode(in.fingerprint), in.changed);
    return dialog.Run();
}

bool COperationWindow::AskPrompt(const CInteraction& in, CSecret& answer)
{
    const CNameCodec utf8 = CNameCodec::Utf8();
    CPromptItem item;
    if (in.promptKind == PromptKind::KeyPassphrase)
        item.text = SPLFormatStringOwned(Str(IDS_PROMPT_PASSPHRASE).c_str(), utf8.Decode(in.promptText).c_str());
    else
        item.text = utf8.Decode(in.promptText); // the server's own text
    item.echo = in.echo;

    // name the connection: the user cannot tell from the dialog which of several running transfers asks
    std::wstring instruction = utf8.Decode(in.instruction);
    if (!options_.hostName.empty())
    {
        const std::wstring hostLine = SPLFormatStringOwned(Str(IDS_OP_PROMPT_HOST).c_str(), options_.hostName.c_str());
        instruction = instruction.empty() ? hostLine : hostLine + L"\n" + instruction;
    }
    CPromptDialog dialog(HWindow, utf8.Decode(in.name), instruction, {item});
    std::vector<CSecret> answers;
    if (!dialog.Run(answers) || answers.size() != 1)
        return false;
    answer = std::move(answers[0]);
    return true;
}

// ---- COperationWindowHost ---------------------------------------------------------------------------------------

CWindowedOperation COperationWindowHost::Create(COperationSettings settings, CPanelConnectParams params,
                                                std::vector<CQueueItem> roots, COperationWindowOptions options)
{
    std::shared_ptr<COperationWindowHost> host(new COperationWindowHost(std::move(options)));
    COperation* raw = new COperation(std::move(settings), std::move(params), std::move(roots));
    raw->SetObserver(host.get());
    host->operation_ = raw;

    // The deleter captures 'host': the observer outlives the operation. It closes the window first (the window thread
    // uses the raw pointer), then destroys the operation (cancels and joins the workers).
    CWindowedOperation result;
    result.operation = std::shared_ptr<COperation>(raw, [host](COperation* operation) {
        host->Shutdown();
        delete operation;
    });
    result.window = host;
    return result;
}

COperationWindowHost::COperationWindowHost(COperationWindowOptions options) : options_(std::move(options))
{
    startMs_ = NowMs();
    closeWhenDone_ = options_.closeWhenDone;
}

COperationWindowHost::~COperationWindowHost()
{
    try
    {
        Shutdown();
    }
    catch (...)
    {
    }
}

bool COperationWindowHost::Show()
{
    std::lock_guard<std::mutex> lifecycle(lifecycleMutex_);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (shutdown_)
            return false;
        wantVisible_ = true;
        if (running_)
        {
            if (hwnd_ != nullptr)
                PostMessageW(hwnd_, WM_APP_SHOW, 0, 0); // else the thread is still starting and shows the window itself
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
        }
        pendingItems_ = pendingProgress_ = pendingState_ = pendingInteraction_ = false;
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

void COperationWindowHost::Hide()
{
    std::lock_guard<std::mutex> lock(mutex_);
    wantVisible_ = false;
    if (running_ && hwnd_ != nullptr)
        PostMessageW(hwnd_, WM_APP_HIDE, 0, 0);
}

void COperationWindowHost::Close()
{
    std::lock_guard<std::mutex> lifecycle(lifecycleMutex_);
    CloseLocked();
}

void COperationWindowHost::Shutdown()
{
    std::lock_guard<std::mutex> lifecycle(lifecycleMutex_);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        shutdown_ = true;
    }
    CloseLocked();
}

void COperationWindowHost::CloseLocked()
{
    if (thread_.joinable() && thread_.get_id() == std::this_thread::get_id())
        return; // called from a window callback: joining oneself would hang
    {
        std::lock_guard<std::mutex> lock(mutex_);
        closing_ = true; // stops the bridge from posting; a window being created destroys itself
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

bool COperationWindowHost::IsWindowOpen() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return running_;
}

void COperationWindowHost::WindowThreadMain()
{
    try
    {
        TraceAttachCurrentThread();
        SetThreadNameInVCAndTrace(L"SFTP operation window");
        COperationWindowOptions options = options_;
        options.closeWhenDone = closeWhenDone_.load();
        COperationWindow window(*this, *operation_, options);
        window.Run();
    }
    catch (...)
    {
        TRACE_E("COperationWindowHost: the window thread ended with an exception.");
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        hwnd_ = nullptr;
        running_ = false;
    }
    // A worker may have asked while this thread started, and a window that could not be created never answers it.
    try
    {
        RejectPendingInteraction();
    }
    catch (...)
    {
    }
}

bool COperationWindowHost::OnWindowCreated(HWND window, bool& wantVisible)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (closing_ || shutdown_)
        return false;
    hwnd_ = window;
    wantVisible = wantVisible_;
    return true;
}

void COperationWindowHost::OnWindowDestroyed()
{
    std::lock_guard<std::mutex> lock(mutex_);
    hwnd_ = nullptr;
    running_ = false; // a Show from now on starts a new window (after joining this thread)
}

void COperationWindowHost::SetWantVisible(bool visible)
{
    std::lock_guard<std::mutex> lock(mutex_);
    wantVisible_ = visible;
}

// ---- the bridge: called by the workers --------------------------------------------------------------------------

// The only place that posts to the window. The handle is read and posted to under mutex_, and the window thread clears
// it (OnWindowDestroyed, also under mutex_) before the window is gone, so a message never goes to a destroyed handle.
void COperationWindowHost::Post(std::atomic<bool>& pending, UINT message)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (hwnd_ == nullptr || closing_)
        return;
    if (pending.exchange(true)) // one message of this kind is on its way
        return;
    if (!PostMessageW(hwnd_, message, 0, 0))
        pending = false;
}

void COperationWindowHost::NoteStateForElapsedTime()
{
    const long long now = NowMs();
    if (IsWorkOver(operation_->State()))
    {
        long long running = 0;
        endMs_.compare_exchange_strong(running, now); // only the first "over" counts
    }
    else
    {
        const long long end = endMs_.exchange(0);
        if (end != 0)
            startMs_ += now - end; // a new run: the idle time does not count
    }
}

void COperationWindowHost::OnItemChanged(uint64_t)
{
    Post(pendingItems_, WM_APP_ITEMS);
}

void COperationWindowHost::OnProgress()
{
    Post(pendingProgress_, WM_APP_PROGRESS);
}

void COperationWindowHost::OnStateChanged()
{
    NoteStateForElapsedTime();
    Post(pendingState_, WM_APP_STATE);
}

void COperationWindowHost::OnFinished()
{
    NoteStateForElapsedTime();
    Post(pendingState_, WM_APP_STATE);
    if (options_.finished)
    {
        try
        {
            options_.finished();
        }
        catch (...)
        {
            // the owner's callback must not break the worker
        }
    }
}

void COperationWindowHost::OnInteractionPending()
{
    bool reject = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (shutdown_ || closing_)
            reject = true;
        else if (hwnd_ != nullptr)
        {
            if (!pendingInteraction_.exchange(true) && !PostMessageW(hwnd_, WM_APP_INTERACTION, 0, 0))
                pendingInteraction_ = false;
        }
        else if (!running_)
            reject = true; // no window and none coming: nobody could answer
        // else: the window thread is starting; the window asks for waiting questions when it exists
    }
    if (reject)
        RejectPendingInteraction();
}

// Called on a worker thread; the operation is alive (it is calling us).
void COperationWindowHost::RejectPendingInteraction()
{
    CInteraction in;
    if (!operation_->TryGetInteraction(in))
        return;
    if (in.kind == CInteraction::Kind::HostKey)
        operation_->AnswerHostKey(HostKeyDecision::Reject);
    else if (in.kind == CInteraction::Kind::Prompt)
        operation_->AnswerPrompt(false, CSecret());
}
