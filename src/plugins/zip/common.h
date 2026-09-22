// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "spl_base.h"
#include "spl_com.h"
#include "spl_crypt.h"
#include "spl_bzip2.h"

#include "array2.h"

#include "typecons.h"
#include "chicon.h"

extern CSalamanderGeneralAbstract* SalamanderGeneral;
extern CSalamanderSafeFileAbstract* SalamanderSafeFile;
extern CSalamanderCryptAbstract* SalamanderCrypt;
extern CSalamanderBZIP2Abstract* SalamanderBZIP2;

// interface providing customized Windows controls used in Salamander
extern CSalamanderGUIAbstract* SalamanderGUI;

#define SizeOf(x) (sizeof(x) / sizeof(x[0]))

#define INIT_CRC 0L

// used in CFileData.PluginData
class CZIPFileData
{
public:
    CZIPFileData(QWORD qwPackedSize, int nItem, BOOL bUnix) : PackedSize(qwPackedSize), ItemNumber(nItem), Unix(bUnix) {}

    QWORD PackedSize;
    int ItemNumber; // # of item in Cetral Directory, not offset
    BOOL Unix;
};

// Name is wide - the selected-file name from the panel (SalEnumSelection's own
// wide names), stored losslessly. Was LPTSTR (narrow): EnumFiles used to CP_ACP-narrow every
// name before constructing one of these, and REFUSED (aborted the whole batch operation) the
// moment any single selected file's name couldn't survive that round trip - see EnumFiles'
// own comment in common.cpp. Widening removes the round trip entirely, so the refusal no
// longer applies to representable-but-non-ANSI names.
struct CExtInfo
{
    wchar_t* Name;
    int ItemNumber; // # of item in Cetral Directory, not offset
    bool IsDir;

    CExtInfo(const wchar_t* pName, bool isDir, int nItem);
    ~CExtInfo();
};

// Present config version (Config.Version)
#define CURRENT_CONFIG_VERSION 3

#define CLR_ASK 0
#define CLR_REPLACE 1
#define CLR_PRESERVE 2

#define EM_ZIP20 0
#define EM_AES128 1
#define EM_AES256 2

struct CConfiguration
{
    int Level;                         // Compression level
    int EncryptMethod;                 // EM_ZIP20/EM_AES128/EM_AES256
    bool NoEmptyDirs;                  //don't add empty directories to zip
    bool BackupZip;                    //create temporary backup of zip before
                                       //any modification of it
    bool ShowExOptions;                //display exteded pack options dialog
    bool TimeToNewestFile;             //set zip file time to the newest file time
    char VolSizeCache[5][MAX_VOL_STR]; //volume sizes
    unsigned VolSizeUnits[5];          //MB if nozero
                                       //KB if zero
    bool LastUsedAuto;                 //automatic volume size last used
    bool AutoExpandMV;                 //automatically expand multi-volume archives
                                       //on non-removable disks
    int Version;                       //config version (0 - default; 1 - beta 3; 2 - beta 4)
    std::wstring DefSfxFile;           // default SFX package basename
    std::wstring LastExportPath;       // default path to export SFX settings to
    int CurSalamanderVersion;          //current version of Altap Salamnder
    int ChangeLangReaction;            //viz CLR_xxx
    BOOL WinZipNames;                  // winzip compatible multi-volume archive names

    // Custom columns:
    BOOL ListInfoPackedSize;          // Show custom column Packed Size
    DWORD ColumnPackedSizeWidth;      // LO/HI-WORD: left/right panel: Width for Packed Size column
    DWORD ColumnPackedSizeFixedWidth; // LO/HI-WORD: left/right panel: FixedWidth for Packed Size column
};

//pack action
#define PA_NORMAL 0      //just add files to an archive
#define PA_SELFEXTRACT 1 //create self extracting archive
#define PA_MULTIVOL 2    //create multi-volume archive

//#define MAX_PACKED_SFXSETTINGS (sizeof(CSfxSettings) + 10 * sizeof(DWORD))

struct CExtendedOptions
{
    char Action;
    __INT64 VolumeSize; // -1 means automatic size
    bool SeqNames;
    bool Encrypt;
    char Password[MAX_PASSWORD];
    //for self extractor
    CSfxSettings SfxSettings;
    char About[SE_MAX_ABOUT];
    CIcon* Icons;
    int IconsCount;

    CExtendedOptions()
    {
        Action = PA_NORMAL;
        VolumeSize = 0;
        SeqNames = false;
        Encrypt = false;
        *Password = 0;
        *About = 0;
        Icons = NULL;
        IconsCount = 0;
    }
    CExtendedOptions& operator=(const CExtendedOptions& origin)
    {
        Action = origin.Action;
        VolumeSize = origin.VolumeSize;
        SeqNames = origin.SeqNames;
        Encrypt = origin.Encrypt;
        strcpy(Password, origin.Password);
        SfxSettings = origin.SfxSettings;
        strcpy(About, origin.About);
        Icons = origin.Icons;
        IconsCount = origin.IconsCount;
        return *this;
    }
};

#define MAX_FAVNAME 50

struct CFavoriteSfx
{
    char Name[MAX_FAVNAME];
    CSfxSettings Settings;
};

struct CFile
{
    HANDLE File;
    QWORD Size;
    QWORD FilePointer;
    QWORD RealFilePointer;
    std::wstring FileName;
    int Flags;
    char* OutputBuffer;
    unsigned BufferPosition;
    char* InputBuffer;
    // unsigned  InputPosition;
    unsigned BigFile : 1; // the file can be larger than 4GB

    CFile()
        : File(INVALID_HANDLE_VALUE), Size(0), FilePointer(0), RealFilePointer(0), Flags(0),
          OutputBuffer(NULL), BufferPosition(0), InputBuffer(NULL), BigFile(0)
    {
    }
};

class CZipCommon
{
public:
    CFile* ZipFile;             //zip file hanndle
    std::wstring ZipName; // local archive path
    const char* ZipRoot;
    int RootLen; //length of ZipRoot
    bool ZeroZip;
    QWORD EOCentrDirOffs;           //offset of end of central directory record
    QWORD Zip64EOCentrDirOffs;      //offset of zip 64 end of central directory record
    CEOCentrDirRecordEx EOCentrDir; //end of central directory record
    QWORD CentrDirSize;
    QWORD CentrDirOffs;
    int CentrDirStartDisk; //jen pro list a extract
                           // number of the disk with the start of the central directory
    bool Zip64;
    QWORD ExtraBytes;
    CSalamanderForOperationsAbstract* Salamander;
    int ErrorID;
    CQuadWord MatchedTotalSize; //total uncopressed size of all files
                                //that are about to be extracted
    CQuadWord ProgressTotalSize;
    bool Fatal;
    bool UserBreak;
    bool Extract;
    //bool                MenuSfx;//this is set when creating self extracting archive from menu
    char* Comment;

    //multi-volume archives
    bool Removable;
    bool MultiVol;
    int DiskNum;
    unsigned CHDiskFlags;
    CConfiguration Config;

    BOOL Unix; // at least one file has the HS_UNIX flag

    // AES Encryption
    CSalAES AESContext;
    BOOL AESContextValid; // is it necessary to call fcrypt_end?

    std::vector<std::wstring>* ArchiveVolumes;

    CZipCommon(const wchar_t* zipName, const char* zipRoot,
               CSalamanderForOperationsAbstract* salamander,
               std::vector<std::wstring>* archiveVolumes);

    ~CZipCommon();

    int CheckZip();

    //file IO
    int Read(CFile* file, void* buffer, unsigned bytesToRead,
             unsigned* bytesRead, bool* skipAll);
    int Write(CFile* file, const void* buffer, unsigned bytesToWrite, bool* skipAll);
    int Flush(CFile* file, const void* buffer, unsigned bytesToWrite, bool* skipAll);
    // if 'useReadCache' is TRUE, InputBuffer is allocated inside 'file'
    int CreateCFile(CFile** file, const wchar_t* fileName, unsigned int access,
                    unsigned int share, unsigned int creation, unsigned int attributes,
                    int flags, bool* skipAll, bool bigFile, bool useReadCache);

    int CloseCFile(CFile* file);

    int ProcessError(int errorID, int lastError, const wchar_t* fileName,
                     int flags, bool* skipAll, const wchar_t* extText = NULL);

    //zip processing functions
    int CheckZipFormat();
    int FindEOCentrDirSig(BOOL* success);
    int FindZip64EOCentrDirLocator();
    int CheckForExtraBytes();
    int ReadCentralHeader(CFileHeader* fileHeader, LPQWORD offset, unsigned int* size);
    void ProcessHeader(CFileHeader* fileHeader, CFileInfo* fileInfo);
    bool IsDirByHeader(CFileHeader* fileHeader);
    int ProcessName(CFileHeader* fileHeader, char* outputName);
    // Wide-primary twin of ProcessName - see common.cpp for why this exists (the UTF-8
    // entry-name mangling bug) and its scope (display only, via CZipList::List).
    int ProcessNameW(CFileHeader* fileHeader, wchar_t* outputName);
    int ReadLocalHeader(CLocalFileHeader* fileHeader, QWORD offset);
    void ProcessLocalHeader(CLocalFileHeader* fileHeader,
                            CFileInfo* fileInfo, CAESExtraField* aesExtraField);
    void SplitPath(char** path, char** name, const char* pathToSplit);
    void QuickSortHeaders(int left, int right, TIndirectArray2<CFileInfo>& headers);
    int EnumFiles(TIndirectArray2<CExtInfo>& namesArray, int& dirs, SalEnumSelection next, void* param);
    int MatchFiles(TIndirectArray2<CFileInfo>& files, TIndirectArray2<CExtInfo>& namesArray, int dirs, char* centrDir);
    /*HWND GetParent()
    {
      HWND parent = Salamander->ProgressGetHWND();
      return parent ? parent : SalamanderGeneral->GetMainWindowHWND();
    }*/
    void DetectRemovable();
    int TestIfExist(const wchar_t* name);

#ifndef SSZIP
    int ChangeDisk();
    std::wstring FindLastFile();
#else  //SSZIP
    int ChangeDisk()
    {
        TRACE_E("ChangeDisk() - dummy, tato funkce by nemnela byt nikdy volana");
        return -1;
    }
    std::wstring FindLastFile()
    {
        TRACE_E("FindLastFile() - dummy, tato funkce by nemnela byt nikdy volana");
        return {};
    }
#endif //SSZIP
};

struct CSfxLang
{
    std::string FileName; // encoded SFX package basename
    DWORD LangID;
    char* DlgTitle;
    char* DlgText;
    char* AboutLicenced;
    char* ButtonText;
    char* Vendor;
    char* WWW;

    CSfxLang() { DlgTitle = NULL; }
    ~CSfxLang()
    {
        if (DlgTitle)
            free(DlgTitle);
    }
};

extern const CExtendedOptions DefOptions;

extern HINSTANCE DLLInstance; // handle of the SPL - language-independent resources
extern HINSTANCE HLanguage;   // handle of the SLG - language-dependent resources

extern const CConfiguration DefConfig;
extern CConfiguration Config;

std::wstring LangStr(int resID);

// Best-effort wide->narrow bridge for this plugin's explicit byte boundaries.
//
// (1) The SFX script that WriteSFXComment builds: those bytes are written to a file with
//     Write(outFile, buf, strlen(buf)) and then parsed by the SFX stub, which reads bytes.
// (2) The ZIP archive comment and a handful of ZIP-format fields, which the format itself
//     defines as bytes.
// These are file FORMATS whose width is not ours to choose. Never use this helper for OS paths or
// ordinary UI presentation.
inline std::string ZipLegacyFormatBytes(const wchar_t* wide)
{
    if (wide == NULL)
        return std::string();
    std::string out;
    EncodeZipLegacyTextExact(wide, out);
    return out;
}
std::wstring LoadStrW(int resID);

int InflateBuffer(char* sour, int sourSize, char* dest, int* destSize);
int LoadSfxFileData(const wchar_t* fileName, CSfxLang** lang);
std::wstring GetInfo(FILETIME* lastWrite, QWORD size);
//bool HasExtension(const char * filename, const char * extension);
bool Atod(const char* string, char* decSep, double* val);
DWORD ExpandSfxSettings(CSfxSettings* settings, void* buffer, DWORD size);
DWORD PackSfxSettings(CSfxSettings* settings, char*& buffer, DWORD& size);
std::wstring FormatZipErrorMessage(int errorID, int lastError);
char* StrNChr(const char* lpStart, int nChar, char wMatch);
char* StrRChr(const char* lpStart, const char* lpEnd, char wMatch);
char* TrimTralingSpaces(char* lpString);
//***********************************************************************************
//
// Routines from SHLWAPI.DLL
//

//BOOL PathAppend(LPTSTR pPath, LPCTSTR pMore);
//BOOL PathAddExtension(LPTSTR pszPath, LPCTSTR pszExtension);
//BOOL PathRenameExtension(LPTSTR pszPath, LPCTSTR pszExt);
//LPTSTR PathFindFileName(LPCTSTR pPath);
//void PathRemoveExtension(LPTSTR pszPath);
//BOOL PathRemoveFileSpec(LPTSTR pszPath);
//LPTSTR PathAddBackslash(LPTSTR pszPath);
//LPTSTR PathStripPath(LPTSTR pszPath);
