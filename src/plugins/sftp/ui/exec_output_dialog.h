// Shows what a command printed (command line of the panel): a read-only text, a status line, Copy and Close. It knows
// nothing about the file system or the session: the caller decodes and prepares the text (see OutputToEditText).
#pragma once

#include <string>

#include "centered_dialog.h"

class CExecOutputDialog : public CCenteredDialog
{
public:
    // editText: CRLF line ends (an edit control needs them); statusText: "Exit code 3", ...
    CExecOutputDialog(HWND parent, std::wstring editText, std::wstring statusText);

    // Shows the dialog; returns when it is closed. Never throws.
    void Run();

    ~CExecOutputDialog();

protected:
    INT_PTR DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam) override;

private:
    void InitControls();
    void CopyToClipboard();
    void SetFixedFont(HWND edit);

    std::wstring text_;
    std::wstring status_;
    HFONT font_ = nullptr; // fixed-width font of the output edit; owned here, deleted in the destructor (the window is gone by then)
};
