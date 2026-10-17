// SPDX-FileCopyrightText: 2025-2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "DisplayContent.h"
#include <windows.h>
#include <atomic>
#include <memory>
#include <mutex>

namespace Sally::Update
{
    bool ReadBundledDisplayContent(const std::wstring& install, const std::string& version, DisplayContent& result);
    std::wstring DisplayWindowIdentity(const std::wstring& install, const std::wstring& profile, bool offline);
    struct DisplayContentSession : std::enable_shared_from_this<DisplayContentSession>
    {
        std::atomic<bool> Cancel{false};
        std::mutex Mutex;
        DisplayContent Release, Live;
        bool HasRelease = false, HasLive = false;
        unsigned Revision = 0;
        void Start(HWND window, UINT message, const std::string& version, const std::wstring& metadataUrl,
                   const std::wstring& signatureUrl, const std::wstring& cacheRoot);
    };
} // namespace Sally::Update
