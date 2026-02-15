// SPDX-FileCopyrightText: 2026 Open Salamander Authors
// SPDX-License-Identifier: GPL-2.0-or-later
// CommentsTranslationProject: TRANSLATED
#pragma once

#include <cstdint>

// UTF-16 first prompt/result definitions for UI ↔ logic decoupling.

struct PromptResult
{
    enum Type
    {
        kOk,
        kCancel,
        kYes,
        kNo,
        kRetry,
        kIgnore,
        kSkip,
        kSkipAll,
        kFocus
    } type;
};

class IPrompter
{
public:
    virtual ~IPrompter() {}

    virtual PromptResult ConfirmOverwrite(const wchar_t* path, const wchar_t* existingInfo) = 0;
    virtual PromptResult ConfirmAdsLoss(const wchar_t* path) = 0;
    virtual PromptResult ConfirmDelete(const wchar_t* path, bool recycleBin) = 0;

    virtual void ShowError(const wchar_t* title, const wchar_t* message) = 0;
    virtual void ShowInfo(const wchar_t* title, const wchar_t* message) = 0;

    // Error with OK/Cancel - returns kOk or kCancel
    virtual PromptResult ConfirmError(const wchar_t* title, const wchar_t* message) = 0;
    // Question with Yes/No - returns kYes or kNo
    virtual PromptResult AskYesNo(const wchar_t* title, const wchar_t* message) = 0;
    // Question with Yes/No/Cancel - returns kYes, kNo, or kCancel
    virtual PromptResult AskYesNoCancel(const wchar_t* title, const wchar_t* message) = 0;

    // Question with Yes/No and "don't show again" checkbox - returns kYes or kNo
    virtual PromptResult AskYesNoWithCheckbox(const wchar_t* title, const wchar_t* message,
                                              const wchar_t* checkboxText, bool* checkboxValue) = 0;

    // Info with OK and "don't show again" checkbox
    virtual void ShowInfoWithCheckbox(const wchar_t* title, const wchar_t* message,
                                      const wchar_t* checkboxText, bool* checkboxValue) = 0;

    // Error with OK and "don't show again" checkbox
    virtual void ShowErrorWithCheckbox(const wchar_t* title, const wchar_t* message,
                                       const wchar_t* checkboxText, bool* checkboxValue) = 0;

    // Confirmation with OK/Cancel and "don't show again" checkbox - returns kOk or kCancel
    virtual PromptResult ConfirmWithCheckbox(const wchar_t* title, const wchar_t* message,
                                             const wchar_t* checkboxText, bool* checkboxValue) = 0;

    // Path too long dialog - returns kSkip, kSkipAll, or kFocus
    virtual PromptResult AskSkipSkipAllFocus(const wchar_t* title, const wchar_t* message) = 0;

    // Error with Skip/Skip All/Cancel - returns kSkip, kSkipAll, or kCancel
    virtual PromptResult AskSkipSkipAllCancel(const wchar_t* title, const wchar_t* message) = 0;

    // Error with Retry/Cancel - returns kRetry or kCancel
    virtual PromptResult AskRetryCancel(const wchar_t* title, const wchar_t* message) = 0;

    // Error with OK and Help button - helpId is for context-sensitive help
    virtual void ShowErrorWithHelp(const wchar_t* title, const wchar_t* message, uint32_t helpId) = 0;

    // Non-virtual ANSI convenience overloads (convert and forward to wide versions).
    // Defined in UIPrompter.cpp.
    void ShowError(const char* title, const char* message);
    void ShowInfo(const char* title, const char* message);
    PromptResult ConfirmError(const char* title, const char* message);
    PromptResult ConfirmDelete(const char* path, bool recycleBin);
    PromptResult ConfirmOverwrite(const char* path, const char* existingInfo);
    PromptResult AskYesNo(const char* title, const char* message);
};

// Global prompter used by UI and worker code. Default is UI-backed.
extern IPrompter* gPrompter;

// Returns the default UI implementation (wraps existing dialogs).
IPrompter* GetUIPrompter();
