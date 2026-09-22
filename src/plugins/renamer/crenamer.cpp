// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"
#include <limits>
#include <vector>

// ****************************************************************************
//
// CSourceFile
//

CSourceFile::CSourceFile(const CFileData* fileData,
                         const wchar_t* path, size_t pathLen, BOOL isDir)
{
    CALL_STACK_MESSAGE_NONE
    const size_t nameLen = wcslen(fileData->Name);
    NameLen = pathLen + nameLen;
    if (pathLen == 0 || path[pathLen - 1] != L'\\')
    {
        FullName = (wchar_t*)malloc((++NameLen + 1) * sizeof(wchar_t));
        if (pathLen > 0)
            memcpy(FullName, path, pathLen * sizeof(wchar_t));
        FullName[pathLen++] = L'\\';
    }
    else
    {
        FullName = (wchar_t*)malloc((NameLen + 1) * sizeof(wchar_t));
        memcpy(FullName, path, pathLen * sizeof(wchar_t));
    }
    Name = FullName + pathLen;
    memcpy(Name, fileData->Name, (nameLen + 1) * sizeof(wchar_t));
    if (isDir)
        Ext = Name + nameLen;
    else
    {
        Ext = FullName + NameLen;
        while (Ext > Name && Ext[-1] != L'.')
            --Ext;
        if (Ext == Name)
            Ext = FullName + NameLen; // ".cvspass" is an extension in Windows
    }
    Size = fileData->Size;
    Attr = fileData->Attr;
    FileTimeToLocalFileTime(&fileData->LastWrite, &LastWrite);
    IsDir = isDir ? 1 : 0;
    State = 0;
}

CSourceFile::CSourceFile(CSourceFile* orig)
{
    CALL_STACK_MESSAGE_NONE
    FullName = _wcsdup(orig->FullName);
    Name = FullName + (orig->Name - orig->FullName);
    Ext = FullName + (orig->Ext - orig->FullName);
    Size = orig->Size;
    Attr = orig->Attr;
    LastWrite = orig->LastWrite;
    NameLen = orig->NameLen;
    IsDir = orig->IsDir;
    State = 0;
}

CSourceFile::CSourceFile(CSourceFile* orig, const wchar_t* newName)
{
    CALL_STACK_MESSAGE_NONE
    FullName = Name = _wcsdup(newName);
    Ext = NULL;
    wchar_t* iterator = FullName;
    while (*iterator != 0)
    {
        if (*iterator == L'\\')
        {
            Name = iterator + 1;
            Ext = NULL;
        }
        if (*iterator == L'.' /*&& iterator > Name*/) // ".cvspass" is an extension in Windows
            Ext = iterator + 1;
        iterator++;
    }
    if (orig->IsDir || Ext == NULL)
        Ext = iterator; // directories do not have an extension + when a file has no extension
    NameLen = iterator - FullName;
    Size = orig->Size;
    Attr = orig->Attr;
    LastWrite = orig->LastWrite;
    IsDir = orig->IsDir;
    State = 0;
}

CSourceFile::CSourceFile(WIN32_FIND_DATAW& fd, const wchar_t* path, size_t pathLen)
{
    CALL_STACK_MESSAGE_NONE
    const size_t fileNameLen = wcslen(fd.cFileName);
    NameLen = pathLen + fileNameLen + 1;
    FullName = (wchar_t*)malloc((NameLen + 1) * sizeof(wchar_t));
    memcpy(FullName, path, pathLen * sizeof(wchar_t));
    FullName[pathLen++] = L'\\';
    wcscpy(FullName + pathLen, fd.cFileName);
    Name = FullName + pathLen;
    Ext = FullName + NameLen;
    while (Ext > Name && Ext[-1] != L'.')
        --Ext;
    if (Ext == Name)
        Ext = FullName + NameLen; // ".cvspass" is an extension in Windows
    Size = CQuadWord(fd.nFileSizeLow, fd.nFileSizeHigh);
    Attr = fd.dwFileAttributes;
    FileTimeToLocalFileTime(&fd.ftLastWriteTime, &LastWrite);
    IsDir = FALSE;
    State = 0;
}

CSourceFile::~CSourceFile()
{
    CALL_STACK_MESSAGE_NONE
    if (FullName)
        free(FullName);
}

CSourceFile*
CSourceFile::SetName(const wchar_t* name)
{
    CALL_STACK_MESSAGE_NONE
    if (FullName)
        free(FullName);
    FullName = Name = _wcsdup(name);
    Ext = NULL;
    wchar_t* iterator = FullName;
    while (*iterator != 0)
    {
        if (*iterator == L'\\')
        {
            Name = iterator + 1;
            Ext = NULL;
        }
        if (*iterator == L'.' /*&& iterator > Name*/) // ".cvspass" is an extension in Windows
            Ext = iterator + 1;
        iterator++;
    }
    if (IsDir || Ext == NULL)
        Ext = iterator; // directories do not have an extension + when a file has no extension
    NameLen = iterator - FullName;
    return this;
}

// ****************************************************************************
//
// CRenamerOptions
//

const wchar_t* CONFIG_NEWNAME = L"NewName";
const wchar_t* CONFIG_SEARCHFOR = L"SearchFor";
const wchar_t* CONFIG_REPLACEWITH = L"ReplaceWith";
const wchar_t* CONFIG_CASESENSITIVE = L"CaseSensitive";
const wchar_t* CONFIG_WHOLEWORDS = L"WholeWords";
const wchar_t* CONFIG_GLOBAL = L"Global";
const wchar_t* CONFIG_REGEXP = L"RegExp";
const wchar_t* CONFIG_EXCLUDEEXT = L"ExcludeExt";
const wchar_t* CONFIG_FILECASE = L"FileCase";
const wchar_t* CONFIG_EXTCASE = L"ExtCase";
const wchar_t* CONFIG_INCLUDEPATH = L"IncludePath";
const wchar_t* CONFIG_SPEC = L"Spec";

void CRenamerOptions::Reset(BOOL soft)
{
    CALL_STACK_MESSAGE_NONE
    NewName = "$(OriginalName)";
    SearchFor.clear();
    ReplaceWith.clear();
    CaseSensitive = TRUE;
    WholeWords = FALSE;
    Global = FALSE;
    RegExp = FALSE;
    ExcludeExt = FALSE;
    FileCase = ccDontChange;
    ExtCase = ccDontChange;
    IncludePath = FALSE;
    if (!soft)
        Spec = rsFileName;
}

BOOL CRenamerOptions::Load(HKEY regKey, CSalamanderRegistryAbstract* registry)
{
    CALL_STACK_MESSAGE1("CRenamerOptions::Load(, )");
    Reset(FALSE);
    // NewName/SearchFor/ReplaceWith stay narrow (feed the still-narrow
    // dialog EDIT controls), but the shared registry facade's REG_SZ path is
    // wide-only - bridge here, same pattern as renamer.cpp's LastMask fix.
    GetValueSZ(registry, regKey, CONFIG_NEWNAME, NewName);
    GetValueSZ(registry, regKey, CONFIG_SEARCHFOR, SearchFor);
    GetValueSZ(registry, regKey, CONFIG_REPLACEWITH, ReplaceWith);
    registry->GetValue(regKey, CONFIG_CASESENSITIVE, REG_DWORD, &CaseSensitive, sizeof(BOOL));
    registry->GetValue(regKey, CONFIG_WHOLEWORDS, REG_DWORD, &WholeWords, sizeof(BOOL));
    registry->GetValue(regKey, CONFIG_GLOBAL, REG_DWORD, &Global, sizeof(BOOL));
    registry->GetValue(regKey, CONFIG_REGEXP, REG_DWORD, &RegExp, sizeof(BOOL));
    registry->GetValue(regKey, CONFIG_EXCLUDEEXT, REG_DWORD, &ExcludeExt, sizeof(BOOL));
    registry->GetValue(regKey, CONFIG_FILECASE, REG_DWORD, &FileCase, sizeof(int));
    registry->GetValue(regKey, CONFIG_EXTCASE, REG_DWORD, &ExtCase, sizeof(int));
    registry->GetValue(regKey, CONFIG_INCLUDEPATH, REG_DWORD, &IncludePath, sizeof(BOOL));
    registry->GetValue(regKey, CONFIG_SPEC, REG_DWORD, &Spec, sizeof(int));
    return TRUE;
}

BOOL CRenamerOptions::Save(HKEY regKey, CSalamanderRegistryAbstract* registry)
{
    CALL_STACK_MESSAGE1("CRenamerOptions::Save(, )");
    SetValueSZ(registry, regKey, CONFIG_NEWNAME, NewName.c_str());
    SetValueSZ(registry, regKey, CONFIG_SEARCHFOR, SearchFor.c_str());
    SetValueSZ(registry, regKey, CONFIG_REPLACEWITH, ReplaceWith.c_str());
    registry->SetValue(regKey, CONFIG_CASESENSITIVE, REG_DWORD, &CaseSensitive, sizeof(BOOL));
    registry->SetValue(regKey, CONFIG_WHOLEWORDS, REG_DWORD, &WholeWords, sizeof(BOOL));
    registry->SetValue(regKey, CONFIG_GLOBAL, REG_DWORD, &Global, sizeof(BOOL));
    registry->SetValue(regKey, CONFIG_REGEXP, REG_DWORD, &RegExp, sizeof(BOOL));
    registry->SetValue(regKey, CONFIG_EXCLUDEEXT, REG_DWORD, &ExcludeExt, sizeof(BOOL));
    registry->SetValue(regKey, CONFIG_FILECASE, REG_DWORD, &FileCase, sizeof(int));
    registry->SetValue(regKey, CONFIG_EXTCASE, REG_DWORD, &ExtCase, sizeof(int));
    registry->SetValue(regKey, CONFIG_INCLUDEPATH, REG_DWORD, &IncludePath, sizeof(BOOL));
    registry->SetValue(regKey, CONFIG_SPEC, REG_DWORD, &Spec, sizeof(int));
    return TRUE;
}

// ****************************************************************************
//
// CRenamer
//

CRenamer::CRenamer(std::wstring& root)
    : Root(root), EngineRootLen(0), EngineRootValid(false)
{
    CALL_STACK_MESSAGE1("CRenamer::CRenamer()");
    EngineRootValid = TryWideToRenamerText(Root.c_str(), EngineRoot);
    if (EngineRootValid)
        EngineRootLen = EngineRoot.size();
    BMSearch = SG->AllocSalamanderBMSearchData();
    RegExp = CreateRegExp();
    Substitute = FALSE;
    FoldSubject = FALSE;
    SubjectFoldScope = renamer::FoldScope::NonAsciiOnly;
    SubjectRebase = 0;
}

CRenamer::~CRenamer()
{
    CALL_STACK_MESSAGE1("CRenamer::~CRenamer()");
    SG->FreeSalamanderBMSearchData(BMSearch);
    ReleaseRegExp(RegExp);
}

BOOL CRenamer::SetOptions(CRenamerOptions* options)
{
    CALL_STACK_MESSAGE1("CRenamerOptions( )");
    Spec = options->Spec;
    FileCase = options->FileCase;
    ExtCase = options->ExtCase;
    IncludePath = options->IncludePath;

    if (!NewName.Compile(options->NewName.c_str(), Error, ErrorPos1, ErrorPos2, NewNameVariables))
    {
        ErrorType = retNewName;
        return FALSE;
    }

    if (!options->SearchFor.empty())
    {
        Substitute = TRUE;
        ReplaceWith = options->ReplaceWith;
        ReplaceWithLen = static_cast<int>(ReplaceWith.size());
        WholeWords = options->WholeWords;
        Global = options->Global;
        ExcludeExt = options->ExcludeExt;

        if ((UseRegExp = options->RegExp) != 0)
        {
            unsigned opts = RE_SINGLELINE;
            if (!options->CaseSensitive)
                opts |= RE_CASELES;

            // The engine's own table only folds ASCII (see RegexpLowerCase in regexp.cpp),
            // which is all it may safely do to UTF-8 bytes. Non-ASCII case insensitivity
            // comes from folding both pattern and subject up front. Regex syntax is pure
            // ASCII, so leaving ASCII alone here keeps \W, \S and friends intact.
            FoldSubject = !options->CaseSensitive;
            SubjectFoldScope = renamer::FoldScope::NonAsciiOnly;
            std::string pattern =
                FoldSubject ? renamer::FoldUtf8Preserving(options->SearchFor,
                                                          SubjectFoldScope)
                            : options->SearchFor;

            if (!RegExp->RegComp(pattern.data(), opts))
            {
                Error = GetRegExpErrorID(RegExp->GetState());
                ErrorPos1 = 0;
                ErrorPos2 = 0;
                ErrorType = retRegExp;
                return FALSE;
            }

            if (!ValidetaReplacePattern())
                return FALSE;
        }
        else
        {
            // BMSearch's case-insensitive mode folds through an ACP 256-entry table, which
            // mangles UTF-8 lead and continuation bytes. Fold both sides completely instead
            // and search case-sensitively; BMSubst works purely from offsets, and the fold
            // preserves length, so the replacement still reads the original bytes.
            FoldSubject = !options->CaseSensitive;
            SubjectFoldScope = renamer::FoldScope::All;

            WORD flags = SASF_FORWARD | SASF_CASESENSITIVE;
            const std::string pattern =
                FoldSubject ? renamer::FoldUtf8Preserving(options->SearchFor,
                                                          SubjectFoldScope)
                            : options->SearchFor;

            BMSearch->Set(pattern.c_str(), flags);
            if (!BMSearch->IsGood())
            {
                Error = IDS_LOWMEM;
                ErrorPos1 = 0;
                ErrorPos2 = 0;
                ErrorType = retBMSearch;
                return FALSE;
            }
        }
    }
    else
        Substitute = FALSE;

    if (!Substitute)
        FoldSubject = FALSE;

    Error = 0;
    return TRUE;
}

int CRenamer::Rename(CSourceFile* file, int counter, char* newName, int newNameSize, char** newPart)
{
    CALL_STACK_MESSAGE_NONE
    if (!IsGood())
        return -1;
    if (Spec == rsRelativePath && !EngineRootValid)
        return -1;

    std::string engineFullName;
    std::string engineNamePrefix;
    std::string engineExtPrefix;
    if (!TryWideToRenamerText(file->FullName, engineFullName) ||
        !TryWideToRenamerText(file->FullName, engineNamePrefix,
                              static_cast<int>(file->Name - file->FullName)) ||
        !TryWideToRenamerText(file->FullName, engineExtPrefix,
                              static_cast<int>(file->Ext - file->FullName)))
        return -1;

    int pathLen = 0;
    if (newPart)
    {
        switch (Spec)
        {
        case rsFileName:
        {
            pathLen = static_cast<int>(engineNamePrefix.size());
            if (pathLen < 0 || pathLen >= newNameSize)
                return -1;
            memcpy(newName, engineFullName.data(), pathLen);
            break;
        }
        case rsRelativePath:
        {
            pathLen = static_cast<int>(EngineRootLen);
            if (pathLen < 0 || pathLen >= newNameSize)
                return -1;
            memcpy(newName, EngineRoot.data(), pathLen);
            if (pathLen > 0 && newName[pathLen - 1] != '\\')
            {
                if (pathLen + 1 >= newNameSize)
                    return -1;
                newName[pathLen++] = '\\';
            }
            break;
        }
        case rsFullPath:
            break;
        }
        newName += pathLen;
        *newPart = newName;
    }

    // parameters for expanding the New Name var-string
    CExecuteNewNameParam param;
    param.Spec = Spec;
    param.File = file;
    param.Counter = counter;
    param.EngineFullName = std::move(engineFullName);
    param.EngineNameOffset = engineNamePrefix.size();
    param.EngineExtOffset = engineExtPrefix.size();
    param.EngineRootLen = EngineRootLen;
    if (param.EngineRootLen > 0 &&
        param.EngineRootLen < param.EngineFullName.size() &&
        param.EngineFullName[param.EngineRootLen] == '\\')
        ++param.EngineRootLen;

    int l;
    if (Substitute)
    {
        std::string tmp;

        // Expand the New Name into dynamically owned UTF-8 engine text.
        if (!NewName.ExecuteOwned(tmp, &param))
            return -1;
        l = static_cast<int>(tmp.size());
        // l is strlen(tmp)
        if (ExcludeExt && !file->IsDir) // extensions are searched only for files
        {
            int i = l, namel = l;
            while (--i >= 0 && tmp[static_cast<size_t>(i)] != '\\')
            {
                if (tmp[static_cast<size_t>(i)] == '.') // ".cvspass" is an extension in Windows
                {
                    namel = i;
                    break;
                }
            }

            // perform the requested substitution in the name
            int substl = UseRegExp ? RESubst(tmp.c_str(), namel, newName, newNameSize - pathLen) : BMSubst(tmp.c_str(), namel, newName, newNameSize - pathLen);

            // dokopirujeme extension
            if (substl < 0 || substl + (l - namel) >= newNameSize - pathLen)
                return -1;
            memcpy(newName + substl, tmp.c_str() + namel, l - namel + 1);
            // calculate new name length : substed name len + appended ext len - '\0'
            l = substl + l - namel;
        }
        else
        {
            // perform the requested substitution
            l = UseRegExp ? RESubst(tmp.c_str(), l, newName, newNameSize - pathLen) : BMSubst(tmp.c_str(), l, newName, newNameSize - pathLen);
        }
    }
    else
    {
        // expand the New Name
        l = NewName.Execute(newName, newNameSize - pathLen, &param);
    }

    if (l < 0)
        return -1;

    if (FileCase != ccDontChange || ExtCase != ccDontChange)
    {
        const CRenamerNamePartOffsets parts =
            FindRenamerNamePartOffsets(newName, static_cast<size_t>(l), file->IsDir != 0);
        const size_t fileOffset = IncludePath ? 0 : parts.File;
        size_t extOffset = parts.Extension;
        std::string result(newName, l);
        if (FileCase != ccDontChange)
        {
            const std::string changed = ChangeCase(FileCase, newName + fileOffset, newName + extOffset);
            result.replace(fileOffset, extOffset - fileOffset, changed);
            extOffset = fileOffset + changed.size();
        }
        if (ExtCase != ccDontChange)
        {
            const std::string changed = ChangeCase(ExtCase, newName + parts.Extension, newName + l);
            result.replace(extOffset, result.size() - extOffset, changed);
        }
        if (result.size() >= (size_t)(newNameSize - pathLen))
            return -1;
        memcpy(newName, result.c_str(), result.size() + 1);
        l = (int)result.size();
    }

    return l;
}

BOOL CRenamer::RenameOwned(CSourceFile* file, int counter, std::wstring& newName,
                           size_t* newPartOffset)
{
    size_t capacity = (std::max)(static_cast<size_t>(256),
                                 static_cast<size_t>(file->NameLen) * 3 + 1);
    while (capacity <= RenamerEngineBufferCeiling)
    {
        std::vector<char> buffer(capacity, '\0');
        char* newPart = NULL;
        const int length = Rename(file, counter, buffer.data(), static_cast<int>(buffer.size()),
                                  newPartOffset != NULL ? &newPart : NULL);
        if (length >= 0)
        {
            // Rename() writes the path prefix (if any) at the start of the
            // buffer, then advances ITS OWN 'newName' pointer past it before
            // generating the file name, so 'length' - Rename's return value -
            // is the name portion's length only. Decoding just 'length' bytes
            // from buffer.data() (offset 0) silently dropped the prefix,
            // truncating every rsFileName/rsRelativePath result to whatever
            // fit within the name's own length - which for a short prefix and
            // long name could even be mostly-correct-looking wrong output.
            // 'newPart' (when requested) is where Rename()'s prefix write
            // ended, so newPart - buffer.data() is exactly that prefix length.
            const int fullLength = newPartOffset != NULL
                                       ? static_cast<int>((newPart - buffer.data()) + length)
                                       : length;
            if (!TryRenamerTextToWide(buffer.data(), newName, fullLength))
                return FALSE;
            if (newPartOffset != NULL)
            {
                std::wstring prefix;
                if (!TryRenamerTextToWide(buffer.data(), prefix,
                                          static_cast<int>(newPart - buffer.data())))
                    return FALSE;
                *newPartOffset = prefix.size();
            }
            return TRUE;
        }
        if (capacity > RenamerEngineBufferCeiling / 2)
            break;
        capacity *= 2;
    }
    newName.clear();
    return FALSE;
}

int CRenamer::BMSearchForward(const char* string, int length, int offset)
{
    CALL_STACK_MESSAGE_NONE
    while (offset < length)
    {
        int ret = BMSearch->SearchForward(string, length, offset);
        if (ret == -1)
            return -1;

        if (WholeWords)
        {
            // check for a word break
            int prev = ret > 0 && IsAlnum(string[ret - 1]) > 0;
            int start = IsAlnum(string[ret]) > 0;
            int end = IsAlnum(string[ret + BMSearch->GetLength() - 1]) > 0;
            int next = ret + BMSearch->GetLength() < length &&
                       IsAlnum(string[ret + BMSearch->GetLength()]) > 0;
            if (prev != start && end != next)
                return ret;
            offset++;
        }
        else
            return ret;
    }
    return -1;
}

BOOL SafeCopy(char* dest, int max, int& pos, const char* source, int count)
{
    CALL_STACK_MESSAGE_NONE
    if (count + pos > max)
        return FALSE;
    memcpy(dest + pos, source, count);
    pos += count;
    return TRUE;
}

BOOL SafeCopy(char* dest, int max, int& pos, const char* source, int count,
              CChangeCase changeCase)
{
    CALL_STACK_MESSAGE_NONE
    if (changeCase == ccDontChange)
    {
        if (count + pos > max)
            return FALSE;
        memcpy(dest + pos, source, count);
        pos += count;
    }
    else
    {
        const std::string changed = ChangeCase(changeCase, source, source + count);
        if ((int)changed.size() + pos > max)
            return FALSE;
        memcpy(dest + pos, changed.data(), changed.size());
        pos += (int)changed.size();
    }
    return TRUE;
}

int CRenamer::BMSubst(const char* source, int len, char* dest, int max)
{
    CALL_STACK_MESSAGE_NONE
    int start;
    int pos = 0;
    int offset = 0;

    // Search the folded copy, but always copy from 'source'. The fold preserves length, so
    // the offsets below index either buffer interchangeably.
    const char* subject = source;
    if (FoldSubject)
    {
        FoldedSubject = renamer::FoldUtf8Preserving(
            std::string_view(source, static_cast<std::size_t>(len)), SubjectFoldScope);
        subject = FoldedSubject.c_str();
    }

    while ((start = BMSearchForward(subject, len, offset)) >= 0)
    {
        // copy the part before the found pattern
        if (!SafeCopy(dest, max, pos, source + offset, start - offset))
            return -1;

        // replace the found pattern with the requested substitution
        if (!SafeCopy(dest, max, pos, ReplaceWith.c_str(), ReplaceWithLen))
            return -1;

        // move the offset forward
        offset = start + BMSearch->GetLength();
        if (!Global || offset >= len)
            break;
    }

    // copy the part (including the terminating NULL) after the last found string
    if (!SafeCopy(dest, max, pos, source + offset, len - offset + 1))
        return -1;

    return pos - 1; // do not count the terminating NULL
}

BOOL CRenamer::ValidetaReplacePattern()
{
    CALL_STACK_MESSAGE_NONE
    ErrorType = retReplacePattern;
    const char* const replaceBegin = ReplaceWith.c_str();
    const char* replace = replaceBegin;
    BOOL bs = FALSE;
    char paren;
    const char* numberStart;
    const char* numberEnd;
    while (*replace)
    {
        // treat '\\' as a regular character
        // if ((bs = *replace == '\\') || *replace == '$')
        if (*replace == '$')
        {
            replace++;
            paren = 0;
            if (!bs && (*replace == '(' || *replace == '{'))
            {
                paren = *replace == '(' ? ')' : '}';
                replace++;
            }
            if (IsDigit(*replace))
            {
                numberStart = replace;
                int i = 0;
                do
                {
                    i = i * 10 + *replace - '0';
                } while (IsDigit(*++replace));
                numberEnd = replace;
                CChangeCase changeCase = ccDontChange;
                if (paren)
                {
                    if (*replace == ':')
                    {
                        replace++;
                        if (SG->StrNICmp(RenamerTextToWide(replace).c_str(), RenamerTextToWide("lower").c_str(), (int)RenamerTextToWide(replace, sizeof("lower") - 1).size()) == 0)
                            replace += sizeof("lower") - 1;
                        else if (SG->StrNICmp(RenamerTextToWide(replace).c_str(), RenamerTextToWide("upper").c_str(), (int)RenamerTextToWide(replace, sizeof("upper") - 1).size()) == 0)
                            replace += sizeof("upper") - 1;
                        else if (SG->StrNICmp(RenamerTextToWide(replace).c_str(), RenamerTextToWide("mixed").c_str(), (int)RenamerTextToWide(replace, sizeof("mixed") - 1).size()) == 0)
                            replace += sizeof("mixed") - 1;
                        else if (SG->StrNICmp(RenamerTextToWide(replace).c_str(), RenamerTextToWide("stripdia").c_str(), (int)RenamerTextToWide(replace, sizeof("stripdia") - 1).size()) == 0)
                            replace += sizeof("stripdia") - 1;
                        else if (*replace != paren)
                        {
                            // expecting a closing bracket or a size definition
                            Error = IDS_REP_EXPCLOSEPAR1;
                            ErrorPos1 = ErrorPos2 = (int)(replace - replaceBegin);
                            return FALSE;
                        }
                        if (*replace != paren)
                        {
                            // expecting a closing bracket
                            Error = IDS_REP_EXPCLOSEPAR2;
                            ErrorPos1 = ErrorPos2 = (int)(replace - replaceBegin);
                            return FALSE;
                        }
                    }
                    else
                    {
                        if (*replace != paren)
                        {
                            // expecting a closing bracket or a colon and size definition
                            Error = IDS_REP_EXPCLOSEPAR3;
                            ErrorPos1 = ErrorPos2 = (int)(replace - replaceBegin);
                            return FALSE;
                        }
                    }
                    replace++;
                }
                if (i >= RegExp->SubExpCount)
                {
                    // reference to an undefined subpattern
                    Error = IDS_REP_BADREF;
                    ErrorPos1 = (int)(numberStart - replaceBegin);
                    ErrorPos2 = (int)(numberEnd - replaceBegin);
                    return FALSE;
                }
            }
            else
            {
                if (paren)
                {
                    Error = IDS_EXP_EXPECTSUBNUM1;
                    ErrorPos1 = ErrorPos2 = (int)(replace - replaceBegin);
                    return FALSE;
                }
                if (!bs && *replace != '$')
                {
                    Error = IDS_EXP_EXPECTSUBNUM2;
                    ErrorPos1 = ErrorPos2 = (int)(replace - replaceBegin);
                    return FALSE;
                }
                if (*replace == 0)
                {
                    Error = IDS_EXP_EXPECTSUBNUM2;
                    ErrorPos1 = ErrorPos2 = (int)(replace - replaceBegin);
                    return FALSE;
                }
                replace++;
            }
        }
        else
            replace++;
    }
    return TRUE;
}

BOOL CRenamer::SafeSubst(char* dest, int max, int& pos)
{
    CALL_STACK_MESSAGE_NONE
    // the ReplaceWith string must be validated; this is optimized code
    // without syntax checking
    const char* replace = ReplaceWith.c_str();
    BOOL paren;
    while (*replace)
    {
        if (pos == max)
            return FALSE;
        // treat '\\' as a regular character
        // if (*replace == '\\' || *replace == '$')
        if (*replace == '$')
        {
            paren = FALSE;
            if (*++replace == '(' || *replace == '{')
            {
                paren = TRUE;
                replace++;
            }
            if (IsDigit(*replace))
            {
                int i = 0;
                do
                {
                    i = i * 10 + *replace - '0';
                } while (IsDigit(*++replace));
                CChangeCase changeCase = ccDontChange;
                if (paren)
                {
                    if (*replace++ == ':') // skip the closing bracket or ':'
                    {
                        switch (*replace)
                        {
                        case 'l':
                            changeCase = ccLower;
                            replace += sizeof("lower") - 1 + 1;
                            break;
                        case 'u':
                            changeCase = ccUpper;
                            replace += sizeof("upper") - 1 + 1;
                            break;
                        case 'm':
                            changeCase = ccMixed;
                            replace += sizeof("mixed") - 1 + 1;
                            break;
                        case 's':
                            changeCase = ccStripDia;
                            replace += sizeof("stripdia") - 1 + 1;
                            break;
                        default: // '}' or ')'
                            replace++;
                            break;
                        }
                    }
                }
                if (i < RegExp->SubExpCount &&
                    !SafeCopy(dest, max, pos, RegExp->Startp[i] + SubjectRebase,
                              (int)(RegExp->Endp[i] - RegExp->Startp[i]), changeCase))
                    return FALSE;
            }
            else
                dest[pos++] = *replace++;
        }
        else
            dest[pos++] = *replace++;
    }
    return TRUE;
}

int CRenamer::RESubst(const char* source, int len, char* dest, int max)
{
    CALL_STACK_MESSAGE_NONE
    int pos = 0;
    int offset = 0;
    int skipChar = 0;

    // As in BMSubst: match against the folded copy, produce output from 'source'.
    const char* subject = source;
    if (FoldSubject)
    {
        FoldedSubject = renamer::FoldUtf8Preserving(
            std::string_view(source, static_cast<std::size_t>(len)), SubjectFoldScope);
        subject = FoldedSubject.c_str();
    }
    SubjectRebase = source - subject;

    while (RegExp->RegExec((char*)subject, len, offset + skipChar))
    {
        // copy the part before the found pattern
        if (!SafeCopy(dest, max, pos, source + offset,
                      (int)(RegExp->Startp[0] - subject - offset)))
            return -1;

        // replace the found pattern with the requested substitution
        if (!SafeSubst(dest, max, pos))
            return -1;

        // move the offset forward
        offset = (int)(RegExp->Endp[0] - subject);
        skipChar = RegExp->Endp[0] - RegExp->Startp[0] == 0 ? 1 : 0;
        if (!Global)
            break;
    }

    // copy the part (including the terminating NULL) after the last found string
    if (!SafeCopy(dest, max, pos, source + offset, len - offset + 1))
        return -1;

    return pos - 1; // do not count the terminating NULL
}

// ****************************************************************************

static std::wstring MapCase(const std::wstring& text, DWORD flags)
{
    const int chars = LCMapStringEx(LOCALE_NAME_USER_DEFAULT, flags, text.data(), (int)text.size(),
                                    NULL, 0, NULL, NULL, 0);
    if (chars <= 0)
        return text;
    std::wstring mapped((size_t)chars, L'\0');
    if (!LCMapStringEx(LOCALE_NAME_USER_DEFAULT, flags, text.data(), (int)text.size(),
                       mapped.data(), chars, NULL, NULL, 0))
        return text;
    return mapped;
}

std::string ChangeCase(CChangeCase change, const char* start, const char* end)
{
    const std::wstring original = RenamerTextToWide(start, (int)(end - start));
    std::wstring changed = original;
    switch (change)
    {
    case ccLower:
        changed = MapCase(original, LCMAP_LOWERCASE);
        break;

    case ccUpper:
        changed = MapCase(original, LCMAP_UPPERCASE);
        break;

    case ccMixed:
    {
        changed = MapCase(original, LCMAP_LOWERCASE);
        bool wordStart = true;
        for (size_t i = 0; i < changed.size(); i++)
        {
            WORD type = 0;
            GetStringTypeW(CT_CTYPE1, &changed[i], 1, &type);
            const bool alnum = (type & (C1_ALPHA | C1_DIGIT)) != 0;
            if (wordStart && alnum)
            {
                CharUpperBuffW(&changed[i], 1);
                wordStart = false;
            }
            else if (!alnum)
                wordStart = true;
        }
        break;
    }

    case ccStripDia:
    {
        const int chars = FoldStringW(MAP_COMPOSITE, original.data(), (int)original.size(), NULL, 0);
        if (chars > 0)
        {
            std::wstring decomposed((size_t)chars, L'\0');
            if (FoldStringW(MAP_COMPOSITE, original.data(), (int)original.size(), decomposed.data(), chars))
            {
                changed.clear();
                for (wchar_t c : decomposed)
                {
                    WORD type = 0;
                    GetStringTypeW(CT_CTYPE3, &c, 1, &type);
                    if ((type & (C3_NONSPACING | C3_DIACRITIC)) == 0)
                        changed.push_back(c);
                }
            }
        }
        break;
    }
    default:
        break;
    }
    return WideToRenamerText(changed.c_str(), (int)changed.size());
}
