// SPDX-FileCopyrightText: 2025-2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

// PictView's dialogs: Sally (WinLib) dialogs on the templates in lang/lang.rc, so they look,
// translate and follow dark mode like every other Sally dialog.

#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstdint>
#include <string>
#include <vector>

#include "viewer.h"
#include "engine/wic_engine.h"
#include "print_layout.h"

class CSalamanderGeneralAbstract;
class CSalamanderGUIAbstract;

namespace pictview
{

void InitializeDialogs(HINSTANCE instance, CSalamanderGeneralAbstract* general, CSalamanderGUIAbstract* gui);
CSalamanderGeneralAbstract* DialogsGeneral();
CSalamanderGUIAbstract* DialogsGui();

// Zoom To (DLG_ZOOM): the current zoom in percent in, the chosen one out.
bool PromptForZoomPercent(HWND owner, int currentPercent, int& selectedPercent);

// Go To Page (DLG_PAGE): zero-based pages.
bool PromptForFrameNumber(HWND owner, uint32_t frameCount, uint32_t currentFrame, uint32_t& selectedFrame);

// Rename (IDD_RENAMEDIALOG): 'initialName' is the file name; the result may come from a mask
// such as "*.jpg" applied to it.
bool PromptForRenameFileName(HWND owner, const std::wstring& initialName, bool selectWholeName, std::wstring& selectedName);

// Screen Capture (DLG_CAPTURE).
bool PromptForCaptureOptions(HWND owner, ViewerCaptureOptions& options);

// Copy File To (IDD_COPYTO): five remembered target directories; returns the full target path.
bool PromptForCopyToTargetPath(HWND owner, const std::wstring& sourcePath, std::wstring& targetPath);

// About (DLG_ABOUT).
void ShowAboutPictViewDialog(HWND parent);

// What Image Properties (DLG_IMGPROP) shows about the current page.
struct ImagePropertiesInfo
{
    uint32_t Page = 0; // zero-based
    uint32_t PageCount = 1;
    bool Animation = false; // pages are animation frames
    uint32_t Width = 0;
    uint32_t Height = 0;
    GUID PixelFormat = GUID_NULL;
    uint32_t BitsPerPixel = 0;
    uint32_t ChannelCount = 0;
    uint64_t MemoryBytes = 0;
    uint64_t FileBytes = 0; // 0 when the image has no file (clipboard, capture)
    double DpiX = 0.0;
    double DpiY = 0.0;
    std::wstring Format;
    std::wstring Compression;
    std::wstring Comment;
};
void ShowImagePropertiesDialog(HWND owner, const ImagePropertiesInfo& info);

// EXIF (DLG_IMGEXIF): every tag of the image, with the user's highlighted tags.
void ShowExifDialog(HWND owner, const std::vector<ImageExifEntry>& entries);

enum class SaveAsRotation
{
    None,
    Clockwise90,
    Rotate180,
    Clockwise270,
};

enum class SaveAsFlip
{
    None,
    Vertical,
    Horizontal,
};

// One entry of the Save As "Compression" list. 'TextId' is a translated name, otherwise 'Text'
// (the codec's own name).
struct SaveAsCompressionChoice
{
    UINT TextId = 0;
    const wchar_t* Text = nullptr;
    ViewerTiffCompression Value = ViewerTiffCompression::Default;
};

// What WIC can write for a format: TIFF offers its compressions, the others their only one.
std::vector<SaveAsCompressionChoice> SaveAsCompressionChoices(ImageSaveFormat format);

// Save As: the system Save dialog with PictView's option panel (IDD_SAVEEX) below it, limited
// to what WIC writes: compression, JPEG quality and subsampling, rotation and flip.
struct SaveAsRequest
{
    std::wstring Filter;                     // "name|patterns|..." rows
    std::vector<ImageSaveFormat> FilterFormats; // the format of each filter row
    DWORD FilterIndex = 1;                   // one-based, in and out
    std::wstring InitialDirectory;
    std::wstring FileName; // suggested name in, chosen full path out
    ViewerTiffCompression TiffCompression = ViewerTiffCompression::Default;
    int JpegQuality = 75;
    ViewerJpegSubsampling JpegSubsampling = ViewerJpegSubsampling::TwoToOneOne;
    SaveAsRotation Rotation = SaveAsRotation::None;
    SaveAsFlip Flip = SaveAsFlip::None;
};
bool PromptForSaveAs(HWND owner, SaveAsRequest& request);

// Print (IDD_PRINT): the printer (Setup... changes it for the session), the image's position and
// size on the page with a live preview. On OK 'Printer' is a DC for the chosen printer, which
// the caller deletes, and 'Settings' holds the choices.
struct PrintRequest
{
    const ImageSurface* Image = nullptr;
    const ImageSurface* SelectionImage = nullptr; // the selected part, when there is a selection
    double ImageDpiX = 0;
    double ImageDpiY = 0;
    PrintSettings Settings;
    HDC Printer = nullptr;
};
bool PromptForPrint(HWND owner, PrintRequest& request);

} // namespace pictview
