// SPDX-FileCopyrightText: 2025-2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "IRegistry.h"
#include <vector>

namespace Sally::Appearance
{
inline constexpr int IconCount = 6;
inline constexpr int IconResources[IconCount] = {960, 961, 962, 963, 984, 985};
inline constexpr ULONG_PTR CopyDataId = 0x41505031;
struct Snapshot
{
    int Icon = 0;
    unsigned SourceDpi = 96;
    bool CustomFont = false;
    LOGFONTW Font = {};
    std::wstring Profile;
    std::wstring Install;
};
int ValidIcon(int index);
unsigned SystemDpi();
std::wstring InstallIdentity(bool updater);
Snapshot Read(const std::wstring& profile, const std::wstring& install, IRegistry& registry);
LOGFONTW FontForDpi(const Snapshot& snapshot, unsigned dpi);
std::vector<BYTE> Encode(const Snapshot& snapshot);
bool Decode(const void* bytes, size_t length, Snapshot& snapshot);
bool Matches(const Snapshot& snapshot, const std::wstring& profile, const std::wstring& install);
void SetWindowIcon(HWND window, HINSTANCE module, int index);
UINT RequestMessage();
void Send(HWND sender, HWND receiver, const Snapshot& snapshot);
void Broadcast(HWND sender, const Snapshot& snapshot);
void Request(HWND receiver, HWND owner = nullptr);
} // namespace Sally::Appearance
