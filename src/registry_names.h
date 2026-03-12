// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

// Shared registry paths, subkeys, and value names used across Sally.
// Keep registry-related string literals here instead of repeating them in call sites.

#define SAL_REG_ROOT_SALLY_1_0_A "Software\\Sally\\1.0"
#define SAL_REG_ROOT_OPENSAL_5_0_A "Software\\Open Salamander\\5.0"
#define SAL_REG_VERSION_SALLY_1_0_A "1.0"

#define SAL_REG_CONFIGURATION_ROOTS \
    SAL_REG_ROOT_SALLY_1_0_A, \
    SAL_REG_ROOT_OPENSAL_5_0_A, \
    "Software\\Altap\\Altap Salamander 4.0", \
    "Software\\Altap\\Altap Salamander 4.0 beta 1 (DB177)", \
    "Software\\Altap\\Altap Salamander 4.0 beta 1 (DB171)", \
    "Software\\Altap\\Altap Salamander 3.08", \
    "Software\\Altap\\Altap Salamander 4.0 beta 1 (DB168)", \
    "Software\\Altap\\Altap Salamander 3.07", \
    "Software\\Altap\\Altap Salamander 3.1 beta 1 (DB162)", \
    "Software\\Altap\\Altap Salamander 3.1 beta 1 (DB159)", \
    "Software\\Altap\\Altap Salamander 3.06", \
    "Software\\Altap\\Altap Salamander 3.1 beta 1 (DB153)", \
    "Software\\Altap\\Altap Salamander 3.05", \
    "Software\\Altap\\Altap Salamander 3.1 beta 1 (DB147)", \
    "Software\\Altap\\Altap Salamander 3.04", \
    "Software\\Altap\\Altap Salamander 3.1 beta 1 (DB141)", \
    "Software\\Altap\\Altap Salamander 3.03", \
    "Software\\Altap\\Altap Salamander 3.1 beta 1 (DB135)", \
    "Software\\Altap\\Altap Salamander 3.02", \
    "Software\\Altap\\Altap Salamander 3.1 beta 1 (DB129)", \
    "Software\\Altap\\Altap Salamander 3.01", \
    "Software\\Altap\\Altap Salamander 3.1 beta 1 (DB123)", \
    "Software\\Altap\\Altap Salamander 3.0", \
    "Software\\Altap\\Altap Salamander 3.0 beta 5 (DB117)", \
    "Software\\Altap\\Altap Salamander 3.0 beta 4", \
    "Software\\Altap\\Altap Salamander 3.0 beta 4 (DB111)", \
    "Software\\Altap\\Altap Salamander 3.0 beta 3", \
    "Software\\Altap\\Altap Salamander 3.0 beta 3 (DB105)", \
    "Software\\Altap\\Altap Salamander 3.0 beta 3 (PB103)", \
    "Software\\Altap\\Altap Salamander 3.0 beta 3 (DB100)", \
    "Software\\Altap\\Altap Salamander 3.0 beta 2", \
    "Software\\Altap\\Altap Salamander 3.0 beta 2 (DB94)", \
    "Software\\Altap\\Altap Salamander 3.0 beta 1", \
    "Software\\Altap\\Altap Salamander 3.0 beta 1 (DB88)", \
    "Software\\Altap\\Altap Salamander 3.0 beta 1 (PB87)", \
    "Software\\Altap\\Altap Salamander 3.0 beta 1 (DB83)", \
    "Software\\Altap\\Altap Salamander 3.0 beta 1 (DB80)", \
    "Software\\Altap\\Altap Salamander 3.0 beta 1 (PB79)", \
    "Software\\Altap\\Altap Salamander 3.0 beta 1 (DB76)", \
    "Software\\Altap\\Altap Salamander 3.0 beta 1 (PB75)", \
    "Software\\Altap\\Altap Salamander 2.55 beta 1 (DB 72)", \
    "Software\\Altap\\Altap Salamander 2.54", \
    "Software\\Altap\\Altap Salamander 2.54 beta 1 (DB 66)", \
    "Software\\Altap\\Altap Salamander 2.53", \
    "Software\\Altap\\Altap Salamander 2.53 (DB 60)", \
    "Software\\Altap\\Altap Salamander 2.53 beta 2", \
    "Software\\Altap\\Altap Salamander 2.53 beta 2 (IB 55)", \
    "Software\\Altap\\Altap Salamander 2.53 (DB 52)", \
    "Software\\Altap\\Altap Salamander 2.53 beta 1", \
    "Software\\Altap\\Altap Salamander 2.53 beta 1 (DB 46)", \
    "Software\\Altap\\Altap Salamander 2.53 beta 1 (PB 44)", \
    "Software\\Altap\\Altap Salamander 2.53 beta 1 (DB 41)", \
    "Software\\Altap\\Altap Salamander 2.53 beta 1 (DB 39)", \
    "Software\\Altap\\Altap Salamander 2.53 beta 1 (PB 38)", \
    "Software\\Altap\\Altap Salamander 2.53 beta 1 (DB 36)", \
    "Software\\Altap\\Altap Salamander 2.53 beta 1 (DB 33)", \
    "Software\\Altap\\Altap Salamander 2.52", \
    "Software\\Altap\\Altap Salamander 2.52 (DB 30)", \
    "Software\\Altap\\Altap Salamander 2.52 beta 2", \
    "Software\\Altap\\Altap Salamander 2.52 beta 1", \
    "Software\\Altap\\Altap Salamander 2.51", \
    "Software\\Altap\\Altap Salamander 2.5", \
    "Software\\Altap\\Altap Salamander 2.5 RC3", \
    "Software\\Altap\\Servant Salamander 2.5 RC3", \
    "Software\\Altap\\Servant Salamander 2.5 RC2", \
    "Software\\Altap\\Servant Salamander 2.5 RC1", \
    "Software\\Altap\\Servant Salamander 2.5 beta 12", \
    "Software\\Altap\\Servant Salamander 2.5 beta 11", \
    "Software\\Altap\\Servant Salamander 2.5 beta 10", \
    "Software\\Altap\\Servant Salamander 2.5 beta 9", \
    "Software\\Altap\\Servant Salamander 2.5 beta 8", \
    "Software\\Altap\\Servant Salamander 2.5 beta 7", \
    "Software\\Altap\\Servant Salamander 2.5 beta 6", \
    "Software\\Altap\\Servant Salamander 2.5 beta 5", \
    "Software\\Altap\\Servant Salamander 2.5 beta 4", \
    "Software\\Altap\\Servant Salamander 2.5 beta 3", \
    "Software\\Altap\\Servant Salamander 2.5 beta 2", \
    "Software\\Altap\\Servant Salamander 2.5 beta 1", \
    "Software\\Altap\\Servant Salamander 2.1 beta 1", \
    "Software\\Altap\\Servant Salamander 2.0", \
    "Software\\Altap\\Servant Salamander 1.6 beta 7", \
    "Software\\Altap\\Servant Salamander 1.6 beta 6", \
    "Software\\Altap\\Servant Salamander", \
    "Software\\Salamander"

#define SAL_REG_CONFIGURATION_VERSIONS \
    SAL_REG_VERSION_SALLY_1_0_A, \
    "5.0", \
    "4.0", \
    "4.0 beta 1 (DB177)", \
    "4.0 beta 1 (DB171)", \
    "3.08", \
    "4.0 beta 1 (DB168)", \
    "3.07", \
    "3.1 beta 1 (DB162)", \
    "3.1 beta 1 (DB159)", \
    "3.06", \
    "3.1 beta 1 (DB153)", \
    "3.05", \
    "3.1 beta 1 (DB147)", \
    "3.04", \
    "3.1 beta 1 (DB141)", \
    "3.03", \
    "3.1 beta 1 (DB135)", \
    "3.02", \
    "3.1 beta 1 (DB129)", \
    "3.01", \
    "3.1 beta 1 (DB123)", \
    "3.0", \
    "3.0 beta 5 (DB117)", \
    "3.0 beta 4", \
    "3.0 beta 4 (DB111)", \
    "3.0 beta 3", \
    "3.0 beta 3 (DB105)", \
    "3.0 beta 3 (PB103)", \
    "3.0 beta 3 (DB100)", \
    "3.0 beta 2", \
    "3.0 beta 2 (DB94)", \
    "3.0 beta 1", \
    "3.0 beta 1 (DB88)", \
    "3.0 beta 1 (PB87)", \
    "3.0 beta 1 (DB83)", \
    "3.0 beta 1 (DB80)", \
    "3.0 beta 1 (PB79)", \
    "3.0 beta 1 (DB76)", \
    "3.0 beta 1 (PB75)", \
    "2.55 beta 1 (DB72)", \
    "2.54", \
    "2.54 beta 1 (DB66)", \
    "2.53", \
    "2.53 (DB60)", \
    "2.53 beta 2", \
    "2.53 beta 2 (IB55)", \
    "2.53 (DB52)", \
    "2.53 beta 1", \
    "2.53 beta 1 (DB46)", \
    "2.53 beta 1 (PB44)", \
    "2.53 beta 1 (DB41)", \
    "2.53 beta 1 (DB39)", \
    "2.53 beta 1 (PB38)", \
    "2.53 beta 1 (DB36)", \
    "2.53 beta 1 (DB33)", \
    "2.52", \
    "2.52 (DB30)", \
    "2.52 beta 2", \
    "2.52 beta 1", \
    "2.51", \
    "2.5", \
    "2.5 RC3", \
    "2.5 RC3", \
    "2.5 RC2", \
    "2.5 RC1", \
    "2.5 beta 12", \
    "2.5 beta 11", \
    "2.5 beta 10", \
    "2.5 beta 9", \
    "2.5 beta 8", \
    "2.5 beta 7", \
    "2.5 beta 6", \
    "2.5 beta 5", \
    "2.5 beta 4", \
    "2.5 beta 3", \
    "2.5 beta 2", \
    "2.5 beta 1", \
    "2.1 beta 1", \
    "2.0", \
    "1.6 beta 7", \
    "1.6 beta 6", \
    "1.6 beta 1-5", \
    "1.52"

#define SAL_REG_SUBKEY_CONFIGURATION_A "Configuration"
#define SAL_REG_VALUE_DEFAULT_A ""
#define SAL_REG_VALUE_VERSION_A "Version"
#define SAL_REG_VALUE_AUTO_IMPORT_CONFIG_A "AutoImportConfig"
#define SAL_REG_MUTEX_LOADSAVE_A "SallyLoadSaveRegistry"

#define SAL_REG_KEY_WINDOWS_THEME_PERSONALIZE_A "Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize"
#define SAL_REG_KEY_WINDOWS_THEME_PERSONALIZE_T TEXT(SAL_REG_KEY_WINDOWS_THEME_PERSONALIZE_A)
#define SAL_REG_VALUE_APPS_USE_LIGHT_THEME_A "AppsUseLightTheme"
#define SAL_REG_VALUE_APPS_USE_LIGHT_THEME_T TEXT(SAL_REG_VALUE_APPS_USE_LIGHT_THEME_A)
#define SAL_REG_VALUE_SYSTEM_USES_LIGHT_THEME_A "SystemUsesLightTheme"
#define SAL_REG_VALUE_SYSTEM_USES_LIGHT_THEME_T TEXT(SAL_REG_VALUE_SYSTEM_USES_LIGHT_THEME_A)

#define SAL_REG_KEY_MICROSOFT_IE_A "Software\\Microsoft\\Internet Explorer"
#define SAL_REG_VALUE_IE_IVER_A "IVer"
#define SAL_REG_VALUE_BUILD_A "Build"

#define SAL_REG_KEY_WINDOWS_NT_CURRENT_VERSION_A "SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion"
#define SAL_REG_VALUE_WINDOWS_PRODUCT_NAME_A "ProductName"
#define SAL_REG_VALUE_WINDOWS_CURRENT_VERSION_A "CurrentVersion"

#define SAL_REG_KEY_HARDWARE_CPU0_A "Hardware\\Description\\System\\CentralProcessor\\0"
#define SAL_REG_VALUE_PROCESSOR_NAME_STRING_A "ProcessorNameString"
#define SAL_REG_VALUE_IDENTIFIER_A "Identifier"
#define SAL_REG_VALUE_VENDOR_IDENTIFIER_A "VendorIdentifier"
#define SAL_REG_VALUE_PROCESSOR_SPEED_MHZ_A "~MHz"

#define SAL_REG_KEY_HARDWARE_DESCRIPTION_SYSTEM_A "Hardware\\Description\\System"
#define SAL_REG_VALUE_SYSTEM_BIOS_VERSION_A "SystemBiosVersion"
#define SAL_REG_VALUE_SYSTEM_BIOS_DATE_A "SystemBiosDate"

#define SAL_REG_KEY_NETWORK_A "Network"
#define SAL_REG_VALUE_REMOTE_PATH_A "RemotePath"
#define SAL_REG_VALUE_USER_NAME_A "UserName"
#define SAL_REG_VALUE_PROVIDER_NAME_A "ProviderName"

#define SAL_REG_KEY_WIN81_ONEDRIVE_A "Software\\Microsoft\\Windows\\CurrentVersion\\OneDrive"
#define SAL_REG_KEY_WIN81_SKYDRIVE_A "Software\\Microsoft\\Windows\\CurrentVersion\\SkyDrive"
#define SAL_REG_KEY_ONEDRIVE_A "Software\\Microsoft\\OneDrive"
#define SAL_REG_KEY_SKYDRIVE_A "Software\\Microsoft\\SkyDrive"
#define SAL_REG_KEY_ONEDRIVE_ACCOUNTS_A "Software\\Microsoft\\OneDrive\\Accounts"
#define SAL_REG_VALUE_USER_FOLDER_A "UserFolder"
#define SAL_REG_VALUE_DISPLAY_NAME_A "DisplayName"

#define SAL_REG_KEY_EXPLORER_FILEEXTS_A "Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\FileExts"
#define SAL_REG_KEY_SYSTEM_FILE_ASSOCIATIONS_A "SystemFileAssociations"
#define SAL_REG_SUBKEY_SHELL_A "\\Shell"
#define SAL_REG_SUBKEY_SHELLEX_ICON_HANDLER_A "\\ShellEx\\IconHandler"
#define SAL_REG_SUBKEY_DEFAULT_ICON_A "\\DefaultIcon"
#define SAL_REG_VALUE_PERCEIVED_TYPE_A "PerceivedType"
#define SAL_REG_SUBKEY_USER_CHOICE_A "UserChoice"
#define SAL_REG_VALUE_PROGID_A "Progid"
#define SAL_REG_SUBKEY_OPEN_WITH_PROGIDS_A "OpenWithProgids"
#define SAL_REG_VALUE_APPLICATION_A "Application"

#define SAL_REG_KEY_CONTROL_PANEL_DESKTOP_A "Control Panel\\Desktop"
#define SAL_REG_KEY_CONTROL_PANEL_DESKTOP_T TEXT(SAL_REG_KEY_CONTROL_PANEL_DESKTOP_A)
#define SAL_REG_KEY_WINDOW_METRICS_A "Control Panel\\Desktop\\WindowMetrics"
#define SAL_REG_VALUE_WAIT_TO_KILL_APP_TIMEOUT_A "WaitToKillAppTimeout"
#define SAL_REG_VALUE_AUTO_END_TASKS_A "AutoEndTasks"
#define SAL_REG_VALUE_SHELL_ICON_SIZE_A "Shell Icon Size"
#define SAL_REG_VALUE_SHELL_ICON_BPP_A "Shell Icon Bpp"

#define SAL_REG_KEY_POLICIES_EXPLORER_CURRENT_USER_A "Software\\Microsoft\\Windows\\CurrentVersion\\Policies\\Explorer"
#define SAL_REG_KEY_POLICIES_EXPLORER_RESTRICT_RUN_CURRENT_USER_A "Software\\Microsoft\\Windows\\CurrentVersion\\Policies\\Explorer\\RestrictRun"
#define SAL_REG_KEY_POLICIES_EXPLORER_DISALLOW_RUN_CURRENT_USER_A "Software\\Microsoft\\Windows\\CurrentVersion\\Policies\\Explorer\\DisallowRun"
#define SAL_REG_KEY_POLICIES_NETWORK_CURRENT_USER_A "Software\\Microsoft\\Windows\\CurrentVersion\\Policies\\Network"
#define SAL_REG_KEY_POLICIES_EXPLORER_MACHINE_A "SOFTWARE\\Policies\\Microsoft\\Windows\\Explorer"
#define SAL_REG_VALUE_NO_RUN_A "NoRun"
#define SAL_REG_VALUE_NO_DRIVES_A "NoDrives"
#define SAL_REG_VALUE_NO_FIND_A "NoFind"
#define SAL_REG_VALUE_NO_SHELL_SEARCH_BUTTON_A "NoShellSearchButton"
#define SAL_REG_VALUE_NO_NET_HOOD_A "NoNetHood"
#define SAL_REG_VALUE_NO_NET_CONNECT_DISCONNECT_A "NoNetConnectDisconnect"
#define SAL_REG_VALUE_RESTRICT_RUN_A "RestrictRun"
#define SAL_REG_VALUE_DISALLOW_RUN_A "DisallowRun"
#define SAL_REG_VALUE_NO_DOT_BREAK_IN_LOGICAL_COMPARE_A "NoDotBreakInLogicalCompare"

#define SAL_REG_KEY_CLASSES_ROOT_CLSID_A "CLSID"
#define SAL_REG_KEY_SHELL_ICON_OVERLAY_IDENTIFIERS_A "Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\ShellIconOverlayIdentifiers"
#define SAL_REG_KEY_EXPLORER_SHELL_ICONS_A "Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Shell Icons"
#define SAL_REG_KEY_EXPLORER_SHELL_ICONS_T TEXT(SAL_REG_KEY_EXPLORER_SHELL_ICONS_A)

#define SAL_REG_KEY_BUG_REPORTER_A "Software\\Sally\\Bug Reporter"
#define SAL_REG_KEY_BUG_REPORTER_DB_A "Software\\Open Salamander\\Bug Reporter"
#define SAL_REG_VALUE_BUG_REPORTER_UID_A "ID"
#define SAL_REG_MUTEX_GLOBAL_BUG_REPORTER_A "Global\\SallyBugReporterRegistryMutex"

#define SAL_REG_KEY_TRANSLATOR_A "Software\\Open Salamander\\Translator"
#define SAL_REG_KEY_TRACE_SERVER_A "Software\\Sally\\Trace Server"
#define SAL_REG_KEY_TRACE_SERVER_T TEXT(SAL_REG_KEY_TRACE_SERVER_A)

#define SAL_REG_KEY_REGEDIT_APPLET_A "Software\\Microsoft\\Windows\\CurrentVersion\\Applets\\Regedit"
#define SAL_REG_VALUE_LAST_KEY_A "LastKey"

#define SAL_REG_SUBKEY_COUNTER_A "Counter"
#define SAL_REG_VALUE_START_A "Start"
#define SAL_REG_VALUE_STEP_A "Step"
#define SAL_REG_VALUE_BASE_A "Base"
#define SAL_REG_VALUE_MIN_WIDTH_A "MinWidth"
#define SAL_REG_VALUE_FILL_A "Fill"
#define SAL_REG_VALUE_LEFT_A "Left"

#define SAL_REG_KEY_SOFTWARE_CLASSES_A "Software\\Classes"
#define SAL_REG_KEY_SHELL_EXT_APPROVED_A "Software\\Microsoft\\Windows\\CurrentVersion\\Shell Extensions\\Approved"
#define SAL_REG_KEY_SHELL_EXTENSION_ROOT_A "Software\\Altap\\Servant Salamander\\Shell Extension"
#define SAL_REG_VALUE_THREADING_MODEL_A "ThreadingModel"
#define SAL_REG_FMT_SOFTWARE_CLASSES_CLSID_A "Software\\Classes\\CLSID\\%s"
#define SAL_REG_FMT_SOFTWARE_CLASSES_DIRECTORY_COPY_HOOK_A "Software\\Classes\\directory\\shellex\\CopyHookHandlers\\%s"
#define SAL_REG_FMT_SOFTWARE_CLASSES_STAR_CONTEXT_MENU_A "Software\\Classes\\*\\shellex\\ContextMenuHandlers\\%s"
#define SAL_REG_FMT_SOFTWARE_CLASSES_DIRECTORY_CONTEXT_MENU_A "Software\\Classes\\Directory\\shellex\\ContextMenuHandlers\\%s"
