// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"

#ifdef _MP4_SUPPORT_

#include "mp4parser.h"
#include "..\mmviewer.rh"
#include "..\mmviewer.rh2"
#include "..\lang\lang.rh"
#include "..\output.h"
#include "mp4head.h"
#include "mp4tag.h"

std::wstring LangStr(int resID);

CParserResultEnum
CParserMP4::OpenFile(const wchar_t* fileName)
{
    CloseFile();

    _wfopen_s(&f, fileName, L"rb");

    if (!f)
        return preOpenError;

    return preOK;
}

CParserResultEnum
CParserMP4::CloseFile()
{
    if (f)
    {
        fclose(f);
        f = NULL;
    }

    return preOK;
}

CParserResultEnum
CParserMP4::GetFileInfo(COutputInterface* output)
{
    AACHEAD_DECODED head;
    bool ret = DecodeAACHeader(f, &head);

    if (ret)
    {
        std::wstring object;
        std::wstring header;
        switch (head.object_type)
        {
        case 0:
            object = LangStr(IDS_MP4_OBJTYPE_MAIN);
            break;
        case 1:
            object = LangStr(IDS_MP4_OBJTYPE_LC);
            break;
        case 2:
            object = LangStr(IDS_MP4_OBJTYPE_SSR);
            break;
        case 3:
            object = LangStr(IDS_MP4_OBJTYPE_LTP);
            break;
        }

        switch (head.header_type)
        {
        case 0:
            header = LangStr(IDS_MP4_HEADTYPE_RAW);
            break;
        case 1:
            header = LangStr(IDS_MP4_HEADTYPE_ADIF);
            break;
        case 2:
            header = LangStr(IDS_MP4_HEADTYPE_ADTS);
            break;
        }

        output->AddHeader(LangStr(IDS_MP4_AACINFO).c_str());

        output->AddItem(LangStr(IDS_MP4_MPEGVERSION).c_str(),
                        FStrW(L"MPEG-%d (%ls %ls)", head.version, header.c_str(), object.c_str()).c_str());
        output->AddItem(LangStr(IDS_MP4_BITRATE).c_str(), FStrW(L"%d", head.bitrate).c_str());

        output->AddItem(LangStr(IDS_MP4_SAMPLINGRATE).c_str(),
                        FormatSize2W(head.sampling_rate).c_str());

        DWORD length_sec = head.length / 1000;
        const std::wstring duration = length_sec / 3600
                                          ? FStrW(L"%02lu:%02lu:%02lu", length_sec / 3600,
                                                  length_sec / 60 % 60, length_sec % 60)
                                          : FStrW(L"%02lu:%02lu", length_sec / 60 % 60,
                                                  length_sec % 60);
        output->AddItem(LangStr(IDS_MP4_DURATION).c_str(), duration.c_str());

        output->AddItem(LangStr(IDS_MP4_CHANNELS).c_str(), FStrW(L"%d", head.channels).c_str());

        return preOK;
    }

    return preUnknownFile;
}

#endif
