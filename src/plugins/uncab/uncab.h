// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "uncab_text.h"

#include <vector>

/* winnt.h
#define FILE_ATTRIBUTE_READONLY             0x00000001
#define FILE_ATTRIBUTE_HIDDEN               0x00000002
#define FILE_ATTRIBUTE_SYSTEM               0x00000004
#define FILE_ATTRIBUTE_DIRECTORY            0x00000010
#define FILE_ATTRIBUTE_ARCHIVE              0x00000020
*/

#define FILE_ATTRIBUTE_MASK (0x37)

// silent flags
//#define SF_DATA       0x00010000
#define SF_LONGNAMES 0x00020000
#define SF_IOERRORS 0x00040000
#define SF_CONTINUED 0x00080000

#define FF_EXTRFILE 0x0001 // file that is being unpacked, not a CAB file
#define FF_SKIPFILE 0x0002 // file that is skipped but must be extracted without writing to disk

struct CFile
{
    HANDLE Handle;
    std::wstring FileName;
    DWORD Flags;
    DWORD cabOffset; // for sfx archives
};

// general Salamander interface - valid from startup until the plugin is unloaded
extern CSalamanderGeneralAbstract* SalamanderGeneral;

//CAB action
#define CA_LIST 0
#define CA_UNPACK 1
#define CA_UNPACK_ONE_FILE 2
#define CA_UNPACK_WHOLE_ARCHIVE 3

//cab cache items
// The cabinet NAME comes out of the CAB byte stream and stays bytes; the
// directory it was found in is Sally's own path and stays UTF-16, so a volume
// sitting in a folder FDI's ANSI interface could not spell is still reachable.
struct CCABCacheEntry
{
    std::string CABName;
    std::wstring CABPath;

    CCABCacheEntry(const char* name, const std::wstring& path);
};

// ****************************************************************************
//
// CPluginInterface
//

class CPluginInterfaceForArchiver : public CPluginInterfaceForArchiverAbstract
{
protected:
    CSalamanderForOperationsAbstract* Salamander;
    BOOL Abort;
    DWORD Action;
    DWORD Silent;
    BOOL NotWholeArchListed;
    BOOL IOError;
    BOOL FirstCAB;
    BOOL FirstCABINET_INFO;
    char CurrentCAB[CB_MAX_CABINET_NAME];
    std::wstring CurrentCABPathW; // directory holding the current/next volume
    char NextCAB[CB_MAX_CABINET_NAME];
    char NextDISK[CB_MAX_DISK_NAME];
    int NextCABIndex;
    int CurrentCABIndex;
    USHORT SetID;
    CSalamanderDirectoryAbstract* Dir; //used by ListFile
    DWORD Count;
    CQuadWord ProgressTotal;
    std::wstring ArcRoot;
    size_t RootLen;
    std::vector<std::wstring> Files;
    const wchar_t* TargetDir;
    std::wstring InitialCabinetPath;
    std::vector<std::wstring> Masks;
    TIndirectArray2<CCABCacheEntry> CABCache;
    BOOL AllocateWholeFile;
    BOOL TestAllocateWholeFile;

    std::wstring NameInArchive; // unpack one file
    BOOL OneFileSuccess;
public:
    CPluginInterfaceForArchiver() : CABCache(16) { ; }

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

    BOOL FDIError(int erfOper);

    BOOL Init();

    //BOOL OpenArchive();
    //BOOL ReadHeader(CFileHeader * header);
    //BOOL ProcessFile(int operation, char * fileName);
    //int ChangeVolProc(char *arcName, int mode);
    //int ProcessDataProc(unsigned char *addr, int size);
    //void SwitchToFirstVol(const char * arcName);
    BOOL ConstructMaskArray(std::vector<std::wstring>& maskArray, const wchar_t* masks);
    BOOL UpdateCABCache(const char* name, const std::wstring& path);
    BOOL GetCachedCABPath(const char* name, std::wstring& path);
    // Full wide path of the volume being read right now - InitialCabinetPath while
    // the initial-cabinet token is current, CurrentCABPathW + CurrentCAB after.
    BOOL CurrentCabinetFullPathW(std::wstring& fullPath) const;
    BOOL ListFile(char* fileName, DWORD size, WORD date, WORD time, DWORD attributes);
    BOOL DoThisFile(const std::wstring& fileName);
    INT_PTR UnpackFile(char* fileName, DWORD size, WORD date, WORD time, DWORD attributes);
    BOOL MakeFilesList(std::vector<std::wstring>& files, SalEnumSelection next, void* nextParam, const wchar_t* targetDir);
    INT_PTR Open(char* pszFile, int oflag, int pmode);
    // Open() resolves its byte argument to this; the retry loops call it directly
    // so a probed volume never round-trips back through the CAB byte domain.
    INT_PTR OpenWide(const std::wstring& openPath, int oflag);
    UINT Read(INT_PTR hf, void* pv, UINT cb);
    UINT Write(INT_PTR hf, void* pv, UINT cb);
    int Close(INT_PTR hf);
    long SafeSeek(CFile* file, DWORD distance, DWORD method);
    long Seek(INT_PTR hf, long dist, int seektype);
    INT_PTR Notify(FDINOTIFICATIONTYPE fdint, PFDINOTIFICATION pfdin);
};

class CPluginInterface : public CPluginInterfaceAbstract
{
public:
    virtual void WINAPI About(HWND parent);

    virtual BOOL WINAPI Release(HWND parent, BOOL force) { return TRUE; }

    virtual void WINAPI LoadConfiguration(HWND parent, HKEY regKey, CSalamanderRegistryAbstract* registry);
    virtual void WINAPI SaveConfiguration(HWND parent, HKEY regKey, CSalamanderRegistryAbstract* registry);
    virtual void WINAPI Configuration(HWND parent);

    virtual void WINAPI Connect(HWND parent, CSalamanderConnectAbstract* salamander);

    virtual void WINAPI ReleasePluginDataInterface(CPluginDataInterfaceAbstract* pluginData) { return; }

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

extern HINSTANCE DLLInstance; // handle to the SPL - language-independent resources
extern HINSTANCE HLanguage;   // handle to the SLG - language-dependent resources

// for now this is sufficient instead of configuration
#define OP_SKIPCONTINUED 0x01    // will we skip all files that start on the previous volume?
#define OP_NO_VOL_ATTENTION 0x02 // do not warn when the entire archive cannot be listed

extern DWORD Options; // configuration

std::wstring GetInfo(const FILETIME* lastWrite, unsigned size);

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
