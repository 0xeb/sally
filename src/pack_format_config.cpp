// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"

#include "common/text/CaseFolding.h"

#include "cfgdlg.h"
#include "mainwnd.h"
#include "zip.h"
#include "usermenu.h"
#include "execute.h"
#include "plugins.h"
#include "pack.h"
#include "common/ExternalToolRunner.h"
#include "common/IFileSystem.h"
#include "common/IPathService.h"
#include "common/fsutil.h"
#include "common/widepath.h"
#include "fileswnd.h"
#include "common/Win32TextCodec.h"
#include "common/unicode/helpers.h"
#include "common/unicode/WideVariableExpansion.h"

// PackErrorHandler's varargs transport is byte-based. UTF-8 is the explicit internal
// encoding so Unicode paths and localized diagnostics survive the trip to the UI sink.
static std::string PackErrorPresentation(const wchar_t* text)
{
    const wchar_t* value = text != NULL ? text : L"";
    std::string utf8;
    if (!Win32EncodeText(CP_UTF8, value, wcslen(value), utf8))
        return "Unable to encode packer diagnostic.";
    return utf8;
}

static std::string PackSystemErrorPresentation(DWORD error)
{
    const std::wstring text = GetErrorTextOwned(error);
    return PackErrorPresentation(text.c_str());
}

static std::string FormatPackNumericError(int resourceId, DWORD value)
{
    const std::wstring formatted = FormatStrW(LoadStrOwned(resourceId).c_str(), value);
    return PackErrorPresentation(formatted.c_str());
}

//
// ****************************************************************************
// Types
// ****************************************************************************
//

// Native-wide state used by the core-only packer variable expansion table. External
// packer list files remain explicitly encoded by their own serializer; paths and command
// text do not cross the frozen plugin-SDK byte callback at all.
struct SPackExpData
{
    const wchar_t* ArcName;
    const wchar_t* SrcDir;
    const wchar_t* TgtDir;
    const wchar_t* LstName;
    const wchar_t* ExtName;
    std::wstring Buffer;
    // following variables exist because we cannot obtain the DOS name of a non-existent file
    // we handle it by returning a substitute DOS name which will later (after creating the archive) be renamed to the desired long name
    // once the archive is created
    BOOL ArcNameFilePossible; // TRUE until the substitute DOS name is used (we must use one name everywhere)
    BOOL DOSTmpFilePossible;  // TRUE while ArcName can be replaced with the substitute DOS name
    std::wstring* DOSTmpFile; // substitute name for ArcName (non-NULL only if DOSTmpFilePossible is TRUE)
    BOOL Failed;
};

class CExecuteWindow : public CWindow
{
protected:
    // wide: this window is entirely self-painted (WM_ERASEBKGND draws Text
    // directly via DrawTextW, no WM_SETTEXT marshalling), so SAVEBITS_CLASSNAME's own
    // ANSI/Unicode registration is irrelevant to what it can paint - same mechanism as
    // CTPHCaptionWindow.
    WCHAR* Text;
    HWND HParent;

public:
    CExecuteWindow(HWND hParent, int textResID, CObjectOrigin origin = ooAllocated);
    ~CExecuteWindow();

    HWND Create();

protected:
    virtual LRESULT WindowProc(UINT uMsg, WPARAM wParam, LPARAM lParam);
};

//
// ****************************************************************************
// Constants and global variables
// ****************************************************************************
//

// Timeout for opening the packer console in milliseconds
// (if less than one, it is not opened at all)
LONG PackWinTimeout = 15000;

// configuration item names in the registry
//
// predefined packers associations
const wchar_t* SALAMANDER_PPA_EXTENSIONS = L"Extension List";
const wchar_t* SALAMANDER_PPA_PINDEX = L"Packer Index";
const wchar_t* SALAMANDER_PPA_UINDEX = L"Unpacker Index";
const wchar_t* SALAMANDER_PPA_USEPACKER = L"Packer Supported";
// predefined packers configuration
const wchar_t* SALAMANDER_PPC_TITLE = L"Packer Title";
const wchar_t* SALAMANDER_PPC_PACKEXE = L"Packer Executable";
const wchar_t* SALAMANDER_PPC_EXESAME = L"Use Packer Executable To Unpack";
const wchar_t* SALAMANDER_PPC_UID = L"Packer UID";
const wchar_t* SALAMANDER_PPC_UNPACKEXE = L"Unpacker Executable";

// main packer configuration
CPackerFormatConfig PackerFormatConfig /*(FALSE)*/;
CArchiverConfig ArchiverConfig /*(FALSE)*/;

//
// Return code tables for individual supported packers
// error code 0 always means success
//

// JAR
const TPackErrorTable JARErrors =
    {
        {1, IDS_PACKRET_WARNING},
        {2, IDS_PACKRET_FATAL},
        {3, IDS_PACKRET_CRC},
        {5, IDS_PACKRET_DISK},
        {6, IDS_PACKRET_FOPEN},
        {7, IDS_PACKRET_PARAMS},
        {8, IDS_PACKRET_MEMORY},
        {9, IDS_PACKRET_NOTARC},
        {10, IDS_PACKRET_INTERN},
        {11, IDS_PACKRET_BREAK},
        {-1, -1}};
// RAR
const TPackErrorTable RARErrors =
    {
        {1, IDS_PACKRET_WARNING},
        {2, IDS_PACKRET_FATAL},
        {3, IDS_PACKRET_CRC},
        {4, IDS_PACKRET_SECURITY},
        {5, IDS_PACKRET_DISK},
        {6, IDS_PACKRET_FOPEN},
        {7, IDS_PACKRET_PARAMS},
        {8, IDS_PACKRET_MEMORY},
        {255, IDS_PACKRET_BREAK},
        {-1, -1}};
// ARJ
const TPackErrorTable ARJErrors =
    {
        {1, IDS_PACKRET_WARNING},
        {2, IDS_PACKRET_FATAL},
        {3, IDS_PACKRET_CRC},
        {4, IDS_PACKRET_SECURITY},
        {5, IDS_PACKRET_DISK},
        {6, IDS_PACKRET_FOPEN},
        {7, IDS_PACKRET_PARAMS},
        {8, IDS_PACKRET_MEMORY},
        {9, IDS_PACKRET_NOTARC},
        {10, IDS_PACKRET_XMSMEM},
        {11, IDS_PACKRET_BREAK},
        {12, IDS_PACKRET_CHAPTERS},
        {-1, -1}};
// LHA
const TPackErrorTable LHAErrors =
    {
        {1, IDS_PACKRET_EXTRACT_LHA},
        {2, IDS_PACKRET_FATAL},
        {3, IDS_PACKRET_TEMP},
        {-1, -1}};
// UC2
const TPackErrorTable UC2Errors =
    {
        {5, IDS_PACKRET_INTERN},
        {7, IDS_PACKRET_SECURITY},
        {10, IDS_PACKRET_FOPEN},
        {15, IDS_PACKRET_WARNING},
        {20, IDS_PACKRET_FOPEN},
        {25, IDS_PACKRET_SKIPPED},
        {30, IDS_PACKRET_SKIPPED},
        {35, IDS_PACKRET_SKIPPED},
        {50, IDS_PACKRET_INTERN},
        {55, IDS_PACKRET_DISK},
        {60, IDS_PACKRET_DISK},
        {65, IDS_PACKRET_FATAL},
        {70, IDS_PACKRET_DISK},
        {75, IDS_PACKRET_WARNING},
        {80, IDS_PACKRET_SKIPPED},
        {85, IDS_PACKRET_DISK},
        {90, IDS_PACKRET_DAMAGED},
        {95, IDS_PACKRET_VIRUS},
        {100, IDS_PACKRET_BREAK},
        {105, IDS_PACKRET_INTERN},
        {110, IDS_PACKRET_PARAMS},
        {115, IDS_PACKRET_PARAMS},
        {120, IDS_PACKRET_NOTARC},
        {123, IDS_PACKRET_PARAMS},
        {125, IDS_PACKRET_SECURITY},
        {130, IDS_PACKRET_NOTARC},
        {135, IDS_PACKRET_FOPEN},
        {140, IDS_PACKRET_PARAMS},
        {145, IDS_PACKRET_EXTRACT},
        {150, IDS_PACKRET_FOPEN},
        {155, IDS_PACKRET_WARNING},
        {157, IDS_PACKRET_WARNING},
        {160, IDS_PACKRET_MEMORY},
        {163, IDS_PACKRET_MEMORY},
        {165, IDS_PACKRET_MEMORY},
        {170, IDS_PACKRET_FATAL},
        {175, IDS_PACKRET_TEMP},
        {180, IDS_PACKRET_DISK},
        {185, IDS_PACKRET_FOPEN},
        {190, IDS_PACKRET_VIRUS},
        {195, IDS_PACKRET_DAMAGED},
        {200, IDS_PACKRET_DAMAGED},
        {205, IDS_PACKRET_FATAL},
        {210, IDS_PACKRET_FATAL},
        {250, IDS_PACKRET_FOPEN},
        {255, IDS_PACKRET_INTERN},
        {-1, -1}};
// PKZIP 2.04g
const TPackErrorTable ZIP204Errors =
    {
        {1, IDS_PACKRET_FOPEN},
        {2, IDS_PACKRET_CRC},
        {3, IDS_PACKRET_CRC},
        {4, IDS_PACKRET_MEMORY},
        {5, IDS_PACKRET_MEMORY},
        {6, IDS_PACKRET_MEMORY},
        {7, IDS_PACKRET_MEMORY},
        {8, IDS_PACKRET_MEMORY},
        {9, IDS_PACKRET_MEMORY},
        {10, IDS_PACKRET_MEMORY},
        {11, IDS_PACKRET_MEMORY},
        {12, IDS_PACKRET_PARAMS},
        {13, IDS_PACKRET_FOPEN},
        {14, IDS_PACKRET_DISK},
        {15, IDS_PACKRET_DISK},
        {16, IDS_PACKRET_PARAMS},
        {17, IDS_PACKRET_PARAMS},
        {18, IDS_PACKRET_FOPEN},
        {255, IDS_PACKRET_BREAK},
        {-1, -1}};
// PKUNZIP 2.04g
const TPackErrorTable UNZIP204Errors =
    {
        {1, IDS_PACKRET_WARNING},
        {2, IDS_PACKRET_CRC},
        {3, IDS_PACKRET_CRC},
        {4, IDS_PACKRET_MEMORY},
        {5, IDS_PACKRET_MEMORY},
        {6, IDS_PACKRET_MEMORY},
        {7, IDS_PACKRET_MEMORY},
        {8, IDS_PACKRET_MEMORY},
        {9, IDS_PACKRET_FOPEN},
        {10, IDS_PACKRET_PARAMS},
        {11, IDS_PACKRET_FOPEN},
        {50, IDS_PACKRET_DISK},
        {51, IDS_PACKRET_CRC},
        {255, IDS_PACKRET_BREAK},
        {-1, -1}};
// ACE
const TPackErrorTable ACEErrors =
    {
        {1, IDS_PACKRET_MEMORY},
        {2, IDS_PACKRET_FOPEN},
        {3, IDS_PACKRET_FOPEN},
        {4, IDS_PACKRET_DISK},
        {5, IDS_PACKRET_FOPEN},
        {6, IDS_PACKRET_FOPEN},
        {7, IDS_PACKRET_DISK},
        {8, IDS_PACKRET_PARAMS},
        {9, IDS_PACKRET_CRC},
        {10, IDS_PACKRET_FATAL},
        {11, IDS_PACKRET_FOPEN},
        {255, IDS_PACKRET_BREAK2},
        {-1, -1}};

// Variables distinguished in the command line and the current directory when
// launching an external program
const wchar_t* PACK_ARC_PATH = L"ArchivePath";
const wchar_t* PACK_ARC_FILE = L"ArchiveFileName";
const wchar_t* PACK_ARC_NAME = L"ArchiveFullName";
const wchar_t* PACK_SRC_PATH = L"SourcePath";
const wchar_t* PACK_TGT_PATH = L"TargetPath";
const wchar_t* PACK_LST_NAME = L"ListFullName";
const wchar_t* PACK_EXT_NAME = L"ExtractFullName";
const wchar_t* PACK_ARC_DOSFILE = L"ArchiveDOSFileName";
const wchar_t* PACK_ARC_DOSNAME = L"ArchiveDOSFullName";
const wchar_t* PACK_TGT_DOSPATH = L"TargetDOSPath";
const wchar_t* PACK_LST_DOSNAME = L"ListDOSFullName";

const wchar_t* PACK_EXE_JAR32 = L"Jar32bitExecutable";
const wchar_t* PACK_EXE_JAR16 = L"Jar16bitExecutable";
const wchar_t* PACK_EXE_RAR32 = L"Rar32bitExecutable";
const wchar_t* PACK_EXE_RAR16 = L"Rar16bitExecutable";
const wchar_t* PACK_EXE_ARJ32 = L"Arj32bitExecutable";
const wchar_t* PACK_EXE_ARJ16 = L"Arj16bitExecutable";
const wchar_t* PACK_EXE_ACE32 = L"Ace32bitExecutable";
const wchar_t* PACK_EXE_ACE16 = L"Ace16bitExecutable";
const wchar_t* PACK_EXE_LHA16 = L"Lha16bitExecutable";
const wchar_t* PACK_EXE_UC216 = L"UC216bitExecutable";
const wchar_t* PACK_EXE_ZIP32 = L"Zip32bitExecutable";
const wchar_t* PACK_EXE_ZIP16 = L"Zip16bitExecutable";
const wchar_t* PACK_EXE_UZP16 = L"Unzip16bitExecutable";

// Menu in configuration

/* used by the export_mnu.py script that generates salmenu.mnu for the
   Translator; keep synchronized with the array below...
MENU_TEMPLATE_ITEM CmdCustomPackers[] = 
{
  {MNTT_PB, 0
  {MNTT_IT, IDS_PACK_EXE_JAR32
  {MNTT_IT, IDS_PACK_EXE_JAR16
  {MNTT_IT, IDS_PACK_EXE_RAR32
  {MNTT_IT, IDS_PACK_EXE_RAR16
  {MNTT_IT, IDS_PACK_EXE_ARJ32
  {MNTT_IT, IDS_PACK_EXE_ARJ16
  {MNTT_IT, IDS_PACK_EXE_ACE32
  {MNTT_IT, IDS_PACK_EXE_ACE16
  {MNTT_IT, IDS_PACK_EXE_LHA16
  {MNTT_IT, IDS_PACK_EXE_UC216
  {MNTT_IT, IDS_PACK_EXE_ZIP32
  {MNTT_IT, IDS_PACK_EXE_ZIP16
  {MNTT_IT, IDS_PACK_EXE_UZP16
  {MNTT_IT, IDS_PACK_EXE_BROWSE
  {MNTT_PE, 0
};
*/

// Command
CExecuteItem CmdCustomPackers[] =
    {
        {PACK_EXE_JAR32, IDS_PACK_EXE_JAR32, EIF_VARIABLE | EIF_REPLACE_ALL},
        {PACK_EXE_JAR16, IDS_PACK_EXE_JAR16, EIF_VARIABLE | EIF_REPLACE_ALL},
        {PACK_EXE_RAR32, IDS_PACK_EXE_RAR32, EIF_VARIABLE | EIF_REPLACE_ALL},
        {PACK_EXE_RAR16, IDS_PACK_EXE_RAR16, EIF_VARIABLE | EIF_REPLACE_ALL},
        {PACK_EXE_ARJ32, IDS_PACK_EXE_ARJ32, EIF_VARIABLE | EIF_REPLACE_ALL},
        {PACK_EXE_ARJ16, IDS_PACK_EXE_ARJ16, EIF_VARIABLE | EIF_REPLACE_ALL},
        {PACK_EXE_ACE32, IDS_PACK_EXE_ACE32, EIF_VARIABLE | EIF_REPLACE_ALL},
        {PACK_EXE_ACE16, IDS_PACK_EXE_ACE16, EIF_VARIABLE | EIF_REPLACE_ALL},
        {PACK_EXE_LHA16, IDS_PACK_EXE_LHA16, EIF_VARIABLE | EIF_REPLACE_ALL},
        {PACK_EXE_UC216, IDS_PACK_EXE_UC216, EIF_VARIABLE | EIF_REPLACE_ALL},
        {PACK_EXE_ZIP32, IDS_PACK_EXE_ZIP32, EIF_VARIABLE | EIF_REPLACE_ALL},
        {PACK_EXE_ZIP16, IDS_PACK_EXE_ZIP16, EIF_VARIABLE | EIF_REPLACE_ALL},
        {PACK_EXE_UZP16, IDS_PACK_EXE_UZP16, EIF_VARIABLE | EIF_REPLACE_ALL},
        {EXECUTE_SEPARATOR, 0, 0},
        {EXECUTE_BROWSE, IDS_PACK_EXE_BROWSE, EIF_REPLACE_ALL},
        {EXECUTE_TERMINATOR, 0, 0},
};

/* used by the export_mnu.py script that generates salmenu.mnu for the
   Translator; keep synchronized with the array below...
MENU_TEMPLATE_ITEM ArgsCustomPackers[] = 
{
  {MNTT_PB, 0
  {MNTT_IT, IDS_PACK_ARC_NAME
  {MNTT_IT, IDS_PACK_ARC_FILE
  {MNTT_IT, IDS_PACK_ARC_PATH
  {MNTT_IT, IDS_PACK_LST_NAME
  {MNTT_IT, IDS_PACK_ARC_DOSNAME
  {MNTT_IT, IDS_PACK_ARC_DOSFILE
  {MNTT_IT, IDS_PACK_LST_DOSNAME
  {MNTT_PE, 0
};
*/

// Arguments
// Custom packers/unpackers
CExecuteItem ArgsCustomPackers[] =
    {
        {PACK_ARC_NAME, IDS_PACK_ARC_NAME, EIF_VARIABLE},
        {PACK_ARC_FILE, IDS_PACK_ARC_FILE, EIF_VARIABLE},
        {PACK_ARC_PATH, IDS_PACK_ARC_PATH, EIF_VARIABLE},
        {PACK_LST_NAME, IDS_PACK_LST_NAME, EIF_VARIABLE},
        {EXECUTE_SEPARATOR, 0, 0},
        {PACK_ARC_DOSNAME, IDS_PACK_ARC_DOSNAME, EIF_VARIABLE},
        {PACK_ARC_DOSFILE, IDS_PACK_ARC_DOSFILE, EIF_VARIABLE},
        {PACK_LST_DOSNAME, IDS_PACK_LST_DOSNAME, EIF_VARIABLE},
        {EXECUTE_TERMINATOR, 0, 0},
};

//
// ****************************************************************************
// Functions
// ****************************************************************************
//

// Function for initializing the spawn executable name with full path
BOOL InitSpawnName(HWND parent)
{
    CALL_STACK_MESSAGE1("InitSpawnName()");
    if (!SpawnExeInitialised)
    {
        std::wstring modulePath;
        PathResult pathResult = gPathService != NULL
                                    ? gPathService->GetModuleFileName(NULL, modulePath)
                                    : PathResult::Error(ERROR_INVALID_PARAMETER);
        if (pathResult.success)
        {
            const size_t nameOffset = modulePath.find_last_of(L'\\');
            if (nameOffset == std::wstring::npos)
                pathResult = PathResult::Error(ERROR_INVALID_NAME);
            else
            {
                modulePath.resize(nameOffset + 1);
                modulePath += L"utils\\";
                modulePath += SPAWN_EXE_NAME;
                SpawnExe = std::move(modulePath);
                SpawnExeInitialised = TRUE;
            }
        }
        if (!pathResult.success)
        {
            // PackErrorHandler formats this byte payload before widening it at the prompter boundary.
            const std::string message = "GetModuleFileName: " + PackSystemErrorPresentation(pathResult.errorCode);
            return (*PackErrorHandlerPtr)(parent, IDS_PACKERR_GENERAL, message.c_str());
        }
    }
    return TRUE;
}

//
// ****************************************************************************
// Implementation of the configuration object - association of extensions and packers
//

CPackerFormatConfig::CPackerFormatConfig(/*BOOL disableDefaultValues*/)
    : Formats(10, 5)
{
    /*
  if (!disableDefaultValues)
  {
    AddDefault(0);
    if (!BuildArray())
    {
      TRACE_E("Unable to create data for archive detection");
    }
  }
*/
}

void CPackerFormatConfig::InitializeDefaultValues()
{
    AddDefault(0);
    if (!BuildArray())
    {
        TRACE_E("Unable to create data for archive detection");
    }
}

void CPackerFormatConfig::AddDefault(int SalamVersion)
{
    int index;

    switch (SalamVersion)
    {
    case 0: // default
    case 1: // version 1.52 had no packers
        if ((index = AddFormat()) == -1)
            return;
        SetFormat(index, L"zip", TRUE, -1, -1, TRUE);
        if ((index = AddFormat()) == -1)
            return;
        SetFormat(index, L"j", TRUE, 0, 0, TRUE);
        if ((index = AddFormat()) == -1)
            return;
        SetFormat(index, L"rar;r##", TRUE, 1, 1, TRUE);
        if ((index = AddFormat()) == -1)
            return;
        SetFormat(index, L"arj;a##", TRUE, 9, 9, TRUE);
        if ((index = AddFormat()) == -1)
            return;
        SetFormat(index, L"lzh", TRUE, 3, 3, TRUE);
        if ((index = AddFormat()) == -1)
            return;
        SetFormat(index, L"uc2", TRUE, 4, 4, TRUE);
        if ((index = AddFormat()) == -1)
            return;
        SetFormat(index, L"ace", TRUE, 10, 10, TRUE);

    case 2: // what was added after beta1
        // workaround to add the PK3 extension to ZIP
        for (index = 0; index < Formats.Count; index++)
            if (!_wcsicmp(Formats[index]->Ext.c_str(), L"zip"))
            {
                Formats[index]->Ext += L";pk3";
                break;
            }
        // and new extensions
        if ((index = AddFormat()) == -1)
            return;
        SetFormat(index, L"pak", TRUE, -3, -3, TRUE);

    case 3: // what was added after beta2
    case 4: // beta3 but with old configuration (contains $(SpawnName))
        // workaround to add the C## extension to ACE
        for (index = 0; index < Formats.Count; index++)
            if (!_wcsicmp(Formats[index]->Ext.c_str(), L"ace"))
            {
                Formats[index]->Ext += L";c##";
                break;
            }

    case 5: // what's new in beta4?
        if ((index = AddFormat()) == -1)
            return;
        SetFormat(index, L"tgz;tbz;taz;tar;gz;bz;bz2;z;rpm;cpio", FALSE, 0, -2, TRUE);
        // workaround to add the JAR extension to ZIP
        for (index = 0; index < Formats.Count; index++)
            if (!_wcsicmp(Formats[index]->Ext.c_str(), L"zip;pk3"))
            {
                Formats[index]->Ext += L";jar";
                break;
            }
    }
}

BOOL CPackerFormatConfig::BuildArray(int* line, int* column)
{
    BOOL ret = TRUE;
    wchar_t buffer[501];
    buffer[500] = '\0';

    // clear the existing array
    int i;
    for (i = 0; i < 256; i++)
        Extensions[i].DestroyMembers();
    // and fill it again
    CExtItem item;
    for (i = 0; i < Formats.Count; i++)
    {
        wcsncpy_s(buffer, GetExt(i), _TRUNCATE);
        wchar_t* context = NULL;
        wchar_t* ptr = wcstok_s(buffer, L";", &context);
        while (ptr != NULL)
        {
            const int len = static_cast<int>(std::wcslen(ptr));
            wchar_t* ext = static_cast<wchar_t*>(
                malloc((static_cast<size_t>(len) + 1) * sizeof(wchar_t)));
            if (ext == NULL)
            {
                if (ret)
                {
                    if (line != NULL)
                        *line = i;
                    if (column != NULL)
                        *column = static_cast<int>(ptr - buffer);
                }
                ret = FALSE;
                break;
            }
            int last = len - 1;
            int idx = 0;
            for (int source = len - 2; source >= 0; --source)
                ext[idx++] = sally::unicode::FoldCharW(ptr[source]);
            ext[idx++] = L'.';
            ext[idx] = L'\0';
            item.Set(ext, i);
            const wchar_t foldedLast =
                sally::unicode::FoldCharW(ptr[last]);
            const unsigned bucket =
                static_cast<unsigned char>(foldedLast & 0xff);
            if (!Extensions[bucket].SIns(item))
            {
                free(ext);
                if (ret)
                {
                    if (line != NULL)
                        *line = i;
                    if (column != NULL)
                        *column = (int)(ptr - buffer);
                    ret = FALSE;
                }
            }
            ptr = wcstok_s(NULL, L";", &context);
        }
    }
    item.Set(NULL, 0);
    return ret;
}

int CPackerFormatConfig::AddFormat()
{
    CPackerFormatConfigData* data = new CPackerFormatConfigData;
    if (data == NULL)
        return -1;
    int index = Formats.Add(data);
    if (!Formats.IsGood())
    {
        Formats.ResetState();
        return -1;
    }
    return index;
}

BOOL CPackerFormatConfig::SetFormat(int index, const wchar_t* ext, BOOL usePacker,
                                    const int packerIndex, const int unpackerIndex, BOOL old)

{
    CPackerFormatConfigData* data = Formats[index];

    // Copy BEFORE Destroy(). GetExt(index) hands back Formats[index]->Ext.c_str(),
    // and CCfgPageArchivesAssoc::StoreControls passes exactly that straight back
    // in - so Destroy()'s Ext.clear() emptied the very buffer 'ext' pointed at,
    // the assignment below then stored an empty extension, IsValid() failed, and
    // Formats.Delete(index) removed the row from the user's configuration. Every
    // visit to Configuration / Archives Associations quietly deleted rows.
    const std::wstring stagedExt = ext != NULL ? ext : L"";
    data->Destroy();

    data->Ext = stagedExt;
    data->UsePacker = usePacker;
    if (usePacker)
        data->PackerIndex = packerIndex;
    else
        data->PackerIndex = -1;
    data->UnpackerIndex = unpackerIndex;
    data->OldType = old;

    if (data->IsValid())
        return TRUE;
    else
    {
        TRACE_E("invalid data");
        Formats.Delete(index);
        return FALSE;
    }
}

// returns the format table index + 1 or FALSE (0) when it's not an archive
int CPackerFormatConfig::PackIsArchive(const wchar_t* archiveName, int archiveNameLen)
{
    // j.r. I disabled the macro because PackIsArchive is heavily called from CFilesWindow::CommonRefresh()
    CALL_STACK_MESSAGE_NONE
    //  CALL_STACK_MESSAGE2("PackIsArchive(%s)", archiveName);
    if (archiveName == NULL || archiveNameLen < -1 || archiveName[0] == L'\0')
        return 0; // not found
    const int length = archiveNameLen == -1
                           ? static_cast<int>(std::wcslen(archiveName))
                           : archiveNameLen;
    if (length <= 0)
        return 0;
    const int idx = length - 1;

    const auto findInBucket = [&](unsigned bucket) {
        CStringArray* array = &Extensions[bucket];
        for (int i = 0; i < array->Count; ++i)
        {
            const wchar_t* pattern = array->At(i).GetExt();
            int nameIndex = idx - 1;
            while (*pattern != L'\0' && nameIndex >= 0)
            {
                const wchar_t name = archiveName[nameIndex];
                if ((*pattern != L'#' &&
                     *pattern == sally::unicode::FoldCharW(name)) ||
                    (*pattern == L'#' && name >= L'0' && name <= L'9'))
                {
                    ++pattern;
                    --nameIndex;
                    continue;
                }
                break;
            }
            if (*pattern == L'\0')
                return array->At(i).GetIndex() + 1;
        }
        return 0;
    };

    const wchar_t foldedLast =
        sally::unicode::FoldCharW(archiveName[idx]);
    const unsigned bucket = static_cast<unsigned char>(foldedLast & 0xff);
    const int exact = findInBucket(bucket);
    if (exact != 0)
        return exact;

    // if the last character is a digit
    if (archiveName[idx] >= L'0' && archiveName[idx] <= L'9')
        return findInBucket(static_cast<unsigned>(L'#'));
    return 0; // not found
}

BOOL CPackerFormatConfig::Load(CPackerFormatConfig& src)
{
    DeleteAllFormats();
    int i;
    for (i = 0; i < src.GetFormatsCount(); i++)
    {
        int index = AddFormat();
        if (index == -1)
            return FALSE;
        if (!SetFormat(index, src.GetExt(i), src.GetUsePacker(i),
                       src.GetPackerIndex(i), src.GetUnpackerIndex(i), src.GetOldType(i)))
            return FALSE;
    }
    return TRUE;
}

BOOL CPackerFormatConfig::Save(int index, HKEY hKey)
{
    DWORD d;
    BOOL ret = TRUE;
    if (ret)
        ret &= SetValueW(hKey, SALAMANDER_PPA_EXTENSIONS, REG_SZ, GetExt(index), -1);
    d = GetUsePacker(index);
    if (ret)
        ret &= SetValueW(hKey, SALAMANDER_PPA_USEPACKER, REG_DWORD, &d, sizeof(d));
    d = GetPackerIndex(index);
    if (ret)
        ret &= SetValueW(hKey, SALAMANDER_PPA_PINDEX, REG_DWORD, &d, sizeof(d));
    d = GetUnpackerIndex(index);
    if (ret)
        ret &= SetValueW(hKey, SALAMANDER_PPA_UINDEX, REG_DWORD, &d, sizeof(d));
    return ret;
}

BOOL CPackerFormatConfig::Load(HKEY hKey)
{
    std::wstring ext;
    DWORD packerIndex, unpackerIndex, usePacker;

    BOOL ret = TRUE;
    if (ret)
        ret &= GetStringValueW(hKey, SALAMANDER_PPA_EXTENSIONS, ext);
    if (ret)
        ret &= GetValueW(hKey, SALAMANDER_PPA_USEPACKER, REG_DWORD, &usePacker, sizeof(DWORD));
    if (ret)
        ret &= GetValueW(hKey, SALAMANDER_PPA_PINDEX, REG_DWORD, &packerIndex, sizeof(DWORD));
    if (ret)
        ret &= GetValueW(hKey, SALAMANDER_PPA_UINDEX, REG_DWORD, &unpackerIndex, sizeof(DWORD));

    if (ret)
    {
        int index;
        if ((index = AddFormat()) == -1)
            return FALSE;
        if (Configuration.ConfigVersion < 44) // convert extensions to lowercase
            ext = sally::text::Fold(ext);
        ret &= SetFormat(index, ext.c_str(), usePacker, packerIndex, unpackerIndex,
                         Configuration.ConfigVersion < 6);
    }

    return ret;
}

/*
BOOL
CPackerFormatConfig::SwapFormats(int index1, int index2)
{
  std::swap(Formats[index1], Formats[index2]);
  return TRUE;
}
*/

BOOL CPackerFormatConfig::MoveFormat(int srcIndex, int dstIndex)
{
    CPackerFormatConfigData* tmp = Formats[srcIndex];
    if (srcIndex < dstIndex)
    {
        int i;
        for (i = srcIndex; i < dstIndex; i++)
            Formats[i] = Formats[i + 1];
    }
    else
    {
        int i;
        for (i = srcIndex; i > dstIndex; i--)
            Formats[i] = Formats[i - 1];
    }
    Formats[dstIndex] = tmp;
    return TRUE;
}

void CPackerFormatConfig::DeleteFormat(int index)
{
    Formats.Delete(index);
}

//
// ****************************************************************************
// Implementation of the configuration class - predefined packers
//

// constructor
CArchiverConfig::CArchiverConfig(/*BOOL disableDefaultValues*/)
    : Archivers(20, 10)
{
    /*
  // set default values if it is not disabled
  if (!disableDefaultValues)
    AddDefault(0);
*/
}

void CArchiverConfig::InitializeDefaultValues()
{
    // set default values if it is not disabled
    AddDefault(0);
}

// sets default values
void CArchiverConfig::AddDefault(int SalamVersion)
{
    int index;

    // BOOL SetArchiver(int index, DWORD uid, const char *title, EPackExeType type, BOOL exesAreSame,
    //                  const char *packerVariable, const char *unpackerVariable,
    //                  const char *packerExecutable, const char *unpackerExecutable,
    //                  const char *packExeFile, const char *unpackExeFile)

    switch (SalamVersion)
    {
    case 0: // default
    case 1: // version 1.52 had no packers
        if ((index = AddArchiver()) == -1)
            return;
        SetArchiver(index, ARC_UID_JAR32, LoadStrW(IDS_EXT_JAR32), EXE_32BIT, TRUE, PACK_EXE_JAR32, NULL,
                    L"jar32", NULL, L"jar32", NULL);
        if ((index = AddArchiver()) == -1)
            return;
        SetArchiver(index, ARC_UID_RAR32, LoadStrW(IDS_EXT_RAR32), EXE_32BIT, TRUE, PACK_EXE_RAR32, NULL,
                    L"rar", NULL, L"rar", NULL);
        if ((index = AddArchiver()) == -1)
            return;
        SetArchiver(index, ARC_UID_ARJ16, LoadStrW(IDS_EXT_ARJ16), EXE_16BIT, TRUE, PACK_EXE_ARJ16, NULL,
                    L"arj", NULL, L"arj", NULL);
        if ((index = AddArchiver()) == -1)
            return;
        SetArchiver(index, ARC_UID_LHA16, LoadStrW(IDS_EXT_LHA16), EXE_16BIT, TRUE, PACK_EXE_LHA16, NULL,
                    L"lha", NULL, L"lha", NULL);
        if ((index = AddArchiver()) == -1)
            return;
        SetArchiver(index, ARC_UID_UC216, LoadStrW(IDS_EXT_UC216), EXE_16BIT, TRUE, PACK_EXE_UC216, NULL,
                    L"uc", NULL, L"uc", NULL);
        if ((index = AddArchiver()) == -1)
            return;
        SetArchiver(index, ARC_UID_JAR16, LoadStrW(IDS_EXT_JAR16), EXE_16BIT, TRUE, PACK_EXE_JAR16, NULL,
                    L"jar16", NULL, L"jar16", NULL);
        if ((index = AddArchiver()) == -1)
            return;
        SetArchiver(index, ARC_UID_RAR16, LoadStrW(IDS_EXT_RAR16), EXE_16BIT, TRUE, PACK_EXE_RAR16, NULL,
                    L"rar", NULL, L"rar", NULL);
        if ((index = AddArchiver()) == -1)
            return;
        SetArchiver(index, ARC_UID_ZIP32, LoadStrW(IDS_EXT_ZIP32), EXE_32BIT, TRUE, PACK_EXE_ZIP32, NULL,
                    L"pkzip25", NULL, L"pkzip25", NULL);
        if ((index = AddArchiver()) == -1)
            return;
        SetArchiver(index, ARC_UID_ZIP16, LoadStrW(IDS_EXT_ZIP16), EXE_16BIT, FALSE, PACK_EXE_ZIP16, PACK_EXE_UZP16,
                    L"pkzip", L"pkunzip", L"pkzip", L"pkunzip");
        if ((index = AddArchiver()) == -1)
            return;
        SetArchiver(index, ARC_UID_ARJ32, LoadStrW(IDS_EXT_ARJ32), EXE_32BIT, TRUE, PACK_EXE_ARJ32, NULL,
                    L"arj32", NULL, L"arj32", NULL);
        if ((index = AddArchiver()) == -1)
            return;
        SetArchiver(index, ARC_UID_ACE32, LoadStrW(IDS_EXT_ACE32), EXE_32BIT, TRUE, PACK_EXE_ACE32, NULL,
                    L"ace32", NULL, L"ace32", NULL);
        if ((index = AddArchiver()) == -1)
            return;
        SetArchiver(index, ARC_UID_ACE16, LoadStrW(IDS_EXT_ACE16), EXE_16BIT, TRUE, PACK_EXE_ACE16, NULL,
                    L"ace", NULL, L"ace", NULL);
        //    case 2:  // what was added after beta1
        //    case 3:  // what was added after beta2
        //    case 4:  // beta3 but with old configuration (contains $(SpawnName))
        //    case 5:   // beta3 but without tar
        //    case 6:   // what's new in beta4?
    }
}

// initializes the configuration based on another configuration
BOOL CArchiverConfig::Load(CArchiverConfig& src)
{
    // clear what we have (if we have anything)
    DeleteAllArchivers();
    // and add what we received
    int i;
    for (i = 0; i < src.GetArchiversCount(); i++)
    {
        int index = AddArchiver();
        if (index == -1)
            return FALSE;
        if (!SetArchiver(index, src.GetArchiverUID(i), src.GetArchiverTitle(i), src.GetArchiverType(i),
                         src.ArchiverExesAreSame(i),
                         src.GetPackerVariable(i), src.GetUnpackerVariable(i),
                         src.GetPackerExecutable(i), src.GetUnpackerExecutable(i),
                         src.GetPackerExeFile(i), src.GetUnpackerExeFile(i)))
            return FALSE;
    }
    return TRUE;
}

// creates a new empty archiver at the end of the array
int CArchiverConfig::AddArchiver()
{
    CArchiverConfigData* data = new CArchiverConfigData;
    if (data == NULL)
        return -1;
    int index = Archivers.Add(data);
    if (!Archivers.IsGood())
    {
        Archivers.ResetState();
        delete data;
        return -1;
    }
    return index;
}

// sets the archiver at the given index to the requested values
BOOL CArchiverConfig::SetArchiver(int index, DWORD uid, const wchar_t* title, EPackExeType type, BOOL exesAreSame,
                                  const wchar_t* packerVariable, const wchar_t* unpackerVariable,
                                  const wchar_t* packerExecutable, const wchar_t* unpackerExecutable,
                                  const wchar_t* packExeFile, const wchar_t* unpackExeFile)
{
    // clear old data, if we have any
    CArchiverConfigData* data = Archivers[index];
    data->Destroy();

    data->UID = uid;
    data->Title = title;
    data->Type = type;
    data->ExesAreSame = exesAreSame;
    // the variable and executable name are constant strings from Salamander's code; a shallow copy is enough
    data->PackerVariable = packerVariable;
    data->PackerExecutable = packerExecutable;
    // the path to the executable is allocated, make a copy
    data->PackExeFile = packExeFile;
    if (!data->ExesAreSame)
    {
        // if the unpacker differs, initialize it as well
        data->UnpackerVariable = unpackerVariable;
        data->UnpackerExecutable = unpackerExecutable;
        data->UnpackExeFile = unpackExeFile;
    }
    else
    {
        // if the packer is the same, the work is easier
        data->UnpackerVariable = NULL;
        data->UnpackerExecutable = NULL;
        data->UnpackExeFile = packExeFile;
    }

    // are the values meaningful?
    if (data->IsValid())
        return TRUE;
    else
    {
        TRACE_E("invalid data");
        Archivers.Delete(index);
        return FALSE;
    }
}

// sets the packer path for the packer at the given index
// (called from the transfer when closing the auto-configuration dialog)
void CArchiverConfig::SetPackerExeFile(int index, const wchar_t* filename)
{
    CArchiverConfigData* data = Archivers[index];
    // if we get NULL (not found by auto-configuration), use the default executable name
    data->PackExeFile = filename != NULL ? filename : data->PackerExecutable;
}

// sets the packer path for the unpacker at the given index
// (called from the transfer when closing the auto-configuration dialog)
void CArchiverConfig::SetUnpackerExeFile(int index, const wchar_t* filename)
{
    CArchiverConfigData* data = Archivers[index];
    // if we get NULL (not found by auto-configuration), use the default executable name
    data->UnpackExeFile = filename != NULL ? filename : data->UnpackerExecutable;
}

// saves the configuration of a single entry into the registry
BOOL CArchiverConfig::Save(int index, HKEY hKey)
{
    DWORD d;
    BOOL ret = TRUE;
    // save UID
    d = GetArchiverUID(index);
    if (ret)
        ret &= SetValueW(hKey, SALAMANDER_PPC_UID, REG_DWORD, &d, sizeof(d));
    // (saves the title) - because it is translated it can no longer be used for identification
    //   of the archiver (UID is now used instead), there is no point in storing it
    //  if (ret) ret &= SetValueW(hKey, SALAMANDER_PPC_TITLE, REG_SZ, GetArchiverTitle(index), -1);
    // save the executable path
    if (ret)
        ret &= SetValueW(hKey, SALAMANDER_PPC_PACKEXE, REG_SZ, GetPackerExeFile(index), -1);
    // save whether packer and unpacker are the same
    d = ArchiverExesAreSame(index);
    if (ret)
        ret &= SetValueW(hKey, SALAMANDER_PPC_EXESAME, REG_DWORD, &d, sizeof(d));

    // and if not, also store the path to the unpacker
    if (!ArchiverExesAreSame(index))
        if (ret)
            ret &= SetValueW(hKey, SALAMANDER_PPC_UNPACKEXE, REG_SZ, GetUnpackerExeFile(index), -1);
    return ret;
}

// loads configuration of a single entry from the registry

// j.r. I reworked configuration loading -- it searches the default list and if a
//      registry item is found in this default list, its values are used, otherwise it is ignored.
//      The old method caused problems because users manually deleted the key
//      contents and there was no path (from the dialog) to restore the original list.
//      It also crashed Salamander, see bug CCfgPageExternalArchivers::DialogProc(0x111

BOOL CArchiverConfig::Load(HKEY hKey)
{
    std::wstring title;
    std::wstring packExe;
    std::wstring unpackExe;
    DWORD exesAreSame = TRUE;
    DWORD uid = -1;

    BOOL ret = TRUE;
    // loads the title
    if (ret && Configuration.ConfigVersion <= 64)
        ret &= GetStringValueW(hKey, SALAMANDER_PPC_TITLE, title);
    // loads the packing executable
    if (ret)
        ret &= GetStringValueW(hKey, SALAMANDER_PPC_PACKEXE, packExe);
    // determine whether the unpacker is the same
    if (ret)
        ret &= GetValueW(hKey, SALAMANDER_PPC_EXESAME, REG_DWORD, &exesAreSame, sizeof(DWORD));
    // UID of the archiver (Title was previously used instead, but it is translated now and it can no longer be used)
    if (ret && Configuration.ConfigVersion > 64)
        ret &= GetValueW(hKey, SALAMANDER_PPC_UID, REG_DWORD, &uid, sizeof(DWORD));
    // load the unpacker executable, if it is different from the packer
    if (!exesAreSame)
        if (ret)
            ret &= GetStringValueW(hKey, SALAMANDER_PPC_UNPACKEXE, unpackExe);

    if (ret)
    {
        int i;
        for (i = 0; i < Archivers.Count; i++)
        {
            CArchiverConfigData* arch = Archivers[i];
            // for keys that are complete and whose title matches the default value, take over their paths
            if (Configuration.ConfigVersion <= 64 && _wcsicmp(title.c_str(), arch->Title.c_str()) == 0 || // Title is now translated and cannot be used anymore
                Configuration.ConfigVersion > 64 && uid == arch->UID)                    // thus we introduced a standard UID
            {
                SetPackerExeFile(i, packExe.c_str());
                SetUnpackerExeFile(i, exesAreSame ? packExe.c_str() : unpackExe.c_str());
                break;
            }
        }
    }
    return ret;
}
/*
BOOL
CArchiverConfig::Load(HKEY hKey)
{
  int max = MAX_PATH + 2;
  wchar_t title[MAX_PATH + 2]; title[0] = 0;
  wchar_t packExe[MAX_PATH + 2]; packExe[0] = 0;
  wchar_t unpackExe[MAX_PATH + 2]; unpackExe[0] = 0;
  DWORD exesAreSame;

  BOOL ret = TRUE;
  // loads the title
  if (ret) ret &= GetValueW(hKey, SALAMANDER_PPC_TITLE, REG_SZ, title, max);
  // loads the packing executable
  if (ret) ret &= GetValueW(hKey, SALAMANDER_PPC_PACKEXE, REG_SZ, packExe, max);
  // determine whether the unpacker is the same
  if (ret) ret &= GetValueW(hKey, SALAMANDER_PPC_EXESAME, REG_DWORD, &exesAreSame, sizeof(DWORD));
  // loads the unpacker executable, if it is different from the packer
  if (!exesAreSame)
    if (ret) ret &= GetValueW(hKey, SALAMANDER_PPC_UNPACKEXE, REG_SZ, unpackExe, max);

  EPackExeType type;
  const char *name, *variablePack, *variableUnpack = NULL, *exePack, *exeUnpack = NULL;

  // and now convert to a newer configuration - missing information is taken from defaults
  // (none of it is configurable anyway :-))
  if (ret)
  {
    int index;
    if ((index = AddArchiver()) == -1) return FALSE;
    // I now assume the indices in the configuration keep their order. If not, nothing is loaded
    switch (index)
    {
      case 0:
        name = LoadStr(IDS_EXT_JAR32);
        type = EXE_32BIT;
        variablePack = PACK_EXE_JAR32;
        exePack = "jar32";
        break;
      case 1:
        name = LoadStr(IDS_EXT_RAR32);
        type = EXE_32BIT;
        variablePack = PACK_EXE_RAR32;
        exePack = "rar";
        break;
      case 2:
        name = LoadStr(IDS_EXT_ARJ16);
        type = EXE_16BIT;
        variablePack = PACK_EXE_ARJ16;
        exePack = "arj";
        break;
      case 3:
        name = LoadStr(IDS_EXT_LHA16);
        type = EXE_16BIT;
        variablePack = PACK_EXE_LHA16;
        exePack = "lha";
        break;
      case 4:
        name = LoadStr(IDS_EXT_UC216);
        type = EXE_16BIT;
        variablePack = PACK_EXE_UC216;
        exePack = "uc";
        break;
      case 5:
        name = LoadStr(IDS_EXT_JAR16);
        type = EXE_16BIT;
        variablePack = PACK_EXE_JAR16;
        exePack = "jar16";
        break;
      case 6:
        name = LoadStr(IDS_EXT_RAR16);
        type = EXE_16BIT;
        variablePack = PACK_EXE_RAR16;
        exePack = "rar";
        break;
      case 7:
        name = LoadStr(IDS_EXT_ZIP32);
        type = EXE_32BIT;
        variablePack = PACK_EXE_ZIP32;
        exePack = "pkzip25";
        break;
      case 8:
        name = LoadStr(IDS_EXT_ZIP16);
        type = EXE_16BIT;
        variablePack = PACK_EXE_ZIP16;
        variableUnpack = PACK_EXE_UZP16;
        exePack = "pkzip";
        exeUnpack = "pkunzip";
        break;
      case 9:
        name = LoadStr(IDS_EXT_ARJ32);
        type = EXE_32BIT;
        variablePack = PACK_EXE_ARJ32;
        exePack = "arj32";
        break;
      case 10:
        name = LoadStr(IDS_EXT_ACE32);
        type = EXE_32BIT;
        variablePack = PACK_EXE_ACE32;
        exePack = "ace32";
        break;
      case 11:
        name = LoadStr(IDS_EXT_ACE16);
        type = EXE_16BIT;
        variablePack = PACK_EXE_ACE16;
        exePack = "ace";
        break;
      default:
        TRACE_E("Too big index of packer, probably mistake in registry");
        Archivers.Delete(index);  // To avoid leaving an uninitialized structure; Salamander 2.0 crashed in SaveConfig
        return FALSE;
    }
    // verify we are really adding the packer we think we are adding
    if (strncmp(title, name, 10) || (exesAreSame && exeUnpack != NULL) || (!exesAreSame && exeUnpack == NULL))
    {
      TRACE_E("Inconsistency in configuration of packers.");
      Archivers.Delete(index);  // To avoid leaving an uninitialized structure; Salamander 2.0 crashed in SaveConfig
      return FALSE;
    }
    // and set all information
    ret &= SetArchiver(index, name, type, exesAreSame, variablePack, variableUnpack,
                       exePack, exeUnpack, packExe, unpackExe);
  }
  return ret;
}
*/

//
// ****************************************************************************
// Parsing functions for replacing variables with their values
//

std::wstring PackExpArcPath(void* param)
{
    SPackExpData* data = (SPackExpData*)param;
    const wchar_t* s = wcsrchr(data->ArcName, L'\\');
    if (s == NULL)
    {
        TRACE_E("Unexpected value in PackExpArcPath().");
        data->Failed = TRUE;
        return L"";
    }
    data->Buffer.assign(data->ArcName, s - data->ArcName + 1);
    return data->Buffer;
}

std::wstring PackExpArcName(void* param)
{
    SPackExpData* data = (SPackExpData*)param;
    if (!data->ArcNameFilePossible)
    {
        TRACE_E("It is not possible to combine DOS and long archive file name (ArchiveFileName and ArchiveFullName) in PackExpArcName().");
        data->Failed = TRUE;
        return L"";
    }
    data->DOSTmpFilePossible = FALSE; // from now on only ArcName

    return data->ArcName;
}

std::wstring PackExpArcFile(void* param)
{
    SPackExpData* data = (SPackExpData*)param;

    if (!data->ArcNameFilePossible)
    {
        TRACE_E("It is not possible to combine DOS and long archive file name (ArchiveFileName and ArchiveFullName) in PackExpArcFile().");
        data->Failed = TRUE;
        return L"";
    }
    data->DOSTmpFilePossible = FALSE; // from now on only ArcName

    const wchar_t* s = wcsrchr(data->ArcName, L'\\');
    if (s == NULL)
    {
        TRACE_E("Unexpected value in PackExpArcFile().");
        data->Failed = TRUE;
        return L"";
    }
    data->Buffer = s + 1;
    return data->Buffer;
}

std::wstring PackExpArcDosName(void* param)
{
    SPackExpData* data = (SPackExpData*)param;
    std::wstring dosArchiveName;

    if (data->ArcNameFilePossible)
    {
        dosArchiveName = GetShortPathW(data->ArcName);
        if (dosArchiveName.empty())
        {
            if (!data->DOSTmpFilePossible)
            {
                TRACE_E("Error (1) in GetShortPathName() in PackExpArcDosName().");
                data->Failed = TRUE;
                return L"";
            }
            data->ArcNameFilePossible = FALSE; // from now on only DOSTmpName
        }
        else
            data->DOSTmpFilePossible = FALSE; // from now on only ArcName
    }
    else
    {
        if (!data->DOSTmpFilePossible)
        {
            TRACE_E("Unable to return DOS nor long archive file name.");
            data->Failed = TRUE;
            return L"";
        }
    }

    if (data->DOSTmpFilePossible) // use a substitute name
    {
        if (data->DOSTmpFile->empty()) // it needs to be generated
        {
            std::wstring directory = data->ArcName;
            if (CutDirectory(directory.data()))
            {
                directory.resize(wcslen(directory.c_str()));
                SalPathAddBackslashW(directory);
                const std::wstring baseName = directory + L"PACK";
                std::wstring path;
                DWORD randNum = (GetTickCount() & 0xFFF);
                while (1)
                {
                    const std::wstring suffix = FormatStrW(L"%X", randNum);
                    path = baseName + suffix + L".*";
                    WIN32_FIND_DATAW findData;
                    HANDLE find = gFileSystem->FindFirstFile(path.c_str(), &findData);
                    if (find != INVALID_HANDLE_VALUE)
                        gFileSystem->CloseFind(find); // this name already exists with some extension, searching again
                    else
                    {
                        path = baseName + suffix;
                        const wchar_t* extension = wcsrchr(data->ArcName, L'.');
                        const wchar_t* separator = wcsrchr(data->ArcName, L'\\');
                        if (extension != NULL && (separator == NULL || extension > separator))
                        {
                            int count = 4; // copy '.' plus at most 3 allowed extension characters (of the 8.3 format)
                            while (count-- && *extension != L'\0' && *extension < 128 &&
                                   *extension != L'[' && *extension != L']' &&
                                   *extension != L';' && *extension != L'=' &&
                                   *extension != L',' && *extension != L' ')
                            {
                                path += *extension++;
                            }
                        }
                        break; // we can use this name (it does not exist with any extension yet)
                    }
                    randNum++;
                }

                HANDLE h = gFileSystem->CreateFile(path.c_str(), GENERIC_WRITE, 0, NULL, CREATE_NEW,
                                                   FILE_ATTRIBUTE_NORMAL, NULL);
                const DWORD openError = GetLastError();
                HANDLES_ADD_EX(__otQuiet, h != INVALID_HANDLE_VALUE, __htFile, __hoCreateFile, h, openError, TRUE);
                if (h != INVALID_HANDLE_VALUE)
                {
                    HANDLES_REMOVE(h, __htFile, "IFileSystem::CloseHandle");
                    gFileSystem->CloseFileHandle(h);
                    const std::wstring shortPath = GetShortPathW(path.c_str());
                    gFileSystem->DeleteFile(path.c_str()); // we no longer need the probe (the archiver will create it)
                    if (shortPath.empty())
                    {
                        TRACE_E("Error (2) in GetShortPathName() in PackExpArcDosName().");
                        data->Failed = TRUE;
                        return L"";
                    }
                    *data->DOSTmpFile = shortPath;
                }
                else
                {
                    TRACE_E("Unable to create file with DOS-name in PackExpArcDosName(), error=" << openError);
                    data->Failed = TRUE;
                    return L"";
                }
            }
            else
            {
                TRACE_E("Unexpected situation in PackExpArcDosName().");
                data->Failed = TRUE;
                return L"";
            }
        }
        dosArchiveName = *data->DOSTmpFile;
    }

    data->Buffer = dosArchiveName;
    return data->Buffer;
}

std::wstring PackExpArcDosFile(void* param)
{
    SPackExpData* data = (SPackExpData*)param;
    const std::wstring fullName = PackExpArcDosName(param);
    if (data->Failed)
        return L"";

    const wchar_t* s = wcsrchr(fullName.c_str(), L'\\');
    if (s == NULL)
    {
        TRACE_E("Unexpected value in PackExpArcDosFile().");
        data->Failed = TRUE;
        return L"";
    }
    data->Buffer = s + 1;
    return data->Buffer;
}

std::wstring PackExpSrcPath(void* param)
{
    SPackExpData* data = (SPackExpData*)param;
    if (data->SrcDir == NULL)
    {
        TRACE_E("Unexpected call to PackExpSrcPath().");
        data->Failed = TRUE;
        return L"";
    }
    return data->SrcDir;
}

std::wstring PackExpTgtPath(void* param)
{
    SPackExpData* data = (SPackExpData*)param;
    if (data->TgtDir == NULL)
    {
        TRACE_E("Unexpected call to PackExpTgtPath().");
        data->Failed = TRUE;
        return L"";
    }
    return data->TgtDir;
}

std::wstring PackExpTgtDosPath(void* param)
{
    SPackExpData* data = (SPackExpData*)param;
    if (data->TgtDir == NULL)
    {
        TRACE_E("Unexpected call to PackExpTgtDosPath().");
        data->Failed = TRUE;
        return L"";
    }
    const std::wstring shortPath = GetShortPathW(data->TgtDir);
    if (shortPath.empty())
    {
        TRACE_E("Error in GetShortPathName() in PackExpTgtDosPath().");
        data->Failed = TRUE;
        return L"";
    }
    return shortPath;
}

std::wstring PackExpLstName(void* param)
{
    SPackExpData* data = (SPackExpData*)param;
    if (data->LstName == NULL)
    {
        TRACE_E("Unexpected call to PackExpLstName().");
        data->Failed = TRUE;
        return L"";
    }
    return data->LstName;
}

std::wstring PackExpLstDosName(void* param)
{
    SPackExpData* data = (SPackExpData*)param;
    if (data->LstName == NULL)
    {
        TRACE_E("Unexpected call to PackExpLstDosName().");
        data->Failed = TRUE;
        return L"";
    }
    const std::wstring shortPath = GetShortPathW(data->LstName);
    if (shortPath.empty())
    {
        TRACE_E("Error in GetShortPathName() in PackExpLstDosName().");
        data->Failed = TRUE;
        return L"";
    }
    return shortPath;
}

std::wstring PackExpExtName(void* param)
{
    SPackExpData* data = (SPackExpData*)param;
    if (data->ExtName == NULL)
    {
        TRACE_E("Unexpected call to PackExpExtName().");
        data->Failed = TRUE;
        return L"";
    }
    return data->ExtName;
}

std::wstring
PackExpExeName(unsigned int index, BOOL unpacker = FALSE)
{
    const wchar_t* exe;
    if (!unpacker)
        exe = ArchiverConfig.GetPackerExeFile(index);
    else
        exe = ArchiverConfig.GetUnpackerExeFile(index);
    // if the packer is not configured it should not be used, but
    // it's not excluded, so use the program name without the path
    if (exe == NULL)
        if (!unpacker)
            exe = ArchiverConfig.GetPackerExecutable(index);
        else
            exe = ArchiverConfig.GetUnpackerExecutable(index);
    else
    {
        // on older Windows it was impossible to redirect output from a DOS program in a directory
        // with a long name; I no longer feel like patching and risking this that it won't work
        const std::wstring shortPath = GetShortPathW(exe);
        if (!shortPath.empty())
            return shortPath;
    }
    if (exe == NULL)
    {
        TRACE_E("Missing external archiver executable in PackExpExeName().");
        return L"";
    }
    std::wstring quoted = exe;
    if (quoted.empty() || quoted.front() != L'"')
        quoted.insert(quoted.begin(), L'"');
    if (quoted.back() != L'"')
        quoted.push_back(L'"');
    std::wstring expanded;
    return ExpandCommand(NULL, quoted.c_str(), expanded, FALSE) ? expanded : quoted;
}

std::wstring PackExpJar32ExeName(void*)
{
    return PackExpExeName(PACKJAR32INDEX);
}

std::wstring PackExpJar16ExeName(void*)
{
    return PackExpExeName(PACKJAR16INDEX);
}

std::wstring PackExpRar32ExeName(void*)
{
    return PackExpExeName(PACKRAR32INDEX);
}

std::wstring PackExpRar16ExeName(void*)
{
    return PackExpExeName(PACKRAR16INDEX);
}

std::wstring PackExpArj32ExeName(void*)
{
    return PackExpExeName(PACKARJ32INDEX);
}

std::wstring PackExpArj16ExeName(void*)
{
    return PackExpExeName(PACKARJ16INDEX);
}

std::wstring PackExpLha16ExeName(void*)
{
    return PackExpExeName(PACKLHA16INDEX);
}

std::wstring PackExpUc216ExeName(void*)
{
    return PackExpExeName(PACKUC216INDEX);
}

std::wstring PackExpAce32ExeName(void*)
{
    return PackExpExeName(PACKACE32INDEX);
}

std::wstring PackExpAce16ExeName(void*)
{
    return PackExpExeName(PACKACE16INDEX);
}

std::wstring PackExpZip32ExeName(void*)
{
    return PackExpExeName(PACKZIP32INDEX);
}

std::wstring PackExpZip16ExeName(void*)
{
    return PackExpExeName(PACKZIP16INDEX);
}

std::wstring PackExpUzp16ExeName(void*)
{
    return PackExpExeName(PACKZIP16INDEX, TRUE);
}

//
// ****************************************************************************
// Constants
// ****************************************************************************
//

// ****************************************************************************
// tables assigning individual evaluation functions to specific variables
//

// Core packer expansion has no plugin ABI consumer, so it uses the native-wide table.
sally::unicode::WideVarEntry PackCmdLineExpArrayW[] =
    {
        {PACK_ARC_PATH, PackExpArcPath},
        {PACK_ARC_FILE, PackExpArcFile},
        {PACK_ARC_DOSFILE, PackExpArcDosFile},
        {PACK_ARC_NAME, PackExpArcName},
        {PACK_ARC_DOSNAME, PackExpArcDosName},
        {PACK_TGT_PATH, PackExpTgtPath},
        {PACK_TGT_DOSPATH, PackExpTgtDosPath},
        {PACK_LST_NAME, PackExpLstName},
        {PACK_LST_DOSNAME, PackExpLstDosName},
        {PACK_EXT_NAME, PackExpExtName},
        {PACK_EXE_JAR32, PackExpJar32ExeName},
        {PACK_EXE_JAR16, PackExpJar16ExeName},
        {PACK_EXE_RAR32, PackExpRar32ExeName},
        {PACK_EXE_RAR16, PackExpRar16ExeName},
        {PACK_EXE_ARJ32, PackExpArj32ExeName},
        {PACK_EXE_ARJ16, PackExpArj16ExeName},
        {PACK_EXE_LHA16, PackExpLha16ExeName},
        {PACK_EXE_UC216, PackExpUc216ExeName},
        {PACK_EXE_ACE32, PackExpAce32ExeName},
        {PACK_EXE_ACE16, PackExpAce16ExeName},
        {PACK_EXE_ZIP32, PackExpZip32ExeName},
        {PACK_EXE_ZIP16, PackExpZip16ExeName},
        {PACK_EXE_UZP16, PackExpUzp16ExeName},
        // sentinel
        {NULL, NULL}};

sally::unicode::WideVarEntry PackInitDirExpArrayW[] =
    {
        {PACK_ARC_PATH, PackExpArcPath},
        {PACK_SRC_PATH, PackExpSrcPath},
        {PACK_TGT_PATH, PackExpTgtPath},
        {PACK_TGT_DOSPATH, PackExpTgtDosPath},
        {NULL, NULL}};

//
// ****************************************************************************
// Functions
// ****************************************************************************
//

//
// ****************************************************************************
// Functions for variable expansion
//

//
// ****************************************************************************
//   Expands variables in the command line
//
//   RET:  TRUE on success, FALSE on error
//   IN:   archiveName is the name of the archive we work with
//         tgtDir is the target directory name for the operation or NULL
//         lstName is the name of the file listing processed items or NULL
//         extName is the name of the extracted file or NULL
//         varText is the command line with variables
//         DOSTmpName is NULL if a long name cannot be replaced by a substitute DOS name,
//   OUT:  buffer is the command line with variables expanded
//         DOSTmpName is the name of the temporary file (or an empty string if no replacement was made)

BOOL PackExpandCmdLine(const wchar_t* archiveName, const wchar_t* tgtDir, const wchar_t* lstName,
                       const wchar_t* extName, const wchar_t* varText, std::wstring& output,
                       std::wstring* DOSTmpName)
{
    CALL_STACK_MESSAGE6("PackExpandCmdLine(%ls, %ls, %ls, %ls, %ls, ,)",
                        archiveName, tgtDir, lstName, extName, varText);

    SPackExpData data;
    data.ArcName = archiveName;
    data.SrcDir = NULL;
    data.TgtDir = tgtDir;
    data.LstName = lstName;
    data.ExtName = extName;
    data.ArcNameFilePossible = TRUE;
    data.DOSTmpFilePossible = DOSTmpName != NULL;
    if (DOSTmpName != NULL)
        DOSTmpName->clear();
    data.DOSTmpFile = DOSTmpName;
    data.Failed = FALSE;
    const BOOL ret = ExpandWideVarStringW(MainWindow->HWindow, varText, output,
                                          PackCmdLineExpArrayW, &data, FALSE);
    return ret && !data.Failed;
}

//
// ****************************************************************************
//   Expands variables in the string specifying the current directory for the launched program
//
//   RET:  TRUE on success, FALSE on error
//   IN:   archiveName is the archive name we work with
//         srcDir is the source directory for the operation or NULL
//         tgtDir is the target directory for the operation or NULL
//         varText is the command line with variables
//   OUT:  buffer is the command line with variables expanded

BOOL PackExpandInitDir(const wchar_t* archiveName, const wchar_t* srcDir, const wchar_t* tgtDir,
                       const wchar_t* varText, std::wstring& output)
{
    CALL_STACK_MESSAGE5("PackExpandInitDir(%ls, %ls, %ls, %ls, ,)",
                        archiveName, srcDir, tgtDir, varText);

    SPackExpData data;
    data.ArcName = archiveName;
    data.SrcDir = srcDir;
    data.TgtDir = tgtDir;
    data.LstName = NULL;
    data.ExtName = NULL;
    data.ArcNameFilePossible = TRUE;
    data.DOSTmpFilePossible = FALSE;
    data.DOSTmpFile = NULL;
    data.Failed = FALSE;
    const BOOL ret = ExpandWideVarStringW(MainWindow->HWindow, varText, output,
                                          PackInitDirExpArrayW, &data, FALSE);
    return ret && !data.Failed;
}

//
// ****************************************************************************
// General functions
//

//
// ****************************************************************************
// BOOL EmptyErrorHandler(HWND parent, const WORD err, ...)
//
//   Empty error function - to handle errors correctly, replace this function
//   in PackErrorHandlerPtr pointer with your own function that processes the error as needed.
//   It is used not only to report errors that occurred (IDS_PACKERR_*) but also
//   to resolve unexpected situations by asking the user (IDS_PACKQRY_*).
//
//   RET:  TRUE to continue, FALSE to abort
//   IN:   parent is the parent window of message boxes
//         err is the error number that occurred
//         remaining parameters further specify the error depending on its code

BOOL EmptyErrorHandler(HWND parent, const WORD err, ...)
{
    TRACE_E("Pack Empty Error Handler: error code " << err);
    return FALSE;
}

//
// ****************************************************************************
// void PackSetErrorHandler(BOOL (*handler)(HWND parent, const WORD errNum, ...))
//
//   Sets the error handling function
//
//   RET:
//   IN:   handler is the new function for error processing

void PackSetErrorHandler(BOOL (*handler)(HWND parent, const WORD errNum, ...))
{
    if (handler == NULL)
        PackErrorHandlerPtr = EmptyErrorHandler;
    else
        PackErrorHandlerPtr = handler;
}

//
// ****************************************************************************
// BOOL PackExecute(HWND parent, const std::wstring &cmdLine, const std::wstring &currentDir,
//                  TPackErrorTable *const errorTable)
//
//   Runs the external program given (including parameters) in cmdLine string
//
//   RET: returns TRUE on success, FALSE on error
//        on error the callback *PackErrorHandlerPtr is called
//   IN:  parent is the parent window for message boxes
//        cmdLine is the command line to execute
//        currentDir is the full current directory for the launched program, or empty if it doesn't matter
//        errorTable is a pointer to the return code table (if NULL, no table)

BOOL PackExecute(HWND parent, const std::wstring& cmdLine, const std::wstring& currentDir,
                 TPackErrorTable* const errorTable)
{
    CALL_STACK_MESSAGE3("PackExecute(, %ls, %ls, ,)", cmdLine.c_str(), currentDir.c_str());

    // if we haven't determined the path to the spawn yet, do it now
    if (!InitSpawnName(parent))
        return FALSE;

    // set everything needed to create the process
    ExternalToolRequest request;
    request.inheritHandles = true;
    request.creationFlags = CREATE_DEFAULT_ERROR_MODE | NORMAL_PRIORITY_CLASS;
    if (PackWinTimeout != 0)
    {
        request.useShowWindow = true;
        request.showWindow = SW_MINIMIZE;
        POINT p;
        if (MultiMonGetDefaultWindowPos(MainWindow->HWindow, &p))
        {
            // if the main window is on another monitor we should open the new window there
            // preferably at the default position (as on the primary monitor)
            request.usePosition = true;
            request.x = p.x;
            request.y = p.y;
        }
    }

    // Determine what we are actually running (for error reporting)
    const wchar_t* commandBegin = cmdLine.c_str();
    // skip leading whitespace
    while (*commandBegin != L'\0' && (*commandBegin == L' ' || *commandBegin == L'\t'))
        commandBegin++;
    // read the program name
    const wchar_t* commandEnd = commandBegin;
    if (*commandBegin == L'"')
    {
        commandBegin++;
        commandEnd = commandBegin;
        while (*commandEnd != L'\0' && *commandEnd != L'"')
            commandEnd++;
    }
    else
    {
        while (*commandEnd != L'\0' && *commandEnd != L' ' && *commandEnd != L'\t' && *commandEnd != L'"')
            commandEnd++;
    }
    const std::wstring command(commandBegin, commandEnd);

    std::wstring spawnCommand = L"\"";
    spawnCommand += SpawnExe;
    spawnCommand += L"\" ";
    spawnCommand += SPAWN_EXE_PARAMS;
    spawnCommand += L" ";
    spawnCommand += cmdLine;
    // launch the external program
    request.commandLine = spawnCommand;
    request.workingDirectory = currentDir;

    ExternalToolResult launchResult = gExternalToolRunner != NULL
                                          ? gExternalToolRunner->Launch(request)
                                          : ExternalToolResult::Error(ERROR_INVALID_PARAMETER);
    if (!launchResult.success)
    {
        DWORD err = launchResult.errorCode;
        const std::string errorText = PackSystemErrorPresentation(err);
        return (*PackErrorHandlerPtr)(parent, IDS_PACKERR_PROCESS,
                                      PackErrorPresentation(SpawnExe.c_str()).c_str(), errorText.c_str());
    }
    HANDLE processHandle = launchResult.DetachNativeProcessHandle();
    if (processHandle == NULL)
    {
        DWORD err = GetLastError();
        launchResult.CloseProcess();
        const std::string errorText = PackSystemErrorPresentation(err);
        return (*PackErrorHandlerPtr)(parent, IDS_PACKERR_PROCESS,
                                      PackErrorPresentation(SpawnExe.c_str()).c_str(), errorText.c_str());
    }
    HANDLES_ADD(__htProcess, __hoCreateProcess, processHandle);
    DWORD processId = launchResult.processId;

    // create a modal window
    HWND hFocusedWnd = GetFocus();
    HWND main = parent == NULL ? MainWindow->HWindow : parent;
    CExecuteWindow tmpWindow(main, IDS_PACK_EXECUTING, ooStatic);
    tmpWindow.Create();
    HWND oldPluginMsgBoxParent = PluginMsgBoxParent;
    // plugin timers may be invoked (happens with WinSCP open in the other panel) -> set parent for message boxes
    PluginMsgBoxParent = tmpWindow.HWindow;
    EnableWindow(main, FALSE);
    // activate the hourglass cursor
    // IDC_WAIT is already MAKEINTRESOURCE(32514) (no UNICODE #define in this build,
    // so that resolves through MAKEINTRESOURCEA) - re-wrapping it in MAKEINTRESOURCEW truncates
    // that already-encoded pointer through (WORD) again (C4302); a plain pointer-type
    // reinterpretation carries the same encoded value without truncating anything.
    HCURSOR prevCrsr = SetCursor(LoadCursorW(NULL, (LPCWSTR)IDC_WAIT));
    // Wait for the external program to finish
    HANDLE objects[] = {processHandle};
    DWORD start = GetTickCount();
    DWORD elapsed = 0;

    DWORD ret;
    do
    {
        /*  // Petr: pumping only WM_PAINT leads to blocking all other instances of Salamander
    //       (even newly started ones) and other softwares (at least during Paste), if we
    //       put a file or directory on the clipboard before packing. Accessing clipboard
    //       data causes OLE to communicate with this process which doesn't respond
    //       because it pumps only WM_PAINT.
    // Original Tom's variant:
    ret = MsgWaitForMultipleObjects(1, objects, FALSE,
                                    PackWinTimeout <= 0 ? INFINITE : PackWinTimeout - elapsed,
                                    QS_PAINT);
*/
        ret = MsgWaitForMultipleObjects(1, objects, FALSE,
                                        PackWinTimeout <= 0 ? INFINITE : PackWinTimeout - elapsed,
                                        QS_ALLINPUT);
        if (ret == WAIT_OBJECT_0 + 1)
        {
            // if a message arrived, handle it
            MSG msg;
            /*    // Original Tom's variant: (see description above)
      while (PeekMessageW(&msg, NULL, WM_PAINT, WM_PAINT, PM_REMOVE))
        DispatchMessageW(&msg);
*/
            while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE))
            {
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
            // if the timeout has expired, stop
            elapsed = GetTickCount() - start;
            if (PackWinTimeout > 0 && (int)elapsed >= PackWinTimeout)
            {
                ret = WAIT_TIMEOUT;
                break;
            }
        }
    } while (ret == WAIT_OBJECT_0 + 1); // wait while WM_PAINT messages arrive

    if (ret == WAIT_TIMEOUT)
    {
        HWND win = NULL;
        DWORD pid;
        do
        {
            win = FindWindowExW(NULL, win, L"ConsoleWindowClass", NULL);
            GetWindowThreadProcessId(win, &pid);
            if (pid == processId)
            {
                ShowWindow(win, SW_RESTORE);
                break;
            }
        } while (win != NULL);
        do
        {
            /*    // Original Tom's variant: (see description above)
      ret = MsgWaitForMultipleObjects(1, objects, FALSE, INFINITE, QS_PAINT);
*/
            ret = MsgWaitForMultipleObjects(1, objects, FALSE, INFINITE, QS_ALLINPUT);
            MSG msg;
            if (ret == WAIT_OBJECT_0 + 1)
            {
                /*      // Original Tom's variant: (see description above)
        while (PeekMessageW(&msg, NULL, WM_PAINT, WM_PAINT, PM_REMOVE))
          DispatchMessageW(&msg);
*/
                while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE))
                {
                    TranslateMessage(&msg);
                    DispatchMessageW(&msg);
                }
            }
        } while (ret == WAIT_OBJECT_0 + 1); // wait while WM_PAINT messages arrive
    }

    EnableWindow(main, TRUE);
    PluginMsgBoxParent = oldPluginMsgBoxParent;
    DestroyWindow(tmpWindow.HWindow);
    // if Salamander is active, call SetFocus on the stored window (SetFocus does
    // not work when the main window is disabled - after deactivation/activation
    // of the disabled main window, the active panel has no focus)
    HWND hwnd = GetForegroundWindow();
    while (hwnd != NULL && hwnd != main)
        hwnd = GetParent(hwnd);
    if (hwnd == main)
        SetFocus(hFocusedWnd);
    // remove the hourglass cursor
    SetCursor(prevCrsr);
    UpdateWindow(main);

    if (ret == WAIT_FAILED)
    {
        const std::string message = "WaitForSingleObject: " + PackSystemErrorPresentation(GetLastError());
        HANDLES(CloseHandle(processHandle));
        return (*PackErrorHandlerPtr)(parent, IDS_PACKERR_GENERAL, message.c_str());
    }

    // and find out how it ended - hopefully they all return 0 as success
    DWORD exitCode;
    if (!GetExitCodeProcess(processHandle, &exitCode))
    {
        const std::string message = "GetExitCodeProcess: " + PackSystemErrorPresentation(GetLastError());
        HANDLES(CloseHandle(processHandle));
        return (*PackErrorHandlerPtr)(parent, IDS_PACKERR_GENERAL, message.c_str());
    }

    // release handles of the process
    HANDLES(CloseHandle(processHandle));

    if (exitCode != 0)
    {
        //
        // First handle salspawn.exe errors if we used it
        //
        if (exitCode >= SPAWN_ERR_BASE)
        {
            // salspawn.exe error - bad parameters or similar
            if (exitCode >= SPAWN_ERR_BASE && exitCode < SPAWN_ERR_BASE * 2)
                return (*PackErrorHandlerPtr)(parent, IDS_PACKERR_RETURN,
                                              PackErrorPresentation(SPAWN_EXE_NAME).c_str(), PackErrorPresentation(LoadStrOwned(IDS_PACKRET_SPAWN).c_str()).c_str());
            // CreateProcess error
            if (exitCode >= SPAWN_ERR_BASE * 2 && exitCode < SPAWN_ERR_BASE * 3)
            {
                const std::string errorText = PackSystemErrorPresentation(exitCode - SPAWN_ERR_BASE * 2);
                return (*PackErrorHandlerPtr)(parent, IDS_PACKERR_PROCESS,
                                              PackErrorPresentation(command.c_str()).c_str(), errorText.c_str());
            }
            // WaitForSingleObject error
            if (exitCode >= SPAWN_ERR_BASE * 3 && exitCode < SPAWN_ERR_BASE * 4)
            {
                const std::string message = "WaitForSingleObject: " + PackSystemErrorPresentation(exitCode - SPAWN_ERR_BASE * 3);
                return (*PackErrorHandlerPtr)(parent, IDS_PACKERR_GENERAL, message.c_str());
            }
            // GetExitCodeProcess error
            if (exitCode >= SPAWN_ERR_BASE * 4)
            {
                const std::string message = "GetExitCodeProcess: " + PackSystemErrorPresentation(exitCode - SPAWN_ERR_BASE * 4);
                return (*PackErrorHandlerPtr)(parent, IDS_PACKERR_GENERAL, message.c_str());
            }
        }
        //
        // now come the external program errors
        //
        // if errorTable == NULL, no translation is done (table doesn't exist)
        if (!errorTable)
        {
            const std::string errorText = FormatPackNumericError(IDS_PACKRET_GENERAL, exitCode);
            return (*PackErrorHandlerPtr)(parent, IDS_PACKERR_RETURN,
                                          PackErrorPresentation(command.c_str()).c_str(), errorText.c_str());
        }
        // find the corresponding text in the table
        int errorIndex;
        for (errorIndex = 0; (*errorTable)[errorIndex][0] != -1 &&
                             (*errorTable)[errorIndex][0] != (int)exitCode;
             errorIndex++)
            ;
        // was it found?
        if ((*errorTable)[errorIndex][0] == -1)
            return (*PackErrorHandlerPtr)(parent, IDS_PACKERR_RETURN,
                                          PackErrorPresentation(command.c_str()).c_str(), PackErrorPresentation(LoadStrOwned(IDS_PACKRET_UNKNOWN).c_str()).c_str());
        else
            return (*PackErrorHandlerPtr)(parent, IDS_PACKERR_RETURN,
                                          PackErrorPresentation(command.c_str()).c_str(), PackErrorPresentation(LoadStrOwned((*errorTable)[errorIndex][1]).c_str()).c_str());
    }
    return TRUE;
}

//****************************************************************************
//
// CExecuteWindow
//

CExecuteWindow::CExecuteWindow(HWND hParent, int textResID, CObjectOrigin origin)
    : CWindow(origin)
{
    CALL_STACK_MESSAGE2("CExecuteWindow::CExecuteWindow(, %d, )", textResID);
    HParent = hParent;
    const wchar_t* t = LoadStrW(textResID);
    int len = (int)wcslen(t);
    Text = new WCHAR[len + 1];
    if (Text == NULL)
        TRACE_E(LOW_MEMORY);
    else
        wcscpy_s(Text, len + 1, t);
}

CExecuteWindow::~CExecuteWindow()
{
    CALL_STACK_MESSAGE1("CExecuteWindow::~CExecuteWindow()");
    if (Text != NULL)
        delete[] Text;
}

#define EXECUTEWINDOW_HMARGIN 25
#define EXECUTEWINDOW_VMARGIN 18

HWND CExecuteWindow::Create()
{
    CALL_STACK_MESSAGE1("CExecuteWindow::Create()");
    // compute text size => window size
    SIZE s;
    s.cx = 300;
    s.cy = 30;
    HDC dc = HANDLES(GetDC(NULL));
    if (dc != NULL)
    {
        HFONT old = (HFONT)SelectObject(dc, EnvFont);
        GetTextExtentPoint32W(dc, Text, (int)wcslen(Text), &s);
        SelectObject(dc, old);
        HANDLES(ReleaseDC(NULL, dc));
    }

    int width = s.cx + 2 * EXECUTEWINDOW_HMARGIN;
    int height = s.cy + 2 * EXECUTEWINDOW_VMARGIN;
    int x;
    int y;

    RECT r2;
    GetWindowRect(MainWindow->HWindow, &r2);
    x = (r2.right + r2.left - width) / 2;
    GetWindowRect(MainWindow->LeftPanel->HWindow, &r2);
    y = (r2.bottom + r2.top - height) / 2;

    CreateEx(WS_EX_DLGMODALFRAME,
             SAVEBITS_CLASSNAME,
             L"",
             WS_BORDER | WS_POPUP,
             x, y, width, height,
             HParent,
             NULL,
             HInstance,
             this);

    ShowWindow(HWindow, SW_SHOWNA);
    return HWindow;
}

LRESULT
CExecuteWindow::WindowProc(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
    case WM_ERASEBKGND:
    {
        LRESULT ret = CWindow::WindowProc(uMsg, wParam, lParam);
        HDC dc = (HDC)wParam;
        RECT r;
        GetClientRect(HWindow, &r);
        if (Text != NULL)
        {
            HFONT hOldFont = (HFONT)SelectObject(dc, EnvFont);
            int prevBkMode = SetBkMode(dc, TRANSPARENT);
            SetTextColor(dc, GetSysColor(COLOR_BTNTEXT));
            DrawTextW(dc, Text, (int)wcslen(Text), &r, DT_CENTER | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
            SetBkMode(dc, prevBkMode);
            SelectObject(dc, hOldFont);
        }
        return ret;
    }
    case WM_SETCURSOR:
    {
        LRESULT ret = CWindow::WindowProc(uMsg, wParam, lParam);
        SetCursor(LoadCursor(NULL, IDC_WAIT));
        return TRUE;
    }
    }
    return CWindow::WindowProc(uMsg, wParam, lParam);
}
