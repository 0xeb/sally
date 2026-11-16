#include "precomp.h"

#include "solve_error_dialog.h"

#include "dark_controls.h"
#include "encoding.h"
#include "local_name.h"
#include "operation.h"
#include "problem_text.h"
#include "progress_format.h"

namespace
{
std::wstring FormatUnixTime(int64_t unixSeconds)
{
    if (unixSeconds <= 0)
        return LangStr(IDS_SOLVE_UNKNOWN_DATE);
    // Unix seconds -> FILETIME (100 ns since 1601) -> local time
    ULARGE_INTEGER ticks;
    ticks.QuadPart = (static_cast<unsigned long long>(unixSeconds) + 11644473600ull) * 10000000ull;
    FILETIME utc = {ticks.LowPart, ticks.HighPart};
    SYSTEMTIME utcTime, local;
    if (!FileTimeToSystemTime(&utc, &utcTime) || !SystemTimeToTzSpecificLocalTime(nullptr, &utcTime, &local))
        return LangStr(IDS_SOLVE_UNKNOWN_DATE);

    wchar_t date[64] = L"";
    wchar_t time[64] = L"";
    GetDateFormatW(LOCALE_USER_DEFAULT, DATE_SHORTDATE, &local, nullptr, date, 64);
    GetTimeFormatW(LOCALE_USER_DEFAULT, TIME_NOSECONDS, &local, nullptr, time, 64);
    return std::wstring(date) + L" " + time;
}

// The last path component of the target: a remote name (decoded) for an upload, else the local one.
std::wstring LeafName(const CQueueItem& item)
{
    if (item.type == ItemType::Upload)
    {
        const std::string::size_type slash = item.remotePath.find_last_of('/');
        const std::string leaf = slash == std::string::npos ? item.remotePath : item.remotePath.substr(slash + 1);
        return CNameCodec::Utf8().Decode(leaf);
    }
    const std::wstring::size_type slash = item.localPath.find_last_of(L"\\/");
    return slash == std::wstring::npos ? item.localPath : item.localPath.substr(slash + 1);
}
} // namespace

CSolveErrorDialog::CSolveErrorDialog(HWND parent, const CQueueItem& item)
    : CCenteredDialog(HLanguage, IDD_SOLVE_ERROR, IDH_SOLVE_ERROR, parent), item_(item), offers_(SolveOffersFor(item)),
      newName_(LeafName(item))
{
}

bool CSolveErrorDialog::Run(CSolveChoice& choice)
{
    try
    {
        if (Execute() != IDOK) // Choose() ends the dialog with IDOK
            return false;
        choice = choice_;
        return true;
    }
    catch (...)
    {
        return false;
    }
}

void CSolveErrorDialog::Transfer(CTransferInfo& ti)
{
    ti.EditLine(IDE_SOLVE_NEWNAME, newName_);
    ti.CheckBox(IDX_SOLVE_ALL, applyToAll_);
}

std::wstring CSolveErrorDialog::SideInfo(int formatId, uint64_t size, int64_t mtime) const
{
    return SPLFormatStringOwned(LangStr(formatId).c_str(), FormatBytes(size).c_str(), FormatUnixTime(mtime).c_str());
}

void CSolveErrorDialog::InitControls()
{
    ApplyDarkButtons(HWindow);
    const std::wstring name = !item_.localPath.empty() ? item_.localPath : CNameCodec::Utf8().Decode(item_.remotePath);
    SetDlgItemTextW(HWindow, IDC_SOLVE_NAME, name.c_str());
    SetDlgItemTextW(HWindow, IDC_SOLVE_MESSAGE, ProblemTextWithDetails(item_.problem, ItemDetails(item_)).c_str());

    if (IsTransferType(item_.type))
        SetDlgItemTextW(HWindow, IDC_SOLVE_SOURCE, SideInfo(IDS_SOLVE_SOURCE_INFO, item_.size, item_.mtime).c_str());
    if (item_.problem == Problem::TargetExists)
        SetDlgItemTextW(HWindow, IDC_SOLVE_TARGET,
                        SideInfo(IDS_SOLVE_TARGET_INFO, item_.targetSize, item_.targetMtime).c_str());
    EnableOffered();
}

void CSolveErrorDialog::EnableOffered()
{
    EnableWindow(GetDlgItem(HWindow, IDB_SOLVE_OVERWRITE), offers_.overwrite);
    EnableWindow(GetDlgItem(HWindow, IDB_SOLVE_RESUME), offers_.resume);
    EnableWindow(GetDlgItem(HWindow, IDB_SOLVE_RETRY), offers_.retry);
    EnableWindow(GetDlgItem(HWindow, IDB_SOLVE_SKIP), offers_.skip);
    EnableWindow(GetDlgItem(HWindow, IDB_SOLVE_RENAME), offers_.rename);
    EnableWindow(GetDlgItem(HWindow, IDE_SOLVE_NEWNAME), offers_.rename);
    EnableWindow(GetDlgItem(HWindow, IDX_SOLVE_ALL), offers_.applyToAll);

    const int defaultId = DefaultButtonId();
    SendMessageW(HWindow, DM_SETDEFID, defaultId, 0);
    SetFocus(GetDlgItem(HWindow, defaultId));
}

// The first offered answer is the default button (Enter); Enter inside the name edit means Rename.
int CSolveErrorDialog::DefaultButtonId() const
{
    if (offers_.retry)
        return IDB_SOLVE_RETRY;
    return offers_.skip ? IDB_SOLVE_SKIP : IDCANCEL;
}

// The same rules the worker applies (a download's new name is a Windows file name; an upload's is one path component).
bool CSolveErrorDialog::NewNameIsValid(const std::wstring& name) const
{
    if (name.empty())
        return false;
    if (item_.type == ItemType::Download)
        return IsValidLocalName(name);
    return name.find_first_of(L"/\\") == std::wstring::npos && name != L"." && name != L"..";
}

// Reads the controls, checks a rename, remembers the answer and ends the dialog.
void CSolveErrorDialog::Choose(SolveAction action)
{
    if (!IsOffered(offers_, action) || !TransferData(ttDataFromWindow))
        return;
    if (action == SolveAction::Rename && !NewNameIsValid(newName_))
    {
        SalamanderGeneral->SalMessageBox(HWindow, LangStr(IDS_SOLVE_BAD_NAME).c_str(), LangStr(IDS_PLUGINNAME).c_str(),
                                         MB_OK | MB_ICONEXCLAMATION);
        SetFocus(GetDlgItem(HWindow, IDE_SOLVE_NEWNAME));
        return;
    }
    choice_.action = action;
    choice_.newName = action == SolveAction::Rename ? newName_ : std::wstring();
    choice_.applyToAll = applyToAll_ != 0 && action != SolveAction::Rename && offers_.applyToAll;
    EndDialog(HWindow, IDOK);
}

INT_PTR CSolveErrorDialog::DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    try
    {
        switch (uMsg)
        {
        case WM_INITDIALOG:
        {
            CCenteredDialog::DialogProc(uMsg, wParam, lParam); // transfers the values
            InitControls();
            return FALSE; // the focus was set by hand
        }

        case WM_COMMAND:
            switch (LOWORD(wParam))
            {
            case IDE_SOLVE_NEWNAME:
                if (HIWORD(wParam) == EN_SETFOCUS)
                    SendMessageW(HWindow, DM_SETDEFID, IDB_SOLVE_RENAME, 0);
                else if (HIWORD(wParam) == EN_KILLFOCUS)
                    SendMessageW(HWindow, DM_SETDEFID, DefaultButtonId(), 0);
                break;
            case IDOK: // no such button; the base class would close the dialog without an answer
                return TRUE;
            case IDB_SOLVE_OVERWRITE:
                Choose(SolveAction::Overwrite);
                return TRUE;
            case IDB_SOLVE_RESUME:
                Choose(SolveAction::Resume);
                return TRUE;
            case IDB_SOLVE_RENAME:
                Choose(SolveAction::Rename);
                return TRUE;
            case IDB_SOLVE_RETRY:
                Choose(SolveAction::Retry);
                return TRUE;
            case IDB_SOLVE_SKIP:
                Choose(SolveAction::Skip);
                return TRUE;
            }
            break;
        }
    }
    catch (...)
    {
        TRACE_E("CSolveErrorDialog: unexpected exception in a window message.");
        return FALSE;
    }
    return CCenteredDialog::DialogProc(uMsg, wParam, lParam);
}

void ShowSolveErrorDialog(HWND parent, COperation& operation, uint64_t uid)
{
    try
    {
        CQueueItem item;
        if (!operation.Queue().GetItem(uid, item) ||
            (item.state != ItemState::Failed && item.state != ItemState::UserInputNeeded))
            return;
        CSolveErrorDialog dialog(parent, item);
        CSolveChoice choice;
        if (dialog.Run(choice))
            operation.Solve(uid, choice.action, choice.newName, choice.applyToAll);
    }
    catch (...)
    {
        TRACE_E("ShowSolveErrorDialog: unexpected exception.");
    }
}
