// SPDX-FileCopyrightText: 2026 Open Salamander Authors
// SPDX-License-Identifier: GPL-2.0-or-later
// CommentsTranslationProject: TRANSLATED

#include "precomp.h"
#include "ui/IPrompter.h"
#include "mainwnd.h"
#include "dialogs.h"
#include "common/unicode/helpers.h"

IPrompter* gPrompter = nullptr;

class CUIPrompter : public IPrompter
{
    // Safe accessor: returns NULL if MainWindow hasn't been created yet
    HWND GetParentHWND()
    {
        return (MainWindow != NULL) ? MainWindow->HWindow : NULL;
    }

public:
    PromptResult ConfirmOverwrite(const wchar_t* path, const wchar_t* existingInfo) override
    {
        wchar_t buf[1024];
        buf[0] = 0;
        if (path != NULL)
            lstrcpynW(buf, path, _countof(buf));
        if (existingInfo != NULL)
        {
            size_t len = wcslen(buf);
            if (len + 2 < _countof(buf))
            {
                buf[len++] = L'\n';
                buf[len] = 0;
                lstrcpynW(buf + len, existingInfo, (int)(_countof(buf) - len));
            }
        }
        int res = MessageBoxW(GetParentHWND(), buf, L"Confirm Overwrite",
                              MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2);
        return res == IDYES ? PromptResult{PromptResult::kYes} : PromptResult{PromptResult::kNo};
    }

    PromptResult ConfirmAdsLoss(const wchar_t* path) override
    {
        int res = MessageBoxW(GetParentHWND(), path, L"Alternate Data Streams",
                              MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2);
        return res == IDYES ? PromptResult{PromptResult::kYes} : PromptResult{PromptResult::kNo};
    }

    PromptResult ConfirmDelete(const wchar_t* path, bool recycleBin) override
    {
        int res = MessageBoxW(GetParentHWND(), path,
                              recycleBin ? L"Confirm Delete (Recycle)" : L"Confirm Delete",
                              MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2);
        return res == IDYES ? PromptResult{PromptResult::kYes} : PromptResult{PromptResult::kNo};
    }

    void ShowError(const wchar_t* title, const wchar_t* message) override
    {
        MessageBoxW(GetParentHWND(), message, title, MB_OK | MB_ICONEXCLAMATION);
    }

    void ShowInfo(const wchar_t* title, const wchar_t* message) override
    {
        MessageBoxW(GetParentHWND(), message, title, MB_OK | MB_ICONINFORMATION);
    }

    PromptResult ConfirmError(const wchar_t* title, const wchar_t* message) override
    {
        int res = MessageBoxW(GetParentHWND(), message, title, MB_OKCANCEL | MB_ICONEXCLAMATION);
        return res == IDOK ? PromptResult{PromptResult::kOk} : PromptResult{PromptResult::kCancel};
    }

    PromptResult AskYesNo(const wchar_t* title, const wchar_t* message) override
    {
        int res = MessageBoxW(GetParentHWND(), message, title, MB_YESNO | MB_ICONQUESTION);
        return res == IDYES ? PromptResult{PromptResult::kYes} : PromptResult{PromptResult::kNo};
    }

    PromptResult AskYesNoCancel(const wchar_t* title, const wchar_t* message) override
    {
        int res = MessageBoxW(GetParentHWND(), message, title, MB_YESNOCANCEL | MB_ICONQUESTION);
        if (res == IDYES)
            return {PromptResult::kYes};
        if (res == IDNO)
            return {PromptResult::kNo};
        return {PromptResult::kCancel};
    }

    PromptResult AskYesNoWithCheckbox(const wchar_t* title, const wchar_t* message,
                                      const wchar_t* checkboxText, bool* checkboxValue) override
    {
        std::string titleA = WideToAnsi(title);
        std::string msgA = WideToAnsi(message);
        std::string cbTextA = WideToAnsi(checkboxText);

        MSGBOXEX_PARAMS params;
        memset(&params, 0, sizeof(params));
        params.HParent = GetParentHWND();
        params.Flags = MSGBOXEX_YESNO | MSGBOXEX_ESCAPEENABLED | MSGBOXEX_ICONQUESTION | MSGBOXEX_SILENT | MSGBOXEX_HINT;
        params.Caption = titleA.c_str();
        params.Text = msgA.c_str();
        params.CheckBoxText = cbTextA.c_str();
        BOOL cbVal = checkboxValue ? (*checkboxValue ? TRUE : FALSE) : FALSE;
        params.CheckBoxValue = &cbVal;
        int res = SalMessageBoxEx(&params);
        if (checkboxValue)
            *checkboxValue = (cbVal != FALSE);
        return res == IDYES ? PromptResult{PromptResult::kYes} : PromptResult{PromptResult::kNo};
    }

    void ShowInfoWithCheckbox(const wchar_t* title, const wchar_t* message,
                              const wchar_t* checkboxText, bool* checkboxValue) override
    {
        std::string titleA = WideToAnsi(title);
        std::string msgA = WideToAnsi(message);
        std::string cbTextA = WideToAnsi(checkboxText);

        MSGBOXEX_PARAMS params;
        memset(&params, 0, sizeof(params));
        params.HParent = GetParentHWND();
        params.Flags = MB_OK | MB_ICONINFORMATION | MSGBOXEX_HINT;
        params.Caption = titleA.c_str();
        params.Text = msgA.c_str();
        params.CheckBoxText = cbTextA.c_str();
        BOOL cbVal = checkboxValue ? (*checkboxValue ? TRUE : FALSE) : FALSE;
        params.CheckBoxValue = &cbVal;
        SalMessageBoxEx(&params);
        if (checkboxValue)
            *checkboxValue = (cbVal != FALSE);
    }

    void ShowErrorWithCheckbox(const wchar_t* title, const wchar_t* message,
                               const wchar_t* checkboxText, bool* checkboxValue) override
    {
        std::string titleA = WideToAnsi(title);
        std::string msgA = WideToAnsi(message);
        std::string cbTextA = WideToAnsi(checkboxText);

        MSGBOXEX_PARAMS params;
        memset(&params, 0, sizeof(params));
        params.HParent = GetParentHWND();
        params.Flags = MB_OK | MB_ICONERROR;
        params.Caption = titleA.c_str();
        params.Text = msgA.c_str();
        params.CheckBoxText = cbTextA.c_str();
        BOOL cbVal = checkboxValue ? (*checkboxValue ? TRUE : FALSE) : FALSE;
        params.CheckBoxValue = &cbVal;
        SalMessageBoxEx(&params);
        if (checkboxValue)
            *checkboxValue = (cbVal != FALSE);
    }

    PromptResult ConfirmWithCheckbox(const wchar_t* title, const wchar_t* message,
                                     const wchar_t* checkboxText, bool* checkboxValue) override
    {
        std::string titleA = WideToAnsi(title);
        std::string msgA = WideToAnsi(message);
        std::string cbTextA = WideToAnsi(checkboxText);

        MSGBOXEX_PARAMS params;
        memset(&params, 0, sizeof(params));
        params.HParent = GetParentHWND();
        params.Flags = MSGBOXEX_OKCANCEL | MSGBOXEX_ICONQUESTION | MSGBOXEX_HINT;
        params.Caption = titleA.c_str();
        params.Text = msgA.c_str();
        params.CheckBoxText = cbTextA.c_str();
        BOOL cbVal = checkboxValue ? (*checkboxValue ? TRUE : FALSE) : FALSE;
        params.CheckBoxValue = &cbVal;
        int res = SalMessageBoxEx(&params);
        if (checkboxValue)
            *checkboxValue = (cbVal != FALSE);
        return res == IDOK ? PromptResult{PromptResult::kOk} : PromptResult{PromptResult::kCancel};
    }

    PromptResult AskSkipSkipAllFocus(const wchar_t* title, const wchar_t* message) override
    {
        std::string titleA = WideToAnsi(title);
        std::string msgA = WideToAnsi(message);

        MSGBOXEX_PARAMS params;
        memset(&params, 0, sizeof(params));
        params.HParent = GetParentHWND();
        params.Flags = MSGBOXEX_YESNOOKCANCEL | MB_ICONEXCLAMATION | MSGBOXEX_DEFBUTTON3 | MSGBOXEX_SILENT;
        params.Caption = titleA.c_str();
        params.Text = msgA.c_str();
        char aliasBtnNames[200];
        sprintf(aliasBtnNames, "%d\t%s\t%d\t%s\t%d\t%s",
                DIALOG_YES, LoadStr(IDS_MSGBOXBTN_SKIP),
                DIALOG_NO, LoadStr(IDS_MSGBOXBTN_SKIPALL),
                DIALOG_OK, LoadStr(IDS_MSGBOXBTN_FOCUS));
        params.AliasBtnNames = aliasBtnNames;
        int res = SalMessageBoxEx(&params);
        if (res == DIALOG_YES)
            return {PromptResult::kSkip};
        if (res == DIALOG_NO)
            return {PromptResult::kSkipAll};
        return {PromptResult::kFocus};
    }

    PromptResult AskSkipSkipAllCancel(const wchar_t* title, const wchar_t* message) override
    {
        std::string titleA = WideToAnsi(title);
        std::string msgA = WideToAnsi(message);

        MSGBOXEX_PARAMS params;
        memset(&params, 0, sizeof(params));
        params.HParent = GetParentHWND();
        params.Flags = MB_YESNOCANCEL | MB_ICONEXCLAMATION | MSGBOXEX_DEFBUTTON3 | MSGBOXEX_SILENT;
        params.Caption = titleA.c_str();
        params.Text = msgA.c_str();
        char aliasBtnNames[200];
        sprintf(aliasBtnNames, "%d\t%s\t%d\t%s",
                DIALOG_YES, LoadStr(IDS_MSGBOXBTN_SKIP),
                DIALOG_NO, LoadStr(IDS_MSGBOXBTN_SKIPALL));
        params.AliasBtnNames = aliasBtnNames;
        int res = SalMessageBoxEx(&params);
        if (res == DIALOG_YES)
            return {PromptResult::kSkip};
        if (res == DIALOG_NO)
            return {PromptResult::kSkipAll};
        return {PromptResult::kCancel};
    }

    PromptResult AskRetryCancel(const wchar_t* title, const wchar_t* message) override
    {
        std::string titleA = WideToAnsi(title);
        std::string msgA = WideToAnsi(message);
        int res = SalMessageBox(GetParentHWND(), msgA.c_str(), titleA.c_str(),
                                MB_RETRYCANCEL | MB_ICONEXCLAMATION);
        return {res == IDRETRY ? PromptResult::kRetry : PromptResult::kCancel};
    }

    void ShowErrorWithHelp(const wchar_t* title, const wchar_t* message, uint32_t helpId) override
    {
        std::string titleA = WideToAnsi(title);
        std::string msgA = WideToAnsi(message);

        MSGBOXEX_PARAMS params;
        memset(&params, 0, sizeof(params));
        params.HParent = GetParentHWND();
        params.Flags = MSGBOXEX_OK | MSGBOXEX_HELP | MSGBOXEX_ICONEXCLAMATION;
        params.Caption = titleA.c_str();
        params.Text = msgA.c_str();
        params.ContextHelpId = helpId;
        params.HelpCallback = MessageBoxHelpCallback;
        SalMessageBoxEx(&params);
    }
};

IPrompter* GetUIPrompter()
{
    static CUIPrompter prompter;
    return &prompter;
}

// Non-virtual ANSI convenience overloads — convert and forward to wide versions.
void IPrompter::ShowError(const char* title, const char* message)
{
    ShowError(AnsiToWide(title).c_str(), AnsiToWide(message).c_str());
}

void IPrompter::ShowInfo(const char* title, const char* message)
{
    ShowInfo(AnsiToWide(title).c_str(), AnsiToWide(message).c_str());
}

PromptResult IPrompter::ConfirmError(const char* title, const char* message)
{
    return ConfirmError(AnsiToWide(title).c_str(), AnsiToWide(message).c_str());
}

PromptResult IPrompter::ConfirmDelete(const char* path, bool recycleBin)
{
    return ConfirmDelete(AnsiToWide(path).c_str(), recycleBin);
}

PromptResult IPrompter::ConfirmOverwrite(const char* path, const char* existingInfo)
{
    return ConfirmOverwrite(path ? AnsiToWide(path).c_str() : nullptr,
                            existingInfo ? AnsiToWide(existingInfo).c_str() : nullptr);
}

PromptResult IPrompter::AskYesNo(const char* title, const char* message)
{
    return AskYesNo(AnsiToWide(title).c_str(), AnsiToWide(message).c_str());
}
