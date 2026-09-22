// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

// legacy_to_core — see legacy_to_core.h.
//
// Never include precomp.h here. The frozen sdk107 types and the live SDK must
// coexist in this translation unit; precomp.h would import the live SDK first
// and defeat that isolation.

#define NOMINMAX
#include <windows.h>

#include <algorithm>
#include <cstddef>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

#include "compat/LegacyPrintfFormat.h"
#include "compat/legacy_to_core.h"

#include "compat/legacy_convert.h"

namespace sally::compat
{
namespace
{

bool WidenOptional(const char* value, std::wstring& storage,
                   const wchar_t*& result)
{
    if (value == nullptr)
    {
        result = nullptr;
        return true;
    }
    if (!WidenPluginText(value, storage))
    {
        result = nullptr;
        return false;
    }
    result = storage.c_str();
    return true;
}

::CCallStackMsgContext* ToLiveCallStackContext(
    sdk107::CCallStackMsgContext* context)
{
    // This opaque context is storage shared across a frozen Push/Pop pair. It
    // contains no text pointers and did not change in v108, so identity must be
    // preserved rather than copied into a temporary that would die after Push.
    return reinterpret_cast<::CCallStackMsgContext*>(context);
}

#if (defined(_DEBUG) || defined(CALLSTK_MEASURETIMES)) && !defined(CALLSTK_DISABLEMEASURETIMES)
static_assert(sizeof(sdk107::CCallStackMsgContext) == sizeof(::CCallStackMsgContext));
static_assert(alignof(sdk107::CCallStackMsgContext) == alignof(::CCallStackMsgContext));
static_assert(offsetof(sdk107::CCallStackMsgContext, PushesCounterStart) ==
              offsetof(::CCallStackMsgContext, PushesCounterStart));
static_assert(offsetof(sdk107::CCallStackMsgContext, PushPerfTimeCounterStart) ==
              offsetof(::CCallStackMsgContext, PushPerfTimeCounterStart));
static_assert(offsetof(sdk107::CCallStackMsgContext, IgnoredPushPerfTimeCounterStart) ==
              offsetof(::CCallStackMsgContext, IgnoredPushPerfTimeCounterStart));
static_assert(offsetof(sdk107::CCallStackMsgContext, StartTime) ==
              offsetof(::CCallStackMsgContext, StartTime));
static_assert(offsetof(sdk107::CCallStackMsgContext, PushCallerAddress) ==
              offsetof(::CCallStackMsgContext, PushCallerAddress));
#endif

::CQuadWord QuadWordToLive(const sdk107::CQuadWord& value)
{
    ::CQuadWord result;
    result.LoDWord = value.LoDWord;
    result.HiDWord = value.HiDWord;
    return result;
}

void QuadWordToLegacy(const ::CQuadWord& value, sdk107::CQuadWord& result)
{
    result.LoDWord = value.LoDWord;
    result.HiDWord = value.HiDWord;
}

bool CopyWideOutputToLegacy(const wchar_t* value, char* output, int outputMax)
{
    if (output == nullptr || outputMax <= 0)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return false;
    }

    output[0] = '\0';
    if (value == nullptr || value[0] == L'\0')
        return true;

    const NarrowResult narrowed = NarrowExact(value);
    if (!narrowed.ok)
    {
        SetLastError(Win32ErrorForNarrowRefusal(narrowed.refusal));
        return false;
    }

    if (narrowed.value.size() >= static_cast<std::size_t>(outputMax))
    {
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return false;
    }
    std::memcpy(output, narrowed.value.c_str(), narrowed.value.size() + 1);
    return true;
}

static_assert(sizeof(sdk107::SAFE_FILE) == sizeof(::SAFE_FILE));
static_assert(alignof(sdk107::SAFE_FILE) == alignof(::SAFE_FILE));
static_assert(offsetof(sdk107::SAFE_FILE, HFile) == offsetof(::SAFE_FILE, HFile));
static_assert(offsetof(sdk107::SAFE_FILE, FileName) == offsetof(::SAFE_FILE, FileName));
static_assert(offsetof(sdk107::SAFE_FILE, HParentWnd) == offsetof(::SAFE_FILE, HParentWnd));
static_assert(offsetof(sdk107::SAFE_FILE, dwDesiredAccess) == offsetof(::SAFE_FILE, dwDesiredAccess));
static_assert(offsetof(sdk107::SAFE_FILE, dwShareMode) == offsetof(::SAFE_FILE, dwShareMode));
static_assert(offsetof(sdk107::SAFE_FILE, dwCreationDisposition) == offsetof(::SAFE_FILE, dwCreationDisposition));
static_assert(offsetof(sdk107::SAFE_FILE, dwFlagsAndAttributes) == offsetof(::SAFE_FILE, dwFlagsAndAttributes));
static_assert(offsetof(sdk107::SAFE_FILE, WholeFileAllocated) == offsetof(::SAFE_FILE, WholeFileAllocated));

} // namespace

CLegacySalamanderDebug::CLegacySalamanderDebug(::CSalamanderDebugAbstract& wideDebug)
    : WideDebug(wideDebug)
{
}

void WINAPI CLegacySalamanderDebug::TraceI(const char* file, int line, const char* str)
{
    std::wstring fileW;
    std::wstring textW;
    const wchar_t* liveFile = nullptr;
    const wchar_t* liveText = nullptr;
    if (!WidenOptional(file, fileW, liveFile) ||
        !WidenOptional(str, textW, liveText))
        return;
    WideDebug.TraceI(liveFile, line, liveText);
}

void WINAPI CLegacySalamanderDebug::TraceIW(const WCHAR* file, int line, const WCHAR* str)
{
    WideDebug.TraceI(file, line, str);
}

void WINAPI CLegacySalamanderDebug::TraceE(const char* file, int line, const char* str)
{
    std::wstring fileW;
    std::wstring textW;
    const wchar_t* liveFile = nullptr;
    const wchar_t* liveText = nullptr;
    if (!WidenOptional(file, fileW, liveFile) ||
        !WidenOptional(str, textW, liveText))
        return;
    WideDebug.TraceE(liveFile, line, liveText);
}

void WINAPI CLegacySalamanderDebug::TraceEW(const WCHAR* file, int line, const WCHAR* str)
{
    WideDebug.TraceE(file, line, str);
}

void WINAPI CLegacySalamanderDebug::TraceAttachThread(HANDLE thread, unsigned tid)
{
    WideDebug.TraceAttachThread(thread, tid);
}

void WINAPI CLegacySalamanderDebug::TraceSetThreadName(const char* name)
{
    std::wstring nameW;
    const wchar_t* liveName = nullptr;
    if (WidenOptional(name, nameW, liveName))
        WideDebug.TraceSetThreadName(liveName);
}

void WINAPI CLegacySalamanderDebug::TraceSetThreadNameW(const WCHAR* name)
{
    WideDebug.TraceSetThreadName(name);
}

unsigned WINAPI CLegacySalamanderDebug::CallWithCallStack(
    unsigned(WINAPI* threadBody)(void*), void* param)
{
    return WideDebug.CallWithCallStack(threadBody, param);
}

void WINAPI CLegacySalamanderDebug::Push(
    const char* format, va_list args,
    sdk107::CCallStackMsgContext* callStackMsgContext, BOOL doNotMeasureTimes)
{
    std::wstring formatW;
    if (!sally::compat::WidenLegacyPrintfFormat(format, formatW))
        return;
    WideDebug.Push(format != nullptr ? formatW.c_str() : nullptr, args,
                   ToLiveCallStackContext(callStackMsgContext), doNotMeasureTimes);
}

void WINAPI CLegacySalamanderDebug::Pop(
    sdk107::CCallStackMsgContext* callStackMsgContext)
{
    WideDebug.Pop(ToLiveCallStackContext(callStackMsgContext));
}

void WINAPI CLegacySalamanderDebug::SetThreadNameInVC(const char* name)
{
    std::wstring nameW;
    const wchar_t* liveName = nullptr;
    if (WidenOptional(name, nameW, liveName))
        WideDebug.SetThreadNameInVC(liveName);
}

void WINAPI CLegacySalamanderDebug::SetThreadNameInVCAndTrace(const char* name)
{
    std::wstring nameW;
    const wchar_t* liveName = nullptr;
    if (WidenOptional(name, nameW, liveName))
        WideDebug.SetThreadNameInVCAndTrace(liveName);
}

void WINAPI CLegacySalamanderDebug::TraceConnectToServer()
{
    WideDebug.TraceConnectToServer();
}

void WINAPI CLegacySalamanderDebug::AddModuleWithPossibleMemoryLeaks(
    const char* fileName)
{
    std::wstring fileNameW;
    const wchar_t* liveFileName = nullptr;
    if (WidenOptional(fileName, fileNameW, liveFileName))
        WideDebug.AddModuleWithPossibleMemoryLeaks(liveFileName);
}

class CLegacySalamanderSafeFile::CState
{
public:
    struct CEntry
    {
        explicit CEntry(const char* fileName)
        {
            const char* text = fileName != nullptr ? fileName : "";
            const std::size_t length = std::strlen(text);
            LegacyFileName.assign(text, text + length + 1);
        }

        ::SAFE_FILE WideFile = {};
        std::vector<char> LegacyFileName;
    };

    std::shared_ptr<CEntry> Find(sdk107::SAFE_FILE* file)
    {
        std::lock_guard<std::mutex> lock(Mutex);
        const auto found = Files.find(file);
        return found != Files.end() ? found->second : nullptr;
    }

    bool Store(sdk107::SAFE_FILE* file, const std::shared_ptr<CEntry>& entry)
    {
        std::lock_guard<std::mutex> lock(Mutex);
        try
        {
            return Files.emplace(file, entry).second;
        }
        catch (const std::bad_alloc&)
        {
            SetLastError(ERROR_NOT_ENOUGH_MEMORY);
            return false;
        }
        catch (const std::length_error&)
        {
            SetLastError(ERROR_INSUFFICIENT_BUFFER);
            return false;
        }
    }

    std::shared_ptr<CEntry> Remove(sdk107::SAFE_FILE* file)
    {
        std::lock_guard<std::mutex> lock(Mutex);
        const auto found = Files.find(file);
        if (found == Files.end())
            return nullptr;
        std::shared_ptr<CEntry> entry = found->second;
        Files.erase(found);
        return entry;
    }

    std::shared_ptr<CEntry> PopOne()
    {
        std::lock_guard<std::mutex> lock(Mutex);
        if (Files.empty())
            return nullptr;
        const auto found = Files.begin();
        std::shared_ptr<CEntry> entry = std::move(found->second);
        Files.erase(found);
        return entry;
    }

    static void SyncLive(const sdk107::SAFE_FILE& legacy, CEntry& entry)
    {
        entry.WideFile.HFile = legacy.HFile;
        entry.WideFile.HParentWnd = legacy.HParentWnd;
        entry.WideFile.dwDesiredAccess = legacy.dwDesiredAccess;
        entry.WideFile.dwShareMode = legacy.dwShareMode;
        entry.WideFile.dwCreationDisposition = legacy.dwCreationDisposition;
        entry.WideFile.dwFlagsAndAttributes = legacy.dwFlagsAndAttributes;
        entry.WideFile.WholeFileAllocated = legacy.WholeFileAllocated;
    }

    static void SyncLegacy(const CEntry& entry, sdk107::SAFE_FILE& legacy)
    {
        legacy.HFile = entry.WideFile.HFile;
        legacy.FileName = entry.WideFile.FileName != nullptr
                              ? const_cast<char*>(entry.LegacyFileName.data())
                              : nullptr;
        legacy.HParentWnd = entry.WideFile.HParentWnd;
        legacy.dwDesiredAccess = entry.WideFile.dwDesiredAccess;
        legacy.dwShareMode = entry.WideFile.dwShareMode;
        legacy.dwCreationDisposition = entry.WideFile.dwCreationDisposition;
        legacy.dwFlagsAndAttributes = entry.WideFile.dwFlagsAndAttributes;
        legacy.WholeFileAllocated = entry.WideFile.WholeFileAllocated;
    }

private:
    std::mutex Mutex;
    std::unordered_map<sdk107::SAFE_FILE*, std::shared_ptr<CEntry>> Files;
};

CLegacySalamanderSafeFile::CLegacySalamanderSafeFile(
    ::CSalamanderSafeFileAbstract& wideSafeFile)
    : WideSafeFile(wideSafeFile), State(std::make_unique<CState>())
{
}

CLegacySalamanderSafeFile::~CLegacySalamanderSafeFile()
{
    while (const std::shared_ptr<CState::CEntry> entry = State->PopOne())
        WideSafeFile.SafeFileClose(&entry->WideFile);
}

BOOL WINAPI CLegacySalamanderSafeFile::SafeFileOpen(
    sdk107::SAFE_FILE* file, const char* fileName, DWORD dwDesiredAccess,
    DWORD dwShareMode, DWORD dwCreationDisposition, DWORD dwFlagsAndAttributes,
    HWND hParent, DWORD flags, DWORD* pressedButton, DWORD* silentMask)
{
    if (file == nullptr || State->Find(file) != nullptr)
    {
        if (pressedButton != nullptr)
            *pressedButton = DIALOG_CANCEL;
        SetLastError(ERROR_INVALID_HANDLE);
        return FALSE;
    }

    std::wstring fileNameW;
    const wchar_t* liveFileName = nullptr;
    if (!WidenOptional(fileName, fileNameW, liveFileName))
    {
        if (pressedButton != nullptr)
            *pressedButton = DIALOG_CANCEL;
        return FALSE;
    }
    std::shared_ptr<CState::CEntry> entry;
    try
    {
        entry = std::make_shared<CState::CEntry>(fileName);
    }
    catch (const std::bad_alloc&)
    {
        if (pressedButton != nullptr)
            *pressedButton = DIALOG_CANCEL;
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }
    catch (const std::length_error&)
    {
        if (pressedButton != nullptr)
            *pressedButton = DIALOG_CANCEL;
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return FALSE;
    }
    const BOOL result = WideSafeFile.SafeFileOpen(
        &entry->WideFile, liveFileName, dwDesiredAccess,
        dwShareMode, dwCreationDisposition, dwFlagsAndAttributes, hParent,
        flags, pressedButton, silentMask);
    if (!result)
        return FALSE;

    if (!State->Store(file, entry))
    {
        const DWORD storeError = GetLastError();
        WideSafeFile.SafeFileClose(&entry->WideFile);
        if (pressedButton != nullptr)
            *pressedButton = DIALOG_CANCEL;
        SetLastError(storeError != ERROR_SUCCESS ? storeError
                                                 : ERROR_INVALID_HANDLE);
        return FALSE;
    }

    CState::SyncLegacy(*entry, *file);
    return TRUE;
}

HANDLE WINAPI CLegacySalamanderSafeFile::SafeFileCreate(
    const char* fileName, DWORD dwDesiredAccess, DWORD dwShareMode,
    DWORD dwFlagsAndAttributes, BOOL isDir, HWND hParent,
    const char* srcFileName, const char* srcFileInfo, DWORD* silentMask,
    BOOL allowSkip, BOOL* skipped, char* skipPath, int skipPathMax,
    sdk107::CQuadWord* allocateWholeFile, sdk107::SAFE_FILE* file)
{
    const bool needsContext = !isDir && file != nullptr;
    if (skipPath != nullptr && skipPathMax <= 0)
    {
        if (skipped != nullptr)
            *skipped = FALSE;
        SetLastError(ERROR_INVALID_PARAMETER);
        return INVALID_HANDLE_VALUE;
    }
    if (needsContext && State->Find(file) != nullptr)
    {
        if (skipped != nullptr)
            *skipped = FALSE;
        if (skipPath != nullptr && skipPathMax > 0)
            skipPath[0] = '\0';
        SetLastError(ERROR_INVALID_HANDLE);
        return INVALID_HANDLE_VALUE;
    }

    std::wstring fileNameW;
    std::wstring srcFileNameW;
    std::wstring srcFileInfoW;
    const wchar_t* liveFileName = nullptr;
    const wchar_t* liveSourceName = nullptr;
    const wchar_t* liveSourceInfo = nullptr;
    if (!WidenOptional(fileName, fileNameW, liveFileName) ||
        !WidenOptional(srcFileName, srcFileNameW, liveSourceName) ||
        !WidenOptional(srcFileInfo, srcFileInfoW, liveSourceInfo))
    {
        if (skipped != nullptr)
            *skipped = FALSE;
        if (skipPath != nullptr && skipPathMax > 0)
            skipPath[0] = '\0';
        return INVALID_HANDLE_VALUE;
    }
    std::vector<wchar_t> skipPathW;
    if (skipPath != nullptr && skipPathMax > 0)
    {
        try
        {
            skipPathW.assign(static_cast<std::size_t>(skipPathMax), L'\0');
        }
        catch (const std::bad_alloc&)
        {
            SetLastError(ERROR_NOT_ENOUGH_MEMORY);
            return INVALID_HANDLE_VALUE;
        }
        catch (const std::length_error&)
        {
            SetLastError(ERROR_INSUFFICIENT_BUFFER);
            return INVALID_HANDLE_VALUE;
        }
    }

    ::CQuadWord allocateWholeFileW;
    ::CQuadWord* allocateWholeFilePtr = nullptr;
    if (allocateWholeFile != nullptr)
    {
        allocateWholeFileW = QuadWordToLive(*allocateWholeFile);
        allocateWholeFilePtr = &allocateWholeFileW;
    }

    std::shared_ptr<CState::CEntry> entry;
    if (needsContext)
    {
        try
        {
            entry = std::make_shared<CState::CEntry>(fileName);
        }
        catch (const std::bad_alloc&)
        {
            SetLastError(ERROR_NOT_ENOUGH_MEMORY);
            return INVALID_HANDLE_VALUE;
        }
        catch (const std::length_error&)
        {
            SetLastError(ERROR_INSUFFICIENT_BUFFER);
            return INVALID_HANDLE_VALUE;
        }
    }

    HANDLE result = WideSafeFile.SafeFileCreate(
        liveFileName, dwDesiredAccess, dwShareMode, dwFlagsAndAttributes, isDir,
        hParent, liveSourceName, liveSourceInfo, silentMask, allowSkip,
        skipped, skipPathW.empty() ? nullptr : skipPathW.data(), skipPathMax,
        allocateWholeFilePtr, entry != nullptr ? &entry->WideFile : nullptr);

    if (allocateWholeFile != nullptr)
        QuadWordToLegacy(allocateWholeFileW, *allocateWholeFile);
    if (!skipPathW.empty() && skipped != nullptr && *skipped)
    {
        skipPathW.back() = L'\0';
        if (!CopyWideOutputToLegacy(skipPathW.data(), skipPath, skipPathMax))
        {
            const DWORD conversionError = GetLastError();
            *skipped = FALSE;
            if (result != INVALID_HANDLE_VALUE)
            {
                if (entry != nullptr)
                    WideSafeFile.SafeFileClose(&entry->WideFile);
                else if (result != nullptr)
                    CloseHandle(result);
            }
            SetLastError(conversionError);
            return INVALID_HANDLE_VALUE;
        }
    }

    if (result != INVALID_HANDLE_VALUE && entry != nullptr)
    {
        if (!State->Store(file, entry))
        {
            const DWORD storeError = GetLastError();
            WideSafeFile.SafeFileClose(&entry->WideFile);
            SetLastError(storeError != ERROR_SUCCESS ? storeError
                                                     : ERROR_INVALID_HANDLE);
            return INVALID_HANDLE_VALUE;
        }
        CState::SyncLegacy(*entry, *file);
    }
    return result;
}

void WINAPI CLegacySalamanderSafeFile::SafeFileClose(sdk107::SAFE_FILE* file)
{
    if (file == nullptr)
        return;
    const std::shared_ptr<CState::CEntry> entry = State->Remove(file);
    if (entry == nullptr)
    {
        SetLastError(ERROR_INVALID_HANDLE);
        return;
    }

    CState::SyncLive(*file, *entry);
    WideSafeFile.SafeFileClose(&entry->WideFile);
    *file = {};
}

BOOL WINAPI CLegacySalamanderSafeFile::SafeFileSeek(
    sdk107::SAFE_FILE* file, sdk107::CQuadWord* distance,
    DWORD moveMethod, DWORD* error)
{
    const std::shared_ptr<CState::CEntry> entry = State->Find(file);
    if (entry == nullptr || distance == nullptr)
    {
        if (error != nullptr)
            *error = ERROR_INVALID_HANDLE;
        SetLastError(ERROR_INVALID_HANDLE);
        return FALSE;
    }

    CState::SyncLive(*file, *entry);
    ::CQuadWord distanceW = QuadWordToLive(*distance);
    const BOOL result = WideSafeFile.SafeFileSeek(
        &entry->WideFile, &distanceW, moveMethod, error);
    QuadWordToLegacy(distanceW, *distance);
    CState::SyncLegacy(*entry, *file);
    return result;
}

BOOL WINAPI CLegacySalamanderSafeFile::SafeFileSeekMsg(
    sdk107::SAFE_FILE* file, sdk107::CQuadWord* distance, DWORD moveMethod,
    HWND hParent, DWORD flags, DWORD* pressedButton, DWORD* silentMask,
    BOOL seekForRead)
{
    const std::shared_ptr<CState::CEntry> entry = State->Find(file);
    if (entry == nullptr || distance == nullptr)
    {
        if (pressedButton != nullptr)
            *pressedButton = DIALOG_CANCEL;
        SetLastError(ERROR_INVALID_HANDLE);
        return FALSE;
    }

    CState::SyncLive(*file, *entry);
    ::CQuadWord distanceW = QuadWordToLive(*distance);
    const BOOL result = WideSafeFile.SafeFileSeekMsg(
        &entry->WideFile, &distanceW, moveMethod, hParent, flags,
        pressedButton, silentMask, seekForRead);
    QuadWordToLegacy(distanceW, *distance);
    CState::SyncLegacy(*entry, *file);
    return result;
}

BOOL WINAPI CLegacySalamanderSafeFile::SafeFileGetSize(
    sdk107::SAFE_FILE* file, sdk107::CQuadWord* fileSize, DWORD* error)
{
    const std::shared_ptr<CState::CEntry> entry = State->Find(file);
    if (entry == nullptr || fileSize == nullptr)
    {
        if (error != nullptr)
            *error = ERROR_INVALID_HANDLE;
        SetLastError(ERROR_INVALID_HANDLE);
        return FALSE;
    }

    CState::SyncLive(*file, *entry);
    ::CQuadWord fileSizeW;
    fileSizeW.LoDWord = 0;
    fileSizeW.HiDWord = 0;
    const BOOL result = WideSafeFile.SafeFileGetSize(
        &entry->WideFile, &fileSizeW, error);
    QuadWordToLegacy(fileSizeW, *fileSize);
    CState::SyncLegacy(*entry, *file);
    return result;
}

BOOL WINAPI CLegacySalamanderSafeFile::SafeFileRead(
    sdk107::SAFE_FILE* file, LPVOID lpBuffer, DWORD nNumberOfBytesToRead,
    LPDWORD lpNumberOfBytesRead, HWND hParent, DWORD flags,
    DWORD* pressedButton, DWORD* silentMask)
{
    const std::shared_ptr<CState::CEntry> entry = State->Find(file);
    if (entry == nullptr)
    {
        if (pressedButton != nullptr)
            *pressedButton = DIALOG_CANCEL;
        SetLastError(ERROR_INVALID_HANDLE);
        return FALSE;
    }

    CState::SyncLive(*file, *entry);
    const BOOL result = WideSafeFile.SafeFileRead(
        &entry->WideFile, lpBuffer, nNumberOfBytesToRead,
        lpNumberOfBytesRead, hParent, flags, pressedButton, silentMask);
    CState::SyncLegacy(*entry, *file);
    return result;
}

BOOL WINAPI CLegacySalamanderSafeFile::SafeFileWrite(
    sdk107::SAFE_FILE* file, LPVOID lpBuffer, DWORD nNumberOfBytesToWrite,
    LPDWORD lpNumberOfBytesWritten, HWND hParent, DWORD flags,
    DWORD* pressedButton, DWORD* silentMask)
{
    const std::shared_ptr<CState::CEntry> entry = State->Find(file);
    if (entry == nullptr)
    {
        if (pressedButton != nullptr)
            *pressedButton = DIALOG_CANCEL;
        SetLastError(ERROR_INVALID_HANDLE);
        return FALSE;
    }

    CState::SyncLive(*file, *entry);
    const BOOL result = WideSafeFile.SafeFileWrite(
        &entry->WideFile, lpBuffer, nNumberOfBytesToWrite,
        lpNumberOfBytesWritten, hParent, flags, pressedButton, silentMask);
    CState::SyncLegacy(*entry, *file);
    return result;
}

CLegacySalamanderPluginEntry::CLegacySalamanderPluginEntry(
    ::CSalamanderPluginEntryAbstract& wideEntry,
    const CLegacyEntryFacadeSet& facades)
    : WideEntry(wideEntry), Facades(facades)
{
}

int WINAPI CLegacySalamanderPluginEntry::GetVersion()
{
    return WideEntry.GetVersion();
}

HWND WINAPI CLegacySalamanderPluginEntry::GetParentWindow()
{
    return WideEntry.GetParentWindow();
}

sdk107::CSalamanderDebugAbstract* WINAPI CLegacySalamanderPluginEntry::GetSalamanderDebug()
{
    return Facades.Debug;
}

BOOL WINAPI CLegacySalamanderPluginEntry::SetBasicPluginData(
    const char* pluginName, DWORD functions, const char* version,
    const char* copyright, const char* description, const char* regKeyName,
    const char* extensions, const char* fsName)
{
    std::wstring pluginNameW;
    std::wstring versionW;
    std::wstring copyrightW;
    std::wstring descriptionW;
    std::wstring regKeyNameW;
    std::wstring extensionsW;
    std::wstring fsNameW;
    const wchar_t* livePluginName = nullptr;
    const wchar_t* liveVersion = nullptr;
    const wchar_t* liveCopyright = nullptr;
    const wchar_t* liveDescription = nullptr;
    const wchar_t* liveRegKeyName = nullptr;
    const wchar_t* liveExtensions = nullptr;
    const wchar_t* liveFSName = nullptr;
    if (!WidenOptional(pluginName, pluginNameW, livePluginName) ||
        !WidenOptional(version, versionW, liveVersion) ||
        !WidenOptional(copyright, copyrightW, liveCopyright) ||
        !WidenOptional(description, descriptionW, liveDescription) ||
        !WidenOptional(regKeyName, regKeyNameW, liveRegKeyName) ||
        !WidenOptional(extensions, extensionsW, liveExtensions) ||
        !WidenOptional(fsName, fsNameW, liveFSName))
        return FALSE;
    return WideEntry.SetBasicPluginData(
        livePluginName, functions, liveVersion, liveCopyright, liveDescription,
        liveRegKeyName, liveExtensions, liveFSName);
}

sdk107::CSalamanderGeneralAbstract* WINAPI CLegacySalamanderPluginEntry::GetSalamanderGeneral()
{
    return Facades.General;
}

DWORD WINAPI CLegacySalamanderPluginEntry::GetLoadInformation()
{
    return WideEntry.GetLoadInformation();
}

HINSTANCE WINAPI CLegacySalamanderPluginEntry::LoadLanguageModule(HWND parent,
                                                                  const char* pluginName)
{
    std::wstring pluginNameW;
    const wchar_t* livePluginName = nullptr;
    return WidenOptional(pluginName, pluginNameW, livePluginName)
               ? WideEntry.LoadLanguageModule(parent, livePluginName)
               : nullptr;
}

WORD WINAPI CLegacySalamanderPluginEntry::GetCurrentSalamanderLanguageID()
{
    return WideEntry.GetCurrentSalamanderLanguageID();
}

sdk107::CSalamanderGUIAbstract* WINAPI CLegacySalamanderPluginEntry::GetSalamanderGUI()
{
    return Facades.GUI;
}

sdk107::CSalamanderSafeFileAbstract* WINAPI CLegacySalamanderPluginEntry::GetSalamanderSafeFile()
{
    return Facades.SafeFile;
}

void WINAPI CLegacySalamanderPluginEntry::SetPluginHomePageURL(const char* url)
{
    std::wstring urlW;
    const wchar_t* liveUrl = nullptr;
    if (WidenOptional(url, urlW, liveUrl))
        WideEntry.SetPluginHomePageURL(liveUrl);
}

BOOL WINAPI CLegacySalamanderPluginEntry::AddFSName(const char* fsName,
                                                     int* newFSNameIndex)
{
    std::wstring fsNameW;
    const wchar_t* liveFSName = nullptr;
    return WidenOptional(fsName, fsNameW, liveFSName)
               ? WideEntry.AddFSName(liveFSName, newFSNameIndex)
               : FALSE;
}

} // namespace sally::compat
