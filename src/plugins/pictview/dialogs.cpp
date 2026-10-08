// SPDX-FileCopyrightText: 2025-2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <mutex>
#include <vector>

#include <wincodec.h>
#include <windowsx.h>
#include <cwchar>
#include <cwctype>
#include <string>

#include "dialogs.h"
#include "plugindarkmode.h"
#include "rename_utils.h"
#include "adapter_actions.h"
#include "print_layout.h"
#include "pictview.rh2"
#include "lang/lang.rh"

namespace pictview
{
namespace
{

HINSTANCE DialogInstance = nullptr;
CSalamanderGeneralAbstract* DialogGeneral = nullptr;
CSalamanderGUIAbstract* DialogGui = nullptr;

void ShowDialogError(HWND parent, UINT textId)
{
    ViewerMessageBox(parent, ViewerText(textId), MB_OK | MB_ICONEXCLAMATION);
}

// Common behavior of PictView's dialogs: centered on the viewer on its monitor.
class CPictViewDialog : public CDialog
{
public:
    // The help ID is the template's, as in the old PictView: the manual maps the dialogs that have
    // a page (Image Properties, EXIF, Screen Capture, Copy To, Print); the others open its start.
    CPictViewDialog(int resourceId, HWND parent, UINT helpId = 0)
        : CDialog(DialogInstance, resourceId, helpId != 0 ? helpId : static_cast<UINT>(resourceId), parent)
    {
    }

protected:
    INT_PTR DialogProc(UINT message, WPARAM wParam, LPARAM lParam) override
    {
        if (message == WM_INITDIALOG && DialogGeneral != nullptr)
            DialogGeneral->MultiMonCenterWindow(HWindow, Parent, TRUE);
        return CDialog::DialogProc(message, wParam, lParam);
    }
};

// Keeps only digits (and one decimal separator when allowed) in an edit or combo box edit.
void KeepNumericCharacters(HWND control, bool comboBox, bool allowSeparator)
{
    const int length = GetWindowTextLengthW(control);
    std::wstring text(static_cast<size_t>(length > 0 ? length : 0) + 1, L'\0');
    GetWindowTextW(control, text.data(), static_cast<int>(text.size()));
    text.resize(wcslen(text.c_str()));

    DWORD start = 0;
    DWORD end = 0;
    SendMessageW(control, comboBox ? CB_GETEDITSEL : EM_GETSEL, reinterpret_cast<WPARAM>(&start), reinterpret_cast<LPARAM>(&end));

    std::wstring kept;
    bool separatorSeen = !allowSeparator;
    for (size_t i = 0; i < text.size(); ++i)
    {
        const wchar_t ch = text[i];
        const bool digit = ch >= L'0' && ch <= L'9';
        const bool separator = !separatorSeen && (ch == L'.' || ch == L',');
        if (digit || separator)
        {
            separatorSeen = separatorSeen || separator;
            kept.push_back(ch);
        }
        else
        {
            if (i < start)
                --start;
            if (i < end)
                --end;
        }
    }
    if (kept != text)
    {
        SetWindowTextW(control, kept.c_str());
        SendMessageW(control, comboBox ? CB_SETEDITSEL : EM_SETSEL, 0, MAKELPARAM(start, end));
    }
}

std::wstring FormatPercent(double percent)
{
    wchar_t text[32] = {};
    swprintf(text, std::size(text), L"%.2f", percent);
    std::wstring result = text;
    while (!result.empty() && result.back() == L'0')
        result.pop_back();
    if (!result.empty() && result.back() == L'.')
        result.pop_back();
    return result;
}

// ------------------------------------------------------------------------------------------
// Zoom To

constexpr int ZoomDialogPresets[] = {6, 12, 25, 50, 75, 100, 125, 150, 200, 400, 600, 800, 1000, 1600};

class CZoomDialog : public CPictViewDialog
{
public:
    CZoomDialog(HWND parent, int& percent) : CPictViewDialog(DLG_ZOOM, parent), m_percent(percent) {}

    void Transfer(CTransferInfo& ti) override
    {
        HWND combo = nullptr;
        if (!ti.GetControl(combo, IDC_ZOOM_ZOOM))
            return;
        if (ti.Type == ttDataToWindow)
        {
            for (const int preset : ZoomDialogPresets)
                SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(std::to_wstring(preset).c_str()));
            SetWindowTextW(combo, FormatPercent(m_percent).c_str());
        }
        else
        {
            std::wstring text = SPLGetDlgItemTextOwned(HWindow, IDC_ZOOM_ZOOM);
            std::replace(text.begin(), text.end(), L',', L'.');
            if (!text.empty()) // an empty field keeps the zoom
                m_percent = static_cast<int>(wcstod(text.c_str(), nullptr) + 0.5);
        }
    }

protected:
    INT_PTR DialogProc(UINT message, WPARAM wParam, LPARAM lParam) override
    {
        if (message == WM_INITDIALOG)
            SendDlgItemMessageW(HWindow, IDC_ZOOM_ZOOM, CB_LIMITTEXT, 7, 0);
        else if (message == WM_COMMAND && HIWORD(wParam) == CBN_EDITUPDATE)
            KeepNumericCharacters(reinterpret_cast<HWND>(lParam), true, true);
        return CPictViewDialog::DialogProc(message, wParam, lParam);
    }

private:
    int& m_percent;
};

// ------------------------------------------------------------------------------------------
// Go To Page

class CPageDialog : public CPictViewDialog
{
public:
    CPageDialog(HWND parent, uint32_t& page, uint32_t pageCount)
        : CPictViewDialog(DLG_PAGE, parent), m_page(page), m_pageCount(pageCount)
    {
    }

    void Transfer(CTransferInfo& ti) override
    {
        HWND combo = nullptr;
        if (!ti.GetControl(combo, IDC_PAGE_PAGE))
            return;
        if (ti.Type == ttDataToWindow)
        {
            for (uint32_t i = 0; i < m_pageCount; ++i)
                SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(std::to_wstring(i + 1).c_str()));
            SetWindowTextW(combo, std::to_wstring(m_page + 1).c_str());
        }
        else
        {
            const long page = wcstol(SPLGetDlgItemTextOwned(HWindow, IDC_PAGE_PAGE).c_str(), nullptr, 10) - 1;
            m_page = static_cast<uint32_t>(std::clamp<long>(page, 0, static_cast<long>(m_pageCount) - 1));
        }
    }

protected:
    INT_PTR DialogProc(UINT message, WPARAM wParam, LPARAM lParam) override
    {
        if (message == WM_INITDIALOG)
            SendDlgItemMessageW(HWindow, IDC_PAGE_PAGE, CB_LIMITTEXT, 5, 0);
        else if (message == WM_COMMAND && HIWORD(wParam) == CBN_EDITUPDATE)
            KeepNumericCharacters(reinterpret_cast<HWND>(lParam), true, false);
        return CPictViewDialog::DialogProc(message, wParam, lParam);
    }

private:
    uint32_t& m_page;
    uint32_t m_pageCount;
};

// ------------------------------------------------------------------------------------------
// Rename

class CRenameDialog : public CPictViewDialog
{
public:
    CRenameDialog(HWND parent, const std::wstring& initialName, bool selectWholeName, std::wstring& result)
        : CPictViewDialog(IDD_RENAMEDIALOG, parent), m_initialName(initialName), m_selectWholeName(selectWholeName),
          m_result(result), m_text(initialName)
    {
    }

    void Validate(CTransferInfo& ti) override
    {
        std::wstring pattern = SPLGetDlgItemTextOwned(HWindow, IDE_PATH);
        TrimRenameFileName(pattern);
        UINT error = 0;
        std::wstring name;
        if (pattern.empty())
            error = IDS_RENAME_EMPTY;
        else if (RenamePatternContainsInvalidCharacter(pattern))
            error = IDS_RENAME_BAD_MASK;
        else
        {
            // Sally's own mask rules ("a.txt" + "*.cpp" -> "a.cpp"), as in its Rename dialog.
            name = DialogGeneral != nullptr ? SPLMaskNameOwned(DialogGeneral, m_initialName.c_str(), pattern.c_str())
                                            : pattern;
            TrimRenameFileName(name);
            if (name.empty())
                error = IDS_RENAME_MASK_EMPTY;
            else if (FileNameContainsInvalidRenameCharacter(name))
                error = IDS_RENAME_BAD_NAME;
        }
        if (error != 0)
        {
            ShowDialogError(HWindow, error);
            ti.ErrorOn(IDE_PATH);
            return;
        }
        m_result = std::move(name);
    }

    void Transfer(CTransferInfo& ti) override
    {
        ti.EditLine(IDE_PATH, m_text);
    }

protected:
    INT_PTR DialogProc(UINT message, WPARAM wParam, LPARAM lParam) override
    {
        switch (message)
        {
        case WM_INITDIALOG:
            if (DialogGui != nullptr)
                DialogGui->SetSubjectTruncatedText(GetDlgItem(HWindow, IDS_SUBJECT), ViewerText(IDS_RENAME_TO),
                                                   m_initialName.c_str(), FALSE, FALSE);
            break;

        case WM_SHOWWINDOW:
            if (m_firstShow)
            {
                m_firstShow = false;
                // Select the name without its extension unless Sally selects whole names.
                size_t end = m_initialName.size();
                const size_t dot = m_initialName.find_last_of(L'.');
                if (!m_selectWholeName && dot != std::wstring::npos && dot != 0)
                    end = dot;
                SendDlgItemMessageW(HWindow, IDE_PATH, EM_SETSEL, 0, static_cast<LPARAM>(end));
            }
            break;
        }
        return CPictViewDialog::DialogProc(message, wParam, lParam);
    }

private:
    const std::wstring& m_initialName;
    bool m_selectWholeName;
    std::wstring& m_result;
    std::wstring m_text;
    bool m_firstShow = true;
};

// ------------------------------------------------------------------------------------------
// Screen Capture

class CCaptureDialog : public CPictViewDialog
{
public:
    CCaptureDialog(HWND parent, ViewerCaptureOptions& options)
        : CPictViewDialog(DLG_CAPTURE, parent, DLG_CAPTURE), m_options(options)
    {
    }

    void Validate(CTransferInfo& ti) override
    {
        if (IsDlgButtonChecked(HWindow, IDC_CAPTURE_HOTKEY) == BST_CHECKED)
        {
            // The key must be free now; registering it for the viewer later then works too.
            const WORD hotKey = static_cast<WORD>(SendDlgItemMessageW(HWindow, IDC_CAPTURE_HOTKEY_VAL, HKM_GETHOTKEY, 0, 0));
            UINT modifiers = 0;
            if ((HIBYTE(hotKey) & HOTKEYF_SHIFT) != 0)
                modifiers |= MOD_SHIFT;
            if ((HIBYTE(hotKey) & HOTKEYF_CONTROL) != 0)
                modifiers |= MOD_CONTROL;
            if ((HIBYTE(hotKey) & HOTKEYF_ALT) != 0)
                modifiers |= MOD_ALT;
            constexpr int probeId = 0x5056; // 'PV'
            if (hotKey == 0 || !RegisterHotKey(HWindow, probeId, modifiers, LOBYTE(hotKey)))
            {
                ShowDialogError(HWindow, IDS_BADHOTKEY);
                ti.ErrorOn(IDC_CAPTURE_HOTKEY_VAL);
                return;
            }
            UnregisterHotKey(HWindow, probeId);
        }
        else
        {
            int seconds = 0;
            ti.EditLine(IDC_CAPTURE_TIMER_VAL, seconds);
            if (ti.IsGood() && (seconds < 1 || seconds > 360))
            {
                ShowDialogError(HWindow, IDS_BADTIMER);
                ti.ErrorOn(IDC_CAPTURE_TIMER_VAL);
            }
        }
    }

    void Transfer(CTransferInfo& ti) override
    {
        if (ti.Type == ttDataToWindow && GetSystemMetrics(SM_CMONITORS) < 2)
        {
            EnableWindow(GetDlgItem(HWindow, IDC_CAPTURE_VIRTUAL), FALSE);
            if (m_options.Scope == ViewerCaptureScope::VirtualScreen)
                m_options.Scope = ViewerCaptureScope::Desktop;
        }

        int scope = static_cast<int>(m_options.Scope);
        ti.RadioButton(IDC_CAPTURE_FULL, static_cast<int>(ViewerCaptureScope::Desktop), scope);
        ti.RadioButton(IDC_CAPTURE_APPL, static_cast<int>(ViewerCaptureScope::Application), scope);
        ti.RadioButton(IDC_CAPTURE_WINDOW, static_cast<int>(ViewerCaptureScope::Window), scope);
        ti.RadioButton(IDC_CAPTURE_CLIENT, static_cast<int>(ViewerCaptureScope::ClientArea), scope);
        ti.RadioButton(IDC_CAPTURE_VIRTUAL, static_cast<int>(ViewerCaptureScope::VirtualScreen), scope);
        m_options.Scope = static_cast<ViewerCaptureScope>(scope);

        int trigger = static_cast<int>(m_options.Trigger);
        ti.RadioButton(IDC_CAPTURE_HOTKEY, static_cast<int>(ViewerCaptureTrigger::HotKey), trigger);
        ti.RadioButton(IDC_CAPTURE_TIMER, static_cast<int>(ViewerCaptureTrigger::Timer), trigger);
        m_options.Trigger = static_cast<ViewerCaptureTrigger>(trigger);

        int cursor = m_options.IncludeCursor ? 1 : 0;
        ti.CheckBox(IDC_CAPTURE_CURSOR, cursor);
        m_options.IncludeCursor = cursor != 0;

        if (ti.Type == ttDataToWindow)
            SendDlgItemMessageW(HWindow, IDC_CAPTURE_HOTKEY_VAL, HKM_SETHOTKEY, m_options.HotKey, 0);
        else
            m_options.HotKey = static_cast<WORD>(SendDlgItemMessageW(HWindow, IDC_CAPTURE_HOTKEY_VAL, HKM_GETHOTKEY, 0, 0));

        ti.EditLine(IDC_CAPTURE_TIMER_VAL, m_options.TimerSeconds);

        if (ti.Type == ttDataToWindow)
            EnableTriggerControls();
    }

protected:
    INT_PTR DialogProc(UINT message, WPARAM wParam, LPARAM lParam) override
    {
        switch (message)
        {
        case WM_INITDIALOG:
            AddScopeHints();
            break;

        case WM_COMMAND:
            if (HIWORD(wParam) == BN_CLICKED && (LOWORD(wParam) == IDC_CAPTURE_HOTKEY || LOWORD(wParam) == IDC_CAPTURE_TIMER))
                EnableTriggerControls();
            break;
        }
        return CPictViewDialog::DialogProc(message, wParam, lParam);
    }

private:
    void EnableTriggerControls()
    {
        const BOOL hotKey = IsDlgButtonChecked(HWindow, IDC_CAPTURE_HOTKEY) == BST_CHECKED;
        EnableWindow(GetDlgItem(HWindow, IDC_CAPTURE_HOTKEY_VAL), hotKey);
        EnableWindow(GetDlgItem(HWindow, IDC_CAPTURE_TIMER_VAL), !hotKey);
    }

    // Tooltips that explain the less obvious capture scopes.
    void AddScopeHints()
    {
        HWND tooltip = CreateWindowExW(0, TOOLTIPS_CLASSW, nullptr, TTS_ALWAYSTIP, CW_USEDEFAULT, CW_USEDEFAULT,
                                       CW_USEDEFAULT, CW_USEDEFAULT, HWindow, nullptr, DialogInstance, nullptr);
        if (tooltip == nullptr)
            return;
        PluginDarkMode_ApplyTooltipTheme(tooltip);
        const struct
        {
            int Control;
            UINT Text;
        } hints[] = {{IDC_CAPTURE_APPL, IDS_CAPTHINT_APPL}, {IDC_CAPTURE_WINDOW, IDS_CAPTHINT_WINDOW}, {IDC_CAPTURE_CLIENT, IDS_CAPTHINT_CLIENT}};
        for (const auto& hint : hints)
        {
            TTTOOLINFOW info = {};
            info.cbSize = sizeof(info);
            info.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
            info.hwnd = HWindow;
            info.uId = reinterpret_cast<UINT_PTR>(GetDlgItem(HWindow, hint.Control));
            info.lpszText = const_cast<wchar_t*>(ViewerText(hint.Text));
            SendMessageW(tooltip, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&info));
        }
    }

    ViewerCaptureOptions& m_options;
};

// ------------------------------------------------------------------------------------------
// Copy File To

constexpr int CopyToRadioIds[] = {IDC_COPYTO_RB_1, IDC_COPYTO_RB_2, IDC_COPYTO_RB_3, IDC_COPYTO_RB_4, IDC_COPYTO_RB_5};
constexpr int CopyToEditIds[] = {IDC_COPYTO_EL_1, IDC_COPYTO_EL_2, IDC_COPYTO_EL_3, IDC_COPYTO_EL_4, IDC_COPYTO_EL_5};
constexpr int CopyToBrowseIds[] = {IDC_COPYTO_BR_1, IDC_COPYTO_BR_2, IDC_COPYTO_BR_3, IDC_COPYTO_BR_4, IDC_COPYTO_BR_5};
static_assert(std::size(CopyToRadioIds) == ViewerCopyToHistoryEntryCount, "one template row per remembered target");

class CCopyToDialog : public CPictViewDialog
{
public:
    CCopyToDialog(HWND parent, const std::wstring& sourcePath, ViewerCopyToHistory& history, std::wstring& targetPath)
        : CPictViewDialog(IDD_COPYTO, parent, IDD_COPYTO), m_sourcePath(sourcePath), m_history(history), m_targetPath(targetPath)
    {
    }

    void Validate(CTransferInfo& ti) override
    {
        int line = 0;
        for (int i = 0; i < static_cast<int>(std::size(CopyToRadioIds)); ++i)
            ti.RadioButton(CopyToRadioIds[i], i, line);

        std::wstring directory = SPLGetDlgItemTextOwned(HWindow, CopyToEditIds[line]);
        const size_t separator = m_sourcePath.find_last_of(L"\\/");
        if (separator == std::wstring::npos)
        {
            ti.ErrorOn(CopyToEditIds[line]);
            return;
        }
        const std::wstring sourceDirectory = m_sourcePath.substr(0, separator);
        const std::wstring sourceName = m_sourcePath.substr(separator + 1);

        // Sally resolves the entry: relative to the viewed file's folder, with its own messages.
        int errorTextId = 0;
        if (!SPLSalGetFullNameOwned(DialogGeneral, directory, &errorTextId, sourceDirectory.c_str()))
        {
            std::wstring text;
            if (errorTextId == GFN_EMPTYNAMENOTALLOWED || !SPLGetGFNErrorTextOwned(DialogGeneral, errorTextId, text))
                text = ViewerText(IDS_SPECIFYPATH);
            ViewerMessageBox(HWindow, text.c_str(), MB_OK | MB_ICONEXCLAMATION);
            ti.ErrorOn(CopyToEditIds[line]);
            return;
        }

        std::wstring target = directory;
        SPLSalPathAppendOwned(target, sourceName.c_str());
        if (_wcsicmp(target.c_str(), m_sourcePath.c_str()) == 0)
        {
            ShowDialogError(HWindow, IDS_COPYTO_SAME_FOLDER);
            ti.ErrorOn(CopyToEditIds[line]);
            return;
        }
        m_targetPath = std::move(target);
    }

    void Transfer(CTransferInfo& ti) override
    {
        for (int i = 0; i < static_cast<int>(std::size(CopyToRadioIds)); ++i)
        {
            ti.RadioButton(CopyToRadioIds[i], i, m_history.LastIndex);
            ti.EditLine(CopyToEditIds[i], m_history.Targets[static_cast<size_t>(i)]);
        }
        if (ti.Type == ttDataToWindow)
            EnableLines();
    }

protected:
    INT_PTR DialogProc(UINT message, WPARAM wParam, LPARAM lParam) override
    {
        if (message == WM_COMMAND && HIWORD(wParam) == BN_CLICKED)
        {
            EnableLines();
            for (size_t i = 0; i < std::size(CopyToBrowseIds); ++i)
            {
                if (LOWORD(wParam) != CopyToBrowseIds[i])
                    continue;
                const std::wstring initial = SPLGetDlgItemTextOwned(HWindow, CopyToEditIds[i]);
                std::wstring chosen;
                if (SPLGetTargetDirectoryOwned(DialogGeneral, HWindow, HWindow, initial.c_str(),
                                               ViewerText(IDS_SELECTTARGETDIR), chosen, FALSE, initial.c_str()))
                    SetDlgItemTextW(HWindow, CopyToEditIds[i], chosen.c_str());
            }
        }
        return CPictViewDialog::DialogProc(message, wParam, lParam);
    }

private:
    void EnableLines()
    {
        for (size_t i = 0; i < std::size(CopyToRadioIds); ++i)
        {
            const BOOL enabled = IsDlgButtonChecked(HWindow, CopyToRadioIds[i]) == BST_CHECKED;
            EnableWindow(GetDlgItem(HWindow, CopyToEditIds[i]), enabled);
            EnableWindow(GetDlgItem(HWindow, CopyToBrowseIds[i]), enabled);
        }
    }

    const std::wstring& m_sourcePath;
    ViewerCopyToHistory& m_history;
    std::wstring& m_targetPath;
};

// ------------------------------------------------------------------------------------------
// About

class CAboutDialog : public CPictViewDialog
{
public:
    explicit CAboutDialog(HWND parent) : CPictViewDialog(DLG_ABOUT, parent) {}

protected:
    INT_PTR DialogProc(UINT message, WPARAM wParam, LPARAM lParam) override
    {
        if (message == WM_INITDIALOG)
        {
            SendDlgItemMessageW(HWindow, IDC_ABOUT_ICON, STM_SETIMAGE, IMAGE_ICON,
                                reinterpret_cast<LPARAM>(LoadIconW(DialogInstance, MAKEINTRESOURCEW(IDI_WINDOW_ICON))));
            const std::wstring titleFormat = SPLGetDlgItemTextOwned(HWindow, IDC_ABOUT_TITLE);
            const std::wstring version = L"" VERSINFO_VERSION;
            std::wstring title = titleFormat;
            const size_t placeholder = title.find(L"%s");
            if (placeholder != std::wstring::npos)
                title.replace(placeholder, 2, version);
            SetDlgItemTextW(HWindow, IDC_ABOUT_TITLE, title.c_str());
            SetDlgItemTextW(HWindow, IDC_ABOUT_COPYRIGHT, VERSINFO_COPYRIGHT);
            if (DialogGui != nullptr)
            {
                DialogGui->AttachStaticText(HWindow, IDC_ABOUT_TITLE, STF_BOLD);
                DialogGui->AttachStaticText(HWindow, IDC_ABOUT_PVW32, STF_BOLD);
                CGUIHyperLinkAbstract* link = DialogGui->AttachHyperLink(HWindow, IDC_ABOUT_WWW, STF_UNDERLINE | STF_HYPERLINK_COLOR);
                if (link != nullptr)
                    link->SetActionOpen(L"https://sally-filemanager.app/");
            }
            // The closed engine's support address is gone with the engine.
            ShowWindow(GetDlgItem(HWindow, IDC_ABOUT_EMAIL), SW_HIDE);
            ShowWindow(GetDlgItem(HWindow, IDC_STATIC_6), SW_HIDE);
        }
        return CPictViewDialog::DialogProc(message, wParam, lParam);
    }
};

// ------------------------------------------------------------------------------------------
// Image Properties

std::wstring FormatPluralCount(UINT formatId, uint64_t count)
{
    // Sally expands the {!}...{|1|s} plural markup for the current language.
    const std::wstring format = ViewerText(formatId);
    CQuadWord value(static_cast<DWORD>(count & 0xFFFFFFFFull), static_cast<DWORD>(count >> 32));
    const std::wstring expanded = DialogGeneral != nullptr ? SPLExpandPluralStringOwned(DialogGeneral, format.c_str(), 1, &value) : format;
    std::wstring text = expanded;
    const size_t placeholder = text.find(L"%d");
    if (placeholder != std::wstring::npos)
        text.replace(placeholder, 2, std::to_wstring(count));
    return text;
}

bool IsIndexedPixelFormat(const GUID& format)
{
    return IsEqualGUID(format, GUID_WICPixelFormat1bppIndexed) || IsEqualGUID(format, GUID_WICPixelFormat2bppIndexed) ||
           IsEqualGUID(format, GUID_WICPixelFormat4bppIndexed) || IsEqualGUID(format, GUID_WICPixelFormat8bppIndexed);
}

bool IsCmykPixelFormat(const GUID& format)
{
    return IsEqualGUID(format, GUID_WICPixelFormat32bppCMYK) || IsEqualGUID(format, GUID_WICPixelFormat64bppCMYK) ||
           IsEqualGUID(format, GUID_WICPixelFormat40bppCMYKAlpha) || IsEqualGUID(format, GUID_WICPixelFormat80bppCMYKAlpha);
}

std::wstring DescribeColors(const ImagePropertiesInfo& info)
{
    if (IsCmykPixelFormat(info.PixelFormat))
        return L"CMYK";
    if (info.BitsPerPixel == 0)
        return ViewerText(IDS_UNKNOWN);
    if (IsIndexedPixelFormat(info.PixelFormat))
        return std::to_wstring(1u << info.BitsPerPixel);
    if (info.ChannelCount == 1)
    {
        std::wstring text = ViewerText(IDS_IMGPROP_GRAY_NBIT);
        const size_t placeholder = text.find(L"%d");
        if (placeholder != std::wstring::npos)
            text.replace(placeholder, 2, std::to_wstring(info.BitsPerPixel));
        return text;
    }
    // The old dialog's technical names (not translated there either).
    if (info.BitsPerPixel == 15 || IsEqualGUID(info.PixelFormat, GUID_WICPixelFormat16bppBGR555))
        return L"HiColor 15Bit";
    if (info.BitsPerPixel == 16)
        return L"HiColor 16Bit";
    return L"TrueColor " + std::to_wstring(info.BitsPerPixel) + L"Bit";
}

class CImagePropertiesDialog : public CPictViewDialog
{
public:
    CImagePropertiesDialog(HWND parent, const ImagePropertiesInfo& info)
        : CPictViewDialog(DLG_IMGPROP, parent, DLG_IMGPROP), m_info(info)
    {
    }

protected:
    INT_PTR DialogProc(UINT message, WPARAM wParam, LPARAM lParam) override
    {
        if (message == WM_INITDIALOG)
        {
            std::wstring page;
            if (m_info.Animation)
            {
                SetDlgItemTextW(HWindow, IDC_IMGPROP_PAGENUM_LBL, ViewerText(IDS_FRAMES));
                page = std::to_wstring(m_info.PageCount);
            }
            else
            {
                page = ViewerText(IDS_OF);
                const size_t first = page.find(L"%d");
                if (first != std::wstring::npos)
                    page.replace(first, 2, std::to_wstring(m_info.Page + 1));
                const size_t second = page.find(L"%d");
                if (second != std::wstring::npos)
                    page.replace(second, 2, std::to_wstring(m_info.PageCount));
            }
            SetDlgItemTextW(HWindow, IDC_IMGPROP_PAGENUM, page.c_str());
            SetDlgItemTextW(HWindow, IDC_IMGPROP_WIDTH, FormatPluralCount(IDS_PIXELS, m_info.Width).c_str());
            SetDlgItemTextW(HWindow, IDC_IMGPROP_HEIGHT, FormatPluralCount(IDS_PIXELS, m_info.Height).c_str());
            SetDlgItemTextW(HWindow, IDC_IMGPROP_COLORS, DescribeColors(m_info).c_str());
            SetDlgItemTextW(HWindow, IDC_IMGPROP_SIZE, FormatPluralCount(IDS_BYTES, m_info.MemoryBytes).c_str());
            SetDlgItemTextW(HWindow, IDC_IMGPROP_FSIZE,
                            m_info.FileBytes != 0 ? FormatPluralCount(IDS_BYTES, m_info.FileBytes).c_str() : ViewerText(IDS_UNKNOWN));
            std::wstring dpi = ViewerText(IDS_UNKNOWN);
            if (m_info.DpiX > 0.0 && m_info.DpiY > 0.0)
                dpi = std::to_wstring(static_cast<unsigned>(m_info.DpiX + 0.5)) + L"x" + std::to_wstring(static_cast<unsigned>(m_info.DpiY + 0.5));
            SetDlgItemTextW(HWindow, IDC_IMGPROP_DPI, dpi.c_str());
            SetDlgItemTextW(HWindow, IDC_IMGPROP_FMT, m_info.Format.c_str());
            SetDlgItemTextW(HWindow, IDC_IMGPROP_COMPR, m_info.Compression.empty() ? ViewerText(IDS_UNKNOWN) : m_info.Compression.c_str());
            SetDlgItemTextW(HWindow, IDC_IMGPROP_COMMENT, m_info.Comment.c_str());
        }
        return CPictViewDialog::DialogProc(message, wParam, lParam);
    }

private:
    const ImagePropertiesInfo& m_info;
};

// ------------------------------------------------------------------------------------------
// EXIF

class CExifDialog : public CPictViewDialog
{
public:
    CExifDialog(HWND parent, const std::vector<ImageExifEntry>& entries)
        : CPictViewDialog(DLG_IMGEXIF, parent, DLG_IMGEXIF), m_entries(entries), m_preferences(GetMetadataDetailsPreferences())
    {
    }

    void Transfer(CTransferInfo& ti) override
    {
        if (ti.Type == ttDataFromWindow)
        {
            m_preferences.GroupHighlights = IsDlgButtonChecked(HWindow, IDC_IMGEXIF_GROUP) == BST_CHECKED;
            m_commit = true;
        }
    }

protected:
    INT_PTR DialogProc(UINT message, WPARAM wParam, LPARAM lParam) override
    {
        switch (message)
        {
        case WM_INITDIALOG:
        {
            m_list = GetDlgItem(HWindow, IDC_IMGEXIF_LIST);
            ListView_SetExtendedListViewStyle(m_list, ListView_GetExtendedListViewStyle(m_list) | LVS_EX_FULLROWSELECT);
            InsertColumn(0, IDS_EXIF_TAG);
            InsertColumn(1, IDS_EXIF_VALUE);
            CheckDlgButton(HWindow, IDC_IMGEXIF_GROUP, m_preferences.GroupHighlights ? BST_CHECKED : BST_UNCHECKED);
            RememberLayout();
            SetWindowPos(HWindow, nullptr, 0, 0, (std::max)(m_preferences.WindowWidth, static_cast<int>(m_minimum.cx)),
                         (std::max)(m_preferences.WindowHeight, static_cast<int>(m_minimum.cy)), SWP_NOZORDER | SWP_NOMOVE);
            FillList();
            ListView_SetColumnWidth(m_list, 0, LVSCW_AUTOSIZE);
            ListView_SetColumnWidth(m_list, 1, LVSCW_AUTOSIZE_USEHEADER);
            break; // CPictViewDialog centers the dialog on the viewer
        }

        case WM_SIZE:
            Layout(LOWORD(lParam), HIWORD(lParam));
            break;

        case WM_GETMINMAXINFO:
            if (m_minimum.cx > 0)
            {
                reinterpret_cast<MINMAXINFO*>(lParam)->ptMinTrackSize.x = m_minimum.cx;
                reinterpret_cast<MINMAXINFO*>(lParam)->ptMinTrackSize.y = m_minimum.cy;
            }
            break;

        case WM_COMMAND:
            if (HIWORD(wParam) == BN_CLICKED)
            {
                switch (LOWORD(wParam))
                {
                case IDC_IMGEXIF_GROUP:
                    FillList();
                    return TRUE;
                case IDC_IMGEXIF_HIGHLIGHT:
                    ToggleHighlight();
                    return TRUE;
                case IDC_IMGEXIF_COPY:
                {
                    const int entry = FocusedEntry();
                    if (entry >= 0 && DialogGeneral != nullptr)
                        DialogGeneral->CopyTextToClipboard(m_entries[static_cast<size_t>(entry)].Value.c_str(), -1, FALSE, HWindow);
                    return TRUE;
                }
                }
            }
            break;

        case WM_NOTIFY:
            if (wParam == IDC_IMGEXIF_LIST)
            {
                LRESULT result = 0;
                if (HandleListNotify(reinterpret_cast<NMHDR*>(lParam), result))
                {
                    SetWindowLongPtrW(HWindow, DWLP_MSGRESULT, result);
                    return TRUE;
                }
            }
            break;

        case WM_DESTROY:
        {
            RECT rect = {};
            GetWindowRect(HWindow, &rect);
            m_preferences.WindowWidth = rect.right - rect.left;
            m_preferences.WindowHeight = rect.bottom - rect.top;
            ViewerMetadataDetailsPreferences stored = GetMetadataDetailsPreferences();
            stored.WindowWidth = m_preferences.WindowWidth;
            stored.WindowHeight = m_preferences.WindowHeight;
            if (m_commit) // OK keeps the highlights; Cancel only the window size
            {
                stored.GroupHighlights = m_preferences.GroupHighlights;
                stored.HighlightedTags = m_preferences.HighlightedTags;
            }
            SetMetadataDetailsPreferences(stored);
            break;
        }
        }
        return CPictViewDialog::DialogProc(message, wParam, lParam);
    }

private:
    void InsertColumn(int index, UINT textId)
    {
        LVCOLUMNW column = {};
        column.mask = LVCF_TEXT | LVCF_SUBITEM;
        column.pszText = const_cast<wchar_t*>(ViewerText(textId));
        column.iSubItem = index;
        ListView_InsertColumn(m_list, index, &column);
    }

    bool IsHighlighted(uint16_t tag) const
    {
        const auto& tags = m_preferences.HighlightedTags;
        return std::find(tags.begin(), tags.end(), tag) != tags.end();
    }

    int FocusedEntry() const
    {
        const int item = ListView_GetNextItem(m_list, -1, LVNI_FOCUSED);
        if (item < 0)
            return -1;
        LVITEMW lvi = {};
        lvi.mask = LVIF_PARAM;
        lvi.iItem = item;
        ListView_GetItem(m_list, &lvi);
        return lvi.lParam >= 0 && static_cast<size_t>(lvi.lParam) < m_entries.size() ? static_cast<int>(lvi.lParam) : -1;
    }

    // Highlighted tags first when grouped, otherwise in file order; the focus follows its entry.
    void FillList()
    {
        const int focusedEntry = FocusedEntry();
        SendMessageW(m_list, WM_SETREDRAW, FALSE, 0);
        ListView_DeleteAllItems(m_list);
        const bool grouped = IsDlgButtonChecked(HWindow, IDC_IMGEXIF_GROUP) == BST_CHECKED;
        int row = 0;
        int focusRow = 0;
        for (int pass = 0; pass < (grouped ? 2 : 1); ++pass)
        {
            for (size_t i = 0; i < m_entries.size(); ++i)
            {
                const bool highlighted = IsHighlighted(m_entries[i].Tag);
                if (grouped && highlighted != (pass == 0))
                    continue;
                LVITEMW lvi = {};
                lvi.mask = LVIF_PARAM | LVIF_TEXT;
                lvi.iItem = row;
                lvi.lParam = static_cast<LPARAM>(i);
                lvi.pszText = const_cast<wchar_t*>(m_entries[i].Name.c_str());
                ListView_InsertItem(m_list, &lvi);
                ListView_SetItemText(m_list, row, 1, const_cast<wchar_t*>(m_entries[i].Value.c_str()));
                if (static_cast<int>(i) == focusedEntry)
                    focusRow = row;
                ++row;
            }
        }
        if (row > 0)
        {
            ListView_SetItemState(m_list, focusRow, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
            ListView_EnsureVisible(m_list, focusRow, FALSE);
        }
        SendMessageW(m_list, WM_SETREDRAW, TRUE, 0);
        ShowFocusedInfo();
    }

    void ToggleHighlight()
    {
        const int entry = FocusedEntry();
        if (entry < 0)
            return;
        auto& tags = m_preferences.HighlightedTags;
        const uint16_t tag = m_entries[static_cast<size_t>(entry)].Tag;
        const auto found = std::find(tags.begin(), tags.end(), tag);
        if (found == tags.end())
            tags.push_back(tag);
        else
            tags.erase(found);
        FillList();
    }

    void ShowFocusedInfo()
    {
        const int entry = FocusedEntry();
        if (entry < 0)
        {
            SetDlgItemTextW(HWindow, IDC_IMGEXIF_INFO, L"");
            return;
        }
        const ImageExifEntry& item = m_entries[static_cast<size_t>(entry)];
        wchar_t tag[16] = {};
        swprintf(tag, std::size(tag), L"0x%04X", static_cast<unsigned>(item.Tag));
        const std::wstring info = item.Name + L" (" + tag + L", " + item.Group + L")\r\n" + item.Value;
        SetDlgItemTextW(HWindow, IDC_IMGEXIF_INFO, info.c_str());
        CheckDlgButton(HWindow, IDC_IMGEXIF_HIGHLIGHT, IsHighlighted(item.Tag) ? BST_CHECKED : BST_UNCHECKED);
    }

    bool HandleListNotify(NMHDR* header, LRESULT& result)
    {
        switch (header->code)
        {
        case NM_CUSTOMDRAW:
        {
            auto* draw = reinterpret_cast<NMLVCUSTOMDRAW*>(header);
            if (draw->nmcd.dwDrawStage == CDDS_PREPAINT)
            {
                result = CDRF_NOTIFYITEMDRAW;
                return true;
            }
            if (draw->nmcd.dwDrawStage == CDDS_ITEMPREPAINT)
            {
                const size_t entry = static_cast<size_t>(draw->nmcd.lItemlParam);
                if (entry < m_entries.size() && IsHighlighted(m_entries[entry].Tag))
                {
                    draw->clrTextBk = RGB(255, 255, 0);
                    draw->clrText = RGB(0, 0, 0);
                    result = CDRF_NEWFONT;
                    return true;
                }
            }
            return false;
        }

        case LVN_ITEMCHANGED:
            if ((reinterpret_cast<NMLISTVIEW*>(header)->uNewState & LVIS_FOCUSED) != 0)
                ShowFocusedInfo();
            return false;

        case NM_DBLCLK:
            PostMessageW(HWindow, WM_COMMAND, MAKEWPARAM(IDC_IMGEXIF_HIGHLIGHT, BN_CLICKED), 0);
            return false;

        case NM_RCLICK:
        {
            const DWORD position = GetMessagePos();
            ShowContextMenu(GET_X_LPARAM(position), GET_Y_LPARAM(position));
            result = TRUE;
            return true;
        }

        case LVN_KEYDOWN:
        {
            const WORD key = reinterpret_cast<NMLVKEYDOWN*>(header)->wVKey;
            if (key == VK_APPS || (key == VK_F10 && (GetKeyState(VK_SHIFT) & 0x8000) != 0))
            {
                RECT rect = {};
                const int item = ListView_GetNextItem(m_list, -1, LVNI_FOCUSED);
                if (item >= 0)
                    ListView_GetItemRect(m_list, item, &rect, LVIR_LABEL);
                POINT point = {rect.left, rect.bottom};
                ClientToScreen(m_list, &point);
                ShowContextMenu(point.x, point.y);
            }
            return false;
        }
        }
        return false;
    }

    // Highlight (default), Copy, Group highlighted items - the dialog's own buttons.
    void ShowContextMenu(int x, int y)
    {
        if (DialogGui == nullptr || FocusedEntry() < 0)
            return;
        CGUIMenuPopupAbstract* popup = DialogGui->CreateMenuPopup();
        if (popup == nullptr)
            return;
        const int buttons[] = {IDC_IMGEXIF_HIGHLIGHT, IDC_IMGEXIF_COPY, 0, IDC_IMGEXIF_GROUP};
        for (const int button : buttons)
        {
            MENU_ITEM_INFO item = {};
            std::wstring text;
            if (button == 0)
            {
                item.Mask = MENU_MASK_TYPE;
                item.Type = MENU_TYPE_SEPARATOR;
            }
            else
            {
                text = SPLGetDlgItemTextOwned(HWindow, button);
                item.Mask = MENU_MASK_TYPE | MENU_MASK_STRING | MENU_MASK_ID | MENU_MASK_STATE;
                item.Type = MENU_TYPE_STRING;
                item.String = text.data();
                item.ID = static_cast<DWORD>(button);
                item.State = (button == IDC_IMGEXIF_HIGHLIGHT ? MENU_STATE_DEFAULT : 0) |
                             (IsDlgButtonChecked(HWindow, button) == BST_CHECKED ? MENU_STATE_CHECKED : 0);
            }
            popup->InsertItem(0xFFFFFFFF, TRUE, &item);
        }
        const DWORD command = popup->Track(MENU_TRACK_RETURNCMD | MENU_TRACK_RIGHTBUTTON, x, y, HWindow, nullptr);
        DialogGui->DestroyMenuPopup(popup);
        if (command == IDC_IMGEXIF_GROUP)
            CheckDlgButton(HWindow, IDC_IMGEXIF_GROUP, IsDlgButtonChecked(HWindow, IDC_IMGEXIF_GROUP) == BST_CHECKED ? BST_UNCHECKED : BST_CHECKED);
        if (command != 0)
            PostMessageW(HWindow, WM_COMMAND, MAKEWPARAM(command, BN_CLICKED), 0);
    }

    // The list and the info box stretch; the buttons keep their distance to the bottom (and
    // the right edge, except Group on the left).
    void RememberLayout()
    {
        RECT window = {};
        GetWindowRect(HWindow, &window);
        m_minimum = {window.right - window.left, window.bottom - window.top};
        RECT client = {};
        GetClientRect(HWindow, &client);
        m_layoutClient = client;
        for (const int id : LayoutButtons)
        {
            RECT rect = {};
            GetWindowRect(GetDlgItem(HWindow, id), &rect);
            MapWindowPoints(nullptr, HWindow, reinterpret_cast<POINT*>(&rect), 2);
            m_buttonRects.push_back(rect);
        }
        GetWindowRect(m_list, &m_listRect);
        MapWindowPoints(nullptr, HWindow, reinterpret_cast<POINT*>(&m_listRect), 2);
        GetWindowRect(GetDlgItem(HWindow, IDC_IMGEXIF_INFO), &m_infoRect);
        MapWindowPoints(nullptr, HWindow, reinterpret_cast<POINT*>(&m_infoRect), 2);
    }

    void Layout(int width, int height)
    {
        if (m_buttonRects.empty())
            return;
        const int dx = width - (m_layoutClient.right - m_layoutClient.left);
        const int dy = height - (m_layoutClient.bottom - m_layoutClient.top);
        SetWindowPos(m_list, nullptr, 0, 0, m_listRect.right - m_listRect.left + dx, m_listRect.bottom - m_listRect.top + dy,
                     SWP_NOZORDER | SWP_NOMOVE);
        SetWindowPos(GetDlgItem(HWindow, IDC_IMGEXIF_INFO), nullptr, m_infoRect.left, m_infoRect.top + dy,
                     m_infoRect.right - m_infoRect.left + dx, m_infoRect.bottom - m_infoRect.top, SWP_NOZORDER);
        for (size_t i = 0; i < m_buttonRects.size(); ++i)
        {
            const RECT& rect = m_buttonRects[i];
            const int x = LayoutButtons[i] == IDC_IMGEXIF_GROUP ? rect.left : rect.left + dx;
            SetWindowPos(GetDlgItem(HWindow, LayoutButtons[i]), nullptr, x, rect.top + dy, 0, 0, SWP_NOZORDER | SWP_NOSIZE);
        }
        InvalidateRect(HWindow, nullptr, TRUE);
    }

    static constexpr int LayoutButtons[] = {IDC_IMGEXIF_GROUP, IDC_IMGEXIF_HIGHLIGHT, IDC_IMGEXIF_COPY, IDOK, IDCANCEL, IDHELP};

    const std::vector<ImageExifEntry>& m_entries;
    ViewerMetadataDetailsPreferences m_preferences;
    HWND m_list = nullptr;
    bool m_commit = false;
    SIZE m_minimum = {};
    RECT m_layoutClient = {};
    RECT m_listRect = {};
    RECT m_infoRect = {};
    std::vector<RECT> m_buttonRects;
};

// ------------------------------------------------------------------------------------------
// Confirm File Overwrite

// "size, date, time" of a file, formatted the way Sally shows them.
std::wstring DescribeFile(const wchar_t* path)
{
    WIN32_FILE_ATTRIBUTE_DATA data = {};
    if (path == nullptr || !GetFileAttributesExW(path, GetFileExInfoStandard, &data))
        return std::wstring();
    CQuadWord size(data.nFileSizeLow, data.nFileSizeHigh);
    std::wstring text = DialogGeneral != nullptr ? SPLNumberToStrOwned(DialogGeneral, size)
                                                 : std::to_wstring((static_cast<uint64_t>(data.nFileSizeHigh) << 32) | data.nFileSizeLow);
    FILETIME local = {};
    SYSTEMTIME time = {};
    if (FileTimeToLocalFileTime(&data.ftLastWriteTime, &local) && FileTimeToSystemTime(&local, &time))
    {
        wchar_t date[64] = {};
        wchar_t clock[64] = {};
        if (GetDateFormatW(LOCALE_USER_DEFAULT, DATE_SHORTDATE, &time, nullptr, date, 64) > 0)
            text += std::wstring(L", ") + date;
        if (GetTimeFormatW(LOCALE_USER_DEFAULT, 0, &time, nullptr, clock, 64) > 0)
            text += std::wstring(L", ") + clock;
    }
    return text;
}

class COverwriteDialog : public CPictViewDialog
{
public:
    COverwriteDialog(HWND parent, const wchar_t* existingPath, const wchar_t* newPath)
        : CPictViewDialog(IDD_OVERWRITE, parent), m_existing(existingPath), m_new(newPath)
    {
    }

protected:
    INT_PTR DialogProc(UINT message, WPARAM wParam, LPARAM lParam) override
    {
        switch (message)
        {
        case WM_INITDIALOG:
            SetDlgItemTextW(HWindow, IDS_SOURCENAME, m_existing);
            SetDlgItemTextW(HWindow, IDS_SOURCEATTR, DescribeFile(m_existing).c_str());
            SetDlgItemTextW(HWindow, IDS_TARGETNAME, m_new);
            SetDlgItemTextW(HWindow, IDS_TARGETATTR, DescribeFile(m_new).c_str());
            break;

        case WM_COMMAND:
            if (LOWORD(wParam) == IDYES || LOWORD(wParam) == IDNO)
            {
                EndDialog(HWindow, LOWORD(wParam));
                return TRUE;
            }
            break;
        }
        return CPictViewDialog::DialogProc(message, wParam, lParam);
    }

private:
    const wchar_t* m_existing;
    const wchar_t* m_new;
};

// "Overwrite file: <existing>  With file: <renamed one>".
RenameOverwriteDecision ConfirmRenameOverwrite(void* context, HWND parent, const wchar_t* sourcePath, const wchar_t* targetPath)
{
    (void)context;
    switch (COverwriteDialog(parent, targetPath, sourcePath).Execute())
    {
    case IDYES:
        return RenameOverwriteDecision::Yes;
    case IDNO:
        return RenameOverwriteDecision::No;
    default:
        return RenameOverwriteDecision::Cancel;
    }
}

// ------------------------------------------------------------------------------------------
// Save As option panel (IDD_SAVEEX)

// Controls of the legacy panel for features WIC cannot write: colour depth, comments, GIF
// interlacing and GIF87a, TIFF strips.
const int SaveAsUnsupportedControls[] = {
    IDC_STATIC_2, IDC_SAVE_BIT_DEPTH, IDC_SAVE_INVERT, IDC_SAVE_COMMENT_STATIC, IDC_SAVE_COMMENT,
    IDC_SAVE_GROUP_GIF, IDC_SAVE_GIF_INTERLACED, IDC_SAVE_GIF_89a, IDC_SAVE_GROUP_TIFF,
    IDC_SAVE_TIFF_MAKE_STRIPS, IDC_SAVE_TIFF_STATIC1, IDC_SAVE_TIFF_STRIP_SIZE, IDC_SAVE_TIFF_STATIC2,
};
const int SaveAsJpegControls[] = {
    IDC_SAVE_GROUP_JPEG, IDC_SAVE_JPEG_STATIC1, IDC_SAVE_JPEG_QUALITY, IDC_SAVE_JPEG_STATIC2,
    IDC_SAVE_JPEG_STATIC3, IDC_SAVE_JPEG_SUBSAMPLING,
};
const int SaveAsTransformControls[] = {IDC_STATIC_3, IDC_SAVE_ROTATION, IDC_STATIC_4, IDC_SAVE_FLIP};

// Common dialog controls the panel lines up with: the "Save as type" label and combo box.
constexpr int SaveDialogTypeLabel = 0x441; // stc2
constexpr int SaveDialogTypeCombo = 0x470; // cmb1

struct SaveAsHookState
{
    SaveAsRequest* Request = nullptr;
    bool Expanded = false;
};

RECT ChildRect(HWND parent, HWND child)
{
    RECT rect = {};
    GetWindowRect(child, &rect);
    MapWindowPoints(nullptr, parent, reinterpret_cast<POINT*>(&rect), 2);
    return rect;
}

void MoveChildBy(HWND dialog, int id, int dx, int dy)
{
    HWND control = GetDlgItem(dialog, id);
    const RECT rect = ChildRect(dialog, control);
    SetWindowPos(control, nullptr, rect.left + dx, rect.top + dy, 0, 0, SWP_NOZORDER | SWP_NOSIZE | SWP_NOACTIVATE);
}

// Lines the panel up with the Save dialog: the compression label under "Save as type", its
// combo box under the type combo box, Options under Cancel; everything else follows the label.
void PositionSaveAsPanel(HWND dialog)
{
    HWND saveDialog = GetParent(dialog);
    auto alignWith = [&](int baseId, int id) {
        HWND base = GetDlgItem(saveDialog, baseId);
        HWND control = GetDlgItem(dialog, id);
        if (base == nullptr || control == nullptr)
            return;
        RECT baseRect = {};
        GetWindowRect(base, &baseRect);
        MapWindowPoints(nullptr, dialog, reinterpret_cast<POINT*>(&baseRect), 2);
        const RECT rect = ChildRect(dialog, control);
        SetWindowPos(control, nullptr, baseRect.left, rect.top, baseRect.right - baseRect.left, rect.bottom - rect.top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
    };

    const RECT labelBefore = ChildRect(dialog, GetDlgItem(dialog, IDC_SAVE_COMPRESSION_LABEL));
    alignWith(SaveDialogTypeLabel, IDC_SAVE_COMPRESSION_LABEL);
    alignWith(SaveDialogTypeCombo, IDC_SAVE_COMPRESSION);
    alignWith(IDCANCEL, IDC_SAVE_ADVANCED);
    const RECT labelAfter = ChildRect(dialog, GetDlgItem(dialog, IDC_SAVE_COMPRESSION_LABEL));
    const int shift = labelAfter.left - labelBefore.left;
    for (HWND child = GetWindow(dialog, GW_CHILD); child != nullptr; child = GetWindow(child, GW_HWNDNEXT))
    {
        const int id = GetDlgCtrlID(child);
        if (id != IDC_SAVE_COMPRESSION_LABEL && id != IDC_SAVE_COMPRESSION && id != IDC_SAVE_ADVANCED)
            MoveChildBy(dialog, id, shift, 0);
    }
}

// Hides what WIC cannot write and closes the gaps: rotation and flip move up into the colour
// depth rows, the JPEG group into the GIF group's place.
void ArrangeSaveAsOptions(HWND dialog)
{
    const RECT depth = ChildRect(dialog, GetDlgItem(dialog, IDC_SAVE_BIT_DEPTH));
    const RECT rotation = ChildRect(dialog, GetDlgItem(dialog, IDC_SAVE_ROTATION));
    const RECT gif = ChildRect(dialog, GetDlgItem(dialog, IDC_SAVE_GROUP_GIF));
    const RECT jpeg = ChildRect(dialog, GetDlgItem(dialog, IDC_SAVE_GROUP_JPEG));
    for (int id : SaveAsUnsupportedControls)
    {
        ShowWindow(GetDlgItem(dialog, id), SW_HIDE);
        EnableWindow(GetDlgItem(dialog, id), FALSE);
    }
    for (int id : SaveAsTransformControls)
        MoveChildBy(dialog, id, 0, depth.top - rotation.top);
    for (int id : SaveAsJpegControls)
        MoveChildBy(dialog, id, 0, gif.top - jpeg.top);
}

// Screen bottom of the panel's lowest option. The Save dialog moves the panel's controls after
// WM_INITDIALOG, so this is measured when needed. (A drop-down list's window is its closed
// height; the list is a window of its own.)
int LowestSaveAsOptionBottom(HWND dialog)
{
    RECT flip = {};
    RECT jpeg = {};
    GetWindowRect(GetDlgItem(dialog, IDC_SAVE_FLIP), &flip);
    GetWindowRect(GetDlgItem(dialog, IDC_SAVE_GROUP_JPEG), &jpeg);
    return (std::max)(flip.bottom, jpeg.bottom);
}

ImageSaveFormat SaveAsFormat(const SaveAsRequest& request, DWORD filterIndex)
{
    if (filterIndex >= 1 && filterIndex <= request.FilterFormats.size())
        return request.FilterFormats[filterIndex - 1];
    return ImageSaveFormat::Png;
}

void FillSaveAsCompression(HWND dialog, ImageSaveFormat format, ViewerTiffCompression selected)
{
    HWND combo = GetDlgItem(dialog, IDC_SAVE_COMPRESSION);
    SendMessageW(combo, CB_RESETCONTENT, 0, 0);
    for (const SaveAsCompressionChoice& choice : SaveAsCompressionChoices(format))
    {
        const wchar_t* text = choice.TextId != 0 ? ViewerText(choice.TextId) : choice.Text;
        const LRESULT index = SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(text));
        SendMessageW(combo, CB_SETITEMDATA, index, static_cast<LPARAM>(choice.Value));
        if (choice.Value == selected || index == 0)
            SendMessageW(combo, CB_SETCURSEL, index, 0);
    }
}

void UpdateSaveAsControls(HWND dialog, const SaveAsHookState& state, ImageSaveFormat format)
{
    for (int id : SaveAsTransformControls)
        EnableWindow(GetDlgItem(dialog, id), state.Expanded);
    for (int id : SaveAsJpegControls)
        EnableWindow(GetDlgItem(dialog, id), state.Expanded && format == ImageSaveFormat::Jpeg);
    // A single compression (every format but TIFF) is information, not a choice.
    EnableWindow(GetDlgItem(dialog, IDC_SAVE_COMPRESSION),
                 SendDlgItemMessageW(dialog, IDC_SAVE_COMPRESSION, CB_GETCOUNT, 0, 0) > 1);
}

// Options grows the Save dialog to show the panel's options, or shrinks it back.
void ToggleSaveAsOptions(HWND dialog, SaveAsHookState& state, ImageSaveFormat format)
{
    HWND saveDialog = GetParent(dialog);
    state.Expanded = !state.Expanded;
    CheckDlgButton(dialog, IDC_SAVE_ADVANCED, state.Expanded ? BST_CHECKED : BST_UNCHECKED);

    RECT panel = {};
    GetWindowRect(dialog, &panel);
    RECT button = {};
    GetWindowRect(GetDlgItem(dialog, IDC_SAVE_ADVANCED), &button);
    const int panelBottom = state.Expanded ? LowestSaveAsOptionBottom(dialog) : button.bottom;
    const int margin = MulDiv(7, HIWORD(GetDialogBaseUnits()), 8);

    RECT window = {};
    GetWindowRect(saveDialog, &window);
    const int newBottom = panelBottom + margin + (window.bottom - panel.bottom);
    SetWindowPos(saveDialog, nullptr, 0, 0, window.right - window.left, newBottom - window.top,
                 SWP_NOZORDER | SWP_NOMOVE | SWP_NOACTIVATE);
    SetWindowPos(dialog, nullptr, 0, 0, panel.right - panel.left, panelBottom + margin - panel.top,
                 SWP_NOZORDER | SWP_NOMOVE | SWP_NOACTIVATE);
    UpdateSaveAsControls(dialog, state, format);
}

void FillSaveAsChoiceCombo(HWND dialog, int id, std::initializer_list<UINT> textIds, int selected)
{
    HWND combo = GetDlgItem(dialog, id);
    for (UINT textId : textIds)
        SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(ViewerText(textId)));
    SendMessageW(combo, CB_SETCURSEL, selected, 0);
}

void ReadSaveAsOptions(HWND dialog, SaveAsRequest& request, ImageSaveFormat format)
{
    if (format == ImageSaveFormat::Tiff)
    {
        HWND combo = GetDlgItem(dialog, IDC_SAVE_COMPRESSION);
        const LRESULT index = SendMessageW(combo, CB_GETCURSEL, 0, 0);
        if (index >= 0)
            request.TiffCompression = static_cast<ViewerTiffCompression>(SendMessageW(combo, CB_GETITEMDATA, index, 0));
    }
    BOOL translated = FALSE;
    const UINT quality = GetDlgItemInt(dialog, IDC_SAVE_JPEG_QUALITY, &translated, FALSE);
    if (translated)
        request.JpegQuality = (std::max)(1, (std::min)(100, static_cast<int>(quality)));
    request.JpegSubsampling = SendDlgItemMessageW(dialog, IDC_SAVE_JPEG_SUBSAMPLING, CB_GETCURSEL, 0, 0) == 0
                                  ? ViewerJpegSubsampling::OneToOneOne
                                  : ViewerJpegSubsampling::TwoToOneOne;
    request.Rotation = static_cast<SaveAsRotation>((std::max)(LRESULT(0), SendDlgItemMessageW(dialog, IDC_SAVE_ROTATION, CB_GETCURSEL, 0, 0)));
    request.Flip = static_cast<SaveAsFlip>((std::max)(LRESULT(0), SendDlgItemMessageW(dialog, IDC_SAVE_FLIP, CB_GETCURSEL, 0, 0)));
}

UINT_PTR CALLBACK SaveAsHookProc(HWND dialog, UINT message, WPARAM wParam, LPARAM lParam)
{
    SaveAsHookState* state = reinterpret_cast<SaveAsHookState*>(GetWindowLongPtrW(dialog, GWLP_USERDATA));
    switch (message)
    {
    case WM_INITDIALOG:
    {
        const OPENFILENAMEW* ofn = reinterpret_cast<const OPENFILENAMEW*>(lParam);
        state = reinterpret_cast<SaveAsHookState*>(ofn->lCustData);
        SetWindowLongPtrW(dialog, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
        const SaveAsRequest& request = *state->Request;

        PositionSaveAsPanel(dialog);
        ArrangeSaveAsOptions(dialog);
        if (DialogGui != nullptr)
            DialogGui->AttachButton(dialog, IDC_SAVE_ADVANCED, BTF_MORE | BTF_CHECKBOX);

        FillSaveAsChoiceCombo(dialog, IDC_SAVE_ROTATION, {IDS_ROT_NONE, IDS_ROT_90, IDS_ROT_180, IDS_ROT_270},
                              static_cast<int>(request.Rotation));
        FillSaveAsChoiceCombo(dialog, IDC_SAVE_FLIP, {IDS_FLIP_NONE, IDS_FLIP_VERT, IDS_FLIP_HOR},
                              static_cast<int>(request.Flip));
        SetDlgItemInt(dialog, IDC_SAVE_JPEG_QUALITY, static_cast<UINT>(request.JpegQuality), FALSE);
        SendDlgItemMessageW(dialog, IDC_SAVE_JPEG_QUALITY, EM_LIMITTEXT, 3, 0);
        HWND subsampling = GetDlgItem(dialog, IDC_SAVE_JPEG_SUBSAMPLING);
        SendMessageW(subsampling, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"1:1:1"));
        SendMessageW(subsampling, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"2:1:1"));
        SendMessageW(subsampling, CB_SETCURSEL, request.JpegSubsampling == ViewerJpegSubsampling::OneToOneOne ? 0 : 1, 0);

        const ImageSaveFormat format = SaveAsFormat(request, ofn->nFilterIndex);
        FillSaveAsCompression(dialog, format, request.TiffCompression);
        UpdateSaveAsControls(dialog, *state, format);
        if (DialogGeneral != nullptr)
            DialogGeneral->MultiMonCenterWindow(GetParent(dialog), GetParent(GetParent(dialog)), FALSE);
        break;
    }

    case WM_COMMAND:
        if (state != nullptr && LOWORD(wParam) == IDC_SAVE_ADVANCED)
        {
            const DWORD filterIndex = static_cast<DWORD>(SendDlgItemMessageW(GetParent(dialog), SaveDialogTypeCombo, CB_GETCURSEL, 0, 0) + 1);
            ToggleSaveAsOptions(dialog, *state, SaveAsFormat(*state->Request, filterIndex));
        }
        break;

    case WM_NOTIFY:
    {
        if (state == nullptr)
            break;
        const OFNOTIFYW* notify = reinterpret_cast<const OFNOTIFYW*>(lParam);
        const ImageSaveFormat format = SaveAsFormat(*state->Request, notify->lpOFN->nFilterIndex);
        if (notify->hdr.code == CDN_TYPECHANGE)
        {
            FillSaveAsCompression(dialog, format, state->Request->TiffCompression);
            UpdateSaveAsControls(dialog, *state, format);
        }
        else if (notify->hdr.code == CDN_FILEOK)
        {
            ReadSaveAsOptions(dialog, *state->Request, format);
        }
        break;
    }
    }
    return FALSE;
}

// ------------------------------------------------------------------------------------------
// Print (IDD_PRINT)

// The printer the user picked with Setup..., shared by every viewer for the session (the old
// PictView kept it the same way). DEVMODE and DEVNAMES are copied out of their HGLOBALs.
struct SavedPrinter
{
    std::mutex Lock;
    std::vector<uint8_t> DevMode;
    std::vector<uint8_t> DevNames;
} PrinterChoice;

std::vector<uint8_t> CopyGlobal(HGLOBAL handle)
{
    std::vector<uint8_t> bytes;
    if (handle == nullptr)
        return bytes;
    const void* data = GlobalLock(handle);
    if (data != nullptr)
    {
        bytes.assign(static_cast<const uint8_t*>(data), static_cast<const uint8_t*>(data) + GlobalSize(handle));
        GlobalUnlock(handle);
    }
    return bytes;
}

HGLOBAL GlobalFromBytes(const std::vector<uint8_t>& bytes)
{
    if (bytes.empty())
        return nullptr;
    HGLOBAL handle = GlobalAlloc(GMEM_MOVEABLE, bytes.size());
    if (handle != nullptr)
    {
        void* data = GlobalLock(handle);
        if (data != nullptr)
        {
            memcpy(data, bytes.data(), bytes.size());
            GlobalUnlock(handle);
        }
        else
        {
            GlobalFree(handle);
            handle = nullptr;
        }
    }
    return handle;
}

// "Device, Port" from DEVNAMES (its offsets count WCHARs).
std::wstring PrinterDisplayName(const std::vector<uint8_t>& devNames)
{
    if (devNames.size() < sizeof(DEVNAMES))
        return std::wstring();
    const DEVNAMES* names = reinterpret_cast<const DEVNAMES*>(devNames.data());
    const wchar_t* base = reinterpret_cast<const wchar_t*>(devNames.data());
    const size_t count = devNames.size() / sizeof(wchar_t);
    auto at = [&](WORD offset) -> std::wstring {
        if (offset >= count)
            return std::wstring();
        return std::wstring(base + offset, wcsnlen(base + offset, count - offset));
    };
    std::wstring name = at(names->wDeviceOffset);
    const std::wstring port = at(names->wOutputOffset);
    if (!port.empty())
        name += L", " + port;
    return name;
}

void ReportPrinterError(HWND parent, DWORD error)
{
    UINT text = IDS_PRINT_SETUPFAILED;
    if (error == PDERR_NODEFAULTPRN)
        text = IDS_PRINT_NODEFPRINTER;
    else if (error == PDERR_NODEVICES)
        text = IDS_PRINT_NOPRINTERS;
    ShowDialogError(parent, text);
}

// Runs the system print dialog: Setup... ('setup') or a silent default-printer query. On
// success the choice is saved and a DC for it returned.
HDC RunPrinterDialog(HWND parent, bool setup)
{
    PRINTDLGW dialog = {};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = parent;
    dialog.Flags = PD_RETURNDC | PD_USEDEVMODECOPIESANDCOLLATE | (setup ? PD_PRINTSETUP : PD_RETURNDEFAULT | PD_NOWARNING);
    if (setup)
    {
        std::lock_guard<std::mutex> lock(PrinterChoice.Lock);
        dialog.hDevMode = GlobalFromBytes(PrinterChoice.DevMode);
        dialog.hDevNames = GlobalFromBytes(PrinterChoice.DevNames);
    }
    const BOOL chosen = PrintDlgW(&dialog);
    const DWORD error = chosen ? 0 : CommDlgExtendedError();
    if (chosen)
    {
        std::lock_guard<std::mutex> lock(PrinterChoice.Lock);
        PrinterChoice.DevMode = CopyGlobal(dialog.hDevMode);
        PrinterChoice.DevNames = CopyGlobal(dialog.hDevNames);
    }
    if (dialog.hDevMode != nullptr)
        GlobalFree(dialog.hDevMode);
    if (dialog.hDevNames != nullptr)
        GlobalFree(dialog.hDevNames);
    if (!chosen)
    {
        if (dialog.hDC != nullptr)
            DeleteDC(dialog.hDC);
        if (error != 0)
            ReportPrinterError(parent, error);
        return nullptr;
    }
    return dialog.hDC;
}

// A DC for the saved printer, or for the default printer the first time.
HDC OpenChosenPrinter(HWND parent)
{
    {
        std::lock_guard<std::mutex> lock(PrinterChoice.Lock);
        if (PrinterChoice.DevNames.size() >= sizeof(DEVNAMES))
        {
            const DEVNAMES* names = reinterpret_cast<const DEVNAMES*>(PrinterChoice.DevNames.data());
            const wchar_t* base = reinterpret_cast<const wchar_t*>(PrinterChoice.DevNames.data());
            const DEVMODEW* mode = PrinterChoice.DevMode.size() >= sizeof(DEVMODEW)
                                       ? reinterpret_cast<const DEVMODEW*>(PrinterChoice.DevMode.data())
                                       : nullptr;
            HDC dc = CreateDCW(nullptr, base + names->wDeviceOffset, nullptr, mode);
            if (dc != nullptr)
                return dc;
        }
    }
    return RunPrinterDialog(parent, false);
}

std::wstring ChosenPrinterName()
{
    std::lock_guard<std::mutex> lock(PrinterChoice.Lock);
    return PrinterDisplayName(PrinterChoice.DevNames);
}

class CPrintDialog;

// The page preview: the paper, its printable area (dotted) and the image where it will print.
class CPrintPreview : public CWindow
{
public:
    CPrintPreview(HWND dialog, CPrintDialog& owner) : CWindow(dialog, IDC_PRINT_PREVIEW, ooStatic), m_owner(owner) {}

protected:
    LRESULT WindowProc(UINT message, WPARAM wParam, LPARAM lParam) override;

private:
    CPrintDialog& m_owner;
};

class CPrintDialog : public CPictViewDialog
{
public:
    CPrintDialog(HWND parent, PrintRequest& request, HDC printer)
        : CPictViewDialog(IDD_PRINT, parent), m_request(request), m_settings(request.Settings), m_printer(printer)
    {
    }
    ~CPrintDialog()
    {
        if (m_printer != nullptr)
            DeleteDC(m_printer);
    }

    // The printer DC for the chosen printer; the caller owns it afterwards.
    HDC TakePrinter()
    {
        HDC printer = m_printer;
        m_printer = nullptr;
        return printer;
    }

    void PaintPreview(HDC dc, const RECT& client);

protected:
    INT_PTR DialogProc(UINT message, WPARAM wParam, LPARAM lParam) override
    {
        switch (message)
        {
        case WM_INITDIALOG:
        {
            // The preview frame gets a sunken edge, as in the old dialog.
            HWND preview = GetDlgItem(HWindow, IDC_PRINT_PREVIEW);
            SetWindowLongPtrW(preview, GWL_EXSTYLE, GetWindowLongPtrW(preview, GWL_EXSTYLE) | WS_EX_CLIENTEDGE);
            SetWindowPos(preview, nullptr, 0, 0, 0, 0, SWP_NOSIZE | SWP_NOMOVE | SWP_NOZORDER | SWP_FRAMECHANGED);
            m_preview = std::make_unique<CPrintPreview>(HWindow, *this);
            m_page = DescribePrinterPage(m_printer);
            m_settings.Selection = m_settings.Selection && m_request.SelectionImage != nullptr;
            ResetSizeToScale();
            SetDlgItemTextW(HWindow, IDC_PRINT_PRINTER, ChosenPrinterName().c_str());
            CheckDlgButton(HWindow, IDC_PRINT_CENTER, m_settings.Center ? BST_CHECKED : BST_UNCHECKED);
            CheckDlgButton(HWindow, IDC_PRINT_FIT, m_settings.Fit ? BST_CHECKED : BST_UNCHECKED);
            CheckDlgButton(HWindow, IDC_PRINT_ASPECT, m_settings.KeepAspect ? BST_CHECKED : BST_UNCHECKED);
            CheckDlgButton(HWindow, IDC_PRINT_BOX, m_settings.BoundingBox ? BST_CHECKED : BST_UNCHECKED);
            CheckDlgButton(HWindow, IDC_PRINT_SELECTION, m_settings.Selection ? BST_CHECKED : BST_UNCHECKED);
            ShowNumbers();
            EnableControls();
            break;
        }

        case WM_COMMAND:
            switch (HIWORD(wParam))
            {
            case EN_UPDATE:
                KeepNumericCharacters(reinterpret_cast<HWND>(lParam), false, true);
                break;
            case EN_KILLFOCUS:
                ReadNumber(LOWORD(wParam), reinterpret_cast<HWND>(lParam));
                break;
            case CBN_SELCHANGE:
            {
                HWND combo = reinterpret_cast<HWND>(lParam);
                m_settings.Units = static_cast<PrintUnits>(SendMessageW(combo, CB_GETITEMDATA, SendMessageW(combo, CB_GETCURSEL, 0, 0), 0));
                ShowNumbers();
                break;
            }
            }
            switch (LOWORD(wParam))
            {
            case IDC_PRINT_CENTER:
                m_settings.Center = IsDlgButtonChecked(HWindow, IDC_PRINT_CENTER) == BST_CHECKED;
                Changed();
                break;
            case IDC_PRINT_FIT:
                m_settings.Fit = IsDlgButtonChecked(HWindow, IDC_PRINT_FIT) == BST_CHECKED;
                Changed();
                break;
            case IDC_PRINT_ASPECT:
                m_settings.KeepAspect = IsDlgButtonChecked(HWindow, IDC_PRINT_ASPECT) == BST_CHECKED;
                if (m_settings.KeepAspect)
                    ResetSizeToScale();
                ShowNumbers();
                Changed();
                break;
            case IDC_PRINT_BOX:
                m_settings.BoundingBox = IsDlgButtonChecked(HWindow, IDC_PRINT_BOX) == BST_CHECKED;
                Changed();
                break;
            case IDC_PRINT_SELECTION:
                m_settings.Selection = IsDlgButtonChecked(HWindow, IDC_PRINT_SELECTION) == BST_CHECKED;
                ResetSizeToScale();
                ShowNumbers();
                Changed();
                break;
            case IDC_PRINT_SETUP:
                if (HDC printer = RunPrinterDialog(HWindow, true))
                {
                    if (m_printer != nullptr)
                        DeleteDC(m_printer);
                    m_printer = printer;
                    m_page = DescribePrinterPage(m_printer);
                    SetDlgItemTextW(HWindow, IDC_PRINT_PRINTER, ChosenPrinterName().c_str());
                    Changed();
                }
                break;
            case IDOK:
                // A value still being typed counts.
                if (HWND focus = GetFocus(); focus != nullptr && GetParent(focus) == HWindow)
                    ReadNumber(GetDlgCtrlID(focus), focus);
                m_request.Settings = m_settings;
                break;
            }
            break;
        }
        return CPictViewDialog::DialogProc(message, wParam, lParam);
    }

private:
    const ImageSurface* PrintedImage() const
    {
        return m_settings.Selection && m_request.SelectionImage != nullptr ? m_request.SelectionImage : m_request.Image;
    }

    void NaturalSize(double& width, double& height) const
    {
        const ImageSurface* image = PrintedImage();
        NaturalPrintSize(image->Width, image->Height, m_request.ImageDpiX, m_request.ImageDpiY, m_page.DpiX, m_page.DpiY,
                         width, height);
    }

    void ResetSizeToScale()
    {
        double width = 0;
        double height = 0;
        NaturalSize(width, height);
        m_settings.Width = width * m_settings.Scale / 100;
        m_settings.Height = height * m_settings.Scale / 100;
    }

    // The scale follows the width; Keep aspect is ticked again when width and height agree.
    void ScaleFromSize()
    {
        double width = 0;
        double height = 0;
        NaturalSize(width, height);
        if (!(width > 0) || !(height > 0))
            return;
        const double widthScale = m_settings.Width / width;
        const double heightScale = m_settings.Height / height;
        m_settings.Scale = widthScale * 100;
        m_settings.KeepAspect = heightScale > 0 && std::fabs(widthScale / heightScale - 1) < 1e-3;
        CheckDlgButton(HWindow, IDC_PRINT_ASPECT, m_settings.KeepAspect ? BST_CHECKED : BST_UNCHECKED);
    }

    void ShowNumbers()
    {
        const UINT unitNames[] = {IDS_UNIT_INCHES, IDS_UNIT_CM, IDS_UNIT_MM, IDS_UNIT_POINTS, IDS_UNIT_PICAS};
        const PrintUnits units[] = {PrintUnits::Inches, PrintUnits::Centimeters, PrintUnits::Millimeters, PrintUnits::Points,
                                    PrintUnits::Picas};
        const struct
        {
            int Edit;
            int Combo;
            double Points;
        } fields[] = {{IDC_PRINT_LEFT, IDC_PRINT_LEFT_UNITS, m_settings.Left},
                      {IDC_PRINT_TOP, IDC_PRINT_TOP_UNITS, m_settings.Top},
                      {IDC_PRINT_WIDTH, IDC_PRINT_WIDTH_UNITS, m_settings.Width},
                      {IDC_PRINT_HEIGHT, IDC_PRINT_HEIGHT_UNITS, m_settings.Height}};
        for (const auto& field : fields)
        {
            HWND combo = GetDlgItem(HWindow, field.Combo);
            SendMessageW(combo, CB_RESETCONTENT, 0, 0);
            for (size_t i = 0; i < std::size(units); ++i)
            {
                const LRESULT index = SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(ViewerText(unitNames[i])));
                SendMessageW(combo, CB_SETITEMDATA, index, static_cast<LPARAM>(units[i]));
                if (units[i] == m_settings.Units)
                    SendMessageW(combo, CB_SETCURSEL, index, 0);
            }
            SetDlgItemTextW(HWindow, field.Edit, FormatPrintNumber(PointsToUnits(field.Points, m_settings.Units)).c_str());
        }
        SetDlgItemTextW(HWindow, IDC_PRINT_SCALE, FormatPrintNumber(m_settings.Scale, 2).c_str());
    }

    void ReadNumber(int id, HWND edit)
    {
        double value = 0;
        if (!ParsePrintNumber(SPLGetDlgItemTextOwned(HWindow, id), value))
            return;
        (void)edit;
        double natural = 0;
        double naturalHeight = 0;
        NaturalSize(natural, naturalHeight);
        switch (id)
        {
        case IDC_PRINT_LEFT:
            m_settings.Left = UnitsToPoints(value, m_settings.Units);
            break;
        case IDC_PRINT_TOP:
            m_settings.Top = UnitsToPoints(value, m_settings.Units);
            break;
        case IDC_PRINT_WIDTH:
            m_settings.Width = UnitsToPoints(value, m_settings.Units);
            if (m_settings.KeepAspect && natural > 0 && std::fabs(m_settings.Width) > 1e-3)
                m_settings.Height = naturalHeight * m_settings.Width / natural;
            ScaleFromSize();
            ShowNumbers();
            break;
        case IDC_PRINT_HEIGHT:
            m_settings.Height = UnitsToPoints(value, m_settings.Units);
            if (m_settings.KeepAspect && naturalHeight > 0 && std::fabs(m_settings.Height) > 1e-3)
                m_settings.Width = natural * m_settings.Height / naturalHeight;
            ScaleFromSize();
            ShowNumbers();
            break;
        case IDC_PRINT_SCALE:
            m_settings.Scale = (std::max)(0.01, value);
            ResetSizeToScale();
            ShowNumbers();
            break;
        default:
            return;
        }
        Changed();
    }

    void EnableControls()
    {
        const bool fit = m_settings.Fit;
        const bool positioned = !fit || m_settings.KeepAspect;
        for (int id : {IDC_PRINT_SCALE, IDC_PRINT_WIDTH, IDC_PRINT_HEIGHT})
            EnableWindow(GetDlgItem(HWindow, id), !fit);
        EnableWindow(GetDlgItem(HWindow, IDC_PRINT_CENTER), positioned);
        const bool topLeft = positioned && !m_settings.Center;
        EnableWindow(GetDlgItem(HWindow, IDC_PRINT_LEFT), topLeft);
        EnableWindow(GetDlgItem(HWindow, IDC_PRINT_TOP), topLeft);
        EnableWindow(GetDlgItem(HWindow, IDC_PRINT_SELECTION), m_request.SelectionImage != nullptr);
    }

    void Changed()
    {
        EnableControls();
        InvalidateRect(GetDlgItem(HWindow, IDC_PRINT_PREVIEW), nullptr, FALSE);
    }

    friend class CPrintPreview;
    PrintRequest& m_request;
    PrintSettings m_settings;
    HDC m_printer = nullptr;
    PrinterPage m_page;
    std::unique_ptr<CPrintPreview> m_preview;
};

LRESULT CPrintPreview::WindowProc(UINT message, WPARAM wParam, LPARAM lParam)
{
    if (message == WM_PAINT)
    {
        PAINTSTRUCT ps = {};
        HDC dc = BeginPaint(HWindow, &ps);
        RECT client = {};
        GetClientRect(HWindow, &client);
        // Drawn off-screen and copied once, so the preview does not flicker while values change.
        HDC memory = CreateCompatibleDC(dc);
        HBITMAP bitmap = memory != nullptr ? CreateCompatibleBitmap(dc, client.right, client.bottom) : nullptr;
        if (bitmap != nullptr)
        {
            HGDIOBJ previous = SelectObject(memory, bitmap);
            m_owner.PaintPreview(memory, client);
            BitBlt(dc, 0, 0, client.right, client.bottom, memory, 0, 0, SRCCOPY);
            SelectObject(memory, previous);
            DeleteObject(bitmap);
        }
        else
        {
            m_owner.PaintPreview(dc, client);
        }
        if (memory != nullptr)
            DeleteDC(memory);
        EndPaint(HWindow, &ps);
        return 0;
    }
    if (message == WM_ERASEBKGND)
        return 1;
    return CWindow::WindowProc(message, wParam, lParam);
}

void CPrintDialog::PaintPreview(HDC dc, const RECT& client)
{
    FillRect(dc, &client, GetSysColorBrush(COLOR_BTNFACE));
    if (!m_page.Valid() || client.right < 8 || client.bottom < 8)
        return;

    // The paper, scaled into the frame with a small margin and centered.
    const int margin = 6;
    const double zoom = (std::min)((client.right - 2.0 * margin) / m_page.PaperWidth,
                                   (client.bottom - 2.0 * margin) / m_page.PaperHeight); // pixels per inch
    RECT paper = {};
    paper.left = static_cast<LONG>((client.right - m_page.PaperWidth * zoom) / 2);
    paper.top = static_cast<LONG>((client.bottom - m_page.PaperHeight * zoom) / 2);
    paper.right = static_cast<LONG>(paper.left + m_page.PaperWidth * zoom);
    paper.bottom = static_cast<LONG>(paper.top + m_page.PaperHeight * zoom);
    FillRect(dc, &paper, GetSysColorBrush(COLOR_WINDOW));
    FrameRect(dc, &paper, GetSysColorBrush(COLOR_WINDOWFRAME));

    RECT printable = paper;
    printable.left += static_cast<LONG>(m_page.MarginLeft * zoom);
    printable.top += static_cast<LONG>(m_page.MarginTop * zoom);
    printable.right -= static_cast<LONG>(m_page.MarginRight * zoom);
    printable.bottom -= static_cast<LONG>(m_page.MarginBottom * zoom);

    double width = 0;
    double height = 0;
    NaturalSize(width, height);
    const double printableWidth = static_cast<double>(m_page.PrintableWidth) / m_page.DpiX * 72.0;
    const double printableHeight = static_cast<double>(m_page.PrintableHeight) / m_page.DpiY * 72.0;
    const PrintPlacement placement = PlaceOnPage(m_settings, width, height, printableWidth, printableHeight);
    if (placement.Valid)
    {
        const double perPoint = zoom / 72.0;
        RECT image = {};
        image.left = printable.left + static_cast<LONG>(std::lround(placement.Left * perPoint));
        image.top = printable.top + static_cast<LONG>(std::lround(placement.Top * perPoint));
        image.right = image.left + (std::max)(1L, static_cast<LONG>(std::lround(placement.Width * perPoint)));
        image.bottom = image.top + (std::max)(1L, static_cast<LONG>(std::lround(placement.Height * perPoint)));

        const ImageSurface* surface = PrintedImage();
        BITMAPINFO info = {};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = static_cast<LONG>(surface->Width);
        info.bmiHeader.biHeight = -static_cast<LONG>(surface->Height);
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        info.bmiHeader.biCompression = BI_RGB;
        const int saved = SaveDC(dc);
        IntersectClipRect(dc, printable.left, printable.top, printable.right, printable.bottom);
        SetStretchBltMode(dc, HALFTONE);
        SetBrushOrgEx(dc, 0, 0, nullptr);
        StretchDIBits(dc, image.left, image.top, image.right - image.left, image.bottom - image.top, 0, 0, surface->Width,
                      surface->Height, surface->Pixels.data(), &info, DIB_RGB_COLORS, SRCCOPY);
        if (m_settings.BoundingBox)
            DrawFocusRect(dc, &image);
        RestoreDC(dc, saved);
    }
    DrawFocusRect(dc, &printable); // the printable area
}

} // namespace

CSalamanderGeneralAbstract* DialogsGeneral()
{
    return DialogGeneral;
}

CSalamanderGUIAbstract* DialogsGui()
{
    return DialogGui;
}

void InitializeDialogs(HINSTANCE instance, CSalamanderGeneralAbstract* general, CSalamanderGUIAbstract* gui)
{
    DialogInstance = instance;
    DialogGeneral = general;
    DialogGui = gui;
    SetRenameOverwritePrompt(ConfirmRenameOverwrite, nullptr);
}

bool PromptForZoomPercent(HWND owner, int currentPercent, int& selectedPercent)
{
    int percent = currentPercent;
    if (CZoomDialog(owner, percent).Execute() != IDOK)
        return false;
    selectedPercent = percent;
    return true;
}

bool PromptForFrameNumber(HWND owner, uint32_t frameCount, uint32_t currentFrame, uint32_t& selectedFrame)
{
    if (frameCount == 0)
        return false;
    uint32_t page = (std::min)(currentFrame, frameCount - 1);
    if (CPageDialog(owner, page, frameCount).Execute() != IDOK)
        return false;
    selectedFrame = page;
    return true;
}

bool PromptForRenameFileName(HWND owner, const std::wstring& initialName, bool selectWholeName, std::wstring& selectedName)
{
    std::wstring result;
    if (CRenameDialog(owner, initialName, selectWholeName, result).Execute() != IDOK || result.empty())
        return false;
    selectedName = std::move(result);
    return true;
}

bool PromptForCaptureOptions(HWND owner, ViewerCaptureOptions& options)
{
    ViewerCaptureOptions edited = options;
    if (CCaptureDialog(owner, edited).Execute() != IDOK)
        return false;
    options = edited;
    return true;
}

bool PromptForCopyToTargetPath(HWND owner, const std::wstring& sourcePath, std::wstring& targetPath)
{
    if (sourcePath.empty() || DialogGeneral == nullptr)
        return false;

    ViewerCopyToHistory history = GetCopyToHistory();
    history.LastIndex = std::clamp(history.LastIndex, 0, static_cast<int>(ViewerCopyToHistoryEntryCount) - 1);
    std::wstring target;
    if (CCopyToDialog(owner, sourcePath, history, target).Execute() != IDOK)
        return false;
    SetCopyToHistory(history); // the edited targets are remembered, as in Sally's own dialogs
    targetPath = std::move(target);
    return true;
}

void ShowAboutPictViewDialog(HWND parent)
{
    CAboutDialog(parent).Execute();
}

void ShowImagePropertiesDialog(HWND owner, const ImagePropertiesInfo& info)
{
    CImagePropertiesDialog(owner, info).Execute();
}

void ShowExifDialog(HWND owner, const std::vector<ImageExifEntry>& entries)
{
    CExifDialog(owner, entries).Execute();
}

std::vector<SaveAsCompressionChoice> SaveAsCompressionChoices(ImageSaveFormat format)
{
    switch (format)
    {
    case ImageSaveFormat::Tiff:
        return {{IDS_SAVE_DEFAULT, nullptr, ViewerTiffCompression::Default},
                {0, L"Deflating (LZ77)", ViewerTiffCompression::Zip},
                {0, L"Lempel-Ziv-Welch (LZW)", ViewerTiffCompression::Lzw},
                {0, L"Uncompressed", ViewerTiffCompression::None}};
    case ImageSaveFormat::Jpeg:
        return {{0, L"JPEG", ViewerTiffCompression::Default}};
    case ImageSaveFormat::Gif:
        return {{0, L"Lempel-Ziv-Welch (LZW)", ViewerTiffCompression::Default}};
    case ImageSaveFormat::Bmp:
        return {{0, L"Uncompressed", ViewerTiffCompression::Default}};
    case ImageSaveFormat::Png:
    default:
        return {{0, L"Deflating (LZ77)", ViewerTiffCompression::Default}};
    }
}

bool PromptForPrint(HWND owner, PrintRequest& request)
{
    request.Printer = nullptr;
    if (request.Image == nullptr || request.Image->Pixels.empty())
        return false;
    HDC printer = OpenChosenPrinter(owner);
    if (printer == nullptr)
        return false;
    CPrintDialog dialog(owner, request, printer);
    if (dialog.Execute() != IDOK)
        return false;
    request.Printer = dialog.TakePrinter();
    return request.Printer != nullptr;
}

bool PromptForSaveAs(HWND owner, SaveAsRequest& request)
{
    if (DialogGeneral == nullptr)
        return false;

    std::wstring filter = request.Filter;
    std::replace(filter.begin(), filter.end(), L'|', L'\0');
    filter.push_back(L'\0');

    SaveAsHookState state;
    state.Request = &request;
    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = owner;
    ofn.hInstance = DialogInstance;
    ofn.lpstrFilter = filter.c_str();
    ofn.nFilterIndex = request.FilterIndex;
    ofn.lpstrInitialDir = request.InitialDirectory.empty() ? nullptr : request.InitialDirectory.c_str();
    ofn.lpfnHook = SaveAsHookProc;
    ofn.lpTemplateName = MAKEINTRESOURCEW(IDD_SAVEEX);
    ofn.lCustData = reinterpret_cast<LPARAM>(&state);
    ofn.Flags = OFN_EXPLORER | OFN_ENABLEHOOK | OFN_ENABLETEMPLATE | OFN_PATHMUSTEXIST | OFN_LONGNAMES |
                OFN_NOCHANGEDIR | OFN_NOTESTFILECREATE | OFN_HIDEREADONLY | OFN_OVERWRITEPROMPT;

    std::wstring fileName = request.FileName;
    if (!SPLSafeGetSaveFileNameOwned(DialogGeneral, &ofn, fileName))
        return false;
    request.FileName = std::move(fileName);
    request.FilterIndex = ofn.nFilterIndex;
    return true;
}

} // namespace pictview
