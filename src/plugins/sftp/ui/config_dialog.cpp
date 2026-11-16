#include "precomp.h"

#include "config_dialog.h"
#include "dark_controls.h"

namespace
{
// The order of the entries in the log level combo box (the combo index is the position in this table).
struct CLevelEntry
{
    SessionLogLevel level;
    int textId;
};
const CLevelEntry LevelEntries[] = {
    {SessionLogLevel::Off, IDS_CONFIG_LOGLEVEL_OFF},
    {SessionLogLevel::Errors, IDS_CONFIG_LOGLEVEL_ERRORS},
    {SessionLogLevel::Normal, IDS_CONFIG_LOGLEVEL_NORMAL},
    {SessionLogLevel::Verbose, IDS_CONFIG_LOGLEVEL_VERBOSE},
};
const int LevelCount = static_cast<int>(sizeof(LevelEntries) / sizeof(LevelEntries[0]));

int LevelIndex(SessionLogLevel level)
{
    for (int i = 0; i < LevelCount; i++)
        if (LevelEntries[i].level == level)
            return i;
    return 0;
}
} // namespace

CConfigDialog::CConfigDialog(HWND parent, const CGlobalConfig& current)
    : CCenteredDialog(HLanguage, IDD_CONFIG, IDH_CONFIGURATION, parent), config_(current)
{
}

bool CConfigDialog::Run(CGlobalConfig& config)
{
    try
    {
        cacheEnabled_ = config_.listingCacheEnabled ? 1 : 0;
        if (Execute() != IDOK)
            return false;
        config_.Clamp();
        config = config_;
        return true;
    }
    catch (...)
    {
        return false;
    }
}

void CConfigDialog::TransferLogLevel(CTransferInfo& ti)
{
    HWND combo = nullptr;
    if (!ti.GetControl(combo, IDC_CFG_LOGLEVEL))
        return;
    if (ti.Type == ttDataToWindow)
    {
        SendMessageW(combo, CB_RESETCONTENT, 0, 0);
        for (int i = 0; i < LevelCount; i++)
            SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(LangStr(LevelEntries[i].textId).c_str()));
        SendMessageW(combo, CB_SETCURSEL, LevelIndex(config_.logLevel), 0);
    }
    else
    {
        const LRESULT index = SendMessageW(combo, CB_GETCURSEL, 0, 0);
        if (index >= 0 && index < LevelCount)
            config_.logLevel = LevelEntries[index].level;
    }
}

void CConfigDialog::Transfer(CTransferInfo& ti)
{
    ti.EditLine(IDE_CFG_WORKERS, config_.defaultWorkers);
    ti.EditLine(IDE_CFG_TIMEOUT, config_.defaultTimeoutSeconds);
    ti.EditLine(IDE_CFG_KEEPALIVE, config_.keepaliveSeconds);
    ti.EditLine(IDE_CFG_MAXWORKERS, config_.maxWorkersTotal);
    ti.EditLine(IDE_CFG_DETACHEDIDLE, config_.detachedIdleMinutes);
    ti.CheckBox(IDX_CFG_CACHE, cacheEnabled_);
    ti.EditLine(IDE_CFG_CACHEAGE, config_.listingCacheMaxAgeSeconds);
    TransferLogLevel(ti);
    ti.EditLine(IDE_CFG_LOGKIB, config_.logMaxKiB);
    ti.EditLine(IDE_CFG_LOGKEEP, config_.closedLogsKept);
    if (ti.Type == ttDataFromWindow)
        config_.listingCacheEnabled = cacheEnabled_ != 0;
}

void CConfigDialog::ShowRangeError(CTransferInfo& ti, int id, int messageId, int low, int high)
{
    SalamanderGeneral->SalMessageBox(HWindow, SPLFormatStringOwned(LangStr(messageId).c_str(), low, high).c_str(),
                                     LangStr(IDS_PLUGINNAME).c_str(), MB_OK | MB_ICONEXCLAMATION);
    ti.ErrorOn(id);
}

// True when the edit holds a number in low..high (or 0 when zeroAllowed); otherwise the user is told and the edit is
// focused (the caller stops at the first problem).
bool CConfigDialog::CheckRange(CTransferInfo& ti, int id, int low, int high, bool zeroAllowed)
{
    int value = 0;
    ti.EditLine(id, value); // reports "not a number" itself
    if (!ti.IsGood())
        return false;
    if ((value >= low && value <= high) || (zeroAllowed && value == 0))
        return true;
    ShowRangeError(ti, id, zeroAllowed && low == MinKeepaliveSeconds ? IDS_CONFIG_ERR_KEEPALIVE : IDS_CONFIG_ERR_RANGE,
                   low, high);
    return false;
}

void CConfigDialog::Validate(CTransferInfo& ti)
{
    CheckRange(ti, IDE_CFG_WORKERS, MinDefaultWorkers, MaxDefaultWorkers, false) &&
        CheckRange(ti, IDE_CFG_TIMEOUT, MinDefaultTimeout, MaxDefaultTimeout, false) &&
        CheckRange(ti, IDE_CFG_KEEPALIVE, MinKeepaliveSeconds, MaxKeepaliveSeconds, true) &&
        CheckRange(ti, IDE_CFG_MAXWORKERS, MinWorkersTotal, MaxWorkersTotal, false) &&
        CheckRange(ti, IDE_CFG_DETACHEDIDLE, 0, MaxDetachedIdleMinutes, false) &&
        CheckRange(ti, IDE_CFG_CACHEAGE, MinCacheAgeSeconds, MaxCacheAgeSeconds, false) &&
        CheckRange(ti, IDE_CFG_LOGKIB, MinLogKiB, MaxLogKiB, false) &&
        CheckRange(ti, IDE_CFG_LOGKEEP, 0, MaxClosedLogsKept, false);
}

// The cache age only matters while the cache is on.
void CConfigDialog::UpdateEnabled()
{
    const bool on = IsDlgButtonChecked(HWindow, IDX_CFG_CACHE) == BST_CHECKED;
    EnableWindow(GetDlgItem(HWindow, IDE_CFG_CACHEAGE), on);
}

INT_PTR CConfigDialog::DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    try
    {
        if (uMsg == WM_COMMAND && LOWORD(wParam) == IDX_CFG_CACHE && HIWORD(wParam) == BN_CLICKED)
        {
            UpdateEnabled();
            return TRUE;
        }
        // the base class transfers the values and centers the dialog; the dark painting and the enabled state follow
        const INT_PTR result = CCenteredDialog::DialogProc(uMsg, wParam, lParam);
        if (uMsg == WM_INITDIALOG)
        {
            ApplyDarkButtons(HWindow);
            UpdateEnabled();
        }
        return result;
    }
    catch (...)
    {
        TRACE_E("CConfigDialog: unexpected exception in a window message.");
    }
    return FALSE;
}

bool ShowConfigDialog(HWND parent, CGlobalConfig& config)
{
    CConfigDialog dialog(parent, config);
    return dialog.Run(config);
}
