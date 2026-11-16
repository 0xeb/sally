// Solve-error dialog: the user decides what happens with one failed or parked queue item.
#pragma once

#include <cstdint>
#include <string>

#include "centered_dialog.h"
#include "op_queue.h"
#include "op_types.h"
#include "solve_offers.h"

class COperation;

// The user's decision.
struct CSolveChoice
{
    SolveAction action = SolveAction::Skip;
    std::wstring newName; // for Rename
    bool applyToAll = false;
};

class CSolveErrorDialog : public CCenteredDialog
{
public:
    CSolveErrorDialog(HWND parent, const CQueueItem& item);

    // True: the user chose an action (in 'choice'). Close, ESC and every failure give false.
    bool Run(CSolveChoice& choice);

    void Transfer(CTransferInfo& ti) override;

protected:
    INT_PTR DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam) override;

private:
    void InitControls();
    void EnableOffered();
    int DefaultButtonId() const;
    void Choose(SolveAction action);
    bool NewNameIsValid(const std::wstring& name) const;
    std::wstring SideInfo(int formatId, uint64_t size, int64_t mtime) const;

    CQueueItem item_;
    CSolveOffers offers_;
    std::wstring newName_;
    int applyToAll_ = 0;
    CSolveChoice choice_;
};

// Fits COperationWindowOptions::solveError: shows the dialog for item 'uid' of 'operation' and applies the answer
// (COperation::Solve). Does nothing when the item is gone or no longer needs a decision. Never throws.
void ShowSolveErrorDialog(HWND parent, COperation& operation, uint64_t uid);
