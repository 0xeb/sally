// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"
#include "dbg.h"

#include "isoimage.h"
#include "uniso_text.h"

#include "uniso.h"
#include "uniso.rh"
#include "uniso.rh2"
#include "lang\lang.rh"

#include "fs.h"

/*
void
CISOImage::CopyDateTimeRecord(CISOImage::CVolumeDateTime &date, BYTE bytes[])
{
#define CpyN(Var, Ofs, N) \
  memcpy(&(date.##Var), bytes + Ofs, N);

  CpyN(Year, 0, 4);
  CpyN(Month, 4, 2);
  CpyN(Day, 6, 2);
  CpyN(Hour, 8, 2);
  CpyN(Minute, 10, 2);
  CpyN(Second, 12, 2);
  CpyN(Second100, 14, 2);
  CpyN(Zone, 16, 1);
#undef CpyN
}
*/

static std::string GetTrackTypeText(CISOImage::Track* track)
{
    CALL_STACK_MESSAGE1("GetTrackTypeText()");

    std::string text;

    switch (track->FSType)
    {
    case CISOImage::fsUnknown:
        text = "Unknown";
        break;
    case CISOImage::fsAudio:
        text = "Audio";
        break;
    case CISOImage::fsISO9660:
        text = "ISO 9660";
        break;
    case CISOImage::fsUDF_ISO9660:
        text = "UDF/ISO 9660";
        break;
    case CISOImage::fsUDF_ISO9660_HFS:
        text = "UDF/ISO 9660/HFS+";
        break;
    case CISOImage::fsUDF_HFS:
        text = "UDF/HFS+";
        break;
    case CISOImage::fsISO9660_HFS:
        text = "ISO 9660/HFS+";
        break;
    case CISOImage::fsHFS:
        text = "HFS+";
        break;
    case CISOImage::fsUDF_ISO9660_APFS:
        text = "UDF/ISO 9660/APFS";
        break;
    case CISOImage::fsUDF_APFS:
        text = "UDF/APFS";
        break;
    case CISOImage::fsISO9660_APFS:
        text = "ISO 9660/APFS";
        break;
    case CISOImage::fsAPFS:
        text = "APFS";
        break;
    case CISOImage::fsUDF:
        text = "UDF";
        break;
    case CISOImage::fsData:
        text = "Data";
        break;
    case CISOImage::fsXbox:
        text = "Xbox";
        break;
    }

    if (track->Bootable)
    {
        text += "/bootable";
    }

    if (track->FSType != CISOImage::fsUnknown && track->FSType != CISOImage::fsAudio && track->FSType != CISOImage::fsHFS)
    {
        if (track->Mode == CISOImage::mMode1)
            text += " (Mode 1)";
        else if (track->Mode == CISOImage::mMode2)
            text += " (Mode 2)";
    }

    return text;
}

static bool WriteLocalizedReportFormat(FILE* outStream, int resourceID, ...) noexcept
{
    try
    {
        std::string format;
        if (!EncodeUnisoReportText(LangStr(resourceID), format))
            return false;

        va_list args;
        va_start(args, resourceID);
        const int written = vfprintf(outStream, format.c_str(), args);
        va_end(args);
        return written >= 0;
    }
    catch (...)
    {
        return false;
    }
}

// The LangStr format strings are narrowed here, and that is correct rather
// than a shortcut: outStream is fopen(..., "w") - a BYTE-oriented stream - and this file
// also writes many narrow literals to it. Mixing fwprintf onto the same FILE* would be
// undefined. The info dump is a narrow text file, so the boundary is the stream itself.
BOOL CISOImage::DumpInfo(FILE* outStream)
{
    CALL_STACK_MESSAGE1("CISOImage::DumpInfo( )");

    std::string imageLabel;
    if (!EncodeUnisoReportText(GetLabel(), imageLabel))
        return FALSE;

    // display information about sessions
    if (!WriteLocalizedReportFormat(outStream, *GetLabel() ? IDS_INFO_LABEL_LABEL : IDS_INFO_LABEL, imageLabel.c_str()) ||
        !WriteLocalizedReportFormat(outStream, IDS_INFO_CNT_SESSIONS, Session.Count))
        return FALSE;

    int session = 0;
    int limit = 0;
    int i;
    for (i = 0; i < Tracks.Count; i++)
    {
        if (i == limit)
        {
            int trks = Session[session];
            limit += Session[session];
            session++;
            if (!WriteLocalizedReportFormat(outStream, IDS_INFO_SESSION_NUM, session) ||
                !WriteLocalizedReportFormat(outStream, IDS_INFO_CNT_TRACKS, trks))
                return FALSE;
        }

        DWORD size = (DWORD)((Tracks[i]->End - Tracks[i]->Start) / 1024);
        const std::string trackType = GetTrackTypeText(Tracks[i]);
        std::string trackLabel;
        if (!EncodeUnisoReportText(Tracks[i]->GetLabel(), trackLabel))
            return FALSE;
        if (!WriteLocalizedReportFormat(outStream, *Tracks[i]->GetLabel() ? IDS_INFO_TRACK_TYPE_SIZE_LABEL : IDS_INFO_TRACK_TYPE_SIZE,
                                        i + 1, trackType.c_str(), size, trackLabel.c_str()))
            return FALSE;
        if (OpenTrack(i, TRUE))
        {
            Tracks[i]->FileSystem->DumpInfo(outStream);
        }
    } // for

    /*
  fprintf(outStream, "Bootable:                      %s\n", Bootable ? LangStr(IDS_YES).c_str(): LangStr(IDS_NO).c_str());
  if (Bootable)
  {
    fprintf(outStream, "Boot System Identifier:        %s\n", MyStrNcpy((char *)BootRecord.BootSystemIdentifier, 32));
    fprintf(outStream, "Boot Identifier:               %s\n", MyStrNcpy((char *)BootRecord.BootIdentifier, 32));
  }

*/
    return TRUE;
}
