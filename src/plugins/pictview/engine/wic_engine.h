// SPDX-FileCopyrightText: 2025-2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <atomic>
#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <wincodec.h>
#include <wrl/client.h>

namespace pictview
{

enum class ImageErrorCode
{
    Ok,
    InvalidArgument,
    ComInitializationFailed,
    WicFactoryFailed,
    OpenFailed,
    UnsupportedFormat,
    MetadataFailed,
    FrameOutOfRange,
    SizeLimitExceeded,
    Canceled,
    OutOfMemory,
    DecodeFailed,
    SaveFailed,
};

struct ImageError
{
    ImageErrorCode Code = ImageErrorCode::Ok;
    HRESULT Hr = S_OK;
    DWORD Win32Error = ERROR_SUCCESS;
    std::wstring Message;

    bool Succeeded() const { return Code == ImageErrorCode::Ok; }
    static ImageError Ok();
    static ImageError Failure(ImageErrorCode code, HRESULT hr, const wchar_t* message);
};

class CancellationToken
{
public:
    CancellationToken();
    bool IsCancellationRequested() const;

private:
    friend class CancellationSource;
    explicit CancellationToken(std::shared_ptr<std::atomic_bool> state);
    std::shared_ptr<std::atomic_bool> m_state;
};

class CancellationSource
{
public:
    CancellationSource();
    CancellationToken Token() const;
    void Cancel();

private:
    std::shared_ptr<std::atomic_bool> m_state;
};

struct DecodeLimits
{
    uint32_t MaxWidth = 100000;
    uint32_t MaxHeight = 100000;
    uint64_t MaxPixels = 200000000;
    uint64_t MaxBytes = 768ull * 1024ull * 1024ull;
};

struct DecodeOptions
{
    DecodeLimits Limits;
    bool ApplyOrientation = true;
    // Called on the decoding thread after each band of rows of a large image, with 1..100.
    std::function<void(uint32_t percent)> Progress;
    // Keep the palette index of each pixel of an indexed image (ImageSurface::PaletteIndices).
    bool KeepPaletteIndices = false;
};

// How many rows one band of a banded pixel copy holds: about 8 MB, at least one row.
uint32_t DecodeBandRows(uint32_t stride, uint32_t height);

enum class ImageSurfaceFormat
{
    Bgra32,
};

struct ImageSurface
{
    uint32_t Width = 0;
    uint32_t Height = 0;
    uint32_t Stride = 0;
    ImageSurfaceFormat Format = ImageSurfaceFormat::Bgra32;
    std::vector<uint8_t> Pixels;
    // Palette images decoded with KeepPaletteIndices: one index per pixel, row by row (Width
    // per row). Empty otherwise, and after any transform.
    std::vector<uint8_t> PaletteIndices;
};

// Unpacks 1, 2, 4 or 8 bits-per-pixel rows (most significant bits first) into one byte per
// pixel. Returns false for other depths or a buffer too small.
bool UnpackPaletteIndices(const uint8_t* packed, uint32_t stride, uint32_t width, uint32_t height, uint32_t bitsPerPixel,
                          std::vector<uint8_t>& indices);

enum class ImageSurfaceTransform
{
    Rotate90Clockwise,
    Rotate90CounterClockwise,
    Rotate180,
    FlipHorizontal,
    FlipVertical,
};

struct TransformSurfaceResult
{
    ImageSurface Surface;
    ImageError Error;

    bool Succeeded() const { return Error.Succeeded(); }
};

TransformSurfaceResult TransformSurface(const ImageSurface& surface, ImageSurfaceTransform transform);

struct CropSurfaceResult
{
    ImageSurface Surface;
    ImageError Error;

    bool Succeeded() const { return Error.Succeeded(); }
};

CropSurfaceResult CropSurface(const ImageSurface& surface,
                              uint32_t left,
                              uint32_t top,
                              uint32_t width,
                              uint32_t height);

struct ImageHistogram
{
    std::array<uint64_t, 256> Luminosity = {};
    std::array<uint64_t, 256> Red = {};
    std::array<uint64_t, 256> Green = {};
    std::array<uint64_t, 256> Blue = {};
    std::array<uint64_t, 256> Rgb = {};
    uint64_t PixelCount = 0;
    uint64_t MaxBucketCount = 0;
};

struct ImageHistogramResult
{
    ImageHistogram Histogram;
    ImageError Error;

    bool Succeeded() const { return Error.Succeeded(); }
};

ImageHistogramResult ComputeSurfaceHistogram(const ImageSurface& surface);

struct ImageExifEntry
{
    uint16_t Tag = 0;
    std::wstring Group; // "IFD0", "Exif", "GPS", "Interoperability"
    std::wstring Name;  // English tag name, or "Tag 0x9999" for tags the table does not know
    std::wstring Value;
    bool HasNumber = false; // a single integer value (enumerations such as Orientation, Flash)
    int64_t Number = 0;
};

struct ImageFrameInfo
{
    uint32_t Width = 0;
    uint32_t Height = 0;
    double DpiX = 0.0;
    double DpiY = 0.0;
    GUID PixelFormat = GUID_NULL;
    uint32_t BitsPerPixel = 0; // from WIC's description of PixelFormat; 0 when unknown
    uint32_t ChannelCount = 0;
    bool HasOrientation = false;
    uint16_t Orientation = 1;
    bool HasAnimationDelay = false;
    uint32_t AnimationDelayMilliseconds = 0;
    bool HasGifDisposal = false;
    uint8_t GifDisposal = 0;
    uint32_t OffsetLeft = 0; // GIF: where the frame sits on the logical screen
    uint32_t OffsetTop = 0;
    std::vector<ImageExifEntry> Exif; // this frame's EXIF (pages of a TIFF have their own)
    std::wstring Comment;             // JPEG COM, PNG tEXt/iTXt comment, GIF comment extension
};

// Comment bytes as text: UTF-8 when they are valid UTF-8, else ISO 8859-1 (what PNG tEXt and
// most JPEG/GIF writers use).
std::wstring DecodeCommentBytes(const char* bytes);

struct ImageMetadataEntry
{
    std::wstring Name;
    std::wstring Value;
};

// One EXIF/TIFF tag of the first frame, as the EXIF dialog lists it.

struct ImageMetadata
{
    GUID ContainerFormat = GUID_NULL;
    std::wstring ContainerName;
    std::vector<ImageFrameInfo> Frames;
    std::vector<ImageMetadataEntry> Details;
    std::vector<ImageExifEntry> Exif;
    uint32_t CanvasWidth = 0; // GIF logical screen; 0 when the container has none
    uint32_t CanvasHeight = 0;

    uint32_t FrameCount() const { return static_cast<uint32_t>(Frames.size()); }
};

class ImageDocument;

struct OpenDocumentResult
{
    std::unique_ptr<ImageDocument> Document;
    ImageError Error;

    bool Succeeded() const { return Error.Succeeded() && Document != nullptr; }
};

struct DecodeFrameResult
{
    ImageSurface Surface;
    ImageError Error;
    enum class SourceKind
    {
        FullFrame,
        EmbeddedThumbnail,
        DecoderPreview,
    };
    SourceKind Source = SourceKind::FullFrame;

    bool Succeeded() const { return Error.Succeeded(); }
};

struct ThumbnailDecodeOptions
{
    bool UseEmbeddedThumbnailSources = true;
};

enum class ImageSaveFormat
{
    Png,
    Jpeg,
    Bmp,
    Tiff,
    Gif,
};

enum class ImageJpegSubsampling
{
    Default,
    OneToOneOne,
    TwoToOneOne,
};

enum class ImageTiffCompression
{
    Default,
    None,
    Lzw,
    Zip,
};

struct SaveOptions
{
    ImageSaveFormat Format = ImageSaveFormat::Png;
    float JpegQuality = 0.90f;
    ImageJpegSubsampling JpegSubsampling = ImageJpegSubsampling::Default;
    ImageTiffCompression TiffCompression = ImageTiffCompression::Default;
    double DpiX = 96.0;
    double DpiY = 96.0;
};

struct SaveImageResult
{
    ImageError Error;

    bool Succeeded() const { return Error.Succeeded(); }
};

class ImageDocument
{
public:
    ImageDocument(ImageDocument&&) noexcept;
    ImageDocument& operator=(ImageDocument&&) noexcept;
    ~ImageDocument();

    const ImageMetadata& Metadata() const { return m_metadata; }
    DecodeFrameResult DecodeFrame(uint32_t frameIndex,
                                  const DecodeOptions& options = DecodeOptions(),
                                  const CancellationToken& cancellation = CancellationToken()) const;
    DecodeFrameResult DecodeFrameThumbnail(uint32_t frameIndex,
                                           uint32_t maxWidth,
                                           uint32_t maxHeight,
                                           const DecodeOptions& options = DecodeOptions(),
                                           const CancellationToken& cancellation = CancellationToken()) const;
    DecodeFrameResult DecodeFrameThumbnail(uint32_t frameIndex,
                                           uint32_t maxWidth,
                                           uint32_t maxHeight,
                                           const ThumbnailDecodeOptions& thumbnailOptions,
                                           const DecodeOptions& options = DecodeOptions(),
                                           const CancellationToken& cancellation = CancellationToken()) const;

private:
    friend class ImageEngine;

    ImageDocument(Microsoft::WRL::ComPtr<IWICImagingFactory> factory,
                  Microsoft::WRL::ComPtr<IWICBitmapDecoder> decoder,
                  ImageMetadata metadata);

    Microsoft::WRL::ComPtr<IWICImagingFactory> m_factory;
    Microsoft::WRL::ComPtr<IWICBitmapDecoder> m_decoder;
    ImageMetadata m_metadata;
};

// An animation as it is shown: frames drawn onto a canvas one after another, each frame's
// disposal applied before the next one is drawn (GIF: 0 and 1 keep it, 2 clears its area to
// transparent, 3 restores what was under it).
struct AnimationCanvas
{
    ImageSurface Surface;
    RECT LastFrameRect = {};
    uint8_t LastDisposal = 0;
    ImageSurface BeforeLastFrame; // kept only when the last frame's disposal is 3
    bool HasFrame = false;
};

// Starts an empty (transparent) canvas of width x height.
bool ResetAnimationCanvas(AnimationCanvas& canvas, uint32_t width, uint32_t height);
// Draws 'frame' at (left, top), after the previous frame's disposal.
void ComposeAnimationFrame(AnimationCanvas& canvas, const ImageSurface& frame, int left, int top, uint8_t disposal);
// Decodes frame 'index' of 'document' and composes it; frame 0 starts a new canvas of the
// logical screen size.
ImageError ComposeDocumentFrame(const ImageDocument& document, uint32_t index, AnimationCanvas& canvas);

class ImageEngine
{
public:
    ImageEngine();
    ~ImageEngine();

    bool IsAvailable() const;
    const ImageError& InitializationError() const { return m_initializationError; }

    OpenDocumentResult OpenPath(const wchar_t* path,
                                const CancellationToken& cancellation = CancellationToken()) const;
    OpenDocumentResult OpenStream(IStream* stream,
                                  const CancellationToken& cancellation = CancellationToken()) const;
    SaveImageResult SaveSurfaceToPath(const ImageSurface& surface,
                                      const wchar_t* path,
                                      const SaveOptions& options = SaveOptions(),
                                      const CancellationToken& cancellation = CancellationToken()) const;
    // The encoded file in memory (Update Thumbnail builds its small JPEG this way).
    SaveImageResult SaveSurfaceToBytes(const ImageSurface& surface, std::vector<uint8_t>& bytes,
                                       const SaveOptions& options = SaveOptions()) const;

    // The file extension list of every installed WIC decoder (".heic,.heif", ...), built-in and
    // added ones (HEIF, AVIF, WebP, camera RAW extensions from the Store, ...).
    std::vector<std::wstring> DecoderFileExtensions() const;

private:
    class ComApartment;

    OpenDocumentResult OpenDecoder(Microsoft::WRL::ComPtr<IWICBitmapDecoder> decoder,
                                   const CancellationToken& cancellation) const;
    SaveImageResult EncodeSurface(const ImageSurface& surface, IStream* stream, const SaveOptions& options,
                                  const CancellationToken& cancellation, const GUID& containerFormat,
                                  const GUID& targetPixelFormat) const;

    std::unique_ptr<ComApartment> m_comApartment;
    Microsoft::WRL::ComPtr<IWICImagingFactory> m_factory;
    ImageError m_initializationError;
};

} // namespace pictview
