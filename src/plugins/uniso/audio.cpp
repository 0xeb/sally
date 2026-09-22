// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"
#include "dbg.h"

#include "uniso.h"
#include "isoimage.h"
#include "audio.h"

#include "uniso.rh"
#include "uniso.rh2"
#include "lang\lang.rh"

// ****************************************************************************
//
// CAudio
//

CAudio::CAudio(CISOImage* image, int track) : CUnISOFSAbstract(image)
{
    Track = track;
}

CAudio::~CAudio()
{
}

BOOL CAudio::DumpInfo(FILE* outStream)
{
    return TRUE;
}

BOOL CAudio::AddFileDir(const wchar_t* path, const wchar_t* fileName,
                        CSalamanderDirectoryAbstract* dir, CPluginDataInterfaceAbstract*& pluginData)
{
    CFileData fd;

    fd.Name = SalamanderGeneral->DupStr(fileName);
    if (fd.Name == NULL)
    {
        Error(IDS_INSUFFICIENT_MEMORY);
        dir->Clear(pluginData);
        return FALSE;
    } // if

    fd.NameLen = static_cast<DWORD>(wcslen(fd.Name)); // generated track names are format-bounded
    wchar_t* s = wcsrchr(fd.Name, L'.');
    if (s != NULL)
        fd.Ext = s + 1; // ".cvspass" is extension in Windows
    else
        fd.Ext = fd.Name + fd.NameLen;

    fd.PluginData = 0;

    SYSTEMTIME st;
    GetSystemTime(&st);
    SystemTimeToFileTime(&st, &fd.LastWrite);

    fd.DosName = NULL;

    fd.Attr = FILE_ATTRIBUTE_READONLY; // everything is read-only by default
    fd.Hidden = 0;

    fd.Size = CQuadWord(0, 0);

    // file
    fd.IsLink = SalamanderGeneral->IsFileLink(fd.Ext);
    fd.IsOffline = 0;
    if (dir && !dir->AddFile(path, fd, pluginData))
    {
        free(fd.Name);
        dir->Clear(pluginData);
        return Error(IDS_ERROR);
    }

    return TRUE;
}

BOOL CAudio::ListDirectory(const std::wstring& path, int session, CSalamanderDirectoryAbstract* dir,
                           CPluginDataInterfaceAbstract*& pluginData)
{
    CALL_STACK_MESSAGE3("CAudio::ListDirectory(%ls, %d, ,)", path.c_str(), session);

    const std::wstring label(Image->GetTrack(Track)->GetLabel());
    const std::wstring trackNumber = Track + 1 < 10 ? L"0" + std::to_wstring(Track + 1)
                                                    : std::to_wstring(Track + 1);
    std::wstring audioTrack;
    if (!label.empty())
        audioTrack = trackNumber + L" " + label;
    else
        audioTrack = L"Audio Track " + trackNumber;

    return AddFileDir(path.c_str(), audioTrack.c_str(), dir, pluginData);
}

int CAudio::UnpackFile(CSalamanderForOperationsAbstract* salamander, const std::wstring& path,
                       const std::wstring& nameInArc, const CFileData* fileData, DWORD& silent, BOOL& toSkip)
{
    return UNPACK_AUDIO_UNSUP;
}

BOOL CAudio::Open(BOOL quiet)
{
    return TRUE;
}

// ****************************************************************************
//
// CAudioTrack
//

const wchar_t* CAudioTrack::GetLabel()
{
    return Label.c_str();
}

bool CAudioTrack::SetLabel(std::wstring_view label) noexcept
{
    try
    {
        std::wstring staged(label);
        Label.swap(staged);
        return true;
    }
    catch (...)
    {
        return false;
    }
}
