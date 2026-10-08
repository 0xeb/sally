// SPDX-FileCopyrightText: 2025-2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#include "wic_engine.h"

#include <algorithm>
#include <cwchar>
#include <limits>
#include <new>
#include <propidl.h>
#include <sstream>
#include <utility>

namespace pictview
{
namespace
{

ImageError MakeError(ImageErrorCode code, HRESULT hr, const wchar_t* message)
{
    return ImageError::Failure(code, hr, message);
}

ImageError MakeWicError(HRESULT hr, const wchar_t* message)
{
    if (hr == WINCODEC_ERR_COMPONENTNOTFOUND || hr == WINCODEC_ERR_UNKNOWNIMAGEFORMAT)
        return MakeError(ImageErrorCode::UnsupportedFormat, hr, message);
    return MakeError(ImageErrorCode::OpenFailed, hr, message);
}

bool IsCanceled(const CancellationToken& cancellation)
{
    return cancellation.IsCancellationRequested();
}

bool GetFriendlyDecoderName(IWICBitmapDecoder* decoder, std::wstring& name)
{
    Microsoft::WRL::ComPtr<IWICBitmapDecoderInfo> info;
    if (decoder == nullptr || FAILED(decoder->GetDecoderInfo(&info)) || info == nullptr)
        return false;

    UINT chars = 0;
    HRESULT hr = info->GetFriendlyName(0, nullptr, &chars);
    if (hr != WINCODEC_ERR_INSUFFICIENTBUFFER || chars == 0)
        return false;

    std::vector<wchar_t> buffer(chars);
    if (FAILED(info->GetFriendlyName(chars, buffer.data(), &chars)))
        return false;

    name.assign(buffer.data());
    return true;
}

bool OrientationSwapsDimensions(uint16_t orientation)
{
    return orientation >= 5 && orientation <= 8;
}

bool OrientationNeedsTransform(uint16_t orientation)
{
    return orientation >= 2 && orientation <= 8;
}

void SourcePointForOrientation(uint16_t orientation,
                               uint32_t dstX,
                               uint32_t dstY,
                               uint32_t srcWidth,
                               uint32_t srcHeight,
                               uint32_t& srcX,
                               uint32_t& srcY)
{
    switch (orientation)
    {
    case 2:
        srcX = srcWidth - 1 - dstX;
        srcY = dstY;
        break;
    case 3:
        srcX = srcWidth - 1 - dstX;
        srcY = srcHeight - 1 - dstY;
        break;
    case 4:
        srcX = dstX;
        srcY = srcHeight - 1 - dstY;
        break;
    case 5:
        srcX = dstY;
        srcY = dstX;
        break;
    case 6:
        srcX = dstY;
        srcY = srcHeight - 1 - dstX;
        break;
    case 7:
        srcX = srcWidth - 1 - dstY;
        srcY = srcHeight - 1 - dstX;
        break;
    case 8:
        srcX = srcWidth - 1 - dstY;
        srcY = dstX;
        break;
    default:
        srcX = dstX;
        srcY = dstY;
        break;
    }
}

ImageError ApplyOrientation(ImageSurface& surface, const ImageFrameInfo& frameInfo)
{
    if (!frameInfo.HasOrientation || !OrientationNeedsTransform(frameInfo.Orientation))
        return ImageError::Ok();

    const uint32_t srcWidth = surface.Width;
    const uint32_t srcHeight = surface.Height;
    const uint32_t dstWidth = OrientationSwapsDimensions(frameInfo.Orientation) ? srcHeight : srcWidth;
    const uint32_t dstHeight = OrientationSwapsDimensions(frameInfo.Orientation) ? srcWidth : srcHeight;
    const uint32_t dstStride = dstWidth * 4u;
    const uint64_t dstBytes = static_cast<uint64_t>(dstStride) * dstHeight;

    std::vector<uint8_t> transformed;
    try
    {
        transformed.resize(static_cast<size_t>(dstBytes));
    }
    catch (const std::bad_alloc&)
    {
        return MakeError(ImageErrorCode::OutOfMemory, E_OUTOFMEMORY, L"Unable to allocate oriented image buffer.");
    }

    for (uint32_t y = 0; y < dstHeight; ++y)
    {
        for (uint32_t x = 0; x < dstWidth; ++x)
        {
            uint32_t srcX = 0;
            uint32_t srcY = 0;
            SourcePointForOrientation(frameInfo.Orientation, x, y, srcWidth, srcHeight, srcX, srcY);

            const uint8_t* src = surface.Pixels.data() + static_cast<size_t>(srcY) * surface.Stride + static_cast<size_t>(srcX) * 4u;
            uint8_t* dst = transformed.data() + static_cast<size_t>(y) * dstStride + static_cast<size_t>(x) * 4u;
            dst[0] = src[0];
            dst[1] = src[1];
            dst[2] = src[2];
            dst[3] = src[3];
        }
    }

    surface.Width = dstWidth;
    surface.Height = dstHeight;
    surface.Stride = dstStride;
    surface.Pixels = std::move(transformed);
    return ImageError::Ok();
}

bool TryGetUnsignedShortMetadata(IWICMetadataQueryReader* reader, const wchar_t* query, uint16_t& value)
{
    if (reader == nullptr)
        return false;

    PROPVARIANT prop;
    PropVariantInit(&prop);
    HRESULT hr = reader->GetMetadataByName(query, &prop);
    if (FAILED(hr))
    {
        PropVariantClear(&prop);
        return false;
    }

    bool ok = false;
    if (prop.vt == VT_UI1)
    {
        value = prop.bVal;
        ok = true;
    }
    else if (prop.vt == VT_UI2)
    {
        value = prop.uiVal;
        ok = true;
    }
    else if (prop.vt == VT_UI4 && prop.ulVal <= (std::numeric_limits<uint16_t>::max)())
    {
        value = static_cast<uint16_t>(prop.ulVal);
        ok = true;
    }

    PropVariantClear(&prop);
    return ok;
}

void TrimMetadataValue(std::wstring& value)
{
    const std::size_t terminator = value.find(L'\0');
    if (terminator != std::wstring::npos)
        value.resize(terminator);

    const std::size_t first = value.find_first_not_of(L" \t\r\n");
    if (first == std::wstring::npos)
    {
        value.clear();
        return;
    }

    const std::size_t last = value.find_last_not_of(L" \t\r\n");
    value = value.substr(first, last - first + 1);
}

std::wstring WidenAsciiMetadata(LPCSTR value)
{
    std::wstring widened;
    if (value == nullptr)
        return widened;

    const BYTE* bytes = reinterpret_cast<const BYTE*>(value);
    while (*bytes != 0)
    {
        if (*bytes >= 0x80)
            return std::wstring();
        widened.push_back(static_cast<wchar_t>(*bytes));
        ++bytes;
    }
    return widened;
}

std::wstring MetadataPropVariantToString(const PROPVARIANT& prop)
{
    switch (prop.vt)
    {
    case VT_LPWSTR:
        return prop.pwszVal != nullptr ? std::wstring(prop.pwszVal) : std::wstring();
    case VT_BSTR:
        return prop.bstrVal != nullptr ? std::wstring(prop.bstrVal) : std::wstring();
    case VT_LPSTR:
        return WidenAsciiMetadata(prop.pszVal);
    case VT_UI1:
        return std::to_wstring(prop.bVal);
    case VT_UI2:
        return std::to_wstring(prop.uiVal);
    case VT_UI4:
        return std::to_wstring(prop.ulVal);
    case VT_I2:
        return std::to_wstring(prop.iVal);
    case VT_I4:
        return std::to_wstring(prop.lVal);
    case VT_R4:
    {
        std::wostringstream stream;
        stream << prop.fltVal;
        return stream.str();
    }
    case VT_R8:
    {
        std::wostringstream stream;
        stream << prop.dblVal;
        return stream.str();
    }
    case VT_FILETIME:
    {
        SYSTEMTIME time = {};
        if (!FileTimeToSystemTime(&prop.filetime, &time))
            return std::wstring();

        wchar_t buffer[32] = {};
        swprintf_s(buffer,
                   L"%04u-%02u-%02u %02u:%02u:%02u UTC",
                   time.wYear,
                   time.wMonth,
                   time.wDay,
                   time.wHour,
                   time.wMinute,
                   time.wSecond);
        return buffer;
    }
    default:
        return std::wstring();
    }
}

void AppendMetadataDetail(std::vector<ImageMetadataEntry>& details,
                          const wchar_t* name,
                          std::wstring value)
{
    TrimMetadataValue(value);
    if (name == nullptr || name[0] == L'\0' || value.empty())
        return;

    for (const ImageMetadataEntry& entry : details)
    {
        if (entry.Name == name)
            return;
    }

    ImageMetadataEntry entry;
    entry.Name = name;
    entry.Value = std::move(value);
    details.push_back(std::move(entry));
}

void TryAppendMetadataQuery(IWICMetadataQueryReader* reader,
                            const wchar_t* name,
                            const wchar_t* query,
                            std::vector<ImageMetadataEntry>& details)
{
    if (reader == nullptr || query == nullptr)
        return;

    PROPVARIANT prop;
    PropVariantInit(&prop);
    HRESULT hr = reader->GetMetadataByName(query, &prop);
    if (SUCCEEDED(hr))
        AppendMetadataDetail(details, name, MetadataPropVariantToString(prop));
    PropVariantClear(&prop);
}

void ProbeFrameMetadataDetails(IWICBitmapFrameDecode* frame,
                               std::vector<ImageMetadataEntry>& details)
{
    Microsoft::WRL::ComPtr<IWICMetadataQueryReader> reader;
    if (frame == nullptr || FAILED(frame->GetMetadataQueryReader(&reader)) || reader == nullptr)
        return;

    struct MetadataQuery
    {
        const wchar_t* Name;
        const wchar_t* Query;
    };

    const MetadataQuery queries[] = {
        {L"Image description", L"/app1/ifd/{ushort=270}"},
        {L"Image description", L"/ifd/{ushort=270}"},
        {L"Camera make", L"/app1/ifd/{ushort=271}"},
        {L"Camera make", L"/ifd/{ushort=271}"},
        {L"Camera model", L"/app1/ifd/{ushort=272}"},
        {L"Camera model", L"/ifd/{ushort=272}"},
        {L"Image date", L"/app1/ifd/{ushort=306}"},
        {L"Image date", L"/ifd/{ushort=306}"},
        {L"Artist", L"/app1/ifd/{ushort=315}"},
        {L"Artist", L"/ifd/{ushort=315}"},
        {L"Copyright", L"/app1/ifd/{ushort=33432}"},
        {L"Copyright", L"/ifd/{ushort=33432}"},
        {L"Exposure time", L"/app1/ifd/exif/{ushort=33434}"},
        {L"Exposure time", L"/ifd/exif/{ushort=33434}"},
        {L"F-number", L"/app1/ifd/exif/{ushort=33437}"},
        {L"F-number", L"/ifd/exif/{ushort=33437}"},
        {L"ISO speed", L"/app1/ifd/exif/{ushort=34855}"},
        {L"ISO speed", L"/ifd/exif/{ushort=34855}"},
        {L"Date taken", L"/app1/ifd/exif/{ushort=36867}"},
        {L"Date taken", L"/ifd/exif/{ushort=36867}"},
        {L"Date digitized", L"/app1/ifd/exif/{ushort=36868}"},
        {L"Date digitized", L"/ifd/exif/{ushort=36868}"},
        {L"Focal length", L"/app1/ifd/exif/{ushort=37386}"},
        {L"Focal length", L"/ifd/exif/{ushort=37386}"},
        {L"Lens model", L"/app1/ifd/exif/{ushort=42036}"},
        {L"Lens model", L"/ifd/exif/{ushort=42036}"},
    };

    for (const MetadataQuery& query : queries)
        TryAppendMetadataQuery(reader.Get(), query.Name, query.Query, details);
}

// Appends the comment blocks a metadata reader exposes. WIC puts JPEG and PNG comments on the
// frame and GIF comment extensions on the container.
void AppendComments(IWICMetadataQueryReader* reader, std::wstring& comment)
{
    const wchar_t* const queries[] = {
        L"/com/TextEntry",        // JPEG COM
        L"/commentext/TextEntry", // GIF comment extension
        L"/tEXt/{str=Comment}",   // PNG
        L"/tEXt/{str=Description}",
        L"/iTXt/TextEntry",
    };
    for (const wchar_t* query : queries)
    {
        PROPVARIANT value;
        PropVariantInit(&value);
        if (SUCCEEDED(reader->GetMetadataByName(query, &value)))
        {
            std::wstring text;
            if (value.vt == VT_LPSTR)
                text = DecodeCommentBytes(value.pszVal);
            else if (value.vt == VT_LPWSTR && value.pwszVal != nullptr)
                text = value.pwszVal;
            while (!text.empty() && (text.back() == L'\0' || text.back() == L'\r' || text.back() == L'\n' || text.back() == L' '))
                text.pop_back();
            if (!text.empty() && comment.find(text) == std::wstring::npos)
            {
                if (!comment.empty())
                    comment += L"\n";
                comment += text;
            }
        }
        PropVariantClear(&value);
    }
}

void ProbeFrameComment(IWICBitmapFrameDecode* frame, ImageFrameInfo& info)
{
    Microsoft::WRL::ComPtr<IWICMetadataQueryReader> reader;
    if (frame != nullptr && SUCCEEDED(frame->GetMetadataQueryReader(&reader)) && reader != nullptr)
        AppendComments(reader.Get(), info.Comment);
}

void ProbeOrientation(IWICBitmapFrameDecode* frame, ImageFrameInfo& info)
{
    Microsoft::WRL::ComPtr<IWICMetadataQueryReader> reader;
    if (frame == nullptr || FAILED(frame->GetMetadataQueryReader(&reader)) || reader == nullptr)
        return;

    uint16_t orientation = 1;
    if (TryGetUnsignedShortMetadata(reader.Get(), L"/app1/ifd/{ushort=274}", orientation) ||
        TryGetUnsignedShortMetadata(reader.Get(), L"/ifd/{ushort=274}", orientation))
    {
        info.HasOrientation = true;
        info.Orientation = orientation;
    }
}

void ProbeGifFrameMetadata(IWICBitmapFrameDecode* frame, ImageFrameInfo& info)
{
    Microsoft::WRL::ComPtr<IWICMetadataQueryReader> reader;
    if (frame == nullptr || FAILED(frame->GetMetadataQueryReader(&reader)) || reader == nullptr)
        return;

    uint16_t delayHundredths = 0;
    if (TryGetUnsignedShortMetadata(reader.Get(), L"/grctlext/Delay", delayHundredths))
    {
        info.HasAnimationDelay = true;
        info.AnimationDelayMilliseconds = static_cast<uint32_t>(delayHundredths) * 10u;
    }

    uint16_t disposal = 0;
    if (TryGetUnsignedShortMetadata(reader.Get(), L"/grctlext/Disposal", disposal) && disposal <= (std::numeric_limits<uint8_t>::max)())
    {
        info.HasGifDisposal = true;
        info.GifDisposal = static_cast<uint8_t>(disposal);
    }

    uint16_t left = 0;
    uint16_t top = 0;
    if (TryGetUnsignedShortMetadata(reader.Get(), L"/imgdesc/Left", left))
        info.OffsetLeft = left;
    if (TryGetUnsignedShortMetadata(reader.Get(), L"/imgdesc/Top", top))
        info.OffsetTop = top;
}

// The GIF logical screen (the canvas frames are placed on).
void ProbeGifCanvas(IWICBitmapDecoder* decoder, ImageMetadata& metadata)
{
    Microsoft::WRL::ComPtr<IWICMetadataQueryReader> reader;
    if (decoder == nullptr || FAILED(decoder->GetMetadataQueryReader(&reader)) || reader == nullptr)
        return;
    uint16_t width = 0;
    uint16_t height = 0;
    if (TryGetUnsignedShortMetadata(reader.Get(), L"/logscrdesc/Width", width) &&
        TryGetUnsignedShortMetadata(reader.Get(), L"/logscrdesc/Height", height) && width > 0 && height > 0)
    {
        metadata.CanvasWidth = width;
        metadata.CanvasHeight = height;
    }
    if (!metadata.Frames.empty())
        AppendComments(reader.Get(), metadata.Frames[0].Comment);
}


// ---------------------------------------------------------------------------------------------
// EXIF enumeration

struct ExifTagName
{
    uint16_t Tag;
    const wchar_t* Name;
};

// TIFF 6.0, EXIF 2.32 and GPS tags (names as in the EXIF specification).
const ExifTagName ExifTagNames[] = {
    {0x0001, L"InteroperabilityIndex"}, {0x0002, L"InteroperabilityVersion"},
    {0x00FE, L"NewSubfileType"}, {0x0100, L"ImageWidth"}, {0x0101, L"ImageLength"},
    {0x0102, L"BitsPerSample"}, {0x0103, L"Compression"}, {0x0106, L"PhotometricInterpretation"},
    {0x010D, L"DocumentName"}, {0x010E, L"ImageDescription"}, {0x010F, L"Make"}, {0x0110, L"Model"},
    {0x0111, L"StripOffsets"}, {0x0112, L"Orientation"}, {0x0115, L"SamplesPerPixel"},
    {0x0116, L"RowsPerStrip"}, {0x0117, L"StripByteCounts"}, {0x011A, L"XResolution"},
    {0x011B, L"YResolution"}, {0x011C, L"PlanarConfiguration"}, {0x0128, L"ResolutionUnit"},
    {0x012D, L"TransferFunction"}, {0x0131, L"Software"}, {0x0132, L"DateTime"}, {0x013B, L"Artist"},
    {0x013C, L"HostComputer"}, {0x013E, L"WhitePoint"}, {0x013F, L"PrimaryChromaticities"},
    {0x0201, L"JPEGInterchangeFormat"}, {0x0202, L"JPEGInterchangeFormatLength"},
    {0x0211, L"YCbCrCoefficients"}, {0x0212, L"YCbCrSubSampling"}, {0x0213, L"YCbCrPositioning"},
    {0x0214, L"ReferenceBlackWhite"}, {0x4746, L"Rating"}, {0x4749, L"RatingPercent"},
    {0x8298, L"Copyright"}, {0x829A, L"ExposureTime"}, {0x829D, L"FNumber"}, {0x8769, L"ExifIFDPointer"},
    {0x8822, L"ExposureProgram"}, {0x8824, L"SpectralSensitivity"}, {0x8825, L"GPSInfoIFDPointer"},
    {0x8827, L"PhotographicSensitivity"}, {0x8828, L"OECF"}, {0x8830, L"SensitivityType"},
    {0x8831, L"StandardOutputSensitivity"}, {0x8832, L"RecommendedExposureIndex"}, {0x8833, L"ISOSpeed"},
    {0x9000, L"ExifVersion"}, {0x9003, L"DateTimeOriginal"}, {0x9004, L"DateTimeDigitized"},
    {0x9010, L"OffsetTime"}, {0x9011, L"OffsetTimeOriginal"}, {0x9012, L"OffsetTimeDigitized"},
    {0x9101, L"ComponentsConfiguration"}, {0x9102, L"CompressedBitsPerPixel"},
    {0x9201, L"ShutterSpeedValue"}, {0x9202, L"ApertureValue"}, {0x9203, L"BrightnessValue"},
    {0x9204, L"ExposureBiasValue"}, {0x9205, L"MaxApertureValue"}, {0x9206, L"SubjectDistance"},
    {0x9207, L"MeteringMode"}, {0x9208, L"LightSource"}, {0x9209, L"Flash"}, {0x920A, L"FocalLength"},
    {0x9214, L"SubjectArea"}, {0x927C, L"MakerNote"}, {0x9286, L"UserComment"},
    {0x9290, L"SubSecTime"}, {0x9291, L"SubSecTimeOriginal"}, {0x9292, L"SubSecTimeDigitized"},
    {0x9C9B, L"XPTitle"}, {0x9C9C, L"XPComment"}, {0x9C9D, L"XPAuthor"}, {0x9C9E, L"XPKeywords"},
    {0x9C9F, L"XPSubject"}, {0xA000, L"FlashpixVersion"}, {0xA001, L"ColorSpace"},
    {0xA002, L"PixelXDimension"}, {0xA003, L"PixelYDimension"}, {0xA004, L"RelatedSoundFile"},
    {0xA005, L"InteroperabilityIFDPointer"}, {0xA20B, L"FlashEnergy"},
    {0xA20C, L"SpatialFrequencyResponse"}, {0xA20E, L"FocalPlaneXResolution"},
    {0xA20F, L"FocalPlaneYResolution"}, {0xA210, L"FocalPlaneResolutionUnit"},
    {0xA214, L"SubjectLocation"}, {0xA215, L"ExposureIndex"}, {0xA217, L"SensingMethod"},
    {0xA300, L"FileSource"}, {0xA301, L"SceneType"}, {0xA302, L"CFAPattern"},
    {0xA401, L"CustomRendered"}, {0xA402, L"ExposureMode"}, {0xA403, L"WhiteBalance"},
    {0xA404, L"DigitalZoomRatio"}, {0xA405, L"FocalLengthIn35mmFilm"}, {0xA406, L"SceneCaptureType"},
    {0xA407, L"GainControl"}, {0xA408, L"Contrast"}, {0xA409, L"Saturation"}, {0xA40A, L"Sharpness"},
    {0xA40B, L"DeviceSettingDescription"}, {0xA40C, L"SubjectDistanceRange"},
    {0xA420, L"ImageUniqueID"}, {0xA430, L"CameraOwnerName"}, {0xA431, L"BodySerialNumber"},
    {0xA432, L"LensSpecification"}, {0xA433, L"LensMake"}, {0xA434, L"LensModel"},
    {0xA435, L"LensSerialNumber"}, {0xA460, L"CompositeImage"}, {0xA500, L"Gamma"},
};

const ExifTagName GpsTagNames[] = {
    {0x0000, L"GPSVersionID"}, {0x0001, L"GPSLatitudeRef"}, {0x0002, L"GPSLatitude"},
    {0x0003, L"GPSLongitudeRef"}, {0x0004, L"GPSLongitude"}, {0x0005, L"GPSAltitudeRef"},
    {0x0006, L"GPSAltitude"}, {0x0007, L"GPSTimeStamp"}, {0x0008, L"GPSSatellites"},
    {0x0009, L"GPSStatus"}, {0x000A, L"GPSMeasureMode"}, {0x000B, L"GPSDOP"}, {0x000C, L"GPSSpeedRef"},
    {0x000D, L"GPSSpeed"}, {0x000E, L"GPSTrackRef"}, {0x000F, L"GPSTrack"}, {0x0010, L"GPSImgDirectionRef"},
    {0x0011, L"GPSImgDirection"}, {0x0012, L"GPSMapDatum"}, {0x0013, L"GPSDestLatitudeRef"},
    {0x0014, L"GPSDestLatitude"}, {0x0015, L"GPSDestLongitudeRef"}, {0x0016, L"GPSDestLongitude"},
    {0x0017, L"GPSDestBearingRef"}, {0x0018, L"GPSDestBearing"}, {0x0019, L"GPSDestDistanceRef"},
    {0x001A, L"GPSDestDistance"}, {0x001B, L"GPSProcessingMethod"}, {0x001C, L"GPSAreaInformation"},
    {0x001D, L"GPSDateStamp"}, {0x001E, L"GPSDifferential"}, {0x001F, L"GPSHPositioningError"},
};

std::wstring ExifTagDisplayName(const std::wstring& group, uint16_t tag)
{
    const bool gps = group == L"GPS";
    const ExifTagName* begin = gps ? std::begin(GpsTagNames) : std::begin(ExifTagNames);
    const ExifTagName* end = gps ? std::end(GpsTagNames) : std::end(ExifTagNames);
    for (const ExifTagName* entry = begin; entry != end; ++entry)
    {
        if (entry->Tag == tag)
            return entry->Name;
    }
    wchar_t text[16] = {};
    swprintf_s(text, L"Tag 0x%04X", static_cast<unsigned int>(tag));
    return text;
}

// "/{ushort=271}" -> 271.
bool ParseExifTagQuery(const std::wstring& item, uint16_t& tag)
{
    const std::size_t equals = item.find(L"ushort=");
    if (equals == std::wstring::npos)
        return false;
    const unsigned long value = wcstoul(item.c_str() + equals + 7, nullptr, 10);
    if (value > 0xFFFF)
        return false;
    tag = static_cast<uint16_t>(value);
    return true;
}

std::wstring FormatRational(uint64_t packed, bool isSigned)
{
    // WIC packs EXIF rationals with the numerator in the low DWORD and the denominator in the high.
    const uint32_t numerator = static_cast<uint32_t>(packed & 0xFFFFFFFFull);
    const uint32_t denominator = static_cast<uint32_t>(packed >> 32);
    std::wstring text = isSigned ? std::to_wstring(static_cast<int32_t>(numerator)) : std::to_wstring(numerator);
    if (denominator != 1)
    {
        text += L"/";
        text += isSigned ? std::to_wstring(static_cast<int32_t>(denominator)) : std::to_wstring(denominator);
    }
    return text;
}

std::wstring FormatExifBytes(uint16_t tag, const BYTE* bytes, ULONG count)
{
    if (bytes == nullptr || count == 0)
        return std::wstring();

    // XPTitle..XPSubject: UTF-16LE text.
    if (tag >= 0x9C9B && tag <= 0x9C9F)
    {
        std::wstring text(reinterpret_cast<const wchar_t*>(bytes), count / sizeof(wchar_t));
        TrimMetadataValue(text);
        return text;
    }
    // UserComment: an 8-byte character code, then the text.
    if (tag == 0x9286 && count > 8)
    {
        if (memcmp(bytes, "UNICODE\0", 8) == 0)
        {
            std::wstring text(reinterpret_cast<const wchar_t*>(bytes + 8), (count - 8) / sizeof(wchar_t));
            TrimMetadataValue(text);
            return text;
        }
        if (memcmp(bytes, "ASCII\0\0\0", 8) == 0)
        {
            std::wstring text;
            for (ULONG i = 8; i < count && bytes[i] != 0; ++i)
                text.push_back(bytes[i] < 0x80 ? static_cast<wchar_t>(bytes[i]) : L'?');
            TrimMetadataValue(text);
            return text;
        }
    }
    // Version tags ("0232") and other short printable runs.
    bool printable = count <= 64;
    for (ULONG i = 0; printable && i < count; ++i)
        printable = bytes[i] == 0 || (bytes[i] >= 0x20 && bytes[i] < 0x7F);
    if (printable)
    {
        std::wstring text;
        for (ULONG i = 0; i < count && bytes[i] != 0; ++i)
            text.push_back(static_cast<wchar_t>(bytes[i]));
        if (!text.empty())
            return text;
    }
    return L"(" + std::to_wstring(count) + L" bytes)";
}

template <typename T, typename Format>
std::wstring JoinVector(const T* values, ULONG count, Format format)
{
    constexpr ULONG maximumShown = 32;
    std::wstring text;
    for (ULONG i = 0; i < count && i < maximumShown; ++i)
    {
        if (i != 0)
            text += L", ";
        text += format(values[i]);
    }
    if (count > maximumShown)
        text += L", ...";
    return text;
}

std::wstring ExifPropVariantToString(uint16_t tag, const PROPVARIANT& prop)
{
    switch (prop.vt)
    {
    case VT_UI8:
        return FormatRational(prop.uhVal.QuadPart, false);
    case VT_I8:
        return FormatRational(static_cast<uint64_t>(prop.hVal.QuadPart), true);
    case VT_BLOB:
        return FormatExifBytes(tag, prop.blob.pBlobData, prop.blob.cbSize);
    case VT_VECTOR | VT_UI1:
        return FormatExifBytes(tag, prop.caub.pElems, prop.caub.cElems);
    case VT_VECTOR | VT_UI2:
        return JoinVector(prop.caui.pElems, prop.caui.cElems, [](USHORT v) { return std::to_wstring(v); });
    case VT_VECTOR | VT_UI4:
        return JoinVector(prop.caul.pElems, prop.caul.cElems, [](ULONG v) { return std::to_wstring(v); });
    case VT_VECTOR | VT_UI8:
        return JoinVector(prop.cauh.pElems, prop.cauh.cElems, [](ULARGE_INTEGER v) { return FormatRational(v.QuadPart, false); });
    case VT_VECTOR | VT_I8:
        return JoinVector(prop.cah.pElems, prop.cah.cElems,
                          [](LARGE_INTEGER v) { return FormatRational(static_cast<uint64_t>(v.QuadPart), true); });
    case VT_VECTOR | VT_LPSTR:
        return JoinVector(prop.calpstr.pElems, prop.calpstr.cElems, [](LPSTR v) { return WidenAsciiMetadata(v); });
    case VT_VECTOR | VT_LPWSTR:
        return JoinVector(prop.calpwstr.pElems, prop.calpwstr.cElems,
                          [](LPWSTR v) { return v != nullptr ? std::wstring(v) : std::wstring(); });
    default:
        return MetadataPropVariantToString(prop);
    }
}

void CollectExifTags(IWICMetadataQueryReader* reader, const wchar_t* group, std::vector<ImageExifEntry>& entries, int depth)
{
    constexpr std::size_t maximumEntries = 2048;
    Microsoft::WRL::ComPtr<IEnumString> names;
    if (reader == nullptr || depth > 3 || FAILED(reader->GetEnumerator(&names)) || names == nullptr)
        return;

    LPOLESTR rawName = nullptr;
    while (entries.size() < maximumEntries && names->Next(1, &rawName, nullptr) == S_OK)
    {
        const std::wstring item = rawName != nullptr ? rawName : L"";
        CoTaskMemFree(rawName);
        rawName = nullptr;

        PROPVARIANT value;
        PropVariantInit(&value);
        if (SUCCEEDED(reader->GetMetadataByName(item.c_str(), &value)))
        {
            if (value.vt == VT_UNKNOWN && value.punkVal != nullptr)
            {
                Microsoft::WRL::ComPtr<IWICMetadataQueryReader> child;
                if (SUCCEEDED(value.punkVal->QueryInterface(IID_PPV_ARGS(&child))))
                {
                    const wchar_t* childGroup = item == L"/exif"      ? L"Exif"
                                                : item == L"/gps"     ? L"GPS"
                                                : item == L"/interop" ? L"Interoperability"
                                                                      : group;
                    CollectExifTags(child.Get(), childGroup, entries, depth + 1);
                }
            }
            else
            {
                uint16_t tag = 0;
                if (ParseExifTagQuery(item, tag))
                {
                    ImageExifEntry entry;
                    entry.Tag = tag;
                    entry.Group = group;
                    entry.Name = ExifTagDisplayName(entry.Group, tag);
                    entry.Value = ExifPropVariantToString(tag, value);
                    switch (value.vt)
                    {
                    case VT_UI1:
                        entry.HasNumber = true;
                        entry.Number = value.bVal;
                        break;
                    case VT_UI2:
                        entry.HasNumber = true;
                        entry.Number = value.uiVal;
                        break;
                    case VT_UI4:
                        entry.HasNumber = true;
                        entry.Number = value.ulVal;
                        break;
                    case VT_I2:
                        entry.HasNumber = true;
                        entry.Number = value.iVal;
                        break;
                    case VT_I4:
                        entry.HasNumber = true;
                        entry.Number = value.lVal;
                        break;
                    }
                    TrimMetadataValue(entry.Value);
                    entries.push_back(std::move(entry));
                }
            }
        }
        PropVariantClear(&value);
    }
}

// The IFD tree of a JPEG (APP1) or TIFF frame.
void ProbeFrameExif(IWICBitmapFrameDecode* frame, std::vector<ImageExifEntry>& entries)
{
    Microsoft::WRL::ComPtr<IWICMetadataQueryReader> reader;
    if (frame == nullptr || FAILED(frame->GetMetadataQueryReader(&reader)) || reader == nullptr)
        return;

    for (const wchar_t* root : {L"/app1/ifd", L"/ifd"})
    {
        PROPVARIANT value;
        PropVariantInit(&value);
        if (SUCCEEDED(reader->GetMetadataByName(root, &value)) && value.vt == VT_UNKNOWN && value.punkVal != nullptr)
        {
            Microsoft::WRL::ComPtr<IWICMetadataQueryReader> ifd;
            if (SUCCEEDED(value.punkVal->QueryInterface(IID_PPV_ARGS(&ifd))))
                CollectExifTags(ifd.Get(), L"IFD0", entries, 0);
        }
        PropVariantClear(&value);
        if (!entries.empty())
            break;
    }
}

void DescribePixelFormat(IWICImagingFactory* factory, ImageFrameInfo& info)
{
    if (factory == nullptr || IsEqualGUID(info.PixelFormat, GUID_NULL))
        return;
    Microsoft::WRL::ComPtr<IWICComponentInfo> component;
    Microsoft::WRL::ComPtr<IWICPixelFormatInfo> pixelFormat;
    if (FAILED(factory->CreateComponentInfo(info.PixelFormat, &component)) || FAILED(component.As(&pixelFormat)))
        return;
    UINT bits = 0;
    UINT channels = 0;
    if (SUCCEEDED(pixelFormat->GetBitsPerPixel(&bits)))
        info.BitsPerPixel = bits;
    if (SUCCEEDED(pixelFormat->GetChannelCount(&channels)))
        info.ChannelCount = channels;
}

ImageError ProbeFrame(IWICBitmapDecoder* decoder,
                      const GUID& containerFormat,
                      uint32_t index,
                      ImageFrameInfo& info,
                      std::vector<ImageMetadataEntry>* details,
                      std::vector<ImageExifEntry>* exif)
{
    Microsoft::WRL::ComPtr<IWICBitmapFrameDecode> frame;
    HRESULT hr = decoder->GetFrame(index, &frame);
    if (FAILED(hr))
        return MakeError(ImageErrorCode::MetadataFailed, hr, L"Unable to read image frame metadata.");

    UINT width = 0;
    UINT height = 0;
    hr = frame->GetSize(&width, &height);
    if (FAILED(hr))
        return MakeError(ImageErrorCode::MetadataFailed, hr, L"Unable to read image dimensions.");

    info.Width = width;
    info.Height = height;
    frame->GetResolution(&info.DpiX, &info.DpiY);
    frame->GetPixelFormat(&info.PixelFormat);
    ProbeOrientation(frame.Get(), info);
    ProbeFrameComment(frame.Get(), info);
    if (IsEqualGUID(containerFormat, GUID_ContainerFormatGif))
        ProbeGifFrameMetadata(frame.Get(), info);
    if (details != nullptr)
        ProbeFrameMetadataDetails(frame.Get(), *details);
    if (exif != nullptr)
        ProbeFrameExif(frame.Get(), *exif);
    return ImageError::Ok();
}

ImageError CheckDecodeLimits(uint32_t width, uint32_t height, const DecodeLimits& limits)
{
    if (width == 0 || height == 0)
        return MakeError(ImageErrorCode::DecodeFailed, WINCODEC_ERR_BADIMAGE, L"Image dimensions are invalid.");

    if (width > limits.MaxWidth || height > limits.MaxHeight)
        return MakeError(ImageErrorCode::SizeLimitExceeded, HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE), L"Image dimensions exceed the configured decode limit.");

    uint64_t pixels = static_cast<uint64_t>(width) * static_cast<uint64_t>(height);
    if (pixels > limits.MaxPixels)
        return MakeError(ImageErrorCode::SizeLimitExceeded, HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE), L"Image pixel count exceeds the configured decode limit.");

    uint64_t stride = static_cast<uint64_t>(width) * 4ull;
    uint64_t bytes = stride * static_cast<uint64_t>(height);
    if (stride > (std::numeric_limits<uint32_t>::max)() || bytes > limits.MaxBytes || bytes > (std::numeric_limits<UINT>::max)())
        return MakeError(ImageErrorCode::SizeLimitExceeded, HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE), L"Decoded image buffer exceeds the configured byte limit.");

    return ImageError::Ok();
}

void FitWithinBounds(uint32_t sourceWidth,
                     uint32_t sourceHeight,
                     uint32_t maxWidth,
                     uint32_t maxHeight,
                     uint32_t& targetWidth,
                     uint32_t& targetHeight)
{
    targetWidth = sourceWidth;
    targetHeight = sourceHeight;

    if (targetWidth == 0 || targetHeight == 0 || maxWidth == 0 || maxHeight == 0)
        return;

    if (targetWidth > maxWidth)
    {
        targetHeight = static_cast<uint32_t>((static_cast<uint64_t>(targetHeight) * maxWidth + targetWidth / 2u) / targetWidth);
        targetWidth = maxWidth;
        if (targetHeight == 0)
            targetHeight = 1;
    }

    if (targetHeight > maxHeight)
    {
        targetWidth = static_cast<uint32_t>((static_cast<uint64_t>(targetWidth) * maxHeight + targetHeight / 2u) / targetHeight);
        targetHeight = maxHeight;
        if (targetWidth == 0)
            targetWidth = 1;
    }
}

bool SourceIsSmallerThanFrame(IWICBitmapSource* source, const ImageFrameInfo& frameInfo)
{
    if (source == nullptr)
        return false;

    UINT width = 0;
    UINT height = 0;
    if (FAILED(source->GetSize(&width, &height)) || width == 0 || height == 0)
        return false;

    const uint64_t sourcePixels = static_cast<uint64_t>(width) * height;
    const uint64_t framePixels = static_cast<uint64_t>(frameInfo.Width) * frameInfo.Height;
    return sourcePixels > 0 && sourcePixels < framePixels;
}

// The source's own pixels when it is a palette image of at most 8 bits per pixel; on any failure
// 'indices' stays empty (the pipette then shows the colour only).
void ReadPaletteIndices(IWICBitmapSource* source, uint32_t width, uint32_t height, std::vector<uint8_t>& indices)
{
    indices.clear();
    WICPixelFormatGUID format = {};
    if (FAILED(source->GetPixelFormat(&format)))
        return;
    uint32_t bits = 0;
    if (IsEqualGUID(format, GUID_WICPixelFormat1bppIndexed))
        bits = 1;
    else if (IsEqualGUID(format, GUID_WICPixelFormat2bppIndexed))
        bits = 2;
    else if (IsEqualGUID(format, GUID_WICPixelFormat4bppIndexed))
        bits = 4;
    else if (IsEqualGUID(format, GUID_WICPixelFormat8bppIndexed))
        bits = 8;
    else
        return;
    const uint64_t stride = (static_cast<uint64_t>(width) * bits + 7) / 8;
    const uint64_t size = stride * height;
    if (size > UINT_MAX)
        return;
    try
    {
        std::vector<uint8_t> packed(static_cast<size_t>(size));
        if (FAILED(source->CopyPixels(nullptr, static_cast<UINT>(stride), static_cast<UINT>(size), packed.data())))
            return;
        UnpackPaletteIndices(packed.data(), static_cast<uint32_t>(stride), width, height, bits, indices);
    }
    catch (const std::bad_alloc&)
    {
        indices.clear();
    }
}

DecodeFrameResult DecodeBitmapSourceToBgra(IWICImagingFactory* factory,
                                           IWICBitmapSource* source,
                                           const ImageFrameInfo& frameInfo,
                                           const DecodeOptions& options,
                                           const CancellationToken& cancellation)
{
    DecodeFrameResult result;

    if (source == nullptr)
    {
        result.Error = MakeError(ImageErrorCode::InvalidArgument, E_INVALIDARG, L"Image source is invalid.");
        return result;
    }

    if (IsCanceled(cancellation))
    {
        result.Error = MakeError(ImageErrorCode::Canceled, HRESULT_FROM_WIN32(ERROR_CANCELLED), L"Image decode was canceled.");
        return result;
    }

    UINT width = 0;
    UINT height = 0;
    HRESULT hr = source->GetSize(&width, &height);
    if (FAILED(hr))
    {
        result.Error = MakeError(ImageErrorCode::DecodeFailed, hr, L"Unable to read image source dimensions.");
        return result;
    }

    result.Error = CheckDecodeLimits(width, height, options.Limits);
    if (!result.Error.Succeeded())
        return result;

    Microsoft::WRL::ComPtr<IWICFormatConverter> converter;
    hr = factory->CreateFormatConverter(&converter);
    if (FAILED(hr))
    {
        result.Error = MakeError(ImageErrorCode::DecodeFailed, hr, L"Unable to create WIC format converter.");
        return result;
    }

    hr = converter->Initialize(source, GUID_WICPixelFormat32bppBGRA,
                               WICBitmapDitherTypeNone, nullptr, 0.0,
                               WICBitmapPaletteTypeCustom);
    if (FAILED(hr))
    {
        result.Error = MakeError(ImageErrorCode::DecodeFailed, hr, L"Unable to convert image source to 32-bit BGRA pixels.");
        return result;
    }

    if (IsCanceled(cancellation))
    {
        result.Error = MakeError(ImageErrorCode::Canceled, HRESULT_FROM_WIN32(ERROR_CANCELLED), L"Image decode was canceled.");
        return result;
    }

    result.Surface.Width = width;
    result.Surface.Height = height;
    result.Surface.Stride = width * 4u;
    const uint64_t byteCount = static_cast<uint64_t>(result.Surface.Stride) * height;

    try
    {
        result.Surface.Pixels.resize(static_cast<size_t>(byteCount));
    }
    catch (const std::bad_alloc&)
    {
        result.Error = MakeError(ImageErrorCode::OutOfMemory, E_OUTOFMEMORY, L"Unable to allocate decoded image buffer.");
        return result;
    }

    // Large images are copied in bands, so the load can show progress and stop when cancelled.
    const uint32_t bandRows = DecodeBandRows(result.Surface.Stride, height);
    for (uint32_t top = 0; top < height; top += bandRows)
    {
        const uint32_t rows = std::min(bandRows, height - top);
        const WICRect band = {0, static_cast<INT>(top), static_cast<INT>(width), static_cast<INT>(rows)};
        hr = converter->CopyPixels(&band, result.Surface.Stride, result.Surface.Stride * rows,
                                   result.Surface.Pixels.data() + static_cast<size_t>(result.Surface.Stride) * top);
        if (FAILED(hr))
        {
            result.Surface.Pixels.clear();
            result.Error = MakeError(ImageErrorCode::DecodeFailed, hr, L"Unable to copy decoded image pixels.");
            return result;
        }
        if (rows == height)
            break; // one band: nothing to report
        if (IsCanceled(cancellation))
            break; // reported below
        if (options.Progress)
            options.Progress(static_cast<uint32_t>((static_cast<uint64_t>(top + rows) * 100u) / height));
    }

    if (IsCanceled(cancellation))
    {
        result.Surface.Pixels.clear();
        result.Error = MakeError(ImageErrorCode::Canceled, HRESULT_FROM_WIN32(ERROR_CANCELLED), L"Image decode was canceled.");
        return result;
    }

    if (options.KeepPaletteIndices && !(options.ApplyOrientation && OrientationNeedsTransform(frameInfo.Orientation)))
        ReadPaletteIndices(source, width, height, result.Surface.PaletteIndices);

    if (options.ApplyOrientation)
    {
        result.Error = ApplyOrientation(result.Surface, frameInfo);
        if (!result.Error.Succeeded())
        {
            result.Surface.Pixels.clear();
            return result;
        }
    }

    result.Error = ImageError::Ok();
    return result;
}

ImageError ValidateTransformSurface(const ImageSurface& surface)
{
    if (surface.Format != ImageSurfaceFormat::Bgra32 || surface.Width == 0 || surface.Height == 0)
        return MakeError(ImageErrorCode::InvalidArgument, E_INVALIDARG, L"Image surface is invalid.");

    const uint64_t sourceStride = static_cast<uint64_t>(surface.Width) * 4ull;
    if (sourceStride > (std::numeric_limits<uint32_t>::max)() || surface.Stride < sourceStride)
        return MakeError(ImageErrorCode::InvalidArgument, E_INVALIDARG, L"Image surface stride is invalid.");

    const uint64_t bytes = static_cast<uint64_t>(surface.Stride) * surface.Height;
    if (bytes > surface.Pixels.size())
        return MakeError(ImageErrorCode::InvalidArgument, E_INVALIDARG, L"Image surface buffer is incomplete.");

    const uint64_t sameStride = static_cast<uint64_t>(surface.Width) * 4ull;
    const uint64_t sameBytes = sameStride * surface.Height;
    const uint64_t dstStride = static_cast<uint64_t>(surface.Height) * 4ull;
    const uint64_t rotateBytes = dstStride * surface.Width;
    if (sameStride > (std::numeric_limits<uint32_t>::max)() ||
        sameBytes > (std::numeric_limits<size_t>::max)() ||
        dstStride > (std::numeric_limits<uint32_t>::max)() ||
        rotateBytes > (std::numeric_limits<size_t>::max)())
    {
        return MakeError(ImageErrorCode::SizeLimitExceeded, HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE), L"Transformed image surface is too large.");
    }

    return ImageError::Ok();
}

void SourcePointForTransform(ImageSurfaceTransform transform,
                             uint32_t dstX,
                             uint32_t dstY,
                             uint32_t srcWidth,
                             uint32_t srcHeight,
                             uint32_t& srcX,
                             uint32_t& srcY)
{
    switch (transform)
    {
    case ImageSurfaceTransform::Rotate90Clockwise:
        srcX = dstY;
        srcY = srcHeight - 1 - dstX;
        break;
    case ImageSurfaceTransform::Rotate90CounterClockwise:
        srcX = srcWidth - 1 - dstY;
        srcY = dstX;
        break;
    case ImageSurfaceTransform::Rotate180:
        srcX = srcWidth - 1 - dstX;
        srcY = srcHeight - 1 - dstY;
        break;
    case ImageSurfaceTransform::FlipHorizontal:
        srcX = srcWidth - 1 - dstX;
        srcY = dstY;
        break;
    case ImageSurfaceTransform::FlipVertical:
        srcX = dstX;
        srcY = srcHeight - 1 - dstY;
        break;
    default:
        srcX = dstX;
        srcY = dstY;
        break;
    }
}

bool GetEncoderSettings(ImageSaveFormat format, GUID& containerFormat, WICPixelFormatGUID& pixelFormat)
{
    switch (format)
    {
    case ImageSaveFormat::Png:
        containerFormat = GUID_ContainerFormatPng;
        pixelFormat = GUID_WICPixelFormat32bppBGRA;
        return true;
    case ImageSaveFormat::Jpeg:
        containerFormat = GUID_ContainerFormatJpeg;
        pixelFormat = GUID_WICPixelFormat24bppBGR;
        return true;
    case ImageSaveFormat::Bmp:
        containerFormat = GUID_ContainerFormatBmp;
        pixelFormat = GUID_WICPixelFormat24bppBGR;
        return true;
    case ImageSaveFormat::Tiff:
        containerFormat = GUID_ContainerFormatTiff;
        pixelFormat = GUID_WICPixelFormat32bppBGRA;
        return true;
    case ImageSaveFormat::Gif:
        containerFormat = GUID_ContainerFormatGif;
        pixelFormat = GUID_WICPixelFormat8bppIndexed;
        return true;
    default:
        return false;
    }
}

ImageError ValidateSaveSurface(const ImageSurface& surface)
{
    if (surface.Format != ImageSurfaceFormat::Bgra32 || surface.Width == 0 || surface.Height == 0)
        return MakeError(ImageErrorCode::InvalidArgument, E_INVALIDARG, L"Image surface is invalid.");

    if (surface.Stride < surface.Width * 4u)
        return MakeError(ImageErrorCode::InvalidArgument, E_INVALIDARG, L"Image surface stride is invalid.");

    const uint64_t bytes = static_cast<uint64_t>(surface.Stride) * surface.Height;
    if (bytes > surface.Pixels.size())
        return MakeError(ImageErrorCode::InvalidArgument, E_INVALIDARG, L"Image surface buffer is incomplete.");

    if (surface.Stride > (std::numeric_limits<UINT>::max)() ||
        bytes > (std::numeric_limits<UINT>::max)())
    {
        return MakeError(ImageErrorCode::SizeLimitExceeded, HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE), L"Image surface is too large to encode.");
    }

    return ImageError::Ok();
}

HRESULT WriteEncoderFloatOption(IPropertyBag2* properties, const wchar_t* name, float value)
{
    PROPBAG2 option = {};
    option.pstrName = const_cast<LPOLESTR>(name);

    VARIANT variant = {};
    variant.vt = VT_R4;
    variant.fltVal = value;

    return properties->Write(1, &option, &variant);
}

HRESULT WriteEncoderByteOption(IPropertyBag2* properties, const wchar_t* name, BYTE value)
{
    PROPBAG2 option = {};
    option.pstrName = const_cast<LPOLESTR>(name);

    VARIANT variant = {};
    variant.vt = VT_UI1;
    variant.bVal = value;

    return properties->Write(1, &option, &variant);
}

bool JpegSubsamplingToWic(ImageJpegSubsampling subsampling, BYTE& value)
{
    switch (subsampling)
    {
    case ImageJpegSubsampling::OneToOneOne:
        value = static_cast<BYTE>(WICJpegYCrCbSubsampling444);
        return true;
    case ImageJpegSubsampling::TwoToOneOne:
        value = static_cast<BYTE>(WICJpegYCrCbSubsampling422);
        return true;
    case ImageJpegSubsampling::Default:
    default:
        return false;
    }
}

bool TiffCompressionToWic(ImageTiffCompression compression, BYTE& value)
{
    switch (compression)
    {
    case ImageTiffCompression::None:
        value = static_cast<BYTE>(WICTiffCompressionNone);
        return true;
    case ImageTiffCompression::Lzw:
        value = static_cast<BYTE>(WICTiffCompressionLZW);
        return true;
    case ImageTiffCompression::Zip:
        value = static_cast<BYTE>(WICTiffCompressionZIP);
        return true;
    case ImageTiffCompression::Default:
    default:
        return false;
    }
}

HRESULT ConfigureEncoderProperties(IPropertyBag2* properties, const SaveOptions& options)
{
    if (properties == nullptr)
        return S_OK;

    if (options.Format == ImageSaveFormat::Jpeg)
    {
        HRESULT hr = WriteEncoderFloatOption(properties,
                                             L"ImageQuality",
                                             (std::max)(0.0f, (std::min)(1.0f, options.JpegQuality)));
        if (FAILED(hr))
            return hr;

        BYTE subsampling = 0;
        if (JpegSubsamplingToWic(options.JpegSubsampling, subsampling))
            hr = WriteEncoderByteOption(properties, L"JpegYCrCbSubsampling", subsampling);

        return hr;
    }

    if (options.Format == ImageSaveFormat::Tiff)
    {
        BYTE compression = 0;
        if (TiffCompressionToWic(options.TiffCompression, compression))
            return WriteEncoderByteOption(properties, L"TiffCompressionMethod", compression);
    }

    return S_OK;
}

ImageError MakeSaveError(HRESULT hr, const wchar_t* message)
{
    if (hr == WINCODEC_ERR_COMPONENTNOTFOUND || hr == WINCODEC_ERR_UNSUPPORTEDPIXELFORMAT)
        return MakeError(ImageErrorCode::UnsupportedFormat, hr, message);
    return MakeError(ImageErrorCode::SaveFailed, hr, message);
}

} // namespace

ImageError ImageError::Ok()
{
    return ImageError();
}

ImageError ImageError::Failure(ImageErrorCode code, HRESULT hr, const wchar_t* message)
{
    ImageError error;
    error.Code = code;
    error.Hr = hr;
    error.Win32Error = HRESULT_FACILITY(hr) == FACILITY_WIN32 ? HRESULT_CODE(hr) : ERROR_SUCCESS;
    if (message != nullptr)
        error.Message = message;
    return error;
}

CancellationToken::CancellationToken() = default;

CancellationToken::CancellationToken(std::shared_ptr<std::atomic_bool> state)
    : m_state(std::move(state))
{
}

bool CancellationToken::IsCancellationRequested() const
{
    return m_state != nullptr && m_state->load(std::memory_order_acquire);
}

CancellationSource::CancellationSource()
    : m_state(std::make_shared<std::atomic_bool>(false))
{
}

CancellationToken CancellationSource::Token() const
{
    return CancellationToken(m_state);
}

void CancellationSource::Cancel()
{
    m_state->store(true, std::memory_order_release);
}

class ImageEngine::ComApartment
{
public:
    ComApartment()
    {
        m_hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        if (m_hr == S_OK || m_hr == S_FALSE)
        {
            m_ownsApartment = true;
        }
        else if (m_hr == RPC_E_CHANGED_MODE)
        {
            m_hr = S_OK;
        }
    }

    ~ComApartment()
    {
        if (m_ownsApartment)
            CoUninitialize();
    }

    HRESULT Result() const { return m_hr; }

private:
    HRESULT m_hr = S_OK;
    bool m_ownsApartment = false;
};

ImageDocument::ImageDocument(Microsoft::WRL::ComPtr<IWICImagingFactory> factory,
                             Microsoft::WRL::ComPtr<IWICBitmapDecoder> decoder,
                             ImageMetadata metadata)
    : m_factory(std::move(factory)),
      m_decoder(std::move(decoder)),
      m_metadata(std::move(metadata))
{
}

ImageDocument::ImageDocument(ImageDocument&&) noexcept = default;
ImageDocument& ImageDocument::operator=(ImageDocument&&) noexcept = default;
ImageDocument::~ImageDocument() = default;

DecodeFrameResult ImageDocument::DecodeFrame(uint32_t frameIndex,
                                             const DecodeOptions& options,
                                             const CancellationToken& cancellation) const
{
    DecodeFrameResult result;

    if (IsCanceled(cancellation))
    {
        result.Error = MakeError(ImageErrorCode::Canceled, HRESULT_FROM_WIN32(ERROR_CANCELLED), L"Image decode was canceled.");
        return result;
    }

    if (frameIndex >= m_metadata.Frames.size())
    {
        result.Error = MakeError(ImageErrorCode::FrameOutOfRange, E_INVALIDARG, L"Requested image frame does not exist.");
        return result;
    }

    const ImageFrameInfo& frameInfo = m_metadata.Frames[frameIndex];
    result.Error = CheckDecodeLimits(frameInfo.Width, frameInfo.Height, options.Limits);
    if (!result.Error.Succeeded())
        return result;

    Microsoft::WRL::ComPtr<IWICBitmapFrameDecode> frame;
    HRESULT hr = m_decoder->GetFrame(frameIndex, &frame);
    if (FAILED(hr))
    {
        result.Error = MakeError(ImageErrorCode::DecodeFailed, hr, L"Unable to read the requested image frame.");
        return result;
    }

    result = DecodeBitmapSourceToBgra(m_factory.Get(), frame.Get(), frameInfo, options, cancellation);
    result.Source = DecodeFrameResult::SourceKind::FullFrame;
    return result;
}

DecodeFrameResult ImageDocument::DecodeFrameThumbnail(uint32_t frameIndex,
                                                      uint32_t maxWidth,
                                                      uint32_t maxHeight,
                                                      const DecodeOptions& options,
                                                      const CancellationToken& cancellation) const
{
    return DecodeFrameThumbnail(frameIndex,
                                maxWidth,
                                maxHeight,
                                ThumbnailDecodeOptions(),
                                options,
                                cancellation);
}

DecodeFrameResult ImageDocument::DecodeFrameThumbnail(uint32_t frameIndex,
                                                      uint32_t maxWidth,
                                                      uint32_t maxHeight,
                                                      const ThumbnailDecodeOptions& thumbnailOptions,
                                                      const DecodeOptions& options,
                                                      const CancellationToken& cancellation) const
{
    DecodeFrameResult result;

    if (IsCanceled(cancellation))
    {
        result.Error = MakeError(ImageErrorCode::Canceled, HRESULT_FROM_WIN32(ERROR_CANCELLED), L"Image decode was canceled.");
        return result;
    }

    if (maxWidth == 0 || maxHeight == 0)
    {
        result.Error = MakeError(ImageErrorCode::InvalidArgument, E_INVALIDARG, L"Thumbnail bounds are invalid.");
        return result;
    }

    if (frameIndex >= m_metadata.Frames.size())
    {
        result.Error = MakeError(ImageErrorCode::FrameOutOfRange, E_INVALIDARG, L"Requested image frame does not exist.");
        return result;
    }

    const ImageFrameInfo& frameInfo = m_metadata.Frames[frameIndex];

    Microsoft::WRL::ComPtr<IWICBitmapFrameDecode> frame;
    HRESULT hr = m_decoder->GetFrame(frameIndex, &frame);
    if (FAILED(hr))
    {
        result.Error = MakeError(ImageErrorCode::DecodeFailed, hr, L"Unable to read the requested image frame.");
        return result;
    }

    Microsoft::WRL::ComPtr<IWICBitmapSource> source;
    DecodeFrameResult::SourceKind sourceKind = DecodeFrameResult::SourceKind::FullFrame;

    if (thumbnailOptions.UseEmbeddedThumbnailSources)
    {
        Microsoft::WRL::ComPtr<IWICBitmapSource> embeddedThumbnail;
        if (SUCCEEDED(frame->GetThumbnail(&embeddedThumbnail)) &&
            SourceIsSmallerThanFrame(embeddedThumbnail.Get(), frameInfo))
        {
            source = embeddedThumbnail;
            sourceKind = DecodeFrameResult::SourceKind::EmbeddedThumbnail;
        }

        if (source == nullptr && frameIndex == 0)
        {
            Microsoft::WRL::ComPtr<IWICBitmapSource> preview;
            if (SUCCEEDED(m_decoder->GetPreview(&preview)) &&
                SourceIsSmallerThanFrame(preview.Get(), frameInfo))
            {
                source = preview;
                sourceKind = DecodeFrameResult::SourceKind::DecoderPreview;
            }
        }
    }

    if (source == nullptr)
    {
        hr = frame.As(&source);
        if (FAILED(hr))
        {
            result.Error = MakeError(ImageErrorCode::DecodeFailed, hr, L"Unable to use image frame as a WIC bitmap source.");
            return result;
        }
    }

    UINT sourceWidth = 0;
    UINT sourceHeight = 0;
    hr = source->GetSize(&sourceWidth, &sourceHeight);
    if (FAILED(hr))
    {
        result.Error = MakeError(ImageErrorCode::DecodeFailed, hr, L"Unable to read image source dimensions.");
        return result;
    }

    const uint32_t preOrientationMaxWidth = OrientationSwapsDimensions(frameInfo.Orientation) ? maxHeight : maxWidth;
    const uint32_t preOrientationMaxHeight = OrientationSwapsDimensions(frameInfo.Orientation) ? maxWidth : maxHeight;

    uint32_t targetWidth = sourceWidth;
    uint32_t targetHeight = sourceHeight;
    FitWithinBounds(sourceWidth, sourceHeight, preOrientationMaxWidth, preOrientationMaxHeight, targetWidth, targetHeight);

    Microsoft::WRL::ComPtr<IWICBitmapSource> decodeSource = source;
    if (targetWidth != sourceWidth || targetHeight != sourceHeight)
    {
        Microsoft::WRL::ComPtr<IWICBitmapScaler> scaler;
        hr = m_factory->CreateBitmapScaler(&scaler);
        if (FAILED(hr))
        {
            result.Error = MakeError(ImageErrorCode::DecodeFailed, hr, L"Unable to create WIC thumbnail scaler.");
            return result;
        }

        hr = scaler->Initialize(source.Get(), targetWidth, targetHeight, WICBitmapInterpolationModeFant);
        if (FAILED(hr))
        {
            result.Error = MakeError(ImageErrorCode::DecodeFailed, hr, L"Unable to scale image source for thumbnail output.");
            return result;
        }

        hr = scaler.As(&decodeSource);
        if (FAILED(hr))
        {
            result.Error = MakeError(ImageErrorCode::DecodeFailed, hr, L"Unable to use scaled thumbnail source.");
            return result;
        }
    }

    result = DecodeBitmapSourceToBgra(m_factory.Get(), decodeSource.Get(), frameInfo, options, cancellation);
    result.Source = sourceKind;
    return result;
}

TransformSurfaceResult TransformSurface(const ImageSurface& surface, ImageSurfaceTransform transform)
{
    TransformSurfaceResult result;
    result.Error = ValidateTransformSurface(surface);
    if (!result.Error.Succeeded())
        return result;

    const bool swapsDimensions = transform == ImageSurfaceTransform::Rotate90Clockwise ||
                                 transform == ImageSurfaceTransform::Rotate90CounterClockwise;
    result.Surface.Width = swapsDimensions ? surface.Height : surface.Width;
    result.Surface.Height = swapsDimensions ? surface.Width : surface.Height;
    result.Surface.Stride = result.Surface.Width * 4u;
    result.Surface.Format = ImageSurfaceFormat::Bgra32;

    const uint64_t byteCount = static_cast<uint64_t>(result.Surface.Stride) * result.Surface.Height;
    try
    {
        result.Surface.Pixels.resize(static_cast<size_t>(byteCount));
    }
    catch (const std::bad_alloc&)
    {
        result.Error = MakeError(ImageErrorCode::OutOfMemory, E_OUTOFMEMORY, L"Unable to allocate transformed image buffer.");
        return result;
    }

    for (uint32_t y = 0; y < result.Surface.Height; ++y)
    {
        for (uint32_t x = 0; x < result.Surface.Width; ++x)
        {
            uint32_t srcX = 0;
            uint32_t srcY = 0;
            SourcePointForTransform(transform, x, y, surface.Width, surface.Height, srcX, srcY);

            const uint8_t* src = surface.Pixels.data() + static_cast<size_t>(srcY) * surface.Stride + static_cast<size_t>(srcX) * 4u;
            uint8_t* dst = result.Surface.Pixels.data() + static_cast<size_t>(y) * result.Surface.Stride + static_cast<size_t>(x) * 4u;
            dst[0] = src[0];
            dst[1] = src[1];
            dst[2] = src[2];
            dst[3] = src[3];
        }
    }

    result.Error = ImageError::Ok();
    return result;
}

CropSurfaceResult CropSurface(const ImageSurface& surface,
                              uint32_t left,
                              uint32_t top,
                              uint32_t width,
                              uint32_t height)
{
    CropSurfaceResult result;
    result.Error = ValidateTransformSurface(surface);
    if (!result.Error.Succeeded())
        return result;

    if (width == 0 || height == 0 ||
        left >= surface.Width || top >= surface.Height ||
        width > surface.Width - left || height > surface.Height - top)
    {
        result.Error = MakeError(ImageErrorCode::InvalidArgument, E_INVALIDARG, L"Crop rectangle is outside the image surface.");
        return result;
    }

    const uint64_t stride = static_cast<uint64_t>(width) * 4ull;
    const uint64_t byteCount = stride * height;
    if (stride > (std::numeric_limits<uint32_t>::max)() ||
        byteCount > (std::numeric_limits<size_t>::max)())
    {
        result.Error = MakeError(ImageErrorCode::SizeLimitExceeded, HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE), L"Cropped image surface is too large.");
        return result;
    }

    result.Surface.Width = width;
    result.Surface.Height = height;
    result.Surface.Stride = static_cast<uint32_t>(stride);
    result.Surface.Format = ImageSurfaceFormat::Bgra32;

    try
    {
        result.Surface.Pixels.resize(static_cast<size_t>(byteCount));
    }
    catch (const std::bad_alloc&)
    {
        result.Error = MakeError(ImageErrorCode::OutOfMemory, E_OUTOFMEMORY, L"Unable to allocate cropped image buffer.");
        return result;
    }

    const size_t rowBytes = static_cast<size_t>(width) * 4u;
    for (uint32_t y = 0; y < height; ++y)
    {
        const uint8_t* src = surface.Pixels.data() +
                             static_cast<size_t>(top + y) * surface.Stride +
                             static_cast<size_t>(left) * 4u;
        uint8_t* dst = result.Surface.Pixels.data() +
                       static_cast<size_t>(y) * result.Surface.Stride;
        std::copy_n(src, rowBytes, dst);
    }

    result.Error = ImageError::Ok();
    return result;
}

ImageHistogramResult ComputeSurfaceHistogram(const ImageSurface& surface)
{
    ImageHistogramResult result;
    result.Error = ValidateTransformSurface(surface);
    if (!result.Error.Succeeded())
        return result;

    result.Histogram.PixelCount = static_cast<uint64_t>(surface.Width) * surface.Height;

    for (uint32_t y = 0; y < surface.Height; ++y)
    {
        const uint8_t* row = surface.Pixels.data() + static_cast<size_t>(y) * surface.Stride;
        for (uint32_t x = 0; x < surface.Width; ++x)
        {
            const uint8_t* pixel = row + static_cast<size_t>(x) * 4u;
            const uint32_t blue = pixel[0];
            const uint32_t green = pixel[1];
            const uint32_t red = pixel[2];
            const uint32_t luminosity = (299u * red + 587u * green + 114u * blue) / 1000u;

            ++result.Histogram.Blue[blue];
            ++result.Histogram.Green[green];
            ++result.Histogram.Red[red];
            ++result.Histogram.Luminosity[luminosity];
            ++result.Histogram.Rgb[red];
            ++result.Histogram.Rgb[green];
            ++result.Histogram.Rgb[blue];
        }
    }

    for (uint64_t& bucket : result.Histogram.Rgb)
        bucket /= 3u;

    for (size_t i = 0; i < result.Histogram.Luminosity.size(); ++i)
    {
        result.Histogram.MaxBucketCount = std::max(result.Histogram.MaxBucketCount, result.Histogram.Luminosity[i]);
        result.Histogram.MaxBucketCount = std::max(result.Histogram.MaxBucketCount, result.Histogram.Red[i]);
        result.Histogram.MaxBucketCount = std::max(result.Histogram.MaxBucketCount, result.Histogram.Green[i]);
        result.Histogram.MaxBucketCount = std::max(result.Histogram.MaxBucketCount, result.Histogram.Blue[i]);
        result.Histogram.MaxBucketCount = std::max(result.Histogram.MaxBucketCount, result.Histogram.Rgb[i]);
    }

    result.Error = ImageError::Ok();
    return result;
}

ImageEngine::ImageEngine()
    : m_comApartment(std::make_unique<ComApartment>())
{
    HRESULT hr = m_comApartment->Result();
    if (FAILED(hr))
    {
        m_initializationError = MakeError(ImageErrorCode::ComInitializationFailed, hr, L"Unable to initialize COM for WIC.");
        return;
    }

    hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                          IID_PPV_ARGS(&m_factory));
    if (FAILED(hr))
    {
        m_initializationError = MakeError(ImageErrorCode::WicFactoryFailed, hr, L"Unable to create WIC imaging factory.");
        return;
    }

    m_initializationError = ImageError::Ok();
}

ImageEngine::~ImageEngine() = default;

bool ImageEngine::IsAvailable() const
{
    return m_initializationError.Succeeded() && m_factory != nullptr;
}

bool UnpackPaletteIndices(const uint8_t* packed, uint32_t stride, uint32_t width, uint32_t height, uint32_t bitsPerPixel,
                          std::vector<uint8_t>& indices)
{
    indices.clear();
    if (packed == nullptr || (bitsPerPixel != 1 && bitsPerPixel != 2 && bitsPerPixel != 4 && bitsPerPixel != 8) ||
        static_cast<uint64_t>(stride) * 8 < static_cast<uint64_t>(width) * bitsPerPixel)
        return false;
    indices.resize(static_cast<size_t>(width) * height);
    const unsigned mask = (1u << bitsPerPixel) - 1u;
    for (uint32_t y = 0; y < height; ++y)
    {
        const uint8_t* row = packed + static_cast<size_t>(stride) * y;
        uint8_t* out = indices.data() + static_cast<size_t>(width) * y;
        for (uint32_t x = 0; x < width; ++x)
        {
            const uint64_t bit = static_cast<uint64_t>(x) * bitsPerPixel;
            const unsigned shift = 8u - bitsPerPixel - static_cast<unsigned>(bit % 8);
            out[x] = static_cast<uint8_t>((row[bit / 8] >> shift) & mask);
        }
    }
    return true;
}

uint32_t DecodeBandRows(uint32_t stride, uint32_t height)
{
    constexpr uint64_t BandBytes = 8ull * 1024ull * 1024ull;
    if (stride == 0)
        return std::max(1u, height);
    const uint64_t rows = std::max<uint64_t>(1, BandBytes / stride);
    return static_cast<uint32_t>(std::min<uint64_t>(rows, std::max(1u, height)));
}

std::wstring DecodeCommentBytes(const char* bytes)
{
    std::wstring text;
    if (bytes == nullptr)
        return text;
    const unsigned char* const begin = reinterpret_cast<const unsigned char*>(bytes);
    const size_t length = strnlen(bytes, 1u << 20);

    // Strict UTF-8 (no overlong forms, surrogates or values past U+10FFFF).
    bool utf8 = true;
    for (size_t i = 0; i < length && utf8;)
    {
        const unsigned lead = begin[i];
        size_t count = 0;
        uint32_t code = 0;
        uint32_t minimum = 0;
        if (lead < 0x80)
            count = 0, code = lead;
        else if (lead >= 0xC2 && lead <= 0xDF)
            count = 1, code = lead & 0x1F, minimum = 0x80;
        else if (lead >= 0xE0 && lead <= 0xEF)
            count = 2, code = lead & 0x0F, minimum = 0x800;
        else if (lead >= 0xF0 && lead <= 0xF4)
            count = 3, code = lead & 0x07, minimum = 0x10000;
        else
        {
            utf8 = false;
            break;
        }
        for (size_t k = 1; k <= count; ++k)
        {
            if (i + k >= length || (begin[i + k] & 0xC0) != 0x80)
            {
                utf8 = false;
                break;
            }
            code = (code << 6) | (begin[i + k] & 0x3F);
        }
        if (!utf8 || code < minimum || code > 0x10FFFF || (code >= 0xD800 && code <= 0xDFFF))
        {
            utf8 = false;
            break;
        }
        if (code >= 0x10000)
        {
            code -= 0x10000;
            text.push_back(static_cast<wchar_t>(0xD800 + (code >> 10)));
            text.push_back(static_cast<wchar_t>(0xDC00 + (code & 0x3FF)));
        }
        else
        {
            text.push_back(static_cast<wchar_t>(code));
        }
        i += count + 1;
    }
    if (utf8)
        return text;

    // ISO 8859-1: each byte is its own code point.
    text.assign(begin, begin + length);
    return text;
}

std::vector<std::wstring> ImageEngine::DecoderFileExtensions() const
{
    std::vector<std::wstring> lists;
    if (!m_factory)
        return lists;
    Microsoft::WRL::ComPtr<IEnumUnknown> components;
    if (FAILED(m_factory->CreateComponentEnumerator(WICDecoder, WICComponentEnumerateDefault, &components)) || !components)
        return lists;
    Microsoft::WRL::ComPtr<IUnknown> component;
    ULONG fetched = 0;
    while (components->Next(1, &component, &fetched) == S_OK && fetched == 1)
    {
        Microsoft::WRL::ComPtr<IWICBitmapCodecInfo> codec;
        UINT length = 0;
        if (SUCCEEDED(component.As(&codec)) && SUCCEEDED(codec->GetFileExtensions(0, nullptr, &length)) && length > 0)
        {
            std::wstring extensions(length, L'\0');
            if (SUCCEEDED(codec->GetFileExtensions(length, extensions.data(), &length)))
            {
                extensions.resize(wcsnlen(extensions.c_str(), extensions.size()));
                lists.push_back(std::move(extensions));
            }
        }
        component.Reset();
    }
    return lists;
}

bool ResetAnimationCanvas(AnimationCanvas& canvas, uint32_t width, uint32_t height)
{
    canvas = AnimationCanvas();
    if (width == 0 || height == 0)
        return false;
    try
    {
        canvas.Surface.Width = width;
        canvas.Surface.Height = height;
        canvas.Surface.Stride = width * 4u;
        canvas.Surface.Pixels.assign(static_cast<size_t>(canvas.Surface.Stride) * height, 0);
    }
    catch (const std::bad_alloc&)
    {
        canvas = AnimationCanvas();
        return false;
    }
    return true;
}

namespace
{

RECT ClipToCanvas(const ImageSurface& canvas, int left, int top, uint32_t width, uint32_t height)
{
    RECT rect = {};
    rect.left = (std::max)(0, left);
    rect.top = (std::max)(0, top);
    rect.right = static_cast<LONG>((std::min)(static_cast<long long>(canvas.Width), static_cast<long long>(left) + width));
    rect.bottom = static_cast<LONG>((std::min)(static_cast<long long>(canvas.Height), static_cast<long long>(top) + height));
    if (rect.right < rect.left)
        rect.right = rect.left;
    if (rect.bottom < rect.top)
        rect.bottom = rect.top;
    return rect;
}

} // namespace

void ComposeAnimationFrame(AnimationCanvas& canvas, const ImageSurface& frame, int left, int top, uint8_t disposal)
{
    ImageSurface& target = canvas.Surface;
    if (target.Pixels.empty())
        return;

    // The previous frame's disposal first.
    if (canvas.HasFrame && (canvas.LastDisposal == 2 || canvas.LastDisposal == 3))
    {
        const RECT& r = canvas.LastFrameRect;
        const bool restore = canvas.LastDisposal == 3 && canvas.BeforeLastFrame.Pixels.size() == target.Pixels.size();
        for (LONG y = r.top; y < r.bottom; ++y)
        {
            uint8_t* row = target.Pixels.data() + static_cast<size_t>(y) * target.Stride + static_cast<size_t>(r.left) * 4u;
            const size_t bytes = static_cast<size_t>(r.right - r.left) * 4u;
            if (restore)
                memcpy(row, canvas.BeforeLastFrame.Pixels.data() + (row - target.Pixels.data()), bytes);
            else
                memset(row, 0, bytes);
        }
    }

    canvas.BeforeLastFrame = disposal == 3 ? target : ImageSurface();

    const RECT r = ClipToCanvas(target, left, top, frame.Width, frame.Height);
    for (LONG y = r.top; y < r.bottom; ++y)
    {
        const uint8_t* source = frame.Pixels.data() + static_cast<size_t>(y - top) * frame.Stride + static_cast<size_t>(r.left - left) * 4u;
        uint8_t* destination = target.Pixels.data() + static_cast<size_t>(y) * target.Stride + static_cast<size_t>(r.left) * 4u;
        for (LONG x = r.left; x < r.right; ++x, source += 4, destination += 4)
        {
            const uint8_t alpha = source[3];
            if (alpha == 255)
            {
                memcpy(destination, source, 4);
            }
            else if (alpha != 0)
            {
                for (int c = 0; c < 3; ++c)
                    destination[c] = static_cast<uint8_t>((source[c] * alpha + destination[c] * (255 - alpha)) / 255);
                destination[3] = static_cast<uint8_t>(alpha + destination[3] * (255 - alpha) / 255);
            }
        }
    }

    canvas.LastFrameRect = r;
    canvas.LastDisposal = disposal;
    canvas.HasFrame = true;
}

ImageError ComposeDocumentFrame(const ImageDocument& document, uint32_t index, AnimationCanvas& canvas)
{
    const ImageMetadata& metadata = document.Metadata();
    if (index >= metadata.Frames.size())
        return MakeError(ImageErrorCode::FrameOutOfRange, E_INVALIDARG, L"Requested image frame does not exist.");

    if (index == 0 || canvas.Surface.Pixels.empty())
    {
        const uint32_t width = metadata.CanvasWidth != 0 ? metadata.CanvasWidth : metadata.Frames[0].Width;
        const uint32_t height = metadata.CanvasHeight != 0 ? metadata.CanvasHeight : metadata.Frames[0].Height;
        if (!ResetAnimationCanvas(canvas, width, height))
            return MakeError(ImageErrorCode::OutOfMemory, E_OUTOFMEMORY, L"Unable to allocate the animation canvas.");
    }

    DecodeOptions options;
    options.ApplyOrientation = false;
    DecodeFrameResult decoded = document.DecodeFrame(index, options);
    if (!decoded.Succeeded())
        return decoded.Error;
    const ImageFrameInfo& info = metadata.Frames[index];
    ComposeAnimationFrame(canvas, decoded.Surface, static_cast<int>(info.OffsetLeft), static_cast<int>(info.OffsetTop),
                          info.HasGifDisposal ? info.GifDisposal : 0);
    return ImageError::Ok();
}

OpenDocumentResult ImageEngine::OpenPath(const wchar_t* path,
                                         const CancellationToken& cancellation) const
{
    OpenDocumentResult result;
    if (path == nullptr || path[0] == L'\0')
    {
        result.Error = MakeError(ImageErrorCode::InvalidArgument, E_INVALIDARG, L"Image path is empty.");
        return result;
    }

    if (!IsAvailable())
    {
        result.Error = m_initializationError;
        return result;
    }

    if (IsCanceled(cancellation))
    {
        result.Error = MakeError(ImageErrorCode::Canceled, HRESULT_FROM_WIN32(ERROR_CANCELLED), L"Image open was canceled.");
        return result;
    }

    Microsoft::WRL::ComPtr<IWICBitmapDecoder> decoder;
    HRESULT hr = m_factory->CreateDecoderFromFilename(path, nullptr, GENERIC_READ,
                                                      WICDecodeMetadataCacheOnDemand,
                                                      &decoder);
    if (FAILED(hr))
    {
        result.Error = MakeWicError(hr, L"Unable to open image with WIC.");
        return result;
    }

    return OpenDecoder(decoder, cancellation);
}

OpenDocumentResult ImageEngine::OpenStream(IStream* stream,
                                           const CancellationToken& cancellation) const
{
    OpenDocumentResult result;
    if (stream == nullptr)
    {
        result.Error = MakeError(ImageErrorCode::InvalidArgument, E_INVALIDARG, L"Image stream is empty.");
        return result;
    }

    if (!IsAvailable())
    {
        result.Error = m_initializationError;
        return result;
    }

    if (IsCanceled(cancellation))
    {
        result.Error = MakeError(ImageErrorCode::Canceled, HRESULT_FROM_WIN32(ERROR_CANCELLED), L"Image open was canceled.");
        return result;
    }

    Microsoft::WRL::ComPtr<IWICBitmapDecoder> decoder;
    HRESULT hr = m_factory->CreateDecoderFromStream(stream, nullptr,
                                                    WICDecodeMetadataCacheOnDemand,
                                                    &decoder);
    if (FAILED(hr))
    {
        result.Error = MakeWicError(hr, L"Unable to open image stream with WIC.");
        return result;
    }

    return OpenDecoder(decoder, cancellation);
}

OpenDocumentResult ImageEngine::OpenDecoder(Microsoft::WRL::ComPtr<IWICBitmapDecoder> decoder,
                                            const CancellationToken& cancellation) const
{
    OpenDocumentResult result;
    if (IsCanceled(cancellation))
    {
        result.Error = MakeError(ImageErrorCode::Canceled, HRESULT_FROM_WIN32(ERROR_CANCELLED), L"Image open was canceled.");
        return result;
    }

    ImageMetadata metadata;
    HRESULT hr = decoder->GetContainerFormat(&metadata.ContainerFormat);
    if (FAILED(hr))
    {
        result.Error = MakeError(ImageErrorCode::MetadataFailed, hr, L"Unable to read image container format.");
        return result;
    }
    GetFriendlyDecoderName(decoder.Get(), metadata.ContainerName);

    UINT frameCount = 0;
    hr = decoder->GetFrameCount(&frameCount);
    if (FAILED(hr) || frameCount == 0)
    {
        result.Error = MakeError(ImageErrorCode::MetadataFailed, FAILED(hr) ? hr : WINCODEC_ERR_BADIMAGE, L"Unable to read image frame count.");
        return result;
    }

    try
    {
        metadata.Frames.resize(frameCount);
    }
    catch (const std::bad_alloc&)
    {
        result.Error = MakeError(ImageErrorCode::OutOfMemory, E_OUTOFMEMORY, L"Unable to allocate image metadata.");
        return result;
    }

    for (UINT i = 0; i < frameCount; ++i)
    {
        if (IsCanceled(cancellation))
        {
            result.Error = MakeError(ImageErrorCode::Canceled, HRESULT_FROM_WIN32(ERROR_CANCELLED), L"Image open was canceled.");
            return result;
        }

        result.Error = ProbeFrame(decoder.Get(),
                                  metadata.ContainerFormat,
                                  i,
                                  metadata.Frames[i],
                                  i == 0 ? &metadata.Details : nullptr,
                                  &metadata.Frames[i].Exif);
        if (!result.Error.Succeeded())
            return result;
        DescribePixelFormat(m_factory.Get(), metadata.Frames[i]);
    }
    if (IsEqualGUID(metadata.ContainerFormat, GUID_ContainerFormatGif))
        ProbeGifCanvas(decoder.Get(), metadata);
    metadata.Exif = metadata.Frames[0].Exif; // the first page's, for callers that show one set

    result.Document.reset(new ImageDocument(m_factory, decoder, std::move(metadata)));
    result.Error = ImageError::Ok();
    return result;
}

SaveImageResult ImageEngine::SaveSurfaceToPath(const ImageSurface& surface,
                                               const wchar_t* path,
                                               const SaveOptions& options,
                                               const CancellationToken& cancellation) const
{
    SaveImageResult result;
    if (path == nullptr || path[0] == L'\0')
    {
        result.Error = MakeError(ImageErrorCode::InvalidArgument, E_INVALIDARG, L"Output image path is empty.");
        return result;
    }

    if (!IsAvailable())
    {
        result.Error = m_initializationError;
        return result;
    }

    if (IsCanceled(cancellation))
    {
        result.Error = MakeError(ImageErrorCode::Canceled, HRESULT_FROM_WIN32(ERROR_CANCELLED), L"Image save was canceled.");
        return result;
    }

    result.Error = ValidateSaveSurface(surface);
    if (!result.Error.Succeeded())
        return result;

    GUID containerFormat = GUID_NULL;
    WICPixelFormatGUID targetPixelFormat = GUID_NULL;
    if (!GetEncoderSettings(options.Format, containerFormat, targetPixelFormat))
    {
        result.Error = MakeError(ImageErrorCode::UnsupportedFormat, E_INVALIDARG, L"Requested image save format is unsupported.");
        return result;
    }

    Microsoft::WRL::ComPtr<IWICStream> stream;
    HRESULT hr = m_factory->CreateStream(&stream);
    if (FAILED(hr))
    {
        result.Error = MakeSaveError(hr, L"Unable to create WIC output stream.");
        return result;
    }

    hr = stream->InitializeFromFilename(path, GENERIC_WRITE);
    if (FAILED(hr))
    {
        result.Error = MakeSaveError(hr, L"Unable to open output image file.");
        return result;
    }

    return EncodeSurface(surface, stream.Get(), options, cancellation, containerFormat, targetPixelFormat);
}

SaveImageResult ImageEngine::SaveSurfaceToBytes(const ImageSurface& surface, std::vector<uint8_t>& bytes,
                                                const SaveOptions& options) const
{
    bytes.clear();
    SaveImageResult result;
    if (!IsAvailable())
    {
        result.Error = m_initializationError;
        return result;
    }
    result.Error = ValidateSaveSurface(surface);
    if (!result.Error.Succeeded())
        return result;
    GUID containerFormat = GUID_NULL;
    WICPixelFormatGUID targetPixelFormat = GUID_NULL;
    if (!GetEncoderSettings(options.Format, containerFormat, targetPixelFormat))
    {
        result.Error = MakeError(ImageErrorCode::UnsupportedFormat, E_INVALIDARG, L"Requested image save format is unsupported.");
        return result;
    }

    Microsoft::WRL::ComPtr<IStream> stream;
    HRESULT hr = CreateStreamOnHGlobal(nullptr, TRUE, &stream);
    if (FAILED(hr))
    {
        result.Error = MakeSaveError(hr, L"Unable to create a memory stream.");
        return result;
    }
    result = EncodeSurface(surface, stream.Get(), options, CancellationToken(), containerFormat, targetPixelFormat);
    if (!result.Succeeded())
        return result;

    HGLOBAL memory = nullptr;
    STATSTG stat = {};
    if (FAILED(stream->Stat(&stat, STATFLAG_NONAME)) || FAILED(GetHGlobalFromStream(stream.Get(), &memory)) ||
        stat.cbSize.QuadPart > SIZE_MAX)
    {
        result.Error = MakeSaveError(E_FAIL, L"Unable to read the encoded image.");
        return result;
    }
    const void* data = GlobalLock(memory);
    if (data == nullptr)
    {
        result.Error = MakeSaveError(E_OUTOFMEMORY, L"Unable to read the encoded image.");
        return result;
    }
    const uint8_t* begin = static_cast<const uint8_t*>(data);
    bytes.assign(begin, begin + static_cast<size_t>(stat.cbSize.QuadPart));
    GlobalUnlock(memory);
    return result;
}

SaveImageResult ImageEngine::EncodeSurface(const ImageSurface& surface, IStream* stream, const SaveOptions& options,
                                           const CancellationToken& cancellation, const GUID& containerFormat,
                                           const GUID& targetPixelFormat) const
{
    SaveImageResult result;
    Microsoft::WRL::ComPtr<IWICBitmapEncoder> encoder;
    HRESULT hr = m_factory->CreateEncoder(containerFormat, nullptr, &encoder);
    if (FAILED(hr))
    {
        result.Error = MakeSaveError(hr, L"Unable to create WIC encoder.");
        return result;
    }

    hr = encoder->Initialize(stream, WICBitmapEncoderNoCache);
    if (FAILED(hr))
    {
        result.Error = MakeSaveError(hr, L"Unable to initialize WIC encoder.");
        return result;
    }

    Microsoft::WRL::ComPtr<IWICBitmapFrameEncode> frame;
    Microsoft::WRL::ComPtr<IPropertyBag2> properties;
    hr = encoder->CreateNewFrame(&frame, &properties);
    if (FAILED(hr))
    {
        result.Error = MakeSaveError(hr, L"Unable to create WIC encoder frame.");
        return result;
    }

    hr = ConfigureEncoderProperties(properties.Get(), options);
    if (FAILED(hr))
    {
        result.Error = MakeSaveError(hr, L"Unable to configure WIC encoder options.");
        return result;
    }

    hr = frame->Initialize(properties.Get());
    if (FAILED(hr))
    {
        result.Error = MakeSaveError(hr, L"Unable to initialize WIC encoder frame.");
        return result;
    }

    hr = frame->SetSize(surface.Width, surface.Height);
    if (FAILED(hr))
    {
        result.Error = MakeSaveError(hr, L"Unable to set output image dimensions.");
        return result;
    }

    if (options.DpiX > 0.0 && options.DpiY > 0.0)
        frame->SetResolution(options.DpiX, options.DpiY);

    const uint64_t sourceBytes = static_cast<uint64_t>(surface.Stride) * surface.Height;
    Microsoft::WRL::ComPtr<IWICBitmap> bitmap;
    hr = m_factory->CreateBitmapFromMemory(surface.Width,
                                           surface.Height,
                                           GUID_WICPixelFormat32bppBGRA,
                                           surface.Stride,
                                           static_cast<UINT>(sourceBytes),
                                           const_cast<BYTE*>(surface.Pixels.data()),
                                           &bitmap);
    if (FAILED(hr))
    {
        result.Error = MakeSaveError(hr, L"Unable to create WIC bitmap from decoded image surface.");
        return result;
    }

    WICPixelFormatGUID actualPixelFormat = targetPixelFormat;
    hr = frame->SetPixelFormat(&actualPixelFormat);
    if (FAILED(hr))
    {
        result.Error = MakeSaveError(hr, L"Unable to set output image pixel format.");
        return result;
    }

    Microsoft::WRL::ComPtr<IWICPalette> palette;
    if (options.Format == ImageSaveFormat::Gif)
    {
        hr = m_factory->CreatePalette(&palette);
        if (FAILED(hr))
        {
            result.Error = MakeSaveError(hr, L"Unable to create WIC GIF palette.");
            return result;
        }

        hr = palette->InitializeFromBitmap(bitmap.Get(), 256, FALSE);
        if (FAILED(hr))
        {
            result.Error = MakeSaveError(hr, L"Unable to build WIC GIF palette.");
            return result;
        }

        hr = frame->SetPalette(palette.Get());
        if (FAILED(hr))
        {
            result.Error = MakeSaveError(hr, L"Unable to set WIC GIF palette.");
            return result;
        }
    }

    if (IsCanceled(cancellation))
    {
        result.Error = MakeError(ImageErrorCode::Canceled, HRESULT_FROM_WIN32(ERROR_CANCELLED), L"Image save was canceled.");
        return result;
    }

    Microsoft::WRL::ComPtr<IWICBitmapSource> source;
    hr = bitmap.As(&source);
    if (FAILED(hr))
    {
        result.Error = MakeSaveError(hr, L"Unable to use decoded image surface as a WIC bitmap source.");
        return result;
    }

    Microsoft::WRL::ComPtr<IWICFormatConverter> converter;
    if (!IsEqualGUID(actualPixelFormat, GUID_WICPixelFormat32bppBGRA))
    {
        hr = m_factory->CreateFormatConverter(&converter);
        if (FAILED(hr))
        {
            result.Error = MakeSaveError(hr, L"Unable to create WIC save format converter.");
            return result;
        }

        hr = converter->Initialize(bitmap.Get(),
                                   actualPixelFormat,
                                   options.Format == ImageSaveFormat::Gif ? WICBitmapDitherTypeSolid : WICBitmapDitherTypeNone,
                                   palette.Get(),
                                   0.0,
                                   WICBitmapPaletteTypeCustom);
        if (FAILED(hr))
        {
            result.Error = MakeSaveError(hr, L"Unable to convert image surface for WIC encoder.");
            return result;
        }
        hr = converter.As(&source);
        if (FAILED(hr))
        {
            result.Error = MakeSaveError(hr, L"Unable to use converted image surface as a WIC bitmap source.");
            return result;
        }
    }

    hr = frame->WriteSource(source.Get(), nullptr);
    if (FAILED(hr))
    {
        result.Error = MakeSaveError(hr, L"Unable to write encoded image pixels.");
        return result;
    }

    hr = frame->Commit();
    if (FAILED(hr))
    {
        result.Error = MakeSaveError(hr, L"Unable to commit WIC encoder frame.");
        return result;
    }

    hr = encoder->Commit();
    if (FAILED(hr))
    {
        result.Error = MakeSaveError(hr, L"Unable to commit WIC encoder.");
        return result;
    }

    result.Error = ImageError::Ok();
    return result;
}

} // namespace pictview
