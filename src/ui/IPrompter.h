// SPDX-FileCopyrightText: 2026 Open Salamander Authors
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

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
        kIgnore
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
};

// Global prompter used by UI and worker code. Default is UI-backed.
extern IPrompter* gPrompter;

// Returns the default UI implementation (wraps existing dialogs).
IPrompter* GetUIPrompter();
