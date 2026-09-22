// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"

#ifdef _OGG_SUPPORT_

#include "oggparser.h"
#include "..\output.h"
#include "..\renderer.h"
#include "..\mmviewer.h"
#include "..\mmviewer.rh"
#include "..\mmviewer.rh2"
#include "..\lang\lang.rh"
#include "..\output.h"

std::wstring LangStr(int resID);

typedef struct
{
    char c[128];
    int str_id;
    char* str;
} OGG_COMMENT;

int FindComment(const char* in_string, int& offset, OGG_COMMENT comments[]) //s=string za '='
{
    const char* in_string_orig = in_string;
    int i = 0;
    offset = 0;
    while (comments[i].c[0] != '\0')
    {
        int j = 0;
        in_string = in_string_orig;
        while (comments[i].c[j] && *in_string && comments[i].c[j] == *in_string)
        {
            j++;
            in_string++;
        }

        if (*in_string && (*in_string == '='))
        {
            offset = ++in_string - in_string_orig;
            return i; //found
        }

        i++;
    }

    return -1;
}

CParserResultEnum
CParserOGG::OpenFile(const wchar_t* fileName)
{
    CloseFile();

    _wfopen_s(&f, fileName, L"rb");

    if (!f)
        return preOpenError;

    if (ov_open(f, &vf, NULL, 0) >= 0)
        return preOK;

    fclose(f);
    f = NULL;

    return preUnknownFile;
}

CParserResultEnum
CParserOGG::CloseFile()
{
    // if ov_open failed, f is always NULL
    if (f)
    {
        ov_clear(&vf);
        fclose(f);
        f = NULL;
    }

    return preOK;
}

CParserResultEnum
CParserOGG::GetFileInfo(COutputInterface* output)
{
    if (f)
    {
        // tags will be printed in this order. reorder them as you like
        // must be uppercase
        OGG_COMMENT comments[] = {
            {"TRACKNUMBER", IDS_OGG_TRACK, NULL},
            //The track number of this piece if part of a specific larger collection or album
            {"TITLE", IDS_OGG_TITLE, NULL},
            //Track name
            {"ARTIST", IDS_OGG_AUTHOR, NULL},
            //The artist generally considered responsible for the work. In popular music this is usually the performing band or singer. For classical music it would be the composer. For an audio book it would be the author of the original text.
            {"ALBUM", IDS_OGG_ALBUM, NULL},
            //The collection name to which this track belongs
            {"DATE", IDS_OGG_DATE, NULL},
            //Date the track was recorded
            {"GENRE", IDS_OGG_GENRE, NULL},
            //A short text indication of music genre
            {"PERFORMER", IDS_OGG_PERFORMER, NULL},
            //The artist(s) who performed the work. In classical music this would be the conductor, orchestra, soloists. In an audio book it would be the actor who did the reading. In popular music this is typically the same as the ARTIST and is ommitted.
            {"ORGANIZATION", IDS_OGG_ORGANIZATION, NULL},
            //Name of the organization producing the track (i.e. the 'record label')
            {"DESCRIPTION", IDS_OGG_DESCRIPTION, NULL},
            //A short text description of the contents
            {"LOCATION", IDS_OGG_LOCATION, NULL},
            //Location where track was recorded
            {"COPYRIGHT", IDS_OGG_COPYRIGHT, NULL},
            //Copyright and license information (e.g. '(c) 2001 Nobody's Band. All rights reserved' or '(c) 1999 Jack Moffit, distributed under the terms of the Open Audio License. see http://www.eff.org/IP/Open_licenses/eff_oal.html for details')
            {"CONTACT", IDS_OGG_CONTACT, NULL},
            //Contact information for the creators or distributors of the track. This could be a URL, an email address, the physical address of the producing label.
            {"ISRC", IDS_OGG_ISRC, NULL},
            //ISRC number for the track; see the ISRC intro page for more information on ISRC numbers.
            {"DISCID", IDS_OGG_DISCID, NULL},
            //Table of contents hash from an associated disc, generally used to index the track in published music databases. See http://freedb.org/ for and example of such a hash.
            {"VERSION", IDS_OGG_VERSIONS, NULL},
            //The version field may be used to differentiate multiple versions of the same track title in a single collection. (e.g. remix info)
            {"COMMENT", IDS_OGG_COMMENTS, NULL},

            {"", 0, NULL} //TERMINATOR
        };

        int countTags = sizeof(comments) / sizeof(OGG_COMMENT) - 1;

        vorbis_info* vi;
        vorbis_comment* vc;
        TDirectArray<char*> addstr(512, 512);

        vi = ov_info(&vf, -1);

        int s = (int)ov_time_total(&vf, -1); // (s) Total time.
        const std::wstring duration = s / 3600
                                          ? FStrW(L"%02lu:%02lu:%02lu", s / 3600,
                                                  s / 60 % 60, s % 60)
                                          : FStrW(L"%02lu:%02lu", s / 60 % 60, s % 60);

        vc = ov_comment(&vf, -1);
        if (vc)
        {
            char** ptr = vc->user_comments;
            int f;
            int offset;
            while (*ptr)
            {
                char* tmp = SalGeneral->DupStr(*ptr);
                _strupr(tmp);
                // UTF8 is preserved on NT-class OS's
                if (((f = FindComment(tmp, offset, comments)) != -1) && (f < countTags))
                    comments[f].str = SalGeneral->DupStr(*ptr + offset);
                else
                    addstr.Add(SalGeneral->DupStr(*ptr));

                SalGeneral->Free(tmp);
                ++ptr;
            }
        }

        // dump
        output->AddHeader(LangStr(IDS_OGG_INFO).c_str());
        output->AddItem(LangStr(IDS_OGG_BITRATE).c_str(),
                        std::to_wstring(vi->bitrate_nominal / 1000).c_str());
        output->AddItem(LangStr(IDS_OGG_FREQUENCY).c_str(), FormatSize2W(vi->rate).c_str());
        output->AddItem(LangStr(IDS_OGG_CHANNELS).c_str(), std::to_wstring(vi->channels).c_str());
        output->AddItem(LangStr(IDS_OGG_LENGTH).c_str(), duration.c_str());
        output->AddItem(LangStr(IDS_OGG_SAMPLES).c_str(),
                        FormatSize2W(static_cast<long>(ov_pcm_total(&vf, -1))).c_str());
        output->AddItem(LangStr(IDS_OGG_VERSION).c_str(), std::to_wstring(vi->version).c_str());

        BOOL first = TRUE;
        int i;
        for (i = 0; i < countTags; i++)
        {
            if (comments[i].str)
            {
                if (first)
                {
                    output->AddSeparator();
                    output->AddHeader(LangStr(IDS_OGG_STDTAGS).c_str());
                    first = FALSE;
                }
                output->AddItem(LangStr(comments[i].str_id).c_str(), comments[i].str);
                SalGeneral->Free(comments[i].str);
            }
        }

        if (addstr.Count)
        {
            output->AddSeparator();
            output->AddHeader(LangStr(IDS_OGG_ADDINFO).c_str());
            for (i = 0; i < addstr.Count; i++)
            {
                output->AddItem(LangStr(IDS_OGG_OTHERTAG).c_str(), addstr[i]);
                SalGeneral->Free(addstr[i]);
            }
        }

        output->AddSeparator();
        output->AddItem(LangStr(IDS_OGG_VENDOR).c_str(), vc->vendor);

        return preOK;
    }

    return preUnknownFile;
}

#endif
