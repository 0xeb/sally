// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"

#include "dumpmem.h"
#include "array2.h"

#include "..\dll\pakiface.h"
#include "pak.rh"
#include "pak.rh2"
#include "lang\lang.rh"
#include "pak.h"
#include "pak_text.h"

// ****************************************************************************

HINSTANCE DLLInstance = NULL; // handle to the SPL - language-independent resources
HINSTANCE HLanguage = NULL;   // handle to the SLG - language-dependent resources
//HINSTANCE PakLibDLL = NULL;

/*
FPAKGetIFace PAKGetIFace;
FPAKReleaseIFace PAKReleaseIFace;
*/

// plugin interface object whose methods are called from Salamander
CPluginInterface PluginInterface;
// additional parts of the CPluginInterface interface
CPluginInterfaceForArchiver InterfaceForArchiver;
CPluginInterfaceForMenuExt InterfaceForMenuExt;

// Salamander's general interface - valid from startup until the plugin shuts down
CSalamanderGeneralAbstract* SalamanderGeneral = NULL;
// interface for comfortable work with files
CSalamanderSafeFileAbstract* SalamanderSafeFile = NULL;

// variable definition for "dbg.h"
CSalamanderDebugAbstract* SalamanderDebug = NULL;

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

    // this plugin is made for the current version of Salamander and newer - perform a check
    if (salamander->GetVersion() < LAST_VERSION_OF_SALAMANDER)
    { // reject older versions
        // wide: same call-site-local widen shape as checksum/unlha/undelete/zip/
        // splitcbn (205-210).
#define PAK_WIDEN2(x) L##x
#define PAK_WIDEN(x) PAK_WIDEN2(x)
        MessageBoxW(salamander->GetParentWindow(),
                    PAK_WIDEN(REQUIRE_LAST_VERSION_OF_SALAMANDER),
                    L"PAK" /* neprekladat! */, MB_OK | MB_ICONERROR);
#undef PAK_WIDEN
#undef PAK_WIDEN2
        return NULL;
    }

    // let the language module (.slg) load
    HLanguage = salamander->LoadLanguageModule(salamander->GetParentWindow(), L"PAK" /* neprekladat! */);
    if (HLanguage == NULL)
        return NULL;

    // obtain Salamander's general interface
    SalamanderGeneral = salamander->GetSalamanderGeneral();
    SalamanderSafeFile = salamander->GetSalamanderSafeFile();

    // set the help file name
    SalamanderGeneral->SetHelpFileName(L"pak.chm");

    /*
  //beta valid until the end of February 2001
  SYSTEMTIME st;
  GetLocalTime(&st);
  if (st.wYear == 2001 && st.wMonth > 2 || st.wYear > 2001)
  {
    SalamanderGeneral->ShowMessageBox(LangStr(IDS_EXPIRE).c_str(), LangStr(IDS_PLUGINNAME).c_str(), MSGBOX_INFO);
    return NULL;
  }
  */

    /*
  char buf[1024];
  BOOL ok = FALSE;
  if (GetModuleFileName(DLLInstance, buf, 1024))
  {
    PathRemoveFileSpec(buf);
    PathAppend(buf, "paklib.dll");
    PakLibDLL = LoadLibrary(buf);
    if (PakLibDLL)
    {
      if ((PAKGetIFace = (FPAKGetIFace) GetProcAddress(PakLibDLL, "PAKGetIFace")) &&
          (PAKReleaseIFace = (FPAKReleaseIFace) GetProcAddress(PakLibDLL, "PAKReleaseIFace")))
      {
        ok = TRUE;
      }
    }
  }
  if (!ok)
  {
    lstrcpyW(buf, LangStr(IDS_ERRLOADPAKLIB).c_str());
    FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL,
                GetLastError(), MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                buf + lstrlenW(buf), 1024 - lstrlenW(buf), NULL);
    MessageBox(salamander->GetParentWindow(), buf, LangStr(IDS_PLUGINNAME).c_str(), MB_OK | MB_ICONERROR);
    return NULL;
  }
*/

    // set the basic information about the plugin
    salamander->SetBasicPluginData(LangStr(IDS_PLUGINNAME).c_str(),
                                   FUNCTION_PANELARCHIVERVIEW | FUNCTION_PANELARCHIVEREDIT |
                                       FUNCTION_CUSTOMARCHIVERPACK | FUNCTION_CUSTOMARCHIVERUNPACK,
                                   _CRT_WIDE(VERSINFO_VERSION_NO_PLATFORM),
                                   _CRT_WIDE(VERSINFO_COPYRIGHT),
                                   LangStr(IDS_PLUGIN_DESCRIPTION).c_str(),
                                   NULL, L"pak");

    // set the plugin home page URL
    salamander->SetPluginHomePageURL(L"https://github.com/0xeb/sally");

    return &PluginInterface;
}

// ****************************************************************************
//
// CFileInfo
//

CFileInfo::CFileInfo(std::wstring name, DWORD status, DWORD dirDepth, DWORD size)
    : Name(std::move(name)), Status(status), DirDepth(dirDepth), Size(size)
{
    CALL_STACK_MESSAGE_NONE
}

CFileInfo* NewFileInfo(std::wstring name, DWORD status, DWORD dirDepth, DWORD size) noexcept
{
    try
    {
        return new CFileInfo(std::move(name), status, dirDepth, size);
    }
    catch (...)
    {
        return nullptr;
    }
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

BOOL CPluginInterface::Release(HWND parent, BOOL force)
{
    CALL_STACK_MESSAGE2("CPluginInterface::Release(, %d)", force);
    //  if (PakLibDLL) FreeLibrary(PakLibDLL);
    return TRUE;
}

void CPluginInterface::Connect(HWND parent, CSalamanderConnectAbstract* salamander)
{
    CALL_STACK_MESSAGE1("CPluginInterface::Connect(,)");

    salamander->AddCustomPacker(L"PAK (Plugin)", L"pak", FALSE);
    salamander->AddCustomUnpacker(L"PAK (Plugin)", L"*.pak", FALSE);
    salamander->AddPanelArchiver(L"pak", TRUE, FALSE);

    // j.r. This item needlessly haunts the menu; probably nobody uses it anymore.
    // I'll remove it experimentally and see if anyone speaks up.
    /*
  
///* serves the export_mnu.py script, which generates salmenu.mnu for the Translator
//   keep synchronized with the salamander->AddMenuItem() call below...
//MENU_TEMPLATE_ITEM PluginMenu[] = 
//{
//  {MNTT_PB, 0
//  {MNTT_IT, IDS_MENUOPTIMIZE
//  {MNTT_PE, 0
//};
//* /
  
  salamander->AddMenuItem(-1, LangStr(IDS_MENUOPTIMIZE).c_str(), 0, OPTIMIZE_MENUID, TRUE, 0, 0,
                          MENU_SKILLLEVEL_INTERMEDIATE | MENU_SKILLLEVEL_ADVANCED);
  // set the plugin's icon
  HBITMAP hBmp = (HBITMAP)LoadImage(DLLInstance, MAKEINTRESOURCE(IDB_PAK),
                                    IMAGE_BITMAP, 16, 16, LR_DEFAULTCOLOR);
  salamander->SetBitmapWithIcons(hBmp);
  DeleteObject(hBmp);
  salamander->SetPluginIcon(0);
  salamander->SetPluginMenuAndToolbarIcon(0);
*/
}

CPluginInterfaceForArchiverAbstract*
CPluginInterface::GetInterfaceForArchiver()
{
    CALL_STACK_MESSAGE_NONE
    return &InterfaceForArchiver;
}

CPluginInterfaceForMenuExtAbstract*
CPluginInterface::GetInterfaceForMenuExt()
{
    CALL_STACK_MESSAGE_NONE
    return &InterfaceForMenuExt;
}

// ****************************************************************************
//
// CPluginInterfaceForArchiver
//

BOOL CPluginInterfaceForArchiver::ListArchive(CSalamanderForOperationsAbstract* salamander, const wchar_t* fileName,
                                              CSalamanderDirectoryAbstract* dir,
                                              CPluginDataInterfaceAbstract*& pluginData)
{
    CALL_STACK_MESSAGE2("CPluginInterfaceForArchiver::ListArchive(, %ls, ,)", fileName);
    pluginData = NULL;
    BOOL ret = TRUE;

    PakIFace = PAKGetIFace();
    if (!PakIFace)
    {
        SalamanderGeneral->ShowMessageBox(LangStr(IDS_LOWMEM).c_str(), LangStr(IDS_PLUGINNAME).c_str(), MSGBOX_ERROR);
        return FALSE;
    }

    Salamander = salamander;
    PakFileName = fileName;

    CPakCallbacks pakCalls(this);
    PakIFace->Init(&pakCalls);

    if (!PakIFace->OpenPak(fileName, OP_READ_MODE))
        ret = FALSE;
    else
    {
        std::string file;
        DWORD size;
        if (!ReadFirstPakEntry(PakIFace, file, size))
            ret = FALSE;
        else
        {
            CFileData fileData;
            while (!file.empty())
            {
                const size_t slash = file.find_last_of('\\');
                const std::string_view pathBytes = slash == std::string::npos
                                                       ? std::string_view()
                                                       : std::string_view(file).substr(0, slash);
                const std::string_view nameBytes = slash == std::string::npos
                                                       ? std::string_view(file)
                                                       : std::string_view(file).substr(slash + 1);
                std::wstring path;
                std::wstring name;
                if (DecodePakText(pathBytes, path) != PakTextStatus::Success ||
                    DecodePakText(nameBytes, name) != PakTextStatus::Success)
                {
                    SalamanderGeneral->ShowMessageBox(LangStr(IDS_ERRLIST).c_str(), LangStr(IDS_PLUGINNAME).c_str(), MSGBOX_ERROR);
                    ret = FALSE;
                    break;
                }
                fileData.Name = SalamanderGeneral->DupStr(name.c_str());
                if (!fileData.Name)
                {
                    SalamanderGeneral->ShowMessageBox(LangStr(IDS_LOWMEM).c_str(), LangStr(IDS_PLUGINNAME).c_str(), MSGBOX_ERROR);
                    ret = FALSE;
                    break;
                }
                fileData.Ext = wcsrchr(fileData.Name, L'.');
                if (fileData.Ext != NULL)
                    fileData.Ext++; // ".cvspass" is an extension in Windows
                else
                    fileData.Ext = fileData.Name + wcslen(fileData.Name);
                fileData.Size = CQuadWord(size, 0);
                fileData.Attr = FILE_ATTRIBUTE_NORMAL;
                fileData.Hidden = 0;
                fileData.PluginData = -1; // unnecessary, just for form's sake
                PakIFace->GetPakTime(&fileData.LastWrite);
                fileData.DosName = NULL;
                fileData.NameLen = (unsigned)wcslen(fileData.Name);
                fileData.IsLink = SalamanderGeneral->IsFileLink(fileData.Ext);
                fileData.IsOffline = 0;
                if (!dir->AddFile(path.c_str(), fileData, NULL))
                {
                    SalamanderGeneral->Free(fileData.Name);
                    SalamanderGeneral->ShowMessageBox(LangStr(IDS_ERRLIST).c_str(), LangStr(IDS_PLUGINNAME).c_str(), MSGBOX_ERROR);
                    break;
                }
                if (!ReadNextPakEntry(PakIFace, file, size))
                {
                    ret = FALSE;
                    break;
                }
            }
        }
        PakIFace->ClosePak();
    }

    PAKReleaseIFace(PakIFace);

    return ret;
}

BOOL CPluginInterfaceForArchiver::MakeFileList(TIndirectArray2<CFileInfo>& files, std::string_view archiveRoot,
                                               SalEnumSelection next, void* nextParam)
{
    CALL_STACK_MESSAGE1("CPluginInterface::MakeFileList(, , , )");
    const wchar_t* nextName;
    std::string nextFull;
    BOOL isDir;
    std::string file;
    DWORD size;

    ProgressTotal = CQuadWord(0, 0);

    while ((nextName = next(NULL, 0, &isDir, NULL, NULL, nextParam, NULL)) != NULL)
    {
        if (JoinPakEntryName(archiveRoot, nextName, nextFull) != PakTextStatus::Success)
        {
            SalamanderGeneral->DialogError(SalamanderGeneral->GetMsgBoxParent(), BUTTONS_OK,
                                           nextName, LangStr(IDS_TOOLONGNAME2).c_str(), NULL);
            return FALSE;
        }
        const int len = static_cast<int>(nextFull.size());
        if (!ReadFirstPakEntry(PakIFace, file, size))
            return FALSE;
        while (!file.empty())
        {
            if (file.size() >= nextFull.size() &&
                CompareStringA(LOCALE_USER_DEFAULT, NORM_IGNORECASE, nextFull.c_str(), len, file.c_str(), len) == CSTR_EQUAL &&
                (file[len] == 0 || file[len] == '\\'))
            {
                std::wstring decoded;
                if (DecodePakText(file, decoded) != PakTextStatus::Success)
                    return FALSE;
                CFileInfo* f = NewFileInfo(std::move(decoded), 0, 0, 0);
                if (!f)
                {
                    SalamanderGeneral->ShowMessageBox(LangStr(IDS_LOWMEM).c_str(), LangStr(IDS_PLUGINNAME).c_str(), MSGBOX_ERROR);
                    return FALSE;
                }
                if (!files.Add(f))
                {
                    delete f;
                    SalamanderGeneral->ShowMessageBox(LangStr(IDS_LOWMEM).c_str(), LangStr(IDS_PLUGINNAME).c_str(), MSGBOX_ERROR);
                    return FALSE;
                }
                ProgressTotal += CQuadWord(size, 0);
                if (!isDir)
                    break;
            }
            if (!ReadNextPakEntry(PakIFace, file, size))
                return FALSE;
        }
    }
    return TRUE;
}

BOOL CPluginInterfaceForArchiver::UnpackFiles(TIndirectArray2<CFileInfo>& files, const wchar_t* arcFile,
                                              int rootLen, const wchar_t* targetDir)
{
    CALL_STACK_MESSAGE4("CPluginInterfaceForArchiver::UnpackFiles(, %ls, %d, %ls)", arcFile,
                        rootLen, targetDir);
    std::string file;
    DWORD size;
    CFileInfo* info;
    CQuadWord currentProgress(0, 0);
    unsigned left = files.Count;
    CQuadWord q;

    if (!ReadFirstPakEntry(PakIFace, file, size))
        return FALSE;
    while (!file.empty() && left)
    {
        std::wstring decodedFile;
        if (DecodePakText(file, decodedFile) != PakTextStatus::Success)
            return FALSE;
        int i;
        for (i = 0; i < files.Count; i++)
        {
            info = files[i];
            if (info->Status == STATUS_OK)
                continue;
            if (CompareStringW(LOCALE_USER_DEFAULT, NORM_IGNORECASE, info->Name.c_str(), -1,
                               decodedFile.c_str(), -1) == CSTR_EQUAL)
            {
                const std::wstring message = LangStr(IDS_EXTRACTING) + decodedFile;
                Salamander->ProgressDialogAddText(message.c_str(), TRUE);
                // Declare path owners before the goto block to avoid skipping initialization.
                std::wstring targetName(targetDir);
                std::wstring arcName(arcFile);
                if (rootLen < 0 || static_cast<size_t>(rootLen) > file.size())
                    return FALSE;
                std::wstring relativeName;
                if (DecodePakText(std::string_view(file).substr(static_cast<size_t>(rootLen)), relativeName) != PakTextStatus::Success)
                    return FALSE;
                SPLSalPathAppendOwned(targetName, relativeName.c_str());
                IOFileName = targetName.c_str();
                SPLSalPathAppendOwned(arcName, decodedFile.c_str());
                FILETIME ft;
                PakIFace->GetPakTime(&ft);
                const std::wstring infoText = GetInfo(&ft, size);
                BOOL skip;
                q = CQuadWord(size, 0);
                bool allocate;
                allocate = CQuadWord(2, 0) < q && q < CQuadWord(0, 0x80000000);
                q += CQuadWord(0, 0x80000000);
                IOFile = SalamanderSafeFile->SafeFileCreate(targetName.c_str(), GENERIC_WRITE,
                                                            FILE_SHARE_READ, FILE_ATTRIBUTE_NORMAL, FALSE,
                                                            SalamanderGeneral->GetMsgBoxParent(), arcName.c_str(), infoText.c_str(), &Silent, TRUE,
                                                            &skip, NULL, 0, allocate ? &q : NULL, NULL);
                if (skip)
                    goto l_next;
                if (IOFile == INVALID_HANDLE_VALUE)
                    return FALSE;
                BOOL ret;
                Abort = TRUE;
                if (!Salamander->ProgressSetSize(CQuadWord(0, 0), CQuadWord(-1, -1), TRUE))
                {
                    CloseHandle(IOFile);
                    return FALSE;
                }
                Salamander->ProgressSetTotalSize(CQuadWord(size, 0), ProgressTotal);
                ret = PakIFace->ExtractFile();
                SetFileTime(IOFile, NULL, NULL, &ft);
                CloseHandle(IOFile);
                if (!ret)
                {
                    DeleteFileW(targetName.c_str());
                    if (Abort)
                        return FALSE;
                }

            l_next:
                currentProgress += CQuadWord(size, 0);
                if (!Salamander->ProgressSetSize(CQuadWord(size, 0), currentProgress, TRUE))
                    return FALSE;
                left--;
                break;
            }
        }
        if (!ReadNextPakEntry(PakIFace, file, size))
            return FALSE;
    }
    return TRUE;
}

BOOL CPluginInterfaceForArchiver::UnpackArchive(CSalamanderForOperationsAbstract* salamander, const wchar_t* fileName,
                                                CPluginDataInterfaceAbstract* pluginData, const wchar_t* targetDir,
                                                const wchar_t* archiveRoot, SalEnumSelection next, void* nextParam)
{
    CALL_STACK_MESSAGE4("CPluginInterfaceForArchiver::UnpackArchive(, %ls, , %ls, %ls, ,)", fileName, targetDir, archiveRoot);

    PakIFace = PAKGetIFace();
    if (!PakIFace)
    {
        SalamanderGeneral->ShowMessageBox(LangStr(IDS_LOWMEM).c_str(), LangStr(IDS_PLUGINNAME).c_str(), MSGBOX_ERROR);
        return FALSE;
    }

    BOOL ret = TRUE;
    Salamander = salamander;
    PakFileName = fileName;
    Silent = 0;
    Abort = FALSE;

    CPakCallbacks pakCalls(this);
    PakIFace->Init(&pakCalls);
    const std::wstring title = SPLFormatStringOwned(
        LangStr(IDS_EXTRPROGTITLE).c_str(), SalamanderGeneral->SalPathFindFileName(PakFileName));
    Salamander->OpenProgressDialog(title.c_str(), TRUE, NULL, FALSE);
    Salamander->ProgressDialogAddText(LangStr(IDS_PREPAREDATA).c_str(), FALSE);

    if (!PakIFace->OpenPak(fileName, OP_READ_MODE))
        ret = FALSE;
    else
    {
        TIndirectArray2<CFileInfo> files(256);
        std::string arcRootStr;
        if (EncodePakEntryName(archiveRoot, arcRootStr) != PakTextStatus::Success)
        {
            SalamanderGeneral->DialogError(SalamanderGeneral->GetMsgBoxParent(), BUTTONS_OK,
                                           archiveRoot, LangStr(IDS_TOOLONGNAME2).c_str(), NULL);
            ret = FALSE;
        }
        else
        {
            std::string_view arcRoot(arcRootStr);
            if (!arcRoot.empty() && arcRoot.front() == '\\')
                arcRoot.remove_prefix(1);
            if (!MakeFileList(files, arcRoot, next, nextParam))
                ret = FALSE;
            if (ret)
            {
                if (!SalamanderGeneral->TestFreeSpace(SalamanderGeneral->GetMsgBoxParent(),
                                                      targetDir, ProgressTotal, LangStr(IDS_PLUGINNAME).c_str()))
                    ret = FALSE;
                else
                {
                    Salamander->ProgressDialogAddText(LangStr(IDS_EXTRACTFILES).c_str(), FALSE);
                    if (!UnpackFiles(files, fileName, static_cast<int>(arcRoot.size()), targetDir))
                        ret = FALSE;
                }
            }
        }
        PakIFace->ClosePak();
    }

    Salamander->CloseProgressDialog();

    PAKReleaseIFace(PakIFace);

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

    PakIFace = PAKGetIFace();
    if (!PakIFace)
    {
        SalamanderGeneral->ShowMessageBox(LangStr(IDS_LOWMEM).c_str(), LangStr(IDS_PLUGINNAME).c_str(), MSGBOX_ERROR);
        return FALSE;
    }

    BOOL ret = FALSE;
    Salamander = salamander;
    PakFileName = fileName;
    Silent = 0;
    Abort = FALSE;

    CPakCallbacks pakCalls(this);
    PakIFace->Init(&pakCalls);

    if (PakIFace->OpenPak(fileName, OP_READ_MODE))
    {
        DWORD size;
        std::string nameInArchiveA;
        if (EncodePakEntryName(nameInArchive, nameInArchiveA) == PakTextStatus::Success &&
            PakIFace->FindFile(nameInArchiveA.c_str(), &size) && size != -1)
        {
            std::wstring targetName;
            const wchar_t* name = wcsrchr(nameInArchive, L'\\');
            if (name)
                name++;
            else
                name = nameInArchive;
            targetName = targetDir;
            SPLSalPathAppendOwned(targetName, name);
            {
                IOFileName = targetName.c_str();
                IOFile = CreateFileW(targetName.c_str(), GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS,
                                     FILE_ATTRIBUTE_NORMAL, NULL);
                if (IOFile == INVALID_HANDLE_VALUE)
                {
                    const std::wstring error = LangStr(IDS_ERROPEN) +
                                               SPLGetErrorTextOwned(SalamanderGeneral, GetLastError());
                    SalamanderGeneral->ShowMessageBox(error.c_str(), LangStr(IDS_PLUGINNAME).c_str(), MSGBOX_ERROR);
                }
                else
                {
                    BOOL r = PakIFace->ExtractFile();
                    FILETIME ft;
                    PakIFace->GetPakTime(&ft);
                    SetFileTime(IOFile, NULL, NULL, &ft);
                    CloseHandle(IOFile);
                    if (r)
                        ret = TRUE;
                    else
                        DeleteFileW(targetName.c_str());
                }
            }
        }
        PakIFace->ClosePak();
    }

    PAKReleaseIFace(PakIFace);
    return ret;
}

BOOL CPluginInterfaceForArchiver::ConstructMaskArray(TIndirectArray2<std::wstring>& maskArray, const wchar_t* masks)
{
    CALL_STACK_MESSAGE2("CPluginInterfaceForArchiver::ConstructMaskArray(, %ls)", masks);
    const wchar_t* sour;
    sour = masks;
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
        while (!buffer.empty() && buffer.back() <= L' ')
            buffer.pop_back();
        size_t first = 0;
        while (first < buffer.size() && buffer[first] <= L' ')
            ++first;
        const std::wstring sourceMask = buffer.substr(first);
        const std::wstring preparedMask =
            SPLPrepareMaskOwned(SalamanderGeneral, sourceMask.c_str());
        if (!preparedMask.empty())
        {
            std::wstring* newMask = nullptr;
            try
            {
                newMask = new std::wstring(preparedMask);
            }
            catch (...)
            {
            }
            if (!newMask)
            {
                SalamanderGeneral->ShowMessageBox(LangStr(IDS_LOWMEM).c_str(), LangStr(IDS_PLUGINNAME).c_str(), MSGBOX_ERROR);
                return FALSE;
            }
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

BOOL CPluginInterfaceForArchiver::MakeFileList2(TIndirectArray2<std::wstring>& masks, TIndirectArray2<CFileInfo>& files)
{
    CALL_STACK_MESSAGE1("CPluginInterface::MakeFileList2(, )");
    BOOL ret = TRUE;
    ProgressTotal = CQuadWord(0, 0);
    std::string file;
    DWORD size;

    if (!ReadFirstPakEntry(PakIFace, file, size))
        return FALSE;

    while (!file.empty())
    {
        std::wstring decodedFile;
        if (DecodePakText(file, decodedFile) != PakTextStatus::Success)
            return FALSE;
        const wchar_t* fileName = SalamanderGeneral->SalPathFindFileName(decodedFile.c_str());
        BOOL fileNameHasExt = wcschr(fileName, L'.') != NULL; // ".cvspass" is an extension in Windows
        int i;
        for (i = 0; i < masks.Count; i++)
        {
            if (SalamanderGeneral->AgreeMask(fileName, masks[i]->c_str(), fileNameHasExt))
            {
                CFileInfo* f = NewFileInfo(std::move(decodedFile), 0, 0, 0);
                if (!f)
                {
                    SalamanderGeneral->ShowMessageBox(LangStr(IDS_LOWMEM).c_str(), LangStr(IDS_PLUGINNAME).c_str(), MSGBOX_ERROR);
                    return FALSE;
                }
                if (!files.Add(f))
                {
                    delete f;
                    SalamanderGeneral->ShowMessageBox(LangStr(IDS_LOWMEM).c_str(), LangStr(IDS_PLUGINNAME).c_str(), MSGBOX_ERROR);
                    return FALSE;
                }
                ProgressTotal += CQuadWord(size, 0);
                break;
            }
        }
        if (!ReadNextPakEntry(PakIFace, file, size))
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

    TIndirectArray2<std::wstring> masks(16);

    Salamander = salamander;

    if (!ConstructMaskArray(masks, mask) || masks.Count == 0)
        return FALSE;

    PakIFace = PAKGetIFace();
    if (!PakIFace)
    {
        SalamanderGeneral->ShowMessageBox(LangStr(IDS_LOWMEM).c_str(), LangStr(IDS_PLUGINNAME).c_str(), MSGBOX_ERROR);
        return FALSE;
    }

    BOOL ret = TRUE;
    if (delArchiveWhenDone)
        archiveVolumes->Add(fileName, -2);
    PakFileName = fileName;
    Silent = 0;
    Abort = FALSE;

    CPakCallbacks pakCalls(this);
    PakIFace->Init(&pakCalls);
    const std::wstring title = SPLFormatStringOwned(
        LangStr(IDS_EXTRPROGTITLE).c_str(), SalamanderGeneral->SalPathFindFileName(PakFileName));
    Salamander->OpenProgressDialog(title.c_str(), TRUE, NULL, FALSE);
    Salamander->ProgressDialogAddText(LangStr(IDS_PREPAREDATA).c_str(), FALSE);

    if (!PakIFace->OpenPak(fileName, OP_READ_MODE))
        ret = FALSE;
    else
    {
        TIndirectArray2<CFileInfo> files(256);
        if (!MakeFileList2(masks, files))
            ret = FALSE;
        if (ret)
        {
            if (!SalamanderGeneral->TestFreeSpace(SalamanderGeneral->GetMsgBoxParent(),
                                                  targetDir, ProgressTotal, LangStr(IDS_PLUGINNAME).c_str()))
                ret = FALSE;
            else
            {
                Salamander->ProgressDialogAddText(LangStr(IDS_EXTRACTFILES).c_str(), FALSE);
                if (!UnpackFiles(files, fileName, 0, targetDir))
                    ret = FALSE;
            }
        }
        PakIFace->ClosePak();
    }

    Salamander->CloseProgressDialog();

    PAKReleaseIFace(PakIFace);
    return ret;
}

/*
void CPluginInterfaceForArchiver::InitPlugin(CSalamanderForOperationsAbstract *salamander, CPakIfaceAbstract * pakIFace,
                const char * pakFileName)
{
  Salamander = salamander;
  PakIFace = pakIFace;
  PakFileName = pakFileName;
}
*/
/*
HWND
CPluginInterfaceForArchiver::GetParentWindow()
{
  HWND parent = Salamander->ProgressGetHWND();
  if (parent) return parent;
  return SalamanderGeneral->GetMainWindowHWND();
}
*/

// ****************************************************************************
//
// CPakCallbacks
//

CPakCallbacks::CPakCallbacks(CPluginInterfaceForArchiver* plugin)
{
    CALL_STACK_MESSAGE1("CPakCallbacks::CPakCallbacks()");
    Plugin = plugin;
    UserBreak = FALSE;
}

BOOL CPakCallbacks::HandleError(DWORD flags, int errorID, va_list arglist)
{
    CALL_STACK_MESSAGE3("CPakCallbacks::HandleError(0x%X, %d, )", flags, errorID);
    try
    {
        const std::wstring message = SPLFormatStringOwnedV(LangStr(errorID).c_str(), arglist);
        if (flags & HE_RETRY)
        {
            if (SalamanderGeneral->DialogError(SalamanderGeneral->GetMsgBoxParent(), BUTTONS_RETRYCANCEL,
                                               Plugin->PakFileName, message.c_str(), NULL) == DIALOG_RETRY)
                return TRUE;
        }
        else
        {
            SalamanderGeneral->DialogError(SalamanderGeneral->GetMsgBoxParent(), BUTTONS_OK,
                                           Plugin->PakFileName, message.c_str(), NULL);
        }
    }
    catch (...)
    {
        return FALSE;
    }
    return FALSE;
}

BOOL CPakCallbacks::SafeSeek(DWORD position)
{
    CALL_STACK_MESSAGE2("CPakCallbacks::SafeSeek(0x%X)", position);
    while (1)
    {
        if (SetFilePointer(Plugin->IOFile, position, NULL, FILE_BEGIN) != 0xFFFFFFFF)
            return TRUE;
        const std::wstring message = LangStr(IDS_UNABLESEEK) +
                                     SPLGetErrorTextOwned(SalamanderGeneral, GetLastError());
        if (Plugin->Silent & SF_IOERRORS)
        {
            Plugin->Abort = FALSE;
            return FALSE;
        }
        switch (SalamanderGeneral->DialogError(SalamanderGeneral->GetMsgBoxParent(), BUTTONS_RETRYSKIPCANCEL, Plugin->IOFileName, message.c_str(), NULL))
        {
        case DIALOG_SKIPALL:
            Plugin->Silent |= SF_IOERRORS;
        case DIALOG_SKIP:
            Plugin->Abort = FALSE;
            return FALSE;
        case DIALOG_CANCEL:
        case DIALOG_FAIL:
            return FALSE;
        }
    }
}

BOOL CPakCallbacks::Write(void* buffer, DWORD size)
{
    CALL_STACK_MESSAGE2("CPakCallbacks::Write(, 0x%X)", size);
    try
    {
    if (size == 0)
        return TRUE;
    DWORD pos;
    while (1)
    {
        pos = SetFilePointer(Plugin->IOFile, 0, NULL, FILE_CURRENT);
        if (pos != 0xFFFFFFFF)
            break;
        const std::wstring message = LangStr(IDS_UNABLEGETFIELPOS) +
                                     SPLGetErrorTextOwned(SalamanderGeneral, GetLastError());
        if (Plugin->Silent & SF_IOERRORS)
        {
            Plugin->Abort = FALSE;
            return FALSE;
        }
        switch (SalamanderGeneral->DialogError(SalamanderGeneral->GetMsgBoxParent(), BUTTONS_RETRYSKIPCANCEL, Plugin->IOFileName, message.c_str(), NULL))
        {
        case DIALOG_SKIPALL:
            Plugin->Silent |= SF_IOERRORS;
        case DIALOG_SKIP:
            Plugin->Abort = FALSE;
            return FALSE;
        case DIALOG_CANCEL:
        case DIALOG_FAIL:
            return FALSE;
        }
    }
    DWORD written;
    while (1)
    {
        if (WriteFile(Plugin->IOFile, buffer, size, &written, NULL))
            return TRUE;
        const std::wstring message = LangStr(IDS_UNABLEWRITE) +
                                     SPLGetErrorTextOwned(SalamanderGeneral, GetLastError());
        if (Plugin->Silent & SF_IOERRORS)
        {
            Plugin->Abort = FALSE;
            return FALSE;
        }
        switch (SalamanderGeneral->DialogError(SalamanderGeneral->GetMsgBoxParent(), BUTTONS_RETRYSKIPCANCEL, Plugin->IOFileName, message.c_str(), NULL))
        {
        case DIALOG_SKIPALL:
            Plugin->Silent |= SF_IOERRORS;
        case DIALOG_SKIP:
            Plugin->Abort = FALSE;
            return FALSE;
        case DIALOG_CANCEL:
        case DIALOG_FAIL:
            return FALSE;
        }
        if (!SafeSeek(pos))
            return FALSE;
    }
    }
    catch (...)
    {
        Plugin->Abort = FALSE;
        return FALSE;
    }
}

BOOL CPakCallbacks::AddProgress(unsigned size)
{
    CALL_STACK_MESSAGE2("CPakCallbacks::AddProgress(0x%X)", size);
    try
    {
        const BOOL ret = Plugin->Salamander->ProgressAddSize(size, TRUE);
        if (!ret)
        {
            if (!UserBreak)
                Plugin->Salamander->ProgressDialogAddText(LangStr(IDS_CANCELING).c_str(), FALSE);
            UserBreak = TRUE;
            Plugin->Salamander->ProgressEnableCancel(FALSE);
        }
        return ret;
    }
    catch (...)
    {
        UserBreak = TRUE;
        return FALSE;
    }
}

// ****************************************************************************
//
// Other functions
//

static std::wstring FormatPakLocalDateOrTime(const SYSTEMTIME& value, bool date)
{
    const int required = date ? GetDateFormatW(LOCALE_USER_DEFAULT, DATE_SHORTDATE, &value, NULL, NULL, 0)
                              : GetTimeFormatW(LOCALE_USER_DEFAULT, 0, &value, NULL, NULL, 0);
    if (required > 1)
    {
        std::wstring text(static_cast<size_t>(required), L'\0');
        const int written = date ? GetDateFormatW(LOCALE_USER_DEFAULT, DATE_SHORTDATE, &value, NULL, text.data(), required)
                                 : GetTimeFormatW(LOCALE_USER_DEFAULT, 0, &value, NULL, text.data(), required);
        if (written == required)
        {
            text.resize(static_cast<size_t>(written - 1));
            return text;
        }
    }
    if (date)
        return SPLFormatStringOwned(L"%u.%u.%u", value.wDay, value.wMonth, value.wYear);
    return SPLFormatStringOwned(L"%u:%02u:%02u", value.wHour, value.wMinute, value.wSecond);
}

std::wstring GetInfo(FILETIME* lastWrite, unsigned size)
{
    CALL_STACK_MESSAGE2("GetInfo(, , 0x%X)", size);
    SYSTEMTIME st;
    FILETIME ft;
    FileTimeToLocalFileTime(lastWrite, &ft);
    FileTimeToSystemTime(&ft, &st);

    const std::wstring date = FormatPakLocalDateOrTime(st, true);
    const std::wstring time = FormatPakLocalDateOrTime(st, false);
    const std::wstring number = SPLNumberToStrOwned(SalamanderGeneral, CQuadWord(size, 0));
    return SPLFormatStringOwned(L"%s, %s, %s", number.c_str(), date.c_str(), time.c_str());
}
