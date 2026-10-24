// SPDX-FileCopyrightText: 2025-2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <string>
#include <windows.h>

// Thin native adapter; storage and selection decisions live in common/ConfigProfileStore.
LONG InitializeConfigurationProfiles(const wchar_t* usualRoot);
const std::wstring& ActiveConfigurationProfile();
const std::wstring& UsualConfigurationRoot();
void ManageConfigurationProfiles(HWND parent, const std::wstring& draftStartup);
LONG ReadConfigurationProfileChoice(std::wstring& name);
void ApplyConfigurationProfileChoice(const std::wstring& name);
LONG SaveConfigurationProfileChoice();
bool PublishConfigurationProfile(HWND mainWindow);
void FinishConfigurationProfileStartup();
bool MatchesConfigurationProfile(HWND mainWindow, DWORD processId);
void ReleaseConfigurationProfiles();
