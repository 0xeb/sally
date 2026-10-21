// SPDX-FileCopyrightText: 2025-2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "split_plan.h"
#include <windows.h>
#include <cstddef>

namespace sally::split
{
    enum class PartDisposition
    {
        Open,
        Skipped,
        Cancelled
    };
    class IPartIo
    {
    public:
        virtual ~IPartIo() = default;
        virtual bool Progress(uint64_t partDone, uint64_t totalDone) = 0;
        virtual bool Prepare(uint64_t remaining, uint64_t wanted, bool automatic, uint64_t& actual) = 0;
        virtual void SetPartTotal(uint64_t bytes) = 0;
        virtual PartDisposition OpenPart(unsigned ordinal, uint64_t bytes) = 0;
        virtual bool Read(BYTE* buffer, size_t requested, size_t& actual) = 0;
        virtual bool Write(const BYTE* buffer, size_t requested, size_t& actual) = 0;
        virtual bool Skip(uint64_t bytes) = 0;
        virtual void Checksum(const BYTE* buffer, size_t size) = 0;
        virtual void ClosePart(bool keep) = 0;
        virtual bool AfterPart(uint64_t remaining, uint64_t nextWanted, bool automatic) = 0;
        virtual void Error(DWORD error) = 0;
    };
    struct PartResult
    {
        bool Success = false;
        unsigned Parts = 0;
        uint64_t Bytes = 0;
    };
    PartResult RunParts(const Plan& plan, size_t bufferBytes, bool checksum, IPartIo& io);
} // namespace sally::split
