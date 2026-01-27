// SPDX-FileCopyrightText: 2026 Open Salamander Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"
#include "ui/IPrompter.h"
#include "dialogs.h"
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
        int res = SalMessageBox(MainWindow->HWindow, buf, LoadStr(IDS_CONFIRM_OVERWRITEW),
                                MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2);
        return res == IDYES ? PromptResult{PromptResult::kYes} : PromptResult{PromptResult::kNo};
    }

    PromptResult ConfirmAdsLoss(const wchar_t* path) override
    {
        int res = SalMessageBox(MainWindow->HWindow, path, LoadStr(IDS_CONFIRM_ADSLOSSW),
                                MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2);
        return res == IDYES ? PromptResult{PromptResult::kYes} : PromptResult{PromptResult::kNo};
    }

    PromptResult ConfirmDelete(const wchar_t* path, bool recycleBin) override
    {
        int res = SalMessageBox(MainWindow->HWindow, path,
                                LoadStr(recycleBin ? IDS_CONFIRM_DELETE_RECYCLEW : IDS_CONFIRM_DELETEW),
                                MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2);
        return res == IDYES ? PromptResult{PromptResult::kYes} : PromptResult{PromptResult::kNo};
    }

    void ShowError(const wchar_t* title, const wchar_t* message) override
    {
        SalMessageBox(MainWindow->HWindow, message, title, MB_OK | MB_ICONEXCLAMATION);
    }

    void ShowInfo(const wchar_t* title, const wchar_t* message) override
    {
        SalMessageBox(MainWindow->HWindow, message, title, MB_OK | MB_ICONINFORMATION);
    }
};

IPrompter* GetUIPrompter()
{
    static CUIPrompter prompter;
    return &prompter;
}
