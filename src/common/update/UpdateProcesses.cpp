// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#include "update/UpdateProcesses.h"

#include <tlhelp32.h>

namespace Sally::Update
{

namespace
{

// Calls fill(buffer, capacity) with a growing buffer until the text fits. fill returns the
// length written, or 0 on failure; a result that fills the whole buffer may be truncated.
template <typename Fill> bool QueryGrowing(std::wstring& text, Fill fill)
{
    DWORD capacity = 256;
    for (int attempt = 0; attempt < 12; ++attempt)
    {
        std::wstring buffer(capacity, L'\0');
        SetLastError(ERROR_SUCCESS);
        const DWORD length = fill(&buffer[0], capacity);
        if (length == 0 && GetLastError() != ERROR_INSUFFICIENT_BUFFER)
            return false;
        if (length != 0 && length < capacity - 1)
        {
            buffer.resize(length);
            text.swap(buffer);
            return true;
        }
        capacity *= 2;
    }
    return false;
}

} // namespace


bool IsPathInsideFolder(const std::wstring& path, const std::wstring& folder)
{
    std::wstring base = folder;
    while (!base.empty() && (base.back() == L'\\' || base.back() == L'/'))
        base.pop_back();
    if (base.empty() || path.size() < base.size())
        return false;
    if (CompareStringOrdinal(path.data(), static_cast<int>(base.size()), base.data(),
                             static_cast<int>(base.size()), TRUE) != CSTR_EQUAL)
        return false;
    return path.size() == base.size() || path[base.size()] == L'\\' || path[base.size()] == L'/';
}

std::vector<RunningProcess> FindProcessesInFolder(const std::wstring& folder, DWORD excludePid)
{
    std::vector<RunningProcess> found;
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE)
        return found;
    PROCESSENTRY32W entry;
    entry.dwSize = sizeof(entry);
    if (Process32FirstW(snapshot, &entry))
    {
        do
        {
            if (entry.th32ProcessID == 0 || entry.th32ProcessID == excludePid)
                continue;
            HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, entry.th32ProcessID);
            if (process == NULL)
                continue;
            std::wstring image;
            const bool named = QueryGrowing(image, [process](wchar_t* buffer, DWORD capacity) -> DWORD {
                DWORD size = capacity;
                return QueryFullProcessImageNameW(process, 0, buffer, &size) ? size : 0;
            });
            if (named && IsPathInsideFolder(image, folder))
                found.push_back({entry.th32ProcessID, image});
            CloseHandle(process);
        } while (Process32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);
    return found;
}

std::wstring ModuleFilePath(HMODULE module)
{
    std::wstring path;
    QueryGrowing(path, [module](wchar_t* buffer, DWORD capacity) { return GetModuleFileNameW(module, buffer, capacity); });
    return path;
}

bool WaitForProcessesToExit(const std::vector<DWORD>& pids, DWORD timeoutMs)
{
    const ULONGLONG deadline = GetTickCount64() + timeoutMs;
    for (DWORD pid : pids)
    {
        HANDLE process = OpenProcess(SYNCHRONIZE, FALSE, pid);
        if (process == NULL)
            continue; // already gone
        const ULONGLONG now = GetTickCount64();
        const DWORD remaining = now >= deadline ? 0 : static_cast<DWORD>(deadline - now);
        const DWORD wait = WaitForSingleObject(process, remaining);
        CloseHandle(process);
        if (wait != WAIT_OBJECT_0)
            return false;
    }
    return true;
}

} // namespace Sally::Update
