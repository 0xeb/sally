// Configuration dialog of the plugin (Plugins / SFTP / Configuration): the plugin-wide settings of CGlobalConfig on one
// page. It knows nothing about the registry or the file system: it gets the current values and returns the edited ones.
#pragma once

#include "centered_dialog.h"
#include "global_config.h"

class CConfigDialog : public CCenteredDialog
{
public:
    CConfigDialog(HWND parent, const CGlobalConfig& current);

    // True: OK (the new values are in 'config'). Cancel, ESC and every failure give false.
    bool Run(CGlobalConfig& config);

    void Transfer(CTransferInfo& ti) override;
    void Validate(CTransferInfo& ti) override;

protected:
    INT_PTR DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam) override;

private:
    bool CheckRange(CTransferInfo& ti, int id, int low, int high, bool zeroAllowed);
    void ShowRangeError(CTransferInfo& ti, int id, int messageId, int low, int high);
    void TransferLogLevel(CTransferInfo& ti);
    void UpdateEnabled();

    CGlobalConfig config_;
    int cacheEnabled_ = 1; // check box value for CTransferInfo::CheckBox
};

// Shows the dialog. True: OK.
bool ShowConfigDialog(HWND parent, CGlobalConfig& config);
