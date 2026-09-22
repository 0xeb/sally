// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "unrardll.h"

/* winnt.h
#define FILE_ATTRIBUTE_READONLY             0x00000001
#define FILE_ATTRIBUTE_HIDDEN               0x00000002
#define FILE_ATTRIBUTE_SYSTEM               0x00000004
#define FILE_ATTRIBUTE_DIRECTORY            0x00000010
#define FILE_ATTRIBUTE_ARCHIVE              0x00000020
*/

#define FILE_ATTRIBUTE_MASK (0x37)

// silent flags
#define SF_DATA 0x00010000
#define SF_LONGNAMES 0x00020000
#define SF_IOERRORS 0x00040000
#define SF_ENCRYPTED 0x00080000
#define SF_ALLENRYPT 0x00100000
#define SF_PASSWD 0x00200000

// archive flags
#define AF_VOLUME 0x0001            // Volume attribute (archive volume)
#define AF_COMMENT 0x0002           // Archive comment present
#define AF_LOCK 0x0004              // Archive lock attribute
#define AF_SOLID 0x0008             // Solid attribute (solid archive)
#define AF_NEWNAMES 0x0010          // New volume naming scheme ('volname.partN.rar')
#define AF_AUTHENTICITY_INFO 0x0020 // Authenticity information present
#define AF_RECOVERY 0x0040          // Recovery record present
#define AF_BLOCK 0x0080             // Block headers are encrypted
#define AF_FIRST_VOLUME 0x0100      // First volume (set only by RAR 3.0 and later)

#define MAX_PASSWORD 256

#ifndef QWORD
#define QWORD unsigned __int64
#endif

struct CFileHeader
{
    char FileName[260];
    WCHAR FileNameW[1024];
    CQuadWord Size;
    CQuadWord CompSize;
    FILETIME Time;
    DWORD Attr;
    DWORD Flags;
};

// general Salamander interface - valid from startup until the plugin ends
extern CSalamanderGeneralAbstract* SalamanderGeneral;

// used in CFileData.PluginData
class CRARFileData
{
public:
    CRARFileData(QWORD qwPackedSize, int nItem, const WCHAR* fileNameW = NULL);
    ~CRARFileData();

    QWORD PackedSize;
    int ItemNumber; // # of item in the archive, not offset
    WCHAR* FileNameW;
};

struct CRARExtractInfo
{
    int ItemNumber; // # of item in the archive, not offset
    std::wstring FileNameW;
};

// ****************************************************************************
//
// CPluginDataInterface
//

#define NUM_PASSWORDS 8

class CPluginDataInterface : public CPluginDataInterfaceAbstract
{
public:
    wchar_t* FirstArchiveVolume;
    DWORD Silent;
    unsigned int SolidEncrypted;
    wchar_t Password[MAX_PASSWORD];
    BOOL PasswordForOpenArchive;

    CPluginDataInterface(wchar_t* firstArchiveVolume = NULL)
    {
        FirstArchiveVolume = firstArchiveVolume;
        Silent = SolidEncrypted = 0;
        Password[0] = 0;
        PasswordForOpenArchive = FALSE;
    }
    virtual ~CPluginDataInterface()
    {
        if (FirstArchiveVolume)
            free(FirstArchiveVolume);
        memset(Password, 0, sizeof(Password));
    }
    const wchar_t* GetFirstVolume() { return FirstArchiveVolume; }

    virtual BOOL WINAPI CallReleaseForFiles() { return TRUE; }
    virtual BOOL WINAPI CallReleaseForDirs() { return TRUE; }
    virtual void WINAPI ReleasePluginData(CFileData& file, BOOL isDir);
    virtual void WINAPI GetFileDataForUpDir(const wchar_t* archivePath, CFileData& upDir) {}
    virtual BOOL WINAPI GetFileDataForNewDir(const wchar_t* dirName, CFileData& dir) { return TRUE; }
    virtual HIMAGELIST WINAPI GetSimplePluginIcons(int iconSize) { return NULL; }
    virtual BOOL WINAPI HasSimplePluginIcon(CFileData& file, BOOL isDir) { return FALSE; }
    virtual HICON WINAPI GetPluginIcon(const CFileData* file, int iconSize, BOOL& destroyIcon) { return NULL; }
    virtual int WINAPI CompareFilesFromFS(const CFileData* file1, const CFileData* file2) { return 0; }
    virtual void WINAPI SetupView(BOOL leftPanel, CSalamanderViewAbstract* view, const wchar_t* archivePath,
                                  const CFileData* upperDir);
    virtual void WINAPI ColumnFixedWidthShouldChange(BOOL leftPanel, const CColumn* column, int newFixedWidth);
    virtual void WINAPI ColumnWidthWasChanged(BOOL leftPanel, const CColumn* column, int newWidth);
    virtual BOOL WINAPI GetInfoLineContent(int panel, const CFileData* file, BOOL isDir, int selectedFiles,
                                           int selectedDirs, BOOL displaySize, const CQuadWord& selectedSize,
                                           CSalamanderStringBuffer* buffer, CSalamanderTextRangeBuffer* hotTexts) { return FALSE; }
    virtual BOOL WINAPI CanBeCopiedToClipboard() { return TRUE; }
    virtual BOOL WINAPI GetByteSize(const CFileData* file, BOOL isDir, CQuadWord* size) { return FALSE; }
    virtual BOOL WINAPI GetLastWriteDate(const CFileData* file, BOOL isDir, SYSTEMTIME* date) { return FALSE; }
    virtual BOOL WINAPI GetLastWriteTime(const CFileData* file, BOOL isDir, SYSTEMTIME* time) { return FALSE; }
};

// ****************************************************************************
//
// CPluginInterface
//

class CPluginInterfaceForArchiver : public CPluginInterfaceForArchiverAbstract
{
protected:
    CSalamanderForOperationsAbstract* Salamander;
    std::wstring ArcFileName;
    HANDLE ArcHandle;
    unsigned ArcFlags;
    BOOL List;
    //unsigned int Silent;
    BOOL Abort;
    CQuadWord ProgressTotal;
    const wchar_t* ArcRoot;
    DWORD RootLenW;
    std::wstring TargetName;
    HANDLE TargetFile;
    BOOL Success;
    //char Password[MAX_PASSWORD];
    //    BOOL FirstFile;
    BOOL NotWholeArchListed;
    CPluginDataInterface* PluginData;
    BOOL AllocateWholeFile;
    BOOL TestAllocateWholeFile;
    CDynamicString* ArchiveVolumes;

public:
    virtual BOOL WINAPI ListArchive(CSalamanderForOperationsAbstract* salamander, const wchar_t* fileName,
                                    CSalamanderDirectoryAbstract* dir,
                                    CPluginDataInterfaceAbstract*& pluginData);
    virtual BOOL WINAPI UnpackArchive(CSalamanderForOperationsAbstract* salamander, const wchar_t* fileName,
                                      CPluginDataInterfaceAbstract* pluginData, const wchar_t* targetDir,
                                      const wchar_t* archiveRoot, SalEnumSelection next, void* nextParam);
    virtual BOOL WINAPI UnpackOneFile(CSalamanderForOperationsAbstract* salamander, const wchar_t* fileName,
                                      CPluginDataInterfaceAbstract* pluginData, const wchar_t* nameInArchive,
                                      const CFileData* fileData, const wchar_t* targetDir,
                                      const wchar_t* newFileName, BOOL* renamingNotSupported);
    virtual BOOL WINAPI PackToArchive(CSalamanderForOperationsAbstract* salamander, const wchar_t* fileName,
                                      const wchar_t* archiveRoot, BOOL move, const wchar_t* sourcePath,
                                      SalEnumSelection2 next, void* nextParam) { return FALSE; }
    virtual BOOL WINAPI DeleteFromArchive(CSalamanderForOperationsAbstract* salamander, const wchar_t* fileName,
                                          CPluginDataInterfaceAbstract* pluginData, const wchar_t* archiveRoot,
                                          SalEnumSelection next, void* nextParam) { return FALSE; }
    virtual BOOL WINAPI UnpackWholeArchive(CSalamanderForOperationsAbstract* salamander, const wchar_t* fileName,
                                           const wchar_t* mask, const wchar_t* targetDir, BOOL delArchiveWhenDone,
                                           CDynamicString* archiveVolumes);
    virtual BOOL WINAPI CanCloseArchive(CSalamanderForOperationsAbstract* salamander, const wchar_t* fileName,
                                        BOOL force, int panel) { return TRUE; }
    virtual BOOL WINAPI GetCacheInfo(CSalamanderStringBuffer* tempPath, BOOL* ownDelete, BOOL* cacheCopies) { return FALSE; }
    virtual void WINAPI DeleteTmpCopy(const wchar_t* fileName, BOOL firstFile) {}
    virtual BOOL WINAPI PrematureDeleteTmpCopy(HWND parent, int copiesCount) { return FALSE; }

    BOOL Error(int error, ...);

    BOOL Init();

    BOOL OpenArchive();
    BOOL ReadHeader(CFileHeader* header);
    BOOL ProcessFile(int operation, const wchar_t* fileName);
    int ChangeVolProc(wchar_t* arcName, int mode);
    BOOL SafeSeek(CQuadWord position);
    int ProcessDataProc(unsigned char* addr, int size);
    int NeedPassword(wchar_t* password, int size);
    BOOL SetSolidPassword();
    BOOL SwitchToFirstVol(const wchar_t* arcName, BOOL* saveFirstVolume = NULL);
    BOOL MakeFilesList(TIndirectArray2<CRARExtractInfo>& files, SalEnumSelection next, void* nextParam, const wchar_t* targetDir);
    BOOL DoThisFile(CFileHeader* header, const wchar_t* arcName, const wchar_t* targetDir);
    BOOL BuildTargetName(CFileHeader* header, const wchar_t* targetDir, const wchar_t* relativeName);
    HANDLE CreateTargetFile(DWORD desiredAccess, DWORD shareMode, DWORD flagsAndAttributes, BOOL isDir,
                            const wchar_t* sourceName, const wchar_t* sourceInfo, BOOL* skipped, CQuadWord* allocateWholeFile);
    void DeleteTargetFile();
    void SetTargetAttributes(DWORD attributes);
    BOOL ConstructMaskArray(TIndirectArray2<wchar_t>& maskArray, const wchar_t* masks);
    BOOL UnpackWholeArchiveCalculateProgress(TIndirectArray2<wchar_t>& masks);
};

class CPluginInterface : public CPluginInterfaceAbstract
{
public:
    virtual void WINAPI About(HWND parent);

    virtual BOOL WINAPI Release(HWND parent, BOOL force);

    virtual void WINAPI LoadConfiguration(HWND parent, HKEY regKey, CSalamanderRegistryAbstract* registry);
    virtual void WINAPI SaveConfiguration(HWND parent, HKEY regKey, CSalamanderRegistryAbstract* registry);
    virtual void WINAPI Configuration(HWND parent);

    virtual void WINAPI Connect(HWND parent, CSalamanderConnectAbstract* salamander);

    virtual void WINAPI ReleasePluginDataInterface(CPluginDataInterfaceAbstract* pluginData)
    {
        if (pluginData)
            delete (CPluginDataInterface*)pluginData;
    }

    virtual CPluginInterfaceForArchiverAbstract* WINAPI GetInterfaceForArchiver();
    virtual CPluginInterfaceForViewerAbstract* WINAPI GetInterfaceForViewer() { return NULL; }
    virtual CPluginInterfaceForMenuExtAbstract* WINAPI GetInterfaceForMenuExt() { return NULL; }
    virtual CPluginInterfaceForFSAbstract* WINAPI GetInterfaceForFS() { return NULL; }
    virtual CPluginInterfaceForThumbLoaderAbstract* WINAPI GetInterfaceForThumbLoader() { return NULL; }

    virtual void WINAPI Event(int event, DWORD param) {}
    virtual void WINAPI ClearHistory(HWND parent) {}
    virtual void WINAPI AcceptChangeOnPathNotification(const wchar_t* path, BOOL includingSubdirs) {}

    virtual void WINAPI PasswordManagerEvent(HWND parent, int event) {}
};

extern HINSTANCE DLLInstance; // handle to SPL - language-independent resources
extern HINSTANCE HLanguage;   // handle to SLG - language-dependent resources

// this is sufficient for now instead of a configuration
#define OP_SKIPCONTINUED 0x01    // should we skip all files that start on the previous volume?
#define OP_NO_VOL_ATTENTION 0x02 // do not warn when the entire archive cannot be listed

#define OP_SAVED_IN_REGISTRY (OP_SKIPCONTINUED | OP_NO_VOL_ATTENTION)

// These options are not to be saved in config and are local for the current archive only
#define OP_SKITHISFILE 0x80000000

struct CConfiguration
{
    DWORD Options;

    // Custom columns:
    BOOL ListInfoPackedSize;          // Show custom column Packed
    DWORD ColumnPackedSizeWidth;      // LO/HI-WORD: left/right panel: Width for Packed column
    DWORD ColumnPackedSizeFixedWidth; // LO/HI-WORD: left/right panel: FixedWidth for Packed column
};

extern struct CConfiguration Config;

std::wstring LangStr(int resID);
void GetInfo(wchar_t* buffer, size_t bufferCount, const FILETIME* lastWrite, const CQuadWord& size);

//***********************************************************************************
//
// Rutiny ze SHLWAPI.DLL
//

//BOOL PathAppend(LPTSTR  pPath, LPCTSTR pMore);
//BOOL PathRemoveFileSpec(LPTSTR pszPath);
//LPTSTR PathAddBackslash(LPTSTR pszPath);
wchar_t* PathFindExtensionW(wchar_t* pszPath);
/*void PathRemoveExtension(LPTSTR pszPath);
BOOL PathRenameExtension(LPTSTR pszPath, LPCTSTR pszExt);*/
//LPTSTR PathStripPath(LPTSTR pszPath);

#ifndef SetWindowLongPtr
// compiling on VC6 w/o reasonably new SDK
#define SetWindowLongPtr SetWindowLong
#define GetWindowLongPtr GetWindowLong
#define GWLP_WNDPROC GWL_WNDPROC
#endif

#define DUMP_MEM_OBJECTS

#if defined(DUMP_MEM_OBJECTS) && defined(_DEBUG)
#define CRT_MEM_CHECKPOINT \
    _CrtMemState ___CrtMemState; \
    _CrtMemCheckpoint(&___CrtMemState);
#define CRT_MEM_DUMP_ALL_OBJECTS_SINCE _CrtMemDumpAllObjectsSince(&___CrtMemState);

#else //DUMP_MEM_OBJECTS
#define CRT_MEM_CHECKPOINT ;
#define CRT_MEM_DUMP_ALL_OBJECTS_SINCE ;

#endif //DUMP_MEM_OBJECTS
