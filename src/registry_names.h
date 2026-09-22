// SPDX-FileCopyrightText: 2025-2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

// Shared registry paths, subkeys, and value names used across Sally.
//
// Every _A literal has a _W twin so call sites migrate to the wide
// registry stack without an AnsiToWideReg conversion at the boundary. The _A set
// stays until the ANSI facades are deleted.
// Keep registry-related string literals here instead of repeating them in call sites.

#define SAL_REG_ROOT_SALLY_1_0_A "Software\\Sally\\1.0"
#define SAL_REG_ROOT_SALLY_1_0_W L"Software\\Sally\\1.0"
#define SAL_REG_ROOT_SALLY_1_0_DEBUG_A "Software\\Sally\\1.0 Debug"
#define SAL_REG_ROOT_SALLY_1_0_DEBUG_W L"Software\\Sally\\1.0 Debug"
#ifdef SALLY_PREVIEW_REGISTRY_VERSION
// Wide twin of the build-provided preview version literal. Two levels: the
// inner paste must see the EXPANDED macro, not its name.
#define SAL_REG_WIDEN_PASTE(x) L##x
#define SAL_REG_WIDEN(x) SAL_REG_WIDEN_PASTE(x)
#define SALLY_PREVIEW_REGISTRY_VERSION_W SAL_REG_WIDEN(SALLY_PREVIEW_REGISTRY_VERSION)
#define SAL_REG_ROOT_SALLY_PREVIEW_A "Software\\Sally\\" SALLY_PREVIEW_REGISTRY_VERSION " Preview"
#define SAL_REG_ROOT_SALLY_PREVIEW_W L"Software\\Sally\\" SALLY_PREVIEW_REGISTRY_VERSION_W L" Preview"
#define SAL_REG_ROOT_SALLY_PREVIEW_DEBUG_A "Software\\Sally\\" SALLY_PREVIEW_REGISTRY_VERSION " Preview Debug"
#define SAL_REG_ROOT_SALLY_PREVIEW_DEBUG_W L"Software\\Sally\\" SALLY_PREVIEW_REGISTRY_VERSION_W L" Preview Debug"
#endif
#define SAL_REG_ROOT_OPENSAL_5_0_A "Software\\Open Salamander\\5.0"
#define SAL_REG_ROOT_OPENSAL_5_0_W L"Software\\Open Salamander\\5.0"
#define SAL_REG_VERSION_SALLY_1_0_A "1.0"
#define SAL_REG_VERSION_SALLY_1_0_W L"1.0"
#define SAL_REG_VERSION_SALLY_1_0_DEBUG_A "1.0 Debug"
#define SAL_REG_VERSION_SALLY_1_0_DEBUG_W L"1.0 Debug"
#ifdef SALLY_PREVIEW_REGISTRY_VERSION
#define SAL_REG_VERSION_SALLY_PREVIEW_A SALLY_PREVIEW_REGISTRY_VERSION " Preview"
#define SAL_REG_VERSION_SALLY_PREVIEW_W SALLY_PREVIEW_REGISTRY_VERSION_W L" Preview"
#define SAL_REG_VERSION_SALLY_PREVIEW_DEBUG_A SALLY_PREVIEW_REGISTRY_VERSION " Preview Debug"
#define SAL_REG_VERSION_SALLY_PREVIEW_DEBUG_W SALLY_PREVIEW_REGISTRY_VERSION_W L" Preview Debug"
#endif

#ifdef SALLY_PREVIEW_REGISTRY_VERSION
#ifdef _DEBUG
#define SAL_REG_ROOT_SALLY_CURRENT_A SAL_REG_ROOT_SALLY_PREVIEW_DEBUG_A
#define SAL_REG_ROOT_SALLY_CURRENT_W SAL_REG_ROOT_SALLY_PREVIEW_DEBUG_W
#define SAL_REG_VERSION_SALLY_CURRENT_A SAL_REG_VERSION_SALLY_PREVIEW_DEBUG_A
#define SAL_REG_VERSION_SALLY_CURRENT_W SAL_REG_VERSION_SALLY_PREVIEW_DEBUG_W
#else
#define SAL_REG_ROOT_SALLY_CURRENT_A SAL_REG_ROOT_SALLY_PREVIEW_A
#define SAL_REG_ROOT_SALLY_CURRENT_W SAL_REG_ROOT_SALLY_PREVIEW_W
#define SAL_REG_VERSION_SALLY_CURRENT_A SAL_REG_VERSION_SALLY_PREVIEW_A
#define SAL_REG_VERSION_SALLY_CURRENT_W SAL_REG_VERSION_SALLY_PREVIEW_W
#endif
#else
#ifdef _DEBUG
#define SAL_REG_ROOT_SALLY_CURRENT_A SAL_REG_ROOT_SALLY_1_0_DEBUG_A
#define SAL_REG_ROOT_SALLY_CURRENT_W SAL_REG_ROOT_SALLY_1_0_DEBUG_W
#define SAL_REG_VERSION_SALLY_CURRENT_A SAL_REG_VERSION_SALLY_1_0_DEBUG_A
#define SAL_REG_VERSION_SALLY_CURRENT_W SAL_REG_VERSION_SALLY_1_0_DEBUG_W
#else
#define SAL_REG_ROOT_SALLY_CURRENT_A SAL_REG_ROOT_SALLY_1_0_A
#define SAL_REG_ROOT_SALLY_CURRENT_W SAL_REG_ROOT_SALLY_1_0_W
#define SAL_REG_VERSION_SALLY_CURRENT_A SAL_REG_VERSION_SALLY_1_0_A
#define SAL_REG_VERSION_SALLY_CURRENT_W SAL_REG_VERSION_SALLY_1_0_W
#endif
#endif

#define SAL_REG_CONFIGURATION_ROOTS \
    SAL_REG_ROOT_SALLY_CURRENT_W, \
    SAL_REG_ROOT_OPENSAL_5_0_W, \
    L"Software\\Altap\\Altap Salamander 4.0", \
    L"Software\\Altap\\Altap Salamander 4.0 beta 1 (DB177)", \
    L"Software\\Altap\\Altap Salamander 4.0 beta 1 (DB171)", \
    L"Software\\Altap\\Altap Salamander 3.08", \
    L"Software\\Altap\\Altap Salamander 4.0 beta 1 (DB168)", \
    L"Software\\Altap\\Altap Salamander 3.07", \
    L"Software\\Altap\\Altap Salamander 3.1 beta 1 (DB162)", \
    L"Software\\Altap\\Altap Salamander 3.1 beta 1 (DB159)", \
    L"Software\\Altap\\Altap Salamander 3.06", \
    L"Software\\Altap\\Altap Salamander 3.1 beta 1 (DB153)", \
    L"Software\\Altap\\Altap Salamander 3.05", \
    L"Software\\Altap\\Altap Salamander 3.1 beta 1 (DB147)", \
    L"Software\\Altap\\Altap Salamander 3.04", \
    L"Software\\Altap\\Altap Salamander 3.1 beta 1 (DB141)", \
    L"Software\\Altap\\Altap Salamander 3.03", \
    L"Software\\Altap\\Altap Salamander 3.1 beta 1 (DB135)", \
    L"Software\\Altap\\Altap Salamander 3.02", \
    L"Software\\Altap\\Altap Salamander 3.1 beta 1 (DB129)", \
    L"Software\\Altap\\Altap Salamander 3.01", \
    L"Software\\Altap\\Altap Salamander 3.1 beta 1 (DB123)", \
    L"Software\\Altap\\Altap Salamander 3.0", \
    L"Software\\Altap\\Altap Salamander 3.0 beta 5 (DB117)", \
    L"Software\\Altap\\Altap Salamander 3.0 beta 4", \
    L"Software\\Altap\\Altap Salamander 3.0 beta 4 (DB111)", \
    L"Software\\Altap\\Altap Salamander 3.0 beta 3", \
    L"Software\\Altap\\Altap Salamander 3.0 beta 3 (DB105)", \
    L"Software\\Altap\\Altap Salamander 3.0 beta 3 (PB103)", \
    L"Software\\Altap\\Altap Salamander 3.0 beta 3 (DB100)", \
    L"Software\\Altap\\Altap Salamander 3.0 beta 2", \
    L"Software\\Altap\\Altap Salamander 3.0 beta 2 (DB94)", \
    L"Software\\Altap\\Altap Salamander 3.0 beta 1", \
    L"Software\\Altap\\Altap Salamander 3.0 beta 1 (DB88)", \
    L"Software\\Altap\\Altap Salamander 3.0 beta 1 (PB87)", \
    L"Software\\Altap\\Altap Salamander 3.0 beta 1 (DB83)", \
    L"Software\\Altap\\Altap Salamander 3.0 beta 1 (DB80)", \
    L"Software\\Altap\\Altap Salamander 3.0 beta 1 (PB79)", \
    L"Software\\Altap\\Altap Salamander 3.0 beta 1 (DB76)", \
    L"Software\\Altap\\Altap Salamander 3.0 beta 1 (PB75)", \
    L"Software\\Altap\\Altap Salamander 2.55 beta 1 (DB 72)", \
    L"Software\\Altap\\Altap Salamander 2.54", \
    L"Software\\Altap\\Altap Salamander 2.54 beta 1 (DB 66)", \
    L"Software\\Altap\\Altap Salamander 2.53", \
    L"Software\\Altap\\Altap Salamander 2.53 (DB 60)", \
    L"Software\\Altap\\Altap Salamander 2.53 beta 2", \
    L"Software\\Altap\\Altap Salamander 2.53 beta 2 (IB 55)", \
    L"Software\\Altap\\Altap Salamander 2.53 (DB 52)", \
    L"Software\\Altap\\Altap Salamander 2.53 beta 1", \
    L"Software\\Altap\\Altap Salamander 2.53 beta 1 (DB 46)", \
    L"Software\\Altap\\Altap Salamander 2.53 beta 1 (PB 44)", \
    L"Software\\Altap\\Altap Salamander 2.53 beta 1 (DB 41)", \
    L"Software\\Altap\\Altap Salamander 2.53 beta 1 (DB 39)", \
    L"Software\\Altap\\Altap Salamander 2.53 beta 1 (PB 38)", \
    L"Software\\Altap\\Altap Salamander 2.53 beta 1 (DB 36)", \
    L"Software\\Altap\\Altap Salamander 2.53 beta 1 (DB 33)", \
    L"Software\\Altap\\Altap Salamander 2.52", \
    L"Software\\Altap\\Altap Salamander 2.52 (DB 30)", \
    L"Software\\Altap\\Altap Salamander 2.52 beta 2", \
    L"Software\\Altap\\Altap Salamander 2.52 beta 1", \
    L"Software\\Altap\\Altap Salamander 2.51", \
    L"Software\\Altap\\Altap Salamander 2.5", \
    L"Software\\Altap\\Altap Salamander 2.5 RC3", \
    L"Software\\Altap\\Servant Salamander 2.5 RC3", \
    L"Software\\Altap\\Servant Salamander 2.5 RC2", \
    L"Software\\Altap\\Servant Salamander 2.5 RC1", \
    L"Software\\Altap\\Servant Salamander 2.5 beta 12", \
    L"Software\\Altap\\Servant Salamander 2.5 beta 11", \
    L"Software\\Altap\\Servant Salamander 2.5 beta 10", \
    L"Software\\Altap\\Servant Salamander 2.5 beta 9", \
    L"Software\\Altap\\Servant Salamander 2.5 beta 8", \
    L"Software\\Altap\\Servant Salamander 2.5 beta 7", \
    L"Software\\Altap\\Servant Salamander 2.5 beta 6", \
    L"Software\\Altap\\Servant Salamander 2.5 beta 5", \
    L"Software\\Altap\\Servant Salamander 2.5 beta 4", \
    L"Software\\Altap\\Servant Salamander 2.5 beta 3", \
    L"Software\\Altap\\Servant Salamander 2.5 beta 2", \
    L"Software\\Altap\\Servant Salamander 2.5 beta 1", \
    L"Software\\Altap\\Servant Salamander 2.1 beta 1", \
    L"Software\\Altap\\Servant Salamander 2.0", \
    L"Software\\Altap\\Servant Salamander 1.6 beta 7", \
    L"Software\\Altap\\Servant Salamander 1.6 beta 6", \
    L"Software\\Altap\\Servant Salamander", \
    L"Software\\Salamander"

#define SAL_REG_CONFIGURATION_VERSIONS \
    SAL_REG_VERSION_SALLY_CURRENT_W, \
    L"5.0", \
    L"4.0", \
    L"4.0 beta 1 (DB177)", \
    L"4.0 beta 1 (DB171)", \
    L"3.08", \
    L"4.0 beta 1 (DB168)", \
    L"3.07", \
    L"3.1 beta 1 (DB162)", \
    L"3.1 beta 1 (DB159)", \
    L"3.06", \
    L"3.1 beta 1 (DB153)", \
    L"3.05", \
    L"3.1 beta 1 (DB147)", \
    L"3.04", \
    L"3.1 beta 1 (DB141)", \
    L"3.03", \
    L"3.1 beta 1 (DB135)", \
    L"3.02", \
    L"3.1 beta 1 (DB129)", \
    L"3.01", \
    L"3.1 beta 1 (DB123)", \
    L"3.0", \
    L"3.0 beta 5 (DB117)", \
    L"3.0 beta 4", \
    L"3.0 beta 4 (DB111)", \
    L"3.0 beta 3", \
    L"3.0 beta 3 (DB105)", \
    L"3.0 beta 3 (PB103)", \
    L"3.0 beta 3 (DB100)", \
    L"3.0 beta 2", \
    L"3.0 beta 2 (DB94)", \
    L"3.0 beta 1", \
    L"3.0 beta 1 (DB88)", \
    L"3.0 beta 1 (PB87)", \
    L"3.0 beta 1 (DB83)", \
    L"3.0 beta 1 (DB80)", \
    L"3.0 beta 1 (PB79)", \
    L"3.0 beta 1 (DB76)", \
    L"3.0 beta 1 (PB75)", \
    L"2.55 beta 1 (DB72)", \
    L"2.54", \
    L"2.54 beta 1 (DB66)", \
    L"2.53", \
    L"2.53 (DB60)", \
    L"2.53 beta 2", \
    L"2.53 beta 2 (IB55)", \
    L"2.53 (DB52)", \
    L"2.53 beta 1", \
    L"2.53 beta 1 (DB46)", \
    L"2.53 beta 1 (PB44)", \
    L"2.53 beta 1 (DB41)", \
    L"2.53 beta 1 (DB39)", \
    L"2.53 beta 1 (PB38)", \
    L"2.53 beta 1 (DB36)", \
    L"2.53 beta 1 (DB33)", \
    L"2.52", \
    L"2.52 (DB30)", \
    L"2.52 beta 2", \
    L"2.52 beta 1", \
    L"2.51", \
    L"2.5", \
    L"2.5 RC3", \
    L"2.5 RC3", \
    L"2.5 RC2", \
    L"2.5 RC1", \
    L"2.5 beta 12", \
    L"2.5 beta 11", \
    L"2.5 beta 10", \
    L"2.5 beta 9", \
    L"2.5 beta 8", \
    L"2.5 beta 7", \
    L"2.5 beta 6", \
    L"2.5 beta 5", \
    L"2.5 beta 4", \
    L"2.5 beta 3", \
    L"2.5 beta 2", \
    L"2.5 beta 1", \
    L"2.1 beta 1", \
    L"2.0", \
    L"1.6 beta 7", \
    L"1.6 beta 6", \
    L"1.6 beta 1-5", \
    L"1.52"

#define SAL_REG_SUBKEY_CONFIGURATION_A "Configuration"
#define SAL_REG_SUBKEY_CONFIGURATION_W L"Configuration"

// Name of the process environment variable through which the core publishes the configuration
// root it actually resolved (see PublishConfigRootToEnvironment). Plugins must prefer this over
// any hardcoded root: a Debug or Preview build does not live under SAL_REG_ROOT_SALLY_1_0_A,
// and a plugin reading the wrong key gets a different theme than the core.
//
// [merge:main->unicode] BOTH spellings are deliberate and both are load-bearing. The core writes
// the W form (SALAMANDER_ROOT_REG is const wchar_t* on this branch, so SetEnvironmentVariableA
// would not even compile), while plugins/shared/plugindarkmode.cpp still reads the A form via
// GetEnvironmentVariableA. That round-trip is exact, not lossy: environment variables are a
// single process-wide store that the OS itself keeps in both encodings, and every configuration
// root is pure ASCII ("Software\Sally\1.0 Debug"). Do not "unify" these by deleting the A form -
// the plugin side is narrow by its own ABI, not by oversight.
#define SAL_ENV_CONFIG_ROOT_A "SALLY_CONFIG_ROOT"
#define SAL_ENV_CONFIG_ROOT_W L"SALLY_CONFIG_ROOT"
#define SAL_REG_VALUE_DEFAULT_A ""
#define SAL_REG_VALUE_DEFAULT_W L""
#define SAL_REG_VALUE_VERSION_A "Version"
#define SAL_REG_VALUE_VERSION_W L"Version"
#define SAL_REG_VALUE_AUTO_IMPORT_CONFIG_A "AutoImportConfig"
#define SAL_REG_VALUE_AUTO_IMPORT_CONFIG_W L"AutoImportConfig"
#define SAL_REG_VALUE_COPY_IS_OK_W L"Copy Is OK"
#define SAL_REG_MUTEX_LOADSAVE_A "SallyLoadSaveRegistry"
#define SAL_REG_MUTEX_LOADSAVE_W L"SallyLoadSaveRegistry"

#define SAL_REG_KEY_WINDOWS_THEME_PERSONALIZE_A "Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize"
#define SAL_REG_KEY_WINDOWS_THEME_PERSONALIZE_W L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize"
#define SAL_REG_VALUE_APPS_USE_LIGHT_THEME_A "AppsUseLightTheme"
#define SAL_REG_VALUE_APPS_USE_LIGHT_THEME_W L"AppsUseLightTheme"
#define SAL_REG_VALUE_SYSTEM_USES_LIGHT_THEME_A "SystemUsesLightTheme"
#define SAL_REG_VALUE_SYSTEM_USES_LIGHT_THEME_W L"SystemUsesLightTheme"

#define SAL_REG_KEY_MICROSOFT_IE_A "Software\\Microsoft\\Internet Explorer"
#define SAL_REG_KEY_MICROSOFT_IE_W L"Software\\Microsoft\\Internet Explorer"
#define SAL_REG_VALUE_IE_IVER_A "IVer"
#define SAL_REG_VALUE_IE_IVER_W L"IVer"
#define SAL_REG_VALUE_BUILD_A "Build"
#define SAL_REG_VALUE_BUILD_W L"Build"

#define SAL_REG_KEY_WINDOWS_NT_CURRENT_VERSION_A "SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion"
#define SAL_REG_KEY_WINDOWS_NT_CURRENT_VERSION_W L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion"
#define SAL_REG_VALUE_WINDOWS_PRODUCT_NAME_A "ProductName"
#define SAL_REG_VALUE_WINDOWS_PRODUCT_NAME_W L"ProductName"
#define SAL_REG_VALUE_WINDOWS_CURRENT_VERSION_A "CurrentVersion"
#define SAL_REG_VALUE_WINDOWS_CURRENT_VERSION_W L"CurrentVersion"

#define SAL_REG_KEY_HARDWARE_CPU0_A "Hardware\\Description\\System\\CentralProcessor\\0"
#define SAL_REG_KEY_HARDWARE_CPU0_W L"Hardware\\Description\\System\\CentralProcessor\\0"
#define SAL_REG_VALUE_PROCESSOR_NAME_STRING_A "ProcessorNameString"
#define SAL_REG_VALUE_PROCESSOR_NAME_STRING_W L"ProcessorNameString"
#define SAL_REG_VALUE_IDENTIFIER_A "Identifier"
#define SAL_REG_VALUE_IDENTIFIER_W L"Identifier"
#define SAL_REG_VALUE_VENDOR_IDENTIFIER_A "VendorIdentifier"
#define SAL_REG_VALUE_VENDOR_IDENTIFIER_W L"VendorIdentifier"
#define SAL_REG_VALUE_PROCESSOR_SPEED_MHZ_A "~MHz"
#define SAL_REG_VALUE_PROCESSOR_SPEED_MHZ_W L"~MHz"

#define SAL_REG_KEY_HARDWARE_DESCRIPTION_SYSTEM_A "Hardware\\Description\\System"
#define SAL_REG_KEY_HARDWARE_DESCRIPTION_SYSTEM_W L"Hardware\\Description\\System"
#define SAL_REG_VALUE_SYSTEM_BIOS_VERSION_A "SystemBiosVersion"
#define SAL_REG_VALUE_SYSTEM_BIOS_VERSION_W L"SystemBiosVersion"
#define SAL_REG_VALUE_SYSTEM_BIOS_DATE_A "SystemBiosDate"
#define SAL_REG_VALUE_SYSTEM_BIOS_DATE_W L"SystemBiosDate"

#define SAL_REG_KEY_NETWORK_A "Network"
#define SAL_REG_KEY_NETWORK_W L"Network"
#define SAL_REG_VALUE_REMOTE_PATH_A "RemotePath"
#define SAL_REG_VALUE_REMOTE_PATH_W L"RemotePath"
#define SAL_REG_VALUE_USER_NAME_A "UserName"
#define SAL_REG_VALUE_USER_NAME_W L"UserName"
#define SAL_REG_VALUE_PROVIDER_NAME_A "ProviderName"
#define SAL_REG_VALUE_PROVIDER_NAME_W L"ProviderName"

#define SAL_REG_KEY_WIN81_ONEDRIVE_A "Software\\Microsoft\\Windows\\CurrentVersion\\OneDrive"
#define SAL_REG_KEY_WIN81_ONEDRIVE_W L"Software\\Microsoft\\Windows\\CurrentVersion\\OneDrive"
#define SAL_REG_KEY_WIN81_SKYDRIVE_A "Software\\Microsoft\\Windows\\CurrentVersion\\SkyDrive"
#define SAL_REG_KEY_WIN81_SKYDRIVE_W L"Software\\Microsoft\\Windows\\CurrentVersion\\SkyDrive"
#define SAL_REG_KEY_ONEDRIVE_A "Software\\Microsoft\\OneDrive"
#define SAL_REG_KEY_ONEDRIVE_W L"Software\\Microsoft\\OneDrive"
#define SAL_REG_KEY_SKYDRIVE_A "Software\\Microsoft\\SkyDrive"
#define SAL_REG_KEY_SKYDRIVE_W L"Software\\Microsoft\\SkyDrive"
#define SAL_REG_KEY_ONEDRIVE_ACCOUNTS_A "Software\\Microsoft\\OneDrive\\Accounts"
#define SAL_REG_KEY_ONEDRIVE_ACCOUNTS_W L"Software\\Microsoft\\OneDrive\\Accounts"
#define SAL_REG_VALUE_USER_FOLDER_A "UserFolder"
#define SAL_REG_VALUE_USER_FOLDER_W L"UserFolder"
#define SAL_REG_VALUE_DISPLAY_NAME_A "DisplayName"
#define SAL_REG_VALUE_DISPLAY_NAME_W L"DisplayName"

#define SAL_REG_KEY_EXPLORER_FILEEXTS_A "Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\FileExts"
#define SAL_REG_KEY_EXPLORER_FILEEXTS_W L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\FileExts"
#define SAL_REG_KEY_SYSTEM_FILE_ASSOCIATIONS_A "SystemFileAssociations"
#define SAL_REG_KEY_SYSTEM_FILE_ASSOCIATIONS_W L"SystemFileAssociations"
#define SAL_REG_SUBKEY_SHELL_A "\\Shell"
#define SAL_REG_SUBKEY_SHELL_W L"\\Shell"
#define SAL_REG_SUBKEY_SHELLEX_ICON_HANDLER_A "\\ShellEx\\IconHandler"
#define SAL_REG_SUBKEY_SHELLEX_ICON_HANDLER_W L"\\ShellEx\\IconHandler"
#define SAL_REG_SUBKEY_DEFAULT_ICON_A "\\DefaultIcon"
#define SAL_REG_SUBKEY_DEFAULT_ICON_W L"\\DefaultIcon"
#define SAL_REG_VALUE_PERCEIVED_TYPE_A "PerceivedType"
#define SAL_REG_VALUE_PERCEIVED_TYPE_W L"PerceivedType"
#define SAL_REG_SUBKEY_USER_CHOICE_A "UserChoice"
#define SAL_REG_SUBKEY_USER_CHOICE_W L"UserChoice"
#define SAL_REG_VALUE_PROGID_A "Progid"
#define SAL_REG_VALUE_PROGID_W L"Progid"
#define SAL_REG_SUBKEY_OPEN_WITH_PROGIDS_A "OpenWithProgids"
#define SAL_REG_SUBKEY_OPEN_WITH_PROGIDS_W L"OpenWithProgids"
#define SAL_REG_VALUE_APPLICATION_A "Application"
#define SAL_REG_VALUE_APPLICATION_W L"Application"

#define SAL_REG_KEY_CONTROL_PANEL_DESKTOP_A "Control Panel\\Desktop"
#define SAL_REG_KEY_CONTROL_PANEL_DESKTOP_W L"Control Panel\\Desktop"
#define SAL_REG_KEY_WINDOW_METRICS_A "Control Panel\\Desktop\\WindowMetrics"
#define SAL_REG_KEY_WINDOW_METRICS_W L"Control Panel\\Desktop\\WindowMetrics"
#define SAL_REG_VALUE_WAIT_TO_KILL_APP_TIMEOUT_A "WaitToKillAppTimeout"
#define SAL_REG_VALUE_WAIT_TO_KILL_APP_TIMEOUT_W L"WaitToKillAppTimeout"
#define SAL_REG_VALUE_AUTO_END_TASKS_A "AutoEndTasks"
#define SAL_REG_VALUE_AUTO_END_TASKS_W L"AutoEndTasks"
#define SAL_REG_VALUE_SHELL_ICON_SIZE_A "Shell Icon Size"
#define SAL_REG_VALUE_SHELL_ICON_SIZE_W L"Shell Icon Size"
#define SAL_REG_VALUE_SHELL_ICON_BPP_A "Shell Icon Bpp"
#define SAL_REG_VALUE_SHELL_ICON_BPP_W L"Shell Icon Bpp"

#define SAL_REG_KEY_POLICIES_EXPLORER_CURRENT_USER_A "Software\\Microsoft\\Windows\\CurrentVersion\\Policies\\Explorer"
#define SAL_REG_KEY_POLICIES_EXPLORER_CURRENT_USER_W L"Software\\Microsoft\\Windows\\CurrentVersion\\Policies\\Explorer"
#define SAL_REG_KEY_POLICIES_EXPLORER_RESTRICT_RUN_CURRENT_USER_A "Software\\Microsoft\\Windows\\CurrentVersion\\Policies\\Explorer\\RestrictRun"
#define SAL_REG_KEY_POLICIES_EXPLORER_RESTRICT_RUN_CURRENT_USER_W L"Software\\Microsoft\\Windows\\CurrentVersion\\Policies\\Explorer\\RestrictRun"
#define SAL_REG_KEY_POLICIES_EXPLORER_DISALLOW_RUN_CURRENT_USER_A "Software\\Microsoft\\Windows\\CurrentVersion\\Policies\\Explorer\\DisallowRun"
#define SAL_REG_KEY_POLICIES_EXPLORER_DISALLOW_RUN_CURRENT_USER_W L"Software\\Microsoft\\Windows\\CurrentVersion\\Policies\\Explorer\\DisallowRun"
#define SAL_REG_KEY_POLICIES_NETWORK_CURRENT_USER_A "Software\\Microsoft\\Windows\\CurrentVersion\\Policies\\Network"
#define SAL_REG_KEY_POLICIES_NETWORK_CURRENT_USER_W L"Software\\Microsoft\\Windows\\CurrentVersion\\Policies\\Network"
#define SAL_REG_KEY_POLICIES_EXPLORER_MACHINE_A "SOFTWARE\\Policies\\Microsoft\\Windows\\Explorer"
#define SAL_REG_KEY_POLICIES_EXPLORER_MACHINE_W L"SOFTWARE\\Policies\\Microsoft\\Windows\\Explorer"
#define SAL_REG_VALUE_NO_RUN_A "NoRun"
#define SAL_REG_VALUE_NO_RUN_W L"NoRun"
#define SAL_REG_VALUE_NO_DRIVES_A "NoDrives"
#define SAL_REG_VALUE_NO_DRIVES_W L"NoDrives"
#define SAL_REG_VALUE_NO_FIND_A "NoFind"
#define SAL_REG_VALUE_NO_FIND_W L"NoFind"
#define SAL_REG_VALUE_NO_SHELL_SEARCH_BUTTON_A "NoShellSearchButton"
#define SAL_REG_VALUE_NO_SHELL_SEARCH_BUTTON_W L"NoShellSearchButton"
#define SAL_REG_VALUE_NO_NET_HOOD_A "NoNetHood"
#define SAL_REG_VALUE_NO_NET_HOOD_W L"NoNetHood"
#define SAL_REG_VALUE_NO_NET_CONNECT_DISCONNECT_A "NoNetConnectDisconnect"
#define SAL_REG_VALUE_NO_NET_CONNECT_DISCONNECT_W L"NoNetConnectDisconnect"
#define SAL_REG_VALUE_RESTRICT_RUN_A "RestrictRun"
#define SAL_REG_VALUE_RESTRICT_RUN_W L"RestrictRun"
#define SAL_REG_VALUE_DISALLOW_RUN_A "DisallowRun"
#define SAL_REG_VALUE_DISALLOW_RUN_W L"DisallowRun"
#define SAL_REG_VALUE_NO_DOT_BREAK_IN_LOGICAL_COMPARE_A "NoDotBreakInLogicalCompare"
#define SAL_REG_VALUE_NO_DOT_BREAK_IN_LOGICAL_COMPARE_W L"NoDotBreakInLogicalCompare"

#define SAL_REG_KEY_CLASSES_ROOT_CLSID_A "CLSID"
#define SAL_REG_KEY_CLASSES_ROOT_CLSID_W L"CLSID"
#define SAL_REG_KEY_SHELL_ICON_OVERLAY_IDENTIFIERS_A "Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\ShellIconOverlayIdentifiers"
#define SAL_REG_KEY_SHELL_ICON_OVERLAY_IDENTIFIERS_W L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\ShellIconOverlayIdentifiers"
#define SAL_REG_KEY_EXPLORER_SHELL_ICONS_A "Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Shell Icons"
#define SAL_REG_KEY_EXPLORER_SHELL_ICONS_W L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Shell Icons"

#define SAL_REG_KEY_BUG_REPORTER_A "Software\\Sally\\Bug Reporter"
#define SAL_REG_KEY_BUG_REPORTER_W L"Software\\Sally\\Bug Reporter"
#define SAL_REG_KEY_BUG_REPORTER_DB_A "Software\\Open Salamander\\Bug Reporter"
#define SAL_REG_KEY_BUG_REPORTER_DB_W L"Software\\Open Salamander\\Bug Reporter"
#define SAL_REG_VALUE_BUG_REPORTER_UID_A "ID"
#define SAL_REG_VALUE_BUG_REPORTER_UID_W L"ID"
#define SAL_REG_MUTEX_GLOBAL_BUG_REPORTER_A "Global\\SallyBugReporterRegistryMutex"
#define SAL_REG_MUTEX_GLOBAL_BUG_REPORTER_W L"Global\\SallyBugReporterRegistryMutex"

#define SAL_REG_KEY_TRANSLATOR_A "Software\\Open Salamander\\Translator"
#define SAL_REG_KEY_TRANSLATOR_W L"Software\\Open Salamander\\Translator"
#define SAL_REG_KEY_TRACE_SERVER_A "Software\\Sally\\Trace Server"
#define SAL_REG_KEY_TRACE_SERVER_W L"Software\\Sally\\Trace Server"

#define SAL_REG_KEY_REGEDIT_APPLET_A "Software\\Microsoft\\Windows\\CurrentVersion\\Applets\\Regedit"
#define SAL_REG_KEY_REGEDIT_APPLET_W L"Software\\Microsoft\\Windows\\CurrentVersion\\Applets\\Regedit"
#define SAL_REG_VALUE_LAST_KEY_A "LastKey"
#define SAL_REG_VALUE_LAST_KEY_W L"LastKey"

#define SAL_REG_SUBKEY_COUNTER_A "Counter"
#define SAL_REG_SUBKEY_COUNTER_W L"Counter"
#define SAL_REG_VALUE_START_A "Start"
#define SAL_REG_VALUE_START_W L"Start"
#define SAL_REG_VALUE_STEP_A "Step"
#define SAL_REG_VALUE_STEP_W L"Step"
#define SAL_REG_VALUE_BASE_A "Base"
#define SAL_REG_VALUE_BASE_W L"Base"
#define SAL_REG_VALUE_MIN_WIDTH_A "MinWidth"
#define SAL_REG_VALUE_MIN_WIDTH_W L"MinWidth"
#define SAL_REG_VALUE_FILL_A "Fill"
#define SAL_REG_VALUE_FILL_W L"Fill"
#define SAL_REG_VALUE_LEFT_A "Left"
#define SAL_REG_VALUE_LEFT_W L"Left"

#define SAL_REG_KEY_SOFTWARE_CLASSES_A "Software\\Classes"
#define SAL_REG_KEY_SOFTWARE_CLASSES_W L"Software\\Classes"
#define SAL_REG_KEY_SHELL_EXT_APPROVED_A "Software\\Microsoft\\Windows\\CurrentVersion\\Shell Extensions\\Approved"
#define SAL_REG_KEY_SHELL_EXT_APPROVED_W L"Software\\Microsoft\\Windows\\CurrentVersion\\Shell Extensions\\Approved"
#define SAL_REG_KEY_SHELL_EXTENSION_ROOT_A "Software\\Altap\\Servant Salamander\\Shell Extension"
#define SAL_REG_KEY_SHELL_EXTENSION_ROOT_W L"Software\\Altap\\Servant Salamander\\Shell Extension"
#define SAL_REG_VALUE_THREADING_MODEL_A "ThreadingModel"
#define SAL_REG_VALUE_THREADING_MODEL_W L"ThreadingModel"
#define SAL_REG_FMT_SOFTWARE_CLASSES_CLSID_A "Software\\Classes\\CLSID\\%s"
#define SAL_REG_FMT_SOFTWARE_CLASSES_CLSID_W L"Software\\Classes\\CLSID\\%s"
#define SAL_REG_FMT_SOFTWARE_CLASSES_DIRECTORY_COPY_HOOK_A "Software\\Classes\\directory\\shellex\\CopyHookHandlers\\%s"
#define SAL_REG_FMT_SOFTWARE_CLASSES_DIRECTORY_COPY_HOOK_W L"Software\\Classes\\directory\\shellex\\CopyHookHandlers\\%s"
#define SAL_REG_FMT_SOFTWARE_CLASSES_STAR_CONTEXT_MENU_A "Software\\Classes\\*\\shellex\\ContextMenuHandlers\\%s"
#define SAL_REG_FMT_SOFTWARE_CLASSES_STAR_CONTEXT_MENU_W L"Software\\Classes\\*\\shellex\\ContextMenuHandlers\\%s"
#define SAL_REG_FMT_SOFTWARE_CLASSES_DIRECTORY_CONTEXT_MENU_A "Software\\Classes\\Directory\\shellex\\ContextMenuHandlers\\%s"
#define SAL_REG_FMT_SOFTWARE_CLASSES_DIRECTORY_CONTEXT_MENU_W L"Software\\Classes\\Directory\\shellex\\ContextMenuHandlers\\%s"
