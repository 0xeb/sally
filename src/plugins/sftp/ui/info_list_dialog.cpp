#include "precomp.h"

#include "dark_controls.h"
#include "info_list_dialog.h"

std::wstring InfoRowsToText(const std::vector<CInfoRow>& rows)
{
    std::wstring text;
    for (const CInfoRow& row : rows)
    {
        text += row.label;
        text += L": ";
        text += row.value;
        text += L"\r\n";
    }
    return text;
}

CInfoListDialog::CInfoListDialog(HWND parent, int dialogId, UINT helpID, std::wstring title, std::vector<CInfoRow> rows)
    : CCenteredDialog(HLanguage, dialogId, helpID, parent), title_(std::move(title)), rows_(std::move(rows))
{
}

void CInfoListDialog::Run()
{
    try
    {
        Execute();
    }
    catch (...)
    {
        TRACE_E("CInfoListDialog: unexpected exception.");
    }
}

void CInfoListDialog::InitControls()
{
    SetWindowTextW(HWindow, title_.c_str());
    ApplyDarkButtons(HWindow);
    HWND list = GetDlgItem(HWindow, IDC_INFO_LIST);
    ListView_SetExtendedListViewStyle(list, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
    ApplyDarkListHeader(list);

    RECT rc;
    GetClientRect(list, &rc);
    const int width = rc.right - rc.left - GetSystemMetrics(SM_CXVSCROLL);
    const int titles[2] = {IDS_INFO_COL_PROPERTY, IDS_INFO_COL_VALUE};
    const int widths[2] = {width * 30 / 100, width * 70 / 100};
    for (int i = 0; i < 2; i++)
    {
        std::wstring title = LangStr(titles[i]);
        LVCOLUMNW column = {};
        column.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
        column.pszText = const_cast<wchar_t*>(title.c_str());
        column.cx = widths[i];
        column.iSubItem = i;
        SendMessageW(list, LVM_INSERTCOLUMNW, i, reinterpret_cast<LPARAM>(&column));
    }
    for (size_t i = 0; i < rows_.size(); i++)
    {
        LVITEMW item = {};
        item.mask = LVIF_TEXT;
        item.iItem = static_cast<int>(i);
        item.pszText = const_cast<wchar_t*>(rows_[i].label.c_str());
        SendMessageW(list, LVM_INSERTITEMW, 0, reinterpret_cast<LPARAM>(&item));
        ListView_SetItemText(list, static_cast<int>(i), 1, const_cast<wchar_t*>(rows_[i].value.c_str()));
    }
    SetFocus(GetDlgItem(HWindow, IDCANCEL));
}

void CInfoListDialog::CopyToClipboard()
{
    const std::wstring text = InfoRowsToText(rows_);
    SalamanderGeneral->CopyTextToClipboard(text.c_str(), static_cast<int>(text.size()), TRUE, HWindow);
}

INT_PTR CInfoListDialog::DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    try
    {
        if (uMsg == WM_COMMAND && LOWORD(wParam) == IDB_INFO_COPY && HIWORD(wParam) == BN_CLICKED)
        {
            CopyToClipboard();
            return TRUE;
        }
        const INT_PTR result = CCenteredDialog::DialogProc(uMsg, wParam, lParam);
        if (uMsg == WM_INITDIALOG)
        {
            InitControls();
            return FALSE; // the focus was set by hand
        }
        return result;
    }
    catch (...)
    {
        TRACE_E("CInfoListDialog: unexpected exception in a window message.");
    }
    return FALSE;
}
