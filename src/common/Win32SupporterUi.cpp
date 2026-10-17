// SPDX-FileCopyrightText: 2025-2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later
#include "supporter/Win32SupporterUi.h"
#include "SallyUrls.h"
#include "Win32TextCodec.h"
#include "ExtendedLengthPath.h"
#include <commctrl.h>
#include <shellapi.h>
#include <shobjidl.h>
#include <algorithm>
#include <fstream>
#include <filesystem>
#include <iterator>

namespace Sally::Supporter
{
    namespace
    {
        std::wstring Wide(const std::string& text)
        {
            std::wstring out;
            Win32DecodeText(CP_UTF8, text, out);
            return out;
        }
        std::string Utf8(const std::wstring& text)
        {
            std::string out;
            Win32EncodeText(CP_UTF8, text, out);
            return out;
        }
        std::wstring LoadText(UINT id, LANGID language)
        {
            HMODULE module = GetModuleHandleW(nullptr);
            HRSRC resource = FindResourceExW(module, RT_STRING, MAKEINTRESOURCEW(id / 16 + 1), language);
            if (!resource)
                return {};
            HGLOBAL memory = LoadResource(module, resource);
            const wchar_t* data = memory ? static_cast<const wchar_t*>(LockResource(memory)) : nullptr;
            const wchar_t* end = data ? data + SizeofResource(module, resource) / sizeof(wchar_t) : nullptr;
            if (!data)
                return {};
            for (unsigned i = 0; i < id % 16; ++i)
            {
                if (data >= end || data + 1 + *data > end)
                    return {};
                data += *data + 1;
            }
            return data < end && data + 1 + *data <= end ? std::wstring(data + 1, *data) : std::wstring();
        }
        std::wstring Date(uint64_t seconds)
        {
            ULARGE_INTEGER value;
            value.QuadPart = (seconds + 11644473600ULL) * 10000000ULL;
            FILETIME file = {value.LowPart, value.HighPart};
            SYSTEMTIME utc{};
            if (!FileTimeToSystemTime(&file, &utc))
                return {};
            int required = GetDateFormatEx(LOCALE_NAME_USER_DEFAULT, DATE_SHORTDATE, &utc, nullptr, nullptr, 0, nullptr);
            if (!required)
                return {};
            std::wstring result(required, L'\0');
            if (!GetDateFormatEx(LOCALE_NAME_USER_DEFAULT, DATE_SHORTDATE, &utc, nullptr, result.data(), required, nullptr))
                return {};
            result.resize(required - 1);
            return result;
        }
        std::wstring WindowText(HWND window)
        {
            int length = GetWindowTextLengthW(window);
            std::wstring text(length + 1, L'\0');
            int actual = GetWindowTextW(window, text.data(), static_cast<int>(text.size()));
            text.resize(actual);
            return text;
        }
        std::wstring PickFile(HWND parent, bool save, LANGID language)
        {
            HRESULT init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
            IFileDialog* dialog = nullptr;
            HRESULT hr = CoCreateInstance(save ? CLSID_FileSaveDialog : CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog));
            std::wstring result;
            if (SUCCEEDED(hr))
            {
                DWORD options = 0;
                dialog->GetOptions(&options);
                dialog->SetOptions(options | FOS_FORCEFILESYSTEM | (save ? FOS_OVERWRITEPROMPT : FOS_FILEMUSTEXIST));
                std::wstring label = SupporterText(IDS_SUP_KEY_FILE, language);
                COMDLG_FILTERSPEC filter = {label.c_str(), L"*.sally-license"};
                dialog->SetFileTypes(1, &filter);
                dialog->SetDefaultExtension(L"sally-license");
                if (save)
                    dialog->SetFileName(L"supporter.sally-license");
                if (SUCCEEDED(dialog->Show(parent)))
                {
                    IShellItem* item = nullptr;
                    if (SUCCEEDED(dialog->GetResult(&item)))
                    {
                        PWSTR path = nullptr;
                        if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)))
                        {
                            result = path;
                            CoTaskMemFree(path);
                        }
                        item->Release();
                    }
                }
                dialog->Release();
            }
            if (SUCCEEDED(init))
                CoUninitialize();
            return result;
        }
        enum
        {
            Input = 1001, Preview = 1002, Import = 1004, Save = 1005, Title = 1006,
            Export = 1007, Remove = 1008, Replace = 1014,
            PasteLabel, StatusLabel, DateLabels, DateValues, LocalName, Hint, PreviewBox, ImportHint
        };
        struct Dialog
        {
            HWND Window = nullptr, KeyActionsTip = nullptr;
            std::wstring KeyActionsHint;
            LANGID Language = 0x409;
            std::wstring Profile;
            IRegistry* Registry = nullptr;
            Preferences Original;
            Certificate Candidate;
            bool Entry = false, Changed = false, Ready = false;
            int Dpi = 96, MinimumClientHeight = 0;
            HFONT Font = nullptr, NameFont = nullptr;
            std::wstring T(UINT id) const { return SupporterText(id, Language); }
            int Px(int n) const { return MulDiv(n, Dpi, 96); }
            HWND Item(int id) const { return GetDlgItem(Window, id); }
            void Text(int id, const std::wstring& value) { SetWindowTextW(Item(id), value.c_str()); }
            int Height(const std::wstring& value, int width, HFONT font = nullptr) const
            {
                HDC dc = GetDC(Window);
                auto old = SelectObject(dc, font ? font : Font);
                RECT bounds{0, 0, width, 0};
                DrawTextW(dc, value.empty() ? L" " : value.c_str(), -1, &bounds, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
                SelectObject(dc, old); ReleaseDC(Window, dc);
                return bounds.bottom;
            }
            int ButtonWidth(int id) const
            {
                HDC dc = GetDC(Window); auto old = SelectObject(dc, Font);
                SIZE size{}; auto label = WindowText(Item(id));
                GetTextExtentPoint32W(dc, label.c_str(), static_cast<int>(label.size()), &size);
                SelectObject(dc, old); ReleaseDC(Window, dc);
                return (std::max)(Px(90), static_cast<int>(size.cx) + Px(28));
            }
            void Place(int id, bool show, int x, int y, int width, int height)
            {
                ShowWindow(Item(id), show ? SW_SHOWNA : SW_HIDE);
                if (show) SetWindowPos(Item(id), nullptr, x, y, width, height, SWP_NOZORDER | SWP_NOACTIVATE);
            }
            void Layout()
            {
                if (!Ready) return;
                RECT client{}; GetClientRect(Window, &client);
                const int margin = Px(20), gap = Px(10), width = (std::max)(Px(100), static_cast<int>(client.right) - 2 * margin);
                const int line = Height(L"Ag", width), button = (std::max)(Px(28), line + Px(10));
                const int cancelWidth = ButtonWidth(IDCANCEL) + (Entry ? 0 : Px(22));
                const int footer = client.bottom - margin - button;
                Place(IDCANCEL, true, client.right - margin - cancelWidth, footer, cancelWidth, button);
                if (Entry)
                {
                    const int saveWidth = ButtonWidth(Save);
                    Place(Save, true, client.right - margin - cancelWidth - gap - saveWidth, footer, saveWidth, button);
                }
                if (KeyActionsTip)
                {
                    TTTOOLINFOW tool{sizeof(tool)};
                    tool.hwnd = Item(IDCANCEL); tool.uId = 1;
                    GetClientRect(tool.hwnd, &tool.rect);
                    tool.rect.left = tool.rect.right - Px(22);
                    SendMessageW(KeyActionsTip, TTM_NEWTOOLRECTW, 0, reinterpret_cast<LPARAM>(&tool));
                }
                int y = margin;
                const bool authentic = Candidate.Authentic();
                const int nameHeight = authentic ? Height(Wide(Candidate.Holder), width - Px(24), NameFont) : 0;
                const int statusHeight = authentic ? Height(WindowText(Item(StatusLabel)), width - Px(24)) : 0;
                const int datesWidth = width - Px(24);
                const int dateLabelWidth = (std::min)(Px(150), datesWidth / 2);
                const int datesHeight = authentic ? (std::max)(Height(WindowText(Item(DateLabels)), dateLabelWidth), Height(WindowText(Item(DateValues)), datesWidth - dateLabelWidth - gap)) : 0;
                if (Entry)
                {
                    const int labelHeight = Height(T(IDS_SUP_PASTE), width);
                    Place(PasteLabel, true, margin, y, width, labelHeight); y += labelHeight + Px(8);
                    const int previewHeight = authentic ? nameHeight + statusHeight + datesHeight + Px(32) : Px(72);
                    const int inputHeight = (std::max)(Px(46), (std::min)(Px(90), footer - y - button - previewHeight - Px(44)));
                    Place(Input, true, margin, y, width, inputHeight); y += inputHeight + gap;
                    const int importWidth = ButtonWidth(Import);
                    Place(Import, true, margin, y, importWidth, button);
                    Place(ImportHint, true, margin + importWidth + gap, y + Px(3), (std::max)(Px(20), width - importWidth - gap), button);
                    y += button + Px(16);
                    Place(PreviewBox, true, margin, y, width, previewHeight);
                    y += Px(10);
                }
                else
                {
                    Place(PasteLabel, false, 0, 0, 0, 0); Place(Input, false, 0, 0, 0, 0);
                    Place(Import, false, 0, 0, 0, 0); Place(ImportHint, false, 0, 0, 0, 0); Place(PreviewBox, false, 0, 0, 0, 0);
                }
                int x = margin + (Entry ? Px(12) : 0);
                int innerWidth = width - (Entry ? Px(24) : 0);
                Place(Hint, Entry && !authentic, x, y, innerWidth, Height(WindowText(Item(Hint)), innerWidth));
                Place(Preview, authentic, x, y, innerWidth, nameHeight); if (authentic) y += nameHeight + Px(4);
                Place(StatusLabel, authentic, x, y, innerWidth, statusHeight); if (authentic) y += statusHeight + Px(12);
                Place(DateLabels, authentic, x, y, dateLabelWidth, datesHeight);
                Place(DateValues, authentic, x + dateLabelWidth + gap, y, (std::max)(Px(20), innerWidth - dateLabelWidth - gap), datesHeight);
                y += datesHeight + Px(24);
                const int titleHeight = Height(T(IDS_SUP_SHOW_TITLE), width - Px(24)) + Px(4);
                Place(Title, !Entry, margin, y, width, titleHeight); y += titleHeight + Px(4);
                const int localNameHeight = Height(T(IDS_SUP_LOCAL_NAME), width - Px(24));
                Place(LocalName, !Entry, margin + Px(24), y, width - Px(24), localNameHeight);
                if (!Entry) MinimumClientHeight = y + localNameHeight + Px(20) + button + margin;
                InvalidateRect(Window, nullptr, TRUE);
            }
            void PreviewCandidate(Certificate value)
            {
                Candidate = std::move(value);
                Text(Preview, Candidate.Authentic() ? Wide(Candidate.Holder) : L"");
                UINT status = Candidate.State == Status::Active ? IDS_SUP_ACTIVE : Candidate.State == Status::NotYetValid ? IDS_SUP_FUTURE : IDS_SUP_EXPIRED;
                Text(StatusLabel, Candidate.Authentic() ? T(status) : L"");
                Text(DateLabels, T(IDS_SUP_SUPPORT_STARTS) + L"\r\n" + T(IDS_SUP_SUPPORTED_THROUGH));
                Text(DateValues, Candidate.Authentic() ? Date(Candidate.ValidFrom) + L"\r\n" + Date(Candidate.ValidUntil - 1) : L"");
                Text(Hint, Candidate.State == Status::Missing ? T(IDS_SUP_ENTRY_HINT) : T(IDS_SUP_INVALID));
                if (Entry) EnableWindow(Item(Save), Candidate.Authentic());
                Layout();
            }
            void ShowKeyMenu()
            {
                HMENU menu = CreatePopupMenu();
                AppendMenuW(menu, MF_STRING, Replace, T(IDS_SUP_REPLACE).c_str());
                AppendMenuW(menu, MF_STRING, Export, T(IDS_SUP_EXPORT).c_str());
                AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
                AppendMenuW(menu, MF_STRING, Remove, T(IDS_SUP_REMOVE).c_str());
                RECT bounds{}; GetWindowRect(Item(IDCANCEL), &bounds);
                SendMessageW(Item(IDCANCEL), BCM_SETDROPDOWNSTATE, TRUE, 0);
                UINT command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTALIGN | TPM_TOPALIGN,
                    bounds.right, bounds.bottom, 0, Window, nullptr);
                SendMessageW(Item(IDCANCEL), BCM_SETDROPDOWNSTATE, FALSE, 0);
                DestroyMenu(menu);
                if (command) SendMessageW(Window, WM_COMMAND, command, 0);
            }
            void ValidateInput() { PreviewCandidate(VerifyCertificate(Utf8(WindowText(Item(Input))), NowUtc(), BuiltinPublicKeys())); }
            void Error(UINT id) { MessageBoxW(Window, T(id).c_str(), T(Entry ? IDS_SUP_ENTER : IDS_SUP_TITLE).c_str(), MB_OK | MB_ICONERROR); }
            bool Persist(const Preferences& next, unsigned fields)
            {
                if (!WriteSupporterState(Profile, *Registry, next, fields).success) { Error(IDS_SUP_WRITE_FAILED); return false; }
                Original = next; Changed = true; return true;
            }
        };
        LRESULT CALLBACK DialogProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
        {
            auto state = reinterpret_cast<Dialog*>(GetWindowLongPtrW(window, GWLP_USERDATA));
            if (message == WM_NCCREATE)
            {
                state = static_cast<Dialog*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);
                state->Window = window; SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
            }
            if (!state) return DefWindowProcW(window, message, wParam, lParam);
            auto& s = *state;
            switch (message)
            {
            case WM_SIZE: s.Layout(); return 0;
            case WM_GETMINMAXINFO:
            {
                RECT minimum{0, 0, s.Px(460), s.MinimumClientHeight};
                AdjustWindowRectEx(&minimum, static_cast<DWORD>(GetWindowLongPtrW(window, GWL_STYLE)), FALSE,
                    static_cast<DWORD>(GetWindowLongPtrW(window, GWL_EXSTYLE)));
                reinterpret_cast<MINMAXINFO*>(lParam)->ptMinTrackSize = {s.Px(460),
                    s.Entry ? s.Px(390) : (std::max)(s.Px(260), static_cast<int>(minimum.bottom - minimum.top))};
                return 0;
            }
            case DM_GETDEFID: return MAKELRESULT(s.Entry ? Save : IDCANCEL, DC_HASDEFID);
            case WM_CTLCOLORSTATIC:
            {
                const int id = GetDlgCtrlID(reinterpret_cast<HWND>(lParam));
                HDC dc = reinterpret_cast<HDC>(wParam);
                const bool preview = s.Entry && (id == Preview || id == StatusLabel || id == DateLabels || id == DateValues || id == Hint);
                SetBkColor(dc, GetSysColor(preview ? COLOR_WINDOW : COLOR_BTNFACE));
                SetTextColor(dc, id == StatusLabel && s.Candidate.State == Status::Active ? RGB(28, 98, 48) :
                    id == LocalName || id == ImportHint || id == DateLabels || id == Hint ? GetSysColor(COLOR_GRAYTEXT) : GetSysColor(COLOR_WINDOWTEXT));
                return reinterpret_cast<LRESULT>(GetSysColorBrush(preview ? COLOR_WINDOW : COLOR_BTNFACE));
            }
            case WM_NOTIFY:
            {
                auto notification = reinterpret_cast<NMHDR*>(lParam);
                if (!s.Entry && notification && notification->idFrom == IDCANCEL && notification->code == BCN_DROPDOWN)
                {
                    s.ShowKeyMenu();
                    return 0;
                }
                break;
            }
            case WM_CONTEXTMENU:
                if (!s.Entry && reinterpret_cast<HWND>(wParam) == s.Item(IDCANCEL))
                {
                    s.ShowKeyMenu();
                    return 0;
                }
                break;
            case WM_COMMAND:
                switch (LOWORD(wParam))
                {
                case Input:
                    if (s.Ready && s.Entry && HIWORD(wParam) == EN_CHANGE) s.ValidateInput();
                    return 0;
                case Import:
                {
                    auto path = PickFile(window, false, s.Language);
                    if (path.empty()) return 0;
                    std::ifstream file(std::filesystem::path(Sally::ToExtendedLengthPath(path)), std::ios::binary);
                    std::string bytes(4097, '\0'); file.read(bytes.data(), static_cast<std::streamsize>(bytes.size())); bytes.resize(static_cast<size_t>(file.gcount()));
                    auto candidate = VerifyCertificate(bytes, NowUtc(), BuiltinPublicKeys(), true);
                    if (file.bad() || !candidate.Authentic()) { s.Error(IDS_SUP_INVALID); return 0; }
                    SetWindowTextW(s.Item(Input), Wide(candidate.Token).c_str());
                    s.ValidateInput();
                    return 0;
                }
                case Title:
                {
                    if (s.Entry) return 0;
                    auto next = ReadSupporterState(s.Profile, *s.Registry);
                    const bool previous = next.ShowNameInTitle;
                    next.ShowNameInTitle = VerifyCertificate(next.Token, NowUtc(), BuiltinPublicKeys()).Authentic() &&
                        SendMessageW(s.Item(Title), BM_GETCHECK, 0, 0) == BST_CHECKED;
                    const bool saved = s.Persist(next, TitleField);
                    SendMessageW(s.Item(Title), BM_SETCHECK, (saved ? next.ShowNameInTitle : previous) ? BST_CHECKED : BST_UNCHECKED, 0);
                    return 0;
                }
                case Save:
                {
                    if (!s.Entry) return 0;
                    s.ValidateInput();
                    if (!s.Candidate.Authentic()) return 0;
                    auto next = ReadSupporterState(s.Profile, *s.Registry);
                    next.ShowNameInTitle = VerifyCertificate(next.Token, NowUtc(), BuiltinPublicKeys()).Authentic() && next.ShowNameInTitle;
                    next.Token = s.Candidate.Token;
                    if (s.Persist(next, CertificateField | TitleField)) DestroyWindow(window);
                    return 0;
                }
                case Replace:
                {
                    if (ShowSupporterDialog(window, s.Language, s.Profile, *s.Registry, true))
                    {
                        s.Original = ReadSupporterState(s.Profile, *s.Registry); s.Changed = true;
                        s.PreviewCandidate(VerifyCertificate(s.Original.Token, NowUtc(), BuiltinPublicKeys()));
                        SendMessageW(s.Item(Title), BM_SETCHECK, s.Original.ShowNameInTitle ? BST_CHECKED : BST_UNCHECKED, 0);
                    }
                    return 0;
                }
                case Export:
                {
                    auto certificate = VerifyCertificate(ReadSupporterState(s.Profile, *s.Registry).Token, NowUtc(), BuiltinPublicKeys());
                    if (!certificate.Authentic()) { s.Error(IDS_SUP_INVALID); return 0; }
                    if (MessageBoxW(window, s.T(IDS_SUP_EXPORT_NOTICE).c_str(), s.T(IDS_SUP_TITLE).c_str(), MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) != IDYES) return 0;
                    auto path = PickFile(window, true, s.Language);
                    if (path.empty()) return 0;
                    std::ofstream file(std::filesystem::path(Sally::ToExtendedLengthPath(path)), std::ios::binary | std::ios::trunc);
                    file << certificate.Token << '\n'; file.close();
                    if (!file) s.Error(IDS_SUP_WRITE_FAILED);
                    return 0;
                }
                case Remove:
                    if (MessageBoxW(window, s.T(IDS_SUP_REMOVE_CONFIRM).c_str(), s.T(IDS_SUP_TITLE).c_str(), MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) == IDYES)
                    {
                        auto next = ReadSupporterState(s.Profile, *s.Registry); next.Token.clear(); next.ShowNameInTitle = false;
                        if (s.Persist(next, CertificateField | TitleField)) DestroyWindow(window);
                    }
                    return 0;
                case IDCANCEL: DestroyWindow(window); return 0;
                }
                break;
            case WM_CLOSE: DestroyWindow(window); return 0;
            }
            return DefWindowProcW(window, message, wParam, lParam);
        }
    }

    std::wstring SupporterText(UINT id, LANGID language)
    {
        auto text = LoadText(id, language);
        return text.empty() ? LoadText(id, 0x409) : text;
    }
    std::wstring SupporterSummary(const Certificate& certificate, LANGID language)
    {
        UINT id = certificate.State == Status::Active ? IDS_SUP_ACTIVE : certificate.State == Status::NotYetValid ? IDS_SUP_FUTURE
                                                                     : certificate.State == Status::Expired       ? IDS_SUP_EXPIRED
                                                                     : certificate.State == Status::Invalid       ? IDS_SUP_INVALID
                                                                                                                  : IDS_SUP_FREE;
        std::wstring result = SupporterText(id, language);
        if (certificate.Authentic())
        {
            result = Wide(certificate.Holder) + L"\r\n" + result + L"\r\n" + SupporterText(IDS_SUP_DATES, language) + L": " + Date(certificate.ValidFrom) + L" — " + Date(certificate.ValidUntil - 1);
        }
        return result;
    }
    void OpenSupportPage(HWND parent) { ShellExecuteW(parent, L"open", Sally::Urls::Support, nullptr, nullptr, SW_SHOWNORMAL); }
    std::wstring SupporterTitleSuffix(const std::wstring& profile, IRegistry& registry, LANGID language)
    {
        const auto state = ReadSupporterState(profile, registry);
        if (!state.ShowNameInTitle)
            return {};
        auto certificate = VerifyCertificate(state.Token, NowUtc(), BuiltinPublicKeys());
        return certificate.Authentic() ? L" — " + SupporterText(IDS_SUP_SUPPORTED_BY, language) + L" " + Wide(TitleHolder(certificate.Holder)) : std::wstring();
    }
    bool ShowSupporterDialog(HWND parent, LANGID language, const std::wstring& profile, IRegistry& registry, bool enterKey)
    {
        INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_STANDARD_CLASSES | ICC_WIN95_CLASSES};
        InitCommonControlsEx(&controls);
        Dialog s;
        s.Language = language; s.Profile = profile; s.Registry = &registry;
        s.Original = ReadSupporterState(profile, registry);
        s.Candidate = VerifyCertificate(s.Original.Token, NowUtc(), BuiltinPublicKeys());
        s.Entry = enterKey || !s.Candidate.Authentic();
        HDC dc = GetDC(parent); s.Dpi = GetDeviceCaps(dc, LOGPIXELSY); ReleaseDC(parent, dc);
        NONCLIENTMETRICSW metrics = {sizeof(metrics)}; SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0);
        s.Font = CreateFontIndirectW(&metrics.lfMessageFont);
        auto large = metrics.lfMessageFont; large.lfWeight = FW_SEMIBOLD; large.lfHeight = MulDiv(large.lfHeight, 5, 4); s.NameFont = CreateFontIndirectW(&large);
        WNDCLASSW wc{}; wc.lpfnWndProc = DialogProc; wc.hInstance = GetModuleHandleW(nullptr); wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1); wc.lpszClassName = s.Entry ? L"SallySupporterKeyEntry" : L"SallySupporterDetails";
        RegisterClassW(&wc);
        MONITORINFO monitor{sizeof(monitor)}; GetMonitorInfoW(MonitorFromWindow(parent, MONITOR_DEFAULTTOPRIMARY), &monitor);
        const auto& work = monitor.rcWork;
        DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME;
        RECT frame{0, 0, s.Px(s.Entry ? 560 : 520), s.Px(s.Entry ? 380 : 240)}; AdjustWindowRectEx(&frame, style, FALSE, WS_EX_DLGMODALFRAME | WS_EX_CONTROLPARENT);
        int width = (std::min)(frame.right - frame.left, work.right - work.left), height = (std::min)(frame.bottom - frame.top, work.bottom - work.top);
        HWND window = CreateWindowExW(WS_EX_DLGMODALFRAME | WS_EX_CONTROLPARENT, wc.lpszClassName, s.T(s.Entry ? IDS_SUP_ENTER : IDS_SUP_TITLE).c_str(), style,
            work.left + (work.right - work.left - width) / 2, work.top + (work.bottom - work.top - height) / 2, width, height, parent, nullptr, wc.hInstance, &s);
        if (!window) { DeleteObject(s.NameFont); DeleteObject(s.Font); return false; }
        auto add = [&](const wchar_t* cls, int id, const std::wstring& text, DWORD controlStyle, HFONT font = nullptr, DWORD ex = 0)
        {
            HWND control = CreateWindowExW(ex, cls, text.c_str(), WS_CHILD | controlStyle, 0, 0, 10, 10, window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), wc.hInstance, nullptr);
            SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font ? font : s.Font), FALSE); return control;
        };
        add(L"STATIC", PreviewBox, L"", SS_WHITERECT, nullptr, WS_EX_STATICEDGE);
        add(L"STATIC", PasteLabel, s.T(IDS_SUP_PASTE), SS_NOPREFIX);
        HWND input = add(L"EDIT", Input, L"", WS_TABSTOP | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL, nullptr, WS_EX_CLIENTEDGE);
        SendMessageW(input, EM_SETLIMITTEXT, 4096, 0);
        add(L"BUTTON", Import, s.T(IDS_SUP_IMPORT), WS_TABSTOP);
        add(L"STATIC", ImportHint, s.T(IDS_SUP_IMPORT_HINT), SS_NOPREFIX);
        add(L"STATIC", Preview, L"", SS_NOPREFIX, s.NameFont);
        add(L"STATIC", StatusLabel, L"", SS_NOPREFIX);
        add(L"STATIC", DateLabels, L"", SS_NOPREFIX);
        add(L"STATIC", DateValues, L"", SS_NOPREFIX);
        add(L"STATIC", Hint, s.T(IDS_SUP_ENTRY_HINT), SS_NOPREFIX);
        add(L"BUTTON", Title, s.T(IDS_SUP_SHOW_TITLE), WS_TABSTOP | BS_AUTOCHECKBOX | BS_MULTILINE);
        add(L"STATIC", LocalName, s.T(IDS_SUP_LOCAL_NAME), SS_NOPREFIX);
        if (s.Entry) add(L"BUTTON", Save, s.T(IDS_SUP_SAVE_KEY), WS_TABSTOP | BS_DEFPUSHBUTTON);
        add(L"BUTTON", IDCANCEL, s.T(s.Entry ? IDS_SUP_CANCEL : IDS_SUP_CLOSE), WS_TABSTOP | (s.Entry ? 0 : BS_DEFSPLITBUTTON));
        if (!s.Entry)
        {
            BUTTON_SPLITINFO split{}; split.mask = BCSIF_SIZE; split.size.cx = s.Px(22);
            SendMessageW(s.Item(IDCANCEL), BCM_SETSPLITINFO, 0, reinterpret_cast<LPARAM>(&split));
            s.KeyActionsHint = s.T(IDS_SUP_KEY_ACTIONS);
            s.KeyActionsTip = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr,
                WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX, CW_USEDEFAULT, CW_USEDEFAULT,
                CW_USEDEFAULT, CW_USEDEFAULT, window, nullptr, wc.hInstance, nullptr);
            if (s.KeyActionsTip)
            {
                TTTOOLINFOW tool{sizeof(tool)}; tool.uFlags = TTF_SUBCLASS | TTF_TRANSPARENT;
                tool.hwnd = s.Item(IDCANCEL); tool.uId = 1; tool.lpszText = s.KeyActionsHint.data();
                SendMessageW(s.KeyActionsTip, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&tool));
            }
        }
        s.Ready = true;
        if (s.Entry) s.Candidate = {}; // Key entry never exposes the previously saved token.
        s.PreviewCandidate(s.Candidate);
        if (!s.Entry)
        {
            frame = {0, 0, s.Px(520), (std::max)(s.Px(240), s.MinimumClientHeight)};
            AdjustWindowRectEx(&frame, style, FALSE, WS_EX_DLGMODALFRAME | WS_EX_CONTROLPARENT);
            width = (std::min)(frame.right - frame.left, work.right - work.left);
            height = (std::min)(frame.bottom - frame.top, work.bottom - work.top);
            SetWindowPos(window, nullptr, work.left + (work.right - work.left - width) / 2,
                work.top + (work.bottom - work.top - height) / 2, width, height, SWP_NOZORDER | SWP_NOACTIVATE);
        }
        SendMessageW(s.Item(Title), BM_SETCHECK, s.Original.ShowNameInTitle ? BST_CHECKED : BST_UNCHECKED, 0);
        if (s.Entry) EnableWindow(s.Item(Save), FALSE);
        bool enabled = parent && IsWindowEnabled(parent); if (enabled) EnableWindow(parent, FALSE);
        ShowWindow(window, SW_SHOW); SetFocus(s.Item(s.Entry ? Input : IDCANCEL));
        MSG message{};
        while (IsWindow(window))
        {
            BOOL got = GetMessageW(&message, nullptr, 0, 0);
            if (got <= 0) { if (!got) PostQuitMessage(static_cast<int>(message.wParam)); break; }
            if (!s.Entry && message.hwnd == s.Item(IDCANCEL) &&
                (message.message == WM_KEYDOWN || message.message == WM_SYSKEYDOWN) && message.wParam == VK_DOWN)
            { s.ShowKeyMenu(); continue; }
            if (message.hwnd == input && message.message == WM_KEYDOWN && message.wParam == VK_TAB)
            { SetFocus(GetNextDlgTabItem(window, input, (GetKeyState(VK_SHIFT) & 0x8000) != 0)); continue; }
            if (!IsDialogMessageW(window, &message)) { TranslateMessage(&message); DispatchMessageW(&message); }
        }
        if (IsWindow(s.KeyActionsTip)) DestroyWindow(s.KeyActionsTip);
        if (IsWindow(window)) DestroyWindow(window);
        if (enabled && IsWindow(parent)) { EnableWindow(parent, TRUE); SetActiveWindow(parent); }
        DeleteObject(s.NameFont); DeleteObject(s.Font); return s.Changed;
    }
} // namespace Sally::Supporter
