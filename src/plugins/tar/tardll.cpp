// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"

#include "fileio.h"
#include "tardll.h"
#include "tar.h"
#include "gzip/gzip.h"
#include "rpm/rpm.h"

#include "tar.rh"
#include "tar.rh2"
#include "lang\lang.rh"
#include "../shared/plugin_local_path.h"

// TODO: resolve case sensitivity
// TODO: handle multiple files with the same name in one archive
// TODO: finish the output in the RPM viewer (convert dates to a readable format, etc.)
// TODO: clean up viewer deallocations; on error it behaves quite opaquely
// TODO: check tar against the specification to cover as many flags as possible (GNU extensions, etc.)
// TODO: review error messages and where they are used (TAR vs. CPIO)
// TODO: fix error reporting when the stream fails (do not rely on its ErrorNumber, ensure there is always text)
// TODO: handle multi-volume archives
// TODO: support archives with sparse files
// TODO: could links be turned into shortcuts?
// TODO: add LZMA compression to RPM (see flex-32bit-2.5.35-43.88.s390x.rpm sample). It seems rather rarely used, so maybe add it when more people ask...

//
// ****************************************************************************
//
// Declarations and definitions
//
// ****************************************************************************
//

// ConfigVersion: 0 - plugin installation or version released with Servant Salamander 2.0
//                    (without a configuration number)
//                1 - work-in-progress version before Servant Salamander 2.5 beta 1, added TBZ, BZ, BZ2, and RPM
//                2 - work-in-progress version before Servant Salamander 2.5 beta 1, added CPIO (including the *.CPIO viewer)
//                3 - work-in-progress version before Servant Salamander 2.5 beta 1, removed the *.CPIO viewer
//                4 - work-in-progress version before Servant Salamander 2.5 beta 1, added .z archives
//                5 - work-in-progress version before Servant Salamander 2.52 beta 2, added .DEB archives

int ConfigVersion = 0;
#define CURRENT_CONFIG_VERSION 5
const wchar_t* CONFIG_VERSION = L"Version";

// plugin interface object, its methods are called from Salamander
CPluginInterface PluginInterface;
// portion of CPluginInterface used for the archiver
CPluginInterfaceForArchiver InterfaceForArchiver;
// portion of CPluginInterface used for the viewer
CPluginInterfaceForViewer InterfaceForViewer;

// general Salamander interface - valid from plugin start until its termination
CSalamanderGeneralAbstract* SalamanderGeneral = NULL;

// interface for convenient work with files
CSalamanderSafeFileAbstract* SalamanderSafeFile = NULL;

// variable definition for "dbg.h"
CSalamanderDebugAbstract* SalamanderDebug = NULL;

HINSTANCE DLLInstance = NULL; // handle to the SPL - language-independent resources
HINSTANCE HLanguage = NULL;   // handle to the SLG - language-dependent resources

//
// ****************************************************************************
//
// Helper functions
//
// ****************************************************************************
//

// loads a string from the DLL
// SalamanderGeneral->LoadStr returns UTF-16 in the live v108 interface. Keep the
// resource text dynamically owned so callers never borrow mutable SDK scratch.
std::wstring LangStr(int resID)
{
    return SPLLoadStrOwned(SalamanderGeneral, HLanguage, resID);
}

// combines a resource string with an optional error string
const wchar_t* LoadErr(int resID, DWORD LastError)
{
    static std::wstring buffer;
    buffer = LangStr(resID).c_str();
    if (LastError != 0)
        buffer += SPLGetErrorTextOwned(SalamanderGeneral, LastError);
    return buffer.c_str();
}

//
// ****************************************************************************
//
// Plugin initialization
//
// ****************************************************************************
//

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved)
{
    if (fdwReason == DLL_PROCESS_ATTACH)
        DLLInstance = hinstDLL;
    return TRUE; // DLL can be loaded
}

//
// ****************************************************************************
//
// Functions for communication with Salamander
//
// ****************************************************************************
//

// ****************************************************************************
// SalamanderPluginGetReqVer - returns the required Salamander version
//
int WINAPI SalamanderPluginGetReqVer()
{
    return LAST_VERSION_OF_SALAMANDER;
}

// ****************************************************************************
// SalamanderPluginEntry - main entry point
//
CPluginInterfaceAbstract* WINAPI SalamanderPluginEntry(CSalamanderPluginEntryAbstract* salamander)
{
    // set SalamanderDebug for "dbg.h"
    SalamanderDebug = salamander->GetSalamanderDebug();

    CALL_STACK_MESSAGE1("SalamanderPluginEntry()");

    // this plugin is built for the current Salamander version and newer - perform a check
    if (salamander->GetVersion() < LAST_VERSION_OF_SALAMANDER)
    { // reject older versions
        // wide: same call-site-local widen shape used throughout this backlog
        // (205-219).
        MessageBoxW(salamander->GetParentWindow(),
                    _CRT_WIDE(REQUIRE_LAST_VERSION_OF_SALAMANDER),
                    L"TAR" /* do not translate! */, MB_OK | MB_ICONERROR);
        return NULL;
    }

    // let Salamander load the language module (.slg)
    HLanguage = salamander->LoadLanguageModule(salamander->GetParentWindow(), L"TAR" /* do not translate! */);
    if (HLanguage == NULL)
        return NULL;

    // obtain Salamander's general interface
    SalamanderGeneral = salamander->GetSalamanderGeneral();
    SalamanderSafeFile = salamander->GetSalamanderSafeFile();

    // log a diagnostic message
    TRACE_I("SalamanderPluginEntry called, Salamander version " << salamander->GetVersion());

    // set basic plugin information
    salamander->SetBasicPluginData(LangStr(IDS_PLUGINNAME).c_str(),
                                   FUNCTION_LOADSAVECONFIGURATION |
                                       FUNCTION_PANELARCHIVERVIEW | FUNCTION_CUSTOMARCHIVERUNPACK |
                                       FUNCTION_VIEWER,
                                   _CRT_WIDE(VERSINFO_VERSION_NO_PLATFORM),
                                   _CRT_WIDE(VERSINFO_COPYRIGHT),
                                   LangStr(IDS_PLUGIN_DESCRIPTION).c_str(),
                                   L"TAR" /* do not translate! */, L"tar;tgz;taz;tbz;gz;bz;bz2;z;rpm;cpio;deb");

    salamander->SetPluginHomePageURL(L"https://github.com/0xeb/sally");

    return &PluginInterface;
}

void CPluginInterface::About(HWND parent)
{
    const std::wstring text = LangStr(IDS_PLUGINNAME) + L" " _CRT_WIDE(VERSINFO_VERSION) L"\n\n" _CRT_WIDE(VERSINFO_COPYRIGHT)
                                                        L"\nbzip2 library Copyright © 1996-2010 Julian R Seward\n\n" +
                              LangStr(IDS_PLUGIN_DESCRIPTION);
    SalamanderGeneral->SalMessageBox(parent, text.c_str(), LangStr(IDS_ABOUT).c_str(), MB_OK | MB_ICONINFORMATION);
}

void CPluginInterface::LoadConfiguration(HWND parent, HKEY regKey, CSalamanderRegistryAbstract* registry)
{
    CALL_STACK_MESSAGE1("CPluginInterface::LoadConfiguration(, ,)");
    if (regKey != NULL) // load from the registry
    {
        if (!registry->GetValue(regKey, CONFIG_VERSION, REG_DWORD, &ConfigVersion, sizeof(DWORD)))
        {
            ConfigVersion = 0; // error - use the version without loading
        }
    }
    else // default configuration
    {
        ConfigVersion = 0; // without loading
    }
}

void CPluginInterface::SaveConfiguration(HWND parent, HKEY regKey, CSalamanderRegistryAbstract* registry)
{
    CALL_STACK_MESSAGE1("CPluginInterface::SaveConfiguration(, ,)");

    DWORD v = CURRENT_CONFIG_VERSION;
    registry->SetValue(regKey, CONFIG_VERSION, REG_DWORD, &v, sizeof(DWORD));
}

void CPluginInterface::Connect(HWND parent, CSalamanderConnectAbstract* salamander)
{
    CALL_STACK_MESSAGE1("CPluginInterface::Connect()");

    // base part:
    salamander->AddCustomUnpacker(L"TAR (Plugin)",
                                  L"*.tar;*.tgz;*.tbz;*.taz;"
                                  L"*.tar.gz;*.tar.bz;*.tar.bz2;*.tar.z;"
                                  L"*_tar.gz;*_tar.bz;*_tar.bz2;*_tar.z;"
                                  L"*_tar_gz;*_tar_bz;*_tar_bz2;*_tar_z;"
                                  L"*.tar_gz;*.tar_bz;*.tar_bz2;*.tar_z;"
                                  L"*.gz;*.bz;*.bz2;*.z;"
                                  L"*.rpm;*.cpio;*.deb",
                                  ConfigVersion < 5);                                        // ignored during upgrades except when upgrading to version 4 - required update because of "*.z" and others
    salamander->AddPanelArchiver(L"tgz;tbz;taz;tar;gz;bz;bz2;z;rpm;cpio;deb", FALSE, FALSE); // ignored when upgrading the plugin
    salamander->AddViewer(L"*.rpm", FALSE);                                                  // ignored when upgrading the plugin except when upgrading from a version without the viewer (the version shipped with SS 2.0)

    // section for upgrades:
    if (ConfigVersion < 1) // 1 - work-in-progress version before Servant Salamander 2.5 beta 1, added tbz, bz, bz2, and rpm
    {
        salamander->AddPanelArchiver(L"tbz;bz;bz2;rpm", FALSE, TRUE);
    }

    if (ConfigVersion < 2) // 2 - work-in-progress version before Servant Salamander 2.5 beta 1, added cpio (including the *.cpio viewer - that was a mistake)
    {
        salamander->AddPanelArchiver(L"cpio", FALSE, TRUE);

        // adding *.cpio was a mistake, version 3 removes it again
        //salamander->AddViewer("*.cpio", TRUE);
    }

    /*
  if (ConfigVersion < 3)    // 3 - work-in-progress version before Servant Salamander 2.5 beta 1, removed the *.cpio viewer
  {
    salamander->ForceRemoveViewer("*.cpio");   // comment out once we consider it dead code
  }
*/

    if (ConfigVersion < 4) // 4 - work-in-progress version before Servant Salamander 2.5 beta 1, added .z archives
    {
        salamander->AddPanelArchiver(L"taz;z", FALSE, TRUE);
    }
    if (ConfigVersion < 5) // 5 - work-in-progress version before Servant Salamander 2.52 beta 2, added .deb archives
    {
        salamander->AddPanelArchiver(L"deb", FALSE, TRUE);
    }
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

//
// ****************************************************************************
//
// Operational functions provided by the plugin
//
// ****************************************************************************
//

BOOL CPluginInterfaceForArchiver::ListArchive(CSalamanderForOperationsAbstract* salamander,
                                              const wchar_t* fileName,
                                              CSalamanderDirectoryAbstract* dir,
                                              CPluginDataInterfaceAbstract*& pluginData)
{
    CALL_STACK_MESSAGE2("CPluginInterfaceForArchiver::ListArchive(, %ls, ,)", fileName);
    pluginData = NULL;

    // create the archive object
    CArchiveAbstract* archive = CreateArchive(fileName, salamander);
    // and list it
    if (archive)
    {
        BOOL ret = archive->ListArchive(NULL, dir);
        delete archive;
        return ret;
    }
    return FALSE;
}

BOOL CPluginInterfaceForArchiver::UnpackArchive(CSalamanderForOperationsAbstract* salamander,
                                                const wchar_t* fileName, CPluginDataInterfaceAbstract* pluginData,
                                                const wchar_t* targetDir, const wchar_t* archiveRoot,
                                                SalEnumSelection next, void* nextParam)
{
    CALL_STACK_MESSAGE4("CPluginInterfaceForArchiver::UnpackArchive(, %ls, , %ls, %ls,,,)",
                        fileName, targetDir, archiveRoot);

    // create the archive object
    CArchiveAbstract* archive = CreateArchive(fileName, salamander);
    // and extract it
    if (archive)
    {
        BOOL ret = archive->UnpackArchive(targetDir, archiveRoot, next, nextParam);
        delete archive;
        return ret;
    }
    return FALSE;
}

BOOL CPluginInterfaceForArchiver::UnpackOneFile(CSalamanderForOperationsAbstract* salamander,
                                                const wchar_t* fileName, CPluginDataInterfaceAbstract* pluginData,
                                                const wchar_t* nameInArchive, const CFileData* fileData,
                                                const wchar_t* targetDir, const wchar_t* newFileName,
                                                BOOL* renamingNotSupported)
{
    CALL_STACK_MESSAGE4("CPluginInterfaceForArchiver::UnpackOneFile(, %ls, , %ls, , %ls, ,)",
                        fileName, nameInArchive, targetDir);

    // create the archive object
    CArchiveAbstract* archive = CreateArchive(fileName, salamander);
    // and extract it
    if (archive)
    {
        BOOL ret = archive->UnpackOneFile(nameInArchive, fileData, targetDir, newFileName);
        delete archive;
        return ret;
    }
    return FALSE;
}

BOOL CPluginInterfaceForArchiver::UnpackWholeArchive(CSalamanderForOperationsAbstract* salamander,
                                                     const wchar_t* fileName, const wchar_t* mask,
                                                     const wchar_t* targetDir, BOOL delArchiveWhenDone,
                                                     CDynamicString* archiveVolumes)
{
    CALL_STACK_MESSAGE5("CPluginInterfaceForArchiver::UnpackWholeArchive(, %ls, %ls, %ls, %d,)",
                        fileName, mask, targetDir, delArchiveWhenDone);

    // create the archive object
    CArchiveAbstract* archive = CreateArchive(fileName, salamander);
    // and extract it
    if (archive)
    {
        if (delArchiveWhenDone)
            archiveVolumes->Add(fileName, -2); // FIXME: once the plugin learns multi-volume archives, add all volumes here (so the entire archive gets deleted)
        BOOL ret = archive->UnpackWholeArchive(mask, targetDir);
        delete archive;
        return ret;
    }
    return FALSE;
}

/*
BOOL
CPluginInterfaceForArchiver::PackToArchive(CSalamanderForOperationsAbstract *salamander,
                                           const char *fileName, const char *archiveRoot,
                                           BOOL move, const char *sourcePath,
                                           SalEnumSelection2 next, void *nextParam)
{
  CALL_STACK_MESSAGE5("CPluginInterfaceForArchiver::PackToArchive(, %s, %s, %d, %s,,)",
                      fileName, archiveRoot, move, sourcePath);
  return FALSE;
}

BOOL
CPluginInterfaceForArchiver::DeleteFromArchive(CSalamanderForOperationsAbstract *salamander,
                                               const char *fileName, CPluginDataInterfaceAbstract *pluginData,
                                               const char *archiveRoot, SalEnumSelection next,
                                               void *nextParam)
{
  CALL_STACK_MESSAGE3("CPluginInterfaceForArchiver::DeleteFromArchive(, %s, , %s, ,)",
                      fileName, archiveRoot);
  return FALSE;
}
*/

// function for the "file viewer"; called when the viewer should open and load a file
// 'name', 'left'+'right'+'width'+'height'+'showCmd'+'alwaysOnTop' is the recommended window placement
// if 'returnUnlock' is FALSE, 'unlock'+'unlockOwner' have no meaning
// if 'returnUnlock' is TRUE, the viewer should return the system event 'unlock'
// in the non-signaled state; it transitions to the signaled state when viewing
// the file 'name' finishes (the file is removed from the temporary directory at that time). It should also
// return TRUE in 'unlockOwner' if the caller should close the 'unlock' object (FALSE means
// the viewer closes 'unlock' itself); if the viewer does not set 'unlock' (stays NULL)
// the file 'name' is valid only until this ViewFile call ends. If
// 'viewerData' is not NULL, it passes extended parameters to the viewer (see
// CSalamanderGeneralAbstract::ViewFileInPluginViewer). Returns TRUE on success
// (FALSE indicates failure; 'unlock' and 'unlockOwner' have no meaning in that case)
BOOL CPluginInterfaceForViewer::ViewFile(const wchar_t* name, int left, int top, int width, int height,
                                         UINT showCmd, BOOL alwaysOnTop, BOOL returnUnlock, HANDLE* unlock,
                                         BOOL* unlockOwner, CSalamanderPluginViewerData* viewerData,
                                         int enumFilesSourceUID, int enumFilesCurrentIndex)
{
    CALL_STACK_MESSAGE11("CPluginInterfaceForViewer::ViewFile(%ls, %d, %d, %d, %d, "
                         "0x%X, %d, %d, , , , %d, %d)",
                         name, left, top, width, height,
                         showCmd, alwaysOnTop, returnUnlock, enumFilesSourceUID, enumFilesCurrentIndex);

    // unlock stays NULL, we do not need locking
    //   therefore we also ignore returnUnlock and unlockOwner
    // viewerData is not used

    // store the original cursor and show the hourglass (even though it should not take long...)
    HCURSOR hOldCur = SetCursor(LoadCursor(NULL, IDC_WAIT));

    // open the input file
    std::wstring ioPath;
    HANDLE file = PreparePluginLocalPathForIo(name, ioPath)
                      ? CreateFileW(ioPath.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                                    FILE_FLAG_SEQUENTIAL_SCAN, NULL)
                      : INVALID_HANDLE_VALUE;
    if (file == INVALID_HANDLE_VALUE)
    {
        int err = GetLastError();
        SetCursor(hOldCur);
        SalamanderGeneral->ShowMessageBox(LoadErr(IDS_GZERR_FOPEN, err), LangStr(IDS_GZERR_TITLE).c_str(), MSGBOX_ERROR);
        return FALSE;
    }
    // allocate a buffer for reading the file
    unsigned char* buffer = (unsigned char*)malloc(BUFSIZE);
    if (buffer == NULL)
    {
        SetCursor(hOldCur);
        SalamanderGeneral->ShowMessageBox(LangStr(IDS_ERR_MEMORY).c_str(), LangStr(IDS_GZERR_TITLE).c_str(),
                                          MSGBOX_ERROR);
        CloseHandle(file);
        return FALSE;
    }
    // read the first block of data
    DWORD read;
    if (!ReadFile(file, buffer, BUFSIZE, &read, NULL))
    {
        // read error
        int err = GetLastError();
        SetCursor(hOldCur);
        free(buffer);
        CloseHandle(file);
        SalamanderGeneral->ShowMessageBox(LoadErr(IDS_ERR_FREAD, err), LangStr(IDS_GZERR_TITLE).c_str(), MSGBOX_ERROR);
        return FALSE;
    }
    // obtain a name for the temporary text file with the results
    std::wstring tempFileName;
    if (!SPLSalGetTempFileNameOwned(SalamanderGeneral, NULL, L"RPV",
                                    tempFileName, TRUE, NULL))
    {
        SetCursor(hOldCur);
        free(buffer);
        CloseHandle(file);
        SalamanderGeneral->ShowMessageBox(LangStr(IDS_RPMERR_TMPNAME).c_str(), LangStr(IDS_ERR_RPMTITLE).c_str(), MSGBOX_ERROR);
        return FALSE;
    }

    // create the temporary file
    FILE* fContents = NULL;
    _wfopen_s(&fContents, tempFileName.c_str(), L"wt");
    if (fContents == NULL)
    {
        SetCursor(hOldCur);
        free(buffer);
        CloseHandle(file);
        // Built WIDE. The old form composed into char[500] and narrowed
        // the archive name through ACP purely to widen the result again for the
        // message box - so a non-ANSI archive name was mangled in a path that never
        // needed a narrow form at all.
        std::wstring msg(LangStr(IDS_RPMERR_TMPFILE).c_str());
        msg += name;
        SalamanderGeneral->ShowMessageBox(msg.c_str(), LangStr(IDS_ERR_RPMTITLE).c_str(), MSGBOX_ERROR);
        return FALSE;
    }
    // create the RPM object and fill the temporary file with information
    CDecompressFile* archive = new CRPM(name, file, buffer, read, fContents);
    if (archive == NULL)
    {
        SetCursor(hOldCur);
        fclose(fContents);
        DeleteFileW(tempFileName.c_str());
        free(buffer);
        CloseHandle(file);
        SalamanderGeneral->ShowMessageBox(LangStr(IDS_ERR_MEMORY).c_str(), LangStr(IDS_ERR_RPMTITLE).c_str(), MSGBOX_ERROR);
        return FALSE;
    }
    // TODO: probably make sure errors are not reported inside and only a flag is set according to the error
    if (!archive->IsOk())
    {
        SetCursor(hOldCur);
        fclose(fContents);
        DeleteFileW(tempFileName.c_str());
        free(buffer);
        CloseHandle(file);
        if (archive->GetErrorCode() == 0)
            SalamanderGeneral->ShowMessageBox(LangStr(IDS_ERR_NORPM).c_str(), LangStr(IDS_ERR_RPMTITLE).c_str(), MSGBOX_ERROR);
        else
            SalamanderGeneral->ShowMessageBox(LangStr(archive->GetErrorCode()).c_str(), LangStr(IDS_ERR_RPMTITLE).c_str(), MSGBOX_ERROR);
        delete archive;
        return FALSE;
    }
    // now we can close the archive again...
    // the input file and buffer are cleaned up in the CDecompressFile destructor
    delete archive;
    // done
    fclose(fContents);

    // prepare the structure for Salamander's text viewer
    CSalamanderPluginInternalViewerData textViewerData;
    textViewerData.Size = sizeof(textViewerData);
    textViewerData.FileName = tempFileName.c_str();
    textViewerData.Mode = 0; // text mode
    std::wstring caption = name;
    caption += L" - ";
    caption += LangStr(IDS_RPM_VIEWTITLE);
    textViewerData.Caption = caption.c_str();
    textViewerData.WholeCaption = TRUE;
    // show the file in Salamander's text viewer and delete it afterwards
    int err;
    SalamanderGeneral->ViewFileInPluginViewer(NULL, &textViewerData, TRUE, NULL, L"rpm_dump.txt", err);

    // and finally clean up
    SetCursor(hOldCur);
    return TRUE;
}
