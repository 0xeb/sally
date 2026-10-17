// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "7za/CPP/7zip/Common/FileStreams.h"

class CRetryableOutFileStream : public IOutStream, public CMyUnknownImp
{
    Z7_COM_UNKNOWN_IMP_2(IOutStream, ISequentialOutStream)
public:
    CRetryableOutFileStream(HWND hParentWnd);

    virtual ~CRetryableOutFileStream() {}

    STDMETHOD(Write)
    (const void* data, UInt32 size, UInt32* processedSize);

    STDMETHOD(Seek)(Int64 offset, UInt32 origin, UInt64* position) { return Stream->Seek(offset, origin, position); }
    STDMETHOD(SetSize)(UInt64 size) { return Stream->SetSize(size); }
    bool Open(const wchar_t* name, DWORD disposition)
    {
        return disposition == OPEN_EXISTING ? FileStream->Open_EXISTING(name) :
               FileStream->Create_ALWAYS_or_Open_ALWAYS(name, disposition == CREATE_ALWAYS);
    }
    bool Create(const wchar_t* name, bool overwrite) { return overwrite ? FileStream->Create_ALWAYS(name) : FileStream->Create_NEW(name); }
    HRESULT Close() { return FileStream->Close(); }
    bool SetMTime(const FILETIME* time) { return FileStream->SetMTime(time); }

private:
    COutFileStream* FileStream;
    CMyComPtr<IOutStream> Stream;
    HWND hParentWnd;
};

class CRetryableInFileStream : public IInStream, public IStreamGetSize, public CMyUnknownImp
{
    Z7_COM_UNKNOWN_IMP_3(IInStream, ISequentialInStream, IStreamGetSize)
public:
    CRetryableInFileStream(HWND hParentWnd);

    virtual ~CRetryableInFileStream() {}

    STDMETHOD(Read)
    (void* data, UInt32 size, UInt32* processedSize);

    STDMETHOD(Seek)(Int64 offset, UInt32 origin, UInt64* position) { return Stream->Seek(offset, origin, position); }
    STDMETHOD(GetSize)(UInt64* size) { return FileStream->GetSize(size); }
    bool Open(const wchar_t* name) { return FileStream->Open(name); }

private:
    CInFileStream* FileStream;
    CMyComPtr<IInStream> Stream;
    HWND hParentWnd;
};
