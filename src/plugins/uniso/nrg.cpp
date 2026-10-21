// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "nrg_tracks.h"
#include "precomp.h"
#include "dbg.h"

#include "isoimage.h"
#include "uniso_text.h"
#include "audio.h"

#include "uniso.h"
#include "uniso.rh"
#include "uniso.rh2"
#include "lang\lang.rh"

// Nero Header
// for reading information about sessions/tracks in .NRG
struct NRGHeader
{
    char ID[4];
    DWORD Size;
};

struct DAOX
{
    DWORD Size;
    BYTE Data[0x10];
    BYTE FirstTrack;
    BYTE LastTrack;
};

// swaping macros

#define SWAPWORD(d) \
    (((d) & 0x00FF) << 8) | \
        (((d) & 0xFF00) >> 8)

#define SWAPDWORD(d) \
    (((d) & 0x000000FF) << 24) | \
        (((d) & 0x0000FF00) << 8) | \
        (((d) & 0x00FF0000) >> 8) | \
        (((d) & 0xFF000000) >> 24)

#define SWAPLONGLONG(l) \
    (((l) & 0x00000000000000FF) << 56) | \
        (((l) & 0x000000000000FF00) << 40) | \
        (((l) & 0x0000000000FF0000) << 24) | \
        (((l) & 0x00000000FF000000) << 8) | \
        (((l) & 0x000000FF00000000) >> 8) | \
        (((l) & 0x0000FF0000000000) >> 24) | \
        (((l) & 0x00FF000000000000) >> 40) | \
        (((l) & 0xFF00000000000000) >> 56)

static WORD
getWord(void* var)
{
    WORD w;
    memcpy(&w, var, 2);
    return SWAPWORD(w);
}

static DWORD
getDWord(void* var)
{
    DWORD d;
    memcpy(&d, var, 4);
    return SWAPDWORD(d);
}

static LONGLONG
getLongLong(void* var)
{
    LONGLONG l;
    memcpy(&l, var, 8);
    return SWAPLONGLONG(l);
}

static void SetTrackTypeMode(CISOImage::Track* track, int type)
{
    CALL_STACK_MESSAGE2("SetTrackTypeMode(, %d)", type);

    if (type == 0x07)
    {
        track->FSType = CISOImage::fsAudio;
        track->Mode = CISOImage::mNone;
    }
    else if (type == 0x00 || type == 0x05)
    {
        track->FSType = CISOImage::fsISO9660;
        track->Mode = CISOImage::mMode1;
    }
    else if (type & 0x02)
    {
        track->FSType = CISOImage::fsISO9660;
        track->Mode = CISOImage::mMode2;
    }
    else
    {
        track->FSType = CISOImage::fsUnknown;
        track->Mode = CISOImage::mNone;
    }
}

static BOOL
ReadHeader(CFile& file, NRGHeader* header)
{
    CALL_STACK_MESSAGE2("ReadHeader(, %p)", header);

    DWORD read;

    BYTE hdr[8];
    if (!file.Read(hdr, sizeof(hdr), &read, NULL, NULL))
        return FALSE;

    if (read != sizeof(hdr))
        return FALSE;

    memcpy(header->ID, hdr, 4);
    header->Size = getDWord(hdr + 4);

    return TRUE;
}

static BOOL
readCUEX(CFile& file, NRGHeader* header, CISOImage* iso)
{
    CALL_STACK_MESSAGE3("readCUEX(, %p, %p)", header, iso);

    DWORD read;

    if (header->Size < 0x10 || (header->Size - 0x10) % 0x10 != 0)
        return FALSE;
    const DWORD trackCount = (header->Size - 0x10) / 0x10;

    BYTE cuex_hdr[0x10];
    if (!file.Read(&cuex_hdr, sizeof(cuex_hdr), &read, NULL, NULL) || read != sizeof(cuex_hdr))
        return FALSE;

    DWORD i;
    for (i = 0; i < trackCount; i++)
    {
        BYTE data[0x10];
        if (!file.Read(&data, sizeof(data), &read, NULL, NULL) || read != sizeof(data))
            return FALSE;

        CISOImage::Track* track = new CAudioTrack;
        track->ExtentOffset = getDWord(data + 0x04);
        iso->AddTrack(track);
    } // for

    return TRUE;
}

static BOOL
readDAOX(CFile& file, NRGHeader* header, CISOImage* iso)
{
    CALL_STACK_MESSAGE3("readDAOX(, %p, %p)", header, iso);

    std::vector<sally::nrg::DaoTrack> records;
    if (!sally::nrg::ReadDaoTracks(header->Size, static_cast<unsigned>(iso->GetTrackCount()), [&](uint8_t* bytes, size_t count)
                                   {
            DWORD read = 0;
            return file.Read(bytes, static_cast<DWORD>(count), &read, NULL, NULL) && read == count; }, records))
        return FALSE;
    for (const auto& record : records)
        if (iso->GetTrack(static_cast<int>(record.Index)) == NULL)
            return FALSE;
    for (const auto& record : records)
    {
        CISOImage::Track* track = iso->GetTrack(static_cast<int>(record.Index));
        track->SectorSize = record.SectorSize;
        SetTrackTypeMode(track, record.Type);
        track->Start = static_cast<LONGLONG>(record.Start);
        track->End = static_cast<LONGLONG>(record.End);
        track->SectorHeaderSize = CISOImage::GetHeaderSizeFromMode(track->Mode, track->SectorSize);
    }
    return TRUE;
}

static BOOL
readETNF(CFile& file, NRGHeader* header, CISOImage* iso)
{
    CALL_STACK_MESSAGE3("readETNF(, %p, %p)", header, iso);

    if (header->Size == 0 || header->Size % 0x14 != 0)
        return FALSE;

    DWORD read;
    BYTE* data = new BYTE[header->Size];
    ZeroMemory(data, header->Size);

    if (!file.Read(data, header->Size, &read, NULL, NULL) || read != header->Size)
    {
        delete[] data;
        return FALSE;
    }

    int offset = 0;
    unsigned int i;
    for (i = 0; i < header->Size / 0x14; i++)
    {
        CISOImage::Track* track = new CISOImage::Track;

        // sector size
        if (data[offset + 0x0B] & 1)
            track->SectorSize = 0x920;
        else
            track->SectorSize = 0x800;

        track->Start = (LONGLONG)getDWord(data + offset + 0x00);
        track->End = track->Start + (LONGLONG)getDWord(data + offset + 0x04);
        SetTrackTypeMode(track, data[offset + 0x0B]);
        track->SectorHeaderSize = CISOImage::GetHeaderSizeFromMode(track->Mode, track->SectorSize);
        track->ExtentOffset = getDWord(data + offset + 0x0C);
        iso->AddTrack(track);

        offset += 0x14;
    }

    delete[] data;
    return TRUE;
}

static BOOL
readETN2(CFile& file, NRGHeader* header, CISOImage* iso)
{
    CALL_STACK_MESSAGE3("readETN2(, %p, %p)", header, iso);

    if (header->Size < 0x10)
        return FALSE;

    DWORD read;
    BYTE* data = new BYTE[header->Size];
    ZeroMemory(data, header->Size);

    if (!file.Read(data, header->Size, &read, NULL, NULL) || read != header->Size)
    {
        delete[] data;
        return FALSE;
    }

    CISOImage::Track* track = new CISOImage::Track;
    track->SectorSize = 2048;
    track->FSType = CISOImage::fsISO9660;
    track->Mode = CISOImage::mMode1;
    track->SectorHeaderSize = CISOImage::GetHeaderSizeFromMode(CISOImage::mMode1, track->SectorSize);
    track->Start = getLongLong(data + 0x00);
    track->End = getLongLong(data + 0x08);
    track->ExtentOffset = 0;
    iso->AddTrack(track);

    delete[] data;
    return TRUE;
}

static BOOL
readCDTX(CFile& file, NRGHeader* header, CISOImage* iso)
{
    CALL_STACK_MESSAGE3("readCDTX(, %p, %p)", header, iso);

    DWORD read;
    const size_t terminatorCount = 1 + static_cast<size_t>(iso->GetTrackCount());
    if (static_cast<size_t>(header->Size) > static_cast<size_t>(-1) - terminatorCount)
        return FALSE;
    const size_t allocationSize = static_cast<size_t>(header->Size) + terminatorCount;
    char* data = new char[allocationSize];

    if (!file.Read(data, header->Size, &read, NULL, NULL) || read != header->Size)
    {
        delete[] data;
        return FALSE;
    }

    const char* src;
    char* dst;
    if (header->Size < 4)
    {
        delete[] data;
        return FALSE;
    }

    src = data + 4; // Skip unknown 4 bytes (flags)
    dst = data;
    size_t remaining = static_cast<size_t>(header->Size) - 4;
    while (remaining > 0)
    { // Remove some unknown data (there are blocks of 12 bytes of labels followed by unkown 6 bytes)
        const size_t labelBytes = min(static_cast<size_t>(12), remaining);
        memmove(dst, src, labelBytes);
        dst += labelBytes;
        const size_t blockBytes = min(static_cast<size_t>(18), remaining);
        src += blockBytes;
        remaining -= blockBytes;
    }
    // Make our life easier by adding as many safety terminators as may be needed
    memset(dst, 0, terminatorCount);
    src = data;
    const char* const end = dst + terminatorCount;
    const auto nextLabel = [&src, end](std::wstring& label) {
        const void* terminator = memchr(src, 0, static_cast<size_t>(end - src));
        if (terminator == nullptr)
            return false;
        const char* nul = static_cast<const char*>(terminator);
        if (!DecodeUnisoLegacyText(std::string_view(src, static_cast<size_t>(nul - src)), label))
            return false;
        src = nul + 1;
        return true;
    };

    std::wstring label;
    if (!nextLabel(label) || !iso->SetLabel(label))
    {
        delete[] data;
        return FALSE;
    }
    int j;
    for (j = 0; j < iso->GetTrackCount(); j++)
    {
        if (!nextLabel(label) || !iso->GetTrack(j)->SetLabel(label))
        {
            delete[] data;
            return FALSE;
        }
    }

    delete[] data;
    return TRUE;
}

static BOOL
readSINF(CFile& file, NRGHeader* header, CISOImage* iso)
{
    CALL_STACK_MESSAGE3("readSINF(, %p, %p)", header, iso);

    DWORD read;
    DWORD count;

    if (header->Size < sizeof(count) || !file.Read(&count, sizeof(count), &read, NULL, NULL) || read != sizeof(count))
        return FALSE;

    count = SWAPDWORD(count);
    iso->AddSessionTracks(count);

    return TRUE;
}

static BOOL
ReadNRGRoot(CFile& file, CISOImage* iso)
{
    CALL_STACK_MESSAGE2("ReadNRGRoot(, %p)", iso);

    BOOL ret = FALSE;

    // check header
    NRGHeader header;
    DWORD read;

    if (!file.Read(&header.ID, sizeof(header.ID), &read, NULL, NULL))
        return FALSE;

    if ((read != 4) || (strncmp(header.ID, "NER5", 4) != 0))
        return FALSE;

    // read offset
    LONGLONG offset;
    if (!file.Read(&offset, sizeof(offset), &read, NULL, NULL))
        return FALSE;
    if (read != 8)
        return FALSE;

    offset = getLongLong(&offset);

    const LONGLONG footerEnd = file.Seek(0, FILE_CURRENT);
    if (footerEnd < 12 || offset < 0 || offset > footerEnd - 12)
        return FALSE;
    const LONGLONG footerStart = footerEnd - 12;

    // set file pointer to start of session information
    if (file.Seek(offset, FILE_BEGIN) == -1)
        return FALSE;

    do
    {
        if (!ReadHeader(file, &header))
            return FALSE;

        if (strncmp(header.ID, "NER5", 4) == 0)
            return file.Seek(0, FILE_CURRENT) - 8 == footerStart;
        const LONGLONG chunkStart = file.Seek(0, FILE_CURRENT);
        if (chunkStart < 0 || chunkStart > footerStart || header.Size > uint64_t(footerStart - chunkStart))
            return FALSE;
        const LONGLONG chunkEnd = chunkStart + header.Size;

        if (strncmp(header.ID, "CUEX", 4) == 0)
        {
            if (!readCUEX(file, &header, iso))
                return FALSE;
        }
        else if (strncmp(header.ID, "DAOX", 4) == 0)
        {
            if (!readDAOX(file, &header, iso))
                return FALSE;
        }
        else if (strncmp(header.ID, "SINF", 4) == 0)
        {
            if (!readSINF(file, &header, iso))
                return FALSE;
        }
        else if (strncmp(header.ID, "ETNF", 4) == 0)
        {
            if (!readETNF(file, &header, iso))
                return FALSE;
        }
        else if (strncmp(header.ID, "ETN2", 4) == 0)
        {
            if (!readETN2(file, &header, iso))
                return FALSE;
        }
        else if (strncmp(header.ID, "CDTX", 4) == 0)
        {
            if (!readCDTX(file, &header, iso))
                return FALSE;
        }
        const LONGLONG consumed = file.Seek(0, FILE_CURRENT);
        if (consumed < chunkStart || consumed > chunkEnd || file.Seek(chunkEnd, FILE_BEGIN) != chunkEnd)
            return FALSE;

    } while (strncmp(header.ID, "NER5", 4) != 0);

    return TRUE;
}

BOOL CISOImage::ReadSessionNRG(BOOL quiet /* = FALSE*/)
{
    CALL_STACK_MESSAGE2("CISOImage::ReadSessionNRG(%d)", quiet);

    DWORD fileSizeHi;
    DWORD fileSizeLo = File->GetFileSize(&fileSizeHi);
    if (INVALID_FILE_SIZE == fileSizeLo)
        return FALSE;

    LARGE_INTEGER pos;
    pos.QuadPart = (((LONGLONG)fileSizeHi) << 32) + fileSizeLo - 12;
    if (File->Seek(pos.QuadPart, FILE_BEGIN) == -1)
        return FALSE;

    BOOL ret = ReadNRGRoot(*File, this);

    return ret;
}
