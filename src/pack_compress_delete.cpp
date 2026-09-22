// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"

#include "cfgdlg.h"
#include "zip.h"
#include "plugins.h"
#include "pack.h"
#include "common/IFileSystem.h"
#include "common/PackerCommandLinePolicy.h"
#include "common/Win32TextCodec.h"
#include "common/unicode/helpers.h"
#include "common/fsutil.h"

//
// ****************************************************************************
// Constants and global variables
// ****************************************************************************
//

// Table of archive definitions and how to handle them - modifying operations
// !!! WARNING: when changing the order of external archivers, the order in the
// externalArchivers array in the CPlugins::FindViewEdit method must be changed
// as well
const SPackModifyTable PackModifyTable[] =
    {
        // JAR 1.02 Win32
        {
            (TPackErrorTable*)&JARErrors, TRUE,
            L"$(SourcePath)", L"$(Jar32bitExecutable) a -hl \"$(ArchiveFullName)\" -o\"$(TargetPath)\" !\"$(ListFullName)\"", TRUE,
            L"$(ArchivePath)", L"$(Jar32bitExecutable) d -r- \"$(ArchiveFileName)\" !\"$(ListFullName)\"", PMT_EMPDIRS_DELETE,
            L"$(SourcePath)", L"$(Jar32bitExecutable) m -hl \"$(ArchiveFullName)\" -o\"$(TargetPath)\" !\"$(ListFullName)\"", FALSE},
        // RAR 4.20 & 5.0 Win x86/x64
        {
            (TPackErrorTable*)&RARErrors, TRUE,
            L"$(SourcePath)", L"$(Rar32bitExecutable) a -scol \"$(ArchiveFullName)\" -ap\"$(TargetPath)\" @\"$(ListFullName)\"", TRUE, // since version 5.0 we must enforce the -scol switch, version 4.20 is fine; it appears elsewhere and in the registry
            L"$(ArchivePath)", L"$(Rar32bitExecutable) d -scol \"$(ArchiveFileName)\" @\"$(ListFullName)\"", PMT_EMPDIRS_DELETE,
            L"$(SourcePath)", L"$(Rar32bitExecutable) m -scol \"$(ArchiveFullName)\" -ap\"$(TargetPath)\" @\"$(ListFullName)\"", FALSE},
        // ARJ 2.60 MS-DOS
        {
            (TPackErrorTable*)&ARJErrors, FALSE,
            L"$(SourcePath)", L"$(Arj16bitExecutable) a -p -va -hl -a $(ArchiveDOSFullName) !$(ListDOSFullName)", FALSE,
            L".", L"$(Arj16bitExecutable) d -p -va -hl $(ArchiveDOSFullName) !$(ListDOSFullName)", PMT_EMPDIRS_DONOTDELETE,
            L"$(SourcePath)", L"$(Arj16bitExecutable) m -p -va -hl -a $(ArchiveDOSFullName) !$(ListDOSFullName)", FALSE},
        // LHA 2.55 MS-DOS
        {
            (TPackErrorTable*)&LHAErrors, FALSE,
            L"$(SourcePath)", L"$(Lha16bitExecutable) a -m -p -a -l1 -x1 -c $(ArchiveDOSFullName) @$(ListDOSFullName)", FALSE,
            L".", L"$(Lha16bitExecutable) d -p -a -l1 -x1 -c $(ArchiveDOSFullName) @$(ListDOSFullName)", PMT_EMPDIRS_DELETEWITHASTERISK,
            L"$(SourcePath)", L"$(Lha16bitExecutable) m -m -p -a -l1 -x1 -c $(ArchiveDOSFullName) @$(ListDOSFullName)", FALSE},
        // UC2 2r3 PRO MS-DOS
        {
            (TPackErrorTable*)&UC2Errors, FALSE,
            L"$(SourcePath)", L"$(UC216bitExecutable) A !SYSHID=ON $(ArchiveDOSFullName) ##$(TargetPath) @$(ListDOSFullName)", TRUE,
            L".", L"$(UC216bitExecutable) D $(ArchiveDOSFullName) @$(ListDOSFullName) & $$RED $(ArchiveDOSFullName)", PMT_EMPDIRS_DONOTDELETE,
            L"$(SourcePath)", L"$(UC216bitExecutable) AM !SYSHID=ON $(ArchiveDOSFullName) ##$(TargetPath) @$(ListDOSFullName)", FALSE},
        // JAR 1.02 MS-DOS
        {
            (TPackErrorTable*)&JARErrors, FALSE,
            L"$(SourcePath)", L"$(Jar16bitExecutable) a -hl $(ArchiveDOSFullName) -o\"$(TargetPath)\" !$(ListDOSFullName)", TRUE,
            L"$(ArchivePath)", L"$(Jar16bitExecutable) d -r- $(ArchiveDOSFileName) !$(ListDOSFullName)", PMT_EMPDIRS_DELETE,
            L"$(SourcePath)", L"$(Jar16bitExecutable) m -hl $(ArchiveDOSFullName) -o\"$(TargetPath)\" !$(ListDOSFullName)", FALSE},
        // RAR 2.50 MS-DOS
        {
            (TPackErrorTable*)&RARErrors, FALSE,
            L"$(SourcePath)", L"$(Rar16bitExecutable) a $(ArchiveDOSFullName) @$(ListDOSFullName)", FALSE, // P.S. ability to pack into subdirectories removed
            L"$(ArchivePath)", L"$(Rar16bitExecutable) d $(ArchiveDOSFileName) @$(ListDOSFullName)", PMT_EMPDIRS_DELETE,
            L"$(SourcePath)", L"$(Rar16bitExecutable) m $(ArchiveDOSFullName) @$(ListDOSFullName)", FALSE // P.S. ability to pack into subdirectories removed
        },
        // PKZIP 2.50 Win32
        {
            NULL, TRUE,
            L"$(SourcePath)", L"$(Zip32bitExecutable) -add -nozipextension -attr -path \"$(ArchiveFullName)\" @\"$(ListFullName)\"", FALSE,
            L"$(ArchivePath)", L"$(Zip32bitExecutable) -del -nozipextension \"$(ArchiveFileName)\" @\"$(ListFullName)\"", PMT_EMPDIRS_DONOTDELETE,
            L"$(SourcePath)", L"$(Zip32bitExecutable) -add -nozipextension -attr -path -move \"$(ArchiveFullName)\" @\"$(ListFullName)\"", TRUE},
        // PKZIP 2.04g MS-DOS
        {
            (TPackErrorTable*)&ZIP204Errors, FALSE,
            L"$(SourcePath)", L"$(Zip16bitExecutable) -a -P -whs $(ArchiveDOSFullName) @$(ListDOSFullName)", FALSE,
            L".", L"$(Zip16bitExecutable) -d $(ArchiveDOSFullName) @$(ListDOSFullName)", PMT_EMPDIRS_DONOTDELETE,
            L"$(SourcePath)", L"$(Zip16bitExecutable) -m -P -whs $(ArchiveDOSFullName) @$(ListDOSFullName)", FALSE},
        // ARJ 3.00c Win32
        {
            (TPackErrorTable*)&ARJErrors, TRUE,
            L"$(SourcePath)", L"$(Arj32bitExecutable) a -p -va -hl -a \"$(ArchiveFullName)\" !\"$(ListFullName)\"", FALSE,
            L"$(ArchivePath)", L"$(Arj32bitExecutable) d -p -va -hl \"$(ArchiveFileName)\" !\"$(ListFullName)\"", PMT_EMPDIRS_DONOTDELETE,
            L"$(SourcePath)", L"$(Arj32bitExecutable) m -p -va -hl -a \"$(ArchiveFullName)\" !\"$(ListFullName)\"", FALSE},
        // ACE 1.2b Win32
        {
            (TPackErrorTable*)&ACEErrors, TRUE,
            L"$(SourcePath)", L"$(Ace32bitExecutable) a -o -f \"$(ArchiveFullName)\" @\"$(ListFullName)\"", FALSE,
            L"$(ArchivePath)", L"$(Ace32bitExecutable) d -f \"$(ArchiveFileName)\" @\"$(ListFullName)\"", PMT_EMPDIRS_DONOTDELETE,
            L"$(SourcePath)", L"$(Ace32bitExecutable) m -o -f \"$(ArchiveFullName)\" @\"$(ListFullName)\"", TRUE},
        // ACE 1.2b MS-DOS
        {
            (TPackErrorTable*)&ACEErrors, FALSE,
            L"$(SourcePath)", L"$(Ace16bitExecutable) a -o -f $(ArchiveDOSFullName) @$(ListDOSFullName)", FALSE,
            L".", L"$(Ace16bitExecutable) d -f $(ArchiveDOSFullName) @$(ListDOSFullName)", PMT_EMPDIRS_DONOTDELETE,
            L"$(SourcePath)", L"$(Ace16bitExecutable) m -o -f $(ArchiveDOSFullName) @$(ListDOSFullName)", FALSE}};

//
// ****************************************************************************
// Functions
// ****************************************************************************
//

// The encoding decisions themselves live in PackerCommandLinePolicy.h so they can be
// tested without a file handle; these two only move the resulting bytes.
static bool WriteListFileBom(FILE* listFile, sally::pack::EListFileEncoding encoding)
{
    const std::string bom = sally::pack::ListFileBom(encoding);
    return bom.empty() || fwrite(bom.c_str(), 1, bom.length(), listFile) == bom.length();
}

// Writes one entry plus its terminator in the encoding the archiver expects.
static bool WriteListFileEntry(FILE* listFile, sally::pack::EListFileEncoding encoding,
                               const std::wstring& nameW)
{
    std::string bytes;
    if (!sally::pack::TryEncodeListFileEntry(encoding, nameW, bytes))
        return false;
    return fwrite(bytes.c_str(), 1, bytes.length(), listFile) == bytes.length();
}

// PackErrorHandler's byte transport is explicitly UTF-8. Filesystem, archive, and
// command ownership stay UTF-16 and are encoded only at this internal sink.
static std::string PackErrorPresentation(const wchar_t* text)
{
    const wchar_t* value = text != NULL ? text : L"";
    std::string utf8;
    if (!Win32EncodeText(CP_UTF8, value, wcslen(value), utf8))
        return "Unable to encode packer diagnostic.";
    return utf8;
}

static std::string PackSystemErrorPresentation(DWORD error)
{
    const std::wstring text = GetErrorTextOwned(error);
    return PackErrorPresentation(text.c_str());
}

//
// ****************************************************************************
// Functions for compression
//

//
// ****************************************************************************
// BOOL PackCompress(HWND parent, CFilesWindow *panel, const wchar_t *archiveFileName,
//                   const wchar_t *archiveRoot, BOOL move, const wchar_t *sourceDir,
//                   SalEnumSelection2 nextName, void *param)
//
//   Function for adding requested files to an archive.
//
//   RET: returns TRUE on success, FALSE on error
//        on error the callback function *PackErrorHandlerPtr is called
//   IN:  parent is the parent window of message boxes
//        panel is a pointer to the Salamander file panel
//        archiveFileName is the name of the archive to pack into
//        archiveRoot is the directory in the archive to pack into
//        move is TRUE if files are moved into the archive
//        sourceDir is the path from which the files are packed
//        nextName is a callback function that enumerates names to pack
//        param contains parameters for the enumeration function
//   OUT:

BOOL PackCompress(HWND parent, CFilesWindow* panel, const wchar_t* archiveFileName,
                  const wchar_t* archiveRoot, BOOL move, const wchar_t* sourceDir,
                  SalEnumSelection2 nextName, void* param)
{
    CALL_STACK_MESSAGE5("PackCompress(, , %ls, %ls, %d, %ls, ,)", archiveFileName,
                        archiveRoot, move, sourceDir);
    // find the correct one according to the table
    int format = PackerFormatConfig.PackIsArchive(archiveFileName);
    // Did not find a supported archive - error
    if (format == 0)
        return (*PackErrorHandlerPtr)(parent, IDS_PACKERR_ARCNAME_UNSUP);

    format--;
    if (!PackerFormatConfig.GetUsePacker(format))
        return (*PackErrorHandlerPtr)(parent, IDS_PACKERR_PACKER_UNSUP);
    int index = PackerFormatConfig.GetPackerIndex(format);

    // Is this not internal processing (DLL)?
    if (index < 0)
    {
        CPluginData* plugin = Plugins.Get(-index - 1);
        if (plugin == NULL || !plugin->SupportPanelEdit)
        {
            return (*PackErrorHandlerPtr)(parent, IDS_PACKERR_ARCNAME_UNSUP);
        }
        return plugin->PackToArchive(panel, archiveFileName, archiveRoot, move,
                                     sourceDir, nextName, param);
    }

    const SPackModifyTable* modifyTable = ArchiverConfig.GetPackerConfigTable(index);

    // determine whether we perform copy or move
    const wchar_t* compressCommand;
    const wchar_t* compressInitDir;
    if (!move)
    {
        compressCommand = modifyTable->CompressCommand;
        compressInitDir = modifyTable->CompressInitDir;
    }
    else
    {
        compressCommand = modifyTable->MoveCommand;
        compressInitDir = modifyTable->MoveInitDir;
        if (compressCommand == NULL)
        {
            BOOL ret = (*PackErrorHandlerPtr)(parent, IDS_PACKQRY_NOMOVE);
            if (ret)
            {
                compressCommand = modifyTable->CompressCommand;
                compressInitDir = modifyTable->CompressInitDir;
            }
            else
                return FALSE;
        }
    }

    //
    // If the archiver does not support packing into a directory, we must handle it
    //
    const wchar_t* archiveRootPath = archiveRoot;
    if (archiveRoot != NULL && *archiveRoot != L'\0')
    {
        if (!modifyTable->CanPackToDir) // the archiver program does not support it
        {
            if ((*PackErrorHandlerPtr)(parent, IDS_PACKQRY_ARCPATH))
                archiveRootPath = L"\\"; // the user wants to ignore it
            else
                return FALSE; // the user will mind
        }
    }
    else
        archiveRootPath = L"\\";

    // and perform the actual packing
    return PackUniversalCompress(parent, compressCommand, modifyTable->ErrorTable,
                                 compressInitDir, TRUE, modifyTable->SupportLongNames, archiveFileName,
                                 sourceDir, archiveRootPath, nextName, param, modifyTable->NeedANSIListFile);
}

// The host owns the temporary list path as UTF-16. A legacy archiver that requests
// $(ListDOSFullName) obtains its 8.3 spelling in PackExpLstDosName; native tools receive
// the original path through $(ListFullName), without an unnecessary ACP gate.
static BOOL GetPackTempListName(std::wstring& tmpListName)
{
    tmpListName = SalGetTempFileNameW(NULL, L"PACK", true);
    return !tmpListName.empty();
}

//
// ****************************************************************************
// BOOL PackUniversalCompress(HWND parent, const wchar_t *command, TPackErrorTable *const errorTable,
//                            const wchar_t *initDir, BOOL expandInitDir, const BOOL supportLongNames,
//                            const wchar_t *archiveFileName, const wchar_t *sourceDir,
//                            const wchar_t *archiveRoot, SalEnumSelection2 nextName,
//                            void *param, BOOL needANSIListFile)
//
//   Function for adding requested files to an archive. Unlike the previous one
//   it is more general and does not use configuration tables - it can be called
//   independently, everything is determined only by parameters
//
//   RET: returns TRUE on success, FALSE on error
//        on error the callback function *PackErrorHandlerPtr is called
//   IN:  parent is the parent window for message boxes
//        command is the command line used for packing into the archive
//        errorTable is a pointer to the table of archiver return codes, or NULL if it does not exist
//        initDir is the directory in which the program will be started
//        supportLongNames indicates whether the program supports the use of long names
//        archiveFileName is the name of the archive to pack into
//        sourceDir is the path from which the files are packed
//        archiveRoot is the directory in the archive to pack into
//        nextName is a callback function that enumerates names to pack
//        param contains parameters for the enumeration function
//        needANSIListFile is TRUE if the file list should be in ANSI (not OEM)
//   OUT:

BOOL PackUniversalCompress(HWND parent, const wchar_t* command, TPackErrorTable* const errorTable,
                           const wchar_t* initDir, BOOL expandInitDir, const BOOL supportLongNames,
                           const wchar_t* archiveFileName, const wchar_t* sourceDir,
                           const wchar_t* archiveRoot, SalEnumSelection2 nextName,
                           void* param, BOOL needANSIListFile, SalEnumLastNameW lastNameW)
{
    CALL_STACK_MESSAGE9("PackUniversalCompress(, %ls, , %ls, %d, %d, %ls, %ls, %ls, , , %d)",
                        command, initDir, expandInitDir, supportLongNames, archiveFileName,
                        sourceDir, archiveRoot, needANSIListFile);
    (void)lastNameW; // transitional custom-packer argument; SalEnumSelection2 is already W

    //
    // We must adjust the directory in the archive to the required format
    //
    std::wstring rootPath;
    const wchar_t* normalizedRoot = archiveRoot;
    if (normalizedRoot != NULL && *normalizedRoot != L'\0')
    {
        while (*normalizedRoot == L'\\')
            normalizedRoot++;
        if (*normalizedRoot != L'\0')
        {
            rootPath = L"\\";
            rootPath += normalizedRoot;
            while (!rootPath.empty() && rootPath.back() == L'\\')
                rootPath.pop_back();
        }
    }
    // for 32-bit programs there will be empty quotes, for 16-bit we add a slash
    if (!supportLongNames && rootPath.empty())
        rootPath = L"\\";

    // For path length checks we need sourceDir in the "short" form
    std::wstring sourceShortName;
    if (!supportLongNames)
    {
        sourceShortName = GetShortPathW(sourceDir);
        if (sourceShortName.empty())
        {
            const std::string message = "GetShortPathName: " + PackSystemErrorPresentation(GetLastError());
            return (*PackErrorHandlerPtr)(parent, IDS_PACKERR_GENERAL, message.c_str());
        }
    }
    else
        sourceShortName = sourceDir;

    //
    // In the %TEMP% directory a helper file will contain the list of files to pack
    //

    // Create the temporary file name
    std::wstring tmpListName;
    if (!GetPackTempListName(tmpListName))
        return (*PackErrorHandlerPtr)(parent, IDS_PACKERR_GENERAL, "Unable to create a temporary list file for the packer.");

    // How the archiver has been told to read this list. Derived from its own command line
    // rather than kept as a separate setting, so that Sally cannot disagree with the tool
    // it is driving about how the file it is writing should be read. Every packer whose
    // command line carries no such switch keeps exactly the OEM/ANSI behaviour it had.
    const sally::pack::EListFileEncoding listEncoding =
        sally::pack::ListFileEncodingFromCommandLine(
            command, needANSIListFile ? sally::pack::EListFileEncoding::Ansi
                                      : sally::pack::EListFileEncoding::Oem);
    const bool unicodeList = sally::pack::ListFileEncodingIsUnicode(listEncoding);

    // We have the file; open its W path in binary mode. WriteListFileEntry supplies the
    // exact terminator bytes, so the CRT must not rewrite either legacy or Unicode data.
    FILE* listFile = _wfopen(tmpListName.c_str(), L"wb");
    if (listFile == NULL)
    {
        gFileSystem->DeleteFile(tmpListName.c_str());
        return (*PackErrorHandlerPtr)(parent, IDS_PACKERR_FILE);
    }
    if (unicodeList && !WriteListFileBom(listFile, listEncoding))
    {
        fclose(listFile);
        gFileSystem->DeleteFile(tmpListName.c_str());
        return (*PackErrorHandlerPtr)(parent, IDS_PACKERR_FILE);
    }

    // and we can fill it
    BOOL isDir;

    const wchar_t* name;
    const size_t sourceDirLen = sourceShortName.length() + 1;
    int errorOccured = SALENUM_SUCCESS;
    // pick the name
    while ((name = nextName(parent, 1, NULL, &isDir, NULL, NULL, NULL, param, &errorOccured)) != NULL)
    {
        // The current selection callback is W. Only WriteListFileEntry below encodes the
        // name, using the protocol selected by the external archiver's command line.
        std::wstring listName = name;
        if (!supportLongNames)
        {
            std::wstring shortName = GetShortPathW(listName.c_str());
            if (shortName.empty())
            {
                const std::string namePresentation = PackErrorPresentation(name);
                const std::string message = "File: " + namePresentation +
                                            ", GetShortPathName: " + PackSystemErrorPresentation(GetLastError());
                fclose(listFile);
                gFileSystem->DeleteFile(tmpListName.c_str());
                return (*PackErrorHandlerPtr)(parent, IDS_PACKERR_GENERAL, message.c_str());
            }
            listName.swap(shortName);
        }

        // check the length
        if (!supportLongNames &&
            (sourceDirLen >= DOS_MAX_PATH || listName.length() >= DOS_MAX_PATH - sourceDirLen))
        {
            fclose(listFile);
            gFileSystem->DeleteFile(tmpListName.c_str());
            std::wstring pathForError = sourceShortName;
            if (!pathForError.empty() && pathForError.back() != L'\\')
                pathForError.push_back(L'\\');
            pathForError += listName;
            const std::string presentation = PackErrorPresentation(pathForError.c_str());
            return (*PackErrorHandlerPtr)(parent, IDS_PACKERR_PATH, presentation.c_str());
        }

        // Encode only at the list-file protocol boundary. Legacy ACP/OEM encodings are
        // exact-or-refuse; Unicode modes receive the current callback's exact W name.
        if (!isDir && !WriteListFileEntry(listFile, listEncoding, listName))
        {
            fclose(listFile);
            gFileSystem->DeleteFile(tmpListName.c_str());
            return (*PackErrorHandlerPtr)(parent, IDS_PACKERR_FILE);
        }
    }
    // that's it
    fclose(listFile);

    // if an error occurred and the user decided to cancel the operation, end it
    if (errorOccured == SALENUM_CANCEL)
    {
        gFileSystem->DeleteFile(tmpListName.c_str());
        return FALSE;
    }

    //
    // Now we will launch the external program for compression
    //
    // construct the command line
    std::wstring cmdLine;
    // buffer for a temporary name (when creating an archive with a long name and we need its DOS name,
    // DOSTmpName expands instead of the long name; after creating the archive the file is renamed)
    std::wstring DOSTmpName;
    if (!PackExpandCmdLine(archiveFileName, rootPath.c_str(), tmpListName.c_str(), NULL,
                           command, cmdLine, &DOSTmpName))
    {
        gFileSystem->DeleteFile(tmpListName.c_str());
        return (*PackErrorHandlerPtr)(parent, IDS_PACKERR_CMDLNERR);
    }

    // hack for RAR 4.x+ that dislikes "-ap""" when addressing the archive root; this cleanup works with older RAR too
    // see https://forum.altap.cz/viewtopic.php?f=2&t=5487
    if (rootPath.empty() && wcsstr(command, L"$(Rar32bitExecutable) ") == command)
    {
        const size_t ap = cmdLine.find(L"\" -ap\"\" @\"");
        if (ap != std::wstring::npos)
            cmdLine.replace(ap + 1, 7, 7, L' '); // remove "-ap"" that causes issues with newer RAR
    }
    // hack for copying into a directory in RAR - it fails if the path begins with a backslash; it created e.g. \Test directory but Salam shows it as Test
    // https://forum.altap.cz/viewtopic.php?p=24586#p24586
    if (!rootPath.empty() && rootPath[0] == L'\\' &&
        wcsstr(command, L"$(Rar32bitExecutable) ") == command)
    {
        const size_t ap = cmdLine.find(L"\" -ap\"\\");
        if (ap != std::wstring::npos)
            cmdLine.erase(ap + 6, 1); // remove the leading backslash
    }

    // check if the command line is not too long
    if (sally::pack::ShouldRejectLegacyCommandLine(supportLongNames, cmdLine.length(), true))
    {
        gFileSystem->DeleteFile(tmpListName.c_str());
        const std::string presentation = PackErrorPresentation(cmdLine.c_str());
        return (*PackErrorHandlerPtr)(parent, IDS_PACKERR_CMDLNLEN, presentation.c_str());
    }

    // construct the current directory
    std::wstring currentDir;
    if (!expandInitDir)
        currentDir = initDir != NULL ? initDir : L"";
    else
    {
        if (!PackExpandInitDir(archiveFileName, sourceDir, rootPath.c_str(), initDir, currentDir))
        {
            gFileSystem->DeleteFile(tmpListName.c_str());
            return (*PackErrorHandlerPtr)(parent, IDS_PACKERR_IDIRERR);
        }
    }

    // back up the short archive file name, later we check whether the long name
    // survived -> if the short one remained, rename it back to the original long name
    const std::wstring DOSArchiveFileName = GetShortPathW(archiveFileName);

    // and run the external program
    BOOL exec = PackExecute(parent, cmdLine, currentDir, errorTable);
    // meanwhile check whether the long name did not vanish -> if the short one
    // remained, rename it to the original long one
    if (!DOSArchiveFileName.empty() &&
        gFileSystem->GetFileAttributes(archiveFileName) == INVALID_FILE_ATTRIBUTES &&
        gFileSystem->GetFileAttributes(DOSArchiveFileName.c_str()) != INVALID_FILE_ATTRIBUTES)
    {
        SalMoveFile(DOSArchiveFileName.c_str(), archiveFileName); // if it fails, we don't care...
    }
    if (!exec)
    {
        gFileSystem->DeleteFile(tmpListName.c_str());
        return FALSE; // error message has already been displayed
    }

    // the file list is no longer needed
    gFileSystem->DeleteFile(tmpListName.c_str());

    // if we used a temporary DOS name, rename all files of that name (name.*) to the desired long name
    if (!DOSTmpName.empty())
    {
        const std::wstring& dosTmpName = DOSTmpName;
        const size_t tmpSlash = dosTmpName.find_last_of(L'\\');
        const std::wstring tmpOrigName = tmpSlash == std::wstring::npos
                                             ? dosTmpName
                                             : dosTmpName.substr(tmpSlash + 1);
        const std::wstring srcDirectory = tmpSlash == std::wstring::npos
                                              ? std::wstring()
                                              : dosTmpName.substr(0, tmpSlash + 1);

        const std::wstring archiveName = archiveFileName;
        size_t dstExt = archiveName.find_last_of(L"\\.");
        if (dstExt == std::wstring::npos || archiveName[dstExt] == L'\\')
            dstExt = archiveName.length();

        std::wstring searchPath = dosTmpName;
        size_t searchExt = searchPath.find_last_of(L"\\.");
        if (searchExt == std::wstring::npos || searchPath[searchExt] == L'\\')
            searchExt = searchPath.length();
        searchPath.replace(searchExt, std::wstring::npos, L".*");

        WIN32_FIND_DATAW findData;
        int i;
        for (i = 0; i < 2; i++)
        {
            HANDLE find = SalFindFirstFileHW(searchPath.c_str(), &findData);
            if (find != INVALID_HANDLE_VALUE)
            {
                do
                {
                    const std::wstring src = srcDirectory + findData.cFileName;
                    std::wstring dst;
                    if (StrICmpW(tmpOrigName.c_str(), findData.cFileName) == 0)
                        dst = archiveName;
                    else
                    {
                        const wchar_t* srcExt = wcsrchr(findData.cFileName, L'.');
                        dst.assign(archiveName, 0, dstExt);
                        if (srcExt != NULL)
                            dst += srcExt;
                    }
                    if (i == 0)
                    {
                        if (gFileSystem->GetFileAttributes(dst.c_str()) != INVALID_FILE_ATTRIBUTES)
                        {
                            SalLPFindClose(find); // this name already exists with some extension, searching further
                            const std::string srcPresentation = PackErrorPresentation(src.c_str());
                            const std::string dstPresentation = PackErrorPresentation(dst.c_str());
                            (*PackErrorHandlerPtr)(parent, IDS_PACKERR_UNABLETOREN,
                                                   srcPresentation.c_str(), dstPresentation.c_str());
                            return TRUE; // succeeded, only the resulting archive names differ slightly (even multivolume)
                        }
                    }
                    else
                    {
                        if (!SalMoveFile(src.c_str(), dst.c_str()))
                        {
                            DWORD err = GetLastError();
                            const std::string srcPresentation = PackErrorPresentation(src.c_str());
                            const std::string dstPresentation = PackErrorPresentation(dst.c_str());
                            TRACE_E("Error (" << err << ") in SalMoveFile(" << srcPresentation.c_str()
                                               << ", " << dstPresentation.c_str() << ").");
                        }
                    }
                } while (SalLPFindNextFile(find, &findData));
                SalLPFindClose(find); // this name already exists with some extension, searching further
            }
        }
    }

    return TRUE;
}

//
// ****************************************************************************
// Functions for deleting from an archive
//

//
// ****************************************************************************
// BOOL PackDelFromArc(HWND parent, CFilesWindow *panel, const wchar_t *archiveFileName,
//                     CPluginDataInterfaceAbstract *pluginData,
//                     const wchar_t *archiveRoot, SalEnumSelection nextName,
//                     void *param)
//
//   Function for removing the requested files from an archive.
//
//   RET: returns TRUE on success, FALSE on error
//        on error the callback function *PackErrorHandlerPtr is called
//   IN:  parent is the parent window for message boxes
//        panel is a pointer to the Salamander file panel
//        archiveFileName is the name of the archive we delete from
//        archiveRoot is the directory in the archive we delete from
//        nextName is a callback function that enumerates names to delete
//        param contains parameters for the enumeration function
//   OUT:

BOOL PackDelFromArc(HWND parent, CFilesWindow* panel, const wchar_t* archiveFileName,
                    CPluginDataInterfaceAbstract* pluginData,
                    const wchar_t* archiveRoot, SalEnumSelection nextName,
                    void* param)
{
    CALL_STACK_MESSAGE3("PackDelFromArc(, , %ls, , %ls, , ,)", archiveFileName, archiveRoot);

    // find the correct one according to the table
    int format = PackerFormatConfig.PackIsArchive(archiveFileName);
    // Did not find a supported archive - error
    if (format == 0)
        return (*PackErrorHandlerPtr)(parent, IDS_PACKERR_ARCNAME_UNSUP);

    format--;
    if (!PackerFormatConfig.GetUsePacker(format))
        return (*PackErrorHandlerPtr)(parent, IDS_PACKERR_PACKER_UNSUP);
    int index = PackerFormatConfig.GetPackerIndex(format);

    // Is this not internal processing (DLL)?
    if (index < 0)
    {
        CPluginData* plugin = Plugins.Get(-index - 1);
        if (plugin == NULL || !plugin->SupportPanelEdit)
        {
            return (*PackErrorHandlerPtr)(parent, IDS_PACKERR_ARCNAME_UNSUP);
        }
        return plugin->DeleteFromArchive(panel, archiveFileName, pluginData, archiveRoot,
                                         nextName, param);
    }

    const SPackModifyTable* modifyTable = ArchiverConfig.GetPackerConfigTable(index);
    BOOL needANSIListFile = modifyTable->NeedANSIListFile;

    //
    // We must adjust the directory in the archive to the required format
    //
    std::wstring rootPath;
    if (archiveRoot != NULL && *archiveRoot != L'\0')
    {
        if (*archiveRoot == L'\\')
            archiveRoot++;
        rootPath = archiveRoot;
        if (!rootPath.empty() && rootPath.back() != L'\\')
            rootPath.push_back(L'\\');
    }

    //
    // in the %TEMP% directory a helper file will contain the list of files to delete
    //
    // buffer for the full name of the helper file
    std::wstring tmpListName;
    if (!GetPackTempListName(tmpListName))
        return (*PackErrorHandlerPtr)(parent, IDS_PACKERR_GENERAL, "Unable to create a temporary list file for the packer.");

    const sally::pack::EListFileEncoding listEncoding =
        sally::pack::ListFileEncodingFromCommandLine(
            modifyTable->DeleteCommand,
            needANSIListFile ? sally::pack::EListFileEncoding::Ansi
                             : sally::pack::EListFileEncoding::Oem);
    const bool unicodeList = sally::pack::ListFileEncodingIsUnicode(listEncoding);

    // Write bytes only at the external archiver's list-file boundary. Binary mode
    // keeps the encoder's exact legacy or Unicode terminators intact.
    FILE* listFile = _wfopen(tmpListName.c_str(), L"wb");
    if (listFile == NULL)
    {
        gFileSystem->DeleteFile(tmpListName.c_str());
        return (*PackErrorHandlerPtr)(parent, IDS_PACKERR_FILE);
    }
    if (unicodeList && !WriteListFileBom(listFile, listEncoding))
    {
        fclose(listFile);
        gFileSystem->DeleteFile(tmpListName.c_str());
        return (*PackErrorHandlerPtr)(parent, IDS_PACKERR_FILE);
    }

    // and we can fill it
    BOOL isDir;
    const wchar_t* name;
    int errorOccured = SALENUM_SUCCESS;
    // pick the name
    while ((name = nextName(parent, 1, &isDir, NULL, NULL, param, &errorOccured)) != NULL)
    {
        if (isDir && modifyTable->DelEmptyDir == PMT_EMPDIRS_DONOTDELETE)
            continue;

        std::wstring listName = rootPath;
        listName += name;
        if (isDir && modifyTable->DelEmptyDir == PMT_EMPDIRS_DELETEWITHASTERISK)
            listName += L"\\*";

        // These entries control deletion, so legacy ACP/OEM encodings are exact-or-refuse;
        // a lossy wildcard spelling must never reach the external archiver.
        if (!WriteListFileEntry(listFile, listEncoding, listName))
        {
            fclose(listFile);
            gFileSystem->DeleteFile(tmpListName.c_str());
            return (*PackErrorHandlerPtr)(parent, IDS_PACKERR_FILE);
        }
    }
    // that's it
    fclose(listFile);

    // if an error occurred and the user decided to cancel the operation, end it
    if (errorOccured == SALENUM_CANCEL)
    {
        gFileSystem->DeleteFile(tmpListName.c_str());
        return FALSE;
    }

    //
    // Now we will launch the external program for deletion
    //
    // construct the command line
    std::wstring cmdLine;
    if (!PackExpandCmdLine(archiveFileName, NULL, tmpListName.c_str(), NULL,
                           modifyTable->DeleteCommand, cmdLine, NULL))
    {
        gFileSystem->DeleteFile(tmpListName.c_str());
        return (*PackErrorHandlerPtr)(parent, IDS_PACKERR_CMDLNERR);
    }

    // check whether the command line is not too long
    if (sally::pack::ShouldRejectLegacyCommandLine(modifyTable->SupportLongNames,
                                                    cmdLine.length(), true))
    {
        gFileSystem->DeleteFile(tmpListName.c_str());
        const std::string presentation = PackErrorPresentation(cmdLine.c_str());
        return (*PackErrorHandlerPtr)(parent, IDS_PACKERR_CMDLNLEN, presentation.c_str());
    }

    // construct the current directory
    std::wstring currentDir;
    if (!PackExpandInitDir(archiveFileName, NULL, NULL, modifyTable->DeleteInitDir,
                           currentDir))
    {
        gFileSystem->DeleteFile(tmpListName.c_str());
        return (*PackErrorHandlerPtr)(parent, IDS_PACKERR_IDIRERR);
    }

    // take the attributes in case we need them later
    DWORD fileAttrs = gFileSystem->GetFileAttributes(archiveFileName);
    if (fileAttrs == INVALID_FILE_ATTRIBUTES)
        fileAttrs = FILE_ATTRIBUTE_ARCHIVE;

    // back up the short archive file name, later we check whether the long name
    // survived -> if the short one remained, rename it back to the original long name
    const std::wstring DOSArchiveFileName = GetShortPathW(archiveFileName);

    // and run the external program
    BOOL exec = PackExecute(NULL, cmdLine, currentDir, modifyTable->ErrorTable);
    // meanwhile, check whether the long name did not vanish -> if the short one
    // remained, rename it to the original long name
    if (!DOSArchiveFileName.empty() &&
        gFileSystem->GetFileAttributes(archiveFileName) == INVALID_FILE_ATTRIBUTES &&
        gFileSystem->GetFileAttributes(DOSArchiveFileName.c_str()) != INVALID_FILE_ATTRIBUTES)
    {
        SalMoveFile(DOSArchiveFileName.c_str(), archiveFileName); // if it fails, we don't care...
    }
    if (!exec)
    {
        gFileSystem->DeleteFile(tmpListName.c_str());
        return FALSE; // error message has already been displayed
    }

    // if deleting removed the archive, create a zero-length file
    HANDLE tmpHandle = gFileSystem->CreateFile(archiveFileName, GENERIC_READ, 0, NULL,
                                               OPEN_ALWAYS, fileAttrs, NULL);
    HANDLES_ADD_EX(__otQuiet, tmpHandle != INVALID_HANDLE_VALUE, __htFile,
                   __hoCreateFile, tmpHandle, GetLastError(), TRUE);
    if (tmpHandle != INVALID_HANDLE_VALUE)
    {
        HANDLES_REMOVE(tmpHandle, __htFile, "IFileSystem::CloseHandle");
        gFileSystem->CloseFileHandle(tmpHandle);
    }

    // the file list is no longer needed
    gFileSystem->DeleteFile(tmpListName.c_str());

    return TRUE;
}
