// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"
#include "dbg.h"

#include "uniso.h"
#include "isoimage.h"
#include "iso9660.h"
#include "uniso_text.h"
#include "iso_name_decode.h"

#include "uniso.rh"
#include "uniso.rh2"
#include "lang\lang.rh"

#define SECTOR_SIZE 0x800

#define FATTR_HIDDEN 1
#define FATTR_DIRECTORY 2

#define ISO_SEPARATOR1 '.'
#define ISO_SEPARATOR2 ';'

#define SWAPDWORD(d) \
    ((((d) & 0x000000FF) << 24) | \
     (((d) & 0x0000FF00) << 8) | \
     (((d) & 0x00FF0000) >> 8) | \
     (((d) & 0xFF000000) >> 24))

// ****************************************************************************
//
// CISO9660
//

CISO9660::CDirectoryRecord::CDirectoryRecord()
{
    ZeroMemory(RecordingDateAndTime, sizeof(RecordingDateAndTime));
}

CISO9660::CISO9660(CISOImage* image, DWORD extent) : CUnISOFSAbstract(image)
{
    Ext = extNone;
    ExtentOffset = extent;

    BootRecordInfo = NULL;
}

CISO9660::~CISO9660()
{
    delete BootRecordInfo;
}

BOOL CISO9660::Open(BOOL quiet)
{
    CALL_STACK_MESSAGE2("CISO9660::Open(%d)", quiet);

    BOOL ret = TRUE;
    DWORD block = 0x10;
    BYTE sector[SECTOR_SIZE];
    BOOL terminate = FALSE;

    // detecting CD-ROM Volume Descriptor Set
    BOOL isoDescriptors = FALSE;
    // Read the VolumeDescriptor (2k) until we find it, but only up to to 1MB
    while (!terminate)
    {
        // Try to read a block
        if (Image->ReadBlock(block, SECTOR_SIZE, sector) != SECTOR_SIZE)
        {
            // Something went wrong, probably EOF?
            Error(IDS_ERROR_READING_SECTOR, quiet, block);
            ret = FALSE;
            break;
        }

        if (strncmp((char*)(sector + 1), "CD001", 5) == 0)
        {
            switch (sector[0])
            {
            case 0x00:
                ReadBootRecord(sector, quiet);
                break;

            case 0x01: // primary volume descriptor
                memcpy(&PVD, sector, SECTOR_SIZE);
                break;

            case 0x02: // suplementary/enhanced volume descriptor
                memcpy(&SVD, sector, SECTOR_SIZE);

                // Joliet has FileStructureVersion = 1
                if (SVD.FileStructureVersion == 1)
                    Ext = extJoliet;
                // Patera 2009.02.03: The following check seems correct and not the above one...
                // Raptor provided "Pantera Safari.ISO" with corruped FileStructureVersion
                if ((SVD.EscapeSequences[0] == 0x25) && (SVD.EscapeSequences[1] == 0x2F) && ((SVD.EscapeSequences[2] == 0x40) || (SVD.EscapeSequences[2] == 0x43) || (SVD.EscapeSequences[2] == 0x45)))
                    Ext = extJoliet;
                break;

            case 0xff: // terminator
                terminate = TRUE;
                break;
            } // switch
        } // if

        block++;

        if (block > 128)
        {
            // Just to make sure we don't read in a too big file, stop after 128 sectors.
            ret = FALSE;
            break;
        }
    } // while

    if (ret)
    {
        if (Ext == extJoliet)
            FillDirectoryRecord(Root, SVD.DirectoryRecordForRootDirectory);
        else
            FillDirectoryRecord(Root, PVD.DirectoryRecordForRootDirectory);
    }

    return ret;
}

void CISO9660::FillDirectoryRecord(CDirectoryRecord& dr, BYTE bytes[])
{
#define CpyN(Var, Ofs, N) \
    memcpy(&(dr.##Var), bytes + Ofs, N);

    CpyN(LengthOfDirectoryRecord, 0, 1);
    CpyN(ExtendedAttributeRecordLength, 1, 1);
    CpyN(LocationOfExtent, 2, 4);
    CpyN(LocationOfExtentA, 6, 4);
    CpyN(DataLength, 10, 4);
    CpyN(DataLengthA, 14, 4);
    CpyN(RecordingDateAndTime, 18, 7);
    CpyN(FileFlags, 25, 1);
    CpyN(VolumeSequenceNumber, 28, 2);

    CpyN(LengthOfFileIdentifier, 32, 1);
#undef CpyN
}

void CISO9660::FillPathTableRecord(CPathTableRecord& record, BYTE bytes[])
{
#define CpyN(Var, Ofs, N) \
    memcpy(&(record.##Var), bytes + Ofs, N);

    CpyN(LengthOfDirectoryIdentifier, 0, 1);
    CpyN(ExtendedAttributeRecordLength, 1, 1);
    CpyN(LocationOfExtent, 2, 4);
    CpyN(ParentDirectoryNumber, 6, 2);
#undef CpyN
}

#define ROTATE(a) ((((a) & 0xffff) >> 8) | (((a) & 0xff) << 8))

//
std::string CISO9660::ExtractExtFileName(const char* src, CDirectoryRecord& dr)
{
    // processing extensions
    const char* srcSU = src + dr.LengthOfFileIdentifier;
    const char* srcEnd = src + (dr.LengthOfDirectoryRecord - 34);
    int len = dr.LengthOfDirectoryRecord - 34;

    if (dr.LengthOfFileIdentifier % 2 == 0)
    {
        srcSU++;
    }

    // check whether there is any extension in the 'system use area'
    if (srcSU < srcEnd && (Ext != extJoliet))
    {
        EExt ext = extNone;

        CRRExtHeader rrHeader;
        memcpy(&rrHeader, srcSU, sizeof(rrHeader));

        if (strncmp((char*)rrHeader.Signature, "RR", 2) == 0)
            ext = extRockRidge; // this is a RockRidge entry

        std::string extFileName;

        switch (ext)
        {
        case extRockRidge:
        {
            BOOL bNM = FALSE;

            while (srcSU + sizeof(rrHeader) <= srcEnd)
            {
                memcpy(&rrHeader, srcSU, sizeof(rrHeader));

                if (rrHeader.Length < sizeof(rrHeader) || srcSU + rrHeader.Length > srcEnd)
                    break;

                if (strncmp((char*)rrHeader.Signature, "NM", 2) == 0 && rrHeader.Length >= 5)
                {
                    bNM = TRUE;
                    extFileName.append(srcSU + 5, rrHeader.Length - 5);
                }

                srcSU += rrHeader.Length;
            } // while

            if (bNM)
                return extFileName;
        }
        break;

        default:
            break;
        } // switch
    }

    return std::string(src, dr.LengthOfFileIdentifier);
}

//
// Extracts the file name from 'src'
// used for file names in ISO levels 1, 2, and 3
//
std::string CISO9660::ExtractFileName(const char* src, CISO9660::CDirectoryRecord& dr)
{
    std::string fileName = ExtractExtFileName(src, dr);

    // iso filename
    const size_t sep2 = fileName.find(ISO_SEPARATOR2);
    if (sep2 != std::string::npos)
    {
        const size_t sep1 = fileName.find(ISO_SEPARATOR1);
        fileName.resize(sep1 != std::string::npos && sep2 - sep1 == 1 ? sep1 : sep2);
    }
    return fileName;
}

#define READ_SIZE 4096

// 2026-08-26: the narrow ConvJolietName was deleted - confirmed-dead (zero
// callers anywhere; already-migrated to ConvJolietNameW below, and gtest_win32_isolation.cpp's
// FilesWindowDirectoryReadJolietNameWidthPolicy-adjacent test already asserted the old narrow
// calls were gone from both call sites). It had also lost the unguarded
// WideCharToMultiByte(CP_ACP, 0, ...) best-fit-substitution problem this codebase's technique
// looks for, but the function had no live caller left to corrupt.

std::wstring CISO9660::ConvJolietNameW(const char* src, int nLen)
{
    return DecodeJolietName(src, nLen > 0 ? (size_t)nLen : 0);
}

void CISO9660::ExtractJolietFileNameW(std::wstring& fileName)
{
    StripIsoVersionSuffix(fileName);
}

BOOL CISO9660::AddFileDir(const wchar_t* path, const wchar_t* fileName, CDirectoryRecord& dr,
                          CSalamanderDirectoryAbstract* dir, CPluginDataInterfaceAbstract*& pluginData)
{
    CFileData fd;

    memset(&fd, 0, sizeof(CFileData));
    // fileName is already exact wide (Joliet: decoded directly, never
    // narrowed; non-Joliet: widened once from the always-ASCII-by-spec d-characters) - no
    // conversion needed here any more.
    fd.Name = SalamanderGeneral->DupStr(fileName);
    if (fd.Name == NULL)
    {
        Error(IDS_INSUFFICIENT_MEMORY);
        return FALSE;
    } // if

    fd.NameLen = (int)wcslen(fd.Name);
    wchar_t* s = wcsrchr(fd.Name, L'.');
    if (s != NULL)
        fd.Ext = s + 1; // ".cvspass" is extension in Windows
    else
        fd.Ext = fd.Name + fd.NameLen;

    CISOImage::CFilePos* filePos = new CISOImage::CFilePos;
    if (!filePos)
    {
        Error(IDS_INSUFFICIENT_MEMORY);
        return FALSE;
    }

    filePos->Extent = (DWORD)dr.LocationOfExtent;
    filePos->Type = FS_TYPE_ISO9660;

    char u[1204];
    sprintf(u, "CISO9960::AddFileDir(): name: %ls, Extent: 0x%X", fileName, dr.LocationOfExtent);
    TRACE_I(u);

    fd.PluginData = (DWORD_PTR)filePos;

    // set date & time
    if (memcmp(&dr.RecordingDateAndTime, "\x00\x00\x00\x00\x00\x00\x00",
               sizeof(dr.RecordingDateAndTime)) != 0)
    {
        ISODateTimeToFileTime(dr.RecordingDateAndTime, &fd.LastWrite);
    }
    else
        fd.LastWrite = Image->GetLastWrite();

    fd.DosName = NULL;

    fd.Attr = FILE_ATTRIBUTE_READONLY; // Everything is read-only by default
    fd.Hidden = 0;
    fd.IsOffline = 0;
    if (dr.FileFlags & FATTR_HIDDEN)
    {
        fd.Attr |= FILE_ATTRIBUTE_HIDDEN;
        fd.Hidden = 1;
    }

    if (dr.FileFlags & FATTR_DIRECTORY)
    {
        fd.Size = CQuadWord(0, 0);
        fd.Attr |= FILE_ATTRIBUTE_DIRECTORY;
        fd.IsLink = 0;

        if (!SortByExtDirsAsFiles)
            fd.Ext = fd.Name + fd.NameLen; // directories do not have an extension

        if (dir && !dir->AddDir(path, fd, pluginData))
        {
            free(fd.Name);
            delete filePos;
            dir->Clear(pluginData);
            return Error(IDS_ERROR);
        }
    }
    else
    {
        fd.Size = CQuadWord(dr.DataLength, 0);

        // file
        fd.IsLink = SalamanderGeneral->IsFileLink(fd.Ext);
        if (dir && !dir->AddFile(path, fd, pluginData))
        {
            free(fd.Name);
            dir->Clear(pluginData);
            delete filePos;
            return Error(IDS_ERROR);
        }
    }

    return TRUE;
}

const wchar_t* CISO9660::GetBootRecordTypeStr(EBootRecordType type)
{
    CALL_STACK_MESSAGE1("CISO9660::GetBootRecordTypeStr()");

    switch (type)
    {
    case biNoEmul:
        return L"-noemul";
    case bi120:
        return L"-1.2";
    case bi144:
        return L"-1.44";
    case bi288:
        return L"-2.88";
    case biHDD:
        return L"-hdd";
    } // switch

    return L"-";
}

BOOL CISO9660::AddBootRecord(std::wstring& path, int session,
                             CSalamanderDirectoryAbstract* dir, CPluginDataInterfaceAbstract*& pluginData)
{
    // add boot image file
    if (BootRecordInfo != NULL)
    {

        CDirectoryRecord dr;
        ZeroMemory(&dr, sizeof(dr));

        // set up the virtual directory record
        dr.FileFlags = 0;
        dr.LocationOfExtent = BootRecordInfo->LoadRBA;
        //    dr.RecordingDateAndTime = ;
        dr.DataLength = BootRecordInfo->Length;

        std::wstring sessionNumber = std::to_wstring(session == -1 ? 1 : session);
        if (sessionNumber.size() < 2)
            sessionNumber.insert(sessionNumber.begin(), L'0');
        const std::wstring fileName = L"s" + sessionNumber + L"-bootdisk" + GetBootRecordTypeStr(BootRecordInfo->Type) + L".ima";
        if (session == -1)
        {
            session = 1;
            if (!AddFileDir(L"\\", fileName.c_str(), dr, dir, pluginData))
                return FALSE;
            SPLSalPathAppendOwned(path, L"Session 01");
        }
        else
        {
            if (!AddFileDir(L"\\", fileName.c_str(), dr, dir, pluginData))
                return FALSE;
        }
    }

    return TRUE;
}

BOOL CISO9660::ListDirectory(const std::wstring& path, int session,
                             CSalamanderDirectoryAbstract* dir, CPluginDataInterfaceAbstract*& pluginData)
{
    CALL_STACK_MESSAGE3("CISO9660::ListDirectory(%ls, %d, , )", path.c_str(), session);

    std::wstring listingPath(path);
    AddBootRecord(listingPath, session, dir, pluginData);
    return ListDirectoryRe(listingPath, &Root, dir, pluginData) != ERR_TERMINATE;
}

//
int CISO9660::ListDirectoryRe(const std::wstring& path, CDirectoryRecord* root,
                              CSalamanderDirectoryAbstract* dir, CPluginDataInterfaceAbstract*& pluginData)
{
    if (root == NULL)
        return ERR_TERMINATE;

    DWORD block = (DWORD)root->LocationOfExtent - ExtentOffset + root->ExtendedAttributeRecordLength;
    int size = root->DataLength;

    if (block == 0 || size == 0)
        return ERR_TERMINATE;

    char* data = new char[size];
    if (data == NULL)
    {
        Error(IDS_INSUFFICIENT_MEMORY);
        return ERR_TERMINATE;
    }

    if (Image->ReadBlock(block, size, data) != (DWORD)size)
    {
        delete[] data;
        Error(IDS_ERROR_LISTING_IMAGE, FALSE, block);
        // if reading the sector with the root fails
        return (block == (DWORD)Root.LocationOfExtent - ExtentOffset) ? ERR_CONTINUE : ERR_TERMINATE;
    }

    int ret = ERR_OK;
    int offset = 0;
    while (offset < size - 0x21 && ret != ERR_TERMINATE)
    {
        while (!data[offset] && offset < size - 0x21)
            offset++;

        if (offset >= size - 0x21)
            break;

        //
        CDirectoryRecord dirRecord;

        FillDirectoryRecord(dirRecord, (BYTE*)(data + offset));

        // Safety check
        if ((dirRecord.LocationOfExtent != SWAPDWORD(dirRecord.LocationOfExtentA)) || (dirRecord.DataLength != SWAPDWORD(dirRecord.DataLengthA)))
        {
            Error(IDS_ERROR_LISTING_IMAGE, FALSE, block);
            return ERR_TERMINATE;
        }

        if (dirRecord.FileFlags & FATTR_DIRECTORY)
        {
            if (Ext == extJoliet)
            {
                if (dirRecord.LengthOfFileIdentifier > 1)
                {
                    // Joliet names are UTF-16BE, unambiguous - decode
                    // straight to wide (ConvJolietNameW), no CP_ACP narrowing, so a directory
                    // name outside the machine's code page is not corrupted before it becomes
                    // the real fd.Name and the recursion path component below.
                    const std::wstring dirNameW = ConvJolietNameW((data + offset + 33), dirRecord.LengthOfFileIdentifier);
                    if (AddFileDir(path.c_str(), dirNameW.c_str(), dirRecord, dir, pluginData))
                    {
                        std::wstring childPath(path);
                        SPLSalPathAppendOwned(childPath, dirNameW.c_str());
                        // descend only when everything is OK
                        if (ret == ERR_OK)
                        {
                            ret = ListDirectoryRe(childPath, &dirRecord, dir, pluginData);
                            // if we surface with a termination error, keep processing as much as possible
                            if (ret == ERR_TERMINATE)
                                ret = ERR_CONTINUE;
                        }
                    }
                    else
                        ret = ERR_TERMINATE;
                } // if
            }
            else
            {
                char firstChar = data[offset + 33];
                if (firstChar != 0x00 && firstChar != 0x01)
                {
                    const std::string extFileName = ExtractExtFileName((data + offset + 33), dirRecord);
                    std::wstring extFileNameW;
                    if (!DecodeUnisoLegacyText(extFileName, extFileNameW))
                    {
                        Error(IDS_INSUFFICIENT_MEMORY);
                        ret = ERR_TERMINATE;
                        break;
                    }

                    if (AddFileDir(path.c_str(), extFileNameW.c_str(), dirRecord, dir, pluginData))
                    {
                        std::wstring childPath(path);
                        SPLSalPathAppendOwned(childPath, extFileNameW.c_str());
                        //          TRACE_I(path);
                        // descend only when everything is OK
                        if (ret == ERR_OK)
                        {
                            ret = ListDirectoryRe(childPath, &dirRecord, dir, pluginData);
                            // if we surface with a termination error, keep processing as much as possible
                            if (ret == ERR_TERMINATE)
                                ret = ERR_CONTINUE;
                        }
                    }
                    else
                        ret = ERR_TERMINATE;
                } // if
            } // if
        }
        else
        {
            if (Ext == extJoliet)
            {
                // Decode straight to wide and strip the trailing
                // ';version' there too - no CP_ACP round trip for the real extraction-target
                // filename (fd.Name feeds SalPathAppend in UnpackFile).
                std::wstring fileNameW = ConvJolietNameW((data + offset + 33), dirRecord.LengthOfFileIdentifier);
                ExtractJolietFileNameW(fileNameW);

                if (!AddFileDir(path.c_str(), fileNameW.c_str(), dirRecord, dir, pluginData))
                    ret = ERR_TERMINATE;
            }
            else
            {
                const std::string fileName = ExtractFileName((data + offset + 33), dirRecord);
                std::wstring fileNameW;
                if (!DecodeUnisoLegacyText(fileName, fileNameW))
                {
                    Error(IDS_INSUFFICIENT_MEMORY);
                    ret = ERR_TERMINATE;
                    break;
                }

                if (!AddFileDir(path.c_str(), fileNameW.c_str(), dirRecord, dir, pluginData))
                    ret = ERR_TERMINATE;
            }
        } // if

        offset += dirRecord.LengthOfDirectoryRecord;
    } // while

    delete[] data;

    return ret;
}

int CISO9660::UnpackFile(CSalamanderForOperationsAbstract* salamander, const std::wstring& path, const std::wstring& nameInArc,
                         const CFileData* fileData, DWORD& silent, BOOL& toSkip)
{
    CALL_STACK_MESSAGE5("CISO9660::UnpackFile( , %ls, %ls, , %u, %d)", path.c_str(), nameInArc.c_str(), silent, toSkip);

    ///
    std::wstring name(path);
    SPLSalPathAppendOwned(name, fileData->Name);

    FILETIME ft = fileData->LastWrite;
    const std::wstring fileInfo = GetInfo(&ft, fileData->Size);

    DWORD attrs = fileData->Attr;

    HANDLE hFile = SalamanderSafeFile->SafeFileCreate(name.c_str(), GENERIC_WRITE, FILE_SHARE_READ, attrs, FALSE,
                                                      SalamanderGeneral->GetMainWindowHWND(), nameInArc.c_str(), fileInfo.c_str(),
                                                      &silent, TRUE, &toSkip, NULL, 0, NULL, NULL);

    CBufferedFile file(hFile, GENERIC_WRITE);
    // set file time
    file.SetFileTime(&ft, &ft, &ft);

    // the overall operation can continue further: skip only
    if (toSkip)
        return UNPACK_ERROR;

    // the overall operation cannot continue any further: cancel
    if (hFile == INVALID_HANDLE_VALUE)
        return UNPACK_CANCEL;

    ///

    BOOL bFileComplete = TRUE;

    BOOL ret = UNPACK_OK;
    CISOImage::CFilePos* fp = (CISOImage::CFilePos*)fileData->PluginData;
    DWORD block = fp->Extent - ExtentOffset;
    CQuadWord remain = fileData->Size;
    DWORD sectorUserSize = 0x800;
    DWORD nbytes = sectorUserSize;
    BYTE* buffer = new BYTE[sectorUserSize];
    if (!buffer)
    {
        Error(IDS_INSUFFICIENT_MEMORY);
        return UNPACK_CANCEL;
    }
    /*
  char u[1024];
  sprintf(u, "CISO9660::UnpackFile(): opening Extent: %x, Size: %d", block, remain);
  TRACE_I(u);
*/
    while (remain.Value > 0)
    {
        if (remain.Value < nbytes)
            nbytes = remain.LoDWord; // !!! the buffer size must not exceed DWORD

        if (!Image->ReadBlock(block, sectorUserSize, buffer))
        {
            if (silent == 0)
            {
                const std::wstring error = SPLFormatStringOwned(LangStr(IDS_ERROR_READING_SECTOR).c_str(), block);
                int userAction = SalamanderGeneral->DialogError(SalamanderGeneral->GetMsgBoxParent(), BUTTONS_SKIPCANCEL,
                                                                fileData->Name, error.c_str(), LangStr(IDS_READERROR).c_str());

                switch (userAction)
                {
                case DIALOG_CANCEL:
                    ret = UNPACK_CANCEL;
                    break;
                case DIALOG_SKIP:
                    ret = UNPACK_ERROR;
                    break;
                case DIALOG_SKIPALL:
                    ret = UNPACK_ERROR;
                    silent = 1;
                    break;
                }
            }

            bFileComplete = FALSE;
            break;
        }

        block++;

        if (!salamander->ProgressAddSize(nbytes, TRUE)) // delayedPaint==TRUE, so we do not slow things down
        {
            salamander->ProgressDialogAddText(LangStr(IDS_CANCELING_OPERATION).c_str(), FALSE);
            salamander->ProgressEnableCancel(FALSE);

            ret = UNPACK_CANCEL;
            bFileComplete = FALSE;
            break; // action interrupted
        }

        ULONG written;
        if (!file.Write(buffer, nbytes, &written, name.c_str(), NULL))
        {
            // Error message was already displayed by SafeWriteFile()
            ret = UNPACK_CANCEL;
            bFileComplete = FALSE;
            break;
        }
        remain.Value -= nbytes;
    } // while

    delete[] buffer;

    //  sprintf(u, "CISOImage::UnpackFile(): ret: %d, bFileComplete: %d", ret, bFileComplete);
    //  TRACE_I(u);

    if (!file.Close(name.c_str(), NULL))
    {
        // Flushing cache may fail
        ret = UNPACK_CANCEL;
        bFileComplete = FALSE;
    }

    if (!bFileComplete)
    {
        // because it was created with the read-only attribute, we must clear
        // the R attribute so the file can be deleted
        attrs &= ~FILE_ATTRIBUTE_READONLY;
        if (!SetFileAttributesW(name.c_str(), attrs))
            Error(LangStr(IDS_CANT_SET_ATTRS).c_str(), GetLastError());

        // the user cancelled the operation
        // delete the incomplete file afterwards
        if (!DeleteFileW(name.c_str()))
            Error(LangStr(IDS_CANT_DELETE_TEMP_FILE).c_str(), GetLastError());
    }
    else
        SetFileAttrs(name.c_str(), attrs);

    return ret;
}

BOOL CISO9660::DumpInfo(FILE* outStream)
{
    CALL_STACK_MESSAGE1("CISO9660::DumpInfo()");

    const auto writeField = [outStream](const char* label, const BYTE* data, size_t count) {
        std::string value;
        if (!CopyUnisoReportField(std::string_view(reinterpret_cast<const char*>(data), count), value))
            return false;
        return value.empty() || fprintf(outStream, "%s%s\n", label, value.c_str()) >= 0;
    };

    // display info from the PVD
    if (!writeField("    System Identifier:        ", PVD.SystemIdentifier, 32) ||
        !writeField("    Volume:                   ", PVD.VolumeIdentifier, 32) ||
        !writeField("    Volume Set:               ", PVD.VolumeSetIdentifier, 128) ||
        !writeField("    Publisher:                ", PVD.PublisherIdentifier, 128) ||
        !writeField("    Data Preparer:            ", PVD.DataPreparerIdentifier, 128) ||
        !writeField("    Application:              ", PVD.ApplicationIdentifier, 128) ||
        !writeField("    Copyright File:           ", PVD.CopyrightFileIdentifier, 37) ||
        !writeField("    Abstract File:            ", PVD.AbstractFileIdentifier, 37) ||
        !writeField("    Bibliographic File:       ", PVD.BibliographicFileIdentifier, 37))
        return FALSE;

    //  fprintf(outStream, "\n");

    SYSTEMTIME st;
    ISODateTimeStrToSystemTime(PVD.VolumeCreationDateAndTime, &st);
    if (st.wYear != 0)
    {
        std::string formatted;
        if (!FormatUnisoReportSystemTime(st, formatted) || fprintf(outStream, "    Volume Creation Date:     %s\n", formatted.c_str()) < 0)
            return FALSE;
    }

    ISODateTimeStrToSystemTime(PVD.VolumeModificationDateAndTime, &st);
    if (st.wYear != 0)
    {
        std::string formatted;
        if (!FormatUnisoReportSystemTime(st, formatted) || fprintf(outStream, "    Volume Modification Date: %s\n", formatted.c_str()) < 0)
            return FALSE;
    }

    ISODateTimeStrToSystemTime(PVD.VolumeExpirationDateAndTime, &st);
    if (st.wYear != 0)
    {
        std::string formatted;
        if (!FormatUnisoReportSystemTime(st, formatted) || fprintf(outStream, "    Volume Expiration Date:   %s\n", formatted.c_str()) < 0)
            return FALSE;
    }

    ISODateTimeStrToSystemTime(PVD.VolumeEffectiveDateAndTime, &st);
    if (st.wYear != 0)
    {
        std::string formatted;
        if (!FormatUnisoReportSystemTime(st, formatted) || fprintf(outStream, "    Volume Effective Date:    %s\n", formatted.c_str()) < 0)
            return FALSE;
    }

    return TRUE;
}

BOOL CISO9660::ReadBootRecord(BYTE* data, BOOL quiet)
{
    CALL_STACK_MESSAGE2("CISO9660::ReadBootRecord(, %d)", quiet);

    memcpy(&BR, data, sizeof(BR));

    BOOL result = FALSE;

    if (!Options.BootImageAsFile)
        return FALSE;

    // check for El Torito specification
    if (memcmp(BR.BootSystemIdentifier + 0, "EL TORITO SPECIFICATION", 23) == 0)
    {
        DWORD sector = 0;
        memcpy(&sector, data + 0x47, sizeof(DWORD));

        BYTE* catalog = new BYTE[SECTOR_SIZE];
        if (catalog == NULL)
        {
            return Error(IDS_INSUFFICIENT_MEMORY, quiet);
        }

        if (Image->ReadBlock(sector - ExtentOffset, SECTOR_SIZE, catalog) == SECTOR_SIZE)
        {
            result = ReadElTorito(catalog, SECTOR_SIZE, quiet);
        }
        else
        {
            result = Error(IDS_ERROR_READING_SECTOR, FALSE, sector - ExtentOffset);
        }

        delete[] catalog;
    }

    // and finally
    return result;
}
