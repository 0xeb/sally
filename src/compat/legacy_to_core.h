// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

// legacy_to_core — v107 plugin-facing facades over the wide core.
//
// This header intentionally includes the frozen and live SDKs in one
// translation unit. Implementations must not include precomp.h; see sdk107.h.

#pragma once

#include <cstdarg>
#include <memory>
#include <string>
#include <vector>

#include "compat/sdk107.h"

#include "plugins/shared/spl_com.h"
#include "plugins/shared/spl_base.h"
#include "plugins/shared/spl_bzip2.h"
#include "plugins/shared/spl_crypt.h"
#include "plugins/shared/spl_file.h"
#include "plugins/shared/spl_gen.h"
#include "plugins/shared/spl_gui.h"
#include "plugins/shared/spl_menu.h"
#include "plugins/shared/spl_zlib.h"

#include "compat/legacy_general_to_core.h"

namespace sally::compat
{

struct CLegacyEntryFacadeSet
{
    sdk107::CSalamanderDebugAbstract* Debug = nullptr;
    sdk107::CSalamanderGeneralAbstract* General = nullptr;
    sdk107::CSalamanderGUIAbstract* GUI = nullptr;
    sdk107::CSalamanderSafeFileAbstract* SafeFile = nullptr;
};

// Directory callbacks receive a frozen plugin-data pointer even though the
// wide core must see the stable reverse wrapper owned by the per-plugin host.
// Directory is callback-bound and cannot own that reverse graph, so the host
// supplies this identity resolver. A non-null input must resolve; otherwise the
// facade refuses the mutation rather than pass a generation-mismatched pointer.
class CLegacyPluginDataResolver
{
public:
    virtual ~CLegacyPluginDataResolver() = default;
    virtual ::CPluginDataInterfaceAbstract* Resolve(
        sdk107::CPluginDataInterfaceAbstract* legacy) = 0;
    virtual sdk107::CPluginDataInterfaceAbstract* FindLegacy(
        const ::CPluginDataInterfaceAbstract* wide) const = 0;
};

// General accepts and returns long-lived plugin-filesystem objects. The
// per-plugin reverse owner supplies both identity directions; unknown pointers
// must be refused rather than crossing the generation boundary by cast.
class CLegacyPluginFSResolver
{
public:
    virtual ~CLegacyPluginFSResolver() = default;
    virtual ::CPluginFSInterfaceAbstract* Resolve(
        sdk107::CPluginFSInterfaceAbstract* legacy) = 0;
    virtual sdk107::CPluginFSInterfaceAbstract* FindLegacy(
        const ::CPluginFSInterfaceAbstract* wide) const = 0;
};

// Panel get-item methods return live CFileData pointers that must later cross
// back through SelectPanelItem/SetPanelFocusedItem by identity. This owner keeps
// an exact-narrowed frozen mirror at a stable address and remembers the panel
// that made the live pointer valid. Refused refreshes retire stale mirrors.
class CLegacyPanelRowOwner final
{
public:
    CLegacyPanelRowOwner();
    ~CLegacyPanelRowOwner();

    const sdk107::CFileData* Publish(const ::CFileData* wide, int panel);
    const ::CFileData* Resolve(const sdk107::CFileData* legacy,
                               int panel) const;

private:
    class CState;
    std::unique_ptr<CState> State;
};

// A frozen cache path must remain valid until UnlockFileInCache, while one
// frozen event may protect several acquired files. Each live acquisition gets
// its own proxy event so an exact-projection refusal can release only that
// acquisition; the stable byte mirrors retire together by frozen event.
bool PrepareLegacyCacheName(const wchar_t* wideName, std::string& name);

class CLegacyCacheNameOwner final
{
public:
    CLegacyCacheNameOwner();
    ~CLegacyCacheNameOwner();

    const char* Publish(HANDLE legacyLock, HANDLE wideLock,
                        std::string name);
    std::vector<HANDLE> Retire(HANDLE legacyLock);

private:
    class CState;
    std::unique_ptr<CState> State;
};

// EnumConversionTables promises that every returned name remains valid for
// the complete runtime. The live owner already provides stable WCHAR
// pointers; this owner attaches one never-retired exact-ACP mirror to each of
// those identities. It is shared by every call through one General facade and
// is safe for the method's any-thread contract.
class CLegacyConversionTableNameOwner final
{
public:
    CLegacyConversionTableNameOwner();
    ~CLegacyConversionTableNameOwner();

    const char* Publish(const wchar_t* wideName);

private:
    class CState;
    std::unique_ptr<CState> State;
};

// Connect and BuildMenu transfer ownership of icon lists created by the GUI
// facade. Those callbacks do not own that facade, so this narrow interface
// retires a frozen wrapper and returns the live object exactly once.
class CLegacyGUIIconListTransfer
{
public:
    virtual ~CLegacyGUIIconListTransfer() = default;
    virtual ::CGUIIconListAbstract* TakeIconList(
        sdk107::CGUIIconListAbstract* legacy) = 0;
};

// Complete 11-slot frozen Directory callback over the native wide object.
// AddFile/AddDir perform the CFileData ownership transfer; read accessors own
// stable exact-narrowed mirrors and refuse unrepresentable names.
class CLegacySalamanderDirectory final : public sdk107::CSalamanderDirectoryAbstract
{
public:
    CLegacySalamanderDirectory(::CSalamanderDirectoryAbstract& wideDirectory,
                               CLegacyPluginDataResolver& pluginDataResolver,
                               int builtForVersion);
    ~CLegacySalamanderDirectory();

    void WINAPI Clear(sdk107::CPluginDataInterfaceAbstract* pluginData) override;
    void WINAPI SetValidData(DWORD validData) override;
    void WINAPI SetFlags(DWORD flags) override;
    BOOL WINAPI AddFile(const char* path, sdk107::CFileData& file,
                        sdk107::CPluginDataInterfaceAbstract* pluginData) override;
    BOOL WINAPI AddDir(const char* path, sdk107::CFileData& dir,
                       sdk107::CPluginDataInterfaceAbstract* pluginData) override;
    int WINAPI GetFilesCount() const override;
    int WINAPI GetDirsCount() const override;
    const sdk107::CFileData* WINAPI GetFile(int index) const override;
    const sdk107::CFileData* WINAPI GetDir(int index) const override;
    const sdk107::CSalamanderDirectoryAbstract* WINAPI GetSalDir(int index) const override;
    void WINAPI SetApproximateCount(int files, int dirs) override;

private:
    class CState;

    ::CPluginDataInterfaceAbstract* ResolvePluginData(
        sdk107::CPluginDataInterfaceAbstract* pluginData) const;
    BOOL Add(bool isDir, const char* path, sdk107::CFileData& row,
             sdk107::CPluginDataInterfaceAbstract* pluginData);

    ::CSalamanderDirectoryAbstract& WideDirectory;
    CLegacyPluginDataResolver& PluginDataResolver;
    bool TrustNameW;
    std::unique_ptr<CState> State;
};

// General allocates five independently-lived object types. Namespace-distinct
// frozen pointers must never be cast to their live peers, even where the
// byte-oriented vtables happen to match. These wrappers plus the owner below
// preserve identity until the matching General Free slot retires the object.
class CLegacySalamanderMaskGroup final : public sdk107::CSalamanderMaskGroup
{
public:
    explicit CLegacySalamanderMaskGroup(::CSalamanderMaskGroup& wideMaskGroup);

    void WINAPI SetMasksString(const char* masks, BOOL extendedMode) override;
    void WINAPI GetMasksString(char* buffer) override;
    BOOL WINAPI GetExtendedMode() override;
    BOOL WINAPI PrepareMasks(int& errorPos) override;
    BOOL WINAPI AgreeMasks(const char* fileName, const char* fileExt) override;

private:
    ::CSalamanderMaskGroup& WideMaskGroup;
    std::string LegacyMasks;
};

class CLegacySalamanderBMSearchData final : public sdk107::CSalamanderBMSearchData
{
public:
    explicit CLegacySalamanderBMSearchData(::CSalamanderBMSearchData& wideSearchData);

    void WINAPI Set(const char* pattern, WORD flags) override;
    void WINAPI Set(const char* pattern, const int length, WORD flags) override;
    void WINAPI SetFlags(WORD flags) override;
    int WINAPI GetLength() const override;
    const char* WINAPI GetPattern() const override;
    BOOL WINAPI IsGood() const override;
    int WINAPI SearchForward(const char* text, int length, int start) override;
    int WINAPI SearchBackward(const char* text, int length) override;

private:
    ::CSalamanderBMSearchData& WideSearchData;
};

class CLegacySalamanderREGEXPSearchData final : public sdk107::CSalamanderREGEXPSearchData
{
public:
    explicit CLegacySalamanderREGEXPSearchData(::CSalamanderREGEXPSearchData& wideSearchData);

    BOOL WINAPI Set(const char* pattern, WORD flags) override;
    BOOL WINAPI SetFlags(WORD flags) override;
    const char* WINAPI GetLastErrorText() const override;
    const char* WINAPI GetPattern() const override;
    BOOL WINAPI SetLine(const char* start, const char* end) override;
    int WINAPI SearchForward(int start, int& foundLen) override;
    int WINAPI SearchBackward(int length, int& foundLen) override;

private:
    ::CSalamanderREGEXPSearchData& WideSearchData;
};

class CLegacySalamanderMD5 final : public sdk107::CSalamanderMD5
{
public:
    explicit CLegacySalamanderMD5(::CSalamanderMD5& wideMD5);

    void WINAPI Init() override;
    void WINAPI Update(const void* input, DWORD inputLength) override;
    void WINAPI Finalize() override;
    void WINAPI GetDigest(void* dest) override;

private:
    ::CSalamanderMD5& WideMD5;
};

class CLegacyGeneralObjectOwner final
{
public:
    CLegacyGeneralObjectOwner();
    ~CLegacyGeneralObjectOwner();

    sdk107::CSalamanderMaskGroup* PublishMaskGroup(::CSalamanderMaskGroup* wide);
    ::CSalamanderMaskGroup* FindMaskGroup(sdk107::CSalamanderMaskGroup* legacy) const;
    ::CSalamanderMaskGroup* RetireMaskGroup(sdk107::CSalamanderMaskGroup* legacy);

    sdk107::CSalamanderBMSearchData* PublishBMSearchData(::CSalamanderBMSearchData* wide);
    ::CSalamanderBMSearchData* FindBMSearchData(sdk107::CSalamanderBMSearchData* legacy) const;
    ::CSalamanderBMSearchData* RetireBMSearchData(sdk107::CSalamanderBMSearchData* legacy);

    sdk107::CSalamanderREGEXPSearchData* PublishREGEXPSearchData(::CSalamanderREGEXPSearchData* wide);
    ::CSalamanderREGEXPSearchData* FindREGEXPSearchData(sdk107::CSalamanderREGEXPSearchData* legacy) const;
    ::CSalamanderREGEXPSearchData* RetireREGEXPSearchData(sdk107::CSalamanderREGEXPSearchData* legacy);

    sdk107::CSalamanderMD5* PublishMD5(::CSalamanderMD5* wide);
    ::CSalamanderMD5* FindMD5(sdk107::CSalamanderMD5* legacy) const;
    ::CSalamanderMD5* RetireMD5(sdk107::CSalamanderMD5* legacy);

    sdk107::CSalamanderDirectoryAbstract* PublishDirectory(::CSalamanderDirectoryAbstract* wide,
                                                           CLegacyPluginDataResolver& pluginDataResolver,
                                                           int builtForVersion);
    ::CSalamanderDirectoryAbstract* FindDirectory(sdk107::CSalamanderDirectoryAbstract* legacy) const;
    ::CSalamanderDirectoryAbstract* RetireDirectory(sdk107::CSalamanderDirectoryAbstract* legacy);

private:
    class CState;
    std::unique_ptr<CState> State;
};

// Complete 15-slot frozen Connect callback over the native wide object. Text
// is widened at this boundary. SetIconListForGUI consumes only a wrapper owned
// by the associated GUI facade and refuses foreign or already-retired objects.
class CLegacySalamanderConnect final : public sdk107::CSalamanderConnectAbstract
{
public:
    CLegacySalamanderConnect(::CSalamanderConnectAbstract& wideConnect,
                             CLegacyGUIIconListTransfer& iconListTransfer);

    void WINAPI AddCustomPacker(const char* title, const char* defaultExtension,
                                BOOL update) override;
    void WINAPI AddCustomUnpacker(const char* title, const char* masks,
                                  BOOL update) override;
    void WINAPI AddPanelArchiver(const char* extensions, BOOL edit,
                                 BOOL updateExts) override;
    void WINAPI ForceRemovePanelArchiver(const char* extension) override;
    void WINAPI AddViewer(const char* masks, BOOL force) override;
    void WINAPI ForceRemoveViewer(const char* mask) override;
    void WINAPI AddMenuItem(int iconIndex, const char* name, DWORD hotKey,
                            int id, BOOL callGetState, DWORD stateOr,
                            DWORD stateAnd, DWORD skillLevel) override;
    void WINAPI AddSubmenuStart(int iconIndex, const char* name, int id,
                                BOOL callGetState, DWORD stateOr,
                                DWORD stateAnd, DWORD skillLevel) override;
    void WINAPI AddSubmenuEnd() override;
    void WINAPI SetChangeDriveMenuItem(const char* title, int iconIndex) override;
    void WINAPI SetThumbnailLoader(const char* masks) override;
    void WINAPI SetBitmapWithIcons(HBITMAP bitmap) override;
    void WINAPI SetPluginIcon(int iconIndex) override;
    void WINAPI SetPluginMenuAndToolbarIcon(int iconIndex) override;
    void WINAPI SetIconListForGUI(sdk107::CGUIIconListAbstract* iconList) override;

private:
    ::CSalamanderConnectAbstract& WideConnect;
    CLegacyGUIIconListTransfer& IconListTransfer;
};

// Complete four-slot frozen BuildMenu callback over the native wide object.
// Text widens at the callback boundary. SetIconListForMenu consumes only a GUI
// facade wrapper owned by the associated host and refuses foreign wrappers.
class CLegacySalamanderBuildMenu final
    : public sdk107::CSalamanderBuildMenuAbstract
{
public:
    CLegacySalamanderBuildMenu(
        ::CSalamanderBuildMenuAbstract& wideBuildMenu,
        CLegacyGUIIconListTransfer& iconListTransfer);

    void WINAPI AddMenuItem(int iconIndex, const char* name, DWORD hotKey,
                            int id, BOOL callGetState, DWORD stateOr,
                            DWORD stateAnd, DWORD skillLevel) override;
    void WINAPI AddSubmenuStart(int iconIndex, const char* name, int id,
                                BOOL callGetState, DWORD stateOr,
                                DWORD stateAnd, DWORD skillLevel) override;
    void WINAPI AddSubmenuEnd() override;
    void WINAPI SetIconListForMenu(
        sdk107::CGUIIconListAbstract* iconList) override;

private:
    ::CSalamanderBuildMenuAbstract& WideBuildMenu;
    CLegacyGUIIconListTransfer& IconListTransfer;
};

// Complete nine-slot frozen ForOperations callback over the native wide
// object. The facade is valid only for the enclosing plugin callback. Text and
// paths widen at entry; namespace-distinct CQuadWord values copy by halves.
class CLegacySalamanderForOperations final
    : public sdk107::CSalamanderForOperationsAbstract
{
public:
    explicit CLegacySalamanderForOperations(
        ::CSalamanderForOperationsAbstract& wideOperations);

    void WINAPI OpenProgressDialog(const char* title, BOOL twoProgressBars,
                                   HWND parent, BOOL fileProgress) override;
    void WINAPI ProgressDialogAddText(const char* txt,
                                      BOOL delayedPaint) override;
    void WINAPI ProgressSetTotalSize(
        const sdk107::CQuadWord& totalSize1,
        const sdk107::CQuadWord& totalSize2) override;
    BOOL WINAPI ProgressSetSize(const sdk107::CQuadWord& size1,
                                const sdk107::CQuadWord& size2,
                                BOOL delayedPaint) override;
    BOOL WINAPI ProgressAddSize(int size, BOOL delayedPaint) override;
    void WINAPI ProgressEnableCancel(BOOL enable) override;
    HWND WINAPI ProgressGetHWND() override;
    void WINAPI CloseProgressDialog() override;
    BOOL WINAPI MoveFiles(const char* source, const char* target,
                          const char* remapNameFrom,
                          const char* remapNameTo) override;

private:
    ::CSalamanderForOperationsAbstract& WideOperations;
};

class CLegacyViewTransferState;

// Lifetime-stable transfer storage owned by the future reverse plugin-data
// wrapper. A View facade itself is stack/callback-bound, but the pointers a
// v107 plugin obtains from GetTransferVariables and the callbacks installed in
// live columns survive SetupView, so they belong to this separate owner.
class CLegacyViewTransfer final
{
public:
    CLegacyViewTransfer(::CPluginDataInterfaceAbstract* wideOwner,
                        sdk107::CPluginDataInterfaceAbstract* legacyOwner);
    ~CLegacyViewTransfer();

    CLegacyViewTransfer(const CLegacyViewTransfer&) = delete;
    CLegacyViewTransfer& operator=(const CLegacyViewTransfer&) = delete;

    const sdk107::CColumn* MirrorColumnForPlugin(
        const ::CColumn* column);

private:
    friend class CLegacySalamanderView;
    std::shared_ptr<CLegacyViewTransferState> State;
};

// Complete ten-slot frozen View callback over the native wide object. Column
// metadata crosses field-by-field, while plugin text/icon callbacks run through
// CLegacyViewTransfer's stable scratch after this stack-bound facade is gone.
class CLegacySalamanderView final : public sdk107::CSalamanderViewAbstract
{
public:
    CLegacySalamanderView(::CSalamanderViewAbstract& wideView,
                          CLegacyViewTransfer& transfer);

    DWORD WINAPI GetViewMode() override;
    void WINAPI SetViewMode(DWORD viewMode, DWORD validData) override;
    void WINAPI GetTransferVariables(
        const sdk107::CFileData**& transferFileData, int*& transferIsDir,
        char*& transferBuffer, int*& transferLen, DWORD*& transferRowData,
        sdk107::CPluginDataInterfaceAbstract**& transferPluginDataIface,
        DWORD*& transferActCustomData) override;
    void WINAPI SetPluginSimpleIconCallback(
        sdk107::FGetPluginIconIndex callback) override;
    int WINAPI GetColumnsCount() override;
    const sdk107::CColumn* WINAPI GetColumn(int index) override;
    BOOL WINAPI InsertColumn(int index,
                             const sdk107::CColumn* column) override;
    BOOL WINAPI InsertStandardColumn(int index, DWORD id) override;
    BOOL WINAPI SetColumnName(int index, const char* name,
                              const char* description) override;
    BOOL WINAPI DeleteColumn(int index) override;

private:
    ::CSalamanderViewAbstract& WideView;
    std::shared_ptr<CLegacyViewTransferState> Transfer;
};

// Frozen diagnostic facade. The A/W twin slots stay distinct, but ANSI text is
// widened before it reaches the live object. Push also translates printf's
// implicit narrow/wide character conversions while preserving the va_list.
class CLegacySalamanderDebug final : public sdk107::CSalamanderDebugAbstract
{
public:
    explicit CLegacySalamanderDebug(::CSalamanderDebugAbstract& wideDebug);

    void WINAPI TraceI(const char* file, int line, const char* str) override;
    void WINAPI TraceIW(const WCHAR* file, int line, const WCHAR* str) override;
    void WINAPI TraceE(const char* file, int line, const char* str) override;
    void WINAPI TraceEW(const WCHAR* file, int line, const WCHAR* str) override;
    void WINAPI TraceAttachThread(HANDLE thread, unsigned tid) override;
    void WINAPI TraceSetThreadName(const char* name) override;
    void WINAPI TraceSetThreadNameW(const WCHAR* name) override;
    unsigned WINAPI CallWithCallStack(unsigned(WINAPI* threadBody)(void*), void* param) override;
    void WINAPI Push(const char* format, va_list args,
                     sdk107::CCallStackMsgContext* callStackMsgContext,
                     BOOL doNotMeasureTimes) override;
    void WINAPI Pop(sdk107::CCallStackMsgContext* callStackMsgContext) override;
    void WINAPI SetThreadNameInVC(const char* name) override;
    void WINAPI SetThreadNameInVCAndTrace(const char* name) override;
    void WINAPI TraceConnectToServer() override;
    void WINAPI AddModuleWithPossibleMemoryLeaks(const char* fileName) override;

private:
    ::CSalamanderDebugAbstract& WideDebug;
};

// Frozen safe-file facade. A legacy SAFE_FILE cannot be reinterpreted as the
// live structure: its FileName pointer owns ANSI bytes while the live pointer
// owns UTF-16. Each open context therefore has a live shadow, while the frozen
// structure receives synchronized scalar fields and a stable ANSI file name.
class CLegacySalamanderSafeFile final : public sdk107::CSalamanderSafeFileAbstract
{
public:
    explicit CLegacySalamanderSafeFile(::CSalamanderSafeFileAbstract& wideSafeFile);
    ~CLegacySalamanderSafeFile();

    BOOL WINAPI SafeFileOpen(sdk107::SAFE_FILE* file, const char* fileName,
                             DWORD dwDesiredAccess, DWORD dwShareMode,
                             DWORD dwCreationDisposition, DWORD dwFlagsAndAttributes,
                             HWND hParent, DWORD flags, DWORD* pressedButton,
                             DWORD* silentMask) override;
    HANDLE WINAPI SafeFileCreate(const char* fileName, DWORD dwDesiredAccess,
                                 DWORD dwShareMode, DWORD dwFlagsAndAttributes,
                                 BOOL isDir, HWND hParent, const char* srcFileName,
                                 const char* srcFileInfo, DWORD* silentMask,
                                 BOOL allowSkip, BOOL* skipped, char* skipPath,
                                 int skipPathMax, sdk107::CQuadWord* allocateWholeFile,
                                 sdk107::SAFE_FILE* file) override;
    void WINAPI SafeFileClose(sdk107::SAFE_FILE* file) override;
    BOOL WINAPI SafeFileSeek(sdk107::SAFE_FILE* file,
                             sdk107::CQuadWord* distance, DWORD moveMethod,
                             DWORD* error) override;
    BOOL WINAPI SafeFileSeekMsg(sdk107::SAFE_FILE* file,
                                sdk107::CQuadWord* distance, DWORD moveMethod,
                                HWND hParent, DWORD flags, DWORD* pressedButton,
                                DWORD* silentMask, BOOL seekForRead) override;
    BOOL WINAPI SafeFileGetSize(sdk107::SAFE_FILE* file,
                                sdk107::CQuadWord* fileSize, DWORD* error) override;
    BOOL WINAPI SafeFileRead(sdk107::SAFE_FILE* file, LPVOID lpBuffer,
                             DWORD nNumberOfBytesToRead, LPDWORD lpNumberOfBytesRead,
                             HWND hParent, DWORD flags, DWORD* pressedButton,
                             DWORD* silentMask) override;
    BOOL WINAPI SafeFileWrite(sdk107::SAFE_FILE* file, LPVOID lpBuffer,
                              DWORD nNumberOfBytesToWrite, LPDWORD lpNumberOfBytesWritten,
                              HWND hParent, DWORD flags, DWORD* pressedButton,
                              DWORD* silentMask) override;

private:
    class CState;

    ::CSalamanderSafeFileAbstract& WideSafeFile;
    std::unique_ptr<CState> State;
};

// Frozen registry callback facade. Names and string writes widen on entry.
// String reads are transactional and exact-or-refuse: an unrepresentable live
// value never reaches a legacy buffer as substituted or best-fit bytes.
class CLegacySalamanderRegistry final : public sdk107::CSalamanderRegistryAbstract
{
public:
    explicit CLegacySalamanderRegistry(::CSalamanderRegistryAbstract& wideRegistry);

    BOOL WINAPI ClearKey(HKEY key) override;
    BOOL WINAPI CreateKey(HKEY key, const char* name, HKEY& createdKey) override;
    BOOL WINAPI OpenKey(HKEY key, const char* name, HKEY& openedKey) override;
    void WINAPI CloseKey(HKEY key) override;
    BOOL WINAPI DeleteKey(HKEY key, const char* name) override;
    BOOL WINAPI GetValue(HKEY key, const char* name, DWORD type,
                         void* buffer, DWORD bufferSize) override;
    BOOL WINAPI SetValue(HKEY key, const char* name, DWORD type,
                         const void* data, DWORD dataSize) override;
    BOOL WINAPI DeleteValue(HKEY key, const char* name) override;
    BOOL WINAPI GetSize(HKEY key, const char* name, DWORD type,
                        DWORD& bufferSize) override;

private:
    ::CSalamanderRegistryAbstract& WideRegistry;
};

// These utility interfaces are lifetime-stable children of General. ZLIB,
// BZIP2, and Crypt retain the exact v107 byte ABI, so their context storage is
// layout-pinned and passed by identity. Password-manager secrets also remain
// byte-owned. PNG is the one text boundary: named resources widen while
// integer resources keep their Win32 pointer encoding.
class CLegacySalamanderZLIB final : public sdk107::CSalamanderZLIBAbstract
{
public:
    explicit CLegacySalamanderZLIB(::CSalamanderZLIBAbstract& wideZLIB);

    int WINAPI DeflateInit(sdk107::CSalZLIB* zlibInfo, int compressLevel) override;
    int WINAPI Deflate(sdk107::CSalZLIB* zlibInfo, int flush) override;
    int WINAPI DeflateEnd(sdk107::CSalZLIB* zlibInfo) override;
    int WINAPI InflateInit(sdk107::CSalZLIB* zlibInfo) override;
    int WINAPI Inflate(sdk107::CSalZLIB* zlibInfo, int flush) override;
    int WINAPI InflateEnd(sdk107::CSalZLIB* zlibInfo) override;
    int WINAPI InflateInit2(sdk107::CSalZLIB* zlibInfo, int windowBits) override;

private:
    ::CSalamanderZLIBAbstract& WideZLIB;
};

class CLegacySalamanderBZIP2 final : public sdk107::CSalamanderBZIP2Abstract
{
public:
    explicit CLegacySalamanderBZIP2(::CSalamanderBZIP2Abstract& wideBZIP2);

    int WINAPI CompressInit(sdk107::CSalBZIP2* bzip2Info, int blockSize100k,
                            int workFactor) override;
    int WINAPI Compress(sdk107::CSalBZIP2* bzip2Info, int action) override;
    int WINAPI CompressEnd(sdk107::CSalBZIP2* bzip2Info) override;
    int WINAPI DecompressInit(sdk107::CSalBZIP2* bzip2Info,
                              BOOL conserveMemory) override;
    int WINAPI Decompress(sdk107::CSalBZIP2* bzip2Info) override;
    int WINAPI DecompressEnd(sdk107::CSalBZIP2* bzip2Info) override;

private:
    ::CSalamanderBZIP2Abstract& WideBZIP2;
};

class CLegacySalamanderCrypt final : public sdk107::CSalamanderCryptAbstract
{
public:
    explicit CLegacySalamanderCrypt(::CSalamanderCryptAbstract& wideCrypt);

    int WINAPI AESInit(sdk107::CSalAES* aes, int mode, LPCSTR password,
                       size_t passwordLength, LPBYTE salt,
                       LPWORD passwordVerifier) override;
    void WINAPI AESEncrypt(sdk107::CSalAES* aes, LPVOID data,
                           size_t dataLength) override;
    void WINAPI AESDecrypt(sdk107::CSalAES* aes, LPVOID data,
                           size_t dataLength) override;
    void WINAPI AESEnd(sdk107::CSalAES* aes, LPBYTE mac,
                       LPDWORD macLength) override;
    void WINAPI SHA1Init(sdk107::CSalSHA1* sha1) override;
    void WINAPI SHA1Update(sdk107::CSalSHA1* sha1, const LPBYTE data,
                           size_t dataLength) override;
    void WINAPI SHA1Final(sdk107::CSalSHA1* sha1, BYTE digest[20]) override;

private:
    ::CSalamanderCryptAbstract& WideCrypt;
};

class CLegacySalamanderPNG final : public sdk107::CSalamanderPNGAbstract
{
public:
    explicit CLegacySalamanderPNG(::CSalamanderPNGAbstract& widePNG);

    HBITMAP WINAPI LoadPNGBitmap(HINSTANCE instance, sdk107::LPCTSTR bitmapName,
                                 DWORD flags, COLORREF unused) override;
    HBITMAP WINAPI LoadRawPNGBitmap(const void* rawPNG, DWORD rawPNGSize,
                                    DWORD flags, COLORREF unused) override;

private:
    ::CSalamanderPNGAbstract& WidePNG;
};

class CLegacySalamanderPasswordManager final
    : public sdk107::CSalamanderPasswordManagerAbstract
{
public:
    explicit CLegacySalamanderPasswordManager(
        ::CSalamanderPasswordManagerAbstract& widePasswordManager);

    BOOL WINAPI IsUsingMasterPassword() override;
    BOOL WINAPI IsMasterPasswordSet() override;
    BOOL WINAPI AskForMasterPassword(HWND parent) override;
    BOOL WINAPI EncryptPassword(const char* plainPassword,
                                BYTE** encryptedPassword,
                                int* encryptedPasswordSize,
                                BOOL encrypt) override;
    BOOL WINAPI DecryptPassword(const BYTE* encryptedPassword,
                                int encryptedPasswordSize,
                                char** plainPassword) override;
    BOOL WINAPI IsPasswordEncrypted(const BYTE* encryptedPassword,
                                    int encryptedPasswordSize) override;

private:
    ::CSalamanderPasswordManagerAbstract& WidePasswordManager;
};

// Frozen GUI facade and owner of its complete child-interface graph. Every
// returned live child is represented by a stable frozen wrapper. Notification
// windows used by menus/toolbars are proxied because those messages carry
// interface pointers or structures whose text fields changed width in v108.
class CLegacySalamanderGUI final : public sdk107::CSalamanderGUIAbstract,
                                  public CLegacyGUIIconListTransfer
{
public:
    explicit CLegacySalamanderGUI(::CSalamanderGUIAbstract& wideGUI);
    ~CLegacySalamanderGUI();

    sdk107::CGUIProgressBarAbstract* WINAPI AttachProgressBar(HWND hParent, int ctrlID) override;
    sdk107::CGUIStaticTextAbstract* WINAPI AttachStaticText(HWND hParent, int ctrlID, DWORD flags) override;
    sdk107::CGUIHyperLinkAbstract* WINAPI AttachHyperLink(HWND hParent, int ctrlID, DWORD flags) override;
    sdk107::CGUIButtonAbstract* WINAPI AttachButton(HWND hParent, int ctrlID, DWORD flags) override;
    sdk107::CGUIColorArrowButtonAbstract* WINAPI AttachColorArrowButton(HWND hParent, int ctrlID, BOOL showArrow) override;
    BOOL WINAPI ChangeToArrowButton(HWND hParent, int ctrlID) override;
    sdk107::CGUIMenuPopupAbstract* WINAPI CreateMenuPopup() override;
    BOOL WINAPI DestroyMenuPopup(sdk107::CGUIMenuPopupAbstract* popup) override;
    sdk107::CGUIMenuBarAbstract* WINAPI CreateMenuBar(sdk107::CGUIMenuPopupAbstract* menu, HWND hNotifyWindow) override;
    BOOL WINAPI DestroyMenuBar(sdk107::CGUIMenuBarAbstract* menuBar) override;
    BOOL WINAPI CreateGrayscaleAndMaskBitmaps(HBITMAP hSource, COLORREF transparent,
                                              HBITMAP& hGrayscale, HBITMAP& hMask) override;
    sdk107::CGUIToolBarAbstract* WINAPI CreateToolBar(HWND hNotifyWindow) override;
    BOOL WINAPI DestroyToolBar(sdk107::CGUIToolBarAbstract* toolBar) override;
    void WINAPI SetCurrentToolTip(HWND hNotifyWindow, DWORD id) override;
    void WINAPI SuppressToolTipOnCurrentMousePos() override;
    BOOL WINAPI DisableWindowVisualStyles(HWND hWindow) override;
    sdk107::CGUIIconListAbstract* WINAPI CreateIconList() override;
    BOOL WINAPI DestroyIconList(sdk107::CGUIIconListAbstract* iconList) override;
    ::CGUIIconListAbstract* TakeIconList(
        sdk107::CGUIIconListAbstract* iconList) override;
    void WINAPI PrepareToolTipText(char* buf, BOOL stripHotKey) override;
    void WINAPI SetSubjectTruncatedText(HWND subjectWnd, const char* subjectFormatString,
                                        const char* fileName, BOOL isDir,
                                        BOOL duplicateAmpersands) override;
    sdk107::CGUIToolbarHeaderAbstract* WINAPI AttachToolbarHeader(HWND hParent, int ctrlID,
                                                                  HWND hAlignWindow,
                                                                  DWORD buttonMask) override;
    void WINAPI ArrangeHorizontalLines(HWND hWindow) override;
    int WINAPI GetWindowFontHeight(HWND hWindow) override;

private:
    class CState;

    ::CSalamanderGUIAbstract& WideGUI;
    std::unique_ptr<CState> State;
};

// The first plugin->core facade. It implements the frozen v107 entry vtable and
// delegates to the live wide entry object. All ANSI text is widened at this one
// boundary; interface getters return the legacy facades owned by the host.
class CLegacySalamanderPluginEntry final : public sdk107::CSalamanderPluginEntryAbstract
{
public:
    CLegacySalamanderPluginEntry(::CSalamanderPluginEntryAbstract& wideEntry,
                                 const CLegacyEntryFacadeSet& facades);

    int WINAPI GetVersion() override;
    HWND WINAPI GetParentWindow() override;
    sdk107::CSalamanderDebugAbstract* WINAPI GetSalamanderDebug() override;
    BOOL WINAPI SetBasicPluginData(const char* pluginName, DWORD functions,
                                   const char* version, const char* copyright,
                                   const char* description, const char* regKeyName = nullptr,
                                   const char* extensions = nullptr, const char* fsName = nullptr) override;
    sdk107::CSalamanderGeneralAbstract* WINAPI GetSalamanderGeneral() override;
    DWORD WINAPI GetLoadInformation() override;
    HINSTANCE WINAPI LoadLanguageModule(HWND parent, const char* pluginName) override;
    WORD WINAPI GetCurrentSalamanderLanguageID() override;
    sdk107::CSalamanderGUIAbstract* WINAPI GetSalamanderGUI() override;
    sdk107::CSalamanderSafeFileAbstract* WINAPI GetSalamanderSafeFile() override;
    void WINAPI SetPluginHomePageURL(const char* url) override;
    BOOL WINAPI AddFSName(const char* fsName, int* newFSNameIndex) override;

private:
    ::CSalamanderPluginEntryAbstract& WideEntry;
    CLegacyEntryFacadeSet Facades;
};

} // namespace sally::compat
