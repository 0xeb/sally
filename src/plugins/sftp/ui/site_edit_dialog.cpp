#include "precomp.h"

#include <commdlg.h>
#include <uxtheme.h>

#include "dark_controls.h"
#include "global_config.h"
#include "plugindarkmode.h"
#include "secret_edit.h"
#include "site_edit_dialog.h"

namespace
{
void TransferCheck(CTransferInfo& ti, int id, bool& value)
{
    int state = value ? 1 : 0;
    ti.CheckBox(id, state);
    value = state == 1;
}

bool IsChecked(HWND control)
{
    return control != nullptr && SendMessageW(control, BM_GETCHECK, 0, 0) == BST_CHECKED;
}
} // namespace

std::wstring SiteErrorText(SiteError error)
{
    int id = 0;
    switch (error)
    {
    case SiteError::None:
        return std::wstring();
    case SiteError::NameEmpty:
        id = IDS_SITE_ERR_NAME_EMPTY;
        break;
    case SiteError::NameInvalid:
        id = IDS_SITE_ERR_NAME_INVALID;
        break;
    case SiteError::HostEmpty:
        id = IDS_SITE_ERR_HOST_EMPTY;
        break;
    case SiteError::HostInvalid:
        id = IDS_SITE_ERR_HOST_INVALID;
        break;
    case SiteError::PortRange:
        id = IDS_SITE_ERR_PORT_RANGE;
        break;
    case SiteError::UserEmpty:
        id = IDS_SITE_ERR_USER_EMPTY;
        break;
    case SiteError::UserInvalid:
        id = IDS_SITE_ERR_USER_INVALID;
        break;
    case SiteError::TimeoutRange:
        id = IDS_SITE_ERR_TIMEOUT_RANGE;
        break;
    case SiteError::CodePageInvalid:
        id = IDS_SITE_ERR_CODEPAGE_INVALID;
        break;
    case SiteError::WorkersRange:
        id = IDS_SITE_ERR_WORKERS_RANGE;
        break;
    case SiteError::UploadPermissionsInvalid:
        id = IDS_SITE_ERR_UPLOADPERM_INVALID;
        break;
    }
    return id == 0 ? LangStr(IDS_SITE_SAVE_FAILED) : LangStr(id);
}

//
// ****************************************************************************
// Tab pages
//

CSiteTabPage::CSiteTabPage(int resID, UINT helpID, CSiteEditDialog& owner)
    : CDialog(HLanguage, resID, helpID, (HWND)nullptr, ooStatic), owner_(owner) // a member of the owner: never deleted by WinLib
{
}

bool CSiteTabPage::CreatePage(HWND parent)
{
    SetParent(parent);
    if (Create() == nullptr)
        return false;
    // light mode: the page uses the tab body texture (dark mode paints its own background)
    if (!PluginDarkMode_ShouldUseDark())
        EnableThemeDialogTexture(HWindow, ETDT_ENABLETAB);
    return true;
}

INT_PTR CSiteTabPage::DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    if (uMsg == WM_COMMAND)
    {
        const int id = LOWORD(wParam);
        if (id == IDOK || id == IDCANCEL)
            return TRUE; // belong to the owner's buttons; a page must never close itself
        if (lParam != 0)
            owner_.OnPageCommand(id, HIWORD(wParam));
    }
    return CDialog::DialogProc(uMsg, wParam, lParam);
}

void CSiteSessionPage::Transfer(CTransferInfo& ti)
{
    CSiteForm& f = owner_.Form();
    ti.EditLine(IDE_SITE_NAME, f.name);
    ti.EditLine(IDE_SITE_HOST, f.host);
    ti.EditLine(IDE_SITE_PORT, f.port);
    ti.EditLine(IDE_SITE_USER, f.user);
    TransferCheck(ti, IDX_SITE_REMEMBER, f.rememberPassword);
}

void CSiteAuthPage::Transfer(CTransferInfo& ti)
{
    CSiteForm& f = owner_.Form();
    TransferCheck(ti, IDX_SITE_AGENT, f.useAgent);
    ti.EditLine(IDE_SITE_KEYFILE, f.keyFile);
}

void CSiteConnectionPage::Transfer(CTransferInfo& ti)
{
    CSiteForm& f = owner_.Form();
    ti.EditLine(IDE_SITE_TIMEOUT, f.timeout);
    ti.EditLine(IDE_SITE_WORKERS, f.workers);
    TransferCheck(ti, IDX_SITE_COMPRESSION, f.compression);
    TransferCheck(ti, IDX_SITE_SAFEUPLOAD, f.safeUpload);
    TransferCheck(ti, IDX_SITE_PRESERVETIME, f.preserveTime);
    TransferCheck(ti, IDX_SITE_UPLOADPERM, f.setUploadPermissions);
    ti.EditLine(IDE_SITE_UPLOADMODE, f.uploadPermissions);
    int encoding = static_cast<int>(f.encoding);
    ti.RadioButton(IDR_SITE_ENC_UTF8, static_cast<int>(CSite::Encoding::Utf8), encoding);
    ti.RadioButton(IDR_SITE_ENC_AUTO, static_cast<int>(CSite::Encoding::Auto), encoding);
    ti.RadioButton(IDR_SITE_ENC_CODEPAGE, static_cast<int>(CSite::Encoding::CodePage), encoding);
    f.encoding = static_cast<CSite::Encoding>(encoding);
    ti.EditLine(IDE_SITE_CODEPAGE, f.codePage);
}

void CSiteDirectoriesPage::Transfer(CTransferInfo& ti)
{
    CSiteForm& f = owner_.Form();
    ti.EditLine(IDE_SITE_REMOTEPATH, f.initialRemotePath);
    ti.EditLine(IDE_SITE_LOCALPATH, f.initialLocalPath);
}

//
// ****************************************************************************
// CSiteEditDialog
//

CSiteEditDialog::CSiteEditDialog(HWND parent, const CSiteList& sites, const CSite* existing, bool hasPassword,
                                 bool hasPassphrase)
    : CCenteredDialog(HLanguage, IDD_SITEEDIT, IDH_SITE_EDIT, parent),
      sites_(sites),
      editing_(existing != nullptr),
      originalName_(existing != nullptr ? existing->name : std::wstring()),
      hasPassword_(hasPassword),
      hasPassphrase_(hasPassphrase),
      form_(FormFromSite(existing != nullptr ? *existing : MakeNewSite(GlobalConfigStore().Get()))),
      session_(*this),
      auth_(*this),
      connection_(*this),
      directories_(*this)
{
}

bool CSiteEditDialog::Run(CSiteEditResult& result)
{
    try
    {
        INITCOMMONCONTROLSEX icc = {sizeof(icc), ICC_TAB_CLASSES};
        InitCommonControlsEx(&icc);
        if (Execute() != IDOK)
        {
            result_ = CSiteEditResult(); // wipes
            return false;
        }
        result = std::move(result_);
        return true;
    }
    catch (...)
    {
        result_ = CSiteEditResult();
        return false;
    }
}

CSiteTabPage* CSiteEditDialog::Page(int index)
{
    switch (index)
    {
    case SiteTabSession:
        return &session_;
    case SiteTabAuthentication:
        return &auth_;
    case SiteTabConnection:
        return &connection_;
    case SiteTabDirectories:
        return &directories_;
    }
    return nullptr;
}

void CSiteEditDialog::InitDialog()
{
    SetWindowTextW(HWindow, LangStr(editing_ ? IDS_SITEEDIT_TITLE_EDIT : IDS_SITEEDIT_TITLE_NEW).c_str());

    HWND tabs = GetDlgItem(HWindow, IDC_SITEEDIT_TABS);
    ApplyDarkTabControl(tabs);
    static const int tabTitles[SiteTabCount] = {IDS_SITEEDIT_TAB_SESSION, IDS_SITEEDIT_TAB_AUTH,
                                                IDS_SITEEDIT_TAB_CONNECTION, IDS_SITEEDIT_TAB_DIRECTORIES};
    for (int i = 0; i < SiteTabCount; i++)
    {
        std::wstring title = LangStr(tabTitles[i]);
        TCITEMW item = {};
        item.mask = TCIF_TEXT;
        item.pszText = const_cast<wchar_t*>(title.c_str());
        SendMessageW(tabs, TCM_INSERTITEMW, i, reinterpret_cast<LPARAM>(&item));
    }

    for (int i = 0; i < SiteTabCount; i++)
    {
        if (!Page(i)->CreatePage(HWindow))
        {
            TRACE_E("CSiteEditDialog: cannot create tab page " << i);
            EndDialog(HWindow, IDCANCEL);
            return;
        }
        ApplyDarkButtons(Page(i)->HWindow);
    }
    LayoutPages();
    SendMessageW(tabs, TCM_SETCURSEL, SiteTabSession, 0);
    ShowTab(SiteTabSession);
    UpdateEnabled();

    HWND name = session_.Control(IDE_SITE_NAME);
    SetFocus(name);
    SendMessageW(name, EM_SETSEL, 0, -1);
}

// The pages fill the display area of the tab control. They are put on top of the tab control in the z order (a page
// created later would lie below it and be painted over).
void CSiteEditDialog::LayoutPages()
{
    HWND tabs = GetDlgItem(HWindow, IDC_SITEEDIT_TABS);
    RECT rc;
    GetClientRect(tabs, &rc);
    SendMessageW(tabs, TCM_ADJUSTRECT, FALSE, reinterpret_cast<LPARAM>(&rc));
    MapWindowPoints(tabs, HWindow, reinterpret_cast<POINT*>(&rc), 2);
    for (int i = 0; i < SiteTabCount; i++)
        SetWindowPos(Page(i)->HWindow, HWND_TOP, rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top,
                     SWP_NOACTIVATE);
}

void CSiteEditDialog::ShowTab(int index)
{
    for (int i = 0; i < SiteTabCount; i++)
        ShowWindow(Page(i)->HWindow, i == index ? SW_SHOW : SW_HIDE);
}

// The password and passphrase edits are always enabled (typing one ticks "Remember"); the code page edit only
// follows its encoding.
void CSiteEditDialog::UpdateEnabled()
{
    // a ticked "forget" drops the stored secrets on OK, so the "leave empty to keep it" hints would be wrong
    const bool remember = IsChecked(session_.Control(IDX_SITE_REMEMBER)) && !IsChecked(session_.Control(IDX_SITE_FORGET));
    SetWindowTextW(session_.Control(IDC_SITE_PASSWORD_HINT),
                   remember && hasPassword_ ? LangStr(IDS_SITE_KEEP_PASSWORD).c_str() : L"");
    SetWindowTextW(auth_.Control(IDC_SITE_PASSPHRASE_HINT),
                   remember && hasPassphrase_ ? LangStr(IDS_SITE_KEEP_PASSPHRASE).c_str() : L"");
    // "Forget" only makes sense while something is stored
    ShowWindow(session_.Control(IDX_SITE_FORGET), hasPassword_ || hasPassphrase_ ? SW_SHOW : SW_HIDE);
    EnableWindow(connection_.Control(IDE_SITE_CODEPAGE), IsChecked(connection_.Control(IDR_SITE_ENC_CODEPAGE)));
    EnableWindow(connection_.Control(IDE_SITE_UPLOADMODE), IsChecked(connection_.Control(IDX_SITE_UPLOADPERM)));
}

void CSiteEditDialog::OnPageCommand(int id, int notifyCode)
{
    if (notifyCode == EN_CHANGE && (id == IDE_SITE_PASSWORD || id == IDE_SITE_PASSPHRASE))
    {
        // only real typing gets here: the secret edits are never filled programmatically (the wipe leaves them empty)
        HWND edit = id == IDE_SITE_PASSWORD ? session_.Control(id) : auth_.Control(id);
        HWND remember = session_.Control(IDX_SITE_REMEMBER);
        if (GetWindowTextLengthW(edit) > 0 && !IsChecked(remember))
        {
            SendMessageW(remember, BM_SETCHECK, BST_CHECKED, 0);
            UpdateEnabled();
        }
        return;
    }
    if (notifyCode != BN_CLICKED)
        return;
    switch (id)
    {
    case IDX_SITE_REMEMBER:
    case IDX_SITE_FORGET:
    case IDR_SITE_ENC_UTF8:
    case IDR_SITE_ENC_AUTO:
    case IDR_SITE_ENC_CODEPAGE:
    case IDX_SITE_UPLOADPERM:
        UpdateEnabled();
        if (id == IDR_SITE_ENC_CODEPAGE)
            SetFocus(connection_.Control(IDE_SITE_CODEPAGE));
        else if (id == IDX_SITE_UPLOADPERM && IsChecked(connection_.Control(id)))
            SetFocus(connection_.Control(IDE_SITE_UPLOADMODE));
        break;
    case IDB_SITE_KEYBROWSE:
        BrowseKeyFile();
        break;
    }
}

void CSiteEditDialog::BrowseKeyFile()
{
    HWND edit = auth_.Control(IDE_SITE_KEYFILE);
    std::vector<wchar_t> file(32768, L'\0');
    GetWindowTextW(edit, file.data(), static_cast<int>(file.size()));

    // "text|pattern|..." from the resources -> "text\0pattern\0...\0\0"
    std::wstring filter = LangStr(IDS_SITE_KEY_FILTER);
    for (wchar_t& c : filter)
        if (c == L'|')
            c = L'\0';
    filter.push_back(L'\0'); // the second terminator (the string's own one is the first)
    const std::wstring title = LangStr(IDS_SITE_KEY_BROWSE_TITLE);

    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = HWindow;
    ofn.lpstrFilter = filter.c_str();
    ofn.lpstrFile = file.data();
    ofn.nMaxFile = static_cast<DWORD>(file.size());
    ofn.lpstrTitle = title.c_str();
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY | OFN_EXPLORER;
    if (GetOpenFileNameW(&ofn))
    {
        SetWindowTextW(edit, file.data());
        SendMessageW(edit, EM_SETSEL, 0, -1);
    }
}

void CSiteEditDialog::ShowError(const std::wstring& text, SiteField field)
{
    SalamanderGeneral->SalMessageBox(HWindow, text.c_str(), LangStr(IDS_PLUGINNAME).c_str(),
                                     MB_OK | MB_ICONEXCLAMATION);
    const SiteTab tab = TabOfField(field);
    SendMessageW(GetDlgItem(HWindow, IDC_SITEEDIT_TABS), TCM_SETCURSEL, tab, 0);
    ShowTab(tab);

    HWND control = nullptr;
    switch (field)
    {
    case SiteField::Name:
        control = session_.Control(IDE_SITE_NAME);
        break;
    case SiteField::Host:
        control = session_.Control(IDE_SITE_HOST);
        break;
    case SiteField::Port:
        control = session_.Control(IDE_SITE_PORT);
        break;
    case SiteField::User:
        control = session_.Control(IDE_SITE_USER);
        break;
    case SiteField::Timeout:
        control = connection_.Control(IDE_SITE_TIMEOUT);
        break;
    case SiteField::CodePage:
        control = connection_.Control(IDE_SITE_CODEPAGE);
        break;
    case SiteField::Workers:
        control = connection_.Control(IDE_SITE_WORKERS);
        break;
    case SiteField::UploadPermissions:
        control = connection_.Control(IDE_SITE_UPLOADMODE);
        break;
    }
    if (control != nullptr)
    {
        SetFocus(control);
        SendMessageW(control, EM_SETSEL, 0, -1);
    }
}

// Reads all controls, validates, and fills result_. False: an error was shown and the dialog stays open.
bool CSiteEditDialog::TryAccept()
{
    for (int i = 0; i < SiteTabCount; i++)
        if (!Page(i)->TransferData(ttDataFromWindow))
            return false; // the text could not be read; the page focused the control

    CSite site;
    const SiteError error = SiteFromForm(form_, site);
    if (error != SiteError::None)
    {
        ShowError(SiteErrorText(error), FieldOfError(error));
        return false;
    }
    const CSite* other = sites_.Find(site.name);
    if (other != nullptr && !(editing_ && SiteNamesEqual(originalName_, other->name)))
    {
        ShowError(LangStr(IDS_SITE_ERR_NAME_TAKEN), SiteField::Name);
        return false;
    }

    result_ = CSiteEditResult();
    result_.site = std::move(site);
    result_.forgetSecrets = (hasPassword_ || hasPassphrase_) && IsChecked(session_.Control(IDX_SITE_FORGET));
    if (result_.site.rememberPassword && !result_.forgetSecrets)
    {
        result_.password = ReadSecretFromEdit(session_.Control(IDE_SITE_PASSWORD));
        result_.passwordTyped = !result_.password.empty();
        result_.passphrase = ReadSecretFromEdit(auth_.Control(IDE_SITE_PASSPHRASE));
        result_.passphraseTyped = !result_.passphrase.empty();
    }
    return true;
}

void CSiteEditDialog::WipeSecrets()
{
    WipeEdit(session_.Control(IDE_SITE_PASSWORD));
    WipeEdit(auth_.Control(IDE_SITE_PASSPHRASE));
}

INT_PTR CSiteEditDialog::DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
    case WM_INITDIALOG:
        CCenteredDialog::DialogProc(uMsg, wParam, lParam);
        InitDialog();
        return FALSE; // the focus was set by hand

    case WM_NOTIFY:
    {
        const NMHDR* header = reinterpret_cast<const NMHDR*>(lParam);
        if (header != nullptr && header->idFrom == IDC_SITEEDIT_TABS && header->code == TCN_SELCHANGE)
            ShowTab(static_cast<int>(SendMessageW(header->hwndFrom, TCM_GETCURSEL, 0, 0)));
        break;
    }

    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK)
        {
            if (TryAccept())
            {
                WipeSecrets();
                EndDialog(HWindow, IDOK);
            }
            return TRUE;
        }
        if (LOWORD(wParam) == IDCANCEL)
            WipeSecrets();
        break;
    }
    return CCenteredDialog::DialogProc(uMsg, wParam, lParam);
}
