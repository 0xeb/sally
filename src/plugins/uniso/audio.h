// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "fs.h"

// ****************************************************************************
//
// CAudio
//

class CISOImage;

class CAudio : public CUnISOFSAbstract
{
public:
    CAudio(CISOImage* image, int track);
    virtual ~CAudio();
    // methods

    virtual BOOL Open(BOOL quiet);
    virtual BOOL DumpInfo(FILE* outStream);
    virtual BOOL ListDirectory(const std::wstring& path, int session,
                               CSalamanderDirectoryAbstract* dir, CPluginDataInterfaceAbstract*& pluginData);

    virtual int UnpackFile(CSalamanderForOperationsAbstract* salamander, const std::wstring& path,
                           const std::wstring& nameInArc, const CFileData* fileData, DWORD& silent, BOOL& toSkip);

protected:
    int Track;

    BOOL AddFileDir(const wchar_t* path, const wchar_t* fileName,
                    CSalamanderDirectoryAbstract* dir, CPluginDataInterfaceAbstract*& pluginData);
};

struct CISOImage::Track;

struct CAudioTrack : CISOImage::Track
{
public:
    virtual ~CAudioTrack() = default;
    virtual const wchar_t* GetLabel();
    virtual bool SetLabel(std::wstring_view label) noexcept;

private:
    std::wstring Label;
};
