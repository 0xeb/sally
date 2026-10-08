// SPDX-FileCopyrightText: 2025-2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#include "thumbnail_update.h"

#include <windows.h>
#include <wrl/client.h>

#include <set>

#include "engine/wic_engine.h"
#include "jpeg_thumbnail.h"
#include "viewer.h"

namespace pictview
{
namespace
{

constexpr uint64_t MaxJpegFileBytes = 1024ull * 1024ull * 1024ull;

std::wstring SystemErrorText(DWORD error)
{
    wchar_t* buffer = nullptr;
    const DWORD length = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                                        nullptr, error, 0, reinterpret_cast<wchar_t*>(&buffer), 0, nullptr);
    std::wstring text = length > 0 && buffer != nullptr ? std::wstring(buffer, length) : std::wstring();
    if (buffer != nullptr)
        LocalFree(buffer);
    while (!text.empty() && (text.back() == L'\r' || text.back() == L'\n' || text.back() == L' '))
        text.pop_back();
    return text;
}

bool ReadWholeFile(const std::wstring& path, std::vector<uint8_t>& bytes, DWORD& error)
{
    bytes.clear();
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
    {
        error = GetLastError();
        return false;
    }
    LARGE_INTEGER size = {};
    bool ok = GetFileSizeEx(file, &size) != FALSE && size.QuadPart >= 0 && static_cast<uint64_t>(size.QuadPart) <= MaxJpegFileBytes;
    error = ok ? ERROR_SUCCESS : ERROR_FILE_TOO_LARGE;
    if (ok)
    {
        try
        {
            bytes.resize(static_cast<size_t>(size.QuadPart));
        }
        catch (const std::bad_alloc&)
        {
            ok = false;
            error = ERROR_NOT_ENOUGH_MEMORY;
        }
    }
    size_t done = 0;
    while (ok && done < bytes.size())
    {
        const DWORD chunk = static_cast<DWORD>(std::min<size_t>(bytes.size() - done, 1u << 24));
        DWORD read = 0;
        if (!ReadFile(file, bytes.data() + done, chunk, &read, nullptr) || read == 0)
        {
            error = read == 0 && GetLastError() == ERROR_SUCCESS ? ERROR_HANDLE_EOF : GetLastError();
            ok = false;
        }
        done += read;
    }
    CloseHandle(file);
    return ok;
}

// A new file next to 'path' holding 'bytes'; its name is returned in 'tempPath'.
bool WriteTempFile(const std::wstring& path, const std::vector<uint8_t>& bytes, std::wstring& tempPath, DWORD& error)
{
    HANDLE file = INVALID_HANDLE_VALUE;
    for (unsigned attempt = 0; attempt < 16 && file == INVALID_HANDLE_VALUE; ++attempt)
    {
        wchar_t suffix[32];
        swprintf_s(suffix, L".pv%08lx.tmp", static_cast<unsigned long>(GetTickCount64() * 2654435761ull + attempt + GetCurrentProcessId()));
        tempPath = path + suffix;
        file = CreateFileW(tempPath.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        error = file == INVALID_HANDLE_VALUE ? GetLastError() : ERROR_SUCCESS;
        if (file == INVALID_HANDLE_VALUE && error != ERROR_FILE_EXISTS && error != ERROR_ALREADY_EXISTS)
            return false;
    }
    if (file == INVALID_HANDLE_VALUE)
        return false;
    size_t done = 0;
    bool ok = true;
    while (ok && done < bytes.size())
    {
        const DWORD chunk = static_cast<DWORD>(std::min<size_t>(bytes.size() - done, 1u << 24));
        DWORD written = 0;
        ok = WriteFile(file, bytes.data() + done, chunk, &written, nullptr) != FALSE && written == chunk;
        done += written;
    }
    ok = ok && FlushFileBuffers(file) != FALSE;
    error = ok ? ERROR_SUCCESS : GetLastError();
    CloseHandle(file);
    if (!ok)
        DeleteFileW(tempPath.c_str());
    return ok;
}

// Swaps the temporary file in. On ERROR_UNABLE_TO_MOVE_REPLACEMENT_2 Windows has already removed
// the original name, so the new content is moved there instead of being thrown away.
bool SwapInTempFile(const std::wstring& path, const std::wstring& tempPath, DWORD& error)
{
    if (ReplaceFileW(path.c_str(), tempPath.c_str(), nullptr, REPLACEFILE_IGNORE_MERGE_ERRORS | REPLACEFILE_IGNORE_ACL_ERRORS,
                     nullptr, nullptr))
        return true;
    error = GetLastError();
    if (error == ERROR_UNABLE_TO_MOVE_REPLACEMENT_2 && MoveFileExW(tempPath.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING))
        return true;
    return false;
}

std::wstring DirectoryOf(const std::wstring& path)
{
    const size_t slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? std::wstring() : path.substr(0, slash);
}

Microsoft::WRL::ComPtr<IStream> MemoryStream(const std::vector<uint8_t>& bytes)
{
    Microsoft::WRL::ComPtr<IStream> stream;
    if (FAILED(CreateStreamOnHGlobal(nullptr, TRUE, &stream)) || bytes.size() > ULONG_MAX)
        return nullptr;
    ULONG written = 0;
    if (FAILED(stream->Write(bytes.data(), static_cast<ULONG>(bytes.size()), &written)) || written != bytes.size())
        return nullptr;
    const LARGE_INTEGER start = {};
    stream->Seek(start, STREAM_SEEK_SET, nullptr);
    return stream;
}

// What the user decided for the files still to come.
struct Decisions
{
    bool SkipOpenFailures = false;
    bool SkipNotJpeg = false;
    bool CreateJfxx = false;
    bool SkipNoExif = false;
    bool ModifyReadOnly = false;
    bool SkipReadOnly = false;
    bool SkipWriteFailures = false;
};

// Skip / Skip all / Cancel: true to stop everything.
bool SkipOrCancel(ThumbnailUpdateAnswer answer, bool& skipAll)
{
    if (answer == ThumbnailUpdateAnswer::SkipAll)
        skipAll = true;
    return answer == ThumbnailUpdateAnswer::Cancel;
}

} // namespace

std::vector<uint8_t> MakeJpegThumbnail(const ImageEngine& engine, const std::vector<uint8_t>& file, bool& isJpeg,
                                       std::wstring& error)
{
    isJpeg = false;
    error.clear();
    Microsoft::WRL::ComPtr<IStream> stream = MemoryStream(file);
    if (stream == nullptr)
    {
        error = SystemErrorText(ERROR_NOT_ENOUGH_MEMORY);
        return {};
    }
    OpenDocumentResult open = engine.OpenStream(stream.Get());
    if (!open.Succeeded())
    {
        error = ImageErrorMessage(open.Error);
        return {};
    }
    isJpeg = IsEqualGUID(open.Document->Metadata().ContainerFormat, GUID_ContainerFormatJpeg) != FALSE;
    if (!isJpeg)
        return {};

    ThumbnailDecodeOptions thumbnailOptions;
    thumbnailOptions.UseEmbeddedThumbnailSources = false; // the stored one is what gets replaced
    DecodeOptions decodeOptions;
    decodeOptions.ApplyOrientation = false; // EXIF orientation applies to the thumbnail as well
    DecodeFrameResult decoded = open.Document->DecodeFrameThumbnail(0, UpdatedThumbnailWidth, UpdatedThumbnailHeight,
                                                                    thumbnailOptions, decodeOptions);
    if (!decoded.Succeeded())
    {
        error = ImageErrorMessage(decoded.Error);
        return {};
    }
    SaveOptions saveOptions;
    saveOptions.Format = ImageSaveFormat::Jpeg;
    saveOptions.JpegQuality = 0.85f;
    saveOptions.DpiX = saveOptions.DpiY = 72.0;
    std::vector<uint8_t> encoded;
    SaveImageResult saved = engine.SaveSurfaceToBytes(decoded.Surface, encoded, saveOptions);
    std::vector<uint8_t> thumbnail;
    if (!saved.Succeeded() || !StripJpegApplicationSegments(encoded, thumbnail))
    {
        error = ImageErrorMessage(saved.Error);
        return {};
    }
    return thumbnail;
}

ThumbnailUpdateSummary UpdateJpegThumbnails(const std::vector<std::wstring>& paths, const ThumbnailUpdateHost& host)
{
    ThumbnailUpdateSummary summary;
    summary.Total = paths.size();
    ImageEngine engine;
    Decisions decided;
    std::set<std::wstring> changedDirectories;
    auto ask = [&host](ThumbnailUpdateQuestion question, const std::wstring& path, const std::wstring& detail) {
        return host.Ask ? host.Ask(question, path, detail) : ThumbnailUpdateAnswer::Skip;
    };

    for (size_t index = 0; index < paths.size() && !summary.Canceled; ++index)
    {
        const std::wstring& path = paths[index];
        if (host.Progress && !host.Progress(index, paths.size(), path))
        {
            summary.Canceled = true;
            break;
        }

        std::vector<uint8_t> file;
        DWORD error = ERROR_SUCCESS;
        std::wstring detail;
        bool isJpeg = false;
        std::vector<uint8_t> thumbnail;
        if (!ReadWholeFile(path, file, error))
            detail = SystemErrorText(error);
        else
            thumbnail = MakeJpegThumbnail(engine, file, isJpeg, detail);
        if (!isJpeg && detail.empty())
        {
            if (!decided.SkipNotJpeg)
                summary.Canceled = SkipOrCancel(ask(ThumbnailUpdateQuestion::NotJpeg, path, L""), decided.SkipNotJpeg);
            continue;
        }
        if (thumbnail.empty())
        {
            if (!decided.SkipOpenFailures)
                summary.Canceled = SkipOrCancel(ask(ThumbnailUpdateQuestion::OpenFailed, path, detail), decided.SkipOpenFailures);
            continue;
        }

        std::vector<uint8_t> rewritten;
        JpegRewriteStatus status = JpegRewriteStatus::NotJpeg;
        const JpegThumbnailKind kind = ClassifyJpegForThumbnail(file);
        if (kind == JpegThumbnailKind::Exif)
        {
            status = ReplaceExifThumbnail(file, thumbnail, rewritten);
        }
        else if (kind == JpegThumbnailKind::NoExif)
        {
            if (decided.SkipNoExif)
                continue;
            if (!decided.CreateJfxx)
            {
                const ThumbnailUpdateAnswer answer = ask(ThumbnailUpdateQuestion::NoExif, path, L"");
                if (answer == ThumbnailUpdateAnswer::Cancel)
                {
                    summary.Canceled = true;
                    continue;
                }
                if (answer == ThumbnailUpdateAnswer::SkipAll)
                    decided.SkipNoExif = true;
                if (answer == ThumbnailUpdateAnswer::All)
                    decided.CreateJfxx = true;
                if (answer != ThumbnailUpdateAnswer::Yes && answer != ThumbnailUpdateAnswer::All)
                    continue;
            }
            status = InsertJfxxThumbnail(file, thumbnail, rewritten);
        }
        if (status != JpegRewriteStatus::Ok)
        {
            if (!decided.SkipWriteFailures)
                summary.Canceled = SkipOrCancel(ask(ThumbnailUpdateQuestion::WriteFailed, path, L""), decided.SkipWriteFailures);
            continue;
        }

        std::wstring tempPath;
        if (!WriteTempFile(path, rewritten, tempPath, error))
        {
            if (!decided.SkipWriteFailures)
                summary.Canceled = SkipOrCancel(ask(ThumbnailUpdateQuestion::WriteFailed, path, SystemErrorText(error)),
                                                decided.SkipWriteFailures);
            continue;
        }

        bool swapped = SwapInTempFile(path, tempPath, error);
        const DWORD attributes = GetFileAttributesW(path.c_str());
        if (!swapped && attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_READONLY) != 0)
        {
            bool modify = decided.ModifyReadOnly;
            if (!modify && !decided.SkipReadOnly)
            {
                const ThumbnailUpdateAnswer answer = ask(ThumbnailUpdateQuestion::ReadOnly, path, L"");
                summary.Canceled = answer == ThumbnailUpdateAnswer::Cancel;
                decided.ModifyReadOnly = answer == ThumbnailUpdateAnswer::All;
                decided.SkipReadOnly = answer == ThumbnailUpdateAnswer::SkipAll;
                modify = answer == ThumbnailUpdateAnswer::Yes || answer == ThumbnailUpdateAnswer::All;
            }
            if (modify && SetFileAttributesW(path.c_str(), attributes & ~FILE_ATTRIBUTE_READONLY))
            {
                swapped = SwapInTempFile(path, tempPath, error);
                SetFileAttributesW(path.c_str(), GetFileAttributesW(path.c_str()) | FILE_ATTRIBUTE_READONLY); // stays read-only
            }
            if (!modify)
            {
                DeleteFileW(tempPath.c_str());
                continue;
            }
        }
        if (!swapped)
        {
            if (GetFileAttributesW(tempPath.c_str()) != INVALID_FILE_ATTRIBUTES)
                DeleteFileW(tempPath.c_str());
            if (!decided.SkipWriteFailures && !summary.Canceled)
                summary.Canceled = SkipOrCancel(ask(ThumbnailUpdateQuestion::WriteFailed, path, SystemErrorText(error)),
                                                decided.SkipWriteFailures);
            continue;
        }
        ++summary.Updated;
        changedDirectories.insert(DirectoryOf(path));
    }

    if (host.PathChanged)
    {
        for (const std::wstring& directory : changedDirectories)
            host.PathChanged(directory);
    }
    return summary;
}

} // namespace pictview
