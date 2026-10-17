// SPDX-FileCopyrightText: 2025-2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <windows.h>
#include <objidl.h>

struct FolderDropLabels
{
    const wchar_t* move;
    const wchar_t* copy;
    const wchar_t* link;
    const wchar_t* cancel;
};

enum class FolderDropChoice { Cancel, Move, Copy, Link, Extension };
struct FolderDropAction
{
    DWORD effect;
    DWORD keys;
};
inline FolderDropAction NativeFolderDropAction(FolderDropChoice choice)
{
    switch (choice)
    {
    case FolderDropChoice::Move: return {DROPEFFECT_MOVE, MK_SHIFT};
    case FolderDropChoice::Copy: return {DROPEFFECT_COPY, MK_CONTROL};
    case FolderDropChoice::Link: return {DROPEFFECT_LINK, MK_SHIFT | MK_CONTROL};
    default: return {DROPEFFECT_NONE, 0};
    }
}
inline bool FolderDropCommandInRange(UINT command, UINT first, UINT count)
{
    return command >= first && command - first < count;
}
struct FolderDropResult
{
    FolderDropChoice choice = FolderDropChoice::Cancel;
    HRESULT result = S_OK;
};

FolderDropResult ShowFolderDropMenu(HWND owner, const wchar_t* target, IDataObject* data,
                                  DWORD effects, DWORD defaultEffect, const FolderDropLabels& labels);
