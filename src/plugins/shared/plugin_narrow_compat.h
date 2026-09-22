// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

// Transitional narrow accessors for plugins during the v108 break.
//
// WHAT THIS IS FOR. v108 makes the SDK's text APIs wide: CSalamanderGeneral's
// LoadStr now returns WCHAR* and the old LoadStrW is gone. The 33 in-tree plugins
// hold roughly 3500 narrow uses of their own LoadStr(int) wrappers. Propagating
// wide through all of them is done per plugin, deliberately, with its
// resources and dialogs. The goal here is only to keep them COMPILING and BEHAVING
// EXACTLY AS BEFORE while the ABI moves underneath.
//
// So a plugin's local wrapper keeps its narrow signature and only changes which
// function it calls, from the SDK method to LoadStrNarrow below. Nothing else in
// that plugin changes.
//
// THIS IS NOT PART OF THE ABI. It is a header-only helper compiled into each
// plugin, not a vtable entry — deliberately, so the SDK's narrow surface actually
// shrinks rather than being renamed. gtest_sdk_narrow_surface counts the live
// spl_*.h headers only, and this file is not one of them.
//
// IT IS ALSO TRANSITIONAL, AND THAT IS THE POINT OF ITS NAME. Exact ACP
// projection cannot represent every translated string; the adapter refuses those
// values instead of silently substituting characters. Every use remains task-24 debt.
//
// DELETE THIS FILE when the last plugin goes wide. If it still exists after task
// 24, that task is not finished.

#include <windows.h>
#include <tchar.h> // _T() - not every plugin's precomp chain pulls this in on its own

#include <climits>
#include <cstring>
#include <list>
#include <new>
#include <string>
#include <utility>

#include "plugin_text_encoding.h"
#include "plugin_window_text.h"

// NO #include of the SDK headers here, deliberately. spl_gen.h depends on
// spl_com.h/spl_base.h having been included first, and including it from here
// created a cycle: spl_gen.h mentions this file in a comment, an automated
// include pass added the include at the TOP of spl_gen.h, and the class was then
// referenced ~800 lines before its own definition.
//
// This header must therefore be included AFTER the SDK headers. Every plugin
// already includes them (usually via its precomp.h), so that is where the include
// belongs.

// Widen a plugin's narrow string for an SDK call that has gone wide.
//
// THIS DIRECTION IS LOSSLESS, unlike LoadStrNarrow below. Any byte sequence the
// plugin holds came from CP_ACP in the first place, so converting it back to
// UTF-16 recovers exactly what it meant — nothing can be lost on the way in.
// That is why the pure-INPUT tier of the v108 widening is the safe one to do
// first, and why it needs no per-method wrapper: the argument is converted at
// the call site and the SDK sees the real string.
//
// Returns by value; take .c_str() at the call site. The temporary lives to the
// end of the full expression, which is exactly as long as the callee needs it.
inline bool LegacyTextToWide(const char* narrow, std::wstring& output)
{
    if (narrow == NULL)
    {
        std::wstring staged;
        output.swap(staged);
        return true;
    }
    return sally::plugin_text::DecodeAcp(narrow, output);
}

inline std::wstring ToWideArg(const char* narrow)
{
    std::wstring output;
    LegacyTextToWide(narrow, output);
    return output;
}

// Same, for a caller that holds an explicit BYTE COUNT rather than a
// NUL-terminated string.
//
// THIS OVERLOAD EXISTS TO PREVENT A SILENT CORRUPTION. Some SDK calls take a
// (text, length) pair — CopyTextToClipboard among them. Wrapping only the
// pointer with the single-argument ToWideArg above would convert the WHOLE
// NUL-terminated string, while the length beside it still described the caller's
// intended SUBSTRING. On top of that the two counts need not agree at all once
// the string is wide: a DBCS byte count is not a WCHAR count.
//
// So the correct call-site shape is
//
//     Foo(ToWideArg(text, textLen).c_str(), -1, ...)
//
// converting exactly the requested bytes and letting the callee take the whole
// (NUL-terminated) result. Passing the original length alongside would be wrong
// in both directions.
inline std::wstring ToWideArg(const char* narrow, int narrowLen)
{
    if (narrow == NULL)
        return std::wstring();
    if (narrowLen < 0)
        return ToWideArg(narrow);
    if (narrowLen == 0)
        return std::wstring();
    std::wstring out;
    sally::plugin_text::DecodeAcp(narrow, static_cast<size_t>(narrowLen), out);
    return out;
}

// Exact dynamic projection at a still-narrow in-tree plugin boundary. Semantic
// authority remains with the UTF-16 caller; failure means the legacy engine
// cannot represent the value and must not receive a substituted path.
inline bool WideToLegacyTextExact(const wchar_t* wide, std::string& output)
{
    if (wide == NULL)
    {
        std::string staged;
        output.swap(staged);
        return true;
    }
    return sally::plugin_text::EncodeAcpExact(wide, output);
}

// Transactional publication into a frozen or explicitly encoded byte owner.
// A failed conversion or an undersized destination leaves the caller's bytes
// untouched, so a dialog cannot publish a truncated or substituted value.
inline bool CopyWideToLegacyTextExact(const wchar_t* wide, char* output, size_t outputSize)
{
    if (output == NULL || outputSize == 0)
        return false;
    std::string staged;
    if (!WideToLegacyTextExact(wide, staged) || staged.size() + 1 > outputSize)
        return false;
    std::memcpy(output, staged.c_str(), staged.size() + 1);
    return true;
}

inline bool SetWindowLegacyText(HWND window, const char* text)
{
    try
    {
        std::wstring wide;
        return LegacyTextToWide(text, wide) && SetWindowTextW(window, wide.c_str()) != FALSE;
    }
    catch (...)
    {
        return false;
    }
}

inline bool SetDlgItemLegacyText(HWND dialog, int item, const char* text)
{
    return SetWindowLegacyText(GetDlgItem(dialog, item), text);
}

inline int ReadWindowLegacyTextExact(HWND window, char* output, size_t outputSize)
{
    try
    {
        std::wstring wide;
        std::string encoded;
        if (!ReadWindowTextOwnedW(window, wide) ||
            !WideToLegacyTextExact(wide.c_str(), encoded) ||
            output == NULL || encoded.size() + 1 > outputSize ||
            encoded.size() > static_cast<size_t>(INT_MAX))
            return 0;
        std::memcpy(output, encoded.c_str(), encoded.size() + 1);
        return static_cast<int>(encoded.size());
    }
    catch (...)
    {
        return 0;
    }
}

inline int ReadDlgItemLegacyTextExact(HWND dialog, int item, char* output, size_t outputSize)
{
    return ReadWindowLegacyTextExact(GetDlgItem(dialog, item), output, outputSize);
}

inline bool ReadWindowLegacyTextExact(HWND window, std::string& output)
{
    try
    {
        std::wstring wide;
        return ReadWindowTextOwnedW(window, wide) &&
               WideToLegacyTextExact(wide.c_str(), output);
    }
    catch (...)
    {
        return false;
    }
}

inline bool ReadDlgItemLegacyTextExact(HWND dialog, int item, std::string& output)
{
    return ReadWindowLegacyTextExact(GetDlgItem(dialog, item), output);
}

// Temporary rendering projection for legacy plugin controls that still accept char text. This
// must never be used as path or protocol authority; callers keep and consume the UTF-16 owner.
inline BOOL WideToLegacyDisplay(const wchar_t* wide, char* output, int outputSize)
{
    if (output == NULL || outputSize <= 0)
        return FALSE;
    output[0] = 0;
    if (wide == NULL)
        return TRUE;
    // [narrow-ok: display] Transitional rendering only; the authoritative value stays UTF-16.
    std::string staged;
    if (!sally::plugin_text::EncodeAcpLossy(wide, staged) ||
        staged.size() + 1 > static_cast<size_t>(outputSize))
    {
        return FALSE;
    }
    std::memcpy(output, staged.c_str(), staged.size() + 1);
    return TRUE;
}

// Narrow rendering of a plugin resource string.
//
// The v107 return type is frozen, but its storage is not required to be fixed.
// Each thread retains dynamically sized projections so returned pointers remain
// stable without a 10,000-byte ceiling on any ONE string, and retires the oldest
// once the retained total passes the budget the frozen buffer had.
inline char* LoadStrNarrow(CSalamanderGeneralAbstract* general, HINSTANCE module, int resID)
{
    static char failed[] = "ERROR LOADING STRING";
    if (general == NULL)
        return failed;

    // Plain virtual dispatch. It must NOT be written as
    // general->CSalamanderGeneralAbstract::LoadStr(...) - qualifying the call
    // suppresses virtual dispatch and asks the linker for a body that a pure
    // virtual does not have, which fails in every plugin at link time.
    CSalamanderStringBufferOwner owner;
    std::wstring wide;
    if (!owner.IsValid() || !general->LoadStr(module, resID, owner.Buffer()) ||
        !owner.GetValue(wide))
        return failed;

    std::string converted;
    if (!sally::plugin_text::EncodeAcpExact(wide.c_str(), converted))
        return failed;

    // Retain the projection so the returned pointer outlives this call, then TRIM so the retention
    // stays BOUNDED. The frozen contract promises exactly that shape - spl_gen.h on LoadStr:
    // "returns text in internal buffer ... buffer is 10000 characters large, overwrite risk only
    // after it's filled (used cyclically); if you need to use the text later, we recommend copying
    // it to a local buffer". A list that only ever grows keeps the stability half of that promise
    // and silently drops the bounded half: a plugin calling LoadStr once per file - which is how
    // error and progress text is ordinarily built - accumulates every string it has ever loaded for
    // the life of the thread, where the fixed buffer reused the same 10 KB forever.
    //
    // std::list is what makes the trim safe: erasing at the front never relocates an element that is
    // still retained, so every pointer still inside the budget stays valid.
    static thread_local std::list<std::string> values;
    static thread_local size_t retainedBytes = 0;
    retainedBytes += converted.size() + 1;
    values.emplace_back(std::move(converted));

    // Same budget, and therefore the same guarantee a caller could already rely on: a returned
    // pointer survives until roughly 10000 characters' worth of later strings have been loaded on
    // this thread. The floor keeps one expression that passes several LoadStr results as arguments
    // safe even when the individual strings are enormous - the one case the fixed buffer could not
    // survive and this can.
    const size_t retentionBudget = 10000;
    const size_t minRetained = 16;
    while (retainedBytes > retentionBudget && values.size() > minRetained)
    {
        retainedBytes -= values.front().size() + 1;
        values.pop_front();
    }
    return values.back().data();
}
