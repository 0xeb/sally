// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

//****************************************************************************
//
// Copyright (c) 2023 Open Salamander Authors
//
// This is a part of the Open Salamander SDK library.
//
//****************************************************************************

#include "precomp.h"

#include "unfat.h"
#include "fat.h"

#include "unfat.rh"
#include "unfat.rh2"
#include "lang\lang.rh"

#include <vector>

// ****************************************************************************

HINSTANCE DLLInstance = NULL; // handle to the SPL module - language-independent resources
HINSTANCE HLanguage = NULL;   // handle to the SLG module - language-dependent resources

// plugin interface object; its methods are called by Salamander
CPluginInterface PluginInterface;
// the portion of CPluginInterface used for the archiver
CPluginInterfaceForArchiver InterfaceForArchiver;

// general Salamander interface - valid from start until the plugin is unloaded
CSalamanderGeneralAbstract* SalamanderGeneral = NULL;

// interface for working with files - valid from start until the plugin is unloaded
CSalamanderSafeFileAbstract* SalamanderSafeFile = NULL;

// variable definition for "dbg.h"
CSalamanderDebugAbstract* SalamanderDebug = NULL;

// variable definition for "spl_com.h"
int SalamanderVersion = 0;

// this placeholder is enough for configuration for now
//DWORD Options;
COptions Options;

int SortByExtDirsAsFiles = FALSE; // current value of Salamander's SALCFG_SORTBYEXTDIRSASFILES configuration variable

// frequently used error message
const char* LOW_MEMORY = "Low memory";

//const char *CONFIG_OPTIONS = "Options";
//const char *CONFIG_CLEAR_READONLY = "Clear Read Only";
//const char *CONFIG_SESSION_AS_DIR = "Show Session As Directory";

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

    // this plugin is made for the current version of Salamander or newer - perform a check
    if (SalamanderVersion < LAST_VERSION_OF_SALAMANDER)
    { // reject older versions
        // wide: same call-site-local widen shape used throughout this backlog
        // (205-225).
        MessageBoxW(salamander->GetParentWindow(),
                    _CRT_WIDE(REQUIRE_LAST_VERSION_OF_SALAMANDER),
                    L"UnFAT" /* do not translate! */, MB_OK | MB_ICONERROR);
        return NULL;
    }

    // let Salamander load the language module (.slg)
    HLanguage = salamander->LoadLanguageModule(salamander->GetParentWindow(), L"UnFAT" /* do not translate! */);
    if (HLanguage == NULL)
        return NULL;

    // obtain the general Salamander interface
    SalamanderGeneral = salamander->GetSalamanderGeneral();
    SalamanderSafeFile = salamander->GetSalamanderSafeFile();

    SalamanderGeneral->GetConfigParameter(SALCFG_SORTBYEXTDIRSASFILES, &SortByExtDirsAsFiles,
                                          sizeof(SortByExtDirsAsFiles), NULL);

    if (!InitializeWinLib(L"UnFAT" /* do not translate! */, DLLInstance))
        return NULL;
    SetWinLibStrings(L"Invalid number!", LangStr(IDS_PLUGINNAME).c_str());

    // set up the basic plugin information
    salamander->SetBasicPluginData(LangStr(IDS_PLUGINNAME).c_str(),
                                   FUNCTION_PANELARCHIVERVIEW | FUNCTION_CUSTOMARCHIVERUNPACK,
                                   _CRT_WIDE(VERSINFO_VERSION_NO_PLATFORM),
                                   _CRT_WIDE(VERSINFO_COPYRIGHT),
                                   LangStr(IDS_PLUGIN_DESCRIPTION).c_str(),
                                   L"UnFAT" /* do not translate! */, L"ima");

    salamander->SetPluginHomePageURL(L"https://github.com/0xeb/sally");

    return &PluginInterface;
}

BOOL Error(int resID, BOOL quiet, ...)
{
    if (!quiet)
    {
        va_list arglist;
        va_start(arglist, quiet);
        const std::wstring message = SPLFormatStringOwnedV(LangStr(resID).c_str(), arglist);
        va_end(arglist);

        SalamanderGeneral->ShowMessageBox(message.c_str(), LangStr(IDS_PLUGINNAME).c_str(), MSGBOX_ERROR);
    }
    return FALSE;
}

// ****************************************************************************
//
// CPluginInterface
//

void CPluginInterface::About(HWND parent)
{
    const std::wstring text = LangStr(IDS_PLUGINNAME) + L" " _CRT_WIDE(VERSINFO_VERSION) L"\n\n" _CRT_WIDE(VERSINFO_COPYRIGHT) L"\n\n" +
                              LangStr(IDS_PLUGIN_DESCRIPTION);
    SalamanderGeneral->SalMessageBox(parent, text.c_str(), LangStr(IDS_ABOUT).c_str(), MB_OK | MB_ICONINFORMATION);
}

HANDLE SafeFileCreateWithOwnedSkipPath(const wchar_t* fileName,
                                       DWORD desiredAccess,
                                       DWORD shareMode,
                                       DWORD flagsAndAttributes,
                                       BOOL isDir,
                                       HWND parent,
                                       const wchar_t* sourceFileName,
                                       const wchar_t* sourceFileInfo,
                                       DWORD* silentMask,
                                       BOOL allowSkip,
                                       BOOL* skipped,
                                       std::wstring* skipPath,
                                       CQuadWord* allocateWholeFile,
                                       SAFE_FILE* file)
{
    std::vector<wchar_t> skipBuffer;
    if (skipPath != nullptr)
    {
        const size_t required = wcslen(fileName) + 1;
        if (required > INT_MAX)
        {
            skipPath->clear();
            if (skipped != nullptr)
                *skipped = FALSE;
            SetLastError(ERROR_FILENAME_EXCED_RANGE);
            return INVALID_HANDLE_VALUE;
        }
        skipBuffer.resize(required);
    }

    HANDLE result = SalamanderSafeFile->SafeFileCreate(
        fileName, desiredAccess, shareMode, flagsAndAttributes, isDir, parent,
        sourceFileName, sourceFileInfo, silentMask, allowSkip, skipped,
        skipBuffer.empty() ? nullptr : skipBuffer.data(), static_cast<int>(skipBuffer.size()),
        allocateWholeFile, file);
    if (skipPath != nullptr)
        skipPath->assign(skipBuffer.data());
    return result;
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
}

void CPluginInterface::SaveConfiguration(HWND parent, HKEY regKey, CSalamanderRegistryAbstract* registry)
{
    CALL_STACK_MESSAGE1("CPluginInterface::SaveConfiguration(, ,)");
}

void CPluginInterface::Connect(HWND parent, CSalamanderConnectAbstract* salamander)
{
    CALL_STACK_MESSAGE1("CPluginInterface::Connect(,)");

    // BASIC SECTION
    salamander->AddPanelArchiver(L"ima", FALSE, FALSE);
    salamander->AddCustomUnpacker(L"UnFAT (Plugin)", L"*.ima", FALSE);
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

BOOL CPluginInterfaceForArchiver::ListArchive(CSalamanderForOperationsAbstract* salamander,
                                              const wchar_t* fileName,
                                              CSalamanderDirectoryAbstract* dir,
                                              CPluginDataInterfaceAbstract*& pluginData)
{
    CALL_STACK_MESSAGE2("CPluginInterfaceForArchiver::ListArchive(, %ls, ,)", fileName);
    pluginData = NULL;

    // attempt to open the FAT image
    BOOL ret = FALSE;
    HWND hParent = SalamanderGeneral->GetMsgBoxParent();
    CFATImage fatImage; // the destructor calls Close
    if (fatImage.Open(fileName, FALSE, hParent))
    {
        // hand over the complete listing to the Salamander core
        if (fatImage.ListImage(dir, hParent))
        {
            ret = TRUE;
        }
    }
    return ret;
}

BOOL CPluginInterfaceForArchiver::UnpackArchive(CSalamanderForOperationsAbstract* salamander, const wchar_t* fileName,
                                                CPluginDataInterfaceAbstract* pluginData,
                                                const wchar_t* targetDir, const wchar_t* archiveRoot, SalEnumSelection next,
                                                void* nextParam)
{
    CALL_STACK_MESSAGE4("CPluginInterfaceForArchiver::UnpackArchive(, %ls, %ls, %ls, ,)", fileName, targetDir, archiveRoot);

    HWND hParent = SalamanderGeneral->GetMsgBoxParent();

    // attempt to open the FAT image
    CFATImage fatImage; // the destructor calls Close
    if (!fatImage.Open(fileName, FALSE, hParent))
        return FALSE;

    BOOL ret = FALSE;
    // compute 'totalSize' for the progress dialog
    BOOL isDir;
    CQuadWord size;
    CQuadWord totalSize(0, 0);
    CQuadWord realTotalSize(0, 0);
    const wchar_t* name;
    const CFileData* fileData;
    int errorOccured;
    while ((name = next(hParent, 1, &isDir, &size, &fileData, nextParam, &errorOccured)) != NULL)
    {
        if (isDir)
        {
            // directory
            size = COPY_MIN_FILE_SIZE;
        }
        else
        {
            // file
            realTotalSize += size;
            if (size < COPY_MIN_FILE_SIZE)
                size = COPY_MIN_FILE_SIZE;
        }
        totalSize += size;
    }

    // check whether an error occurred or the user requested to cancel the operation (Cancel button) +
    // check the free disk space and optionally unpack
    if (errorOccured != SALENUM_CANCEL &&
        SalamanderGeneral->TestFreeSpace(hParent, targetDir, realTotalSize, LangStr(IDS_PLUGINNAME).c_str()))
    {
        const std::wstring title = SPLFormatStringOwned(
            LangStr(IDS_EXTRACTING_ARCHIVE).c_str(), SalamanderGeneral->SalPathFindFileName(fileName));
        salamander->OpenProgressDialog(title.c_str(), TRUE, NULL, FALSE);
        // set the maximum value for the TOTAL progress bar
        salamander->ProgressSetTotalSize(CQuadWord(-1, -1), totalSize);

        BOOL toSkip = FALSE;

        CAllocWholeFileEnum allocWholeFileOnStart = awfNeededTest;
        std::wstring skipPath; // all subdirectories and files under this path will be ignored
        DWORD silentMask = 0; // mask holding skip modes; 0 = no skip
        ret = TRUE;
        next(NULL, -1, NULL, NULL, NULL, nextParam, NULL); // reset the enumeration
        while ((name = next(NULL /* do not report errors the second time */, 1, &isDir, &size, &fileData, nextParam, NULL)) != NULL)
        {
            std::wstring destPath(targetDir);

            std::wstring nameInArchive(archiveRoot);
            SPLSalPathAppendOwned(nameInArchive, name);

            // reset the FILE progress bar to its initial value
            salamander->ProgressSetSize(CQuadWord(0, 0), CQuadWord(-1, -1), TRUE);

            SPLSalPathAppendOwned(destPath, name);
            {
                if (!skipPath.empty())
                {
                    if (SalamanderGeneral->PathIsPrefix(skipPath.c_str(), destPath.c_str()))
                        continue; // the item lies under skipPath so it is ignored
                    else
                        skipPath.clear(); // the item does not belong under skipPath, discard skipPath
                }

                if (isDir)
                {
                    // create the target path
                    BOOL skipped;
                    if (SafeFileCreateWithOwnedSkipPath(destPath.c_str(), 0, 0, 0, TRUE, hParent, NULL, NULL, &silentMask, TRUE, &skipped,
                                                        &skipPath, NULL, NULL) == INVALID_HANDLE_VALUE)
                    {
                        if (!skipped)
                        {
                            ret = FALSE;
                            break;
                        }
                    }
                    // set the maximum value for the FILE progress bar
                    salamander->ProgressSetTotalSize(COPY_MIN_FILE_SIZE, CQuadWord(-1, -1));
                    // "extracting: %s..."
                    const std::wstring progressText =
                        SPLFormatStringOwned(LangStr(IDS_EXTRACTING).c_str(), nameInArchive.c_str());
                    salamander->ProgressDialogAddText(progressText.c_str(), TRUE);

                    CQuadWord size2 = COPY_MIN_FILE_SIZE;
                    if (!salamander->ProgressAddSize((int)size2.Value, TRUE))
                    {
                        ret = FALSE;
                        break; // the operation was cancelled
                    }
                }
                else
                {
                    // file
                    // set the maximum value for the FILE progress bar
                    salamander->ProgressSetTotalSize(fileData->Size, CQuadWord(-1, -1));

                    const size_t lastComp = destPath.find_last_of(L'\\');
                    if (lastComp != std::wstring::npos)
                        destPath.resize(lastComp);

                    BOOL skipped;
                    if (!fatImage.UnpackFile(salamander, fileName, nameInArchive.c_str(), fileData, destPath.c_str(),
                                             &silentMask, TRUE, &skipped, &skipPath,
                                             hParent, &allocWholeFileOnStart))
                    {
                        if (!skipped)
                        {
                            ret = FALSE;
                            break;
                        }
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
                                                const wchar_t* nameInArchive,
                                                const CFileData* fileData, const wchar_t* targetDir,
                                                const wchar_t* newFileName, BOOL* renamingNotSupported)
{
    CALL_STACK_MESSAGE4("CPluginInterfaceForArchiver::UnpackOneFile(, %ls, %ls, , %ls, ,)", fileName,
                        nameInArchive, targetDir);

    if (newFileName != NULL)
    {
        *renamingNotSupported = TRUE;
        return FALSE;
    }

    HWND hParent = SalamanderGeneral->GetMsgBoxParent();
    // attempt to open the FAT image
    CFATImage fatImage; // the destructor calls Close
    if (!fatImage.Open(fileName, FALSE, hParent))
        return FALSE;

    BOOL ret = TRUE;

    const std::wstring title = SPLFormatStringOwned(
        LangStr(IDS_EXTRACTING_ARCHIVE).c_str(), SalamanderGeneral->SalPathFindFileName(fileName));
    salamander->OpenProgressDialog(title.c_str(), FALSE, NULL, FALSE);
    CQuadWord totalSize;
    if (fileData->Size < COPY_MIN_FILE_SIZE)
        totalSize = COPY_MIN_FILE_SIZE;
    else
        totalSize = fileData->Size;
    salamander->ProgressSetTotalSize(totalSize, CQuadWord(-1, -1));

    DWORD silentDummy = 0;

    CAllocWholeFileEnum allocWholeFileOnStart = awfNeededTest;
    ret = fatImage.UnpackFile(salamander, fileName, nameInArchive, fileData, targetDir,
                              &silentDummy, FALSE, NULL, NULL, hParent,
                              &allocWholeFileOnStart);

    salamander->CloseProgressDialog();

    return ret;
}

void CalcSize(CSalamanderDirectoryAbstract const* dir, CSalamanderMaskGroup* maskGroup,
              CQuadWord* totalSize, CQuadWord* realTotalSize)
{
    *totalSize += COPY_MIN_FILE_SIZE; // account for the directory

    int count = dir->GetFilesCount();
    int i;
    for (i = 0; i < count; i++)
    {
        CFileData const* file = dir->GetFile(i);

        if (maskGroup->AgreeMasks(file->Name, file->Ext))
        {
            *realTotalSize += file->Size;

            if (file->Size < COPY_MIN_FILE_SIZE)
                *totalSize += COPY_MIN_FILE_SIZE;
            else
                *totalSize += file->Size;
        }
    }

    count = dir->GetDirsCount();
    int j;
    for (j = 0; j < count; j++)
    {
        CFileData const* file = dir->GetDir(j);
        CSalamanderDirectoryAbstract const* subDir = dir->GetSalDir(j);
        CalcSize(subDir, maskGroup, totalSize, realTotalSize);
    }
}

BOOL ExtractArchive(CSalamanderDirectoryAbstract const* dir, CSalamanderMaskGroup* maskGroup,
                    CSalamanderForOperationsAbstract* salamander, CFATImage* img,
                    const wchar_t* archiveName, const wchar_t* targetDir,
                     std::wstring& path, DWORD* silent, std::wstring& skipPath)
{
    HWND hParent = SalamanderGeneral->GetMsgBoxParent();
    // process files first
    CAllocWholeFileEnum allocWholeFileOnStart = awfNeededTest;
    int filesCount = dir->GetFilesCount();
    int unpackedCount = 0;
    size_t pathLen = path.size();

    int i;
    for (i = 0; i < filesCount; i++)
    {
        salamander->ProgressSetSize(CQuadWord(0, 0), CQuadWord(-1, -1), TRUE);

        CFileData const* fileData = dir->GetFile(i);

        if (maskGroup->AgreeMasks(fileData->Name, fileData->Ext))
        {
            // set the maximum value for the FILE progress bar
            salamander->ProgressSetTotalSize(fileData->Size, CQuadWord(-1, -1));

            std::wstring myTargetDir(targetDir);
            SPLSalPathAppendOwned(myTargetDir, path.c_str());

            SPLSalPathAppendOwned(path, fileData->Name);

            BOOL unpack = TRUE;
            if (!skipPath.empty())
            {
                if (SalamanderGeneral->PathIsPrefix(skipPath.c_str(), myTargetDir.c_str()))
                {
                    unpack = FALSE; // the item lies under skipPath so it is ignored
                }
                else
                    skipPath.clear(); // the item does not belong under skipPath, discard skipPath
            }

            if (unpack)
            {
                BOOL skipped;
                if (!img->UnpackFile(salamander, archiveName, path.c_str(), fileData, myTargetDir.c_str(), silent,
                                      TRUE, &skipped, &skipPath, hParent, &allocWholeFileOnStart))
                {
                    // skip || skip all || cancel || error
                    if (!skipped)
                        return FALSE; // Cancel -- we have to exit
                }
            }

            unpackedCount++;

            path.resize(pathLen);
        }
    }

    // then recurse into directories
    int dirsCount = dir->GetDirsCount();
    pathLen = path.size();
    int j;
    for (j = 0; j < dirsCount; j++)
    {
        CFileData const* subDirData = dir->GetDir(j);
        SPLSalPathAppendOwned(path, subDirData->Name);
        CSalamanderDirectoryAbstract const* subDir = dir->GetSalDir(j);

        BOOL unpack = TRUE;
        if (!skipPath.empty())
        {
            std::wstring testPath(targetDir);
            SPLSalPathAppendOwned(testPath, path.c_str());

            if (SalamanderGeneral->PathIsPrefix(skipPath.c_str(), testPath.c_str()))
            {
                unpack = FALSE; // the item lies under skipPath so it is ignored
            }
            else
                skipPath.clear(); // the item does not belong under skipPath, discard skipPath
        }

        if (unpack)
        {
            if (!ExtractArchive(subDir, maskGroup, salamander, img, archiveName,
                                targetDir, path, silent, skipPath))
                return FALSE;
        }
        path.resize(pathLen);
    }

    if (unpackedCount == 0 && dirsCount == 0)
    {
        // descend into the last empty directory -- create at least that directory
        salamander->ProgressSetSize(CQuadWord(0, 0), CQuadWord(-1, -1), TRUE);
        // set the maximum value for the FILE progress bar
        salamander->ProgressSetTotalSize(COPY_MIN_FILE_SIZE, CQuadWord(-1, -1));

        // an empty directory must be created explicitly
        std::wstring dirName(targetDir);
        SPLSalPathAppendOwned(dirName, path.c_str());

        // "extracting: %s..."
        const std::wstring progressText =
            SPLFormatStringOwned(LangStr(IDS_EXTRACTING).c_str(), path.c_str());
        salamander->ProgressDialogAddText(progressText.c_str(), TRUE);

        // create the target path
        BOOL skipped;
        if (SafeFileCreateWithOwnedSkipPath(dirName.c_str(), 0, 0, 0, TRUE, hParent, NULL, NULL, silent, TRUE, &skipped,
                                            &skipPath, NULL, NULL) == INVALID_HANDLE_VALUE)
        {
            if (!skipped)
                return FALSE;
        }

        CQuadWord size = COPY_MIN_FILE_SIZE;
        if (!salamander->ProgressAddSize((int)size.Value, TRUE))
            return FALSE;
    }

    return TRUE;
}

BOOL CPluginInterfaceForArchiver::UnpackWholeArchive(CSalamanderForOperationsAbstract* salamander, const wchar_t* fileName,
                                                     const wchar_t* mask, const wchar_t* targetDir, BOOL delArchiveWhenDone,
                                                     CDynamicString* archiveVolumes)
{
    CALL_STACK_MESSAGE5("CPluginInterfaceForArchiver::UnpackWholeArchive(, %ls, %ls, %ls, %d,)",
                        fileName, mask, targetDir, delArchiveWhenDone);

    CSalamanderDirectoryAbstract* dir = SalamanderGeneral->AllocSalamanderDirectory(FALSE);
    if (dir == NULL)
        return FALSE;

    BOOL ret = FALSE;
    CPluginDataInterfaceAbstract* pluginData = NULL;
    if (ListArchive(salamander, fileName, dir, pluginData))
    {
        CSalamanderMaskGroup* maskGroup = SalamanderGeneral->AllocSalamanderMaskGroup();
        if (maskGroup != NULL)
        {
            maskGroup->SetMasksString(mask, FALSE);
            int err;
            if (maskGroup->PrepareMasks(err))
            {
                std::wstring path;
                CQuadWord totalSize(0, 0);
                CQuadWord realTotalSize(0, 0);
                CalcSize(dir, maskGroup, &totalSize, &realTotalSize);

                if (SalamanderGeneral->TestFreeSpace(SalamanderGeneral->GetMsgBoxParent(),
                                                     targetDir, realTotalSize, LangStr(IDS_PLUGINNAME).c_str()))
                {
                    if (delArchiveWhenDone)
                        archiveVolumes->Add(fileName, -2);

                    const std::wstring title = SPLFormatStringOwned(
                        LangStr(IDS_EXTRACTING_ARCHIVE).c_str(), SalamanderGeneral->SalPathFindFileName(fileName));
                    salamander->OpenProgressDialog(title.c_str(), TRUE, NULL, FALSE);
                    // set the maximum value for the TOTAL progress bar
                    salamander->ProgressSetTotalSize(CQuadWord(-1, -1), totalSize);

                    HWND hParent = SalamanderGeneral->GetMsgBoxParent();
                    CFATImage fatImage; // the destructor calls Close
                    if (fatImage.Open(fileName, FALSE, hParent))
                    {
                        path.clear();
                        DWORD silent = 0;
                        std::wstring skipPath; // all subdirectories and files under this path will be ignored
                        ret = ExtractArchive(dir, maskGroup, salamander, &fatImage, fileName, targetDir, path, &silent, skipPath);
                    }

                    salamander->CloseProgressDialog();
                }
            }
            SalamanderGeneral->FreeSalamanderMaskGroup(maskGroup);
        }

        if (pluginData != NULL)
        {
            // dir->Clear(pluginData); // unused, no need to free it
            PluginInterface.ReleasePluginDataInterface(pluginData);
        }
    }

    SalamanderGeneral->FreeSalamanderDirectory(dir);

    return ret;
}
