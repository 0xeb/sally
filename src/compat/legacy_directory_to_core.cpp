// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

// legacy_directory_to_core — complete frozen-v107 Directory callback facade.
// This mixed-generation TU must never include precomp.h.

#define NOMINMAX
#include <windows.h>

#include <memory>
#include <string>
#include <unordered_map>

#include "compat/legacy_convert.h"
#include "compat/legacy_to_core.h"

namespace sally::compat
{
namespace
{

bool WidenOptionalPath(const char* path, std::wstring& storage,
                       const wchar_t*& result)
{
    if (path == nullptr)
    {
        result = nullptr;
        return true;
    }
    if (!WidenPluginText(path, storage))
    {
        result = nullptr;
        return false;
    }
    result = storage.c_str();
    return true;
}

struct CLegacyFileMirror
{
    ~CLegacyFileMirror() { FreeLegacyFileData(Row); }

    const ::CFileData* Source = nullptr;
    sdk107::CFileData Row = {};
};

using CFileMirrorMap = std::unordered_map<int, std::unique_ptr<CLegacyFileMirror>>;

const sdk107::CFileData* GetMirror(const ::CFileData* source, int index,
                                  CFileMirrorMap& mirrors)
{
    if (source == nullptr || index < 0)
        return nullptr;

    auto found = mirrors.find(index);
    if (found != mirrors.end() && found->second->Source == source)
        return &found->second->Row;

    try
    {
        auto mirror = std::make_unique<CLegacyFileMirror>();
        NarrowRefusal refusal = NarrowRefusal::None;
        if (!FileDataToLegacyWithWideNameFallback(*source, mirror->Row, &refusal))
        {
            // GetFile/GetDir are frozen accessors with no documented NULL case, so a plugin that
            // trusts the contract dereferences whatever comes back. That is why this one caller
            // converts with the NameW fallback: the case that used to get here most often - a name
            // the active code page cannot spell - now travels with an approximate Name and the
            // exact spelling in NameW, so Cyrillic and CJK rows survive the trip out, including
            // rows the plugin itself added (the INBOUND direction has always trusted NameW).
            //
            // What remains is a missing name, a name past INT_MAX, and allocation failure. None of
            // those has a side channel that would make an approximation safe, so they still answer
            // NULL - and they publish the reason the way a frozen Win32-shaped ABI publishes one,
            // where an unset GetLastError left the caller staring at a stale error from whatever it
            // did last.
            SetLastError(Win32ErrorForNarrowRefusal(refusal));
            mirrors.erase(index);
            return nullptr;
        }
        mirror->Source = source;
        const sdk107::CFileData* result = &mirror->Row;
        mirrors[index] = std::move(mirror);
        return result;
    }
    catch (const std::bad_alloc&)
    {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return nullptr;
    }
    catch (const std::length_error&)
    {
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return nullptr;
    }
}

} // namespace

class CLegacySalamanderDirectory::CState
{
public:
    CState(CLegacyPluginDataResolver& resolver, int builtForVersion)
        : Resolver(resolver), BuiltForVersion(builtForVersion)
    {
    }

    void Invalidate()
    {
        Files.clear();
        Dirs.clear();
        Children.clear();
    }

    const sdk107::CFileData* File(const ::CFileData* source, int index)
    {
        return GetMirror(source, index, Files);
    }

    const sdk107::CFileData* Dir(const ::CFileData* source, int index)
    {
        return GetMirror(source, index, Dirs);
    }

    const sdk107::CSalamanderDirectoryAbstract* Child(
        const ::CSalamanderDirectoryAbstract* source)
    {
        if (source == nullptr)
            return nullptr;
        auto found = Children.find(source);
        if (found != Children.end())
            return found->second.get();

        try
        {
            auto child = std::make_unique<CLegacySalamanderDirectory>(
                *const_cast<::CSalamanderDirectoryAbstract*>(source), Resolver,
                BuiltForVersion);
            const sdk107::CSalamanderDirectoryAbstract* result = child.get();
            Children.emplace(source, std::move(child));
            return result;
        }
        catch (const std::bad_alloc&)
        {
            SetLastError(ERROR_NOT_ENOUGH_MEMORY);
            return nullptr;
        }
        catch (const std::length_error&)
        {
            SetLastError(ERROR_INSUFFICIENT_BUFFER);
            return nullptr;
        }
    }

private:
    CLegacyPluginDataResolver& Resolver;
    int BuiltForVersion;
    CFileMirrorMap Files;
    CFileMirrorMap Dirs;
    std::unordered_map<const ::CSalamanderDirectoryAbstract*,
                       std::unique_ptr<CLegacySalamanderDirectory>> Children;
};

CLegacySalamanderDirectory::CLegacySalamanderDirectory(
    ::CSalamanderDirectoryAbstract& wideDirectory,
    CLegacyPluginDataResolver& pluginDataResolver, int builtForVersion)
    : WideDirectory(wideDirectory), PluginDataResolver(pluginDataResolver),
      TrustNameW(builtForVersion >= sdk107::ArchiveNameWVersion),
      State(std::make_unique<CState>(pluginDataResolver, builtForVersion))
{
}

CLegacySalamanderDirectory::~CLegacySalamanderDirectory() = default;

::CPluginDataInterfaceAbstract* CLegacySalamanderDirectory::ResolvePluginData(
    sdk107::CPluginDataInterfaceAbstract* pluginData) const
{
    return pluginData != nullptr ? PluginDataResolver.Resolve(pluginData) : nullptr;
}

void WINAPI CLegacySalamanderDirectory::Clear(
    sdk107::CPluginDataInterfaceAbstract* pluginData)
{
    ::CPluginDataInterfaceAbstract* widePluginData = ResolvePluginData(pluginData);
    if (pluginData != nullptr && widePluginData == nullptr)
        return;
    WideDirectory.Clear(widePluginData);
    State->Invalidate();
}

void WINAPI CLegacySalamanderDirectory::SetValidData(DWORD validData)
{
    WideDirectory.SetValidData(validData);
}

void WINAPI CLegacySalamanderDirectory::SetFlags(DWORD flags)
{
    WideDirectory.SetFlags(flags);
}

BOOL CLegacySalamanderDirectory::Add(
    bool isDir, const char* path, sdk107::CFileData& row,
    sdk107::CPluginDataInterfaceAbstract* pluginData)
{
    ::CPluginDataInterfaceAbstract* widePluginData = ResolvePluginData(pluginData);
    if (pluginData != nullptr && widePluginData == nullptr)
        return FALSE;

    ::CFileData converted = {};
    if (!FileDataFromLegacy(row, converted, TrustNameW))
        return FALSE;

    std::wstring widePath;
    const wchar_t* pathArg = nullptr;
    if (!WidenOptionalPath(path, widePath, pathArg))
    {
        FreeConvertedFileData(converted);
        return FALSE;
    }
    const BOOL accepted = isDir
                              ? WideDirectory.AddDir(pathArg, converted, widePluginData)
                              : WideDirectory.AddFile(pathArg, converted, widePluginData);
    if (!accepted)
    {
        FreeConvertedFileData(converted);
        return FALSE;
    }

    // AddFile/AddDir transferred the converted row to core. Retire the plugin's
    // original buffers now; a pre-v107 NameW value is explicitly discarded
    // without being read or freed because that field was not trustworthy.
    if (!TrustNameW)
        row.NameW = nullptr;
    FreeLegacyFileData(row);
    State->Invalidate();
    return TRUE;
}

BOOL WINAPI CLegacySalamanderDirectory::AddFile(
    const char* path, sdk107::CFileData& file,
    sdk107::CPluginDataInterfaceAbstract* pluginData)
{
    return Add(false, path, file, pluginData);
}

BOOL WINAPI CLegacySalamanderDirectory::AddDir(
    const char* path, sdk107::CFileData& dir,
    sdk107::CPluginDataInterfaceAbstract* pluginData)
{
    return Add(true, path, dir, pluginData);
}

int WINAPI CLegacySalamanderDirectory::GetFilesCount() const
{
    return WideDirectory.GetFilesCount();
}

int WINAPI CLegacySalamanderDirectory::GetDirsCount() const
{
    return WideDirectory.GetDirsCount();
}

const sdk107::CFileData* WINAPI CLegacySalamanderDirectory::GetFile(int index) const
{
    return State->File(WideDirectory.GetFile(index), index);
}

const sdk107::CFileData* WINAPI CLegacySalamanderDirectory::GetDir(int index) const
{
    return State->Dir(WideDirectory.GetDir(index), index);
}

const sdk107::CSalamanderDirectoryAbstract* WINAPI
CLegacySalamanderDirectory::GetSalDir(int index) const
{
    return State->Child(WideDirectory.GetSalDir(index));
}

void WINAPI CLegacySalamanderDirectory::SetApproximateCount(int files, int dirs)
{
    WideDirectory.SetApproximateCount(files, dirs);
}

} // namespace sally::compat
