// Copy/Move dialog for F5/F6 from the SFTP panel: the target path (with Sally's shared history), what to do when a file
// already exists, and whether the downloaded files keep their modification time.
#pragma once

#include <string>

#include "centered_dialog.h"
#include "op_types.h"

// What the user chose; also the starting values.
struct CCopyMoveChoice
{
    std::wstring target;
    ConflictPolicy policy = ConflictPolicy::Ask;
    bool preserveTime = true;
};

class CCopyMoveDialog : public CCenteredDialog
{
public:
    // 'copy' picks the title; 'subject' is the text above the target ("Copy 3 files from the SFTP server to:").
    CCopyMoveDialog(HWND parent, bool copy, const std::wstring& subject);

    // True: OK (the values are in 'choice'). Cancel, ESC and every failure give false.
    bool Run(CCopyMoveChoice& choice);

    void Transfer(CTransferInfo& ti) override;

protected:
    INT_PTR DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam) override;

private:
    void FillPolicyCombo(HWND combo) const;

    bool copy_;
    std::wstring subject_;
    CCopyMoveChoice choice_;
    int preserveTime_ = 1; // the checkbox value for CTransferInfo::CheckBox
    wchar_t** history_ = nullptr; // Sally's shared history of target paths
    int historyCount_ = 0;
};
