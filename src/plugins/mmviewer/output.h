// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "number_text.h"

//****************************************************************************
//
// COutputInterface
//
// Because the output interface needs to be passed to parser objects, I choose
// this representation with an abstract interface in case the parsers
// live in an external DLL library.
//

std::wstring FStrW(const wchar_t* format, ...);

class COutputInterface
{
public:
    // 'name' is a localized LABEL and is wide. 'value' stays bytes: it is
    // lifted straight out of the media file, and this plugin sniffs its encoding per item
    // (IsUTF8Text -> OIF_UTF8) and honours that at render time. Widening it would mean
    // deciding the tag's encoding here instead, which is a decoding feature, not a widening.
    virtual BOOL AddItem(const wchar_t* name, const char* value) = 0;
    virtual BOOL AddItem(const wchar_t* name, const wchar_t* value) = 0;
    virtual BOOL AddSeparator() = 0;
    virtual BOOL AddHeader(const wchar_t* name, BOOL superHeader = FALSE) = 0;

    virtual BOOL PrepareForRender(HWND parentWnd) = 0;
};

//****************************************************************************
//
// COutputInterface
//

#define OIF_SEPARATOR 0x00000001 // empty item
#define OIF_HEADER 0x00000002    // header
#define OIF_EMPHASIZE 0x00000004 // emphasize the property (for now only for OIF_HEADER)
#define OIF_UTF8 0x00000008      // The Value is encoded in UTF-8

struct COutputItem
{
    DWORD Flags;
    wchar_t* Name; // localized label - wide
    char* Value; // original media/parser bytes retained for encoded exports
    wchar_t* DisplayValue; // decoded once for all live UI/clipboard consumers
    HWND hwnd; //edit box
};

class COutput : public COutputInterface
{
private:
    TDirectArray<COutputItem> Items;

public:
    COutput();
    ~COutput();

    // returns the number of held items
    int GetCount();

    // returns an item
    const COutputItem* GetItem(int i);

    // releases all held items, leaving it in an empty state
    void DestroyItems();

    // methods from COutputInterface
    virtual BOOL AddItem(const wchar_t* name, const char* value);
    virtual BOOL AddItem(const wchar_t* name, const wchar_t* value);
    virtual BOOL AddHeader(const wchar_t* name, BOOL superHeader = FALSE);
    virtual BOOL AddSeparator();

    virtual BOOL PrepareForRender(HWND parentWnd);
};
