// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <string>
#include <string_view>

// Salamander's general interface - valid from startup until the plugin shuts down
extern CSalamanderGeneralAbstract* SalamanderGeneral;

#define STATUS_OK 1

struct CFileInfo
{
    std::wstring Name;
    DWORD Status;
    DWORD DirDepth;
    DWORD Size;

    CFileInfo(std::wstring name, DWORD status, DWORD dirDepth, DWORD size);
};

CFileInfo* NewFileInfo(std::wstring name, DWORD status, DWORD dirDepth, DWORD size) noexcept;

struct COptDlgData
{
    DWORD PakSize;
    DWORD ValData;
};

// ****************************************************************************
//
// CPluginInterface
//

#define SF_LONGNAMES 0x00010000
#define SF_IOERRORS 0x00020000
#define SF_LARGE 0x00040000
#define SF_OVEWRITEALL 0x00080000
#define SF_SKIPALL 0x00100000

#define OPTIMIZE_MENUID 0x01

class CPluginInterfaceForArchiver : public CPluginInterfaceForArchiverAbstract
{
public:
    CSalamanderForOperationsAbstract* Salamander;
    CPakIfaceAbstract* PakIFace;
    const wchar_t* PakFileName;
    HANDLE IOFile;
    const wchar_t* IOFileName;
    DWORD Silent;
    BOOL Abort;
    CQuadWord ProgressTotal;

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
                                      SalEnumSelection2 next, void* nextParam);
    virtual BOOL WINAPI DeleteFromArchive(CSalamanderForOperationsAbstract* salamander, const wchar_t* fileName,
                                          CPluginDataInterfaceAbstract* pluginData, const wchar_t* archiveRoot,
                                          SalEnumSelection next, void* nextParam);
    virtual BOOL WINAPI UnpackWholeArchive(CSalamanderForOperationsAbstract* salamander, const wchar_t* fileName,
                                           const wchar_t* mask, const wchar_t* targetDir, BOOL delArchiveWhenDone,
                                           CDynamicString* archiveVolumes);
    virtual BOOL WINAPI CanCloseArchive(CSalamanderForOperationsAbstract* salamander, const wchar_t* fileName,
                                        BOOL force, int panel) { return TRUE; }
    virtual BOOL WINAPI GetCacheInfo(CSalamanderStringBuffer* tempPath, BOOL* ownDelete, BOOL* cacheCopies) { return FALSE; }
    virtual void WINAPI DeleteTmpCopy(const wchar_t* fileName, BOOL firstFile) {}
    virtual BOOL WINAPI PrematureDeleteTmpCopy(HWND parent, int copiesCount) { return FALSE; }

    /*void InitPlugin(CSalamanderForOperationsAbstract * salamander, CPakIfaceAbstract * pakIFace,
                    const char * pakFileName);*/

    //    HWND GetParentWindow();

    // 'archiveRoot' here is PAK's own byte-domain directory text, not the raw wide SDK
    // parameter. Conversion is owned by pak_text's exact-or-refuse adapter.
    BOOL MakeFileList(TIndirectArray2<CFileInfo>& files, std::string_view archiveRoot,
                      SalEnumSelection next, void* nextParam);

    BOOL UnpackFiles(TIndirectArray2<CFileInfo>& files, const wchar_t* arcFile,
                     int rootLen, const wchar_t* targetDir);

    BOOL ConstructMaskArray(TIndirectArray2<std::wstring>& maskArray, const wchar_t* masks);

    BOOL MakeFileList2(TIndirectArray2<std::wstring>& masks, TIndirectArray2<CFileInfo>& files);

    BOOL DeleteFiles(std::string_view archiveRoot, SalEnumSelection next, void* nextParam);

    BOOL MakeFileList3(TIndirectArray2<CFileInfo>& files, BOOL* del, std::string_view archiveRoot, const wchar_t* sourcePath,
                       SalEnumSelection2 next, void* nextParam);

    BOOL DelFilesToBeOverwritten(unsigned* deleted);

    BOOL AddFiles(TIndirectArray2<CFileInfo>& files, unsigned deleted, const wchar_t* sourPath, std::string_view archiveRoot);

    void DeleteSourceFiles(TIndirectArray2<CFileInfo>& files, const wchar_t* sourcePath);
};

class CPluginInterfaceForMenuExt : public CPluginInterfaceForMenuExtAbstract
{
public:
    virtual DWORD WINAPI GetMenuItemState(int id, DWORD eventMask);
    virtual BOOL WINAPI ExecuteMenuItem(CSalamanderForOperationsAbstract* salamander, HWND parent,
                                        int id, DWORD eventMask);
    virtual BOOL WINAPI HelpForMenuItem(HWND parent, int id);
    virtual void WINAPI BuildMenu(HWND parent, CSalamanderBuildMenuAbstract* salamander) {}
};

class CPluginInterface : public CPluginInterfaceAbstract
{
public:
    virtual void WINAPI About(HWND parent);

    virtual BOOL WINAPI Release(HWND parent, BOOL force);

    virtual void WINAPI LoadConfiguration(HWND parent, HKEY regKey, CSalamanderRegistryAbstract* registry) {}
    virtual void WINAPI SaveConfiguration(HWND parent, HKEY regKey, CSalamanderRegistryAbstract* registry) {}
    virtual void WINAPI Configuration(HWND parent) {}

    virtual void WINAPI Connect(HWND parent, CSalamanderConnectAbstract* salamander);

    virtual void WINAPI ReleasePluginDataInterface(CPluginDataInterfaceAbstract* pluginData) { return; }

    virtual CPluginInterfaceForArchiverAbstract* WINAPI GetInterfaceForArchiver();
    virtual CPluginInterfaceForViewerAbstract* WINAPI GetInterfaceForViewer() { return NULL; }
    virtual CPluginInterfaceForMenuExtAbstract* WINAPI GetInterfaceForMenuExt();
    virtual CPluginInterfaceForFSAbstract* WINAPI GetInterfaceForFS() { return NULL; }
    virtual CPluginInterfaceForThumbLoaderAbstract* WINAPI GetInterfaceForThumbLoader() { return NULL; }

    virtual void WINAPI Event(int event, DWORD param) {}
    virtual void WINAPI ClearHistory(HWND parent) {}
    virtual void WINAPI AcceptChangeOnPathNotification(const wchar_t* path, BOOL includingSubdirs) {}

    virtual void WINAPI PasswordManagerEvent(HWND parent, int event) {}
};

class CPakCallbacks : public CPakCallbacksAbstract
{
    BOOL UserBreak;

public:
    CPluginInterfaceForArchiver* Plugin;

    CPakCallbacks(CPluginInterfaceForArchiver* plugin);

    //returns TRUE if the processing should continue, or FALSE
    //if the operation should finish
    virtual BOOL HandleError(DWORD flags, int errorID, va_list arglist);

    //reads data from the output file
    //called while packing files
    //returns TRUE when the operation should continue
    virtual BOOL Read(void* buffer, DWORD size);

    //writes data to the output file
    //called while extracting files
    //returns TRUE when the operation should continue
    virtual BOOL Write(void* buffer, DWORD size);

    //reports on the progress of data processing
    //adds the 'size' amount
    //returns TRUE when the operation should continue
    virtual BOOL AddProgress(unsigned size);

    //reports on ongoing deletion
    virtual BOOL DelNotify(const char* fileName, unsigned fileProgressTotal);

    BOOL SafeSeek(DWORD position);
};

extern HINSTANCE DLLInstance; // handle to the SPL - language-independent resources
extern HINSTANCE HLanguage;   // handle to the SLG - language-dependent resources
/*
extern FPAKGetIFace PAKGetIFace;
extern FPAKReleaseIFace PAKReleaseIFace;
*/

extern CPluginInterfaceForArchiver InterfaceForArchiver;
extern CPluginInterfaceForMenuExt InterfaceForMenuExt;

std::wstring LangStr(int resID);
std::wstring GetInfo(FILETIME* lastWrite, unsigned size);
INT_PTR WINAPI OptimizeDlgProc(HWND hDlg, UINT uMsg, WPARAM wParam, LPARAM lParam);
