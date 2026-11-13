#include "precomp.h"

#include <algorithm>

#include "site_list_dialog.h"

#include "dark_controls.h"

namespace
{
// Small input dialog of the Rename command.
class CSiteNameDialog : public CCenteredDialog
{
public:
    CSiteNameDialog(HWND parent, const CSiteList& sites, const std::wstring& current)
        : CCenteredDialog(HLanguage, IDD_SITENAME, parent), sites_(sites), current_(current), name_(current), trimmed_(current)
    {
    }

    bool Run(std::wstring& newName)
    {
        if (Execute() != IDOK)
            return false;
        newName = trimmed_; // set by Validate, which runs before the dialog closes with OK
        return true;
    }

    void Transfer(CTransferInfo& ti) override { ti.EditLine(IDE_SITENAME_NAME, name_); }

    void Validate(CTransferInfo& ti) override
    {
        std::wstring text;
        ti.EditLine(IDE_SITENAME_NAME, text);
        if (!ti.IsGood())
            return;
        // trimmed like the site edit dialog does
        const size_t first = text.find_first_not_of(L" \t");
        text = first == std::wstring::npos ? std::wstring() : text.substr(first, text.find_last_not_of(L" \t") - first + 1);

        std::wstring error;
        const SiteError nameError = ValidateSiteName(text);
        if (nameError != SiteError::None)
            error = SiteErrorText(nameError);
        else
        {
            const CSite* other = sites_.Find(text);
            if (other != nullptr && !SiteNamesEqual(other->name, current_))
                error = LangStr(IDS_SITE_ERR_NAME_TAKEN);
        }
        if (!error.empty())
        {
            SalamanderGeneral->SalMessageBox(HWindow, error.c_str(), LangStr(IDS_PLUGINNAME).c_str(),
                                             MB_OK | MB_ICONEXCLAMATION);
            ti.ErrorOn(IDE_SITENAME_NAME);
            return;
        }
        trimmed_ = text;
    }

protected:
    INT_PTR DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam) override
    {
        if (uMsg == WM_INITDIALOG)
        {
            CCenteredDialog::DialogProc(uMsg, wParam, lParam);
            SetWindowTextW(HWindow, LangStr(IDS_SITENAME_TITLE).c_str());
            return TRUE;
        }
        return CCenteredDialog::DialogProc(uMsg, wParam, lParam);
    }

private:
    const CSiteList& sites_;
    std::wstring current_;
    std::wstring name_;
    std::wstring trimmed_;
};
} // namespace

CSiteListDialog::CSiteListDialog(HWND parent, ISiteStore& store)
    : CCenteredDialog(HLanguage, IDD_SITELIST, parent), store_(store)
{
}

bool CSiteListDialog::Run(std::wstring& path)
{
    try
    {
        INITCOMMONCONTROLSEX icc = {sizeof(icc), ICC_LISTVIEW_CLASSES};
        InitCommonControlsEx(&icc);
        path_.clear();
        if (Execute() != IDOK || path_.empty())
            return false;
        path = path_;
        return true;
    }
    catch (...)
    {
        TRACE_E("CSiteListDialog::Run: out of memory.");
        return false;
    }
}

void CSiteListDialog::InitDialog()
{
    SetWindowTextW(HWindow, LangStr(IDS_SITELIST_TITLE).c_str());

    HWND list = GetDlgItem(HWindow, IDC_SITELIST_LIST);
    ListView_SetExtendedListViewStyle(list, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
    ApplyDarkListHeader(list);
    RECT rc;
    GetClientRect(list, &rc);
    const int width = rc.right - rc.left - GetSystemMetrics(SM_CXVSCROLL);
    static const int titles[3] = {IDS_SITELIST_COL_NAME, IDS_SITELIST_COL_HOST, IDS_SITELIST_COL_USER};
    static const int percents[3] = {34, 40, 26};
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
    SetDlgItemTextW(HWindow, IDE_SITELIST_QUICK, quick_.c_str());
    FillList(selected_);
    SetFocus(list);
}

// Rebuilds the list from the store (in stored order) and selects 'select' (or the first site).
void CSiteListDialog::FillList(const std::wstring& select)
{
    HWND list = GetDlgItem(HWindow, IDC_SITELIST_LIST);
    SendMessageW(list, WM_SETREDRAW, FALSE, 0);
    ListView_DeleteAllItems(list);
    const std::vector<CSite>& sites = store_.Sites().Sites();
    int selectIndex = sites.empty() ? -1 : 0;
    for (size_t i = 0; i < sites.size(); i++)
    {
        const CSite& site = sites[i];
        const std::wstring host = SiteHostColumn(site);
        LVITEMW item = {};
        item.mask = LVIF_TEXT;
        item.iItem = static_cast<int>(i);
        item.pszText = const_cast<wchar_t*>(site.name.c_str());
        SendMessageW(list, LVM_INSERTITEMW, 0, reinterpret_cast<LPARAM>(&item));
        ListView_SetItemText(list, static_cast<int>(i), 1, const_cast<wchar_t*>(host.c_str()));
        ListView_SetItemText(list, static_cast<int>(i), 2, const_cast<wchar_t*>(site.user.c_str()));
        if (!select.empty() && SiteNamesEqual(site.name, select))
            selectIndex = static_cast<int>(i);
    }
    if (selectIndex >= 0)
    {
        ListView_SetItemState(list, selectIndex, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
        ListView_EnsureVisible(list, selectIndex, FALSE);
    }
    SendMessageW(list, WM_SETREDRAW, TRUE, 0);
    UpdateButtons();
}

std::wstring CSiteListDialog::SelectedName() const
{
    HWND list = GetDlgItem(HWindow, IDC_SITELIST_LIST);
    const int index = ListView_GetNextItem(list, -1, LVNI_SELECTED);
    const CSiteList& sites = store_.Sites();
    if (index < 0 || static_cast<size_t>(index) >= sites.Count())
        return std::wstring();
    return sites.At(static_cast<size_t>(index)).name;
}

std::wstring CSiteListDialog::QuickText() const
{
    wchar_t buffer[1024];
    GetDlgItemTextW(HWindow, IDE_SITELIST_QUICK, buffer, ARRAYSIZE(buffer));
    return buffer;
}

void CSiteListDialog::UpdateButtons()
{
    const bool hasSelection = !SelectedName().empty();
    for (int id : {IDB_SITELIST_EDIT, IDB_SITELIST_DELETE, IDB_SITELIST_RENAME, IDB_SITELIST_DUPLICATE})
        EnableWindow(GetDlgItem(HWindow, id), hasSelection);
    // Connect also works for the quick connect line; Enter must keep working, so it stays enabled when it has text
    EnableWindow(GetDlgItem(HWindow, IDOK), hasSelection || !QuickText().empty());
}

void CSiteListDialog::ListChanged(const std::wstring& select)
{
    FillList(select);
    // Salamander otherwise saves the configuration when it exits; do it now so a crash does not lose the sites
    store_.SaveNow();
}

bool CSiteListDialog::RefuseIfReadOnly()
{
    if (!store_.IsReadOnly())
        return false;
    SalamanderGeneral->SalMessageBox(HWindow, LangStr(IDS_SITES_READONLY).c_str(), LangStr(IDS_PLUGINNAME).c_str(),
                                     MB_OK | MB_ICONINFORMATION);
    return true;
}

void CSiteListDialog::OnConnect()
{
    // Enter in the quick connect line connects to what is typed; anywhere else to the selected site
    const std::wstring quick = QuickText();
    const std::wstring name = SelectedName();
    const bool quickHasFocus = GetFocus() == GetDlgItem(HWindow, IDE_SITELIST_QUICK);
    if (!quick.empty() && (quickHasFocus || name.empty()))
    {
        CSftpPath parsed;
        if (!ParseQuickConnect(quick, parsed))
        {
            SalamanderGeneral->SalMessageBox(HWindow, LangStr(IDS_SITELIST_BAD_QUICK).c_str(),
                                             LangStr(IDS_PLUGINNAME).c_str(), MB_OK | MB_ICONEXCLAMATION);
            SetFocus(GetDlgItem(HWindow, IDE_SITELIST_QUICK));
            return;
        }
        path_ = FormatSftpPath(parsed);
    }
    else if (!name.empty())
        path_ = SiteConnectPath(name);
    else
        return;

    selected_ = name;
    quick_ = quick;
    EndDialog(HWindow, IDOK);
}

// Stores or drops the remembered secrets of a saved site after its edit dialog was accepted.
void CSiteListDialog::StoreSecrets(const std::wstring& siteName, CSiteEditResult& result)
{
    bool ok = true;
    if (ShouldClearSecrets(result.site.rememberPassword, result.forgetSecrets))
        store_.ClearSecrets(siteName);
    else
    {
        if (result.passwordTyped)
            ok = store_.SetPassword(siteName, result.password, HWindow) && ok;
        if (result.passphraseTyped)
            ok = store_.SetPassphrase(siteName, result.passphrase, HWindow) && ok;
    }
    if (!ok)
        SalamanderGeneral->SalMessageBox(HWindow, LangStr(IDS_SITE_SECRET_NOT_STORED).c_str(),
                                         LangStr(IDS_PLUGINNAME).c_str(), MB_OK | MB_ICONEXCLAMATION);
}

void CSiteListDialog::OnNew()
{
    if (RefuseIfReadOnly())
        return;
    CSiteEditDialog dialog(HWindow, store_.Sites(), nullptr, false, false);
    CSiteEditResult result;
    if (!dialog.Run(result))
        return;
    if (!store_.AddSite(result.site))
    {
        SalamanderGeneral->SalMessageBox(HWindow, LangStr(IDS_SITE_SAVE_FAILED).c_str(),
                                         LangStr(IDS_PLUGINNAME).c_str(), MB_OK | MB_ICONEXCLAMATION);
        return;
    }
    StoreSecrets(result.site.name, result);
    ListChanged(result.site.name);
}

void CSiteListDialog::OnEdit()
{
    if (RefuseIfReadOnly())
        return;
    const CSite* found = store_.Sites().Find(SelectedName());
    if (found == nullptr)
        return;
    const CSite original = *found; // the list may change while the dialog runs on the copy
    CSiteEditDialog dialog(HWindow, store_.Sites(), &original, store_.HasPassword(original.name),
                           store_.HasPassphrase(original.name));
    CSiteEditResult result;
    if (!dialog.Run(result))
        return;
    if (!store_.UpdateSite(original.name, result.site))
    {
        SalamanderGeneral->SalMessageBox(HWindow, LangStr(IDS_SITE_SAVE_FAILED).c_str(),
                                         LangStr(IDS_PLUGINNAME).c_str(), MB_OK | MB_ICONEXCLAMATION);
        return;
    }
    StoreSecrets(result.site.name, result);
    ListChanged(result.site.name);
}

void CSiteListDialog::OnDelete()
{
    if (RefuseIfReadOnly())
        return;
    const std::wstring name = SelectedName();
    if (name.empty())
        return;
    const std::wstring text = SPLFormatStringOwned(LangStr(IDS_SITELIST_DELETE_CONFIRM).c_str(), name.c_str());
    if (SalamanderGeneral->SalMessageBox(HWindow, text.c_str(), LangStr(IDS_PLUGINNAME).c_str(),
                                         MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) != IDYES)
        return;
    const int index = store_.Sites().IndexOf(name);
    store_.RemoveSite(name);
    // select the neighbour that moved into the deleted row
    const CSiteList& sites = store_.Sites();
    std::wstring next;
    if (sites.Count() > 0)
        next = sites.At(std::min<size_t>(static_cast<size_t>(index < 0 ? 0 : index), sites.Count() - 1)).name;
    ListChanged(next);
}

void CSiteListDialog::OnRename()
{
    if (RefuseIfReadOnly())
        return;
    const CSite* found = store_.Sites().Find(SelectedName());
    if (found == nullptr)
        return;
    CSite renamed = *found;
    const std::wstring oldName = renamed.name;
    CSiteNameDialog dialog(HWindow, store_.Sites(), oldName);
    std::wstring newName;
    if (!dialog.Run(newName) || newName == oldName)
        return;
    renamed.name = newName;
    if (!store_.UpdateSite(oldName, renamed)) // moves the remembered secrets with the site
    {
        SalamanderGeneral->SalMessageBox(HWindow, LangStr(IDS_SITE_SAVE_FAILED).c_str(),
                                         LangStr(IDS_PLUGINNAME).c_str(), MB_OK | MB_ICONEXCLAMATION);
        return;
    }
    ListChanged(newName);
}

void CSiteListDialog::OnDuplicate()
{
    if (RefuseIfReadOnly())
        return;
    const std::wstring name = SelectedName();
    if (name.empty())
        return;
    const std::wstring copy = store_.DuplicateSite(name);
    if (!copy.empty())
        ListChanged(copy);
}

INT_PTR CSiteListDialog::DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    try
    {
        switch (uMsg)
        {
        case WM_INITDIALOG:
            CCenteredDialog::DialogProc(uMsg, wParam, lParam);
            InitDialog();
            return FALSE; // the focus was set by hand

        case WM_COMMAND:
        {
            const int id = LOWORD(wParam);
            if (id == IDE_SITELIST_QUICK && HIWORD(wParam) == EN_CHANGE)
            {
                UpdateButtons();
                return TRUE;
            }
            switch (id)
            {
            case IDOK:
                OnConnect();
                return TRUE;
            case IDCANCEL:
                selected_ = SelectedName();
                quick_ = QuickText();
                break; // the base class closes the dialog
            case IDB_SITELIST_NEW:
                OnNew();
                return TRUE;
            case IDB_SITELIST_EDIT:
                OnEdit();
                return TRUE;
            case IDB_SITELIST_DELETE:
                OnDelete();
                return TRUE;
            case IDB_SITELIST_RENAME:
                OnRename();
                return TRUE;
            case IDB_SITELIST_DUPLICATE:
                OnDuplicate();
                return TRUE;
            }
            break;
        }

        case WM_NOTIFY:
        {
            const NMHDR* header = reinterpret_cast<const NMHDR*>(lParam);
            if (header == nullptr || header->idFrom != IDC_SITELIST_LIST)
                break;
            if (header->code == LVN_ITEMCHANGED)
                UpdateButtons();
            else if (header->code == NM_DBLCLK)
            {
                if (!SelectedName().empty())
                {
                    // a double click on a site connects to it, whatever is in the quick connect line
                    SetFocus(GetDlgItem(HWindow, IDC_SITELIST_LIST));
                    OnConnect();
                }
            }
            else if (header->code == LVN_KEYDOWN)
            {
                switch (reinterpret_cast<const NMLVKEYDOWN*>(lParam)->wVKey)
                {
                case VK_DELETE:
                    OnDelete();
                    break;
                case VK_INSERT:
                    OnNew();
                    break;
                case VK_F2:
                    OnRename();
                    break;
                }
            }
            break;
        }
        }
    }
    catch (...)
    {
        TRACE_E("CSiteListDialog: unexpected exception.");
        return TRUE;
    }
    return CCenteredDialog::DialogProc(uMsg, wParam, lParam);
}
