// Site list dialog (Change Drive item and plugin menu): the saved sites with Connect / New / Edit / Delete / Rename /
// Duplicate, and a quick connect line. Changes are applied to the site store at once and saved to the registry at once.
#pragma once

#include <string>

#include "centered_dialog.h"
#include "site_edit_dialog.h"
#include "site_store.h"

class CSiteListDialog : public CCenteredDialog
{
public:
    CSiteListDialog(HWND parent, ISiteStore& store);

    // Shows the dialog. True: Connect; 'path' is the user part of the file system path to open ("@Name/" or
    // "user@host:port/path"). False: Close. The dialog remembers the selection and the quick connect text, so it
    // can be run again (after a failed connect) with the user's input intact.
    bool Run(std::wstring& path);

protected:
    INT_PTR DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam) override;

private:
    void InitDialog();
    void FillList(const std::wstring& select);
    void UpdateButtons();
    std::wstring SelectedName() const;
    std::wstring QuickText() const;

    void OnConnect();
    void OnNew();
    void OnEdit();
    void OnDelete();
    void OnRename();
    void OnDuplicate();
    void StoreSecrets(const std::wstring& siteName, CSiteEditResult& result);
    void ListChanged(const std::wstring& select);
    // True (after telling the user) when the stored sites come from a newer plugin version and cannot be changed.
    bool RefuseIfReadOnly();

    ISiteStore& store_;
    std::wstring selected_; // site selected when the dialog was last closed
    std::wstring quick_;    // quick connect text when the dialog was last closed
    std::wstring path_;     // result of Connect
};
