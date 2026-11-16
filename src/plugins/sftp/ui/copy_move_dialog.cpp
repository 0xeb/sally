#include "precomp.h"

#include "copy_move_dialog.h"
#include "dark_controls.h"

#include <vector>

namespace
{
const UINT WM_APP_SHOW_TARGET = WM_APP + 1; // posted to ourselves at the end of WM_INITDIALOG

// The order of the entries in the "If the file exists" combo box (the combo index is the position in this table).
struct CPolicyEntry
{
    ConflictPolicy policy;
    int textId;
};
const CPolicyEntry PolicyEntries[] = {
    {ConflictPolicy::Ask, IDS_POLICY_ASK},
    {ConflictPolicy::Overwrite, IDS_POLICY_OVERWRITE},
    {ConflictPolicy::Resume, IDS_POLICY_RESUME},
    {ConflictPolicy::Skip, IDS_POLICY_SKIP},
    {ConflictPolicy::OverwriteIfOlder, IDS_POLICY_OVERWRITE_OLDER},
    {ConflictPolicy::Rename, IDS_POLICY_RENAME},
};
const int PolicyCount = static_cast<int>(sizeof(PolicyEntries) / sizeof(PolicyEntries[0]));

int PolicyIndex(ConflictPolicy policy)
{
    for (int i = 0; i < PolicyCount; i++)
        if (PolicyEntries[i].policy == policy)
            return i;
    return 0;
}
} // namespace

CCopyMoveDialog::CCopyMoveDialog(HWND parent, bool copy, const std::wstring& subject)
    : CCenteredDialog(HLanguage, IDD_COPYMOVE, IDH_COPY_MOVE, parent), copy_(copy), subject_(subject)
{
    SalamanderGeneral->GetStdHistoryValues(SALHIST_COPYMOVETGT, &history_, &historyCount_);
}

bool CCopyMoveDialog::Run(CCopyMoveChoice& choice)
{
    try
    {
        choice_ = choice;
        preserveTime_ = choice.preserveTime ? 1 : 0;
        if (Execute() != IDOK)
            return false;
        choice = choice_;
        return true;
    }
    catch (...)
    {
        return false;
    }
}

void CCopyMoveDialog::FillPolicyCombo(HWND combo) const
{
    SendMessageW(combo, CB_RESETCONTENT, 0, 0);
    for (int i = 0; i < PolicyCount; i++)
        SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(LangStr(PolicyEntries[i].textId).c_str()));
}

void CCopyMoveDialog::Transfer(CTransferInfo& ti)
{
    HWND target = nullptr;
    if (ti.GetControl(target, IDC_COPYMOVE_TARGET))
    {
        if (ti.Type == ttDataToWindow)
        {
            if (history_ != nullptr)
                SalamanderGeneral->LoadComboFromStdHistoryValues(target, history_, historyCount_);
            SetWindowTextW(target, choice_.target.c_str());
        }
        else
        {
            const int length = GetWindowTextLengthW(target);
            std::vector<wchar_t> text(static_cast<size_t>(length) + 1, L'\0');
            GetWindowTextW(target, text.data(), length + 1);
            choice_.target.assign(text.data());
            if (history_ != nullptr && !choice_.target.empty())
                SalamanderGeneral->AddValueToStdHistoryValues(history_, historyCount_, choice_.target.c_str(), FALSE);
        }
    }

    HWND policy = nullptr;
    if (ti.GetControl(policy, IDC_COPYMOVE_POLICY))
    {
        if (ti.Type == ttDataToWindow)
        {
            FillPolicyCombo(policy);
            SendMessageW(policy, CB_SETCURSEL, PolicyIndex(choice_.policy), 0);
        }
        else
        {
            const LRESULT index = SendMessageW(policy, CB_GETCURSEL, 0, 0);
            if (index >= 0 && index < PolicyCount)
                choice_.policy = PolicyEntries[index].policy;
        }
    }

    ti.CheckBox(IDX_COPYMOVE_PRESERVETIME, preserveTime_);
    if (ti.Type == ttDataFromWindow)
        choice_.preserveTime = preserveTime_ != 0;
}

INT_PTR CCopyMoveDialog::DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    try
    {
        if (uMsg == WM_INITDIALOG)
        {
            // title and subject first: the base class transfers the values and centers the dialog
            SalamanderGeneral->InstallWordBreakProc(GetDlgItem(HWindow, IDC_COPYMOVE_TARGET));
            SetWindowTextW(HWindow, LangStr(copy_ ? IDS_COPYMOVE_TITLE_COPY : IDS_COPYMOVE_TITLE_MOVE).c_str());
            SetDlgItemTextW(HWindow, IDC_COPYMOVE_SUBJECT, subject_.c_str());
            ApplyDarkButtons(HWindow);
        }
        if (uMsg == WM_APP_SHOW_TARGET)
        {
            // runs after the dialog has finished opening: whatever filled the combo meanwhile (an entry of the history),
            // the proposed target is what the user must see
            HWND target = GetDlgItem(HWindow, IDC_COPYMOVE_TARGET);
            if (!choice_.target.empty())
                SetWindowTextW(target, choice_.target.c_str());
            SendMessageW(target, CB_SETEDITSEL, 0, MAKELPARAM(0, -1));
            return TRUE;
        }
        const INT_PTR result = CCenteredDialog::DialogProc(uMsg, wParam, lParam);
        if (uMsg == WM_INITDIALOG)
            PostMessageW(HWindow, WM_APP_SHOW_TARGET, 0, 0);
        return result;
    }
    catch (...)
    {
        TRACE_E("CCopyMoveDialog: unexpected exception in a window message.");
    }
    return FALSE;
}
