// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"

#ifdef _MPG_SUPPORT_

#include "mpgparser.h"
#include "..\mmviewer.rh"
#include "..\mmviewer.rh2"
#include "..\lang\lang.rh"
#include "..\output.h"
#include "mpeghead.h"
#include "id3tagv1.h"
#include "id3tagv2.h"

struct MPEG_INFO : public MPEGHEAD_DECODED
{
};

std::wstring LangStr(int resID);
void chkstrcpy(char* out, const char* in, size_t maxoutsize)
{
    strncpy_s(out, maxoutsize, in, _TRUNCATE);
}
CParserResultEnum
CParserMPG::OpenFile(const wchar_t* fileName)
{
    CloseFile();

    _wfopen_s(&f, fileName, L"rb");

    if (!f)
        return preOpenError;

    return preOK;
}

CParserResultEnum
CParserMPG::CloseFile()
{
    if (f)
    {
        fclose(f);
        f = NULL;
    }

    return preOK;
}

CParserResultEnum
CParserMPG::GetFileInfo(COutputInterface* output)
{
    if (f)
    {
        DWORD header;
        MPEG_INFO mpeginfo;
        ID3TAGV1 id3v1;
        ID3TAGV1_DECODED id3v1dec;
        ID3TAGV2_HEADER id3v2head;
        ID3TAGV2_DECODED id3v2dec;
        DWORD filesize;

        BOOL id3tagv1found = FALSE;
        BOOL id3tagv2found = FALSE;
        std::wstring emphasis;
        std::wstring mode;
        std::wstring duration;

        int usetag = 0; // the program decides which tag to use (1 or 2)

        memset(&mpeginfo, 0, sizeof(mpeginfo));

        // find out the file size
        fseek(f, 0, SEEK_END);
        filesize = ftell(f);
        fseek(f, 0, SEEK_SET);

        if (ID3TAGV2_ReadMainHead(f, &id3v2head))
        {
            if (ID3TAGV2_Read(f, &id3v2head, &id3v2dec))
            {
                id3tagv2found = TRUE;
            }

            fseek(f, sizeof(ID3TAGV2_HEADER) + CONVERT_C2DW(id3v2head.size), SEEK_SET); // safely skip all id3v2 data
        }

        if ((header = ScanForHeader(f, filesize)) != NULL)
        {
            //header found
            if (DecodeHeader(header, &mpeginfo, f, filesize))
            {
                switch (mpeginfo.emphasis)
                {
                case 0:
                    emphasis = LangStr(IDS_MP3_DASH);
                    break;
                case 1:
                    emphasis = LangStr(IDS_MP3_5015MS);
                    break;
                case 3:
                    emphasis = LangStr(IDS_MP3_CCITTJ17);
                    break;
                default:
                    emphasis = LangStr(IDS_MP3_QUESTIONMARK);
                    break;
                }

                switch (mpeginfo.mode)
                {
                case 0:
                    mode = LangStr(IDS_MP3_STEREO);
                    break;
                case 1:
                    mode = LangStr(IDS_MP3_JSTEREO);
                    break;
                case 2:
                    mode = LangStr(IDS_MP3_DCHANNEL);
                    break;
                case 3:
                    mode = LangStr(IDS_MP3_SCHANNEL);
                    break;
                }

                DWORD length_sec = mpeginfo.length_msec / 1000;
                duration = length_sec / 3600
                               ? FStrW(L"%02lu:%02lu:%02lu", length_sec / 3600,
                                       length_sec / 60 % 60, length_sec % 60)
                               : FStrW(L"%02lu:%02lu", length_sec / 60 % 60,
                                       length_sec % 60);
            }
        }
        else
        {
            if (id3tagv2found)
                ID3TAGV2_Free(&id3v2dec);
            return preUnknownFile;
        }

        // Read ID3TAGV1

        fseek(f, -signed(sizeof(ID3TAGV1)), SEEK_END);
        if (ID3TAGV1_Read(f, &id3v1))
        {
            if (ID3TAGV1_Decode(&id3v1, &id3v1dec))
            {
                id3tagv1found = TRUE;
            }
        }

        //dump
        const std::wstring layer(static_cast<size_t>(mpeginfo.layer), L'I');

        output->AddHeader(LangStr(IDS_MP3_MP3INFO).c_str());
        output->AddItem(LangStr(IDS_MP3_MP3VERSION).c_str(),
                        FStrW(LangStr(IDS_MP3_MP3VERSIONFMT).c_str(), HIBYTE(mpeginfo.mpeg),
                              LOBYTE(mpeginfo.mpeg), layer.c_str()).c_str());
        output->AddItem(LangStr(IDS_MP3_BITRATE).c_str(),
                        FStrW(L"%ls%u",
                              mpeginfo.variable_bitrate ? LangStr(IDS_MP3_AVERAGE).c_str() : L"",
                              mpeginfo.kbps).c_str());
        output->AddItem(LangStr(IDS_MP3_FREQUENCY).c_str(),
                        FormatSize2W(mpeginfo.hz).c_str());
        output->AddItem(LangStr(IDS_MP3_FRAMES).c_str(),
                        FormatSize2W(mpeginfo.frames).c_str());
        output->AddItem(LangStr(IDS_MP3_LENGTH).c_str(), duration.c_str());
        output->AddItem(LangStr(IDS_MP3_MODE).c_str(), mode.c_str());
        output->AddItem(LangStr(IDS_MP3_EMPHASIS).c_str(), emphasis.c_str());
        output->AddItem(LangStr(IDS_MP3_COPYRIGHT).c_str(),
                        mpeginfo.copyright ? LangStr(IDS_YES).c_str() : LangStr(IDS_NO).c_str());
        output->AddItem(LangStr(IDS_MP3_ORIGINAL).c_str(),
                        mpeginfo.original ? LangStr(IDS_YES).c_str() : LangStr(IDS_NO).c_str());

#define TAGITEM_AVAIL(tagitem) (tagitem && tagitem[0])

        if (id3tagv1found)
        {
            output->AddSeparator();

            output->AddHeader(FStrW(LangStr(IDS_MP3_ID3TAGV).c_str(), HIBYTE(id3v1dec.version), LOBYTE(id3v1dec.version)).c_str());

            if (TAGITEM_AVAIL(id3v1dec.title))
                output->AddItem(LangStr(IDS_MP3_TITLE).c_str(), id3v1dec.title);
            if (TAGITEM_AVAIL(id3v1dec.artist))
                output->AddItem(LangStr(IDS_MP3_AUTHOR).c_str(), id3v1dec.artist);
            if (TAGITEM_AVAIL(id3v1dec.album))
                output->AddItem(LangStr(IDS_MP3_ALBUM).c_str(), id3v1dec.album);
            if (TAGITEM_AVAIL(id3v1dec.year))
                output->AddItem(LangStr(IDS_MP3_YEAR).c_str(), id3v1dec.year);
            if (TAGITEM_AVAIL(id3v1dec.genre))
                output->AddItem(LangStr(IDS_MP3_GENRE).c_str(), id3v1dec.genre);
            if (LOBYTE(id3v1dec.version) == 1)
                output->AddItem(LangStr(IDS_MP3_TRACK).c_str(), FStrW(L"%u", id3v1dec.track).c_str());
            if (TAGITEM_AVAIL(id3v1dec.comments))
                output->AddItem(LangStr(IDS_MP3_COMMENTS).c_str(), id3v1dec.comments);
        }

        if (id3tagv2found)
        {
            output->AddSeparator();

            output->AddHeader(FStrW(LangStr(IDS_MP3_ID3TAGV).c_str(), HIBYTE(id3v2dec.version), LOBYTE(id3v2dec.version)).c_str());

            if (TAGITEM_AVAIL(id3v2dec.title))
                output->AddItem(LangStr(IDS_MP3_TITLE).c_str(), id3v2dec.title);
            if (TAGITEM_AVAIL(id3v2dec.artist))
                output->AddItem(LangStr(IDS_MP3_AUTHOR).c_str(), id3v2dec.artist);
            if (TAGITEM_AVAIL(id3v2dec.album))
                output->AddItem(LangStr(IDS_MP3_ALBUM).c_str(), id3v2dec.album);
            if (TAGITEM_AVAIL(id3v2dec.year))
                output->AddItem(LangStr(IDS_MP3_YEAR).c_str(), id3v2dec.year);
            if (TAGITEM_AVAIL(id3v2dec.genre))
                output->AddItem(LangStr(IDS_MP3_GENRE).c_str(), id3v2dec.genre);
            if (TAGITEM_AVAIL(id3v2dec.track))
                output->AddItem(LangStr(IDS_MP3_TRACK).c_str(), id3v2dec.track);
            if (TAGITEM_AVAIL(id3v2dec.comments))
                output->AddItem(LangStr(IDS_MP3_COMMENTS).c_str(), id3v2dec.comments);

            if (TAGITEM_AVAIL(id3v2dec.composer))
                output->AddItem(LangStr(IDS_MP3_COMPOSER).c_str(), id3v2dec.composer);
            if (TAGITEM_AVAIL(id3v2dec.original_artist))
                output->AddItem(LangStr(IDS_MP3_ORIGARTIST).c_str(), id3v2dec.original_artist);
            if (TAGITEM_AVAIL(id3v2dec.publisher))
                output->AddItem(LangStr(IDS_MP3_PUBLISHER).c_str(), id3v2dec.publisher);
            if (TAGITEM_AVAIL(id3v2dec.copyright))
                output->AddItem(LangStr(IDS_MP3_COPYRIGHT).c_str(), id3v2dec.copyright);
            if (TAGITEM_AVAIL(id3v2dec.encodedby))
                output->AddItem(LangStr(IDS_MP3_ENCODEDBY).c_str(), id3v2dec.encodedby);

            if (TAGITEM_AVAIL(id3v2dec.text_userdefined))
                output->AddItem(LangStr(IDS_MP3_USERTEXT).c_str(), id3v2dec.text_userdefined);

            if (TAGITEM_AVAIL(id3v2dec.url_cominfo))
                output->AddItem(LangStr(IDS_MP3_URL_COMINFO).c_str(), id3v2dec.url_cominfo);
            if (TAGITEM_AVAIL(id3v2dec.url_copyright))
                output->AddItem(LangStr(IDS_MP3_URL_COPYRIGHT).c_str(), id3v2dec.url_copyright);
            if (TAGITEM_AVAIL(id3v2dec.url_official))
                output->AddItem(LangStr(IDS_MP3_URL_OFFICIAL).c_str(), id3v2dec.url_official);
            if (TAGITEM_AVAIL(id3v2dec.url_artist))
                output->AddItem(LangStr(IDS_MP3_URL_ARTIST).c_str(), id3v2dec.url_artist);
            if (TAGITEM_AVAIL(id3v2dec.url_audiosource))
                output->AddItem(LangStr(IDS_MP3_URL_ASOURCE).c_str(), id3v2dec.url_audiosource);
            if (TAGITEM_AVAIL(id3v2dec.url_iradio))
                output->AddItem(LangStr(IDS_MP3_URL_IRADIO).c_str(), id3v2dec.url_iradio);
            if (TAGITEM_AVAIL(id3v2dec.url_payment))
                output->AddItem(LangStr(IDS_MP3_URL_PAYMENT).c_str(), id3v2dec.url_payment);
            if (TAGITEM_AVAIL(id3v2dec.url_publisher))
                output->AddItem(LangStr(IDS_MP3_URL_PUBLISHER).c_str(), id3v2dec.url_publisher);
            if (TAGITEM_AVAIL(id3v2dec.url_userdefined))
                output->AddItem(LangStr(IDS_MP3_USERURL).c_str(), id3v2dec.url_userdefined);

            ID3TAGV2_Free(&id3v2dec);
        }

#undef TAGITEM_AVAIL

        return preOK;
    }

    return preUnknownFile;
}

#endif
