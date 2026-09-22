// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"

#include <stdlib.h>
#include "unicode/helpers.h"
#include "unrar_callback_contract.h"
#include "unrar_name_narrow.h"

#define UNRAR_WIDEN_IMPL(value) L##value
#define UNRAR_WIDEN(value) UNRAR_WIDEN_IMPL(value)

// ****************************************************************************

HINSTANCE DLLInstance = NULL; // handle to SPL - language-independent resources
HINSTANCE HLanguage = NULL;   // handle to SLG - language-dependent resources

// plugin interface object whose methods are called from Salamander
CPluginInterface PluginInterface;
// part of the CPluginInterface interface for the archiver
CPluginInterfaceForArchiver InterfaceForArchiver;

// general Salamander interface - valid from startup until the plugin shuts down
CSalamanderGeneralAbstract* SalamanderGeneral = NULL;

// interface for convenient work with files
CSalamanderSafeFileAbstract* SalamanderSafeFile = NULL;

// variable definition for "dbg.h"
CSalamanderDebugAbstract* SalamanderDebug = NULL;

struct CConfiguration Config;

const SYSTEMTIME MinTime = {1980, 01, 2, 01, 00, 00, 00, 000};

const wchar_t* CONFIG_OPTIONS = L"Options";

const wchar_t* CONFIG_LIST_INFO_PACKED_SIZE = L"List Info Packed Size";
const wchar_t* CONFIG_COL_PACKEDSIZE_FIXEDWIDTH = L"Column PackedSize FixedWidth";
const wchar_t* CONFIG_COL_PACKEDSIZE_WIDTH = L"Column PackedSize Width";

static WCHAR* DupWideStringLocal(const WCHAR* text)
{
    if (text == NULL || *text == L'\0')
        return NULL;

    int chars = lstrlenW(text) + 1;
    WCHAR* result = (WCHAR*)malloc((size_t)chars * sizeof(WCHAR));
    if (result == NULL)
        return NULL;

    memcpy(result, text, (size_t)chars * sizeof(WCHAR));
    return result;
}

static int CompareArchiveName(const WCHAR* leftW, const char* rightA, const WCHAR* rightW)
{
    if (rightW != NULL && *rightW != L'\0')
        return CompareStringW(LOCALE_USER_DEFAULT, NORM_IGNORECASE, leftW, -1, rightW, -1);

    std::wstring rightFallbackW;
    if (!DecodeRarLegacyName(rightA, rightFallbackW))
        return 0;
    return CompareStringW(LOCALE_USER_DEFAULT, NORM_IGNORECASE, leftW, -1, rightFallbackW.c_str(), -1);
}

CRARFileData::CRARFileData(QWORD qwPackedSize, int nItem, const WCHAR* fileNameW)
    : PackedSize(qwPackedSize), ItemNumber(nItem), FileNameW(DupWideStringLocal(fileNameW))
{
}

CRARFileData::~CRARFileData()
{
    free(FileNameW);
    FileNameW = NULL;
}

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

std::wstring LangStr(int resID)
{
    return SPLLoadStrOwned(SalamanderGeneral, HLanguage, resID);
}

// Defined next to RARCallback, which is the reason it needs a narrow form at all.
static void RARSetPasswordNarrow(HANDLE arcHandle, const wchar_t* password);

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

    // this plugin is built for the current Salamander version and newer - perform a check
    if (salamander->GetVersion() < LAST_VERSION_OF_SALAMANDER)
    { // reject older versions
        MessageBoxW(salamander->GetParentWindow(),
                    UNRAR_WIDEN(REQUIRE_LAST_VERSION_OF_SALAMANDER),
                    L"UnRAR" /* neprekladat! */, MB_OK | MB_ICONERROR);
        return NULL;
    }

    // load the language module (.slg)
    HLanguage = salamander->LoadLanguageModule(salamander->GetParentWindow(), L"UnRAR" /* neprekladat! */);
    if (HLanguage == NULL)
        return NULL;

    // obtain the general Salamander interface
    SalamanderGeneral = salamander->GetSalamanderGeneral();
    SalamanderSafeFile = salamander->GetSalamanderSafeFile();

    if (!InterfaceForArchiver.Init())
        return NULL;

    // set basic information about the plugin
    salamander->SetBasicPluginData(SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_PLUGINNAME).c_str(),
                                   FUNCTION_PANELARCHIVERVIEW | FUNCTION_CUSTOMARCHIVERUNPACK |
                                       FUNCTION_CONFIGURATION | FUNCTION_LOADSAVECONFIGURATION,
                                   UNRAR_WIDEN(VERSINFO_VERSION_NO_PLATFORM),
                                   UNRAR_WIDEN(VERSINFO_COPYRIGHT),
                                   SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_PLUGIN_DESCRIPTION).c_str(),
                                   L"UnRAR" /* neprekladat! */, L"rar");

    salamander->SetPluginHomePageURL(L"https://github.com/0xeb/sally");

    return &PluginInterface;
}

// ****************************************************************************
//
// CPluginInterface
//

void CPluginInterface::About(HWND parent)
{
    const std::wstring message = SPLFormatStringOwned(
        L"%ls %ls\n\n%ls\n\n%ls",
        SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_PLUGINNAME).c_str(),
        UNRAR_WIDEN(VERSINFO_VERSION), UNRAR_WIDEN(VERSINFO_COPYRIGHT),
        SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_PLUGIN_DESCRIPTION).c_str());
    SalamanderGeneral->SalMessageBox(parent, message.c_str(),
                                     SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_ABOUT).c_str(),
                                     MB_OK | MB_ICONINFORMATION);
}

BOOL CPluginInterface::Release(HWND parent, BOOL force)
{
    CALL_STACK_MESSAGE2("CPluginInterface::Release(, %d)", force);
    return TRUE;
}

void CPluginInterface::LoadConfiguration(HWND parent, HKEY regKey, CSalamanderRegistryAbstract* registry)
{
    CALL_STACK_MESSAGE1("CPluginInterface::LoadConfiguration(, ,)");
    memset(&Config, 0, sizeof(Config));
    Config.ListInfoPackedSize = TRUE;

    if (regKey != NULL) // load z registry
    {
        registry->GetValue(regKey, CONFIG_OPTIONS, REG_DWORD, &Config.Options, sizeof(DWORD));
        Config.Options &= OP_SAVED_IN_REGISTRY;
        registry->GetValue(regKey, CONFIG_LIST_INFO_PACKED_SIZE, REG_DWORD, &Config.ListInfoPackedSize, sizeof(DWORD));
        registry->GetValue(regKey, CONFIG_COL_PACKEDSIZE_FIXEDWIDTH, REG_DWORD, &Config.ColumnPackedSizeFixedWidth, sizeof(DWORD));
        registry->GetValue(regKey, CONFIG_COL_PACKEDSIZE_WIDTH, REG_DWORD, &Config.ColumnPackedSizeWidth, sizeof(DWORD));
    }
}

void CPluginInterface::SaveConfiguration(HWND parent, HKEY regKey, CSalamanderRegistryAbstract* registry)
{
    CALL_STACK_MESSAGE1("CPluginInterface::SaveConfiguration(, ,)");

    Config.Options &= OP_SAVED_IN_REGISTRY;
    registry->SetValue(regKey, CONFIG_OPTIONS, REG_DWORD, &Config.Options, sizeof(DWORD));
    registry->SetValue(regKey, CONFIG_LIST_INFO_PACKED_SIZE, REG_DWORD, &Config.ListInfoPackedSize, sizeof(DWORD));
    registry->SetValue(regKey, CONFIG_COL_PACKEDSIZE_FIXEDWIDTH, REG_DWORD, &Config.ColumnPackedSizeFixedWidth, sizeof(DWORD));
    registry->SetValue(regKey, CONFIG_COL_PACKEDSIZE_WIDTH, REG_DWORD, &Config.ColumnPackedSizeWidth, sizeof(DWORD));
}

void CPluginInterface::Configuration(HWND parent)
{
    CALL_STACK_MESSAGE1("CPluginInterface::Configuration()");
    if (IDOK == ConfigDialog(parent))
    {
        if (SalamanderGeneral->GetPanelPluginData(PANEL_LEFT) != NULL)
            SalamanderGeneral->PostRefreshPanelPath(PANEL_LEFT);
        if (SalamanderGeneral->GetPanelPluginData(PANEL_RIGHT) != NULL)
            SalamanderGeneral->PostRefreshPanelPath(PANEL_RIGHT);
    }
}

void CPluginInterface::Connect(HWND parent, CSalamanderConnectAbstract* salamander)
{
    CALL_STACK_MESSAGE1("CPluginInterface::Connect(,)");

    salamander->AddCustomUnpacker(L"UnRAR (Plugin)", L"*.rar;*.r##", FALSE);
    salamander->AddPanelArchiver(L"rar;r##", FALSE, FALSE);
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

BOOL CPluginInterfaceForArchiver::ListArchive(CSalamanderForOperationsAbstract* salamander, const wchar_t* fileName,
                                              CSalamanderDirectoryAbstract* dir,
                                              CPluginDataInterfaceAbstract*& pluginData)
{
    CALL_STACK_MESSAGE1("CPluginInterfaceForArchiver::ListArchive()");
    Salamander = salamander;
    List = TRUE;
    BOOL ret = TRUE;
    Abort = FALSE;
    NotWholeArchListed = FALSE;
    int count = 0;

    BOOL saveFirstVolume = FALSE;
    if (!SwitchToFirstVol(fileName, &saveFirstVolume))
        return FALSE;

    pluginData = PluginData = new CPluginDataInterface(saveFirstVolume ? _wcsdup(ArcFileName.c_str()) : NULL);

    int sortByExtDirsAsFiles;
    SalamanderGeneral->GetConfigParameter(SALCFG_SORTBYEXTDIRSASFILES, &sortByExtDirsAsFiles,
                                          sizeof(sortByExtDirsAsFiles), NULL);

    if (OpenArchive())
    {
        CFileHeader header;
        CFileData fileData;
        WCHAR* slash;
        const WCHAR* path;
        WCHAR* name;

        while ((ret = ReadHeader(&header)) != 0 && *header.FileName)
        {
            if (header.Flags & RHDF_SPLITBEFORE)
                NotWholeArchListed = TRUE;
            path = header.FileNameW;
            name = header.FileNameW;
            slash = wcsrchr(header.FileNameW, L'\\');
            if (slash)
            {
                *slash = L'\0';
                name = slash + 1;
            }
            else
                path = L"";
            fileData.NameLen = lstrlenW(name);
            fileData.Name = (wchar_t*)SalamanderGeneral->Alloc((fileData.NameLen + 1) * sizeof(wchar_t));
            if (!fileData.Name)
            {
                SalamanderGeneral->ShowMessageBox(SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_LOWMEM).c_str(), SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_PLUGINNAME).c_str(), MSGBOX_ERROR);
                ret = FALSE;
                break;
            }
            lstrcpyW(fileData.Name, name);
            fileData.Ext = wcsrchr(fileData.Name, L'.');
            if (fileData.Ext)
                fileData.Ext++; // ".cvspass" is considered an extension on Windows
            else
                fileData.Ext = fileData.Name + fileData.NameLen;
            fileData.Size = header.Size;
            fileData.Attr = header.Attr;
            if (header.Flags & RHDF_ENCRYPTED)
            {
                fileData.Attr |= FILE_ATTRIBUTE_ENCRYPTED;
                if (header.Flags & RHDF_SOLID)
                    PluginData->SolidEncrypted = 1;
            }
            fileData.Hidden = fileData.Attr & FILE_ATTRIBUTE_HIDDEN ? 1 : 0;
            if (slash)
                *slash = L'\\';
            fileData.PluginData = (DWORD_PTR) new CRARFileData(header.CompSize.Value, count, header.FileNameW);
            if (slash)
                *slash = L'\0';
            fileData.LastWrite = header.Time;
            fileData.DosName = NULL;
            fileData.IsOffline = 0;
            if (header.Flags & RHDF_DIRECTORY)
            {
                if (!sortByExtDirsAsFiles)
                    fileData.Ext = fileData.Name + fileData.NameLen; // directories have no extensions
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
                if (slash)
                    *slash = L'\\';
                delete (CRARFileData*)fileData.PluginData;
                SalamanderGeneral->Free(fileData.Name);
                SalamanderGeneral->ShowMessageBox(SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_LIST).c_str(), SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_PLUGINNAME).c_str(), MSGBOX_ERROR);
                break;
            }
            if (slash)
                *slash = L'\\';
            //if (!ProcessFile(OP_SKIP, "", header.FileName)) */
            if (!ProcessFile(RAR_SKIP, header.FileNameW))
            {
                if (Abort)
                    ret = FALSE;
                break;
            }
            count++;
        }

        RARCloseArchive(ArcHandle);
        ArcHandle = NULL;
    }
    else
        ret = FALSE;

    if (NotWholeArchListed && !(Config.Options & OP_NO_VOL_ATTENTION))
        AttentionDialog(SalamanderGeneral->GetMainWindowHWND());

    // some files have already been listed, so do not pack and display them
    if (!ret && count)
        ret = TRUE;

    Salamander = NULL;
    if (!ret)
        delete pluginData;

    return ret;
}

void FreeString(void* strig)
{
    CALL_STACK_MESSAGE_NONE
    free(strig);
}

BOOL CPluginInterfaceForArchiver::SetSolidPassword()
{
    CALL_STACK_MESSAGE1("CPluginInterfaceForArchiver::SetSolidPassword()");
    if (!(PluginData->Silent & SF_ALLENRYPT))
    {
        if (PasswordDialog(SalamanderGeneral->GetMsgBoxParent(), ArcFileName.c_str(), PluginData->Password,
                           PD_NOSKIP | PD_NOSKIPALL | PD_NOALL) != IDCANCEL)
        {
            PluginData->Silent |= SF_ALLENRYPT;
        }
        else
            return FALSE;
    }
    RARSetPasswordNarrow(ArcHandle, PluginData->Password);
    return TRUE;
}

BOOL CPluginInterfaceForArchiver::UnpackArchive(CSalamanderForOperationsAbstract* salamander, const wchar_t* fileName,
                                                CPluginDataInterfaceAbstract* pluginDataPar, const wchar_t* targetDir,
                                                const wchar_t* archiveRoot, SalEnumSelection next, void* nextParam)
{
    CALL_STACK_MESSAGE1("CPluginInterfaceForArchiver::UnpackArchive()");

    Salamander = salamander;
    List = FALSE;
    BOOL ret = TRUE;
    Abort = FALSE;
    //  FirstFile = TRUE;
    AllocateWholeFile = TRUE;
    TestAllocateWholeFile = TRUE;
    ArcRoot = archiveRoot ? archiveRoot : L"";
    if (*ArcRoot == L'\\')
        ArcRoot++;
    RootLenW = lstrlenW(ArcRoot);
    TIndirectArray2<CRARExtractInfo> files(256);
    PluginData = (CPluginDataInterface*)pluginDataPar;

    // extract files

    // check whether plugin data already stores the first volume
    const CFileData* fd;
    DWORD attr;
    const wchar_t* fv;
    if (next(NULL /* we do not want any prompts */, 1, NULL, NULL, &fd, nextParam, NULL) &&
        (fv = PluginData->GetFirstVolume()) != NULL &&
        (attr = SalamanderGeneral->SalGetFileAttributes(fv)) != -1 &&
        (attr & FILE_ATTRIBUTE_DIRECTORY) == 0)
    {
        ArcFileName = fv;
    }
    else
    {
        SwitchToFirstVol(fileName);
    }

    next(NULL, -1, NULL, NULL, NULL, nextParam, NULL);

    /*  if (PluginData->SolidEncrypted) // The archive is not opened yet
    if (!SetSolidPassword())
      return FALSE;*/

    const std::wstring title = SPLFormatStringOwned(
        SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_EXTRPROGTITLE).c_str(),
        SalamanderGeneral->SalPathFindFileName(ArcFileName.c_str()));
    Salamander->OpenProgressDialog(title.c_str(), TRUE, NULL, FALSE);
    Salamander->ProgressDialogAddText(SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_PREPAREDATA).c_str(), FALSE);

    PluginData->Silent &= SF_ALLENRYPT;

    ret = OpenArchive();
    if (ret)
    {
        ret = MakeFilesList(files, next, nextParam, targetDir);
        if (ret)
        {
            CFileHeader header;
            int op, count = 0;
            BOOL match;
            CQuadWord currentProgress = CQuadWord(0, 0);
            Salamander->ProgressDialogAddText(SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_EXTRACTFILES).c_str(), FALSE);
            while (files.Count && (ret = ReadHeader(&header)) != 0 && *header.FileName)
            {
                op = RAR_SKIP;
                match = FALSE;
                TargetFile = INVALID_HANDLE_VALUE;
                /*if ((header.Flags & RHDF_SOLID) && FirstFile && (header.Flags & RHDF_ENCRYPTED))
        {
          if (PluginData->Silent & SF_ENCRYPTED) goto UA_NEXT;
          if (!(PluginData->Silent & SF_ALLENRYPT))
          {
            switch (PasswordDialog(SalamanderGeneral->GetMsgBoxParent(), header.FileName, PluginData->Password, PD_NOSKIP))
            {
              case IDALL: PluginData->Silent |= SF_ALLENRYPT;
              case IDOK:  break;
              case IDSKIPALL: PluginData->Silent |= SF_ENCRYPTED; goto UA_NEXT;
              default: Abort = TRUE; goto UA_NEXT;
            }
          }
          RARSetPasswordNarrow(ArcHandle, PluginData->Password);
        }*/
                int i;
                for (i = 0; i < files.Count; i++)
                {
                    if (!(header.Flags & RHDF_DIRECTORY) &&
                        (CompareStringW(LOCALE_USER_DEFAULT, NORM_IGNORECASE, header.FileNameW, -1, files[i]->FileNameW.c_str(), -1) == CSTR_EQUAL) &&
                        (files[i]->ItemNumber == count))
                    {
                        match = TRUE;
                        files.Delete(i);
                        Salamander->ProgressSetTotalSize(header.Size, ProgressTotal);
                        if (!Salamander->ProgressSetSize(CQuadWord(0, 0), CQuadWord(-1, -1), TRUE))
                        {
                            Abort = TRUE;
                            break;
                        }
                        if (DoThisFile(&header, fileName, targetDir))
                            op = RAR_TEST;
                        break;
                    }
                }

                //UA_NEXT:
                ret = ProcessFile(op, header.FileNameW);
                if (op == RAR_TEST)
                {
                    SetFileTime(TargetFile, NULL, NULL, &header.Time);
                    CloseHandle(TargetFile);
                    if (!ret)
                        DeleteTargetFile();
                    else
                        SetTargetAttributes(header.Attr);
                }
                if (Abort)
                {
                    ret = FALSE;
                    break;
                }
                if (match)
                {
                    currentProgress += header.Size;
                    if (!Salamander->ProgressSetSize(header.Size, currentProgress, TRUE))
                    {
                        ret = FALSE;
                        break;
                    }
                }
                if (!(header.Flags & RHDF_SPLITBEFORE))
                {
                    // Don't count the same file twice
                    count++;
                }
                //        if (!(header.Flags & RHDF_DIRECTORY)) FirstFile = FALSE;
            }
        }
        Config.Options &= ~OP_SKITHISFILE;
        RARCloseArchive(ArcHandle);
        ArcHandle = NULL;
    }

    Salamander->CloseProgressDialog();

    Salamander = NULL;

    return ret;
}

static void DestroyIllegalCharsW(WCHAR* pszPath)
{
    CALL_STACK_MESSAGE1("DestroyIllegalCharsW()");
    while (*pszPath)
    {
        if (wcschr(L"*?<>|\":", *pszPath) != NULL)
            *pszPath = L'_';
        pszPath++;
    }
}

BOOL CPluginInterfaceForArchiver::BuildTargetName(CFileHeader* header, const wchar_t* targetDir, const wchar_t* relativeName)
{
    TargetName = targetDir;
    SPLSalPathAppendOwned(TargetName, relativeName);
    if (TargetName.size() > 2)
        DestroyIllegalCharsW(TargetName.data() + 2);
    return TRUE;
}

HANDLE CPluginInterfaceForArchiver::CreateTargetFile(DWORD desiredAccess, DWORD shareMode, DWORD flagsAndAttributes, BOOL isDir,
                                                     const wchar_t* sourceName, const wchar_t* sourceInfo, BOOL* skipped, CQuadWord* allocateWholeFile)
{
    return SalamanderSafeFile->SafeFileCreate(TargetName.c_str(), desiredAccess, shareMode, flagsAndAttributes, isDir,
                                              SalamanderGeneral->GetMsgBoxParent(), sourceName, sourceInfo,
                                              &PluginData->Silent, TRUE, skipped, NULL, 0,
                                              allocateWholeFile, NULL);
}

void CPluginInterfaceForArchiver::DeleteTargetFile()
{
    DeleteFileW(TargetName.c_str());
}

void CPluginInterfaceForArchiver::SetTargetAttributes(DWORD attributes)
{
    SetFileAttributesW(TargetName.c_str(), attributes);
}

BOOL CPluginInterfaceForArchiver::UnpackOneFile(CSalamanderForOperationsAbstract* salamander,
                                                const wchar_t* fileName, CPluginDataInterfaceAbstract* pluginDataPar,
                                                const wchar_t* nameInArchive, const CFileData* fileData,
                                                const wchar_t* targetDir, const wchar_t* newFileName,
                                                BOOL* renamingNotSupported)
{
    CALL_STACK_MESSAGE1("CPluginInterfaceForArchiver::UnpackOneFile()");

    if (newFileName != NULL)
    {
        *renamingNotSupported = TRUE;
        return FALSE;
    }

    Salamander = salamander;
    BOOL ret = FALSE;
    ArcRoot = L"";
    RootLenW = Abort = 0;
    //  FirstFile = TRUE;
    AllocateWholeFile = TRUE;
    TestAllocateWholeFile = TRUE;
    PluginData = (CPluginDataInterface*)pluginDataPar;

    // extract files
    List = FALSE;

    // check whether plugin data already stores the first volume
    DWORD attr;
    const wchar_t* fv;
    if (fileData &&
        (fv = PluginData->GetFirstVolume()) != NULL &&
        (attr = SalamanderGeneral->SalGetFileAttributes(fv)) != -1 &&
        (attr & FILE_ATTRIBUTE_DIRECTORY) == 0)
    {
        ArcFileName = fv;
    }
    else
    {
        SwitchToFirstVol(fileName);
    }

    PluginData->Silent &= SF_ALLENRYPT;

    if (OpenArchive())
    {
        if (PluginData->SolidEncrypted)
            if (!SetSolidPassword())
            {
                RARCloseArchive(ArcHandle);
                ArcHandle = NULL;
                return FALSE;
            }

        CFileHeader header;
        int op, count = 0;
        BOOL match = FALSE;
        CRARFileData* rarFileData = (CRARFileData*)fileData->PluginData;
        const wchar_t* selectedNameW = rarFileData != NULL && rarFileData->FileNameW != NULL
                                           ? rarFileData->FileNameW
                                           : nameInArchive;
        while (ReadHeader(&header) && *header.FileName)
        {
            TargetFile = INVALID_HANDLE_VALUE;
            op = RAR_SKIP;
            /*if ((header.Flags & RHDF_SOLID) && FirstFile && (header.Flags & RHDF_ENCRYPTED))
      {
        if (PluginData->Silent & SF_ENCRYPTED) goto UOF_NEXT;
        if (!(PluginData->Silent & SF_ALLENRYPT))
        {
          switch (PasswordDialog(SalamanderGeneral->GetMsgBoxParent(), header.FileName, PluginData->Password, PD_NOSKIP))
          {
            case IDALL: PluginData->Silent |= SF_ALLENRYPT;
            case IDOK:  break;
            case IDSKIPALL: PluginData->Silent |= SF_ENCRYPTED; goto UOF_NEXT;
            default: Abort = TRUE; goto UOF_NEXT;
          }
        }
        RARSetPasswordNarrow(ArcHandle, PluginData->Password);
            }*/
            if (!(header.Flags & RHDF_DIRECTORY) &&
                (CompareArchiveName(selectedNameW, header.FileName, header.FileNameW) == CSTR_EQUAL) &&
                (!rarFileData || (count == rarFileData->ItemNumber)))
            {
                match = TRUE;
                if (header.Flags & RHDF_SPLITBEFORE)
                {
                    ContinuedFileDialog(SalamanderGeneral->GetMsgBoxParent(), header.FileNameW);
                    goto UOF_NEXT;
                }
                std::wstring justName(selectedNameW);
                SPLSalPathStripPathOwned(SalamanderGeneral, justName);
                DestroyIllegalCharsW(justName.data());
                if (!BuildTargetName(&header, targetDir, justName.c_str()))
                {
                    Error(IDS_TOOLONGNAME);
                    goto UOF_NEXT;
                }
                if (!(header.Flags & RHDF_SOLID) && (header.Flags & RHDF_ENCRYPTED))
                {
                    if (!(PluginData->Silent & SF_ALLENRYPT))
                    {
                        switch (PasswordDialog(SalamanderGeneral->GetMsgBoxParent(), header.FileNameW, PluginData->Password, PD_NOSKIP | PD_NOSKIPALL))
                        {
                        case IDALL:
                            PluginData->Silent |= SF_ALLENRYPT;
                        case IDOK:
                            break;
                        default:
                            goto UOF_NEXT;
                        }
                    }
                    RARSetPasswordNarrow(ArcHandle, PluginData->Password);
                }
                wchar_t buf[100];
                GetInfo(buf, _countof(buf), &header.Time, header.Size);
                BOOL skip;
                CQuadWord q = header.Size;
                bool allocate = CQuadWord(2, 0) < q && q < CQuadWord(0, 0x80000000);
                q += CQuadWord(0, 0x80000000);
                TargetFile = CreateTargetFile(GENERIC_WRITE, FILE_SHARE_READ,
                                              header.Attr & ~FILE_ATTRIBUTE_READONLY | FILE_FLAG_SEQUENTIAL_SCAN,
                                              FALSE, selectedNameW, buf, &skip, allocate ? &q : NULL);
                if (TargetFile != INVALID_HANDLE_VALUE)
                    op = RAR_TEST;
            }

        UOF_NEXT:
            int r;
            r = ProcessFile(op, header.FileNameW);
            if (op == RAR_TEST)
            {
                SetFileTime(TargetFile, NULL, NULL, &header.Time);
                CloseHandle(TargetFile);
                if (r)
                {
                    SetTargetAttributes(header.Attr);
                    ret = TRUE;
                }
                else
                    DeleteTargetFile();
            }
            if (match || !r)
                break;
            if (!(header.Flags & RHDF_SPLITBEFORE))
            {
                // Don't count the same file twice
                count++;
            }
            //      if (!(header.Flags & RHDF_DIRECTORY)) FirstFile = FALSE;
        }
        Config.Options &= ~OP_SKITHISFILE;
        RARCloseArchive(ArcHandle);
        ArcHandle = NULL;
    }

    Salamander = NULL;
    if (!ret)
    {
        SalamanderGeneral->ShowMessageBox(LangStr(IDS_FILENOTFOUND).c_str(), LangStr(IDS_PLUGINNAME).c_str(), MSGBOX_ERROR);
    }

    return ret;
}

BOOL CPluginInterfaceForArchiver::UnpackWholeArchive(CSalamanderForOperationsAbstract* salamander, const wchar_t* fileName,
                                                     const wchar_t* mask, const wchar_t* targetDir, BOOL delArchiveWhenDone,
                                                     CDynamicString* archiveVolumes)
{
    CALL_STACK_MESSAGE2("CPluginInterfaceForArchiver::UnpackWholeArchive(, , , , %d,)", delArchiveWhenDone);

    BOOL ret = TRUE;
    Abort = FALSE;
    ArcRoot = L"";
    RootLenW = 0;
    TIndirectArray2<wchar_t> masks(16);

    if (!ConstructMaskArray(masks, mask) || masks.Count == 0)
        return FALSE;

    // extract files
    if (!SwitchToFirstVol(fileName))
    {
        return FALSE;
    }

    PluginData = new CPluginDataInterface;

    Salamander = salamander;
    BOOL haveTotalProgress = UnpackWholeArchiveCalculateProgress(masks);
    if (Abort)
    {
        Salamander = NULL;
        delete PluginData;
        return FALSE;
    }

    List = FALSE;
    //  FirstFile = TRUE;
    AllocateWholeFile = TRUE;
    TestAllocateWholeFile = TRUE;

    // Petr: cannot be placed before calling UnpackWholeArchiveCalculateProgress,
    // otherwise the archive volume names are collected twice (except for the first volume,
    // which will appear only once)
    if (delArchiveWhenDone)
        archiveVolumes->Add(ArcFileName.c_str(), -2);
    if (ArchiveVolumes != NULL)
        TRACE_E("CPluginInterfaceForArchiver::UnpackWholeArchive(): unexpected situation: ArchiveVolumes is not NULL!");
    ArchiveVolumes = delArchiveWhenDone ? archiveVolumes : NULL;

    const std::wstring title = SPLFormatStringOwned(
        SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_EXTRPROGTITLE).c_str(),
        SalamanderGeneral->SalPathFindFileName(ArcFileName.c_str()));
    Salamander->OpenProgressDialog(title.c_str(), haveTotalProgress, NULL, TRUE);
    Salamander->ProgressDialogAddText(SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_PREPAREDATA).c_str(), FALSE);
    if (haveTotalProgress)
        Salamander->ProgressSetTotalSize(CQuadWord(-1, -1), ProgressTotal);

    ret = OpenArchive();
    if (ret)
    {
        CFileHeader header;
        CQuadWord currentProgress = CQuadWord(0, 0);
        Salamander->ProgressDialogAddText(SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_EXTRACTFILES).c_str(), FALSE);
        while ((ret = ReadHeader(&header)) != 0 && *header.FileName)
        {
            if (!(header.Flags & RHDF_SPLITBEFORE))
            {
                // This means that the file is supposed to be skipped.
                // Extracting a file spanning multiple volumes is done at once and a fragment with RHDF_SPLITBEFORE is never met
                //        continue;
                Config.Options &= ~OP_SKITHISFILE;
            }
            int op = RAR_SKIP;
            BOOL match = FALSE;
            TargetFile = INVALID_HANDLE_VALUE;
            /*if ((header.Flags & RHDF_SOLID) && FirstFile && (header.Flags & RHDF_ENCRYPTED))
      {
        if (PluginData->Silent & SF_ENCRYPTED) goto UWA_NEXT;
        if (!(PluginData->Silent & SF_ALLENRYPT))
        {
          switch (PasswordDialog(SalamanderGeneral->GetMsgBoxParent(), header.FileName, PluginData->Password, PD_NOSKIP))
          {
            case IDALL: PluginData->Silent |= SF_ALLENRYPT;
            case IDOK:  break;
            case IDSKIPALL: PluginData->Silent |= SF_ENCRYPTED; goto UWA_NEXT;
            default: Abort = TRUE; goto UWA_NEXT;
          }
        }
        RARSetPasswordNarrow(ArcHandle, PluginData->Password);
      }*/

            // Match the mask against the entry's GENUINE wide name, and fall back to the
            // narrow mirror only when the archive did not give us one - which is exactly
            // what DoThisFile below already does.
            //
            // Matching the mirror unconditionally meant matching a best-fit ACP projection:
            // DecodeRarLegacyName refuses outright for a name CP_ACP cannot hold, so a
            // Cyrillic or Japanese entry took the `continue` and was silently left out of
            // the extraction (and, in the pre-pass below, out of the progress total); and
            // where ACP did produce something, it produced a Latin look-alike that the
            // user's mask was then tested against.
            std::wstring headerNameFallbackW;
            if (header.FileNameW[0] == L'\0' &&
                !DecodeRarLegacyName(header.FileName, headerNameFallbackW))
                continue;
            const wchar_t* headerNameW = header.FileNameW[0] != L'\0'
                                             ? header.FileNameW
                                             : headerNameFallbackW.c_str();
            const wchar_t* name = SalamanderGeneral->SalPathFindFileName(headerNameW);
            BOOL nameHasExt = wcsrchr(name, L'.') != NULL; // ".cvspass" is considered an extension on Windows

            int i;
            for (i = 0; i < masks.Count; i++)
            {
                if (SalamanderGeneral->AgreeMask(name, masks[i], nameHasExt))
                {
                    match = TRUE;
                    Salamander->ProgressSetTotalSize(header.Size, CQuadWord(-1, -1));
                    if (!Salamander->ProgressSetSize(CQuadWord(0, 0), CQuadWord(-1, -1), TRUE))
                    {
                        Abort = TRUE;
                        break;
                    }
                    if (DoThisFile(&header, fileName, targetDir))
                    {
                        if (!(header.Flags & RHDF_DIRECTORY))
                            op = RAR_TEST;
                        else
                            SetTargetAttributes(header.Attr);
                    }
                    break;
                }
            }

            //UWA_NEXT:
            ret = ProcessFile(op, header.FileNameW);
            if (op == RAR_TEST)
            {
                SetFileTime(TargetFile, NULL, NULL, &header.Time);
                CloseHandle(TargetFile);
                if (!ret)
                    DeleteTargetFile();
                else
                    SetTargetAttributes(header.Attr);
            }
            if (Abort)
            {
                ret = FALSE;
                break;
            }
            if (match)
            {
                currentProgress += header.Size;
                if (!Salamander->ProgressSetSize(header.Size, haveTotalProgress ? currentProgress : CQuadWord(-1, -1), TRUE))
                {
                    ret = FALSE;
                    break;
                }
            }
            //      if (!(header.Flags & RHDF_DIRECTORY)) FirstFile = FALSE;
        }
        Config.Options &= ~OP_SKITHISFILE;
        RARCloseArchive(ArcHandle);
        ArcHandle = NULL;
    }

    Salamander->CloseProgressDialog();
    Salamander = NULL;
    ArchiveVolumes = NULL;
    delete PluginData;

    return ret;
}

BOOL CPluginInterfaceForArchiver::Error(int error, ...)
{
    CALL_STACK_MESSAGE_NONE
    int lastErr = GetLastError();
    CALL_STACK_MESSAGE2("CPluginInterfaceForArchiver::Error(%d, )", error);
    // Wide throughout. LoadStr is the FORMAT here, so widening it without widening
    // the buffer and the vprintf would have fed a wchar_t* format to vsprintf - which compiles as
    // a warning at most and prints garbage.
    wchar_t buf[1024]; //temp variable
    *buf = 0;
    va_list arglist;
    va_start(arglist, error);
    _vsnwprintf_s(buf, _countof(buf), _TRUNCATE, LangStr(error).c_str(), arglist);
    va_end(arglist);
    if (lastErr != ERROR_SUCCESS)
    {
        int l = lstrlenW(buf);
        FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL, lastErr,
                       MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), buf + l, _countof(buf) - l, NULL);
    }
    SalamanderGeneral->ShowMessageBox(buf, LangStr(IDS_PLUGINNAME).c_str(), MSGBOX_ERROR);

    return FALSE;
}

BOOL CPluginInterfaceForArchiver::Init()
{
    CALL_STACK_MESSAGE1("CPluginInterfaceForArchiver::Init()");
    ArchiveVolumes = NULL;
    if (RARGetDllVersion() < RAR_DLL_VERSION)
        return Error(IDS_BADDLL);
    return TRUE;
}

/*int PASCAL
RARChangeVolProc(char *arcName, int mode)
{
  return InterfaceForArchiver.ChangeVolProc(arcName, mode);
}

int PASCAL
RARProcessDataProc(unsigned char *addr, int size)
{
  return InterfaceForArchiver.ProcessDataProc(addr, size);
}*/

// [narrow-ok: vendored ABI] RARSetPassword is unrar's own C entry point and has no wide form
// (unrarsrc/dll.hpp declares only the char* one, and unrarsrc is vendored - rewriting it would
// breach the provenance rules in CLAUDE.md). It is also not the path a password normally takes:
// unrarsrc asks through UCM_NEEDPASSWORDW first, which IS lossless, and only reaches
// Cmd.Password when it was pre-set here. So the loss is confined to pre-seeding a password that
// cannot be spelled in the machine code page, and only for archives where the library never
// calls back.
static void RARSetPasswordNarrow(HANDLE arcHandle, const wchar_t* password)
{
    std::string narrow;
    if (!EncodeRarPasswordExact(password, narrow) || narrow.size() >= MAX_PASSWORD)
        return; // cannot be represented; leave Cmd.Password unset and let the W callback ask
    // The frozen UnRAR C ABI predates const-correctness and declares this input as char*.
    // Keep the mutable view call-scoped; ownership remains with the exact projection above.
    RARSetPassword(arcHandle, narrow.data());
}

int CALLBACK RARCallback(UINT msg, LPARAM UserData, LPARAM P1, LPARAM P2)
{
    CALL_STACK_MESSAGE_NONE
    switch (msg)
    {
    // The W forms. unrarsrc offers both and tries the wide one FIRST
    // (volume.cpp:212, arcread.cpp:948), falling back to the narrow callback only if the wide
    // one left the buffer unchanged - so handling W here is not merely better, it is the branch
    // the library actually reaches. The A cases are deliberately absent: returning 0 from the
    // default arm means "buffer unchanged", which is exactly the answer the fallback needs after
    // the W handler has already made the decision (including a user cancel).
    case UCM_CHANGEVOLUMEW:
        return InterfaceForArchiver.ChangeVolProc((wchar_t*)P1, (int)P2);

    case UCM_PROCESSDATA:
        return InterfaceForArchiver.ProcessDataProc((unsigned char*)P1, (int)P2);

    case UCM_NEEDPASSWORDW:
        return InterfaceForArchiver.NeedPassword((wchar_t*)P1, (int)P2);
    }
    return 0;
}

BOOL CPluginInterfaceForArchiver::OpenArchive()
{
    CALL_STACK_MESSAGE1("CPluginInterfaceForArchiver::OpenArchive()");
    RAROpenArchiveDataEx oad;
    ZeroMemory(&oad, sizeof(oad));
    // ArcNameW is authoritative (the RAR SDK prefers it when set); ArcName is a best-effort
    // narrow fallback only - a failed round-trip leaves it empty rather than best-fit, since
    // correctness rests entirely on ArcNameW, not on this legacy narrow field.
    std::string arcNameA;
    Win32EncodeAcpExact(ArcFileName, arcNameA);
    oad.ArcName = const_cast<char*>(arcNameA.c_str());
    oad.ArcNameW = ArcFileName.data();
    // Warning: RAR_OM_LIST lists files spanned over multiple parts only once,
    // but RAR_OM_EXTRACT lists every file segment
    oad.OpenMode = List ? RAR_OM_LIST : RAR_OM_EXTRACT;
    oad.CmtBuf = NULL;
    oad.CmtBufSize = 0;
    oad.Callback = RARCallback;
    PluginData->PasswordForOpenArchive = TRUE;
    ArcHandle = RAROpenArchiveEx(&oad);
    PluginData->PasswordForOpenArchive = FALSE;
    if (!ArcHandle)
    {
        int err;
        switch (oad.OpenResult)
        {
        case ERAR_NO_MEMORY:
            err = IDS_LOWMEM;
            break;
        case ERAR_BAD_DATA:
            err = IDS_BADDATA;
            break;
        case ERAR_UNKNOWN_FORMAT:
            err = IDS_BADARC;
            break;
        case ERAR_BAD_ARCHIVE:
            err = IDS_BADARC;
            break;
        case ERAR_EOPEN:
            err = IDS_ERROPENARC;
            break;
        case ERAR_BAD_PASSWORD:
            err = IDS_BADPASSWORD;
            break;
        case ERAR_MISSING_PASSWORD:
            return FALSE; // Cancel in the password dialog; another message would make no sense (the user knows why the archive did not open)
        default:
            err = IDS_UNKNOWN;
        }
        return Error(err);
    }
    ArcFlags = oad.Flags;
    //RARSetChangeVolProc(ArcHandle, RARChangeVolProc);
    //RARSetProcessDataProc(ArcHandle, RARProcessDataProc);
    //RARSetCallback(ArcHandle, RARCallback, 0);

    // for RAR4 archives with encrypted file names we get here after the callback when an incorrect password is entered,
    // and because PasswordForOpenArchive == TRUE when calling RAROpenArchiveEx, SF_ALLENRYPT is set even after merely clicking OK in the password dialog,
    // which ensures we do not ask for the password again (instead an error message about the wrong password appears)
    if ((oad.Flags & 0x0080) /* block headers encrypted */)
    {
        if (!(PluginData->Silent & SF_ALLENRYPT))
        {
            if (PasswordDialog(SalamanderGeneral->GetMsgBoxParent(), ArcFileName.c_str(), PluginData->Password,
                               PD_NOSKIP | PD_NOSKIPALL | PD_NOALL) == IDCANCEL)
            {
                RARCloseArchive(ArcHandle);
                ArcHandle = NULL;
                return FALSE;
            }
            else
                PluginData->Silent |= SF_ALLENRYPT;
        }
        RARSetPasswordNarrow(ArcHandle, PluginData->Password);
    }
    return TRUE;
}

BOOL CPluginInterfaceForArchiver::ReadHeader(CFileHeader* header)
{
    DEBUG_SLOW_CALL_STACK_MESSAGE1("CPluginInterfaceForArchiver::ReadHeader()");
    RARHeaderDataEx headerData;
    ZeroMemory(&headerData, sizeof(headerData));
    headerData.CmtBuf = NULL;
    headerData.CmtBufSize = 0;
    int err = 0;
    switch (RARReadHeaderEx(ArcHandle, &headerData))
    {
    case 0:
    {
        const WCHAR* headerFileNameW = headerData.FileNameW;
        if (headerFileNameW[0] == L'\\')
            headerFileNameW++;
        header->FileNameW[0] = L'\0';
        lstrcpynW(header->FileNameW, headerFileNameW, _countof(header->FileNameW));

        // Not every char representable in ANSI page can be reprsented in OEM page used by RAR files
        // e.g. the Ellipsis character 0x2026
        if (headerData.FileNameW[0])
        {
            // Refuse rather than best-fit-substitute - see
            // unrar_name_narrow.h. Also matches header->FileNameW's own leading-backslash strip
            // (headerFileNameW) instead of headerData.FileNameW directly, so both mirrors agree.
            const std::string narrowName = ProjectRarFileNameToAnsiExactOrPlaceholder(headerFileNameW);
            lstrcpynA(header->FileName, narrowName.c_str(), sizeof(header->FileName));
        }
        else
        {
            lstrcpyA(header->FileName, headerData.FileName + (headerData.FileName[0] == '\\' ? 1 : 0));
            OemToCharA(header->FileName, header->FileName);
            CopyRarLegacyNameToWideExact(header->FileName, header->FileNameW,
                                         _countof(header->FileNameW));
        }
        header->Size = CQuadWord(headerData.UnpSize, headerData.UnpSizeHigh);
        header->CompSize = CQuadWord(headerData.PackSize, headerData.PackSizeHigh);
        FILETIME ft;
        if (!DosDateTimeToFileTime(HIWORD(headerData.FileTime),
                                   LOWORD(headerData.FileTime), &ft))
        {
            SystemTimeToFileTime(&MinTime, &ft);
        }
        LocalFileTimeToFileTime(&ft, &header->Time);

        // adjust attributes from archives originating on Unix and other systems incompatible with Win32
        header->Attr = headerData.FileAttr & FILE_ATTRIBUTE_MASK;
        switch (headerData.HostOS)
        {
        case 0 /* MS-DOS */:
        case 1 /* OS/2 */:
        case 2 /* Win32 */:
            break;

            //      case HOST_UNIX:
            //      case HOST_BEOS:
        default:
        {
            if (headerData.Flags & RHDF_DIRECTORY)
                header->Attr = FILE_ATTRIBUTE_DIRECTORY;
            else
                header->Attr = FILE_ATTRIBUTE_ARCHIVE;
            break;
        }
        }

        if (headerData.Flags & RHDF_DIRECTORY)
            header->Attr |= FILE_ATTRIBUTE_DIRECTORY;
        else
            header->Attr &= ~FILE_ATTRIBUTE_DIRECTORY;

        header->Flags = headerData.Flags;
        break;
    }

    case ERAR_END_ARCHIVE:
        header->FileName[0] = 0;
        header->FileNameW[0] = 0;
        break;

    case ERAR_BAD_DATA:
        err = IDS_BADDATA;
        break;
    case ERAR_EOPEN:
        if (NotWholeArchListed)
        {
            // Looks like missing volume -> abort listing the archive
            // returning TRUE could mean listing spanned file twice
            return FALSE;
        }
        err = IDS_VOLUMEERR;
        break;
    default:
        err = IDS_UNKNOWN;
    }
    if (err)
        return Error(err);
    return TRUE;
}

BOOL CPluginInterfaceForArchiver::ProcessFile(int operation, const wchar_t* fileName)
{
    DEBUG_SLOW_CALL_STACK_MESSAGE2("CPluginInterfaceForArchiver::ProcessFile(%d, )", operation);
    int err = 0;
    Success = FALSE;
    wchar_t destPathW[] = L"";
    int ret = RARProcessFileW(ArcHandle, operation, destPathW, NULL);
    if (Abort)
        return FALSE;
    switch (ret)
    {
    case 0:
        break;
    case ERAR_NO_MEMORY:
        err = IDS_LOWMEM;
        break;
    case ERAR_UNKNOWN_FORMAT:
    case ERAR_BAD_ARCHIVE:
        err = IDS_BADARC;
        break;
    case ERAR_EOPEN:
        if (List)
            return FALSE;
        err = IDS_VOLUMEERR;
        break;
    case ERAR_EREAD:
        err = IDS_ERRREADARC;
        break;

    case ERAR_BAD_PASSWORD:
    case ERAR_BAD_DATA:
    {
        DWORD silentFlag = ret == ERAR_BAD_PASSWORD ? SF_PASSWD : SF_DATA;
        if (!(PluginData->Silent & silentFlag) && !Abort)
        {
            PluginData->Silent &= ~SF_ALLENRYPT;
            switch (SalamanderGeneral->DialogError(SalamanderGeneral->GetMsgBoxParent(), BUTTONS_SKIPCANCEL,
                                                   fileName, LangStr(ret == ERAR_BAD_PASSWORD ? IDS_BADPASSWORD : IDS_CRC).c_str(), NULL))
            {
            case DIALOG_SKIPALL:
                PluginData->Silent |= silentFlag;
            case DIALOG_SKIP:
                return FALSE;
            default:
                Abort = TRUE;
                return FALSE;
            }
        }
        return FALSE;
    }

    case ERAR_END_ARCHIVE:
        if (List)
        {
            Abort = TRUE;
            return Error(IDS_UNKNOWN);
        }
        return Success;

    //case ERAR_EREFERENCE:
    default:
        err = IDS_UNKNOWN;
    }
    if (err)
    {
        if (!Abort)
        {
            Abort = TRUE;
            Error(err);
        }
        return FALSE;
    }
    return TRUE;
}

int CPluginInterfaceForArchiver::ChangeVolProc(wchar_t* arcName, int mode)
{
    CALL_STACK_MESSAGE2("CPluginInterfaceForArchiver::ChangeVolProc(, %d)", mode);
    if (mode == RAR_VOL_ASK)
    {
        if (List)
        {
            NotWholeArchListed = TRUE;
            return -1; // list only while it works without asking for additional volumes
        }
        std::wstring selectedVolume(arcName);
        if (NextVolumeDialog(SalamanderGeneral->GetMsgBoxParent(), selectedVolume) != IDOK ||
            !UnrarChangeVolumeFits(selectedVolume))
        {
            Abort = TRUE;
            return -1;
        }
        std::wmemcpy(arcName, selectedVolume.c_str(), selectedVolume.size() + 1);
    }
    else
    {
        if (mode == RAR_VOL_NOTIFY && ArchiveVolumes != NULL)
        {
            ArchiveVolumes->Add(arcName, -2);
        }
    }
    return 1;
}

BOOL CPluginInterfaceForArchiver::SafeSeek(CQuadWord position)
{
    CALL_STACK_MESSAGE2("CPluginInterfaceForArchiver::SafeSeek(0x%I64X)", position.Value);
    wchar_t buf[1024];
    while (1)
    {
        CQuadWord pos = position;
        if (SetFilePointer(TargetFile, pos.LoDWord, LPLONG(&pos.HiDWord), FILE_BEGIN) != 0xFFFFFFFF ||
            GetLastError() == NO_ERROR)
            return TRUE; // success
        lstrcpyW(buf, LangStr(IDS_UNABLESEEK).c_str());
        FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL,
                       GetLastError(), MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), buf + lstrlenW(buf), 1024 - lstrlenW(buf), NULL);

        if (PluginData->Silent & SF_IOERRORS)
            return FALSE;

        switch (SalamanderGeneral->DialogError(SalamanderGeneral->GetMsgBoxParent(), BUTTONS_RETRYSKIPCANCEL, TargetName.c_str(), buf, NULL))
        {
        case DIALOG_SKIPALL:
            PluginData->Silent |= SF_IOERRORS;
        case DIALOG_SKIP:
            return FALSE;
        case DIALOG_CANCEL:
        case DIALOG_FAIL:
            Abort = TRUE;
            return FALSE;
        }
    }
}

int CPluginInterfaceForArchiver::ProcessDataProc(unsigned char* addr, int size)
{
    SLOW_CALL_STACK_MESSAGE2("CPluginInterfaceForArchiver::ProcessDataProc(, %d)", size);
    Success = FALSE;
    if (TargetFile == INVALID_HANDLE_VALUE)
        return 1;
    if (size == 0)
    {
        Success = TRUE;
        return 1;
    }
    wchar_t buf[1024];
    CQuadWord pos = CQuadWord(0, 0);
    while (1)
    {
        pos.LoDWord = SetFilePointer(TargetFile, 0, LPLONG(&pos.HiDWord), FILE_CURRENT);
        if (pos.LoDWord != 0xFFFFFFFF || GetLastError() == NO_ERROR)
            break;
        lstrcpyW(buf, LangStr(IDS_UNABLEGETFIELPOS).c_str());
        FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL,
                       GetLastError(), MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), buf + lstrlenW(buf), 1024 - lstrlenW(buf), NULL);

        if (PluginData->Silent & SF_IOERRORS)
            return -1;

        switch (SalamanderGeneral->DialogError(SalamanderGeneral->GetMsgBoxParent(), BUTTONS_RETRYSKIPCANCEL, TargetName.c_str(), buf, NULL))
        {
        case DIALOG_SKIPALL:
            PluginData->Silent |= SF_IOERRORS;
        case DIALOG_SKIP:
            return -1;
        case DIALOG_CANCEL:
        case DIALOG_FAIL:
            Abort = TRUE;
            return -1;
        }
    }
    DWORD written;
    while (1)
    {
        if (WriteFile(TargetFile, addr, size, &written, NULL))
        {
            if (!Salamander->ProgressAddSize(size, TRUE))
            {
                Abort = TRUE;
                return -1;
            }
            Success = TRUE;
            return 1; // sucess
        }
        lstrcpyW(buf, LangStr(IDS_UNABLEWRITE).c_str());
        FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL,
                       GetLastError(), MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), buf + lstrlenW(buf), 1024 - lstrlenW(buf), NULL);

        if (PluginData->Silent & SF_IOERRORS)
            return -1;

        switch (SalamanderGeneral->DialogError(SalamanderGeneral->GetMsgBoxParent(), BUTTONS_RETRYSKIPCANCEL, TargetName.c_str(), buf, NULL))
        {
        case DIALOG_SKIPALL:
            PluginData->Silent |= SF_IOERRORS;
        case DIALOG_SKIP:
            return -1;
        case DIALOG_CANCEL:
        case DIALOG_FAIL:
            Abort = TRUE;
            return -1;
        }
        if (!SafeSeek(pos))
            return -1;
    }
    return 1;
}

int CPluginInterfaceForArchiver::NeedPassword(wchar_t* password, int size)
{
    if (!(PluginData->Silent & SF_ALLENRYPT))
    {
        switch (PasswordDialog(SalamanderGeneral->GetMsgBoxParent(), ArcFileName.c_str(), PluginData->Password,
                               PD_NOSKIP | PD_NOSKIPALL | (PluginData->PasswordForOpenArchive ? PD_NOALL : 0)))
        {
        case IDOK:
            if (!PluginData->PasswordForOpenArchive)
                break;
            // else break; // Petr: the break is not missing here! (OK acts as All)
        case IDALL:
            PluginData->Silent |= SF_ALLENRYPT;
            break;
        default:
            Abort = TRUE;
            return -1;
        }
    }
    lstrcpynW(password, PluginData->Password, size);
    return 1;
}

BOOL CPluginInterfaceForArchiver::SwitchToFirstVol(const wchar_t* arcName, BOOL* saveFirstVolume)
{
    CALL_STACK_MESSAGE1("CPluginInterfaceForArchiver::SwitchToFirstVol()");
    if (arcName == NULL)
        return FALSE;
    std::wstring candidate(arcName);
    wchar_t* ext = PathFindExtensionW(candidate.data());
    if (!ext)
    {
        ArcFileName = candidate;
        return TRUE;
    }
    if (lstrlenW(ext) > 3 &&
        iswdigit(ext[2]) && iswdigit(ext[3]))
    {
        std::wstring oldExt(ext);
        if (iswdigit(ext[1]))
        {
            lstrcpyW(ext, L".001"); // .001, .002, .003, etc.
        }
        else
        {
            lstrcpyW(ext, L".rar"); // .rar, .r00, .r01, etc.
        }
        DWORD attr = SalamanderGeneral->SalGetFileAttributes(candidate.c_str());
        if (attr == -1 || attr & FILE_ATTRIBUTE_DIRECTORY)
        {
            std::wstring path(candidate);
            if (NextVolumeDialog(SalamanderGeneral->GetMsgBoxParent(), path,
                                 LangStr(IDS_SELECTFIRST).c_str()) == IDOK)
            {
                ArcFileName = path;
                if (saveFirstVolume)
                    *saveFirstVolume = TRUE;
                return TRUE;
            }
            else
            {
                lstrcpyW(ext, oldExt.c_str());
                return FALSE;
            }
        }
        ArcFileName = candidate;
        return TRUE;
    }
    // also test whether this archive follows the new naming conventions
    wchar_t* part = ext - 1;
    while (part >= candidate.data())
    {
        if (*part == L'.')
            break; // ".cvspass" is considered an extension on Windows
        part--;
    }
    if (part >= candidate.data() && _wcsnicmp(part, L".part", 5) == 0)
    {
        wchar_t* iterator = part + 5;
        while (iswdigit(*iterator))
            iterator++;
        int digits = (int)(iterator - part - 5);
        if (digits && *iterator == L'.')
        {
            // the file is named according to the new convention
            std::wstring firstVolume(candidate.c_str(), part - candidate.data());
            firstVolume += SPLFormatStringOwned(L".part%0*d.rar", digits, 1);
            std::wstring path(firstVolume);

            DWORD attr = SalamanderGeneral->SalGetFileAttributes(path.c_str());
            if (!(attr & FILE_ATTRIBUTE_DIRECTORY))
            {
                ArcFileName = path;
                if (saveFirstVolume)
                    *saveFirstVolume = TRUE;
                return TRUE;
            }
            if (NextVolumeDialog(SalamanderGeneral->GetMsgBoxParent(), path,
                                 LangStr(IDS_SELECTFIRST).c_str()) == IDOK)
            {
                candidate = path;
                if (saveFirstVolume)
                    *saveFirstVolume = TRUE;
            }
            else
            {
                return FALSE;
            }
            // Why is this here?
            UpdateWindow(SalamanderGeneral->GetMainWindowHWND());
        }
    }
    ArcFileName = candidate;
    return TRUE;
}

BOOL CPluginInterfaceForArchiver::MakeFilesList(TIndirectArray2<CRARExtractInfo>& files, SalEnumSelection next, void* nextParam, const wchar_t* targetDir)
{
    CALL_STACK_MESSAGE1("CPluginInterfaceForArchiver::MakeFilesList()");
    const wchar_t* nextName;
    BOOL isDir;
    CQuadWord size;
    std::wstring dir(targetDir);
    const CFileData* pFileData;
    int errorOccured;

    SPLSalPathAddBackslashOwned(dir);

    ProgressTotal = CQuadWord(0, 0);
    while ((nextName = next(SalamanderGeneral->GetMsgBoxParent(), 1, &isDir, &size, &pFileData, nextParam, &errorOccured)) != NULL)
    {
        if (isDir)
        {
            std::wstring directoryPath = dir + nextName;
            DestroyIllegalCharsW(directoryPath.data() + dir.size());
            BOOL skip;
            if (SalamanderSafeFile->SafeFileCreate(directoryPath.c_str(), 0, 0, 0, TRUE, SalamanderGeneral->GetMsgBoxParent(), NULL, NULL,
                                                   &PluginData->Silent, TRUE, &skip, NULL, 0, NULL, NULL) == INVALID_HANDLE_VALUE &&
                !skip)
                return FALSE;
            SetFileAttributesW(directoryPath.c_str(), pFileData->Attr);
        }
        else
        {
            CRARExtractInfo* ei = new CRARExtractInfo;
            if (!ei)
                return Error(IDS_LOWMEM);
            CRARFileData* rarFileData = (CRARFileData*)pFileData->PluginData;
            ei->ItemNumber = rarFileData ? rarFileData->ItemNumber : -1;
            if (rarFileData != NULL && rarFileData->FileNameW != NULL)
                ei->FileNameW = rarFileData->FileNameW;
            else
            {
                ei->FileNameW = ArcRoot;
                SPLSalPathAppendOwned(ei->FileNameW, nextName);
            }
            if (!files.Add(ei))
            {
                delete ei;
                return Error(IDS_LOWMEM);
            }
            ProgressTotal += size;
        }
    }
    return errorOccured != SALENUM_CANCEL && // test whether no error occurred and the user did not request to abort the operation (Cancel button)
           SalamanderGeneral->TestFreeSpace(SalamanderGeneral->GetMsgBoxParent(), targetDir, ProgressTotal, SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_PLUGINNAME).c_str());
}

BOOL CPluginInterfaceForArchiver::DoThisFile(CFileHeader* header, const wchar_t* arcName, const wchar_t* targetDir)
{
    CALL_STACK_MESSAGE1("CPluginInterfaceForArchiver::DoThisFile()");
    std::wstring headerNameFallbackW;
    if (header->FileNameW[0] == L'\0' &&
        !DecodeRarLegacyName(header->FileName, headerNameFallbackW))
        return FALSE;
    const wchar_t* headerNameW = header->FileNameW[0] != L'\0'
                                     ? header->FileNameW
                                     : headerNameFallbackW.c_str();
    std::wstring message = SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_EXTRACTING).c_str();
    message += headerNameW;
    Salamander->ProgressDialogAddText(message.c_str(), TRUE);
    if (header->Flags & RHDF_SPLITBEFORE)
    {
        TRACE_I("Skipping file continued from previuous volume: " << header->FileName);
        if (!(Config.Options & (OP_SKIPCONTINUED | OP_SKITHISFILE)) && ContinuedFileDialog(SalamanderGeneral->GetMsgBoxParent(), header->FileNameW) != IDOK)
            Abort = TRUE;
        return FALSE;
    }
    if (!(header->Flags & RHDF_SOLID) && (header->Flags & RHDF_ENCRYPTED))
    {
        if (PluginData->Silent & SF_ENCRYPTED)
            return FALSE;
        if (!(PluginData->Silent & SF_ALLENRYPT))
        {
            switch (PasswordDialog(SalamanderGeneral->GetMsgBoxParent(), header->FileNameW, PluginData->Password, 0))
            {
            case IDALL:
                PluginData->Silent |= SF_ALLENRYPT;
            case IDOK:
                break;
            case IDSKIPALL:
                PluginData->Silent |= SF_ENCRYPTED;
            case IDSKIP:
                return FALSE;
            default:
                Abort = TRUE;
                return FALSE;
            }
        }
        RARSetPasswordNarrow(ArcHandle, PluginData->Password);
    }
    const wchar_t* relativeName;
    if (header->FileNameW[0] != L'\0')
    {
        relativeName = header->FileNameW;
        if (RootLenW < (DWORD)lstrlenW(header->FileNameW))
            relativeName += RootLenW;
    }
    else
    {
        relativeName = headerNameFallbackW.c_str();
        if (RootLenW <= headerNameFallbackW.length())
            relativeName += RootLenW;
    }
    if (!BuildTargetName(header, targetDir, relativeName))
    {
        if (PluginData->Silent & SF_LONGNAMES)
            return FALSE;
        switch (SalamanderGeneral->DialogError(SalamanderGeneral->GetMsgBoxParent(), BUTTONS_SKIPCANCEL, header->FileNameW, LangStr(IDS_TOOLONGNAME).c_str(), NULL))
        {
        case DIALOG_SKIPALL:
            PluginData->Silent |= SF_LONGNAMES;
        case DIALOG_SKIP:
            return FALSE;
        case DIALOG_CANCEL:
        case DIALOG_FAIL:
            Abort = TRUE;
            return FALSE;
        }
    }
    std::wstring nameInArc(arcName);
    SPLSalPathAppendOwned(nameInArc, headerNameW);
    wchar_t buf[100];
    GetInfo(buf, _countof(buf), &header->Time, header->Size);
    BOOL skip;
    CQuadWord q = header->Size;
    BOOL allocate = AllocateWholeFile &&
                    CQuadWord(2, 0) < q && q < CQuadWord(0, 0x80000000);
    if (TestAllocateWholeFile)
        q += CQuadWord(0, 0x80000000);
    TargetFile = CreateTargetFile(GENERIC_WRITE, FILE_SHARE_READ,
                                  header->Attr & ~FILE_ATTRIBUTE_READONLY | FILE_FLAG_SEQUENTIAL_SCAN,
                                  (header->Flags & RHDF_DIRECTORY) != 0, nameInArc.c_str(), buf, &skip,
                                  allocate ? &q : NULL);
    if (skip)
    {
        if (header->Flags & RHDF_SPLITAFTER)
        {
            Config.Options |= OP_SKITHISFILE;
        }
        return FALSE;
    }
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
        // allocation failed, but we will try again next time
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
    //if (header->Attr & FILE_ATTRIBUTE_DIRECTORY) header->Size = 1;
    //Salamander->ProgressSetTotalSize(header->Size, ProgressTotal);
    return TRUE;
}

BOOL CPluginInterfaceForArchiver::ConstructMaskArray(TIndirectArray2<wchar_t>& maskArray, const wchar_t* masks)
{
    CALL_STACK_MESSAGE1("CPluginInterfaceForArchiver::ConstructMaskArray()");
    const wchar_t* sour = masks;
    while (*sour)
    {
        std::wstring buffer;
        while (*sour)
        {
            if (*sour == L';')
            {
                if (*(sour + 1) == L';')
                    sour++;
                else
                    break;
            }
            buffer.push_back(*sour++);
        }
        const size_t first = buffer.find_first_not_of(L" \t\r\n");
        if (first != std::wstring::npos)
        {
            const size_t last = buffer.find_last_not_of(L" \t\r\n");
            const std::wstring mask = buffer.substr(first, last - first + 1);
            const std::wstring preparedMask =
                SPLPrepareMaskOwned(SalamanderGeneral, mask.c_str());
            wchar_t* newMask = new wchar_t[preparedMask.size() + 1];
            if (!newMask)
            {
                SalamanderGeneral->ShowMessageBox(LangStr(IDS_LOWMEM).c_str(), LangStr(IDS_PLUGINNAME).c_str(), MSGBOX_ERROR);
                return FALSE;
            }
            wcscpy(newMask, preparedMask.c_str());
            if (!maskArray.Add(newMask))
            {
                delete newMask;
                SalamanderGeneral->ShowMessageBox(LangStr(IDS_LOWMEM).c_str(), LangStr(IDS_PLUGINNAME).c_str(), MSGBOX_ERROR);
                return FALSE;
            }
        }
        if (*sour)
            sour++;
    }
    return TRUE;
}

BOOL CPluginInterfaceForArchiver::UnpackWholeArchiveCalculateProgress(TIndirectArray2<wchar_t>& masks)
{
    CALL_STACK_MESSAGE1("CPluginInterfaceForArchiver::UnpackWholeArchiveCalculateProgress()");
    List = TRUE;
    BOOL ret = TRUE;
    NotWholeArchListed = FALSE;

    ProgressTotal = CQuadWord(0, 0);

    if (OpenArchive())
    {
        if ((ArcFlags & AF_VOLUME) && !(ArcFlags & AF_FIRST_VOLUME))
        {
            NotWholeArchListed = TRUE;
        }
        else
        {
            CFileHeader header;
            CFileData fileData;
            while ((ret = ReadHeader(&header)) != 0 && *header.FileName)
            {
                if (header.Flags & RHDF_SPLITBEFORE)
                {
                    NotWholeArchListed = TRUE;
                    break;
                }
                // Same correction as UnpackWholeArchive: the mask is tested against the
                // genuine wide name, with the narrow mirror used only as a fallback. This
                // pre-pass sizes the progress bar, so a name the mirror could not hold used
                // to drop out of the total and leave the bar short by that file.
                std::wstring headerNameFallbackW;
                if (header.FileNameW[0] == L'\0' &&
                    !DecodeRarLegacyName(header.FileName, headerNameFallbackW))
                    continue;
                const wchar_t* headerNameW = header.FileNameW[0] != L'\0'
                                                 ? header.FileNameW
                                                 : headerNameFallbackW.c_str();
                const wchar_t* name = SalamanderGeneral->SalPathFindFileName(headerNameW);
                BOOL nameHasExt = wcsrchr(name, L'.') != NULL; // ".cvspass" is considered an extension on Windows
                int i;
                for (i = 0; i < masks.Count; i++)
                {
                    if (SalamanderGeneral->AgreeMask(name, masks[i], nameHasExt))
                    {
                        ProgressTotal += header.Size;
                        break;
                    }
                }
                if (!ProcessFile(RAR_SKIP, header.FileNameW))
                {
                    if (Abort)
                        ret = FALSE;
                    break;
                }
            }
        }

        RARCloseArchive(ArcHandle);
        ArcHandle = NULL;
    }
    else
        ret = FALSE;

    if (NotWholeArchListed)
        ret = FALSE;

    return ret;
}

// ****************************************************************************

void GetInfo(wchar_t* buffer, size_t bufferCount, const FILETIME* lastWrite, const CQuadWord& size)
{
    CALL_STACK_MESSAGE2("GetInfo(, , 0x%I64X)", size.Value);
    SYSTEMTIME st;
    FILETIME ft;
    FileTimeToLocalFileTime(lastWrite, &ft);
    FileTimeToSystemTime(&ft, &st);

    wchar_t date[50], time[50];
    if (GetTimeFormatW(LOCALE_USER_DEFAULT, 0, &st, NULL, time, _countof(time)) == 0)
        _snwprintf_s(time, _countof(time), _TRUNCATE, L"%u:%02u:%02u", st.wHour, st.wMinute, st.wSecond);
    if (GetDateFormatW(LOCALE_USER_DEFAULT, DATE_SHORTDATE, &st, NULL, date, _countof(date)) == 0)
        _snwprintf_s(date, _countof(date), _TRUNCATE, L"%u.%u.%u", st.wDay, st.wMonth, st.wYear);
    const std::wstring number = SPLNumberToStrOwned(SalamanderGeneral, size);
    _snwprintf_s(buffer, bufferCount, _TRUNCATE, L"%ls, %ls, %ls", number.c_str(), date, time);
}

//***********************************************************************************
//
// Rutiny ze SHLWAPI.DLL
//

// Wide. A local reimplementation of shlwapi's routine with exactly one caller
// (SwitchToFirstVol), which is wide now. Widened rather than paired, because a narrow twin with
// no caller is just a second thing to keep in step.
wchar_t* PathFindExtensionW(wchar_t* pszPath)
{
    CALL_STACK_MESSAGE_NONE
    if (pszPath == NULL)
    {
        TRACE_E("pszPath == NULL");
        return NULL;
    }
    int len = lstrlenW(pszPath);
    wchar_t* iterator = pszPath + len - 1;
    while (iterator >= pszPath)
    {
        if (*iterator == L'.') // ".cvspass" is considered an extension on Windows
        {
            return iterator;
        }
        if (*iterator == L'\\')
            break;
        iterator--;
    }
    return pszPath + len;
}

//
// ****************************************************************************
// CPluginDataInterface
//

void CPluginDataInterface::ReleasePluginData(CFileData& file, BOOL isDir)
{
    // file.PluginData is NULL for folders not having extra items in the archive - see GetFileDataForUpDir & GetFileDataForNewDir
    delete (CRARFileData*)file.PluginData; // However, delete NULL is perfectly OK
}

// Callback called by Salamander to obtain custom column text - see spl_com.h / FColumnGetText
// Global variables - pointers to global variables used by Salamander
static const CFileData** TransferFileData = NULL;
static int* TransferIsDir = NULL;
static wchar_t* TransferBuffer = NULL;
static int* TransferLen = NULL;
static DWORD* TransferRowData = NULL;
static CPluginDataInterfaceAbstract** TransferPluginDataIface = NULL;
static DWORD* TransferActCustomData = NULL;

static void WINAPI GetPackedSizeText()
{
    if (*TransferIsDir)
    {
        *TransferLen = 0;
    }
    else
    {
        CRARFileData* rarFileData = (CRARFileData*)(*TransferFileData)->PluginData;
        if (rarFileData->PackedSize != 0)
        {
            const std::wstring number = SPLNumberToStrOwned(
                SalamanderGeneral, CQuadWord().SetUI64(rarFileData->PackedSize));
            *TransferLen = static_cast<int>((std::min<size_t>)(number.size(), TRANSFER_BUFFER_MAX));
            wmemcpy(TransferBuffer, number.data(), static_cast<size_t>(*TransferLen));
        }
        else
        {
            *TransferLen = 0;
        }
    }
}

void WINAPI
CPluginDataInterface::SetupView(BOOL leftPanel, CSalamanderViewAbstract* view, const wchar_t* archivePath,
                                const CFileData* upperDir)
{
    view->GetTransferVariables(TransferFileData, TransferIsDir, TransferBuffer, TransferLen, TransferRowData,
                               TransferPluginDataIface, TransferActCustomData);

    // Special columns added only in Detailed mode
    if (view->GetViewMode() == VIEW_MODE_DETAILED)
    {
        CColumn column;
        if (Config.ListInfoPackedSize)
        {
            // We add Packed Size just after the Size column; or at the end in case of failure
            int sizeIndex = view->GetColumnsCount();
            int i;
            for (i = 0; i < sizeIndex; i++)
                if (view->GetColumn(i)->ID == COLUMN_ID_SIZE)
                {
                    sizeIndex = i + 1;
                    break;
                }

            lstrcpynW(column.Name, SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_LISTINFO_PAKEDSIZE).c_str(), _countof(column.Name));
            lstrcpynW(column.Description, SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_LISTINFO_PAKEDSIZE_DESC).c_str(), _countof(column.Description));
            column.GetText = GetPackedSizeText;
            column.SupportSorting = 0;
            column.LeftAlignment = 0;
            column.ID = COLUMN_ID_CUSTOM;
            column.CustomData = 0;
            column.Width = leftPanel ? LOWORD(Config.ColumnPackedSizeWidth) : HIWORD(Config.ColumnPackedSizeWidth);
            column.FixedWidth = leftPanel ? LOWORD(Config.ColumnPackedSizeFixedWidth) : HIWORD(Config.ColumnPackedSizeFixedWidth);
            view->InsertColumn(sizeIndex, &column);
        }
    }
}

void CPluginDataInterface::ColumnFixedWidthShouldChange(BOOL leftPanel, const CColumn* column, int newFixedWidth)
{
    if (column->CustomData == 0)
    {
        if (leftPanel)
            Config.ColumnPackedSizeFixedWidth = MAKELONG(newFixedWidth, HIWORD(Config.ColumnPackedSizeFixedWidth));
        else
            Config.ColumnPackedSizeFixedWidth = MAKELONG(LOWORD(Config.ColumnPackedSizeFixedWidth), newFixedWidth);
    }
    if (newFixedWidth)
        ColumnWidthWasChanged(leftPanel, column, column->Width);
}

void CPluginDataInterface::ColumnWidthWasChanged(BOOL leftPanel, const CColumn* column, int newWidth)
{
    if (column->CustomData == 0)
    {
        if (leftPanel)
            Config.ColumnPackedSizeWidth = MAKELONG(newWidth, HIWORD(Config.ColumnPackedSizeWidth));
        else
            Config.ColumnPackedSizeWidth = MAKELONG(LOWORD(Config.ColumnPackedSizeWidth), newWidth);
    }
}
