// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"
#include "tar_text.h"

#include "dlldefs.h"
#include "fileio.h"

#include "gzip/gzip.h"
#include "bzip/bzlib.h"
#include "bzip/bzip.h"
#include "compress/compress.h"
#include "rpm/rpm.h"
#include "lzh/lzh.h"

#include "tar.rh"
#include "tar.rh2"
#include "lang\lang.rh"
#include "../shared/plugin_local_path.h"

//********************************************************
//
//  CDecompressFile
//

CDecompressFile*
CDecompressFile::CreateInstance(const wchar_t* fileName, DWORD inputOffset, CQuadWord inputSize)
{
    CALL_STACK_MESSAGE2("CDecompressFile::CreateInstance(%ls)", fileName);
    // open the input file
    std::wstring ioPath;
    HANDLE file = PreparePluginLocalPathForIo(fileName, ioPath)
                      ? CreateFileW(ioPath.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                                    FILE_FLAG_SEQUENTIAL_SCAN, NULL)
                      : INVALID_HANDLE_VALUE;
    if (file == INVALID_HANDLE_VALUE)
    {
        int err = GetLastError();
        std::wstring txtbuf = LangStr(IDS_GZERR_FOPEN).c_str();
        txtbuf += SPLGetErrorTextOwned(SalamanderGeneral, err);
        SalamanderGeneral->ShowMessageBox(txtbuf.c_str(), LangStr(IDS_GZERR_TITLE).c_str(), MSGBOX_ERROR);
        return NULL;
    }
    if (inputOffset != 0)
    {
        if (SetFilePointer(file, inputOffset, NULL, FILE_BEGIN) == INVALID_SET_FILE_POINTER)
        {
            int err = GetLastError();
            CloseHandle(file);
            std::wstring txtbuf = LangStr(IDS_GZERR_SEEK).c_str();
            txtbuf += SPLGetErrorTextOwned(SalamanderGeneral, err);
            SalamanderGeneral->ShowMessageBox(txtbuf.c_str(), LangStr(IDS_GZERR_TITLE).c_str(), MSGBOX_ERROR);
            return NULL;
        }
    }
    // allocate a buffer for reading the file
    unsigned char* buffer = (unsigned char*)malloc(BUFSIZE);
    if (buffer == NULL)
    {
        SalamanderGeneral->ShowMessageBox(LangStr(IDS_ERR_MEMORY).c_str(), LangStr(IDS_GZERR_TITLE).c_str(),
                                          MSGBOX_ERROR);
        CloseHandle(file);
        return NULL;
    }
    // read the first block of data
    DWORD read;
    if (!ReadFile(file, buffer, BUFSIZE, &read, NULL))
    {
        // read error
        int err = GetLastError();
        std::wstring txtbuf = LangStr(IDS_ERR_FREAD).c_str();
        txtbuf += SPLGetErrorTextOwned(SalamanderGeneral, err);
        SalamanderGeneral->ShowMessageBox(txtbuf.c_str(), LangStr(IDS_GZERR_TITLE).c_str(), MSGBOX_ERROR);
        free(buffer);
        CloseHandle(file);
        return NULL;
    }
    //
    // detect the compression algorithm
    //

    // try RPM
    CDecompressFile* archive = new CRPM(fileName, file, buffer, read);
    if (archive != NULL && !archive->IsOk() && archive->GetErrorCode() == 0)
    {
        delete archive;
        // then gzip
        archive = new CGZip(fileName, file, buffer, inputOffset, read, inputSize);
        if (archive != NULL && !archive->IsOk() && archive->GetErrorCode() == 0)
        {
            // not gzip, try compress
            delete archive;
            archive = new CCompress(fileName, file, buffer, read, inputSize);
            if (archive != NULL && !archive->IsOk() && archive->GetErrorCode() == 0)
            {
                // not compress, try bzip
                delete archive;
                archive = new CBZip(fileName, file, buffer, inputOffset, read, inputSize);
                if (archive != NULL && !archive->IsOk() && archive->GetErrorCode() == 0)
                {
                    // not compress, try lzh
                    delete archive;
                    archive = new CLZH(fileName, file, buffer, read);
                    if (archive != NULL && !archive->IsOk() && archive->GetErrorCode() == 0)
                    {
                        // not compressed, fall back to the base class
                        delete archive;
                        archive = new CDecompressFile(fileName, file, buffer, inputOffset, read, inputSize);
                    }
                }
            }
        }
    }
    if (archive == NULL || !archive->IsOk())
    {
        if (archive == NULL)
            SalamanderGeneral->ShowMessageBox(LangStr(IDS_ERR_MEMORY).c_str(), LangStr(IDS_GZERR_TITLE).c_str(),
                                              MSGBOX_ERROR);
        else
        {
            SalamanderGeneral->ShowMessageBox(LoadErr(archive->GetErrorCode(), archive->GetLastErr()), LangStr(IDS_GZERR_TITLE).c_str(), MSGBOX_ERROR);
            delete archive;
        }
        CloseHandle(file);
        free(buffer);
        return NULL;
    }
    return archive;
}

// class constructor
CDecompressFile::CDecompressFile(const wchar_t* filename, HANDLE file, unsigned char* buffer, unsigned long start, unsigned long read, CQuadWord inputSize) : FileName(filename), File(file), Buffer(buffer), DataStart(buffer), DataEnd(buffer + read),
                                                                                                                                                           OldName(NULL), Ok(TRUE), StreamPos(start, 0), ErrorCode(0), LastError(0), FreeBufAndFile(TRUE)
{
    CALL_STACK_MESSAGE3("CDecompressFile::CDecompressFile(%ls, , %u)", filename, read);

    if (CQuadWord(0, 0) == inputSize)
    {
        // determine the file size
        DWORD SizeLow, SizeHigh;
        SizeLow = GetFileSize(File, &SizeHigh);
        InputSize.Set(SizeLow, SizeHigh);
    }
    else
    {
        InputSize = inputSize;
    }
}

CDecompressFile::~CDecompressFile()
{
    CALL_STACK_MESSAGE1("CDecompressFile::~CDecompressFile()");
    if (FreeBufAndFile)
    {
        if (File != INVALID_HANDLE_VALUE)
            CloseHandle(File);
        if (Buffer != NULL)
            free(Buffer);
    }
    if (OldName != NULL)
        free(OldName);
}

void CDecompressFile::SetOldName(const char* oldName)
{
    if (OldName != NULL)
        free(OldName);
    OldName = _strdup(oldName);
}

// reads a block from the input file
const unsigned char*
CDecompressFile::FReadBlock(unsigned int number)
{
    if (!Ok)
    {
        TRACE_E("ReadBlock called on a broken stream.");
        return NULL;
    }

    // do we have a large enough buffer?
    if (number > BUFSIZE)
    {
        TRACE_E("Requested block is too large.");
        Ok = FALSE;
        ErrorCode = IDS_ERR_INTERNAL;
        return NULL;
    }
    // if we are at the end of the buffer, reinitialize it
    if (DataEnd == DataStart && DataStart == Buffer + BUFSIZE)
    {
        DataEnd = Buffer;
        DataStart = Buffer;
    }
    // if there is not enough contiguous space, compact the buffer
    if (BUFSIZE - (DataStart - Buffer) < (int)number)
    {
        memmove(Buffer, DataStart, DataEnd - DataStart);
        DataEnd = Buffer + (DataEnd - DataStart);
        DataStart = Buffer;
    }
    // if there is not enough data available, refill the buffer
    if (DataEnd == DataStart || (unsigned int)(DataEnd - DataStart) < number)
    {
        DWORD read = (DWORD)(Buffer + BUFSIZE - DataEnd);

        if (StreamPos.Value + read > InputSize.Value)
        {
            read = (DWORD)(InputSize.Value - StreamPos.Value);
        }

        if (!ReadFile(File, DataEnd, read, &read, NULL))
        {
            // read error
            Ok = FALSE;
            ErrorCode = IDS_ERR_FREAD;
            LastError = GetLastError();
            return NULL;
        }
        DataEnd += read;
        // did we read enough data?
        if ((unsigned int)(DataEnd - DataStart) < number)
        {
            ErrorCode = IDS_ERR_EOF;
            LastError = 0;
            return NULL;
        }
    }
    unsigned char* ret = DataStart;
    // adjust the pointers
    DataStart += number;
    StreamPos += CQuadWord(number, 0);
    // and return the result
    return ret;
}

// reads one byte from the input file
unsigned char
CDecompressFile::FReadByte()
{
    if (!Ok)
    {
        TRACE_E("ReadByte called on a broken stream.");
        return 0;
    }
    // if the buffer is empty, fill it
    if (DataEnd - DataStart < 1)
    {
        DataStart = Buffer;
        DWORD read;
        if (StreamPos >= InputSize)
        {
            Ok = FALSE;
            ErrorCode = IDS_ERR_EOF;
            return 0;
        }
        if (!ReadFile(File, DataStart, BUFSIZE, &read, NULL))
        {
            // read error
            Ok = FALSE;
            ErrorCode = IDS_ERR_FREAD;
            LastError = GetLastError();
            return 0;
        }
        DataEnd = Buffer + read;
        // did we read enough data?
        if (read < 1)
        {
            // we still have less than required - error
            Ok = FALSE;
            ErrorCode = IDS_ERR_EOF;
            return 0;
        }
    }
    // adjust the pointers
    ++StreamPos;
    return *(DataStart++);
}

const char*
CDecompressFile::GetOldName()
{
    if (OldName == NULL)
    {
        const std::wstring oldName = GetOldNameW();
        std::string encoded;
        if (!EncodeTarUtf8Name(oldName, encoded))
            encoded.clear();
        OldName = (char*)malloc(encoded.size() + 1);
        if (OldName == NULL)
        {
            SalamanderGeneral->ShowMessageBox(LangStr(IDS_ERR_MEMORY).c_str(), LangStr(IDS_GZERR_TITLE).c_str(),
                                              MSGBOX_ERROR);
            return NULL;
        }
        memcpy(OldName, encoded.c_str(), encoded.size() + 1);
    }
    return OldName;
}

std::wstring CDecompressFile::GetOldNameW() const
{
    if (OldName != NULL)
    {
        std::wstring decoded;
        if (DecodeTarLegacyName(OldName, decoded))
            return decoded;
        return std::wstring();
    }

    size_t begin = FileName.find_last_of(L"\\/");
    begin = begin == std::wstring::npos ? 0 : begin + 1;
    size_t end = FileName.find_last_of(L'.');
    if (end == std::wstring::npos || end < begin)
        end = FileName.size();
    return FileName.substr(begin, end - begin);
}

// returns the last read data for reuse with the compressed stream
// it cannot return more than the last loaded block (earlier data may
// no longer be in memory)
void CDecompressFile::Rewind(unsigned short size)
{
    if (DataStart - size >= Buffer)
    {
        DataStart -= size;
        StreamPos.Value -= size;
    }
    else
    {
        TRACE_E("Rewind - requested rewind by too large a portion.");
        SalamanderGeneral->ShowMessageBox(LangStr(IDS_ERR_INTERNAL).c_str(), LangStr(IDS_GZERR_TITLE).c_str(),
                                          MSGBOX_ERROR);
        Ok = FALSE;
        ErrorCode = IDS_ERR_INTERNAL;
    }
}

// public function for reading from the input (with optional decompression)
const unsigned char*
CDecompressFile::GetBlock(unsigned short size, unsigned short* read /* = NULL*/)
{
    const unsigned char* src = FReadBlock(size);
    if (read != NULL)
        *read = (unsigned short)(DataEnd - DataStart);
    return src;
}

void CDecompressFile::GetFileInfo(FILETIME& lastWrite, CQuadWord& fileSize, DWORD& fileAttr)
{
    TRACE_E("GetFileInfo called on an uncompressed stream.");
    memset(&lastWrite, 0, sizeof(FILETIME));
    fileSize.Set(0, 0);
    fileAttr = 0;
}

//********************************************************
//
//  CZippedFile
//

CZippedFile::CZippedFile(const wchar_t* filename, HANDLE file, unsigned char* buffer, unsigned long start, unsigned long read, CQuadWord inputSize) : CDecompressFile(filename, file, buffer, start, read, inputSize), Window(NULL), ExtrStart(NULL), ExtrEnd(NULL)
{
    // if the parent constructor failed, bail out immediately
    if (!Ok)
        return;

    // allocate a circular buffer
    Window = (unsigned char*)malloc(BUFSIZE);
    if (Window == NULL)
    {
        FreeBufAndFile = FALSE;
        Ok = FALSE;
        ErrorCode = IDS_ERR_MEMORY;
        return;
    }
    ExtrStart = Window;
    ExtrEnd = Window;
}

CZippedFile::~CZippedFile()
{
    CALL_STACK_MESSAGE1("CZippedFile::~CZippedFile()");
    if (Window)
        free(Window);
}

// returns the last read data for reuse with the compressed stream
// it cannot return more than the last loaded block (earlier data may
// no longer be in memory)
void CZippedFile::Rewind(unsigned short size)
{
    if (ExtrStart - size >= Window)
        ExtrStart -= size;
    else
    {
        TRACE_E("Rewind - requested rewind by too large a portion.");
        Ok = FALSE;
        ErrorCode = IDS_ERR_INTERNAL;
    }
}

BOOL CZippedFile::CompactBuffer()
{
    CALL_STACK_MESSAGE1("CZippedFile::CompactBuffer()");
    unsigned int old = (unsigned int)(ExtrStart - Window);
    // we have to shift the data so that we have enough space
    memmove(Window, ExtrStart, BUFSIZE - old);
    // and update the pointers
    ExtrEnd -= old;
    ExtrStart = Window;
    return TRUE;
}

// public function for reading from the input (with optional decompression)
const unsigned char*
CZippedFile::GetBlock(unsigned short size, unsigned short* read /* = NULL*/)
{
    if (size > BUFSIZE)
    {
        TRACE_E("GetBlock - requested block is too large.");
        ErrorCode = IDS_ERR_INTERNAL;
        Ok = FALSE;
        return NULL;
    }
    // do we have the required number of bytes in the buffer?
    if (ExtrStart == ExtrEnd || ExtrEnd - ExtrStart < size)
    {
        // we emptied the entire buffer...
        if (ExtrStart == ExtrEnd && ExtrStart == Window + BUFSIZE)
        {
            ExtrStart = Window;
            ExtrEnd = Window;
        }
        // do we have enough contiguous space?
        if (Window + BUFSIZE - ExtrStart < size)
            if (!CompactBuffer())
                return NULL;
        // decompress the block into the buffer
        if (!DecompressBlock(size) || ExtrEnd - ExtrStart < size)
        {
            if (read != NULL)
                *read = (unsigned short)(ExtrEnd - ExtrStart);
            return NULL;
        }
    }
    if (read != NULL)
        *read = (unsigned short)(ExtrEnd - ExtrStart);
    unsigned char* ret = ExtrStart;
    // size bytes will be consumed...
    ExtrStart += size;
    return ret;
}

void CZippedFile::GetFileInfo(FILETIME& lastWrite, CQuadWord& fileSize, DWORD& fileAttr)
{
    CALL_STACK_MESSAGE1("CZippedFile::GetFileInfo(,,)");

    // there is no way to determine the file size in the archive without decompression - return zero
    fileSize.Set(0, 0);
    // take the rest from the archive file
    BY_HANDLE_FILE_INFORMATION fileinfo;
    if (GetFileInformationByHandle(File, &fileinfo))
    {
        lastWrite = fileinfo.ftLastWriteTime;
        fileAttr = fileinfo.dwFileAttributes;
    }
    else
    {
        fileAttr = FILE_ATTRIBUTE_ARCHIVE;
        lastWrite.dwLowDateTime = 0;
        lastWrite.dwHighDateTime = 0;
    }
}
