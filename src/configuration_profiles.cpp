// SPDX-FileCopyrightText: 2025-2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later
#include "precomp.h"
#include "configuration_profiles.h"
#include "common/ConfigProfileStore.h"
#include "common/CommandLineParser.h"
#include "ui/IPrompter.h"
#include "dialogs.h"
#include "darkmode.h"
#include "tasklist.h"

namespace
{
std::wstring UsualRoot;
std::wstring ActiveProfile;
std::wstring StartupChoice;
bool ChoiceLoaded = false;
bool ChoiceChanged = false;
std::wstring ObjectPrefix;
ATOM RootAtom = 0;
constexpr const wchar_t* RootProperty = L"Sally.ConfigurationRoot";
struct Handle
{
    HANDLE value = nullptr;
    ~Handle() { if (value) CloseHandle(value); }
} Usage;
Handle StartupGate;
bool StartupGateHeld = false;

HANDLE SharedMutex(const std::wstring& name)
{
    HANDLE mutex = CreateMutexW(nullptr, FALSE, name.c_str());
    return mutex ? mutex : OpenMutexW(SYNCHRONIZE, FALSE, name.c_str());
}

std::wstring UsageName(const std::wstring& name)
{
    std::wstring result = ObjectPrefix + L".Usage.";
    for (wchar_t ch : name) result += ch >= L'a' && ch <= L'z' ? ch - L'a' + L'A' : ch;
    return result;
}
struct CatalogLock
{
    Handle mutex;
    bool held = false;
    CatalogLock()
    {
        mutex.value = SharedMutex(ObjectPrefix + L".Catalog");
        if (mutex.value)
        {
            const DWORD result = WaitForSingleObject(mutex.value, 5000);
            held = result == WAIT_OBJECT_0 || result == WAIT_ABANDONED;
        }
    }
    ~CatalogLock() { if (held) ReleaseMutex(mutex.value); }
};
bool InUse(const std::wstring& name)
{
    Handle usage;
    usage.value = OpenMutexW(SYNCHRONIZE, FALSE, UsageName(name).c_str());
    // Access denied also means the object exists: do not delete an elevated instance's profile.
    return usage.value != nullptr || GetLastError() != ERROR_FILE_NOT_FOUND;
}
IRegistry& Registry() { return *(gRegistry ? gRegistry : GetWin32Registry()); }
void Error(HWND parent, LONG error)
{
    gPrompter->ShowError(parent, LoadStrW(IDS_ERRORTITLE), GetErrorTextOwned(error).c_str());
}
class ProfileManager : public CCommonDialog
{
    std::vector<std::wstring> Names;
    std::wstring DraftStartup;
    bool Protected(const std::wstring& name)
    {
        return name.empty() || _wcsicmp(name.c_str(), ActiveProfile.c_str()) == 0 ||
            _wcsicmp(name.c_str(), StartupChoice.c_str()) == 0 ||
            _wcsicmp(name.c_str(), DraftStartup.c_str()) == 0 || InUse(name) || TaskList.HasUnknownProfilePeer();
    }
    void Fill()
    {
        sally::profile::Store store(Registry(), UsualRoot);
        const auto result = store.List(Names);
        if (!result.success) Error(HWindow, result.errorCode);
        SendDlgItemMessageW(HWindow, IDC_PROFILE_LIST, LB_RESETCONTENT, 0, 0);
        for (const auto& name : Names)
            SendDlgItemMessageW(HWindow, IDC_PROFILE_LIST, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(name.c_str()));
        const HWND list = GetDlgItem(HWindow, IDC_PROFILE_LIST);
        HDC dc = GetDC(list);
        int width = 0;
        if (dc)
        {
            HGDIOBJ previous = SelectObject(dc, reinterpret_cast<HFONT>(SendMessageW(list, WM_GETFONT, 0, 0)));
            for (const auto& name : Names)
            {
                SIZE size{};
                if (GetTextExtentPoint32W(dc, name.c_str(), static_cast<int>(name.size()), &size))
                    width = (std::max)(width, static_cast<int>(size.cx) + 8);
            }
            if (previous && previous != HGDI_ERROR) SelectObject(dc, previous);
            ReleaseDC(list, dc);
        }
        SendMessageW(list, LB_SETHORIZONTALEXTENT, width, 0);
        EnableWindow(GetDlgItem(HWindow, IDC_PROFILE_DELETE), FALSE);
    }
    std::wstring Selection()
    {
        const auto index = SendDlgItemMessageW(HWindow, IDC_PROFILE_LIST, LB_GETCURSEL, 0, 0);
        return index >= 0 && static_cast<size_t>(index) < Names.size() ? Names[index] : L"";
    }
    void SelectionChanged()
    {
        const auto name = Selection();
        sally::profile::Store store(Registry(), UsualRoot);
        std::wstring startup;
        const auto result = store.ReadStartup(startup);
        const bool removable = result.success && !Protected(name) && _wcsicmp(name.c_str(), startup.c_str()) != 0;
        EnableWindow(GetDlgItem(HWindow, IDC_PROFILE_DELETE), removable);
    }
    INT_PTR DialogProc(UINT message, WPARAM wParam, LPARAM lParam) override
    {
        if (message == WM_INITDIALOG)
        {
            CCommonDialog::DialogProc(message, wParam, lParam);
            SendDlgItemMessageW(HWindow, IDC_PROFILE_NAME, EM_SETLIMITTEXT, 64, 0);
            Fill();
            return TRUE;
        }
        if (message == WM_COMMAND)
        {
            const int id = LOWORD(wParam);
            if (id == IDC_PROFILE_LIST && HIWORD(wParam) == LBN_SELCHANGE) { SelectionChanged(); return TRUE; }
            if (id == IDC_PROFILE_CREATE)
            {
                const int length = GetWindowTextLengthW(GetDlgItem(HWindow, IDC_PROFILE_NAME));
                std::wstring name(static_cast<size_t>(length) + 1, L'\0');
                GetDlgItemTextW(HWindow, IDC_PROFILE_NAME, name.data(), static_cast<int>(name.size()));
                name.resize(length);
                if (!sally::profile::IsValidProfileName(name))
                {
                    gPrompter->ShowError(HWindow, LoadStrW(IDS_ERRORTITLE), LoadStrW(IDS_PROFILE_NAME_RULE));
                    SetFocus(GetDlgItem(HWindow, IDC_PROFILE_NAME));
                    return TRUE;
                }
                CatalogLock lock;
                if (!lock.held) { Error(HWindow, ERROR_BUSY); return TRUE; }
                sally::profile::Store store(Registry(), UsualRoot);
                const auto result = store.Create(name);
                if (!result.success) Error(HWindow, result.errorCode);
                else { Fill(); SetDlgItemTextW(HWindow, IDC_PROFILE_NAME, L""); }
                return TRUE;
            }
            if (id == IDC_PROFILE_DELETE)
            {
                const auto name = Selection();
                if (name.empty()) return TRUE;
                const auto question = LoadStrOwned(IDS_PROFILE_DELETE_CONFIRM) + L"\n\n" + name;
                if (gPrompter->AskYesNo(HWindow, LoadStrW(IDS_PROFILE_MANAGER), question.c_str()).type != PromptResult::kYes) return TRUE;
                CatalogLock lock;
                if (!lock.held || Protected(name)) { Error(HWindow, ERROR_BUSY); return TRUE; }
                sally::profile::Store store(Registry(), UsualRoot);
                const auto result = store.Remove(name, ActiveProfile);
                if (!result.success) Error(HWindow, result.errorCode);
                Fill();
                return TRUE;
            }
        }
        return CCommonDialog::DialogProc(message, wParam, lParam);
    }
public:
    ProfileManager(HWND parent, const std::wstring& draft) : CCommonDialog(HLanguage, IDD_PROFILE_MANAGER, parent), DraftStartup(draft) {}
};
}
LONG InitializeConfigurationProfiles(const wchar_t* usualRoot)
{
    UsualRoot = usualRoot;
    LPWSTR sid = nullptr;
    if (!GetStringSid(&sid)) return ERROR_ACCESS_DENIED;
    ObjectPrefix = L"Global\\Sally.Profiles." + std::wstring(sid) + L".";
    LocalFree(sid);
    // Reversible hex, rather than a lossy path conversion or a colliding hash.
    for (wchar_t ch : UsualRoot)
    {
        if (ch >= L'a' && ch <= L'z') ch -= L'a' - L'A';
        const wchar_t* digits = L"0123456789ABCDEF";
        for (int shift = 12; shift >= 0; shift -= 4) ObjectPrefix += digits[(ch >> shift) & 15];
    }
    {
        CatalogLock lock;
        if (!lock.held) return ERROR_BUSY;
        const auto request = sally::cmdline::ParseRawCommandLine(GetCommandLineW());
        if (!request.ok()) return ERROR_SUCCESS; // existing command-line error handling owns the diagnostic
        sally::profile::Store store(Registry(), UsualRoot);
        std::wstring remembered;
        if (!request.profileSpecified)
        {
            const auto result = store.ReadStartup(remembered);
            if (!result.success) return result.errorCode;
            StartupChoice = remembered;
            ChoiceLoaded = true;
        }
        ActiveProfile = sally::profile::ResolveStartup(request.profile, request.profileSpecified, remembered, true);
        Usage.value = SharedMutex(UsageName(ActiveProfile));
        RootAtom = GlobalAddAtomW(store.Root(ActiveProfile).c_str());
        if (!Usage.value || !RootAtom) return ERROR_NOT_ENOUGH_MEMORY;
    }
    // Register one same-profile startup at a time. Otherwise two -o launches can
    // each wait for a STARTING peer blocked on the activation-dispatch mutex.
    // Do not hold the catalog lock while waiting for another process to finish.
    StartupGate.value = SharedMutex(UsageName(ActiveProfile) + L".Startup");
    if (!StartupGate.value) return ERROR_ACCESS_DENIED;
    const DWORD wait = WaitForSingleObject(StartupGate.value, 30000);
    StartupGateHeld = wait == WAIT_OBJECT_0 || wait == WAIT_ABANDONED;
    return StartupGateHeld ? ERROR_SUCCESS : ERROR_BUSY;
}
const std::wstring& ActiveConfigurationProfile() { return ActiveProfile; }
const std::wstring& UsualConfigurationRoot() { return UsualRoot; }
void ManageConfigurationProfiles(HWND parent, const std::wstring& draftStartup) { ProfileManager(parent, draftStartup).Execute(); }
LONG ReadConfigurationProfileChoice(std::wstring& name)
{
    if (!ChoiceChanged)
    {
        sally::profile::Store store(Registry(), UsualRoot);
        std::wstring current;
        const auto result = store.ReadStartup(current);
        if (!result.success)
        {
            ChoiceLoaded = false;
            name.clear();
            return result.errorCode;
        }
        StartupChoice = std::move(current);
        ChoiceLoaded = true;
    }
    name = StartupChoice;
    return ERROR_SUCCESS;
}
void ApplyConfigurationProfileChoice(const std::wstring& name)
{
    if (!ChoiceLoaded || _wcsicmp(StartupChoice.c_str(), name.c_str()) != 0)
    {
        StartupChoice = name;
        ChoiceChanged = true;
    }
    ChoiceLoaded = true;
}
LONG SaveConfigurationProfileChoice()
{
    if (!ChoiceChanged) return ERROR_SUCCESS;
    CatalogLock lock;
    if (!lock.held) return ERROR_BUSY;
    sally::profile::Store store(Registry(), UsualRoot);
    const auto result = store.WriteStartup(StartupChoice);
    if (result.success) ChoiceChanged = false;
    return result.errorCode;
}
bool PublishConfigurationProfile(HWND mainWindow)
{
    return RootAtom && SetPropW(mainWindow, RootProperty, reinterpret_cast<HANDLE>(static_cast<ULONG_PTR>(RootAtom)));
}
void FinishConfigurationProfileStartup()
{
    if (StartupGateHeld) ReleaseMutex(StartupGate.value);
    StartupGateHeld = false;
    if (StartupGate.value) CloseHandle(StartupGate.value);
    StartupGate.value = nullptr;
}
bool MatchesConfigurationProfile(HWND mainWindow, DWORD processId)
{
    DWORD owner = 0;
    GetWindowThreadProcessId(mainWindow, &owner);
    return owner == processId && RootAtom && reinterpret_cast<ULONG_PTR>(GetPropW(mainWindow, RootProperty)) == RootAtom;
}
void ReleaseConfigurationProfiles()
{
    FinishConfigurationProfileStartup();
    if (RootAtom) GlobalDeleteAtom(RootAtom);
    RootAtom = 0;
    if (Usage.value) CloseHandle(Usage.value);
    Usage.value = nullptr;
    std::wstring().swap(UsualRoot);
    std::wstring().swap(ActiveProfile);
    std::wstring().swap(StartupChoice);
    ChoiceLoaded = ChoiceChanged = false;
    std::wstring().swap(ObjectPrefix);
}
