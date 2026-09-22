// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

/*
	Salamander Plugin Development Framework
	
	Copyright (c) 2015 Milan Kase <manison@manison.cz>
	Copyright (c) 2015 Open Salamander Authors
	
	fxfs.h
	Contains classes for developing Salamander file system plugins.
*/

#pragma once

#include "globals.h"
#include "fx.h"

#include <string>
#include <vector>

extern "C" CPluginInterfaceAbstract* WINAPI SalamanderPluginEntry(_In_::CSalamanderPluginEntryAbstract* salamander);

namespace Fx
{

    class CFxPathComponentToken;

    class CFxPath : public CFxString
    {
    protected:
        CFxPath()
        {
        }

        CFxPath(const CFxString::XCHAR* userPart)
            : CFxString(userPart)
        {
        }

        CFxPath(const CFxString& path)
            : CFxString(path)
        {
        }

    public:
        virtual void WINAPI SetRoot() = 0;

        virtual HRESULT WINAPI Canonicalize() = 0;

        virtual void WINAPI ExcludeTrailingSeparator() = 0;

        virtual void WINAPI IncludeTrailingSeparator() = 0;

        virtual bool WINAPI IsRooted() const = 0;

        virtual bool WINAPI CutLastComponent(wchar_t* lastComponent, int lastComponentMaxSize) = 0;

        bool WINAPI CutLastComponent()
        {
            return CutLastComponent(nullptr, 0);
        }

        bool WINAPI CutLastComponent(std::wstring& lastComponent)
        {
            std::vector<wchar_t> buffer(static_cast<size_t>(GetLength()) + 1, L'\0');
            if (!CutLastComponent(buffer.data(), static_cast<int>(buffer.size())))
                return false;
            lastComponent.assign(buffer.data());
            return true;
        }

        virtual bool WINAPI GetNextPathComponent(_Inout_ CFxPathComponentToken& token) = 0;

        virtual bool WINAPI Equals(const CFxPath::XCHAR* path1, const CFxPath::XCHAR* path2) const = 0;

        virtual bool WINAPI EqualsLen(const CFxPath::XCHAR* path1, const CFxPath::XCHAR* path2, int len) const = 0;

        virtual bool WINAPI Equals(const CFxPath::XCHAR* otherPath) const
        {
            return Equals(GetString(), otherPath);
        }
    };

    void WINAPI FxPathAltSepToSepAndRemoveDups(
        CFxPath::XCHAR* path,
        CFxPath::XCHAR sep,
        CFxPath::XCHAR altSep);

    class CFxPathComponentToken
    {
    protected:
        CFxPath* m_path;
        int m_tokenStart;
        int m_tokenLength;

        friend class CFxPath;

    public:
        CFxPathComponentToken()
            : m_path(nullptr),
              m_tokenStart(-1),
              m_tokenLength(0)
        {
        }

        CFxString WINAPI GetComponent() const
        {
            _ASSERTE(m_path != nullptr);
            _ASSERTE(m_tokenStart >= 0);
            _ASSERTE(m_tokenLength > 0);
            return m_path->Mid(m_tokenStart, m_tokenLength);
        }

        bool WINAPI ComponentEquals(const CFxPath::XCHAR* component) const
        {
            _ASSERTE(m_path != nullptr);
            _ASSERTE(m_tokenStart >= 0);
            _ASSERTE(m_tokenLength > 0);

            if (m_path->EqualsLen(component, m_path->GetString() + m_tokenStart, m_tokenLength))
            {
                return component[m_tokenLength] == L'\0';
            }

            return false;
        }

        bool WINAPI ComponentEquals(const CFxString& component) const
        {
            _ASSERTE(m_path != nullptr);
            _ASSERTE(m_tokenStart >= 0);
            _ASSERTE(m_tokenLength > 0);

            if (component.GetLength() == m_tokenLength)
            {
                return m_path->EqualsLen(component, m_path->GetString() + m_tokenStart, m_tokenLength);
            }

            return false;
        }

        void WINAPI ReplaceComponent(const CFxPath::XCHAR* component, int len)
        {
            _ASSERTE(component != nullptr);

            if (len < 0)
            {
                len = CFxPath::StrTraits::SafeStringLen(component);
            }

            _ASSERTE(len > 0);
            _ASSERTE(len == m_tokenLength);
            _ASSERTE(m_path != nullptr);
            _ASSERTE(m_tokenStart >= 0);
            memcpy(m_path->GetBuffer() + m_tokenStart, component, len * sizeof(CFxPath::XCHAR));
            m_path->ReleaseBufferSetLength(m_path->GetLength());
        }

        void WINAPI ReplaceComponent(const CFxString& component)
        {
            ReplaceComponent(component.GetString(), component.GetLength());
        }

        int WINAPI GetTokenEnd() const
        {
            _ASSERTE(m_path != nullptr);
            _ASSERTE(m_tokenStart >= 0);
            return m_tokenStart + m_tokenLength;
        }
    };

    template <typename TTraits>
    class TFxPath : public CFxPath
    {
    private:
        class CFxPathComponentTokenFriend : public CFxPathComponentToken
        {
            friend class TFxPath<TTraits>;
        };

    public:
        typedef TTraits TTraits;

        TFxPath()
        {
        }

        TFxPath(const CFxPath::XCHAR* userPart)
            : CFxPath(userPart)
        {
        }

        TFxPath(const CFxString& path)
            : CFxString(path)
        {
        }

        virtual void WINAPI SetRoot() override
        {
            SetString(TTraits::RootPath);
        }

        virtual HRESULT WINAPI Canonicalize() override
        {
            // Remove whitespace characters from the beginning and from
            // the end of the path.
            Trim();

            // Change alternative path separator (forward slash) to the regular
            // separator (backslash) and remove duplicate separators.
            FxPathAltSepToSepAndRemoveDups(GetBuffer(), TTraits::PathSeparator, TTraits::AltPathSeparator);
            ReleaseBuffer();

            // Remove the slash from the end of the path (only keep it for the root path).
            ExcludeTrailingSeparator();

            // Ensure that the path is rooted.
            bool startsWithRoot = IsRooted();
            if (!startsWithRoot && !TTraits::AllowRelativePaths)
            {
                Insert(0, TTraits::RootPath);
                startsWithRoot = true;
            }

            // Remove navigation components . and ..
            if (TTraits::UseDotAndDotDotSemantics)
            {
                _ASSERTE(TTraits::PathSeparator == '\\');
                // SalRemovePointsFromPath removes . and .. components.
                // The method wants pointer *after* the root.
                int skip = startsWithRoot ? TTraits::RootPathLen : 0;
                std::wstring path(GetString(), GetLength());
                BOOL removedOk = SPLSalRemovePointsFromPathOwned(
                    SalamanderGeneral, path, static_cast<size_t>(skip));
                if (removedOk)
                    SetString(path.c_str());
                if (!removedOk)
                {
                    return FX_E_BAD_PATHNAME;
                }
            }

            return S_OK;
        }

        virtual void WINAPI ExcludeTrailingSeparator() override
        {
            int len = GetLength();
            if (len > 0)
            {
                CFxPath::XCHAR c = GetAt(len - 1);
                if ((c == TTraits::PathSeparator || c == TTraits::AltPathSeparator) && len > 1)
                {
                    Truncate(len - 1);
                }
            }
        }

        virtual void WINAPI IncludeTrailingSeparator() override
        {
            int len = GetLength();
            if (len > 0)
            {
                CFxPath::XCHAR c = GetAt(len - 1);
                if (c == TTraits::PathSeparator)
                {
                    // Already contains path separator.
                    return;
                }
                else if (c == TTraits::AltPathSeparator)
                {
                    // Change alternative path separator to the regular one.
                    SetAt(len - 1, TTraits::PathSeparator);
                    return;
                }
            }

            // No separator, append one.
            AppendChar(TTraits::PathSeparator);
        }

        virtual bool WINAPI IsRooted() const override
        {
            if (TTraits::RootPathLen == 0)
            {
                return true;
            }
            else if (TTraits::RootPathLen == 1)
            {
                return GetLength() > 0 && GetAt(0) == TTraits::PathSeparator;
            }
            else
            {
                _ASSERTE(TTraits::RootPathLen > 1);
                return Find(TTraits::RootPath) == 0;
            }
        }

        virtual bool WINAPI CutLastComponent(wchar_t* lastComponent, int lastComponentMaxSize) override
        {
            ExcludeTrailingSeparator();

            int lastSep = ReverseFind(TTraits::PathSeparator);
            if (lastSep < 0)
            {
                return false;
            }

            int len = GetLength();
            if (lastSep + 1 == len)
            {
                // There's nothing after the last separator, we're at root.
                return false;
            }

            if (lastComponent != nullptr)
            {
                StringCchCopyW(lastComponent, lastComponentMaxSize, GetString() + lastSep + 1);
            }

            if (lastSep == 0)
            {
                // Do not truncate the root.
                ++lastSep;
            }

            Truncate(lastSep);

            return true;
        }

        virtual bool WINAPI GetNextPathComponent(_Inout_ CFxPathComponentToken& token_) override
        {
            CFxPathComponentTokenFriend& token = *static_cast<CFxPathComponentTokenFriend*>(&token_);
            _ASSERTE(token.m_path == this || token.m_path == nullptr);
            token.m_path = this;

            int len = GetLength();
            int startIndex = token.m_tokenStart + token.m_tokenLength;
            int separatorIndex;
            bool found = true;

            do
            {
                // Start right after the previous token.
                ++startIndex;
                if (startIndex >= len)
                {
                    separatorIndex = -1;
                    found = false;
                    break;
                }

                separatorIndex = Find(TTraits::PathSeparator, startIndex);
            } while (separatorIndex >= 0 && separatorIndex == startIndex);

            // Skip the separator.
            token.m_tokenStart = startIndex;
            token.m_tokenLength = (separatorIndex < 0 ? len : separatorIndex) - token.m_tokenStart;

            _ASSERTE(token.m_tokenLength > 0 || !found);

            return found;
        }

        virtual bool WINAPI Equals(const CFxPath::XCHAR* path1, const CFxPath::XCHAR* path2) const override
        {
            int res;

            if (TTraits::CaseSensitive)
            {
                res = StrTraits::StringCompare(path1, path2);
            }
            else
            {
                res = StrTraits::StringCompareIgnore(path1, path2);
            }

            return res == 0;
        }

        virtual bool WINAPI EqualsLen(const CFxPath::XCHAR* path1, const CFxPath::XCHAR* path2, int len) const override
        {
            int res;

            _ASSERTE(len >= 0);

            if (TTraits::CaseSensitive)
            {
                res = StrCmpNW(path1, path2, len);
            }
            else
            {
                res = StrCmpNIW(path1, path2, len);
            }

            return res == 0;
        }
    };

    class CFxStandardPathTraitsBase
    {
    public:
        static const CFxPath::XCHAR PathSeparator = '\\';
        static const CFxPath::XCHAR AltPathSeparator = '/';
        static const CFxPath::XCHAR RootPath[];
        static const int RootPathLen = 1;

        /// Whether . and .. are recognized as current dir and up dir respectively.
        static const bool UseDotAndDotDotSemantics = true;
    };

    class CFxStandardPathTraits : public CFxStandardPathTraitsBase
    {
    public:
        static const bool AllowRelativePaths = false;
        static const bool CaseSensitive = false;
    };

    typedef TFxPath<CFxStandardPathTraits> CFxStandardPath;

    class CFxPluginInterfaceForFS : public CPluginInterfaceForFSAbstract
    {
    protected:
        CFxPluginInterface& m_owner;

        CFxPluginInterfaceForFS(CFxPluginInterface& owner);

        virtual void WINAPI RetrieveAssignedFSName() = 0;

        // RetrieveAssignedName can only be called from SalamanderPluginEntry.
        friend CPluginInterfaceAbstract* WINAPI ::SalamanderPluginEntry(_In_ CSalamanderPluginEntryAbstract* salamander);

    public:
        virtual ~CFxPluginInterfaceForFS();

        virtual PCWSTR WINAPI GetSuggestedFSName() const = 0;

        virtual PCWSTR WINAPI GetAssignedFSName() const = 0;

        CFxPluginInterface& GetOwner()
        {
            return m_owner;
        }

        /* CPluginInterfaceForFSAbstract */

        virtual void WINAPI ExecuteChangeDriveMenuItem(int panel) override;

        virtual BOOL WINAPI ChangeDriveMenuItemContextMenu(
            HWND parent,
            int panel,
            int x,
            int y,
            CPluginFSInterfaceAbstract* pluginFS,
            const wchar_t* pluginFSName,
            int pluginFSNameIndex,
            BOOL isDetachedFS,
            BOOL& refreshMenu,
            BOOL& closeMenu,
            int& postCmd,
            void*& postCmdParam) override;

        virtual void WINAPI ExecuteChangeDrivePostCommand(int panel, int postCmd, void* postCmdParam) override;

        virtual void WINAPI ExecuteOnFS(
            int panel,
            CPluginFSInterfaceAbstract* pluginFS,
            const wchar_t* pluginFSName,
            int pluginFSNameIndex,
            CFileData& file,
            int isDir) override;

        virtual BOOL WINAPI DisconnectFS(
            HWND parent,
            BOOL isInPanel,
            int panel,
            CPluginFSInterfaceAbstract* pluginFS,
            const wchar_t* pluginFSName,
            int pluginFSNameIndex) override;

        virtual BOOL WINAPI ConvertPathToInternal(
            const wchar_t* fsName,
            int fsNameIndex,
            CSalamanderStringBuffer* fsUserPart) override;

        virtual BOOL WINAPI ConvertPathToExternal(
            const wchar_t* fsName,
            int fsNameIndex,
            CSalamanderStringBuffer* fsUserPart) override;

        virtual void WINAPI EnsureShareExistsOnServer(int panel, const wchar_t* server, const wchar_t* share) override;
    };

    template <class TFS>
    class TFxPluginInterfaceForFS : public CFxPluginInterfaceForFS
    {
    public:
        typedef TFS TPluginFSInterface;

    private:
        int m_cActiveFS;

    protected:
        TFxPluginInterfaceForFS(CFxPluginInterface& owner)
            : CFxPluginInterfaceForFS(owner)
        {
            m_cActiveFS = 0;
        }

        virtual void WINAPI RetrieveAssignedFSName() override
        {
            TPluginFSInterface::RetrieveAssignedName();
        }

        virtual CPluginFSInterfaceAbstract* WINAPI CreateFS(const wchar_t* fsName, int fsNameIndex)
        {
            UNREFERENCED_PARAMETER(fsName);
            UNREFERENCED_PARAMETER(fsNameIndex);
            return new TPluginFSInterface(*this);
        }

    public:
        virtual ~TFxPluginInterfaceForFS()
        {
            _ASSERTE(m_cActiveFS == 0);
        }

        virtual PCWSTR WINAPI GetSuggestedFSName() const override
        {
            return TPluginFSInterface::SUGGESTED_NAME;
        }

        virtual PCWSTR WINAPI GetAssignedFSName() const override
        {
            return TPluginFSInterface::GetAssignedName();
        }

        /* CPluginInterfaceForFSAbstract */

        virtual CPluginFSInterfaceAbstract* WINAPI OpenFS(const wchar_t* fsName, int fsNameIndex) override
        {
            auto tfs = CreateFS(fsName, fsNameIndex);
            ++m_cActiveFS;
            return tfs;
        }

        virtual void WINAPI CloseFS(CPluginFSInterfaceAbstract* fs) override
        {
            _ASSERTE(fs != nullptr);

            // Typecast to call the correct destructor.
            auto tfs = static_cast<TPluginFSInterface*>(fs);
            delete tfs;

            --m_cActiveFS;
        }
    };

    class CFxPluginFSInterface : public CPluginFSInterfaceAbstract
    {
    private:
        CFxPath* m_currentPath;

        CFxItemEnumerator* m_currentPathEnumerator;

        bool m_calledFromDisconnectDialog;

        FILETIME m_lastParentItemTime;

        void WINAPI ExecuteOnFS(CFileData& file, int isDir);

        friend class CFxPluginInterfaceForFS;

    protected:
        CFxPluginInterfaceForFS& m_owner;

        CFxPluginFSInterface(CFxPluginInterfaceForFS& owner);

        void WINAPI ShowChangePathError(PCWSTR path, HRESULT hr);
        void WINAPI ShowChangePathError(PCWSTR path, PCWSTR error);

        void WINAPI SetCurrentPath(CFxPath* path);
        void WINAPI SetCurrentPathEnumerator(CFxItemEnumerator* enumerator);

        void WINAPI SetCurrentPathEnumerator(CFxPath* path, CFxItemEnumerator* enumerator)
        {
            SetCurrentPath(path);
            SetCurrentPathEnumerator(enumerator);
        }

        const CFxPath& WINAPI GetCurrentPath() const
        {
            return *m_currentPath;
        }

        bool WINAPI IsCalledFromDisconnectDialog() const
        {
            return m_calledFromDisconnectDialog;
        }

        void WINAPI ChangeDirectory(PCWSTR newPath, PCWSTR focusedName = nullptr);

        /* Overridables */

        /// Returns enumerator for the current path.
        virtual HRESULT WINAPI GetEnumeratorForCurrentPath(_Out_ CFxItemEnumerator*& enumerator, bool forceRefresh);

        virtual HRESULT WINAPI GetChildEnumerator(
            _Out_ CFxItemEnumerator*& enumerator,
            CFxItem* parentItem,
            int level,
            bool forceRefresh) = 0;

        /// \remarks The method may update the path - e.g. letter casing.
        virtual HRESULT WINAPI GetEnumeratorForPath(
            _Out_ CFxItemEnumerator*& enumerator,
            _Inout_ CFxPath& path,
            _Out_ int& pathCutIndex,
            bool forceRefresh);

        virtual IFxItemConverter* WINAPI GetConverter() const;

        /// Factory method to create CPluginDataInterfaceAbstract.
        virtual CFxPluginDataInterface* WINAPI CreatePluginData(CFxItemEnumerator* enumerator);

        virtual CFxPath* WINAPI CreatePath(const CFxPath::XCHAR* userPart) = 0;

        virtual bool WINAPI IsUpDirNeededForCurrentPath();

        virtual void WINAPI ExecuteUpDir();

        virtual void WINAPI ExecuteItem(CFxItem& item);

        virtual bool WINAPI GetFileDataForUpDir(_Out_ CFileData& upDirFileData);

    public:
        virtual ~CFxPluginFSInterface();

        /* CPluginFSInterfaceAbstract */

        virtual BOOL WINAPI IsCurrentPath(int currentFSNameIndex, int fsNameIndex,
                                          const wchar_t* userPart) override;

        virtual BOOL WINAPI IsOurPath(int currentFSNameIndex, int fsNameIndex,
                                      const wchar_t* userPart) override;

        virtual BOOL WINAPI GetRootPath(CSalamanderStringBuffer* userPart) override;

        virtual BOOL WINAPI GetCurrentPath(CSalamanderStringBuffer* userPart) override;

        virtual BOOL WINAPI ChangePath(
            int currentFSNameIndex,
            CSalamanderStringBuffer* fsName,
            int fsNameIndex,
            const wchar_t* userPart,
            CSalamanderStringBuffer* cutFileName,
            BOOL* pathWasCut,
            BOOL forceRefresh,
            int mode) override;

        virtual BOOL WINAPI ListCurrentPath(
            CSalamanderDirectoryAbstract* dir,
            CPluginDataInterfaceAbstract*& pluginData,
            int& iconsType,
            BOOL forceRefresh) override;

        virtual HICON WINAPI GetFSIcon(BOOL& destroyIcon) override;

        virtual BOOL WINAPI GetFullName(CFileData& file, int isDir,
                                        CSalamanderStringBuffer* fullName) override;

        virtual BOOL WINAPI GetFullFSPath(
            HWND parent,
            const wchar_t* fsName,
            CSalamanderStringBuffer* path,
            BOOL& success) override;

        virtual BOOL WINAPI TryCloseOrDetach(BOOL forceClose, BOOL canDetach, BOOL& detach, int reason) override;

        virtual void WINAPI Event(int event, DWORD param) override;

        virtual void WINAPI ReleaseObject(HWND parent) override;

        virtual BOOL WINAPI GetChangeDriveOrDisconnectItem(
            const wchar_t* fsName,
            wchar_t*& title,
            HICON& icon,
            BOOL& destroyIcon) override;

        virtual void WINAPI GetDropEffect(
            const wchar_t* srcFSPath,
            const wchar_t* tgtFSPath,
            DWORD allowedEffects,
            DWORD keyState,
            DWORD* dropEffect) override;

        virtual void WINAPI GetFSFreeSpace(CQuadWord* retValue) override;

        virtual BOOL WINAPI GetNextDirectoryLineHotPath(const wchar_t* text, int pathLen, int& offset) override;

        virtual BOOL WINAPI CompleteDirectoryLineHotPath(CSalamanderStringBuffer* path) override;

        virtual BOOL WINAPI GetPathForMainWindowTitle(const wchar_t* fsName, int mode, CSalamanderStringBuffer* buf) override;

        virtual void WINAPI ShowInfoDialog(const wchar_t* fsName, HWND parent) override;

        virtual BOOL WINAPI ExecuteCommandLine(HWND parent, CSalamanderStringBuffer* command, int& selFrom, int& selTo) override;

        virtual BOOL WINAPI QuickRename(
            const wchar_t* fsName,
            int mode,
            HWND parent,
            CFileData& file,
            BOOL isDir,
            CSalamanderStringBuffer* newName,
            BOOL& cancel) override;

        virtual void WINAPI AcceptChangeOnPathNotification(
            const wchar_t* fsName,
            const wchar_t* path,
            BOOL includingSubdirs) override;

        virtual BOOL WINAPI CreateDir(
            const wchar_t* fsName,
            int mode,
            HWND parent,
            CSalamanderStringBuffer* newName,
            BOOL& cancel) override;

        virtual void WINAPI ViewFile(
            const wchar_t* fsName,
            HWND parent,
            CSalamanderForViewFileOnFSAbstract* salamander,
            CFileData& file) override;

        virtual BOOL WINAPI Delete(
            const wchar_t* fsName,
            int mode,
            HWND parent,
            int panel,
            int selectedFiles,
            int selectedDirs,
            BOOL& cancelOrError) override;

        virtual BOOL WINAPI CopyOrMoveFromFS(
            BOOL copy,
            int mode,
            const wchar_t* fsName,
            HWND parent,
            int panel,
            int selectedFiles,
            int selectedDirs,
            CSalamanderStringBuffer* targetPath,
            BOOL& operationMask,
            BOOL& cancelOrHandlePath,
            HWND dropTarget) override;

        virtual BOOL WINAPI CopyOrMoveFromDiskToFS(
            BOOL copy,
            int mode,
            const wchar_t* fsName,
            HWND parent,
            const wchar_t* sourcePath,
            SalEnumSelection2 next,
            void* nextParam,
            int sourceFiles,
            int sourceDirs,
            CSalamanderStringBuffer* targetPath,
            BOOL* invalidPathOrCancel) override;

        virtual BOOL WINAPI ChangeAttributes(
            const wchar_t* fsName,
            HWND parent,
            int panel,
            int selectedFiles,
            int selectedDirs) override;

        virtual void WINAPI ShowProperties(
            const wchar_t* fsName,
            HWND parent,
            int panel,
            int selectedFiles,
            int selectedDirs) override;

        virtual void WINAPI ContextMenu(
            const wchar_t* fsName,
            HWND parent,
            int menuX,
            int menuY,
            int type,
            int panel,
            int selectedFiles,
            int selectedDirs) override;

        virtual BOOL WINAPI HandleMenuMsg(
            UINT uMsg,
            WPARAM wParam,
            LPARAM lParam,
            LRESULT* plResult) override;

        virtual BOOL WINAPI OpenFindDialog(const wchar_t* fsName, int panel) override;

        virtual void WINAPI OpenActiveFolder(const wchar_t* fsName, HWND parent) override;

        virtual void WINAPI GetAllowedDropEffects(int mode, const wchar_t* tgtFSPath, DWORD* allowedEffects) override;

        virtual BOOL WINAPI GetNoItemsInPanelText(CSalamanderStringBuffer* textBuf) override;

        virtual void WINAPI ShowSecurityInfo(HWND parent) override;
    };

    class CFxPluginFSDataInterface : public CFxPluginDataInterface
    {
    protected:
        CFxPluginFSInterface& m_owner;

    private:
        static int WINAPI GetSimpleIconIndex();

    public:
        CFxPluginFSDataInterface(CFxPluginFSInterface& owner);

        virtual ~CFxPluginFSDataInterface();

        /* CFxPluginDataInterface Overrides */

        virtual void WINAPI SetupView(
            BOOL leftPanel,
            CSalamanderViewAbstract* view,
            const wchar_t* archivePath,
            const CFileData* upperDir) override;
    };

    template <class TFS, class TPath = CFxStandardPath>
    class TFxPluginFSInterface : public CFxPluginFSInterface
    {
    private:
        /**
		Name of the filesystem assigned to us by Salamander
		(need not to be the same as suggested name).
	*/
        static std::wstring s_assignedFSName;

        static void RetrieveAssignedName()
        {
            _ASSERTE(SalamanderGeneral != nullptr);
            s_assignedFSName = SPLGetPluginFSNameOwned(SalamanderGeneral, 0);
        }

        friend class TFxPluginInterfaceForFS<TFS>;

    protected:
        virtual CFxPath* WINAPI CreatePath(const CFxPath::XCHAR* userPart) override
        {
            return new TPath(userPart);
        }

    public:
        typedef TPath TPath;

        TFxPluginFSInterface(CFxPluginInterfaceForFS& owner)
            : CFxPluginFSInterface(owner)
        {
        }

        virtual ~TFxPluginFSInterface()
        {
        }

        static PCWSTR GetAssignedName()
        {
            return s_assignedFSName.c_str();
        }

        /* CPluginFSInterfaceAbstract */

        virtual DWORD WINAPI GetSupportedServices() override
        {
            return static_cast<TFS*>(this)->SUPPORTED_SERVICES |
                   FS_SERVICE_GETFSICON; // GetFSIcon provided by the framework
        }

        virtual BOOL WINAPI GetRootPath(CSalamanderStringBuffer* userPart) override
        {
            return userPart != nullptr &&
                   sally::plugin_abi::WriteStringBuffer(
                       *userPart, std::wstring(TPath::TTraits::RootPath));
        }
    };

    template <typename TFS, typename TPath = CFxStandardPath>
    std::wstring TFxPluginFSInterface<TFS, TPath>::s_assignedFSName;

}; // namespace Fx
