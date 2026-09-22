// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"
#include "common/IFileEnumerator.h"
#include "common/IFileSystem.h"
#include "common/IPathService.h"
#include "common/text/EncodingDetector.h" // UTF-8 pre-check
#include "common/CodeTableTextEncoding.h"

#include "codetbl.h"
#include "ui/IPrompter.h"
#include "common/unicode/helpers.h"
#include "common/widepath.h"
#include "cfgdlg.h"

CCodeTables CodeTables;

//
//*****************************************************************************
// CCodeTable
//

enum class EReadTableError
{
    None,
    BadFileSize,
    System
};

EReadTableError ReadTable(const wchar_t* fileName, char* table, DWORD* systemError)
{ // reads the 'fileName' file and reports a POD error across InitAux's SEH boundary
    CALL_STACK_MESSAGE2("ReadTable(%ls,)", fileName);
    *systemError = ERROR_SUCCESS;
    IFileSystem* fileSystem =
        gFileSystem != nullptr ? gFileSystem : GetWin32FileSystem();
    HANDLE hFile = fileSystem->OpenFileForRead(fileName, FILE_SHARE_READ);
    if (hFile != INVALID_HANDLE_VALUE)
    {
        uint64_t fileSize = 0;
        const FileResult sizeResult =
            fileSystem->GetHandleFileSize(hFile, &fileSize);
        if (sizeResult.success && fileSize == 256)
        {
            char buf[256];
            DWORD read = 0;
            const FileResult readResult =
                fileSystem->ReadFromHandle(hFile, buf, 256, &read);
            if (readResult.success && read == 256)
            {
                int i;
                for (i = 0; i < 256; i++)
                    table[i] = buf[static_cast<BYTE>(table[i])];
            }
            else
            {
                *systemError = readResult.success ? ERROR_READ_FAULT : readResult.errorCode;
                fileSystem->CloseFileHandle(hFile);
                return EReadTableError::System;
            }
        }
        else if (!sizeResult.success)
        {
            *systemError = sizeResult.errorCode;
            fileSystem->CloseFileHandle(hFile);
            return EReadTableError::System;
        }
        else
        {
            fileSystem->CloseFileHandle(hFile);
            return EReadTableError::BadFileSize;
        }
        fileSystem->CloseFileHandle(hFile);
    }
    else
    {
        *systemError = GetLastError();
        return EReadTableError::System;
    }
    return EReadTableError::None;
}

static bool CopyCodeTableTextW(const char* text, int textLen,
                               wchar_t* output, int outputSize)
{
    if (text == nullptr || textLen < 0 || output == nullptr || outputSize <= 0)
        return false;

    std::wstring decoded;
    if (!sally::code_table::DecodeLegacyText(
            text, static_cast<size_t>(textLen), decoded) ||
        decoded.size() >= static_cast<size_t>(outputSize))
        return false;

    std::wmemcpy(output, decoded.c_str(), decoded.size() + 1);
    return true;
}

static wchar_t* DuplicateCodeTableTextW(const char* text)
{
    return sally::code_table::DuplicateLegacyText(text);
}

// Keep C++ objects outside InitAux's SEH scope. The parser reports only a
// resource ID plus stable pointers; formatting and prompting happen here.
static void ShowCodeTableError(int textId, const wchar_t* fileName,
                               EReadTableError detailKind, DWORD detailError)
{
    std::wstring detail;
    if (detailKind == EReadTableError::BadFileSize)
        detail = LoadStrW(IDS_VIEWERBADFILESIZE);
    else if (detailKind == EReadTableError::System)
        detail = GetErrorTextOwned(detailError).c_str();
    const std::wstring msg = !detail.empty()
                                 ? FormatStrW(LoadStrW(textId), fileName, detail.c_str())
                                 : FormatStrW(LoadStrW(textId), fileName);
    gPrompter->ShowError(LoadStrW(IDS_ERRORTITLE), msg.c_str());
}

static void ShowFileReadError(const wchar_t* fileName)
{
    std::wstring msg = FormatStrW(LoadStrW(IDS_FILEREADERROR), fileName);
    gPrompter->ShowError(LoadStrW(IDS_ERRORTITLE), msg.c_str());
}

void InitAux(HWND hWindow, TIndirectArray<CCodeTablesData>& Data,
             char* fileMem, DWORD fileSize, wchar_t* fileName,
             wchar_t* fileNameEnd, int fileNameCapacity,
             wchar_t* absoluteFileName, int absoluteFileNameCapacity,
             const wchar_t* convertCfgFileName,
             wchar_t* winCodePage, DWORD* identifier, wchar_t* description)
{
    (void)hWindow;
    char* txt = fileMem;
    char nameBuf[200];
    char* name;
    char table[256];

    BOOL comment;
    int textId; // != 0 means an error message
    EReadTableError detailKind;
    DWORD detailError;
    char* endTxt = txt + fileSize;
    __try
    {
        while (txt < endTxt)
        {
            comment = FALSE;
            textId = 0;
            detailKind = EReadTableError::None;
            detailError = ERROR_SUCCESS;
            const wchar_t* errorFileName = convertCfgFileName;
            name = nameBuf;

            if (endTxt - txt >= 18 && StrNICmp(txt, "WINDOWS_CODE_PAGE=", 18) == 0)
            {
                txt += 18;      // skip "WINDOWS_CODE_PAGE="
                comment = TRUE; // do not process further (treat it as a comment)

                // read the "windows code page" name
                while (txt < endTxt && (*txt == ' ' || *txt == '\t'))
                    txt++; // skip white spaces
                char* beg = txt;
                while (txt < endTxt && *txt != '\r' && *txt != '\n')
                    txt++;
                int l = (int)min(txt - beg, 100);
                while (l > 0 && (beg[l - 1] == ' ' || beg[l - 1] == '\t'))
                    l--; // trim trailing white spaces
                CopyCodeTableTextW(beg, l, winCodePage, 101);
            }
            else if (endTxt - txt >= 29 && StrNICmp(txt, "WINDOWS_CODE_PAGE_IDENTIFIER=", 29) == 0)
            {
                txt += 29;      // skip "WINDOWS_CODE_PAGE_IDENTIFIER="
                comment = TRUE; // treat the rest as a comment

                // read the identifier
                while (txt < endTxt && (*txt == ' ' || *txt == '\t'))
                    txt++; // skip white spaces
                char* beg = txt;
                while (txt < endTxt && *txt != '\r' && *txt != '\n')
                    txt++;
                int l = (int)min(txt - beg, 100);
                char buff[101];
                memcpy(buff, beg, l);
                while (l > 0 && (buff[l - 1] == ' ' || buff[l - 1] == '\t'))
                    l--; // trim trailing white spaces
                buff[l] = 0;
                *identifier = atoi(buff);
            }
            else if (endTxt - txt >= 30 && StrNICmp(txt, "WINDOWS_CODE_PAGE_DESCRIPTION=", 30) == 0)
            {
                txt += 30;      // skip "WINDOWS_CODE_PAGE_DESCRIPTION="
                comment = TRUE; // do not process further (treat it as a comment)

                // read the description
                while (txt < endTxt && (*txt == ' ' || *txt == '\t'))
                    txt++; // skip white spaces
                char* beg = txt;
                while (txt < endTxt && *txt != '\r' && *txt != '\n')
                    txt++;
                int l = (int)min(txt - beg, 100);
                while (l > 0 && (beg[l - 1] == ' ' || beg[l - 1] == '\t'))
                    l--; // trim trailing white spaces
                CopyCodeTableTextW(beg, l, description, 101);
            }
            else
            {
                if (*txt == '=' && txt + 1 < endTxt && *(txt + 1) == '=') // separator
                {
                    name = NULL;
                }
                else
                {
                    if (*txt == '#') // comment
                    {
                        comment = TRUE;
                    }
                    else // name=files
                    {
                        char* beg = txt;
                        BOOL white = TRUE;
                        while (txt < endTxt && *txt != '=' && *txt != '\r' && *txt != '\n')
                        {
                            if (*txt != ' ' && *txt != '\t')
                                white = FALSE;
                            txt++;
                        }
                        if (txt < endTxt && *txt == '=' && txt > beg)
                        {
                            int l = (int)min(txt - beg, 199);
                            memcpy(name, beg, l);
                            name[l] = 0;

                            int maxFileNameLen =
                                fileNameCapacity - (int)(fileNameEnd - fileName) - 1;
                            if (maxFileNameLen < 0)
                                maxFileNameLen = 0;
                            int i;
                            for (i = 0; i < 256; i++)
                                table[i] = i;
                            do
                            {
                                txt++; // skip '=' or '|'
                                beg = txt;
                                while (txt < endTxt && *txt != '\r' && *txt != '\n' && *txt != '|')
                                    txt++;
                                if (beg < txt) // another encoding file (or ANSI->OEM/OEM->ANSI)
                                {
                                    if (*beg == '\\' || beg + 1 < txt && *(beg + 1) == ':') // full-name (UNC, normal)
                                    {
                                        l = (int)(txt - beg);
                                        if (!CopyCodeTableTextW(
                                                beg, l, absoluteFileName,
                                                absoluteFileNameCapacity))
                                        {
                                            textId = IDS_VIEWERINVALIDLINE;
                                        }
                                        else
                                        {
                                            errorFileName = absoluteFileName;
                                            detailKind = ReadTable(absoluteFileName, table, &detailError);
                                            if (detailKind != EReadTableError::None)
                                                textId = IDS_VIEWERERROPENFILE;
                                        }
                                    }
                                    else
                                    {
                                        if (*beg == ':') // internal variable (ANSI->OEM or OEM->ANSI)
                                        {
                                            char buf[256];
                                            l = (int)min((txt - beg) - 1, 255);
                                            memcpy(buf, beg + 1, l);
                                            buf[l] = 0;
                                            if (StrICmp(buf, "ansi->oem") == 0)
                                            {
                                                CharToOemBuffA(table, table, 256); // table is a byte-to-byte encoding table (codetbl.h), not text
                                            }
                                            else
                                            {
                                                if (StrICmp(buf, "oem->ansi") == 0)
                                                {
                                                    OemToCharBuffA(table, table, 256); // table is a byte-to-byte encoding table (codetbl.h), not text
                                                }
                                                else
                                                {
                                                    textId = IDS_VIEWERBADINTCODING;
                                                }
                                            }
                                        }
                                        else // relative file name
                                        {
                                            l = (int)min(txt - beg, maxFileNameLen);
                                            if (!CopyCodeTableTextW(
                                                    beg, l, fileNameEnd,
                                                    maxFileNameLen + 1))
                                            {
                                                textId = IDS_VIEWERINVALIDLINE;
                                            }
                                            else
                                            {
                                                errorFileName = fileName;
                                                detailKind = ReadTable(fileName, table, &detailError);
                                                if (detailKind != EReadTableError::None)
                                                    textId = IDS_VIEWERERROPENFILE;
                                            }
                                        }
                                    }
                                }
                            } while (textId == 0 && txt < endTxt && *txt != '\r' && *txt != '\n');
                        }
                        else // missing '=' or it's at the start of the line (and not doubled)
                        {
                            if (!white || txt < endTxt && *txt == '=')
                            {
                                textId = IDS_VIEWERINVALIDLINE;
                            }
                            else
                                comment = TRUE;
                        }
                    }
                }
            }

            if (textId == 0 && !comment) // add code to the code table
            {
                CCodeTablesData* code = new CCodeTablesData;
                if (code != NULL)
                {
                    if (name != NULL) // regular entry
                    {
                        code->Name = DuplicateCodeTableTextW(name);
                    }
                    else // separator
                    {
                        code->Name = NULL;
                    }
                    if (name != NULL && code->Name == NULL)
                    {
                        delete code;
                        TRACE_E(LOW_MEMORY);
                    }
                    else
                    {
                        memcpy(code->Table, table, 256);
                        Data.Add(code);
                        if (!Data.IsGood())
                        {
                            Data.ResetState();
                            free(code->Name);
                            delete code;
                        }
                    }
                }
            }

            // move txt behind the first EOL
            while (txt < endTxt && *txt != '\r' && *txt != '\n')
                txt++;
            if (txt < endTxt)
            {
                if (*txt == '\r') // '\r'
                {
                    if (txt + 1 < endTxt && *(txt + 1) == '\n')
                        txt += 2; // '\r\n'
                    else
                        txt++;
                }
                else
                    txt++; // '\n'
            }

            if (textId != 0)
            {
                ShowCodeTableError(textId, errorFileName, detailKind, detailError);
            }
        }
        if (winCodePage[0] == 0)
            TRACE_EW(L"File " << convertCfgFileName << L" does not contain assignment to WINDOWS_CODE_PAGE!");
        if (*identifier == 0xffffffff)
            TRACE_EW(L"File " << convertCfgFileName << L" does not contain assignment to WINDOWS_CODE_PAGE_IDENTIFIER!");
        if (description[0] == 0)
            TRACE_EW(L"File " << convertCfgFileName << L" does not contain assignment to WINDOWS_CODE_PAGE_DESCRIPTION!");
    }
    __except (HandleFileException(GetExceptionInformation(), fileMem, fileSize))
    {
        // file error
        ShowFileReadError(convertCfgFileName);
    }
}

CCodeTable::CCodeTable(HWND hWindow, const wchar_t* dirName)
    : Data(10, 5)
{
    WinCodePage[0] = 0;
    WinCodePageIdentifier = 0xffffffff; // 0 is unsuitable because GetACP returns 0 for UNICODE-only encodings
    WinCodePageDescription[0] = 0;
    DirectoryName = dirName != NULL ? dirName : L"";
    State = ctsDefaultValues;

    std::wstring modulePath;
    const bool moduleReady = gPathService != NULL &&
                             gPathService->GetModuleFileName(HInstance, modulePath).success &&
                             CutDirectoryW(modulePath);
    std::wstring conversionDirectory;
    bool pathReady = moduleReady;
    if (pathReady)
    {
        conversionDirectory = modulePath;
        SalPathAppendW(conversionDirectory, L"convert");
        SalPathAppendW(conversionDirectory, dirName);
        SalPathAddBackslashW(conversionDirectory);
    }

    std::wstring convertCfgFileName;
    if (pathReady)
    {
        convertCfgFileName = conversionDirectory;
        SalPathAppendW(convertCfgFileName, L"convert.cfg");
    }

    IFileSystem* fileSystem =
        gFileSystem != nullptr ? gFileSystem : GetWin32FileSystem();
    HANDLE hFile = pathReady
                       ? fileSystem->OpenFileForRead(convertCfgFileName.c_str(), FILE_SHARE_READ)
                       : INVALID_HANDLE_VALUE;
    if (hFile != INVALID_HANDLE_VALUE)
    {
        DWORD err = NO_ERROR;
        uint64_t fileSize = 0;
        const FileResult sizeResult =
            fileSystem->GetHandleFileSize(hFile, &fileSize);
        if (sizeResult.success && fileSize > 0 && fileSize <= INT_MAX &&
            conversionDirectory.length() <= static_cast<size_t>(INT_MAX) - static_cast<size_t>(fileSize) - 1)
        {
            const int fileNameCapacity = static_cast<int>(conversionDirectory.length() + fileSize + 1);
            const int absoluteFileNameCapacity = static_cast<int>(fileSize + 1);
            char* fileMem = static_cast<char*>(malloc((size_t)fileSize));
            wchar_t* fileName = static_cast<wchar_t*>(malloc(static_cast<size_t>(fileNameCapacity) * sizeof(wchar_t)));
            wchar_t* absoluteFileName = static_cast<wchar_t*>(malloc(static_cast<size_t>(absoluteFileNameCapacity) * sizeof(wchar_t)));
            if (fileMem != NULL && fileName != NULL && absoluteFileName != NULL)
            {
                memcpy(fileName, conversionDirectory.c_str(),
                       (conversionDirectory.length() + 1) * sizeof(wchar_t));
                wchar_t* fileNameEnd = fileName + conversionDirectory.length();
                DWORD offset = 0;
                while (offset < (DWORD)fileSize && err == NO_ERROR)
                {
                    DWORD read = 0;
                    const FileResult readResult = fileSystem->ReadFromHandle(
                        hFile, fileMem + offset, (DWORD)fileSize - offset, &read);
                    if (!readResult.success || read == 0)
                        err = readResult.success ? ERROR_HANDLE_EOF
                                                 : readResult.errorCode;
                    else
                        offset += read;
                }
                if (err == NO_ERROR)
                {
                    InitAux(hWindow, Data, fileMem, (DWORD)fileSize, fileName,
                            fileNameEnd, fileNameCapacity, absoluteFileName,
                            absoluteFileNameCapacity, convertCfgFileName.c_str(),
                            WinCodePage, &WinCodePageIdentifier,
                            WinCodePageDescription);
                    State = ctsSuccessfullyLoaded;
                }
            }
            else
                err = ERROR_NOT_ENOUGH_MEMORY;
            free(absoluteFileName);
            free(fileName);
            free(fileMem);
        }
        else if (!sizeResult.success)
            err = sizeResult.errorCode;
        else if (fileSize > INT_MAX ||
                 (fileSize > 0 && conversionDirectory.length() >
                                      static_cast<size_t>(INT_MAX) - static_cast<size_t>(fileSize) - 1))
            err = ERROR_FILE_TOO_LARGE;
        else
            err = ERROR_FILE_INVALID;
        fileSystem->CloseFileHandle(hFile);

        if (err != NO_ERROR)
        {
            std::wstring msg = FormatStrW(LoadStrW(IDS_VIEWERERROPENCODES),
                                          convertCfgFileName.c_str(),
                                          GetErrorTextOwned(err).c_str());
            gPrompter->ShowError(LoadStrW(IDS_ERRORTITLE), msg.c_str());
        }
    }
    else // if convert\\xxx\\convert.cfg is missing, "load" the default configuration
    {
        lstrcpynW(WinCodePage, LoadStrW(IDS_VIEWERANSICODEPAGE), 101); // "ANSI" code page
        int i;
        for (i = 0; i < 2; i++)
        {
            CCodeTablesData* code = new CCodeTablesData;
            if (code != NULL)
            {
                int k;
                for (k = 0; k < 256; k++)
                    code->Table[k] = k;
                switch (i)
                {
                case 0:
                {
                    code->Name = DupStr(LoadStrW(IDS_VIEWEROEM2ANSICODING));
                    OemToCharBuffA(code->Table, code->Table, 256); // Table is a byte-to-byte encoding table (codetbl.h), not text
                    break;
                }

                case 1:
                {
                    code->Name = DupStr(LoadStrW(IDS_VIEWERANSI2OEMCODING));
                    CharToOemBuffA(code->Table, code->Table, 256); // Table is a byte-to-byte encoding table (codetbl.h), not text
                    break;
                }
                }

                if (code->Name == NULL)
                {
                    delete code;
                    TRACE_E(LOW_MEMORY);
                }
                else
                {
                    Data.Add(code);
                    if (!Data.IsGood())
                    {
                        Data.ResetState();
                        free(code->Name);
                        delete code;
                    }
                }
            }
        }
    }
}

CCodeTable::~CCodeTable()
{
    int i;
    for (i = 0; i < Data.Count; i++)
    {
        if (Data[i]->Name != NULL)
            free(Data[i]->Name);
    }
}

//
//*****************************************************************************
// CCodeTables
//

CCodeTables::CCodeTables()
    : Preloaded(1, 5)
{
    Loaded = FALSE;
    HANDLES(InitializeCriticalSection(&LoadCS));
    HANDLES(InitializeCriticalSection(&PreloadCS));
    Table = NULL;
}

CCodeTables::~CCodeTables()
{
    if (Table != NULL)
        delete Table;
    HANDLES(DeleteCriticalSection(&LoadCS));
    HANDLES(DeleteCriticalSection(&PreloadCS));
}

void CCodeTables::PreloadAllConversions()
{
    HANDLES(EnterCriticalSection(&PreloadCS));
    Preloaded.DestroyMembers();

    std::wstring path;
    if (gPathService != NULL && gPathService->GetModuleFileName(NULL, path).success &&
        CutDirectoryW(path))
    {
        SalPathAppendW(path, L"convert");
        IFileEnumerator* enumerator =
            gFileEnumerator != nullptr ? gFileEnumerator
                                       : GetWin32FileEnumerator();
        HENUM enumeration = enumerator != nullptr
                                ? enumerator->StartEnum(path.c_str(), nullptr)
                                : INVALID_HENUM;
        if (enumeration != INVALID_HENUM)
        {
            FileEnumEntry entry;
            for (;;)
            {
                const EnumResult result =
                    enumerator->NextFile(enumeration, entry);
                if (result.noMoreFiles || !result.success)
                    break;
                if (entry.IsDirectory() && !entry.name.empty() &&
                    entry.name != L"." && entry.name != L"..")
                {
                    // try to open the internal convert.cfg file
                    CCodeTable* table =
                        new CCodeTable(NULL, entry.name.c_str());
                    if (table == NULL)
                    {
                        TRACE_E(LOW_MEMORY);
                        break;
                    }
                    if (table->GetState() == ctsSuccessfullyLoaded)
                    {
                        Preloaded.Add(table);
                        if (!Preloaded.IsGood())
                        {
                            Preloaded.ResetState();
                            delete table;
                            break;
                        }
                    }
                    else
                        delete table; //  we are not interested in the default table -- discard it
                }
            }
            enumerator->EndEnum(enumeration);
        }
    }
}

void CCodeTables::FreePreloadedConversions()
{
    Preloaded.DestroyMembers();
    HANDLES(LeaveCriticalSection(&PreloadCS));
}

BOOL CCodeTables::EnumPreloadedConversions(int* index, const wchar_t** winCodePage,
                                           DWORD* winCodePageIdentifier,
                                           const wchar_t** winCodePageDescription,
                                           const wchar_t** dirName)
{
    if (*index < 0 || *index >= Preloaded.Count)
        return FALSE;
    CCodeTable* table = Preloaded[*index];
    *winCodePage = table->WinCodePage;
    *winCodePageIdentifier = table->WinCodePageIdentifier;
    *winCodePageDescription = table->WinCodePageDescription;
    *dirName = table->DirectoryName.c_str();
    (*index)++;
    return TRUE;
}

BOOL CCodeTables::GetPreloadedIndex(const wchar_t* dirName, int* index)
{
    int i;
    for (i = 0; i < Preloaded.Count; i++)
    {
        CCodeTable* table = Preloaded[i];
        if (_wcsicmp(dirName, table->DirectoryName.c_str()) == 0)
        {
            *index = i;
            return TRUE;
        }
    }
    return FALSE;
}

std::wstring CCodeTables::GetBestPreloadedConversion(const wchar_t* cfgDirName)
{
    //  criterion (1): cfgDirName
    int dummy;
    if (cfgDirName[0] != L'*' &&
        GetPreloadedIndex(cfgDirName, &dummy))
    {
        return cfgDirName;
    }

    //  criterion (2): item matching the OS code page
    DWORD cp = GetACP();
    int i;
    for (i = 0; i < Preloaded.Count; i++)
    {
        CCodeTable* table = Preloaded[i];
        if (table->WinCodePageIdentifier == cp)
        {
            return table->DirectoryName;
        }
    }

    //  criterion (3): westeuro
    if (GetPreloadedIndex(L"westeuro", &dummy))
    {
        return L"westeuro";
    }

    if (Preloaded.Count > 0)
    {
        //  criterion (4): first in the list
        return Preloaded[0]->DirectoryName;
    }
    else
    {
        //  criterion (5): if there is no item in the list, return an empty string
        return {};
    }
}

BOOL CCodeTables::Init(HWND hWindow)
{
    BOOL ret = TRUE;
    CALL_STACK_MESSAGE1("CCodeTables::Init()");
    HANDLES(EnterCriticalSection(&LoadCS));
    if (!Loaded)
    {
        BOOL findBest = FALSE;
        if (Configuration.ConversionTable[0] != L'*')
        {
            // if the path to convert.cfg is initialized, try to load it
            Table = new CCodeTable(hWindow, Configuration.ConversionTable.c_str());
            if (Table != NULL && Table->GetState() != ctsSuccessfullyLoaded)
            {
                // if the load did not succeed perfectly, give other tables a chance
                findBest = TRUE;
            }
        }
        else
            findBest = TRUE;

        if (findBest)
        {
            if (Table != NULL)
            {
                delete Table;
                Table = NULL;
            }
            PreloadAllConversions();
            std::wstring dirName = GetBestPreloadedConversion(Configuration.ConversionTable.c_str());
            FreePreloadedConversions();
            Configuration.ConversionTable = dirName;
            Table = new CCodeTable(hWindow, Configuration.ConversionTable.c_str());
            // do not check Table->State anymore -- accept anything
        }
        if (Table == NULL)
            TRACE_E(LOW_MEMORY);
        Loaded = Table != NULL;
        ret = Loaded;
    }
    HANDLES(LeaveCriticalSection(&LoadCS));
    return ret;
}

void CCodeTables::InitMenu(HMENU menu, int& codeType)
{
    CALL_STACK_MESSAGE1("CCodeTables::InitMenu(,)");
    if (!Loaded)
    {
        TRACE_E("CCodeTables::InitMenu: Table is not loaded");
        return;
    }
    MENUITEMINFOW mi;
    if (GetMenuItemCount(menu) == 0) // empty menu, needs to be filled
    {
        int count = 0;
        memset(&mi, 0, sizeof(mi));
        mi.cbSize = sizeof(mi);
        mi.fMask = MIIM_TYPE | MIIM_ID;
        mi.fType = MFT_STRING;
        mi.wID = CM_CODING_MIN;
        mi.dwTypeData = LoadStrW(IDS_VIEWERNONECODING);
        InsertMenuItemW(menu, count++, TRUE, &mi);

        int i;
        for (i = 0; i < Table->Data.Count; i++)
        {
            if (Table->Data[i]->Name == NULL) // separator
            {
                // if nothing follows the separator, do not insert it
                if (i == Table->Data.Count - 1)
                    continue;

                memset(&mi, 0, sizeof(mi));
                mi.cbSize = sizeof(mi);
                mi.fMask = MIIM_TYPE;
                mi.fType = MFT_SEPARATOR;
                InsertMenuItemW(menu, count++, TRUE, &mi);
            }
            else
            {
                memset(&mi, 0, sizeof(mi));
                mi.cbSize = sizeof(mi);
                mi.fMask = MIIM_TYPE | MIIM_ID;
                mi.fType = MFT_STRING;
                mi.wID = CM_CODING_MIN + i + 1; // +1 because of 'None'
                if (mi.wID > CM_CODING_MAX)
                {
                    TRACE_E("mi.wID > CM_CODING_MAX");
                    break;
                }
                mi.dwTypeData = Table->Data[i]->Name;
                InsertMenuItemW(menu, count++, TRUE, &mi);
            }
        }
    }

    // set the selected conversion radio
    if (!Valid(codeType))
        codeType = 0;                                             // invalid codeType -> fallback to 'none'
    if (codeType == 0 || Table->Data[codeType - 1]->Name != NULL) // neither "none", nor a separator
        CheckMenuRadioItem(menu, CM_CODING_MIN, CM_CODING_MAX, CM_CODING_MIN + codeType, MF_BYCOMMAND);
}

void CCodeTables::Next(int& codeType)
{
    CALL_STACK_MESSAGE1("CCodeTables::Next()");
    if (!Loaded)
    {
        TRACE_E("CCodeTables::Next: Table is not loaded");
        return;
    }
    int i;
    for (i = codeType + 1; i < Table->Data.Count + 1; i++)
    {
        if (Table->Data[i - 1]->Name != NULL)
        {
            codeType = i;
            return;
        }
    }
    codeType = 0;
}

void CCodeTables::Previous(int& codeType)
{
    CALL_STACK_MESSAGE1("CCodeTables::Previous()");
    if (!Loaded)
    {
        TRACE_E("CCodeTables::Previous: Table is not loaded");
        return;
    }
    if (codeType > Table->Data.Count)
        codeType = Table->Data.Count;
    if (codeType == 0)
        codeType = Table->Data.Count + 1;
    int i;
    for (i = codeType - 1; i > 0; i--)
    {
        if (Table->Data[i - 1]->Name != NULL)
        {
            codeType = i;
            return;
        }
    }
    codeType = 0;
}

BOOL CCodeTables::EnumCodeTables(HWND parent, int* index, const wchar_t** name, const char** table)
{
    CALL_STACK_MESSAGE1("CCodeTables::EnumCodeTables(, , ,)");
    if (name != NULL)
        *name = NULL;
    if (table != NULL)
        *table = NULL;
    if (*index == 0)
    {
        CodeTables.Init(parent);
        if (!Loaded)
        {
            TRACE_E("CCodeTables::EnumCodeTables: Table is not loaded");
            return FALSE;
        }
    }
    if (*index < Table->Data.Count)
    {
        wchar_t* n = Table->Data[*index]->Name;
        if (n != NULL)
        {
            if (name != NULL)
                *name = n;
            if (table != NULL)
                *table = Table->Data[*index]->Table;
        }
        (*index)++;
        return TRUE;
    }
    return FALSE;
}

BOOL CCodeTables::GetCode(char* table, int& codeType)
{
    CALL_STACK_MESSAGE1("CCodeTables::GetCode(,)");
    if (!Loaded)
    {
        TRACE_E("CCodeTables::GetCode: Table is not loaded");
        return FALSE;
    }
    if (!Valid(codeType))
        codeType = 0;
    if (codeType == 0)
        return FALSE; // 'none'
    // The shipped .tab files and both consumers define a 256-byte substitution
    // map: one input byte selects one output byte.
    memcpy(table, Table->Data[codeType - 1]->Table, 256);
    return TRUE;
}

BOOL CCodeTables::GetCodeType(const wchar_t* coding, int& codeType)
{
    CALL_STACK_MESSAGE2("CCodeTables::GetCode(%ls, )", coding);
    if (!Loaded)
    {
        TRACE_E("CCodeTables::GetCodeType: Table is not loaded");
        return FALSE;
    }
    int i;
    for (i = 0; i < Table->Data.Count; i++)
    {
        const wchar_t* n = Table->Data[i]->Name;
        if (n != NULL) // not a separator
        {
            const wchar_t* c = coding;
            while (1)
            {
                while (*n != 0 && (*n <= L' ' || *n == L'-' || *n == L'&'))
                    n++;
                while (*c != 0 && (*c <= L' ' || *c == L'-') || *c == L'&')
                    c++;
                // LowerCase is a BYTE[256] fold table: indexing it with a
                // wchar_t is an out-of-bounds read above U+00FF, and silently the wrong
                // answer below it. towlower() is the fold that matches this data.
                if (towlower(*n) != towlower(*c) || *n == 0)
                    break;
                n++;
                c++;
            }
            if (*n == 0 && *c == 0)
            {
                codeType = i + 1;
                return TRUE;
            }
        }
    }
    codeType = 0;
    return FALSE; // not found
}

BOOL CCodeTables::Valid(int codeType)
{
    CALL_STACK_MESSAGE2("CCodeTables::Valid(%d)", codeType);
    if (!Loaded)
    {
        TRACE_E("CCodeTables::Valid: Table is not loaded");
        return FALSE;
    }
    return codeType >= 0 && codeType < Table->Data.Count + 1 &&
           (codeType == 0 || Table->Data[codeType - 1]->Name != NULL);
}

BOOL CCodeTables::GetCodeName(int codeType, std::wstring& name)
{
    CALL_STACK_MESSAGE2("CCodeTables::GetCodeName(%d)", codeType);
    name.clear();
    if (!Loaded)
    {
        TRACE_E("CCodeTables::GetCodeName: Table is not loaded");
        return FALSE;
    }
    if (!Valid(codeType))
        return FALSE;
    if (codeType == 0)
        name = LoadStrW(IDS_VIEWERNONECODING);
    else
        name = Table->Data[codeType - 1]->Name;
    return TRUE;
}

std::wstring CCodeTables::GetWinCodePage()
{
    CALL_STACK_MESSAGE1("CCodeTables::GetWinCodePage()");
    if (!Loaded)
    {
        TRACE_E("CCodeTables::GetWinCodePage: Table is not loaded");
        return {};
    }
    return Table->WinCodePage;
}

void CCodeTables::RecognizeFileType(const char* pattern, int patternLen, BOOL forceText, BOOL* isText,
                                    std::wstring* codePage)
{
    CALL_STACK_MESSAGE3("CCodeTables::RecognizeFileType(, %d, %d, ,)", patternLen, forceText);
    if (!Loaded)
    {
        TRACE_E("CCodeTables::RecognizeFileType: Table is not loaded");
        if (codePage != NULL)
            codePage->clear();
        if (isText != NULL)
            *isText = FALSE;
        return;
    }

    if (isText != NULL)
        *isText = FALSE;
    if (codePage != NULL)
        codePage->clear();
    if (patternLen == 0)
    {
        if (isText != NULL)
            *isText = TRUE;
        return; // nothing to do
    }

    // UTF-8 pre-check: this recogniser scores LEGACY code-page
    // tables against each other, so a BOM-less UTF-8 file was always attributed
    // to whichever single-byte table penalised its bytes least - the auto-detect
    // blind spot. Valid UTF-8 with real multi-byte sequences is text, and it is
    // not any of those code pages, so answer that before scoring begins and
    // leave the code page EMPTY (meaning "no legacy conversion applies").
    {
        bool sawMultiByte = false;
        if (sally::text::ValidateUtf8((const std::uint8_t*)pattern, (std::size_t)patternLen,
                                     /*allowTruncatedTail=*/true, &sawMultiByte) &&
            sawMultiByte)
        {
            if (isText != NULL)
                *isText = TRUE;
            if (codePage != NULL)
                codePage->clear();
            return;
        }
    }

    char* buf = (char*)malloc(patternLen);
    const char* testBuf;
    std::wstring lastCodePage;
    int winCodePageLen = (int)wcslen(Table->WinCodePage);
    DWORD bestPenalty = 0xFFFFFFFF;
    if (buf != NULL)
    {
        int i;
        for (i = -1; i < Table->Data.Count; i++)
        {
            const wchar_t* n = (i == -1 ? NULL : Table->Data[i]->Name);
            testBuf = NULL;
            if (i == -1) // without changing encoding (text in WinCodePage)
            {
                testBuf = pattern;
                lastCodePage = Table->WinCodePage;
            }
            else
            {
                if (n != NULL && winCodePageLen > 0) // not a separator and WinCodePage is loaded
                {
                    // remove '&' characters, they would interfere with comparison
                    std::wstring displayName(n);
                    RemoveAmpersands(displayName.data());
                    displayName.resize(wcslen(displayName.c_str()));
                    n = displayName.c_str();

                    int nameLen = (int)wcslen(n); // check if the "target" conversion is WinCodePage
                    if (nameLen > winCodePageLen && StrICmpW(n + nameLen - winCodePageLen, Table->WinCodePage) == 0)
                    {
                        const wchar_t* s = n + nameLen - winCodePageLen;
                        while (s > n && (*(s - 1) <= L' ' || *(s - 1) == L'-'))
                            s--;
                        lastCodePage.assign(n, static_cast<size_t>(s - n));

                        testBuf = buf;
                        const char* src = pattern;
                        const char* end = pattern + patternLen;
                        char* dst = buf;
                        const char* table = Table->Data[i]->Table;
                        while (src < end)
                            *dst++ = table[(unsigned char)*src++];
                    }
                }
            }

            if (testBuf != NULL)
            {
#define PENALTY_TWOSAME_ALPHA_NOR_NUM_PENALTY 50  // two identical alpha or numeric characters
#define PENALTY_NOT_ALPHA_NOR_NUM_PENALTY 20      // character is neither alpha nor numeric
#define PENALTY_NOT_ALPHA_NOR_NUM_PENALTY_ADD1 2  // addition: non-alpha/non-numeric character + preceded by alpha/num
#define PENALTY_NOT_ALPHA_NOR_NUM_PENALTY_ADD2 1  // addition: non-alpha/non-numeric character + followed by alpha/num
#define PENALTY_NOT_ALPHA_NOR_NUM_PENALTY_ADD3 50 // addition: at least three identical non-alpha/non-numeric characters + preceded by alpha/num
#define PENALTY_NOT_ALPHA_NOR_NUM_PENALTY_ADD4 50 // addition: at least three identical non-alpha/non-numeric characters + followed by alpha/num
#define PENALTY_UPPER_TO_LOWER 2                  // uppercase letter followed by lowercase
#define PENALTY_LOWER_TO_UPPER 10                 // lowercase letter followed by uppercase
#define PENALTY_CHANGE 1                          // type of adjacent characters differs (type = lower/upper/number)
#define PENALTY_MAYBE_UNKNOWN_CHAR 1              // after five '?' characters - likely unknown characters in the target encoding (standard is to replace unknown character with '?')

                const unsigned char* s = (const unsigned char*)testBuf;
                DWORD penalty = 0;
                const unsigned char* end = s + patternLen;
                int nonAscii = 0; // characters >= 128 (not part of ASCII)
                int binar = 0;
                int minBinar = patternLen / 200;
                int nulls = 0;
                int questions = 0;
                DWORD ignoreChar = -1;
                while (s < end)
                {
                    if (!forceText)
                    {
                        if (*s < ' ' && *s != 0 && *s != '\a' && *s != '\b' && *s != '\r' &&
                            *s != '\f' && *s != '\n' && *s != '\t' && *s != '\v' &&
                            *s != '\x1a' && *s != '\x04')
                        { // disallowed character
                            if (++binar > minBinar)
                                break; // more than 0.5% of disallowed characters
                        }
                        if (*s == 0)
                        {
                            if (++nulls > 10)
                                break; // more than ten NULLs in a row -> likely binary
                        }
                        else
                            nulls = 0;
                    }
                    if (*s >= 128)
                        nonAscii++;
                    if (IsNotAlphaNorNum[*s]) // character is neither alpha nor numeric
                    {
                        if (*s == '?')
                        {
                            if (++questions == 5) // divide by five to weaken this penalty compared to others
                            {
                                penalty += PENALTY_MAYBE_UNKNOWN_CHAR;
                                questions = 0;
                            }
                        }

                        if (*s != ' ' && *s != '\t' && *s != '\r' && *s != '\n')
                        { // spaces, tabs and line ends are ignored
                            BOOL skipChar = FALSE;
                            if (*s >= 128) // ignore one non-ASCII character (used to skip an extra apostrophe '\x92' in ASCII text)
                            {
                                if (ignoreChar != (DWORD)*s)
                                {
                                    if (ignoreChar == -1)
                                    {
                                        ignoreChar = (DWORD)*s;
                                        skipChar = TRUE;
                                    }
                                }
                                else
                                    skipChar = TRUE;
                            }
                            if (!skipChar)
                            {
                                if (s > (const unsigned char*)testBuf && !IsNotAlphaNorNum[*(s - 1)])
                                {
                                    penalty += PENALTY_NOT_ALPHA_NOR_NUM_PENALTY_ADD1;
                                    // at least three identical characters preceded by alpha or num (frame corner is a letter - e.g. CP437 text and tested page in CP852)
                                    if (s + 2 < end && *(s + 2) == *(s + 1) && *(s + 1) == *s)
                                    {
                                        penalty += PENALTY_NOT_ALPHA_NOR_NUM_PENALTY_ADD3;
                                    }
                                }
                                penalty += PENALTY_NOT_ALPHA_NOR_NUM_PENALTY;
                                if (s + 1 < end && !IsNotAlphaNorNum[*(s + 1)])
                                {
                                    penalty += PENALTY_NOT_ALPHA_NOR_NUM_PENALTY_ADD2;
                                    // at least three identical characters followed by alpha or num (frame corner is a letter - e.g. CP437 text and tested page in CP852)
                                    if (s - 2 >= (const unsigned char*)testBuf && *(s - 2) == *(s - 1) && *(s - 1) == *s)
                                    {
                                        penalty += PENALTY_NOT_ALPHA_NOR_NUM_PENALTY_ADD4;
                                    }
                                }
                            }
                        }
                    }
                    else // character is alpha or numeric
                    {
                        if (s + 1 < end && !IsNotAlphaNorNum[*(s + 1)]) // next character is also alpha or numeric
                        {
                            int c1; // current character: 1 - lower, 2 - upper, 3 - digit
                            if (!IsAlpha[*s])
                                c1 = 3;
                            else
                                c1 = UpperCase[*s] == *s ? 2 : 1;
                            int c2; // next character: 1 - lower, 2 - upper, 3 - digit
                            if (!IsAlpha[*(s + 1)])
                                c2 = 3;
                            else
                                c2 = UpperCase[*(s + 1)] == *(s + 1) ? 2 : 1;

                            if (c1 == 2 && c2 == 1)
                            {
                                if (s > (const unsigned char*)testBuf && IsAlpha[*(s - 1)])
                                    penalty += PENALTY_UPPER_TO_LOWER; // the word "Interest" would otherwise be re-encoded as MACCE (uppercase 'U' changes to lowercase 'r' in MACCE)
                            }
                            else
                            {
                                if (c1 == 1 && c2 == 2)
                                    penalty += PENALTY_LOWER_TO_UPPER;
                                else
                                {
                                    if (c1 != c2)
                                        penalty += PENALTY_CHANGE;
                                    else
                                    {
                                        if (*s == *(s + 1))
                                            penalty += PENALTY_TWOSAME_ALPHA_NOR_NUM_PENALTY;
                                    }
                                }
                            }
                        }
                    }
                    s++;
                }

                if (s == end) // this is a text
                // && penalty / patternLen <= 5)  // and not a totally unreadable mess (Lukas' test:
                // characters 0x04 -> only EBCDIC passed, but ratio was
                // 10 -> unreadable) - WARNING: unusable because
                // configuraiton files and .inf files also look like complete mess based on 'penalty'
                {
                    if (isText != NULL)
                        *isText = TRUE;

                    if (penalty < bestPenalty)
                    {
                        bestPenalty = penalty;
                        if (codePage != NULL)
                            *codePage = lastCodePage;

                        if (i == -1 && nonAscii * 200 < patternLen)
                            break; // under 0.5% non-ASCII characters -> ASCII, stop searching
                    }
                }
            }
        }
        free(buf);
    }
    else
        TRACE_E(LOW_MEMORY);
}

int CCodeTables::GetConversionToWinCodePage(const wchar_t* codePage)
{
    CALL_STACK_MESSAGE2("CCodeTables::GetConversionToWinCodePage(%ls)", codePage);

    if (!Loaded)
    {
        TRACE_E("CCodeTables::GetConversionToWinCodePage: Table is not loaded");
        return 0;
    }

    // remove '&' characters; they would interfere with comparison
    wchar_t buf2[200];
    lstrcpynW(buf2, codePage, 200);
    RemoveAmpersands(buf2);
    codePage = buf2;

    if (StrICmpW(codePage, Table->WinCodePage) == 0)
        return 0; // return "none"
    int winCodePageLen = (int)wcslen(Table->WinCodePage);
    if (winCodePageLen > 0) // only if WinCodePage is loaded
    {
        int i;
        for (i = 0; i < Table->Data.Count; i++)
        {
            const wchar_t* n = Table->Data[i]->Name;
            if (n != NULL) // not a separator
            {
                // remove '&' characters; they would interfere with comparison
                wchar_t buf3[200];
                lstrcpynW(buf3, n, 200);
                RemoveAmpersands(buf3);
                n = buf3;

                int nameLen = (int)wcslen(n); // check whether the "target" conversion is WinCodePage
                if (nameLen > winCodePageLen && StrICmpW(n + nameLen - winCodePageLen, Table->WinCodePage) == 0)
                {
                    const wchar_t* s = n + nameLen - winCodePageLen;
                    while (s > n && (*(s - 1) <= L' ' || *(s - 1) == L'-'))
                        s--;
                    int l = (int)min(100, s - n);
                    if (StrNICmpW(n, codePage, l) == 0)
                        return i + 1; // found
                }
            }
        }
    }
    return -1; // not found
}
