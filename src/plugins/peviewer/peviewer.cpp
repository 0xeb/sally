// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

// vim: et:sw=2:ts=2

#include "precomp.h"

#include "peviewer.h"
#include "peviewer.rh"
#include "peviewer.rh2"
#include "lang\lang.rh"
#include "pefile.h"
#include "cfgdlg.h"
#include "cfg.h"
#include "unicode/helpers.h"

// Plugin interface object; its methods are called from Salamander.
CPluginInterface PluginInterface;
// Viewer-specific part of the CPluginInterface interface.
CPluginInterfaceForViewer InterfaceForViewer;

HINSTANCE DLLInstance = NULL; // handle to the SPL - language-independent resources
HINSTANCE HLanguage = NULL;   // handle to the SLG - language-dependent resources

// Salamander general interface - valid from launch until the plugin shuts down.
CSalamanderGeneralAbstract* SalGeneral = NULL;

// Variable definition for "dbg.h".
CSalamanderDebugAbstract* SalamanderDebug = NULL;

// ConfigVersion: 0 - default
//                1 - development version SS 2.1 beta 1
//                2 - version SS 2.5 beta 1
//                3 - AS 3.07: allows configuring the visibility of individual dump sections
int ConfigVersion = 0;
#define CURRENT_CONFIG_VERSION 3
const wchar_t* CONFIG_VERSION = L"Version";
const wchar_t* CONFIG_DUMPERS = L"Dumpers";

struct CFG_LOAD_ITEM
{
    const CFG_DUMPER* pDumperCfg;
    DWORD dwOrder;
};

//
// ****************************************************************************
// DllMain
//

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved)
{
    if (fdwReason == DLL_PROCESS_ATTACH)
        DLLInstance = hinstDLL;
    return TRUE; // DLL can be loaded
}

//
// ****************************************************************************
// LangStr
//

// Wide - SalGeneral->LoadStr has returned WCHAR* since the v108 ABI break.
std::wstring LangStr(int resID)
{
    return SPLLoadStrOwned(SalGeneral, HLanguage, resID);
}

//
// ****************************************************************************
// SalamanderPluginGetReqVer
//

int WINAPI SalamanderPluginGetReqVer()
{
    return LAST_VERSION_OF_SALAMANDER;
}

//
// ****************************************************************************
// SalamanderPluginEntry
//

CPluginInterfaceAbstract* WINAPI SalamanderPluginEntry(CSalamanderPluginEntryAbstract* salamander)
{
    // Set SalamanderDebug for "dbg.h".
    SalamanderDebug = salamander->GetSalamanderDebug();

    CALL_STACK_MESSAGE1("SalamanderPluginEntry()");

    // This plugin is built for the current Salamander version and newer - perform a check.
    if (salamander->GetVersion() < LAST_VERSION_OF_SALAMANDER)
    { // reject older versions
        // wide: REQUIRE_LAST_VERSION_OF_SALAMANDER is a shared narrow SDK macro
        // (spl_vers.h) used by ~35 plugins - widen only at this call site via the same
        // two-macro token-paste idiom already used for __WFILE__ in common/trace.h, rather
        // than touching the shared macro itself. (The note that used to follow - "_T() does not
        // help, this plugin does not define UNICODE" - stopped being true at P4.2, but the
        // token-paste is still the right answer: it does not depend on a define at all.)
#define PEVIEWER_WIDEN2(x) L##x
#define PEVIEWER_WIDEN(x) PEVIEWER_WIDEN2(x)
        MessageBoxW(salamander->GetParentWindow(),
                    PEVIEWER_WIDEN(REQUIRE_LAST_VERSION_OF_SALAMANDER),
                    L"Portable Executable Viewer" /* do not translate! */, MB_OK | MB_ICONERROR);
#undef PEVIEWER_WIDEN
#undef PEVIEWER_WIDEN2
        return NULL;
    }

    // Load the language module (.slg).
    HLanguage = salamander->LoadLanguageModule(salamander->GetParentWindow(), L"Portable Executable Viewer" /* do not translate! */);
    if (HLanguage == NULL)
        return NULL;

    if (!InitializeWinLib(L"PEVIEWER" /* do not translate! */, DLLInstance))
    {
        return NULL;
    }

    // Obtain Salamander's general interface.
    SalGeneral = salamander->GetSalamanderGeneral();

    // Set the basic plugin information.
    salamander->SetBasicPluginData(LangStr(IDS_PLUGIN_NAME).c_str(),
                                   FUNCTION_CONFIGURATION | FUNCTION_LOADSAVECONFIGURATION | FUNCTION_VIEWER,
                                   _CRT_WIDE(VERSINFO_VERSION_NO_PLATFORM),
                                   _CRT_WIDE(VERSINFO_COPYRIGHT),
                                   LangStr(IDS_PLUGIN_DESCRIPTION).c_str(),
                                   L"PEVIEWER");

    salamander->SetPluginHomePageURL(L"https://github.com/0xeb/sally");

    // Default configuration.
    BuildDefaultDumperChain();

    return &PluginInterface;
}

//
// ****************************************************************************
// CPluginInterface
//

void CPluginInterface::About(HWND parent)
{
    try
    {
        const std::wstring message = SPLFormatStringOwned(
            L"%s " _CRT_WIDE(VERSINFO_VERSION) L"\n\n" _CRT_WIDE(VERSINFO_COPYRIGHT) L"\n\n%s",
            LangStr(IDS_PLUGIN_NAME).c_str(), LangStr(IDS_PLUGIN_DESCRIPTION).c_str());
        SalGeneral->SalMessageBox(parent, message.c_str(), LangStr(IDS_ABOUT).c_str(), MB_OK | MB_ICONINFORMATION);
    }
    catch (...)
    {
        SalGeneral->SalMessageBox(parent, LangStr(IDS_PLUGIN_NAME).c_str(), LangStr(IDS_ABOUT).c_str(), MB_OK | MB_ICONINFORMATION);
    }
}

BOOL CPluginInterface::Release(HWND parent, BOOL force)
{
    ReleaseWinLib(DLLInstance);
    return TRUE;
}

static int CmpCfgLoadItems(void* context, const void* x, const void* y)
{
    UNREFERENCED_PARAMETER(context);

    const CFG_LOAD_ITEM* item1 = (const CFG_LOAD_ITEM*)x;
    const CFG_LOAD_ITEM* item2 = (const CFG_LOAD_ITEM*)y;

    if (item1->dwOrder == 0)
    {
        if (item2->dwOrder == 0)
        {
            return 0;
        }
        else
        {
            return 1;
        }
    }
    else if (item2->dwOrder == 0)
    {
        return -1;
    }

    return (int)item1->dwOrder - (int)item2->dwOrder;
}

void CPluginInterface::LoadConfiguration(HWND parent, HKEY regKey, CSalamanderRegistryAbstract* registry)
{
    CALL_STACK_MESSAGE1("CPluginInterface::LoadConfiguration(, ,)");
    if (regKey != NULL) // load from the registry
    {
        if (!registry->GetValue(regKey, CONFIG_VERSION, REG_DWORD, &ConfigVersion, sizeof(DWORD)))
        {
            ConfigVersion = CURRENT_CONFIG_VERSION; // apparently some mischief-maker... ;-)
        }

        HKEY subKey;
        if (registry->OpenKey(regKey, CONFIG_DUMPERS, subKey))
        {
            // Load the dumper order from the registry into this array. The items are then sorted
            // by the values read from the registry. We do not use the registry value directly for
            // indexing the array, because someone might tamper with it, the configuration could be
            // corrupted, etc.
            CFG_LOAD_ITEM dumpersInRegistry[AVAILABLE_PE_DUMPERS] = {
                0,
            };

            for (int i = 0; i < AVAILABLE_PE_DUMPERS; i++)
            {
                DWORD dwOrder;
                if (!registry->GetValue(subKey, g_cfgDumpers[i].pszKey, REG_DWORD, &dwOrder, sizeof(DWORD)))
                {
                    // If a dumper is not configured in the registry, enable it by default, but place it at the end of the current output.
                    dwOrder = 1000 + i;
                }
                dumpersInRegistry[i].pDumperCfg = &g_cfgDumpers[i];
                dumpersInRegistry[i].dwOrder = dwOrder;
            }

            registry->CloseKey(subKey);

            qsort_s(dumpersInRegistry, AVAILABLE_PE_DUMPERS, sizeof(CFG_LOAD_ITEM), CmpCfgLoadItems, NULL);

            g_cfgChainLength = 0;
            for (int i = 0; i < AVAILABLE_PE_DUMPERS; i++)
            {
                if (dumpersInRegistry[i].dwOrder == 0)
                {
                    // Disabled dumpers were moved to the end of the array; as soon as I encounter
                    // the first disabled one, I stop.
                    break;
                }

                g_cfgChain[i].pDumperCfg = dumpersInRegistry[i].pDumperCfg;
                ++g_cfgChainLength;
            }
        }
    }
    else // default configuration
    {
        ConfigVersion = 0;
    }
}

void CPluginInterface::SaveConfiguration(HWND parent, HKEY regKey, CSalamanderRegistryAbstract* registry)
{
    DWORD v = CURRENT_CONFIG_VERSION;
    registry->SetValue(regKey, CONFIG_VERSION, REG_DWORD, &v, sizeof(DWORD));

    HKEY subKey;
    if (registry->CreateKey(regKey, CONFIG_DUMPERS, subKey))
    {
        for (int i = 0; i < AVAILABLE_PE_DUMPERS; i++)
        {
            const CFG_DUMPER* dumper = &g_cfgDumpers[i];
            int nDumperIndexInChain = FindDumperInChain(dumper);                   // 0..n-1, -1=not found
            DWORD dwOrder = nDumperIndexInChain < 0 ? 0 : nDumperIndexInChain + 1; // 1..n, 0=disabled
            registry->SetValue(subKey, dumper->pszKey, REG_DWORD, &dwOrder, sizeof(dwOrder));
        }

        registry->CloseKey(subKey);
    }
}

void CPluginInterface::Configuration(HWND parent)
{
    CPeViewerConfigDialog(parent).Execute();
}

void CPluginInterface::Connect(HWND parent, CSalamanderConnectAbstract* salamander)
{
    CALL_STACK_MESSAGE1("CPluginInterface::Connect(,)");
    salamander->AddViewer(L"*.cpl;*.dll;*.drv;*.exe;*.ocx;*.spl;*.sys;*.scr", FALSE); // default (plugin installation); otherwise Salamander ignores it

    if (ConfigVersion == 1) // in this development version before SS 2.5 beta 1 we removed *.SCR (this is a fix)
    {
        salamander->AddViewer(L"*.scr", TRUE); // re-add the "*.scr" extension (we already have CPluginInterfaceForViewerAbstract::CanViewFile)
    }
}

CPluginInterfaceForViewerAbstract*
CPluginInterface::GetInterfaceForViewer()
{
    return &InterfaceForViewer;
}

//
// ****************************************************************************
// CPluginInterfaceForViewer
//

BOOL MapFileToMemory(const wchar_t* name, HANDLE& hFile, HANDLE& hFileMapping,
                     LPVOID& lpFileBase, DWORD& fileSize, BOOL quietMode)
{
    const auto showOpenError = [name]() {
        try
        {
            const std::wstring message = SPLFormatStringOwned(LangStr(IDS_ERR_OPEN_FILE).c_str(), name);
            SalGeneral->SalMessageBox(SalGeneral->GetMsgBoxParent(), message.c_str(),
                                      LangStr(IDS_PLUGIN_NAME).c_str(), MB_OK | MB_ICONEXCLAMATION);
        }
        catch (...)
        {
            SalGeneral->SalMessageBox(SalGeneral->GetMsgBoxParent(), LangStr(IDS_ERR_OPEN_FILE).c_str(),
                                      LangStr(IDS_PLUGIN_NAME).c_str(), MB_OK | MB_ICONEXCLAMATION);
        }
    };

    hFile = CreateFileW(name, GENERIC_READ, FILE_SHARE_READ, NULL,
                        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);

    if (hFile == INVALID_HANDLE_VALUE)
    {
        if (!quietMode)
        {
            showOpenError();
        }
        return FALSE;
    }

    fileSize = GetFileSize(hFile, NULL);
    if (fileSize == -1)
    {
        if (!quietMode)
        {
            showOpenError();
        }
        CloseHandle(hFile);
        return FALSE;
    }

    hFileMapping = CreateFileMapping(hFile, NULL, PAGE_READONLY, 0, 0, NULL);
    if (hFileMapping == 0)
    {
        if (!quietMode)
        {
            showOpenError();
        }
        CloseHandle(hFile);
        return FALSE;
    }

    lpFileBase = MapViewOfFile(hFileMapping, FILE_MAP_READ, 0, 0, 0);
    if (lpFileBase == 0)
    {
        if (!quietMode)
        {
            showOpenError();
        }
        CloseHandle(hFileMapping);
        CloseHandle(hFile);
        return FALSE;
    }
    return TRUE;
}

void UnmapFileFromMemory(HANDLE& hFile, HANDLE& hFileMapping, LPVOID& lpFileBase)
{
    UnmapViewOfFile(lpFileBase);
    CloseHandle(hFileMapping);
    CloseHandle(hFile);
}

namespace
{
void ShowPeviewerFileMessage(int messageId, const wchar_t* name) noexcept
{
    try
    {
        const std::wstring message = SPLFormatStringOwned(LangStr(messageId).c_str(), name);
        SalGeneral->SalMessageBox(SalGeneral->GetMsgBoxParent(), message.c_str(),
                                  LangStr(IDS_PLUGIN_NAME).c_str(), MB_OK | MB_ICONEXCLAMATION);
    }
    catch (...)
    {
        SalGeneral->SalMessageBox(SalGeneral->GetMsgBoxParent(), LangStr(messageId).c_str(),
                                  LangStr(IDS_PLUGIN_NAME).c_str(), MB_OK | MB_ICONEXCLAMATION);
    }
}
} // namespace

BOOL CPluginInterfaceForViewer::ViewFile(const wchar_t* name, int left, int top, int width,
                                         int height, UINT showCmd, BOOL alwaysOnTop,
                                         BOOL returnLock, HANDLE* lock, BOOL* lockOwner,
                                         CSalamanderPluginViewerData* viewerData,
                                         int enumFilesSourceUID, int enumFilesCurrentIndex)
{
    CALL_STACK_MESSAGE11("CPluginInterfaceForViewer::ViewFile(%ls, %d, %d, %d, %d, "
                         "0x%X, %d, %d, , , , %d, %d)",
                         name, left, top, width, height,
                         showCmd, alwaysOnTop, returnLock, enumFilesSourceUID, enumFilesCurrentIndex);

    // 'lock' and 'lockOwner' are not set; the lifetime of the file 'name' only needs to
    // cover this method.

    HANDLE hFile;
    HANDLE hFileMapping;
    LPVOID lpFileBase;
    DWORD fileSize;

    HCURSOR hOldCur = SetCursor(LoadCursor(NULL, IDC_WAIT));

    // Try to map the file into memory.
    if (!MapFileToMemory(name, hFile, hFileMapping, lpFileBase, fileSize, FALSE))
    {
        SetCursor(hOldCur);
        return FALSE;
    }

    std::wstring tempFileName;
    if (SPLSalGetTempFileNameOwned(SalGeneral, NULL, L"PEV", tempFileName, TRUE, NULL))
    {
        int err;
        CSalamanderPluginInternalViewerData vData;

        // Create a temporary file and pour the module dump into it.
        FILE* outStream = NULL;
        _wfopen_s(&outStream, tempFileName.c_str(), L"w");
        if (outStream == NULL)
        {
            DeleteFileW(tempFileName.c_str());
            SetCursor(hOldCur);
            SalGeneral->SalMessageBox(SalGeneral->GetMsgBoxParent(), LangStr(IDS_ERR_TMP).c_str(),
                                      LangStr(IDS_PLUGIN_NAME).c_str(), MB_OK | MB_ICONEXCLAMATION);
            UnmapFileFromMemory(hFile, hFileMapping, lpFileBase);
            return FALSE;
        }
        BOOL dumpSucceeded = FALSE;
        try
        {
            dumpSucceeded = DumpFileInfo(lpFileBase, fileSize, outStream);
        }
        catch (...)
        {
            dumpSucceeded = FALSE;
        }
        const int closeResult = fclose(outStream);
        if (!dumpSucceeded || closeResult != 0)
        {
            SetCursor(hOldCur);
            ShowPeviewerFileMessage(IDS_EXCEPTION, name);
            DeleteFileW(tempFileName.c_str());
            UnmapFileFromMemory(hFile, hFileMapping, lpFileBase);
            return FALSE;
        }

        // Hand the file over to Salamander - it moves it into the cache and deletes it once
        // it is no longer needed.
        vData.Size = sizeof(vData);
        vData.FileName = tempFileName.c_str();
        vData.Mode = 0; // text mode
        std::wstring caption;
        try
        {
            caption = name;
            caption += L" - ";
            caption += LangStr(IDS_PLUGIN_NAME);
            vData.Caption = caption.c_str();
        }
        catch (...)
        {
            vData.Caption = name;
        }
        vData.WholeCaption = TRUE;
        if (!SalGeneral->ViewFileInPluginViewer(NULL, &vData, TRUE, NULL, L"pe_dump.txt", err))
        {
            // The file is deleted even in case of failure.
        }
    }
    else
    {
        if (!tempFileName.empty())
            DeleteFileW(tempFileName.c_str());
        SetCursor(hOldCur);
        SalGeneral->SalMessageBox(SalGeneral->GetMsgBoxParent(), LangStr(IDS_ERR_TMP).c_str(),
                                  LangStr(IDS_PLUGIN_NAME).c_str(), MB_OK | MB_ICONEXCLAMATION);
    }

    // Unmap the file from memory.
    UnmapFileFromMemory(hFile, hFileMapping, lpFileBase);

    SetCursor(hOldCur);
    return TRUE;
}

BOOL CPluginInterfaceForViewer::CanViewFile(const wchar_t* name)
{
    HANDLE hFile;
    HANDLE hFileMapping;
    LPVOID lpFileBase;
    DWORD fileSize;

    // Try to map the file into memory (silently -- no error messages).
    if (!MapFileToMemory(name, hFile, hFileMapping, lpFileBase, fileSize, TRUE))
        return FALSE;

    // Extract the header type.
    CPEFile PEFile(lpFileBase, fileSize);
    DWORD imageType = PEFile.ImageFileType(NULL);

    // Unmap the file from memory.
    UnmapFileFromMemory(hFile, hFileMapping, lpFileBase);

    // Allow our viewer if it is a known header.
    return imageType == IMAGE_DOS_SIGNATURE || imageType == IMAGE_OS2_SIGNATURE ||
           imageType == IMAGE_VXD_SIGNATURE || imageType == IMAGE_NT_SIGNATURE;
}
