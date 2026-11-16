#include "precomp.h"

#include "chmod_dialog.h"
#include "dark_controls.h"

namespace
{
const int kGridCount = 9; // the checkboxes IDX_CHMOD_FIRST_BIT ... +8

// The permission bit of grid box 'index': owner r,w,x = 0400,0200,0100, group 040,020,010, others 04,02,01.
uint32_t BitOfBox(int index)
{
    return 0400u >> index;
}

bool IsGridControl(int id)
{
    const int index = id - IDX_CHMOD_FIRST_BIT;
    return index >= 0 && index < kGridCount;
}
} // namespace

CChmodDialog::CChmodDialog(HWND parent, const CChmodDialogInput& input)
    : CCenteredDialog(HLanguage, IDD_CHMOD, IDH_CHMOD, parent), input_(input), state_(input.state)
{
}

bool CChmodDialog::Run(CChmodChoice& choice)
{
    try
    {
        recursive_ = 0;
        files_ = 1;
        dirs_ = 1;
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

void CChmodDialog::Transfer(CTransferInfo& ti)
{
    ti.CheckBox(IDX_CHMOD_RECURSIVE, recursive_);
    ti.CheckBox(IDX_CHMOD_FILES, files_);
    ti.CheckBox(IDX_CHMOD_DIRS, dirs_);
    if (ti.Type == ttDataFromWindow)
    {
        // recursion makes sense for directories only; without it the selected items themselves all get the change
        choice_.recursive = input_.anyDirectory && recursive_ != 0;
        choice_.applyToFiles = !choice_.recursive || files_ != 0;
        choice_.applyToDirs = !choice_.recursive || dirs_ != 0;
        choice_.change = ChangeOfState(state_);
    }
}

// ---- state <-> controls --------------------------------------------------------------------------------------------

// The dialog keeps the truth in state_; the checkboxes are read back after a click (the user may have cycled a box
// through "leave as it is").
void CChmodDialog::ReadGrid()
{
    CModeState state = state_;
    for (int i = 0; i < kGridCount; i++)
    {
        const uint32_t bit = BitOfBox(i);
        const LRESULT check = SendDlgItemMessageW(HWindow, IDX_CHMOD_FIRST_BIT + i, BM_GETCHECK, 0, 0);
        state.checked &= ~bit;
        state.mixed &= ~bit;
        if (check == BST_CHECKED)
            state.checked |= bit;
        else if (check == BST_INDETERMINATE)
            state.mixed |= bit;
    }
    state_ = state;
}

// A box whose bit the items disagree on has three states (checked / unchecked / "leave as it is"); the others only two,
// so the user is not offered a third state that would mean nothing.
void CChmodDialog::WriteGrid()
{
    for (int i = 0; i < kGridCount; i++)
    {
        const uint32_t bit = BitOfBox(i);
        const bool mixed = (state_.mixed & bit) != 0;
        const int id = IDX_CHMOD_FIRST_BIT + i;
        SendDlgItemMessageW(HWindow, id, BM_SETSTYLE, mixed ? BS_AUTO3STATE : BS_AUTOCHECKBOX, TRUE);
        const int check = mixed ? BST_INDETERMINATE : ((state_.checked & bit) != 0 ? BST_CHECKED : BST_UNCHECKED);
        SendDlgItemMessageW(HWindow, id, BM_SETCHECK, check, 0);
    }
}

void CChmodDialog::ShowOctal()
{
    updating_ = true; // SetWindowText sends EN_CHANGE; that is not the user typing
    SetDlgItemTextW(HWindow, IDE_CHMOD_OCTAL, FormatOctal(ShownMode(state_)).c_str());
    updating_ = false;
}

void CChmodDialog::ShowSymbolic()
{
    SetDlgItemTextW(HWindow, IDC_CHMOD_SYMBOLIC, FormatSymbolic(ShownMode(state_), state_.mixed).c_str());
}

void CChmodDialog::InitControls()
{
    const std::wstring title =
        input_.itemCount == 1 ? SPLFormatStringOwned(LangStr(IDS_CHMOD_TITLE_ONE).c_str(), input_.name.c_str())
                              : SPLFormatStringOwned(LangStr(IDS_CHMOD_TITLE_MANY).c_str(), input_.itemCount);
    SetWindowTextW(HWindow, title.c_str());
    ApplyDarkButtons(HWindow);
    SendDlgItemMessageW(HWindow, IDE_CHMOD_OCTAL, EM_SETLIMITTEXT, 4, 0);
    WriteGrid();
    ShowOctal();
    ShowSymbolic();
    UpdateEnabled();
}

// Recursion needs a directory; its two sub-options need the recursion; OK needs something to change.
void CChmodDialog::UpdateEnabled()
{
    const bool canRecurse = input_.anyDirectory;
    const bool recursive = canRecurse && IsDlgButtonChecked(HWindow, IDX_CHMOD_RECURSIVE) == BST_CHECKED;
    EnableWindow(GetDlgItem(HWindow, IDX_CHMOD_RECURSIVE), canRecurse);
    EnableWindow(GetDlgItem(HWindow, IDX_CHMOD_FILES), recursive);
    EnableWindow(GetDlgItem(HWindow, IDX_CHMOD_DIRS), recursive);
    const bool nothing = recursive && IsDlgButtonChecked(HWindow, IDX_CHMOD_FILES) != BST_CHECKED &&
                         IsDlgButtonChecked(HWindow, IDX_CHMOD_DIRS) != BST_CHECKED;
    EnableWindow(GetDlgItem(HWindow, IDOK), !nothing);
}

void CChmodDialog::OnGridClicked()
{
    ReadGrid();
    ShowOctal();
    ShowSymbolic();
}

// Typing an octal value decides every bit (also the ones that were mixed): "make all of them 0644".
void CChmodDialog::OnOctalChanged()
{
    wchar_t text[16] = L"";
    GetDlgItemTextW(HWindow, IDE_CHMOD_OCTAL, text, 16);
    uint32_t mode = 0;
    if (!ParseOctal(text, mode))
        return; // half typed (or wrong): the grid stays as it is, OK refuses a wrong value
    state_ = StateOfMode(mode);
    WriteGrid();
    ShowSymbolic();
}

bool CChmodDialog::OctalIsValid()
{
    wchar_t text[16] = L"";
    GetDlgItemTextW(HWindow, IDE_CHMOD_OCTAL, text, 16);
    uint32_t mode = 0;
    return ParseOctal(text, mode);
}

INT_PTR CChmodDialog::DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    try
    {
        if (uMsg == WM_COMMAND)
        {
            const int id = LOWORD(wParam);
            const int code = HIWORD(wParam);
            if (IsGridControl(id) && code == BN_CLICKED)
            {
                OnGridClicked();
                return TRUE;
            }
            if (id == IDE_CHMOD_OCTAL && code == EN_CHANGE && !updating_)
            {
                OnOctalChanged();
                return TRUE;
            }
            if ((id == IDX_CHMOD_RECURSIVE || id == IDX_CHMOD_FILES || id == IDX_CHMOD_DIRS) && code == BN_CLICKED)
            {
                UpdateEnabled();
                return TRUE;
            }
            if (id == IDOK && !OctalIsValid())
            {
                SalamanderGeneral->SalMessageBox(HWindow, LangStr(IDS_ERR_CHMOD_OCTAL).c_str(),
                                                 LangStr(IDS_PLUGINNAME).c_str(), MB_OK | MB_ICONEXCLAMATION);
                SetFocus(GetDlgItem(HWindow, IDE_CHMOD_OCTAL));
                SendDlgItemMessageW(HWindow, IDE_CHMOD_OCTAL, EM_SETSEL, 0, -1);
                return TRUE;
            }
        }
        // the base class transfers the checkbox values and centers the dialog; our controls are set up after that
        const INT_PTR result = CCenteredDialog::DialogProc(uMsg, wParam, lParam);
        if (uMsg == WM_INITDIALOG)
            InitControls();
        return result;
    }
    catch (...)
    {
        TRACE_E("CChmodDialog: unexpected exception in a window message.");
    }
    return FALSE;
}

bool ShowChmodDialog(HWND parent, const CChmodDialogInput& input, CChmodChoice& choice)
{
    CChmodDialog dialog(parent, input);
    return dialog.Run(choice);
}
