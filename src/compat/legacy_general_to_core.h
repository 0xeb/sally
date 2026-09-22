// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

// Complete frozen-v107 General facade declaration. The 249 method declarations
// are mechanically transcribed from the immutable snapshot; conversion bodies
// are ordinary reviewed source in legacy_general_to_core.cpp.

#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include "compat/sdk107.h"
#include "plugins/shared/spl_gen.h"

namespace sally::unicode
{
struct WideTextRange;
}

namespace sally::compat
{

class CLegacyPluginDataResolver;
class CLegacyPluginFSResolver;

class CLegacyViewerDataBridge final
{
public:
    bool Prepare(const sdk107::CSalamanderPluginViewerData* legacy);
    ::CSalamanderPluginViewerData* Data();

private:
    std::vector<unsigned char> Storage;
    std::wstring FileName;
    std::wstring Caption;
};

// Typed-same does not mean type-identical: the frozen and live namespaces own
// distinct structs, enums, and callback signatures. These small, testable
// bridges keep casts out of the General vtable forwarders.
struct CLegacyMessageBoxParams
{
    ::MSGBOXEX_PARAMS Params = {};
    std::wstring Text;
    std::wstring Caption;
    std::wstring CheckBoxText;
    std::wstring AliasBtnNames;
    std::wstring URL;
    std::wstring URLText;
};
bool CopyLegacyMessageBoxParams(const sdk107::MSGBOXEX_PARAMS* legacy,
                                CLegacyMessageBoxParams& wide);
bool MapLegacyHtmlHelpCommand(sdk107::CHtmlHelpCommand legacy,
                              ::CHtmlHelpCommand& wide);
bool PrepareLegacyThemeInfo(const sdk107::CSalamanderThemeInfo* legacy,
                            ::CSalamanderThemeInfo& wide);
void CommitLegacyThemeInfo(const ::CSalamanderThemeInfo& wide,
                           sdk107::CSalamanderThemeInfo& legacy);
enum class GeneralOutputCommitStatus
{
    Complete,
    InsufficientBuffer,
    NotRepresentable,
};
GeneralOutputCommitStatus PrepareWideGeneralOutputExact(
    const wchar_t* value, std::size_t outputSize, std::string& output);
bool PrepareLegacyInstalledModuleOutputs(
    const wchar_t* wideModule, const wchar_t* wideVersion,
    std::string& module, std::string& version);
bool PrepareLegacyClipboardText(const char* text, int textLen,
                                std::wstring& wide);
GeneralOutputCommitStatus PrepareLegacyPanelPathOutput(
    const wchar_t* path, const wchar_t* archiveOrFS, std::size_t outputSize,
    std::string& output, std::size_t& archiveOrFSOffset);
GeneralOutputCommitStatus PrepareLegacyParsePathOutputs(
    const wchar_t* path, const wchar_t* secondPart,
    const wchar_t* nextFocus, std::size_t pathOutputSize,
    bool requireSecondPart, std::string& pathOutput,
    std::size_t& secondPartOffset, std::string& nextFocusOutput);
bool WideOffsetForLegacyPointer(const char* legacy,
                                const std::wstring& wide,
                                const char* legacyPointer,
                                std::size_t& wideOffset);
GeneralOutputCommitStatus PrepareLegacySplitWindowsPathOutput(
    const wchar_t* path, std::size_t widePathCapacity,
    const wchar_t* mask, std::size_t outputSize, bool requireMask,
    std::string& output, std::size_t& maskOffset);
GeneralOutputCommitStatus PrepareLegacySplitGeneralPathOutputs(
    const wchar_t* path, std::size_t widePathCapacity,
    const wchar_t* mask, bool requireMask, const wchar_t* newDirs,
    bool prepareNewDirs, std::size_t pathOutputSize,
    std::size_t newDirsOutputSize, std::string& pathOutput,
    std::size_t& maskOffset, std::string& newDirsOutput);
bool PrepareLegacyVarStringExpansionOutput(
    const std::wstring& wideOutput,
    const sally::unicode::WideTextRange* widePlacements,
    std::size_t widePlacementCount, std::size_t outputSize, std::string& output,
    std::vector<DWORD>& placements);
bool CopyWideGeneralOutputExact(const wchar_t* value, char* output,
                                std::size_t outputSize);
bool PrepareLegacyMaskNameOutput(const wchar_t* wide, std::size_t outputSize,
                                 std::string& output);
GeneralOutputCommitStatus PrepareLegacyFullNameOutputs(
    const wchar_t* wideName, const wchar_t* wideNextFocus,
    std::size_t nameOutputSize, std::string& nameOutput,
    std::string& nextFocusOutput, bool& publishNextFocus);
bool PrepareLegacyTruncatedGeneralTextOutput(const wchar_t* wide,
                                             std::size_t outputSize,
                                             std::string& output);
char* PublishLegacyGeneralErrorText(const wchar_t* wide);
bool LegacyByteOffsetForWidePointer(const char* legacy,
                                    const std::wstring& wide,
                                    const wchar_t* widePointer,
                                    std::size_t& legacyOffset);
int LegacyByteLengthForWidePrefix(const char* legacy,
                                  const std::wstring& wide,
                                  int wideLength);
bool PrepareLegacyVarStringErrorPositions(
    const char* legacy, const std::wstring& wide,
    int wideErrorPos1, int wideErrorPos2,
    int& legacyErrorPos1, int& legacyErrorPos2);
int CommitLegacyRootPathOutput(const wchar_t* wide, int wideLength,
                               char* output, std::size_t outputSize);
bool PrepareLegacyCutDirectoryOutput(
    const wchar_t* widePath, const wchar_t* wideCutDir,
    std::size_t outputSize, std::string& output,
    std::size_t& cutDirOffset);

class CLegacyLoadOrSaveConfigurationCallback final
{
public:
    CLegacyLoadOrSaveConfigurationCallback(
        sdk107::FSalLoadOrSaveConfiguration callback, void* parameter);

    static void WINAPI Invoke(BOOL load, HKEY regKey,
                              ::CSalamanderRegistryAbstract* registry,
                              void* parameter);

private:
    sdk107::FSalLoadOrSaveConfiguration Callback;
    void* Parameter;
};

class CLegacyPluginOperationFromDiskCallback final
{
public:
    CLegacyPluginOperationFromDiskCallback(
        sdk107::SalPluginOperationFromDisk callback, void* parameter);

    static void WINAPI Invoke(const wchar_t* sourcePath,
                              ::SalEnumSelection2 next, void* nextParam,
                              void* parameter);

private:
    sdk107::SalPluginOperationFromDisk Callback;
    void* Parameter;
};

class CLegacySamePathCallbackScope final
{
public:
    explicit CLegacySamePathCallbackScope(
        sdk107::SGP_IsTheSamePathF callback);
    ~CLegacySamePathCallbackScope();

    ::SGP_IsTheSamePathF WideCallback() const;

    CLegacySamePathCallbackScope(const CLegacySamePathCallbackScope&) = delete;
    CLegacySamePathCallbackScope& operator=(
        const CLegacySamePathCallbackScope&) = delete;

private:
    static BOOL WINAPI Invoke(const wchar_t* path1, const wchar_t* path2);
    static thread_local CLegacySamePathCallbackScope* Current;

    sdk107::SGP_IsTheSamePathF Callback;
    CLegacySamePathCallbackScope* Previous = nullptr;
};

class CLegacySalamanderGeneral final : public sdk107::CSalamanderGeneralAbstract
{
public:
    using MSGBOXEX_PARAMS = sdk107::MSGBOXEX_PARAMS;
    using CQuadWord = sdk107::CQuadWord;
    using CSalamanderMaskGroup = sdk107::CSalamanderMaskGroup;
    using CPluginFSInterfaceAbstract = sdk107::CPluginFSInterfaceAbstract;
    using CPluginDataInterfaceAbstract = sdk107::CPluginDataInterfaceAbstract;
    using CFileData = sdk107::CFileData;
    using CSalamanderVarStrEntry = sdk107::CSalamanderVarStrEntry;
    using FSalLoadOrSaveConfiguration = sdk107::FSalLoadOrSaveConfiguration;
    using CSalamanderPluginViewerData = sdk107::CSalamanderPluginViewerData;
    using SGP_IsTheSamePathF = sdk107::SGP_IsTheSamePathF;
    using SalPluginOperationFromDisk = sdk107::SalPluginOperationFromDisk;
    using CSalamanderBMSearchData = sdk107::CSalamanderBMSearchData;
    using CSalamanderREGEXPSearchData = sdk107::CSalamanderREGEXPSearchData;
    using CSalamanderMD5 = sdk107::CSalamanderMD5;
    using CSalamanderDirectoryAbstract = sdk107::CSalamanderDirectoryAbstract;
    using CHtmlHelpCommand = sdk107::CHtmlHelpCommand;
    using CSalamanderZLIBAbstract = sdk107::CSalamanderZLIBAbstract;
    using CSalamanderPNGAbstract = sdk107::CSalamanderPNGAbstract;
    using CSalamanderCryptAbstract = sdk107::CSalamanderCryptAbstract;
    using CSalamanderPasswordManagerAbstract = sdk107::CSalamanderPasswordManagerAbstract;
    using CSalamanderBZIP2Abstract = sdk107::CSalamanderBZIP2Abstract;
    using CSalamanderThemeInfo = sdk107::CSalamanderThemeInfo;
    using LPCTSTR = sdk107::LPCTSTR;
    using LPTSTR = sdk107::LPTSTR;

    CLegacySalamanderGeneral(::CSalamanderGeneralAbstract& wideGeneral,
                             CLegacyPluginDataResolver& pluginDataResolver,
                             CLegacyPluginFSResolver& pluginFSResolver,
                             int builtForVersion);
    ~CLegacySalamanderGeneral();

    int WINAPI ShowMessageBox(const char* text, const char* title, int type) override;
    int WINAPI SalMessageBox(HWND hParent, LPCTSTR lpText, LPCTSTR lpCaption, UINT uType) override;
    int WINAPI SalMessageBoxEx(const MSGBOXEX_PARAMS* params) override;
    HWND WINAPI GetMsgBoxParent() override;
    HWND WINAPI GetMainWindowHWND() override;
    void WINAPI RestoreFocusInSourcePanel() override;
    int WINAPI DialogError(HWND parent, DWORD flags, const char* fileName, const char* error, const char* title) override;
    int WINAPI DialogOverwrite(HWND parent, DWORD flags, const char* fileName1, const char* fileData1, const char* fileName2, const char* fileData2) override;
    int WINAPI DialogQuestion(HWND parent, DWORD flags, const char* fileName, const char* question, const char* title) override;
    BOOL WINAPI CheckAndCreateDirectory(const char* dir, HWND parent = NULL, BOOL quiet = TRUE, char* errBuf = NULL, int errBufSize = 0, char* firstCreatedDir = NULL, BOOL manualCrDir = FALSE) override;
    BOOL WINAPI TestFreeSpace(HWND parent, const char* path, const CQuadWord& totalSize, const char* messageTitle) override;
    void WINAPI GetDiskFreeSpace(CQuadWord* retValue, const char* path, CQuadWord* total) override;
    BOOL WINAPI SalGetDiskFreeSpace(const char* path, LPDWORD lpSectorsPerCluster, LPDWORD lpBytesPerSector, LPDWORD lpNumberOfFreeClusters, LPDWORD lpTotalNumberOfClusters) override;
    BOOL WINAPI SalGetVolumeInformation(const char* path, char* rootOrCurReparsePoint, LPTSTR lpVolumeNameBuffer, DWORD nVolumeNameSize, LPDWORD lpVolumeSerialNumber, LPDWORD lpMaximumComponentLength, LPDWORD lpFileSystemFlags, LPTSTR lpFileSystemNameBuffer, DWORD nFileSystemNameSize) override;
    UINT WINAPI SalGetDriveType(const char* path) override;
    BOOL WINAPI SalGetTempFileName(const char* path, const char* prefix, char* tmpName, BOOL file, DWORD* err) override;
    void WINAPI RemoveTemporaryDir(const char* dir) override;
    BOOL WINAPI SalMoveFile(const char* srcName, const char* destName, DWORD* err) override;
    BOOL WINAPI SalGetFileSize(HANDLE file, CQuadWord& size, DWORD& err) override;
    void WINAPI ExecuteAssociation(HWND parent, const char* path, const char* name) override;
    BOOL WINAPI GetTargetDirectory(HWND parent, HWND hCenterWindow, const char* title, const char* comment, char* path, BOOL onlyNet, const char* initDir) override;
    void WINAPI PrepareMask(char* mask, const char* src) override;
    BOOL WINAPI AgreeMask(const char* filename, const char* mask, BOOL hasExtension) override;
    char* WINAPI MaskName(char* buffer, int bufSize, const char* name, const char* mask) override;
    void WINAPI PrepareExtMask(char* mask, const char* src) override;
    BOOL WINAPI AgreeExtMask(const char* filename, const char* mask, BOOL hasExtension) override;
    CSalamanderMaskGroup* WINAPI AllocSalamanderMaskGroup() override;
    void WINAPI FreeSalamanderMaskGroup(CSalamanderMaskGroup* maskGroup) override;
    void* WINAPI Alloc(int size) override;
    void WINAPI Free(void* ptr) override;
    char* WINAPI DupStr(const char* str) override;
    void WINAPI GetLowerAndUpperCase(unsigned char** lowerCase, unsigned char** upperCase) override;
    void WINAPI ToLowerCase(char* str) override;
    void WINAPI ToUpperCase(char* str) override;
    int WINAPI StrCmpEx(const char* s1, int l1, const char* s2, int l2) override;
    int WINAPI StrICpy(char* dest, const char* src) override;
    int WINAPI StrICmp(const char* s1, const char* s2) override;
    int WINAPI StrICmpEx(const char* s1, int l1, const char* s2, int l2) override;
    int WINAPI StrNICmp(const char* s1, const char* s2, int n) override;
    int WINAPI MemICmp(const void* buf1, const void* buf2, int n) override;
    int WINAPI RegSetStrICmp(const char* s1, const char* s2) override;
    int WINAPI RegSetStrICmpEx(const char* s1, int l1, const char* s2, int l2, BOOL* numericalyEqual) override;
    int WINAPI RegSetStrCmp(const char* s1, const char* s2) override;
    int WINAPI RegSetStrCmpEx(const char* s1, int l1, const char* s2, int l2, BOOL* numericalyEqual) override;
    BOOL WINAPI GetPanelPath(int panel, char* buffer, int bufferSize, int* type, char** archiveOrFS, BOOL convertFSPathToExternal = FALSE) override;
    BOOL WINAPI GetLastWindowsPanelPath(int panel, char* buffer, int bufferSize) override;
    void WINAPI GetPluginFSName(char* buf, int fsNameIndex) override;
    CPluginFSInterfaceAbstract* WINAPI GetPanelPluginFS(int panel) override;
    CPluginDataInterfaceAbstract* WINAPI GetPanelPluginData(int panel) override;
    const CFileData* WINAPI GetPanelFocusedItem(int panel, BOOL* isDir) override;
    const CFileData* WINAPI GetPanelItem(int panel, int* index, BOOL* isDir) override;
    const CFileData* WINAPI GetPanelSelectedItem(int panel, int* index, BOOL* isDir) override;
    BOOL WINAPI GetPanelSelection(int panel, int* selectedFiles, int* selectedDirs) override;
    int WINAPI GetPanelTopIndex(int panel) override;
    void WINAPI SkipOneActivateRefresh() override;
    void WINAPI SelectPanelItem(int panel, const CFileData* file, BOOL select) override;
    void WINAPI RepaintChangedItems(int panel) override;
    void WINAPI SelectAllPanelItems(int panel, BOOL select, BOOL repaint) override;
    void WINAPI SetPanelFocusedItem(int panel, const CFileData* file, BOOL partVis) override;
    BOOL WINAPI GetFilterFromPanel(int panel, char* masks, int masksBufSize) override;
    int WINAPI GetSourcePanel() override;
    BOOL WINAPI GetPanelWithPluginFS(CPluginFSInterfaceAbstract* pluginFS, int& panel) override;
    void WINAPI ChangePanel() override;
    char* WINAPI NumberToStr(char* buffer, const CQuadWord& number) override;
    char* WINAPI PrintDiskSize(char* buf, const CQuadWord& size, int mode) override;
    char* WINAPI PrintTimeLeft(char* buf, const CQuadWord& secs) override;
    BOOL WINAPI HasTheSameRootPath(const char* path1, const char* path2) override;
    int WINAPI CommonPrefixLength(const char* path1, const char* path2) override;
    BOOL WINAPI PathIsPrefix(const char* prefix, const char* path) override;
    BOOL WINAPI IsTheSamePath(const char* path1, const char* path2) override;
    int WINAPI GetRootPath(char* root, const char* path) override;
    BOOL WINAPI CutDirectory(char* path, char** cutDir = NULL) override;
    BOOL WINAPI SalPathAppend(char* path, const char* name, int pathSize) override;
    BOOL WINAPI SalPathAddBackslash(char* path, int pathSize) override;
    void WINAPI SalPathRemoveBackslash(char* path) override;
    void WINAPI SalPathStripPath(char* path) override;
    void WINAPI SalPathRemoveExtension(char* path) override;
    BOOL WINAPI SalPathAddExtension(char* path, const char* extension, int pathSize) override;
    BOOL WINAPI SalPathRenameExtension(char* path, const char* extension, int pathSize) override;
    const char* WINAPI SalPathFindFileName(const char* path) override;
    BOOL WINAPI SalGetFullName(char* name, int* errTextID = NULL, const char* curDir = NULL, char* nextFocus = NULL, int nameBufSize = MAX_PATH) override;
    void WINAPI SalUpdateDefaultDir(BOOL activePrefered) override;
    char* WINAPI GetGFNErrorText(int GFN, char* buf, int bufSize) override;
    char* WINAPI GetErrorText(int err, char* buf = NULL, int bufSize = 0) override;
    COLORREF WINAPI GetCurrentColor(int color) override;
    void WINAPI FocusNameInPanel(int panel, const char* path, const char* name) override;
    BOOL WINAPI ChangePanelPath(int panel, const char* path, int* failReason = NULL, int suggestedTopIndex = -1, const char* suggestedFocusName = NULL, BOOL convertFSPathToInternal = TRUE) override;
    BOOL WINAPI ChangePanelPathToDisk(int panel, const char* path, int* failReason = NULL, int suggestedTopIndex = -1, const char* suggestedFocusName = NULL) override;
    BOOL WINAPI ChangePanelPathToArchive(int panel, const char* archive, const char* archivePath, int* failReason = NULL, int suggestedTopIndex = -1, const char* suggestedFocusName = NULL, BOOL forceUpdate = FALSE) override;
    BOOL WINAPI ChangePanelPathToPluginFS(int panel, const char* fsName, const char* fsUserPart, int* failReason = NULL, int suggestedTopIndex = -1, const char* suggestedFocusName = NULL, BOOL forceUpdate = FALSE, BOOL convertPathToInternal = FALSE) override;
    BOOL WINAPI ChangePanelPathToDetachedFS(int panel, CPluginFSInterfaceAbstract* detachedFS, int* failReason = NULL, int suggestedTopIndex = -1, const char* suggestedFocusName = NULL) override;
    BOOL WINAPI ChangePanelPathToFixedDrive(int panel, int* failReason = NULL) override;
    void WINAPI RefreshPanelPath(int panel, BOOL forceRefresh = FALSE, BOOL focusFirstNewItem = FALSE) override;
    void WINAPI PostRefreshPanelPath(int panel, BOOL focusFirstNewItem = FALSE) override;
    void WINAPI PostRefreshPanelFS(CPluginFSInterfaceAbstract* modifiedFS, BOOL focusFirstNewItem = FALSE) override;
    BOOL WINAPI CloseDetachedFS(HWND parent, CPluginFSInterfaceAbstract* detachedFS) override;
    BOOL WINAPI DuplicateAmpersands(char* buffer, int bufferSize) override;
    void WINAPI RemoveAmpersands(char* text) override;
    BOOL WINAPI ValidateVarString(HWND msgParent, const char* varText, int& errorPos1, int& errorPos2, const CSalamanderVarStrEntry* variables) override;
    BOOL WINAPI ExpandVarString(HWND msgParent, const char* varText, char* buffer, int bufferLen, const CSalamanderVarStrEntry* variables, void* param, BOOL ignoreEnvVarNotFoundOrTooLong = FALSE, DWORD* varPlacements = NULL, int* varPlacementsCount = NULL, BOOL detectMaxVarWidths = FALSE, int* maxVarWidths = NULL, int maxVarWidthsCount = 0) override;
    BOOL WINAPI SetFlagLoadOnSalamanderStart(BOOL start) override;
    void WINAPI PostUnloadThisPlugin() override;
    BOOL WINAPI EnumInstalledModules(int* index, char* module, char* version) override;
    void WINAPI CallLoadOrSaveConfiguration(BOOL load, FSalLoadOrSaveConfiguration loadOrSaveFunc, void* param) override;
    BOOL WINAPI CopyTextToClipboard(const char* text, int textLen, BOOL showEcho, HWND echoParent) override;
    BOOL WINAPI CopyTextToClipboardW(const wchar_t* text, int textLen, BOOL showEcho, HWND echoParent) override;
    void WINAPI PostMenuExtCommand(int id, BOOL waitForSalIdle) override;
    BOOL WINAPI SalamanderIsNotBusy(DWORD* lastIdleTime) override;
    void WINAPI SetPluginBugReportInfo(const char* message, const char* email) override;
    BOOL WINAPI IsPluginInstalled(const char* pluginSPL) override;
    BOOL WINAPI ViewFileInPluginViewer(const char* pluginSPL, CSalamanderPluginViewerData* pluginData, BOOL useCache, const char* rootTmpPath, const char* fileNameInCache, int& error) override;
    void WINAPI PostChangeOnPathNotification(const char* path, BOOL includingSubdirs) override;
    DWORD WINAPI SalCheckPath(BOOL echo, const char* path, DWORD err, HWND parent) override;
    BOOL WINAPI SalCheckAndRestorePath(HWND parent, const char* path, BOOL tryNet) override;
    BOOL WINAPI SalCheckAndRestorePathWithCut(HWND parent, char* path, BOOL& tryNet, DWORD& err, DWORD& lastErr, BOOL& pathInvalid, BOOL& cut, BOOL donotReconnect) override;
    BOOL WINAPI SalParsePath(HWND parent, char* path, int& type, BOOL& isDir, char*& secondPart, const char* errorTitle, char* nextFocus, BOOL curPathIsDiskOrArchive, const char* curPath, const char* curArchivePath, int* error, int pathBufSize) override;
    BOOL WINAPI SalSplitWindowsPath(HWND parent, const char* title, const char* errorTitle, int selCount, char* path, char* secondPart, BOOL pathIsDir, BOOL backslashAtEnd, const char* dirName, const char* curDiskPath, char*& mask) override;
    BOOL WINAPI SalSplitGeneralPath(HWND parent, const char* title, const char* errorTitle, int selCount, char* path, char* afterRoot, char* secondPart, BOOL pathIsDir, BOOL backslashAtEnd, const char* dirName, const char* curPath, char*& mask, char* newDirs, SGP_IsTheSamePathF isTheSamePathF) override;
    BOOL WINAPI SalRemovePointsFromPath(char* afterRoot) override;
    BOOL WINAPI GetConfigParameter(int paramID, void* buffer, int bufferSize, int* type) override;
    void WINAPI AlterFileName(char* tgtName, char* srcName, int format, int changedParts, BOOL isDir) override;
    void WINAPI CreateSafeWaitWindow(const char* message, const char* caption, int delay, BOOL showCloseButton, HWND hForegroundWnd) override;
    void WINAPI DestroySafeWaitWindow() override;
    void WINAPI ShowSafeWaitWindow(BOOL show) override;
    BOOL WINAPI GetSafeWaitWindowClosePressed() override;
    void WINAPI SetSafeWaitWindowText(const char* message) override;
    BOOL WINAPI GetFileFromCache(const char* uniqueFileName, const char*& tmpName, HANDLE fileLock) override;
    void WINAPI UnlockFileInCache(HANDLE fileLock) override;
    BOOL WINAPI MoveFileToCache(const char* uniqueFileName, const char* nameInCache, const char* rootTmpPath, const char* newFileName, const CQuadWord& newFileSize, BOOL* alreadyExists) override;
    void WINAPI RemoveOneFileFromCache(const char* uniqueFileName) override;
    void WINAPI RemoveFilesFromCache(const char* fileNamesRoot) override;
    BOOL WINAPI EnumConversionTables(HWND parent, int* index, const char** name, const char** table) override;
    BOOL WINAPI GetConversionTable(HWND parent, char* table, const char* conversion) override;
    void WINAPI GetWindowsCodePage(HWND parent, char* codePage) override;
    void WINAPI RecognizeFileType(HWND parent, const char* pattern, int patternLen, BOOL forceText, BOOL* isText, char* codePage) override;
    BOOL WINAPI IsANSIText(const char* text, int textLen) override;
    void WINAPI CallPluginOperationFromDisk(int panel, SalPluginOperationFromDisk callback, void* param) override;
    BYTE WINAPI GetUserDefaultCharset() override;
    CSalamanderBMSearchData* WINAPI AllocSalamanderBMSearchData() override;
    void WINAPI FreeSalamanderBMSearchData(CSalamanderBMSearchData* data) override;
    CSalamanderREGEXPSearchData* WINAPI AllocSalamanderREGEXPSearchData() override;
    void WINAPI FreeSalamanderREGEXPSearchData(CSalamanderREGEXPSearchData* data) override;
    BOOL WINAPI EnumSalamanderCommands(int* index, int* salCmd, char* nameBuf, int nameBufSize, BOOL* enabled, int* type) override;
    BOOL WINAPI GetSalamanderCommand(int salCmd, char* nameBuf, int nameBufSize, BOOL* enabled, int* type) override;
    void WINAPI PostSalamanderCommand(int salCmd) override;
    void WINAPI SetUserWorkedOnPanelPath(int panel) override;
    void WINAPI StoreSelectionOnPanelPath(int panel) override;
    DWORD WINAPI UpdateCrc32(const void* buffer, DWORD count, DWORD crcVal) override;
    CSalamanderMD5* WINAPI AllocSalamanderMD5() override;
    void WINAPI FreeSalamanderMD5(CSalamanderMD5* md5) override;
    BOOL WINAPI LookForSubTexts(char* text, DWORD* varPlacements, int* varPlacementsCount) override;
    void WINAPI WaitForESCRelease() override;
    DWORD WINAPI GetMouseWheelScrollLines() override;
    HWND WINAPI GetTopVisibleParent(HWND hParent) override;
    BOOL WINAPI MultiMonGetDefaultWindowPos(HWND hByWnd, POINT* p) override;
    void WINAPI MultiMonGetClipRectByRect(const RECT* rect, RECT* workClipRect, RECT* monitorClipRect) override;
    void WINAPI MultiMonGetClipRectByWindow(HWND hByWnd, RECT* workClipRect, RECT* monitorClipRect) override;
    void WINAPI MultiMonCenterWindow(HWND hWindow, HWND hByWnd, BOOL findTopWindow) override;
    BOOL WINAPI MultiMonEnsureRectVisible(RECT* rect, BOOL partialOK) override;
    BOOL WINAPI InstallWordBreakProc(HWND hWindow) override;
    BOOL WINAPI IsFirstInstance3OrLater() override;
    int WINAPI ExpandPluralString(char* buffer, int bufferSize, const char* format, int parametersCount, const CQuadWord* parametersArray) override;
    int WINAPI ExpandPluralFilesDirs(char* buffer, int bufferSize, int files, int dirs, int mode, BOOL forDlgCaption) override;
    int WINAPI ExpandPluralBytesFilesDirs(char* buffer, int bufferSize, const CQuadWord& selectedBytes, int files, int dirs, BOOL useSubTexts) override;
    void WINAPI GetCommonFSOperSourceDescr(char* sourceDescr, int sourceDescrSize, int panel, int selectedFiles, int selectedDirs, const char* fileOrDirName, BOOL isDir, BOOL forDlgCaption) override;
    void WINAPI AddStrToStr(char* dstStr, int dstBufSize, const char* srcStr) override;
    BOOL WINAPI SalIsValidFileNameComponent(const char* fileNameComponent) override;
    void WINAPI SalMakeValidFileNameComponent(char* fileNameComponent) override;
    BOOL WINAPI IsFileEnumSourcePanel(int srcUID, int* panel) override;
    BOOL WINAPI GetNextFileNameForViewer(int srcUID, int* lastFileIndex, const char* lastFileName, BOOL preferSelected, BOOL onlyAssociatedExtensions, char* fileName, BOOL* noMoreFiles, BOOL* srcBusy) override;
    BOOL WINAPI GetPreviousFileNameForViewer(int srcUID, int* lastFileIndex, const char* lastFileName, BOOL preferSelected, BOOL onlyAssociatedExtensions, char* fileName, BOOL* noMoreFiles, BOOL* srcBusy) override;
    BOOL WINAPI IsFileNameForViewerSelected(int srcUID, int lastFileIndex, const char* lastFileName, BOOL* isFileSelected, BOOL* srcBusy) override;
    BOOL WINAPI SetSelectionOnFileNameForViewer(int srcUID, int lastFileIndex, const char* lastFileName, BOOL select, BOOL* srcBusy) override;
    BOOL WINAPI GetStdHistoryValues(int historyID, char*** historyArr, int* historyItemsCount) override;
    void WINAPI AddValueToStdHistoryValues(char** historyArr, int historyItemsCount, const char* value, BOOL caseSensitiveValue) override;
    void WINAPI LoadComboFromStdHistoryValues(HWND combo, char** historyArr, int historyItemsCount) override;
    BOOL WINAPI CanUse256ColorsBitmap() override;
    HWND WINAPI GetWndToFlash(HWND parent) override;
    void WINAPI ActivateDropTarget(HWND dropTarget, HWND progressWnd) override;
    void WINAPI PostOpenPackDlgForThisPlugin(int delFilesAfterPacking) override;
    void WINAPI PostOpenUnpackDlgForThisPlugin(const char* unpackMask) override;
    HANDLE WINAPI SalCreateFileEx(const char* fileName, DWORD desiredAccess, DWORD shareMode, DWORD flagsAndAttributes, DWORD* err) override;
    BOOL WINAPI SalCreateDirectoryEx(const char* name, DWORD* err) override;
    void WINAPI PanelStopMonitoring(int panel, BOOL stopMonitoring) override;
    CSalamanderDirectoryAbstract* WINAPI AllocSalamanderDirectory(BOOL isForFS) override;
    void WINAPI FreeSalamanderDirectory(CSalamanderDirectoryAbstract* salDir) override;
    BOOL WINAPI AddPluginFSTimer(int timeout, CPluginFSInterfaceAbstract* timerOwner, DWORD timerParam) override;
    int WINAPI KillPluginFSTimer(CPluginFSInterfaceAbstract* timerOwner, BOOL allTimers, DWORD timerParam) override;
    BOOL WINAPI GetChangeDriveMenuItemVisibility() override;
    void WINAPI SetChangeDriveMenuItemVisibility(BOOL visible) override;
    void WINAPI OleSpySetBreak(int alloc) override;
    HICON WINAPI GetSalamanderIcon(int icon, int iconSize) override;
    BOOL WINAPI GetFileIcon(const char* path, BOOL pathIsPIDL, HICON* hIcon, int iconSize, BOOL fallbackToDefIcon, BOOL defIconIsDir) override;
    BOOL WINAPI FileExists(const char* fileName) override;
    void WINAPI DisconnectFSFromPanel(HWND parent, int panel) override;
    BOOL WINAPI IsArchiveHandledByThisPlugin(const char* name) override;
    DWORD WINAPI GetIconLRFlags() override;
    int WINAPI IsFileLink(const char* fileExtension) override;
    DWORD WINAPI GetImageListColorFlags() override;
    BOOL WINAPI SafeGetOpenFileName(LPOPENFILENAME lpofn) override;
    BOOL WINAPI SafeGetSaveFileName(LPOPENFILENAME lpofn) override;
    void WINAPI SetHelpFileName(const char* chmName) override;
    BOOL WINAPI OpenHtmlHelp(HWND parent, CHtmlHelpCommand command, DWORD_PTR dwData, BOOL quiet) override;
    BOOL WINAPI PathsAreOnTheSameVolume(const char* path1, const char* path2, BOOL* resIsOnlyEstimation) override;
    void* WINAPI Realloc(void* ptr, int size) override;
    void WINAPI GetPanelEnumFilesParams(int panel, int* enumFilesSourceUID, int* enumFilesCurrentIndex) override;
    BOOL WINAPI PostRefreshPanelFS2(CPluginFSInterfaceAbstract* modifiedFS, BOOL focusFirstNewItem = FALSE) override;
    char* WINAPI LoadStr(HINSTANCE module, int resID) override;
    WCHAR* WINAPI LoadStrW(HINSTANCE module, int resID) override;
    BOOL WINAPI ChangePanelPathToRescuePathOrFixedDrive(int panel, int* failReason = NULL) override;
    void WINAPI SetPluginIsNethood() override;
    void WINAPI OpenNetworkContextMenu(HWND parent, int panel, BOOL forItems, int menuX, int menuY, const char* netPath, char* newlyMappedDrive) override;
    BOOL WINAPI DuplicateBackslashes(char* buffer, int bufferSize) override;
    int WINAPI StartThrobber(int panel, const char* tooltip, int delay) override;
    BOOL WINAPI StopThrobber(int id) override;
    void WINAPI ShowSecurityIcon(int panel, BOOL showIcon, BOOL isLocked, const char* tooltip) override;
    void WINAPI RemoveCurrentPathFromHistory(int panel) override;
    BOOL WINAPI IsUserAdmin() override;
    BOOL WINAPI IsRemoteSession() override;
    DWORD WINAPI SalWNetAddConnection2Interactive(LPNETRESOURCE lpNetResource) override;
    DWORD WINAPI GetMouseWheelScrollChars() override;
    CSalamanderZLIBAbstract* WINAPI GetSalamanderZLIB() override;
    CSalamanderPNGAbstract* WINAPI GetSalamanderPNG() override;
    CSalamanderCryptAbstract* WINAPI GetSalamanderCrypt() override;
    void WINAPI SetPluginUsesPasswordManager() override;
    CSalamanderPasswordManagerAbstract* WINAPI GetSalamanderPasswordManager() override;
    BOOL WINAPI OpenHtmlHelpForSalamander(HWND parent, CHtmlHelpCommand command, DWORD_PTR dwData, BOOL quiet) override;
    CSalamanderBZIP2Abstract* WINAPI GetSalamanderBZIP2() override;
    void WINAPI GetFocusedItemMenuPos(POINT* pos) override;
    void WINAPI LockMainWindow(BOOL lock, HWND hToolWnd, const char* lockReason) override;
    void WINAPI PostPluginMenuChanged() override;
    BOOL WINAPI GetMenuItemHotKey(int id, WORD* hotKey, char* hotKeyText, int hotKeyTextSize) override;
    LONG WINAPI SalRegQueryValue(HKEY hKey, LPCSTR lpSubKey, LPSTR lpData, PLONG lpcbData) override;
    LONG WINAPI SalRegQueryValueEx(HKEY hKey, LPCSTR lpValueName, LPDWORD lpReserved, LPDWORD lpType, LPBYTE lpData, LPDWORD lpcbData) override;
    DWORD WINAPI SalGetFileAttributes(const char* fileName) override;
    BOOL WINAPI IsPathOnSSD(const char* path) override;
    BOOL WINAPI IsUNCPath(const char* path) override;
    BOOL WINAPI ResolveSubsts(char* resPath) override;
    void WINAPI ResolveLocalPathWithReparsePoints(char* resPath, const char* path, BOOL* cutResPathIsPossible, BOOL* rootOrCurReparsePointSet, char* rootOrCurReparsePoint, char* junctionOrSymlinkTgt, int* linkType, char* netPath) override;
    BOOL WINAPI GetResolvedPathMountPointAndGUID(const char* path, char* mountPoint, char* guidPath) override;
    BOOL WINAPI PointToLocalDecimalSeparator(char* buffer, int bufferSize) override;
    void WINAPI SetPluginIconOverlays(int iconOverlaysCount, HICON* iconOverlays) override;
    BOOL WINAPI SalGetFileSize2(const char* fileName, CQuadWord& size, DWORD* err) override;
    BOOL WINAPI GetLinkTgtFileSize(HWND parent, const char* fileName, CQuadWord* size, BOOL* cancel, BOOL* ignoreAll) override;
    BOOL WINAPI DeleteDirLink(const char* name, DWORD* err) override;
    BOOL WINAPI ClearReadOnlyAttr(const char* name, DWORD attr = -1) override;
    BOOL WINAPI IsCriticalShutdown() override;
    void WINAPI CloseAllOwnedEnabledDialogs(HWND parent, DWORD tid = 0) override;
    BOOL WINAPI GetThemeInfo(CSalamanderThemeInfo* info) override;

private:
    class CState;

    static CState* CreateState();
    static void DestroyState(CState* state);
    using CStateOwner = std::unique_ptr<CState, void (*)(CState*)>;

    ::CSalamanderGeneralAbstract& WideGeneral;
    CLegacyPluginDataResolver& PluginDataResolver;
    CLegacyPluginFSResolver& PluginFSResolver;
    int BuiltForVersion;
    CStateOwner State;
};

} // namespace sally::compat
