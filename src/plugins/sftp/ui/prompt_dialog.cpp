#include "precomp.h"

#include "prompt_dialog.h"

CPromptDialog::CPromptDialog(HWND parent, const std::wstring& title, const std::wstring& instruction,
                             const std::vector<CPromptItem>& prompts)
    : CCenteredDialog(HLanguage, IDD_PROMPT, IDH_PROMPT, parent), title_(title), instruction_(instruction), prompts_(prompts)
{
}

bool CPromptDialog::Run(std::vector<CSecret>& answers)
{
    answers.clear();
    try
    {
        if (prompts_.empty())
            return false;
        if (prompts_.size() > MaxPrompts)
        {
            SalamanderGeneral->SalMessageBox(Parent, LangStr(IDS_PROMPT_TOO_MANY).c_str(),
                                             LangStr(IDS_PROMPT_TITLE).c_str(), MB_OK | MB_ICONEXCLAMATION);
            return false;
        }
        answers_.clear();
        if (Execute() != IDOK || answers_.size() != prompts_.size())
        {
            answers_.clear();
            return false;
        }
        answers = std::move(answers_);
        return true;
    }
    catch (...)
    {
        answers_.clear();
        answers.clear();
        return false;
    }
}

void CPromptDialog::InitControls()
{
    SetWindowTextW(HWindow, title_.empty() ? LangStr(IDS_PROMPT_TITLE).c_str() : title_.c_str());
    SetDlgItemTextW(HWindow, IDC_PROMPT_INSTRUCTION, instruction_.c_str());
    for (size_t i = 0; i < prompts_.size(); i++)
    {
        SetDlgItemTextW(HWindow, IDC_PROMPT_LABEL1 + (int)i, prompts_[i].text.c_str());
        if (prompts_[i].echo)
        {
            HWND edit = GetDlgItem(HWindow, IDE_PROMPT1 + (int)i);
            SendMessageW(edit, EM_SETPASSWORDCHAR, 0, 0); // the template edits are ES_PASSWORD
            InvalidateRect(edit, nullptr, TRUE);
        }
    }
    RemoveUnusedRows();
    SetFocus(GetDlgItem(HWindow, IDE_PROMPT1));
}

// Hides the unused label/edit pairs (and the instruction when it is empty), moves the controls below them up and
// shrinks the dialog by the freed height. Template geometry in dialog units: the instruction takes 28 (text 24 +
// gap 4) and every pair takes 28.
void CPromptDialog::RemoveUnusedRows()
{
    RECT unit = {0, 0, 0, 28};
    MapDialogRect(HWindow, &unit);
    const int rowPx = unit.bottom; // the same for the instruction and a pair

    const bool noInstruction = instruction_.empty();
    if (noInstruction)
        ShowWindow(GetDlgItem(HWindow, IDC_PROMPT_INSTRUCTION), SW_HIDE);

    auto moveUp = [this](int id, int px)
    {
        HWND ctrl = GetDlgItem(HWindow, id);
        RECT r;
        GetWindowRect(ctrl, &r);
        MapWindowPoints(nullptr, HWindow, (POINT*)&r, 2);
        SetWindowPos(ctrl, nullptr, r.left, r.top - px, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    };

    for (int i = 0; i < (int)MaxPrompts; i++)
    {
        if (i < (int)prompts_.size())
        {
            if (noInstruction)
            {
                moveUp(IDC_PROMPT_LABEL1 + i, rowPx);
                moveUp(IDE_PROMPT1 + i, rowPx);
            }
        }
        else
        {
            ShowWindow(GetDlgItem(HWindow, IDC_PROMPT_LABEL1 + i), SW_HIDE);
            ShowWindow(GetDlgItem(HWindow, IDE_PROMPT1 + i), SW_HIDE);
            EnableWindow(GetDlgItem(HWindow, IDE_PROMPT1 + i), FALSE); // hidden controls must not take the focus
        }
    }
    const int up = ((noInstruction ? 1 : 0) + (int)(MaxPrompts - prompts_.size())) * rowPx;
    moveUp(IDOK, up);
    moveUp(IDCANCEL, up);

    RECT w;
    GetWindowRect(HWindow, &w);
    SetWindowPos(HWindow, nullptr, 0, 0, w.right - w.left, w.bottom - w.top - up,
                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
}

// Edit text (UTF-16) -> UTF-8 CSecret; the temporary UTF-16 buffer is wiped.
void CPromptDialog::CollectAnswers()
{
    answers_.clear();
    for (size_t i = 0; i < prompts_.size(); i++)
    {
        HWND edit = GetDlgItem(HWindow, IDE_PROMPT1 + (int)i);
        const int length = GetWindowTextLengthW(edit);
        std::vector<wchar_t> wide((size_t)length + 1, L'\0');
        GetWindowTextW(edit, wide.data(), length + 1);

        CSecret answer;
        const int bytes = length > 0 ? WideCharToMultiByte(CP_UTF8, 0, wide.data(), length, nullptr, 0, nullptr, nullptr) : 0;
        if (bytes > 0)
        {
            answer.resize((size_t)bytes);
            WideCharToMultiByte(CP_UTF8, 0, wide.data(), length, answer.data(), bytes, nullptr, nullptr);
        }
        SecureZeroMemory(wide.data(), wide.size() * sizeof(wchar_t));
        answers_.push_back(std::move(answer));
    }
}

// Best effort: the edit control's own buffer is replaced by an empty text.
void CPromptDialog::WipeEdits()
{
    for (size_t i = 0; i < prompts_.size(); i++)
        SetDlgItemTextW(HWindow, IDE_PROMPT1 + (int)i, L"");
}

INT_PTR CPromptDialog::DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
    case WM_INITDIALOG:
    {
        CCenteredDialog::DialogProc(uMsg, wParam, lParam);
        InitControls();
        return FALSE; // the focus was set by hand
    }

    case WM_COMMAND:
    {
        if (LOWORD(wParam) == IDOK)
        {
            CollectAnswers();
            WipeEdits();
            EndDialog(HWindow, IDOK);
            return TRUE;
        }
        if (LOWORD(wParam) == IDCANCEL)
            WipeEdits();
        break;
    }
    }
    return CCenteredDialog::DialogProc(uMsg, wParam, lParam);
}
