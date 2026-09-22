// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "TreeMap.FileData.CZFile.h"

#define SHGFI_ADDOVERLAYS 0x000000020 // apply the appropriate overlays

#define SHGFI_ICONMASK (SHGFI_ICON | SHGFI_SELECTED | SHGFI_LARGEICON | SHGFI_SMALLICON | SHGFI_OPENICON | SHGFI_SHELLICONSIZE | SHGFI_ADDOVERLAYS | SHGFI_USEFILEATTRIBUTES)

class CZIconLoader
{
protected:
    DWORD _flags;
    std::wstring _filename;
    std::wstring _fallbackFilename;

    static DWORD_PTR WINAPI LoadIconThreadProc(CWorkerThread* mythread, LPVOID lpParam)
    {
        CZIconLoader* self = (CZIconLoader*)lpParam;
        HICON hicon = CZIconLoader::LoadIconSync(self->_filename.c_str(), self->_flags);
        if (hicon == NULL && !self->_fallbackFilename.empty())
            hicon = CZIconLoader::LoadIconSync(self->_fallbackFilename.c_str(), self->_flags | SHGFI_USEFILEATTRIBUTES);
        delete self;
        return (DWORD_PTR)hicon;
    }
    CZIconLoader(CZFile* file, DWORD flags)
    {
        file->GetFullName(this->_filename);
        this->_fallbackFilename = file->GetName();
        this->_flags = flags;
    }
    CZIconLoader(wchar_t const* path, DWORD flags)
    {
        this->_filename = path != NULL ? path : L"";
        this->_flags = flags;
    }
    ~CZIconLoader()
    {
    }
    static CWorkerThread* BeginAsyncLoadIcon(CZIconLoader* self, HWND owner, UINT msg, LPVOID lParam)
    {
        CWorkerThread* mythread = new CWorkerThread(
            NULL,
            CZIconLoader::LoadIconThreadProc,
            self,
            owner,
            msg,
            lParam,
            FALSE);
        return mythread;
    }

public:
    static CWorkerThread* BeginAsyncLoadIcon(CZFile* file, DWORD flags, HWND owner, UINT msg)
    {
        CZIconLoader* self = new CZIconLoader(file, flags);
        return CZIconLoader::BeginAsyncLoadIcon(self, owner, msg, file);
    }
    static CWorkerThread* BeginAsyncLoadIcon(CZString const* path, DWORD flags, HWND owner, UINT msg, LPVOID lParam)
    {
        CZIconLoader* self = new CZIconLoader(path->GetString(), flags);
        return CZIconLoader::BeginAsyncLoadIcon(self, owner, msg, lParam);
    }
    static HICON LoadIconSync(CZFile* file, DWORD flags)
    {
        std::wstring path;
        file->GetFullName(path);
        HICON icon = CZIconLoader::LoadIconSync(path.c_str(), flags);
        if (icon == NULL)
            icon = CZIconLoader::LoadIconSync(file->GetName(), flags | SHGFI_USEFILEATTRIBUTES);
        return icon;
    }
    static HICON LoadIconSync(const wchar_t* path, DWORD flags)
    {
        flags |= SHGFI_ICON;     //get the icon
        flags &= SHGFI_ICONMASK; //remove unwanted flags
        SHFILEINFOW shfi;
        if (SHGetFileInfoW(path, 0, &shfi, sizeof(shfi), flags) == 0)
            return NULL;
        return shfi.hIcon;
    }
};
