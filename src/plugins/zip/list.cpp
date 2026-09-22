// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"
#include <crtdbg.h>
#include <ostream>
#include <commctrl.h>
#include <tchar.h>
#include <vector>

#include "spl_com.h"
#include "spl_base.h"
#include "spl_gen.h"
#include "spl_arc.h"
#include "spl_menu.h"
#include "dbg.h"

#include "array2.h"

#include "selfextr/comdefs.h"

#include "config.h"
#include "typecons.h"
#include "zip.rh2"
#include "chicon.h"
#include "common.h"
#include "list.h"

int CZipList::ListArchive(CSalamanderDirectoryAbstract* dir, BOOL& haveFiles)
{
    CALL_STACK_MESSAGE1("CZipList::ListArchive( )");
    int ret;

    haveFiles = FALSE;
    ret = CreateCFile(&ZipFile, ZipName.c_str(), GENERIC_READ, FILE_SHARE_READ,
                      OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, PE_NOSKIP, NULL,
                      true, true); // set 'useReadCache' to TRUE -> optimized central directory reading
    if (ret)
        if (ret == ERR_LOWMEM)
            return ErrorID = IDS_LOWMEM;
        else
            return ErrorID = IDS_NODISPLAY;
    ErrorID = CheckZip();
    if (!ErrorID)
        return ErrorID = List(dir, haveFiles);
    return ErrorID;
}

int CZipList::List(CSalamanderDirectoryAbstract* dir, BOOL& haveFiles)
{
    CALL_STACK_MESSAGE1("CZipList::List()");
    CFileHeader* centralHeader;
    CFileInfo fileInfo;
    CFileData file;
    int errorID = 0;
    QWORD readOffset;
    //  char *              pathBuf;
    const char* path;
    char* name;

    if (ZeroZip)
        return 0;
    centralHeader = (CFileHeader*)malloc(MAX_HEADER_SIZE);
    fileInfo.Name = (char*)malloc(sizeof(char) * MAX_HEADER_SIZE);
    //  pathBuf = (char *) malloc( MAX_HEADER_SIZE);
    // Wide scratch buffer for ProcessNameW - see its own comment in common.cpp. Kept
    // alongside the narrow fileInfo.Name (still used for diagnostics/length checks below,
    // unchanged) rather than replacing it, to avoid touching that existing narrow logic.
    std::vector<wchar_t> wideNameBuf(MAX_HEADER_SIZE);
    if (!centralHeader || !fileInfo.Name /* || !pathBuf*/)
    {
        if (centralHeader)
            free(centralHeader);
        //if (fileInfo.Name ) free(fileInfo.Name); freed in destructor
        //    if (pathBuf)
        //      free(pathBuf);
        return IDS_LOWMEM;
    }

START_LIST:
    // TRACE_I("zip listing started");

    haveFiles = FALSE;
    readOffset = CentrDirOffs + ExtraBytes;
    if (DiskNum != CentrDirStartDisk && MultiVol)
    {
        DiskNum = CentrDirStartDisk;
        errorID = ChangeDisk();
    }
    if (!errorID)
    {
        int sortByExtDirsAsFiles;
        int cnt = 0;

        SalamanderGeneral->GetConfigParameter(SALCFG_SORTBYEXTDIRSASFILES, &sortByExtDirsAsFiles,
                                              sizeof(sortByExtDirsAsFiles), NULL);

        QWORD readSize;
        for (readSize = 0; readSize < CentrDirSize; cnt++)
        {
            unsigned int s;
            int err = ReadCentralHeader(centralHeader, &readOffset, &s);
            if (err)
            {
                errorID = err;
                break;
            }
            readSize += s;
            if (centralHeader->Version >> 8 == HS_UNIX && !Unix)
            {
                Unix = TRUE;
                dir->Clear(NULL);
                dir->SetFlags(SALDIRFLAG_CASESENSITIVE);
                goto START_LIST;
            }
            ProcessName(centralHeader, fileInfo.Name);
            ProcessHeader(centralHeader, &fileInfo);
            //      path = pathBuf;
            //      SplitPath(&path, &name, fileInfo.Name);
            // j.r. optimization instead of SplitPath
            path = fileInfo.Name;
            // strrchr works correctly on MBCS file names
            name = strrchr(fileInfo.Name, '\\');
            /*      name = fileInfo.Name + fileInfoNameLen;
      while (name > fileInfo.Name && *name != '\\')
        name--;*/
            if (name && (*name == '\\'))
            {
                *name = 0;
                name++;
            }
            else
            {
                name = fileInfo.Name;
                path = "";
            }
            // ProcessNameW shares ProcessName's archive decoder and supplies UTF-16 directly
            // to the host directory model. The byte path/name fields above are normalized UTF-8.
            ProcessNameW(centralHeader, wideNameBuf.data());
            wchar_t* wideName = wideNameBuf.data();
            wchar_t* wideSlash = wcsrchr(wideName, L'\\');
            std::wstring pathW, nameW;
            if (wideSlash)
            {
                pathW.assign(wideName, wideSlash);
                nameW.assign(wideSlash + 1);
            }
            else
            {
                nameW.assign(wideName);
            }

            file.NameLen = (int)nameW.size();
            file.Name = (wchar_t*)SalamanderGeneral->Alloc(sizeof(wchar_t) * (file.NameLen + 1));
            if (!file.Name)
            {
                errorID = IDS_LOWMEM;
                break;
            }
            memcpy(file.Name, nameW.c_str(), sizeof(wchar_t) * (file.NameLen + 1));
            //initialize remaining members of CFileData
            file.Size = CQuadWord().SetUI64(fileInfo.Size);
            file.Attr = fileInfo.FileAttr & FILE_ATTTRIBUTE_MASK;
            if (fileInfo.Flag & GPF_ENCRYPTED)
                file.Attr |= FILE_ATTRIBUTE_ENCRYPTED;
            file.LastWrite = fileInfo.LastWrite;
            file.DosName = NULL;
            file.Hidden = file.Attr & FILE_ATTRIBUTE_HIDDEN ? 1 : 0;
            file.PluginData = (DWORD_PTR) new CZIPFileData(fileInfo.CompSize, cnt, Unix);
            if (!file.PluginData)
            {
                SalamanderGeneral->Free(file.Name);
                errorID = IDS_LOWMEM;
                break;
            }

            if (!sortByExtDirsAsFiles && fileInfo.IsDir)
            {
                file.Ext = file.Name + file.NameLen; // directories do not have extensions
            }
            else
            {
                wchar_t* dot = file.Name + file.NameLen - 1;
                //search backward for last dot
                for (; dot >= file.Name && *dot != L'.'; dot--)
                    ; // ".cvspass" is extension in Windows
                //dot found?
                if (dot >= file.Name)
                    file.Ext = dot + 1;
                else
                    file.Ext = file.Name + file.NameLen;
            }
            file.IsOffline = 0;
            if (fileInfo.IsDir)
            {
                file.IsLink = 0;
                if (!dir->AddDir(pathW.c_str(), file, NULL))
                {
                    delete (CZIPFileData*)file.PluginData;
                    // file.Name is wide (ProcessNameW, see the comment above); pathW is its
                    // matching wide counterpart already in scope, so this trace uses TRACE_EW
                    // instead of mixing a wide pointer into the narrow TRACE_E ostream.
                    TRACE_EW(pathW.c_str() << L"\\" << file.Name << L" in the list");
                    SalamanderGeneral->Free(file.Name);
                    errorID = IDS_ERRADDDIR;
                    break;
                }
            }
            else
            {
                file.IsLink = SalamanderGeneral->IsFileLink(file.Ext);
                if (!dir->AddFile(pathW.c_str(), file, NULL))
                {
                    delete (CZIPFileData*)file.PluginData;
                    // file.Name is wide (ProcessNameW, see the comment above); pathW is its
                    // matching wide counterpart already in scope, so this trace uses TRACE_EW
                    // instead of mixing a wide pointer into the narrow TRACE_E ostream.
                    TRACE_EW(pathW.c_str() << L"\\" << file.Name << L" to the list");
                    SalamanderGeneral->Free(file.Name);
                    errorID = IDS_ERRADDFILE;
                    break;
                }
            }

            /*
      TRACE_I("Listing file: " << fileInfo.Name <<
              ", method: " << fileInfo.Method <<
              ", flag: " << fileInfo.Flag <<
              ", isdir: " << fileInfo.IsDir <<
              ", file attr: " << fileInfo.FileAttr);
*/
        }
        haveFiles = cnt > 0;
    }
    free(centralHeader);
    //free(fileInfo.Name); handled in the destructor
    //  free(pathBuf);

    // TRACE_I("zip listing finished");

    return errorID;
}
