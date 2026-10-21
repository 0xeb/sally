// SPDX-FileCopyrightText: 2025-2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"

#include "dialogs_command_shell.h"

#include "resource.rh2"

namespace
{
    bool SameTarget(const ShellTarget& left, const ShellTarget& right)
    {
        if (left.kind != right.kind)
            return false;
        if (left.kind != ShellTargetKind::WindowsTerminalProfile)
            return true;
        if (!left.profileGuid.empty() && !right.profileGuid.empty())
            return _wcsicmp(left.profileGuid.c_str(), right.profileGuid.c_str()) == 0;
        return _wcsicmp(left.profileName.c_str(), right.profileName.c_str()) == 0;
    }

    class CDefaultShellDialog : public CCommonDialog
    {
    public:
        CDefaultShellDialog(HWND parent, const WindowsTerminalCatalog& catalog,
                            const ShellTarget& current)
            : CCommonDialog(HLanguage, IDD_DEFAULT_SHELL, parent), Current(current)
        {
            ShellTarget resolved;
            if (ResolveWindowsTerminalTarget(catalog, current, resolved))
                Current = std::move(resolved);
            Choices.push_back(ShellTarget{});
            if (catalog.installed)
            {
                ShellTarget terminalDefault;
                terminalDefault.kind = ShellTargetKind::WindowsTerminalDefault;
                Choices.push_back(std::move(terminalDefault));
                for (const WindowsTerminalProfile& profile : catalog.profiles)
                {
                    ShellTarget target;
                    target.kind = ShellTargetKind::WindowsTerminalProfile;
                    target.profileGuid = profile.guid;
                    target.profileName = profile.name;
                    Choices.push_back(std::move(target));
                }
            }
        }

        ShellTarget Selected;

    protected:
        INT_PTR DialogProc(UINT message, WPARAM wParam, LPARAM lParam) override
        {
            if (message == WM_COMMAND && LOWORD(wParam) == IDOK)
            {
                int index = static_cast<int>(SendDlgItemMessage(HWindow, IDC_DEFAULT_SHELL_TARGET,
                                                                CB_GETCURSEL, 0, 0));
                if (index >= 0 && index < static_cast<int>(Choices.size()))
                    Selected = Choices[index];
            }

            INT_PTR result = CCommonDialog::DialogProc(message, wParam, lParam);
            if (message == WM_INITDIALOG)
            {
                HWND combo = GetDlgItem(HWindow, IDC_DEFAULT_SHELL_TARGET);
                int selectedIndex = -1;
                for (size_t index = 0; index < Choices.size(); index++)
                {
                    const ShellTarget& target = Choices[index];
                    const wchar_t* label = nullptr;
                    if (target.kind == ShellTargetKind::ComSpec)
                        label = LoadStrW(IDS_SHELL_COMSPEC);
                    else if (target.kind == ShellTargetKind::WindowsTerminalDefault)
                        label = LoadStrW(IDS_SHELL_WT_DEFAULT);
                    else
                        label = target.profileName.c_str();
                    SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label));
                    if (selectedIndex < 0 && SameTarget(target, Current))
                        selectedIndex = static_cast<int>(index);
                }
                SendMessage(combo, CB_SETCURSEL, selectedIndex < 0 ? 0 : selectedIndex, 0);
                return TRUE;
            }

            return result;
        }

    private:
        std::vector<ShellTarget> Choices;
        ShellTarget Current;
    };
} // namespace

bool ShowDefaultShellDialog(HWND parent, const WindowsTerminalCatalog& catalog,
                            const ShellTarget& current, ShellTarget& selected)
{
    CDefaultShellDialog dialog(parent, catalog, current);
    if (dialog.Execute() != IDOK)
        return false;
    selected = std::move(dialog.Selected);
    return true;
}
