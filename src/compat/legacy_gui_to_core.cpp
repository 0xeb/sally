// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

// legacy_gui_to_core — frozen v107 GUI graph over the live wide GUI.
//
// Never include precomp.h here. Standard-library headers must be visible at
// global scope before sdk107.h enters its frozen namespace wrapper.

#define NOMINMAX
#include <windows.h>

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <cwchar>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "compat/legacy_to_core.h"

#include "compat/legacy_convert.h"

namespace sally::compat
{
namespace
{

bool WidenGUIOptional(const char* value, std::wstring& storage,
                      const wchar_t*& wide)
{
    storage.clear();
    wide = nullptr;
    if (value == nullptr)
        return true;
    if (!WidenPluginText(value, storage))
        return false;
    wide = storage.c_str();
    return true;
}

bool WidenGUICounted(const char* value, int count, std::wstring& wide)
{
    wide.clear();
    if (value == nullptr)
    {
        if (count > 0)
        {
            SetLastError(ERROR_INVALID_PARAMETER);
            return false;
        }
        return true;
    }
    if (count < 0)
        return WidenPluginText(value, wide);
    return WidenPluginSpan(value, count, wide);
}

template <typename T>
bool AssignGUIBuffer(std::vector<T>& buffer, std::size_t count)
{
    try
    {
        buffer.assign(count, T{});
        return true;
    }
    catch (const std::bad_alloc&)
    {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return false;
    }
    catch (const std::length_error&)
    {
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return false;
    }
}

bool WidenGUIBuffer(const std::vector<char>& buffer, std::wstring& wide)
{
    if (buffer.empty())
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return false;
    }
    const void* terminator =
        std::memchr(buffer.data(), '\0', buffer.size());
    if (terminator == nullptr)
    {
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return false;
    }
    const std::size_t length =
        static_cast<const char*>(terminator) - buffer.data();
    if (length > static_cast<std::size_t>(INT_MAX))
    {
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return false;
    }
    return WidenPluginSpan(buffer.data(), static_cast<int>(length), wide);
}

bool NarrowGUIText(const wchar_t* value, NarrowResult& narrowed)
{
    if (value == nullptr || value[0] == L'\0')
    {
        narrowed = {};
        narrowed.ok = true;
        SetLastError(ERROR_SUCCESS);
        return true;
    }
    try
    {
        narrowed = NarrowExact(std::wstring(value != nullptr ? value : L""));
        return narrowed.ok;
    }
    catch (const std::bad_alloc&)
    {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return false;
    }
    catch (const std::length_error&)
    {
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return false;
    }
}

bool CopyExactGUIText(const wchar_t* value, char* output, std::size_t capacity,
                      std::size_t* required = nullptr)
{
    NarrowResult narrowed;
    if (!NarrowGUIText(value, narrowed))
    {
        if (output != nullptr && capacity > 0)
            output[0] = '\0';
        if (required != nullptr)
            *required = 0;
        return false;
    }

    if (required != nullptr)
        *required = narrowed.value.size();
    if (output == nullptr)
        return true;
    if (capacity <= narrowed.value.size())
    {
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return false;
    }
    if (!narrowed.value.empty())
        std::memcpy(output, narrowed.value.data(), narrowed.value.size());
    output[narrowed.value.size()] = '\0';
    return true;
}

// Query-style copy that mirrors lstrcpyn: a caller buffer too small for the whole caption
// gets a truncated, terminated caption rather than an error. '*required' always reports the
// untruncated length so a caller that passes NULL can size its buffer.
bool CopyTruncatedGUIText(const wchar_t* value, char* output, std::size_t capacity,
                          std::size_t* required)
{
    NarrowResult narrowed;
    if (!NarrowGUIText(value, narrowed))
    {
        if (output != nullptr && capacity > 0)
            output[0] = '\0';
        if (required != nullptr)
            *required = 0;
        return false;
    }

    if (required != nullptr)
        *required = narrowed.value.size();
    if (output == nullptr)
        return true;
    if (capacity == 0)
        return true;
    const std::size_t copied = (std::min)(narrowed.value.size(), capacity - 1);
    if (copied != 0)
        std::memcpy(output, narrowed.value.data(), copied);
    output[copied] = '\0';
    return true;
}

bool CanCopyWideGUIText(const std::wstring& value, wchar_t* output,
                        int capacity)
{
    if (output == nullptr)
        return true;
    if (capacity <= 0 || value.size() >= static_cast<std::size_t>(capacity))
    {
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return false;
    }
    return true;
}

void CopyWideGUIText(const std::wstring& value, wchar_t* output)
{
    if (output == nullptr)
        return;
    if (!value.empty())
        std::wmemcpy(output, value.data(), value.size());
    output[value.size()] = L'\0';
}

::CQuadWord GUIQuadWordToLive(const sdk107::CQuadWord& value)
{
    ::CQuadWord result;
    result.LoDWord = value.LoDWord;
    result.HiDWord = value.HiDWord;
    return result;
}

static_assert(sizeof(sdk107::MENU_TEMPLATE_ITEM) == sizeof(::MENU_TEMPLATE_ITEM));
static_assert(alignof(sdk107::MENU_TEMPLATE_ITEM) == alignof(::MENU_TEMPLATE_ITEM));
static_assert(offsetof(sdk107::MENU_TEMPLATE_ITEM, RowType) == offsetof(::MENU_TEMPLATE_ITEM, RowType));
static_assert(offsetof(sdk107::MENU_TEMPLATE_ITEM, TextResID) == offsetof(::MENU_TEMPLATE_ITEM, TextResID));
static_assert(offsetof(sdk107::MENU_TEMPLATE_ITEM, SkillLevel) == offsetof(::MENU_TEMPLATE_ITEM, SkillLevel));
static_assert(offsetof(sdk107::MENU_TEMPLATE_ITEM, ID) == offsetof(::MENU_TEMPLATE_ITEM, ID));
static_assert(offsetof(sdk107::MENU_TEMPLATE_ITEM, ImageIndex) == offsetof(::MENU_TEMPLATE_ITEM, ImageIndex));
static_assert(offsetof(sdk107::MENU_TEMPLATE_ITEM, State) == offsetof(::MENU_TEMPLATE_ITEM, State));
static_assert(offsetof(sdk107::MENU_TEMPLATE_ITEM, Enabler) == offsetof(::MENU_TEMPLATE_ITEM, Enabler));

class ILegacyGUIState
{
public:
    virtual ~ILegacyGUIState() = default;
    virtual sdk107::CGUIMenuPopupAbstract* WrapMenu(::CGUIMenuPopupAbstract* menu) = 0;
    virtual ::CGUIMenuPopupAbstract* UnwrapMenu(sdk107::CGUIMenuPopupAbstract* menu) const = 0;
    virtual ::CGUIIconListAbstract* UnwrapIconList(sdk107::CGUIIconListAbstract* iconList) const = 0;
    virtual HWND ProxyFor(HWND target) = 0;
};

class CLegacyGUIProgressBar final : public sdk107::CGUIProgressBarAbstract
{
public:
    CLegacyGUIProgressBar(ILegacyGUIState&, ::CGUIProgressBarAbstract* wide) : Wide(wide) {}

    void WINAPI SetProgress(DWORD progress, const char* text) override
    {
        std::wstring textW;
        const wchar_t* liveText = nullptr;
        if (WidenGUIOptional(text, textW, liveText))
            Wide->SetProgress(progress, liveText);
    }
    void WINAPI SetSelfMoveTime(DWORD time) override { Wide->SetSelfMoveTime(time); }
    void WINAPI SetSelfMoveSpeed(DWORD moveTime) override { Wide->SetSelfMoveSpeed(moveTime); }
    void WINAPI Stop() override { Wide->Stop(); }
    void WINAPI SetProgress2(const sdk107::CQuadWord& current,
                             const sdk107::CQuadWord& total,
                             const char* text) override
    {
        const ::CQuadWord currentW = GUIQuadWordToLive(current);
        const ::CQuadWord totalW = GUIQuadWordToLive(total);
        std::wstring textW;
        const wchar_t* liveText = nullptr;
        if (WidenGUIOptional(text, textW, liveText))
            Wide->SetProgress2(currentW, totalW, liveText);
    }

private:
    ::CGUIProgressBarAbstract* Wide;
};

class CLegacyGUIStaticText final : public sdk107::CGUIStaticTextAbstract
{
public:
    CLegacyGUIStaticText(ILegacyGUIState& state, ::CGUIStaticTextAbstract* wide)
        : State(state), Wide(wide) {}

    BOOL WINAPI SetText(const char* text) override
    {
        std::wstring textW;
        const wchar_t* liveText = nullptr;
        return WidenGUIOptional(text, textW, liveText)
                   ? Wide->SetText(liveText)
                   : FALSE;
    }
    const char* WINAPI GetText() override
    {
        const wchar_t* text = Wide->GetText();
        if (text == nullptr)
        {
            Text.clear();
            return nullptr;
        }
        NarrowResult narrowed;
        if (!NarrowGUIText(text, narrowed))
        {
            Text.clear();
            return nullptr;
        }
        Text.swap(narrowed.value);
        return Text.c_str();
    }
    void WINAPI SetPathSeparator(char separator) override
    {
        char value[2] = {separator, '\0'};
        std::wstring separatorW;
        if (WidenPluginText(value, separatorW) && !separatorW.empty())
            Wide->SetPathSeparator(separatorW[0]);
    }
    BOOL WINAPI SetToolTipText(const char* text) override
    {
        std::wstring textW;
        const wchar_t* liveText = nullptr;
        return WidenGUIOptional(text, textW, liveText)
                   ? Wide->SetToolTipText(liveText)
                   : FALSE;
    }
    void WINAPI SetToolTip(HWND hNotifyWindow, DWORD id) override
    {
        Wide->SetToolTip(State.ProxyFor(hNotifyWindow), id);
    }

private:
    ILegacyGUIState& State;
    ::CGUIStaticTextAbstract* Wide;
    std::string Text;
};

class CLegacyGUIHyperLink final : public sdk107::CGUIHyperLinkAbstract
{
public:
    CLegacyGUIHyperLink(ILegacyGUIState& state, ::CGUIHyperLinkAbstract* wide)
        : State(state), Wide(wide) {}

    BOOL WINAPI SetText(const char* text) override
    {
        std::wstring textW;
        const wchar_t* liveText = nullptr;
        return WidenGUIOptional(text, textW, liveText)
                   ? Wide->SetText(liveText)
                   : FALSE;
    }
    const char* WINAPI GetText() override
    {
        const wchar_t* text = Wide->GetText();
        if (text == nullptr)
        {
            Text.clear();
            return nullptr;
        }
        NarrowResult narrowed;
        if (!NarrowGUIText(text, narrowed))
        {
            Text.clear();
            return nullptr;
        }
        Text.swap(narrowed.value);
        return Text.c_str();
    }
    void WINAPI SetActionOpen(const char* file) override
    {
        std::wstring fileW;
        const wchar_t* liveFile = nullptr;
        if (WidenGUIOptional(file, fileW, liveFile))
            Wide->SetActionOpen(liveFile);
    }
    void WINAPI SetActionPostCommand(WORD command) override { Wide->SetActionPostCommand(command); }
    BOOL WINAPI SetActionShowHint(const char* text) override
    {
        std::wstring textW;
        const wchar_t* liveText = nullptr;
        return WidenGUIOptional(text, textW, liveText)
                   ? Wide->SetActionShowHint(liveText)
                   : FALSE;
    }
    BOOL WINAPI SetToolTipText(const char* text) override
    {
        std::wstring textW;
        const wchar_t* liveText = nullptr;
        return WidenGUIOptional(text, textW, liveText)
                   ? Wide->SetToolTipText(liveText)
                   : FALSE;
    }
    void WINAPI SetToolTip(HWND hNotifyWindow, DWORD id) override
    {
        Wide->SetToolTip(State.ProxyFor(hNotifyWindow), id);
    }

private:
    ILegacyGUIState& State;
    ::CGUIHyperLinkAbstract* Wide;
    std::string Text;
};

class CLegacyGUIButton final : public sdk107::CGUIButtonAbstract
{
public:
    CLegacyGUIButton(ILegacyGUIState& state, ::CGUIButtonAbstract* wide)
        : State(state), Wide(wide) {}
    BOOL WINAPI SetToolTipText(const char* text) override
    {
        std::wstring textW;
        const wchar_t* liveText = nullptr;
        return WidenGUIOptional(text, textW, liveText)
                   ? Wide->SetToolTipText(liveText)
                   : FALSE;
    }
    void WINAPI SetToolTip(HWND hNotifyWindow, DWORD id) override
    {
        Wide->SetToolTip(State.ProxyFor(hNotifyWindow), id);
    }

private:
    ILegacyGUIState& State;
    ::CGUIButtonAbstract* Wide;
};

class CLegacyGUIColorArrowButton final : public sdk107::CGUIColorArrowButtonAbstract
{
public:
    CLegacyGUIColorArrowButton(ILegacyGUIState&, ::CGUIColorArrowButtonAbstract* wide) : Wide(wide) {}
    void WINAPI SetColor(COLORREF textColor, COLORREF bkgndColor) override { Wide->SetColor(textColor, bkgndColor); }
    void WINAPI SetTextColor(COLORREF textColor) override { Wide->SetTextColor(textColor); }
    void WINAPI SetBkgndColor(COLORREF bkgndColor) override { Wide->SetBkgndColor(bkgndColor); }
    COLORREF WINAPI GetTextColor() override { return Wide->GetTextColor(); }
    COLORREF WINAPI GetBkgndColor() override { return Wide->GetBkgndColor(); }

private:
    ::CGUIColorArrowButtonAbstract* Wide;
};

bool CopyMenuInput(const sdk107::MENU_ITEM_INFO& legacy, ::MENU_ITEM_INFO& wide,
                   ILegacyGUIState& state, std::wstring& stringW)
{
    wide.Mask = legacy.Mask;
    if (legacy.Mask & MENU_MASK_TYPE) wide.Type = legacy.Type;
    if (legacy.Mask & MENU_MASK_STATE) wide.State = legacy.State;
    if (legacy.Mask & MENU_MASK_ID) wide.ID = legacy.ID;
    if (legacy.Mask & MENU_MASK_SUBMENU) wide.SubMenu = state.UnwrapMenu(legacy.SubMenu);
    if (legacy.Mask & MENU_MASK_CHECKMARKS)
    {
        wide.HBmpChecked = legacy.HBmpChecked;
        wide.HBmpUnchecked = legacy.HBmpUnchecked;
    }
    if (legacy.Mask & MENU_MASK_BITMAP) wide.HBmpItem = legacy.HBmpItem;
    if (legacy.Mask & MENU_MASK_STRING)
    {
        const wchar_t* liveString = nullptr;
        if (!WidenGUIOptional(legacy.String, stringW, liveString))
            return false;
        wide.String = const_cast<wchar_t*>(liveString);
        wide.StringLen = static_cast<DWORD>(stringW.size());
    }
    if (legacy.Mask & MENU_MASK_IMAGEINDEX) wide.ImageIndex = legacy.ImageIndex;
    if (legacy.Mask & MENU_MASK_ICON) wide.HIcon = legacy.HIcon;
    if (legacy.Mask & MENU_MASK_OVERLAY) wide.HOverlay = legacy.HOverlay;
    if (legacy.Mask & MENU_MASK_CUSTOMDATA) wide.CustomData = legacy.CustomData;
    if (legacy.Mask & MENU_MASK_SKILLLEVEL) wide.SkillLevel = legacy.SkillLevel;
    if (legacy.Mask & MENU_MASK_ENABLER) wide.Enabler = legacy.Enabler;
    if (legacy.Mask & MENU_MASK_FLAGS) wide.Flags = legacy.Flags;
    return true;
}

class CLegacyGUIMenuPopup final : public sdk107::CGUIMenuPopupAbstract
{
public:
    CLegacyGUIMenuPopup(ILegacyGUIState& state, ::CGUIMenuPopupAbstract* wide)
        : State(state), Wide(wide) {}

    BOOL WINAPI LoadFromTemplate(HINSTANCE hInstance,
                                 const sdk107::MENU_TEMPLATE_ITEM* menuTemplate,
                                 DWORD* enablersOffset, HIMAGELIST hImageList,
                                 HIMAGELIST hHotImageList) override
    {
        return Wide->LoadFromTemplate(
            hInstance, reinterpret_cast<const ::MENU_TEMPLATE_ITEM*>(menuTemplate),
            enablersOffset, hImageList, hHotImageList);
    }
    void WINAPI SetSelectedItemIndex(int index) override { Wide->SetSelectedItemIndex(index); }
    int WINAPI GetSelectedItemIndex() override { return Wide->GetSelectedItemIndex(); }
    void WINAPI SetTemplateMenu(HMENU menu) override { Wide->SetTemplateMenu(menu); }
    HMENU WINAPI GetTemplateMenu() override { return Wide->GetTemplateMenu(); }
    sdk107::CGUIMenuPopupAbstract* WINAPI GetSubMenu(DWORD position, BOOL byPosition) override
    {
        return State.WrapMenu(Wide->GetSubMenu(position, byPosition));
    }
    BOOL WINAPI InsertItem(DWORD position, BOOL byPosition,
                           const sdk107::MENU_ITEM_INFO* mii) override
    {
        if (mii == nullptr)
            return FALSE;
        ::MENU_ITEM_INFO wide = {};
        std::wstring stringW;
        if (!CopyMenuInput(*mii, wide, State, stringW))
            return FALSE;
        if ((mii->Mask & MENU_MASK_SUBMENU) != 0 && mii->SubMenu != nullptr && wide.SubMenu == nullptr)
            return FALSE;
        return Wide->InsertItem(position, byPosition, &wide);
    }
    BOOL WINAPI SetItemInfo(DWORD position, BOOL byPosition,
                            const sdk107::MENU_ITEM_INFO* mii) override
    {
        if (mii == nullptr)
            return FALSE;
        ::MENU_ITEM_INFO wide = {};
        std::wstring stringW;
        if (!CopyMenuInput(*mii, wide, State, stringW))
            return FALSE;
        if ((mii->Mask & MENU_MASK_SUBMENU) != 0 && mii->SubMenu != nullptr && wide.SubMenu == nullptr)
            return FALSE;
        return Wide->SetItemInfo(position, byPosition, &wide);
    }
    BOOL WINAPI GetItemInfo(DWORD position, BOOL byPosition,
                            sdk107::MENU_ITEM_INFO* mii) override
    {
        if (mii == nullptr)
            return FALSE;

        const DWORD legacyCapacity = mii->StringLen;
        std::vector<wchar_t> text;
        if ((mii->Mask & MENU_MASK_STRING) != 0)
        {
            ::MENU_ITEM_INFO query = {};
            query.Mask = MENU_MASK_STRING;
            if (!Wide->GetItemInfo(position, byPosition, &query) ||
                query.StringLen == MAXDWORD ||
                !AssignGUIBuffer(text,
                                 static_cast<std::size_t>(query.StringLen) + 1))
                return FALSE;
        }

        ::MENU_ITEM_INFO wide = {};
        wide.Mask = mii->Mask;
        if (!text.empty())
        {
            wide.String = text.data();
            wide.StringLen = static_cast<DWORD>(text.size());
        }
        if (!Wide->GetItemInfo(position, byPosition, &wide))
            return FALSE;

        std::size_t required = 0;
        // pre-unicode used lstrcpyn here: it truncated and still returned TRUE with every
        // other requested field filled in. Refusing left State/ID/Flags uninitialised for a
        // plugin that merely passed a short caption buffer.
        if ((mii->Mask & MENU_MASK_STRING) != 0 &&
            !CopyTruncatedGUIText(text.data(), mii->String, legacyCapacity, &required))
            return FALSE;

        sdk107::CGUIMenuPopupAbstract* legacySubMenu = nullptr;
        if ((mii->Mask & MENU_MASK_SUBMENU) != 0 && wide.SubMenu != nullptr)
        {
            legacySubMenu = State.WrapMenu(wide.SubMenu);
            if (legacySubMenu == nullptr)
                return FALSE;
        }

        if (mii->Mask & MENU_MASK_TYPE) mii->Type = wide.Type;
        if (mii->Mask & MENU_MASK_STATE) mii->State = wide.State;
        if (mii->Mask & MENU_MASK_ID) mii->ID = wide.ID;
        if (mii->Mask & MENU_MASK_SUBMENU) mii->SubMenu = legacySubMenu;
        if (mii->Mask & MENU_MASK_CHECKMARKS)
        {
            mii->HBmpChecked = wide.HBmpChecked;
            mii->HBmpUnchecked = wide.HBmpUnchecked;
        }
        if (mii->Mask & MENU_MASK_BITMAP) mii->HBmpItem = wide.HBmpItem;
        if ((mii->Mask & MENU_MASK_STRING) != 0 && mii->String == nullptr)
            mii->StringLen = static_cast<DWORD>(required);
        if (mii->Mask & MENU_MASK_IMAGEINDEX) mii->ImageIndex = wide.ImageIndex;
        if (mii->Mask & MENU_MASK_ICON) mii->HIcon = wide.HIcon;
        if (mii->Mask & MENU_MASK_OVERLAY) mii->HOverlay = wide.HOverlay;
        if (mii->Mask & MENU_MASK_CUSTOMDATA) mii->CustomData = wide.CustomData;
        if (mii->Mask & MENU_MASK_SKILLLEVEL) mii->SkillLevel = wide.SkillLevel;
        if (mii->Mask & MENU_MASK_ENABLER) mii->Enabler = wide.Enabler;
        if (mii->Mask & MENU_MASK_FLAGS) mii->Flags = wide.Flags;
        return TRUE;
    }
    BOOL WINAPI SetStyle(DWORD style) override { return Wide->SetStyle(style); }
    BOOL WINAPI CheckItem(DWORD position, BOOL byPosition, BOOL checked) override { return Wide->CheckItem(position, byPosition, checked); }
    BOOL WINAPI CheckRadioItem(DWORD first, DWORD last, DWORD check, BOOL byPosition) override { return Wide->CheckRadioItem(first, last, check, byPosition); }
    BOOL WINAPI SetDefaultItem(DWORD position, BOOL byPosition) override { return Wide->SetDefaultItem(position, byPosition); }
    BOOL WINAPI EnableItem(DWORD position, BOOL byPosition, BOOL enabled) override { return Wide->EnableItem(position, byPosition, enabled); }
    int WINAPI GetItemCount() override { return Wide->GetItemCount(); }
    void WINAPI RemoveAllItems() override { Wide->RemoveAllItems(); }
    BOOL WINAPI RemoveItemsRange(int first, int last) override { return Wide->RemoveItemsRange(first, last); }
    BOOL WINAPI BeginModifyMode() override { return Wide->BeginModifyMode(); }
    BOOL WINAPI EndModifyMode() override { return Wide->EndModifyMode(); }
    void WINAPI SetSkillLevel(DWORD level) override { Wide->SetSkillLevel(level); }
    int WINAPI FindItemPosition(DWORD id) override { return Wide->FindItemPosition(id); }
    BOOL WINAPI FillMenuHandle(HMENU menu) override { return Wide->FillMenuHandle(menu); }
    BOOL WINAPI GetStatesFromHWindowsMenu(HMENU menu) override { return Wide->GetStatesFromHWindowsMenu(menu); }
    void WINAPI SetImageList(HIMAGELIST list, BOOL subMenu) override { Wide->SetImageList(list, subMenu); }
    void WINAPI SetHotImageList(HIMAGELIST list, BOOL subMenu) override { Wide->SetHotImageList(list, subMenu); }
    DWORD WINAPI Track(DWORD flags, int x, int y, HWND hwnd, const RECT* exclude) override
    {
        return Wide->Track(flags, x, y, State.ProxyFor(hwnd), exclude);
    }
    BOOL WINAPI GetItemRect(int index, RECT* rect) override { return Wide->GetItemRect(index, rect); }
    void WINAPI UpdateItemsState() override { Wide->UpdateItemsState(); }
    void WINAPI SetMinWidth(int width) override { Wide->SetMinWidth(width); }
    void WINAPI SetPopupID(DWORD id) override { Wide->SetPopupID(id); }
    DWORD WINAPI GetPopupID() override { return Wide->GetPopupID(); }
    void WINAPI AssignHotKeys() override { Wide->AssignHotKeys(); }

private:
    ILegacyGUIState& State;
    ::CGUIMenuPopupAbstract* Wide;
};

class CLegacyGUIMenuBar final : public sdk107::CGUIMenuBarAbstract
{
public:
    CLegacyGUIMenuBar(ILegacyGUIState&, ::CGUIMenuBarAbstract* wide) : Wide(wide) {}
    BOOL WINAPI CreateWnd(HWND parent) override { return Wide->CreateWnd(parent); }
    HWND WINAPI GetHWND() override { return Wide->GetHWND(); }
    int WINAPI GetNeededWidth() override { return Wide->GetNeededWidth(); }
    int WINAPI GetNeededHeight() override { return Wide->GetNeededHeight(); }
    void WINAPI SetFont() override { Wide->SetFont(); }
    BOOL WINAPI GetItemRect(int index, RECT& rect) override { return Wide->GetItemRect(index, rect); }
    void WINAPI EnterMenu() override { Wide->EnterMenu(); }
    BOOL WINAPI IsInMenuLoop() override { return Wide->IsInMenuLoop(); }
    void WINAPI SetHelpMode(BOOL helpMode) override { Wide->SetHelpMode(helpMode); }
    BOOL WINAPI IsMenuBarMessage(CONST MSG* message) override { return Wide->IsMenuBarMessage(message); }

private:
    ::CGUIMenuBarAbstract* Wide;
};

bool CopyToolbarInput(const sdk107::TLBI_ITEM_INFO2& legacy,
                      ::TLBI_ITEM_INFO2& wide,
                      std::wstring& textW)
{
    wide.Mask = legacy.Mask;
    if (legacy.Mask & TLBI_MASK_STYLE) wide.Style = legacy.Style;
    if (legacy.Mask & TLBI_MASK_STATE) wide.State = legacy.State;
    if (legacy.Mask & TLBI_MASK_ID) wide.ID = legacy.ID;
    if (legacy.Mask & TLBI_MASK_TEXT)
    {
        if (!WidenGUICounted(
                legacy.Text,
                (legacy.Mask & TLBI_MASK_TEXTLEN) != 0 ? legacy.TextLen : -1,
                textW))
            return false;
        wide.Text = legacy.Text != nullptr ? textW.data() : nullptr;
        wide.TextLen = static_cast<int>(textW.size());
    }
    if (legacy.Mask & TLBI_MASK_WIDTH) wide.Width = legacy.Width;
    if (legacy.Mask & TLBI_MASK_IMAGEINDEX) wide.ImageIndex = legacy.ImageIndex;
    if (legacy.Mask & TLBI_MASK_ICON) wide.HIcon = legacy.HIcon;
    if (legacy.Mask & TLBI_MASK_OVERLAY) wide.HOverlay = legacy.HOverlay;
    if (legacy.Mask & TLBI_MASK_CUSTOMDATA) wide.CustomData = legacy.CustomData;
    if (legacy.Mask & TLBI_MASK_ENABLER) wide.Enabler = legacy.Enabler;
    return true;
}

class CLegacyGUIToolBar final : public sdk107::CGUIToolBarAbstract
{
public:
    CLegacyGUIToolBar(ILegacyGUIState&, ::CGUIToolBarAbstract* wide) : Wide(wide) {}
    BOOL WINAPI CreateWnd(HWND parent) override { return Wide->CreateWnd(parent); }
    HWND WINAPI GetHWND() override { return Wide->GetHWND(); }
    int WINAPI GetNeededWidth() override { return Wide->GetNeededWidth(); }
    int WINAPI GetNeededHeight() override { return Wide->GetNeededHeight(); }
    void WINAPI SetFont() override { Wide->SetFont(); }
    BOOL WINAPI GetItemRect(int index, RECT& rect) override { return Wide->GetItemRect(index, rect); }
    BOOL WINAPI CheckItem(DWORD position, BOOL byPosition, BOOL checked) override { return Wide->CheckItem(position, byPosition, checked); }
    BOOL WINAPI EnableItem(DWORD position, BOOL byPosition, BOOL enabled) override { return Wide->EnableItem(position, byPosition, enabled); }
    BOOL WINAPI ReplaceImage(DWORD position, BOOL byPosition, HICON icon, BOOL normal, BOOL hot) override { return Wide->ReplaceImage(position, byPosition, icon, normal, hot); }
    int WINAPI FindItemPosition(DWORD id) override { return Wide->FindItemPosition(id); }
    void WINAPI SetImageList(HIMAGELIST list) override { Wide->SetImageList(list); }
    HIMAGELIST WINAPI GetImageList() override { return Wide->GetImageList(); }
    void WINAPI SetHotImageList(HIMAGELIST list) override { Wide->SetHotImageList(list); }
    HIMAGELIST WINAPI GetHotImageList() override { return Wide->GetHotImageList(); }
    void WINAPI SetStyle(DWORD style) override { Wide->SetStyle(style); }
    DWORD WINAPI GetStyle() override { return Wide->GetStyle(); }
    BOOL WINAPI RemoveItem(DWORD position, BOOL byPosition) override { return Wide->RemoveItem(position, byPosition); }
    void WINAPI RemoveAllItems() override { Wide->RemoveAllItems(); }
    int WINAPI GetItemCount() override { return Wide->GetItemCount(); }
    void WINAPI Customize() override { Wide->Customize(); }
    void WINAPI SetPadding(const sdk107::TOOLBAR_PADDING* padding) override
    {
        if (padding == nullptr)
        {
            Wide->SetPadding(nullptr);
            return;
        }
        ::TOOLBAR_PADDING wide = {
            padding->ToolBarVertical, padding->ButtonIconText,
            padding->IconLeft, padding->IconRight,
            padding->TextLeft, padding->TextRight};
        Wide->SetPadding(&wide);
    }
    void WINAPI GetPadding(sdk107::TOOLBAR_PADDING* padding) override
    {
        if (padding == nullptr)
        {
            Wide->GetPadding(nullptr);
            return;
        }
        ::TOOLBAR_PADDING wide = {};
        Wide->GetPadding(&wide);
        padding->ToolBarVertical = wide.ToolBarVertical;
        padding->ButtonIconText = wide.ButtonIconText;
        padding->IconLeft = wide.IconLeft;
        padding->IconRight = wide.IconRight;
        padding->TextLeft = wide.TextLeft;
        padding->TextRight = wide.TextRight;
    }
    void WINAPI UpdateItemsState() override { Wide->UpdateItemsState(); }
    int WINAPI HitTest(int x, int y) override { return Wide->HitTest(x, y); }
    BOOL WINAPI InsertMarkHitTest(int x, int y, int& index, BOOL& after) override { return Wide->InsertMarkHitTest(x, y, index, after); }
    void WINAPI SetInsertMark(int index, BOOL after) override { Wide->SetInsertMark(index, after); }
    int WINAPI SetHotItem(int index) override { return Wide->SetHotItem(index); }
    void WINAPI OnColorsChanged() override { Wide->OnColorsChanged(); }
    BOOL WINAPI InsertItem2(DWORD position, BOOL byPosition,
                            const sdk107::TLBI_ITEM_INFO2* tii) override
    {
        if (tii == nullptr)
            return FALSE;
        ::TLBI_ITEM_INFO2 wide = {};
        std::wstring textW;
        if (!CopyToolbarInput(*tii, wide, textW))
            return FALSE;
        return Wide->InsertItem2(position, byPosition, &wide);
    }
    BOOL WINAPI SetItemInfo2(DWORD position, BOOL byPosition,
                             const sdk107::TLBI_ITEM_INFO2* tii) override
    {
        if (tii == nullptr)
            return FALSE;
        ::TLBI_ITEM_INFO2 wide = {};
        std::wstring textW;
        if (!CopyToolbarInput(*tii, wide, textW))
            return FALSE;
        return Wide->SetItemInfo2(position, byPosition, &wide);
    }
    BOOL WINAPI GetItemInfo2(DWORD position, BOOL byPosition,
                             sdk107::TLBI_ITEM_INFO2* tii) override
    {
        if (tii == nullptr)
            return FALSE;

        const int legacyCapacity = tii->TextLen;
        std::vector<wchar_t> text;
        if ((tii->Mask & TLBI_MASK_TEXT) != 0)
        {
            if (legacyCapacity <= 0)
            {
                SetLastError(ERROR_INSUFFICIENT_BUFFER);
                return FALSE;
            }
            if (!AssignGUIBuffer(text,
                                 static_cast<std::size_t>(legacyCapacity)))
                return FALSE;
        }

        ::TLBI_ITEM_INFO2 wide = {};
        wide.Mask = tii->Mask;
        wide.Text = text.empty() ? nullptr : text.data();
        wide.TextLen = static_cast<int>(text.size());
        if (!Wide->GetItemInfo2(position, byPosition, &wide))
            return FALSE;

        std::size_t required = 0;
        if ((tii->Mask & TLBI_MASK_TEXT) != 0 &&
            !CopyExactGUIText(text.data(), tii->Text,
                              legacyCapacity > 0 ? static_cast<std::size_t>(legacyCapacity) : 0,
                              &required))
            return FALSE;

        if (tii->Mask & TLBI_MASK_STYLE) tii->Style = wide.Style;
        if (tii->Mask & TLBI_MASK_STATE) tii->State = wide.State;
        if (tii->Mask & TLBI_MASK_ID) tii->ID = wide.ID;
        if (tii->Mask & TLBI_MASK_IMAGEINDEX) tii->ImageIndex = wide.ImageIndex;
        if (tii->Mask & TLBI_MASK_ICON) tii->HIcon = wide.HIcon;
        if (tii->Mask & TLBI_MASK_OVERLAY) tii->HOverlay = wide.HOverlay;
        if (tii->Mask & TLBI_MASK_WIDTH) tii->Width = wide.Width;
        if (tii->Mask & TLBI_MASK_ENABLER) tii->Enabler = wide.Enabler;
        if (tii->Mask & TLBI_MASK_CUSTOMDATA) tii->CustomData = wide.CustomData;
        if ((tii->Mask & TLBI_MASK_TEXTLEN) != 0 ||
            ((tii->Mask & TLBI_MASK_TEXT) != 0 && tii->Text == nullptr))
            tii->TextLen = static_cast<int>(required);
        return TRUE;
    }

private:
    ::CGUIToolBarAbstract* Wide;
};

class CLegacyGUIIconList final : public sdk107::CGUIIconListAbstract
{
public:
    CLegacyGUIIconList(ILegacyGUIState& state, ::CGUIIconListAbstract* wide)
        : State(state), Wide(wide) {}
    BOOL WINAPI Create(int width, int height, int count) override { return Wide->Create(width, height, count); }
    BOOL WINAPI CreateFromImageList(HIMAGELIST list, int size) override { return Wide->CreateFromImageList(list, size); }
    BOOL WINAPI CreateFromPNG(HINSTANCE instance, sdk107::LPCTSTR bitmapName, int width) override
    {
        LPCWSTR liveName = nullptr;
        std::wstring nameW;
        if (bitmapName != nullptr)
        {
            if (IS_INTRESOURCE(bitmapName))
                liveName = reinterpret_cast<LPCWSTR>(bitmapName);
            else
            {
                // The v107 slot was LPCTSTR in a non-UNICODE SDK. Treat its
                // pointer as bytes even after the host later flips UNICODE.
                if (!WidenPluginText(
                        reinterpret_cast<const char*>(bitmapName), nameW))
                    return FALSE;
                liveName = nameW.c_str();
            }
        }
        return Wide->CreateFromPNG(instance, liveName, width);
    }
    BOOL WINAPI ReplaceIcon(int index, HICON icon) override { return Wide->ReplaceIcon(index, icon); }
    HICON WINAPI GetIcon(int index) override { return Wide->GetIcon(index); }
    BOOL WINAPI CreateFromRawPNG(const void* raw, DWORD size, int width) override { return Wide->CreateFromRawPNG(raw, size, width); }
    BOOL WINAPI CreateAsCopy(const sdk107::CGUIIconListAbstract* iconList, BOOL grayscale) override
    {
        ::CGUIIconListAbstract* live = State.UnwrapIconList(
            const_cast<sdk107::CGUIIconListAbstract*>(iconList));
        if (iconList != nullptr && live == nullptr)
            return FALSE;
        return Wide->CreateAsCopy(live, grayscale);
    }
    HIMAGELIST WINAPI GetImageList() override { return Wide->GetImageList(); }

private:
    ILegacyGUIState& State;
    ::CGUIIconListAbstract* Wide;
};

class CLegacyGUIToolbarHeader final : public sdk107::CGUIToolbarHeaderAbstract
{
public:
    CLegacyGUIToolbarHeader(ILegacyGUIState& state, ::CGUIToolbarHeaderAbstract* wide) : State(state), Wide(wide) {}
    void WINAPI EnableToolbar(DWORD mask) override { Wide->EnableToolbar(mask); }
    void WINAPI CheckToolbar(DWORD mask) override { Wide->CheckToolbar(mask); }
    void WINAPI SetNotifyWindow(HWND window) override { Wide->SetNotifyWindow(State.ProxyFor(window)); }

private:
    ILegacyGUIState& State;
    ::CGUIToolbarHeaderAbstract* Wide;
};

class CLegacyGUINotifyProxy final
{
public:
    CLegacyGUINotifyProxy(ILegacyGUIState& state, HWND target)
        : State(state), Target(target)
    {
        std::call_once(ClassOnce, []()
        {
            WNDCLASSW wc = {};
            wc.lpfnWndProc = WindowProc;
            wc.hInstance = GetModuleHandleW(nullptr);
            wc.lpszClassName = L"Sally.LegacyGUI.NotifyProxy";
            ClassAtom = RegisterClassW(&wc);
            if (ClassAtom == 0 && GetLastError() == ERROR_CLASS_ALREADY_EXISTS)
                ClassAtom = 1;
        });

        if (ClassAtom != 0)
        {
            Window = CreateWindowExW(0, L"Sally.LegacyGUI.NotifyProxy", L"", 0,
                                     0, 0, 0, 0, HWND_MESSAGE, nullptr,
                                     GetModuleHandleW(nullptr), this);
        }
    }

    ~CLegacyGUINotifyProxy()
    {
        if (Window != nullptr)
            DestroyWindow(Window);
    }

    HWND GetWindow() const { return Window; }

private:
    static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
    {
        CLegacyGUINotifyProxy* self = reinterpret_cast<CLegacyGUINotifyProxy*>(
            GetWindowLongPtrW(window, GWLP_USERDATA));
        if (message == WM_NCCREATE)
        {
            const CREATESTRUCTW* create = reinterpret_cast<const CREATESTRUCTW*>(lParam);
            self = static_cast<CLegacyGUINotifyProxy*>(create->lpCreateParams);
            SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
            return TRUE;
        }
        if (message == WM_NCDESTROY)
        {
            SetWindowLongPtrW(window, GWLP_USERDATA, 0);
            return DefWindowProcW(window, message, wParam, lParam);
        }
        // WM_NOTIFY is below WM_USER but the core's toolbar delivers NM_RCLICK through it
        // (toolbar_base.cpp) and honours a TRUE reply. Dropping it cost v107 plugins their
        // toolbar context menu. The payload is a bare NMHDR - no text to convert.
        if (message < WM_USER && message != WM_COMMAND && message != WM_NOTIFY)
            return DefWindowProcW(window, message, wParam, lParam);
        return self != nullptr ? self->Forward(message, wParam, lParam)
                               : DefWindowProcW(window, message, wParam, lParam);
    }

    LRESULT ForwardToolbarTooltip(UINT message, WPARAM wParam, LPARAM lParam)
    {
        if (lParam == 0)
            return SendMessageW(Target, message, wParam, lParam);

        ::TOOLBAR_TOOLTIP* wide = reinterpret_cast<::TOOLBAR_TOOLTIP*>(lParam);
        std::vector<char> buffer;
        if (!AssignGUIBuffer(buffer, TOOLTIP_TEXT_MAX))
            return 0;
        sdk107::TOOLBAR_TOOLTIP legacy = {};
        legacy.HToolBar = wide->HToolBar;
        legacy.ID = wide->ID;
        legacy.Index = wide->Index;
        legacy.CustomData = wide->CustomData;
        legacy.Buffer = buffer.data();
        const LRESULT result = SendMessageW(
            Target, message, wParam, reinterpret_cast<LPARAM>(&legacy));
        std::wstring text;
        if (!WidenGUIBuffer(buffer, text) ||
            !CanCopyWideGUIText(text, wide->Buffer, TOOLTIP_TEXT_MAX))
            return 0;
        CopyWideGUIText(text, wide->Buffer);
        return result;
    }

    LRESULT ForwardToolbarEnumeration(UINT message, WPARAM wParam, LPARAM lParam)
    {
        if (lParam == 0)
            return SendMessageW(Target, message, wParam, lParam);

        ::TLBI_ITEM_INFO2* wide = reinterpret_cast<::TLBI_ITEM_INFO2*>(lParam);
        const int textCapacity = wide->Text != nullptr ? wide->TextLen : 0;
        const int nameCapacity = wide->Name != nullptr ? wide->NameLen : 0;
        if ((wide->Text != nullptr && textCapacity <= 0) ||
            (wide->Name != nullptr && nameCapacity <= 0))
        {
            SetLastError(ERROR_INSUFFICIENT_BUFFER);
            return 0;
        }
        std::vector<char> text;
        std::vector<char> name;
        if ((wide->Text != nullptr &&
             !AssignGUIBuffer(text, static_cast<std::size_t>(textCapacity))) ||
            (wide->Name != nullptr &&
             !AssignGUIBuffer(name, static_cast<std::size_t>(nameCapacity))))
            return 0;

        if ((wide->Text != nullptr &&
             !CopyExactGUIText(wide->Text, text.data(), text.size())) ||
            (wide->Name != nullptr &&
             !CopyExactGUIText(wide->Name, name.data(), name.size())))
            return 0;

        sdk107::TLBI_ITEM_INFO2 legacy = {};
        legacy.Mask = wide->Mask;
        legacy.Style = wide->Style;
        legacy.State = wide->State;
        legacy.ID = wide->ID;
        legacy.Text = wide->Text != nullptr ? text.data() : nullptr;
        legacy.TextLen = textCapacity;
        legacy.Width = wide->Width;
        legacy.ImageIndex = wide->ImageIndex;
        legacy.HIcon = wide->HIcon;
        legacy.HOverlay = wide->HOverlay;
        legacy.CustomData = wide->CustomData;
        legacy.Enabler = wide->Enabler;
        legacy.Index = wide->Index;
        legacy.Name = wide->Name != nullptr ? name.data() : nullptr;
        legacy.NameLen = nameCapacity;

        const LRESULT result = SendMessageW(
            Target, message, wParam, reinterpret_cast<LPARAM>(&legacy));
        if (result != 0)
        {
            std::wstring textW;
            std::wstring nameW;
            if ((wide->Text != nullptr && !WidenGUIBuffer(text, textW)) ||
                (wide->Name != nullptr && !WidenGUIBuffer(name, nameW)) ||
                !CanCopyWideGUIText(textW, wide->Text, wide->TextLen) ||
                !CanCopyWideGUIText(nameW, wide->Name, wide->NameLen))
                return 0;
            CopyWideGUIText(textW, wide->Text);
            CopyWideGUIText(nameW, wide->Name);
            wide->Mask = legacy.Mask;
            wide->Style = legacy.Style;
            wide->State = legacy.State;
            wide->ID = legacy.ID;
            wide->Width = legacy.Width;
            wide->ImageIndex = legacy.ImageIndex;
            wide->HIcon = legacy.HIcon;
            wide->HOverlay = legacy.HOverlay;
            wide->CustomData = legacy.CustomData;
            wide->Enabler = legacy.Enabler;
            wide->Index = legacy.Index;
        }
        return result;
    }

    LRESULT Forward(UINT message, WPARAM wParam, LPARAM lParam)
    {
        switch (message)
        {
        case WM_USER_INITMENUPOPUP:
        case WM_USER_UNINITMENUPOPUP:
        case WM_USER_CONTEXTMENU:
            return SendMessageW(
                Target, message,
                reinterpret_cast<WPARAM>(State.WrapMenu(
                    reinterpret_cast<::CGUIMenuPopupAbstract*>(wParam))),
                lParam);

        case WM_USER_TBGETTOOLTIP:
            return ForwardToolbarTooltip(message, wParam, lParam);

        case WM_USER_TBENUMBUTTON2:
            return ForwardToolbarEnumeration(message, wParam, lParam);

        case WM_USER_TTGETTEXTW:
        {
            std::vector<char> text;
            if (!AssignGUIBuffer(text, TOOLTIP_TEXT_MAX))
                return 0;
            const LRESULT result = SendMessageW(
                Target, WM_USER_TTGETTEXT, wParam,
                reinterpret_cast<LPARAM>(text.data()));
            std::wstring textW;
            wchar_t* output = reinterpret_cast<wchar_t*>(lParam);
            if (!WidenGUIBuffer(text, textW) ||
                !CanCopyWideGUIText(textW, output, TOOLTIP_TEXT_MAX))
                return 0;
            CopyWideGUIText(textW, output);
            return result;
        }

        case WM_COMMAND:
        case WM_USER_LEAVEMENULOOP2:
            PostMessageW(Target, message, wParam, lParam);
            return 0;

        default:
            return SendMessageW(Target, message, wParam, lParam);
        }
    }

    ILegacyGUIState& State;
    HWND Target = nullptr;
    HWND Window = nullptr;

    static std::once_flag ClassOnce;
    static ATOM ClassAtom;
};

std::once_flag CLegacyGUINotifyProxy::ClassOnce;
ATOM CLegacyGUINotifyProxy::ClassAtom = 0;

template <typename TLive, typename TLegacy, typename TWrapper>
class CLegacyGUIObjectMap
{
public:
    TLegacy* Wrap(ILegacyGUIState& state, TLive* live)
    {
        if (live == nullptr)
            return nullptr;
        const auto found = ByLive.find(live);
        if (found != ByLive.end())
            return found->second.get();

        try
        {
            auto wrapper = std::make_unique<TWrapper>(state, live);
            TLegacy* legacy = wrapper.get();
            const auto published = ByLive.emplace(live, std::move(wrapper));
            if (!published.second)
                return published.first->second.get();
            try
            {
                if (!ByLegacy.emplace(legacy, live).second)
                {
                    ByLive.erase(published.first);
                    SetLastError(ERROR_ALREADY_EXISTS);
                    return nullptr;
                }
            }
            catch (...)
            {
                ByLive.erase(published.first);
                throw;
            }
            return legacy;
        }
        catch (const std::bad_alloc&)
        {
            SetLastError(ERROR_NOT_ENOUGH_MEMORY);
            return nullptr;
        }
        catch (const std::length_error&)
        {
            SetLastError(ERROR_INSUFFICIENT_BUFFER);
            return nullptr;
        }
        catch (...)
        {
            SetLastError(ERROR_GEN_FAILURE);
            return nullptr;
        }
    }

    TLive* Unwrap(TLegacy* legacy) const
    {
        if (legacy == nullptr)
            return nullptr;
        const auto found = ByLegacy.find(legacy);
        return found != ByLegacy.end() ? found->second : nullptr;
    }

    void Erase(TLegacy* legacy)
    {
        const auto found = ByLegacy.find(legacy);
        if (found == ByLegacy.end())
            return;
        TLive* live = found->second;
        ByLegacy.erase(found);
        ByLive.erase(live);
    }

    TLive* Take(TLegacy* legacy)
    {
        TLive* live = Unwrap(legacy);
        if (live == nullptr)
            return nullptr;
        Erase(legacy);
        return live;
    }

private:
    std::unordered_map<TLive*, std::unique_ptr<TWrapper>> ByLive;
    std::unordered_map<TLegacy*, TLive*> ByLegacy;
};

} // namespace

class CLegacySalamanderGUI::CState final : public ILegacyGUIState
{
public:
    sdk107::CGUIProgressBarAbstract* WrapProgress(::CGUIProgressBarAbstract* value) { return Progress.Wrap(*this, value); }
    sdk107::CGUIStaticTextAbstract* WrapStatic(::CGUIStaticTextAbstract* value) { return Static.Wrap(*this, value); }
    sdk107::CGUIHyperLinkAbstract* WrapHyperLink(::CGUIHyperLinkAbstract* value) { return HyperLink.Wrap(*this, value); }
    sdk107::CGUIButtonAbstract* WrapButton(::CGUIButtonAbstract* value) { return Button.Wrap(*this, value); }
    sdk107::CGUIColorArrowButtonAbstract* WrapColorButton(::CGUIColorArrowButtonAbstract* value) { return ColorButton.Wrap(*this, value); }
    sdk107::CGUIMenuPopupAbstract* WrapMenu(::CGUIMenuPopupAbstract* value) override { return Menu.Wrap(*this, value); }
    sdk107::CGUIMenuBarAbstract* WrapMenuBar(::CGUIMenuBarAbstract* value) { return MenuBar.Wrap(*this, value); }
    sdk107::CGUIToolBarAbstract* WrapToolBar(::CGUIToolBarAbstract* value) { return ToolBar.Wrap(*this, value); }
    sdk107::CGUIIconListAbstract* WrapIconList(::CGUIIconListAbstract* value) { return IconList.Wrap(*this, value); }
    sdk107::CGUIToolbarHeaderAbstract* WrapToolbarHeader(::CGUIToolbarHeaderAbstract* value) { return ToolbarHeader.Wrap(*this, value); }

    ::CGUIMenuPopupAbstract* UnwrapMenu(sdk107::CGUIMenuPopupAbstract* value) const override { return Menu.Unwrap(value); }
    ::CGUIMenuBarAbstract* UnwrapMenuBar(sdk107::CGUIMenuBarAbstract* value) const { return MenuBar.Unwrap(value); }
    ::CGUIToolBarAbstract* UnwrapToolBar(sdk107::CGUIToolBarAbstract* value) const { return ToolBar.Unwrap(value); }
    ::CGUIIconListAbstract* UnwrapIconList(sdk107::CGUIIconListAbstract* value) const override { return IconList.Unwrap(value); }

    void ForgetMenu(sdk107::CGUIMenuPopupAbstract* value) { Menu.Erase(value); }
    void ForgetMenuBar(sdk107::CGUIMenuBarAbstract* value) { MenuBar.Erase(value); }
    void ForgetToolBar(sdk107::CGUIToolBarAbstract* value) { ToolBar.Erase(value); }
    void ForgetIconList(sdk107::CGUIIconListAbstract* value) { IconList.Erase(value); }
    ::CGUIIconListAbstract* TakeIconList(sdk107::CGUIIconListAbstract* value) { return IconList.Take(value); }

    HWND ProxyFor(HWND target) override
    {
        if (target == nullptr)
            return nullptr;
        const auto found = Proxies.find(target);
        if (found != Proxies.end())
            return found->second->GetWindow();
        try
        {
            auto proxy =
                std::make_unique<CLegacyGUINotifyProxy>(*this, target);
            if (proxy->GetWindow() == nullptr)
                return nullptr;
            HWND window = proxy->GetWindow();
            if (!Proxies.emplace(target, std::move(proxy)).second)
                return Proxies.find(target)->second->GetWindow();
            return window;
        }
        catch (const std::bad_alloc&)
        {
            SetLastError(ERROR_NOT_ENOUGH_MEMORY);
            return nullptr;
        }
        catch (const std::length_error&)
        {
            SetLastError(ERROR_INSUFFICIENT_BUFFER);
            return nullptr;
        }
        catch (...)
        {
            SetLastError(ERROR_GEN_FAILURE);
            return nullptr;
        }
    }

private:
    CLegacyGUIObjectMap<::CGUIProgressBarAbstract, sdk107::CGUIProgressBarAbstract, CLegacyGUIProgressBar> Progress;
    CLegacyGUIObjectMap<::CGUIStaticTextAbstract, sdk107::CGUIStaticTextAbstract, CLegacyGUIStaticText> Static;
    CLegacyGUIObjectMap<::CGUIHyperLinkAbstract, sdk107::CGUIHyperLinkAbstract, CLegacyGUIHyperLink> HyperLink;
    CLegacyGUIObjectMap<::CGUIButtonAbstract, sdk107::CGUIButtonAbstract, CLegacyGUIButton> Button;
    CLegacyGUIObjectMap<::CGUIColorArrowButtonAbstract, sdk107::CGUIColorArrowButtonAbstract, CLegacyGUIColorArrowButton> ColorButton;
    CLegacyGUIObjectMap<::CGUIMenuPopupAbstract, sdk107::CGUIMenuPopupAbstract, CLegacyGUIMenuPopup> Menu;
    CLegacyGUIObjectMap<::CGUIMenuBarAbstract, sdk107::CGUIMenuBarAbstract, CLegacyGUIMenuBar> MenuBar;
    CLegacyGUIObjectMap<::CGUIToolBarAbstract, sdk107::CGUIToolBarAbstract, CLegacyGUIToolBar> ToolBar;
    CLegacyGUIObjectMap<::CGUIIconListAbstract, sdk107::CGUIIconListAbstract, CLegacyGUIIconList> IconList;
    CLegacyGUIObjectMap<::CGUIToolbarHeaderAbstract, sdk107::CGUIToolbarHeaderAbstract, CLegacyGUIToolbarHeader> ToolbarHeader;
    std::unordered_map<HWND, std::unique_ptr<CLegacyGUINotifyProxy>> Proxies;
};

CLegacySalamanderGUI::CLegacySalamanderGUI(::CSalamanderGUIAbstract& wideGUI)
    : WideGUI(wideGUI), State(std::make_unique<CState>())
{
}

CLegacySalamanderGUI::~CLegacySalamanderGUI() = default;

sdk107::CGUIProgressBarAbstract* WINAPI CLegacySalamanderGUI::AttachProgressBar(HWND parent, int ctrlID)
{
    return State->WrapProgress(WideGUI.AttachProgressBar(parent, ctrlID));
}

sdk107::CGUIStaticTextAbstract* WINAPI CLegacySalamanderGUI::AttachStaticText(HWND parent, int ctrlID, DWORD flags)
{
    return State->WrapStatic(WideGUI.AttachStaticText(parent, ctrlID, flags));
}

sdk107::CGUIHyperLinkAbstract* WINAPI CLegacySalamanderGUI::AttachHyperLink(HWND parent, int ctrlID, DWORD flags)
{
    return State->WrapHyperLink(WideGUI.AttachHyperLink(parent, ctrlID, flags));
}

sdk107::CGUIButtonAbstract* WINAPI CLegacySalamanderGUI::AttachButton(HWND parent, int ctrlID, DWORD flags)
{
    return State->WrapButton(WideGUI.AttachButton(parent, ctrlID, flags));
}

sdk107::CGUIColorArrowButtonAbstract* WINAPI CLegacySalamanderGUI::AttachColorArrowButton(HWND parent, int ctrlID, BOOL showArrow)
{
    return State->WrapColorButton(WideGUI.AttachColorArrowButton(parent, ctrlID, showArrow));
}

BOOL WINAPI CLegacySalamanderGUI::ChangeToArrowButton(HWND parent, int ctrlID)
{
    return WideGUI.ChangeToArrowButton(parent, ctrlID);
}

sdk107::CGUIMenuPopupAbstract* WINAPI CLegacySalamanderGUI::CreateMenuPopup()
{
    ::CGUIMenuPopupAbstract* wide = WideGUI.CreateMenuPopup();
    sdk107::CGUIMenuPopupAbstract* legacy = State->WrapMenu(wide);
    if (wide != nullptr && legacy == nullptr)
        WideGUI.DestroyMenuPopup(wide);
    return legacy;
}

BOOL WINAPI CLegacySalamanderGUI::DestroyMenuPopup(sdk107::CGUIMenuPopupAbstract* popup)
{
    ::CGUIMenuPopupAbstract* wide = State->UnwrapMenu(popup);
    if (wide == nullptr)
        return FALSE;
    const BOOL result = WideGUI.DestroyMenuPopup(wide);
    if (result)
        State->ForgetMenu(popup);
    return result;
}

sdk107::CGUIMenuBarAbstract* WINAPI CLegacySalamanderGUI::CreateMenuBar(
    sdk107::CGUIMenuPopupAbstract* menu, HWND notifyWindow)
{
    ::CGUIMenuPopupAbstract* wideMenu = State->UnwrapMenu(menu);
    if (menu != nullptr && wideMenu == nullptr)
        return nullptr;
    HWND proxy = State->ProxyFor(notifyWindow);
    if (notifyWindow != nullptr && proxy == nullptr)
        return nullptr;
    ::CGUIMenuBarAbstract* wide = WideGUI.CreateMenuBar(wideMenu, proxy);
    sdk107::CGUIMenuBarAbstract* legacy = State->WrapMenuBar(wide);
    if (wide != nullptr && legacy == nullptr)
        WideGUI.DestroyMenuBar(wide);
    return legacy;
}

BOOL WINAPI CLegacySalamanderGUI::DestroyMenuBar(sdk107::CGUIMenuBarAbstract* menuBar)
{
    ::CGUIMenuBarAbstract* wide = State->UnwrapMenuBar(menuBar);
    if (wide == nullptr)
        return FALSE;
    const BOOL result = WideGUI.DestroyMenuBar(wide);
    if (result)
        State->ForgetMenuBar(menuBar);
    return result;
}

BOOL WINAPI CLegacySalamanderGUI::CreateGrayscaleAndMaskBitmaps(
    HBITMAP source, COLORREF transparent, HBITMAP& grayscale, HBITMAP& mask)
{
    return WideGUI.CreateGrayscaleAndMaskBitmaps(source, transparent, grayscale, mask);
}

sdk107::CGUIToolBarAbstract* WINAPI CLegacySalamanderGUI::CreateToolBar(HWND notifyWindow)
{
    HWND proxy = State->ProxyFor(notifyWindow);
    if (notifyWindow != nullptr && proxy == nullptr)
        return nullptr;
    ::CGUIToolBarAbstract* wide = WideGUI.CreateToolBar(proxy);
    sdk107::CGUIToolBarAbstract* legacy = State->WrapToolBar(wide);
    if (wide != nullptr && legacy == nullptr)
        WideGUI.DestroyToolBar(wide);
    return legacy;
}

BOOL WINAPI CLegacySalamanderGUI::DestroyToolBar(sdk107::CGUIToolBarAbstract* toolBar)
{
    ::CGUIToolBarAbstract* wide = State->UnwrapToolBar(toolBar);
    if (wide == nullptr)
        return FALSE;
    const BOOL result = WideGUI.DestroyToolBar(wide);
    if (result)
        State->ForgetToolBar(toolBar);
    return result;
}

void WINAPI CLegacySalamanderGUI::SetCurrentToolTip(HWND notifyWindow, DWORD id)
{
    WideGUI.SetCurrentToolTip(State->ProxyFor(notifyWindow), id);
}

void WINAPI CLegacySalamanderGUI::SuppressToolTipOnCurrentMousePos()
{
    WideGUI.SuppressToolTipOnCurrentMousePos();
}

BOOL WINAPI CLegacySalamanderGUI::DisableWindowVisualStyles(HWND window)
{
    return WideGUI.DisableWindowVisualStyles(window);
}

sdk107::CGUIIconListAbstract* WINAPI CLegacySalamanderGUI::CreateIconList()
{
    ::CGUIIconListAbstract* wide = WideGUI.CreateIconList();
    sdk107::CGUIIconListAbstract* legacy = State->WrapIconList(wide);
    if (wide != nullptr && legacy == nullptr)
        WideGUI.DestroyIconList(wide);
    return legacy;
}

BOOL WINAPI CLegacySalamanderGUI::DestroyIconList(sdk107::CGUIIconListAbstract* iconList)
{
    ::CGUIIconListAbstract* wide = State->UnwrapIconList(iconList);
    if (wide == nullptr)
        return FALSE;
    const BOOL result = WideGUI.DestroyIconList(wide);
    if (result)
        State->ForgetIconList(iconList);
    return result;
}

::CGUIIconListAbstract* CLegacySalamanderGUI::TakeIconList(
    sdk107::CGUIIconListAbstract* iconList)
{
    return State->TakeIconList(iconList);
}

void WINAPI CLegacySalamanderGUI::PrepareToolTipText(char* buffer, BOOL stripHotKey)
{
    if (buffer == nullptr)
        return;
    const std::size_t legacyLength = std::strlen(buffer);
    std::wstring text;
    if (!WidenPluginText(buffer, text))
    {
        buffer[0] = '\0';
        return;
    }
    CSalamanderStringBufferOwner owner(text);
    if (!owner.IsValid() ||
        !WideGUI.PrepareToolTipText(owner.Buffer(), stripHotKey) ||
        !owner.GetValue(text))
    {
        buffer[0] = '\0';
        return;
    }
    const NarrowResult narrowed = NarrowExact(text);
    if (!narrowed.ok)
    {
        buffer[0] = '\0';
        return;
    }
    if (legacyLength > SIZE_MAX - 2 ||
        narrowed.value.size() > legacyLength + 2)
    {
        buffer[0] = '\0';
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return;
    }
    std::memcpy(buffer, narrowed.value.c_str(), narrowed.value.size() + 1);
}

void WINAPI CLegacySalamanderGUI::SetSubjectTruncatedText(
    HWND subjectWindow, const char* format, const char* fileName,
    BOOL isDir, BOOL duplicateAmpersands)
{
    std::wstring formatW;
    std::wstring fileNameW;
    const wchar_t* liveFormat = nullptr;
    const wchar_t* liveFileName = nullptr;
    if (!WidenGUIOptional(format, formatW, liveFormat) ||
        !WidenGUIOptional(fileName, fileNameW, liveFileName))
        return;
    WideGUI.SetSubjectTruncatedText(
        subjectWindow, liveFormat, liveFileName, isDir, duplicateAmpersands);
}

sdk107::CGUIToolbarHeaderAbstract* WINAPI CLegacySalamanderGUI::AttachToolbarHeader(
    HWND parent, int ctrlID, HWND alignWindow, DWORD buttonMask)
{
    return State->WrapToolbarHeader(
        WideGUI.AttachToolbarHeader(parent, ctrlID, alignWindow, buttonMask));
}

void WINAPI CLegacySalamanderGUI::ArrangeHorizontalLines(HWND window)
{
    WideGUI.ArrangeHorizontalLines(window);
}

int WINAPI CLegacySalamanderGUI::GetWindowFontHeight(HWND window)
{
    return WideGUI.GetWindowFontHeight(window);
}

} // namespace sally::compat
