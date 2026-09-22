// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

// legacy_for_operations_to_core — frozen v107 operation callback over the
// live wide API.

// Never include precomp.h here. Standard-library headers must be visible at
// global scope before sdk107.h enters its frozen namespace wrapper.

#define NOMINMAX
#include <windows.h>

#include <string>

#include "compat/legacy_to_core.h"

#include "compat/legacy_convert.h"

namespace sally::compat
{
    namespace
    {

        bool WidenOperationsOptional(const char* value, std::wstring& storage,
                                     const wchar_t*& result)
        {
            if (value == nullptr)
            {
                result = nullptr;
                return true;
            }
            if (!WidenPluginText(value, storage))
            {
                result = nullptr;
                return false;
            }
            result = storage.c_str();
            return true;
        }

        ::CQuadWord ToWideQuad(const sdk107::CQuadWord& value)
        {
            return ::CQuadWord(value.LoDWord, value.HiDWord);
        }

    } // namespace

    CLegacySalamanderForOperations::CLegacySalamanderForOperations(
        ::CSalamanderForOperationsAbstract& wideOperations)
        : WideOperations(wideOperations)
    {
    }

    void WINAPI CLegacySalamanderForOperations::OpenProgressDialog(
        const char* title, BOOL twoProgressBars, HWND parent, BOOL fileProgress)
    {
        std::wstring titleW;
        const wchar_t* liveTitle = nullptr;
        if (!WidenOperationsOptional(title, titleW, liveTitle))
            return;
        WideOperations.OpenProgressDialog(liveTitle, twoProgressBars, parent,
                                          fileProgress);
    }

    void WINAPI CLegacySalamanderForOperations::ProgressDialogAddText(
        const char* txt, BOOL delayedPaint)
    {
        std::wstring textW;
        const wchar_t* liveText = nullptr;
        if (!WidenOperationsOptional(txt, textW, liveText))
            return;
        WideOperations.ProgressDialogAddText(liveText, delayedPaint);
    }

    void WINAPI CLegacySalamanderForOperations::ProgressSetTotalSize(
        const sdk107::CQuadWord& totalSize1,
        const sdk107::CQuadWord& totalSize2)
    {
        const ::CQuadWord wideTotalSize1 = ToWideQuad(totalSize1);
        const ::CQuadWord wideTotalSize2 = ToWideQuad(totalSize2);
        WideOperations.ProgressSetTotalSize(wideTotalSize1, wideTotalSize2);
    }

    BOOL WINAPI CLegacySalamanderForOperations::ProgressSetSize(
        const sdk107::CQuadWord& size1, const sdk107::CQuadWord& size2,
        BOOL delayedPaint)
    {
        const ::CQuadWord wideSize1 = ToWideQuad(size1);
        const ::CQuadWord wideSize2 = ToWideQuad(size2);
        return WideOperations.ProgressSetSize(wideSize1, wideSize2, delayedPaint);
    }

    BOOL WINAPI CLegacySalamanderForOperations::ProgressAddSize(
        int size, BOOL delayedPaint)
    {
        return WideOperations.ProgressAddSize(size, delayedPaint);
    }

    void WINAPI CLegacySalamanderForOperations::ProgressEnableCancel(BOOL enable)
    {
        WideOperations.ProgressEnableCancel(enable);
    }

    HWND WINAPI CLegacySalamanderForOperations::ProgressGetHWND()
    {
        return WideOperations.ProgressGetHWND();
    }

    void WINAPI CLegacySalamanderForOperations::CloseProgressDialog()
    {
        WideOperations.CloseProgressDialog();
    }

    BOOL WINAPI CLegacySalamanderForOperations::MoveFiles(
        const char* source, const char* target, const char* remapNameFrom,
        const char* remapNameTo)
    {
        std::wstring sourceW;
        std::wstring targetW;
        std::wstring remapNameFromW;
        std::wstring remapNameToW;
        const wchar_t* liveSource = nullptr;
        const wchar_t* liveTarget = nullptr;
        const wchar_t* liveRemapFrom = nullptr;
        const wchar_t* liveRemapTo = nullptr;
        if (!WidenOperationsOptional(source, sourceW, liveSource) ||
            !WidenOperationsOptional(target, targetW, liveTarget) ||
            !WidenOperationsOptional(remapNameFrom, remapNameFromW,
                                     liveRemapFrom) ||
            !WidenOperationsOptional(remapNameTo, remapNameToW, liveRemapTo))
            return FALSE;
        return WideOperations.MoveFiles(liveSource, liveTarget, liveRemapFrom,
                                        liveRemapTo);
    }

} // namespace sally::compat
