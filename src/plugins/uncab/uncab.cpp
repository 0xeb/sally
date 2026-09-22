// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"

#include <new>
#include <vector>

#include "array2.h"

#include "fdi.h"
#include "uncab.h"
#include "dialogs.h"

#include "uncab.rh"
#include "uncab.rh2"
#include "lang\lang.rh"

// The only sizes seen so far are 0x10660 & 0x17c7c
#define SFX_BUFF_SIZE 0x18000

// FDI exposes a byte-only open callback.  The first cabinet is application
// state, not cabinet metadata, so give FDI an ASCII handle and resolve it back
// to the exact Windows path in Open().  Names of subsequent cabinets remain
// the byte fields carried by the CAB format itself.
static const char INITIAL_CABINET_TOKEN[] = ":sally:initial-cabinet:";

// The directory is Sally's own UTF-16 path; only the cabinet name has to cross
// back out of the CAB byte domain, so a volume in a folder the ANSI code page
// cannot spell still composes.
static BOOL BuildCabinetPathWide(const std::wstring& path, const char* name,
                                 std::wstring& widePath)
{
    std::wstring wideName;
    if (name != NULL && *name != '\0' && !ProjectCabBytesToWide(name, wideName))
        return FALSE;
    widePath = path;
    if (!widePath.empty() && widePath.back() != L'\\')
        widePath.push_back(L'\\');
    widePath.append(wideName);
    return TRUE;
}

static std::wstring BuildIoErrorText(int resourceId, DWORD error)
{
    std::wstring text = SPLLoadStrOwned(SalamanderGeneral, HLanguage, resourceId);
    if (error != ERROR_SUCCESS)
        text += SPLGetErrorTextOwned(SalamanderGeneral, error);
    return text;
}

// ****************************************************************************

HINSTANCE DLLInstance = NULL; // handle to the SPL - language-independent resources
HINSTANCE HLanguage = NULL;   // handle to the SLG - language-dependent resources

// plugin interface object; its methods are called from Salamander
CPluginInterface PluginInterface;
// the part of CPluginInterface interface for the archiver
CPluginInterfaceForArchiver InterfaceForArchiver;

// general Salamander interface - valid from startup until the plugin is unloaded
CSalamanderGeneralAbstract* SalamanderGeneral = NULL;

// interface for convenient work with files
CSalamanderSafeFileAbstract* SalamanderSafeFile = NULL;

// variable definition for "dbg.h"
CSalamanderDebugAbstract* SalamanderDebug = NULL;

// for now this is sufficient instead of configuration
DWORD Options;

const SYSTEMTIME MinTime = {1980, 01, 2, 01, 00, 00, 00, 000};

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

    // this plugin is built for the current Salamander version and higher - verify it
    if (salamander->GetVersion() < LAST_VERSION_OF_SALAMANDER)
    { // we reject older versions
        // wide: same call-site-local widen shape used throughout this backlog
        // (205-223).
#define UNCAB_WIDEN2(x) L##x
#define UNCAB_WIDEN(x) UNCAB_WIDEN2(x)
        MessageBoxW(salamander->GetParentWindow(),
                    UNCAB_WIDEN(REQUIRE_LAST_VERSION_OF_SALAMANDER),
                    L"UnCAB" /* neprekladat! */, MB_OK | MB_ICONERROR);
#undef UNCAB_WIDEN
#undef UNCAB_WIDEN2
        return NULL;
    }

    // let it load the language module (.slg)
    HLanguage = salamander->LoadLanguageModule(salamander->GetParentWindow(), L"UnCAB" /* neprekladat! */);
    if (HLanguage == NULL)
        return NULL;

    // obtain the general Salamander interface
    SalamanderGeneral = salamander->GetSalamanderGeneral();
    SalamanderSafeFile = salamander->GetSalamanderSafeFile();

    if (!InterfaceForArchiver.Init())
        return NULL;

    // set the basic plugin information
    salamander->SetBasicPluginData(SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_PLUGINNAME).c_str(),
                                   FUNCTION_PANELARCHIVERVIEW | FUNCTION_CUSTOMARCHIVERUNPACK |
                                       FUNCTION_CONFIGURATION | FUNCTION_LOADSAVECONFIGURATION,
                                   _CRT_WIDE(VERSINFO_VERSION_NO_PLATFORM),
                                   _CRT_WIDE(VERSINFO_COPYRIGHT),
                                   SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_PLUGIN_DESCRIPTION).c_str(),
                                   L"UnCAB" /* neprekladat! */, L"cab");

    salamander->SetPluginHomePageURL(L"https://github.com/0xeb/sally");

    return &PluginInterface;
}

// ****************************************************************************
//
// CCABCacheEntry
//

CCABCacheEntry::CCABCacheEntry(const char* name, const std::wstring& path)
{
    CALL_STACK_MESSAGE_NONE
    CABName = name;
    CABPath = path;
}

// ****************************************************************************
//
// Callback functions
//

void HUGE* FAR DIAMONDAPI Malloc(ULONG cb)
{
    CALL_STACK_MESSAGE_NONE
    return malloc(cb);
}

void FAR DIAMONDAPI Free(void HUGE* pv)
{
    CALL_STACK_MESSAGE_NONE
    free(pv);
}

INT_PTR FAR DIAMONDAPI Open(LPSTR pszFile, int oflag, int pmode)
{
    CALL_STACK_MESSAGE_NONE
    return InterfaceForArchiver.Open(pszFile, oflag, pmode);
}

UINT FAR DIAMONDAPI Read(INT_PTR hf, void FAR* pv, UINT cb)
{
    CALL_STACK_MESSAGE_NONE
    return InterfaceForArchiver.Read(hf, pv, cb);
}

UINT FAR DIAMONDAPI Write(INT_PTR hf, void FAR* pv, UINT cb)
{
    CALL_STACK_MESSAGE_NONE
    return InterfaceForArchiver.Write(hf, pv, cb);
}

int FAR DIAMONDAPI Close(INT_PTR hf)
{
    CALL_STACK_MESSAGE_NONE
    return InterfaceForArchiver.Close(hf);
}

long FAR DIAMONDAPI Seek(INT_PTR hf, long dist, int seektype)
{
    CALL_STACK_MESSAGE_NONE
    return InterfaceForArchiver.Seek(hf, dist, seektype);
}

INT_PTR FAR DIAMONDAPI Notify(FDINOTIFICATIONTYPE fdint, PFDINOTIFICATION pfdin)
{
    CALL_STACK_MESSAGE_NONE
    return InterfaceForArchiver.Notify(fdint, pfdin);
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
    if (regKey != NULL) // load from the registry
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

    salamander->AddCustomUnpacker(L"UnCAB (Plugin)", L"*.cab", FALSE);
    salamander->AddPanelArchiver(L"cab", FALSE, FALSE);
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
    CALL_STACK_MESSAGE2("CPluginInterfaceForArchiver::ListArchive(, %ls, ,)", fileName);
    Salamander = salamander;
    pluginData = NULL;
    Action = CA_LIST;
    Silent = 0;
    BOOL ret = TRUE;
    Abort = FALSE;
    NotWholeArchListed = FALSE;
    IOError = FALSE;
    FirstCAB = TRUE;
    HFDI hfdi;
    ERF err;
    InitialCabinetPath = fileName;
    std::wstring arcPath(fileName);
    std::wstring arcName;
    Dir = dir;
    Count = 0;

    if (!SPLCutDirectoryOwned(SalamanderGeneral, arcPath, &arcName))
        return FALSE;
    strcpy_s(NextCAB, INITIAL_CABINET_TOKEN);
    SPLSalPathAddBackslashOwned(arcPath);
    // FDI only ever hands these names back to our own Open(), so the directory
    // never has to survive a trip through the ANSI cabinet fields - keep it here
    // in UTF-16 and resolve against it.
    CurrentCABPathW = arcPath;

    memset(&err, 0, sizeof(ERF));
    hfdi = FDICreate(Malloc, Free, ::Open, ::Read, ::Write, ::Close, ::Seek, cpuUNKNOWN, &err);
    if (!hfdi)
        return FDIError(err.erfOper);

    while (1)
    {
        FirstCABINET_INFO = TRUE;
        ret = FDICopy(hfdi, NextCAB, (char*)"", 0, ::Notify, NULL, this);
        if (!ret)
        {
            FDIError(err.erfOper);
            break;
        }
        if (!*NextCAB)
            break;
        FirstCAB = FALSE;
        std::wstring nextCabinet;
        if (!ProjectCabBytesToWide(NextCAB, nextCabinet))
        {
            NotWholeArchListed = TRUE;
            break;
        }
        const std::wstring buffer = arcPath + nextCabinet;
        DWORD attr = SalamanderGeneral->SalGetFileAttributes(buffer.c_str());
        if (attr == -1 || attr & FILE_ATTRIBUTE_DIRECTORY)
        {
            NotWholeArchListed = TRUE;
            break;
        }
    }

    FDIDestroy(hfdi);

    if (ret && NotWholeArchListed && !(Options & OP_NO_VOL_ATTENTION))
        AttentionDialog(SalamanderGeneral->GetMainWindowHWND());

    // we have already listed some files, so do not abort and display them
    if (!ret && Count)
        ret = TRUE;

    Salamander = NULL;

    return ret;
}

void FreeString(void* strig)
{
    CALL_STACK_MESSAGE_NONE
    free(strig);
}

BOOL CPluginInterfaceForArchiver::UnpackArchive(CSalamanderForOperationsAbstract* salamander, const wchar_t* fileName,
                                                CPluginDataInterfaceAbstract* pluginData, const wchar_t* targetDir,
                                                const wchar_t* archiveRoot, SalEnumSelection next, void* nextParam)
{
    CALL_STACK_MESSAGE4("CPluginInterfaceForArchiver::UnpackArchive(, %ls, , %ls, %ls, ,)", fileName, targetDir, archiveRoot);

    Salamander = salamander;
    Action = CA_UNPACK;
    Silent = 0;
    BOOL ret = TRUE;
    Abort = FALSE;
    IOError = FALSE;
    FirstCAB = TRUE;
    HFDI hfdi;
    ERF err;
    Count = 0;
    ArcRoot = archiveRoot != NULL ? archiveRoot : L"";
    if (!ArcRoot.empty() && ArcRoot.front() == L'\\')
        ArcRoot.erase(0, 1);
    RootLen = ArcRoot.size();
    TargetDir = targetDir;
    AllocateWholeFile = TRUE;
    TestAllocateWholeFile = TRUE;

    std::wstring currentCABPathW(fileName);
    std::wstring arcName;
    if (!SPLCutDirectoryOwned(SalamanderGeneral, currentCABPathW, &arcName))
        return FALSE;
    SPLSalPathAddBackslashOwned(currentCABPathW);
    InitialCabinetPath = fileName;
    strcpy_s(CurrentCAB, INITIAL_CABINET_TOKEN);
    CurrentCABPathW = currentCABPathW;

    memset(&err, 0, sizeof(ERF));
    hfdi = FDICreate(Malloc, Free, ::Open, ::Read, ::Write, ::Close, ::Seek, cpuUNKNOWN, &err);
    if (!hfdi)
        return FDIError(err.erfOper);

    const std::wstring title = SPLFormatStringOwned(
        SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_EXTRPROGTITLE).c_str(),
        arcName.c_str());
    Salamander->OpenProgressDialog(title.c_str(), TRUE, NULL, FALSE);
    Salamander->ProgressDialogAddText(SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_PREPAREDATA).c_str(), FALSE);

    ret = MakeFilesList(Files, next, nextParam, targetDir);
    if (ret && !Files.empty())
    {
        Salamander->ProgressDialogAddText(SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_EXTRACTFILES).c_str(), FALSE);
        Salamander->ProgressSetTotalSize(CQuadWord(-1, -1), ProgressTotal);
        while (!Abort)
        {
            FirstCABINET_INFO = TRUE;
            ret = FDICopy(hfdi, CurrentCAB, (char*)"", 0, ::Notify, NULL, this);
            if (!ret)
            {
                FDIError(err.erfOper);
                break;
            }
            if (!*NextCAB || Files.empty())
                break;
            FirstCAB = FALSE;
            strcpy_s(CurrentCAB, NextCAB);
            BOOL firstTry;
            firstTry = TRUE;
            while (1)
            {
                GetCachedCABPath(CurrentCAB, CurrentCABPathW);
                std::wstring cabinetPath;
                const BOOL decoded = BuildCabinetPathWide(CurrentCABPathW, CurrentCAB,
                                                          cabinetPath);
                DWORD attr = decoded ? SalamanderGeneral->SalGetFileAttributes(cabinetPath.c_str())
                                     : static_cast<DWORD>(-1);
                if (attr != -1 && !(attr & FILE_ATTRIBUTE_DIRECTORY))
                {
                    INT_PTR f = OpenWide(cabinetPath, _O_RDONLY | _O_EXCL);
                    if (f == -1)
                        break;
                    FDICABINETINFO ci;
                    BOOL r = FDIIsCabinet(hfdi, f, &ci);
                    Close(f);
                    if (r)
                    {
                        if (ci.hasprev && ci.setID == SetID && ci.iCabinet == NextCABIndex)
                            break; //OK
                        err.erfOper = FDIERROR_WRONG_CABINET;
                        err.fError = TRUE;
                    }
                    if (!firstTry)
                        FDIError(err.erfOper);
                }
                if (NextVolumeDialog(SalamanderGeneral->GetMsgBoxParent(), CurrentCAB, CurrentCABPathW,
                                     NextDISK, NextCABIndex + 1) != IDOK)
                {
                    Abort = TRUE;
                    break;
                }
                firstTry = FALSE;
            }
        }
    }

    Files.clear();
    CABCache.Destroy();
    Salamander->CloseProgressDialog();
    FDIDestroy(hfdi);
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

    std::wstring rootDir(nameInArchive);
    const size_t rootEnd = rootDir.rfind(L'\\');
    if (rootEnd == std::wstring::npos)
        rootDir.clear();
    else
        rootDir.resize(rootEnd);
    Salamander = salamander;
    Action = CA_UNPACK_ONE_FILE;
    Silent = 0;
    BOOL ret = TRUE;
    Abort = FALSE;
    IOError = FALSE;
    FirstCAB = TRUE;
    HFDI hfdi;
    ERF err;
    Count = 0;
    ArcRoot = rootDir;
    if (!ArcRoot.empty() && ArcRoot.front() == L'\\')
        ArcRoot.erase(0, 1);
    RootLen = ArcRoot.size();
    TargetDir = targetDir;
    NameInArchive = nameInArchive;
    OneFileSuccess = FALSE;
    AllocateWholeFile = TRUE;
    TestAllocateWholeFile = TRUE;

    std::wstring currentCABPathW(fileName);
    if (!SPLCutDirectoryOwned(SalamanderGeneral, currentCABPathW))
        return FALSE;
    SPLSalPathAddBackslashOwned(currentCABPathW);
    InitialCabinetPath = fileName;
    strcpy_s(CurrentCAB, INITIAL_CABINET_TOKEN);
    CurrentCABPathW = currentCABPathW;

    memset(&err, 0, sizeof(ERF));
    hfdi = FDICreate(Malloc, Free, ::Open, ::Read, ::Write, ::Close, ::Seek, cpuUNKNOWN, &err);
    if (!hfdi)
        return FDIError(err.erfOper);

    while (!Abort)
    {
        FirstCABINET_INFO = TRUE;
        ret = FDICopy(hfdi, CurrentCAB, (char*)"", 0, ::Notify, NULL, this);
        if (!ret)
        {
            if (OneFileSuccess)
                ret = TRUE;
            else
                FDIError(err.erfOper);
            break;
        }
        if (OneFileSuccess)
            break;
        if (!*NextCAB)
        {
            ret = Error(IDS_NOTFOUND);
            break;
        }
        FirstCAB = FALSE;
        strcpy_s(CurrentCAB, NextCAB);
        BOOL firstTry;
        firstTry = TRUE;
        while (1)
        {
            GetCachedCABPath(CurrentCAB, CurrentCABPathW);
            std::wstring cabinetPath;
            const BOOL decoded = BuildCabinetPathWide(CurrentCABPathW, CurrentCAB,
                                                      cabinetPath);
            DWORD attr = decoded ? SalamanderGeneral->SalGetFileAttributes(cabinetPath.c_str())
                                 : static_cast<DWORD>(-1);
            if (attr != -1 && !(attr & FILE_ATTRIBUTE_DIRECTORY))
            {
                INT_PTR f = OpenWide(cabinetPath, _O_RDONLY | _O_EXCL);
                if (f == -1)
                    break;
                FDICABINETINFO ci;
                BOOL r = FDIIsCabinet(hfdi, f, &ci);
                Close(f);
                if (r)
                {
                    if (ci.hasprev && ci.setID == SetID && ci.iCabinet == NextCABIndex)
                        break; //OK
                    err.erfOper = FDIERROR_WRONG_CABINET;
                    err.fError = TRUE;
                }
                if (!firstTry)
                    FDIError(err.erfOper);
            }
            if (NextVolumeDialog(SalamanderGeneral->GetMsgBoxParent(), CurrentCAB, CurrentCABPathW,
                                 NextDISK, NextCABIndex + 1) != IDOK)
            {
                Abort = TRUE;
                break;
            }
            firstTry = FALSE;
        }
    }

    CABCache.Destroy();
    FDIDestroy(hfdi);
    Salamander = NULL;

    return ret;
}

BOOL CPluginInterfaceForArchiver::UnpackWholeArchive(CSalamanderForOperationsAbstract* salamander, const wchar_t* fileName,
                                                     const wchar_t* mask, const wchar_t* targetDir, BOOL delArchiveWhenDone,
                                                     CDynamicString* archiveVolumes)
{
    CALL_STACK_MESSAGE5("CPluginInterfaceForArchiver::UnpackWholeArchive(, %ls, %ls, %ls, %d,)",
                        fileName, mask, targetDir, delArchiveWhenDone);

    Salamander = salamander;
    Action = CA_UNPACK_WHOLE_ARCHIVE;
    Silent = 0;
    BOOL ret = TRUE;
    Abort = FALSE;
    IOError = FALSE;
    FirstCAB = TRUE;
    HFDI hfdi;
    ERF err;
    Count = 0;
    ArcRoot.clear();
    RootLen = 0;
    TargetDir = targetDir;
    AllocateWholeFile = TRUE;
    TestAllocateWholeFile = TRUE;

    std::wstring initialCABPathW(fileName);
    std::wstring arcName;
    if (!SPLCutDirectoryOwned(SalamanderGeneral, initialCABPathW, &arcName))
        return FALSE;
    SPLSalPathAddBackslashOwned(initialCABPathW);
    InitialCabinetPath = fileName;
    strcpy_s(CurrentCAB, INITIAL_CABINET_TOKEN);
    CurrentCABPathW = initialCABPathW;

    memset(&err, 0, sizeof(ERF));
    hfdi = FDICreate(Malloc, Free, ::Open, ::Read, ::Write, ::Close, ::Seek, cpuUNKNOWN, &err);
    if (!hfdi)
        return FDIError(err.erfOper);

    const std::wstring title = SPLFormatStringOwned(
        SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_EXTRPROGTITLE).c_str(),
        arcName.c_str());
    Salamander->OpenProgressDialog(title.c_str(), FALSE, NULL, FALSE);
    Salamander->ProgressDialogAddText(SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_PREPAREDATA).c_str(), FALSE);

    ret = ConstructMaskArray(Masks, mask);
    if (ret && !Masks.empty())
    {
        Salamander->ProgressDialogAddText(SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_EXTRACTFILES).c_str(), FALSE);
        //Salamander->ProgressSetTotalSize(CQuadWord(-1, -1), ProgressTotal); // FIXME: ProgressTotal is not computed; then the second progress can be enabled
        while (!Abort)
        {
            FirstCABINET_INFO = TRUE;
            if (delArchiveWhenDone)
            {
                // Petr: we collect the names of multi-volume archive volumes here; the entire archive is processed volume by volume here;
                // inside FDICopy in Notify (see fdintCABINET_INFO) additional volumes are traversed if a file
                // from the current volume spills into subsequent volumes ... nevertheless those additional ones are processed again here,
                // so collecting them in Notify does not seem particularly fortunate (it would require discarding repeated volume names)
                if (strcmp(CurrentCAB, INITIAL_CABINET_TOKEN) == 0)
                    archiveVolumes->Add(InitialCabinetPath.c_str(), -2);
                else
                {
                    std::wstring currentCABVolumeW;
                    if (!ProjectCabBytesToWide(CurrentCAB, currentCABVolumeW))
                    {
                        ret = FALSE;
                        Abort = TRUE;
                        break;
                    }
                    int len = (int)CurrentCABPathW.length();
                    archiveVolumes->Add(CurrentCABPathW.c_str(), len);
                    if (len > 0 && CurrentCABPathW[len - 1] != L'\\' && CurrentCABPathW[len - 1] != L'/')
                        archiveVolumes->Add(L"\\", 1);
                    archiveVolumes->Add(currentCABVolumeW.c_str(), -2);
                }
            }
            ret = FDICopy(hfdi, CurrentCAB, (char*)"", 0, ::Notify, NULL, this);
            if (!ret)
            {
                FDIError(err.erfOper);
                break;
            }
            if (!*NextCAB)
                break;
            FirstCAB = FALSE;
            strcpy_s(CurrentCAB, NextCAB);
            BOOL firstTry;
            firstTry = TRUE;
            while (1)
            {
                GetCachedCABPath(CurrentCAB, CurrentCABPathW);
                std::wstring cabinetPath;
                const BOOL decoded = BuildCabinetPathWide(CurrentCABPathW, CurrentCAB,
                                                          cabinetPath);
                DWORD attr = decoded ? SalamanderGeneral->SalGetFileAttributes(cabinetPath.c_str())
                                     : static_cast<DWORD>(-1);
                if (attr != -1 && !(attr & FILE_ATTRIBUTE_DIRECTORY))
                {
                    INT_PTR f = OpenWide(cabinetPath, _O_RDONLY | _O_EXCL);
                    if (f == -1)
                        break;
                    FDICABINETINFO ci;
                    BOOL r = FDIIsCabinet(hfdi, f, &ci);
                    Close(f);
                    if (r)
                    {
                        if (ci.hasprev && ci.setID == SetID && ci.iCabinet == NextCABIndex)
                            break; //OK
                        err.erfOper = FDIERROR_WRONG_CABINET;
                        err.fError = TRUE;
                    }
                    if (!firstTry)
                        FDIError(err.erfOper);
                }
                if (NextVolumeDialog(SalamanderGeneral->GetMsgBoxParent(), CurrentCAB, CurrentCABPathW,
                                     NextDISK, NextCABIndex + 1) != IDOK)
                {
                    Abort = TRUE;
                    break;
                }
                firstTry = FALSE;
            }
        }
    }

    Masks.clear();
    CABCache.Destroy();
    Salamander->CloseProgressDialog();
    FDIDestroy(hfdi);
    Salamander = NULL;

    return ret;
}

BOOL CPluginInterfaceForArchiver::Error(int error, ...)
{
    CALL_STACK_MESSAGE_NONE
    const DWORD lastErr = GetLastError();
    CALL_STACK_MESSAGE2("CPluginInterfaceForArchiver::Error(%d, )", error);
    va_list arglist;
    va_start(arglist, error);
    std::wstring message = SPLFormatStringOwnedV(
        SPLLoadStrOwned(SalamanderGeneral, HLanguage, error).c_str(), arglist);
    va_end(arglist);
    if (lastErr != ERROR_SUCCESS)
        message += SPLGetErrorTextOwned(SalamanderGeneral, lastErr);
    SalamanderGeneral->ShowMessageBox(
        message.c_str(),
        SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_PLUGINNAME).c_str(),
        MSGBOX_ERROR);

    return FALSE;
}

BOOL CPluginInterfaceForArchiver::FDIError(int erfOper)
{
    CALL_STACK_MESSAGE2("CPluginInterfaceForArchiver::FDIError(%d)", erfOper);
    if (!erfOper)
        return TRUE;
    int ret;
    switch (erfOper)
    {
    case FDIERROR_NONE:
        ret = 0;
        break;
    case FDIERROR_CABINET_NOT_FOUND:
        ret = IDS_CABINET_NOT_FOUND;
        break;
    case FDIERROR_NOT_A_CABINET:
        ret = IDS_NOT_A_CABINET;
        break;
    case FDIERROR_UNKNOWN_CABINET_VERSION:
        ret = IDS_UNKNOWN_CABINET_VERSION;
        break;
    case FDIERROR_CORRUPT_CABINET:
        if (!IOError)
            ret = IDS_CORRUPT_CABINET;
        else
            ret = 0;
        break;
    case FDIERROR_ALLOC_FAIL:
        ret = IDS_LOWMEM;
        break;
    case FDIERROR_BAD_COMPR_TYPE:
        ret = IDS_BAD_COMPR_TYPE;
        break;
    case FDIERROR_MDI_FAIL:
        ret = IDS_MDI_FAIL;
        break;
    case FDIERROR_TARGET_FILE:
        ret = 0;
        break;
    case FDIERROR_RESERVE_MISMATCH:
        ret = IDS_RESERVE_MISMATCH;
        break;
    case FDIERROR_WRONG_CABINET:
        ret = IDS_WRONG_CABINET;
        break;
    case FDIERROR_USER_ABORT:
        ret = 0;
        break;
    default:
        ret = IDS_UNKNOWN;
    }
    if (ret)
        return Error(ret);
    else
        return TRUE;
}

BOOL CPluginInterfaceForArchiver::Init()
{
    CALL_STACK_MESSAGE1("CPluginInterfaceForArchiver::Init()");

    return TRUE;
}

BOOL CPluginInterfaceForArchiver::MakeFilesList(std::vector<std::wstring>& files, SalEnumSelection next, void* nextParam, const wchar_t* targetDir)
{
    CALL_STACK_MESSAGE2("CPluginInterfaceForArchiver::MakeFilesList(, , , %ls)", targetDir);
    files.clear();
    const wchar_t* nextName;
    BOOL isDir;
    CQuadWord size;
    int errorOccured;

    ProgressTotal = CQuadWord(0, 0);
    while ((nextName = next(SalamanderGeneral->GetMsgBoxParent(), 1, &isDir, &size, NULL, nextParam, &errorOccured)) != NULL)
    {
        if (!isDir)
        {
            try
            {
                std::wstring selected = ArcRoot;
                if (!selected.empty() && selected.back() != L'\\')
                    selected.push_back(L'\\');
                selected.append(nextName);
                files.emplace_back(std::move(selected));
            }
            catch (const std::bad_alloc&)
            {
                return Error(IDS_LOWMEM);
            }
            ProgressTotal += size;
        }
    }
    return errorOccured != SALENUM_CANCEL && // test whether no error occurred and the user did not wish to cancel the operation (Cancel button)
           SalamanderGeneral->TestFreeSpace(SalamanderGeneral->GetMsgBoxParent(), targetDir, ProgressTotal,
                                            SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_PLUGINNAME).c_str());
}

BOOL CPluginInterfaceForArchiver::ConstructMaskArray(std::vector<std::wstring>& maskArray,
                                                     const wchar_t* masks)
{
    CALL_STACK_MESSAGE1("CPluginInterfaceForArchiver::ConstructMaskArray() ");
    maskArray.clear();
    for (const std::wstring& source : SplitUnCabMasks(masks))
    {
        std::wstring normalized =
            SPLPrepareMaskOwned(SalamanderGeneral, source.c_str());
        if (!normalized.empty())
            maskArray.emplace_back(std::move(normalized));
    }
    return TRUE;
}

BOOL CPluginInterfaceForArchiver::UpdateCABCache(const char* name, const std::wstring& path)
{
    CALL_STACK_MESSAGE1("CPluginInterfaceForArchiver::UpdateCABCache(, )");
    int i;
    for (i = 0; i < CABCache.Count; i++)
    {
        if (EqualCabBytesIgnoringCase(name, CABCache[i]->CABName.c_str()))
        {
            CABCache[i]->CABPath = path;
            return TRUE;
        }
    }
    CCABCacheEntry* entry = new CCABCacheEntry(name, path);
    if (!entry)
        return Error(IDS_LOWMEM);
    if (!CABCache.Add(entry))
    {
        delete entry;
        return Error(IDS_LOWMEM);
    }
    return TRUE;
}

BOOL CPluginInterfaceForArchiver::GetCachedCABPath(const char* name, std::wstring& path)
{
    CALL_STACK_MESSAGE1("CPluginInterfaceForArchiver::GetCachedCABPath(, )");
    int i;
    for (i = 0; i < CABCache.Count; i++)
    {
        if (EqualCabBytesIgnoringCase(name, CABCache[i]->CABName.c_str()))
        {
            path = CABCache[i]->CABPath;
            return TRUE;
        }
    }
    return FALSE;
}

BOOL CPluginInterfaceForArchiver::CurrentCabinetFullPathW(std::wstring& fullPath) const
{
    CALL_STACK_MESSAGE_NONE
    // While the initial-cabinet token is current, the volume being read is the
    // archive Salamander handed us - report that exact path rather than trying
    // to spell the token.
    if (strcmp(CurrentCAB, INITIAL_CABINET_TOKEN) == 0)
    {
        fullPath = InitialCabinetPath;
        return TRUE;
    }
    return BuildCabinetPathWide(CurrentCABPathW, CurrentCAB, fullPath);
}

BOOL CPluginInterfaceForArchiver::ListFile(char* fileName, DWORD size, WORD date, WORD time, DWORD attributes)
{
    CALL_STACK_MESSAGE6("CPluginInterfaceForArchiver::ListFile( %s, 0x%X, 0x%X, "
                        "0x%X, 0x%X)",
                        fileName, size, date, time, attributes);
    std::wstring fileNameW;
    if (!DecodeCabMemberName(fileName, (attributes & _A_NAME_IS_UTF) != 0, fileNameW))
    {
        SalamanderGeneral->ShowMessageBox(
            SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_LIST).c_str(),
            SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_PLUGINNAME).c_str(),
            MSGBOX_ERROR);
        return FALSE;
    }
    wchar_t* fileNameBuffer = fileNameW.data();
    CFileData fileData;
    wchar_t* slash;
    const wchar_t* path;
    wchar_t* name;
    BOOL ret = TRUE;

    path = fileNameBuffer;
    name = fileNameBuffer;
    slash = wcsrchr(fileNameBuffer, L'\\');
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
        if (slash)
            *slash = L'\\';
        return FALSE;
    }
    fileData.Ext = wcsrchr(fileData.Name, L'.');
    if (fileData.Ext != NULL)
        fileData.Ext++; // ".cvspass" is an extension in Windows
    else
        fileData.Ext = fileData.Name + wcslen(fileData.Name);
    fileData.Size = CQuadWord(size, 0);
    fileData.Attr = attributes & FILE_ATTRIBUTE_MASK;
    fileData.Hidden = fileData.Attr & FILE_ATTRIBUTE_HIDDEN ? 1 : 0;
    fileData.PluginData = -1; // unnecessary, just for form's sake
    FILETIME ft;
    if (!DosDateTimeToFileTime(date, time, &ft))
    {
        SystemTimeToFileTime(&MinTime, &ft);
    }
    LocalFileTimeToFileTime(&ft, &fileData.LastWrite);
    fileData.DosName = NULL;
    fileData.NameLen = static_cast<int>(wcslen(fileData.Name));
    fileData.IsLink = SalamanderGeneral->IsFileLink(fileData.Ext);
    fileData.IsOffline = 0;
    if (!Dir->AddFile(path, fileData, NULL))
    {
        free(fileData.Name);
        SalamanderGeneral->ShowMessageBox(SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_LIST).c_str(),
                                          SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_PLUGINNAME).c_str(), MSGBOX_ERROR);
        ret = FALSE;
    }
    else
        Count++;
    if (slash)
        *slash = L'\\';
    return ret;
}

BOOL CPluginInterfaceForArchiver::DoThisFile(const std::wstring& fileName)
{
    CALL_STACK_MESSAGE2("CPluginInterfaceForArchiver::DoThisFile(%ls)", fileName.c_str());
    BOOL ret = FALSE;
    switch (Action)
    {
    case CA_UNPACK:
    {
        for (auto item = Files.begin(); item != Files.end(); ++item)
        {
            if (EqualCabMemberNames(fileName, *item))
            {
                ret = TRUE;
                Files.erase(item);
                break;
            }
        }
        break;
    }
    case CA_UNPACK_ONE_FILE:
        return EqualCabMemberNames(fileName, NameInArchive);
    case CA_UNPACK_WHOLE_ARCHIVE:
    {
        const wchar_t* name = SalamanderGeneral->SalPathFindFileName(fileName.c_str());
        BOOL nameHasExt = wcschr(name, L'.') != NULL; // ".cvspass" is an extension in Windows
        for (const std::wstring& mask : Masks)
        {
            if (SalamanderGeneral->AgreeMask(name, mask.c_str(), nameHasExt))
            {
                ret = TRUE;
                break;
            }
        }
        break;
    }
    }
    return ret;
}

INT_PTR
CPluginInterfaceForArchiver::UnpackFile(char* fileName, DWORD size, WORD date, WORD time, DWORD attributes)
{
    CALL_STACK_MESSAGE6("CPluginInterfaceForArchiver::UnpackFile( %s, 0x%X, 0x%X, "
                        "0x%X, 0x%X)",
                        fileName, size, date, time, attributes);
    std::wstring fileNameW;
    if (!DecodeCabMemberName(fileName, (attributes & _A_NAME_IS_UTF) != 0, fileNameW))
    {
        Abort = TRUE;
        return -1;
    }
    if (!DoThisFile(fileNameW))
        return Abort ? -1 : 0;
    std::wstring message = SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_EXTRACTING).c_str();
    message.append(fileNameW);
    if (Action != CA_UNPACK_ONE_FILE)
        Salamander->ProgressDialogAddText(message.c_str(), TRUE);

    if (Action != CA_UNPACK_ONE_FILE)
        Salamander->ProgressSetTotalSize(CQuadWord(size, 0), CQuadWord(-1, -1));
    if (Action != CA_UNPACK_ONE_FILE && !Salamander->ProgressSetSize(CQuadWord(0, 0), CQuadWord(-1, -1), TRUE))
    {
        Abort = TRUE;
        return -1;
    }

    CFile* ret = new CFile;
    if (!ret)
    {
        Error(IDS_LOWMEM);
        Abort = TRUE;
        return -1;
    }
    if (RootLen > fileNameW.size())
    {
        delete ret;
        Abort = TRUE;
        return -1;
    }
    const std::wstring fileNameSuffixW = fileNameW.substr(RootLen);
    ret->FileName = TargetDir;
    SPLSalPathAppendOwned(ret->FileName, fileNameSuffixW.c_str());
    std::wstring nameInArc;
    if (!CurrentCabinetFullPathW(nameInArc))
    {
        delete ret;
        Abort = TRUE;
        return -1;
    }
    SPLSalPathAppendOwned(nameInArc, fileNameW.c_str());
    FILETIME ft, lft;
    if (!DosDateTimeToFileTime(date, time, &lft))
    {
        SystemTimeToFileTime(&MinTime, &lft);
    }
    LocalFileTimeToFileTime(&lft, &ft);
    const std::wstring fileInfo = GetInfo(&ft, size);
    BOOL skip;
    CQuadWord q = CQuadWord(size, 0);
    BOOL allocate = AllocateWholeFile &&
                    CQuadWord(2, 0) < q && q < CQuadWord(0, 0x80000000);
    if (TestAllocateWholeFile)
        q += CQuadWord(0, 0x80000000);
    ret->Handle = SalamanderSafeFile->SafeFileCreate(ret->FileName.c_str(), GENERIC_WRITE, FILE_SHARE_READ,
                                                     attributes & FILE_ATTRIBUTE_MASK & ~FILE_ATTRIBUTE_READONLY | FILE_FLAG_SEQUENTIAL_SCAN,
                                                     FALSE, SalamanderGeneral->GetMsgBoxParent(), nameInArc.c_str(), fileInfo.c_str(), &Silent, TRUE, &skip, NULL, 0,
                                                     allocate ? &q : NULL, NULL);
    if (skip)
    {
        delete ret;
        if (Action != CA_UNPACK_ONE_FILE && !Salamander->ProgressAddSize(size, TRUE))
        {
            Abort = TRUE;
            return -1;
        }
        return 0;
    }
    if (ret->Handle == INVALID_HANDLE_VALUE)
    {
        delete ret;
        Abort = TRUE;
        return -1;
    }
    if (q == CQuadWord(0, 0x80000000))
    {
        // allocation failed and we will not try it anymore
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
    ret->Flags = FF_EXTRFILE;

    return (INT_PTR)ret;
}

INT_PTR
CPluginInterfaceForArchiver::Open(char* pszFile, int oflag, int pmode)
{
    CALL_STACK_MESSAGE3("CPluginInterfaceForArchiver::Open(%s, 0x%X)", pszFile, oflag);
    if (strcmp(pszFile, INITIAL_CABINET_TOKEN) == 0)
        return OpenWide(InitialCabinetPath, oflag);

    // What FDI hands back here is the bare cabinet name out of the CAB byte
    // stream - the directory it lives in never entered that byte domain, so
    // rejoin the two on this side rather than asking FDI to carry a path it
    // could only spell in the ANSI code page.
    std::wstring cabinetName;
    if (!ProjectCabBytesToWide(pszFile, cabinetName))
    {
        SetLastError(ERROR_NO_UNICODE_TRANSLATION);
        Error(IDS_UNABLECREATE);
        return -1;
    }
    std::wstring openPath;
    if (cabinetName.find_first_of(L"\\/:") != std::wstring::npos)
        openPath = cabinetName; // already qualified - use it as given
    else
    {
        std::wstring directory = CurrentCABPathW;
        GetCachedCABPath(pszFile, directory);
        openPath = directory;
        if (!openPath.empty() && openPath.back() != L'\\')
            openPath.push_back(L'\\');
        openPath.append(cabinetName);
    }
    return OpenWide(openPath, oflag);
}

INT_PTR
CPluginInterfaceForArchiver::OpenWide(const std::wstring& openPathOwner, int oflag)
{
    CALL_STACK_MESSAGE2("CPluginInterfaceForArchiver::OpenWide(, 0x%X)", oflag);
    const wchar_t* openPath = openPathOwner.c_str();
    IOError = TRUE;

    DWORD fileaccess;
    switch (oflag & (_O_RDONLY | _O_WRONLY | _O_RDWR))
    {
    case _O_RDONLY: /* read access */
        fileaccess = GENERIC_READ;
        break;
    case _O_WRONLY: /* write access */
        fileaccess = GENERIC_WRITE;
        break;
    case _O_RDWR: /* read and write access */
        fileaccess = GENERIC_READ | GENERIC_WRITE;
        break;
    default: /* error, bad oflag */
        return -1;
    }

    DWORD filecreate;
    switch (oflag & (_O_CREAT | _O_EXCL | _O_TRUNC))
    {
    case 0:
    case _O_EXCL: // ignore EXCL w/o CREAT
        filecreate = OPEN_EXISTING;
        break;
    case _O_CREAT:
        filecreate = OPEN_ALWAYS;
        break;
    case _O_CREAT | _O_EXCL:
    case _O_CREAT | _O_TRUNC | _O_EXCL:
        filecreate = CREATE_NEW;
        break;
    case _O_TRUNC:
    case _O_TRUNC | _O_EXCL: // ignore EXCL w/o CREAT
        filecreate = TRUNCATE_EXISTING;
        break;
    case _O_CREAT | _O_TRUNC:
        filecreate = CREATE_ALWAYS;
        break;
    default:
        // this can't happen ... all cases are covered
        return -1;
    }

    DWORD fileattrib = FILE_ATTRIBUTE_NORMAL; /* default */
    if (oflag & _O_TEMPORARY)
    {
        fileattrib |= FILE_FLAG_DELETE_ON_CLOSE;
        fileaccess |= DELETE;
    }
    if (oflag & _O_SHORT_LIVED)
        fileattrib |= FILE_ATTRIBUTE_TEMPORARY;
    if (oflag & _O_SEQUENTIAL)
        fileattrib |= FILE_FLAG_SEQUENTIAL_SCAN;
    else if (oflag & _O_RANDOM)
        fileattrib |= FILE_FLAG_RANDOM_ACCESS;

    CFile* ret = new CFile;
    if (!ret)
    {
        Error(IDS_LOWMEM);
        return -1;
    }

    while (1)
    {
        ret->Handle = CreateFileW(openPath, fileaccess, FILE_SHARE_READ, NULL, filecreate, fileattrib, NULL);
        if (ret->Handle != INVALID_HANDLE_VALUE)
        {
            ret->FileName = openPath;
            ret->Flags = 0;
            IOError = FALSE;
            ret->cabOffset = 0;
            if (oflag == _O_BINARY)
            {
                // opening in binary mode for reading only
                DWORD magic;

                Read((INT_PTR)ret, &magic, sizeof(magic));
                if (magic != 'FCSM')
                {
                    // does not start with CAB magic -> check for SFX EXE part
                    char* ptr = (char*)malloc(SFX_BUFF_SIZE);
                    int size;
                    if (ptr)
                    {
                        char* ptr2 = ptr;

                        size = Read((INT_PTR)ret, ptr, SFX_BUFF_SIZE) - (2 * sizeof(magic) - 1);
                        while (size > 0)
                        {
                            if ((*(DWORD*)ptr2 == 'FCSM') && (((DWORD*)ptr2)[1] == 0 /*reserved*/))
                            {
                                // there must be more than just testing FCSM to avoid wrong match with progra, code
                                ret->cabOffset = (DWORD)(ptr2 - ptr + sizeof(magic));
                                break;
                            }
                            ptr2++;
                            size--;
                        }
                        free(ptr);
                    }
                }
                SafeSeek(ret, 0, FILE_BEGIN);
            }

            return (INT_PTR)ret; //sucess
        }
        const std::wstring errorText = BuildIoErrorText(IDS_UNABLECREATE, GetLastError());
        if (SalamanderGeneral->DialogError(SalamanderGeneral->GetMsgBoxParent(),
                                           BUTTONS_RETRYCANCEL, openPath,
                                           errorText.c_str(), NULL) != DIALOG_RETRY)
        {
            Abort = TRUE;
            delete ret;
            return -1;
        }
    }
}

UINT CPluginInterfaceForArchiver::Read(INT_PTR hf, void* pv, UINT cb)
{
    CALL_STACK_MESSAGE2("CPluginInterfaceForArchiver::Read(, 0x%X)", cb);
    CFile* file = (CFile*)hf;
    if (cb == 0)
        return 0;
    if (file->Flags & FF_SKIPFILE)
        return -1;
    IOError = TRUE;
    DWORD pos;
    while (1)
    {
        pos = SetFilePointer(file->Handle, 0, NULL, FILE_CURRENT);
        if (pos != 0xFFFFFFFF)
            break;
        const std::wstring errorText = BuildIoErrorText(IDS_UNABLEGETFIELPOS,
                                                        GetLastError());

        if (Silent & SF_IOERRORS && file->Flags & FF_EXTRFILE)
            return -1;

        int ret;
        if (file->Flags & FF_EXTRFILE)
            ret = SalamanderGeneral->DialogError(SalamanderGeneral->GetMsgBoxParent(), BUTTONS_RETRYSKIPCANCEL, file->FileName.c_str(), errorText.c_str(), NULL);
        else
            ret = SalamanderGeneral->DialogError(SalamanderGeneral->GetMsgBoxParent(), BUTTONS_RETRYCANCEL, file->FileName.c_str(), errorText.c_str(), NULL);
        switch (ret)
        {
        case DIALOG_SKIPALL:
            Silent |= SF_IOERRORS;
        case DIALOG_SKIP:
            return -1;
        case DIALOG_CANCEL:
        case DIALOG_FAIL:
            Abort = TRUE;
            return -1;
        }
    }
    while (1)
    {
        DWORD read;
        if (ReadFile(file->Handle, pv, cb, &read, NULL))
        {
            IOError = FALSE;
            return read; //success
        }
        const std::wstring errorText = BuildIoErrorText(IDS_UNABLEREAD,
                                                        GetLastError());

        if (Silent & SF_IOERRORS && file->Flags & FF_EXTRFILE)
            return -1;

        int ret;
        if (file->Flags & FF_EXTRFILE)
            ret = SalamanderGeneral->DialogError(SalamanderGeneral->GetMsgBoxParent(), BUTTONS_RETRYSKIPCANCEL, file->FileName.c_str(), errorText.c_str(), NULL);
        else
            ret = SalamanderGeneral->DialogError(SalamanderGeneral->GetMsgBoxParent(), BUTTONS_RETRYCANCEL, file->FileName.c_str(), errorText.c_str(), NULL);
        switch (ret)
        {
        case DIALOG_SKIPALL:
            Silent |= SF_IOERRORS;
        case DIALOG_SKIP:
            return -1;
        case DIALOG_CANCEL:
        case DIALOG_FAIL:
            Abort = TRUE;
            return -1;
        }
        if (SafeSeek(file, pos - file->cabOffset, FILE_BEGIN) == -1)
            return -1;
    }
    return -1;
} /* CPluginInterfaceForArchiver::Read */

UINT CPluginInterfaceForArchiver::Write(INT_PTR hf, void* pv, UINT cb)
{
    CALL_STACK_MESSAGE2("CPluginInterfaceForArchiver::Write(, 0x%X)", cb);
    CFile* file = (CFile*)hf;
    if (cb == 0)
        return 0;
    if (file->Flags & FF_SKIPFILE)
    {
        if (Action != CA_UNPACK_ONE_FILE && !Salamander->ProgressAddSize(cb, TRUE))
        {
            Abort = TRUE;
            return -1;
        }
        return cb;
    }
    DWORD pos;
    while (1)
    {
        pos = SetFilePointer(file->Handle, 0, NULL, FILE_CURRENT);
        if (pos != 0xFFFFFFFF)
            break;
        const std::wstring errorText = BuildIoErrorText(IDS_UNABLEGETFIELPOS,
                                                        GetLastError());

        if (Silent & SF_IOERRORS && file->Flags & FF_EXTRFILE)
        {
            if (Action != CA_UNPACK_ONE_FILE)
                Salamander->ProgressDialogAddText(SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_SKIPPING).c_str(), TRUE);
            file->Flags |= FF_SKIPFILE;
            if (Action != CA_UNPACK_ONE_FILE && !Salamander->ProgressAddSize(cb, TRUE))
            {
                Abort = TRUE;
                return -1;
            }
            return cb;
        }

        int ret;
        if (file->Flags & FF_EXTRFILE)
            ret = SalamanderGeneral->DialogError(SalamanderGeneral->GetMsgBoxParent(), BUTTONS_RETRYSKIPCANCEL, file->FileName.c_str(), errorText.c_str(), NULL);
        else
            ret = SalamanderGeneral->DialogError(SalamanderGeneral->GetMsgBoxParent(), BUTTONS_RETRYCANCEL, file->FileName.c_str(), errorText.c_str(), NULL);
        switch (ret)
        {
        case DIALOG_SKIPALL:
            Silent |= SF_IOERRORS;
        case DIALOG_SKIP:
        {
            if (file->Flags & FF_EXTRFILE)
            {
                if (Action != CA_UNPACK_ONE_FILE)
                    Salamander->ProgressDialogAddText(SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_SKIPPING).c_str(), TRUE);
                file->Flags |= FF_SKIPFILE;
                if (Action != CA_UNPACK_ONE_FILE && !Salamander->ProgressAddSize(cb, TRUE))
                {
                    Abort = TRUE;
                    return -1;
                }
                return cb;
            }
            else
                return -1;
        }
        case DIALOG_CANCEL:
        case DIALOG_FAIL:
            Abort = TRUE;
            return -1;
        }
    }
    while (1)
    {
        DWORD written;
        if (WriteFile(file->Handle, pv, cb, &written, NULL))
        {
            if (Action != CA_UNPACK_ONE_FILE && !Salamander->ProgressAddSize(cb, TRUE))
            {
                Abort = TRUE;
                return -1;
            }
            return written; // sucess
        }

        const std::wstring errorText = BuildIoErrorText(IDS_UNABLEWRITE,
                                                        GetLastError());

        if (Silent & SF_IOERRORS && file->Flags & FF_EXTRFILE)
        {
            if (Action != CA_UNPACK_ONE_FILE)
                Salamander->ProgressDialogAddText(SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_SKIPPING).c_str(), TRUE);
            file->Flags |= FF_SKIPFILE;
            if (Action != CA_UNPACK_ONE_FILE && !Salamander->ProgressAddSize(cb, TRUE))
            {
                Abort = TRUE;
                return -1;
            }
            return cb;
        }

        int ret;
        if (file->Flags & FF_EXTRFILE)
            ret = SalamanderGeneral->DialogError(SalamanderGeneral->GetMsgBoxParent(), BUTTONS_RETRYSKIPCANCEL, file->FileName.c_str(), errorText.c_str(), NULL);
        else
            ret = SalamanderGeneral->DialogError(SalamanderGeneral->GetMsgBoxParent(), BUTTONS_RETRYCANCEL, file->FileName.c_str(), errorText.c_str(), NULL);
        switch (ret)
        {
        case DIALOG_SKIPALL:
            Silent |= SF_IOERRORS;
        case DIALOG_SKIP:
        {
            if (file->Flags & FF_EXTRFILE)
            {
                if (Action != CA_UNPACK_ONE_FILE)
                    Salamander->ProgressDialogAddText(SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_SKIPPING).c_str(), TRUE);
                file->Flags |= FF_SKIPFILE;
                if (Action != CA_UNPACK_ONE_FILE && !Salamander->ProgressAddSize(cb, TRUE))
                {
                    Abort = TRUE;
                    return -1;
                }
                return cb;
            }
            else
                return -1;
        }
        case DIALOG_CANCEL:
        case DIALOG_FAIL:
            Abort = TRUE;
            return -1;
        }
        if (SafeSeek(file, pos - file->cabOffset, FILE_BEGIN) == -1)
            return -1;
    }
    return -1;
} /* CPluginInterfaceForArchiver::Write */

int CPluginInterfaceForArchiver::Close(INT_PTR hf)
{
    CALL_STACK_MESSAGE1("CPluginInterfaceForArchiver::Close( )");
    CFile* file = (CFile*)hf;
    CloseHandle(file->Handle);
    // it was not successfully unpacked, so delete it
    if (file->Flags & FF_EXTRFILE)
        DeleteFileW(file->FileName.c_str());
    delete file;
    return 0;
}

long CPluginInterfaceForArchiver::SafeSeek(CFile* file, DWORD distance, DWORD method)
{
    CALL_STACK_MESSAGE3("CPluginInterfaceForArchiver::SafeSeek(, %u, 0x%X)", distance, method);
    if (file->Flags & FF_SKIPFILE)
        return distance;
    while (1)
    {
        DWORD pos;
        if (method == FILE_BEGIN)
            distance += file->cabOffset;
        pos = SetFilePointer(file->Handle, distance, NULL, method);
        if (pos != 0xFFFFFFFF)
            return pos - file->cabOffset; //success
        const std::wstring errorText = BuildIoErrorText(IDS_UNABLESEEK,
                                                        GetLastError());

        if (Silent & SF_IOERRORS && file->Flags & FF_EXTRFILE)
        {
            file->Flags |= FF_SKIPFILE;
            return distance;
        }

        int ret;
        if (file->Flags & FF_EXTRFILE)
            ret = SalamanderGeneral->DialogError(SalamanderGeneral->GetMsgBoxParent(), BUTTONS_RETRYSKIPCANCEL, file->FileName.c_str(), errorText.c_str(), NULL);
        else
            ret = SalamanderGeneral->DialogError(SalamanderGeneral->GetMsgBoxParent(), BUTTONS_RETRYCANCEL, file->FileName.c_str(), errorText.c_str(), NULL);
        switch (ret)
        {
        case DIALOG_SKIPALL:
            Silent |= SF_IOERRORS;
        case DIALOG_SKIP:
        {
            if (file->Flags & FF_EXTRFILE)
            {
                file->Flags |= FF_SKIPFILE;
                return distance;
            }
            else
                return -1;
        }
        case DIALOG_CANCEL:
        case DIALOG_FAIL:
            Abort = TRUE;
            return -1;
        }
    }
}

long CPluginInterfaceForArchiver::Seek(INT_PTR hf, long dist, int seektype)
{
    CALL_STACK_MESSAGE4("CPluginInterfaceForArchiver::Seek(0x%IX, %d, %d)", hf,
                        dist, seektype);
    CFile* file = (CFile*)hf;
    DWORD method;
    switch (seektype)
    {
    case SEEK_SET:
        method = FILE_BEGIN;
        break;
    case SEEK_CUR:
        method = FILE_CURRENT;
        break;
    case SEEK_END:
        method = FILE_END;
        break;
    default:
        return -1;
    }
    long ret = SafeSeek(file, dist, method);
    if (ret == -1)
        IOError = TRUE;
    return ret;
} /* CPluginInterfaceForArchiver::Seek */

INT_PTR
CPluginInterfaceForArchiver::Notify(FDINOTIFICATIONTYPE fdint, PFDINOTIFICATION pfdin)
{
    CALL_STACK_MESSAGE1("CPluginInterfaceForArchiver::Notify(, )");
    switch (fdint)
    {
    case fdintCABINET_INFO:
    {
        if (Action == CA_UNPACK || Action == CA_UNPACK_WHOLE_ARCHIVE)
        {
            // psz3 is only the byte prefix we handed FDICopy; CurrentCABPathW is
            // the directory we actually opened this volume from, so cache that.
            if (!UpdateCABCache(CurrentCAB, CurrentCABPathW))
                return -1;
        }
        if (FirstCABINET_INFO)
        {
            strcpy_s(NextCAB, pfdin->psz1);
            strcpy_s(NextDISK, pfdin->psz2);
            NextCABIndex = pfdin->iCabinet + 1;
            SetID = pfdin->setID;
        }
        CurrentCABIndex = pfdin->iCabinet;
        if (FirstCAB && pfdin->iCabinet != 0)
            NotWholeArchListed = TRUE;
        FirstCABINET_INFO = FALSE;
        break;
    }

    case fdintPARTIAL_FILE:
    {
        if (Action == CA_LIST)
        {
            if (FirstCAB && !ListFile(pfdin->psz1, 0, 0, 0, pfdin->attribs))
                return -1;
        }
        else
        {
            std::wstring partialName;
            if (!DecodeCabMemberName(pfdin->psz1,
                                     (pfdin->attribs & _A_NAME_IS_UTF) != 0,
                                     partialName))
                return -1;
            if (DoThisFile(partialName) && (Action != CA_UNPACK_WHOLE_ARCHIVE || FirstCAB))
            {
                INT_PTR ret;
                if (Silent & SF_CONTINUED)
                    ret = IDSKIP;
                else
                {
                    ret = ContinuedFileDialog(SalamanderGeneral->GetMsgBoxParent(), partialName);
                    if (ret == IDALL)
                    {
                        Silent |= SF_CONTINUED;
                        ret = IDSKIP;
                    }
                }
                if (ret != IDSKIP)
                {
                    Abort = TRUE;
                    return -1;
                }
            }
        }
        break;
    }

    case fdintCOPY_FILE:
    {
        if (Action == CA_LIST)
        {
            if (!ListFile(pfdin->psz1, pfdin->cb, pfdin->date, pfdin->time, pfdin->attribs))
                return -1;
        }
        else
            return UnpackFile(pfdin->psz1, pfdin->cb, pfdin->date, pfdin->time, pfdin->attribs);
        break;
    }

    case fdintCLOSE_FILE_INFO:
    {
        CFile* file = (CFile*)pfdin->hf;
        FILETIME ft, lft;
        if (!DosDateTimeToFileTime(pfdin->date, pfdin->time, &lft))
        {
            SystemTimeToFileTime(&MinTime, &lft);
        }
        LocalFileTimeToFileTime(&lft, &ft);
        SetFileTime(file->Handle, NULL, NULL, &ft);
        CloseHandle(file->Handle);
        if (file->Flags & FF_SKIPFILE)
            DeleteFileW(file->FileName.c_str());
        else
            SetFileAttributesW(file->FileName.c_str(), pfdin->attribs & FILE_ATTRIBUTE_MASK);
        delete file;
        if (Action == CA_UNPACK_ONE_FILE)
        {
            OneFileSuccess = TRUE;
            return FALSE;
        }
        return TRUE;
    }

    case fdintNEXT_CABINET:
    {
        static BOOL firstTry;
        if (pfdin->fdie == FDIERROR_NONE)
        {
            firstTry = TRUE;
            strcpy_s(CurrentCAB, pfdin->psz1);
            GetCachedCABPath(CurrentCAB, CurrentCABPathW);
            std::wstring cabinetPath;
            const BOOL decoded = BuildCabinetPathWide(CurrentCABPathW, CurrentCAB,
                                                      cabinetPath);
            DWORD attr = decoded ? SalamanderGeneral->SalGetFileAttributes(cabinetPath.c_str())
                                 : static_cast<DWORD>(-1);
            if (attr != -1 && !(attr & FILE_ATTRIBUTE_DIRECTORY))
                break;
        }
        else if (!firstTry)
            FDIError(pfdin->fdie);
        // Leave psz3 empty: FDI only feeds it back to our own Open(), which
        // resolves the bare name against CurrentCABPathW.
        if (NextVolumeDialog(SalamanderGeneral->GetMsgBoxParent(), CurrentCAB, CurrentCABPathW,
                             pfdin->psz2, CurrentCABIndex + 2) != IDOK)
        {
            Abort = TRUE;
            return -1;
        }
        firstTry = FALSE;
        break;
    }

    case fdintENUMERATE:
        return 0;
    default:
        return 0;
    }
    return 0;
}

// ****************************************************************************

static std::wstring FormatLocaleTime(const SYSTEMTIME& time)
{
    const int required = GetTimeFormatW(LOCALE_USER_DEFAULT, 0, &time, NULL, NULL, 0);
    if (required > 0)
    {
        std::wstring value(static_cast<size_t>(required), L'\0');
        const int written = GetTimeFormatW(LOCALE_USER_DEFAULT, 0, &time, NULL,
                                           value.data(), required);
        if (written > 0)
        {
            value.resize(static_cast<size_t>(written - 1));
            return value;
        }
    }
    return SPLFormatStringOwned(L"%u:%02u:%02u", time.wHour, time.wMinute,
                                time.wSecond);
}

static std::wstring FormatLocaleDate(const SYSTEMTIME& time)
{
    const int required = GetDateFormatW(LOCALE_USER_DEFAULT, DATE_SHORTDATE, &time,
                                        NULL, NULL, 0);
    if (required > 0)
    {
        std::wstring value(static_cast<size_t>(required), L'\0');
        const int written = GetDateFormatW(LOCALE_USER_DEFAULT, DATE_SHORTDATE, &time,
                                           NULL, value.data(), required);
        if (written > 0)
        {
            value.resize(static_cast<size_t>(written - 1));
            return value;
        }
    }
    return SPLFormatStringOwned(L"%u.%u.%u", time.wDay, time.wMonth, time.wYear);
}

std::wstring GetInfo(const FILETIME* lastWrite, unsigned size)
{
    CALL_STACK_MESSAGE2("GetInfo(, , 0x%X)", size);
    SYSTEMTIME st;
    FILETIME ft;
    FileTimeToLocalFileTime(lastWrite, &ft);
    FileTimeToSystemTime(&ft, &st);

    const std::wstring date = FormatLocaleDate(st);
    const std::wstring time = FormatLocaleTime(st);
    const std::wstring number = SPLNumberToStrOwned(SalamanderGeneral, CQuadWord(size, 0));
    return SPLFormatStringOwned(L"%ls, %ls, %ls", number.c_str(), date.c_str(),
                                time.c_str());
}
