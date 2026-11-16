// Site edit dialog: a tab control with four child dialogs (Session, Authentication, Connection, Directories) that all
// read and write one CSiteForm. The tabs are plain child dialogs created by this dialog (not a property sheet): the
// dialog validates everything at once on OK and can switch to the tab of the offending field.
#pragma once

#include <string>

#include "centered_dialog.h"
#include "secret.h"
#include "site_edit.h"

// What the user entered. Secrets are only filled when the user typed something and "remember" is on.
struct CSiteEditResult
{
    CSite site;
    CSecret password;
    bool passwordTyped = false;
    CSecret passphrase;
    bool passphraseTyped = false;
    bool forgetSecrets = false; // "forget the stored password and passphrase" was ticked; the typed secrets are empty
};

// The text that belongs to a validation error (also used by the rename dialog).
std::wstring SiteErrorText(SiteError error);

class CSiteEditDialog;

// One tab: a child dialog bound to the owner's form. Commands (buttons, check boxes) go to the owner.
class CSiteTabPage : public CDialog
{
public:
    CSiteTabPage(int resID, UINT helpID, CSiteEditDialog& owner);
    bool CreatePage(HWND parent);
    HWND Control(int id) const { return HWindow == nullptr ? nullptr : GetDlgItem(HWindow, id); }

protected:
    INT_PTR DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam) override;
    CSiteEditDialog& owner_;
};

class CSiteSessionPage : public CSiteTabPage
{
public:
    explicit CSiteSessionPage(CSiteEditDialog& owner) : CSiteTabPage(IDD_SITETAB_SESSION, IDH_SITE_EDIT_SESSION, owner) {}
    void Transfer(CTransferInfo& ti) override;
};

class CSiteAuthPage : public CSiteTabPage
{
public:
    explicit CSiteAuthPage(CSiteEditDialog& owner) : CSiteTabPage(IDD_SITETAB_AUTH, IDH_SITE_EDIT_AUTH, owner) {}
    void Transfer(CTransferInfo& ti) override;
};

class CSiteConnectionPage : public CSiteTabPage
{
public:
    explicit CSiteConnectionPage(CSiteEditDialog& owner) : CSiteTabPage(IDD_SITETAB_CONNECTION, IDH_SITE_EDIT_CONNECTION, owner) {}
    void Transfer(CTransferInfo& ti) override;
};

class CSiteDirectoriesPage : public CSiteTabPage
{
public:
    explicit CSiteDirectoriesPage(CSiteEditDialog& owner) : CSiteTabPage(IDD_SITETAB_DIRECTORIES, IDH_SITE_EDIT_DIRECTORIES, owner) {}
    void Transfer(CTransferInfo& ti) override;
};

class CSiteEditDialog : public CCenteredDialog
{
public:
    // existing == NULL: a new site (defaults). Otherwise the site to edit; its name may stay (changing only the
    // letter case is fine). 'sites' is used for the duplicate-name check. hasPassword/hasPassphrase: a secret is
    // already stored (the hint "leave empty to keep it" and the "forget" check box are shown).
    CSiteEditDialog(HWND parent, const CSiteList& sites, const CSite* existing, bool hasPassword, bool hasPassphrase);

    // True: OK, result is filled. False: Cancel.
    bool Run(CSiteEditResult& result);

    CSiteForm& Form() { return form_; }
    // Called by the pages.
    void OnPageCommand(int id, int notifyCode);

protected:
    INT_PTR DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam) override;

private:
    void InitDialog();
    void LayoutPages();
    void ShowTab(int index);
    void UpdateEnabled();
    void BrowseKeyFile();
    bool TryAccept();
    void ShowError(const std::wstring& text, SiteField field);
    void WipeSecrets();
    CSiteTabPage* Page(int index);

    const CSiteList& sites_;
    const bool editing_;
    const std::wstring originalName_;
    const bool hasPassword_;
    const bool hasPassphrase_;
    CSiteForm form_;
    CSiteEditResult result_;

    CSiteSessionPage session_;
    CSiteAuthPage auth_;
    CSiteConnectionPage connection_;
    CSiteDirectoriesPage directories_;
};
