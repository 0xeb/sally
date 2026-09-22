// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"

#include "unole_plugin.h"
#include "dialogs.h"

#include "unole2.rh"
#include "unole2.rh2"
#include "lang\lang.rh"

#define BUF_SIZE 65536

// ****************************************************************************

HINSTANCE DLLInstance = NULL; // handle to SPL - language-independent resources
HINSTANCE HLanguage = NULL;   // handle to SLG - language-dependent resources

// plugin interface object; its methods are called from Salamander
CPluginInterface PluginInterface;
// part of the CPluginInterface interface for the archiver
CPluginInterfaceForArchiver InterfaceForArchiver;

// Salamander general interface - valid from plugin startup until shutdown
CSalamanderGeneralAbstract* SalamanderGeneral = NULL;

// interface for convenient work with files
CSalamanderSafeFileAbstract* SalamanderSafeFile = NULL;

// variable definition for "dbg.h"
CSalamanderDebugAbstract* SalamanderDebug = NULL;

// variable definition for "spl_com.h"
int SalamanderVersion = 0;

const SYSTEMTIME MinTime = {1980, 01, 2, 01, 00, 00, 00, 000};

//const char *CONFIG_OPTIONS = "Options";

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
    // set SalamanderVersion for "spl_com.h"
    SalamanderVersion = salamander->GetVersion();

    CALL_STACK_MESSAGE1("SalamanderPluginEntry()");

    // this plugin is built for the current Salamander version and newer - perform a check
    if (SalamanderVersion < LAST_VERSION_OF_SALAMANDER)
    { // reject older versions
        // wide: same call-site-local widen shape used throughout this backlog
        // (205-227).
        MessageBoxW(salamander->GetParentWindow(),
                    _CRT_WIDE(REQUIRE_LAST_VERSION_OF_SALAMANDER),
                    L"UnOLE2" /* do not translate! */, MB_OK | MB_ICONERROR);
        return NULL;
    }

    // let the language module (.slg) load
    HLanguage = salamander->LoadLanguageModule(salamander->GetParentWindow(), L"UnOLE2" /* do not translate! */);
    if (HLanguage == NULL)
        return NULL;

    // obtain the general Salamander interface
    SalamanderGeneral = salamander->GetSalamanderGeneral();
    SalamanderSafeFile = salamander->GetSalamanderSafeFile();

    /*
  // beta valid until the end of February 2001
  SYSTEMTIME st;
  GetLocalTime(&st);
  if (st.wYear == 2001 && st.wMonth > 2 || st.wYear > 2001)
  {
    SalamanderGeneral->ShowMessageBox(SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_EXPIRE).c_str(), SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_PLUGINNAME).c_str(), MSGBOX_INFO);
    return NULL;
  }
  */

    if (!InterfaceForArchiver.Init())
        return NULL;

    // set the basic plugin information
    salamander->SetBasicPluginData(SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_PLUGINNAME).c_str(),
                                   FUNCTION_PANELARCHIVERVIEW | FUNCTION_CUSTOMARCHIVERUNPACK |
                                       FUNCTION_CONFIGURATION | FUNCTION_LOADSAVECONFIGURATION,
                                   _CRT_WIDE(VERSINFO_VERSION_NO_PLATFORM),
                                   _CRT_WIDE(VERSINFO_COPYRIGHT),
                                   SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_PLUGIN_DESCRIPTION).c_str(),
                                   L"UnOLE2" /* do not translate! */, L"ole"); // "UnOLE2", "doc;xls");

    salamander->SetPluginHomePageURL(L"https://github.com/0xeb/sally");

    return &PluginInterface;
}

// ****************************************************************************
//
// Callback functions
//

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

    /*  Options = 0;
  if (regKey != NULL)   // load from the registry
  {
    registry->GetValue(regKey, CONFIG_OPTIONS, REG_DWORD, &Options, sizeof(DWORD));
  }*/
}

void CPluginInterface::SaveConfiguration(HWND parent, HKEY regKey, CSalamanderRegistryAbstract* registry)
{
    CALL_STACK_MESSAGE1("CPluginInterface::SaveConfiguration(, ,)");
    //  registry->SetValue(regKey, CONFIG_OPTIONS, REG_DWORD, &Options, sizeof(DWORD));
}

void CPluginInterface::Configuration(HWND parent)
{
    CALL_STACK_MESSAGE1("CPluginInterface::Configuration()");
}

void CPluginInterface::Connect(HWND parent, CSalamanderConnectAbstract* salamander)
{
    CALL_STACK_MESSAGE1("CPluginInterface::Connect(,)");

    salamander->AddCustomUnpacker(L"UnOLE2 (Plugin)", L"*.ole", FALSE);
    salamander->AddPanelArchiver(L"ole", FALSE, FALSE);
    //  salamander->AddCustomUnpacker("UnOLE2 (Plugin)", "*.doc;*.xls", FALSE);
    //  salamander->AddPanelArchiver("doc;xls", FALSE, FALSE);
}

CPluginInterfaceForArchiverAbstract*
CPluginInterface::GetInterfaceForArchiver()
{
    CALL_STACK_MESSAGE_NONE
    return &InterfaceForArchiver;
}

int SortByExtDirsAsFiles = FALSE; // global variable that must be restored before calling ParseStorage

BOOL ParseStorage(CSalamanderDirectoryAbstract* Dir, LPSTORAGE CF, LPMALLOC pIMalloc, std::wstring& path)
{
    STATSTG element;
    HRESULT hr;
    IEnumSTATSTG* pIEnum;
    CFileData fileData;
    const size_t oldPathLen = path.size();
    FILETIME* pFT;
    BOOL ret = TRUE;
    int tmp;

    hr = CF->EnumElements(0, NULL, 0 /*threeth:)) reserved*/, &pIEnum);
    if (FAILED(hr))
        return FALSE;
    while (!FAILED(hr))
    {
        hr = pIEnum->Next(1, &element, NULL);
        if (FAILED(hr) || !element.pwcsName)
            break;

        fileData.Name = SalamanderGeneral->DupStr(element.pwcsName);
        if (!fileData.Name)
        {
            SalamanderGeneral->ShowMessageBox(SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_LOWMEM).c_str(), SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_PLUGINNAME).c_str(), MSGBOX_ERROR);
            ret = FALSE;
            pIMalloc->Free(element.pwcsName);
            continue;
        }
        fileData.Ext = wcsrchr(fileData.Name, L'.');
        if (fileData.Ext != NULL)
            fileData.Ext++; // ".cvspass" is an extension on Windows
        else
            fileData.Ext = fileData.Name + wcslen(fileData.Name);
        fileData.Size = CQuadWord(element.cbSize.u.LowPart, element.cbSize.u.HighPart);
        fileData.Attr = 0;
        if (element.type == STGTY_STORAGE)
        {
            fileData.Attr |= FILE_ATTRIBUTE_DIRECTORY;
        }
        fileData.Hidden = 0;
        fileData.PluginData = -1; // unnecessary, just for formality
        if (element.mtime.dwLowDateTime && element.mtime.dwHighDateTime)
        {
            pFT = &element.mtime;
        }
        else if (element.ctime.dwLowDateTime && element.ctime.dwHighDateTime)
        {
            pFT = &element.ctime;
        }
        else
        {
            pFT = &element.atime;
        }
        fileData.LastWrite.dwLowDateTime = pFT->dwLowDateTime;
        fileData.LastWrite.dwHighDateTime = pFT->dwHighDateTime;
        fileData.DosName = NULL;
        fileData.NameLen = static_cast<int>(wcslen(fileData.Name));
        fileData.IsOffline = 0;
        if (element.type == STGTY_STORAGE)
        {
            fileData.IsLink = 0;
            if (!SortByExtDirsAsFiles)
                fileData.Ext = fileData.Name + fileData.NameLen; // directories do not have extensions
            tmp = Dir->AddDir(path.c_str(), fileData, NULL);
        }
        else
        {
            fileData.IsLink = SalamanderGeneral->IsFileLink(fileData.Ext);
            tmp = Dir->AddFile(path.c_str(), fileData, NULL);
        }
        if (!tmp)
        {
            free(fileData.Name);
            SalamanderGeneral->ShowMessageBox(SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_LIST).c_str(), SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_PLUGINNAME).c_str(), MSGBOX_ERROR);
            ret = FALSE;
        }
        if (element.type == STGTY_STORAGE)
        {
            LPSTORAGE pSubStorage;

            SPLSalPathAppendOwned(path, element.pwcsName);
            hr = CF->OpenStorage(element.pwcsName, NULL /*priority*/,
                                 /*STGM_READ | */ STGM_DIRECT | STGM_SHARE_EXCLUSIVE /*| STGM_SHARE_DENY_WRITE/*EXCLUSIVE*/, NULL /*skip*/,
                                 0 /*reserved*/, &pSubStorage);
            if (SUCCEEDED(hr))
            {
                ret &= ParseStorage(Dir, pSubStorage, pIMalloc, path);
                pSubStorage->Release();
            }
            path.resize(oldPathLen);
        }
        pIMalloc->Free(element.pwcsName);
    }
    pIEnum->Release();
    return ret;
}

// ****************************************************************************
//
// CPluginInterfaceForArchiver
//

BOOL OpenStorage(const wchar_t* fileName, LPSTORAGE* ppStorage, LPMALLOC* ppIMalloc)
{
    HRESULT hr;

    hr = StgOpenStorage(fileName, NULL, STGM_DIRECT | STGM_READ | STGM_SHARE_DENY_WRITE /*EXCLUSIVE*/, NULL, 0, ppStorage);
    if (FAILED(hr))
    {
        std::wstring message = SPLFormatStringOwned(
            SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_CANNOT_OPEN).c_str(), fileName);
        message.append(SPLGetErrorTextOwned(SalamanderGeneral, static_cast<DWORD>(hr)));
        SalamanderGeneral->ShowMessageBox(message.c_str(), SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_PLUGINNAME).c_str(), MSGBOX_ERROR);
        return FALSE;
    }
    hr = CoGetMalloc(MEMCTX_TASK, ppIMalloc);
    if (FAILED(hr))
    {
        Error(hr, IDS_MEMORY_ALLOCATOR);
        (*ppStorage)->Release();
        return FALSE;
    }
    return TRUE;
} /* OpenStorage */

BOOL CPluginInterfaceForArchiver::ListArchive(CSalamanderForOperationsAbstract* salamander, const wchar_t* fileName,
                                               CSalamanderDirectoryAbstract* dir,
                                               CPluginDataInterfaceAbstract*& pluginData)
{
    CALL_STACK_MESSAGE2("CPluginInterfaceForArchiver::ListArchive(, %ls, ,)", fileName);
    BOOL ret;
    std::wstring path;
    LPSTORAGE pStorage;
    LPMALLOC pIMalloc;

    if (!OpenStorage(fileName, &pStorage, &pIMalloc))
    {
        return FALSE;
    }

    SalamanderGeneral->GetConfigParameter(SALCFG_SORTBYEXTDIRSASFILES, &SortByExtDirsAsFiles,
                                          sizeof(SortByExtDirsAsFiles), NULL);

    // path is used to accumulate path inside the CF
    ret = ParseStorage(dir, pStorage, pIMalloc, path);
    pStorage->Release();
    pIMalloc->Release();
    return ret;
}

BOOL CPluginInterfaceForArchiver::UnpackArchive(CSalamanderForOperationsAbstract* salamander, const wchar_t* fileName,
                                                CPluginDataInterfaceAbstract* pluginData, const wchar_t* targetDir,
                                                const wchar_t* archiveRoot, SalEnumSelection next, void* nextParam)
{
    CALL_STACK_MESSAGE4("CPluginInterfaceForArchiver::UnpackArchive(, %ls, , %ls, %ls, ,)", fileName, targetDir, archiveRoot);

    LPSTORAGE pStorage;
    LPMALLOC pIMalloc;
    BOOL ret = TRUE;

    if (!OpenStorage(fileName, &pStorage, &pIMalloc))
    {
        return FALSE;
    }

    salamander->OpenProgressDialog(SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_EXTRPROGTITLE).c_str(), TRUE, NULL, FALSE);

    // unpack the files

    pStorage->Release();
    pIMalloc->Release();
    salamander->CloseProgressDialog();

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

    LPSTORAGE pStorage;
    LPMALLOC pIMalloc;
    BOOL ret = TRUE;
    LPSTREAM pStream;

    if (!OpenStorage(fileName, &pStorage, &pIMalloc))
    {
        return FALSE;
    }
    // extract the file
    if (!wcschr(nameInArchive, L'\\'))
    {
        HRESULT hr;
        std::wstring targetFileName(targetDir);
        char* pBuf;
        HANDLE hFile;

        pBuf = (char*)malloc(BUF_SIZE);
        if (pBuf)
        {
            hr = pStorage->OpenStream(nameInArchive, NULL, STGM_DIRECT | STGM_READ | STGM_SHARE_EXCLUSIVE, NULL, &pStream);
            if (SUCCEEDED(hr))
            {
                SPLSalPathAppendOwned(targetFileName, nameInArchive);
                hFile = CreateFileW(targetFileName.c_str(), GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_NEW, 0, NULL);
                if (hFile != INVALID_HANDLE_VALUE)
                {
                    for (;;)
                    {
                        ULONG nBytesRead;
                        DWORD nBytesWritten;

                        hr = pStream->Read(pBuf, BUF_SIZE, &nBytesRead);
                        if (FAILED(hr))
                        {
                            Error(hr, IDS_CANNOT_READ, targetFileName.c_str());
                            ret = FALSE;
                            break;
                        }
                        if (!nBytesRead)
                            break;
                        WriteFile(hFile, pBuf, nBytesRead, &nBytesWritten, NULL);
                        if (nBytesRead != nBytesWritten)
                        {
                            Error(GetLastError(), IDS_CANNOT_WRITE);
                            ret = FALSE;
                            break;
                        }
                    }
                    CloseHandle(hFile);
                }
                else
                {
                    Error(GetLastError(), IDS_CANNOT_CREATE_FILE, targetFileName.c_str());
                    ret = FALSE;
                }
                pStream->Release();
            }
            else
            {
                Error(hr, IDS_CANNOT_OPEN_STREAM, nameInArchive);
                ret = FALSE;
            }
        }
        else
        {
            SalamanderGeneral->ShowMessageBox(SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_LOWMEM).c_str(), SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_PLUGINNAME).c_str(), MSGBOX_ERROR);
            ret = FALSE;
        }
        if (pBuf)
            free(pBuf);
    }
    else
    {
        // not supported yet
        _ASSERTE(wcschr(nameInArchive, L'\\'));
    }
    pStorage->Release();
    pIMalloc->Release();

    return ret;
}

BOOL CPluginInterfaceForArchiver::UnpackWholeArchive(CSalamanderForOperationsAbstract* salamander, const wchar_t* fileName,
                                                     const wchar_t* mask, const wchar_t* targetDir, BOOL delArchiveWhenDone,
                                                     CDynamicString* archiveVolumes)
{
    CALL_STACK_MESSAGE5("CPluginInterfaceForArchiver::UnpackWholeArchive(, %ls, %ls, %ls, %d,)",
                        fileName, mask, targetDir, delArchiveWhenDone);

    LPSTORAGE pStorage;
    LPMALLOC pIMalloc;
    BOOL ret = TRUE;

    if (!OpenStorage(fileName, &pStorage, &pIMalloc))
    {
        return FALSE;
    }

    if (delArchiveWhenDone)
        archiveVolumes->Add(fileName, -2); // FIXME: once the plugin supports multi-volume archives, we must add all archive volumes here (so the entire archive is deleted)

    salamander->OpenProgressDialog(SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_EXTRPROGTITLE).c_str(), FALSE, NULL, TRUE);

    // Unpack the compound file

    pStorage->Release();
    pIMalloc->Release();
    salamander->CloseProgressDialog();

    return ret;
}

BOOL Error(HRESULT hr, int error, ...)
{
    CALL_STACK_MESSAGE_NONE

    CALL_STACK_MESSAGE2("CPluginInterfaceForArchiver::Error(%d, )", error);
    va_list arglist;
    va_start(arglist, error);
    std::wstring message = SPLFormatStringOwnedV(
        SPLLoadStrOwned(SalamanderGeneral, HLanguage, error).c_str(), arglist);
    va_end(arglist);
    if (hr != S_OK)
        message.append(SPLGetErrorTextOwned(SalamanderGeneral, static_cast<DWORD>(hr)));
    SalamanderGeneral->ShowMessageBox(message.c_str(), SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_PLUGINNAME).c_str(), MSGBOX_ERROR);

    return FALSE;
}

BOOL CPluginInterfaceForArchiver::Init()
{
    CALL_STACK_MESSAGE1("CPluginInterfaceForArchiver::Init()");

    return TRUE;
}
