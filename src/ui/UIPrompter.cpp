// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"
#include "IPrompter.h"
#include "DeletePromptPolicy.h"
#include "../mainwnd.h"
#include "../dialogs.h"
#include "../common/unicode/helpers.h"

IPrompter* gPrompter = nullptr;

class CUIPrompter : public IPrompter
{
    // Safe accessor: returns NULL if MainWindow hasn't been created yet
    HWND GetDefaultParentHWND()
    {
        return (MainWindow != NULL) ? MainWindow->HWindow : NULL;
    }

    HWND ResolveParentHWND(HWND parent)
    {
        if (parent != NULL && IsWindow(parent))
            return parent;
        return GetDefaultParentHWND();
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
        int res = MessageBoxW(GetDefaultParentHWND(), buf, L"Confirm Overwrite",
                              MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2);
        return res == IDYES ? PromptResult{PromptResult::kYes} : PromptResult{PromptResult::kNo};
    }

    PromptResult ConfirmAdsLoss(const wchar_t* path) override
    {
        int res = MessageBoxW(GetDefaultParentHWND(), path, L"Alternate Data Streams",
                              MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2);
        return res == IDYES ? PromptResult{PromptResult::kYes} : PromptResult{PromptResult::kNo};
    }

    PromptResult ConfirmDelete(const wchar_t* path, bool recycleBin) override
    {
        int res = MessageBoxW(GetDefaultParentHWND(), path,
                              recycleBin ? L"Confirm Delete (Recycle)" : L"Confirm Delete",
                              DeletePromptPolicy::ConfirmDeleteMessageBoxFlags());
        return DeletePromptPolicy::MapConfirmDeleteResult(res);
    }

    void ShowError(const wchar_t* title, const wchar_t* message) override
    {
        MessageBoxW(GetDefaultParentHWND(), message, title, MB_OK | MB_ICONEXCLAMATION);
    }

    void ShowError(HWND parent, const wchar_t* title, const wchar_t* message) override
    {
        SalMessageBoxW(ResolveParentHWND(parent), message, title,
                      MB_OK | MB_ICONEXCLAMATION);
    }

    void ShowInfo(const wchar_t* title, const wchar_t* message) override
    {
        MessageBoxW(GetDefaultParentHWND(), message, title, MB_OK | MB_ICONINFORMATION);
    }

    void ShowInfo(HWND parent, const wchar_t* title, const wchar_t* message) override
    {
        SalMessageBoxW(ResolveParentHWND(parent), message, title,
                      MB_OK | MB_ICONINFORMATION);
    }

    PromptResult ConfirmError(const wchar_t* title, const wchar_t* message) override
    {
        int res = MessageBoxW(GetDefaultParentHWND(), message, title, MB_OKCANCEL | MB_ICONEXCLAMATION);
        return res == IDOK ? PromptResult{PromptResult::kOk} : PromptResult{PromptResult::kCancel};
    }

    PromptResult ConfirmError(HWND parent, const wchar_t* title, const wchar_t* message) override
    {
        int res = SalMessageBoxW(ResolveParentHWND(parent), message, title,
                                MB_OKCANCEL | MB_ICONEXCLAMATION);
        return res == IDOK ? PromptResult{PromptResult::kOk} : PromptResult{PromptResult::kCancel};
    }

    PromptResult AskYesNo(const wchar_t* title, const wchar_t* message) override
    {
        int res = MessageBoxW(GetDefaultParentHWND(), message, title, MB_YESNO | MB_ICONQUESTION);
        return res == IDYES ? PromptResult{PromptResult::kYes} : PromptResult{PromptResult::kNo};
    }

    PromptResult AskYesNo(HWND parent, const wchar_t* title, const wchar_t* message) override
    {
        int res = SalMessageBoxW(ResolveParentHWND(parent), message, title,
                                MB_YESNO | MB_ICONQUESTION);
        return res == IDYES ? PromptResult{PromptResult::kYes} : PromptResult{PromptResult::kNo};
    }

    PromptResult AskYesNoCancel(const wchar_t* title, const wchar_t* message) override
    {
        int res = MessageBoxW(GetDefaultParentHWND(), message, title, MB_YESNOCANCEL | MB_ICONQUESTION);
        if (res == IDYES)
            return {PromptResult::kYes};
        if (res == IDNO)
            return {PromptResult::kNo};
        return {PromptResult::kCancel};
    }

    PromptResult AskYesNoCancel(HWND parent, const wchar_t* title, const wchar_t* message) override
    {
        int res = SalMessageBoxW(ResolveParentHWND(parent), message, title,
                                MB_YESNOCANCEL | MB_ICONQUESTION);
        if (res == IDYES)
            return {PromptResult::kYes};
        if (res == IDNO)
            return {PromptResult::kNo};
        return {PromptResult::kCancel};
    }

    PromptResult AskYesNoWithCheckbox(const wchar_t* title, const wchar_t* message,
                                      const wchar_t* checkboxText, bool* checkboxValue) override
    {

        MSGBOXEX_PARAMS params;
        memset(&params, 0, sizeof(params));
        params.HParent = GetDefaultParentHWND();
        params.Flags = MSGBOXEX_YESNO | MSGBOXEX_ESCAPEENABLED | MSGBOXEX_ICONQUESTION | MSGBOXEX_SILENT | MSGBOXEX_HINT;
        params.Caption = title;
        params.Text = message;
        params.CheckBoxText = checkboxText;
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

        MSGBOXEX_PARAMS params;
        memset(&params, 0, sizeof(params));
        params.HParent = GetDefaultParentHWND();
        params.Flags = MB_OK | MB_ICONINFORMATION | MSGBOXEX_HINT;
        params.Caption = title;
        params.Text = message;
        params.CheckBoxText = checkboxText;
        BOOL cbVal = checkboxValue ? (*checkboxValue ? TRUE : FALSE) : FALSE;
        params.CheckBoxValue = &cbVal;
        SalMessageBoxEx(&params);
        if (checkboxValue)
            *checkboxValue = (cbVal != FALSE);
    }

    void ShowErrorWithCheckbox(const wchar_t* title, const wchar_t* message,
                               const wchar_t* checkboxText, bool* checkboxValue) override
    {

        MSGBOXEX_PARAMS params;
        memset(&params, 0, sizeof(params));
        params.HParent = GetDefaultParentHWND();
        params.Flags = MB_OK | MB_ICONERROR;
        params.Caption = title;
        params.Text = message;
        params.CheckBoxText = checkboxText;
        BOOL cbVal = checkboxValue ? (*checkboxValue ? TRUE : FALSE) : FALSE;
        params.CheckBoxValue = &cbVal;
        SalMessageBoxEx(&params);
        if (checkboxValue)
            *checkboxValue = (cbVal != FALSE);
    }

    PromptResult ConfirmWithCheckbox(const wchar_t* title, const wchar_t* message,
                                     const wchar_t* checkboxText, bool* checkboxValue) override
    {

        MSGBOXEX_PARAMS params;
        memset(&params, 0, sizeof(params));
        params.HParent = GetDefaultParentHWND();
        params.Flags = MSGBOXEX_OKCANCEL | MSGBOXEX_ICONQUESTION | MSGBOXEX_HINT;
        params.Caption = title;
        params.Text = message;
        params.CheckBoxText = checkboxText;
        BOOL cbVal = checkboxValue ? (*checkboxValue ? TRUE : FALSE) : FALSE;
        params.CheckBoxValue = &cbVal;
        int res = SalMessageBoxEx(&params);
        if (checkboxValue)
            *checkboxValue = (cbVal != FALSE);
        return res == IDOK ? PromptResult{PromptResult::kOk} : PromptResult{PromptResult::kCancel};
    }

    PromptResult AskSkipSkipAllFocus(const wchar_t* title, const wchar_t* message) override
    {
        MSGBOXEX_PARAMS params;
        memset(&params, 0, sizeof(params));
        params.HParent = GetDefaultParentHWND();
        params.Flags = MSGBOXEX_YESNOOKCANCEL | MB_ICONEXCLAMATION | MSGBOXEX_DEFBUTTON3 | MSGBOXEX_SILENT;
        params.Caption = title;
        params.Text = message;
        wchar_t aliasBtnNames[200];
        swprintf_s(aliasBtnNames, L"%d\t%s\t%d\t%s\t%d\t%s",
                DIALOG_YES, LoadStrW(IDS_MSGBOXBTN_SKIP),
                DIALOG_NO, LoadStrW(IDS_MSGBOXBTN_SKIPALL),
                DIALOG_OK, LoadStrW(IDS_MSGBOXBTN_FOCUS));
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
        MSGBOXEX_PARAMS params;
        memset(&params, 0, sizeof(params));
        params.HParent = GetDefaultParentHWND();
        params.Flags = MB_YESNOCANCEL | MB_ICONEXCLAMATION | MSGBOXEX_DEFBUTTON3 | MSGBOXEX_SILENT;
        params.Caption = title;
        params.Text = message;
        wchar_t aliasBtnNames[200];
        swprintf_s(aliasBtnNames, L"%d\t%s\t%d\t%s",
                DIALOG_YES, LoadStrW(IDS_MSGBOXBTN_SKIP),
                DIALOG_NO, LoadStrW(IDS_MSGBOXBTN_SKIPALL));
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
        int res = SalMessageBoxW(GetDefaultParentHWND(), message, title,
                                MB_RETRYCANCEL | MB_ICONEXCLAMATION);
        return {res == IDRETRY ? PromptResult::kRetry : PromptResult::kCancel};
    }

    PromptResult AskRetryCancel(HWND parent, const wchar_t* title, const wchar_t* message) override
    {
        int res = SalMessageBoxW(ResolveParentHWND(parent), message, title,
                                MB_RETRYCANCEL | MB_ICONEXCLAMATION);
        return {res == IDRETRY ? PromptResult::kRetry : PromptResult::kCancel};
    }

    void ShowErrorWithHelp(const wchar_t* title, const wchar_t* message, uint32_t helpId) override
    {
        MSGBOXEX_PARAMS params;
        memset(&params, 0, sizeof(params));
        params.HParent = GetDefaultParentHWND();
        params.Flags = MSGBOXEX_OK | MSGBOXEX_HELP | MSGBOXEX_ICONEXCLAMATION;
        params.Caption = title;
        params.Text = message;
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
void IPrompter::ShowError(HWND parent, const wchar_t* title, const wchar_t* message)
{
    (void)parent;
    ShowError(title, message);
}

void IPrompter::ShowInfo(HWND parent, const wchar_t* title, const wchar_t* message)
{
    (void)parent;
    ShowInfo(title, message);
}

PromptResult IPrompter::ConfirmError(HWND parent, const wchar_t* title, const wchar_t* message)
{
    (void)parent;
    return ConfirmError(title, message);
}

PromptResult IPrompter::AskYesNo(HWND parent, const wchar_t* title, const wchar_t* message)
{
    (void)parent;
    return AskYesNo(title, message);
}

PromptResult IPrompter::AskYesNoCancel(HWND parent, const wchar_t* title, const wchar_t* message)
{
    (void)parent;
    return AskYesNoCancel(title, message);
}

PromptResult IPrompter::AskRetryCancel(HWND parent, const wchar_t* title, const wchar_t* message)
{
    (void)parent;
    return AskRetryCancel(title, message);
}
