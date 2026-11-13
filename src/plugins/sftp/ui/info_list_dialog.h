// A read-only dialog that shows "label: value" rows in a list view, with Copy (all rows as text to the clipboard) and
// Close. The info, security and properties dialogs are this class with different rows and window templates; every
// template has the controls IDC_INFO_LIST, IDB_INFO_COPY and IDCANCEL. It knows nothing about the file system.
#pragma once

#include <string>
#include <vector>

#include "centered_dialog.h"

struct CInfoRow
{
    std::wstring label;
    std::wstring value;
};

class CInfoListDialog : public CCenteredDialog
{
public:
    // dialogId: the resource template (IDD_FSINFO, ...), helpID: its IDH_* topic; title: the window caption.
    CInfoListDialog(HWND parent, int dialogId, UINT helpID, std::wstring title, std::vector<CInfoRow> rows);

    // Shows the dialog; returns when it is closed. Never throws.
    void Run();

protected:
    INT_PTR DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam) override;

private:
    void InitControls();
    void CopyToClipboard();

    std::wstring title_;
    std::vector<CInfoRow> rows_;
};

// Rows as text, one "label: value" per line (what Copy puts on the clipboard).
std::wstring InfoRowsToText(const std::vector<CInfoRow>& rows);
