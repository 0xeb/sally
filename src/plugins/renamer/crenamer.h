// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <string>

#include "renamer_case_fold.h"

struct CSourceFile
{
    wchar_t* FullName;     // allocated UTF-16 file name (with full path)
    wchar_t* Name;         // pointer within FullName after the last backslash or to the start of FullName
    wchar_t* Ext;          // for files: pointer in Name after the first dot from the right (except a dot at
                           // the start of the name) or to the end of Name if there is no extension;
                           // for directories: pointer to the end of Name (directories have no extensions)
    CQuadWord Size;        // file size in bytes
    DWORD Attr;            // file attributes - ORed FILE_ATTRIBUTE_XXX constants
    FILETIME LastWrite;    // time of the last write to the file (UTC-based time)
    size_t NameLen;        // UTF-16 character length of FullName
    unsigned IsDir : 1;
    // unsigned Delete:	1; // the destructor should call free(FullName);
    unsigned State : 1; // 0 -- file not renamed (error, cancel, undo)
                        // 1 -- successfully renamed

    CSourceFile(const CFileData* fileData, const wchar_t* path, size_t pathLen, BOOL isDir);
    CSourceFile(CSourceFile* orig);
    CSourceFile(CSourceFile* orig, const wchar_t* newName);
    CSourceFile(WIN32_FIND_DATAW& fd, const wchar_t* path, size_t pathLen);
    ~CSourceFile();
    CSourceFile* SetName(const wchar_t* name);
};

enum CChangeCase
{
    ccDontChange,
    ccLower,
    ccUpper,
    ccMixed,
    ccStripDia
};

enum CRenameSpec
{
    rsFileName,
    rsRelativePath,
    rsFullPath
};

struct CRenamerOptions
{
    std::string NewName;     // UTF-8 expression-engine text
    std::string SearchFor;   // UTF-8 expression-engine text
    std::string ReplaceWith; // UTF-8 expression-engine text
    BOOL CaseSensitive;
    BOOL WholeWords;
    BOOL Global;
    BOOL RegExp;
    BOOL ExcludeExt;
    CChangeCase FileCase;
    CChangeCase ExtCase;
    BOOL IncludePath;
    CRenameSpec Spec;

    CRenamerOptions() { Reset(FALSE); }
    CRenamerOptions& operator=(const CRenamerOptions& other)
    {
        if (this != &other)
        {
            NewName = other.NewName;
            SearchFor = other.SearchFor;
            ReplaceWith = other.ReplaceWith;
            CaseSensitive = other.CaseSensitive;
            WholeWords = other.WholeWords;
            Global = other.Global;
            RegExp = other.RegExp;
            ExcludeExt = other.ExcludeExt;
            FileCase = other.FileCase;
            ExtCase = other.ExtCase;
            IncludePath = other.IncludePath;
            Spec = other.Spec;
        }
        return *this;
    }
    void Reset(BOOL soft);
    BOOL Load(HKEY regKey, CSalamanderRegistryAbstract* registry);
    BOOL Save(HKEY regKey, CSalamanderRegistryAbstract* registry);
};

// ****************************************************************************

extern const char* VarOriginalName;
extern const char* VarDrive;
extern const char* VarPath;
extern const char* VarRelativePath;
extern const char* VarName;
extern const char* VarNamePart;
extern const char* VarExtPart;
extern const char* VarSize;
extern const char* VarTime;
extern const char* VarDate;
extern const char* VarCounter;

class CRenamer;

struct CExecuteNewNameParam
{
    CRenameSpec Spec;
    const CSourceFile* File;
    int Counter;
    std::string EngineFullName;
    size_t EngineNameOffset;
    size_t EngineExtOffset;
    size_t EngineRootLen;
};

extern CVarString::CVariableEntry NewNameVariables[];

enum CRenamerErrorType
{
    retGenericError,
    retNewName,
    retBMSearch,
    retRegExp,
    retReplacePattern
};

// ****************************************************************************

class CRenamer
{
protected:
    std::wstring& Root;
    std::string EngineRoot;
    size_t EngineRootLen;
    bool EngineRootValid;

    // information about the last error
    int Error, ErrorPos1, ErrorPos2;
    CRenamerErrorType ErrorType;

    CRenameSpec Spec;
    CVarString NewName;
    CChangeCase FileCase;
    CChangeCase ExtCase;
    BOOL IncludePath;

    BOOL Substitute;
    CSalamanderBMSearchData* BMSearch;
    CRegExpAbstract* RegExp;
    std::string ReplaceWith;
    int ReplaceWithLen;
    BOOL UseRegExp;
    BOOL WholeWords;
    BOOL Global;
    BOOL ExcludeExt;
    // Set when a case-insensitive search runs over a length-preserving folded copy of the
    // subject instead of the subject itself (see renamer_case_fold.h). Both byte engines
    // then report offsets that are still valid in the original, unfolded buffer.
    BOOL FoldSubject;
    renamer::FoldScope SubjectFoldScope;
    std::string FoldedSubject;
    // Added to any pointer the regexp engine reports so it addresses the original,
    // unfolded subject. Zero unless a folded copy is being searched.
    std::ptrdiff_t SubjectRebase;

public:
    explicit CRenamer(std::wstring& root);
    ~CRenamer();

    BOOL IsGood() { return Error == 0; }
    void GetError(int& error, int& errorPos1, int& errorPos2,
                  CRenamerErrorType& errorType)
    {
        error = Error;
        errorPos1 = ErrorPos1;
        errorPos2 = ErrorPos2;
        errorType = ErrorType;
    }

    BOOL SetOptions(CRenamerOptions* options);

    int Rename(CSourceFile* file, int counter, char* newName, int newNameSize,
               char** newPart);
    BOOL RenameOwned(CSourceFile* file, int counter, std::wstring& newName,
                     size_t* newPartOffset = NULL);

protected:
    int BMSearchForward(const char* string, int length, int offset);
    int BMSubst(const char* source, int len, char* dest, int max);
    BOOL ValidetaReplacePattern(); // must be called before SafeSubst, but only after RegCompile
    int SafeSubst(char* dest, int max, int& pos);
    int RESubst(const char* source, int len, char* dest, int max);
};

std::string ChangeCase(CChangeCase change, const char* start, const char* end);
