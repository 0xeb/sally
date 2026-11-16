// Prompt dialog for keyboard-interactive prompts and key passphrase requests: an instruction text and 1..4
// label/edit pairs. The answers are collected as CSecret and the dialog's own copies are wiped.
#pragma once

#include <string>
#include <vector>

#include "centered_dialog.h"
#include "secret.h"

struct CPromptItem
{
    std::wstring text;
    bool echo = false; // false: the typed text is hidden like a password
};

class CPromptDialog : public CCenteredDialog
{
public:
    static const size_t MaxPrompts = 4; // the pairs in the dialog template

    // title may be empty (a default title is used); instruction may be empty (the space is removed).
    CPromptDialog(HWND parent, const std::wstring& title, const std::wstring& instruction,
                  const std::vector<CPromptItem>& prompts);

    // Shows the dialog. True: OK, answers has one entry per prompt (UTF-8). False: Cancel/ESC/close, or the dialog
    // could not be shown (also when more than MaxPrompts or no prompt was requested; too many shows an error).
    bool Run(std::vector<CSecret>& answers);

protected:
    INT_PTR DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam) override;

private:
    void InitControls();
    void RemoveUnusedRows();
    void CollectAnswers();
    void WipeEdits();

    std::wstring title_;
    std::wstring instruction_;
    std::vector<CPromptItem> prompts_;
    std::vector<CSecret> answers_; // CSecret wipes itself
};
