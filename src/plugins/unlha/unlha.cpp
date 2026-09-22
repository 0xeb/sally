// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"

#include "lha.h"
#include "unlha.h"
#include "unlha_text.h"
#include "unlha.rh"
#include "unlha.rh2"
#include "lang\lang.rh"

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

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved)
{
    if (fdwReason == DLL_PROCESS_ATTACH)
        DLLInstance = hinstDLL;
    return TRUE; // DLL can be loaded
}

// Wide. SalamanderGeneral->LoadStr has returned WCHAR* since the v108
// ABI break; this went through LoadStrNarrow and was widened again at every call site.
std::wstring LangStr(int resID)
{
    return SPLLoadStrOwned(SalamanderGeneral, HLanguage, resID);
}

//****************************************************************************

int WINAPI SalamanderPluginGetReqVer()
{
    return LAST_VERSION_OF_SALAMANDER;
}

// ****************************************************************************

CPluginInterfaceAbstract* WINAPI SalamanderPluginEntry(CSalamanderPluginEntryAbstract* salamander)
{
    // set SalamanderDebug for "dbg.h"
    SalamanderDebug = salamander->GetSalamanderDebug();

    CALL_STACK_MESSAGE1("SalamanderPluginEntry()");

    // this plugin is built for the current Salamander version and newer - perform a check
    if (salamander->GetVersion() < LAST_VERSION_OF_SALAMANDER)
    { // reject older versions
        // wide: same call-site-local widen shape as checksum.cpp (205) -
        // REQUIRE_LAST_VERSION_OF_SALAMANDER is a shared narrow SDK macro used by ~35 plugins,
        // so widen only here rather than at its definition.
        MessageBoxW(salamander->GetParentWindow(),
                    _CRT_WIDE(REQUIRE_LAST_VERSION_OF_SALAMANDER),
                    L"UnLHA" /* neprekladat! */, MB_OK | MB_ICONERROR);
        return NULL;
    }

    // load the language module (.slg)
    HLanguage = salamander->LoadLanguageModule(salamander->GetParentWindow(), L"UnLHA" /* neprekladat! */);
    if (HLanguage == NULL)
        return NULL;

    // obtain the general Salamander interface
    SalamanderGeneral = salamander->GetSalamanderGeneral();
    SalamanderSafeFile = salamander->GetSalamanderSafeFile();

    // initialize the unpacker
    LHAInit();

    // set basic information about the plugin
    salamander->SetBasicPluginData(LangStr(IDS_PLUGINNAME).c_str(),
                                   FUNCTION_PANELARCHIVERVIEW | FUNCTION_CUSTOMARCHIVERUNPACK,
                                   _CRT_WIDE(VERSINFO_VERSION_NO_PLATFORM),
                                   _CRT_WIDE(VERSINFO_COPYRIGHT),
                                   LangStr(IDS_PLUGIN_DESCRIPTION).c_str(),
                                   L"UnLHA" /* do not translate! */, L"lzh;lha;lzs");

    salamander->SetPluginHomePageURL(L"https://github.com/0xeb/sally");

    return &PluginInterface;
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

void CPluginInterface::LoadConfiguration(HWND parent, HKEY regKey, CSalamanderRegistryAbstract* registry)
{
    CALL_STACK_MESSAGE1("CPluginInterface::LoadConfiguration(, ,)");
}

void CPluginInterface::SaveConfiguration(HWND parent, HKEY regKey, CSalamanderRegistryAbstract* registry)
{
    CALL_STACK_MESSAGE1("CPluginInterface::SaveConfiguration(, ,)");
}

void CPluginInterface::Configuration(HWND parent)
{
}

void CPluginInterface::Connect(HWND parent, CSalamanderConnectAbstract* salamander)
{
    CALL_STACK_MESSAGE1("CPluginInterface::Connect(,)");

    salamander->AddCustomUnpacker(L"UnLHA (Plugin)", L"*.lzh;*.lha;*.lzs", FALSE);
    salamander->AddPanelArchiver(L"lzh;lha;lzs", FALSE, FALSE);
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

BOOL CPluginInterfaceForArchiver::ListArchive(CSalamanderForOperationsAbstract* salamander, const wchar_t* fileName,
                                              CSalamanderDirectoryAbstract* dir,
                                              CPluginDataInterfaceAbstract*& pluginData)
{
    CALL_STACK_MESSAGE2("CPluginInterfaceForArchiver::ListArchive(, %ls, ,)", fileName);
    pluginData = NULL;

    FILE* f;
    if (!LHAOpenArchive(f, fileName))
    {
        SalamanderGeneral->ShowMessageBox(LangStr(iLHAErrorStrId).c_str(), LangStr(IDS_PLUGINNAME).c_str(), MSGBOX_ERROR);
        return FALSE;
    }

    int sortByExtDirsAsFiles;
    SalamanderGeneral->GetConfigParameter(SALCFG_SORTBYEXTDIRSASFILES, &sortByExtDirsAsFiles,
                                          sizeof(sortByExtDirsAsFiles), NULL);

    LHA_HEADER hdr;
    CFileData fd;
    int ret = TRUE, gh;
    int count = 0;
    int symlinkinfo = 0, crcinfo = 0;
    int currentOffset = ftell(f);

    while ((gh = LHAGetHeader(f, &hdr)) == GH_SUCCESS)
    {
        std::wstring decodedName;
        if (!DecodeLhaName(hdr.name, decodedName))
        {
            SalamanderGeneral->ShowMessageBox(LangStr(IDS_FILECORRUPT).c_str(), LangStr(IDS_PLUGINNAME).c_str(), MSGBOX_ERROR);
            ret = FALSE;
            break;
        }
        if (!decodedName.empty() && decodedName.back() == L'\\')
            decodedName.pop_back();
        const size_t slash = decodedName.find_last_of(L'\\');
        const std::wstring path = slash == std::wstring::npos ? std::wstring() : decodedName.substr(0, slash);
        const wchar_t* name = slash == std::wstring::npos ? decodedName.c_str() : decodedName.c_str() + slash + 1;

        ZeroMemory(&fd, sizeof(fd)); // just to be safe...

        fd.Name = SalamanderGeneral->DupStr(name);
        if (!fd.Name)
        {
            SalamanderGeneral->ShowMessageBox(LangStr(IDS_LOWMEM).c_str(), LangStr(IDS_PLUGINNAME).c_str(), MSGBOX_ERROR);
            ret = FALSE;
            break;
        }

        fd.Ext = wcsrchr(fd.Name, L'.');
        if (fd.Ext != NULL)
            fd.Ext++; // ".cvspass" counts as an extension on Windows
        else
            fd.Ext = fd.Name + wcslen(fd.Name);
        fd.Size = CQuadWord(hdr.original_size, 0);
        fd.Attr = hdr.attribute;
        fd.Hidden = fd.Attr & FILE_ATTRIBUTE_HIDDEN ? 1 : 0;
        fd.PluginData = currentOffset;
        fd.LastWrite = hdr.last_modified_filetime;
        fd.DosName = NULL;
        fd.NameLen = (unsigned)wcslen(fd.Name);
        fd.IsOffline = 0;

        if ((hdr.unix_mode & UNIX_FILE_TYPEMASK) == UNIX_FILE_REGULAR &&
            hdr.method != LZHDIRS_METHOD_NUM)
        {
            fd.IsLink = SalamanderGeneral->IsFileLink(fd.Ext);
            if (!dir->AddFile(path.c_str(), fd, NULL))
                ret = FALSE;
        }
        else if ((hdr.unix_mode & UNIX_FILE_TYPEMASK) == UNIX_FILE_DIRECTORY ||
                 hdr.method == LZHDIRS_METHOD_NUM)
        {
            if (!sortByExtDirsAsFiles)
                fd.Ext = fd.Name + fd.NameLen; // directories have no extensions
            fd.IsLink = 0;
            if (!dir->AddDir(path.c_str(), fd, NULL))
                ret = FALSE;
        }
        else if (!symlinkinfo && (hdr.unix_mode & UNIX_FILE_TYPEMASK) == UNIX_FILE_SYMLINK)
        {
            SalamanderGeneral->ShowMessageBox(LangStr(IDS_SYMLINK).c_str(), LangStr(IDS_PLUGINNAME).c_str(), MSGBOX_WARNING);
            symlinkinfo = 1;
        }

        if (!ret)
        {
            SalamanderGeneral->Free(fd.Name);
            SalamanderGeneral->ShowMessageBox(LangStr(IDS_LIST).c_str(), LangStr(IDS_PLUGINNAME).c_str(), MSGBOX_ERROR);
            break;
        }

        fseek(f, hdr.packed_size, SEEK_CUR);
        count++;
        currentOffset = ftell(f);
    }

    fclose(f);

    if (gh == GH_ERROR)
    {
        SalamanderGeneral->ShowMessageBox(LangStr(iLHAErrorStrId).c_str(), LangStr(IDS_PLUGINNAME).c_str(), MSGBOX_ERROR);
        ret = FALSE;
    }

    // some files have already been listed, so do not pack and display them
    if (!ret && count)
        ret = TRUE;

    return ret;
}

static BOOL ProgressCallback(int size) // callback invoked during decompression
{
    if (InterfaceForArchiver.UnpackWhole)
        return InterfaceForArchiver.Salamander->ProgressSetSize(CQuadWord(size, 0), CQuadWord(-1, -1), TRUE);
    else
        return InterfaceForArchiver.Salamander->ProgressSetSize(CQuadWord(size, 0),
                                                                InterfaceForArchiver.currentProgress + CQuadWord(size, 0), TRUE);
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

    FILE* f;
    if (!LHAOpenArchive(f, fileName))
    {
        SalamanderGeneral->ShowMessageBox(LangStr(iLHAErrorStrId).c_str(), LangStr(IDS_PLUGINNAME).c_str(), MSGBOX_ERROR);
        return FALSE;
    }

    LHA_HEADER hdr;
    fseek(f, (long)fileData->PluginData, SEEK_SET);
    if (LHAGetHeader(f, &hdr) != GH_SUCCESS)
    {
        SalamanderGeneral->ShowMessageBox(LangStr(IDS_UNPACKERROR).c_str(), LangStr(IDS_PLUGINNAME).c_str(), MSGBOX_ERROR);
        fclose(f);
        return FALSE;
    }

    if (hdr.method == LHA_UNKNOWNMETHOD && hdr.original_size != 0 /* see below : */)
    {
        // I do not know why, but LHA sometimes packs zero-length files with a nonsensical method name...
        // If the file length is 0, I therefore ignore the method type.
        SalamanderGeneral->ShowMessageBox(LangStr(IDS_METHOD).c_str(), LangStr(IDS_PLUGINNAME).c_str(), MSGBOX_ERROR);
        fclose(f);
        return FALSE;
    }

    const wchar_t* justName = SalamanderGeneral->SalPathFindFileName(nameInArchive);
    std::wstring targetName = targetDir != NULL ? targetDir : L"";
    SPLSalPathAppendOwned(targetName, justName);
    BOOL skip;
    DWORD silent = 0;

    const std::wstring info = GetInfo(&hdr.last_modified_filetime, hdr.original_size);

    HANDLE hOutFile = SalamanderSafeFile->SafeFileCreate(targetName.c_str(), GENERIC_WRITE, FILE_SHARE_READ,
                                                         hdr.attribute & ~FILE_ATTRIBUTE_READONLY | FILE_FLAG_SEQUENTIAL_SCAN,
                                                         FALSE, SalamanderGeneral->GetMsgBoxParent(),
                                                         nameInArchive, info.c_str(), &silent, TRUE, &skip, NULL, 0, NULL, NULL);

    if (hOutFile == INVALID_HANDLE_VALUE)
    {
        fclose(f);
        return FALSE;
    }

    int crc = 0, uf = 1;
    if (hdr.original_size)
    {
        uf = LHAUnpackFile(f, hOutFile, &hdr, &crc, targetName.c_str());
    }

    CloseHandle(hOutFile);
    fclose(f);

    if (!uf)
    {
        if (iLHAErrorStrId != IDS_WRITEERROR) // because of SafeWriteFile
            SalamanderGeneral->ShowMessageBox(LangStr(iLHAErrorStrId).c_str(), LangStr(IDS_PLUGINNAME).c_str(), MSGBOX_ERROR);
        return FALSE;
    }
    if (hdr.has_crc && crc != hdr.crc)
    {
        SalamanderGeneral->ClearReadOnlyAttr(targetName.c_str()); // so a read-only file can be deleted
        DeleteFileW(targetName.c_str());
        SalamanderGeneral->ShowMessageBox(LangStr(IDS_CRCERROR).c_str(), LangStr(IDS_PLUGINNAME).c_str(), MSGBOX_ERROR);
        return FALSE;
    }

    return TRUE;
}

BOOL CPluginInterfaceForArchiver::UnpackArchive(CSalamanderForOperationsAbstract* salamander, const wchar_t* fileName,
                                                CPluginDataInterfaceAbstract* pluginData, const wchar_t* targetDir,
                                                const wchar_t* archiveRoot, SalEnumSelection next, void* nextParam)
{
    CALL_STACK_MESSAGE4("CPluginInterfaceForArchiver::UnpackArchive(, %ls, , %ls, %ls, ,)", fileName, targetDir, archiveRoot);

    Salamander = salamander;
    Silent = 0;
    Ret = TRUE;
    Abort = FALSE;
    UnpackWhole = FALSE;
    CRCSkipAll = FALSE;
    const wchar_t* normalizedRoot = (archiveRoot != NULL) ? archiveRoot : L"";
    if (*normalizedRoot == L'\\')
        normalizedRoot++;
    RootLen = wcslen(normalizedRoot);
    TDirectArray<int> offsets(256, 256);

    if (!MakeFilesList(offsets, next, nextParam, targetDir))
        return FALSE;

    const std::wstring title = SPLFormatStringOwned(
        LangStr(IDS_EXTRPROGTITLE).c_str(), SalamanderGeneral->SalPathFindFileName(fileName));
    Salamander->OpenProgressDialog(title.c_str(), TRUE, NULL, FALSE);
    Salamander->ProgressDialogAddText(LangStr(IDS_PREPAREDATA).c_str(), FALSE);

    FILE* f;
    if (!LHAOpenArchive(f, fileName))
    {
        SalamanderGeneral->ShowMessageBox(LangStr(iLHAErrorStrId).c_str(), LangStr(IDS_PLUGINNAME).c_str(), MSGBOX_ERROR);
        Salamander->CloseProgressDialog();
        return FALSE;
    }

    currentProgress = CQuadWord(0, 0);
    Salamander->ProgressDialogAddText(LangStr(IDS_EXTRACTFILES).c_str(), FALSE);
    pfLHAProgress = ProgressCallback;

    LHA_HEADER hdr;
    int i;
    for (i = 0; i < offsets.Count; i++)
    {
        fseek(f, offsets[i], SEEK_SET);
        if (LHAGetHeader(f, &hdr) != GH_SUCCESS)
        {
            Ret = FALSE;
            break;
        }

        Salamander->ProgressSetTotalSize(CQuadWord(hdr.original_size, 0), ProgressTotal);
        if (!Salamander->ProgressSetSize(CQuadWord(0, 0), CQuadWord(-1, -1), TRUE))
        {
            Ret = FALSE;
            break;
        }

        UnpackInnerBody(f, targetDir, fileName, hdr, FALSE);
        if (Abort)
            break;

        currentProgress += CQuadWord(hdr.original_size, 0);
        Salamander->ProgressSetSize(CQuadWord(hdr.original_size, 0), currentProgress, TRUE);
    }

    pfLHAProgress = NULL;
    Salamander->CloseProgressDialog();
    fclose(f);

    return Ret;
}

BOOL CPluginInterfaceForArchiver::UnpackWholeArchive(CSalamanderForOperationsAbstract* salamander, const wchar_t* fileName,
                                                     const wchar_t* mask, const wchar_t* targetDir, BOOL delArchiveWhenDone,
                                                     CDynamicString* archiveVolumes)
{
    CALL_STACK_MESSAGE5("CPluginInterfaceForArchiver::UnpackWholeArchive(, %ls, %ls, %ls, %d,)",
                        fileName, mask, targetDir, delArchiveWhenDone);

    Salamander = salamander;
    Silent = 0;
    Abort = FALSE;
    Ret = TRUE;
    RootLen = 0;
    UnpackWhole = TRUE;
    CRCSkipAll = FALSE;

    TIndirectArray<std::wstring> masks(16, 16, dtDelete);
    if (!ConstructMaskArray(masks, mask) || masks.Count == 0)
        return FALSE;

    if (delArchiveWhenDone)
        archiveVolumes->Add(fileName, -2);

    const std::wstring title = SPLFormatStringOwned(
        LangStr(IDS_EXTRPROGTITLE).c_str(), SalamanderGeneral->SalPathFindFileName(fileName));
    Salamander->OpenProgressDialog(title.c_str(), FALSE, NULL, TRUE);

    FILE* f;
    if (!LHAOpenArchive(f, fileName))
    {
        SalamanderGeneral->ShowMessageBox(LangStr(iLHAErrorStrId).c_str(), LangStr(IDS_PLUGINNAME).c_str(), MSGBOX_ERROR);
        Salamander->CloseProgressDialog();
        return FALSE;
    }

    pfLHAProgress = ProgressCallback;

    LHA_HEADER hdr;
    int gh;
    while (!Abort && ((gh = LHAGetHeader(f, &hdr)) == GH_SUCCESS))
    {
        unpacked = FALSE;

        std::wstring hdrNameW;
        if (!DecodeLhaName(hdr.name, hdrNameW))
        {
            Abort = TRUE;
            Ret = FALSE;
            break;
        }
        const wchar_t* name = SalamanderGeneral->SalPathFindFileName(hdrNameW.c_str());
        std::wstring maskName(name);
        if (!maskName.empty() && maskName.back() == L'\\') // due to SalPathFindFileName the '\\' can only be at the end; remove it before calling AgreeMask
        {
            maskName.pop_back();
            name = maskName.c_str();
        }
        BOOL nameHasExt = wcschr(name, L'.') != NULL; // ".cvspass" is considered an extension on Windows

        int i;
        for (i = 0; i < masks.Count; i++)
        {
            if (SalamanderGeneral->AgreeMask(name, masks[i]->c_str(), nameHasExt))
            {
                BOOL bFile = (hdr.unix_mode & UNIX_FILE_TYPEMASK) == UNIX_FILE_REGULAR &&
                             hdr.method != LZHDIRS_METHOD_NUM;
                BOOL bDir = (hdr.unix_mode & UNIX_FILE_TYPEMASK) == UNIX_FILE_DIRECTORY ||
                            hdr.method == LZHDIRS_METHOD_NUM;
                if (!bFile && !bDir)
                    break; // symlink...?

                Salamander->ProgressSetTotalSize(CQuadWord(hdr.original_size, 0), CQuadWord(-1, -1));
                if (!Salamander->ProgressSetSize(CQuadWord(0, 0), CQuadWord(-1, -1), TRUE))
                {
                    Abort = TRUE;
                    Ret = FALSE;
                    break;
                }

                UnpackInnerBody(f, targetDir, fileName, hdr, bDir);

                if (!bDir)
                    Salamander->ProgressSetSize(CQuadWord(hdr.original_size, 0), CQuadWord(-1, -1), TRUE);
                break;
            }
        }

        if (!unpacked)
            fseek(f, hdr.packed_size, SEEK_CUR);
    }

    Salamander->CloseProgressDialog();
    pfLHAProgress = NULL;
    fclose(f);

    return Ret;
}

// UnpackInnerBody - called from UnpackWholeArchive and UnpackArchive

void CPluginInterfaceForArchiver::UnpackInnerBody(FILE* f, const wchar_t* targetDir, const wchar_t* fileName, LHA_HEADER& hdr, BOOL bDir)
{
    CALL_STACK_MESSAGE4("CPluginInterfaceForArchiver::UnpackInnerBody( , %ls, %ls, , %ld)", targetDir, fileName, bDir);

    std::wstring hdrName;
    if (!DecodeLhaName(hdr.name, hdrName) || RootLen > hdrName.size())
    {
        SalamanderGeneral->ShowMessageBox(LangStr(IDS_FILECORRUPT).c_str(), LangStr(IDS_PLUGINNAME).c_str(), MSGBOX_ERROR);
        Abort = TRUE;
        Ret = FALSE;
        return;
    }

    std::wstring message = LangStr(IDS_UNPACKING) + hdrName;
    Salamander->ProgressDialogAddText(message.c_str(), TRUE);

    if (hdr.method == LHA_UNKNOWNMETHOD && hdr.original_size)
    {
        SalamanderGeneral->ShowMessageBox(LangStr(IDS_METHOD).c_str(), LangStr(IDS_PLUGINNAME).c_str(), MSGBOX_WARNING);
        Salamander->ProgressDialogAddText(LangStr(IDS_METHOD2).c_str(), TRUE);
        Ret = FALSE;
        return;
    }

    std::wstring targetName = targetDir != NULL ? targetDir : L"";
    SPLSalPathAppendOwned(targetName, hdrName.c_str() + RootLen);

    std::wstring nameInArc = fileName != NULL ? fileName : L"";
    SPLSalPathAppendOwned(nameInArc, hdrName.c_str());
    const std::wstring info = GetInfo(&hdr.last_modified_filetime, hdr.original_size);
    BOOL skip;
    HANDLE hOutFile = SalamanderSafeFile->SafeFileCreate(targetName.c_str(), GENERIC_WRITE, FILE_SHARE_READ,
                                                         hdr.attribute & ~FILE_ATTRIBUTE_READONLY | FILE_FLAG_SEQUENTIAL_SCAN,
                                                         bDir, SalamanderGeneral->GetMsgBoxParent(),
                                                         nameInArc.c_str(), info.c_str(), &Silent, TRUE, &skip, NULL, 0, NULL, NULL);

    if (!bDir)
    {
        if (hOutFile == INVALID_HANDLE_VALUE)
        {
            if (!skip)
            {
                Abort = TRUE;
                Ret = FALSE;
            }
            return;
        }

        int crc = 0, uf = 1;
        if (hdr.original_size)
        {
            uf = LHAUnpackFile(f, hOutFile, &hdr, &crc, targetName.c_str());
        }
        SetFileTime(hOutFile, NULL, NULL, &hdr.last_modified_filetime);
        CloseHandle(hOutFile);

        BOOL bCanceled = !uf && iLHAErrorStrId == -1;
        BOOL bCRCError = hdr.has_crc && crc != hdr.crc;

        if (bCanceled || bCRCError)
        {
            DeleteFileW(targetName.c_str());
            if (bCanceled)
                Abort = TRUE;
        }
        else
            SetFileAttributesW(targetName.c_str(), hdr.attribute);

        if (!bCanceled)
        {
            if (!uf)
            {
                if (iLHAErrorStrId != IDS_WRITEERROR) // because of SafeWriteFile
                    SalamanderGeneral->ShowMessageBox(LangStr(iLHAErrorStrId).c_str(), LangStr(IDS_PLUGINNAME).c_str(), MSGBOX_ERROR);
                Abort = TRUE;
                Ret = FALSE;
            }
            else if (bCRCError)
            {
                if (!CRCSkipAll)
                    switch (SalamanderGeneral->DialogError(SalamanderGeneral->GetMsgBoxParent(), BUTTONS_SKIPCANCEL,
                                                           hdrName.c_str() + RootLen, LangStr(IDS_CRCERROR).c_str(), NULL))
                    {
                    case DIALOG_SKIPALL:
                        CRCSkipAll = TRUE;
                    case DIALOG_SKIP:
                        break;
                    case DIALOG_CANCEL:
                        Abort = TRUE;
                    }
                Ret = FALSE;
            }
        }
        unpacked = TRUE;
    }
}

//****************************************************************************
//
//  Helper functions
//

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

static int __cdecl compare_offsets(const void* elem1, const void* elem2)
{
    return *((int*)elem1) - *((int*)elem2);
}

BOOL CPluginInterfaceForArchiver::MakeFilesList(TDirectArray<int>& offsets, SalEnumSelection next, void* nextParam, const wchar_t* targetDir)
{
    CALL_STACK_MESSAGE2("CPluginInterfaceForArchiver::MakeFilesList(, , , %ls)", targetDir);
    const wchar_t* nextName;
    BOOL isDir;
    CQuadWord size;
    std::wstring targetRoot = targetDir != NULL ? targetDir : L"";
    const CFileData* pfd;
    int errorOccured;

    ProgressTotal = CQuadWord(0, 0);
    while ((nextName = next(SalamanderGeneral->GetMsgBoxParent(), 1, &isDir, &size, &pfd, nextParam, &errorOccured)) != NULL)
    {
        if (isDir)
        {
            std::wstring dir = targetRoot;
            SPLSalPathAppendOwned(dir, nextName);
            BOOL skip;
            if (SalamanderSafeFile->SafeFileCreate(dir.c_str(), 0, 0, 0, TRUE, SalamanderGeneral->GetMsgBoxParent(), NULL, NULL,
                                                   &Silent, TRUE, &skip, NULL, 0, NULL, NULL) == INVALID_HANDLE_VALUE &&
                !skip)
                return FALSE;
        }
        else
        {
            offsets.Add((const int)pfd->PluginData);
            ProgressTotal += size;
        }
    }
    // test whether no error occurred and the user did not request to abort the operation (Cancel button)
    if (errorOccured == SALENUM_CANCEL)
        return FALSE;

    qsort(offsets.GetData(), offsets.Count, sizeof(int), compare_offsets);
    return SalamanderGeneral->TestFreeSpace(SalamanderGeneral->GetMsgBoxParent(), targetDir, ProgressTotal, LangStr(IDS_PLUGINNAME).c_str());
}

BOOL CPluginInterfaceForArchiver::ConstructMaskArray(TIndirectArray<std::wstring>& maskArray, const wchar_t* masks)
{
    CALL_STACK_MESSAGE2("CPluginInterfaceForArchiver::ConstructMaskArray(, %ls)", masks);
    for (const std::wstring& mask : SplitLhaMasks(masks))
    {
        std::wstring* newMask = new std::wstring(
            SPLPrepareMaskOwned(SalamanderGeneral, mask.c_str()));
        if (!newMask)
        {
            SalamanderGeneral->ShowMessageBox(LangStr(IDS_LOWMEM).c_str(), LangStr(IDS_PLUGINNAME).c_str(), MSGBOX_ERROR);
            return FALSE;
        }
        maskArray.Add(newMask);
        if (!maskArray.IsGood())
        {
            maskArray.ResetState();
            delete newMask;
            SalamanderGeneral->ShowMessageBox(LangStr(IDS_LOWMEM).c_str(), LangStr(IDS_PLUGINNAME).c_str(), MSGBOX_ERROR);
            return FALSE;
        }
    }
    return TRUE;
}
