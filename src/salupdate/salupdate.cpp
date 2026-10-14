// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

// salupdate.exe: Sally's updater. Sally starts it to look for a newer release on GitHub (the
// update window, --check), to report how an update went (--report), and the updater runs itself
// to install a release (--prepare, --apply). Run by hand with a downloaded release it installs
// that. See common/update/UpdaterCommand.h for the command lines and salupdate.h for the parts.
// salupdate.log, next to the result file, keeps the details.

#include <windows.h>
#include <shellapi.h>

#include <algorithm>
#include <string>
#include <vector>

#include "ExtendedLengthPath.h"
#include "common/BuiltinLanguages.h"
#include "registry_names.h"
#include "salupdate.h"
#include "salupdate.rh"
#include "ui_style.h"
#include "Win32TextCodec.h"
#include "update/UpdateProcesses.h"
#include "update/UpdaterCommand.h"

using namespace Sally::Update;

namespace salupdate
{

std::wstring LogPath;

void Log(const std::wstring& text)
{
    if (LogPath.empty())
        return;
    SYSTEMTIME now;
    GetLocalTime(&now);
    wchar_t stamp[64];
    swprintf_s(stamp, L"%04u-%02u-%02u %02u:%02u:%02u [%lu] ", now.wYear, now.wMonth, now.wDay, now.wHour,
               now.wMinute, now.wSecond, GetCurrentProcessId());
    const std::wstring line = stamp + text + L"\r\n";
    std::string utf8;
    if (!Win32EncodeText(CP_UTF8, line, utf8))
        return;
    HANDLE file = CreateFileW(Sally::ToExtendedLengthPath(LogPath).c_str(), FILE_APPEND_DATA,
                              FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE)
        return;
    DWORD written = 0;
    WriteFile(file, utf8.data(), static_cast<DWORD>(utf8.size()), &written, NULL);
    CloseHandle(file);
}

std::wstring ParentFolder(const std::wstring& path)
{
    const size_t slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? std::wstring() : path.substr(0, slash);
}

std::wstring ModulePath()
{
    return ModuleFilePath(NULL);
}

bool RemoveTree(const std::wstring& folder)
{
    WIN32_FIND_DATAW find;
    HANDLE handle = FindFirstFileExW(Sally::ToExtendedLengthPath(folder + L"\\*").c_str(), FindExInfoBasic, &find,
                                     FindExSearchNameMatch, NULL, 0);
    if (handle != INVALID_HANDLE_VALUE)
    {
        do
        {
            const std::wstring name = find.cFileName;
            if (name == L"." || name == L"..")
                continue;
            const std::wstring child = folder + L"\\" + name;
            const std::wstring extended = Sally::ToExtendedLengthPath(child);
            if ((find.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
            {
                // A link to a folder is removed, never followed.
                if ((find.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) == 0 && !RemoveTree(child))
                {
                    FindClose(handle);
                    return false;
                }
                if ((find.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0 && !RemoveDirectoryW(extended.c_str()))
                {
                    FindClose(handle);
                    return false;
                }
            }
            else
            {
                if ((find.dwFileAttributes & FILE_ATTRIBUTE_READONLY) != 0)
                    SetFileAttributesW(extended.c_str(), find.dwFileAttributes & ~FILE_ATTRIBUTE_READONLY);
                if (!DeleteFileW(extended.c_str()))
                {
                    FindClose(handle);
                    return false;
                }
            }
        } while (FindNextFileW(handle, &find));
        FindClose(handle);
    }
    const BOOL removed = RemoveDirectoryW(Sally::ToExtendedLengthPath(folder).c_str());
    return removed || GetLastError() == ERROR_FILE_NOT_FOUND || GetLastError() == ERROR_PATH_NOT_FOUND;
}

LANGID Language = sally::languages::kEnglishLangId;

namespace
{

std::wstring LoadTextFor(UINT id, LANGID language)
{
    HMODULE module = GetModuleHandleW(NULL);
    HRSRC found = FindResourceExW(module, RT_STRING, MAKEINTRESOURCEW(id / 16 + 1), language);
    if (found == NULL)
        return std::wstring();
    HGLOBAL loaded = LoadResource(module, found);
    const WCHAR* data = loaded != NULL ? static_cast<const WCHAR*>(LockResource(loaded)) : NULL;
    if (data == NULL)
        return std::wstring();
    const WCHAR* end = data + SizeofResource(module, found) / sizeof(WCHAR);
    for (UINT i = 0; i < id % 16; i++)
    {
        if (data >= end)
            return std::wstring();
        data += 1 + *data;
    }
    if (data >= end || data + 1 + *data > end)
        return std::wstring();
    return std::wstring(data + 1, *data);
}

} // namespace

std::wstring Text(UINT id)
{
    std::wstring text = LoadTextFor(id, Language);
    return text.empty() ? LoadTextFor(id, sally::languages::kEnglishLangId) : text;
}

namespace
{

// The language Sally is set to (its configuration in the registry), else the first of the user's
// languages Sally has, else English.
void ChooseLanguage()
{
    std::wstring configuredName;
    // The configuration of the Sally that started us (a -profile has its own), else the usual one.
    std::wstring root(SAL_REG_ROOT_SALLY_1_0_W);
    wchar_t started[512] = {};
    const DWORD length = GetEnvironmentVariableW(SAL_ENV_CONFIG_ROOT_W, started, _countof(started));
    if (length > 0 && length < _countof(started))
        root = started;
    HKEY key = NULL;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, (root + L"\\Configuration").c_str(), 0, KEY_QUERY_VALUE, &key) ==
        ERROR_SUCCESS)
    {
        wchar_t name[64] = {};
        DWORD size = sizeof(name) - sizeof(wchar_t);
        DWORD type = 0;
        if (RegQueryValueExW(key, L"Language", NULL, &type, reinterpret_cast<BYTE*>(name), &size) == ERROR_SUCCESS &&
            type == REG_SZ)
        {
            configuredName = name;
        }
        RegCloseKey(key);
    }
    std::vector<LANGID> available;
    for (const sally::languages::BuiltinLanguage& language : sally::languages::kBuiltinLanguages)
        available.push_back(language.LangId);
    std::vector<std::wstring> preferred;
    ULONG count = 0;
    ULONG chars = 0;
    if (GetUserPreferredUILanguages(MUI_LANGUAGE_NAME, &count, NULL, &chars) && chars > 0)
    {
        std::wstring names(chars, L'\0');
        if (GetUserPreferredUILanguages(MUI_LANGUAGE_NAME, &count, &names[0], &chars))
        {
            for (size_t start = 0; start < names.size() && names[start] != L'\0';)
            {
                const size_t end = names.find(L'\0', start);
                preferred.push_back(names.substr(start, end - start));
                start = end + 1;
            }
        }
    }
    Language = sally::languages::ChooseConfiguredOrDefault(configuredName.c_str(), available, preferred);
}

} // namespace

namespace
{
thread_local HFONT MessageFont = NULL;

BOOL CALLBACK StyleMessageControl(HWND control, LPARAM)
{
    SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(MessageFont), TRUE);
    const int id = GetDlgCtrlID(control);
    const UINT text = id == IDYES ? IDS_UPDATER_YES : id == IDNO ? IDS_UPDATER_NO
                                                   : id == IDOK ? IDS_UPDATER_OK : 0;
    if (text != 0)
        SetWindowTextW(control, Text(text).c_str());
    return TRUE;
}

LRESULT CALLBACK StyleMessageBox(int code, WPARAM wParam, LPARAM lParam)
{
    if (code == HCBT_ACTIVATE)
    {
        HWND window = reinterpret_cast<HWND>(wParam);
        wchar_t className[32] = {};
        GetClassNameW(window, className, _countof(className));
        if (lstrcmpW(className, L"#32770") != 0)
            return CallNextHookEx(NULL, code, wParam, lParam);
        HICON icon = LoadIconW(GetModuleHandleW(NULL), MAKEINTRESOURCEW(IDI_UPDATER));
        SendMessageW(window, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(icon));
        SendMessageW(window, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(icon));
        if (MessageFont != NULL)
            EnumChildWindows(window, StyleMessageControl, 0);
    }
    return CallNextHookEx(NULL, code, wParam, lParam);
}

class MessageBoxStyle
{
    HHOOK Hook = NULL;
    HFONT PreviousFont = MessageFont;
    HFONT Font = NULL;

public:
    MessageBoxStyle()
    {
        HDC screen = GetDC(NULL);
        const unsigned dpi = screen != NULL ? GetDeviceCaps(screen, LOGPIXELSY) : 96;
        if (screen != NULL)
            ReleaseDC(NULL, screen);
        LOGFONTW font = DialogFontForDpi(dpi);
        Font = CreateFontIndirectW(&font);
        MessageFont = Font;
        Hook = SetWindowsHookExW(WH_CBT, StyleMessageBox, NULL, GetCurrentThreadId());
    }
    ~MessageBoxStyle()
    {
        if (Hook != NULL)
            UnhookWindowsHookEx(Hook);
        MessageFont = PreviousFont;
        if (Font != NULL)
            DeleteObject(Font);
    }
};
} // namespace

void Tell(const std::wstring& text, UINT icon, HWND owner)
{
    MessageBoxStyle style;
    MessageBoxW(owner, text.c_str(), Text(IDS_UPDATER_TITLE).c_str(), MB_OK | icon | MB_SETFOREGROUND | MB_TOPMOST);
}

bool Ask(const std::wstring& text, HWND owner)
{
    MessageBoxStyle style;
    return MessageBoxW(owner, text.c_str(), Text(IDS_UPDATER_TITLE).c_str(),
                       MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2 | MB_SETFOREGROUND | MB_TOPMOST) == IDYES;
}

std::wstring FileNameOf(const std::wstring& path)
{
    const size_t slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? path : path.substr(slash + 1);
}

std::wstring SystemErrorText(DWORD error)
{
    wchar_t* allocated = NULL;
    const DWORD count =
        FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                       NULL, error, Language, reinterpret_cast<wchar_t*>(&allocated), 0, NULL);
    std::wstring text = count > 0 && allocated != NULL ? std::wstring(allocated, count) : std::wstring();
    if (allocated != NULL)
        LocalFree(allocated);
    while (!text.empty() && (text.back() == L'\n' || text.back() == L'\r' || text.back() == L' '))
        text.pop_back();
    return text.empty() ? L"error " + std::to_wstring(error) : text;
}

// "1.0.36.188" -> "1.0.36", as Sally's About box and the release notes write it.
std::wstring ReleaseVersion(const std::wstring& fileVersion)
{
    size_t dots = 0;
    for (size_t i = 0; i < fileVersion.size(); ++i)
    {
        if (fileVersion[i] == L'.' && ++dots == 3)
            return fileVersion.substr(0, i);
    }
    return fileVersion;
}

} // namespace salupdate

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
    using namespace salupdate;
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    std::vector<std::wstring> args;
    for (int i = 1; argv != NULL && i < argc; ++i)
        args.push_back(argv[i]);
    if (argv != NULL)
        LocalFree(argv);

    ChooseLanguage();
    UpdaterCommand command;
    std::wstring error;
    if (!ParseUpdaterCommand(args, command, error))
    {
        Tell(Text(IDS_UPDATER_USAGE) + L"\n\n" + error, MB_ICONWARNING);
        return 2;
    }
    switch (command.Mode)
    {
    case UpdaterMode::Manual:
        return Manual(command);
    case UpdaterMode::Check:
        return CheckForUpdates(command);
    case UpdaterMode::Report:
        return ReportUpdate(command);
    default:
        LogPath = ParentFolder(command.ResultFile) + L"\\salupdate.log";
        return command.Mode == UpdaterMode::Prepare ? Prepare(command) : Apply(command);
    }
}
