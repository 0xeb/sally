// SPDX-FileCopyrightText: 2025-2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <string>
#include <vector>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

class CSalamanderRegistryAbstract;

namespace pictview
{

// The configuration version found by the last load (0 for a new installation); the plugin's
// Connect uses it to adjust registered extensions on upgrades.
DWORD LoadedConfigurationVersion();

// The codec-added extensions PictView has already put on Sally's viewer list. Each is added
// once, so an extension the user takes off the list stays off.
std::vector<std::wstring> GetOfferedCodecExtensions();
void SetOfferedCodecExtensions(const std::vector<std::wstring>& extensions);

void LoadViewerPreferencesConfiguration(HKEY regKey, CSalamanderRegistryAbstract* registry);
void SaveViewerPreferencesConfiguration(HKEY regKey, CSalamanderRegistryAbstract* registry);
void LoadCopyToConfiguration(HKEY regKey, CSalamanderRegistryAbstract* registry);
void SaveCopyToConfiguration(HKEY regKey, CSalamanderRegistryAbstract* registry);
void LoadRecentHistoryConfiguration(HKEY regKey, CSalamanderRegistryAbstract* registry);
void SaveRecentHistoryConfiguration(HKEY regKey, CSalamanderRegistryAbstract* registry, bool saveEntries = true);
void LoadHistogramConfiguration(HKEY regKey, CSalamanderRegistryAbstract* registry);
void SaveHistogramConfiguration(HKEY regKey, CSalamanderRegistryAbstract* registry);
void LoadMetadataDetailsConfiguration(HKEY regKey, CSalamanderRegistryAbstract* registry);
void SaveMetadataDetailsConfiguration(HKEY regKey, CSalamanderRegistryAbstract* registry);

} // namespace pictview
