// SPDX-FileCopyrightText: 2025-2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "common/IRegistry.h"

namespace sally::configuration
{
    // Observe a configuration-save operation without replacing the shared registry
    // implementation. Worker results are recorded again on the requesting thread,
    // preserving their exact error rather than relying on thread-local GetLastError.
    class RegistryWriteMonitor
    {
    public:
        explicit RegistryWriteMonitor(bool suppressWriteErrors = false)
            : Previous(Current), SuppressWriteErrors(suppressWriteErrors) { Current = this; }
        ~RegistryWriteMonitor() { Current = Previous; }
        RegistryWriteMonitor(const RegistryWriteMonitor&) = delete;
        RegistryWriteMonitor& operator=(const RegistryWriteMonitor&) = delete;

        bool Succeeded() const { return Error == ERROR_SUCCESS; }
        LONG ErrorCode() const { return Error; }
        static bool SuppressesWriteErrors()
        {
            for (auto* monitor = Current; monitor != nullptr; monitor = monitor->Previous)
                if (monitor->SuppressWriteErrors)
                    return true;
            return false;
        }
        static bool Require(bool success, LONG error = ERROR_WRITE_FAULT)
        {
            return Record(success ? RegistryResult::Ok() : RegistryResult::Error(error));
        }

        static bool Record(RegistryResult result, bool allowAbsent = false)
        {
            if (!result.success && !(allowAbsent &&
                                     (result.errorCode == ERROR_FILE_NOT_FOUND || result.errorCode == ERROR_PATH_NOT_FOUND)))
            {
                for (auto* monitor = Current; monitor != nullptr; monitor = monitor->Previous)
                    if (monitor->Error == ERROR_SUCCESS)
                        monitor->Error = result.errorCode != ERROR_SUCCESS ? result.errorCode : ERROR_WRITE_FAULT;
            }
            // Keep the existing registry facade's return contract, including FALSE
            // for an absent value whose deletion is harmless to this save.
            return result.success;
        }

    private:
        inline static thread_local RegistryWriteMonitor* Current = nullptr;
        RegistryWriteMonitor* Previous;
        bool SuppressWriteErrors;
        LONG Error = ERROR_SUCCESS;
    };
} // namespace sally::configuration
