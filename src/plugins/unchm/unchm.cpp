// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"
#include "dbg.h"

#include "chmlib/types.h"
#include "unchm.h"
#include "chmfile.h"

#include "unchm.rh"
#include "unchm.rh2"
#include "lang\lang.rh"

// ****************************************************************************

HINSTANCE DLLInstance = NULL; // handle to the SPL - language-independent resources
HINSTANCE HLanguage = NULL;   // handle to the SLG - language-dependent resources

// plugin interface object; its methods are called by Salamander
CPluginInterface PluginInterface;
// archiver-specific part of CPluginInterface
CPluginInterfaceForArchiver InterfaceForArchiver;

// general Salamander interface - valid from startup until the plugin shuts down
CSalamanderGeneralAbstract* SalamanderGeneral = NULL;

// interface for convenient work with files
CSalamanderSafeFileAbstract* SalamanderSafeFile = NULL;

// variable definition for "dbg.h"
CSalamanderDebugAbstract* SalamanderDebug = NULL;

// variable definition for "spl_com.h"
int SalamanderVersion = 0;

// configuration
COptions Options;

//const SYSTEMTIME  MinTime = { 1980, 01, 2, 01, 00, 00, 00, 000};

//const char *CONFIG_OPTIONS = "Options";
//const char *CONFIG_CLEAR_READONLY = "Clear Read Only";
//const char *CONFIG_SESSION_AS_DIR = "Show Session As Directory";
//const char *CONFIG_BOOTIMAGE_AS_FILE = "Show Boot Image As File";

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

// Wide. SalamanderGeneral->LoadStr has returned WCHAR* since the v108
// ABI break; this went through LoadStrNarrow and was widened again at every call site.
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

    CALL_STACK_MESSAGE1("SalamanderPluginEntry()");

    // this plugin is built for the current Salamander version and newer - perform a check
    if (SalamanderVersion < LAST_VERSION_OF_SALAMANDER)
    { // reject older versions
        // wide: same call-site-local widen shape used throughout this backlog
        // (205-221).
        MessageBoxW(salamander->GetParentWindow(),
                    _CRT_WIDE(REQUIRE_LAST_VERSION_OF_SALAMANDER),
                    L"UnCHM" /* do not translate! */, MB_OK | MB_ICONERROR);
        return NULL;
    }

    // let it load the language module (.slg)
    HLanguage = salamander->LoadLanguageModule(salamander->GetParentWindow(), L"UnCHM" /* do not translate! */);
    if (HLanguage == NULL)
        return NULL;

    // obtain the general Salamander interface
    SalamanderGeneral = salamander->GetSalamanderGeneral();
    SalamanderSafeFile = salamander->GetSalamanderSafeFile();

    if (!InterfaceForArchiver.Init())
        return NULL;

    if (!InitializeWinLib(L"UnCHM" /* do not translate! */, DLLInstance))
        return NULL;
    SetWinLibStrings(LangStr(IDS_INVALIDNUMBER).c_str(), LangStr(IDS_PLUGINNAME).c_str());

    // set the basic information about the plugin
    salamander->SetBasicPluginData(SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_PLUGINNAME).c_str(),
                                   FUNCTION_PANELARCHIVERVIEW | FUNCTION_CUSTOMARCHIVERUNPACK,
                                   _CRT_WIDE(VERSINFO_VERSION_NO_PLATFORM),
                                   _CRT_WIDE(VERSINFO_COPYRIGHT),
                                   SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_PLUGIN_DESCRIPTION).c_str(),
                                   L"UnCHM" /* do not translate! */, L"chm");

    salamander->SetPluginHomePageURL(L"https://github.com/0xeb/sally");

    return &PluginInterface;
}

BOOL Warning(int resID, BOOL quiet, ...)
{
    if (!quiet)
    {
        va_list arglist;
        va_start(arglist, quiet);
        const std::wstring message = SPLFormatStringOwnedV(
            SPLLoadStrOwned(SalamanderGeneral, HLanguage, resID).c_str(), arglist);
        va_end(arglist);

        SalamanderGeneral->ShowMessageBox(message.c_str(), SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_PLUGINNAME).c_str(), MSGBOX_WARNING);
    }
    return FALSE;
}

BOOL Error(int resID, BOOL quiet, ...)
{
    if (!quiet)
    {
        va_list arglist;
        va_start(arglist, quiet);
        const std::wstring message = SPLFormatStringOwnedV(
            SPLLoadStrOwned(SalamanderGeneral, HLanguage, resID).c_str(), arglist);
        va_end(arglist);

        SalamanderGeneral->ShowMessageBox(message.c_str(), SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_PLUGINNAME).c_str(), MSGBOX_ERROR);
    }
    return FALSE;
}

BOOL Error(const wchar_t* msg, DWORD err, BOOL quiet)
{
    if (!quiet)
        if (err != ERROR_FILE_NOT_FOUND && err != ERROR_NO_MORE_FILES) // this is an error
        {
            const std::wstring message = SPLFormatStringOwned(
                L"%ls\n\n%ls", msg, SPLGetErrorTextOwned(SalamanderGeneral, err).c_str());
            SalamanderGeneral->ShowMessageBox(message.c_str(), SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_PLUGINNAME).c_str(), MSGBOX_ERROR);
        }

    return FALSE;
}

BOOL SysError(int title, int error, ...)
{
    int lastErr = GetLastError();
    CALL_STACK_MESSAGE3("SysError(%d, %d, ...)", title, error);
    va_list arglist;
    va_start(arglist, error);
    std::wstring message = SPLFormatStringOwnedV(
        SPLLoadStrOwned(SalamanderGeneral, HLanguage, error).c_str(), arglist);
    va_end(arglist);
    if (lastErr != ERROR_SUCCESS)
    {
        message.push_back(L' ');
        message.append(SPLGetErrorTextOwned(SalamanderGeneral, lastErr));
    }
    SalamanderGeneral->ShowMessageBox(message.c_str(), SPLLoadStrOwned(SalamanderGeneral, HLanguage, title).c_str(), MSGBOX_ERROR);
    return FALSE;
}

// ****************************************************************************
//
// CPluginInterface
//

void CPluginInterface::About(HWND parent)
{
    const std::wstring text = SPLFormatStringOwned(
        L"%ls %ls\n\n%ls\n%ls\n\n%ls",
        SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_PLUGINNAME).c_str(),
        _CRT_WIDE(VERSINFO_VERSION), _CRT_WIDE(VERSINFO_COPYRIGHT),
        SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_CHMLIBCOPYRIGHT).c_str(),
        SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_PLUGIN_DESCRIPTION).c_str());
    SalamanderGeneral->SalMessageBox(parent, text.c_str(),
                                     SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_ABOUT).c_str(),
                                     MB_OK | MB_ICONINFORMATION);
}

BOOL CPluginInterface::Release(HWND parent, BOOL force)
{
    CALL_STACK_MESSAGE2("CPluginInterface::Release(, %d)", force);

    ReleaseWinLib(DLLInstance);

    return TRUE;
}

void CPluginInterface::Connect(HWND parent, CSalamanderConnectAbstract* salamander)
{
    CALL_STACK_MESSAGE1("CPluginInterface::Connect(,)");

    salamander->AddCustomUnpacker(L"UnCHM (Plugin)", L"*.chm", FALSE);
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

    return ListArchiveWide(salamander, fileName, dir, pluginData);
}

BOOL CPluginInterfaceForArchiver::ListArchiveWide(CSalamanderForOperationsAbstract* salamander,
                                                   const wchar_t* fileName,
                                                   CSalamanderDirectoryAbstract* dir,
                                                   CPluginDataInterfaceAbstract*& pluginData)
{

    pluginData = new CPluginDataInterface();
    if (pluginData == NULL)
    {
        return Error(IDS_INSUFFICIENT_MEMORY);
    }

    BOOL ret = FALSE;
    HWND hParent = SalamanderGeneral->GetMsgBoxParent();
    CCHMFile chm; // destructor calls Close
    if (chm.Open(fileName, FALSE))
    {
        // pass the complete listing to Salamander's core
        if (chm.EnumObjects(dir, pluginData))
            ret = TRUE;
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

    // try to open the CHM image
    CCHMFile chm; // destructor calls Close
    if (!chm.Open(fileName, FALSE))
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
    // check whether no error occurred and the user did not request to cancel the operation (Cancel button)
    if (errorOccured == SALENUM_CANCEL)
        return FALSE;

    chm.ButtonFlags = BUTTONS_SKIPCANCEL;
    // unpack
    BOOL delTempDir = TRUE;
    const std::wstring unpackingTitle =
        SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_UNPACKING_ARCHIVE);
    if (SalamanderGeneral->TestFreeSpace(SalamanderGeneral->GetMsgBoxParent(),
                                         targetDir, totalSize, unpackingTitle.c_str()))
    {
        salamander->OpenProgressDialog(unpackingTitle.c_str(), TRUE, NULL, FALSE);
        salamander->ProgressSetTotalSize(CQuadWord(0, 0), totalSize);

        DWORD silent = 0;
        BOOL toSkip = FALSE;

        std::wstring currentPath(archiveRoot);

        ret = TRUE;
        next(NULL, -1, NULL, NULL, NULL, nextParam, NULL);
        while ((name = next(NULL /* do not report errors the second time */, 1, &isDir, &size, &fileData, nextParam, NULL)) != NULL)
        {
            // directories do not concern us; they are created when unpacking files
            std::wstring destPath(targetDir);

            SPLSalPathAppendOwned(destPath, name);
            {
                if (isDir)
                {
                    if (chm.UnpackDir(destPath.c_str(), fileData) == UNPACK_CANCEL ||
                        !salamander->ProgressAddSize(1, TRUE))
                    {
                        ret = FALSE;
                        break;
                    }
                }
                else
                {
                    salamander->ProgressDialogAddText(name, TRUE); // delayedPaint==TRUE so we do not slow down

                    //  if the path we are unpacking to does not exist -> create it
                    const size_t lastComp = destPath.find_last_of(L'\\');
                    if (lastComp != std::wstring::npos)
                    {
                        destPath.resize(lastComp);
                        SalamanderGeneral->CheckAndCreateDirectory(destPath.c_str());
                    } // if

                    salamander->ProgressSetSize(CQuadWord(0, 0), CQuadWord(-1, -1), TRUE);
                    salamander->ProgressSetTotalSize(fileData->Size + CQuadWord(1, 0), CQuadWord(-1, -1));

                    if (chm.ExtractObject(salamander, currentPath.c_str(), destPath.c_str(), fileData, silent, toSkip) == UNPACK_CANCEL ||
                        !salamander->ProgressAddSize(1, TRUE)) // correction for zero-sized files
                    {
                        ret = FALSE;
                        break;
                    }
                }
            }
        } // while

        salamander->CloseProgressDialog();
    }

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

    // open the CHM
    CCHMFile chm; // destructor calls Close
    if (!chm.Open(fileName, FALSE))
        return FALSE;

    BOOL ret = TRUE;

    salamander->OpenProgressDialog(SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_UNPACKING_ARCHIVE).c_str(), FALSE, NULL, FALSE);
    salamander->ProgressSetTotalSize(fileData->Size + CQuadWord(1, 0), CQuadWord(-1, -1));

    std::wstring name(targetDir);
    const wchar_t* lastComp = wcsrchr(nameInArchive, L'\\');
    if (lastComp != NULL)
        lastComp++;
    else
        lastComp = nameInArchive;

    SPLSalPathAppendOwned(name, lastComp);
    {
        DWORD silent = 0;
        BOOL toSkip = FALSE;

        salamander->ProgressDialogAddText(name.c_str(), TRUE); // delayedPaint==TRUE so we do not slow down

        std::wstring srcPath(nameInArchive);
        const size_t lComp = srcPath.find_last_of(L'\\');
        if (lComp != std::wstring::npos)
            srcPath.resize(lComp);

        chm.ButtonFlags = BUTTONS_OK;
        ret = chm.ExtractObject(salamander, srcPath.c_str(), targetDir, fileData, silent, toSkip);
    } // if

    salamander->CloseProgressDialog();

    return ret == UNPACK_OK;
}

void CalcSize(CSalamanderDirectoryAbstract const* dir, const wchar_t* mask, std::wstring& path,
              CQuadWord& size, CQuadWord& fileCount)
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
    const size_t pathLen = path.size();
    for (i = 0; i < count; i++)
    {
        CFileData const* file = dir->GetDir(i);
        //    TRACE_I("CalcSize(): directory: " << path << (path[0] != 0 ? "\\" : "") << file->Name);
        SPLSalPathAppendOwned(path, file->Name);
        CSalamanderDirectoryAbstract const* subDir = dir->GetSalDir(i);
        CalcSize(subDir, mask, path, size, fileCount);
        path.resize(pathLen);
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
    if (ListArchiveWide(salamander, fileName, dir, pluginData))
    {
        CQuadWord totalSize(0, 0);
        CQuadWord fileCount(0, 0);
        std::wstring sizePathW;
        CalcSize(dir, mask, sizePathW, totalSize, fileCount);

        if (delArchiveWhenDone)
            archiveVolumes->Add(fileName, -2);

        //
        BOOL delTempDir = TRUE;
        const std::wstring unpackingTitle =
            SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_UNPACKING_ARCHIVE);
        if (SalamanderGeneral->TestFreeSpace(SalamanderGeneral->GetMsgBoxParent(),
                                             targetDir, totalSize, unpackingTitle.c_str()))
        {
            salamander->OpenProgressDialog(unpackingTitle.c_str(), TRUE, NULL, FALSE);
            salamander->ProgressSetTotalSize(CQuadWord(0, 0), totalSize);

            const std::wstring modmask =
                SPLPrepareMaskOwned(SalamanderGeneral, mask);

            // try to open the CHM image
            CCHMFile chm; // destructor calls Close
            if (chm.Open(fileName, FALSE))
            {
                DWORD silent = 0;
                BOOL toSkip = FALSE;
                std::wstring strTargetW(targetDir);
                std::wstring srcPathW;

                chm.ButtonFlags = BUTTONS_SKIPCANCEL;
                ret = chm.ExtractAllObjects(salamander, srcPathW, dir, modmask.c_str(), strTargetW, silent, toSkip) != UNPACK_CANCEL;
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

// ****************************************************************************
//
// CPluginDataInterface
//

CPluginDataInterface::CPluginDataInterface()
{
}

CPluginDataInterface::~CPluginDataInterface()
{
}

void CPluginDataInterface::ReleasePluginData(CFileData& file, BOOL isDir)
{
    CALL_STACK_MESSAGE1("CPluginDataInterface::ReleasePluginData(, )");

    delete (chmUnitInfo*)file.PluginData;
    file.PluginData = NULL;
}
