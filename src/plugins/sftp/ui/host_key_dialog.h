// Host key dialog: unknown key (neutral text) or changed key (red warning, Cancel is the default button).
#pragma once

#include <string>

#include "centered_dialog.h"
#include "ssh_types.h"

class CHostKeyDialog : public CCenteredDialog
{
public:
    // host: "host:port" as shown; fingerprint: "SHA256:..." as the core formats it.
    CHostKeyDialog(HWND parent, const std::wstring& host, const std::wstring& keyType,
                   const std::wstring& fingerprint, bool changed);
    ~CHostKeyDialog() override;

    // Shows the dialog. Cancel, ESC, close and every failure give Reject.
    HostKeyDecision Run();

    void Transfer(CTransferInfo& ti) override;

protected:
    INT_PTR DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam) override;

private:
    void InitControls();

    std::wstring host_;
    std::wstring keyType_;
    std::wstring fingerprint_;
    bool changed_;
    HFONT boldFont_ = nullptr;
};
