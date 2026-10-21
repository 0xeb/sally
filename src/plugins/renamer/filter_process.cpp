// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later
#include "filter_process.h"
#include <algorithm>
#include <memory>
#include <new>
#include <limits>
#include <stdexcept>
#include <utility>
#include <vector>

namespace sally::renamer
{
    namespace
    {
        struct Handle
        {
            HANDLE Value = nullptr;
            explicit Handle(HANDLE value = nullptr) : Value(value) {}
            ~Handle()
            {
                if (Value && Value != INVALID_HANDLE_VALUE)
                    CloseHandle(Value);
            }
            Handle(const Handle&) = delete;
            Handle& operator=(const Handle&) = delete;
            Handle(Handle&& other) noexcept : Value(std::exchange(other.Value, nullptr)) {}
        };

        bool CaptureJobProcesses(HANDLE job, std::vector<Handle>& processes, DWORD& error)
        {
            size_t slots = 1;
            while (true)
            {
                const size_t headerWords = offsetof(JOBOBJECT_BASIC_PROCESS_ID_LIST, ProcessIdList) / sizeof(ULONG_PTR);
                if (slots > (std::numeric_limits<DWORD>::max)() / sizeof(ULONG_PTR) - headerWords)
                {
                    error = ERROR_ARITHMETIC_OVERFLOW;
                    return false;
                }
                std::vector<ULONG_PTR> storage(headerWords + slots);
                auto* list = reinterpret_cast<JOBOBJECT_BASIC_PROCESS_ID_LIST*>(storage.data());
                const DWORD bytes = static_cast<DWORD>(storage.size() * sizeof(ULONG_PTR));
                if (!QueryInformationJobObject(job, JobObjectBasicProcessIdList, list, bytes, nullptr))
                {
                    error = GetLastError();
                    if (error != ERROR_MORE_DATA)
                        return false;
                    slots = (std::max)(slots * 2, static_cast<size_t>(list->NumberOfAssignedProcesses));
                    continue;
                }
                if (list->NumberOfProcessIdsInList > slots)
                {
                    error = ERROR_INVALID_DATA;
                    return false;
                }
                if (list->NumberOfProcessIdsInList < list->NumberOfAssignedProcesses)
                {
                    slots = (std::max)(slots * 2, static_cast<size_t>(list->NumberOfAssignedProcesses));
                    continue;
                }
                for (DWORD i = 0; i < list->NumberOfProcessIdsInList; ++i)
                {
                    Handle process(OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE,
                                               static_cast<DWORD>(list->ProcessIdList[i])));
                    if (!process.Value)
                    {
                        error = GetLastError();
                        if (error == ERROR_INVALID_PARAMETER)
                            continue; // it exited and its process object is already gone
                        return false;
                    }
                    BOOL belongs = FALSE;
                    if (!IsProcessInJob(process.Value, job, &belongs))
                    {
                        error = GetLastError();
                        return false;
                    }
                    if (belongs)
                        processes.push_back(std::move(process));
                }
                return true;
            }
        }
        struct Attributes
        {
            std::unique_ptr<BYTE[]> Storage;
            LPPROC_THREAD_ATTRIBUTE_LIST List = nullptr;
            ~Attributes()
            {
                if (List)
                    DeleteProcThreadAttributeList(List);
            }
            bool Initialize(HANDLE* handles, size_t bytes)
            {
                SIZE_T needed = 0;
                InitializeProcThreadAttributeList(nullptr, 1, 0, &needed);
                if (!needed)
                    return false;
                Storage = std::make_unique<BYTE[]>(needed);
                auto list = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(Storage.get());
                if (!InitializeProcThreadAttributeList(list, 1, 0, &needed))
                    return false;
                List = list;
                return UpdateProcThreadAttribute(List, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
                                                 handles, bytes, nullptr, nullptr) != FALSE;
            }
        };
    } // namespace

    FilterResult RunFilterProcess(const std::wstring& commandLine, const wchar_t* directory,
                                  HANDLE input, HANDLE output, HANDLE error, IFilterProgress& progress)
    {
        if (commandLine.empty() || commandLine.size() >= WindowsCommandLineLimit)
            return {FilterState::Failed, ERROR_INVALID_PARAMETER};
        if (!progress.KeepRunning())
            return {FilterState::Cancelled};
        try
        {
            Attributes attributes;
            HANDLE inherited[] = {input, output, error};
            if (!attributes.Initialize(inherited, sizeof(inherited)))
                return {FilterState::Failed, GetLastError()};
            Handle job(CreateJobObjectW(nullptr, nullptr));
            if (!job.Value)
                return {FilterState::Failed, GetLastError()};
            JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
            limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
            if (!SetInformationJobObject(job.Value, JobObjectExtendedLimitInformation, &limits, sizeof(limits)))
                return {FilterState::Failed, GetLastError()};
            std::vector<wchar_t> mutableCommand(commandLine.begin(), commandLine.end());
            mutableCommand.push_back(L'\0');
            STARTUPINFOEXW startup{};
            startup.StartupInfo.cb = sizeof(startup);
            startup.StartupInfo.dwFlags = STARTF_USESHOWWINDOW | STARTF_USESTDHANDLES;
            startup.StartupInfo.wShowWindow = SW_HIDE;
            startup.StartupInfo.hStdInput = input;
            startup.StartupInfo.hStdOutput = output;
            startup.StartupInfo.hStdError = error;
            startup.lpAttributeList = attributes.List;
            PROCESS_INFORMATION created{};
            if (!CreateProcessW(nullptr, mutableCommand.data(), nullptr, nullptr, TRUE,
                                CREATE_SUSPENDED | CREATE_NO_WINDOW | CREATE_DEFAULT_ERROR_MODE | EXTENDED_STARTUPINFO_PRESENT,
                                nullptr, directory, &startup.StartupInfo, &created))
                return {FilterState::Failed, GetLastError()};
            Handle process(created.hProcess), thread(created.hThread);
            if (!AssignProcessToJobObject(job.Value, process.Value))
            {
                const DWORD failure = GetLastError();
                // This suspended process has executed no user instruction and
                // belongs to no private job yet; terminate only this owned handle.
                TerminateProcess(process.Value, ERROR_CANCELLED);
                return {FilterState::Failed, failure};
            }
            if (ResumeThread(thread.Value) == static_cast<DWORD>(-1))
                return {FilterState::Failed, GetLastError()};
            bool cancelled = false;
            std::vector<Handle> finishing;
            DWORD captureError = ERROR_SUCCESS;
            while (true)
            {
                const DWORD waited = WaitForSingleObject(process.Value, 25);
                if (waited == WAIT_FAILED)
                    return {FilterState::Failed, GetLastError()};
                if (waited == WAIT_OBJECT_0)
                    break;
                const bool keepRunning = progress.KeepRunning();
                if (!cancelled && !keepRunning)
                {
                    if (!CaptureJobProcesses(job.Value, finishing, captureError))
                        return {FilterState::Failed, captureError};
                    if (!TerminateJobObject(job.Value, ERROR_CANCELLED))
                        return {FilterState::Failed, GetLastError()};
                    cancelled = true;
                }
            }
            DWORD exitCode = 0;
            if (!GetExitCodeProcess(process.Value, &exitCode))
                return {FilterState::Failed, GetLastError()};
            // Filter output must not remain open in detached descendants while
            // the caller imports it or removes the private temporary directory.
            if (!cancelled)
            {
                if (!CaptureJobProcesses(job.Value, finishing, captureError))
                    return {FilterState::Failed, captureError};
                if (!TerminateJobObject(job.Value, ERROR_CANCELLED))
                    return {FilterState::Failed, GetLastError()};
            }
            while (true)
            {
                JOBOBJECT_BASIC_ACCOUNTING_INFORMATION active{};
                if (!QueryInformationJobObject(job.Value, JobObjectBasicAccountingInformation, &active, sizeof(active), nullptr))
                    return {FilterState::Failed, GetLastError()};
                if (active.ActiveProcesses == 0)
                    break;
                if (!progress.KeepRunning())
                    cancelled = true;
                Sleep(10);
            }
            // Job accounting can reach zero before a terminating member's
            // process object is signaled. Retain and wait those exact handles;
            // never substitute a sleep or use a PID to terminate a process.
            for (const auto& member : finishing)
                while (true)
                {
                    const DWORD waited = WaitForSingleObject(member.Value, 25);
                    if (waited == WAIT_OBJECT_0)
                        break;
                    if (waited == WAIT_FAILED)
                        return {FilterState::Failed, GetLastError()};
                    if (!progress.KeepRunning())
                        cancelled = true;
                }
            return {cancelled ? FilterState::Cancelled : FilterState::Completed, ERROR_SUCCESS, exitCode};
        }
        catch (const std::bad_alloc&)
        {
            return {FilterState::Failed, ERROR_NOT_ENOUGH_MEMORY};
        }
        catch (const std::length_error&)
        {
            return {FilterState::Failed, ERROR_NOT_ENOUGH_MEMORY};
        }
    }
} // namespace sally::renamer
