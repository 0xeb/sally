// SPDX-FileCopyrightText: 2025-2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <windows.h>

class CSalamanderGeneralAbstract;
class CSalamanderGUIAbstract;

#include "engine/wic_engine.h"

namespace pictview
{

constexpr std::size_t ViewerCopyToHistoryEntryCount = 5;
constexpr std::size_t ViewerRecentHistoryEntryCount = 10;

struct ViewerCopyToHistory
{
    std::array<std::wstring, ViewerCopyToHistoryEntryCount> Targets;
    int LastIndex = 0;
};

struct ViewerRecentHistory
{
    std::array<std::wstring, ViewerRecentHistoryEntryCount> Files;
    std::array<std::wstring, ViewerRecentHistoryEntryCount> Directories;
};

enum class ViewerDefaultZoomMode
{
    FitWhole = 0,
    FitWidth = 1,
    ActualSize = 2,
    FullScreen = 3,
};

enum class ViewerWindowSizeMode
{
    SameAsSally = 0,
    LargerIfNeeded = 1,
    AsNeeded = 2,
};

enum class ViewerCaptureScope
{
    Desktop = 0,
    Application = 1,
    Window = 2,
    ClientArea = 3,
    VirtualScreen = 4,
};

enum class ViewerCaptureTrigger
{
    HotKey = 0,
    Timer = 1,
};

enum class ViewerJpegSubsampling
{
    OneToOneOne = 0,
    TwoToOneOne = 1,
};

enum class ViewerTiffCompression
{
    Default = 0,
    None = 1,
    Lzw = 2,
    Zip = 3,
};

// The old PictView's channel numbers (stored in its registry value).
enum class ViewerHistogramChannel
{
    Luminosity = 0,
    Red = 1,
    Green = 2,
    Blue = 3,
    Rgb = 4,
};

struct ViewerHistogramPreferences
{
    int WindowX = -1;
    int WindowY = -1;
    int ChartWidth = 256;  // the histogram area's client size, as the old PictView stored it
    int ChartHeight = 119; // 100 graph + 3 separator + 16 tone band
    ViewerHistogramChannel Channel = ViewerHistogramChannel::Rgb;
    bool ShowOtherChannels = true;
};

// Bar heights 0..1 per channel (indexed by ViewerHistogramChannel), scaled as the old
// PictView did: against four times the average bucket of all channels (at least 10 pixels),
// so a few huge spikes do not flatten the rest; taller buckets are clipped to 1.
using HistogramLevels = std::array<std::array<double, 256>, 5>;
HistogramLevels ComputeHistogramLevels(const struct ImageHistogram& histogram);

struct ViewerMetadataDetailsPreferences
{
    int WindowWidth = 520;
    int WindowHeight = 440;
    bool GroupHighlights = false;
    std::vector<uint16_t> HighlightedTags; // EXIF tag numbers, as the old PictView stored them
};

enum class ViewerHostEvent
{
    ColorsChanged,
    SettingsChanged,
    ConfigurationChanged,
};

struct ViewerPreferences
{
    bool ToolbarVisible = true;
    bool StatusbarVisible = true;
    bool PageUpDownScrolls = true;
    bool PipetteInHex = false;
    bool ShowFullPathInTitle = true;
    bool AutoRotate = true;
    bool IgnoreThumbnails = false;
    bool RememberSavePath = true;
    bool ShowSaveSuccessMessage = true;
    bool ConfirmThumbnailUpdate = true; // Update Thumbnail asks first (legacy DontShowAnymore bit 1)
    bool ShowAlphaLossWarning = true;
    bool CaptureCursor = false;
    ViewerCaptureScope CaptureScope = ViewerCaptureScope::Desktop;
    ViewerCaptureTrigger CaptureTrigger = ViewerCaptureTrigger::HotKey;
    WORD CaptureHotKey = VK_F10;
    int CaptureTimerSeconds = 5;
    int MaxThumbImgSize = 90;
    int JpegQuality = 75;
    ViewerJpegSubsampling JpegSubsampling = ViewerJpegSubsampling::TwoToOneOne;
    ViewerTiffCompression TiffCompression = ViewerTiffCompression::Default;
    int LastSaveFilterIndex = 1;
    std::wstring SaveInitialDirectory;
    int SelectRatioX = 1;
    int SelectRatioY = 1;
    ViewerDefaultZoomMode DefaultZoomMode = ViewerDefaultZoomMode::FitWhole;
    ViewerWindowSizeMode WindowSizeMode = ViewerWindowSizeMode::SameAsSally;
    COLORREF BackgroundColor = RGB(128, 128, 128);
    COLORREF FullScreenBackgroundColor = RGB(0, 0, 0);
    COLORREF TransparentColor = RGB(255, 255, 255);
    COLORREF FullScreenTransparentColor = RGB(255, 255, 255);
};

struct ViewerCaptureOptions
{
    ViewerCaptureScope Scope = ViewerCaptureScope::Desktop;
    ViewerCaptureTrigger Trigger = ViewerCaptureTrigger::HotKey;
    WORD HotKey = VK_F10;
    int TimerSeconds = 5;
    bool IncludeCursor = false;
};

struct ViewerSourceFile
{
    std::wstring Path;
    int SourceIndex = -1;
};

enum class ViewerSourceNavigationMode
{
    Previous,
    Next,
    PreviousSelected,
    NextSelected,
    First,
    Last,
};

enum class ViewerWallpaperMode
{
    Center,
    Tile,
    Stretch,
    Restore,
    None,
};

struct ViewerSourceNavigation
{
    void* Context = nullptr;
    int SourceUID = -1;
    int CurrentIndex = -1;
    std::wstring CurrentPath;
    bool (*FindFile)(void* context,
                     int sourceUID,
                     int currentIndex,
                     const wchar_t* currentPath,
                     ViewerSourceNavigationMode mode,
                     ViewerSourceFile& file,
                     bool& noMoreFiles,
                     bool& sourceBusy) = nullptr;
    bool (*ToggleSelection)(void* context,
                            int sourceUID,
                            int currentIndex,
                            const wchar_t* currentPath,
                            bool& selected,
                            bool& sourceBusy) = nullptr;
    bool (*FocusCurrentFile)(void* context,
                             const wchar_t* currentPath,
                             bool& sourceBusy) = nullptr;
};

struct ViewerHostActions
{
    void* Context = nullptr;
    bool SelectWholeNameOnRename = true;
    bool (*DeleteFile)(void* context,
                       HWND parent,
                       const wchar_t* path,
                       bool recycle,
                       bool& deleted,
                       bool& canceled) = nullptr;
    bool (*RenameFile)(void* context,
                       HWND parent,
                       const wchar_t* path,
                       const wchar_t* newName,
                       std::wstring& newPath,
                       bool& renamed,
                       DWORD& error) = nullptr;
    bool (*ChooseRenameName)(void* context,
                             HWND parent,
                             const wchar_t* oldName,
                             std::wstring& newName,
                             bool& canceled) = nullptr;
    bool (*CopyFile)(void* context,
                     HWND parent,
                     const wchar_t* sourcePath,
                     const wchar_t* targetPath,
                     bool& copied,
                     bool& canceled) = nullptr;
    bool (*ChooseCopyTargetPath)(void* context,
                                 HWND parent,
                                 const wchar_t* sourcePath,
                                 std::wstring& targetPath,
                                 bool& canceled) = nullptr;
    bool (*ChooseFrameIndex)(void* context,
                             HWND parent,
                             uint32_t frameCount,
                             uint32_t currentFrame,
                             uint32_t& frameIndex,
                             bool& canceled) = nullptr;
    bool (*ChooseZoomPercent)(void* context,
                              HWND parent,
                              int currentPercent,
                              int& zoomPercent,
                              bool& canceled) = nullptr;
    bool (*ChooseOpenPath)(void* context,
                           HWND parent,
                           const wchar_t* currentPath,
                           std::wstring& inputPath,
                           bool& canceled) = nullptr;
    bool (*ChooseSavePath)(void* context,
                           HWND parent,
                           const wchar_t* suggestedName,
                           std::wstring& outputPath,
                           bool& canceled) = nullptr;
    bool (*ConfirmSaveOverwrite)(void* context,
                                 HWND parent,
                                 const wchar_t* outputPath,
                                 bool& overwrite) = nullptr;
    bool (*ConfirmAlphaLoss)(void* context,
                             HWND parent,
                             const wchar_t* outputPath,
                             bool& proceed) = nullptr;
    void (*NotifySaveSuccess)(void* context,
                              HWND parent,
                              const wchar_t* outputPath) = nullptr;
    bool (*ChooseWallpaperPath)(void* context,
                                 HWND parent,
                                 std::wstring& outputPath) = nullptr;
    bool (*ApplyWallpaper)(void* context,
                           HWND parent,
                           ViewerWallpaperMode mode,
                           const wchar_t* wallpaperPath,
                           bool& applied) = nullptr;
    bool (*PrintSurface)(void* context,
                         HWND parent,
                         const wchar_t* documentName,
                         uint32_t width,
                         uint32_t height,
                         uint32_t stride,
                         const uint8_t* bgraPixels,
                         std::size_t pixelBytes,
                         bool& printed) = nullptr;
};

bool InitializeViewer(HINSTANCE instance, CSalamanderGeneralAbstract* general, CSalamanderGUIAbstract* gui);

// Text from the plugin's string table, in the language Sally selected (every language is
// compiled into the module and the resource loader picks it). The pointer stays valid for the
// life of the module.
const wchar_t* ViewerText(UINT id);
// ViewerText(id) used as a printf format.
std::wstring FormatViewerText(UINT id, ...);
// A translated summary of an engine error, then its diagnostic and HRESULT.
std::wstring ImageErrorMessage(const ImageError& error);
// The pipette readout: IDS_RGBXY, or IDS_RGBXY_HEX for hexadecimal channels.
// The pipette readout; an index >= 0 (palette images) is appended as ", Index: n".
std::wstring FormatPipetteSample(int x, int y, int red, int green, int blue, bool hex, int index = -1);
// Sally's plural syntax ("{!}%d color{|1|s}") for 'value', through the host when there is one.
std::wstring ExpandPluralText(const wchar_t* format, uint64_t value);
// The old PictView's zoom ladder: powers of the cube root of 2 (100, 126, 159, 200 %, ...),
// up to 1600 %. The next level above or below 'scale' (1.0 = 100 %).
double NextZoomLevel(double scale, bool zoomIn);
// Where a selection [left, top, right, bottom) of a width x height image lands after a transform.
// How much taller than wide one image pixel is drawn: DpiX / DpiY, inverted when the image was
// turned a quarter. 1 for square pixels, unknown resolution or a far-fetched ratio.
double PixelAspectRatio(double dpiX, double dpiY, bool quarterTurned);

// One scrollbar over the pan offset (the image's offset from the centred position).
struct ViewerScrollAxis
{
    bool Visible = false; // the image is larger than the view along this axis
    int Maximum = 0;      // nMax: the drawn size - 1
    int Page = 0;         // nPage: the view size
    int Position = 0;     // 0 shows the image's left (top) edge
};
ViewerScrollAxis ScrollAxisFromPan(long long drawSize, int viewSize, int pan);
int PanFromScrollPosition(long long drawSize, int viewSize, int position);
// The position after a WM_HSCROLL/WM_VSCROLL request (SB_LINEUP, SB_PAGEDOWN, SB_THUMBTRACK, ...).
int ScrollPositionForRequest(int request, int position, int trackPosition, long long drawSize, int viewSize);
RECT TransformSelectionRect(const RECT& selection, uint32_t width, uint32_t height, ImageSurfaceTransform transform);
// Masks of formats added by installed codecs, offered in the Open dialog next to the built-in ones.
void SetAdditionalOpenMasks(const std::wstring& masks);
std::wstring GetAdditionalOpenMasks();
// "16777216 colors", "256 colors", "CMYK": the old PictView's colour description.
std::wstring DescribeImageColors(uint32_t bitsPerPixel, const GUID& pixelFormat);
// The window title, as the old PictView wrote it (IDS_TITLE / IDS_TITLE_MULTI):
// "name (W x H x colors [n of m] - zoom%) - PictView".
std::wstring FormatViewerTitle(const std::wstring& name, uint32_t width, uint32_t height, const std::wstring& colors,
                               uint32_t frame, uint32_t frameCount, int zoomPercent);

// Message boxes go through the host when one is attached (Sally's SalMessageBox, which follows
// its theme and parent rules); the caption is the plugin's name.
using ViewerMessageBoxFunction = int (*)(HWND parent, const wchar_t* text, const wchar_t* caption, UINT type);
void SetViewerMessageBox(ViewerMessageBoxFunction messageBox);
int ViewerMessageBox(HWND parent, const wchar_t* text, UINT type);
void ReleaseViewer();
bool CloseAllViewerWindows(bool force);
bool AnyViewerWindowOpen();
// The old PictView's recent lists drop an entry that no longer opens.
void RemoveRecentFile(const std::wstring& path);
void RemoveRecentDirectory(const std::wstring& path);
ViewerCopyToHistory GetCopyToHistory();
void SetCopyToHistory(const ViewerCopyToHistory& history);
ViewerRecentHistory GetRecentHistory();
void SetRecentHistory(const ViewerRecentHistory& history);
void ClearRecentHistory();
ViewerPreferences GetViewerPreferences();
void SetViewerPreferences(const ViewerPreferences& preferences);
ViewerHistogramPreferences GetHistogramPreferences();
void SetHistogramPreferences(const ViewerHistogramPreferences& preferences);
ViewerMetadataDetailsPreferences GetMetadataDetailsPreferences();
void SetMetadataDetailsPreferences(const ViewerMetadataDetailsPreferences& preferences);
void NotifyViewerHostEvent(ViewerHostEvent event);
// PictView Configuration, from the plugin or from a viewer: one dialog at a time (a second one
// shows IDS_CFG_CONFLICT). On OK the preferences are stored, every open viewer applies them,
// and the thumbnail-settings handler runs when thumbnail settings changed.
bool EditViewerPreferences(HWND parent, HINSTANCE instance);
void SetThumbnailSettingsChangedHandler(void (*handler)());
void ShowAboutDialog(HWND parent);

BOOL OpenViewerWindow(const std::wstring& path,
                      int left,
                      int top,
                      int width,
                      int height,
                      UINT showCmd,
                      BOOL alwaysOnTop,
                      BOOL returnLock,
                      HANDLE* lock,
                      BOOL* lockOwner,
                      const ViewerHostActions& hostActions,
                      const ViewerSourceNavigation& sourceNavigation);

BOOL OpenClipboardViewerWindow(int left,
                               int top,
                               int width,
                               int height,
                               UINT showCmd,
                               BOOL alwaysOnTop);

BOOL OpenScreenCaptureViewerWindow(int left,
                                   int top,
                                   int width,
                                   int height,
                                   UINT showCmd,
                                   BOOL alwaysOnTop);

} // namespace pictview
