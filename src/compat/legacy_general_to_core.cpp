// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

// Frozen-v107 General reviewed forwarders over the live wide interface.
// Construction remains absent until every typed/conversion method is complete.

#define NOMINMAX
#include <windows.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <limits>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "common/IFileSystem.h"
#include "common/unicode/WideVariableExpansion.h"
#include "common/widepath.h"
#include "compat/legacy_convert.h"
#include "compat/legacy_to_core.h"
#include "plugins/shared/plugin_narrow_compat.h"

namespace sally::compat
{

namespace
{

bool WidenOptionalGeneralText(const char* value, std::wstring& storage,
                              const wchar_t*& wide)
{
    storage.clear();
    wide = nullptr;
    if (value == nullptr)
        return true;
    if (!WidenPluginText(value, storage))
        return false;
    wide = storage.c_str();
    return true;
}

int BoundedPluginLength(const char* value, int byteLimit)
{
    int length = 0;
    while (length < byteLimit && value[length] != '\0')
        ++length;
    return length;
}

template <class TLegacy, class TWide, class TWrapper>
class CStableWrappers
{
public:
    TLegacy* Publish(TWide* wide)
    {
        if (wide == nullptr)
            return nullptr;
        const auto found = Wrappers.find(wide);
        if (found != Wrappers.end())
            return found->second.get();

        try
        {
            auto wrapper = std::make_unique<TWrapper>(*wide);
            TLegacy* legacy = wrapper.get();
            const auto published = Wrappers.emplace(wide, std::move(wrapper));
            return published.first->second.get();
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
        catch (...)
        {
            SetLastError(ERROR_GEN_FAILURE);
            return nullptr;
        }
    }

private:
    std::unordered_map<TWide*, std::unique_ptr<TWrapper>> Wrappers;
};

struct CLegacyPanelRowMirror
{
    ~CLegacyPanelRowMirror() { FreeLegacyFileData(Row); }

    const ::CFileData* Wide = nullptr;
    int Panel = 0;
    sdk107::CFileData Row = {};
};

} // namespace

bool CLegacyViewerDataBridge::Prepare(
    const sdk107::CSalamanderPluginViewerData* legacy)
{
    Storage.clear();
    FileName.clear();
    Caption.clear();
    if (legacy == nullptr)
        return true;
    if (legacy->Size <
        static_cast<int>(sizeof(sdk107::CSalamanderPluginViewerData)))
        return false;

    const wchar_t* wideFileName = nullptr;
    if (!WidenOptionalGeneralText(legacy->FileName, FileName,
                                  wideFileName))
        return false;
    const wchar_t* wideCaption = nullptr;
    if (legacy->Size == static_cast<int>(
                            sizeof(sdk107::CSalamanderPluginInternalViewerData)))
    {
        const auto* internal = static_cast<
            const sdk107::CSalamanderPluginInternalViewerData*>(legacy);
        if (!WidenOptionalGeneralText(internal->Caption, Caption,
                                      wideCaption))
            return false;
    }

    try
    {
        Storage.resize(static_cast<std::size_t>(legacy->Size));
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
    std::memcpy(Storage.data(), legacy, Storage.size());
    auto* wide =
        reinterpret_cast<::CSalamanderPluginViewerData*>(Storage.data());
    wide->FileName = wideFileName;
    if (legacy->Size == static_cast<int>(
                            sizeof(sdk107::CSalamanderPluginInternalViewerData)))
    {
        auto* internal = reinterpret_cast<
            ::CSalamanderPluginInternalViewerData*>(wide);
        internal->Caption = wideCaption;
    }
    return true;
}

::CSalamanderPluginViewerData* CLegacyViewerDataBridge::Data()
{
    return Storage.empty()
               ? nullptr
               : reinterpret_cast<::CSalamanderPluginViewerData*>(
                     Storage.data());
}

bool CopyLegacyMessageBoxParams(const sdk107::MSGBOXEX_PARAMS* legacy,
                                CLegacyMessageBoxParams& wide)
{
    if (legacy == nullptr)
        return false;
    CLegacyMessageBoxParams converted;
    const wchar_t* ignored = nullptr;
    if (!WidenOptionalGeneralText(legacy->Text, converted.Text, ignored) ||
        !WidenOptionalGeneralText(legacy->Caption, converted.Caption,
                                  ignored) ||
        !WidenOptionalGeneralText(legacy->CheckBoxText,
                                  converted.CheckBoxText, ignored) ||
        !WidenOptionalGeneralText(legacy->AliasBtnNames,
                                  converted.AliasBtnNames, ignored) ||
        !WidenOptionalGeneralText(legacy->URL, converted.URL, ignored) ||
        !WidenOptionalGeneralText(legacy->URLText, converted.URLText,
                                  ignored))
        return false;
    converted.Params.HParent = legacy->HParent;
    converted.Params.Flags = legacy->Flags;
    converted.Params.HIcon = legacy->HIcon;
    converted.Params.ContextHelpId = legacy->ContextHelpId;
    converted.Params.HelpCallback = legacy->HelpCallback;
    converted.Params.CheckBoxValue = legacy->CheckBoxValue;
    wide = std::move(converted);
    // std::wstring moves may change storage addresses, so publish pointers only
    // after ownership reaches its final call-scoped object.
    wide.Params.Text = legacy->Text != nullptr ? wide.Text.c_str() : nullptr;
    wide.Params.Caption = legacy->Caption != nullptr ? wide.Caption.c_str() : nullptr;
    wide.Params.CheckBoxText = legacy->CheckBoxText != nullptr ? wide.CheckBoxText.c_str() : nullptr;
    wide.Params.AliasBtnNames = legacy->AliasBtnNames != nullptr ? wide.AliasBtnNames.c_str() : nullptr;
    wide.Params.URL = legacy->URL != nullptr ? wide.URL.c_str() : nullptr;
    wide.Params.URLText = legacy->URLText != nullptr ? wide.URLText.c_str() : nullptr;
    return true;
}

bool LegacyVariableNameEquals(const wchar_t* segment, int segmentLength,
                              const std::wstring& name)
{
    if (segment == nullptr || segmentLength < 0 ||
        name.size() > static_cast<std::size_t>((std::numeric_limits<int>::max)()) ||
        static_cast<std::size_t>(segmentLength) != name.size())
        return false;
    return CompareStringOrdinal(segment, segmentLength, name.c_str(),
                                static_cast<int>(name.size()), TRUE) ==
           CSTR_EQUAL;
}

const wchar_t* WINAPI FailedLiveVariableCallback(HWND, void*)
{
    return nullptr;
}

bool MapLegacyHtmlHelpCommand(sdk107::CHtmlHelpCommand legacy,
                              ::CHtmlHelpCommand& wide)
{
    switch (legacy)
    {
    case sdk107::HHCDisplayTOC:
        wide = ::HHCDisplayTOC;
        return true;
    case sdk107::HHCDisplayIndex:
        wide = ::HHCDisplayIndex;
        return true;
    case sdk107::HHCDisplaySearch:
        wide = ::HHCDisplaySearch;
        return true;
    case sdk107::HHCDisplayContext:
        wide = ::HHCDisplayContext;
        return true;
    default:
        return false;
    }
}

bool PrepareLegacyThemeInfo(const sdk107::CSalamanderThemeInfo* legacy,
                            ::CSalamanderThemeInfo& wide)
{
    if (legacy == nullptr || legacy->Size < sizeof(*legacy))
        return false;
    wide = {};
    wide.Size = sizeof(wide);
    return true;
}

void CommitLegacyThemeInfo(const ::CSalamanderThemeInfo& wide,
                           sdk107::CSalamanderThemeInfo& legacy)
{
    legacy.ThemeMode = wide.ThemeMode;
    legacy.UseDarkColors = wide.UseDarkColors;
}

bool CopyWideGeneralOutputExact(const wchar_t* value, char* output,
                                std::size_t outputSize)
{
    if (output == nullptr || outputSize == 0)
        return false;
    output[0] = '\0';
    std::string prepared;
    if (PrepareWideGeneralOutputExact(value, outputSize, prepared) !=
        GeneralOutputCommitStatus::Complete)
        return false;
    std::memcpy(output, prepared.c_str(), prepared.size() + 1);
    return true;
}

bool PrepareLegacyRegistryQueryOutput(const wchar_t* wide,
                                      std::size_t wideBytes,
                                      std::string& output)
{
    output.clear();
    if (wideBytes % sizeof(wchar_t) != 0 ||
        (wide == nullptr && wideBytes != 0))
        return false;

    const std::size_t wideChars = wideBytes / sizeof(wchar_t);
    if (wideChars == 0)
        return true;
    if (wideChars >
        static_cast<std::size_t>((std::numeric_limits<int>::max)()))
        return false;

    // lpcbData is a byte count on both sides. Preserve every UTF-16 code unit,
    // including the terminator the live helper guarantees, before exactly
    // projecting the frozen ACP record.
    const NarrowResult narrowed =
        NarrowExact(std::wstring(wide, wideBytes / sizeof(wchar_t)));
    if (!narrowed.ok)
        return false;
    output = narrowed.value;
    return true;
}

bool PrepareLegacyMaskNameOutput(const wchar_t* wide,
                                 std::size_t outputSize,
                                 std::string& output)
{
    output.clear();
    if (wide == nullptr || outputSize == 0)
        return false;

    std::size_t wideLength = std::wcslen(wide);
    if (wideLength == 0)
        return true;

    const NarrowResult complete =
        NarrowExact(std::wstring(wide, wideLength));
    if (!complete.ok)
        return false;
    if (complete.value.size() + 1 <= outputSize)
    {
        output = complete.value;
        return true;
    }

    // The frozen method promises truncation rather than refusal. Shorten by
    // whole wide characters so a multibyte ACP sequence is never split, and
    // retain MaskName's invariant that the returned name has no trailing dot.
    while (wideLength > 0)
    {
        --wideLength;
        while (wideLength > 0 && wide[wideLength - 1] == L'.')
            --wideLength;
        if (wideLength == 0)
            return true;

        const NarrowResult prefix =
            NarrowExact(std::wstring(wide, wideLength));
        if (!prefix.ok)
            return false;
        if (prefix.value.size() + 1 <= outputSize)
        {
            output = prefix.value;
            return true;
        }
    }
    return true;
}

GeneralOutputCommitStatus PrepareLegacyFullNameOutputs(
    const wchar_t* wideName, const wchar_t* wideNextFocus,
    std::size_t nameOutputSize, std::string& nameOutput,
    std::string& nextFocusOutput, bool& publishNextFocus)
{
    nameOutput.clear();
    nextFocusOutput.clear();
    publishNextFocus = false;
    if (wideName == nullptr)
        return GeneralOutputCommitStatus::NotRepresentable;

    const GeneralOutputCommitStatus nameStatus =
        PrepareWideGeneralOutputExact(wideName, nameOutputSize, nameOutput);
    if (nameStatus != GeneralOutputCommitStatus::Complete)
        return nameStatus;
    if (wideNextFocus == nullptr || wideNextFocus[0] == L'\0')
        return GeneralOutputCommitStatus::Complete;

    const NarrowResult focus = NarrowExact(wideNextFocus);
    if (!focus.ok)
    {
        nameOutput.clear();
        return GeneralOutputCommitStatus::NotRepresentable;
    }

    // Frozen v107 exposes no focus capacity. Its implementation writes only
    // when the relative leaf occupies fewer than MAX_PATH bytes, and otherwise
    // leaves the caller's focus buffer untouched.
    if (focus.value.size() >= MAX_PATH)
        return GeneralOutputCommitStatus::Complete;

    nextFocusOutput = focus.value;
    publishNextFocus = true;
    return GeneralOutputCommitStatus::Complete;
}

bool PrepareLegacyTruncatedGeneralTextOutput(const wchar_t* wide,
                                             std::size_t outputSize,
                                             std::string& output)
{
    output.clear();
    if (wide == nullptr || outputSize == 0)
        return false;

    // lstrcpyn in the frozen facade promises a terminated prefix. Search back
    // from the complete live text so the byte-sized legacy allocation receives
    // the longest exact ACP prefix that fits without splitting a character.
    std::size_t wideLength = std::wcslen(wide);
    for (;;)
    {
        const NarrowResult narrowed =
            NarrowExact(std::wstring(wide, wideLength));
        if (narrowed.ok && narrowed.value.size() < outputSize)
        {
            output = narrowed.value;
            return true;
        }
        if (wideLength == 0)
            return true;
        --wideLength;
    }
}

static bool PrepareLegacyDoubleStringOutput(const wchar_t* first,
                                            const wchar_t* second,
                                            std::size_t outputSize,
                                            std::string& output)
{
    output.clear();
    if (first == nullptr || second == nullptr || outputSize < 2)
        return false;

    const NarrowResult firstComplete = NarrowExact(first);
    const NarrowResult secondComplete = NarrowExact(second);
    if (!firstComplete.ok || !secondComplete.ok)
        return false;

    const std::size_t payloadSize = outputSize - 2;
    std::string firstOutput = firstComplete.value;
    std::string secondOutput = secondComplete.value;
    if (firstOutput.size() > payloadSize ||
        secondOutput.size() > payloadSize - firstOutput.size())
    {
        // The frozen API measures the two strings in ACP bytes and shortens
        // both sides as evenly as possible. The live owner has already made
        // the same pairing decision in UTF-16; this projects it back without
        // a split multibyte character.
        const std::size_t half = outputSize / 2 - 1;
        std::size_t firstLimit = half;
        std::size_t secondLimit = half;
        if (secondComplete.value.size() <= half)
        {
            firstLimit = payloadSize - secondComplete.value.size();
            secondLimit = secondComplete.value.size();
        }
        else if (firstComplete.value.size() <= half)
        {
            firstLimit = firstComplete.value.size();
            secondLimit = payloadSize - firstComplete.value.size();
        }

        if (!PrepareLegacyTruncatedGeneralTextOutput(
                first, firstLimit + 1, firstOutput) ||
            !PrepareLegacyTruncatedGeneralTextOutput(
                second, secondLimit + 1, secondOutput))
            return false;
    }

    output.reserve(firstOutput.size() + secondOutput.size() + 2);
    output.append(firstOutput);
    output.push_back('\0');
    output.append(secondOutput);
    output.push_back('\0');
    return true;
}

char* PublishLegacyGeneralErrorText(const wchar_t* wide)
{
    static std::array<std::array<char, MAX_PATH + 20>, 10> buffers = {};
    static std::size_t next = 0;
    static SRWLOCK lock = SRWLOCK_INIT;

    std::string prepared;
    if (!PrepareLegacyTruncatedGeneralTextOutput(
            wide, buffers.front().size(), prepared))
        prepared.clear();

    AcquireSRWLockExclusive(&lock);
    char* published = buffers[next].data();
    std::memcpy(published, prepared.c_str(), prepared.size() + 1);
    next = (next + 1) % buffers.size();
    ReleaseSRWLockExclusive(&lock);
    return published;
}

bool LegacyByteOffsetForWidePointer(const char* legacy,
                                    const std::wstring& wide,
                                    const wchar_t* widePointer,
                                    std::size_t& legacyOffset)
{
    legacyOffset = std::string::npos;
    if (legacy == nullptr || widePointer == nullptr ||
        wide.size() > std::numeric_limits<std::size_t>::max() / sizeof(wchar_t))
        return false;

    const std::uintptr_t begin =
        reinterpret_cast<std::uintptr_t>(wide.c_str());
    const std::uintptr_t byteLength = wide.size() * sizeof(wchar_t);
    const std::uintptr_t end = begin + byteLength;
    const std::uintptr_t pointer =
        reinterpret_cast<std::uintptr_t>(widePointer);
    if (end < begin || pointer < begin || pointer > end ||
        (pointer - begin) % sizeof(wchar_t) != 0)
        return false;

    const std::size_t wideOffset =
        static_cast<std::size_t>((pointer - begin) / sizeof(wchar_t));
    if (wideOffset == 0)
    {
        legacyOffset = 0;
        return true;
    }
    const NarrowResult prefix = NarrowExact(wide.substr(0, wideOffset));
    if (!prefix.ok)
        return false;

    const std::size_t legacyLength = std::strlen(legacy);
    if (legacyLength < prefix.value.size() ||
        std::memcmp(legacy, prefix.value.data(), prefix.value.size()) != 0)
        return false;

    legacyOffset = prefix.value.size();
    return true;
}

bool WideOffsetForLegacyPointer(const char* legacy,
                                const std::wstring& wide,
                                const char* legacyPointer,
                                std::size_t& wideOffset)
{
    wideOffset = std::string::npos;
    if (legacy == nullptr || legacyPointer == nullptr)
        return false;

    const std::size_t legacyLength = std::strlen(legacy);
    const std::uintptr_t begin = reinterpret_cast<std::uintptr_t>(legacy);
    const std::uintptr_t pointer =
        reinterpret_cast<std::uintptr_t>(legacyPointer);
    const std::uintptr_t end = begin + legacyLength;
    if (end < begin || pointer < begin || pointer > end)
        return false;

    const std::size_t legacyOffset =
        static_cast<std::size_t>(pointer - begin);
    if (legacyOffset >
        static_cast<std::size_t>(std::numeric_limits<int>::max()))
        return false;

    std::wstring prefix;
    if (!WidenPluginSpan(legacy, static_cast<int>(legacyOffset), prefix) ||
        prefix.size() > wide.size() ||
        wide.compare(0, prefix.size(), prefix) != 0)
        return false;

    const NarrowResult exactPrefix = NarrowExact(prefix);
    if (!exactPrefix.ok || exactPrefix.value.size() != legacyOffset ||
        std::memcmp(legacy, exactPrefix.value.data(), legacyOffset) != 0)
        return false;

    wideOffset = prefix.size();
    return true;
}

int LegacyByteLengthForWidePrefix(const char* legacy,
                                  const std::wstring& wide,
                                  int wideLength)
{
    if (legacy == nullptr || wideLength <= 0 ||
        static_cast<std::size_t>(wideLength) > wide.size())
        return 0;

    // Live CommonPrefixLength returns a WCHAR index. Frozen v107 returned the
    // pointer difference inside path1, so its unit is ACP bytes. Reproject the
    // exact prefix and prove it is the original spelling before returning it.
    std::size_t legacyOffset = std::string::npos;
    if (!LegacyByteOffsetForWidePointer(
            legacy, wide, wide.c_str() + wideLength, legacyOffset) ||
        legacyOffset > static_cast<std::size_t>(std::numeric_limits<int>::max()))
        return 0;
    return static_cast<int>(legacyOffset);
}

bool PrepareLegacyVarStringErrorPositions(
    const char* legacy, const std::wstring& wide,
    int wideErrorPos1, int wideErrorPos2,
    int& legacyErrorPos1, int& legacyErrorPos2)
{
    if (legacy == nullptr || wideErrorPos1 < 0 || wideErrorPos2 < 0 ||
        static_cast<std::size_t>(wideErrorPos1) > wide.size() ||
        static_cast<std::size_t>(wideErrorPos2) > wide.size())
        return false;

    std::size_t preparedErrorPos1 = std::string::npos;
    std::size_t preparedErrorPos2 = std::string::npos;
    if (!LegacyByteOffsetForWidePointer(
            legacy, wide, wide.c_str() + wideErrorPos1,
            preparedErrorPos1) ||
        !LegacyByteOffsetForWidePointer(
            legacy, wide, wide.c_str() + wideErrorPos2,
            preparedErrorPos2) ||
        preparedErrorPos1 >
            static_cast<std::size_t>(std::numeric_limits<int>::max()) ||
        preparedErrorPos2 >
            static_cast<std::size_t>(std::numeric_limits<int>::max()))
        return false;

    legacyErrorPos1 = static_cast<int>(preparedErrorPos1);
    legacyErrorPos2 = static_cast<int>(preparedErrorPos2);
    return true;
}

bool PrepareLegacyVarStringExpansionOutput(
    const std::wstring& wideOutput,
    const sally::unicode::WideTextRange* widePlacements,
    std::size_t widePlacementCount, std::size_t outputSize, std::string& output,
    std::vector<DWORD>& placements)
{
    output.clear();
    placements.clear();
    if (outputSize == 0 ||
        (widePlacementCount > 0 && widePlacements == nullptr))
        return false;

    const NarrowResult preparedOutput = NarrowExact(wideOutput);
    if (!preparedOutput.ok || preparedOutput.value.size() + 1 > outputSize)
        return false;

    // The placement record has two WORD fields. Refuse an ambiguous wrapped
    // live record instead of mapping a truncated WCHAR offset to the wrong ACP
    // byte offset.
    if (widePlacementCount > 0 &&
        (wideOutput.size() > MAXWORD || preparedOutput.value.size() > MAXWORD))
        return false;

    std::vector<DWORD> preparedPlacements;
    preparedPlacements.reserve(widePlacementCount);
    for (std::size_t index = 0; index < widePlacementCount; ++index)
    {
        const std::size_t wideOffset = widePlacements[index].Offset;
        const std::size_t wideLength = widePlacements[index].Length;
        if (wideOffset > wideOutput.size() ||
            wideLength > wideOutput.size() - wideOffset)
            return false;

        const NarrowResult prefix =
            NarrowExact(wideOutput.substr(0, wideOffset));
        const NarrowResult value =
            NarrowExact(wideOutput.substr(wideOffset, wideLength));
        if (!prefix.ok || !value.ok || prefix.value.size() > MAXWORD ||
            value.value.size() > MAXWORD)
            return false;
        preparedPlacements.push_back(
            MAKELPARAM(prefix.value.size(), value.value.size()));
    }

    output = preparedOutput.value;
    placements = std::move(preparedPlacements);
    return true;
}

int CommitLegacyRootPathOutput(const wchar_t* wide, int wideLength,
                               char* output, std::size_t outputSize)
{
    if (output == nullptr || outputSize == 0)
        return 0;
    output[0] = '\0';
    if (wide == nullptr || wideLength <= 0 || outputSize < 2 ||
        std::wcslen(wide) != static_cast<std::size_t>(wideLength))
        return 0;

    const NarrowResult narrowed = NarrowExact(
        std::wstring(wide, static_cast<std::size_t>(wideLength)));
    if (!narrowed.ok || narrowed.value.empty() ||
        narrowed.value.back() != '\\')
        return 0;

    if (narrowed.value.size() + 1 <= outputSize)
    {
        if (narrowed.value.size() >
            static_cast<std::size_t>(std::numeric_limits<int>::max()))
            return 0;
        std::memcpy(output, narrowed.value.c_str(),
                    narrowed.value.size() + 1);
        return static_cast<int>(narrowed.value.size());
    }

    // Frozen v107 deliberately copied at most MAX_PATH-2 bytes and then
    // appended a backslash and terminator. Keep that byte-capacity behavior;
    // the live call remains the sole owner of root parsing.
    const std::size_t prefixLength = outputSize - 2;
    if (prefixLength + 1 >
        static_cast<std::size_t>(std::numeric_limits<int>::max()))
        return 0;
    std::memcpy(output, narrowed.value.data(), prefixLength);
    output[prefixLength] = '\\';
    output[prefixLength + 1] = '\0';
    return static_cast<int>(prefixLength + 1);
}

bool PrepareLegacyCutDirectoryOutput(
    const wchar_t* widePath, const wchar_t* wideCutDir,
    std::size_t outputSize, std::string& output,
    std::size_t& cutDirOffset)
{
    output.clear();
    cutDirOffset = std::string::npos;
    if (widePath == nullptr || outputSize == 0)
        return false;

    std::string path;
    if (PrepareWideGeneralOutputExact(widePath, outputSize, path) !=
        GeneralOutputCommitStatus::Complete)
        return false;
    output = path;
    output.push_back('\0');

    if (wideCutDir == nullptr)
        return true;

    // The live growable records publish the shortened path and removed
    // component separately. Recreate the frozen adjacent byte layout here.
    std::string cut;
    if (PrepareWideGeneralOutputExact(wideCutDir, outputSize, cut) !=
            GeneralOutputCommitStatus::Complete ||
        output.size() >= outputSize ||
        cut.size() > outputSize - output.size() - 1)
    {
        output.clear();
        return false;
    }

    cutDirOffset = output.size();
    output.append(cut);
    output.push_back('\0');
    return true;
}

GeneralOutputCommitStatus PrepareWideGeneralOutputExact(
    const wchar_t* value, std::size_t outputSize, std::string& output)
{
    output.clear();
    if (outputSize == 0)
        return GeneralOutputCommitStatus::InsufficientBuffer;
    if (value == nullptr || value[0] == L'\0')
        return GeneralOutputCommitStatus::Complete;

    const NarrowResult narrowed = NarrowExact(value);
    if (!narrowed.ok)
        return GeneralOutputCommitStatus::NotRepresentable;
    if (narrowed.value.size() + 1 > outputSize)
        return GeneralOutputCommitStatus::InsufficientBuffer;
    output = narrowed.value;
    return GeneralOutputCommitStatus::Complete;
}

bool PrepareLegacyInstalledModuleOutputs(
    const wchar_t* wideModule, const wchar_t* wideVersion,
    std::string& module, std::string& version)
{
    module.clear();
    version.clear();

    std::string preparedModule;
    std::string preparedVersion;
    if (PrepareWideGeneralOutputExact(
            wideModule, MAX_PATH, preparedModule) !=
            GeneralOutputCommitStatus::Complete ||
        PrepareWideGeneralOutputExact(
            wideVersion, MAX_PATH, preparedVersion) !=
            GeneralOutputCommitStatus::Complete)
        return false;

    module.swap(preparedModule);
    version.swap(preparedVersion);
    return true;
}

bool PrepareLegacyClipboardText(const char* text, int textLen,
                                std::wstring& wide)
{
    wide.clear();
    if (text == nullptr || textLen < -1)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return false;
    }

    // A counted clipboard value is a frozen encoded-byte record and may
    // intentionally contain embedded NULs. The sentinel form is ordinary
    // NUL-terminated text. Keep those domains distinct at this one adapter.
    return textLen == -1 ? WidenPluginText(text, wide)
                         : WidenPluginBytes(text, textLen, wide);
}

GeneralOutputCommitStatus PrepareLegacyPanelPathOutput(
    const wchar_t* path, const wchar_t* archiveOrFS, std::size_t outputSize,
    std::string& output, std::size_t& archiveOrFSOffset)
{
    archiveOrFSOffset = std::string::npos;
    const GeneralOutputCommitStatus status =
        PrepareWideGeneralOutputExact(path, outputSize, output);
    if (status != GeneralOutputCommitStatus::Complete || archiveOrFS == nullptr)
        return status;
    if (path == nullptr)
    {
        output.clear();
        return GeneralOutputCommitStatus::NotRepresentable;
    }

    const std::size_t wideLength = std::wcslen(path);
    const std::uintptr_t pathAddress =
        reinterpret_cast<std::uintptr_t>(path);
    const std::uintptr_t pointerAddress =
        reinterpret_cast<std::uintptr_t>(archiveOrFS);
    if (wideLength >
        (std::numeric_limits<std::uintptr_t>::max() - pathAddress) /
            sizeof(wchar_t))
    {
        output.clear();
        return GeneralOutputCommitStatus::NotRepresentable;
    }
    const std::uintptr_t endAddress =
        pathAddress + wideLength * sizeof(wchar_t);
    if (pointerAddress < pathAddress || pointerAddress > endAddress ||
        (pointerAddress - pathAddress) % sizeof(wchar_t) != 0)
    {
        output.clear();
        return GeneralOutputCommitStatus::NotRepresentable;
    }
    const std::size_t wideOffset =
        (pointerAddress - pathAddress) / sizeof(wchar_t);
    if (wideOffset == 0)
    {
        archiveOrFSOffset = 0;
        return GeneralOutputCommitStatus::Complete;
    }

    const NarrowResult prefix =
        NarrowExact(std::wstring(path, static_cast<std::size_t>(wideOffset)));
    if (!prefix.ok)
    {
        output.clear();
        return GeneralOutputCommitStatus::NotRepresentable;
    }
    archiveOrFSOffset = prefix.value.size();
    return GeneralOutputCommitStatus::Complete;
}

GeneralOutputCommitStatus PrepareLegacyParsePathOutputs(
    const wchar_t* path, const wchar_t* secondPart,
    const wchar_t* nextFocus, std::size_t pathOutputSize,
    bool requireSecondPart, std::string& pathOutput,
    std::size_t& secondPartOffset, std::string& nextFocusOutput)
{
    pathOutput.clear();
    nextFocusOutput.clear();
    secondPartOffset = std::string::npos;

    const GeneralOutputCommitStatus pathStatus =
        PrepareLegacyPanelPathOutput(path, secondPart, pathOutputSize,
                                     pathOutput, secondPartOffset);
    if (pathStatus != GeneralOutputCommitStatus::Complete)
        return pathStatus;
    if (requireSecondPart && secondPartOffset == std::string::npos)
    {
        pathOutput.clear();
        return GeneralOutputCommitStatus::NotRepresentable;
    }
    if (nextFocus == nullptr || nextFocus[0] == L'\0')
        return GeneralOutputCommitStatus::Complete;

    const NarrowResult focus = NarrowExact(nextFocus);
    if (!focus.ok)
    {
        pathOutput.clear();
        secondPartOffset = std::string::npos;
        return GeneralOutputCommitStatus::NotRepresentable;
    }

    // v107 exposes no focus capacity. SalParsePath clears the output first and
    // publishes a focus only when it occupies fewer than MAX_PATH bytes.
    if (focus.value.size() < MAX_PATH)
        nextFocusOutput = focus.value;
    return GeneralOutputCommitStatus::Complete;
}

GeneralOutputCommitStatus PrepareLegacySplitWindowsPathOutput(
    const wchar_t* path, std::size_t widePathCapacity,
    const wchar_t* mask, std::size_t outputSize, bool requireMask,
    std::string& output, std::size_t& maskOffset)
{
    output.clear();
    maskOffset = std::string::npos;
    if (path == nullptr || widePathCapacity == 0)
        return GeneralOutputCommitStatus::NotRepresentable;

    const wchar_t* const wideEnd = path + widePathCapacity;
    const wchar_t* const pathEnd = std::find(path, wideEnd, L'\0');
    if (pathEnd == wideEnd)
        return GeneralOutputCommitStatus::NotRepresentable;

    const NarrowResult preparedPath =
        NarrowExact(std::wstring(path, pathEnd));
    if (!preparedPath.ok)
        return GeneralOutputCommitStatus::NotRepresentable;

    if (!requireMask)
    {
        if (preparedPath.value.size() + 1 > outputSize)
            return GeneralOutputCommitStatus::InsufficientBuffer;
        output = preparedPath.value;
        return GeneralOutputCommitStatus::Complete;
    }

    if (pathEnd + 1 >= wideEnd || mask == nullptr ||
        reinterpret_cast<std::uintptr_t>(mask) !=
            reinterpret_cast<std::uintptr_t>(pathEnd + 1))
        return GeneralOutputCommitStatus::NotRepresentable;

    const wchar_t* const maskEnd = std::find(mask, wideEnd, L'\0');
    if (maskEnd == wideEnd)
        return GeneralOutputCommitStatus::NotRepresentable;
    const NarrowResult preparedMask =
        NarrowExact(std::wstring(mask, maskEnd));
    if (!preparedMask.ok)
        return GeneralOutputCommitStatus::NotRepresentable;

    const std::size_t required = preparedPath.value.size() + 1 +
                                 preparedMask.value.size() + 1;
    if (required > outputSize)
        return GeneralOutputCommitStatus::InsufficientBuffer;

    output = preparedPath.value;
    output.push_back('\0');
    maskOffset = output.size();
    output.append(preparedMask.value);
    return GeneralOutputCommitStatus::Complete;
}

GeneralOutputCommitStatus PrepareLegacySplitGeneralPathOutputs(
    const wchar_t* path, std::size_t widePathCapacity,
    const wchar_t* mask, bool requireMask, const wchar_t* newDirs,
    bool prepareNewDirs, std::size_t pathOutputSize,
    std::size_t newDirsOutputSize, std::string& pathOutput,
    std::size_t& maskOffset, std::string& newDirsOutput)
{
    pathOutput.clear();
    maskOffset = std::string::npos;
    newDirsOutput.clear();

    const GeneralOutputCommitStatus pathStatus =
        PrepareLegacySplitWindowsPathOutput(
            path, widePathCapacity, mask, pathOutputSize, requireMask,
            pathOutput, maskOffset);
    if (pathStatus != GeneralOutputCommitStatus::Complete)
        return pathStatus;
    if (!prepareNewDirs)
        return GeneralOutputCommitStatus::Complete;

    std::string preparedNewDirs;
    const GeneralOutputCommitStatus newDirsStatus =
        PrepareWideGeneralOutputExact(
            newDirs, newDirsOutputSize, preparedNewDirs);
    if (newDirsStatus != GeneralOutputCommitStatus::Complete)
    {
        pathOutput.clear();
        maskOffset = std::string::npos;
        return newDirsStatus;
    }

    newDirsOutput = std::move(preparedNewDirs);
    return GeneralOutputCommitStatus::Complete;
}

thread_local CLegacySamePathCallbackScope*
    CLegacySamePathCallbackScope::Current = nullptr;

CLegacySamePathCallbackScope::CLegacySamePathCallbackScope(
    sdk107::SGP_IsTheSamePathF callback)
    : Callback(callback)
{
    if (Callback != nullptr)
    {
        Previous = Current;
        Current = this;
    }
}

CLegacySamePathCallbackScope::~CLegacySamePathCallbackScope()
{
    if (Callback != nullptr)
        Current = Previous;
}

::SGP_IsTheSamePathF CLegacySamePathCallbackScope::WideCallback() const
{
    return Callback != nullptr ? &CLegacySamePathCallbackScope::Invoke
                               : nullptr;
}

BOOL WINAPI CLegacySamePathCallbackScope::Invoke(
    const wchar_t* path1, const wchar_t* path2)
{
    if (Current == nullptr || Current->Callback == nullptr ||
        path1 == nullptr || path2 == nullptr)
        return FALSE;

    const NarrowResult legacyPath1 = NarrowExact(path1);
    const NarrowResult legacyPath2 = NarrowExact(path2);
    if (!legacyPath1.ok || !legacyPath2.ok)
        return FALSE;
    return Current->Callback(legacyPath1.value.c_str(),
                             legacyPath2.value.c_str());
}

CLegacyLoadOrSaveConfigurationCallback::
    CLegacyLoadOrSaveConfigurationCallback(
        sdk107::FSalLoadOrSaveConfiguration callback, void* parameter)
    : Callback(callback), Parameter(parameter)
{
}

void WINAPI CLegacyLoadOrSaveConfigurationCallback::Invoke(
    BOOL load, HKEY regKey, ::CSalamanderRegistryAbstract* registry,
    void* parameter)
{
    auto* callback =
        static_cast<CLegacyLoadOrSaveConfigurationCallback*>(parameter);
    if (callback == nullptr || callback->Callback == nullptr)
        return;
    if (registry == nullptr)
    {
        callback->Callback(load, regKey, nullptr, callback->Parameter);
        return;
    }
    CLegacySalamanderRegistry legacyRegistry(*registry);
    callback->Callback(load, regKey, &legacyRegistry, callback->Parameter);
}

CLegacyPluginOperationFromDiskCallback::
    CLegacyPluginOperationFromDiskCallback(
        sdk107::SalPluginOperationFromDisk callback, void* parameter)
    : Callback(callback), Parameter(parameter)
{
}

void WINAPI CLegacyPluginOperationFromDiskCallback::Invoke(
    const wchar_t* sourcePath, ::SalEnumSelection2 next, void* nextParam,
    void* parameter)
{
    auto* callback =
        static_cast<CLegacyPluginOperationFromDiskCallback*>(parameter);
    if (callback == nullptr || callback->Callback == nullptr)
        return;
    const NarrowResult legacySourcePath = NarrowExact(sourcePath);
    if (!legacySourcePath.ok)
        return;
    if (next == nullptr)
    {
        callback->Callback(legacySourcePath.value.c_str(), nullptr, nextParam,
                           callback->Parameter);
        return;
    }
    CLegacySelection2Bridge enumeration(next, nextParam);
    callback->Callback(legacySourcePath.value.c_str(), &CLegacySelection2Bridge::Invoke,
                       &enumeration, callback->Parameter);
}

class CLegacyPanelRowOwner::CState
{
public:
    const sdk107::CFileData* Publish(const ::CFileData* wide, int panel)
    {
        if (wide == nullptr)
            return nullptr;

        sdk107::CFileData converted = {};
        if (!FileDataToLegacy(*wide, converted))
        {
            const auto stale = ByWide.find(wide);
            if (stale != ByWide.end())
            {
                ByLegacy.erase(&stale->second->Row);
                ByWide.erase(stale);
            }
            return nullptr;
        }

        const auto found = ByWide.find(wide);
        if (found != ByWide.end())
        {
            FreeLegacyFileData(found->second->Row);
            found->second->Row = converted;
            found->second->Panel = panel;
            return &found->second->Row;
        }

        auto mirror = std::make_unique<CLegacyPanelRowMirror>();
        mirror->Wide = wide;
        mirror->Panel = panel;
        mirror->Row = converted;
        const sdk107::CFileData* legacy = &mirror->Row;
        ByLegacy.emplace(legacy, mirror.get());
        ByWide.emplace(wide, std::move(mirror));
        return legacy;
    }

    const ::CFileData* Resolve(const sdk107::CFileData* legacy, int panel) const
    {
        if (legacy == nullptr)
            return nullptr;
        const auto found = ByLegacy.find(legacy);
        return found != ByLegacy.end() && found->second->Panel == panel
                   ? found->second->Wide
                   : nullptr;
    }

private:
    std::unordered_map<const ::CFileData*, std::unique_ptr<CLegacyPanelRowMirror>> ByWide;
    std::unordered_map<const sdk107::CFileData*, CLegacyPanelRowMirror*> ByLegacy;
};

CLegacyPanelRowOwner::CLegacyPanelRowOwner()
    : State(std::make_unique<CState>())
{
}

CLegacyPanelRowOwner::~CLegacyPanelRowOwner() = default;

const sdk107::CFileData* CLegacyPanelRowOwner::Publish(const ::CFileData* wide, int panel)
{
    return State->Publish(wide, panel);
}

const ::CFileData* CLegacyPanelRowOwner::Resolve(const sdk107::CFileData* legacy, int panel) const
{
    return State->Resolve(legacy, panel);
}

bool PrepareLegacyCacheName(const wchar_t* wideName, std::string& name)
{
    name.clear();
    if (wideName == nullptr || wideName[0] == L'\0')
        return false;

    NarrowResult narrowed = NarrowExact(wideName);
    if (!narrowed.ok)
        return false;

    name = std::move(narrowed.value);
    return true;
}

class CLegacyCacheNameOwner::CState
{
public:
    struct CEntry
    {
        HANDLE LegacyLock = nullptr;
        HANDLE WideLock = nullptr;
        std::string Name;
    };

    SRWLOCK Lock = SRWLOCK_INIT;
    std::vector<std::unique_ptr<CEntry>> Entries;
};

CLegacyCacheNameOwner::CLegacyCacheNameOwner()
    : State(std::make_unique<CState>())
{
}

CLegacyCacheNameOwner::~CLegacyCacheNameOwner() = default;

const char* CLegacyCacheNameOwner::Publish(HANDLE legacyLock,
                                           HANDLE wideLock,
                                           std::string name)
{
    if (legacyLock == nullptr || wideLock == nullptr || name.empty())
        return nullptr;

    using CEntry = CState::CEntry;
    auto entry = std::make_unique<CEntry>();
    entry->LegacyLock = legacyLock;
    entry->WideLock = wideLock;
    entry->Name = std::move(name);
    const char* published = entry->Name.c_str();

    AcquireSRWLockExclusive(&State->Lock);
    State->Entries.push_back(std::move(entry));
    ReleaseSRWLockExclusive(&State->Lock);
    return published;
}

std::vector<HANDLE> CLegacyCacheNameOwner::Retire(HANDLE legacyLock)
{
    std::vector<HANDLE> retired;
    AcquireSRWLockExclusive(&State->Lock);
    for (auto entry = State->Entries.begin(); entry != State->Entries.end();)
    {
        if ((*entry)->LegacyLock == legacyLock)
        {
            retired.push_back((*entry)->WideLock);
            entry = State->Entries.erase(entry);
        }
        else
            ++entry;
    }
    ReleaseSRWLockExclusive(&State->Lock);
    return retired;
}

class CLegacyConversionTableNameOwner::CState
{
public:
    SRWLOCK Lock = SRWLOCK_INIT;
    std::unordered_map<const wchar_t*, std::unique_ptr<std::string>> ByWide;
};

CLegacyConversionTableNameOwner::CLegacyConversionTableNameOwner()
    : State(std::make_unique<CState>())
{
}

CLegacyConversionTableNameOwner::~CLegacyConversionTableNameOwner() = default;

const char* CLegacyConversionTableNameOwner::Publish(
    const wchar_t* wideName)
{
    if (wideName == nullptr)
        return nullptr;

    NarrowResult narrowed = NarrowExact(wideName);
    if (!narrowed.ok)
        return nullptr;
    auto mirror = std::make_unique<std::string>(std::move(narrowed.value));

    AcquireSRWLockExclusive(&State->Lock);
    const auto found = State->ByWide.find(wideName);
    if (found != State->ByWide.end())
    {
        const char* published = found->second->c_str();
        ReleaseSRWLockExclusive(&State->Lock);
        return published;
    }

    const char* published = mirror->c_str();
    State->ByWide.emplace(wideName, std::move(mirror));
    ReleaseSRWLockExclusive(&State->Lock);
    return published;
}

class CLegacyHistoryArrayOwner
{
public:
    bool Publish(wchar_t** wideHistoryArr, int historyItemsCount,
                 char**& legacyHistoryArr)
    {
        legacyHistoryArr = nullptr;
        if (wideHistoryArr == nullptr || historyItemsCount <= 0)
            return false;

        std::vector<std::unique_ptr<std::string>> prepared(
            static_cast<std::size_t>(historyItemsCount));
        for (int index = 0; index < historyItemsCount; ++index)
        {
            if (wideHistoryArr[index] == nullptr)
                continue;
            const NarrowResult narrowed = NarrowExact(wideHistoryArr[index]);
            if (!narrowed.ok)
                return false;
            prepared[static_cast<std::size_t>(index)] =
                std::make_unique<std::string>(std::move(narrowed.value));
        }

        AcquireSRWLockExclusive(&Lock);
        CEntry* entry = nullptr;
        for (const auto& candidate : Entries)
        {
            if (candidate->WideHistoryArr == wideHistoryArr &&
                candidate->HistoryItemsCount == historyItemsCount)
            {
                entry = candidate.get();
                break;
            }
        }
        if (entry == nullptr)
        {
            auto created = std::make_unique<CEntry>();
            created->WideHistoryArr = wideHistoryArr;
            created->HistoryItemsCount = historyItemsCount;
            entry = created.get();
            Entries.push_back(std::move(created));
        }

        entry->Values = std::move(prepared);
        entry->Pointers.resize(static_cast<std::size_t>(historyItemsCount));
        for (int index = 0; index < historyItemsCount; ++index)
        {
            const auto& value = entry->Values[static_cast<std::size_t>(index)];
            entry->Pointers[static_cast<std::size_t>(index)] =
                value != nullptr ? value->data() : nullptr;
        }
        legacyHistoryArr = entry->Pointers.data();
        ReleaseSRWLockExclusive(&Lock);
        return true;
    }

    bool Resolve(char** legacyHistoryArr, int historyItemsCount,
                 wchar_t**& wideHistoryArr)
    {
        wideHistoryArr = nullptr;
        if (legacyHistoryArr == nullptr || historyItemsCount <= 0)
            return false;

        AcquireSRWLockShared(&Lock);
        for (const auto& entry : Entries)
        {
            if (entry->HistoryItemsCount == historyItemsCount &&
                entry->Pointers.data() == legacyHistoryArr)
            {
                wideHistoryArr = entry->WideHistoryArr;
                ReleaseSRWLockShared(&Lock);
                return true;
            }
        }
        ReleaseSRWLockShared(&Lock);
        return false;
    }

private:
    struct CEntry
    {
        wchar_t** WideHistoryArr = nullptr;
        int HistoryItemsCount = 0;
        std::vector<std::unique_ptr<std::string>> Values;
        std::vector<char*> Pointers;
    };

    SRWLOCK Lock = SRWLOCK_INIT;
    std::vector<std::unique_ptr<CEntry>> Entries;
};

class CScopedLegacyHistoryArray
{
public:
    ~CScopedLegacyHistoryArray()
    {
        for (wchar_t* value : Values)
            std::free(value);
    }

    bool Capture(char** legacyHistoryArr, int historyItemsCount)
    {
        if (legacyHistoryArr == nullptr || historyItemsCount < 0)
            return false;
        Values.assign(static_cast<std::size_t>(historyItemsCount), nullptr);
        for (int index = 0; index < historyItemsCount; ++index)
        {
            const char* const legacy = legacyHistoryArr[index];
            if (legacy == nullptr)
                continue;
            std::wstring wide;
            if (!WidenPluginText(legacy, wide))
                return false;
            if (wide.size() >
                (std::numeric_limits<std::size_t>::max() / sizeof(wchar_t)) - 1)
                return false;

            const std::size_t bytes = (wide.size() + 1) * sizeof(wchar_t);
            wchar_t* const copy = static_cast<wchar_t*>(std::malloc(bytes));
            if (copy == nullptr)
                return false;
            std::memcpy(copy, wide.c_str(), bytes);
            Values[static_cast<std::size_t>(index)] = copy;
        }
        return true;
    }

    wchar_t** Data() { return Values.data(); }

    bool Commit(char** legacyHistoryArr)
    {
        if (legacyHistoryArr == nullptr)
            return false;

        std::vector<char*> prepared(Values.size(), nullptr);
        const auto freePrepared = [&prepared]() {
            for (char* value : prepared)
                std::free(value);
        };
        for (std::size_t index = 0; index < Values.size(); ++index)
        {
            if (Values[index] == nullptr)
                continue;
            const NarrowResult narrowed = NarrowExact(Values[index]);
            if (!narrowed.ok ||
                narrowed.value.size() ==
                    std::numeric_limits<std::size_t>::max())
            {
                freePrepared();
                return false;
            }

            char* const copy = static_cast<char*>(
                std::malloc(narrowed.value.size() + 1));
            if (copy == nullptr)
            {
                freePrepared();
                return false;
            }
            std::memcpy(copy, narrowed.value.c_str(),
                        narrowed.value.size() + 1);
            prepared[index] = copy;
        }

        for (std::size_t index = 0; index < Values.size(); ++index)
        {
            std::free(legacyHistoryArr[index]);
            legacyHistoryArr[index] = prepared[index];
            prepared[index] = nullptr;
        }
        return true;
    }

private:
    std::vector<wchar_t*> Values;
};

class CLegacySalamanderGeneral::CState
{
public:
    CLegacyGeneralObjectOwner Objects;
    CStableWrappers<sdk107::CSalamanderZLIBAbstract, ::CSalamanderZLIBAbstract, CLegacySalamanderZLIB> ZLIB;
    CStableWrappers<sdk107::CSalamanderPNGAbstract, ::CSalamanderPNGAbstract, CLegacySalamanderPNG> PNG;
    CStableWrappers<sdk107::CSalamanderCryptAbstract, ::CSalamanderCryptAbstract, CLegacySalamanderCrypt> Crypt;
    CStableWrappers<sdk107::CSalamanderPasswordManagerAbstract, ::CSalamanderPasswordManagerAbstract, CLegacySalamanderPasswordManager> PasswordManager;
    CStableWrappers<sdk107::CSalamanderBZIP2Abstract, ::CSalamanderBZIP2Abstract, CLegacySalamanderBZIP2> BZIP2;
    CLegacyPanelRowOwner PanelRows;
    CLegacyCacheNameOwner CacheNames;
    CLegacyConversionTableNameOwner ConversionTableNames;
    CLegacyHistoryArrayOwner HistoryArrays;
};

CLegacySalamanderGeneral::CState* CLegacySalamanderGeneral::CreateState()
{
    return new CState;
}

void CLegacySalamanderGeneral::DestroyState(CState* state)
{
    delete state;
}

int WINAPI CLegacySalamanderGeneral::ShowMessageBox(
    const char* text, const char* title, int type)
{
    std::wstring textW;
    std::wstring titleW;
    const wchar_t* liveText = nullptr;
    const wchar_t* liveTitle = nullptr;
    if (!WidenOptionalGeneralText(text, textW, liveText) ||
        !WidenOptionalGeneralText(title, titleW, liveTitle))
        return 0;
    return WideGeneral.ShowMessageBox(liveText, liveTitle, type);
}

int WINAPI CLegacySalamanderGeneral::SalMessageBox(
    HWND hParent, sdk107::LPCTSTR lpText, sdk107::LPCTSTR lpCaption,
    UINT uType)
{
    std::wstring textW;
    std::wstring captionW;
    const wchar_t* liveText = nullptr;
    const wchar_t* liveCaption = nullptr;
    if (!WidenOptionalGeneralText(lpText, textW, liveText) ||
        !WidenOptionalGeneralText(lpCaption, captionW, liveCaption))
        return 0;
    return WideGeneral.SalMessageBox(hParent, liveText, liveCaption, uType);
}

int WINAPI CLegacySalamanderGeneral::DialogError(
    HWND parent, DWORD flags, const char* fileName, const char* error,
    const char* title)
{
    std::wstring fileNameW;
    std::wstring errorW;
    std::wstring titleW;
    const wchar_t* liveFileName = nullptr;
    const wchar_t* liveError = nullptr;
    const wchar_t* liveTitle = nullptr;
    if (!WidenOptionalGeneralText(fileName, fileNameW, liveFileName) ||
        !WidenOptionalGeneralText(error, errorW, liveError) ||
        !WidenOptionalGeneralText(title, titleW, liveTitle))
        return 0;
    return WideGeneral.DialogError(parent, flags, liveFileName, liveError,
                                   liveTitle);
}

int WINAPI CLegacySalamanderGeneral::DialogOverwrite(
    HWND parent, DWORD flags, const char* fileName1, const char* fileData1,
    const char* fileName2, const char* fileData2)
{
    std::wstring fileName1W;
    std::wstring fileData1W;
    std::wstring fileName2W;
    std::wstring fileData2W;
    const wchar_t* liveFileName1 = nullptr;
    const wchar_t* liveFileData1 = nullptr;
    const wchar_t* liveFileName2 = nullptr;
    const wchar_t* liveFileData2 = nullptr;
    if (!WidenOptionalGeneralText(fileName1, fileName1W, liveFileName1) ||
        !WidenOptionalGeneralText(fileData1, fileData1W, liveFileData1) ||
        !WidenOptionalGeneralText(fileName2, fileName2W, liveFileName2) ||
        !WidenOptionalGeneralText(fileData2, fileData2W, liveFileData2))
        return 0;
    return WideGeneral.DialogOverwrite(parent, flags, liveFileName1,
                                       liveFileData1, liveFileName2,
                                       liveFileData2);
}

int WINAPI CLegacySalamanderGeneral::DialogQuestion(
    HWND parent, DWORD flags, const char* fileName, const char* question,
    const char* title)
{
    std::wstring fileNameW;
    std::wstring questionW;
    std::wstring titleW;
    const wchar_t* liveFileName = nullptr;
    const wchar_t* liveQuestion = nullptr;
    const wchar_t* liveTitle = nullptr;
    if (!WidenOptionalGeneralText(fileName, fileNameW, liveFileName) ||
        !WidenOptionalGeneralText(question, questionW, liveQuestion) ||
        !WidenOptionalGeneralText(title, titleW, liveTitle))
        return 0;
    return WideGeneral.DialogQuestion(parent, flags, liveFileName,
                                      liveQuestion, liveTitle);
}

BOOL WINAPI CLegacySalamanderGeneral::CheckAndCreateDirectory(
    const char* dir, HWND parent, BOOL quiet, char* errBuf, int errBufSize,
    char* firstCreatedDir, BOOL manualCrDir)
{
    if (errBuf != nullptr && errBufSize > 0)
        errBuf[0] = '\0';
    if (firstCreatedDir != nullptr)
        firstCreatedDir[0] = '\0';
    if (dir == nullptr)
        return FALSE;

    std::wstring dirW;
    if (!WidenPluginText(dir, dirW))
        return FALSE;
    std::wstring errorTextW;
    std::wstring firstCreatedDirW;
    const BOOL result = SPLCheckAndCreateDirectoryOwned(
        &WideGeneral, dirW.c_str(), parent, quiet,
        errBuf != nullptr && errBufSize > 0 ? &errorTextW : nullptr,
        firstCreatedDir != nullptr ? &firstCreatedDirW : nullptr, manualCrDir);
    if (errBuf != nullptr && errBufSize > 0)
        CopyWideGeneralOutputExact(errorTextW.c_str(), errBuf,
                                   static_cast<std::size_t>(errBufSize));
    if (firstCreatedDir != nullptr)
        CopyWideGeneralOutputExact(firstCreatedDirW.c_str(), firstCreatedDir,
                                   MAX_PATH);
    return result;
}

BOOL WINAPI CLegacySalamanderGeneral::TestFreeSpace(
    HWND parent, const char* path, const sdk107::CQuadWord& totalSize,
    const char* messageTitle)
{
    std::wstring pathW;
    std::wstring messageTitleW;
    const wchar_t* liveMessageTitle = nullptr;
    if (!WidenPluginText(path, pathW) ||
        !WidenOptionalGeneralText(messageTitle, messageTitleW,
                                  liveMessageTitle))
        return FALSE;
    const ::CQuadWord wideTotalSize = QuadWordFromLegacy(totalSize);
    return WideGeneral.TestFreeSpace(
        parent, pathW.c_str(), wideTotalSize,
        liveMessageTitle);
}

void WINAPI CLegacySalamanderGeneral::GetDiskFreeSpace(
    sdk107::CQuadWord* retValue, const char* path,
    sdk107::CQuadWord* total)
{
    std::wstring pathW;
    if (!WidenPluginText(path, pathW))
        return;
    ::CQuadWord wideFreeSpace(0, 0);
    ::CQuadWord wideTotalSpace(0, 0);
    WideGeneral.GetDiskFreeSpace(
        retValue != nullptr ? &wideFreeSpace : nullptr, pathW.c_str(),
        retValue != nullptr && total != nullptr ? &wideTotalSpace : nullptr);
    if (retValue == nullptr)
        return;
    *retValue = QuadWordToLegacy(wideFreeSpace);
    if (total != nullptr)
        *total = QuadWordToLegacy(wideTotalSpace);
}

BOOL WINAPI CLegacySalamanderGeneral::SalGetDiskFreeSpace(
    const char* path, LPDWORD lpSectorsPerCluster, LPDWORD lpBytesPerSector,
    LPDWORD lpNumberOfFreeClusters, LPDWORD lpTotalNumberOfClusters)
{
    std::wstring pathW;
    if (!WidenPluginText(path, pathW))
        return FALSE;
    return WideGeneral.SalGetDiskFreeSpace(
        pathW.c_str(), lpSectorsPerCluster, lpBytesPerSector,
        lpNumberOfFreeClusters, lpTotalNumberOfClusters);
}

BOOL WINAPI CLegacySalamanderGeneral::SalGetVolumeInformation(
    const char* path, char* rootOrCurReparsePoint, char* lpVolumeNameBuffer,
    DWORD nVolumeNameSize, LPDWORD lpVolumeSerialNumber,
    LPDWORD lpMaximumComponentLength, LPDWORD lpFileSystemFlags,
    char* lpFileSystemNameBuffer, DWORD nFileSystemNameSize)
{
    std::wstring pathW;
    if (!WidenPluginText(path, pathW))
        return FALSE;
    CSalamanderStringBufferOwner rootOwner;
    CSalamanderStringBufferOwner volumeOwner;
    CSalamanderStringBufferOwner fileSystemOwner;
    if (!rootOwner.IsValid() || !volumeOwner.IsValid() ||
        !fileSystemOwner.IsValid())
        return FALSE;

    DWORD volumeSerialNumber = 0;
    DWORD maximumComponentLength = 0;
    DWORD fileSystemFlags = 0;
    const BOOL result = WideGeneral.SalGetVolumeInformation(
        pathW.c_str(), rootOrCurReparsePoint != nullptr ? rootOwner.Buffer() : nullptr,
        lpVolumeNameBuffer != nullptr ? volumeOwner.Buffer() : nullptr,
        lpVolumeSerialNumber != nullptr ? &volumeSerialNumber : nullptr,
        lpMaximumComponentLength != nullptr ? &maximumComponentLength : nullptr,
        lpFileSystemFlags != nullptr ? &fileSystemFlags : nullptr,
        lpFileSystemNameBuffer != nullptr ? fileSystemOwner.Buffer() : nullptr);
    if (result == FALSE)
        return FALSE;

    std::wstring rootW;
    std::wstring volumeNameW;
    std::wstring fileSystemNameW;
    if ((rootOrCurReparsePoint != nullptr && !rootOwner.GetValue(rootW)) ||
        (lpVolumeNameBuffer != nullptr && !volumeOwner.GetValue(volumeNameW)) ||
        (lpFileSystemNameBuffer != nullptr &&
         !fileSystemOwner.GetValue(fileSystemNameW)))
        return FALSE;

    std::string root;
    std::string volumeName;
    std::string fileSystemName;
    GeneralOutputCommitStatus status = GeneralOutputCommitStatus::Complete;
    if (rootOrCurReparsePoint != nullptr)
        status = PrepareWideGeneralOutputExact(rootW.c_str(), MAX_PATH, root);
    if (status == GeneralOutputCommitStatus::Complete && lpVolumeNameBuffer != nullptr)
        status = PrepareWideGeneralOutputExact(
            volumeNameW.c_str(), nVolumeNameSize, volumeName);
    if (status == GeneralOutputCommitStatus::Complete &&
        lpFileSystemNameBuffer != nullptr)
        status = PrepareWideGeneralOutputExact(
            fileSystemNameW.c_str(), nFileSystemNameSize, fileSystemName);
    if (status != GeneralOutputCommitStatus::Complete)
    {
        SetLastError(status == GeneralOutputCommitStatus::InsufficientBuffer
                         ? ERROR_MORE_DATA
                         : ERROR_NO_UNICODE_TRANSLATION);
        return FALSE;
    }

    if (rootOrCurReparsePoint != nullptr)
        std::memcpy(rootOrCurReparsePoint, root.c_str(), root.size() + 1);
    if (lpVolumeNameBuffer != nullptr)
        std::memcpy(lpVolumeNameBuffer, volumeName.c_str(),
                    volumeName.size() + 1);
    if (lpFileSystemNameBuffer != nullptr)
        std::memcpy(lpFileSystemNameBuffer, fileSystemName.c_str(),
                    fileSystemName.size() + 1);
    if (lpVolumeSerialNumber != nullptr)
        *lpVolumeSerialNumber = volumeSerialNumber;
    if (lpMaximumComponentLength != nullptr)
        *lpMaximumComponentLength = maximumComponentLength;
    if (lpFileSystemFlags != nullptr)
        *lpFileSystemFlags = fileSystemFlags;
    return TRUE;
}

UINT WINAPI CLegacySalamanderGeneral::SalGetDriveType(const char* path)
{
    std::wstring pathW;
    if (!WidenPluginText(path, pathW))
        return DRIVE_UNKNOWN;
    return WideGeneral.SalGetDriveType(pathW.c_str());
}

BOOL WINAPI CLegacySalamanderGeneral::SalGetTempFileName(
    const char* path, const char* prefix, char* tmpName, BOOL file, DWORD* err)
{
    if (tmpName == nullptr)
        return FALSE;

    // Consume both inputs before the live call: a legacy caller may alias
    // either input to the frozen output buffer.
    std::wstring pathW;
    std::wstring prefixW;
    const wchar_t* widePath = nullptr;
    const wchar_t* widePrefix = nullptr;
    if (!WidenOptionalGeneralText(path, pathW, widePath) ||
        !WidenOptionalGeneralText(prefix, prefixW, widePrefix))
    {
        tmpName[0] = '\0';
        if (err != nullptr)
            *err = GetLastError();
        return FALSE;
    }
    CSalamanderStringBufferOwner tmpNameOwner;
    if (!tmpNameOwner.IsValid())
        return FALSE;
    DWORD wideError = NO_ERROR;
    const BOOL result = WideGeneral.SalGetTempFileName(
        widePath, widePrefix, tmpNameOwner.Buffer(), file,
        err != nullptr ? &wideError : nullptr);
    if (result == FALSE)
    {
        tmpName[0] = '\0';
        if (err != nullptr)
            *err = wideError;
        return FALSE;
    }

    std::wstring tmpNameW;
    if (!tmpNameOwner.GetValue(tmpNameW))
        return FALSE;

    std::string prepared;
    const GeneralOutputCommitStatus status =
        PrepareWideGeneralOutputExact(tmpNameW.c_str(), MAX_PATH, prepared);
    if (status != GeneralOutputCommitStatus::Complete)
    {
        // The live call has already created this object. Returning no usable
        // frozen name without undoing it would permanently leak the object.
        const FileResult cleanupResult =
            file != FALSE ? gFileSystem->DeleteFile(tmpNameW.c_str())
                          : gFileSystem->RemoveDirectory(tmpNameW.c_str());
        DWORD failure = status == GeneralOutputCommitStatus::InsufficientBuffer
                            ? ERROR_BUFFER_OVERFLOW
                            : ERROR_NO_UNICODE_TRANSLATION;
        if (!cleanupResult.success && cleanupResult.errorCode != NO_ERROR)
            failure = cleanupResult.errorCode;
        tmpName[0] = '\0';
        if (err != nullptr)
            *err = failure;
        return FALSE;
    }

    std::memcpy(tmpName, prepared.c_str(), prepared.size() + 1);
    if (err != nullptr)
        *err = NO_ERROR;
    return TRUE;
}

void WINAPI CLegacySalamanderGeneral::RemoveTemporaryDir(const char* dir)
{
    std::wstring dirW;
    const wchar_t* liveDir = nullptr;
    if (WidenOptionalGeneralText(dir, dirW, liveDir))
        WideGeneral.RemoveTemporaryDir(liveDir);
}

BOOL WINAPI CLegacySalamanderGeneral::SalMoveFile(
    const char* srcName, const char* destName, DWORD* err)
{
    std::wstring srcNameW;
    std::wstring destNameW;
    if (!WidenPluginText(srcName, srcNameW) ||
        !WidenPluginText(destName, destNameW))
        return FALSE;
    return WideGeneral.SalMoveFile(srcNameW.c_str(), destNameW.c_str(), err);
}

void WINAPI CLegacySalamanderGeneral::ExecuteAssociation(
    HWND parent, const char* path, const char* name)
{
    std::wstring pathW;
    std::wstring nameW;
    const wchar_t* widePath = nullptr;
    const wchar_t* wideName = nullptr;
    if (!WidenOptionalGeneralText(path, pathW, widePath) ||
        !WidenOptionalGeneralText(name, nameW, wideName))
        return;
    WideGeneral.ExecuteAssociation(parent, widePath, wideName);
}

BOOL WINAPI CLegacySalamanderGeneral::GetTargetDirectory(
    HWND parent, HWND hCenterWindow, const char* title, const char* comment,
    char* path, BOOL onlyNet, const char* initDir)
{
    if (path == nullptr)
        return FALSE;

    // All three inputs may alias the frozen output buffer. Consume them before
    // the live dialog can publish its result.
    std::wstring titleW;
    std::wstring commentW;
    std::wstring initDirW;
    const wchar_t* wideTitle = nullptr;
    const wchar_t* wideComment = nullptr;
    const wchar_t* wideInitDir = nullptr;
    if (!WidenOptionalGeneralText(title, titleW, wideTitle) ||
        !WidenOptionalGeneralText(comment, commentW, wideComment) ||
        !WidenOptionalGeneralText(initDir, initDirW, wideInitDir))
        return FALSE;
    std::wstring pathW;
    if (!SPLGetTargetDirectoryOwned(
            &WideGeneral, parent, hCenterWindow, wideTitle, wideComment,
            pathW, onlyNet, wideInitDir))
        return FALSE;

    std::string prepared;
    const GeneralOutputCommitStatus status =
        PrepareWideGeneralOutputExact(pathW.c_str(), MAX_PATH, prepared);
    if (status != GeneralOutputCommitStatus::Complete)
        return FALSE;

    std::memcpy(path, prepared.c_str(), prepared.size() + 1);
    return TRUE;
}

void WINAPI CLegacySalamanderGeneral::PrepareMask(char* mask,
                                                   const char* src)
{
    if (mask == nullptr)
        return;

    const std::size_t frozenCapacity =
        src != nullptr ? std::strlen(src) + 1 : 1;
    std::wstring sourceW;
    if (!WidenPluginText(src != nullptr ? src : "", sourceW))
        return;
    std::wstring maskW;
    if (!SPLPrepareMaskOwned(&WideGeneral, sourceW.c_str(), maskW))
        return;

    std::string prepared;
    if (PrepareWideGeneralOutputExact(maskW.c_str(), frozenCapacity, prepared) !=
        GeneralOutputCommitStatus::Complete)
        return;
    std::memcpy(mask, prepared.c_str(), prepared.size() + 1);
}

BOOL WINAPI CLegacySalamanderGeneral::AgreeMask(const char* filename,
                                                const char* mask,
                                                BOOL hasExtension)
{
    std::wstring filenameW;
    std::wstring maskW;
    if (!WidenPluginText(filename, filenameW) ||
        !WidenPluginText(mask, maskW))
        return FALSE;
    return WideGeneral.AgreeMask(filenameW.c_str(), maskW.c_str(),
                                 hasExtension);
}

char* WINAPI CLegacySalamanderGeneral::MaskName(char* buffer, int bufSize,
                                                const char* name,
                                                const char* mask)
{
    if (buffer == nullptr || bufSize <= 0 || name == nullptr)
        return nullptr;

    // Both inputs can alias the frozen output. Consume them before the live
    // transformation writes its separately staged result.
    std::wstring nameW;
    std::wstring maskW;
    const wchar_t* wideMask = nullptr;
    if (!WidenPluginText(name, nameW) ||
        !WidenOptionalGeneralText(mask, maskW, wideMask))
        return nullptr;
    std::wstring maskedNameW;
    if (!SPLMaskNameOwned(&WideGeneral, nameW.c_str(), wideMask,
                          maskedNameW))
        return nullptr;

    std::string prepared;
    if (!PrepareLegacyMaskNameOutput(maskedNameW.c_str(),
                                     static_cast<std::size_t>(bufSize),
                                     prepared))
        return nullptr;
    std::memcpy(buffer, prepared.c_str(), prepared.size() + 1);
    return buffer;
}

void WINAPI CLegacySalamanderGeneral::PrepareExtMask(char* mask,
                                                      const char* src)
{
    if (mask == nullptr)
        return;

    const std::size_t frozenCapacity =
        src != nullptr ? std::strlen(src) + 1 : 1;
    std::wstring sourceW;
    if (!WidenPluginText(src != nullptr ? src : "", sourceW))
        return;
    std::wstring maskW;
    if (!SPLPrepareMaskOwned(&WideGeneral, sourceW.c_str(), maskW, TRUE))
        return;

    std::string prepared;
    if (PrepareWideGeneralOutputExact(maskW.c_str(), frozenCapacity, prepared) !=
        GeneralOutputCommitStatus::Complete)
        return;
    std::memcpy(mask, prepared.c_str(), prepared.size() + 1);
}

BOOL WINAPI CLegacySalamanderGeneral::AgreeExtMask(const char* filename,
                                                   const char* mask,
                                                   BOOL hasExtension)
{
    std::wstring filenameW;
    std::wstring maskW;
    if (!WidenPluginText(filename, filenameW) ||
        !WidenPluginText(mask, maskW))
        return FALSE;
    return WideGeneral.AgreeExtMask(filenameW.c_str(), maskW.c_str(),
                                    hasExtension);
}

HWND WINAPI CLegacySalamanderGeneral::GetMsgBoxParent()
{
    return WideGeneral.GetMsgBoxParent();
}

HWND WINAPI CLegacySalamanderGeneral::GetMainWindowHWND()
{
    return WideGeneral.GetMainWindowHWND();
}

void WINAPI CLegacySalamanderGeneral::RestoreFocusInSourcePanel()
{
    WideGeneral.RestoreFocusInSourcePanel();
}

void* WINAPI CLegacySalamanderGeneral::Alloc(int size)
{
    return WideGeneral.Alloc(size);
}

void WINAPI CLegacySalamanderGeneral::Free(void* ptr)
{
    WideGeneral.Free(ptr);
}

char* WINAPI CLegacySalamanderGeneral::DupStr(const char* str)
{
    if (str == nullptr)
        return nullptr;

    const std::size_t frozenLength = std::strlen(str);
    if (frozenLength >=
        static_cast<std::size_t>(std::numeric_limits<int>::max()))
        return nullptr;

    std::wstring wide;
    if (!WidenPluginText(str, wide))
        return nullptr;
    const auto releaseWide = [this](wchar_t* value) noexcept {
        this->Free(value);
    };
    std::unique_ptr<wchar_t, decltype(releaseWide)> wideCopy(
        WideGeneral.DupStr(wide.c_str()), releaseWide);
    if (wideCopy == nullptr)
        return nullptr;

    std::string prepared;
    if (*wideCopy != L'\0')
    {
        NarrowResult narrowed;
        try
        {
            narrowed = NarrowExact(std::wstring(wideCopy.get()));
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
        if (!narrowed.ok)
            return nullptr;
        prepared = std::move(narrowed.value);
    }
    if (prepared.size() != frozenLength ||
        std::memcmp(prepared.data(), str, frozenLength) != 0)
        return nullptr;

    const std::size_t allocationSize = prepared.size() + 1;
    char* frozenCopy =
        static_cast<char*>(Alloc(static_cast<int>(allocationSize)));
    if (frozenCopy == nullptr)
        return nullptr;
    std::memcpy(frozenCopy, prepared.c_str(), allocationSize);
    return frozenCopy;
}

void WINAPI CLegacySalamanderGeneral::GetLowerAndUpperCase(unsigned char** lowerCase, unsigned char** upperCase)
{
    WideGeneral.GetLowerAndUpperCase(lowerCase, upperCase);
}

void WINAPI CLegacySalamanderGeneral::ToLowerCase(char* str)
{
    if (str == nullptr)
        return;

    const std::size_t frozenCapacity = std::strlen(str) + 1;
    std::wstring wide;
    if (!WidenPluginText(str, wide))
        return;
    CSalamanderStringBufferOwner owner(wide);
    if (!owner.IsValid() || !WideGeneral.ToLowerCase(owner.Buffer()) ||
        !owner.GetValue(wide))
        return;

    std::string prepared;
    if (PrepareWideGeneralOutputExact(wide.c_str(), frozenCapacity, prepared) !=
        GeneralOutputCommitStatus::Complete)
        return;
    std::memcpy(str, prepared.c_str(), prepared.size() + 1);
}

void WINAPI CLegacySalamanderGeneral::ToUpperCase(char* str)
{
    if (str == nullptr)
        return;

    const std::size_t frozenCapacity = std::strlen(str) + 1;
    std::wstring wide;
    if (!WidenPluginText(str, wide))
        return;
    CSalamanderStringBufferOwner owner(wide);
    if (!owner.IsValid() || !WideGeneral.ToUpperCase(owner.Buffer()) ||
        !owner.GetValue(wide))
        return;

    std::string prepared;
    if (PrepareWideGeneralOutputExact(wide.c_str(), frozenCapacity, prepared) !=
        GeneralOutputCommitStatus::Complete)
        return;
    std::memcpy(str, prepared.c_str(), prepared.size() + 1);
}

int WINAPI CLegacySalamanderGeneral::StrCmpEx(const char* s1, int l1,
                                               const char* s2, int l2)
{
    std::wstring s1W;
    std::wstring s2W;
    if (!WidenPluginSpan(s1, l1, s1W) ||
        !WidenPluginSpan(s2, l2, s2W))
        return 0;
    return WideGeneral.StrCmpEx(s1W.c_str(), static_cast<int>(s1W.size()),
                                s2W.c_str(), static_cast<int>(s2W.size()));
}

int WINAPI CLegacySalamanderGeneral::StrICpy(char* dest, const char* src)
{
    if (dest == nullptr || src == nullptr)
        return 0;

    // The frozen implementation ALWAYS wrote at least the terminator, whatever the input, and
    // v107 plugins are written against that: the SDK's own recommended use is
    // `char key[MAX_PATH]; StrICpy(key, name);` followed by handing 'key' to the disk cache. Every
    // "return 0" below leaves 'dest' as the caller left it, so without this a refusal published an
    // uninitialized stack buffer as a NUL-terminated string. Returning an EMPTY string is a
    // degraded answer; returning garbage is a read past the buffer and a poisoned cache key.
    *dest = '\0';

    const std::size_t frozenLength = std::strlen(src);
    if (frozenLength >=
        static_cast<std::size_t>(std::numeric_limits<int>::max()))
        return 0;
    const std::size_t frozenCapacity = frozenLength + 1;
    std::wstring wide;
    if (!WidenPluginText(src, wide))
        return 0;

    // The live method owns the invariant-folding policy in a growable record.
    // Publish only when the frozen caller-sized allocation can hold an exact
    // ANSI projection.
    CSalamanderStringBufferOwner owner;
    if (!owner.IsValid() || !WideGeneral.StrICpy(wide.c_str(), owner.Buffer()))
        return 0;
    std::wstring wideOutput;
    if (!owner.GetValue(wideOutput))
        return 0;

    std::string prepared;
    if (PrepareWideGeneralOutputExact(wideOutput.c_str(), frozenCapacity,
                                      prepared) !=
        GeneralOutputCommitStatus::Complete)
        return 0;
    std::memcpy(dest, prepared.c_str(), prepared.size() + 1);
    return static_cast<int>(prepared.size());
}

int WINAPI CLegacySalamanderGeneral::StrICmp(const char* s1, const char* s2)
{
    if (s1 == nullptr || s2 == nullptr)
        return 0;
    std::wstring s1W;
    std::wstring s2W;
    if (!WidenPluginText(s1, s1W) || !WidenPluginText(s2, s2W))
        return std::strcmp(s1, s2);
    return WideGeneral.StrICmp(s1W.c_str(), s2W.c_str());
}

int WINAPI CLegacySalamanderGeneral::StrICmpEx(const char* s1, int l1,
                                                const char* s2, int l2)
{
    std::wstring s1W;
    std::wstring s2W;
    if (!WidenPluginSpan(s1, l1, s1W) ||
        !WidenPluginSpan(s2, l2, s2W))
        return 0;
    return WideGeneral.StrICmpEx(s1W.c_str(), static_cast<int>(s1W.size()),
                                 s2W.c_str(), static_cast<int>(s2W.size()));
}

int WINAPI CLegacySalamanderGeneral::StrNICmp(const char* s1, const char* s2,
                                               int n)
{
    if (s1 == nullptr || s2 == nullptr || n <= 0)
        return 0;

    const int l1 = BoundedPluginLength(s1, n);
    const int l2 = BoundedPluginLength(s2, n);
    std::wstring s1W;
    std::wstring s2W;
    if (!WidenPluginSpan(s1, l1, s1W) ||
        !WidenPluginSpan(s2, l2, s2W))
        return 0;

    const std::size_t wideLimit = std::max(s1W.size(), s2W.size());
    if (wideLimit > static_cast<std::size_t>(
                        std::numeric_limits<int>::max()))
        return 0;
    return WideGeneral.StrNICmp(s1W.c_str(), s2W.c_str(),
                                static_cast<int>(wideLimit));
}

int WINAPI CLegacySalamanderGeneral::MemICmp(const void* buf1, const void* buf2, int n)
{
    return WideGeneral.MemICmp(buf1, buf2, n);
}

int WINAPI CLegacySalamanderGeneral::RegSetStrICmp(const char* s1,
                                                    const char* s2)
{
    if (s1 == nullptr || s2 == nullptr)
        return 0;
    std::wstring s1W;
    std::wstring s2W;
    if (!WidenPluginText(s1, s1W) || !WidenPluginText(s2, s2W))
        return std::strcmp(s1, s2);
    return WideGeneral.RegSetStrICmp(s1W.c_str(), s2W.c_str());
}

int WINAPI CLegacySalamanderGeneral::RegSetStrICmpEx(
    const char* s1, int l1, const char* s2, int l2,
    BOOL* numericalyEqual)
{
    std::wstring s1W;
    std::wstring s2W;
    if (!WidenPluginSpan(s1, l1, s1W) ||
        !WidenPluginSpan(s2, l2, s2W))
        return 0;
    return WideGeneral.RegSetStrICmpEx(
        s1W.c_str(), static_cast<int>(s1W.size()), s2W.c_str(),
        static_cast<int>(s2W.size()), numericalyEqual);
}

int WINAPI CLegacySalamanderGeneral::RegSetStrCmp(const char* s1,
                                                   const char* s2)
{
    if (s1 == nullptr || s2 == nullptr)
        return 0;
    std::wstring s1W;
    std::wstring s2W;
    if (!WidenPluginText(s1, s1W) || !WidenPluginText(s2, s2W))
        return std::strcmp(s1, s2);
    return WideGeneral.RegSetStrCmp(s1W.c_str(), s2W.c_str());
}

int WINAPI CLegacySalamanderGeneral::RegSetStrCmpEx(
    const char* s1, int l1, const char* s2, int l2,
    BOOL* numericalyEqual)
{
    std::wstring s1W;
    std::wstring s2W;
    if (!WidenPluginSpan(s1, l1, s1W) ||
        !WidenPluginSpan(s2, l2, s2W))
        return 0;
    return WideGeneral.RegSetStrCmpEx(
        s1W.c_str(), static_cast<int>(s1W.size()), s2W.c_str(),
        static_cast<int>(s2W.size()), numericalyEqual);
}

BOOL WINAPI CLegacySalamanderGeneral::GetPanelPath(
    int panel, char* buffer, int bufferSize, int* type, char** archiveOrFS,
    BOOL convertFSPathToExternal)
{
    if (type != nullptr)
        *type = 0;
    if (archiveOrFS != nullptr)
        *archiveOrFS = nullptr;
    const BOOL queryOnly = bufferSize == 0;
    if (bufferSize > 0 && buffer == nullptr)
        return FALSE;

    CSalamanderStringBufferOwner owner;
    if (bufferSize > 0)
        buffer[0] = '\0';
    DWORD archiveOrFSW = SAL_STRING_BUFFER_NPOS;
    const BOOL result = WideGeneral.GetPanelPath(
        panel, bufferSize > 0 && owner.IsValid() ? owner.Buffer() : nullptr,
        type, archiveOrFS != nullptr && !queryOnly ? &archiveOrFSW : nullptr,
        convertFSPathToExternal);
    if (result == FALSE || bufferSize <= 0)
        return result;

    std::wstring pathW;
    if (!owner.GetValue(pathW) ||
        (archiveOrFSW != SAL_STRING_BUFFER_NPOS && archiveOrFSW > pathW.size()))
        return FALSE;
    std::string prepared;
    std::size_t archiveOrFSOffset = std::string::npos;
    if (PrepareLegacyPanelPathOutput(
            pathW.c_str(), archiveOrFSW != SAL_STRING_BUFFER_NPOS
                               ? pathW.c_str() + archiveOrFSW
                               : nullptr,
            static_cast<std::size_t>(bufferSize), prepared,
            archiveOrFSOffset) != GeneralOutputCommitStatus::Complete)
        return FALSE;

    std::memcpy(buffer, prepared.c_str(), prepared.size() + 1);
    if (archiveOrFS != nullptr && archiveOrFSOffset != std::string::npos)
        *archiveOrFS = buffer + archiveOrFSOffset;
    return TRUE;
}

BOOL WINAPI CLegacySalamanderGeneral::GetLastWindowsPanelPath(
    int panel, char* buffer, int bufferSize)
{
    if (buffer == nullptr || bufferSize <= 0)
        return FALSE;
    buffer[0] = '\0';
    CSalamanderStringBufferOwner owner;
    if (!owner.IsValid())
        return FALSE;
    const BOOL result = WideGeneral.GetLastWindowsPanelPath(
        panel, owner.Buffer());
    if (result == FALSE)
        return result;

    std::wstring pathW;
    if (!owner.GetValue(pathW))
        return FALSE;
    std::string prepared;
    if (PrepareWideGeneralOutputExact(
            pathW.c_str(), static_cast<std::size_t>(bufferSize), prepared) !=
        GeneralOutputCommitStatus::Complete)
        return FALSE;
    std::memcpy(buffer, prepared.c_str(), prepared.size() + 1);
    return TRUE;
}

void WINAPI CLegacySalamanderGeneral::GetPluginFSName(char* buf,
                                                       int fsNameIndex)
{
    if (buf == nullptr)
        return;
    buf[0] = '\0';
    CSalamanderStringBufferOwner owner;
    std::wstring nameW;
    if (owner.IsValid() &&
        WideGeneral.GetPluginFSName(owner.Buffer(), fsNameIndex) &&
        owner.GetValue(nameW))
        CopyWideGeneralOutputExact(nameW.c_str(), buf, MAX_PATH);
}

BOOL WINAPI CLegacySalamanderGeneral::GetPanelSelection(int panel, int* selectedFiles, int* selectedDirs)
{
    return WideGeneral.GetPanelSelection(panel, selectedFiles, selectedDirs);
}

int WINAPI CLegacySalamanderGeneral::GetPanelTopIndex(int panel)
{
    return WideGeneral.GetPanelTopIndex(panel);
}

void WINAPI CLegacySalamanderGeneral::SkipOneActivateRefresh()
{
    WideGeneral.SkipOneActivateRefresh();
}

void WINAPI CLegacySalamanderGeneral::RepaintChangedItems(int panel)
{
    WideGeneral.RepaintChangedItems(panel);
}

void WINAPI CLegacySalamanderGeneral::SelectAllPanelItems(int panel, BOOL select, BOOL repaint)
{
    WideGeneral.SelectAllPanelItems(panel, select, repaint);
}

int WINAPI CLegacySalamanderGeneral::GetSourcePanel()
{
    return WideGeneral.GetSourcePanel();
}

void WINAPI CLegacySalamanderGeneral::ChangePanel()
{
    WideGeneral.ChangePanel();
}

char* WINAPI CLegacySalamanderGeneral::NumberToStr(
    char* buffer, const sdk107::CQuadWord& number)
{
    const ::CQuadWord numberW = QuadWordFromLegacy(number);
    std::wstring formattedW;
    if (!SPLNumberToStrOwned(&WideGeneral, numberW, formattedW))
        formattedW.clear();
    CopyWideGeneralOutputExact(formattedW.c_str(), buffer, 50);
    return buffer;
}

char* WINAPI CLegacySalamanderGeneral::PrintDiskSize(
    char* buf, const sdk107::CQuadWord& size, int mode)
{
    const ::CQuadWord sizeW = QuadWordFromLegacy(size);
    std::wstring formattedW;
    if (!SPLPrintDiskSizeOwned(&WideGeneral, sizeW, mode, formattedW))
        formattedW.clear();
    CopyWideGeneralOutputExact(formattedW.c_str(), buf, 100);
    return buf;
}

char* WINAPI CLegacySalamanderGeneral::PrintTimeLeft(
    char* buf, const sdk107::CQuadWord& secs)
{
    const ::CQuadWord secsW = QuadWordFromLegacy(secs);
    std::wstring formattedW;
    if (!SPLPrintTimeLeftOwned(&WideGeneral, secsW, formattedW))
        formattedW.clear();
    CopyWideGeneralOutputExact(formattedW.c_str(), buf, 100);
    return buf;
}

BOOL WINAPI CLegacySalamanderGeneral::HasTheSameRootPath(
    const char* path1, const char* path2)
{
    if (path1 == nullptr || path2 == nullptr)
        return FALSE;
    if (path1[0] == '\0' || path2[0] == '\0' ||
        path1[1] == '\0' || path2[1] == '\0')
        return FALSE;

    std::wstring path1W;
    std::wstring path2W;
    if (!WidenPluginText(path1, path1W) ||
        !WidenPluginText(path2, path2W))
        return FALSE;
    return WideGeneral.HasTheSameRootPath(path1W.c_str(), path2W.c_str());
}

int WINAPI CLegacySalamanderGeneral::CommonPrefixLength(
    const char* path1, const char* path2)
{
    if (path1 == nullptr || path2 == nullptr)
        return 0;

    std::wstring path1W;
    std::wstring path2W;
    if (!WidenPluginText(path1, path1W) ||
        !WidenPluginText(path2, path2W))
        return 0;
    const int wideLength = WideGeneral.CommonPrefixLength(
        path1W.c_str(), path2W.c_str());
    return LegacyByteLengthForWidePrefix(path1, path1W, wideLength);
}

BOOL WINAPI CLegacySalamanderGeneral::PathIsPrefix(
    const char* prefix, const char* path)
{
    if (prefix == nullptr || path == nullptr)
        return FALSE;

    std::wstring prefixW;
    std::wstring pathW;
    if (!WidenPluginText(prefix, prefixW) ||
        !WidenPluginText(path, pathW))
        return FALSE;
    return WideGeneral.PathIsPrefix(prefixW.c_str(), pathW.c_str());
}

BOOL WINAPI CLegacySalamanderGeneral::IsTheSamePath(
    const char* path1, const char* path2)
{
    if (path1 == nullptr || path2 == nullptr)
        return FALSE;

    std::wstring path1W;
    std::wstring path2W;
    if (!WidenPluginText(path1, path1W) ||
        !WidenPluginText(path2, path2W))
        return FALSE;
    return WideGeneral.IsTheSamePath(path1W.c_str(), path2W.c_str());
}

int WINAPI CLegacySalamanderGeneral::GetRootPath(
    char* root, const char* path)
{
    if (root == nullptr)
        return 0;
    root[0] = '\0';
    if (path == nullptr || path[0] == '\0')
        return 0;

    std::wstring pathW;
    if (!WidenPluginText(path, pathW) || pathW.empty() ||
        pathW.size() > static_cast<std::size_t>(std::numeric_limits<int>::max() - 2))
        return 0;

    std::wstring rootW;
    if (!SPLGetRootPathOwned(&WideGeneral, pathW.c_str(), rootW) ||
        rootW.size() > static_cast<std::size_t>((std::numeric_limits<int>::max)()))
        return 0;
    return CommitLegacyRootPathOutput(rootW.c_str(), static_cast<int>(rootW.size()),
                                      root, MAX_PATH);
}

BOOL WINAPI CLegacySalamanderGeneral::CutDirectory(
    char* path, char** cutDir)
{
    if (path == nullptr)
    {
        if (cutDir != nullptr)
            *cutDir = nullptr;
        return FALSE;
    }

    const std::size_t legacyLength = std::strlen(path);
    if (cutDir != nullptr)
        *cutDir = nullptr;
    if (legacyLength == 0 ||
        legacyLength > std::numeric_limits<std::size_t>::max() - 2)
        return FALSE;

    std::wstring pathW;
    if (!WidenPluginText(path, pathW) || pathW.empty() ||
        pathW.size() > static_cast<std::size_t>(std::numeric_limits<int>::max() - 2))
        return FALSE;

    std::wstring shortenedPath = pathW;
    std::wstring removedDirectory;
    if (!SPLCutDirectoryOwned(&WideGeneral, shortenedPath,
                              cutDir != nullptr ? &removedDirectory : nullptr))
        return FALSE;

    std::string prepared;
    std::size_t preparedCutDirOffset = std::string::npos;
    if (!PrepareLegacyCutDirectoryOutput(
            shortenedPath.c_str(),
            cutDir != nullptr ? removedDirectory.c_str() : nullptr,
            legacyLength + 2,
            prepared, preparedCutDirOffset))
        return FALSE;

    std::memcpy(path, prepared.data(), prepared.size());
    if (cutDir != nullptr)
        *cutDir = path + preparedCutDirOffset;
    return TRUE;
}

BOOL WINAPI CLegacySalamanderGeneral::SalPathAppend(
    char* path, const char* name, int pathSize)
{
    if (path == nullptr || name == nullptr || pathSize <= 0)
        return FALSE;

    const std::size_t legacyLength = std::strlen(path);
    if (legacyLength >= static_cast<std::size_t>(pathSize))
        return FALSE;

    // Consume both inputs before the live in/out call. A frozen caller may
    // point 'name' into its path allocation.
    std::wstring pathW;
    std::wstring nameW;
    if (!WidenPluginText(path, pathW) ||
        !WidenPluginText(name, nameW))
        return FALSE;
    std::wstring result = pathW;
    SPLSalPathAppendOwned(result, nameW.c_str());

    std::string prepared;
    if (PrepareWideGeneralOutputExact(
            result.c_str(), static_cast<std::size_t>(pathSize), prepared) !=
        GeneralOutputCommitStatus::Complete)
        return FALSE;

    std::memcpy(path, prepared.c_str(), prepared.size() + 1);
    return TRUE;
}

BOOL WINAPI CLegacySalamanderGeneral::SalPathAddBackslash(
    char* path, int pathSize)
{
    if (path == nullptr || pathSize <= 0)
        return FALSE;

    const std::size_t legacyLength = std::strlen(path);
    if (legacyLength >= static_cast<std::size_t>(pathSize))
        return FALSE;

    std::wstring pathW;
    if (!WidenPluginText(path, pathW))
        return FALSE;
    std::wstring result = pathW;
    SPLSalPathAddBackslashOwned(result);

    std::string prepared;
    if (PrepareWideGeneralOutputExact(
            result.c_str(), static_cast<std::size_t>(pathSize), prepared) !=
        GeneralOutputCommitStatus::Complete)
        return FALSE;

    std::memcpy(path, prepared.c_str(), prepared.size() + 1);
    return TRUE;
}

void WINAPI CLegacySalamanderGeneral::SalPathRemoveBackslash(char* path)
{
    if (path == nullptr)
        return;

    const std::size_t legacyLength = std::strlen(path);
    std::wstring pathW;
    if (!WidenPluginText(path, pathW))
        return;
    if (!SPLSalPathRemoveBackslashOwned(&WideGeneral, pathW))
        return;

    std::string prepared;
    if (PrepareWideGeneralOutputExact(
            pathW.c_str(), legacyLength + 1, prepared) !=
        GeneralOutputCommitStatus::Complete)
        return;

    std::memcpy(path, prepared.c_str(), prepared.size() + 1);
}

void WINAPI CLegacySalamanderGeneral::SalPathStripPath(char* path)
{
    if (path == nullptr)
        return;

    const std::size_t legacyLength = std::strlen(path);
    std::wstring pathW;
    if (!WidenPluginText(path, pathW))
        return;
    if (!SPLSalPathStripPathOwned(&WideGeneral, pathW))
        return;

    std::string prepared;
    if (PrepareWideGeneralOutputExact(
            pathW.c_str(), legacyLength + 1, prepared) !=
        GeneralOutputCommitStatus::Complete)
        return;

    std::memcpy(path, prepared.c_str(), prepared.size() + 1);
}

void WINAPI CLegacySalamanderGeneral::SalPathRemoveExtension(char* path)
{
    if (path == nullptr)
        return;

    const std::size_t legacyLength = std::strlen(path);
    std::wstring pathW;
    if (!WidenPluginText(path, pathW))
        return;
    if (!SPLSalPathRemoveExtensionOwned(&WideGeneral, pathW))
        return;

    std::string prepared;
    if (PrepareWideGeneralOutputExact(
            pathW.c_str(), legacyLength + 1, prepared) !=
        GeneralOutputCommitStatus::Complete)
        return;

    std::memcpy(path, prepared.c_str(), prepared.size() + 1);
}

BOOL WINAPI CLegacySalamanderGeneral::SalPathAddExtension(
    char* path, const char* extension, int pathSize)
{
    if (path == nullptr || extension == nullptr)
        return FALSE;

    const std::size_t legacyPathLength = std::strlen(path);
    std::wstring pathW;
    std::wstring extensionW;
    if (!WidenPluginText(path, pathW) ||
        !WidenPluginText(extension, extensionW))
        return FALSE;
    const std::wstring originalPath = pathW;
    bool hadExtension = false;
    for (std::size_t i = originalPath.size(); i > 0; --i)
    {
        if (originalPath[i - 1] == L'.')
        {
            hadExtension = true;
            break;
        }
        if (originalPath[i - 1] == L'\\')
            break;
    }
    if (!SPLSalPathAddExtensionOwned(&WideGeneral, pathW,
                                     extensionW.c_str()))
        return FALSE;

    if (pathW == originalPath)
        return hadExtension ||
               (pathSize > 0 &&
                legacyPathLength < static_cast<std::size_t>(pathSize));
    if (pathSize <= 0)
        return FALSE;

    std::string prepared;
    if (PrepareWideGeneralOutputExact(
            pathW.c_str(), static_cast<std::size_t>(pathSize), prepared) !=
        GeneralOutputCommitStatus::Complete)
        return FALSE;

    std::memcpy(path, prepared.c_str(), prepared.size() + 1);
    return TRUE;
}

BOOL WINAPI CLegacySalamanderGeneral::SalPathRenameExtension(
    char* path, const char* extension, int pathSize)
{
    if (path == nullptr || extension == nullptr)
        return FALSE;

    std::wstring pathW;
    std::wstring extensionW;
    if (!WidenPluginText(path, pathW) ||
        !WidenPluginText(extension, extensionW))
        return FALSE;
    if (!SPLSalPathRenameExtensionOwned(&WideGeneral, pathW,
                                        extensionW.c_str()))
        return FALSE;
    if (pathSize <= 0)
        return FALSE;

    std::string prepared;
    if (PrepareWideGeneralOutputExact(
            pathW.c_str(), static_cast<std::size_t>(pathSize), prepared) !=
        GeneralOutputCommitStatus::Complete)
        return FALSE;

    std::memcpy(path, prepared.c_str(), prepared.size() + 1);
    return TRUE;
}

const char* WINAPI CLegacySalamanderGeneral::SalPathFindFileName(
    const char* path)
{
    if (path == nullptr)
        return nullptr;

    std::wstring pathW;
    if (!WidenPluginText(path, pathW))
        return nullptr;

    const wchar_t* wideName =
        WideGeneral.SalPathFindFileName(pathW.c_str());
    std::size_t legacyOffset = std::string::npos;
    if (!LegacyByteOffsetForWidePointer(
            path, pathW, wideName, legacyOffset))
        return nullptr;
    return path + legacyOffset;
}

BOOL WINAPI CLegacySalamanderGeneral::SalGetFullName(
    char* name, int* errTextID, const char* curDir, char* nextFocus,
    int nameBufSize)
{
    if (name == nullptr || nameBufSize <= 0)
        return FALSE;

    // Consume both frozen inputs before either output can be published. A
    // caller may point curDir into the in/out name allocation.
    std::wstring nameInputW;
    std::wstring curDirW;
    const wchar_t* wideCurDir = nullptr;
    if (!WidenPluginText(name, nameInputW) ||
        !WidenOptionalGeneralText(curDir, curDirW, wideCurDir))
    {
        if (errTextID != nullptr)
            *errTextID = GFN_PATHISINVALID;
        return FALSE;
    }
    if (nameInputW.size() + 1 > static_cast<std::size_t>(nameBufSize))
    {
        if (errTextID != nullptr)
            *errTextID = GFN_TOOLONGPATH;
        return FALSE;
    }

    std::wstring nameW = nameInputW;
    std::wstring focusW;
    int wideError = 0;
    const BOOL result = SPLSalGetFullNameOwned(
        &WideGeneral, nameW, errTextID != nullptr ? &wideError : nullptr,
        wideCurDir, nextFocus != nullptr ? &focusW : nullptr);
    if (result == FALSE)
    {
        if (errTextID != nullptr)
            *errTextID = wideError;
        return FALSE;
    }

    std::string preparedName;
    std::string preparedFocus;
    bool publishFocus = false;
    const GeneralOutputCommitStatus status = PrepareLegacyFullNameOutputs(
        nameW.c_str(), nextFocus != nullptr ? focusW.c_str() : nullptr,
        static_cast<std::size_t>(nameBufSize), preparedName, preparedFocus,
        publishFocus);
    if (status != GeneralOutputCommitStatus::Complete)
    {
        if (errTextID != nullptr)
        {
            *errTextID = status == GeneralOutputCommitStatus::InsufficientBuffer
                             ? GFN_TOOLONGPATH
                             : GFN_PATHISINVALID;
        }
        return FALSE;
    }

    // Frozen v107 writes nextFocus before it assembles the final path. Preserve
    // that order so an exact alias still leaves the resolved name authoritative.
    if (nextFocus != nullptr && publishFocus)
        std::memcpy(nextFocus, preparedFocus.c_str(), preparedFocus.size() + 1);
    std::memcpy(name, preparedName.c_str(), preparedName.size() + 1);
    if (errTextID != nullptr)
        *errTextID = wideError;
    return TRUE;
}

void WINAPI CLegacySalamanderGeneral::SalUpdateDefaultDir(BOOL activePrefered)
{
    WideGeneral.SalUpdateDefaultDir(activePrefered);
}

char* WINAPI CLegacySalamanderGeneral::GetGFNErrorText(int GFN, char* buf,
                                                       int bufSize)
{
    if (buf == nullptr || bufSize <= 0)
        return buf;

    std::wstring bufferW;
    const BOOL gotText = SPLGetGFNErrorTextOwned(&WideGeneral, GFN, bufferW);

    std::string prepared;
    if (!gotText ||
        !PrepareLegacyTruncatedGeneralTextOutput(
            bufferW.c_str(), static_cast<std::size_t>(bufSize), prepared))
    {
        buf[0] = '\0';
        return buf;
    }

    std::memcpy(buf, prepared.c_str(), prepared.size() + 1);
    return buf;
}

char* WINAPI CLegacySalamanderGeneral::GetErrorText(int err, char* buf,
                                                    int bufSize)
{
    const bool useInternalBuffer = buf == nullptr || bufSize == 0;
    if (!useInternalBuffer && bufSize < 0)
    {
        buf[0] = '\0';
        return buf;
    }

    std::wstring bufferW;
    if (!SPLGetErrorTextOwned(&WideGeneral, err, bufferW))
    {
        if (!useInternalBuffer)
            buf[0] = '\0';
        return useInternalBuffer ? PublishLegacyGeneralErrorText(L"") : buf;
    }
    if (useInternalBuffer)
        return PublishLegacyGeneralErrorText(bufferW.c_str());

    std::string prepared;
    if (!PrepareLegacyTruncatedGeneralTextOutput(
            bufferW.c_str(), static_cast<std::size_t>(bufSize), prepared))
    {
        buf[0] = '\0';
        return buf;
    }

    std::memcpy(buf, prepared.c_str(), prepared.size() + 1);
    return buf;
}

COLORREF WINAPI CLegacySalamanderGeneral::GetCurrentColor(int color)
{
    return WideGeneral.GetCurrentColor(color);
}

void WINAPI CLegacySalamanderGeneral::FocusNameInPanel(
    int panel, const char* path, const char* name)
{
    std::wstring pathW;
    std::wstring nameW;
    const wchar_t* widePath = nullptr;
    const wchar_t* wideName = nullptr;
    if (!WidenOptionalGeneralText(path, pathW, widePath) ||
        !WidenOptionalGeneralText(name, nameW, wideName))
        return;
    WideGeneral.FocusNameInPanel(panel, widePath, wideName);
}

BOOL WINAPI CLegacySalamanderGeneral::ChangePanelPath(
    int panel, const char* path, int* failReason, int suggestedTopIndex,
    const char* suggestedFocusName, BOOL convertFSPathToInternal)
{
    std::wstring pathW;
    std::wstring suggestedFocusNameW;
    const wchar_t* widePath = nullptr;
    const wchar_t* wideSuggestedFocusName = nullptr;
    if (!WidenOptionalGeneralText(path, pathW, widePath) ||
        !WidenOptionalGeneralText(suggestedFocusName, suggestedFocusNameW,
                                  wideSuggestedFocusName))
        return FALSE;
    return WideGeneral.ChangePanelPath(
        panel, widePath, failReason, suggestedTopIndex,
        wideSuggestedFocusName, convertFSPathToInternal);
}

BOOL WINAPI CLegacySalamanderGeneral::ChangePanelPathToDisk(
    int panel, const char* path, int* failReason, int suggestedTopIndex,
    const char* suggestedFocusName)
{
    std::wstring pathW;
    std::wstring suggestedFocusNameW;
    const wchar_t* widePath = nullptr;
    const wchar_t* wideSuggestedFocusName = nullptr;
    if (!WidenOptionalGeneralText(path, pathW, widePath) ||
        !WidenOptionalGeneralText(suggestedFocusName, suggestedFocusNameW,
                                  wideSuggestedFocusName))
        return FALSE;
    return WideGeneral.ChangePanelPathToDisk(
        panel, widePath, failReason, suggestedTopIndex,
        wideSuggestedFocusName);
}

BOOL WINAPI CLegacySalamanderGeneral::ChangePanelPathToArchive(
    int panel, const char* archive, const char* archivePath, int* failReason,
    int suggestedTopIndex, const char* suggestedFocusName, BOOL forceUpdate)
{
    std::wstring archiveW;
    std::wstring archivePathW;
    std::wstring suggestedFocusNameW;
    const wchar_t* wideArchive = nullptr;
    const wchar_t* wideArchivePath = nullptr;
    const wchar_t* wideSuggestedFocusName = nullptr;
    if (!WidenOptionalGeneralText(archive, archiveW, wideArchive) ||
        !WidenOptionalGeneralText(archivePath, archivePathW,
                                  wideArchivePath) ||
        !WidenOptionalGeneralText(suggestedFocusName, suggestedFocusNameW,
                                  wideSuggestedFocusName))
        return FALSE;
    return WideGeneral.ChangePanelPathToArchive(
        panel, wideArchive, wideArchivePath, failReason, suggestedTopIndex,
        wideSuggestedFocusName, forceUpdate);
}

BOOL WINAPI CLegacySalamanderGeneral::ChangePanelPathToPluginFS(
    int panel, const char* fsName, const char* fsUserPart, int* failReason,
    int suggestedTopIndex, const char* suggestedFocusName, BOOL forceUpdate,
    BOOL convertPathToInternal)
{
    std::wstring fsNameW;
    std::wstring fsUserPartW;
    std::wstring suggestedFocusNameW;
    const wchar_t* wideFSName = nullptr;
    const wchar_t* wideFSUserPart = nullptr;
    const wchar_t* wideSuggestedFocusName = nullptr;
    if (!WidenOptionalGeneralText(fsName, fsNameW, wideFSName) ||
        !WidenOptionalGeneralText(fsUserPart, fsUserPartW, wideFSUserPart) ||
        !WidenOptionalGeneralText(suggestedFocusName, suggestedFocusNameW,
                                  wideSuggestedFocusName))
        return FALSE;
    return WideGeneral.ChangePanelPathToPluginFS(
        panel, wideFSName, wideFSUserPart, failReason, suggestedTopIndex,
        wideSuggestedFocusName, forceUpdate, convertPathToInternal);
}

BOOL WINAPI CLegacySalamanderGeneral::ChangePanelPathToDetachedFS(
    int panel, sdk107::CPluginFSInterfaceAbstract* detachedFS,
    int* failReason, int suggestedTopIndex, const char* suggestedFocusName)
{
    ::CPluginFSInterfaceAbstract* wideDetachedFS =
        PluginFSResolver.Resolve(detachedFS);
    if (wideDetachedFS == nullptr)
    {
        if (failReason != nullptr)
            *failReason = CHPPFR_INVALIDPATH;
        return FALSE;
    }

    std::wstring suggestedFocusNameW;
    const wchar_t* wideSuggestedFocusName = nullptr;
    if (!WidenOptionalGeneralText(suggestedFocusName, suggestedFocusNameW,
                                  wideSuggestedFocusName))
        return FALSE;
    return WideGeneral.ChangePanelPathToDetachedFS(
        panel, wideDetachedFS, failReason, suggestedTopIndex,
        wideSuggestedFocusName);
}

BOOL WINAPI CLegacySalamanderGeneral::ChangePanelPathToFixedDrive(int panel, int* failReason)
{
    return WideGeneral.ChangePanelPathToFixedDrive(panel, failReason);
}

void WINAPI CLegacySalamanderGeneral::RefreshPanelPath(int panel, BOOL forceRefresh, BOOL focusFirstNewItem)
{
    WideGeneral.RefreshPanelPath(panel, forceRefresh, focusFirstNewItem);
}

void WINAPI CLegacySalamanderGeneral::PostRefreshPanelPath(int panel, BOOL focusFirstNewItem)
{
    WideGeneral.PostRefreshPanelPath(panel, focusFirstNewItem);
}

BOOL WINAPI CLegacySalamanderGeneral::SetFlagLoadOnSalamanderStart(BOOL start)
{
    return WideGeneral.SetFlagLoadOnSalamanderStart(start);
}

void WINAPI CLegacySalamanderGeneral::PostUnloadThisPlugin()
{
    WideGeneral.PostUnloadThisPlugin();
}

void WINAPI CLegacySalamanderGeneral::PostMenuExtCommand(int id, BOOL waitForSalIdle)
{
    WideGeneral.PostMenuExtCommand(id, waitForSalIdle);
}

BOOL WINAPI CLegacySalamanderGeneral::SalamanderIsNotBusy(DWORD* lastIdleTime)
{
    return WideGeneral.SalamanderIsNotBusy(lastIdleTime);
}

BOOL WINAPI CLegacySalamanderGeneral::GetConfigParameter(int paramID, void* buffer, int bufferSize, int* type)
{
    if (paramID == SALCFG_INFOLINECONTENT ||
        paramID == SALCFG_RECYCLEBINMASKS ||
        paramID == SALCFG_IFPATHISINACCESSIBLEGOTO)
    {
        if (type != nullptr)
            *type = SALCFGTYPE_STRING;
        if (buffer == nullptr || bufferSize <= 0)
        {
            SetLastError(ERROR_INSUFFICIENT_BUFFER);
            return FALSE;
        }
        static_cast<char*>(buffer)[0] = '\0';
        std::wstring wide;
        if (!SPLGetConfigParameterStringOwned(&WideGeneral, paramID, wide))
            return FALSE;
        std::string prepared;
        if (PrepareWideGeneralOutputExact(wide.c_str(),
                                          static_cast<std::size_t>(bufferSize),
                                          prepared) != GeneralOutputCommitStatus::Complete)
            return FALSE;
        std::memcpy(buffer, prepared.c_str(), prepared.size() + 1);
        return TRUE;
    }
    return WideGeneral.GetConfigParameter(paramID, buffer, bufferSize, type);
}

void WINAPI CLegacySalamanderGeneral::CreateSafeWaitWindow(
    const char* message, const char* caption, int delay,
    BOOL showCloseButton, HWND hForegroundWnd)
{
    // The live owner copies both strings into its cross-thread storage before
    // returning, so these call-scoped conversions preserve the any-thread
    // frozen contract without extending ACP beyond this boundary.
    std::wstring messageW;
    std::wstring captionW;
    const wchar_t* wideMessage = nullptr;
    const wchar_t* wideCaption = nullptr;
    if (!WidenOptionalGeneralText(message, messageW, wideMessage) ||
        !WidenOptionalGeneralText(caption, captionW, wideCaption))
        return;
    WideGeneral.CreateSafeWaitWindow(
        wideMessage, wideCaption, delay, showCloseButton, hForegroundWnd);
}

void WINAPI CLegacySalamanderGeneral::DestroySafeWaitWindow()
{
    WideGeneral.DestroySafeWaitWindow();
}

void WINAPI CLegacySalamanderGeneral::ShowSafeWaitWindow(BOOL show)
{
    WideGeneral.ShowSafeWaitWindow(show);
}

BOOL WINAPI CLegacySalamanderGeneral::GetSafeWaitWindowClosePressed()
{
    return WideGeneral.GetSafeWaitWindowClosePressed();
}

void WINAPI CLegacySalamanderGeneral::SetSafeWaitWindowText(const char* message)
{
    std::wstring messageW;
    const wchar_t* wideMessage = nullptr;
    if (!WidenOptionalGeneralText(message, messageW, wideMessage))
        return;
    WideGeneral.SetSafeWaitWindowText(wideMessage);
}

BOOL WINAPI CLegacySalamanderGeneral::GetFileFromCache(
    const char* uniqueFileName, const char*& tmpName, HANDLE fileLock)
{
    tmpName = nullptr;
    if (uniqueFileName == nullptr || fileLock == nullptr)
        return FALSE;

    std::wstring uniqueFileNameW;
    if (!WidenPluginText(uniqueFileName, uniqueFileNameW))
        return FALSE;
    HANDLE wideLock = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (wideLock == nullptr)
        return FALSE;

    const wchar_t* wideName = nullptr;
    if (!WideGeneral.GetFileFromCache(uniqueFileNameW.c_str(), wideName, wideLock))
    {
        ::CloseHandle(wideLock);
        return FALSE;
    }
    const auto releaseWideName = [this, wideLock]() {
        WideGeneral.UnlockFileInCache(wideLock);
        ::CloseHandle(wideLock);
    };

    std::string preparedName;
    if (!PrepareLegacyCacheName(wideName, preparedName))
    {
        releaseWideName();
        return FALSE;
    }

    const char* legacyName = State->CacheNames.Publish(
        fileLock, wideLock, std::move(preparedName));
    if (legacyName == nullptr)
    {
        releaseWideName();
        return FALSE;
    }

    tmpName = legacyName;
    return TRUE;
}

void WINAPI CLegacySalamanderGeneral::UnlockFileInCache(HANDLE fileLock)
{
    std::vector<HANDLE> wideLocks = State->CacheNames.Retire(fileLock);
    const bool closeWideLocks = !wideLocks.empty();
    if (wideLocks.empty())
        wideLocks.push_back(fileLock);

    for (HANDLE wideLock : wideLocks)
    {
        WideGeneral.UnlockFileInCache(wideLock);
        if (closeWideLocks)
            ::CloseHandle(wideLock);
    }
}

BOOL WINAPI CLegacySalamanderGeneral::MoveFileToCache(
    const char* uniqueFileName, const char* nameInCache,
    const char* rootTmpPath, const char* newFileName,
    const sdk107::CQuadWord& newFileSize, BOOL* alreadyExists)
{
    // Consume all potentially aliased frozen inputs before live General can
    // publish the scalar output or move the source file.
    std::wstring uniqueFileNameW;
    std::wstring nameInCacheW;
    std::wstring rootTmpPathW;
    std::wstring newFileNameW;
    const wchar_t* wideUniqueFileName = nullptr;
    const wchar_t* wideNameInCache = nullptr;
    const wchar_t* wideRootTmpPath = nullptr;
    const wchar_t* wideNewFileName = nullptr;
    if (!WidenOptionalGeneralText(uniqueFileName, uniqueFileNameW,
                                  wideUniqueFileName) ||
        !WidenOptionalGeneralText(nameInCache, nameInCacheW,
                                  wideNameInCache) ||
        !WidenOptionalGeneralText(rootTmpPath, rootTmpPathW,
                                  wideRootTmpPath) ||
        !WidenOptionalGeneralText(newFileName, newFileNameW,
                                  wideNewFileName))
        return FALSE;
    const ::CQuadWord wideNewFileSize = QuadWordFromLegacy(newFileSize);

    BOOL wideAlreadyExists = FALSE;
    const BOOL result = WideGeneral.MoveFileToCache(
        wideUniqueFileName, wideNameInCache, wideRootTmpPath,
        wideNewFileName, wideNewFileSize,
        alreadyExists != nullptr ? &wideAlreadyExists : nullptr);
    if (alreadyExists != nullptr)
        *alreadyExists = wideAlreadyExists;
    return result;
}

void WINAPI CLegacySalamanderGeneral::RemoveOneFileFromCache(
    const char* uniqueFileName)
{
    std::wstring uniqueFileNameW;
    const wchar_t* wideUniqueFileName = nullptr;
    if (!WidenOptionalGeneralText(uniqueFileName, uniqueFileNameW,
                                  wideUniqueFileName))
        return;
    WideGeneral.RemoveOneFileFromCache(wideUniqueFileName);
}

void WINAPI CLegacySalamanderGeneral::RemoveFilesFromCache(
    const char* fileNamesRoot)
{
    std::wstring fileNamesRootW;
    const wchar_t* wideFileNamesRoot = nullptr;
    if (!WidenOptionalGeneralText(fileNamesRoot, fileNamesRootW,
                                  wideFileNamesRoot))
        return;
    WideGeneral.RemoveFilesFromCache(wideFileNamesRoot);
}

BOOL WINAPI CLegacySalamanderGeneral::EnumConversionTables(
    HWND parent, int* index, const char** name, const char** table)
{
    if (name != nullptr)
        *name = nullptr;
    if (table != nullptr)
        *table = nullptr;

    for (;;)
    {
        const wchar_t* wideName = nullptr;
        const char* wideTable = nullptr;
        if (!WideGeneral.EnumConversionTables(parent, index, &wideName, &wideTable))
            return FALSE;

        if (wideName == nullptr)
            return TRUE;

        if (name == nullptr)
        {
            if (table != nullptr)
                *table = wideTable;
            return TRUE;
        }

        const char* legacyName =
            State->ConversionTableNames.Publish(wideName);
        if (legacyName == nullptr)
            continue;

        *name = legacyName;
        if (table != nullptr)
            *table = wideTable;
        return TRUE;
    }
}

BOOL WINAPI CLegacySalamanderGeneral::GetConversionTable(
    HWND parent, char* table, const char* conversion)
{
    std::wstring conversionW;
    const wchar_t* wideConversion = nullptr;
    if (!WidenOptionalGeneralText(conversion, conversionW, wideConversion))
        return FALSE;
    return WideGeneral.GetConversionTable(parent, table, wideConversion);
}

void WINAPI CLegacySalamanderGeneral::GetWindowsCodePage(
    HWND parent, char* codePage)
{
    if (codePage == nullptr)
        return;
    std::wstring codePageW;
    if (!SPLGetWindowsCodePageOwned(&WideGeneral, parent, codePageW) ||
        !CopyWideGeneralOutputExact(codePageW.c_str(), codePage, 101))
        codePage[0] = '\0';
}

void WINAPI CLegacySalamanderGeneral::RecognizeFileType(
    HWND parent, const char* pattern, int patternLen, BOOL forceText,
    BOOL* isText, char* codePage)
{
    std::wstring codePageW;
    if (!SPLRecognizeFileTypeOwned(&WideGeneral, parent, pattern, patternLen,
                                   forceText, isText,
                                   codePage != nullptr ? &codePageW : nullptr))
    {
        if (codePage != nullptr)
            codePage[0] = '\0';
        return;
    }
    if (codePage != nullptr)
        CopyWideGeneralOutputExact(codePageW.c_str(), codePage, 101);
}

BOOL WINAPI CLegacySalamanderGeneral::IsANSIText(const char* text, int textLen)
{
    return WideGeneral.IsANSIText(text, textLen);
}

BYTE WINAPI CLegacySalamanderGeneral::GetUserDefaultCharset()
{
    return WideGeneral.GetUserDefaultCharset();
}

BOOL WINAPI CLegacySalamanderGeneral::EnumSalamanderCommands(
    int* index, int* salCmd, char* nameBuf, int nameBufSize, BOOL* enabled,
    int* type)
{
    std::wstring nameW;
    const BOOL result = SPLEnumSalamanderCommandsOwned(
        &WideGeneral, index, salCmd, nameW, enabled, type);
    if (nameBuf != nullptr && nameBufSize > 0)
    {
        std::string prepared;
        PrepareLegacyTruncatedGeneralTextOutput(
            nameW.c_str(), static_cast<std::size_t>(nameBufSize), prepared);
        std::memcpy(nameBuf, prepared.c_str(), prepared.size() + 1);
    }
    return result;
}

BOOL WINAPI CLegacySalamanderGeneral::GetSalamanderCommand(
    int salCmd, char* nameBuf, int nameBufSize, BOOL* enabled, int* type)
{
    std::wstring nameW;
    const BOOL result = SPLGetSalamanderCommandOwned(
        &WideGeneral, salCmd, nameW, enabled, type);
    if (result != FALSE && nameBuf != nullptr && nameBufSize > 0)
    {
        std::string prepared;
        PrepareLegacyTruncatedGeneralTextOutput(
            nameW.c_str(), static_cast<std::size_t>(nameBufSize), prepared);
        std::memcpy(nameBuf, prepared.c_str(), prepared.size() + 1);
    }
    return result;
}

void WINAPI CLegacySalamanderGeneral::PostSalamanderCommand(int salCmd)
{
    WideGeneral.PostSalamanderCommand(salCmd);
}

void WINAPI CLegacySalamanderGeneral::SetUserWorkedOnPanelPath(int panel)
{
    WideGeneral.SetUserWorkedOnPanelPath(panel);
}

void WINAPI CLegacySalamanderGeneral::StoreSelectionOnPanelPath(int panel)
{
    WideGeneral.StoreSelectionOnPanelPath(panel);
}

DWORD WINAPI CLegacySalamanderGeneral::UpdateCrc32(const void* buffer, DWORD count, DWORD crcVal)
{
    return WideGeneral.UpdateCrc32(buffer, count, crcVal);
}

BOOL WINAPI CLegacySalamanderGeneral::LookForSubTexts(
    char* text, DWORD* varPlacements, int* varPlacementsCount)
{
    if (text == nullptr || varPlacementsCount == nullptr)
        return FALSE;
    const int placementCapacity = (std::max)(*varPlacementsCount, 0);
    if (placementCapacity > 0 && varPlacements == nullptr)
        return FALSE;

    // The frozen owner has no explicit text capacity. It only removes marker
    // characters, so the original ACP byte length (including the terminator)
    // is a sufficient exact-publication capacity for every completed result.
    const std::size_t textSize = std::strlen(text) + 1;
    std::wstring textW;
    if (!WidenPluginText(text, textW))
    {
        *varPlacementsCount = 0;
        return FALSE;
    }

    CSalamanderStringBufferOwner textOwner(textW);
    CSalamanderTextRangeBufferOwner rangeOwner;
    if (!textOwner.IsValid() || !rangeOwner.IsValid() ||
        !WideGeneral.LookForSubTexts(textOwner.Buffer(), rangeOwner.Buffer()))
    {
        *varPlacementsCount = 0;
        return FALSE;
    }
    if (!textOwner.GetValue(textW))
    {
        *varPlacementsCount = 0;
        return FALSE;
    }
    std::vector<CSalamanderTextRange> liveRanges;
    if (!rangeOwner.GetValue(liveRanges) ||
        liveRanges.size() > static_cast<std::size_t>(placementCapacity))
    {
        *varPlacementsCount = 0;
        return FALSE;
    }
    std::vector<sally::unicode::WideTextRange> widePlacements;
    widePlacements.reserve(liveRanges.size());
    for (const CSalamanderTextRange& range : liveRanges)
        widePlacements.push_back({range.Offset, range.Length});

    std::string preparedText;
    std::vector<DWORD> preparedPlacements;
    if (!PrepareLegacyVarStringExpansionOutput(
            textW, widePlacements.data(), widePlacements.size(), textSize,
            preparedText, preparedPlacements))
    {
        *varPlacementsCount = 0;
        return FALSE;
    }

    std::memcpy(text, preparedText.c_str(), preparedText.size() + 1);
    if (!preparedPlacements.empty())
        std::copy(preparedPlacements.begin(), preparedPlacements.end(), varPlacements);
    *varPlacementsCount = static_cast<int>(preparedPlacements.size());
    return TRUE;
}

void WINAPI CLegacySalamanderGeneral::WaitForESCRelease()
{
    WideGeneral.WaitForESCRelease();
}

DWORD WINAPI CLegacySalamanderGeneral::GetMouseWheelScrollLines()
{
    return WideGeneral.GetMouseWheelScrollLines();
}

HWND WINAPI CLegacySalamanderGeneral::GetTopVisibleParent(HWND hParent)
{
    return WideGeneral.GetTopVisibleParent(hParent);
}

BOOL WINAPI CLegacySalamanderGeneral::MultiMonGetDefaultWindowPos(HWND hByWnd, POINT* p)
{
    return WideGeneral.MultiMonGetDefaultWindowPos(hByWnd, p);
}

void WINAPI CLegacySalamanderGeneral::MultiMonGetClipRectByRect(const RECT* rect, RECT* workClipRect, RECT* monitorClipRect)
{
    WideGeneral.MultiMonGetClipRectByRect(rect, workClipRect, monitorClipRect);
}

void WINAPI CLegacySalamanderGeneral::MultiMonGetClipRectByWindow(HWND hByWnd, RECT* workClipRect, RECT* monitorClipRect)
{
    WideGeneral.MultiMonGetClipRectByWindow(hByWnd, workClipRect, monitorClipRect);
}

void WINAPI CLegacySalamanderGeneral::MultiMonCenterWindow(HWND hWindow, HWND hByWnd, BOOL findTopWindow)
{
    WideGeneral.MultiMonCenterWindow(hWindow, hByWnd, findTopWindow);
}

BOOL WINAPI CLegacySalamanderGeneral::MultiMonEnsureRectVisible(RECT* rect, BOOL partialOK)
{
    return WideGeneral.MultiMonEnsureRectVisible(rect, partialOK);
}

BOOL WINAPI CLegacySalamanderGeneral::InstallWordBreakProc(HWND hWindow)
{
    return WideGeneral.InstallWordBreakProc(hWindow);
}

BOOL WINAPI CLegacySalamanderGeneral::IsFirstInstance3OrLater()
{
    return WideGeneral.IsFirstInstance3OrLater();
}

int WINAPI CLegacySalamanderGeneral::ExpandPluralString(
    char* buffer, int bufferSize, const char* format, int parametersCount,
    const CQuadWord* parametersArray)
{
    if (buffer == nullptr || bufferSize <= 0)
        return 0;

    std::wstring formatW;
    const wchar_t* wideFormat = nullptr;
    if (!WidenOptionalGeneralText(format, formatW, wideFormat))
        return 0;
    std::vector<::CQuadWord> parametersArrayW;
    if (parametersCount > 0 && parametersArray != nullptr)
    {
        parametersArrayW.reserve(static_cast<std::size_t>(parametersCount));
        for (int index = 0; index < parametersCount; ++index)
            parametersArrayW.push_back(QuadWordFromLegacy(parametersArray[index]));
    }
    const ::CQuadWord* wideParameters = parametersArrayW.empty()
                                            ? nullptr
                                            : parametersArrayW.data();
    const std::wstring expandedW = SPLExpandPluralStringOwned(
        &WideGeneral, wideFormat, parametersCount, wideParameters);

    std::string prepared;
    if (!PrepareLegacyTruncatedGeneralTextOutput(
            expandedW.c_str(), static_cast<std::size_t>(bufferSize), prepared))
    {
        buffer[0] = '\0';
        return 0;
    }

    std::memcpy(buffer, prepared.c_str(), prepared.size() + 1);
    return static_cast<int>(prepared.size());
}

int WINAPI CLegacySalamanderGeneral::ExpandPluralFilesDirs(
    char* buffer, int bufferSize, int files, int dirs, int mode,
    BOOL forDlgCaption)
{
    if (buffer == nullptr || bufferSize <= 0)
        return 0;

    const std::wstring expandedW = SPLExpandPluralFilesDirsOwned(
        &WideGeneral, files, dirs, mode, forDlgCaption);

    std::string prepared;
    if (!PrepareLegacyTruncatedGeneralTextOutput(
            expandedW.c_str(), static_cast<std::size_t>(bufferSize), prepared))
    {
        buffer[0] = '\0';
        return 0;
    }

    std::memcpy(buffer, prepared.c_str(), prepared.size() + 1);
    return static_cast<int>(prepared.size());
}

int WINAPI CLegacySalamanderGeneral::ExpandPluralBytesFilesDirs(
    char* buffer, int bufferSize, const CQuadWord& selectedBytes, int files,
    int dirs, BOOL useSubTexts)
{
    if (buffer == nullptr || bufferSize <= 0)
        return 0;

    const ::CQuadWord selectedBytesW = QuadWordFromLegacy(selectedBytes);
    const std::wstring expandedW = SPLExpandPluralBytesFilesDirsOwned(
        &WideGeneral, selectedBytesW, files, dirs, useSubTexts);

    std::string prepared;
    if (!PrepareLegacyTruncatedGeneralTextOutput(
            expandedW.c_str(), static_cast<std::size_t>(bufferSize), prepared))
    {
        buffer[0] = '\0';
        return 0;
    }

    std::memcpy(buffer, prepared.c_str(), prepared.size() + 1);
    return static_cast<int>(prepared.size());
}

void WINAPI CLegacySalamanderGeneral::GetCommonFSOperSourceDescr(
    char* sourceDescr, int sourceDescrSize, int panel, int selectedFiles,
    int selectedDirs, const char* fileOrDirName, BOOL isDir,
    BOOL forDlgCaption)
{
    // Consume the optional frozen input before the output can overwrite an
    // aliased caller buffer. The live owner keeps all panel/focus selection
    // and localized formatting policy.
    std::wstring fileOrDirNameW;
    const wchar_t* wideFileOrDirName = nullptr;
    if (!WidenOptionalGeneralText(fileOrDirName, fileOrDirNameW,
                                  wideFileOrDirName))
        return;
    std::wstring sourceDescrW;
    if (sourceDescr == nullptr || sourceDescrSize <= 0 ||
        !SPLGetCommonFSOperSourceDescrOwned(
            &WideGeneral, panel, selectedFiles, selectedDirs,
            wideFileOrDirName, isDir, forDlgCaption, sourceDescrW))
        return;

    std::string prepared;
    if (!PrepareLegacyTruncatedGeneralTextOutput(
            sourceDescrW.c_str(), static_cast<std::size_t>(sourceDescrSize), prepared))
    {
        sourceDescr[0] = '\0';
        return;
    }

    std::memcpy(sourceDescr, prepared.c_str(), prepared.size() + 1);
}

void WINAPI CLegacySalamanderGeneral::AddStrToStr(
    char* dstStr, int dstBufSize, const char* srcStr)
{
    if (dstStr == nullptr || srcStr == nullptr || dstBufSize < 2)
        return;

    // srcStr may point into dstStr after its first terminator. Consume both
    // frozen strings before staging the wide in/out record.
    const std::string dstInput(dstStr);
    const std::string srcInput(srcStr);
    std::wstring dstInputW;
    std::wstring srcInputW;
    if (!WidenPluginText(dstInput.c_str(), dstInputW) ||
        !WidenPluginText(srcInput.c_str(), srcInputW))
        return;

    CSalamanderStringBufferOwner owner(dstInputW);
    if (!owner.IsValid() ||
        !WideGeneral.AddStrToStr(owner.Buffer(), srcInputW.c_str()))
        return;

    std::wstring pair;
    if (!owner.GetValue(pair))
        return;
    const std::wstring::size_type secondOffset = pair.find(L'\0');
    if (secondOffset == std::wstring::npos || secondOffset + 1 > pair.size())
        return;

    const std::size_t outputSize = static_cast<std::size_t>(dstBufSize);
    std::string prepared;
    if (!PrepareLegacyDoubleStringOutput(
            pair.c_str(), pair.c_str() + secondOffset + 1,
            outputSize, prepared))
    {
        dstStr[0] = '\0';
        return;
    }

    std::memcpy(dstStr, prepared.data(), prepared.size());
}

BOOL WINAPI CLegacySalamanderGeneral::SalIsValidFileNameComponent(
    const char* fileNameComponent)
{
    std::wstring fileNameComponentW;
    const wchar_t* wideFileNameComponent = nullptr;
    if (!WidenOptionalGeneralText(fileNameComponent, fileNameComponentW,
                                  wideFileNameComponent))
        return FALSE;
    return WideGeneral.SalIsValidFileNameComponent(wideFileNameComponent);
}

void WINAPI CLegacySalamanderGeneral::SalMakeValidFileNameComponent(
    char* fileNameComponent)
{
    if (fileNameComponent == nullptr)
        return;

    // The frozen declaration requires room for one additional character.
    // Stage the mutable text so the wide owner cannot expose a temporary.
    const std::size_t frozenCapacity = std::strlen(fileNameComponent) + 2;
    const std::string input(fileNameComponent);
    std::wstring completeInputW;
    if (!WidenPluginText(input.c_str(), completeInputW))
        return;

    std::wstring inputW;
    std::size_t inputLength = (std::min)(
        input.size(), static_cast<std::size_t>(MAX_PATH - 4));
    while (!WidenPluginSpan(input.c_str(), static_cast<int>(inputLength),
                            inputW))
    {
        // A valid frozen ACP string can only fail at this capped boundary by
        // splitting a multibyte character. Retain its longest exact prefix.
        if (inputLength == 0)
            return;
        --inputLength;
    }

    if (!SPLSalMakeValidFileNameComponentOwned(&WideGeneral, inputW))
        return;

    std::string prepared;
    if (PrepareWideGeneralOutputExact(
            inputW.c_str(), frozenCapacity, prepared) !=
        GeneralOutputCommitStatus::Complete)
        return;
    std::memcpy(fileNameComponent, prepared.c_str(), prepared.size() + 1);
}

BOOL WINAPI CLegacySalamanderGeneral::IsFileEnumSourcePanel(int srcUID, int* panel)
{
    return WideGeneral.IsFileEnumSourcePanel(srcUID, panel);
}

BOOL WINAPI CLegacySalamanderGeneral::GetNextFileNameForViewer(
    int srcUID, int* lastFileIndex, const char* lastFileName,
    BOOL preferSelected, BOOL onlyAssociatedExtensions, char* fileName,
    BOOL* noMoreFiles, BOOL* srcBusy)
{
    if (fileName == nullptr || lastFileIndex == nullptr)
    {
        if (noMoreFiles != nullptr)
            *noMoreFiles = FALSE;
        if (srcBusy != nullptr)
            *srcBusy = FALSE;
        return FALSE;
    }

    std::wstring lastFileNameW;
    const wchar_t* wideLastFileName = nullptr;
    if (!WidenOptionalGeneralText(lastFileName, lastFileNameW,
                                  wideLastFileName))
    {
        if (noMoreFiles != nullptr)
            *noMoreFiles = FALSE;
        if (srcBusy != nullptr)
            *srcBusy = FALSE;
        return FALSE;
    }

    CSalamanderStringBufferOwner fileNameOwner;
    if (!fileNameOwner.IsValid())
        return FALSE;
    int stagedLastFileIndex = *lastFileIndex;
    BOOL stagedNoMoreFiles = FALSE;
    BOOL stagedSrcBusy = FALSE;
    const BOOL result = WideGeneral.GetNextFileNameForViewer(
        srcUID, &stagedLastFileIndex, wideLastFileName, preferSelected,
        onlyAssociatedExtensions, fileNameOwner.Buffer(), &stagedNoMoreFiles,
        &stagedSrcBusy);
    if (result == FALSE)
    {
        *lastFileIndex = stagedLastFileIndex;
        if (noMoreFiles != nullptr)
            *noMoreFiles = stagedNoMoreFiles;
        if (srcBusy != nullptr)
            *srcBusy = stagedSrcBusy;
        return FALSE;
    }

    std::wstring fileNameW;
    if (!fileNameOwner.GetValue(fileNameW))
        return FALSE;
    std::string prepared;
    if (PrepareWideGeneralOutputExact(fileNameW.c_str(), MAX_PATH, prepared) !=
        GeneralOutputCommitStatus::Complete)
        return FALSE;

    std::memcpy(fileName, prepared.c_str(), prepared.size() + 1);
    *lastFileIndex = stagedLastFileIndex;
    if (noMoreFiles != nullptr)
        *noMoreFiles = stagedNoMoreFiles;
    if (srcBusy != nullptr)
        *srcBusy = stagedSrcBusy;
    return TRUE;
}

BOOL WINAPI CLegacySalamanderGeneral::GetPreviousFileNameForViewer(
    int srcUID, int* lastFileIndex, const char* lastFileName,
    BOOL preferSelected, BOOL onlyAssociatedExtensions, char* fileName,
    BOOL* noMoreFiles, BOOL* srcBusy)
{
    if (fileName == nullptr || lastFileIndex == nullptr)
    {
        if (noMoreFiles != nullptr)
            *noMoreFiles = FALSE;
        if (srcBusy != nullptr)
            *srcBusy = FALSE;
        return FALSE;
    }

    std::wstring lastFileNameW;
    const wchar_t* wideLastFileName = nullptr;
    if (!WidenOptionalGeneralText(lastFileName, lastFileNameW,
                                  wideLastFileName))
    {
        if (noMoreFiles != nullptr)
            *noMoreFiles = FALSE;
        if (srcBusy != nullptr)
            *srcBusy = FALSE;
        return FALSE;
    }

    CSalamanderStringBufferOwner fileNameOwner;
    if (!fileNameOwner.IsValid())
        return FALSE;
    int stagedLastFileIndex = *lastFileIndex;
    BOOL stagedNoMoreFiles = FALSE;
    BOOL stagedSrcBusy = FALSE;
    const BOOL result = WideGeneral.GetPreviousFileNameForViewer(
        srcUID, &stagedLastFileIndex, wideLastFileName, preferSelected,
        onlyAssociatedExtensions, fileNameOwner.Buffer(), &stagedNoMoreFiles,
        &stagedSrcBusy);
    if (result == FALSE)
    {
        *lastFileIndex = stagedLastFileIndex;
        if (noMoreFiles != nullptr)
            *noMoreFiles = stagedNoMoreFiles;
        if (srcBusy != nullptr)
            *srcBusy = stagedSrcBusy;
        return FALSE;
    }

    std::wstring fileNameW;
    if (!fileNameOwner.GetValue(fileNameW))
        return FALSE;
    std::string prepared;
    if (PrepareWideGeneralOutputExact(fileNameW.c_str(), MAX_PATH, prepared) !=
        GeneralOutputCommitStatus::Complete)
        return FALSE;

    std::memcpy(fileName, prepared.c_str(), prepared.size() + 1);
    *lastFileIndex = stagedLastFileIndex;
    if (noMoreFiles != nullptr)
        *noMoreFiles = stagedNoMoreFiles;
    if (srcBusy != nullptr)
        *srcBusy = stagedSrcBusy;
    return TRUE;
}

BOOL WINAPI CLegacySalamanderGeneral::IsFileNameForViewerSelected(
    int srcUID, int lastFileIndex, const char* lastFileName,
    BOOL* isFileSelected, BOOL* srcBusy)
{
    if (isFileSelected == nullptr)
    {
        if (srcBusy != nullptr)
            *srcBusy = FALSE;
        return FALSE;
    }

    std::wstring lastFileNameW;
    const wchar_t* wideLastFileName = nullptr;
    if (!WidenOptionalGeneralText(lastFileName, lastFileNameW,
                                  wideLastFileName))
    {
        *isFileSelected = FALSE;
        if (srcBusy != nullptr)
            *srcBusy = FALSE;
        return FALSE;
    }

    // The live query owns source/busy policy. Stage both observable results so
    // frozen callers never observe a partially published query outcome.
    BOOL stagedIsFileSelected = FALSE;
    BOOL stagedSrcBusy = FALSE;
    const BOOL result = WideGeneral.IsFileNameForViewerSelected(
        srcUID, lastFileIndex, wideLastFileName, &stagedIsFileSelected,
        &stagedSrcBusy);
    *isFileSelected = stagedIsFileSelected;
    if (srcBusy != nullptr)
        *srcBusy = stagedSrcBusy;
    return result;
}

BOOL WINAPI CLegacySalamanderGeneral::SetSelectionOnFileNameForViewer(
    int srcUID, int lastFileIndex, const char* lastFileName, BOOL select,
    BOOL* srcBusy)
{
    std::wstring lastFileNameW;
    const wchar_t* wideLastFileName = nullptr;
    if (!WidenOptionalGeneralText(lastFileName, lastFileNameW,
                                  wideLastFileName))
    {
        if (srcBusy != nullptr)
            *srcBusy = FALSE;
        return FALSE;
    }

    // This request mutates the source but exposes only the busy outcome.
    // The live owner retains all selection and notification policy.
    BOOL stagedSrcBusy = FALSE;
    const BOOL result = WideGeneral.SetSelectionOnFileNameForViewer(
        srcUID, lastFileIndex, wideLastFileName, select, &stagedSrcBusy);
    if (srcBusy != nullptr)
        *srcBusy = stagedSrcBusy;
    return result;
}

BOOL WINAPI CLegacySalamanderGeneral::GetStdHistoryValues(
    int historyID, char*** historyArr, int* historyItemsCount)
{
    if (historyArr == nullptr || historyItemsCount == nullptr)
        return FALSE;

    wchar_t** wideHistoryArr = nullptr;
    int wideHistoryItemsCount = 0;
    if (WideGeneral.GetStdHistoryValues(
            historyID, &wideHistoryArr, &wideHistoryItemsCount) == FALSE ||
        wideHistoryArr == nullptr || wideHistoryItemsCount <= 0)
    {
        *historyArr = nullptr;
        *historyItemsCount = 0;
        return FALSE;
    }

    char** legacyHistoryArr = nullptr;
    if (!State->HistoryArrays.Publish(
            wideHistoryArr, wideHistoryItemsCount, legacyHistoryArr))
    {
        *historyArr = nullptr;
        *historyItemsCount = 0;
        return FALSE;
    }

    *historyArr = legacyHistoryArr;
    *historyItemsCount = wideHistoryItemsCount;
    return TRUE;
}

void WINAPI CLegacySalamanderGeneral::AddValueToStdHistoryValues(
    char** historyArr, int historyItemsCount, const char* value,
    BOOL caseSensitiveValue)
{
    if (historyArr == nullptr || historyItemsCount <= 0 || value == nullptr)
        return;

    std::wstring valueW;
    if (!WidenPluginText(value, valueW))
        return;

    wchar_t** wideHistoryArr = nullptr;
    if (State->HistoryArrays.Resolve(
            historyArr, historyItemsCount, wideHistoryArr))
    {
        WideGeneral.AddValueToStdHistoryValues(
            wideHistoryArr, historyItemsCount, valueW.c_str(),
            caseSensitiveValue);
        char** refreshedHistoryArr = nullptr;
        State->HistoryArrays.Publish(
            wideHistoryArr, historyItemsCount, refreshedHistoryArr);
        return;
    }

    CScopedLegacyHistoryArray genericHistory;
    if (!genericHistory.Capture(historyArr, historyItemsCount))
        return;
    WideGeneral.AddValueToStdHistoryValues(
        genericHistory.Data(), historyItemsCount, valueW.c_str(),
        caseSensitiveValue);
    genericHistory.Commit(historyArr);
}

void WINAPI CLegacySalamanderGeneral::LoadComboFromStdHistoryValues(
    HWND combo, char** historyArr, int historyItemsCount)
{
    if (historyArr == nullptr)
        return;

    wchar_t** wideHistoryArr = nullptr;
    if (historyItemsCount > 0 && State->HistoryArrays.Resolve(
                                      historyArr, historyItemsCount,
                                      wideHistoryArr))
    {
        WideGeneral.LoadComboFromStdHistoryValues(
            combo, wideHistoryArr, historyItemsCount);
        return;
    }

    CScopedLegacyHistoryArray genericHistory;
    if (!genericHistory.Capture(historyArr, historyItemsCount))
        return;
    WideGeneral.LoadComboFromStdHistoryValues(
        combo, genericHistory.Data(), historyItemsCount);
}

HANDLE WINAPI CLegacySalamanderGeneral::SalCreateFileEx(
    const char* fileName, DWORD desiredAccess, DWORD shareMode,
    DWORD flagsAndAttributes, DWORD* err)
{
    std::wstring fileNameW;
    const wchar_t* wideFileName = nullptr;
    if (!WidenOptionalGeneralText(fileName, fileNameW, wideFileName))
    {
        if (err != nullptr)
            *err = GetLastError();
        return INVALID_HANDLE_VALUE;
    }
    return WideGeneral.SalCreateFileEx(
        wideFileName, desiredAccess, shareMode, flagsAndAttributes, err);
}

BOOL WINAPI CLegacySalamanderGeneral::SalCreateDirectoryEx(
    const char* name, DWORD* err)
{
    std::wstring nameW;
    const wchar_t* wideName = nullptr;
    if (!WidenOptionalGeneralText(name, nameW, wideName))
    {
        if (err != nullptr)
            *err = GetLastError();
        return FALSE;
    }
    return WideGeneral.SalCreateDirectoryEx(wideName, err);
}

BOOL WINAPI CLegacySalamanderGeneral::CanUse256ColorsBitmap()
{
    return WideGeneral.CanUse256ColorsBitmap();
}

HWND WINAPI CLegacySalamanderGeneral::GetWndToFlash(HWND parent)
{
    return WideGeneral.GetWndToFlash(parent);
}

void WINAPI CLegacySalamanderGeneral::ActivateDropTarget(HWND dropTarget, HWND progressWnd)
{
    WideGeneral.ActivateDropTarget(dropTarget, progressWnd);
}

void WINAPI CLegacySalamanderGeneral::PostOpenPackDlgForThisPlugin(int delFilesAfterPacking)
{
    WideGeneral.PostOpenPackDlgForThisPlugin(delFilesAfterPacking);
}

void WINAPI CLegacySalamanderGeneral::PostOpenUnpackDlgForThisPlugin(
    const char* unpackMask)
{
    std::wstring unpackMaskW;
    const wchar_t* wideUnpackMask = nullptr;
    if (!WidenOptionalGeneralText(unpackMask, unpackMaskW, wideUnpackMask))
        return;
    WideGeneral.PostOpenUnpackDlgForThisPlugin(wideUnpackMask);
}

void WINAPI CLegacySalamanderGeneral::PanelStopMonitoring(int panel, BOOL stopMonitoring)
{
    WideGeneral.PanelStopMonitoring(panel, stopMonitoring);
}

BOOL WINAPI CLegacySalamanderGeneral::GetChangeDriveMenuItemVisibility()
{
    return WideGeneral.GetChangeDriveMenuItemVisibility();
}

void WINAPI CLegacySalamanderGeneral::SetChangeDriveMenuItemVisibility(BOOL visible)
{
    WideGeneral.SetChangeDriveMenuItemVisibility(visible);
}

void WINAPI CLegacySalamanderGeneral::OleSpySetBreak(int alloc)
{
    WideGeneral.OleSpySetBreak(alloc);
}

HICON WINAPI CLegacySalamanderGeneral::GetSalamanderIcon(int icon, int iconSize)
{
    return WideGeneral.GetSalamanderIcon(icon, iconSize);
}

BOOL WINAPI CLegacySalamanderGeneral::GetFileIcon(const char* path, BOOL pathIsPIDL, HICON* hIcon, int iconSize, BOOL fallbackToDefIcon, BOOL defIconIsDir)
{
    if (pathIsPIDL)
        return WideGeneral.GetFileIconFromPIDL(
            reinterpret_cast<LPCITEMIDLIST>(path), hIcon, iconSize,
            fallbackToDefIcon, defIconIsDir);
    std::wstring pathW;
    const wchar_t* widePath = nullptr;
    if (!WidenOptionalGeneralText(path, pathW, widePath))
        return FALSE;
    return WideGeneral.GetFileIcon(widePath, hIcon, iconSize,
                                   fallbackToDefIcon, defIconIsDir);
}

BOOL WINAPI CLegacySalamanderGeneral::FileExists(const char* fileName)
{
    std::wstring fileNameW;
    const wchar_t* wideFileName = nullptr;
    if (!WidenOptionalGeneralText(fileName, fileNameW, wideFileName))
        return FALSE;
    return WideGeneral.FileExists(wideFileName);
}

void WINAPI CLegacySalamanderGeneral::DisconnectFSFromPanel(HWND parent, int panel)
{
    WideGeneral.DisconnectFSFromPanel(parent, panel);
}

BOOL WINAPI CLegacySalamanderGeneral::IsArchiveHandledByThisPlugin(
    const char* name)
{
    std::wstring nameW;
    const wchar_t* wideName = nullptr;
    if (!WidenOptionalGeneralText(name, nameW, wideName))
        return FALSE;
    return WideGeneral.IsArchiveHandledByThisPlugin(wideName);
}

DWORD WINAPI CLegacySalamanderGeneral::GetIconLRFlags()
{
    return WideGeneral.GetIconLRFlags();
}

int WINAPI CLegacySalamanderGeneral::IsFileLink(const char* fileExtension)
{
    std::wstring fileExtensionW;
    const wchar_t* wideFileExtension = nullptr;
    if (!WidenOptionalGeneralText(fileExtension, fileExtensionW,
                                  wideFileExtension))
        return 0;
    return WideGeneral.IsFileLink(wideFileExtension);
}

DWORD WINAPI CLegacySalamanderGeneral::GetImageListColorFlags()
{
    return WideGeneral.GetImageListColorFlags();
}

void WINAPI CLegacySalamanderGeneral::SetHelpFileName(const char* chmName)
{
    std::wstring chmNameW;
    const wchar_t* wideChmName = nullptr;
    if (!WidenOptionalGeneralText(chmName, chmNameW, wideChmName))
        return;
    WideGeneral.SetHelpFileName(wideChmName);
}

BOOL WINAPI CLegacySalamanderGeneral::PathsAreOnTheSameVolume(
    const char* path1, const char* path2, BOOL* resIsOnlyEstimation)
{
    std::wstring path1W;
    std::wstring path2W;
    if (!WidenPluginText(path1, path1W) ||
        !WidenPluginText(path2, path2W))
        return FALSE;
    return WideGeneral.PathsAreOnTheSameVolume(
        path1W.c_str(), path2W.c_str(), resIsOnlyEstimation);
}

BOOL WINAPI CLegacySalamanderGeneral::SafeGetOpenFileName(LPOPENFILENAME lpofn)
{
    return WideGeneral.SafeGetOpenFileName(lpofn);
}

BOOL WINAPI CLegacySalamanderGeneral::SafeGetSaveFileName(LPOPENFILENAME lpofn)
{
    return WideGeneral.SafeGetSaveFileName(lpofn);
}

void* WINAPI CLegacySalamanderGeneral::Realloc(void* ptr, int size)
{
    return WideGeneral.Realloc(ptr, size);
}

void WINAPI CLegacySalamanderGeneral::GetPanelEnumFilesParams(int panel, int* enumFilesSourceUID, int* enumFilesCurrentIndex)
{
    WideGeneral.GetPanelEnumFilesParams(panel, enumFilesSourceUID, enumFilesCurrentIndex);
}

char* WINAPI CLegacySalamanderGeneral::LoadStr(HINSTANCE module, int resID)
{
    // Preserve the frozen narrow return-buffer contract with the existing
    // cyclic adapter owner over the live wide-primary virtual call.
    return ::LoadStrNarrow(&WideGeneral, module, resID);
}

WCHAR* WINAPI CLegacySalamanderGeneral::LoadStrW(HINSTANCE module, int resID)
{
    CSalamanderStringBufferOwner owner;
    std::wstring value;
    if (!owner.IsValid() || !WideGeneral.LoadStr(module, resID, owner.Buffer()) ||
        !owner.GetValue(value))
    {
        static wchar_t failed[] = L"ERROR LOADING STRING";
        return failed;
    }

    // Bounded cyclic retention, the wide twin of LoadStrNarrow's - and for the same reason. The
    // frozen contract (spl_gen.h on LoadStrW) promises a 10000-character buffer "used cyclically",
    // so a pointer stays good until that much later text has been loaded. A list that only grows
    // keeps the stability and loses the bound, leaking one string per call for the life of the
    // thread. std::list survives the trim: popping the front never relocates a retained element.
    static thread_local std::list<std::wstring> values;
    static thread_local size_t retainedChars = 0;
    retainedChars += value.size() + 1;
    values.emplace_back(std::move(value));

    const size_t retentionBudget = 10000;
    const size_t minRetained = 16;
    while (retainedChars > retentionBudget && values.size() > minRetained)
    {
        retainedChars -= values.front().size() + 1;
        values.pop_front();
    }
    return values.back().data();
}

BOOL WINAPI CLegacySalamanderGeneral::ChangePanelPathToRescuePathOrFixedDrive(int panel, int* failReason)
{
    return WideGeneral.ChangePanelPathToRescuePathOrFixedDrive(panel, failReason);
}

void WINAPI CLegacySalamanderGeneral::SetPluginIsNethood()
{
    WideGeneral.SetPluginIsNethood();
}

void WINAPI CLegacySalamanderGeneral::OpenNetworkContextMenu(
    HWND parent, int panel, BOOL forItems, int menuX, int menuY,
    const char* netPath, char* newlyMappedDrive)
{
    std::wstring netPathW;
    const wchar_t* wideNetPath = nullptr;
    if (!WidenOptionalGeneralText(netPath, netPathW, wideNetPath))
        return;
    wchar_t mappedDriveW = L'\0';
    WideGeneral.OpenNetworkContextMenu(
        parent, panel, forItems, menuX, menuY,
        wideNetPath,
        newlyMappedDrive != nullptr ? &mappedDriveW : nullptr);

    // The frozen output is only a drive letter or zero, so it is safely
    // representable without applying a general narrow-output policy.
    if (newlyMappedDrive != nullptr)
        *newlyMappedDrive = mappedDriveW >= L'A' && mappedDriveW <= L'Z'
                                ? static_cast<char>(mappedDriveW)
                                : '\0';
}

BOOL WINAPI CLegacySalamanderGeneral::DuplicateBackslashes(char* buffer,
                                                           int bufferSize)
{
    if (buffer == nullptr || bufferSize <= 0)
        return FALSE;

    const int length = BoundedPluginLength(buffer, bufferSize);
    if (length == bufferSize)
        return FALSE;

    const std::string original(buffer, static_cast<std::size_t>(length));
    std::wstring inputW;
    if (!WidenPluginText(original.c_str(), inputW))
        return FALSE;

    CSalamanderStringBufferOwner owner(inputW);
    if (!owner.IsValid() ||
        !WideGeneral.DuplicateBackslashes(owner.Buffer()))
        return FALSE;

    std::wstring resultW;
    if (!owner.GetValue(resultW))
        return FALSE;
    const NarrowResult complete = NarrowExact(resultW);
    if (complete.ok && complete.value.size() < static_cast<std::size_t>(bufferSize))
    {
        std::memcpy(buffer, complete.value.c_str(), complete.value.size() + 1);
        return TRUE;
    }

    // The frozen mutator reports truncation, so publish the largest complete
    // ACP prefix instead of splitting a multibyte character at its byte limit.
    std::string truncated;
    if (!PrepareLegacyTruncatedGeneralTextOutput(
            resultW.c_str(), static_cast<std::size_t>(bufferSize), truncated))
    {
        return FALSE;
    }
    std::memcpy(buffer, truncated.c_str(), truncated.size() + 1);
    return FALSE;
}

int WINAPI CLegacySalamanderGeneral::StartThrobber(int panel,
                                                    const char* tooltip,
                                                    int delay)
{
    std::wstring tooltipW;
    const wchar_t* wideTooltip = nullptr;
    if (!WidenOptionalGeneralText(tooltip, tooltipW, wideTooltip))
        return 0;
    return WideGeneral.StartThrobber(panel, wideTooltip, delay);
}

BOOL WINAPI CLegacySalamanderGeneral::StopThrobber(int id)
{
    return WideGeneral.StopThrobber(id);
}

void WINAPI CLegacySalamanderGeneral::ShowSecurityIcon(
    int panel, BOOL showIcon, BOOL isLocked, const char* tooltip)
{
    std::wstring tooltipW;
    const wchar_t* wideTooltip = nullptr;
    if (!WidenOptionalGeneralText(tooltip, tooltipW, wideTooltip))
        return;
    WideGeneral.ShowSecurityIcon(panel, showIcon, isLocked, wideTooltip);
}

void WINAPI CLegacySalamanderGeneral::RemoveCurrentPathFromHistory(int panel)
{
    WideGeneral.RemoveCurrentPathFromHistory(panel);
}

BOOL WINAPI CLegacySalamanderGeneral::IsUserAdmin()
{
    return WideGeneral.IsUserAdmin();
}

BOOL WINAPI CLegacySalamanderGeneral::IsRemoteSession()
{
    return WideGeneral.IsRemoteSession();
}

DWORD WINAPI CLegacySalamanderGeneral::SalWNetAddConnection2Interactive(LPNETRESOURCE lpNetResource)
{
    return WideGeneral.SalWNetAddConnection2Interactive(lpNetResource);
}

DWORD WINAPI CLegacySalamanderGeneral::GetMouseWheelScrollChars()
{
    return WideGeneral.GetMouseWheelScrollChars();
}

void WINAPI CLegacySalamanderGeneral::SetPluginUsesPasswordManager()
{
    WideGeneral.SetPluginUsesPasswordManager();
}

void WINAPI CLegacySalamanderGeneral::GetFocusedItemMenuPos(POINT* pos)
{
    WideGeneral.GetFocusedItemMenuPos(pos);
}

void WINAPI CLegacySalamanderGeneral::LockMainWindow(BOOL lock,
                                                      HWND hToolWnd,
                                                      const char* lockReason)
{
    std::wstring lockReasonW;
    const wchar_t* wideLockReason = nullptr;
    if (!WidenOptionalGeneralText(lockReason, lockReasonW, wideLockReason))
        return;
    WideGeneral.LockMainWindow(lock, hToolWnd, wideLockReason);
}

void WINAPI CLegacySalamanderGeneral::PostPluginMenuChanged()
{
    WideGeneral.PostPluginMenuChanged();
}

BOOL WINAPI CLegacySalamanderGeneral::GetMenuItemHotKey(
    int id, WORD* hotKey, char* hotKeyText, int hotKeyTextSize)
{
    CSalamanderStringBufferOwner hotKeyTextOwner;
    if (hotKeyText != nullptr && hotKeyTextSize > 0 &&
        !hotKeyTextOwner.IsValid())
        return FALSE;

    const BOOL result = WideGeneral.GetMenuItemHotKey(
        id, hotKey,
        hotKeyText != nullptr && hotKeyTextSize > 0
            ? hotKeyTextOwner.Buffer()
            : nullptr);
    if (result != FALSE && hotKeyText != nullptr && hotKeyTextSize > 0)
    {
        std::wstring hotKeyTextW;
        if (!hotKeyTextOwner.GetValue(hotKeyTextW))
            return FALSE;
        std::string prepared;
        PrepareLegacyTruncatedGeneralTextOutput(
            hotKeyTextW.c_str(), static_cast<std::size_t>(hotKeyTextSize),
            prepared);
        std::memcpy(hotKeyText, prepared.c_str(), prepared.size() + 1);
    }
    return result;
}

LONG WINAPI CLegacySalamanderGeneral::SalRegQueryValue(
    HKEY hKey, LPCSTR lpSubKey, LPSTR lpData, PLONG lpcbData)
{
    std::wstring subKeyW;
    const wchar_t* wideSubKey = nullptr;
    if (!WidenOptionalGeneralText(lpSubKey, subKeyW, wideSubKey))
        return static_cast<LONG>(GetLastError());
    if (lpcbData == nullptr)
    {
        if (lpData != nullptr)
            return ERROR_INVALID_PARAMETER;
        return WideGeneral.SalRegQueryValue(hKey, wideSubKey, nullptr, nullptr);
    }

    LONG wideDataSize = 0;
    LONG result = WideGeneral.SalRegQueryValue(
        hKey, wideSubKey, nullptr, &wideDataSize);
    if (result != ERROR_SUCCESS)
        return result;
    if (wideDataSize < 0 ||
        static_cast<std::size_t>(wideDataSize) % sizeof(wchar_t) != 0)
        return ERROR_INVALID_DATA;

    std::vector<wchar_t> wideData(
        static_cast<std::size_t>(wideDataSize) / sizeof(wchar_t), L'\0');
    if (!wideData.empty())
    {
        result = WideGeneral.SalRegQueryValue(
            hKey, wideSubKey, wideData.data(), &wideDataSize);
        if (result != ERROR_SUCCESS)
            return result;
    }
    if (wideDataSize < 0 ||
        static_cast<std::size_t>(wideDataSize) >
            wideData.size() * sizeof(wchar_t))
        return ERROR_INVALID_DATA;

    std::string prepared;
    if (!PrepareLegacyRegistryQueryOutput(
            wideData.data(), static_cast<std::size_t>(wideDataSize), prepared))
        return ERROR_NO_UNICODE_TRANSLATION;
    if (prepared.size() >
        static_cast<std::size_t>((std::numeric_limits<LONG>::max)()))
        return ERROR_MORE_DATA;

    const LONG requiredSize = static_cast<LONG>(prepared.size());
    const LONG frozenCapacity = *lpcbData;
    if (lpData != nullptr &&
        (frozenCapacity < 0 || frozenCapacity < requiredSize))
    {
        *lpcbData = requiredSize;
        return ERROR_MORE_DATA;
    }

    if (lpData != nullptr && !prepared.empty())
        std::memcpy(lpData, prepared.data(), prepared.size());
    *lpcbData = requiredSize;
    return ERROR_SUCCESS;
}

LONG WINAPI CLegacySalamanderGeneral::SalRegQueryValueEx(
    HKEY hKey, LPCSTR lpValueName, LPDWORD lpReserved, LPDWORD lpType,
    LPBYTE lpData, LPDWORD lpcbData)
{
    std::wstring valueNameW;
    const wchar_t* wideValueName = nullptr;
    if (!WidenOptionalGeneralText(lpValueName, valueNameW, wideValueName))
        return static_cast<LONG>(GetLastError());
    // Value data and its size are byte-domain on both interfaces. Only the
    // optional registry value name crosses the frozen text boundary.
    return WideGeneral.SalRegQueryValueEx(
        hKey, wideValueName, lpReserved, lpType, lpData, lpcbData);
}

DWORD WINAPI CLegacySalamanderGeneral::SalGetFileAttributes(
    const char* fileName)
{
    std::wstring fileNameW;
    const wchar_t* wideFileName = nullptr;
    if (!WidenOptionalGeneralText(fileName, fileNameW, wideFileName))
        return INVALID_FILE_ATTRIBUTES;
    return WideGeneral.SalGetFileAttributes(wideFileName);
}

BOOL WINAPI CLegacySalamanderGeneral::IsPathOnSSD(const char* path)
{
    std::wstring pathW;
    if (!WidenPluginText(path, pathW))
        return FALSE;
    return WideGeneral.IsPathOnSSD(pathW.c_str());
}

BOOL WINAPI CLegacySalamanderGeneral::IsUNCPath(const char* path)
{
    std::wstring pathW;
    const wchar_t* widePath = nullptr;
    if (!WidenOptionalGeneralText(path, pathW, widePath))
        return FALSE;
    return WideGeneral.IsUNCPath(widePath);
}

BOOL WINAPI CLegacySalamanderGeneral::ResolveSubsts(char* resPath)
{
    if (resPath == nullptr)
        return FALSE;
    std::wstring resPathW;
    if (!WidenPluginText(resPath, resPathW))
        return FALSE;
    CSalamanderStringBufferOwner owner(resPathW);
    if (!owner.IsValid())
        return FALSE;
    const BOOL result = WideGeneral.ResolveSubsts(owner.Buffer());
    if (!owner.GetValue(resPathW))
        return FALSE;

    std::string prepared;
    if (PrepareWideGeneralOutputExact(resPathW.c_str(), MAX_PATH, prepared) !=
        GeneralOutputCommitStatus::Complete)
        return FALSE;

    std::memcpy(resPath, prepared.c_str(), prepared.size() + 1);
    return result;
}

void WINAPI CLegacySalamanderGeneral::ResolveLocalPathWithReparsePoints(
    char* resPath, const char* path, BOOL* cutResPathIsPossible,
    BOOL* rootOrCurReparsePointSet, char* rootOrCurReparsePoint,
    char* junctionOrSymlinkTgt, int* linkType, char* netPath)
{
    // The frozen contract requires this out-pointer.  Do not let an invalid
    // legacy call turn a conversion boundary into a write through NULL.
    if (cutResPathIsPossible == nullptr)
        return;

    std::wstring pathW;
    if (!WidenPluginText(path, pathW))
        return;
    CSalamanderStringBufferOwner resPathOwner;
    CSalamanderStringBufferOwner rootOrCurReparsePointOwner;
    CSalamanderStringBufferOwner junctionOrSymlinkTgtOwner;
    CSalamanderStringBufferOwner netPathOwner;
    if (!resPathOwner.IsValid())
        return;

    // The frozen root flag is an in/out request: its companion path is only
    // published when the caller supplied both outputs and asked for it by
    // passing FALSE.  The live interface reports this information directly,
    // so keep it entirely staged until every requested string projects back
    // into the frozen ACP record exactly.
    const bool canPublishRootOrCurReparsePoint =
        rootOrCurReparsePointSet != nullptr &&
        *rootOrCurReparsePointSet == FALSE &&
        rootOrCurReparsePoint != nullptr;
    BOOL stagedCutResPathIsPossible = TRUE;
    BOOL stagedRootOrCurReparsePointSet = FALSE;
    int stagedLinkType = 0;
    if (!WideGeneral.ResolveLocalPathWithReparsePoints(
        pathW.c_str(), resPathOwner.Buffer(), &stagedCutResPathIsPossible,
        canPublishRootOrCurReparsePoint
            ? &stagedRootOrCurReparsePointSet
            : nullptr,
        canPublishRootOrCurReparsePoint
            ? rootOrCurReparsePointOwner.Buffer()
            : nullptr,
        junctionOrSymlinkTgt != nullptr
            ? junctionOrSymlinkTgtOwner.Buffer()
            : nullptr,
        linkType != nullptr ? &stagedLinkType : nullptr,
        netPath != nullptr ? netPathOwner.Buffer() : nullptr))
        return;

    std::wstring resPathW;
    std::wstring rootOrCurReparsePointW;
    std::wstring junctionOrSymlinkTgtW;
    std::wstring netPathW;
    if (!resPathOwner.GetValue(resPathW) ||
        (canPublishRootOrCurReparsePoint &&
         !rootOrCurReparsePointOwner.GetValue(rootOrCurReparsePointW)) ||
        (junctionOrSymlinkTgt != nullptr &&
         !junctionOrSymlinkTgtOwner.GetValue(junctionOrSymlinkTgtW)) ||
        (netPath != nullptr && !netPathOwner.GetValue(netPathW)))
        return;

    const bool publishRootOrCurReparsePoint =
        canPublishRootOrCurReparsePoint &&
        stagedRootOrCurReparsePointSet != FALSE;
    const bool publishJunctionOrSymlinkTgt =
        junctionOrSymlinkTgt != nullptr && !junctionOrSymlinkTgtW.empty();
    const bool publishLinkType = linkType != nullptr && stagedLinkType != 0;
    const bool publishNetPath = netPath != nullptr && !netPathW.empty();

    std::string preparedResPath;
    std::string preparedRootOrCurReparsePoint;
    std::string preparedJunctionOrSymlinkTgt;
    std::string preparedNetPath;
    if (PrepareWideGeneralOutputExact(resPathW.c_str(), MAX_PATH,
                                      preparedResPath) !=
            GeneralOutputCommitStatus::Complete ||
        (publishRootOrCurReparsePoint &&
         PrepareWideGeneralOutputExact(rootOrCurReparsePointW.c_str(), MAX_PATH,
                                       preparedRootOrCurReparsePoint) !=
             GeneralOutputCommitStatus::Complete) ||
        (publishJunctionOrSymlinkTgt &&
         PrepareWideGeneralOutputExact(junctionOrSymlinkTgtW.c_str(), MAX_PATH,
                                       preparedJunctionOrSymlinkTgt) !=
             GeneralOutputCommitStatus::Complete) ||
        (publishNetPath &&
         PrepareWideGeneralOutputExact(netPathW.c_str(), MAX_PATH,
                                       preparedNetPath) !=
             GeneralOutputCommitStatus::Complete))
    {
        return;
    }

    // A frozen caller cannot observe a partial result: non-representable or
    // over-capacity text leaves every legacy string and scalar untouched.
    std::memcpy(resPath, preparedResPath.c_str(), preparedResPath.size() + 1);
    if (stagedCutResPathIsPossible == FALSE)
        *cutResPathIsPossible = FALSE;
    if (publishRootOrCurReparsePoint)
    {
        std::memcpy(rootOrCurReparsePoint,
                    preparedRootOrCurReparsePoint.c_str(),
                    preparedRootOrCurReparsePoint.size() + 1);
        *rootOrCurReparsePointSet = TRUE;
    }
    if (publishJunctionOrSymlinkTgt)
        std::memcpy(junctionOrSymlinkTgt,
                    preparedJunctionOrSymlinkTgt.c_str(),
                    preparedJunctionOrSymlinkTgt.size() + 1);
    if (publishLinkType)
        *linkType = stagedLinkType;
    if (publishNetPath)
        std::memcpy(netPath, preparedNetPath.c_str(),
                    preparedNetPath.size() + 1);
}

BOOL WINAPI CLegacySalamanderGeneral::GetResolvedPathMountPointAndGUID(
    const char* path, char* mountPoint, char* guidPath)
{
    std::wstring pathW;
    if (!WidenPluginText(path, pathW))
        return FALSE;
    CSalamanderStringBufferOwner mountPointOwner;
    CSalamanderStringBufferOwner guidPathOwner;
    const BOOL result = WideGeneral.GetResolvedPathMountPointAndGUID(
        pathW.c_str(), mountPoint != nullptr ? mountPointOwner.Buffer() : nullptr,
        guidPath != nullptr ? guidPathOwner.Buffer() : nullptr);
    if (result == FALSE)
        return FALSE;

    std::wstring mountPointW;
    std::wstring guidPathW;
    if ((mountPoint != nullptr && !mountPointOwner.GetValue(mountPointW)) ||
        (guidPath != nullptr && !guidPathOwner.GetValue(guidPathW)))
        return FALSE;

    // Both frozen outputs are documented as MAX_PATH bytes but have no size
    // parameter.  Prepare the complete requested record before publishing
    // either path: a truncated mount point can address another volume, and a
    // GUID path is only useful when it describes that same volume.
    std::string preparedMountPoint;
    std::string preparedGuidPath;
    if ((mountPoint != nullptr &&
         PrepareWideGeneralOutputExact(mountPointW.c_str(), MAX_PATH,
                                       preparedMountPoint) !=
             GeneralOutputCommitStatus::Complete) ||
        (guidPath != nullptr &&
         PrepareWideGeneralOutputExact(guidPathW.c_str(), MAX_PATH,
                                       preparedGuidPath) !=
             GeneralOutputCommitStatus::Complete))
    {
        return FALSE;
    }

    if (mountPoint != nullptr)
        std::memcpy(mountPoint, preparedMountPoint.c_str(),
                    preparedMountPoint.size() + 1);
    if (guidPath != nullptr)
        std::memcpy(guidPath, preparedGuidPath.c_str(),
                    preparedGuidPath.size() + 1);
    return TRUE;
}

BOOL WINAPI CLegacySalamanderGeneral::PointToLocalDecimalSeparator(
    char* buffer, int bufferSize)
{
    if (buffer == nullptr || bufferSize <= 0)
        return FALSE;

    const int inputLength = BoundedPluginLength(buffer, bufferSize);
    if (inputLength == bufferSize)
        return FALSE;

    std::wstring inputW;
    if (!WidenPluginText(buffer, inputW))
        return FALSE;
    CSalamanderStringBufferOwner owner(inputW);
    if (!owner.IsValid() ||
        !WideGeneral.PointToLocalDecimalSeparator(owner.Buffer()))
        return FALSE;

    std::wstring resultW;
    if (!owner.GetValue(resultW))
        return FALSE;

    std::string prepared;
    if (PrepareWideGeneralOutputExact(resultW.c_str(),
                                      static_cast<std::size_t>(bufferSize),
                                      prepared) !=
        GeneralOutputCommitStatus::Complete)
    {
        return FALSE;
    }

    std::memcpy(buffer, prepared.c_str(), prepared.size() + 1);
    return TRUE;
}

void WINAPI CLegacySalamanderGeneral::SetPluginIconOverlays(int iconOverlaysCount, HICON* iconOverlays)
{
    WideGeneral.SetPluginIconOverlays(iconOverlaysCount, iconOverlays);
}

BOOL WINAPI CLegacySalamanderGeneral::SalGetFileSize2(
    const char* fileName, sdk107::CQuadWord& size, DWORD* err)
{
    std::wstring fileNameW;
    if (!WidenPluginText(fileName, fileNameW))
        return FALSE;
    // The frozen and live CQuadWord declarations are distinct value types.
    // The live query owns its success/failure zeroing and optional error
    // protocol, so convert its completed result after that single call.
    ::CQuadWord wideSize(0, 0);
    const BOOL result =
        WideGeneral.SalGetFileSize2(fileNameW.c_str(), wideSize, err);
    size = QuadWordToLegacy(wideSize);
    return result;
}

BOOL WINAPI CLegacySalamanderGeneral::GetLinkTgtFileSize(
    HWND parent, const char* fileName, sdk107::CQuadWord* size, BOOL* cancel,
    BOOL* ignoreAll)
{
    if (size == nullptr)
        return FALSE;
    std::wstring fileNameW;
    if (!WidenPluginText(fileName, fileNameW))
        return FALSE;
    // The wide owner controls the prompt, retry, cancellation, and ignore-all
    // protocol through the original Boolean pointers.  Only its namespace-
    // distinct CQuadWord output requires an adapter-owned staging value.
    ::CQuadWord wideSize(0, 0);
    const BOOL result = WideGeneral.GetLinkTgtFileSize(
        parent, fileNameW.c_str(), &wideSize, cancel, ignoreAll);
    *size = QuadWordToLegacy(wideSize);
    return result;
}

BOOL WINAPI CLegacySalamanderGeneral::DeleteDirLink(const char* name,
                                                     DWORD* err)
{
    std::wstring nameW;
    if (!WidenPluginText(name, nameW))
        return FALSE;
    return WideGeneral.DeleteDirLink(nameW.c_str(), err);
}

BOOL WINAPI CLegacySalamanderGeneral::ClearReadOnlyAttr(const char* name,
                                                         DWORD attr)
{
    std::wstring nameW;
    if (!WidenPluginText(name, nameW))
        return FALSE;
    return WideGeneral.ClearReadOnlyAttr(nameW.c_str(), attr);
}

BOOL WINAPI CLegacySalamanderGeneral::IsCriticalShutdown()
{
    return WideGeneral.IsCriticalShutdown();
}

void WINAPI CLegacySalamanderGeneral::CloseAllOwnedEnabledDialogs(HWND parent, DWORD tid)
{
    WideGeneral.CloseAllOwnedEnabledDialogs(parent, tid);
}

sdk107::CSalamanderMaskGroup* WINAPI CLegacySalamanderGeneral::AllocSalamanderMaskGroup()
{
    return State->Objects.PublishMaskGroup(WideGeneral.AllocSalamanderMaskGroup());
}

void WINAPI CLegacySalamanderGeneral::FreeSalamanderMaskGroup(sdk107::CSalamanderMaskGroup* maskGroup)
{
    if (::CSalamanderMaskGroup* wide = State->Objects.RetireMaskGroup(maskGroup))
        WideGeneral.FreeSalamanderMaskGroup(wide);
}

sdk107::CSalamanderBMSearchData* WINAPI CLegacySalamanderGeneral::AllocSalamanderBMSearchData()
{
    return State->Objects.PublishBMSearchData(WideGeneral.AllocSalamanderBMSearchData());
}

void WINAPI CLegacySalamanderGeneral::FreeSalamanderBMSearchData(sdk107::CSalamanderBMSearchData* data)
{
    if (::CSalamanderBMSearchData* wide = State->Objects.RetireBMSearchData(data))
        WideGeneral.FreeSalamanderBMSearchData(wide);
}

sdk107::CSalamanderREGEXPSearchData* WINAPI CLegacySalamanderGeneral::AllocSalamanderREGEXPSearchData()
{
    return State->Objects.PublishREGEXPSearchData(WideGeneral.AllocSalamanderREGEXPSearchData());
}

void WINAPI CLegacySalamanderGeneral::FreeSalamanderREGEXPSearchData(sdk107::CSalamanderREGEXPSearchData* data)
{
    if (::CSalamanderREGEXPSearchData* wide = State->Objects.RetireREGEXPSearchData(data))
        WideGeneral.FreeSalamanderREGEXPSearchData(wide);
}

sdk107::CSalamanderMD5* WINAPI CLegacySalamanderGeneral::AllocSalamanderMD5()
{
    return State->Objects.PublishMD5(WideGeneral.AllocSalamanderMD5());
}

void WINAPI CLegacySalamanderGeneral::FreeSalamanderMD5(sdk107::CSalamanderMD5* md5)
{
    if (::CSalamanderMD5* wide = State->Objects.RetireMD5(md5))
        WideGeneral.FreeSalamanderMD5(wide);
}

sdk107::CSalamanderDirectoryAbstract* WINAPI CLegacySalamanderGeneral::AllocSalamanderDirectory(BOOL isForFS)
{
    return State->Objects.PublishDirectory(WideGeneral.AllocSalamanderDirectory(isForFS), PluginDataResolver, BuiltForVersion);
}

void WINAPI CLegacySalamanderGeneral::FreeSalamanderDirectory(sdk107::CSalamanderDirectoryAbstract* salDir)
{
    if (::CSalamanderDirectoryAbstract* wide = State->Objects.RetireDirectory(salDir))
        WideGeneral.FreeSalamanderDirectory(wide);
}

sdk107::CSalamanderZLIBAbstract* WINAPI CLegacySalamanderGeneral::GetSalamanderZLIB()
{
    return State->ZLIB.Publish(WideGeneral.GetSalamanderZLIB());
}

sdk107::CSalamanderPNGAbstract* WINAPI CLegacySalamanderGeneral::GetSalamanderPNG()
{
    return State->PNG.Publish(WideGeneral.GetSalamanderPNG());
}

sdk107::CSalamanderCryptAbstract* WINAPI CLegacySalamanderGeneral::GetSalamanderCrypt()
{
    return State->Crypt.Publish(WideGeneral.GetSalamanderCrypt());
}

sdk107::CSalamanderPasswordManagerAbstract* WINAPI CLegacySalamanderGeneral::GetSalamanderPasswordManager()
{
    return State->PasswordManager.Publish(WideGeneral.GetSalamanderPasswordManager());
}

sdk107::CSalamanderBZIP2Abstract* WINAPI CLegacySalamanderGeneral::GetSalamanderBZIP2()
{
    return State->BZIP2.Publish(WideGeneral.GetSalamanderBZIP2());
}

sdk107::CPluginFSInterfaceAbstract* WINAPI CLegacySalamanderGeneral::GetPanelPluginFS(int panel)
{
    return PluginFSResolver.FindLegacy(WideGeneral.GetPanelPluginFS(panel));
}

sdk107::CPluginDataInterfaceAbstract* WINAPI CLegacySalamanderGeneral::GetPanelPluginData(int panel)
{
    return PluginDataResolver.FindLegacy(WideGeneral.GetPanelPluginData(panel));
}

const sdk107::CFileData* WINAPI CLegacySalamanderGeneral::GetPanelFocusedItem(int panel, BOOL* isDir)
{
    BOOL wideIsDir = FALSE;
    const ::CFileData* wide = WideGeneral.GetPanelFocusedItem(panel, isDir != nullptr ? &wideIsDir : nullptr);
    const sdk107::CFileData* legacy = State->PanelRows.Publish(wide, panel);
    if (legacy != nullptr && isDir != nullptr)
        *isDir = wideIsDir;
    return legacy;
}

const sdk107::CFileData* WINAPI CLegacySalamanderGeneral::GetPanelItem(int panel, int* index, BOOL* isDir)
{
    for (;;)
    {
        BOOL wideIsDir = FALSE;
        const ::CFileData* wide = WideGeneral.GetPanelItem(panel, index, isDir != nullptr ? &wideIsDir : nullptr);
        if (wide == nullptr)
            return nullptr;
        const sdk107::CFileData* legacy = State->PanelRows.Publish(wide, panel);
        if (legacy != nullptr)
        {
            if (isDir != nullptr)
                *isDir = wideIsDir;
            return legacy;
        }
    }
}

const sdk107::CFileData* WINAPI CLegacySalamanderGeneral::GetPanelSelectedItem(int panel, int* index, BOOL* isDir)
{
    for (;;)
    {
        BOOL wideIsDir = FALSE;
        const ::CFileData* wide = WideGeneral.GetPanelSelectedItem(panel, index, isDir != nullptr ? &wideIsDir : nullptr);
        if (wide == nullptr)
            return nullptr;
        const sdk107::CFileData* legacy = State->PanelRows.Publish(wide, panel);
        if (legacy != nullptr)
        {
            if (isDir != nullptr)
                *isDir = wideIsDir;
            return legacy;
        }
    }
}

void WINAPI CLegacySalamanderGeneral::SelectPanelItem(int panel, const sdk107::CFileData* file, BOOL select)
{
    const ::CFileData* wide = State->PanelRows.Resolve(file, panel);
    if (wide != nullptr)
        WideGeneral.SelectPanelItem(panel, wide, select);
}

void WINAPI CLegacySalamanderGeneral::SetPanelFocusedItem(int panel, const sdk107::CFileData* file, BOOL partVis)
{
    const ::CFileData* wide = State->PanelRows.Resolve(file, panel);
    if (wide != nullptr)
        WideGeneral.SetPanelFocusedItem(panel, wide, partVis);
}

BOOL WINAPI CLegacySalamanderGeneral::GetFilterFromPanel(
    int panel, char* masks, int masksBufSize)
{
    if (masks == nullptr || masksBufSize <= 0)
        return FALSE;
    CSalamanderStringBufferOwner owner;
    if (!owner.IsValid())
        return FALSE;
    const BOOL result = WideGeneral.GetFilterFromPanel(
        panel, owner.Buffer());
    if (result == FALSE)
        return FALSE;

    std::wstring masksW;
    if (!owner.GetValue(masksW))
        return FALSE;
    std::string prepared;
    if (PrepareWideGeneralOutputExact(
            masksW.c_str(), static_cast<std::size_t>(masksBufSize), prepared) !=
        GeneralOutputCommitStatus::Complete)
        return FALSE;
    std::memcpy(masks, prepared.c_str(), prepared.size() + 1);
    return TRUE;
}

BOOL WINAPI CLegacySalamanderGeneral::GetPanelWithPluginFS(sdk107::CPluginFSInterfaceAbstract* pluginFS, int& panel)
{
    ::CPluginFSInterfaceAbstract* wide = PluginFSResolver.Resolve(pluginFS);
    return wide != nullptr ? WideGeneral.GetPanelWithPluginFS(wide, panel) : FALSE;
}

void WINAPI CLegacySalamanderGeneral::PostRefreshPanelFS(sdk107::CPluginFSInterfaceAbstract* modifiedFS, BOOL focusFirstNewItem)
{
    ::CPluginFSInterfaceAbstract* wide = PluginFSResolver.Resolve(modifiedFS);
    if (wide != nullptr)
        WideGeneral.PostRefreshPanelFS(wide, focusFirstNewItem);
}

BOOL WINAPI CLegacySalamanderGeneral::CloseDetachedFS(HWND parent, sdk107::CPluginFSInterfaceAbstract* detachedFS)
{
    ::CPluginFSInterfaceAbstract* wide = PluginFSResolver.Resolve(detachedFS);
    if (wide == nullptr || !WideGeneral.CloseDetachedFS(parent, wide))
        return FALSE;
    return TRUE;
}

BOOL WINAPI CLegacySalamanderGeneral::DuplicateAmpersands(
    char* buffer, int bufferSize)
{
    if (buffer == nullptr || bufferSize <= 0)
        return FALSE;

    const std::size_t legacyLength = std::strlen(buffer);
    if (legacyLength >= static_cast<std::size_t>(bufferSize))
        return FALSE;

    std::wstring wideBuffer;
    if (!WidenPluginText(buffer, wideBuffer))
        return FALSE;
    if (!SPLDuplicateAmpersandsOwned(&WideGeneral, wideBuffer))
        return FALSE;

    std::string prepared;
    if (!PrepareLegacyTruncatedGeneralTextOutput(
            wideBuffer.c_str(), static_cast<std::size_t>(bufferSize), prepared))
        return FALSE;

    std::memcpy(buffer, prepared.c_str(), prepared.size() + 1);
    const NarrowResult complete = NarrowExact(wideBuffer);
    return complete.ok &&
           complete.value.size() < static_cast<std::size_t>(bufferSize);
}

void WINAPI CLegacySalamanderGeneral::RemoveAmpersands(char* text)
{
    if (text == nullptr)
        return;

    const std::size_t legacyLength = std::strlen(text);
    std::wstring wideText;
    if (!WidenPluginText(text, wideText))
        return;
    if (!SPLRemoveAmpersandsOwned(&WideGeneral, wideText))
        return;

    std::string prepared;
    if (PrepareWideGeneralOutputExact(
            wideText.c_str(), legacyLength + 1, prepared) !=
        GeneralOutputCommitStatus::Complete)
        return;

    std::memcpy(text, prepared.c_str(), prepared.size() + 1);
}

BOOL WINAPI CLegacySalamanderGeneral::ValidateVarString(
    HWND msgParent, const char* varText, int& errorPos1, int& errorPos2,
    const CSalamanderVarStrEntry* variables)
{
    if (varText == nullptr || variables == nullptr)
        return FALSE;

    std::wstring varTextW;
    if (!WidenPluginText(varText, varTextW))
        return FALSE;

    // Validation consumes names only. Own every widened name until the live call
    // returns and never carry the frozen executable callback into the live ABI.
    std::size_t variableCount = 0;
    while (variables[variableCount].Name != nullptr)
        ++variableCount;
    std::vector<std::wstring> variableNames;
    variableNames.reserve(variableCount);
    for (std::size_t index = 0; index < variableCount; ++index)
    {
        std::wstring name;
        if (!WidenPluginText(variables[index].Name, name))
            return FALSE;
        variableNames.push_back(std::move(name));
    }

    std::vector<::CSalamanderVarStrEntry> variablesW;
    variablesW.reserve(variableCount + 1);
    for (const std::wstring& name : variableNames)
        variablesW.push_back({name.c_str(), nullptr});
    variablesW.push_back({nullptr, nullptr});

    int wideErrorPos1 = 0;
    int wideErrorPos2 = 0;
    const BOOL result = WideGeneral.ValidateVarString(
        msgParent, varTextW.c_str(), wideErrorPos1, wideErrorPos2,
        variablesW.data());
    if (result != FALSE)
        return TRUE;

    int legacyErrorPos1 = 0;
    int legacyErrorPos2 = 0;
    if (!PrepareLegacyVarStringErrorPositions(
            varText, varTextW, wideErrorPos1, wideErrorPos2,
            legacyErrorPos1, legacyErrorPos2))
        return FALSE;

    errorPos1 = legacyErrorPos1;
    errorPos2 = legacyErrorPos2;
    return FALSE;
}

BOOL WINAPI CLegacySalamanderGeneral::ExpandVarString(
    HWND msgParent, const char* varText, char* buffer, int bufferLen,
    const CSalamanderVarStrEntry* variables, void* param,
    BOOL ignoreEnvVarNotFoundOrTooLong, DWORD* varPlacements,
    int* varPlacementsCount, BOOL detectMaxVarWidths, int* maxVarWidths,
    int maxVarWidthsCount)
{
    if (buffer == nullptr || bufferLen <= 0 || varText == nullptr || variables == nullptr)
        return FALSE;
    if (varPlacementsCount != nullptr && *varPlacementsCount > 0 &&
        varPlacements == nullptr)
        return FALSE;
    if (maxVarWidthsCount > 0 && maxVarWidths == nullptr)
        return FALSE;

    std::wstring varTextW;
    if (!WidenPluginText(varText, varTextW))
        return FALSE;

    std::size_t variableCount = 0;
    while (variables[variableCount].Name != nullptr)
        ++variableCount;
    std::vector<std::wstring> variableNames;
    variableNames.reserve(variableCount);
    for (std::size_t index = 0; index < variableCount; ++index)
    {
        std::wstring name;
        if (!WidenPluginText(variables[index].Name, name))
            return FALSE;
        variableNames.push_back(std::move(name));
    }

    const int placementCapacity =
        varPlacementsCount != nullptr && *varPlacementsCount > 0
            ? *varPlacementsCount
            : 0;
    std::vector<sally::unicode::WideTextRange> widePlacements;

    const auto resolve = [&](const wchar_t* name, int nameLength, bool execute,
                             int requestedWidth, std::wstring& value,
                             int& measurementWidth) {
        std::size_t entryIndex = 0;
        while (entryIndex < variableNames.size() &&
               !LegacyVariableNameEquals(name, nameLength,
                                         variableNames[entryIndex]))
            ++entryIndex;
        if (entryIndex == variableNames.size())
            return sally::unicode::WideVarResolveResult::NotFound;
        const CSalamanderVarStrEntry* entry = variables + entryIndex;
        if (!execute)
            return sally::unicode::WideVarResolveResult::Found;
        if (entry->Execute == nullptr)
            return sally::unicode::WideVarResolveResult::Failed;

        const char* rawValue = entry->Execute(msgParent, param);
        if (rawValue == nullptr)
            return sally::unicode::WideVarResolveResult::Failed;
        std::string formatted(rawValue);
        if (formatted.size() >
            static_cast<std::size_t>((std::numeric_limits<int>::max)()) ||
            requestedWidth < 0)
            return sally::unicode::WideVarResolveResult::Failed;
        measurementWidth = static_cast<int>(formatted.size());
        if (requestedWidth > 0)
        {
            const std::size_t width = static_cast<std::size_t>(requestedWidth);
            if (formatted.size() > width)
                formatted.resize(width);
            else if (formatted.size() < width)
                formatted.append(width - formatted.size(), ' ');
        }
        if (!WidenPluginText(formatted.c_str(), value))
            return sally::unicode::WideVarResolveResult::Failed;
        return sally::unicode::WideVarResolveResult::Found;
    };

    const auto handleEnvironmentError =
        [&](const sally::unicode::WideVarError& environmentError) {
            if (ignoreEnvVarNotFoundOrTooLong != FALSE)
                return true;
            const std::wstring expression =
                L"$[" + environmentError.Argument + L"]";
            const ::CSalamanderVarStrEntry emptyVariables[] = {
                {nullptr, nullptr}};
            CSalamanderStringBufferOwner ignoredOutput;
            return WideGeneral.ExpandVarString(
                       msgParent, expression.c_str(), ignoredOutput.Buffer(),
                       emptyVariables, nullptr, FALSE) != FALSE;
        };

    std::wstring wideOutputText;
    sally::unicode::WideVarError error;
    if (!sally::unicode::ExpandWideVarStringCore(
            varTextW.c_str(), false, resolve, &wideOutputText,
            varPlacementsCount != nullptr ? &widePlacements : nullptr,
            detectMaxVarWidths != FALSE, maxVarWidths, maxVarWidthsCount,
            (std::numeric_limits<std::size_t>::max)(), &error,
            handleEnvironmentError))
    {
        if (error.Kind == sally::unicode::WideVarErrorKind::VariableCallbackFailed)
        {
            const ::CSalamanderVarStrEntry failedVariables[] = {
                {L"legacy", FailedLiveVariableCallback}, {nullptr, nullptr}};
            CSalamanderStringBufferOwner ignoredOutput;
            WideGeneral.ExpandVarString(
                msgParent, L"$(legacy)", ignoredOutput.Buffer(), failedVariables,
                nullptr, TRUE);
        }
        else if (error.Kind != sally::unicode::WideVarErrorKind::EnvironmentNotFound &&
                 error.Kind != sally::unicode::WideVarErrorKind::EnvironmentTooLarge)
        {
            int ignoredErrorPos1 = 0;
            int ignoredErrorPos2 = 0;
            ValidateVarString(msgParent, varText, ignoredErrorPos1,
                              ignoredErrorPos2, variables);
        }
        return FALSE;
    }

    std::string preparedOutput;
    std::vector<DWORD> preparedPlacements;
    const std::size_t projectedPlacementCount = (std::min)(
        widePlacements.size(), static_cast<std::size_t>(placementCapacity));
    if (!PrepareLegacyVarStringExpansionOutput(
            wideOutputText,
            projectedPlacementCount > 0 ? widePlacements.data() : nullptr,
            projectedPlacementCount, static_cast<std::size_t>(bufferLen),
            preparedOutput, preparedPlacements))
        return FALSE;

    std::memcpy(buffer, preparedOutput.c_str(), preparedOutput.size() + 1);
    if (varPlacements != nullptr && !preparedPlacements.empty())
        std::copy(preparedPlacements.begin(), preparedPlacements.end(), varPlacements);
    if (varPlacementsCount != nullptr)
        *varPlacementsCount = static_cast<int>(projectedPlacementCount);
    return TRUE;
}

BOOL WINAPI CLegacySalamanderGeneral::EnumInstalledModules(
    int* index, char* module, char* version)
{
    if (index == nullptr || module == nullptr || version == nullptr)
        return FALSE;

    for (;;)
    {
        std::wstring wideModule;
        std::wstring wideVersion;
        if (!SPLEnumInstalledModulesOwned(
                &WideGeneral, index, wideModule, wideVersion))
            return FALSE;

        std::string preparedModule;
        std::string preparedVersion;
        if (!PrepareLegacyInstalledModuleOutputs(
                wideModule.c_str(), wideVersion.c_str(), preparedModule,
                preparedVersion))
        {
            // Enumeration already advanced. Skip a row the frozen plugin
            // cannot represent instead of reporting a false end-of-list.
            continue;
        }

        std::memcpy(module, preparedModule.c_str(), preparedModule.size() + 1);
        std::memcpy(version, preparedVersion.c_str(), preparedVersion.size() + 1);
        return TRUE;
    }
}

BOOL WINAPI CLegacySalamanderGeneral::AddPluginFSTimer(int timeout, sdk107::CPluginFSInterfaceAbstract* timerOwner, DWORD timerParam)
{
    ::CPluginFSInterfaceAbstract* wide = PluginFSResolver.Resolve(timerOwner);
    return wide != nullptr ? WideGeneral.AddPluginFSTimer(timeout, wide, timerParam) : FALSE;
}

int WINAPI CLegacySalamanderGeneral::KillPluginFSTimer(sdk107::CPluginFSInterfaceAbstract* timerOwner, BOOL allTimers, DWORD timerParam)
{
    ::CPluginFSInterfaceAbstract* wide = PluginFSResolver.Resolve(timerOwner);
    return wide != nullptr ? WideGeneral.KillPluginFSTimer(wide, allTimers, timerParam) : 0;
}

BOOL WINAPI CLegacySalamanderGeneral::PostRefreshPanelFS2(sdk107::CPluginFSInterfaceAbstract* modifiedFS, BOOL focusFirstNewItem)
{
    ::CPluginFSInterfaceAbstract* wide = PluginFSResolver.Resolve(modifiedFS);
    return wide != nullptr ? WideGeneral.PostRefreshPanelFS2(wide, focusFirstNewItem) : FALSE;
}

int WINAPI CLegacySalamanderGeneral::SalMessageBoxEx(
    const sdk107::MSGBOXEX_PARAMS* params)
{
    CLegacyMessageBoxParams wide;
    return CopyLegacyMessageBoxParams(params, wide)
               ? WideGeneral.SalMessageBoxEx(&wide.Params)
               : 0;
}

BOOL WINAPI CLegacySalamanderGeneral::SalGetFileSize(
    HANDLE file, sdk107::CQuadWord& size, DWORD& err)
{
    // 'size' is output-only; do not read a plugin's default-constructed (and
    // therefore intentionally uninitialized) CQuadWord before the live call.
    ::CQuadWord wideSize(0, 0);
    const BOOL result = WideGeneral.SalGetFileSize(file, wideSize, err);
    size = QuadWordToLegacy(wideSize);
    return result;
}

void WINAPI CLegacySalamanderGeneral::CallLoadOrSaveConfiguration(
    BOOL load, sdk107::FSalLoadOrSaveConfiguration loadOrSaveFunc,
    void* param)
{
    if (loadOrSaveFunc == nullptr)
        return;
    CLegacyLoadOrSaveConfigurationCallback callback(loadOrSaveFunc, param);
    WideGeneral.CallLoadOrSaveConfiguration(
        load, &CLegacyLoadOrSaveConfigurationCallback::Invoke, &callback);
}

BOOL WINAPI CLegacySalamanderGeneral::CopyTextToClipboard(
    const char* text, int textLen, BOOL showEcho, HWND echoParent)
{
    std::wstring textW;
    if (!PrepareLegacyClipboardText(text, textLen, textW))
        return FALSE;
    return WideGeneral.CopyTextToClipboard(
        textW.c_str(), static_cast<int>(textW.size()), showEcho, echoParent);
}

BOOL WINAPI CLegacySalamanderGeneral::CopyTextToClipboardW(
    const wchar_t* text, int textLen, BOOL showEcho, HWND echoParent)
{
    return WideGeneral.CopyTextToClipboard(text, textLen, showEcho, echoParent);
}

void WINAPI CLegacySalamanderGeneral::SetPluginBugReportInfo(
    const char* message, const char* email)
{
    std::wstring messageW;
    std::wstring emailW;
    const wchar_t* wideMessage = nullptr;
    if (!WidenOptionalGeneralText(message, messageW, wideMessage))
        return;
    const wchar_t* wideEmail = nullptr;
    if (email != nullptr)
    {
        const int emailLength = BoundedPluginLength(email, 100);
        if (!WidenPluginSpan(email, emailLength, emailW))
            return;
        wideEmail = emailW.c_str();
    }
    WideGeneral.SetPluginBugReportInfo(wideMessage, wideEmail);
}

BOOL WINAPI CLegacySalamanderGeneral::IsPluginInstalled(
    const char* pluginSPL)
{
    std::wstring pluginSPLW;
    const wchar_t* widePluginSPL = nullptr;
    if (!WidenOptionalGeneralText(pluginSPL, pluginSPLW, widePluginSPL))
        return FALSE;
    return WideGeneral.IsPluginInstalled(widePluginSPL);
}

BOOL WINAPI CLegacySalamanderGeneral::ViewFileInPluginViewer(
    const char* pluginSPL,
    sdk107::CSalamanderPluginViewerData* pluginData, BOOL useCache,
    const char* rootTmpPath, const char* fileNameInCache, int& error)
{
    std::wstring pluginSPLW;
    std::wstring rootTmpPathW;
    std::wstring fileNameInCacheW;
    const wchar_t* widePluginSPL = nullptr;
    const wchar_t* wideRootTmpPath = nullptr;
    const wchar_t* wideFileNameInCache = nullptr;
    if (!WidenOptionalGeneralText(pluginSPL, pluginSPLW, widePluginSPL))
        return FALSE;
    if (useCache != FALSE)
    {
        if (!WidenOptionalGeneralText(rootTmpPath, rootTmpPathW,
                                      wideRootTmpPath) ||
            !WidenOptionalGeneralText(fileNameInCache, fileNameInCacheW,
                                      wideFileNameInCache))
            return FALSE;
    }

    static_assert(sizeof(::CSalamanderPluginViewerData) ==
                  sizeof(sdk107::CSalamanderPluginViewerData));
    static_assert(alignof(::CSalamanderPluginViewerData) ==
                  alignof(sdk107::CSalamanderPluginViewerData));
    static_assert(offsetof(::CSalamanderPluginViewerData, Size) ==
                  offsetof(sdk107::CSalamanderPluginViewerData, Size));
    static_assert(offsetof(::CSalamanderPluginViewerData, FileName) ==
                  offsetof(sdk107::CSalamanderPluginViewerData, FileName));
    static_assert(sizeof(::CSalamanderPluginInternalViewerData) ==
                  sizeof(sdk107::CSalamanderPluginInternalViewerData));
    static_assert(alignof(::CSalamanderPluginInternalViewerData) ==
                  alignof(sdk107::CSalamanderPluginInternalViewerData));
    static_assert(offsetof(::CSalamanderPluginInternalViewerData, Mode) ==
                  offsetof(sdk107::CSalamanderPluginInternalViewerData, Mode));
    static_assert(offsetof(::CSalamanderPluginInternalViewerData, Caption) ==
                  offsetof(sdk107::CSalamanderPluginInternalViewerData, Caption));
    static_assert(offsetof(::CSalamanderPluginInternalViewerData, WholeCaption) ==
                  offsetof(sdk107::CSalamanderPluginInternalViewerData, WholeCaption));

    CLegacyViewerDataBridge viewerData;
    if (!viewerData.Prepare(pluginData))
        return FALSE;
    return WideGeneral.ViewFileInPluginViewer(
        widePluginSPL, viewerData.Data(), useCache, wideRootTmpPath,
        wideFileNameInCache, error);
}

void WINAPI CLegacySalamanderGeneral::PostChangeOnPathNotification(
    const char* path, BOOL includingSubdirs)
{
    std::wstring pathW;
    const wchar_t* widePath = nullptr;
    if (!WidenOptionalGeneralText(path, pathW, widePath))
        return;
    WideGeneral.PostChangeOnPathNotification(widePath, includingSubdirs);
}

DWORD WINAPI CLegacySalamanderGeneral::SalCheckPath(
    BOOL echo, const char* path, DWORD err, HWND parent)
{
    std::wstring pathW;
    const wchar_t* widePath = nullptr;
    if (!WidenOptionalGeneralText(path, pathW, widePath))
        return GetLastError();
    return WideGeneral.SalCheckPath(echo, widePath, err, parent);
}

BOOL WINAPI CLegacySalamanderGeneral::SalCheckAndRestorePath(
    HWND parent, const char* path, BOOL tryNet)
{
    std::wstring pathW;
    const wchar_t* widePath = nullptr;
    if (!WidenOptionalGeneralText(path, pathW, widePath))
        return FALSE;
    return WideGeneral.SalCheckAndRestorePath(parent, widePath, tryNet);
}

BOOL WINAPI CLegacySalamanderGeneral::SalCheckAndRestorePathWithCut(
    HWND parent, char* path, BOOL& tryNet, DWORD& err, DWORD& lastErr,
    BOOL& pathInvalid, BOOL& cut, BOOL donotReconnect)
{
    if (path == nullptr)
        return FALSE;

    const std::size_t pathCapacity = std::strlen(path) + 1;
    std::wstring pathW;
    if (!WidenPluginText(path, pathW))
        return FALSE;
    BOOL stagedTryNet = tryNet;
    DWORD stagedErr = ERROR_SUCCESS;
    DWORD stagedLastErr = ERROR_SUCCESS;
    BOOL stagedPathInvalid = FALSE;
    BOOL stagedCut = FALSE;
    const BOOL result = WideGeneral.SalCheckAndRestorePathWithCut(
        parent, pathW, stagedTryNet, stagedErr, stagedLastErr,
        stagedPathInvalid, stagedCut, donotReconnect);

    std::string preparedPath;
    if (PrepareWideGeneralOutputExact(
            pathW.c_str(), pathCapacity, preparedPath) !=
        GeneralOutputCommitStatus::Complete)
        return FALSE;

    std::memcpy(path, preparedPath.c_str(), preparedPath.size() + 1);
    tryNet = stagedTryNet;
    err = stagedErr;
    lastErr = stagedLastErr;
    pathInvalid = stagedPathInvalid;
    cut = stagedCut;
    return result;
}

BOOL WINAPI CLegacySalamanderGeneral::SalParsePath(
    HWND parent, char* path, int& type, BOOL& isDir, char*& secondPart,
    const char* errorTitle, char* nextFocus, BOOL curPathIsDiskOrArchive,
    const char* curPath, const char* curArchivePath, int* error,
    int pathBufSize)
{
    std::string originalPath;
    bool originalPathAvailable = false;
    const auto refuseBoundary = [&]() {
        if (nextFocus != nullptr)
            nextFocus[0] = '\0';
        if (originalPathAvailable)
            std::memcpy(path, originalPath.c_str(), originalPath.size() + 1);
        type = -1;
        isDir = FALSE;
        secondPart = nullptr;
        if (error != nullptr)
            *error = SPP_WINDOWSPATHERROR;
        return FALSE;
    };

    if (path == nullptr || pathBufSize <= 0)
        return refuseBoundary();
    const int pathLength = BoundedPluginLength(path, pathBufSize);
    if (pathLength == pathBufSize)
        return refuseBoundary();

    // Consume every possibly aliased input before live General can publish an
    // output. In particular, nextFocus is allowed to share the path allocation.
    originalPath.assign(path, static_cast<std::size_t>(pathLength));
    originalPathAvailable = true;
    std::wstring pathInputW;
    std::wstring errorTitleW;
    std::wstring curPathW;
    std::wstring curArchivePathW;
    const wchar_t* wideErrorTitle = nullptr;
    const wchar_t* wideCurPath = nullptr;
    const wchar_t* wideCurArchivePath = nullptr;
    if (!WidenPluginText(originalPath.c_str(), pathInputW) ||
        !WidenOptionalGeneralText(errorTitle, errorTitleW, wideErrorTitle) ||
        !WidenOptionalGeneralText(curPath, curPathW, wideCurPath) ||
        !WidenOptionalGeneralText(curArchivePath, curArchivePathW,
                                  wideCurArchivePath) ||
        pathInputW.size() + 1 > static_cast<std::size_t>(pathBufSize))
        return refuseBoundary();

    CSalamanderStringBufferOwner pathOwner(pathInputW);
    CSalamanderStringBufferOwner focusOwner;
    if (!pathOwner.IsValid() || (nextFocus != nullptr && !focusOwner.IsValid()))
        return refuseBoundary();
    int stagedType = -1;
    BOOL stagedIsDir = FALSE;
    DWORD stagedSecondPartOffset = SAL_STRING_BUFFER_NPOS;
    int stagedError = 0;
    const BOOL result = WideGeneral.SalParsePath(
        parent, pathOwner.Buffer(), stagedType, stagedIsDir, stagedSecondPartOffset,
        wideErrorTitle, nextFocus != nullptr ? focusOwner.Buffer() : nullptr,
        curPathIsDiskOrArchive, wideCurPath, wideCurArchivePath,
        error != nullptr ? &stagedError : nullptr);

    std::wstring pathW;
    std::wstring focusW;
    if (!pathOwner.GetValue(pathW) ||
        (nextFocus != nullptr && !focusOwner.GetValue(focusW)))
        return refuseBoundary();
    const wchar_t* stagedSecondPart =
        stagedSecondPartOffset != SAL_STRING_BUFFER_NPOS &&
                stagedSecondPartOffset <= pathW.size()
            ? pathW.c_str() + stagedSecondPartOffset
            : nullptr;

    std::string preparedPath;
    std::string preparedFocus;
    std::size_t secondPartOffset = std::string::npos;
    if (PrepareLegacyParsePathOutputs(
            pathW.c_str(), stagedSecondPart,
            nextFocus != nullptr ? focusW.c_str() : nullptr,
            static_cast<std::size_t>(pathBufSize), result != FALSE,
            preparedPath, secondPartOffset, preparedFocus) !=
        GeneralOutputCommitStatus::Complete)
        return refuseBoundary();

    // Publish the focus before the path so the in/out path remains
    // authoritative even when the two frozen buffers alias exactly.
    if (nextFocus != nullptr)
        std::memcpy(nextFocus, preparedFocus.c_str(),
                    preparedFocus.size() + 1);
    std::memcpy(path, preparedPath.c_str(), preparedPath.size() + 1);
    type = stagedType;
    isDir = stagedIsDir;
    secondPart = nullptr;
    if (secondPartOffset != std::string::npos)
        secondPart = path + secondPartOffset;
    if (error != nullptr)
        *error = stagedError;
    return result;
}

BOOL WINAPI CLegacySalamanderGeneral::SalSplitWindowsPath(
    HWND parent, const char* title, const char* errorTitle, int selCount,
    char* path, char* secondPart, BOOL pathIsDir, BOOL backslashAtEnd,
    const char* dirName, const char* curDiskPath, char*& mask)
{
    mask = nullptr;
    if (path == nullptr || secondPart == nullptr)
        return FALSE;

    const int pathLength = BoundedPluginLength(path, 2 * MAX_PATH);
    if (pathLength == 2 * MAX_PATH)
        return FALSE;
    const std::string originalPath(path, static_cast<std::size_t>(pathLength));
    std::wstring pathInputW;
    std::size_t secondPartOffset = std::string::npos;
    if (!WidenPluginText(originalPath.c_str(), pathInputW) ||
        !WideOffsetForLegacyPointer(path, pathInputW, secondPart,
                                    secondPartOffset))
        return FALSE;

    // Consume every possibly aliased input before live General mutates its
    // staged path. The frozen contract permits all four text inputs to point
    // into the caller-owned path allocation.
    std::wstring titleW;
    std::wstring errorTitleW;
    std::wstring dirNameW;
    std::wstring curDiskPathW;
    const wchar_t* wideTitle = nullptr;
    const wchar_t* wideErrorTitle = nullptr;
    const wchar_t* wideDirName = nullptr;
    const wchar_t* wideCurDiskPath = nullptr;
    if (!WidenOptionalGeneralText(title, titleW, wideTitle) ||
        !WidenOptionalGeneralText(errorTitle, errorTitleW, wideErrorTitle) ||
        !WidenOptionalGeneralText(dirName, dirNameW, wideDirName) ||
        !WidenOptionalGeneralText(curDiskPath, curDiskPathW, wideCurDiskPath))
        return FALSE;

    CSalamanderStringBufferOwner pathOwner(pathInputW);
    CSalamanderStringBufferOwner maskOwner;
    if (!pathOwner.IsValid() || !maskOwner.IsValid())
        return FALSE;
    const BOOL result = WideGeneral.SalSplitWindowsPath(
        parent, wideTitle, wideErrorTitle, selCount, pathOwner.Buffer(),
        static_cast<DWORD>(secondPartOffset), pathIsDir, backslashAtEnd,
        wideDirName, wideCurDiskPath, maskOwner.Buffer());

    std::wstring pathW;
    std::wstring maskW;
    if (!pathOwner.GetValue(pathW) || !maskOwner.GetValue(maskW))
        return FALSE;
    std::vector<wchar_t> pathRecord(pathW.size() + maskW.size() + 2, L'\0');
    std::copy(pathW.begin(), pathW.end(), pathRecord.begin());
    std::copy(maskW.begin(), maskW.end(), pathRecord.begin() + pathW.size() + 1);
    wchar_t* wideMask = pathRecord.data() + pathW.size() + 1;

    std::string preparedRecord;
    std::size_t maskOffset = std::string::npos;
    if (PrepareLegacySplitWindowsPathOutput(
            pathRecord.data(), pathRecord.size(), result != FALSE ? wideMask : nullptr,
            2 * MAX_PATH, result != FALSE, preparedRecord, maskOffset) !=
        GeneralOutputCommitStatus::Complete)
        return FALSE;

    std::memcpy(path, preparedRecord.data(), preparedRecord.size() + 1);
    if (maskOffset != std::string::npos)
        mask = path + maskOffset;
    return result;
}

BOOL WINAPI CLegacySalamanderGeneral::SalSplitGeneralPath(
    HWND parent, const char* title, const char* errorTitle, int selCount,
    char* path, char* afterRoot, char* secondPart, BOOL pathIsDir,
    BOOL backslashAtEnd, const char* dirName, const char* curPath,
    char*& mask, char* newDirs, sdk107::SGP_IsTheSamePathF isTheSamePathF)
{
    mask = nullptr;
    if (path == nullptr || afterRoot == nullptr || secondPart == nullptr)
        return FALSE;

    const int pathLength = BoundedPluginLength(path, 2 * MAX_PATH);
    if (pathLength == 2 * MAX_PATH)
        return FALSE;
    const std::string originalPath(path, static_cast<std::size_t>(pathLength));
    std::wstring pathInputW;
    std::size_t afterRootOffset = std::string::npos;
    std::size_t secondPartOffset = std::string::npos;
    if (!WidenPluginText(originalPath.c_str(), pathInputW) ||
        !WideOffsetForLegacyPointer(path, pathInputW, afterRoot,
                                    afterRootOffset) ||
        !WideOffsetForLegacyPointer(path, pathInputW, secondPart,
                                    secondPartOffset))
        return FALSE;

    // Consume every input that may alias the caller-owned path before live
    // General mutates its staged record. The callback itself remains valid for
    // the duration of this synchronous call and is stacked separately below.
    std::wstring titleW;
    std::wstring errorTitleW;
    std::wstring dirNameW;
    std::wstring curPathW;
    const wchar_t* wideTitle = nullptr;
    const wchar_t* wideErrorTitle = nullptr;
    const wchar_t* wideDirName = nullptr;
    const wchar_t* wideCurPath = nullptr;
    if (!WidenOptionalGeneralText(title, titleW, wideTitle) ||
        !WidenOptionalGeneralText(errorTitle, errorTitleW, wideErrorTitle) ||
        !WidenOptionalGeneralText(dirName, dirNameW, wideDirName) ||
        !WidenOptionalGeneralText(curPath, curPathW, wideCurPath))
        return FALSE;

    CSalamanderStringBufferOwner pathOwner(pathInputW);
    CSalamanderStringBufferOwner maskOwner;
    CSalamanderStringBufferOwner newDirsOwner;
    if (!pathOwner.IsValid() || !maskOwner.IsValid() ||
        (newDirs != nullptr && !newDirsOwner.IsValid()))
        return FALSE;
    CLegacySamePathCallbackScope callback(isTheSamePathF);
    const BOOL result = WideGeneral.SalSplitGeneralPath(
        parent, wideTitle, wideErrorTitle, selCount, pathOwner.Buffer(),
        static_cast<DWORD>(afterRootOffset), static_cast<DWORD>(secondPartOffset),
        pathIsDir, backslashAtEnd, wideDirName, wideCurPath, maskOwner.Buffer(),
        newDirs != nullptr ? newDirsOwner.Buffer() : nullptr,
        callback.WideCallback());

    std::wstring pathW;
    std::wstring maskW;
    std::wstring newDirsW;
    if (!pathOwner.GetValue(pathW) || !maskOwner.GetValue(maskW) ||
        (newDirs != nullptr && !newDirsOwner.GetValue(newDirsW)))
        return FALSE;
    std::vector<wchar_t> pathRecord(pathW.size() + maskW.size() + 2, L'\0');
    std::copy(pathW.begin(), pathW.end(), pathRecord.begin());
    std::copy(maskW.begin(), maskW.end(), pathRecord.begin() + pathW.size() + 1);
    wchar_t* wideMask = pathRecord.data() + pathW.size() + 1;

    std::string preparedPath;
    std::string preparedNewDirs;
    std::size_t maskOffset = std::string::npos;
    if (PrepareLegacySplitGeneralPathOutputs(
            pathRecord.data(), pathRecord.size(),
            result != FALSE ? wideMask : nullptr, result != FALSE,
            newDirs != nullptr ? newDirsW.c_str() : nullptr,
            newDirs != nullptr, 2 * MAX_PATH, MAX_PATH, preparedPath,
            maskOffset, preparedNewDirs) !=
        GeneralOutputCommitStatus::Complete)
        return FALSE;

    // Prepare both allocations before publishing either. Copy the subordinate
    // new-directory output first so the path remains authoritative even for an
    // invalid caller that aliases the two output buffers exactly.
    if (newDirs != nullptr)
        std::memcpy(newDirs, preparedNewDirs.c_str(),
                    preparedNewDirs.size() + 1);
    std::memcpy(path, preparedPath.data(), preparedPath.size() + 1);
    if (maskOffset != std::string::npos)
        mask = path + maskOffset;
    return result;
}

BOOL WINAPI CLegacySalamanderGeneral::SalRemovePointsFromPath(
    char* afterRoot)
{
    if (afterRoot == nullptr)
        return FALSE;

    const std::size_t legacyLength = std::strlen(afterRoot);
    std::wstring afterRootW;
    if (!WidenPluginText(afterRoot, afterRootW))
        return FALSE;
    CSalamanderStringBufferOwner owner(afterRootW);
    if (!owner.IsValid())
        return FALSE;
    const BOOL result = WideGeneral.SalRemovePointsFromPath(owner.Buffer());
    if (!owner.GetValue(afterRootW))
        return FALSE;

    std::string prepared;
    if (PrepareWideGeneralOutputExact(
            afterRootW.c_str(), legacyLength + 1, prepared) !=
        GeneralOutputCommitStatus::Complete)
        return FALSE;

    std::memcpy(afterRoot, prepared.c_str(), prepared.size() + 1);
    return result;
}

void WINAPI CLegacySalamanderGeneral::AlterFileName(
    char* tgtName, char* srcName, int format, int changedParts, BOOL isDir)
{
    if (tgtName == nullptr || srcName == nullptr)
        return;

    // The frozen contract gives no explicit target size: it only promises a
    // buffer large enough for the original source. Consume the source before
    // any output so the exact in-place case remains safe as well.
    const std::size_t frozenCapacity = std::strlen(srcName) + 1;
    std::wstring sourceW;
    if (!WidenPluginText(srcName, sourceW))
        return;
    std::wstring target;
    if (!SPLAlterFileNameOwned(&WideGeneral, sourceW.c_str(), format,
                               changedParts, isDir, target))
        return;

    std::string prepared;
    if (PrepareWideGeneralOutputExact(target.c_str(), frozenCapacity,
                                      prepared) !=
        GeneralOutputCommitStatus::Complete)
        return;

    std::memcpy(tgtName, prepared.c_str(), prepared.size() + 1);
}

void WINAPI CLegacySalamanderGeneral::CallPluginOperationFromDisk(
    int panel, sdk107::SalPluginOperationFromDisk operation, void* param)
{
    if (operation == nullptr)
        return;
    CLegacyPluginOperationFromDiskCallback callback(operation, param);
    WideGeneral.CallPluginOperationFromDisk(
        panel, &CLegacyPluginOperationFromDiskCallback::Invoke, &callback);
}

BOOL WINAPI CLegacySalamanderGeneral::OpenHtmlHelp(
    HWND parent, sdk107::CHtmlHelpCommand command, DWORD_PTR dwData,
    BOOL quiet)
{
    ::CHtmlHelpCommand wideCommand = ::HHCDisplayTOC;
    return MapLegacyHtmlHelpCommand(command, wideCommand)
               ? WideGeneral.OpenHtmlHelp(parent, wideCommand, dwData, quiet)
               : FALSE;
}

BOOL WINAPI CLegacySalamanderGeneral::OpenHtmlHelpForSalamander(
    HWND parent, sdk107::CHtmlHelpCommand command, DWORD_PTR dwData,
    BOOL quiet)
{
    ::CHtmlHelpCommand wideCommand = ::HHCDisplayTOC;
    return MapLegacyHtmlHelpCommand(command, wideCommand)
               ? WideGeneral.OpenHtmlHelpForSalamander(parent, wideCommand,
                                                       dwData, quiet)
               : FALSE;
}

BOOL WINAPI CLegacySalamanderGeneral::GetThemeInfo(
    sdk107::CSalamanderThemeInfo* info)
{
    ::CSalamanderThemeInfo wide = {};
    if (!PrepareLegacyThemeInfo(info, wide) ||
        !WideGeneral.GetThemeInfo(&wide))
        return FALSE;
    CommitLegacyThemeInfo(wide, *info);
    return TRUE;
}

} // namespace sally::compat
