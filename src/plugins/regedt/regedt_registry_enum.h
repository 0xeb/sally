// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <string>
#include <vector>

#include <windows.h>

LONG RegedtEnumerateSubKeyOwned(HKEY key, DWORD index, DWORD initialCapacity,
                                std::wstring& name, FILETIME& time);

LONG RegedtEnumerateValueOwned(HKEY key, DWORD index, DWORD initialNameCapacity,
                               DWORD initialDataCapacity, std::wstring& name,
                               DWORD& type, std::vector<BYTE>& data, DWORD& dataSize,
                               bool readData = true);

LONG RegedtQueryKeyInfoOwned(HKEY key, std::wstring* className,
                             DWORD* maximumValueData, FILETIME* lastWriteTime) noexcept;

LONG RegedtQueryValueOwned(HKEY key, const wchar_t* valueName, DWORD initialCapacity,
                           DWORD& type, std::vector<BYTE>& data) noexcept;

bool RegedtPrepareViewerBytes(DWORD type, const std::vector<BYTE>& registryData,
                              bool preferAnsi, UINT codePage,
                              std::vector<BYTE>& viewerData) noexcept;
