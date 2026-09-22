// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "arraylt.h"

#define SF_LONGNAMES 0x00020000

// general Salamander interface - valid from startup until the plugin ends
extern CSalamanderGeneralAbstract* SalamanderGeneral;

// ****************************************************************************
//
// CPluginInterface
//

class CPluginInterfaceForArchiver : public CPluginInterfaceForArchiverAbstract
{
private:
    CSalamanderForOperationsAbstract* Salamander;
    DWORD Silent;
    BOOL Abort;
    size_t RootLen; // UTF-16 length of archiveRoot; skips a prefix of the decoded member name
    CQuadWord ProgressTotal;
    CQuadWord currentProgress;
    BOOL Ret;
    BOOL UnpackWhole;
    BOOL unpacked;
    BOOL CRCSkipAll;

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

    void UnpackInnerBody(FILE* f, const wchar_t* targetDir, const wchar_t* fileName, LHA_HEADER& hdr, BOOL bDir);
    BOOL MakeFilesList(TDirectArray<int>& offsets, SalEnumSelection next, void* nextParam, const wchar_t* targetDir);
    BOOL ConstructMaskArray(TIndirectArray<std::wstring>& maskArray, const wchar_t* masks);

    friend BOOL ProgressCallback(int);
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

std::wstring LangStr(int resID);
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
