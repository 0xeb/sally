// SPDX-FileCopyrightText: 2026 Open Salamander Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"
#include "ui/IPrompter.h"
#include "mainwnd.h"

IPrompter* gPrompter = nullptr;

class CUIPrompter : public IPrompter
{
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
        int res = MessageBoxW(MainWindow->HWindow, buf, L"Confirm Overwrite",
                              MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2);
        return res == IDYES ? PromptResult{PromptResult::kYes} : PromptResult{PromptResult::kNo};
    }

    PromptResult ConfirmAdsLoss(const wchar_t* path) override
    {
        int res = MessageBoxW(MainWindow->HWindow, path, L"Alternate Data Streams",
                              MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2);
        return res == IDYES ? PromptResult{PromptResult::kYes} : PromptResult{PromptResult::kNo};
    }

    PromptResult ConfirmDelete(const wchar_t* path, bool recycleBin) override
    {
        int res = MessageBoxW(MainWindow->HWindow, path,
                              recycleBin ? L"Confirm Delete (Recycle)" : L"Confirm Delete",
                              MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2);
        return res == IDYES ? PromptResult{PromptResult::kYes} : PromptResult{PromptResult::kNo};
    }

    void ShowError(const wchar_t* title, const wchar_t* message) override
    {
        MessageBoxW(MainWindow->HWindow, message, title, MB_OK | MB_ICONEXCLAMATION);
    }

    void ShowInfo(const wchar_t* title, const wchar_t* message) override
    {
        MessageBoxW(MainWindow->HWindow, message, title, MB_OK | MB_ICONINFORMATION);
    }

    PromptResult ConfirmError(const wchar_t* title, const wchar_t* message) override
    {
        int res = MessageBoxW(MainWindow->HWindow, message, title, MB_OKCANCEL | MB_ICONEXCLAMATION);
        return res == IDOK ? PromptResult{PromptResult::kOk} : PromptResult{PromptResult::kCancel};
    }

    PromptResult AskYesNo(const wchar_t* title, const wchar_t* message) override
    {
        int res = MessageBoxW(MainWindow->HWindow, message, title, MB_YESNO | MB_ICONQUESTION);
        return res == IDYES ? PromptResult{PromptResult::kYes} : PromptResult{PromptResult::kNo};
    }
};

IPrompter* GetUIPrompter()
{
    static CUIPrompter prompter;
    return &prompter;
}
