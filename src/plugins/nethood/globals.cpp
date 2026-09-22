// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"
#include "nethood.h"
#include "nethoodfs.h"
#include "cache.h"
#include "icons.h"
#include "nethoodmenu.h"
#include "globals.h"

CSalamanderGeneralAbstract* SalamanderGeneral;
CSalamanderDebugAbstract* SalamanderDebug;
int SalamanderVersion;
CSalamanderGUIAbstract* SalamanderGUI;
CNethoodPluginInterface g_oNethoodPlugin;
CNethoodPluginInterfaceForFS g_oNethoodFS;
CNethoodCache g_oNethoodCache;
std::wstring g_assignedFSName;
size_t g_cchAssignedFSName;
HINSTANCE g_hInstance;
HINSTANCE g_hLangInstance;
const CFileData** g_transferFileData;
int* g_transferIsDir;
wchar_t* g_transferBuffer;
int* g_transferLen;
DWORD* g_transferRowData;
CPluginDataInterfaceAbstract** g_transferPluginDataIface;
DWORD* g_transferActCustomData;
CNethoodIcons g_oIcons;
CNethoodPluginInterfaceForMenuExt g_oMenuExt;

std::wstring g_redirectPaths[2];

DWORD_PTR g_adwPostedThrobberQueue[POSTED_THROBBER_QUEUE_LEN];
int g_iPostedThrobberQueue;

std::wstring g_focusShareNames[2];
int g_iFocusSharePanel;
