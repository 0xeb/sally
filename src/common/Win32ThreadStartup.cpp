// SPDX-FileCopyrightText: 2025-2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#include "IThreadStartup.h"
#include <cstdlib>
#include <memory>
#include <ostream>
#include "trace.h"

namespace
{
    class Win32ThreadStartup : public IThreadStartup
    {
    public:
        StartupHandleResult CreateSignalEvent(bool manualReset, bool initialState) override
        {
            HANDLE handle = ::CreateEventW(nullptr, manualReset, initialState, nullptr);
            return {handle, handle ? ERROR_SUCCESS : GetLastError()};
        }
        StartupHandleResult StartThread(LPTHREAD_START_ROUTINE routine, void* parameter, DWORD* threadId) override
        {
            HANDLE handle = ::CreateThread(nullptr, 0, routine, parameter, 0, threadId);
            return {handle, handle ? ERROR_SUCCESS : GetLastError()};
        }
        void* AllocateBuffer(size_t bytes) override { return std::malloc(bytes); }
    };
    Win32ThreadStartup DefaultThreadStartup;
} // namespace

IThreadStartup* gThreadStartup = &DefaultThreadStartup;
IThreadStartup* GetWin32ThreadStartup() { return &DefaultThreadStartup; }

std::wstring FormatThreadStartupErrorW(DWORD error)
{
    if (error == ERROR_SUCCESS)
        error = ERROR_GEN_FAILURE;
    wchar_t* text = nullptr;
    const DWORD size = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                                      nullptr, error, 0, reinterpret_cast<wchar_t*>(&text), 0, nullptr);
    const std::unique_ptr<wchar_t, decltype(&::LocalFree)> owner(text, ::LocalFree);
    std::wstring result = L"(" + std::to_wstring(error) + L") ";
    if (size && text)
        result.append(text, size);
    return result;
}
