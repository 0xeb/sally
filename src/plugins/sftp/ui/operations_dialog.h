// The list of running and finished operations (menu "Operations..."): state, source -> target, Show, Cancel.
// Modal, for Sally's main thread. It only reads the COperationsList and cancels operations; it does not know how the
// operation windows are kept (that is the owner's business, passed in as callbacks), so ui/ stays free of fs/.
#pragma once

#include <cstdint>
#include <functional>
#include <string>

#include "centered_dialog.h"
#include "operations_list.h"

struct COperationsDialogCallbacks
{
    // "source -> target" of an operation (what its window shows). Without it the roots of the settings are shown.
    std::function<std::wstring(uint64_t uid)> describe;
    // Show: reopens or raises the window of the operation. Returns false when it has no window any more.
    std::function<bool(uint64_t uid)> showWindow;
};

class COperationsDialog : public CCenteredDialog
{
public:
    COperationsDialog(HWND parent, COperationsList& operations, COperationsDialogCallbacks callbacks);

    void Run();

protected:
    INT_PTR DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam) override;

private:
    void InitList();
    void Refresh();
    void UpdateButtons();
    uint64_t SelectedUid() const;
    std::wstring TitleOf(const COperationInfo& info) const;
    std::wstring StateOf(const COperationInfo& info) const;
    std::wstring DescriptionOf(const COperationInfo& info) const;
    void ShowSelected();
    void CancelSelected();

    COperationsList& operations_;
    COperationsDialogCallbacks callbacks_;
};

// Shows the dialog (returns when it is closed). Never throws.
void ShowOperationsDialog(HWND parent, COperationsList& operations, COperationsDialogCallbacks callbacks);
