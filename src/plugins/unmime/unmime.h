// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

extern CSalamanderGeneralAbstract* SalamanderGeneral;
extern CSalamanderSafeFileAbstract* SalamanderSafeFile;
extern CSalamanderGUIAbstract* SalamanderGUI;

// Flags which error/warning messages have been displayed and the user
// has chosen "Don't show this again"
#define DSA_RESOURCE_FORK_LOST 1

typedef struct tagGlobals
{
    BOOL bListMailHeaders;
    BOOL bListMessageBodies;
    BOOL bAppendCharset;
    BOOL bListAttachments;
    DWORD nDontShowAnymore; // DSA_xxx flags
} SGlobals;

extern SGlobals G;

class CArchiveData
{
public:
    CParserOutput ParserOutput;
    FILETIME ft;
    int RefCount;
};

class CPluginDataInterface : public CPluginDataInterfaceAbstract
{
public:
    virtual BOOL WINAPI CallReleaseForFiles() { return TRUE; }
    virtual BOOL WINAPI CallReleaseForDirs() { return FALSE; }
    virtual void WINAPI ReleasePluginData(CFileData& file, BOOL isDir);

    virtual void WINAPI GetFileDataForUpDir(const wchar_t* archivePath, CFileData& upDir) {}
    virtual BOOL WINAPI GetFileDataForNewDir(const wchar_t* dirName, CFileData& dir) { return TRUE; }

    virtual HIMAGELIST WINAPI GetSimplePluginIcons(int iconSize) { return NULL; }
    virtual BOOL WINAPI HasSimplePluginIcon(CFileData& file, BOOL isDir) { return TRUE; }
    virtual HICON WINAPI GetPluginIcon(const CFileData* file, int iconSize, BOOL& destroyIcon) { return NULL; }
    virtual int WINAPI CompareFilesFromFS(const CFileData* file1, const CFileData* file2) { return 0; }

    virtual void WINAPI SetupView(BOOL leftPanel, CSalamanderViewAbstract* view, const wchar_t* archivePath,
                                  const CFileData* upperDir) {};
    virtual void WINAPI ColumnFixedWidthShouldChange(BOOL leftPanel, const CColumn* column, int newFixedWidth) {}
    virtual void WINAPI ColumnWidthWasChanged(BOOL leftPanel, const CColumn* column, int newWidth) {}

    virtual BOOL WINAPI GetInfoLineContent(int panel, const CFileData* file, BOOL isDir, int selectedFiles,
                                           int selectedDirs, BOOL displaySize, const CQuadWord& selectedSize,
                                           CSalamanderStringBuffer* buffer, CSalamanderTextRangeBuffer* hotTexts) { return FALSE; }

    virtual BOOL WINAPI CanBeCopiedToClipboard() { return TRUE; }

    virtual BOOL WINAPI GetByteSize(const CFileData* file, BOOL isDir, CQuadWord* size) { return FALSE; }
    virtual BOOL WINAPI GetLastWriteDate(const CFileData* file, BOOL isDir, SYSTEMTIME* date) { return FALSE; }
    virtual BOOL WINAPI GetLastWriteTime(const CFileData* file, BOOL isDir, SYSTEMTIME* time) { return FALSE; }
};

class CPluginInterfaceForArchiver : public CPluginInterfaceForArchiverAbstract
{
private:
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
BOOL Error(int error, ...);
void ShowOneTimeMessage(HWND HParent, int msg, BOOL* pChecked);
BOOL SafeWriteFile(HANDLE hFile, LPVOID lpBuffer, DWORD nBytesToWrite, DWORD* pnBytesWritten, const wchar_t* fileName);

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
