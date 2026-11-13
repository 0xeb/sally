#include "precomp.h"

#include "operations_dialog.h"

#include "dark_controls.h"

#include "encoding.h"
#include "progress_format.h"

namespace
{
const UINT_PTR TIMER_REFRESH = 1;
}

COperationsDialog::COperationsDialog(HWND parent, COperationsList& operations, COperationsDialogCallbacks callbacks)
    : CCenteredDialog(HLanguage, IDD_OPERATIONS, parent), operations_(operations), callbacks_(std::move(callbacks))
{
}

void COperationsDialog::Run()
{
    try
    {
        Execute();
    }
    catch (...)
    {
        TRACE_E("COperationsDialog: unexpected exception.");
    }
}

INT_PTR COperationsDialog::DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    try
    {
        switch (uMsg)
        {
        case WM_INITDIALOG:
        {
            const INT_PTR result = CCenteredDialog::DialogProc(uMsg, wParam, lParam);
            InitList();
            Refresh();
            SetTimer(HWindow, TIMER_REFRESH, 1000, nullptr);
            return result;
        }

        case WM_TIMER:
            if (wParam == TIMER_REFRESH)
                Refresh();
            return TRUE;

        case WM_NOTIFY:
        {
            const NMHDR* header = reinterpret_cast<const NMHDR*>(lParam);
            if (header->idFrom == IDC_OPS_LIST)
            {
                if (header->code == LVN_ITEMCHANGED)
                    UpdateButtons();
                else if (header->code == NM_DBLCLK)
                    ShowSelected();
            }
            break;
        }

        case WM_COMMAND:
            switch (LOWORD(wParam))
            {
            case IDB_OPS_SHOW:
            case IDOK:
                ShowSelected();
                return TRUE;
            case IDB_OPS_CANCEL:
                CancelSelected();
                return TRUE;
            }
            break;

        case WM_DESTROY:
            KillTimer(HWindow, TIMER_REFRESH);
            break;
        }
    }
    catch (...)
    {
        TRACE_E("COperationsDialog: unexpected exception in a window message.");
        return FALSE;
    }
    return CCenteredDialog::DialogProc(uMsg, wParam, lParam);
}

void COperationsDialog::InitList()
{
    HWND list = GetDlgItem(HWindow, IDC_OPS_LIST);
    ListView_SetExtendedListViewStyle(list, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
    ApplyDarkListHeader(list);
    RECT rc;
    GetClientRect(list, &rc);
    const int width = rc.right - rc.left - GetSystemMetrics(SM_CXVSCROLL);
    const int titles[3] = {IDS_OPS_COL_OPERATION, IDS_OPS_COL_STATE, IDS_OPS_COL_DESCRIPTION};
    const int percents[3] = {22, 24, 54};
    for (int i = 0; i < 3; i++)
    {
        std::wstring title = LangStr(titles[i]);
        LVCOLUMNW column = {};
        column.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
        column.pszText = const_cast<wchar_t*>(title.c_str());
        column.cx = width * percents[i] / 100;
        column.iSubItem = i;
        SendMessageW(list, LVM_INSERTCOLUMNW, i, reinterpret_cast<LPARAM>(&column));
    }
}

std::wstring COperationsDialog::TitleOf(const COperationInfo& info) const
{
    const COperationSettings& settings = info.operation->Settings();
    if (settings.direction == TransferDirection::Delete)
        return LangStr(IDS_OP_TITLE_DELETE);
    if (settings.direction == TransferDirection::Chmod)
        return LangStr(IDS_OP_TITLE_CHMOD);
    if (settings.direction == TransferDirection::Download)
        return LangStr(settings.move ? IDS_OP_TITLE_MOVE_DOWNLOAD : IDS_OP_TITLE_DOWNLOAD);
    return LangStr(settings.move ? IDS_OP_TITLE_MOVE_UPLOAD : IDS_OP_TITLE_UPLOAD);
}

std::wstring COperationsDialog::StateOf(const COperationInfo& info) const
{
    const CQueueSummary summary = info.operation->Summary();
    switch (DecideStatus(info.state, info.paused, info.cancelled, info.finished))
    {
    case OperationStatus::Running:
        return LangStr(IDS_OP_STATUS_RUNNING);
    case OperationStatus::Paused:
        return LangStr(IDS_OP_STATUS_PAUSED);
    case OperationStatus::Cancelling:
        return LangStr(IDS_OP_STATUS_CANCELLING);
    case OperationStatus::Cancelled:
        return LangStr(IDS_OP_STATUS_CANCELLED);
    case OperationStatus::Finished:
        return LangStr(IDS_OP_STATUS_FINISHED);
    case OperationStatus::FinishedWithSkips:
        return SPLFormatStringOwned(LangStr(IDS_OP_STATUS_FINISHED_SKIPS).c_str(), summary.counters.skipped);
    case OperationStatus::FinishedWithErrors:
        return SPLFormatStringOwned(LangStr(IDS_OP_STATUS_FINISHED_ERRORS).c_str(), summary.counters.failed);
    case OperationStatus::NeedsUser:
        return SPLFormatStringOwned(LangStr(IDS_OP_STATUS_NEEDS_USER).c_str(), summary.counters.userInput);
    }
    return std::wstring();
}

std::wstring COperationsDialog::DescriptionOf(const COperationInfo& info) const
{
    if (callbacks_.describe)
    {
        const std::wstring text = callbacks_.describe(info.uid);
        if (!text.empty())
            return text;
    }
    // fallback: the two roots of the settings (the selected items themselves are not named here)
    const COperationSettings& settings = info.operation->Settings();
    const std::wstring remote = CNameCodec::Utf8().Decode(settings.remoteRoot);
    if (settings.direction == TransferDirection::Delete || settings.direction == TransferDirection::Chmod)
        return remote;
    if (settings.direction == TransferDirection::Download)
        return SPLFormatStringOwned(LangStr(IDS_OPS_DESCRIPTION).c_str(), remote.c_str(), settings.localRoot.c_str());
    return SPLFormatStringOwned(LangStr(IDS_OPS_DESCRIPTION).c_str(), settings.localRoot.c_str(), remote.c_str());
}

uint64_t COperationsDialog::SelectedUid() const
{
    HWND list = GetDlgItem(HWindow, IDC_OPS_LIST);
    const int index = ListView_GetNextItem(list, -1, LVNI_SELECTED);
    if (index < 0)
        return 0;
    LVITEMW item = {};
    item.mask = LVIF_PARAM;
    item.iItem = index;
    return ListView_GetItem(list, &item) ? static_cast<uint64_t>(item.lParam) : 0;
}

// Rebuilds the rows from a snapshot of the list and keeps the selection (by uid).
void COperationsDialog::Refresh()
{
    HWND list = GetDlgItem(HWindow, IDC_OPS_LIST);
    const uint64_t selected = SelectedUid();
    const std::vector<COperationInfo> infos = operations_.Snapshot();

    SendMessageW(list, WM_SETREDRAW, FALSE, 0);
    ListView_DeleteAllItems(list);
    int selectIndex = -1;
    for (size_t i = 0; i < infos.size(); i++)
    {
        const int row = static_cast<int>(i);
        const std::wstring title = TitleOf(infos[i]);
        const std::wstring state = StateOf(infos[i]);
        const std::wstring description = DescriptionOf(infos[i]);

        LVITEMW item = {};
        item.mask = LVIF_TEXT | LVIF_PARAM;
        item.iItem = row;
        item.pszText = const_cast<wchar_t*>(title.c_str());
        item.lParam = static_cast<LPARAM>(infos[i].uid);
        SendMessageW(list, LVM_INSERTITEMW, 0, reinterpret_cast<LPARAM>(&item));
        ListView_SetItemText(list, row, 1, const_cast<wchar_t*>(state.c_str()));
        ListView_SetItemText(list, row, 2, const_cast<wchar_t*>(description.c_str()));
        if (infos[i].uid == selected)
            selectIndex = row;
    }
    if (selectIndex < 0 && !infos.empty())
        selectIndex = 0;
    if (selectIndex >= 0)
        ListView_SetItemState(list, selectIndex, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
    SendMessageW(list, WM_SETREDRAW, TRUE, 0);
    UpdateButtons();
}

void COperationsDialog::UpdateButtons()
{
    const uint64_t uid = SelectedUid();
    bool canCancel = false;
    if (uid != 0)
    {
        const std::shared_ptr<COperation> operation = operations_.Get(uid);
        canCancel = operation != nullptr && !operation->WasCancelled();
    }
    EnableWindow(GetDlgItem(HWindow, IDB_OPS_SHOW), uid != 0 && callbacks_.showWindow != nullptr);
    EnableWindow(GetDlgItem(HWindow, IDB_OPS_CANCEL), canCancel);
}

// Show reopens the operation window; the list closes so the window gets the focus.
void COperationsDialog::ShowSelected()
{
    const uint64_t uid = SelectedUid();
    if (uid == 0 || !callbacks_.showWindow)
        return;
    callbacks_.showWindow(uid);
    EndDialog(HWindow, IDOK);
}

void COperationsDialog::CancelSelected()
{
    const uint64_t uid = SelectedUid();
    const std::shared_ptr<COperation> operation = uid != 0 ? operations_.Get(uid) : nullptr;
    if (operation == nullptr || operation->WasCancelled())
        return;
    const int answer = SalamanderGeneral->SalMessageBox(HWindow, LangStr(IDS_OP_CANCEL_CONFIRM).c_str(),
                                                        LangStr(IDS_PLUGINNAME).c_str(),
                                                        MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2);
    if (answer == IDYES)
        operation->RequestCancel(); // returns at once; the next refresh shows "Cancelling"
    Refresh();
}

void ShowOperationsDialog(HWND parent, COperationsList& operations, COperationsDialogCallbacks callbacks)
{
    try
    {
        COperationsDialog dialog(parent, operations, std::move(callbacks));
        dialog.Run();
    }
    catch (...)
    {
        TRACE_E("ShowOperationsDialog: unexpected exception.");
    }
}
