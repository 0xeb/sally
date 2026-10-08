// SPDX-FileCopyrightText: 2025-2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"

#include <commdlg.h>

#include <string>

#include "configuration_dialog.h"
#include "dialogs.h"
#include "pictview.rh2"
#include "lang/lang.rh"

namespace pictview
{
namespace
{

// The pages edit one copy of the preferences; OK hands it back.
class CConfigPage : public CPropSheetPage
{
public:
    CConfigPage(HINSTANCE instance, int resourceId, ViewerPreferences& preferences)
        : CPropSheetPage(nullptr, instance, resourceId, resourceId, PSP_HASHELP, nullptr), Preferences(preferences)
    {
    }

protected:
    ViewerPreferences& Preferences;
};

class CAppearancePage : public CConfigPage
{
public:
    CAppearancePage(HINSTANCE instance, ViewerPreferences& preferences) : CConfigPage(instance, DLG_CFGPAGE_APPEARANCE, preferences) {}

    void Transfer(CTransferInfo& ti) override
    {
        int windowSize = static_cast<int>(Preferences.WindowSizeMode);
        ti.RadioButton(IDC_CFGAPP_WINPOS_SAME, static_cast<int>(ViewerWindowSizeMode::SameAsSally), windowSize);
        ti.RadioButton(IDC_CFGAPP_WINPOS_LARGER, static_cast<int>(ViewerWindowSizeMode::LargerIfNeeded), windowSize);
        ti.RadioButton(IDC_CFGAPP_WINPOS_ANY, static_cast<int>(ViewerWindowSizeMode::AsNeeded), windowSize);
        Preferences.WindowSizeMode = static_cast<ViewerWindowSizeMode>(windowSize);

        int zoom = static_cast<int>(Preferences.DefaultZoomMode);
        ti.RadioButton(IDC_CFGAPP_ZOOM_WHOLE, static_cast<int>(ViewerDefaultZoomMode::FitWhole), zoom);
        ti.RadioButton(IDC_CFGAPP_ZOOM_WIDTH, static_cast<int>(ViewerDefaultZoomMode::FitWidth), zoom);
        ti.RadioButton(IDC_CFGAPP_ZOOM_ORIG, static_cast<int>(ViewerDefaultZoomMode::ActualSize), zoom);
        ti.RadioButton(IDC_CFGAPP_ZOOM_FSCREEN, static_cast<int>(ViewerDefaultZoomMode::FullScreen), zoom);
        Preferences.DefaultZoomMode = static_cast<ViewerDefaultZoomMode>(zoom);

        int path = Preferences.ShowFullPathInTitle ? 1 : 0;
        ti.CheckBox(IDC_CFGAPP_PATH, path);
        Preferences.ShowFullPathInTitle = path != 0;
    }
};

class CColorsPage : public CConfigPage
{
public:
    CColorsPage(HINSTANCE instance, ViewerPreferences& preferences, CSalamanderGUIAbstract* gui)
        : CConfigPage(instance, DLG_CFGPAGE_COLORS, preferences), m_gui(gui)
    {
    }

    void Transfer(CTransferInfo& ti) override
    {
        if (ti.Type == ttDataFromWindow)
        {
            for (const ColorSlot& slot : m_slots)
                Preferences.*(slot.Field) = slot.Color;
        }
    }

protected:
    INT_PTR DialogProc(UINT message, WPARAM wParam, LPARAM lParam) override
    {
        switch (message)
        {
        case WM_INITDIALOG:
            for (ColorSlot& slot : m_slots)
            {
                slot.Color = Preferences.*(slot.Field);
                if (m_gui != nullptr)
                    slot.Button = m_gui->AttachColorArrowButton(HWindow, slot.Control, TRUE);
                if (slot.Button != nullptr)
                    slot.Button->SetColor(slot.Color, slot.Color);
            }
            break;

        case WM_COMMAND:
            for (ColorSlot& slot : m_slots)
            {
                if (LOWORD(wParam) == slot.Control)
                {
                    ChooseSlotColor(slot);
                    return 0;
                }
            }
            break;
        }
        return CConfigPage::DialogProc(message, wParam, lParam);
    }

private:
    struct ColorSlot
    {
        int Control;
        COLORREF ViewerPreferences::*Field;
        COLORREF Default;
        COLORREF Color = 0;
        CGUIColorArrowButtonAbstract* Button = nullptr;
    };

    // The button's popup offers a custom color or the automatic (default) one.
    void ChooseSlotColor(ColorSlot& slot)
    {
        if (m_gui == nullptr)
            return;
        CGUIMenuPopupAbstract* popup = m_gui->CreateMenuPopup();
        if (popup == nullptr)
            return;
        const bool automatic = slot.Color == slot.Default;
        MENU_ITEM_INFO item = {};
        item.Mask = MENU_MASK_TYPE | MENU_MASK_STRING | MENU_MASK_ID | MENU_MASK_STATE;
        item.Type = MENU_TYPE_STRING | MENU_TYPE_RADIOCHECK;
        std::wstring custom = ViewerText(IDS_CUSTOM_COLOR);
        item.String = custom.data();
        item.ID = 1;
        item.State = automatic ? 0 : MENU_STATE_CHECKED;
        popup->InsertItem(0xFFFFFFFF, TRUE, &item);
        std::wstring automaticText = ViewerText(IDS_AUTOMATIC_COLOR);
        item.String = automaticText.data();
        item.ID = 2;
        item.State = automatic ? MENU_STATE_CHECKED : 0;
        popup->InsertItem(0xFFFFFFFF, TRUE, &item);

        RECT rect = {};
        GetWindowRect(GetDlgItem(HWindow, slot.Control), &rect);
        popup->SetSelectedItemIndex(0);
        const DWORD command = popup->Track(MENU_TRACK_RETURNCMD | MENU_TRACK_RIGHTBUTTON | MENU_TRACK_SELECT,
                                           rect.right, rect.top, HWindow, &rect);
        m_gui->DestroyMenuPopup(popup);

        if (command == 1)
        {
            static COLORREF customColors[16] = {};
            CHOOSECOLORW choose = {};
            choose.lStructSize = sizeof(choose);
            choose.hwndOwner = HWindow;
            choose.lpCustColors = customColors;
            choose.rgbResult = slot.Color;
            choose.Flags = CC_RGBINIT | CC_FULLOPEN | CC_SOLIDCOLOR;
            if (!ChooseColorW(&choose))
                return;
            slot.Color = choose.rgbResult;
        }
        else if (command == 2)
            slot.Color = slot.Default;
        else
            return;
        if (slot.Button != nullptr)
            slot.Button->SetColor(slot.Color, slot.Color);
    }

    CSalamanderGUIAbstract* m_gui;
    ColorSlot m_slots[4] = {
        {IDC_CFGCLR_BORDER, &ViewerPreferences::BackgroundColor, RGB(128, 128, 128)},
        {IDC_CFGCLR_BKGND, &ViewerPreferences::TransparentColor, RGB(255, 255, 255)},
        {IDC_CFGCLR_FS_BORDER, &ViewerPreferences::FullScreenBackgroundColor, RGB(0, 0, 0)},
        {IDC_CFGCLR_FS_BKGND, &ViewerPreferences::FullScreenTransparentColor, RGB(255, 255, 255)},
    };
};

class CKeyboardPage : public CConfigPage
{
public:
    CKeyboardPage(HINSTANCE instance, ViewerPreferences& preferences) : CConfigPage(instance, DLG_CFGPAGE_KEYBOARD, preferences) {}

    void Transfer(CTransferInfo& ti) override
    {
        int scrolls = Preferences.PageUpDownScrolls ? 1 : 0;
        ti.RadioButton(IDC_CFGKBD_PGDNUP_SCR, 1, scrolls);
        ti.RadioButton(IDC_CFGKBD_PGDNUP_PREV, 0, scrolls);
        Preferences.PageUpDownScrolls = scrolls != 0;
    }
};

class CToolsPage : public CConfigPage
{
public:
    CToolsPage(HINSTANCE instance, ViewerPreferences& preferences) : CConfigPage(instance, DLG_CFGPAGE_TOOLS, preferences) {}

    void Validate(CTransferInfo& ti) override
    {
        if (IsDlgButtonChecked(HWindow, IDC_CFGTOOLS_OTHER) != BST_CHECKED)
            return;
        for (const int control : {IDC_CFGTOOLS_RATIO_X, IDC_CFGTOOLS_RATIO_Y})
        {
            int value = 0;
            ti.EditLine(control, value);
            if (ti.IsGood() && value <= 0)
            {
                ViewerMessageBox(HWindow, ViewerText(IDS_MUSTBEPOSITIVE), MB_OK | MB_ICONEXCLAMATION);
                ti.ErrorOn(control);
                return;
            }
        }
    }

    void Transfer(CTransferInfo& ti) override
    {
        struct Ratio
        {
            int Control, X, Y;
        };
        static constexpr Ratio presets[] = {
            {IDC_CFGTOOLS_SQUARE, 1, 1}, {IDC_CFGTOOLS_4_3, 4, 3}, {IDC_CFGTOOLS_3_2, 3, 2}, {IDC_CFGTOOLS_16_9, 16, 9}};

        int choice = IDC_CFGTOOLS_OTHER;
        for (const Ratio& preset : presets)
        {
            if (Preferences.SelectRatioX == preset.X && Preferences.SelectRatioY == preset.Y)
                choice = preset.Control;
        }
        for (const Ratio& preset : presets)
            ti.RadioButton(preset.Control, preset.Control, choice);
        ti.RadioButton(IDC_CFGTOOLS_OTHER, IDC_CFGTOOLS_OTHER, choice);

        int x = Preferences.SelectRatioX;
        int y = Preferences.SelectRatioY;
        ti.EditLine(IDC_CFGTOOLS_RATIO_X, x);
        ti.EditLine(IDC_CFGTOOLS_RATIO_Y, y);
        if (ti.Type == ttDataFromWindow)
        {
            for (const Ratio& preset : presets)
            {
                if (choice == preset.Control)
                {
                    x = preset.X;
                    y = preset.Y;
                }
            }
            Preferences.SelectRatioX = x;
            Preferences.SelectRatioY = y;
        }

        int hex = Preferences.PipetteInHex ? 1 : 0;
        ti.CheckBox(IDC_CFGTOOLS_PIPHEX, hex);
        Preferences.PipetteInHex = hex != 0;

        if (ti.Type == ttDataToWindow)
            EnableRatioEdits();
    }

protected:
    INT_PTR DialogProc(UINT message, WPARAM wParam, LPARAM lParam) override
    {
        if (message == WM_COMMAND && HIWORD(wParam) == BN_CLICKED)
            EnableRatioEdits();
        return CConfigPage::DialogProc(message, wParam, lParam);
    }

private:
    void EnableRatioEdits()
    {
        const BOOL other = IsDlgButtonChecked(HWindow, IDC_CFGTOOLS_OTHER) == BST_CHECKED;
        EnableWindow(GetDlgItem(HWindow, IDC_CFGTOOLS_RATIO_X), other);
        EnableWindow(GetDlgItem(HWindow, IDC_CFGTOOLS_RATIO_Y), other);
    }
};

class CAdvancedPage : public CConfigPage
{
public:
    CAdvancedPage(HINSTANCE instance, ViewerPreferences& preferences) : CConfigPage(instance, DLG_CFGPAGE_ADVANCED, preferences) {}

    void Transfer(CTransferInfo& ti) override
    {
        int ignore = Preferences.IgnoreThumbnails ? 1 : 0;
        ti.CheckBox(IDC_CFGADV_CREATE_THUMBNAILS, ignore);
        Preferences.IgnoreThumbnails = ignore != 0;

        int megapixels = Preferences.MaxThumbImgSize;
        ti.EditLine(IDC_CFGADV_THUMB_MP, megapixels);
        if (megapixels >= 0)
            Preferences.MaxThumbImgSize = megapixels;

        int rotate = Preferences.AutoRotate ? 1 : 0;
        ti.CheckBox(IDC_CFGADV_AUTOROTATE, rotate);
        Preferences.AutoRotate = rotate != 0;

        int saveMessage = Preferences.ShowSaveSuccessMessage ? 1 : 0;
        ti.CheckBox(IDC_CFGADV_SAVE, saveMessage);
        Preferences.ShowSaveSuccessMessage = saveMessage != 0;

        int rememberPath = Preferences.RememberSavePath ? 1 : 0;
        ti.CheckBox(IDC_CFGADV_REMEMBER_PATH, rememberPath);
        Preferences.RememberSavePath = rememberPath != 0;
    }
};

DWORD LastConfigurationPage = 0;

// Centers the sheet on its parent and drops the caption's '?' button, as Sally's own sheets do.
int CALLBACK ConfigurationSheetCallback(HWND sheet, UINT message, LPARAM lParam)
{
    if (message == PSCB_PRECREATE)
    {
        auto* extended = reinterpret_cast<DLGTEMPLATE*>(lParam);
        if (reinterpret_cast<WORD*>(extended)[1] == 0xFFFF) // DLGTEMPLATEEX: style follows helpID and exStyle
            reinterpret_cast<DWORD*>(reinterpret_cast<BYTE*>(lParam) + 12)[0] &= ~DS_CONTEXTHELP;
        else
            extended->style &= ~DS_CONTEXTHELP;
    }
    else if (message == PSCB_INITIALIZED)
    {
        HWND parent = GetParent(sheet);
        if (parent != nullptr && DialogsGeneral() != nullptr)
            DialogsGeneral()->MultiMonCenterWindow(sheet, parent, TRUE);
    }
    return 0;
}

} // namespace

bool PromptViewerPreferences(HWND parent, HINSTANCE instance, ViewerPreferences& preferences)
{
    ViewerPreferences edited = preferences;
    CAppearancePage appearance(instance, edited);
    CColorsPage colors(instance, edited, DialogsGui());
    CKeyboardPage keyboard(instance, edited);
    CToolsPage tools(instance, edited);
    CAdvancedPage advanced(instance, edited);

    CPropertyDialog sheet(parent, instance, ViewerText(IDS_CONFIGURATION_TITLE), static_cast<int>(LastConfigurationPage),
                          PSH_USECALLBACK | PSH_NOAPPLYNOW | PSH_HASHELP, nullptr, &LastConfigurationPage,
                          ConfigurationSheetCallback);
    sheet.Add(&appearance);
    sheet.Add(&colors);
    sheet.Add(&keyboard);
    sheet.Add(&tools);
    sheet.Add(&advanced);
    if (sheet.Execute() != IDOK)
        return false;
    preferences = edited;
    return true;
}

} // namespace pictview
