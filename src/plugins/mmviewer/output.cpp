// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"

#include "output.h"
#include "renderer.h"
#include "mmviewer.h"
#include "mmviewer_text_codec.h"

// Wide sibling for item NAMES; the byte one still serves item VALUES.
static wchar_t* DuplicateWideString(const wchar_t* text)
{
    if (text == NULL)
        return NULL;
    const size_t chars = wcslen(text) + 1;
    if (chars > static_cast<size_t>(INT_MAX) / sizeof(wchar_t))
        return NULL;
    wchar_t* ret = (wchar_t*)SalGeneral->Alloc((int)(chars * sizeof(wchar_t)));
    if (ret != NULL)
        memcpy(ret, text, chars * sizeof(wchar_t));
    return ret;
}

static char* DuplicateByteString(const char* text)
{
    if (text == NULL)
        return NULL;

    const size_t textLen = strlen(text);
    if (textLen >= INT_MAX)
        return NULL;

    const int allocationSize = (int)textLen + 1;
    char* copy = (char*)SalGeneral->Alloc(allocationSize);
    if (copy != NULL)
        memcpy(copy, text, allocationSize);
    return copy;
}

static wchar_t* DecodeByteString(const char* text, UINT codePage, bool permissive = false)
{
    if (text == NULL)
        return NULL;

    std::wstring decoded;
    const bool ok = permissive
                        ? mmviewer::DecodeMetadataTextPermissive(codePage, text, strlen(text), decoded)
                        : mmviewer::DecodeMetadataText(codePage, text, strlen(text), decoded);
    if (!ok)
        return NULL;
    if (decoded.size() >= static_cast<size_t>(INT_MAX) / sizeof(wchar_t))
        return NULL;
    const size_t bytes = (decoded.size() + 1) * sizeof(wchar_t);
    wchar_t* copy = static_cast<wchar_t*>(SalGeneral->Alloc(static_cast<int>(bytes)));
    if (copy != NULL)
        memcpy(copy, decoded.c_str(), bytes);
    return copy;
}

COutput::COutput() : Items(10, 5)
{
}

COutput::~COutput()
{
    DestroyItems();
}

int COutput::GetCount()
{
    return Items.Count;
}

const COutputItem*
COutput::GetItem(int i)
{
    return &Items[i];
}

void COutput::DestroyItems()
{
    int i;
    for (i = 0; i < Items.Count; i++)
    {
        COutputItem* item = &Items[i];

        // the parent window deletes all windows during destruction, so this is unnecessary
        if (IsWindow(item->hwnd))
            DestroyWindow(item->hwnd);

        if ((item->Flags & OIF_SEPARATOR) == 0)
        {
            if (item->Name)
                SalGeneral->Free(item->Name);
            if (item->Value)
                SalGeneral->Free(item->Value);
            if (item->DisplayValue)
                SalGeneral->Free(item->DisplayValue);
        }
    }
    Items.DetachMembers();
}

BOOL COutput::AddItem(const wchar_t* name, const char* value)
{
    COutputItem item = {};
    item.Flags = 0;
    item.hwnd = NULL;
    item.Name = DuplicateWideString(name);
    item.Value = DuplicateByteString(value);
    if (item.Name != NULL && item.Value != NULL)
    {
        if (IsUTF8Text(item.Value))
        {
            item.Flags |= OIF_UTF8;
            item.DisplayValue = DecodeByteString(item.Value, CP_UTF8);
        }
        if (item.DisplayValue == NULL)
        {
            item.Flags &= ~OIF_UTF8;
            // Last resort: decode permissively rather than drop the row.
            item.DisplayValue = DecodeByteString(item.Value, CP_ACP, /*permissive*/ true);
        }
    }
    if (item.Name != NULL && item.Value != NULL && item.DisplayValue != NULL)
    {
        Items.Add(item);
        if (Items.IsGood())
            return TRUE;
        Items.ResetState();
    }
    if (item.Name != NULL)
        SalGeneral->Free(item.Name);
    if (item.Value != NULL)
        SalGeneral->Free(item.Value);
    if (item.DisplayValue != NULL)
        SalGeneral->Free(item.DisplayValue);
    return FALSE;
}

BOOL COutput::AddItem(const wchar_t* name, const wchar_t* value)
{
    if (name == NULL || value == NULL)
        return FALSE;

    std::string utf8Value;
    if (!mmviewer::EncodeUtf16TagAsUtf8(value, utf8Value))
        return FALSE;

    COutputItem item = {};
    item.Flags = OIF_UTF8;
    item.Name = DuplicateWideString(name);
    item.Value = DuplicateByteString(utf8Value.c_str());
    item.DisplayValue = DuplicateWideString(value);
    if (item.Name != NULL && item.Value != NULL && item.DisplayValue != NULL)
    {
        Items.Add(item);
        if (Items.IsGood())
            return TRUE;
        Items.ResetState();
    }
    if (item.Name != NULL)
        SalGeneral->Free(item.Name);
    if (item.Value != NULL)
        SalGeneral->Free(item.Value);
    if (item.DisplayValue != NULL)
        SalGeneral->Free(item.DisplayValue);
    return FALSE;
}

BOOL COutput::AddSeparator()
{
    COutputItem item = {};
    item.Flags = OIF_SEPARATOR;
    item.Name = NULL;
    item.Value = NULL;
    item.DisplayValue = NULL;
    item.hwnd = NULL;
    Items.Add(item);
    if (Items.IsGood())
        return TRUE;
    Items.ResetState();
    return FALSE;
}

BOOL COutput::AddHeader(const wchar_t* name, BOOL superHeader)
{
    COutputItem item = {};
    item.Flags = OIF_HEADER;
    if (superHeader)
        item.Flags |= OIF_EMPHASIZE;
    item.Name = DuplicateWideString(name);
    item.Value = NULL;
    item.DisplayValue = NULL;
    item.hwnd = NULL;
    Items.Add(item);
    if (Items.IsGood())
        return TRUE;
    else
        Items.ResetState();
    return FALSE;
}

LRESULT APIENTRY EditSubclassProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
    case WM_SYSKEYDOWN:
    case WM_KEYDOWN:
    {
        switch (wParam)
        {
        case VK_TAB:
        {
            HWND h = GetWindow(hwnd, KEY_DOWN(VK_SHIFT) ? GW_HWNDPREV : GW_HWNDNEXT);
            if (!h)
            {
                TRACE_I("EditSubclassProc: VK_TAB (parent): " << h);
                h = GetParent(hwnd);
                SetFocus(h);
            }
            else
            {
                TRACE_I("EditSubclassProc: VK_TAB (next/prev): " << h);
                SetFocus(h);
                SendMessageW(h, EM_SETSEL, 0, -1);
                SendMessageW(h, WM_ENSUREVISIBLE, 0, 0);
            }
        }
        break;
        }
    }
    break;

    case WM_MOUSEWHEEL:
    {
        HWND HWindow = GetParent(hwnd);
        SendMessageW(HWindow, uMsg, wParam, lParam);
    }
        return 0;

    case WM_ENSUREVISIBLE:
    {
        TRACE_I("EditSubclassProc: WM_ENSUREVISIBLE: ");
        HWND HWindow = GetParent(hwnd);
        RECT r;
        GetClientRect(hwnd, &r);
        MapWindowPoints(hwnd, HWindow, (LPPOINT)&r, (sizeof(RECT) / sizeof(POINT)));
        SendMessageW(HWindow, WM_ENSUREVISIBLE, 0, (LPARAM)&r);
    }
        return 0;
    }

    return CallWindowProcW((WNDPROC)GetWindowLongPtr(hwnd, GWLP_USERDATA), hwnd, uMsg, wParam, lParam);
}

BOOL COutput::PrepareForRender(HWND parentWnd)
{
    BOOL r = TRUE;
    int i;
    for (i = 0; i < Items.Count; i++)
    {
        COutputItem& oi = Items[i];

        if ((oi.Flags & ~OIF_UTF8) == 0)
        { // Ignore non-text items
            oi.hwnd = CreateWindowW(L"EDIT", oi.DisplayValue,
                                    WS_TABSTOP | WS_VISIBLE | WS_CHILD | ES_READONLY | ES_LEFT | ES_AUTOVSCROLL | ES_MULTILINE,
                                    0, 0, 4, 4, parentWnd, NULL, DLLInstance, NULL);

            if (oi.hwnd)
            {
                // store the original window proc pointer in GWL_USERDATA
                SetWindowLongPtr(oi.hwnd, GWLP_USERDATA, SetWindowLongPtr(oi.hwnd, GWLP_WNDPROC, (LONG_PTR)EditSubclassProc));

                SendMessageW(oi.hwnd, WM_SETFONT, (WPARAM)HBoldFont, 0);
                SendMessageW(oi.hwnd, EM_SETMARGINS, EC_LEFTMARGIN, 5);
                SendMessageW(oi.hwnd, EM_SETMARGINS, EC_RIGHTMARGIN, 5);
            }
            else
                r = FALSE;
        }
    }

    return r;
}
