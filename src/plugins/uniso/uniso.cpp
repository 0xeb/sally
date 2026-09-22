// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"
#include "dbg.h"

#include "uniso.h"
#include "isoimage.h"
#include "dialogs.h"

#include "uniso.rh"
#include "uniso.rh2"
#include "lang\lang.rh"
#include "unicode/helpers.h"

// ****************************************************************************

HINSTANCE DLLInstance = NULL; // handle to the SPL - language-independent resources
HINSTANCE HLanguage = NULL;   // handle to the SLG - language-dependent resources

// interface object whose methods are called from Salamander
CPluginInterface PluginInterface;
// portion of the CPluginInterface for the archiver
CPluginInterfaceForArchiver InterfaceForArchiver;
// portion of the CPluginInterface for the viewer
CPluginInterfaceForViewer InterfaceForViewer;

// Salamander's general interface - valid from startup until the plugin is closed
CSalamanderGeneralAbstract* SalamanderGeneral = NULL;

// ZLIB compression/decompression interface;
CSalamanderZLIBAbstract* SalZLIB = NULL;

// BZIP2 compression/decompression interface;
CSalamanderBZIP2Abstract* SalBZIP2 = NULL;

// interface for comfortable work with files
CSalamanderSafeFileAbstract* SalamanderSafeFile = NULL;

// variable definition for "dbg.h"
CSalamanderDebugAbstract* SalamanderDebug = NULL;

// interface providing customized Windows controls used in Salamander
CSalamanderGUIAbstract* SalamanderGUI = NULL;

// variable definition for "spl_com.h"
int SalamanderVersion = 0;

int ConfigVersion = 0;
#define CURRENT_CONFIG_VERSION 6
const wchar_t* CONFIG_VERSION = L"Version";

// for now this configuration slot is sufficient
//DWORD Options;
COptions Options;

CSalamanderBZIP2Abstract* GetSalamanderBZIP2();

const SYSTEMTIME MinTime = {1980, 01, 2, 01, 00, 00, 00, 000};

int SortByExtDirsAsFiles = FALSE; // current value of Salamander's configuration variable SALCFG_SORTBYEXTDIRSASFILES

//const char *CONFIG_OPTIONS = "Options";
const wchar_t* CONFIG_CLEAR_READONLY = L"Clear Read Only";
const wchar_t* CONFIG_SESSION_AS_DIR = L"Show Session As Directory";
const wchar_t* CONFIG_BOOTIMAGE_AS_FILE = L"Show Boot Image As File";

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved)
{
    switch (fdwReason)
    {
    case DLL_PROCESS_ATTACH:
    {
        DLLInstance = hinstDLL;
        break;
    }

    case DLL_PROCESS_DETACH:
    {
        break;
    }
    }
    return TRUE; // DLL can be loaded
}

// Wide - SalamanderGeneral->LoadStr has returned WCHAR* since the v108 ABI break.
std::wstring LangStr(int resID)
{
    return SPLLoadStrOwned(SalamanderGeneral, HLanguage, resID);
}

int WINAPI SalamanderPluginGetReqVer()
{
    return LAST_VERSION_OF_SALAMANDER;
}

CPluginInterfaceAbstract* WINAPI SalamanderPluginEntry(CSalamanderPluginEntryAbstract* salamander)
{
    // set SalamanderDebug for "dbg.h"
    SalamanderDebug = salamander->GetSalamanderDebug();
    // set SalamanderVersion for "spl_com.h"
    SalamanderVersion = salamander->GetVersion();
    HANDLES_CAN_USE_TRACE();

    CALL_STACK_MESSAGE1("SalamanderPluginEntry()");

    // this plugin is built for the current version of Salamander and newer - perform a check
    if (SalamanderVersion < LAST_VERSION_OF_SALAMANDER)
    { // we reject older versions
        // wide: same call-site-local widen shape used throughout this backlog
        // (205-218).
#define UNISO_WIDEN2(x) L##x
#define UNISO_WIDEN(x) UNISO_WIDEN2(x)
        MessageBoxW(salamander->GetParentWindow(),
                    UNISO_WIDEN(REQUIRE_LAST_VERSION_OF_SALAMANDER),
                    L"UnISO" /* do not translate! */, MB_OK | MB_ICONERROR);
#undef UNISO_WIDEN
#undef UNISO_WIDEN2
        return NULL;
    }

    // load the language module (.slg)
    HLanguage = salamander->LoadLanguageModule(salamander->GetParentWindow(), L"UnISO" /* do not translate! */);
    if (HLanguage == NULL)
        return NULL;

    // obtain Salamander's general interface
    SalamanderGeneral = salamander->GetSalamanderGeneral();
    SalZLIB = SalamanderGeneral->GetSalamanderZLIB();
    // obtain the interface providing customized Windows controls used in Salamander
    SalamanderGUI = salamander->GetSalamanderGUI();

#if LAST_VERSION_OF_SALAMANDER >= 33
    SalBZIP2 = SalamanderGeneral->GetSalamanderBZIP2();
#else
    SalBZIP2 = GetSalamanderBZIP2();
#endif

    SalamanderSafeFile = salamander->GetSalamanderSafeFile();

    // set the name of the help file
    SalamanderGeneral->SetHelpFileName(L"uniso.chm");

    SalamanderGeneral->GetConfigParameter(SALCFG_SORTBYEXTDIRSASFILES, &SortByExtDirsAsFiles,
                                          sizeof(SortByExtDirsAsFiles), NULL);

    if (!InterfaceForArchiver.Init())
        return NULL;

    if (!InitializeWinLib(L"UnISO" /* do not translate! */, DLLInstance))
        return NULL;
    SetWinLibStrings(L"Invalid number!", LangStr(IDS_PLUGINNAME).c_str());

    // set up the basic plugin information
    salamander->SetBasicPluginData(LangStr(IDS_PLUGINNAME).c_str(),
                                   FUNCTION_PANELARCHIVERVIEW | FUNCTION_CUSTOMARCHIVERUNPACK |
                                       FUNCTION_CONFIGURATION | FUNCTION_LOADSAVECONFIGURATION |
                                       FUNCTION_VIEWER,
                                   _CRT_WIDE(VERSINFO_VERSION_NO_PLATFORM),
                                   _CRT_WIDE(VERSINFO_COPYRIGHT),
                                   LangStr(IDS_PLUGIN_DESCRIPTION).c_str(),
                                   L"UnISO" /* do not translate! */, L"iso;isz;nrg;bin;img;pdi;cdi;cif;ncd;c2d;dmg");

    salamander->SetPluginHomePageURL(L"https://github.com/0xeb/sally");

    return &PluginInterface;
}

static std::wstring FormatResourceText(int resID, va_list args)
{
    va_list countArgs;
    va_copy(countArgs, args);
    const int length = _vscwprintf(LangStr(resID).c_str(), countArgs);
    va_end(countArgs);
    if (length < 0)
        return std::wstring();

    std::vector<wchar_t> buffer((size_t)length + 1, L'\0');
    va_list writeArgs;
    va_copy(writeArgs, args);
    const int written = _vsnwprintf_s(buffer.data(), buffer.size(), _TRUNCATE, LangStr(resID).c_str(), writeArgs);
    va_end(writeArgs);
    return written < 0 ? std::wstring() : std::wstring(buffer.data(), written);
}

BOOL Warning(int resID, BOOL quiet, ...)
{
    if (!quiet)
    {
        va_list arglist;
        va_start(arglist, quiet);
        const std::wstring message = FormatResourceText(resID, arglist);
        va_end(arglist);

        SalamanderGeneral->ShowMessageBox(message.c_str(), LangStr(IDS_PLUGINNAME).c_str(), MSGBOX_WARNING);
    }
    return FALSE;
}

BOOL Error(int resID, BOOL quiet, ...)
{
    if (!quiet)
    {
        va_list arglist;
        va_start(arglist, quiet);
        const std::wstring message = FormatResourceText(resID, arglist);
        va_end(arglist);

        SalamanderGeneral->ShowMessageBox(message.c_str(), LangStr(IDS_PLUGINNAME).c_str(), MSGBOX_ERROR);
    }
    return FALSE;
}

BOOL Error(const wchar_t* msg, DWORD err, BOOL quiet)
{
    if (!quiet)
        if (err != ERROR_FILE_NOT_FOUND && err != ERROR_NO_MORE_FILES) // it is an error
        {
            // GetErrorText is wide-only (spl_gen.h); the second %s used to read it as a
            // narrow char*, walking UTF-16 bytes and producing a garbled/truncated system
            // error message in every I/O-error dialog this overload reports.
            const std::wstring message = std::wstring(msg) + L"\n\n" + SPLGetErrorTextOwned(SalamanderGeneral, err);
            SalamanderGeneral->ShowMessageBox(message.c_str(), LangStr(IDS_PLUGINNAME).c_str(), MSGBOX_ERROR);
        }

    return FALSE;
}

BOOL SysError(int title, int error, ...)
{
    int lastErr = GetLastError();
    CALL_STACK_MESSAGE3("SysError(%d, %d, ...)", title, error);
    va_list arglist;
    va_start(arglist, error);
    std::wstring message = FormatResourceText(error, arglist);
    va_end(arglist);
    if (lastErr != ERROR_SUCCESS)
    {
        message += L" ";
        message += SPLGetErrorTextOwned(SalamanderGeneral, lastErr);
    }
    SalamanderGeneral->ShowMessageBox(message.c_str(), LangStr(title).c_str(), MSGBOX_ERROR);
    return FALSE;
}

// ****************************************************************************
//
// CPluginInterface
//

void CPluginInterface::About(HWND parent)
{
    try
    {
        const std::wstring message = SPLFormatStringOwned(
            L"%s " _CRT_WIDE(VERSINFO_VERSION) L"\n\n" _CRT_WIDE(VERSINFO_COPYRIGHT) L"\n\n%s",
            LangStr(IDS_PLUGINNAME).c_str(), LangStr(IDS_PLUGIN_DESCRIPTION).c_str());
        SalamanderGeneral->SalMessageBox(parent, message.c_str(), LangStr(IDS_ABOUT).c_str(), MB_OK | MB_ICONINFORMATION);
    }
    catch (...)
    {
        SalamanderGeneral->SalMessageBox(parent, LangStr(IDS_PLUGINNAME).c_str(), LangStr(IDS_ABOUT).c_str(), MB_OK | MB_ICONINFORMATION);
    }
}

BOOL CPluginInterface::Release(HWND parent, BOOL force)
{
    CALL_STACK_MESSAGE2("CPluginInterface::Release(, %d)", force);

    ReleaseWinLib(DLLInstance);

    return TRUE;
}

void CPluginInterface::LoadConfiguration(HWND parent, HKEY regKey, CSalamanderRegistryAbstract* registry)
{
    CALL_STACK_MESSAGE1("CPluginInterface::LoadConfiguration(, ,)");

    if (regKey != NULL) // load from the registry
    {
        if (!registry->GetValue(regKey, CONFIG_VERSION, REG_DWORD, &ConfigVersion, sizeof(DWORD)))
            ConfigVersion = 0; // default configuration
    }
    else
    {
        ConfigVersion = 0; // default configuration
    }

    // set defaults
    Options.ClearReadOnly = TRUE;
    Options.SessionAsDirectory = TRUE; // by default we show how good we are (they can turn it off if they want)
    Options.BootImageAsFile = TRUE;    // by default we show the boot image (they can turn it off if they want)

    if (regKey != NULL) // load from the registry
    {
        registry->GetValue(regKey, CONFIG_CLEAR_READONLY, REG_DWORD, &Options.ClearReadOnly, sizeof(DWORD));
        registry->GetValue(regKey, CONFIG_SESSION_AS_DIR, REG_DWORD, &Options.SessionAsDirectory, sizeof(DWORD));
        registry->GetValue(regKey, CONFIG_BOOTIMAGE_AS_FILE, REG_DWORD, &Options.BootImageAsFile, sizeof(DWORD));
    }
}

void CPluginInterface::SaveConfiguration(HWND parent, HKEY regKey, CSalamanderRegistryAbstract* registry)
{
    CALL_STACK_MESSAGE1("CPluginInterface::SaveConfiguration(, ,)");

    DWORD v = CURRENT_CONFIG_VERSION;
    registry->SetValue(regKey, CONFIG_VERSION, REG_DWORD, &v, sizeof(DWORD));

    registry->SetValue(regKey, CONFIG_CLEAR_READONLY, REG_DWORD, &Options.ClearReadOnly, sizeof(DWORD));
    registry->SetValue(regKey, CONFIG_SESSION_AS_DIR, REG_DWORD, &Options.SessionAsDirectory, sizeof(DWORD));
    registry->SetValue(regKey, CONFIG_BOOTIMAGE_AS_FILE, REG_DWORD, &Options.BootImageAsFile, sizeof(DWORD));
}

void CPluginInterface::Configuration(HWND parent)
{
    CConfigurationDialog dlg(parent);
    dlg.Execute();
}

void CPluginInterface::Connect(HWND parent, CSalamanderConnectAbstract* salamander)
{
    CALL_STACK_MESSAGE1("CPluginInterface::Connect(,)");

    /*  GENERAL RULES FOR IMPLEMENTING CONNECT (for more complex plugins with a configuration version - the
                                                ConfigVersion variable and the CURRENT_CONFIG_VERSION constant):
    - with each change you need to increase the CURRENT_CONFIG_VERSION number
      (in the first version CURRENT_CONFIG_VERSION = 1, not 0, so an upgrade can be distinguished from
       an installation)
    - in the base part (before the "if (ConfigVersion < YYY)" conditions):
      - write the code for the first installation of the plugin (the state where the plugin does not yet have a record
        in Salamander)
      - for AddCustomPacker and AddCustomUnpacker calls provide the condition "ConfigVersion < XXX" in the 'update'
        parameter, where XXX is the number of the last version in which the extensions for custom packers or
        unpackers changed (XXX for packers may differ from unpackers)
      - AddMenuItem, SetChangeDriveMenuItem, and SetThumbnailLoader work the same every time the plugin is loaded
        (installation/upgrades make no difference - we always start on a clean slate)
    - in the upgrade section (after the base part):
      - add a condition "if (ConfigVersion < XXX)", where XXX is the new value of the
        CURRENT_CONFIG_VERSION constant + add a comment from that version;
        in the body of that condition call:
        - if extensions were added for the "panel archiver", call
          "AddPanelArchiver(PPP, EEE, TRUE)", where PPP are only the new extensions separated
          by a semicolon and EEE is TRUE/FALSE ("panel view+edit"/"panel view only")
        - if extensions were added for the "viewer", call "AddViewer(PPP, TRUE)",
          where PPP are only the new extensions separated by a semicolon
        - if some old extensions for the "viewer" need to be removed, call
          "ForceRemoveViewer(PPP)" for each such extension PPP
        - if extensions for the "panel archiver" need to be removed, let Petr know; nobody has
          needed it yet, so it is not implemented
  */

    // Davide, when you add more extensions you need to increase CURRENT_CONFIG_VERSION, see ^^^

    // BASE PART
    // AddViewer and AddPanelArchiver are subject to the UPGRADE SECTION
    salamander->AddViewer(L"*.bin;*.img;*.iso;*.isz;*.nrg;*.pdi;*.cdi;*.cif;*.ncd;*.c2d;*.mdf", FALSE); // default (plugin installation), otherwise Salamander ignores it
    salamander->AddPanelArchiver(L"iso;isz;nrg;bin;img;pdi;cdi;cif;ncd;c2d;mdf;dmg", FALSE, FALSE);

    // in version 3 we added C2D
    // in version 4 we added MDF
    salamander->AddCustomUnpacker(L"UnISO (Plugin)",
                                  L"*.iso;*.isz;*.nrg;*.bin;*.img;*.pdi;*.cdi;*.cif;*.ncd;*.c2d;*.mdf;*.dmg", ConfigVersion < 6);

    // set the plugin icon
    HBITMAP hBmp = (HBITMAP)LoadImage(DLLInstance, MAKEINTRESOURCE(IDB_UNISO),
                                      IMAGE_BITMAP, 16, 16, LR_DEFAULTCOLOR);
    salamander->SetBitmapWithIcons(hBmp);
    DeleteObject(hBmp);
    salamander->SetPluginIcon(0);
    salamander->SetPluginMenuAndToolbarIcon(0);

    // UPGRADE SECTION
    if (ConfigVersion < 2) // addition of NRG, PDI, CDI, CIF, NCD
    {
        salamander->AddViewer(L"*.nrg;*.pdi;*.cdi;*.cif;*.ncd", TRUE);
        salamander->AddPanelArchiver(L"nrg;pdi;cdi;cif;ncd", FALSE, TRUE);
    }

    if (ConfigVersion < 3) // addition of C2D
    {
        salamander->AddViewer(L"*.c2d", TRUE);
        salamander->AddPanelArchiver(L"c2d", FALSE, TRUE);
    }

    if (ConfigVersion < 4) // addition of MDF/MDS
    {
        salamander->AddViewer(L"*.mdf", TRUE);
        salamander->AddPanelArchiver(L"mdf", FALSE, TRUE);
    }

    if (ConfigVersion < 5) // addition of DMG
    {
        salamander->AddViewer(L"*.dmg", TRUE);
        salamander->AddPanelArchiver(L"dmg", FALSE, TRUE);
    }

    if (ConfigVersion < 6) // addition of ISZ
    {
        salamander->AddViewer(L"*.isz", TRUE);
        salamander->AddPanelArchiver(L"isz", FALSE, TRUE);
    }
    //  _CrtSetBreakAlloc(13012);
}

void CPluginInterface::Event(int event, DWORD param)
{
    if (event == PLUGINEVENT_CONFIGURATIONCHANGED)
    {
        SalamanderGeneral->GetConfigParameter(SALCFG_SORTBYEXTDIRSASFILES, &SortByExtDirsAsFiles,
                                              sizeof(SortByExtDirsAsFiles), NULL);
    }
}

void CPluginInterface::ReleasePluginDataInterface(CPluginDataInterfaceAbstract* pluginData)
{
    delete ((CPluginDataInterface*)pluginData);
}

CPluginInterfaceForArchiverAbstract*
CPluginInterface::GetInterfaceForArchiver()
{
    return &InterfaceForArchiver;
}

CPluginInterfaceForViewerAbstract*
CPluginInterface::GetInterfaceForViewer()
{
    return &InterfaceForViewer;
}

// ****************************************************************************
//
// CPluginInterfaceForArchiver
//

CPluginInterfaceForArchiver::CPluginInterfaceForArchiver()
{
}

BOOL CPluginInterfaceForArchiver::Init()
{
    CALL_STACK_MESSAGE1("CPluginInterfaceForArchiver::Init()");

    return TRUE;
}

BOOL CPluginInterfaceForArchiver::ListArchive(CSalamanderForOperationsAbstract* salamander,
                                              const wchar_t* fileName,
                                              CSalamanderDirectoryAbstract* dir,
                                              CPluginDataInterfaceAbstract*& pluginData)
{
    CALL_STACK_MESSAGE2("CPluginInterfaceForArchiver::ListArchive(, %ls, ,)", fileName);

    pluginData = new CPluginDataInterface();
    if (pluginData == NULL)
    {
        return Error(IDS_INSUFFICIENT_MEMORY);
    }

    CPluginDataInterface* pd = (CPluginDataInterface*)pluginData;

    // try to open the ISO image
    BOOL ret = FALSE;
    HWND hParent = SalamanderGeneral->GetMsgBoxParent();
    CISOImage isoImage; // the destructor calls Close
    isoImage.DisplayMissingCCDWarning = pd->DisplayMissingCCDWarning;
    // CISOImage::Open is wide now - opens the .iso/.nrg/... image's own on-disk path
    // directly, no narrow round trip (ToNarrowArchive used to risk failing to open
    // images whose host-filesystem path isn't representable in the ANSI codepage).
    if (isoImage.Open(fileName, FALSE))
    {
        // hand over the complete listing to Salamander's core
        if (isoImage.ListImage(dir, pluginData))
        {
            ret = TRUE;

            pd->DisplayMissingCCDWarning = isoImage.DisplayMissingCCDWarning;
        }
    }

    if (ret == FALSE)
    {
        delete (CPluginDataInterface*)pluginData;
        pluginData = NULL;
    }

    return ret;
}

void FreeString(void* strig)
{
    free(strig);
}

BOOL CPluginInterfaceForArchiver::UnpackArchive(CSalamanderForOperationsAbstract* salamander, const wchar_t* fileName,
                                                CPluginDataInterfaceAbstract* pluginData, const wchar_t* targetDir,
                                                const wchar_t* archiveRoot, SalEnumSelection next, void* nextParam)
{
    CALL_STACK_MESSAGE4("CPluginInterfaceForArchiver::UnpackArchive(, %ls, , %ls, %ls, ,)", fileName, targetDir, archiveRoot);

    if (pluginData == NULL)
    {
        TRACE_E("Internal error");
        return FALSE;
    }

    CPluginDataInterface* pd = (CPluginDataInterface*)pluginData;

    // try to open the ISO image
    CISOImage isoImage; // the destructor calls Close
    isoImage.DisplayMissingCCDWarning = pd->DisplayMissingCCDWarning;
    if (!isoImage.Open(fileName, FALSE))
        return FALSE;

    BOOL ret = FALSE;
    // compute 'totalSize' for the progress dialog
    BOOL isDir;
    CQuadWord size;
    CQuadWord totalSize(0, 0);
    CQuadWord fileCount(0, 0);
    const wchar_t* name;
    const CFileData* fileData;
    int errorOccured;
    while ((name = next(SalamanderGeneral->GetMsgBoxParent(), 1, &isDir, &size, &fileData, nextParam, &errorOccured)) != NULL)
    {
        if (!isDir)
        {
            totalSize += size;
            ++fileCount;
        } // if

        totalSize += CQuadWord(1, 0);
    }

    // unpack
    BOOL delTempDir = TRUE;
    if (errorOccured != SALENUM_CANCEL && // test to see whether an error occurred and the user did not request to cancel the operation (Cancel button)
        SalamanderGeneral->TestFreeSpace(SalamanderGeneral->GetMsgBoxParent(),
                                         targetDir, totalSize, LangStr(IDS_UNPACKING_ARCHIVE).c_str()))
    {
        salamander->OpenProgressDialog(LangStr(IDS_UNPACKING_ARCHIVE).c_str(), TRUE, NULL, FALSE);
        salamander->ProgressSetTotalSize(CQuadWord(0, 0), totalSize);

        DWORD silent = 0;
        BOOL toSkip = FALSE, bAudioEncountered = FALSE;

        const std::wstring currentISOPath(archiveRoot);

        ret = TRUE;
        next(NULL, -1, NULL, NULL, NULL, nextParam, NULL);
        while ((name = next(NULL /* we do not print the errors a second time */, 1, &isDir, &size, &fileData, nextParam, NULL)) != NULL)
        {
            // directories do not interest us; they are created while unpacking files
            std::wstring destPath(targetDir);
            SPLSalPathAppendOwned(destPath, name);
            if (isDir)
            {
                if (isoImage.UnpackDir(destPath.c_str(), fileData) == UNPACK_CANCEL ||
                    !salamander->ProgressAddSize(1, TRUE))
                {
                    ret = FALSE;
                    break;
                }
            }
            else
            {
                salamander->ProgressDialogAddText(name, TRUE); // delayedPaint==TRUE, so we do not slow things down

                //  if the destination path does not exist -> create it
                const size_t lastComp = destPath.find_last_of(L'\\');
                if (lastComp != std::wstring::npos)
                {
                    destPath.resize(lastComp);
                    SalamanderGeneral->CheckAndCreateDirectory(destPath.c_str());
                }

                salamander->ProgressSetSize(CQuadWord(0, 0), CQuadWord(-1, -1), TRUE);
                salamander->ProgressSetTotalSize(fileData->Size + CQuadWord(1, 0), CQuadWord(-1, -1));

                int err = isoImage.UnpackFile(salamander, currentISOPath, destPath, fileData, silent, toSkip);

                if ((UNPACK_AUDIO_UNSUP == err) && !bAudioEncountered)
                {
                    bAudioEncountered = TRUE;
                    Error(IDS_AUDIO_NOT_EXTRACTABLE);
                }

                if (err == UNPACK_CANCEL || !salamander->ProgressAddSize(1, TRUE)) // correction for zero-sized files
                {
                    ret = FALSE;
                    break;
                }
            }
        } // while

        salamander->CloseProgressDialog();
    }

    pd->DisplayMissingCCDWarning = isoImage.DisplayMissingCCDWarning;

    return ret;
}

BOOL CPluginInterfaceForArchiver::UnpackOneFile(CSalamanderForOperationsAbstract* salamander,
                                                const wchar_t* fileName, CPluginDataInterfaceAbstract* pluginData,
                                                const wchar_t* nameInArchive, const CFileData* fileData,
                                                const wchar_t* targetDir, const wchar_t* newFileName,
                                                BOOL* renamingNotSupported)
{
    CALL_STACK_MESSAGE4("CPluginInterfaceForArchiver::UnpackOneFile(, %ls, , %ls, , %ls, ,)", fileName,
                        nameInArchive, targetDir);

    if (newFileName != NULL)
    {
        *renamingNotSupported = TRUE;
        return FALSE;
    }

    if (pluginData == NULL)
    {
        TRACE_E("Internal error");
        return FALSE;
    }

    CPluginDataInterface* pd = (CPluginDataInterface*)pluginData;

    // try to open the ISO image
    CISOImage isoImage; // the destructor calls Close
    isoImage.DisplayMissingCCDWarning = pd->DisplayMissingCCDWarning;
    if (!isoImage.Open(fileName, FALSE))
        return FALSE;

    BOOL ret = TRUE;

    salamander->OpenProgressDialog(LangStr(IDS_UNPACKING_ARCHIVE).c_str(), FALSE, NULL, FALSE);
    salamander->ProgressSetTotalSize(fileData->Size + CQuadWord(1, 0), CQuadWord(-1, -1));

    std::wstring name(targetDir);
    const wchar_t* lastComp = wcsrchr(nameInArchive, L'\\');
    if (lastComp != NULL)
        lastComp++;
    else
        lastComp = nameInArchive;

    SPLSalPathAppendOwned(name, lastComp);
    DWORD silent = 0;
    BOOL toSkip = FALSE;
    int err;

    salamander->ProgressDialogAddText(name.c_str(), TRUE); // delayedPaint==TRUE, so we do not slow things down

    std::wstring srcPath(nameInArchive);
    const size_t sourceName = srcPath.find_last_of(L'\\');
    if (sourceName != std::wstring::npos)
        srcPath.resize(sourceName);
    else
        srcPath.clear();

    err = isoImage.UnpackFile(salamander, srcPath, std::wstring(targetDir), fileData, silent, toSkip);
    if (UNPACK_AUDIO_UNSUP == err)
        Error(IDS_AUDIO_NOT_EXTRACTABLE);
    ret = err == UNPACK_OK;

    salamander->CloseProgressDialog();

    pd->DisplayMissingCCDWarning = isoImage.DisplayMissingCCDWarning;

    return ret;
}

void CalcSize(CSalamanderDirectoryAbstract const* dir, const wchar_t* mask, CQuadWord& size, CQuadWord& fileCount)
{
    int count = dir->GetFilesCount();
    int i;
    for (i = 0; i < count; i++)
    {
        CFileData const* file = dir->GetFile(i);
        //    TRACE_I("CalcSize(): file: " << path << (path[0] != 0 ? "\\" : "") << file->Name);

        if (SalamanderGeneral->AgreeMask(file->Name, mask, file->Ext[0] != 0))
        {
            size += file->Size + CQuadWord(1, 0);
            ++fileCount;
        }
    }

    count = dir->GetDirsCount();
    int j;
    for (j = 0; j < count; j++)
    {
        CFileData const* file = dir->GetDir(j);
        //    TRACE_I("CalcSize(): directory: " << path << (path[0] != 0 ? "\\" : "") << file->Name);
        CSalamanderDirectoryAbstract const* subDir = dir->GetSalDir(j);
        CalcSize(subDir, mask, size, fileCount);
    }
}

BOOL CPluginInterfaceForArchiver::UnpackWholeArchive(CSalamanderForOperationsAbstract* salamander, const wchar_t* fileName,
                                                     const wchar_t* mask, const wchar_t* targetDir, BOOL delArchiveWhenDone,
                                                     CDynamicString* archiveVolumes)
{
    CALL_STACK_MESSAGE5("CPluginInterfaceForArchiver::UnpackWholeArchive(, %ls, %ls, %ls, %d,)",
                        fileName, mask, targetDir, delArchiveWhenDone);

    CSalamanderDirectoryAbstract* dir = SalamanderGeneral->AllocSalamanderDirectory(FALSE);
    if (dir == NULL)
        return Error(IDS_INSUFFICIENT_MEMORY);

    BOOL ret = FALSE;
    CPluginDataInterfaceAbstract* pluginData = NULL;
    if (ListArchive(salamander, fileName, dir, pluginData))
    {
        CQuadWord totalSize(0, 0);
        CQuadWord fileCount(0, 0);
        CalcSize(dir, mask, totalSize, fileCount);

        BOOL delTempDir = TRUE;
        if (SalamanderGeneral->TestFreeSpace(SalamanderGeneral->GetMsgBoxParent(),
                                             targetDir, totalSize, LangStr(IDS_UNPACKING_ARCHIVE).c_str()))
        {
            salamander->OpenProgressDialog(LangStr(IDS_UNPACKING_ARCHIVE).c_str(), TRUE, NULL, FALSE);
            salamander->ProgressSetTotalSize(CQuadWord(0, 0), totalSize);

            const std::wstring modmask =
                SPLPrepareMaskOwned(SalamanderGeneral, mask);

            // try to open the ISO image
            CISOImage isoImage; // the destructor calls Close
            if (isoImage.Open(fileName, FALSE))
            {
                if (delArchiveWhenDone)
                    archiveVolumes->Add(fileName, -2);

                DWORD silent = 0;
                BOOL toSkip = FALSE;
                ret = isoImage.ExtractAllItems(salamander, std::wstring(), dir, modmask.c_str(), std::wstring(targetDir), silent, toSkip) != UNPACK_CANCEL;
            }

            salamander->CloseProgressDialog();
        }

        if (pluginData != NULL)
        {
            dir->Clear(pluginData);
            PluginInterface.ReleasePluginDataInterface(pluginData);
        }

        int panel = -1;
        CanCloseArchive(salamander, fileName, TRUE, panel);
    }

    SalamanderGeneral->FreeSalamanderDirectory(dir);

    return ret;
}

BOOL CPluginInterfaceForArchiver::CanCloseArchive(CSalamanderForOperationsAbstract* salamander,
                                                  const wchar_t* fileName,
                                                  BOOL force, int panel)
{
    CALL_STACK_MESSAGE4("CPluginInterfaceForArchiver::CanCloseArchive(, %ls, %d, %d)", fileName, force, panel);

    return TRUE;
}

//
// ****************************************************************************
// CPluginInterfaceForViewer
//

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

    // we do not set 'lock' or 'lockOwner'; we only need the validity of the file 'name'
    // within this method

    HCURSOR hOldCur = SetCursor(LoadCursor(NULL, IDC_WAIT));

    CISOImage* image;
    if ((image = new CISOImage()) == NULL)
    {
        SetCursor(hOldCur);
        return Error(IDS_INSUFFICIENT_MEMORY);
    }

    if (!image->Open(name))
    {
        delete image;
        SetCursor(hOldCur);
        return FALSE;
    }

    /*
  // open the last track
  int lastTrack = image->GetLastTrack();
  if (!image->OpenTrack(lastTrack)) {
    Error(IDS_CANT_OPEN_TRACK, FALSE, lastTrack);
    delete image;
    return FALSE;
  }
*/

    std::wstring tempFileName;
    if (SPLSalGetTempFileNameOwned(SalamanderGeneral, NULL, L"ISO", tempFileName, TRUE, NULL))
    {
        int err;
        CSalamanderPluginInternalViewerData vData;

        // create a temporary file and pour the module dump into it
        FILE* outStream = NULL;
        _wfopen_s(&outStream, tempFileName.c_str(), L"w");
        if (outStream == NULL)
        {
            DeleteFileW(tempFileName.c_str());
            SetCursor(hOldCur);
            SalamanderGeneral->SalMessageBox(SalamanderGeneral->GetMsgBoxParent(), LangStr(IDS_ERR_TMP).c_str(),
                                             LangStr(IDS_PLUGINNAME).c_str(), MB_OK | MB_ICONEXCLAMATION);
            delete image;
            return FALSE;
        }
        const BOOL dumpSucceeded = image->DumpInfo(outStream);
        const int closeResult = fclose(outStream);
        delete image;
        if (!dumpSucceeded || closeResult != 0)
        {
            DeleteFileW(tempFileName.c_str());
            SetCursor(hOldCur);
            SalamanderGeneral->SalMessageBox(SalamanderGeneral->GetMsgBoxParent(), LangStr(IDS_ERR_TMP).c_str(),
                                             LangStr(IDS_PLUGINNAME).c_str(), MB_OK | MB_ICONEXCLAMATION);
            return FALSE;
        }

        // hand the file over to Salamander - it will move it into the cache and once it stops
        // using it, it will delete it
        vData.Size = sizeof(vData);
        vData.FileName = tempFileName.c_str();
        vData.Mode = 0; // text mode
        std::wstring caption = name;
        caption += L" - ";
        caption += LangStr(IDS_PLUGINNAME);
        vData.Caption = caption.c_str();
        vData.WholeCaption = TRUE;
        if (!SalamanderGeneral->ViewFileInPluginViewer(NULL, &vData, TRUE, NULL, L"iso_dump.txt", err))
        {
            // the file is deleted even in case of failure
        }
    }
    else
    {
        if (!tempFileName.empty())
            DeleteFileW(tempFileName.c_str());
        SetCursor(hOldCur);
        SalamanderGeneral->SalMessageBox(SalamanderGeneral->GetMsgBoxParent(), LangStr(IDS_ERR_TMP).c_str(),
                                         LangStr(IDS_PLUGINNAME).c_str(), MB_OK | MB_ICONEXCLAMATION);
        delete image;
    }

    SetCursor(hOldCur);
    return TRUE;
}

BOOL CPluginInterfaceForViewer::CanViewFile(const wchar_t* name)
{
    CALL_STACK_MESSAGE2("CPluginInterfaceForViewer::CanViewFile(%ls)", name);

    BOOL canView = FALSE;

    CISOImage* image;
    if ((image = new CISOImage()) != NULL)
    {
        if (image->Open(name, TRUE))
            canView = TRUE;

        delete image;
    }

    return canView;
}

// ****************************************************************************
//
// CPluginDataInterface
//

CPluginDataInterface::CPluginDataInterface()
{
    // initialy display the 'missing CCD file' warning
    DisplayMissingCCDWarning = TRUE;
}

CPluginDataInterface::~CPluginDataInterface()
{
}

void CPluginDataInterface::ReleasePluginData(CFileData& file, BOOL isDir)
{
    CALL_STACK_MESSAGE1("CPluginDataInterface::ReleasePluginData(, )");

    delete (CISOImage::CFilePos*)(file.PluginData);
    file.PluginData = NULL;
}
