// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"
#include "dbg.h"

#include "uniso.h"
#include "isoimage.h"
#include "udfiso.h"
#include "udf_ostacompress.h"
#include "uniso_text.h"

#include "uniso.rh"
#include "uniso.rh2"
#include "lang\lang.rh"

// ****************************************************************************
//
// CUDFISO
//

CUDFISO::CUDFISO(CISOImage* image, DWORD extent) : CUnISOFSAbstract(image)
{
    ExtentOffset = extent;
    ISO = NULL;
    UDF = NULL;
    HFS = NULL;
}

CUDFISO::~CUDFISO()
{
    delete ISO;
    delete UDF;
    delete HFS;
}

BOOL CUDFISO::Open(BOOL quiet)
{
    CALL_STACK_MESSAGE2("CUDFISO::Open(%d)", quiet);

    // scan ISO part
    CISO9660* iso = new CISO9660(Image, ExtentOffset);
    if (!iso)
    {
        Error(IDS_INSUFFICIENT_MEMORY);
        return FALSE;
    }

    if (iso->Open(quiet))
        ISO = iso;
    else
        delete iso;

    // scan udf part
    CUDF* udf = new CUDF(Image, ExtentOffset, 0 /****/);
    if (!udf)
    {
        Error(IDS_INSUFFICIENT_MEMORY);
        return FALSE;
    }

    if (udf->Open(quiet))
        UDF = udf;
    else
        delete udf;

    // scan udf part
    CHFS* hfs = new CHFS(Image);
    if (!hfs)
    {
        Error(IDS_INSUFFICIENT_MEMORY);
        return FALSE;
    }

    // Scan HFS part but silently ignore any problem
    if (hfs->Open((udf || iso) ? TRUE : quiet))
        HFS = hfs;
    else
        delete hfs;

    return TRUE;
}

BOOL CUDFISO::ListDirectory(const std::wstring& path, int session, CSalamanderDirectoryAbstract* dir,
                            CPluginDataInterfaceAbstract*& pluginData)
{
    CALL_STACK_MESSAGE3("CUDFISO::ListDirectory(%ls, %d, , )", path.c_str(), session);

    if (ISO != NULL)
    {
        char volId[33]; // volume identifier for ISO is 32 chars long
        strncpy_s(volId, (char*)ISO->PVD.VolumeIdentifier, _TRUNCATE);
        // trim trailing spaces
        int i;
        for (i = 32; i > 0 && (volId[i] == ' ' || volId[i] == '\0'); i--)
            volId[i] = '\0';
        std::wstring volumeId;
        if (volId[0] != '\0')
            DecodeUnisoLegacyText(volId, volumeId);
        std::wstring partName = volumeId.empty() ? L"ISO partition" : L"ISO (" + volumeId + L")";
        std::wstring partPath(path);
        SPLSalPathAppendOwned(partPath, partName.c_str());

        // avoid creating virtual session folder
        // (we have created ISO virtual folder (for partition) and therefore virtual session folder is not needed)
        if (ISO->BootRecordInfo != NULL && session == -1)
            session = 1;
        ISO->ListDirectory(partPath, session, dir, pluginData);
    }

    if (UDF != NULL)
    {
        const BYTE identifierBytes = UDF->LVD.LogicalVolumeIdentifier[127];
        std::wstring volumeId;
        if (identifierBytes > 0 && identifierBytes <= 127)
            DecodeOSTACompressedOwned(UDF->LVD.LogicalVolumeIdentifier, identifierBytes, volumeId);

        std::wstring partName = volumeId.empty() ? L"UDF partition" : L"UDF (" + volumeId + L")";
        std::wstring partPath(path);
        SPLSalPathAppendOwned(partPath, partName.c_str());
        UDF->ListDirectory(partPath, session, dir, pluginData);
    }

    if (HFS != NULL)
    {
        // HFS+ root names are real HFSUniStr255 Unicode strings (up to 255 UTF-16 code units),
        // unlike the ISO9660/UDF-8bit volume identifiers above which are restricted-charset disk
        // metadata - kept wide end-to-end so a non-ANSI HFS+ volume name isn't best-fit-
        // substituted before it becomes the virtual "\HFS (name)" directory a user navigates
        // into.
        std::wstring rootName;
        std::wstring partName;

        if (HFS->GetRootName(rootName))
            partName = L"HFS (" + rootName + L")";
        else
            partName = L"HFS";

        std::wstring partPath(path);
        SPLSalPathAppendOwned(partPath, partName.c_str());
        HFS->ListDirectory(partPath, session, dir, pluginData);
    }

    return TRUE;
}

int CUDFISO::UnpackFile(CSalamanderForOperationsAbstract* salamander, const std::wstring& path,
                        const std::wstring& nameInArc, const CFileData* fileData, DWORD& silent, BOOL& toSkip)
{
    CALL_STACK_MESSAGE6("CUDFISO::UnpackFile( , %ls, %ls, %p, %u, %d)", path.c_str(), nameInArc.c_str(), fileData, silent, toSkip);

    if (fileData == NULL)
        return UNPACK_ERROR;

    CISOImage::CFilePos* fp = (CISOImage::CFilePos*)fileData->PluginData;
    switch (fp->Type)
    {
    case FS_TYPE_ISO9660:
        return ISO->UnpackFile(salamander, path, nameInArc, fileData, silent, toSkip);

    case FS_TYPE_UDF:
        return UDF->UnpackFile(salamander, path, nameInArc, fileData, silent, toSkip);

    case FS_TYPE_HFS:
        return HFS->UnpackFile(salamander, path, nameInArc, fileData, silent, toSkip);

    default:
        return UNPACK_ERROR;
    }
}

BOOL CUDFISO::DumpInfo(FILE* outStream)
{
    CALL_STACK_MESSAGE1("CUDFISO::DumpInfo( )");

    return ISO->DumpInfo(outStream);
}
