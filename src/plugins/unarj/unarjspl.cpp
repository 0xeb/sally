// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"

#include "array2.h"

#include "unarjdll.h"
#include "unarjspl.h"
#include "unarj_text.h"
#include "dialogs.h"

#include "unarj.rh"
#include "unarj.rh2"
#include "lang\lang.rh"

// ****************************************************************************

HINSTANCE DLLInstance = NULL; // handle to the SPL - language-independent resources
HINSTANCE HLanguage = NULL;   // handle to the SLG - language-dependent resources

/*
FPAKGetIFace PAKGetIFace;
FPAKReleaseIFace PAKReleaseIFace;
*/

// plugin interface object; its methods are called from Salamander
CPluginInterface PluginInterface;
// part of the CPluginInterface interface for the archiver
CPluginInterfaceForArchiver InterfaceForArchiver;

// general Salamander interface - valid from startup until the plugin is closed
CSalamanderGeneralAbstract* SalamanderGeneral = NULL;

// interface for comfortable work with files
CSalamanderSafeFileAbstract* SalamanderSafeFile = NULL;

// variable definition for "dbg.h"
CSalamanderDebugAbstract* SalamanderDebug = NULL;

// for now this configuration placeholder is sufficient
DWORD Options;

const wchar_t* CONFIG_OPTIONS = L"Options";

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved)
{
    CALL_STACK_MESSAGE_NONE
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

int WINAPI SalamanderPluginGetReqVer()
{
    CALL_STACK_MESSAGE_NONE
    return LAST_VERSION_OF_SALAMANDER;
}

CPluginInterfaceAbstract* WINAPI SalamanderPluginEntry(CSalamanderPluginEntryAbstract* salamander)
{
    CALL_STACK_MESSAGE_NONE
    // set SalamanderDebug for "dbg.h"
    SalamanderDebug = salamander->GetSalamanderDebug();

    CALL_STACK_MESSAGE1("SalamanderPluginEntry()");

    // this plugin is made for the current Salamander version and newer - perform a check
    if (salamander->GetVersion() < LAST_VERSION_OF_SALAMANDER)
    { // reject older versions
        // wide: same call-site-local widen shape used throughout this backlog
        // (205-222).
        MessageBoxW(salamander->GetParentWindow(),
                    _CRT_WIDE(REQUIRE_LAST_VERSION_OF_SALAMANDER),
                    L"UnARJ" /* do not translate! */, MB_OK | MB_ICONERROR);
        return NULL;
    }

    // let it load the language module (.slg)
    HLanguage = salamander->LoadLanguageModule(salamander->GetParentWindow(), L"UnARJ" /* do not translate! */);
    if (HLanguage == NULL)
        return NULL;

    // obtain the general Salamander interface
    SalamanderGeneral = salamander->GetSalamanderGeneral();
    SalamanderSafeFile = salamander->GetSalamanderSafeFile();

    /*
  // beta is valid until the end of February 2001
  SYSTEMTIME st;
  GetLocalTime(&st);
  if (st.wYear == 2001 && st.wMonth > 2 || st.wYear > 2001)
  {
    SalamanderGeneral->ShowMessageBox(SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_EXPIRE).c_str(), SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_PLUGINNAME).c_str(), MSGBOX_INFO);
    return NULL;
  }
  */

    // set the basic information about the plugin
    salamander->SetBasicPluginData(SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_PLUGINNAME).c_str(),
                                   FUNCTION_PANELARCHIVERVIEW | FUNCTION_CUSTOMARCHIVERUNPACK |
                                       FUNCTION_CONFIGURATION | FUNCTION_LOADSAVECONFIGURATION,
                                   _CRT_WIDE(VERSINFO_VERSION_NO_PLATFORM),
                                   _CRT_WIDE(VERSINFO_COPYRIGHT),
                                   SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_PLUGIN_DESCRIPTION).c_str(),
                                   L"UnARJ" /* do not translate! */, L"arj");

    salamander->SetPluginHomePageURL(L"https://github.com/0xeb/sally");

    return &PluginInterface;
}

// ****************************************************************************
//
// CPluginInterface
//

void CPluginInterface::About(HWND parent)
{
    const std::wstring text = SPLFormatStringOwned(
        L"%ls %ls\n\n%ls\n\n%ls",
        SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_PLUGINNAME).c_str(),
        _CRT_WIDE(VERSINFO_VERSION), _CRT_WIDE(VERSINFO_COPYRIGHT),
        SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_PLUGIN_DESCRIPTION).c_str());
    SalamanderGeneral->SalMessageBox(parent, text.c_str(),
                                     SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_ABOUT).c_str(),
                                     MB_OK | MB_ICONINFORMATION);
}

void CPluginInterface::LoadConfiguration(HWND parent, HKEY regKey, CSalamanderRegistryAbstract* registry)
{
    CALL_STACK_MESSAGE1("CPluginInterface::LoadConfiguration(, ,)");
    Options = 0;
    if (regKey != NULL) // load z registry
    {
        registry->GetValue(regKey, CONFIG_OPTIONS, REG_DWORD, &Options, sizeof(DWORD));
    }
}

void CPluginInterface::SaveConfiguration(HWND parent, HKEY regKey, CSalamanderRegistryAbstract* registry)
{
    CALL_STACK_MESSAGE1("CPluginInterface::SaveConfiguration(, ,)");
    registry->SetValue(regKey, CONFIG_OPTIONS, REG_DWORD, &Options, sizeof(DWORD));
}

void CPluginInterface::Configuration(HWND parent)
{
    CALL_STACK_MESSAGE1("CPluginInterface::Configuration()");
    ConfigDialog(parent);
}

void CPluginInterface::Connect(HWND parent, CSalamanderConnectAbstract* salamander)
{
    CALL_STACK_MESSAGE1("CPluginInterface::Connect(,)");

    salamander->AddCustomUnpacker(L"UnARJ (Plugin)", L"*.arj;*.a##", FALSE);
    salamander->AddPanelArchiver(L"arj;a##", FALSE, FALSE);
}

CPluginInterfaceForArchiverAbstract*
CPluginInterface::GetInterfaceForArchiver()
{
    CALL_STACK_MESSAGE_NONE
    return &InterfaceForArchiver;
}

// ****************************************************************************
//
// CPluginInterfaceForArchiver
//

BOOL WINAPI
ARJChangeVolProc(std::wstring& volName, const wchar_t* prevName, int mode)
{
    CALL_STACK_MESSAGE_NONE
    return InterfaceForArchiver.ChangeVolProc(volName, prevName, mode);
}

BOOL WINAPI
ARJProcessDataProc(const void* buffer, DWORD size)
{
    CALL_STACK_MESSAGE_NONE
    return InterfaceForArchiver.ProcessDataProc(buffer, size);
}

BOOL WINAPI
ARJErrorProc(int error, BOOL flags)
{
    CALL_STACK_MESSAGE_NONE
    return InterfaceForArchiver.ErrorProc(error, flags);
}

void WINAPI
ARJArchiveVolumeProc(const wchar_t* volumeName)
{
    CALL_STACK_MESSAGE_NONE
    InterfaceForArchiver.AddArchiveVolume(volumeName);
}

BOOL CPluginInterfaceForArchiver::ListArchive(CSalamanderForOperationsAbstract* salamander, const wchar_t* fileName,
                                              CSalamanderDirectoryAbstract* dir,
                                              CPluginDataInterfaceAbstract*& pluginData)
{
    CALL_STACK_MESSAGE2("CPluginInterfaceForArchiver::ListArchive(, %ls, ,)", fileName);
    pluginData = NULL;
    if (!SwitchToFirstVol(fileName))
        return FALSE;

    Salamander = salamander;
    List = TRUE;
    Silent = 0;
    BOOL ret = TRUE;
    Abort = FALSE;
    NotWholeArchListed = FALSE;
    int count = 0;

    CARJOpenData od;
    od.ArcName = ArcFileName.c_str();
    od.ARJChangeVolProc = ARJChangeVolProc;
    od.ARJErrorProc = ARJErrorProc;
    od.ARJProcessDataProc = ARJProcessDataProc;
    od.ARJArchiveVolumeProc = NULL;
    ret = ARJOpenArchive(&od);
    if (ret)
    {
        int sortByExtDirsAsFiles;
        SalamanderGeneral->GetConfigParameter(SALCFG_SORTBYEXTDIRSASFILES, &sortByExtDirsAsFiles,
                                              sizeof(sortByExtDirsAsFiles), NULL);

        CARJHeaderData header;
        CFileData fileData;
        wchar_t* slash;
        const wchar_t* path;
        wchar_t* name;
        while ((ret = ARJReadHeader(&header)) != 0 && !header.FileName.empty())
        {
            if (header.Flags & FF_EXTFILE)
                NotWholeArchListed = TRUE;
            std::wstring headerPath = header.FileName;
            path = headerPath.data();
            name = headerPath.data();
            slash = wcsrchr(headerPath.data(), L'\\');
            if (slash)
            {
                *slash = L'\0';
                name = slash + 1;
            }
            else
                path = L"";
            fileData.Name = SalamanderGeneral->DupStr(name);
            if (!fileData.Name)
            {
                SalamanderGeneral->ShowMessageBox(SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_LOWMEM).c_str(),
                                                  SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_PLUGINNAME).c_str(), MSGBOX_ERROR);
                ret = FALSE;
                break;
            }
            fileData.Ext = wcsrchr(fileData.Name, L'.');
            if (fileData.Ext != NULL)
                fileData.Ext++; // ".cvspass" is an extension in Windows
            else
                fileData.Ext = fileData.Name + lstrlenW(fileData.Name);
            fileData.Attr = header.Attr;
            if (header.FileType == FT_DIRECTORY)
                fileData.Attr |= FILE_ATTRIBUTE_DIRECTORY;
            if (header.Flags & FF_ENCRYPTED && !(fileData.Attr & FILE_ATTRIBUTE_DIRECTORY))
                fileData.Attr |= FILE_ATTRIBUTE_ENCRYPTED;
            fileData.Hidden = fileData.Attr & FILE_ATTRIBUTE_HIDDEN ? 1 : 0;
            fileData.PluginData = -1; // unnecessary, just for form's sake
            fileData.LastWrite = header.Time;
            fileData.DosName = NULL;
            fileData.NameLen = lstrlenW(fileData.Name);

            //if (slash) *slash = '\\';
            //if (!ProcessFile(OP_SKIP, "", header.FileName)) */
            if (!ARJProcessFile(PFO_SKIP, &header.Size))
            {
                if (Abort)
                    ret = FALSE;
                free(fileData.Name);
                break;
            }

            fileData.Size = CQuadWord(header.Size, 0);
            fileData.IsOffline = 0;
            if (fileData.Attr & FILE_ATTRIBUTE_DIRECTORY)
            {
                if (!sortByExtDirsAsFiles)
                    fileData.Ext = fileData.Name + fileData.NameLen; // directories do not have extensions
                fileData.IsLink = 0;
                if (!dir->AddDir(path, fileData, NULL))
                    ret = FALSE;
            }
            else
            {
                fileData.IsLink = SalamanderGeneral->IsFileLink(fileData.Ext);
                if (!dir->AddFile(path, fileData, NULL))
                    ret = FALSE;
            }

            if (!ret)
            {
                free(fileData.Name);
                SalamanderGeneral->ShowMessageBox(SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_LIST).c_str(),
                                                  SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_PLUGINNAME).c_str(), MSGBOX_ERROR);
                break;
            }

            count++;
        }
        ARJCloseArchive();
    }

    if (NotWholeArchListed && !(Options & OP_NO_VOL_ATTENTION))
        AttentionDialog(SalamanderGeneral->GetMainWindowHWND());

    Salamander = NULL;

    // we have already listed some files, so we will not abort and will display them
    if (!ret && count)
        ret = TRUE;

    return ret;
}

void FreeString(void* strig)
{
    CALL_STACK_MESSAGE_NONE
    free(strig);
}

void CPluginInterfaceForArchiver::AddArchiveVolume(const wchar_t* volumeName)
{
    ArchiveVolumes->Add(volumeName, -2);
}

BOOL CPluginInterfaceForArchiver::UnpackArchive(CSalamanderForOperationsAbstract* salamander, const wchar_t* fileName,
                                                CPluginDataInterfaceAbstract* pluginData, const wchar_t* targetDir,
                                                const wchar_t* archiveRoot, SalEnumSelection next, void* nextParam)
{
    CALL_STACK_MESSAGE4("CPluginInterfaceForArchiver::UnpackArchive(, %ls, , %ls, %ls, ,)", fileName, targetDir, archiveRoot);

    if (!SwitchToFirstVol(fileName))
        return FALSE;
    const wchar_t* normalizedRoot = archiveRoot != NULL ? archiveRoot : L"";
    if (*normalizedRoot == L'\\')
        normalizedRoot++;

    Salamander = salamander;
    List = FALSE;
    Silent = 0;
    BOOL ret = TRUE;
    Abort = FALSE;
    UnPackWholeArchive = FALSE;
    RootLen = lstrlenW(normalizedRoot);
    AllocateWholeFile = TRUE;
    TestAllocateWholeFile = TRUE;
    TIndirectArray2<std::wstring> files(256);

    // extract files
    const std::wstring title = SPLFormatStringOwned(
        SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_EXTRPROGTITLE).c_str(),
        SalamanderGeneral->SalPathFindFileName(ArcFileName.c_str()));
    Salamander->OpenProgressDialog(title.c_str(), TRUE, NULL, FALSE);
    Salamander->ProgressDialogAddText(SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_PREPAREDATA).c_str(), FALSE);

    CARJOpenData od;
    od.ArcName = ArcFileName.c_str();
    od.ARJChangeVolProc = ARJChangeVolProc;
    od.ARJErrorProc = ARJErrorProc;
    od.ARJProcessDataProc = ARJProcessDataProc;
    od.ARJArchiveVolumeProc = NULL;
    ret = ARJOpenArchive(&od);
    if (ret)
    {
        ret = MakeFilesList(files, next, nextParam, normalizedRoot, targetDir);
        if (ret)
        {
            CARJHeaderData header;
            int op;
            BOOL match;
            CQuadWord currentProgress(0, 0);
            Salamander->ProgressDialogAddText(SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_EXTRACTFILES).c_str(), FALSE);
            while (files.Count && (ret = ARJReadHeader(&header)) != 0 && !header.FileName.empty())
            {
                op = PFO_SKIP;
                match = FALSE;
                TargetFile = INVALID_HANDLE_VALUE;
                int i;
                for (i = 0; i < files.Count; i++)
                {
                    if (!(header.Attr & FILE_ATTRIBUTE_DIRECTORY || header.FileType == FT_DIRECTORY) &&
                        CompareStringW(LOCALE_USER_DEFAULT, NORM_IGNORECASE,
                                       header.FileName.c_str(), -1, files[i]->c_str(), -1) == CSTR_EQUAL)
                    {
                        match = TRUE;
                        files.Delete(i);
                        Salamander->ProgressSetTotalSize(CQuadWord(header.Size, 0), ProgressTotal);
                        if (!Salamander->ProgressSetSize(CQuadWord(0, 0), CQuadWord(-1, -1), TRUE))
                        {
                            Abort = TRUE;
                            break;
                        }
                        if (DoThisFile(&header, fileName, targetDir))
                            op = PFO_EXTRACT;
                        break;
                    }
                }

                UpdateProgress = TRUE;
                ret = ARJProcessFile(op);
                if (op == PFO_EXTRACT)
                {
                    SetFileTime(TargetFile, NULL, NULL, &header.Time);
                    CloseHandle(TargetFile);
                    if (!ret)
                        DeleteFileW(TargetName.c_str());
                    else
                        SetFileAttributesW(TargetName.c_str(), header.Attr);
                }
                if (Abort)
                {
                    ret = FALSE;
                    break;
                }
                if (match)
                {
                    currentProgress += CQuadWord(header.Size, 0);
                    if (!Salamander->ProgressSetSize(CQuadWord(header.Size, 0), currentProgress, TRUE))
                    {
                        ret = FALSE;
                        break;
                    }
                }
            }
        }
        ARJCloseArchive();
    }

    Salamander->CloseProgressDialog();

    Salamander = NULL;

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

    if (!SwitchToFirstVol(fileName))
        return FALSE;

    Salamander = salamander;
    Silent = 0;
    BOOL ret = FALSE;
    RootLen = 0;
    AllocateWholeFile = TRUE;
    TestAllocateWholeFile = TRUE;
    std::wstring justName(nameInArchive);
    SPLSalPathStripPathOwned(SalamanderGeneral, justName);

    // extract files
    List = FALSE;
    CARJOpenData od;
    od.ArcName = ArcFileName.c_str();
    od.ARJChangeVolProc = ARJChangeVolProc;
    od.ARJErrorProc = ARJErrorProc;
    od.ARJProcessDataProc = ARJProcessDataProc;
    od.ARJArchiveVolumeProc = NULL;
    if (ARJOpenArchive(&od))
    {
        CARJHeaderData header;
        int op;
        BOOL match = FALSE;
        while (ARJReadHeader(&header) && !header.FileName.empty())
        {
            TargetFile = INVALID_HANDLE_VALUE;
            op = PFO_SKIP;
            if (!(header.Attr & FILE_ATTRIBUTE_DIRECTORY || header.FileType == FT_DIRECTORY) &&
                CompareStringW(LOCALE_USER_DEFAULT, NORM_IGNORECASE,
                               nameInArchive, -1, header.FileName.c_str(), -1) == CSTR_EQUAL)
            {
                match = TRUE;
                if (header.Flags & FF_EXTFILE)
                {
                    ContinuedFileDialog(SalamanderGeneral->GetMsgBoxParent(), header.FileName.c_str());
                    goto UOF_NEXT;
                }
                const std::wstring info = GetInfo(&header.Time, header.Size);
                TargetName = targetDir;
                SPLSalPathAppendOwned(TargetName, justName.c_str());
                BOOL skip;
                CQuadWord q = CQuadWord(header.Size, 0);
                bool allocate = CQuadWord(2, 0) < q && q < CQuadWord(0, 0x80000000);
                q += CQuadWord(0, 0x80000000);
                TargetFile = SalamanderSafeFile->SafeFileCreate(TargetName.c_str(),
                                                                GENERIC_WRITE, FILE_SHARE_READ,
                                                                header.Attr & ~FILE_ATTRIBUTE_READONLY | FILE_FLAG_SEQUENTIAL_SCAN,
                                                                FALSE, SalamanderGeneral->GetMsgBoxParent(), nameInArchive, info.c_str(), &Silent,
                                                                TRUE, &skip, NULL, 0, allocate ? &q : NULL, NULL);
                if (TargetFile != INVALID_HANDLE_VALUE)
                    op = PFO_EXTRACT;
            }

        UOF_NEXT:
            UpdateProgress = TRUE;
            int r;
            r = ARJProcessFile(op);
            if (op == PFO_EXTRACT)
            {
                SetFileTime(TargetFile, NULL, NULL, &header.Time);
                CloseHandle(TargetFile);
                if (r)
                {
                    SetFileAttributesW(TargetName.c_str(), header.Attr);
                    ret = TRUE;
                }
                else
                    DeleteFileW(TargetName.c_str());
            }
            if (match || !r)
                break;
        }
        ARJCloseArchive();
    }

    Salamander = NULL;

    return ret;
}

BOOL CPluginInterfaceForArchiver::UnpackWholeArchive(CSalamanderForOperationsAbstract* salamander, const wchar_t* fileName,
                                                     const wchar_t* mask, const wchar_t* targetDir, BOOL delArchiveWhenDone,
                                                     CDynamicString* archiveVolumes)
{
    CALL_STACK_MESSAGE5("CPluginInterfaceForArchiver::UnpackWholeArchive(, %ls, %ls, %ls, %d,)",
                        fileName, mask, targetDir, delArchiveWhenDone);

    if (!SwitchToFirstVol(fileName))
        return FALSE;

    Salamander = salamander;
    List = FALSE;
    Silent = 0;
    BOOL ret = TRUE;
    Abort = FALSE;
    UnPackWholeArchive = TRUE;
    RootLen = 0;
    AllocateWholeFile = TRUE;
    TestAllocateWholeFile = TRUE;
    TIndirectArray2<std::wstring> masks(16);

    if (!ConstructMaskArray(masks, mask) || masks.Count == 0)
        return FALSE;

    // extract files
    const std::wstring title = SPLFormatStringOwned(
        SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_EXTRPROGTITLE).c_str(),
        SalamanderGeneral->SalPathFindFileName(ArcFileName.c_str()));
    Salamander->OpenProgressDialog(title.c_str(), FALSE, NULL, TRUE);
    Salamander->ProgressDialogAddText(SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_PREPAREDATA).c_str(), FALSE);

    ArchiveVolumes = delArchiveWhenDone ? archiveVolumes : NULL;
    CARJOpenData od;
    od.ArcName = ArcFileName.c_str();
    od.ARJChangeVolProc = ARJChangeVolProc;
    od.ARJErrorProc = ARJErrorProc;
    od.ARJProcessDataProc = ARJProcessDataProc;
    od.ARJArchiveVolumeProc = delArchiveWhenDone ? ARJArchiveVolumeProc : NULL;
    ret = ARJOpenArchive(&od);
    if (ret)
    {
        if (delArchiveWhenDone)
            archiveVolumes->Add(ArcFileName.c_str(), -2);
        CARJHeaderData header;
        int op;
        BOOL match;
        Salamander->ProgressDialogAddText(SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_EXTRACTFILES).c_str(), FALSE);
        while ((ret = ARJReadHeader(&header)) != 0 && !header.FileName.empty())
        {
            op = PFO_SKIP;
            match = FALSE;
            TargetFile = INVALID_HANDLE_VALUE;

            const wchar_t* name = SalamanderGeneral->SalPathFindFileName(header.FileName.c_str());
            BOOL nameHasExt = wcschr(name, L'.') != NULL; // ".cvspass" is an extension in Windows
            int i;
            for (i = 0; i < masks.Count; i++)
            {
                if (SalamanderGeneral->AgreeMask(name, masks[i]->c_str(), nameHasExt))
                {
                    match = TRUE;
                    Salamander->ProgressSetTotalSize(CQuadWord(header.Size, 0), CQuadWord(-1, -1));
                    if (!Salamander->ProgressSetSize(CQuadWord(0, 0), CQuadWord(-1, -1), TRUE))
                    {
                        Abort = TRUE;
                        break;
                    }
                    if (DoThisFile(&header, fileName, targetDir) && !(header.Attr & FILE_ATTRIBUTE_DIRECTORY ||
                                                                    header.FileType == FT_DIRECTORY))
                        op = PFO_EXTRACT;
                    break;
                }
            }

            UpdateProgress = TRUE;
            ret = ARJProcessFile(op);
            if (op == PFO_EXTRACT)
            {
                SetFileTime(TargetFile, NULL, NULL, &header.Time);
                CloseHandle(TargetFile);
                if (!ret)
                    DeleteFileW(TargetName.c_str());
                else
                    SetFileAttributesW(TargetName.c_str(), header.Attr);
            }
            if (Abort)
            {
                ret = FALSE;
                break;
            }
            if (match)
            {
                if (!Salamander->ProgressSetSize(CQuadWord(header.Size, 0), CQuadWord(-1, -1), TRUE))
                {
                    ret = FALSE;
                    break;
                }
            }
        }
        ARJCloseArchive();
    }
    ArchiveVolumes = NULL;

    Salamander->CloseProgressDialog();

    Salamander = NULL;

    return ret;
}

BOOL CPluginInterfaceForArchiver::Error(int error, ...)
{
    CALL_STACK_MESSAGE_NONE
    int lastErr = GetLastError();
    CALL_STACK_MESSAGE2("CPluginInterfaceForArchiver::Error(%d, )", error);
    va_list arglist;
    va_start(arglist, error);
    std::wstring message = SPLFormatStringOwnedV(
        SPLLoadStrOwned(SalamanderGeneral, HLanguage, error).c_str(), arglist);
    va_end(arglist);
    if (lastErr != ERROR_SUCCESS)
        message.append(SPLGetErrorTextOwned(SalamanderGeneral, lastErr));
    SalamanderGeneral->ShowMessageBox(message.c_str(), SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_PLUGINNAME).c_str(), MSGBOX_ERROR);

    return FALSE;
}

int CPluginInterfaceForArchiver::ChangeVolProc(std::wstring& arcName, const wchar_t* prevName, int mode)
{
    CALL_STACK_MESSAGE2("CPluginInterfaceForArchiver::ChangeVolProc(, %d)", mode);
    UpdateProgress = FALSE;
    if (mode == CVM_ASK)
    {
        if (List)
        {
            NotWholeArchListed = TRUE;
            return 0; // list only as long as it works without asking for additional volumes
        }
        if (NextVolumeDialog(SalamanderGeneral->GetMsgBoxParent(), arcName, prevName) != IDOK)
            return 0;
    }
    return 1;
}

BOOL CPluginInterfaceForArchiver::SafeSeek(DWORD position)
{
    CALL_STACK_MESSAGE2("CPluginInterfaceForArchiver::SafeSeek(0x%X)", position);
    while (1)
    {
        if (SetFilePointer(TargetFile, position, NULL, FILE_BEGIN) != 0xFFFFFFFF)
            return TRUE; // success
        const DWORD lastError = GetLastError();
        std::wstring message = SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_UNABLESEEK);
        message.append(SPLGetErrorTextOwned(SalamanderGeneral, lastError));

        if (Silent & SF_IOERRORS)
            return FALSE;

        switch (SalamanderGeneral->DialogError(SalamanderGeneral->GetMsgBoxParent(), BUTTONS_RETRYSKIPCANCEL, TargetName.c_str(), message.c_str(), NULL))
        {
        case DIALOG_SKIPALL:
            Silent |= SF_IOERRORS;
        case DIALOG_SKIP:
            return FALSE;
        case DIALOG_CANCEL:
        case DIALOG_FAIL:
            Abort = TRUE;
            return FALSE;
        }
    }
}

int CPluginInterfaceForArchiver::ProcessDataProc(const void* buffer, DWORD size)
{
    CALL_STACK_MESSAGE2("CPluginInterfaceForArchiver::ProcessDataProc(, 0x%X)", size);
    if (TargetFile == INVALID_HANDLE_VALUE)
        return 1;
    if (size == 0)
        return 1;
    DWORD pos;
    while (1)
    {
        pos = SetFilePointer(TargetFile, 0, NULL, FILE_CURRENT);
        if (pos != 0xFFFFFFFF)
            break;
        const DWORD lastError = GetLastError();
        std::wstring message = SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_UNABLEGETFIELPOS);
        message.append(SPLGetErrorTextOwned(SalamanderGeneral, lastError));

        if (Silent & SF_IOERRORS)
            return 0;

        switch (SalamanderGeneral->DialogError(SalamanderGeneral->GetMsgBoxParent(), BUTTONS_RETRYSKIPCANCEL, TargetName.c_str(), message.c_str(), NULL))
        {
        case DIALOG_SKIPALL:
            Silent |= SF_IOERRORS;
        case DIALOG_SKIP:
            return 0;
        case DIALOG_CANCEL:
        case DIALOG_FAIL:
            Abort = TRUE;
            return 0;
        }
    }
    DWORD written;
    while (1)
    {
        if (WriteFile(TargetFile, buffer, size, &written, NULL))
        {
            DWORD a = UpdateProgress ? size : 0;
            if (!Salamander->ProgressAddSize(a, TRUE))
            {
                Abort = TRUE;
                return 0;
            }
            return 1; // sucess
        }
        const DWORD lastError = GetLastError();
        std::wstring message = SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_UNABLEWRITE);
        message.append(SPLGetErrorTextOwned(SalamanderGeneral, lastError));

        if (Silent & SF_IOERRORS)
            return 0;

        switch (SalamanderGeneral->DialogError(SalamanderGeneral->GetMsgBoxParent(), BUTTONS_RETRYSKIPCANCEL, TargetName.c_str(), message.c_str(), NULL))
        {
        case DIALOG_SKIPALL:
            Silent |= SF_IOERRORS;
        case DIALOG_SKIP:
            return 0;
        case DIALOG_CANCEL:
        case DIALOG_FAIL:
            Abort = TRUE;
            return 0;
        }
        if (!SafeSeek(pos))
            return 0;
    }
    return 1;
}

BOOL CPluginInterfaceForArchiver::ErrorProc(int error, BOOL flags)
{
    CALL_STACK_MESSAGE3("CPluginInterfaceForArchiver::ErrorProc(%d, %d)", error, flags);
    int err;
    DWORD silent;
    switch (error)
    {
    case AE_SUCCESS:
        return TRUE;

    case AE_OPEN:
        err = IDS_ERROPENARC;
        goto EP_ARC;
    case AE_ACCESS:
        err = IDS_ERRACCESSARC;
        goto EP_ARC;
    case AE_EOF:
        err = IDS_ARCEOF;
        goto EP_ARC;
    case AE_BADARC:
        err = IDS_BADARC;
        goto EP_ARC;
    case AE_BADDATA:
        err = IDS_BADARC;
        goto EP_ARC;
    case AE_BADVOL:
        err = IDS_BADVOL;

    EP_ARC:
        if (SalamanderGeneral->SalMessageBox(SalamanderGeneral->GetMsgBoxParent(), SPLLoadStrOwned(SalamanderGeneral, HLanguage, err).c_str(),
                                             SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_PLUGINNAME).c_str(),
                                             MB_ICONEXCLAMATION | (flags & EF_RETRY ? MB_RETRYCANCEL : MB_OK)) == IDRETRY)
            return TRUE;
        Abort = TRUE;
        break;

    case AE_BADVERSION:
        err = IDS_VERSION;
        silent = SF_VERSION;
        goto EP_FILE;
    case AE_ENCRYPT:
        err = IDS_ENCRYPT;
        silent = SF_ENCRYPTED;
        goto EP_FILE;
    case AE_METHOD:
        err = IDS_METHOD;
        silent = SF_METHOD;
        goto EP_FILE;
    case AE_UNKNTYPE:
        err = IDS_TYPE;
        silent = SF_TYPE;
        goto EP_FILE;
    case AE_CRC:
        err = IDS_CRC;
        silent = SF_DATA;

    EP_FILE:
        if (!(Silent & silent))
        {
            switch (SalamanderGeneral->DialogError(SalamanderGeneral->GetMsgBoxParent(), BUTTONS_SKIPCANCEL, TargetName.c_str(), SPLLoadStrOwned(SalamanderGeneral, HLanguage, err).c_str(), NULL))
            {
            case DIALOG_SKIPALL:
                Silent |= silent;
            case DIALOG_SKIP:
                break;
            case DIALOG_CANCEL:
            case DIALOG_FAIL:
                Abort = TRUE;
                break;
            }
        }
        break;

    default:
        SalamanderGeneral->ShowMessageBox(SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_UNKNOWN).c_str(),
                                          SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_PLUGINNAME).c_str(), MSGBOX_ERROR);
        Abort = TRUE;
    }
    return FALSE;
}

BOOL CPluginInterfaceForArchiver::SwitchToFirstVol(const wchar_t* arcName)
{
    CALL_STACK_MESSAGE2("CPluginInterfaceForArchiver::SwitchToFirstVol(%ls)", arcName);
    if (arcName == NULL)
        return FALSE;
    ArcFileName = arcName;
    const size_t separator = ArcFileName.find_last_of(L"\\/");
    const size_t extensionPos = ArcFileName.find_last_of(L'.');
    if (extensionPos != std::wstring::npos &&
        (separator == std::wstring::npos || extensionPos > separator) &&
        ArcFileName.size() - extensionPos > 3 &&
        ArcFileName[extensionPos + 2] >= L'0' && ArcFileName[extensionPos + 2] <= L'9' &&
        ArcFileName[extensionPos + 3] >= L'0' && ArcFileName[extensionPos + 3] <= L'9')
    {
        const std::wstring originalName = ArcFileName;
        ArcFileName.replace(extensionPos, std::wstring::npos, L".arj");
        DWORD attr = SalamanderGeneral->SalGetFileAttributes(ArcFileName.c_str());
        if (attr == -1 || attr & FILE_ATTRIBUTE_DIRECTORY)
            ArcFileName = originalName;
    }
    return TRUE;
}

BOOL CPluginInterfaceForArchiver::MakeFilesList(TIndirectArray2<std::wstring>& files, SalEnumSelection next, void* nextParam,
                                                const wchar_t* archiveRoot, const wchar_t* targetDir)
{
    CALL_STACK_MESSAGE3("CPluginInterfaceForArchiver::MakeFilesList(, , , %ls, %ls)", archiveRoot, targetDir);
    const wchar_t* nextName;
    BOOL isDir;
    CQuadWord size;
    int errorOccured;

    ProgressTotal = CQuadWord(0, 0);
    while ((nextName = next(SalamanderGeneral->GetMsgBoxParent(), 1, &isDir, &size, NULL, nextParam, &errorOccured)) != NULL)
    {
        if (isDir)
        {
            std::wstring dir(targetDir);
            SPLSalPathAppendOwned(dir, nextName);
            BOOL skip;
            if (SalamanderSafeFile->SafeFileCreate(dir.c_str(), 0, 0, 0, TRUE, SalamanderGeneral->GetMsgBoxParent(), NULL, NULL,
                                                   &Silent, TRUE, &skip, NULL, 0, NULL, NULL) == INVALID_HANDLE_VALUE &&
                !skip)
            {
                return FALSE;
            }
        }
        else
        {
            std::wstring* selectedName = new std::wstring(archiveRoot);
            if (!selectedName)
                return Error(IDS_LOWMEM);
            if (!selectedName->empty() && selectedName->back() != L'\\')
                selectedName->push_back(L'\\');
            selectedName->append(nextName);
            if (!files.Add(selectedName))
            {
                delete selectedName;
                return Error(IDS_LOWMEM);
            }
            ProgressTotal += size;
        }
    }
    return errorOccured != SALENUM_CANCEL && // check that no error occurred and the user did not request to cancel the operation (Cancel button)
           SalamanderGeneral->TestFreeSpace(SalamanderGeneral->GetMsgBoxParent(), targetDir, ProgressTotal,
                                            SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_PLUGINNAME).c_str());
}

BOOL CPluginInterfaceForArchiver::DoThisFile(CARJHeaderData* hdr, const wchar_t* arcName, const wchar_t* targetDir)
{
    CALL_STACK_MESSAGE3("CPluginInterfaceForArchiver::DoThisFile(, %ls, %ls)", arcName,
                        targetDir);
    std::wstring message = SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_EXTRACTING);

    message.append(hdr->FileName);
    if (UnPackWholeArchive)
        Salamander->ProgressDialogAddText(message.c_str(), TRUE);
    else
        Salamander->ProgressDialogAddText(message.c_str(), TRUE);
    if (hdr->Flags & FF_EXTFILE)
    {
        TRACE_IW(L"Skipping file continued from previous volume: " << hdr->FileName.c_str());
        if (!(Options & OP_SKIPCONTINUED) && ContinuedFileDialog(SalamanderGeneral->GetMsgBoxParent(), hdr->FileName.c_str()) != IDOK)
            Abort = TRUE;
        return FALSE;
    }
    if (RootLen > hdr->FileName.size())
    {
        Abort = TRUE;
        return FALSE;
    }
    TargetName = targetDir;
    SPLSalPathAppendOwned(TargetName, hdr->FileName.c_str() + RootLen);
    std::wstring nameInArc(arcName);
    SPLSalPathAppendOwned(nameInArc, hdr->FileName.c_str());
    const std::wstring info = GetInfo(&hdr->Time, hdr->Size);
    BOOL skip;
    CQuadWord q = CQuadWord(hdr->Size, 0);
    BOOL allocate = AllocateWholeFile &&
                    CQuadWord(2, 0) < q && q < CQuadWord(0, 0x80000000);
    if (TestAllocateWholeFile)
        q += CQuadWord(0, 0x80000000);
    TargetFile = SalamanderSafeFile->SafeFileCreate(TargetName.c_str(), GENERIC_WRITE, FILE_SHARE_READ,
                                                    hdr->Attr & ~FILE_ATTRIBUTE_READONLY | FILE_FLAG_SEQUENTIAL_SCAN,
                                                    hdr->Attr & FILE_ATTRIBUTE_DIRECTORY || hdr->FileType == FT_DIRECTORY,
                                                    SalamanderGeneral->GetMsgBoxParent(), nameInArc.c_str(), info.c_str(), &Silent, TRUE, &skip, NULL, 0,
                                                    allocate ? &q : NULL, NULL);
    if (skip)
        return FALSE;
    if (TargetFile == INVALID_HANDLE_VALUE)
    {
        Abort = TRUE;
        return FALSE;
    }
    if (q == CQuadWord(0, 0x80000000))
    {
        // allocation failed and we will not try again
        AllocateWholeFile = false;
        TestAllocateWholeFile = false;
    }
    else if (q == CQuadWord(0, 0x00000000))
    {
        // allocation failed, but we'll try again next time
    }
    else
    {
        // allocation succeeded
        TestAllocateWholeFile = false;
    }
    if (!Salamander->ProgressSetSize(CQuadWord(0, 0), CQuadWord(-1, -1), TRUE))
    {
        CloseHandle(TargetFile);
        Abort = TRUE;
        return FALSE;
    }
    //if (hdr->Attr & FILE_ATTRIBUTE_DIRECTORY) hdr->Size = 1;
    //Salamander->ProgressSetTotalSize(hdr->Size, ProgressTotal);
    return TRUE;
}

BOOL CPluginInterfaceForArchiver::ConstructMaskArray(TIndirectArray2<std::wstring>& maskArray, const wchar_t* masks)
{
    CALL_STACK_MESSAGE2("CPluginInterfaceForArchiver::ConstructMaskArray(, %ls)", masks);
    for (const std::wstring& mask : SplitArjMasks(masks))
    {
        std::wstring* newMask = new std::wstring(
            SPLPrepareMaskOwned(SalamanderGeneral, mask.c_str()));
        if (!newMask)
        {
            SalamanderGeneral->ShowMessageBox(SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_LOWMEM).c_str(),
                                              SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_PLUGINNAME).c_str(), MSGBOX_ERROR);
            return FALSE;
        }
        if (!maskArray.Add(newMask))
        {
            delete newMask;
            SalamanderGeneral->ShowMessageBox(SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_LOWMEM).c_str(),
                                              SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_PLUGINNAME).c_str(), MSGBOX_ERROR);
            return FALSE;
        }
    }
    return TRUE;
}

// ****************************************************************************

static std::wstring FormatLocalDateOrTime(const SYSTEMTIME& time, bool date)
{
    const int required = date ? GetDateFormatW(LOCALE_USER_DEFAULT, DATE_SHORTDATE, &time, NULL, NULL, 0)
                              : GetTimeFormatW(LOCALE_USER_DEFAULT, 0, &time, NULL, NULL, 0);
    if (required > 1)
    {
        std::wstring result(static_cast<size_t>(required), L'\0');
        const int written = date ? GetDateFormatW(LOCALE_USER_DEFAULT, DATE_SHORTDATE, &time, NULL, result.data(), required)
                                 : GetTimeFormatW(LOCALE_USER_DEFAULT, 0, &time, NULL, result.data(), required);
        if (written == required)
        {
            result.resize(static_cast<size_t>(written - 1));
            return result;
        }
    }
    if (date)
        return std::to_wstring(time.wDay) + L"." + std::to_wstring(time.wMonth) + L"." + std::to_wstring(time.wYear);
    return std::to_wstring(time.wHour) + L":" + (time.wMinute < 10 ? L"0" : L"") + std::to_wstring(time.wMinute) +
           L":" + (time.wSecond < 10 ? L"0" : L"") + std::to_wstring(time.wSecond);
}

std::wstring GetInfo(const FILETIME* lastWrite, unsigned size)
{
    CALL_STACK_MESSAGE2("GetInfo(, 0x%X)", size);
    SYSTEMTIME st;
    FILETIME ft;
    FileTimeToLocalFileTime(lastWrite, &ft);
    FileTimeToSystemTime(&ft, &st);

    const std::wstring date = FormatLocalDateOrTime(st, true);
    const std::wstring time = FormatLocalDateOrTime(st, false);
    const std::wstring number = SPLNumberToStrOwned(SalamanderGeneral, CQuadWord(size, 0));
    return number + L", " + date + L", " + time;
}
