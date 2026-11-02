// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later
#include "Win32LongPathSetting.h"
#include <shellapi.h>
#include <string>
#include <vector>

namespace sally::configuration
{
namespace
{
bool Supported()
{
    using VersionFunction = LONG(WINAPI*)(OSVERSIONINFOW*);
    const auto module = GetModuleHandleW(L"ntdll.dll");
    const auto version = module ? reinterpret_cast<VersionFunction>(GetProcAddress(module, "RtlGetVersion")) : nullptr;
    OSVERSIONINFOW info = {};
    info.dwOSVersionInfoSize = sizeof(info);
    return version && version(&info) == 0 &&
           (info.dwMajorVersion > 10 || (info.dwMajorVersion == 10 && info.dwBuildNumber >= 14393));
}
}

Win32LongPathSetting::~Win32LongPathSetting()
{
    if (Process) CloseHandle(Process);
}

LongPathState Win32LongPathSetting::Read() const
{
    if (!Supported()) return LongPathState::Unsupported;
    HKEY key = nullptr;
    const LONG opened = RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Control\\FileSystem",
                                    0, KEY_QUERY_VALUE | KEY_WOW64_64KEY, &key);
    if (opened == ERROR_FILE_NOT_FOUND) return LongPathState::Disabled;
    if (opened != ERROR_SUCCESS) return LongPathState::Unknown;
    DWORD value = 0, bytes = sizeof(value), type = 0;
    const LONG error = RegQueryValueExW(key, L"LongPathsEnabled", nullptr, &type,
                                     reinterpret_cast<BYTE*>(&value), &bytes);
    RegCloseKey(key);
    const auto kind = error == ERROR_FILE_NOT_FOUND ? LongPathValue::Missing :
                      error != ERROR_SUCCESS ? LongPathValue::Unreadable :
                      type == REG_DWORD && bytes == sizeof(value) ? LongPathValue::Dword : LongPathValue::Invalid;
    return ClassifyLongPathSetting(true, kind, value);
}

DWORD Win32LongPathSetting::Begin(HWND owner, bool enabled)
{
    const auto state = Read();
    if (Process || Launching || (state != LongPathState::Disabled && state != LongPathState::Enabled) ||
        (state == LongPathState::Enabled) == enabled) return ERROR_INVALID_STATE;
    RequestedEnabled = enabled;
    // Query/retry the Windows directory; never resolve an elevated executable via PATH.
    std::vector<wchar_t> directory(1);
    for (;;)
    {
        const UINT length = GetWindowsDirectoryW(directory.data(), static_cast<UINT>(directory.size()));
        if (!length) return GetLastError();
        if (length < directory.size()) break;
        directory.resize(static_cast<size_t>(length) + 1);
    }
    BOOL wow64 = FALSE;
    if (!IsWow64Process(GetCurrentProcess(), &wow64)) return GetLastError();
    std::wstring executable = directory.data();
    executable += wow64 ? L"\\Sysnative\\reg.exe" : L"\\System32\\reg.exe";
    SHELLEXECUTEINFOW request = {};
    request.cbSize = sizeof(request);
    request.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC | SEE_MASK_FLAG_NO_UI;
    request.hwnd = owner;
    request.lpVerb = L"runas";
    request.lpFile = executable.c_str();
    request.lpParameters = enabled ? L"add HKLM\\SYSTEM\\CurrentControlSet\\Control\\FileSystem /v LongPathsEnabled /t REG_DWORD /d 1 /f" :
        L"add HKLM\\SYSTEM\\CurrentControlSet\\Control\\FileSystem /v LongPathsEnabled /t REG_DWORD /d 0 /f";
    request.lpDirectory = directory.data();
    request.nShow = SW_HIDE;
    // ShellExecuteEx may pump this STA's messages while UAC is active. A timer
    // must not mistake the not-yet-published process handle for launch failure.
    Launching = true;
    const BOOL launched = ShellExecuteExW(&request);
    const DWORD error = launched ? ERROR_SUCCESS : GetLastError();
    Process = request.hProcess;
    Launching = false;
    if (!launched) return error;
    return Process ? ERROR_SUCCESS : ERROR_INVALID_HANDLE;
}

LongPathChangeResult Win32LongPathSetting::Poll()
{
    if (Launching) return LongPathChangeResult::Pending;
    if (!Process) return LongPathChangeResult::Failed;
    DWORD code = 0;
    const BOOL queried = GetExitCodeProcess(Process, &code);
    if (queried && code == STILL_ACTIVE) return LongPathChangeResult::Pending;
    CloseHandle(Process);
    Process = nullptr;
    return queried && code == 0 && Read() == (RequestedEnabled ? LongPathState::Enabled : LongPathState::Disabled) ?
               LongPathChangeResult::Verified : LongPathChangeResult::Failed;
}
}
