// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

// core_to_legacy — live wide wrappers over frozen v107 plugin interfaces.

#pragma once

#include <cstddef>
#include <memory>
#include <unordered_map>

#include "compat/legacy_to_core.h"
#include "plugins/shared/spl_arc.h"
#include "plugins/shared/spl_fs.h"
#include "plugins/shared/spl_gen.h"
#include "plugins/shared/spl_thum.h"
#include "plugins/shared/spl_view.h"

namespace sdk107
{

    // The frozen interface makes its slots private inside Sally and grants this
    // historical encapsulation name friendship. This compat-only definition is the
    // single legal call path into a v107 plugin-data implementation.
    class CPluginDataInterfaceEncapsulation
    {
    public:
        explicit CPluginDataInterfaceEncapsulation(
            CPluginDataInterfaceAbstract& legacy);

        BOOL CallReleaseForFiles();
        BOOL CallReleaseForDirs();
        void ReleasePluginData(CFileData& file, BOOL isDir);
        void GetFileDataForUpDir(const char* archivePath, CFileData& upDir);
        BOOL GetFileDataForNewDir(const char* dirName, CFileData& dir);
        HIMAGELIST GetSimplePluginIcons(int iconSize);
        BOOL HasSimplePluginIcon(CFileData& file, BOOL isDir);
        HICON GetPluginIcon(const CFileData* file, int iconSize,
                            BOOL& destroyIcon);
        int CompareFilesFromFS(const CFileData* file1, const CFileData* file2);
        void SetupView(BOOL leftPanel, CSalamanderViewAbstract* view,
                       const char* archivePath, const CFileData* upperDir);
        void ColumnFixedWidthShouldChange(BOOL leftPanel, const CColumn* column,
                                          int newFixedWidth);
        void ColumnWidthWasChanged(BOOL leftPanel, const CColumn* column,
                                   int newWidth);
        BOOL GetInfoLineContent(int panel, const CFileData* file, BOOL isDir,
                                int selectedFiles, int selectedDirs,
                                BOOL displaySize, const CQuadWord& selectedSize,
                                char* buffer, DWORD* hotTexts,
                                int& hotTextsCount);
        BOOL CanBeCopiedToClipboard();
        BOOL GetByteSize(const CFileData* file, BOOL isDir, CQuadWord* size);
        BOOL GetLastWriteDate(const CFileData* file, BOOL isDir,
                              SYSTEMTIME* date);
        BOOL GetLastWriteTime(const CFileData* file, BOOL isDir,
                              SYSTEMTIME* time);

    private:
        CPluginDataInterfaceAbstract& Legacy;
    };

    // Historical friend proxies are the only legal Sally-side callers of the
    // frozen top-level and Archiver vtables when INSIDE_SALAMANDER is defined.
    class CPluginInterfaceEncapsulation
    {
    public:
        explicit CPluginInterfaceEncapsulation(CPluginInterfaceAbstract& legacy);

        void About(HWND parent);
        BOOL Release(HWND parent, BOOL force);
        void LoadConfiguration(HWND parent, HKEY regKey,
                               CSalamanderRegistryAbstract* registry);
        void SaveConfiguration(HWND parent, HKEY regKey,
                               CSalamanderRegistryAbstract* registry);
        void Configuration(HWND parent);
        void Connect(HWND parent, CSalamanderConnectAbstract* salamander);
        void ReleasePluginDataInterface(
            CPluginDataInterfaceAbstract* pluginData);
        CPluginInterfaceForArchiverAbstract* GetInterfaceForArchiver();
        CPluginInterfaceForViewerAbstract* GetInterfaceForViewer();
        CPluginInterfaceForMenuExtAbstract* GetInterfaceForMenuExt();
        CPluginInterfaceForFSAbstract* GetInterfaceForFS();
        CPluginInterfaceForThumbLoaderAbstract* GetInterfaceForThumbLoader();
        void Event(int event, DWORD param);
        void ClearHistory(HWND parent);
        void AcceptChangeOnPathNotification(const char* path,
                                            BOOL includingSubdirs);
        void PasswordManagerEvent(HWND parent, int event);

    private:
        CPluginInterfaceAbstract& Legacy;
    };

    class CPluginInterfaceForArchiverEncapsulation
    {
    public:
        explicit CPluginInterfaceForArchiverEncapsulation(
            CPluginInterfaceForArchiverAbstract& legacy);

        BOOL ListArchive(CSalamanderForOperationsAbstract* salamander,
                         const char* fileName,
                         CSalamanderDirectoryAbstract* directory,
                         CPluginDataInterfaceAbstract*& pluginData);
        BOOL UnpackArchive(CSalamanderForOperationsAbstract* salamander,
                           const char* fileName,
                           CPluginDataInterfaceAbstract* pluginData,
                           const char* targetDir, const char* archiveRoot,
                           SalEnumSelection next, void* nextParam);
        BOOL UnpackOneFile(CSalamanderForOperationsAbstract* salamander,
                           const char* fileName,
                           CPluginDataInterfaceAbstract* pluginData,
                           const char* nameInArchive,
                           const CFileData* fileData, const char* targetDir,
                           const char* newFileName,
                           BOOL* renamingNotSupported);
        BOOL PackToArchive(CSalamanderForOperationsAbstract* salamander,
                           const char* fileName, const char* archiveRoot,
                           BOOL move, const char* sourcePath,
                           SalEnumSelection2 next, void* nextParam);
        BOOL DeleteFromArchive(CSalamanderForOperationsAbstract* salamander,
                               const char* fileName,
                               CPluginDataInterfaceAbstract* pluginData,
                               const char* archiveRoot, SalEnumSelection next,
                               void* nextParam);
        BOOL UnpackWholeArchive(CSalamanderForOperationsAbstract* salamander,
                                const char* fileName, const char* mask,
                                const char* targetDir, BOOL delArchiveWhenDone,
                                CDynamicString* archiveVolumes);
        BOOL CanCloseArchive(CSalamanderForOperationsAbstract* salamander,
                             const char* fileName, BOOL force, int panel);
        BOOL GetCacheInfo(char* tempPath, BOOL* ownDelete, BOOL* cacheCopies);
        void DeleteTmpCopy(const char* fileName, BOOL firstFile);
        BOOL PrematureDeleteTmpCopy(HWND parent, int copiesCount);

    private:
        CPluginInterfaceForArchiverAbstract& Legacy;
    };

    class CPluginInterfaceForViewerEncapsulation
    {
    public:
        explicit CPluginInterfaceForViewerEncapsulation(
            CPluginInterfaceForViewerAbstract& legacy);

        BOOL ViewFile(const char* name, int left, int top, int width,
                      int height, UINT showCmd, BOOL alwaysOnTop,
                      BOOL returnLock, HANDLE* lock, BOOL* lockOwner,
                      CSalamanderPluginViewerData* viewerData,
                      int enumFilesSourceUID, int enumFilesCurrentIndex);
        BOOL CanViewFile(const char* name);

    private:
        CPluginInterfaceForViewerAbstract& Legacy;
    };

    class CPluginInterfaceForMenuExtEncapsulation
    {
    public:
        explicit CPluginInterfaceForMenuExtEncapsulation(
            CPluginInterfaceForMenuExtAbstract& legacy);

        DWORD GetMenuItemState(int id, DWORD eventMask);
        BOOL ExecuteMenuItem(CSalamanderForOperationsAbstract* salamander,
                             HWND parent, int id, DWORD eventMask);
        BOOL HelpForMenuItem(HWND parent, int id);
        void BuildMenu(HWND parent, CSalamanderBuildMenuAbstract* salamander);

    private:
        CPluginInterfaceForMenuExtAbstract& Legacy;
    };

    class CPluginInterfaceForThumbLoaderEncapsulation
    {
    public:
        explicit CPluginInterfaceForThumbLoaderEncapsulation(
            CPluginInterfaceForThumbLoaderAbstract& legacy);

        BOOL LoadThumbnail(const char* filename, int thumbWidth,
                           int thumbHeight,
                           CSalamanderThumbnailMakerAbstract* thumbMaker,
                           BOOL fastThumbnail);

    private:
        CPluginInterfaceForThumbLoaderAbstract& Legacy;
    };

    class CPluginInterfaceForFSEncapsulation
    {
    public:
        explicit CPluginInterfaceForFSEncapsulation(
            CPluginInterfaceForFSAbstract& legacy);

        CPluginFSInterfaceAbstract* OpenFS(const char* fsName,
                                           int fsNameIndex);
        void CloseFS(CPluginFSInterfaceAbstract* fs);
        void ExecuteChangeDriveMenuItem(int panel);
        BOOL ChangeDriveMenuItemContextMenu(
            HWND parent, int panel, int x, int y,
            CPluginFSInterfaceAbstract* pluginFS, const char* pluginFSName,
            int pluginFSNameIndex, BOOL isDetachedFS, BOOL& refreshMenu,
            BOOL& closeMenu, int& postCmd, void*& postCmdParam);
        void ExecuteChangeDrivePostCommand(int panel, int postCmd,
                                           void* postCmdParam);
        void ExecuteOnFS(int panel, CPluginFSInterfaceAbstract* pluginFS,
                         const char* pluginFSName, int pluginFSNameIndex,
                         CFileData& file, int isDir);
        BOOL DisconnectFS(HWND parent, BOOL isInPanel, int panel,
                          CPluginFSInterfaceAbstract* pluginFS,
                          const char* pluginFSName, int pluginFSNameIndex);
        void ConvertPathToInternal(const char* fsName, int fsNameIndex,
                                   char* fsUserPart);
        void ConvertPathToExternal(const char* fsName, int fsNameIndex,
                                   char* fsUserPart);
        void EnsureShareExistsOnServer(int panel, const char* server,
                                       const char* share);

    private:
        CPluginInterfaceForFSAbstract& Legacy;
    };

    class CPluginFSInterfaceEncapsulation
    {
    public:
        explicit CPluginFSInterfaceEncapsulation(
            CPluginFSInterfaceAbstract& legacy);

        BOOL GetCurrentPath(char* userPart);
        BOOL GetFullName(CFileData& file, int isDir, char* buf, int bufSize);
        BOOL GetFullFSPath(HWND parent, const char* fsName, char* path,
                           int pathSize, BOOL& success);
        BOOL GetRootPath(char* userPart);
        BOOL IsCurrentPath(int currentFSNameIndex, int fsNameIndex,
                           const char* userPart);
        BOOL IsOurPath(int currentFSNameIndex, int fsNameIndex,
                       const char* userPart);
        BOOL ChangePath(int currentFSNameIndex, char* fsName, int fsNameIndex,
                        const char* userPart, char* cutFileName,
                        BOOL* pathWasCut, BOOL forceRefresh, int mode);
        BOOL ListCurrentPath(CSalamanderDirectoryAbstract* dir,
                             CPluginDataInterfaceAbstract*& pluginData,
                             int& iconsType, BOOL forceRefresh);
        BOOL TryCloseOrDetach(BOOL forceClose, BOOL canDetach, BOOL& detach,
                              int reason);
        void Event(int event, DWORD param);
        void ReleaseObject(HWND parent);
        DWORD GetSupportedServices();
        BOOL GetChangeDriveOrDisconnectItem(const char* fsName, char*& title,
                                            HICON& icon,
                                            BOOL& destroyIcon);
        HICON GetFSIcon(BOOL& destroyIcon);
        void GetDropEffect(const char* srcFSPath, const char* tgtFSPath,
                           DWORD allowedEffects, DWORD keyState,
                           DWORD* dropEffect);
        void GetFSFreeSpace(CQuadWord* retValue);
        BOOL GetNextDirectoryLineHotPath(const char* text, int pathLen,
                                         int& offset);
        void CompleteDirectoryLineHotPath(char* path, int pathBufSize);
        BOOL GetPathForMainWindowTitle(const char* fsName, int mode, char* buf,
                                       int bufSize);
        void ShowInfoDialog(const char* fsName, HWND parent);
        BOOL ExecuteCommandLine(HWND parent, char* command, int& selFrom,
                                int& selTo);
        BOOL QuickRename(const char* fsName, int mode, HWND parent,
                         CFileData& file, BOOL isDir, char* newName,
                         BOOL& cancel);
        void AcceptChangeOnPathNotification(const char* fsName,
                                            const char* path,
                                            BOOL includingSubdirs);
        BOOL CreateDir(const char* fsName, int mode, HWND parent,
                       char* newName, BOOL& cancel);
        void ViewFile(const char* fsName, HWND parent,
                      CSalamanderForViewFileOnFSAbstract* salamander,
                      CFileData& file);
        BOOL Delete(const char* fsName, int mode, HWND parent, int panel,
                    int selectedFiles, int selectedDirs,
                    BOOL& cancelOrError);
        BOOL CopyOrMoveFromFS(BOOL copy, int mode, const char* fsName,
                              HWND parent, int panel, int selectedFiles,
                              int selectedDirs, char* targetPath,
                              BOOL& operationMask, BOOL& cancelOrHandlePath,
                              HWND dropTarget);
        BOOL CopyOrMoveFromDiskToFS(
            BOOL copy, int mode, const char* fsName, HWND parent,
            const char* sourcePath, SalEnumSelection2 next, void* nextParam,
            int sourceFiles, int sourceDirs, char* targetPath,
            BOOL* invalidPathOrCancel);
        BOOL ChangeAttributes(const char* fsName, HWND parent, int panel,
                              int selectedFiles, int selectedDirs);
        void ShowProperties(const char* fsName, HWND parent, int panel,
                            int selectedFiles, int selectedDirs);
        void ContextMenu(const char* fsName, HWND parent, int menuX, int menuY,
                         int type, int panel, int selectedFiles,
                         int selectedDirs);
        BOOL HandleMenuMsg(UINT uMsg, WPARAM wParam, LPARAM lParam,
                           LRESULT* plResult);
        BOOL OpenFindDialog(const char* fsName, int panel);
        void OpenActiveFolder(const char* fsName, HWND parent);
        void GetAllowedDropEffects(int mode, const char* tgtFSPath,
                                   DWORD* allowedEffects);
        BOOL GetNoItemsInPanelText(char* textBuf, int textBufSize);
        void ShowSecurityInfo(HWND parent);
        BOOL GetCurrentPathW(wchar_t* userPart, int userPartSize);
        BOOL GetFullNameW(CFileData& file, int isDir, wchar_t* buf,
                          int bufSize);
        BOOL GetFullFSPathW(HWND parent, const wchar_t* fsName, wchar_t* path,
                            int pathSize, BOOL& success);
        BOOL GetRootPathW(wchar_t* userPart, int userPartSize);
        BOOL IsCurrentPathW(int currentFSNameIndex, int fsNameIndex,
                            const wchar_t* userPart);
        BOOL IsOurPathW(int currentFSNameIndex, int fsNameIndex,
                        const wchar_t* userPart);
        BOOL ChangePathW(int currentFSNameIndex, wchar_t* fsName,
                         int fsNameIndex, const wchar_t* userPart,
                         wchar_t* cutFileName, int cutFileNameSize,
                         BOOL* pathWasCut, BOOL forceRefresh, int mode);

    private:
        CPluginFSInterfaceAbstract& Legacy;
    };

} // namespace sdk107

namespace sally::compat
{

    namespace detail
    {
        bool RemapPluginInfoHotTexts(UINT codePage, const char* text,
                                     std::size_t textLength, DWORD* hotTexts,
                                     int count);
    }

    // Complete 17-slot live plugin-data interface around a frozen v107 plugin
    // object. Rows and columns are exact-or-refuse; display text widens back into
    // native storage. View callback state stays stable for this wrapper's lifetime.
    class CLegacyPluginDataInterface final
        : public ::CPluginDataInterfaceAbstract
    {
    public:
        explicit CLegacyPluginDataInterface(
            sdk107::CPluginDataInterfaceAbstract& legacy);

        sdk107::CPluginDataInterfaceAbstract* LegacyInterface() const;

        BOOL WINAPI CallReleaseForFiles() override;
        BOOL WINAPI CallReleaseForDirs() override;
        void WINAPI ReleasePluginData(::CFileData& file, BOOL isDir) override;
        void WINAPI GetFileDataForUpDir(const wchar_t* archivePath,
                                        ::CFileData& upDir) override;
        BOOL WINAPI GetFileDataForNewDir(const wchar_t* dirName,
                                         ::CFileData& dir) override;
        HIMAGELIST WINAPI GetSimplePluginIcons(int iconSize) override;
        BOOL WINAPI HasSimplePluginIcon(::CFileData& file, BOOL isDir) override;
        HICON WINAPI GetPluginIcon(const ::CFileData* file, int iconSize,
                                   BOOL& destroyIcon) override;
        int WINAPI CompareFilesFromFS(const ::CFileData* file1,
                                      const ::CFileData* file2) override;
        void WINAPI SetupView(BOOL leftPanel, ::CSalamanderViewAbstract* view,
                              const wchar_t* archivePath,
                              const ::CFileData* upperDir) override;
        void WINAPI ColumnFixedWidthShouldChange(
            BOOL leftPanel, const ::CColumn* column,
            int newFixedWidth) override;
        void WINAPI ColumnWidthWasChanged(BOOL leftPanel,
                                          const ::CColumn* column,
                                          int newWidth) override;
        BOOL WINAPI GetInfoLineContent(
            int panel, const ::CFileData* file, BOOL isDir, int selectedFiles,
            int selectedDirs, BOOL displaySize, const ::CQuadWord& selectedSize,
            CSalamanderStringBuffer* buffer,
            CSalamanderTextRangeBuffer* hotTexts) override;
        BOOL WINAPI CanBeCopiedToClipboard() override;
        BOOL WINAPI GetByteSize(const ::CFileData* file, BOOL isDir,
                                ::CQuadWord* size) override;
        BOOL WINAPI GetLastWriteDate(const ::CFileData* file, BOOL isDir,
                                     SYSTEMTIME* date) override;
        BOOL WINAPI GetLastWriteTime(const ::CFileData* file, BOOL isDir,
                                     SYSTEMTIME* time) override;

    private:
        sdk107::CPluginDataInterfaceAbstract& FrozenInterface;
        sdk107::CPluginDataInterfaceEncapsulation Legacy;
        CLegacyViewTransfer ViewTransfer;
    };

    // Per-plugin stable reverse-identity map. It owns wrappers, never the plugin's
    // frozen objects. Retire removes only the wrapper and returns the frozen object
    // so the enclosing plugin-interface adapter can release it.
    class CLegacyPluginDataOwner final : public CLegacyPluginDataResolver
    {
    public:
        ::CPluginDataInterfaceAbstract* Resolve(
            sdk107::CPluginDataInterfaceAbstract* legacy) override;
        sdk107::CPluginDataInterfaceAbstract* FindLegacy(
            const ::CPluginDataInterfaceAbstract* wide) const override;
        sdk107::CPluginDataInterfaceAbstract* Retire(
            ::CPluginDataInterfaceAbstract* wide);

    private:
        std::unordered_map<
            sdk107::CPluginDataInterfaceAbstract*,
            std::unique_ptr<CLegacyPluginDataInterface>>
            Wrappers;
        std::unordered_map<const ::CPluginDataInterfaceAbstract*,
                           sdk107::CPluginDataInterfaceAbstract*>
            LegacyByWrapper;
    };

    // The top-level vtable returns five separately-owned child interfaces. The
    // reverse host supplies their stable live identities as each child tranche
    // is completed; the top-level wrapper never substitutes NULL for a non-NULL
    // frozen child.
    class CLegacyPluginChildResolver
    {
    public:
        virtual ~CLegacyPluginChildResolver() = default;
        virtual ::CPluginInterfaceForArchiverAbstract* ResolveArchiver(
            sdk107::CPluginInterfaceForArchiverAbstract* legacy) = 0;
        virtual ::CPluginInterfaceForViewerAbstract* ResolveViewer(
            sdk107::CPluginInterfaceForViewerAbstract* legacy) = 0;
        virtual ::CPluginInterfaceForMenuExtAbstract* ResolveMenuExt(
            sdk107::CPluginInterfaceForMenuExtAbstract* legacy) = 0;
        virtual ::CPluginInterfaceForFSAbstract* ResolveFS(
            sdk107::CPluginInterfaceForFSAbstract* legacy) = 0;
        virtual ::CPluginInterfaceForThumbLoaderAbstract* ResolveThumbLoader(
            sdk107::CPluginInterfaceForThumbLoaderAbstract* legacy) = 0;
    };

    // Complete ten-slot live Archiver interface around the frozen v107 object.
    // Paths and enumeration names are exact-or-refuse; produced plugin-data is
    // published only after a successful listing and through the stable owner.
    class CLegacyPluginInterfaceForArchiver final
        : public ::CPluginInterfaceForArchiverAbstract
    {
    public:
        CLegacyPluginInterfaceForArchiver(
            sdk107::CPluginInterfaceForArchiverAbstract& legacy,
            CLegacyPluginDataOwner& pluginDataOwner, int builtForVersion);

        BOOL WINAPI ListArchive(
            ::CSalamanderForOperationsAbstract* salamander,
            const wchar_t* fileName, ::CSalamanderDirectoryAbstract* directory,
            ::CPluginDataInterfaceAbstract*& pluginData) override;
        BOOL WINAPI UnpackArchive(
            ::CSalamanderForOperationsAbstract* salamander,
            const wchar_t* fileName,
            ::CPluginDataInterfaceAbstract* pluginData,
            const wchar_t* targetDir, const wchar_t* archiveRoot,
            ::SalEnumSelection next, void* nextParam) override;
        BOOL WINAPI UnpackOneFile(
            ::CSalamanderForOperationsAbstract* salamander,
            const wchar_t* fileName,
            ::CPluginDataInterfaceAbstract* pluginData,
            const wchar_t* nameInArchive, const ::CFileData* fileData,
            const wchar_t* targetDir, const wchar_t* newFileName,
            BOOL* renamingNotSupported) override;
        BOOL WINAPI PackToArchive(
            ::CSalamanderForOperationsAbstract* salamander,
            const wchar_t* fileName, const wchar_t* archiveRoot, BOOL move,
            const wchar_t* sourcePath, ::SalEnumSelection2 next,
            void* nextParam) override;
        BOOL WINAPI DeleteFromArchive(
            ::CSalamanderForOperationsAbstract* salamander,
            const wchar_t* fileName,
            ::CPluginDataInterfaceAbstract* pluginData,
            const wchar_t* archiveRoot, ::SalEnumSelection next,
            void* nextParam) override;
        BOOL WINAPI UnpackWholeArchive(
            ::CSalamanderForOperationsAbstract* salamander,
            const wchar_t* fileName, const wchar_t* mask,
            const wchar_t* targetDir, BOOL delArchiveWhenDone,
            ::CDynamicString* archiveVolumes) override;
        BOOL WINAPI CanCloseArchive(
            ::CSalamanderForOperationsAbstract* salamander,
            const wchar_t* fileName, BOOL force, int panel) override;
        BOOL WINAPI GetCacheInfo(CSalamanderStringBuffer* tempPath, BOOL* ownDelete,
                                 BOOL* cacheCopies) override;
        void WINAPI DeleteTmpCopy(const wchar_t* fileName,
                                  BOOL firstFile) override;
        BOOL WINAPI PrematureDeleteTmpCopy(HWND parent,
                                           int copiesCount) override;

    private:
        sdk107::CPluginDataInterfaceAbstract* ResolvePluginData(
            const ::CPluginDataInterfaceAbstract* pluginData) const;

        sdk107::CPluginInterfaceForArchiverAbstract& FrozenInterface;
        sdk107::CPluginInterfaceForArchiverEncapsulation Legacy;
        CLegacyPluginDataOwner& PluginDataOwner;
        int BuiltForVersion;
    };

    // Complete two-slot live Viewer interface around the frozen v107 object.
    // The primary file name is exact-or-refuse. ViewerData is an extensible
    // packed SDK record whose byte ABI did not change, so its identity and any
    // plugin-defined tail remain intact across this boundary.
    class CLegacyPluginInterfaceForViewer final
        : public ::CPluginInterfaceForViewerAbstract
    {
    public:
        explicit CLegacyPluginInterfaceForViewer(
            sdk107::CPluginInterfaceForViewerAbstract& legacy);

        BOOL WINAPI ViewFile(
            const wchar_t* name, int left, int top, int width, int height,
            UINT showCmd, BOOL alwaysOnTop, BOOL returnLock, HANDLE* lock,
            BOOL* lockOwner, ::CSalamanderPluginViewerData* viewerData,
            int enumFilesSourceUID, int enumFilesCurrentIndex) override;
        BOOL WINAPI CanViewFile(const wchar_t* name) override;

    private:
        sdk107::CPluginInterfaceForViewerEncapsulation Legacy;
    };

    // Complete four-slot live MenuExt interface around the frozen v107 object.
    // The operations and dynamic-menu callback facades are stack-bound to the
    // plugin call, matching the lifetime promised by both SDK generations.
    class CLegacyPluginInterfaceForMenuExt final
        : public ::CPluginInterfaceForMenuExtAbstract
    {
    public:
        CLegacyPluginInterfaceForMenuExt(
            sdk107::CPluginInterfaceForMenuExtAbstract& legacy,
            CLegacyGUIIconListTransfer& iconListTransfer);

        DWORD WINAPI GetMenuItemState(int id, DWORD eventMask) override;
        BOOL WINAPI ExecuteMenuItem(
            ::CSalamanderForOperationsAbstract* salamander, HWND parent,
            int id, DWORD eventMask) override;
        BOOL WINAPI HelpForMenuItem(HWND parent, int id) override;
        void WINAPI BuildMenu(
            HWND parent,
            ::CSalamanderBuildMenuAbstract* salamander) override;

    private:
        sdk107::CPluginInterfaceForMenuExtEncapsulation Legacy;
        CLegacyGUIIconListTransfer& IconListTransfer;
    };

    // Call-bound frozen callback facade used only while a legacy thumbnail
    // loader is active. All five slots are width-neutral and preserve the live
    // thumbnail maker's buffer and return-value contracts.
    class CLegacySalamanderThumbnailMaker final
        : public sdk107::CSalamanderThumbnailMakerAbstract
    {
    public:
        explicit CLegacySalamanderThumbnailMaker(
            ::CSalamanderThumbnailMakerAbstract& wideMaker);

        BOOL WINAPI SetParameters(int picWidth, int picHeight,
                                  DWORD flags) override;
        BOOL WINAPI ProcessBuffer(void* buffer, int rowsCount) override;
        void* WINAPI GetBuffer(int rowsCount) override;
        void WINAPI SetError() override;
        BOOL WINAPI GetCancelProcessing() override;

    private:
        ::CSalamanderThumbnailMakerAbstract& WideMaker;
    };

    // Complete one-slot live ThumbLoader interface around the frozen v107
    // object. File names exact-narrow or refuse before plugin code; the maker
    // callback facade is scoped to the one LoadThumbnail call.
    class CLegacyPluginInterfaceForThumbLoader final
        : public ::CPluginInterfaceForThumbLoaderAbstract
    {
    public:
        explicit CLegacyPluginInterfaceForThumbLoader(
            sdk107::CPluginInterfaceForThumbLoaderAbstract& legacy);

        BOOL WINAPI LoadThumbnail(
            const wchar_t* filename, int thumbWidth, int thumbHeight,
            ::CSalamanderThumbnailMakerAbstract* thumbMaker,
            BOOL fastThumbnail) override;

    private:
        sdk107::CPluginInterfaceForThumbLoaderEncapsulation Legacy;
    };

    // Complete per-open FS object. The live v108 slots are dynamically owned
    // UTF-16. Only this named adapter may project them onto the frozen v107
    // ANSI/W fixed-buffer slots; all other textual slots cross the same
    // exact-or-refuse gate.
    class CLegacyPluginFSInterface final : public ::CPluginFSInterfaceAbstract
    {
    public:
        CLegacyPluginFSInterface(
            sdk107::CPluginFSInterfaceAbstract& legacy,
            CLegacyPluginDataOwner& pluginDataOwner, int builtForVersion);

        sdk107::CPluginFSInterfaceAbstract* LegacyInterface() const;

        BOOL WINAPI GetCurrentPath(
            ::CSalamanderStringBuffer* userPart) override;
        BOOL WINAPI GetFullName(
            ::CFileData& file, int isDir,
            ::CSalamanderStringBuffer* fullName) override;
        BOOL WINAPI GetFullFSPath(
            HWND parent, const wchar_t* fsName,
            ::CSalamanderStringBuffer* path, BOOL& success) override;
        BOOL WINAPI GetRootPath(
            ::CSalamanderStringBuffer* userPart) override;
        BOOL WINAPI IsCurrentPath(int currentFSNameIndex, int fsNameIndex,
                                  const wchar_t* userPart) override;
        BOOL WINAPI IsOurPath(int currentFSNameIndex, int fsNameIndex,
                              const wchar_t* userPart) override;
        BOOL WINAPI ChangePath(
            int currentFSNameIndex, ::CSalamanderStringBuffer* fsName,
            int fsNameIndex, const wchar_t* userPart,
            ::CSalamanderStringBuffer* cutFileName, BOOL* pathWasCut,
                               BOOL forceRefresh, int mode) override;
        BOOL WINAPI ListCurrentPath(
            ::CSalamanderDirectoryAbstract* dir,
            ::CPluginDataInterfaceAbstract*& pluginData, int& iconsType,
            BOOL forceRefresh) override;
        BOOL WINAPI TryCloseOrDetach(BOOL forceClose, BOOL canDetach,
                                     BOOL& detach, int reason) override;
        void WINAPI Event(int event, DWORD param) override;
        void WINAPI ReleaseObject(HWND parent) override;
        DWORD WINAPI GetSupportedServices() override;
        BOOL WINAPI GetChangeDriveOrDisconnectItem(
            const wchar_t* fsName, wchar_t*& title, HICON& icon,
            BOOL& destroyIcon) override;
        HICON WINAPI GetFSIcon(BOOL& destroyIcon) override;
        void WINAPI GetDropEffect(const wchar_t* srcFSPath,
                                  const wchar_t* tgtFSPath,
                                  DWORD allowedEffects, DWORD keyState,
                                  DWORD* dropEffect) override;
        void WINAPI GetFSFreeSpace(::CQuadWord* retValue) override;
        BOOL WINAPI GetNextDirectoryLineHotPath(
            const wchar_t* text, int pathLen, int& offset) override;
        BOOL WINAPI CompleteDirectoryLineHotPath(
            CSalamanderStringBuffer* path) override;
        BOOL WINAPI GetPathForMainWindowTitle(
            const wchar_t* fsName, int mode,
            CSalamanderStringBuffer* buf) override;
        void WINAPI ShowInfoDialog(const wchar_t* fsName,
                                   HWND parent) override;
        BOOL WINAPI ExecuteCommandLine(HWND parent, CSalamanderStringBuffer* command,
                                       int& selFrom, int& selTo) override;
        BOOL WINAPI QuickRename(const wchar_t* fsName, int mode, HWND parent,
                                ::CFileData& file, BOOL isDir,
                                CSalamanderStringBuffer* newName, BOOL& cancel) override;
        void WINAPI AcceptChangeOnPathNotification(
            const wchar_t* fsName, const wchar_t* path,
            BOOL includingSubdirs) override;
        BOOL WINAPI CreateDir(const wchar_t* fsName, int mode, HWND parent,
                              CSalamanderStringBuffer* newName, BOOL& cancel) override;
        void WINAPI ViewFile(
            const wchar_t* fsName, HWND parent,
            ::CSalamanderForViewFileOnFSAbstract* salamander,
            ::CFileData& file) override;
        BOOL WINAPI Delete(const wchar_t* fsName, int mode, HWND parent,
                           int panel, int selectedFiles, int selectedDirs,
                           BOOL& cancelOrError) override;
        BOOL WINAPI CopyOrMoveFromFS(
            BOOL copy, int mode, const wchar_t* fsName, HWND parent, int panel,
            int selectedFiles, int selectedDirs, CSalamanderStringBuffer* targetPath,
            BOOL& operationMask, BOOL& cancelOrHandlePath,
            HWND dropTarget) override;
        BOOL WINAPI CopyOrMoveFromDiskToFS(
            BOOL copy, int mode, const wchar_t* fsName, HWND parent,
            const wchar_t* sourcePath, ::SalEnumSelection2 next,
            void* nextParam, int sourceFiles, int sourceDirs,
            CSalamanderStringBuffer* targetPath, BOOL* invalidPathOrCancel) override;
        BOOL WINAPI ChangeAttributes(const wchar_t* fsName, HWND parent,
                                     int panel, int selectedFiles,
                                     int selectedDirs) override;
        void WINAPI ShowProperties(const wchar_t* fsName, HWND parent,
                                   int panel, int selectedFiles,
                                   int selectedDirs) override;
        void WINAPI ContextMenu(const wchar_t* fsName, HWND parent, int menuX,
                                int menuY, int type, int panel,
                                int selectedFiles,
                                int selectedDirs) override;
        BOOL WINAPI HandleMenuMsg(UINT uMsg, WPARAM wParam, LPARAM lParam,
                                  LRESULT* plResult) override;
        BOOL WINAPI OpenFindDialog(const wchar_t* fsName,
                                   int panel) override;
        void WINAPI OpenActiveFolder(const wchar_t* fsName,
                                     HWND parent) override;
        void WINAPI GetAllowedDropEffects(int mode, const wchar_t* tgtFSPath,
                                          DWORD* allowedEffects) override;
        BOOL WINAPI GetNoItemsInPanelText(
            CSalamanderStringBuffer* textBuf) override;
        void WINAPI ShowSecurityInfo(HWND parent) override;
    private:
        sdk107::CPluginFSInterfaceAbstract& FrozenInterface;
        sdk107::CPluginFSInterfaceEncapsulation Legacy;
        CLegacyPluginDataOwner& PluginDataOwner;
        int BuiltForVersion;
    };

    // Stable bidirectional identity for long-lived objects returned by OpenFS.
    class CLegacyPluginFSOwner final : public CLegacyPluginFSResolver
    {
    public:
        CLegacyPluginFSOwner(CLegacyPluginDataOwner& pluginDataOwner,
                             int builtForVersion);

        CLegacyPluginFSInterface* Resolve(
            sdk107::CPluginFSInterfaceAbstract* legacy) override;
        sdk107::CPluginFSInterfaceAbstract* FindLegacy(
            const ::CPluginFSInterfaceAbstract* wide) const override;
        sdk107::CPluginFSInterfaceAbstract* Retire(
            ::CPluginFSInterfaceAbstract* wide);

    private:
        CLegacyPluginDataOwner& PluginDataOwner;
        int BuiltForVersion;
        std::unordered_map<
            sdk107::CPluginFSInterfaceAbstract*,
            std::unique_ptr<CLegacyPluginFSInterface>>
            Wrappers;
        std::unordered_map<const ::CPluginFSInterfaceAbstract*,
                           sdk107::CPluginFSInterfaceAbstract*>
            LegacyByWrapper;
    };

    // Complete ten-slot live FS factory. It owns no plugin objects; OpenFS and
    // CloseFS publish/retire identities through CLegacyPluginFSOwner.
    class CLegacyPluginInterfaceForFS final
        : public ::CPluginInterfaceForFSAbstract
    {
    public:
        CLegacyPluginInterfaceForFS(
            sdk107::CPluginInterfaceForFSAbstract& legacy,
            CLegacyPluginFSOwner& fsOwner);

        ::CPluginFSInterfaceAbstract* WINAPI OpenFS(
            const wchar_t* fsName, int fsNameIndex) override;
        void WINAPI CloseFS(::CPluginFSInterfaceAbstract* fs) override;
        void WINAPI ExecuteChangeDriveMenuItem(int panel) override;
        BOOL WINAPI ChangeDriveMenuItemContextMenu(
            HWND parent, int panel, int x, int y,
            ::CPluginFSInterfaceAbstract* pluginFS,
            const wchar_t* pluginFSName, int pluginFSNameIndex,
            BOOL isDetachedFS, BOOL& refreshMenu, BOOL& closeMenu,
            int& postCmd, void*& postCmdParam) override;
        void WINAPI ExecuteChangeDrivePostCommand(int panel, int postCmd,
                                                  void* postCmdParam) override;
        void WINAPI ExecuteOnFS(int panel,
                                ::CPluginFSInterfaceAbstract* pluginFS,
                                const wchar_t* pluginFSName,
                                int pluginFSNameIndex, ::CFileData& file,
                                int isDir) override;
        BOOL WINAPI DisconnectFS(HWND parent, BOOL isInPanel, int panel,
                                 ::CPluginFSInterfaceAbstract* pluginFS,
                                 const wchar_t* pluginFSName,
                                 int pluginFSNameIndex) override;
        BOOL WINAPI ConvertPathToInternal(const wchar_t* fsName,
                                          int fsNameIndex,
                                          CSalamanderStringBuffer* fsUserPart) override;
        BOOL WINAPI ConvertPathToExternal(const wchar_t* fsName,
                                          int fsNameIndex,
                                          CSalamanderStringBuffer* fsUserPart) override;
        void WINAPI EnsureShareExistsOnServer(int panel,
                                              const wchar_t* server,
                                              const wchar_t* share) override;

    private:
        sdk107::CPluginInterfaceForFSEncapsulation Legacy;
        CLegacyPluginFSOwner& FSOwner;
    };

    // Complete sixteen-slot live top-level plugin interface. Registry and
    // Connect callbacks are stack-bound facades; plugin-data retirement and
    // all child interface identities are delegated to their owning graphs.
    class CLegacyPluginInterface final : public ::CPluginInterfaceAbstract
    {
    public:
        CLegacyPluginInterface(
            sdk107::CPluginInterfaceAbstract& legacy,
            CLegacyPluginDataOwner& pluginDataOwner,
            CLegacyPluginChildResolver& childResolver,
            CLegacyGUIIconListTransfer& iconListTransfer);

        void WINAPI About(HWND parent) override;
        BOOL WINAPI Release(HWND parent, BOOL force) override;
        void WINAPI LoadConfiguration(
            HWND parent, HKEY regKey,
            ::CSalamanderRegistryAbstract* registry) override;
        void WINAPI SaveConfiguration(
            HWND parent, HKEY regKey,
            ::CSalamanderRegistryAbstract* registry) override;
        void WINAPI Configuration(HWND parent) override;
        void WINAPI Connect(HWND parent,
                            ::CSalamanderConnectAbstract* salamander) override;
        void WINAPI ReleasePluginDataInterface(
            ::CPluginDataInterfaceAbstract* pluginData) override;
        ::CPluginInterfaceForArchiverAbstract* WINAPI
        GetInterfaceForArchiver() override;
        ::CPluginInterfaceForViewerAbstract* WINAPI
        GetInterfaceForViewer() override;
        ::CPluginInterfaceForMenuExtAbstract* WINAPI
        GetInterfaceForMenuExt() override;
        ::CPluginInterfaceForFSAbstract* WINAPI GetInterfaceForFS() override;
        ::CPluginInterfaceForThumbLoaderAbstract* WINAPI
        GetInterfaceForThumbLoader() override;
        void WINAPI Event(int event, DWORD param) override;
        void WINAPI ClearHistory(HWND parent) override;
        void WINAPI AcceptChangeOnPathNotification(
            const wchar_t* path, BOOL includingSubdirs) override;
        void WINAPI PasswordManagerEvent(HWND parent, int event) override;

    private:
        sdk107::CPluginInterfaceEncapsulation Legacy;
        CLegacyPluginDataOwner& PluginDataOwner;
        CLegacyPluginChildResolver& ChildResolver;
        CLegacyGUIIconListTransfer& IconListTransfer;
    };

} // namespace sally::compat
