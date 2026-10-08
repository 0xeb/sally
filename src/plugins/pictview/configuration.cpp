// SPDX-FileCopyrightText: 2025-2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#include "configuration.h"
#include "codec_masks.h"

#include "viewer.h"

#include <algorithm>
#include <limits>
#include <mutex>
#include <string>

#include <commctrl.h>
#include <commdlg.h>

#include "spl_com.h"
#include "spl_base.h"
#include "spl_gen.h"

namespace pictview
{
namespace
{

const wchar_t* ConfigCopyToKey = L"CopyTo";
const wchar_t* ConfigCopyToLastIndex = L"LastIndex";
const wchar_t* ConfigRecentFilesKey = L"FilesHistory";
const wchar_t* ConfigRecentDirectoriesKey = L"DirsHistory";
const wchar_t* ConfigVersion = L"Version";
const wchar_t* ConfigHistogram = L"Histogram Configuration";
const wchar_t* ConfigSaveKey = L"Save";
const wchar_t* ConfigSaveFlags = L"Flags";
const wchar_t* ConfigSaveJpegQuality = L"JPEG Quality";
const wchar_t* ConfigSaveJpegSubsampling = L"JPEG Subsampling";
const wchar_t* ConfigSaveTiffCompression = L"TIFF Compression";
const wchar_t* ConfigSaveTiffStripSize = L"TIFF Strip Size";
const wchar_t* ConfigSaveInitDir = L"InitDir";
const wchar_t* ConfigSaveRememberPath = L"RememberPath";
const wchar_t* ConfigSaveFilterMono = L"FilterMono";
const wchar_t* ConfigSaveFilterColor = L"FilterColor";
const wchar_t* ConfigDontShowAnymore = L"DontShowAnymore";
const wchar_t* ConfigToolbarVisible = L"ToolbarVisible";
const wchar_t* ConfigStatusbarVisible = L"StatusbarVisible";
const wchar_t* ConfigPageUpDownScrolls = L"PageUpDnScrolls";
const wchar_t* ConfigPipetteInHex = L"PipetteInHex";
const wchar_t* ConfigShowFullPathInTitle = L"ShowPathInTitle";
const wchar_t* ConfigAutoRotate = L"AutoRotate";
const wchar_t* ConfigIgnoreThumbnails = L"IgnoreThumbnails";
const wchar_t* ConfigCaptureCursor = L"CaptureCursor";
const wchar_t* ConfigCaptureScope = L"CaptureScope";
const wchar_t* ConfigCaptureTrigger = L"CaptureTrigger";
const wchar_t* ConfigCaptureHotKey = L"CaptureHotKey";
const wchar_t* ConfigCaptureTimer = L"CaptureTimer";
const wchar_t* ConfigMaxThumbImgSize = L"MaxThumbImgSize";
const wchar_t* ConfigSelectRatioX = L"SelectRatioX";
const wchar_t* ConfigSelectRatioY = L"SelectRatioY";
const wchar_t* ConfigDefaultZoomMode = L"ShrinkToFit";
const wchar_t* ConfigWindowSizeMode = L"WinPos";
const wchar_t* ConfigBackgroundColor = L"RendererWSColor";
const wchar_t* ConfigTransparentColor = L"RendererBGColor";
const wchar_t* ConfigFullScreenBackgroundColor = L"FullScreenWSColor";
const wchar_t* ConfigFullScreenTransparentColor = L"FullScreenBGColor";
const wchar_t* ConfigExifDialogWidth = L"ExifDlgWidth";
const wchar_t* ConfigExifDialogHeight = L"ExifDlgHeight";
const wchar_t* ConfigExifHighlights = L"ExifHighlights";
const wchar_t* ConfigExifGroupHighlights = L"ExifGroupHighlights";
const wchar_t* ConfigOfferedCodecExtensions = L"Codec Extensions Offered";

// The old PictView's CHistogramWindow::CConfiguration, field for field.
struct HistogramRegistryData
{
    int Version = 1;
    int WindowX = -1;
    int WindowY = -1;
    int HistogramHeight = 119;
    int HistogramWidth = 256;
    int Channel = static_cast<int>(ViewerHistogramChannel::Rgb);
    int ShowOtherChannels = TRUE;
};
static_assert(sizeof(HistogramRegistryData) == 7 * sizeof(int), "legacy layout");

constexpr DWORD HistogramRegistryVersion = 1;
// 22: the last closed-engine PictView; 23: the WIC PictView (viewer extensions reduced to what
// WIC decodes).
constexpr DWORD CurrentConfigVersion = 23;
DWORD LoadedVersion = 0;
constexpr DWORD DontShowUpdateThumbnails = 1;
constexpr DWORD DontShowSaveSuccess = 2;
constexpr DWORD DontShowAlphaLoss = 4;

int ClampCopyToHistoryIndex(int index)
{
    constexpr int entryCount = static_cast<int>(ViewerCopyToHistoryEntryCount);
    if (index < 0)
        return 0;
    if (index >= entryCount)
        return entryCount - 1;
    return index;
}

int CopyToHistoryIndexFromDword(DWORD index)
{
    constexpr DWORD entryCount = static_cast<DWORD>(ViewerCopyToHistoryEntryCount);
    if (index >= entryCount)
        return static_cast<int>(entryCount - 1);
    return static_cast<int>(index);
}

std::wstring HistoryValueName(std::size_t index)
{
    return std::to_wstring(index + 1);
}

bool LoadRegistryBool(CSalamanderRegistryAbstract* registry, HKEY key, const wchar_t* name, bool defaultValue)
{
    DWORD value = defaultValue ? 1u : 0u;
    if (registry == nullptr || key == nullptr || name == nullptr ||
        !registry->GetValue(key, name, REG_DWORD, &value, sizeof(value)))
    {
        return defaultValue;
    }
    return value != 0;
}

void SaveRegistryBool(CSalamanderRegistryAbstract* registry, HKEY key, const wchar_t* name, bool value)
{
    if (registry == nullptr || key == nullptr || name == nullptr)
        return;

    const DWORD data = value ? 1u : 0u;
    registry->SetValue(key, name, REG_DWORD, &data, sizeof(data));
}

bool TryLoadRegistryDword(CSalamanderRegistryAbstract* registry, HKEY key, const wchar_t* name, DWORD& value)
{
    value = 0;
    if (registry == nullptr || key == nullptr || name == nullptr)
        return false;

    return registry->GetValue(key, name, REG_DWORD, &value, sizeof(value)) != FALSE;
}

bool HasRegistryDword(CSalamanderRegistryAbstract* registry, HKEY key, const wchar_t* name)
{
    if (registry == nullptr || key == nullptr || name == nullptr)
        return false;

    DWORD bytes = 0;
    return registry->GetSize(key, name, REG_DWORD, bytes) && bytes == sizeof(DWORD);
}

DWORD LoadRegistryDword(CSalamanderRegistryAbstract* registry, HKEY key, const wchar_t* name, DWORD defaultValue)
{
    DWORD value = 0;
    if (!TryLoadRegistryDword(registry, key, name, value))
    {
        return defaultValue;
    }
    return value;
}

void SaveRegistryDword(CSalamanderRegistryAbstract* registry, HKEY key, const wchar_t* name, DWORD value)
{
    if (registry == nullptr || key == nullptr || name == nullptr)
        return;

    registry->SetValue(key, name, REG_DWORD, &value, sizeof(value));
}

ViewerDefaultZoomMode ViewerDefaultZoomModeFromOldDword(DWORD value)
{
    switch (value)
    {
    case 2:
        return ViewerDefaultZoomMode::FitWidth;
    case 3:
        return ViewerDefaultZoomMode::ActualSize;
    case 4:
        return ViewerDefaultZoomMode::FullScreen;
    case 1:
    default:
        return ViewerDefaultZoomMode::FitWhole;
    }
}

ViewerHistogramChannel HistogramChannelFromDword(DWORD value)
{
    switch (value)
    {
    case static_cast<DWORD>(ViewerHistogramChannel::Luminosity):
        return ViewerHistogramChannel::Luminosity;
    case static_cast<DWORD>(ViewerHistogramChannel::Red):
        return ViewerHistogramChannel::Red;
    case static_cast<DWORD>(ViewerHistogramChannel::Green):
        return ViewerHistogramChannel::Green;
    case static_cast<DWORD>(ViewerHistogramChannel::Blue):
        return ViewerHistogramChannel::Blue;
    case static_cast<DWORD>(ViewerHistogramChannel::Rgb):
    default:
        return ViewerHistogramChannel::Rgb;
    }
}

ViewerWindowSizeMode ViewerWindowSizeModeFromDword(DWORD value)
{
    switch (value)
    {
    case static_cast<DWORD>(ViewerWindowSizeMode::LargerIfNeeded):
        return ViewerWindowSizeMode::LargerIfNeeded;
    case static_cast<DWORD>(ViewerWindowSizeMode::AsNeeded):
        return ViewerWindowSizeMode::AsNeeded;
    case static_cast<DWORD>(ViewerWindowSizeMode::SameAsSally):
    default:
        return ViewerWindowSizeMode::SameAsSally;
    }
}

ViewerCaptureScope ViewerCaptureScopeFromDword(DWORD value)
{
    switch (value)
    {
    case static_cast<DWORD>(ViewerCaptureScope::Application):
        return ViewerCaptureScope::Application;
    case static_cast<DWORD>(ViewerCaptureScope::Window):
        return ViewerCaptureScope::Window;
    case static_cast<DWORD>(ViewerCaptureScope::ClientArea):
        return ViewerCaptureScope::ClientArea;
    case static_cast<DWORD>(ViewerCaptureScope::VirtualScreen):
        return ViewerCaptureScope::VirtualScreen;
    case static_cast<DWORD>(ViewerCaptureScope::Desktop):
    default:
        return ViewerCaptureScope::Desktop;
    }
}

ViewerCaptureTrigger ViewerCaptureTriggerFromDword(DWORD value)
{
    switch (value)
    {
    case static_cast<DWORD>(ViewerCaptureTrigger::Timer):
        return ViewerCaptureTrigger::Timer;
    case static_cast<DWORD>(ViewerCaptureTrigger::HotKey):
    default:
        return ViewerCaptureTrigger::HotKey;
    }
}

DWORD ViewerDefaultZoomModeToOldDword(ViewerDefaultZoomMode mode)
{
    switch (mode)
    {
    case ViewerDefaultZoomMode::FitWidth:
        return 2;
    case ViewerDefaultZoomMode::ActualSize:
        return 3;
    case ViewerDefaultZoomMode::FullScreen:
        return 4;
    case ViewerDefaultZoomMode::FitWhole:
    default:
        return 1;
    }
}

DWORD ViewerWindowSizeModeToDword(ViewerWindowSizeMode mode)
{
    return static_cast<DWORD>(mode);
}

DWORD ViewerCaptureScopeToDword(ViewerCaptureScope scope)
{
    return static_cast<DWORD>(scope);
}

DWORD ViewerCaptureTriggerToDword(ViewerCaptureTrigger trigger)
{
    return static_cast<DWORD>(trigger);
}

WORD SanitizeCaptureHotKey(DWORD hotKey)
{
    return hotKey == 0 || hotKey > 0xffffu ? static_cast<WORD>(VK_F10) : static_cast<WORD>(hotKey);
}

int SanitizeCaptureTimerSeconds(DWORD seconds)
{
    if (seconds < 1u || seconds > 360u)
        return 5;
    return static_cast<int>(seconds);
}

ViewerJpegSubsampling ViewerJpegSubsamplingFromDword(DWORD value)
{
    switch (value)
    {
    case static_cast<DWORD>(ViewerJpegSubsampling::OneToOneOne):
        return ViewerJpegSubsampling::OneToOneOne;
    case static_cast<DWORD>(ViewerJpegSubsampling::TwoToOneOne):
    default:
        return ViewerJpegSubsampling::TwoToOneOne;
    }
}

DWORD ViewerJpegSubsamplingToDword(ViewerJpegSubsampling subsampling)
{
    return static_cast<DWORD>(subsampling);
}

ViewerTiffCompression ViewerTiffCompressionFromDword(DWORD value)
{
    switch (value)
    {
    case static_cast<DWORD>(ViewerTiffCompression::None):
        return ViewerTiffCompression::None;
    case static_cast<DWORD>(ViewerTiffCompression::Lzw):
        return ViewerTiffCompression::Lzw;
    case static_cast<DWORD>(ViewerTiffCompression::Zip):
        return ViewerTiffCompression::Zip;
    case static_cast<DWORD>(ViewerTiffCompression::Default):
    default:
        return ViewerTiffCompression::Default;
    }
}

DWORD ViewerTiffCompressionToDword(ViewerTiffCompression compression)
{
    return static_cast<DWORD>(compression);
}

COLORREF SanitizeColorValue(DWORD value, COLORREF defaultValue)
{
    if ((value & 0xff000000u) != 0)
        return defaultValue;
    return static_cast<COLORREF>(value);
}

int SanitizeSelectRatio(DWORD value)
{
    if (value == 0 || value > 999)
        return 1;
    return static_cast<int>(value);
}

int SanitizeMaxThumbImgSize(DWORD value)
{
    constexpr DWORD maxReasonableMegapixels = 100000;
    if (value > maxReasonableMegapixels)
        return 90;
    return static_cast<int>(value);
}

int SanitizeJpegQuality(DWORD value)
{
    if (value < 1 || value > 100)
        return 75;
    return static_cast<int>(value);
}

int SanitizeMetadataDetailsDimension(DWORD value, int defaultValue)
{
    if (value < 260 || value > 4000)
        return defaultValue;
    return static_cast<int>(value);
}

int SanitizeSaveFilterIndex(DWORD value)
{
    if (value < 1 || value > 5)
        return 1;
    return static_cast<int>(value);
}

int OldPictViewColorSaveFilterIndexToV2(DWORD value)
{
    switch (value)
    {
    case 3:
        return 5; // GIF
    case 4:
        return 2; // JPEG
    case 6:
        return 1; // PNG
    case 10:
        return 3; // TIFF
    case 13:
        return 4; // BMP
    default:
        return 1;
    }
}

int OldPictViewMonoSaveFilterIndexToV2(DWORD value)
{
    switch (value)
    {
    case 3:
        return 5; // GIF
    case 5:
        return 2; // JPEG
    case 7:
        return 1; // PNG
    case 11:
        return 3; // TIFF
    case 14:
        return 4; // BMP
    default:
        return 1;
    }
}

bool SaveKeyUsesOldPictViewFilterIndexes(CSalamanderRegistryAbstract* registry, HKEY saveKey)
{
    if (HasRegistryDword(registry, saveKey, ConfigSaveTiffCompression))
        return false;

    return HasRegistryDword(registry, saveKey, ConfigSaveFlags) ||
           HasRegistryDword(registry, saveKey, ConfigSaveTiffStripSize) ||
           HasRegistryDword(registry, saveKey, ConfigSaveFilterMono);
}

int LoadSaveFilterIndex(CSalamanderRegistryAbstract* registry, HKEY saveKey)
{
    const bool oldPictViewSaveKey = SaveKeyUsesOldPictViewFilterIndexes(registry, saveKey);

    DWORD value = 0;
    if (TryLoadRegistryDword(registry, saveKey, ConfigSaveFilterColor, value))
        return oldPictViewSaveKey ? OldPictViewColorSaveFilterIndexToV2(value) : SanitizeSaveFilterIndex(value);

    if (oldPictViewSaveKey && TryLoadRegistryDword(registry, saveKey, ConfigSaveFilterMono, value))
        return OldPictViewMonoSaveFilterIndexToV2(value);

    return 1;
}

bool LoadRegistryString(CSalamanderRegistryAbstract* registry, HKEY key, const wchar_t* name, std::wstring& value)
{
    value.clear();
    if (registry == nullptr || key == nullptr)
        return false;
    return SPLRegistryGetStringOwned(registry, key, name, value) != FALSE;
}

void SaveRegistryString(CSalamanderRegistryAbstract* registry, HKEY key, const wchar_t* name, const std::wstring& value)
{
    if (registry == nullptr || key == nullptr || name == nullptr)
        return;

    if (value.empty())
    {
        registry->DeleteValue(key, name);
        return;
    }
    SPLRegistrySetString(registry, key, name, value);
}

bool LoadRecentHistoryList(CSalamanderRegistryAbstract* registry,
                           HKEY regKey,
                           const wchar_t* keyName,
                           std::array<std::wstring, ViewerRecentHistoryEntryCount>& entries)
{
    if (registry == nullptr || regKey == nullptr || keyName == nullptr)
        return false;

    HKEY historyKey = nullptr;
    if (!registry->OpenKey(regKey, keyName, historyKey))
        return false;

    for (std::size_t i = 0; i < entries.size(); ++i)
    {
        const std::wstring valueName = HistoryValueName(i);
        LoadRegistryString(registry, historyKey, valueName.c_str(), entries[i]);
    }

    registry->CloseKey(historyKey);
    return true;
}

void SaveRecentHistoryList(CSalamanderRegistryAbstract* registry,
                           HKEY regKey,
                           const wchar_t* keyName,
                           const std::array<std::wstring, ViewerRecentHistoryEntryCount>& entries,
                           bool saveEntries)
{
    if (registry == nullptr || regKey == nullptr || keyName == nullptr)
        return;

    HKEY historyKey = nullptr;
    if (!registry->CreateKey(regKey, keyName, historyKey))
        return;

    registry->ClearKey(historyKey);
    if (saveEntries)
    {
        for (std::size_t i = 0; i < entries.size(); ++i)
        {
            const std::wstring valueName = HistoryValueName(i);
            SaveRegistryString(registry, historyKey, valueName.c_str(), entries[i]);
        }
    }

    registry->CloseKey(historyKey);
}

} // namespace

std::mutex OfferedCodecExtensionsMutex;
std::vector<std::wstring> OfferedCodecExtensions;

std::vector<std::wstring> GetOfferedCodecExtensions()
{
    std::lock_guard<std::mutex> lock(OfferedCodecExtensionsMutex);
    return OfferedCodecExtensions;
}

void SetOfferedCodecExtensions(const std::vector<std::wstring>& extensions)
{
    std::lock_guard<std::mutex> lock(OfferedCodecExtensionsMutex);
    OfferedCodecExtensions = SplitExtensions(JoinExtensions(extensions)); // normalized, sorted
}

DWORD LoadedConfigurationVersion()
{
    return LoadedVersion;
}

void LoadViewerPreferencesConfiguration(HKEY regKey, CSalamanderRegistryAbstract* registry)
{
    ViewerPreferences preferences;
    LoadedVersion = 0;
    std::vector<std::wstring> offeredCodecExtensions;
    if (regKey != nullptr && registry != nullptr)
    {
        LoadedVersion = LoadRegistryDword(registry, regKey, ConfigVersion, 0);
        std::wstring offered;
        if (LoadRegistryString(registry, regKey, ConfigOfferedCodecExtensions, offered))
            offeredCodecExtensions = SplitExtensions(offered);
        preferences.ToolbarVisible = LoadRegistryBool(registry, regKey, ConfigToolbarVisible, true);
        preferences.StatusbarVisible = LoadRegistryBool(registry, regKey, ConfigStatusbarVisible, true);
        preferences.PageUpDownScrolls = LoadRegistryBool(registry, regKey, ConfigPageUpDownScrolls, true);
        preferences.PipetteInHex = LoadRegistryBool(registry, regKey, ConfigPipetteInHex, false);
        preferences.ShowFullPathInTitle = LoadRegistryBool(registry, regKey, ConfigShowFullPathInTitle, true);
        preferences.AutoRotate = LoadRegistryBool(registry, regKey, ConfigAutoRotate, true);
        preferences.IgnoreThumbnails = LoadRegistryBool(registry, regKey, ConfigIgnoreThumbnails, false);
        preferences.CaptureCursor = LoadRegistryBool(registry, regKey, ConfigCaptureCursor, false);
        preferences.CaptureScope = ViewerCaptureScopeFromDword(LoadRegistryDword(registry,
                                                                                 regKey,
                                                                                 ConfigCaptureScope,
                                                                                 ViewerCaptureScopeToDword(ViewerCaptureScope::Desktop)));
        preferences.CaptureTrigger = ViewerCaptureTriggerFromDword(LoadRegistryDword(registry,
                                                                                     regKey,
                                                                                     ConfigCaptureTrigger,
                                                                                     ViewerCaptureTriggerToDword(ViewerCaptureTrigger::HotKey)));
        preferences.CaptureHotKey = SanitizeCaptureHotKey(LoadRegistryDword(registry,
                                                                            regKey,
                                                                            ConfigCaptureHotKey,
                                                                            VK_F10));
        preferences.CaptureTimerSeconds = SanitizeCaptureTimerSeconds(LoadRegistryDword(registry,
                                                                                       regKey,
                                                                                       ConfigCaptureTimer,
                                                                                       5));
        preferences.MaxThumbImgSize = SanitizeMaxThumbImgSize(LoadRegistryDword(registry, regKey, ConfigMaxThumbImgSize, 90));
        const DWORD dontShowAnymore = LoadRegistryDword(registry, regKey, ConfigDontShowAnymore, 0);
        preferences.ShowSaveSuccessMessage = (dontShowAnymore & DontShowSaveSuccess) == 0;
        preferences.ShowAlphaLossWarning = (dontShowAnymore & DontShowAlphaLoss) == 0;
        preferences.ConfirmThumbnailUpdate = (dontShowAnymore & DontShowUpdateThumbnails) == 0;
        HKEY saveKey = nullptr;
        if (registry->OpenKey(regKey, ConfigSaveKey, saveKey))
        {
            preferences.JpegQuality = SanitizeJpegQuality(LoadRegistryDword(registry, saveKey, ConfigSaveJpegQuality, 75));
            preferences.JpegSubsampling = ViewerJpegSubsamplingFromDword(LoadRegistryDword(registry,
                                                                                           saveKey,
                                                                                           ConfigSaveJpegSubsampling,
                                                                                           ViewerJpegSubsamplingToDword(ViewerJpegSubsampling::TwoToOneOne)));
            preferences.TiffCompression = ViewerTiffCompressionFromDword(LoadRegistryDword(registry,
                                                                                           saveKey,
                                                                                           ConfigSaveTiffCompression,
                                                                                           ViewerTiffCompressionToDword(ViewerTiffCompression::Default)));
            preferences.RememberSavePath = LoadRegistryBool(registry,
                                                            saveKey,
                                                            ConfigSaveRememberPath,
                                                            LoadRegistryBool(registry, regKey, ConfigSaveRememberPath, true));
            preferences.LastSaveFilterIndex = LoadSaveFilterIndex(registry, saveKey);
            LoadRegistryString(registry, saveKey, ConfigSaveInitDir, preferences.SaveInitialDirectory);
            registry->CloseKey(saveKey);
        }
        preferences.SelectRatioX = SanitizeSelectRatio(LoadRegistryDword(registry, regKey, ConfigSelectRatioX, 1));
        preferences.SelectRatioY = SanitizeSelectRatio(LoadRegistryDword(registry, regKey, ConfigSelectRatioY, 1));
        preferences.DefaultZoomMode = ViewerDefaultZoomModeFromOldDword(LoadRegistryDword(registry,
                                                                                          regKey,
                                                                                          ConfigDefaultZoomMode,
                                                                                          ViewerDefaultZoomModeToOldDword(ViewerDefaultZoomMode::FitWhole)));
        preferences.WindowSizeMode = ViewerWindowSizeModeFromDword(LoadRegistryDword(registry,
                                                                                     regKey,
                                                                                     ConfigWindowSizeMode,
                                                                                     ViewerWindowSizeModeToDword(ViewerWindowSizeMode::SameAsSally)));
        preferences.BackgroundColor = SanitizeColorValue(LoadRegistryDword(registry, regKey, ConfigBackgroundColor, RGB(128, 128, 128)), RGB(128, 128, 128));
        preferences.TransparentColor = SanitizeColorValue(LoadRegistryDword(registry, regKey, ConfigTransparentColor, RGB(255, 255, 255)), RGB(255, 255, 255));
        preferences.FullScreenBackgroundColor = SanitizeColorValue(LoadRegistryDword(registry, regKey, ConfigFullScreenBackgroundColor, RGB(0, 0, 0)), RGB(0, 0, 0));
        preferences.FullScreenTransparentColor = SanitizeColorValue(LoadRegistryDword(registry, regKey, ConfigFullScreenTransparentColor, RGB(255, 255, 255)), RGB(255, 255, 255));
    }
    SetOfferedCodecExtensions(offeredCodecExtensions);
    SetViewerPreferences(preferences);
}

void SaveViewerPreferencesConfiguration(HKEY regKey, CSalamanderRegistryAbstract* registry)
{
    if (regKey == nullptr || registry == nullptr)
        return;

    const ViewerPreferences preferences = GetViewerPreferences();
    SaveRegistryDword(registry, regKey, ConfigVersion, CurrentConfigVersion);
    SaveRegistryString(registry, regKey, ConfigOfferedCodecExtensions, JoinExtensions(GetOfferedCodecExtensions()));
    SaveRegistryBool(registry, regKey, ConfigToolbarVisible, preferences.ToolbarVisible);
    SaveRegistryBool(registry, regKey, ConfigStatusbarVisible, preferences.StatusbarVisible);
    SaveRegistryBool(registry, regKey, ConfigPageUpDownScrolls, preferences.PageUpDownScrolls);
    SaveRegistryBool(registry, regKey, ConfigPipetteInHex, preferences.PipetteInHex);
    SaveRegistryBool(registry, regKey, ConfigShowFullPathInTitle, preferences.ShowFullPathInTitle);
    SaveRegistryBool(registry, regKey, ConfigAutoRotate, preferences.AutoRotate);
    SaveRegistryBool(registry, regKey, ConfigIgnoreThumbnails, preferences.IgnoreThumbnails);
    SaveRegistryBool(registry, regKey, ConfigCaptureCursor, preferences.CaptureCursor);
    SaveRegistryDword(registry, regKey, ConfigCaptureScope, ViewerCaptureScopeToDword(preferences.CaptureScope));
    SaveRegistryDword(registry, regKey, ConfigCaptureTrigger, ViewerCaptureTriggerToDword(preferences.CaptureTrigger));
    SaveRegistryDword(registry, regKey, ConfigCaptureHotKey, SanitizeCaptureHotKey(preferences.CaptureHotKey));
    SaveRegistryDword(registry, regKey, ConfigCaptureTimer, static_cast<DWORD>(SanitizeCaptureTimerSeconds(static_cast<DWORD>(preferences.CaptureTimerSeconds))));
    SaveRegistryDword(registry, regKey, ConfigMaxThumbImgSize, static_cast<DWORD>(SanitizeMaxThumbImgSize(static_cast<DWORD>(preferences.MaxThumbImgSize))));
    DWORD dontShowAnymore = LoadRegistryDword(registry, regKey, ConfigDontShowAnymore, 0);
    if (preferences.ShowSaveSuccessMessage)
        dontShowAnymore &= ~DontShowSaveSuccess;
    else
        dontShowAnymore |= DontShowSaveSuccess;
    if (preferences.ShowAlphaLossWarning)
        dontShowAnymore &= ~DontShowAlphaLoss;
    else
        dontShowAnymore |= DontShowAlphaLoss;
    if (preferences.ConfirmThumbnailUpdate)
        dontShowAnymore &= ~DontShowUpdateThumbnails;
    else
        dontShowAnymore |= DontShowUpdateThumbnails;
    SaveRegistryDword(registry, regKey, ConfigDontShowAnymore, dontShowAnymore);
    HKEY saveKey = nullptr;
    if (registry->CreateKey(regKey, ConfigSaveKey, saveKey))
    {
        SaveRegistryDword(registry, saveKey, ConfigSaveJpegQuality, static_cast<DWORD>(SanitizeJpegQuality(static_cast<DWORD>(preferences.JpegQuality))));
        SaveRegistryDword(registry, saveKey, ConfigSaveJpegSubsampling, ViewerJpegSubsamplingToDword(preferences.JpegSubsampling));
        SaveRegistryDword(registry, saveKey, ConfigSaveTiffCompression, ViewerTiffCompressionToDword(preferences.TiffCompression));
        SaveRegistryBool(registry, saveKey, ConfigSaveRememberPath, preferences.RememberSavePath);
        SaveRegistryDword(registry, saveKey, ConfigSaveFilterColor, static_cast<DWORD>(SanitizeSaveFilterIndex(static_cast<DWORD>(preferences.LastSaveFilterIndex))));
        SaveRegistryString(registry, saveKey, ConfigSaveInitDir, preferences.SaveInitialDirectory);
        registry->CloseKey(saveKey);
    }
    SaveRegistryDword(registry, regKey, ConfigSelectRatioX, static_cast<DWORD>(SanitizeSelectRatio(static_cast<DWORD>(preferences.SelectRatioX))));
    SaveRegistryDword(registry, regKey, ConfigSelectRatioY, static_cast<DWORD>(SanitizeSelectRatio(static_cast<DWORD>(preferences.SelectRatioY))));
    SaveRegistryDword(registry, regKey, ConfigDefaultZoomMode, ViewerDefaultZoomModeToOldDword(preferences.DefaultZoomMode));
    SaveRegistryDword(registry, regKey, ConfigWindowSizeMode, ViewerWindowSizeModeToDword(preferences.WindowSizeMode));
    SaveRegistryDword(registry, regKey, ConfigBackgroundColor, static_cast<DWORD>(preferences.BackgroundColor));
    SaveRegistryDword(registry, regKey, ConfigTransparentColor, static_cast<DWORD>(preferences.TransparentColor));
    SaveRegistryDword(registry, regKey, ConfigFullScreenBackgroundColor, static_cast<DWORD>(preferences.FullScreenBackgroundColor));
    SaveRegistryDword(registry, regKey, ConfigFullScreenTransparentColor, static_cast<DWORD>(preferences.FullScreenTransparentColor));
}

void LoadCopyToConfiguration(HKEY regKey, CSalamanderRegistryAbstract* registry)
{
    ViewerCopyToHistory history;
    if (regKey != nullptr && registry != nullptr)
    {
        HKEY copyToKey = nullptr;
        if (registry->OpenKey(regKey, ConfigCopyToKey, copyToKey))
        {
            DWORD lastIndex = 0;
            if (registry->GetValue(copyToKey, ConfigCopyToLastIndex, REG_DWORD, &lastIndex, sizeof(lastIndex)))
                history.LastIndex = CopyToHistoryIndexFromDword(lastIndex);

            for (std::size_t i = 0; i < history.Targets.size(); ++i)
            {
                const std::wstring valueName = HistoryValueName(i);
                LoadRegistryString(registry, copyToKey, valueName.c_str(), history.Targets[i]);
            }

            registry->CloseKey(copyToKey);
        }
    }
    SetCopyToHistory(history);
}

void SaveCopyToConfiguration(HKEY regKey, CSalamanderRegistryAbstract* registry)
{
    if (regKey == nullptr || registry == nullptr)
        return;

    HKEY copyToKey = nullptr;
    if (!registry->CreateKey(regKey, ConfigCopyToKey, copyToKey))
        return;

    registry->ClearKey(copyToKey);
    const ViewerCopyToHistory history = GetCopyToHistory();
    const DWORD lastIndex = static_cast<DWORD>(ClampCopyToHistoryIndex(history.LastIndex));
    registry->SetValue(copyToKey, ConfigCopyToLastIndex, REG_DWORD, &lastIndex, sizeof(lastIndex));

    for (std::size_t i = 0; i < history.Targets.size(); ++i)
    {
        const std::wstring valueName = HistoryValueName(i);
        SaveRegistryString(registry, copyToKey, valueName.c_str(), history.Targets[i]);
    }

    registry->CloseKey(copyToKey);
}

void LoadRecentHistoryConfiguration(HKEY regKey, CSalamanderRegistryAbstract* registry)
{
    ViewerRecentHistory history;
    if (regKey != nullptr && registry != nullptr)
    {
        LoadRecentHistoryList(registry, regKey, ConfigRecentFilesKey, history.Files);
        LoadRecentHistoryList(registry, regKey, ConfigRecentDirectoriesKey, history.Directories);
    }
    SetRecentHistory(history);
}

void SaveRecentHistoryConfiguration(HKEY regKey, CSalamanderRegistryAbstract* registry, bool saveEntries)
{
    if (regKey == nullptr || registry == nullptr)
        return;

    const ViewerRecentHistory history = GetRecentHistory();
    SaveRecentHistoryList(registry, regKey, ConfigRecentFilesKey, history.Files, saveEntries);
    SaveRecentHistoryList(registry, regKey, ConfigRecentDirectoriesKey, history.Directories, saveEntries);
}

void LoadHistogramConfiguration(HKEY regKey, CSalamanderRegistryAbstract* registry)
{
    ViewerHistogramPreferences preferences;
    HistogramRegistryData data;
    if (regKey != nullptr && registry != nullptr &&
        registry->GetValue(regKey, ConfigHistogram, REG_BINARY, &data, sizeof(data)) &&
        data.Version == HistogramRegistryVersion)
    {
        preferences.WindowX = data.WindowX;
        preferences.WindowY = data.WindowY;
        preferences.ChartWidth = data.HistogramWidth;
        preferences.ChartHeight = data.HistogramHeight;
        preferences.Channel = HistogramChannelFromDword(static_cast<DWORD>(data.Channel));
        preferences.ShowOtherChannels = data.ShowOtherChannels != FALSE;
    }
    SetHistogramPreferences(preferences);
}

void SaveHistogramConfiguration(HKEY regKey, CSalamanderRegistryAbstract* registry)
{
    if (regKey == nullptr || registry == nullptr)
        return;

    const ViewerHistogramPreferences preferences = GetHistogramPreferences();
    HistogramRegistryData data;
    data.WindowX = preferences.WindowX;
    data.WindowY = preferences.WindowY;
    data.HistogramWidth = preferences.ChartWidth;
    data.HistogramHeight = preferences.ChartHeight;
    data.Channel = static_cast<int>(preferences.Channel);
    data.ShowOtherChannels = preferences.ShowOtherChannels ? TRUE : FALSE;
    registry->SetValue(regKey, ConfigHistogram, REG_BINARY, &data, sizeof(data));
}

void LoadMetadataDetailsConfiguration(HKEY regKey, CSalamanderRegistryAbstract* registry)
{
    ViewerMetadataDetailsPreferences preferences;
    if (regKey != nullptr && registry != nullptr)
    {
        preferences.WindowWidth = SanitizeMetadataDetailsDimension(LoadRegistryDword(registry,
                                                                                    regKey,
                                                                                    ConfigExifDialogWidth,
                                                                                    static_cast<DWORD>(preferences.WindowWidth)),
                                                                  preferences.WindowWidth);
        preferences.WindowHeight = SanitizeMetadataDetailsDimension(LoadRegistryDword(registry,
                                                                                     regKey,
                                                                                     ConfigExifDialogHeight,
                                                                                     static_cast<DWORD>(preferences.WindowHeight)),
                                                                   preferences.WindowHeight);
        preferences.GroupHighlights = LoadRegistryBool(registry, regKey, ConfigExifGroupHighlights, false);
        std::wstring highlights;
        if (LoadRegistryString(registry, regKey, ConfigExifHighlights, highlights))
        {
            // Comma-separated EXIF tag numbers.
            size_t start = 0;
            while (start < highlights.size())
            {
                size_t end = highlights.find(L',', start);
                if (end == std::wstring::npos)
                    end = highlights.size();
                const unsigned long tag = wcstoul(highlights.substr(start, end - start).c_str(), nullptr, 10);
                if (tag > 0 && tag <= 0xFFFF)
                    preferences.HighlightedTags.push_back(static_cast<uint16_t>(tag));
                start = end + 1;
            }
        }
    }
    SetMetadataDetailsPreferences(preferences);
}

void SaveMetadataDetailsConfiguration(HKEY regKey, CSalamanderRegistryAbstract* registry)
{
    if (regKey == nullptr || registry == nullptr)
        return;

    const ViewerMetadataDetailsPreferences preferences = GetMetadataDetailsPreferences();
    SaveRegistryDword(registry, regKey, ConfigExifDialogWidth, static_cast<DWORD>(SanitizeMetadataDetailsDimension(static_cast<DWORD>(preferences.WindowWidth), 520)));
    SaveRegistryDword(registry, regKey, ConfigExifDialogHeight, static_cast<DWORD>(SanitizeMetadataDetailsDimension(static_cast<DWORD>(preferences.WindowHeight), 440)));
    SaveRegistryBool(registry, regKey, ConfigExifGroupHighlights, preferences.GroupHighlights);
    std::wstring highlights;
    for (const uint16_t tag : preferences.HighlightedTags)
    {
        if (!highlights.empty())
            highlights += L',';
        highlights += std::to_wstring(tag);
    }
    SaveRegistryString(registry, regKey, ConfigExifHighlights, highlights);
}

} // namespace pictview
