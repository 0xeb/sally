// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"
#include "reg_sz_narrow_bridge.h"
#include "ftp_persisted_text_codec.h"

#include <algorithm>
#include <limits>
#include <utility>
#include <vector>

//
// ****************************************************************************
// CSrvTypeColumn
//

static char* DupSallyText(const char* text)
{
    if (text == NULL)
        return NULL;
    const int length = (int)strlen(text) + 1;
    char* copy = (char*)SalamanderGeneral->Alloc(length);
    if (copy != NULL)
        memcpy(copy, text, length);
    return copy;
}

CSrvTypeColumn::CSrvTypeColumn(CSrvTypeColWidths* colWidths)
{
    Visible = FALSE;
    ID = NULL;
    NameID = -1;
    NameStr = NULL;
    DescrID = -1;
    DescrStr = NULL;
    Type = stctNone;
    EmptyValue = NULL;
    LeftAlignment = TRUE;
    if (colWidths != NULL)
        ColWidths = colWidths->AddRef();
    else
        ColWidths = new CSrvTypeColWidths;
}

CSrvTypeColumn::~CSrvTypeColumn()
{
    if (ID != NULL)
        SalamanderGeneral->Free(ID);
    if (NameStr != NULL)
        SalamanderGeneral->Free(NameStr);
    if (DescrStr != NULL)
        SalamanderGeneral->Free(DescrStr);
    if (EmptyValue != NULL)
        SalamanderGeneral->Free(EmptyValue);
    if (ColWidths != NULL && ColWidths->Release())
        delete ColWidths;
}

void CSrvTypeColumn::LoadFromObj(CSrvTypeColumn* copyFrom)
{
    Visible = copyFrom->Visible;
    ID = DupSallyText(copyFrom->ID);
    NameID = copyFrom->NameID;
    NameStr = DupSallyText(copyFrom->NameStr);
    DescrID = copyFrom->DescrID;
    DescrStr = DupSallyText(copyFrom->DescrStr);
    Type = copyFrom->Type;
    EmptyValue = DupSallyText(copyFrom->EmptyValue);
    LeftAlignment = copyFrom->LeftAlignment;
    ColWidths->FixedWidth = copyFrom->ColWidths->FixedWidth;
    ColWidths->Width = copyFrom->ColWidths->Width;
}

BOOL CSrvTypeColumn::LoadStr(const char** str, char** result)
{
    const char* field = *str;
    const char* s = field;
    size_t decodedLength = 0;
    while (*s != 0 && *s != ',')
    {
        if (*s == '\\')
        {
            s++;
            if (*s == 0)
                return FALSE;
        }
        s++;
        decodedLength++;
    }
    if (s - field == 2 && field[0] == '\\' && field[1] == '0')
        *result = NULL; // "\\0" is the escape sequence for NULL
    else
    {
        if (decodedLength >= static_cast<size_t>((std::numeric_limits<int>::max)()))
            return FALSE;
        char* staged = (char*)SalamanderGeneral->Alloc((int)decodedLength + 1);
        if (staged == NULL)
        {
            TRACE_E(LOW_MEMORY);
            return FALSE;
        }
        const char* source = field;
        char* destination = staged;
        while (source < s)
        {
            if (*source == '\\')
                source++;
            *destination++ = *source++;
        }
        *destination = 0;
        *result = staged;
    }
    if (*s == ',')
        s++;
    *str = s;
    return TRUE;
}

BOOL CSrvTypeColumn::LoadFromStr(const char* str)
{
    Visible = *str++ == '1';
    if (*str++ != ',')
        return FALSE;
    BOOL err = FALSE;
    if (!LoadStr(&str, &ID))
        return FALSE;
    char* strNameID;
    if (!LoadStr(&str, &strNameID) || strNameID == NULL)
        return FALSE;
    NameID = atoi(strNameID);
    SalamanderGeneral->Free(strNameID);
    if (!LoadStr(&str, &NameStr))
        return FALSE;
    char* strDescrID;
    if (!LoadStr(&str, &strDescrID) || strDescrID == NULL)
        return FALSE;
    DescrID = atoi(strDescrID);
    SalamanderGeneral->Free(strDescrID);
    if (!LoadStr(&str, &DescrStr))
        return FALSE;
    char* strType;
    if (!LoadStr(&str, &strType) || strType == NULL)
        return FALSE;
    Type = (CSrvTypeColumnTypes)atoi(strType);
    SalamanderGeneral->Free(strType);
    if (Type <= stctNone || Type >= stctLastItem)
        return FALSE;
    if (!LoadStr(&str, &EmptyValue))
        return FALSE;
    if (*str == 0)
        LeftAlignment = TRUE; // older versions did not have alignment yet = align to the left
    else
    {
        LeftAlignment = *str++ == '1';
        if (*str == ',')
            str++;
    }
    if (*str == 0)
        ColWidths->FixedWidth = 0; // older versions did not have FixedWidth yet = flexible column
    else
    {
        switch (*str++)
        {
        case '1':
            ColWidths->FixedWidth = MAKELONG(1, 0);
            break;
        case '2':
            ColWidths->FixedWidth = MAKELONG(0, 1);
            break;
        case '3':
            ColWidths->FixedWidth = MAKELONG(1, 1);
            break;
        default:
            ColWidths->FixedWidth = 0;
            break;
        }
        if (*str == ',')
            str++;
    }
    if (*str == 0)
        ColWidths->Width = 0; // older versions did not have Width yet = minimal width
    else
    {
        char* strWidth;
        if (!LoadStr(&str, &strWidth) || strWidth == NULL)
            return FALSE;
        ColWidths->Width = atoi(strWidth);
        SalamanderGeneral->Free(strWidth);
    }
    return TRUE;
}

BOOL CSrvTypeColumn::SaveToStr(std::string& text, BOOL ignoreColWidths) noexcept
{
    if (!FtpSerializeServerTypeColumnRecord(Visible, ID, NameID, NameStr, DescrID,
                                            DescrStr, (int)Type, EmptyValue,
                                            LeftAlignment, ColWidths->FixedWidth,
                                            ColWidths->Width, ignoreColWidths, text))
    {
        TRACE_E(LOW_MEMORY);
        return FALSE;
    }
    return TRUE;
}

CSrvTypeColumn*
CSrvTypeColumn::MakeCopy()
{
    CSrvTypeColumn* n = new CSrvTypeColumn(ColWidths);
    if (n != NULL && n->IsGood())
    {
        n->Visible = Visible;
        n->ID = DupSallyText(ID);
        n->NameID = NameID;
        n->NameStr = DupSallyText(NameStr);
        n->DescrID = DescrID;
        n->DescrStr = DupSallyText(DescrStr);
        n->Type = Type;
        n->EmptyValue = DupSallyText(EmptyValue);
        n->LeftAlignment = LeftAlignment;
    }
    else
    {
        TRACE_E(LOW_MEMORY);
        if (n != NULL)
            delete n;
    }
    return n;
}

void CSrvTypeColumn::Set(BOOL visible, const char* id, int nameID, const char* nameStr, int descrID,
                         const char* descrStr, CSrvTypeColumnTypes type, const char* emptyValue,
                         BOOL leftAlignment, int fixedWidth, int width)
{
    Visible = visible;
    ID = DupSallyText(id);
    NameID = nameID;
    NameStr = DupSallyText(nameStr);
    DescrID = descrID;
    DescrStr = DupSallyText(descrStr);
    Type = type;
    EmptyValue = DupSallyText(emptyValue);
    LeftAlignment = leftAlignment;
    ColWidths->FixedWidth = fixedWidth;
    ColWidths->Width = width;
}

//
// ****************************************************************************

BOOL ValidateSrvTypeColumns(TIndirectArray<CSrvTypeColumn>* columns, int* errResID)
{
    // check whether the first column is Name and visible
    if (columns->Count > 0 && columns->At(0)->Type == stctName && columns->At(0)->Visible)
    {
        // check ID non-emptiness + type counts + whether Ext is second and visible + check of "empty value"
        // + check non-emptiness of Name and Description
        int i, counts[stctLastItem - 1];
        memset(counts, 0, sizeof(counts));
        for (i = 0; i < columns->Count; i++)
        {
            CSrvTypeColumn* col = columns->At(i);

            if (!IsValidIdentifier(col->ID, errResID))
                return FALSE;
            CSrvTypeColumnTypes type = col->Type;
            if (type > stctNone && type < stctLastItem)
                counts[(int)type - 1]++;
            else
            {
                TRACE_E("Unexpected situation in ValidateSrvTypeColumns(): unknown column type!");
                if (errResID != NULL)
                    *errResID = IDS_STC_ERR_TYPEERR;
                return FALSE;
            }
            if (type == stctExt && (i != 1 || !col->Visible))
            {
                if (errResID != NULL)
                    *errResID = IDS_STC_ERR_EXTERR;
                return FALSE;
            }
            // empty values make no sense for the Name, Ext, and Type types, clear them
            if ((type == stctName || type == stctExt || type == stctType) && col->EmptyValue != NULL)
            {
                free(col->EmptyValue);
                col->EmptyValue = NULL;
            }
            if (!GetColumnEmptyValue(col->EmptyValue, type, NULL, NULL, NULL, FALSE))
            {
                if (errResID != NULL)
                    *errResID = IDS_STC_ERR_INVALEMPTY;
                return FALSE;
            }

            if (col->NameID == -1 && (col->NameStr == NULL || *(col->NameStr) == 0))
            {
                if (errResID != NULL)
                    *errResID = IDS_STC_ERR_EMPTYNAME;
                return FALSE;
            }

            if (col->DescrID == -1 && (col->DescrStr == NULL || *(col->DescrStr) == 0))
            {
                if (errResID != NULL)
                    *errResID = IDS_STC_ERR_EMPTYDESCR;
                return FALSE;
            }
        }
        for (i = 1; i < stctLastItem; i++)
        {
            if (counts[i - 1] > 1 && i < stctFirstGeneral)
            {
                if (errResID != NULL)
                    *errResID = IDS_STC_ERR_TYPEERR;
                return FALSE;
            }
        }
        // check uniqueness of IDs
        for (i = 0; i < columns->Count - 1; i++)
        {
            char* id = columns->At(i)->ID;
            int j;
            for (j = i + 1; j < columns->Count; j++)
            {
                if (FtpEqualLocalTextNoCase(id, columns->At(j)->ID))
                {
                    if (errResID != NULL)
                        *errResID = IDS_STC_ERR_IDNOTUNIQUE;
                    return FALSE;
                }
            }
        }
    }
    else
    {
        if (errResID != NULL)
            *errResID = IDS_STC_ERR_NAMEERR;
        return FALSE;
    }
    if (errResID != NULL)
        *errResID = 0;
    return TRUE;
}

//
// ****************************************************************************
// CServerType
//

void CServerType::Init()
{
    TypeName = NULL;
    AutodetectCond = NULL;
    RulesForParsing = NULL;
    CompiledAutodetCond = NULL;
    CompiledParser = NULL;
}

void CServerType::Release()
{
    if (TypeName != NULL)
        SalamanderGeneral->Free(TypeName);
    if (AutodetectCond != NULL)
        SalamanderGeneral->Free(AutodetectCond);
    Columns.DestroyMembers();
    if (RulesForParsing != NULL)
        SalamanderGeneral->Free(RulesForParsing);
    if (CompiledAutodetCond != NULL)
        delete CompiledAutodetCond;
    if (CompiledParser != NULL)
        delete CompiledParser;
    Init();
}

BOOL CServerType::Set(const char* typeName, const char* autodetectCond, int columnsCount,
                      const char* columnsStr[], const char* rulesForParsing)
{
    BOOL err = FALSE;
    if (typeName == NULL)
    {
        TRACE_E("Server Type Name can't be NULL!");
        err = TRUE;
    }
    else
    {
        UpdateStr(TypeName, typeName, &err);
        UpdateStr(AutodetectCond, autodetectCond, &err);
        UpdateStr(RulesForParsing, rulesForParsing, &err);
        Columns.DestroyMembers();
        int i;
        for (i = 0; i < columnsCount; i++)
        {
            CSrvTypeColumn* c = new CSrvTypeColumn;
            if (c != NULL && c->IsGood() && c->LoadFromStr(columnsStr[i]))
            {
                Columns.Add(c);
                if (!Columns.IsGood())
                {
                    delete c;
                    Columns.ResetState();
                    err = TRUE;
                    break;
                }
            }
            else
            {
                if (c != NULL)
                {
                    if (c->IsGood())
                        TRACE_E("Unable to load CSrvTypeColumn from string: " << columnsStr[i]);
                    else
                        TRACE_E(LOW_MEMORY);
                    delete c;
                }
                else
                    TRACE_E(LOW_MEMORY);
                err = TRUE;
                break;
            }
        }
        if (CompiledAutodetCond != NULL)
        {
            delete CompiledAutodetCond;
            CompiledAutodetCond = NULL;
        }
        if (CompiledParser != NULL)
        {
            delete CompiledParser;
            CompiledParser = NULL;
        }
    }
    return !err;
}

BOOL CServerType::Set(const char* typeName, CServerType* copyFrom)
{
    BOOL err = FALSE;
    if (typeName == NULL)
    {
        TRACE_E("Server Type Name can't be NULL!");
        err = TRUE;
    }
    else
    {
        UpdateStr(TypeName, typeName, &err);
        UpdateStr(AutodetectCond, copyFrom->AutodetectCond, &err);
        UpdateStr(RulesForParsing, copyFrom->RulesForParsing, &err);
        Columns.DestroyMembers();
        int i;
        for (i = 0; i < copyFrom->Columns.Count; i++)
        {
            CSrvTypeColumn* c = new CSrvTypeColumn;
            if (c != NULL && c->IsGood())
            {
                c->LoadFromObj(copyFrom->Columns[i]);
                Columns.Add(c);
                if (!Columns.IsGood())
                {
                    delete c;
                    Columns.ResetState();
                    err = TRUE;
                    break;
                }
            }
            else
            {
                if (c != NULL)
                {
                    if (!c->IsGood())
                        TRACE_E(LOW_MEMORY);
                    delete c;
                }
                else
                    TRACE_E(LOW_MEMORY);
                err = TRUE;
                break;
            }
        }
        if (CompiledAutodetCond != NULL)
        {
            delete CompiledAutodetCond;
            CompiledAutodetCond = NULL;
        }
        if (CompiledParser != NULL)
        {
            delete CompiledParser;
            CompiledParser = NULL;
        }
    }
    return !err;
}

// Registry-corruption bug family (see reg_sz_narrow_bridge.h):
// this file's TypeName/AutodetectCond/parser-column strings/RulesForParsing
// and CFTPProxyServer's encoded proxy script stays narrow
// char* by design (protocol/parser text, not something a widening tick
// converts), but the shared registry facade's REG_SZ path is wide-only.
// Bridge here, at each Load()/Save() boundary, converting to/from wide right
// at the registry call - same idiom as plugins/ftp/ftp.cpp's SetValueSZ/
// GetValueSZ (which are file-local static, hence this file needs its own).
static BOOL SetValueSZ(CSalamanderRegistryAbstract* registry, HKEY regKey, const wchar_t* name, const char* narrowValue)
{
    std::wstring wide;
    if (!EncodeRegSzFromNarrowOwned(narrowValue, wide))
        return FALSE;
    return SPLRegistrySetString(registry, regKey, name, wide);
}

static BOOL GetValueSZ(CSalamanderRegistryAbstract* registry, HKEY regKey, const wchar_t* name, std::string& value)
{
    std::wstring wideValue;
    std::string staged;
    if (!SPLRegistryGetStringOwned(registry, regKey, name, wideValue) ||
        !DecodeRegSzToNarrowOwned(wideValue.c_str(), staged))
        return FALSE;
    value.swap(staged);
    return TRUE;
}

static BOOL GetValueStringW(CSalamanderRegistryAbstract* registry, HKEY regKey, const wchar_t* name, std::wstring& value)
{
    return SPLRegistryGetStringOwned(registry, regKey, name, value);
}

static BOOL SetValueStringW(CSalamanderRegistryAbstract* registry, HKEY regKey, const wchar_t* name, const std::wstring& value)
{
    return SPLRegistrySetString(registry, regKey, name, value);
}

BOOL CServerType::Load(HWND parent, HKEY regKey, CSalamanderRegistryAbstract* registry)
{
    std::string name;
    if (!GetValueSZ(registry, regKey, CONFIG_STNAME, name))
        return FALSE; // name is mandatory
    std::string autodetectCond;
    GetValueSZ(registry, regKey, CONFIG_STADCOND, autodetectCond); // optional

    std::vector<std::string> columnStrings;
    HKEY columnsKey;
    if (registry->OpenKey(regKey, CONFIG_STCOLUMNS, columnsKey))
    {
        std::string colStr;
        std::wstring num;
        int i = 1;
        if (!FTPFormatDecimalIndex(num, i))
        {
            registry->CloseKey(columnsKey);
            return FALSE;
        }
        while (GetValueSZ(registry, columnsKey, num.c_str(), colStr))
        {
            try
            {
                columnStrings.push_back(colStr);
            }
            catch (...)
            {
                registry->CloseKey(columnsKey);
                return FALSE;
            }
            if (!FTPFormatDecimalIndex(num, ++i))
            {
                registry->CloseKey(columnsKey);
                return FALSE;
            }
        }
        registry->CloseKey(columnsKey);
    }
    else
        return FALSE; // columns are mandatory

    std::string rulesForParsingReg;
    std::string rulesForParsing;
    if (!GetValueSZ(registry, regKey, CONFIG_STRULESFORPARS, rulesForParsingReg))
        return FALSE; // parsing rules are mandatory as well
    if (!FtpDecodePersistedText(rulesForParsingReg.c_str(), rulesForParsing))
        return FALSE;
    std::vector<const char*> columnPointers;
    try
    {
        columnPointers.reserve(columnStrings.size());
        for (const std::string& column : columnStrings)
            columnPointers.push_back(column.c_str());
    }
    catch (...)
    {
        return FALSE;
    }
    BOOL ret = Set(name.c_str(), autodetectCond.empty() ? NULL : autodetectCond.c_str(),
                   (int)columnPointers.size(), columnPointers.data(),
                   rulesForParsing.empty() ? NULL : rulesForParsing.c_str());

    // verify that the list of columns is valid
    if (ret)
        ret = ValidateSrvTypeColumns(&Columns, NULL);

    // verify that the parsing rules are valid
    if (ret)
    {
        CFTPParser* parser = CompileParsingRules(HandleNULLStr(RulesForParsing), &Columns, NULL, NULL, NULL);
        if (parser != NULL)
            delete parser; // parser is OK, discard it again
        else
            ret = FALSE; // error in the rules
    }

    // verify that the autodetect condition is valid
    if (ret)
    {
        CFTPAutodetCondNode* node = CompileAutodetectCond(
            HandleNULLStr(AutodetectCond), NULL, NULL, NULL, NULL);
        if (node != NULL)
            delete node; // condition is OK, discard it again
        else
            ret = FALSE; // error in the condition
    }

    return ret;
}

void CServerType::Save(HWND parent, HKEY regKey, CSalamanderRegistryAbstract* registry)
{
    std::vector<std::string> columnRecords;
    try
    {
        columnRecords.reserve(Columns.Count);
        for (int i = 0; i < Columns.Count; i++)
        {
            std::string record;
            if (!Columns[i]->SaveToStr(record))
                return;
            columnRecords.push_back(std::move(record));
        }
    }
    catch (...)
    {
        TRACE_E(LOW_MEMORY);
        return;
    }
    std::string rulesForParsingReg;
    if (!FtpEncodePersistedText(HandleNULLStr(RulesForParsing), rulesForParsingReg))
        return;

    SetValueSZ(registry, regKey, CONFIG_STNAME, TypeName);
    SetValueSZ(registry, regKey, CONFIG_STADCOND, HandleNULLStr(AutodetectCond));
    HKEY columnsKey;
    if (registry->CreateKey(regKey, CONFIG_STCOLUMNS, columnsKey))
    {
        registry->ClearKey(columnsKey);
        std::wstring num;
        int i;
        for (i = 0; i < (int)columnRecords.size(); i++)
        {
            if (!FTPFormatDecimalIndex(num, i + 1))
                break;
            SetValueSZ(registry, columnsKey, num.c_str(), columnRecords[i].c_str());
        }
        registry->CloseKey(columnsKey);
    }
    SetValueSZ(registry, regKey, CONFIG_STRULESFORPARS, rulesForParsingReg.c_str());
}

CServerType*
CServerType::MakeCopy()
{
    CServerType* n = new CServerType;
    if (n != NULL)
    {
        BOOL err = FALSE;
        n->TypeName = DupSallyText(TypeName);
        n->AutodetectCond = DupSallyText(AutodetectCond);
        n->RulesForParsing = DupSallyText(RulesForParsing);
        int i;
        for (i = 0; i < Columns.Count; i++)
        {
            CSrvTypeColumn* c = Columns[i]->MakeCopy();
            if (c != NULL)
            {
                n->Columns.Add(c);
                if (!n->Columns.IsGood())
                {
                    delete c;
                    n->Columns.ResetState();
                    err = TRUE;
                    break;
                }
            }
            else
            {
                err = TRUE;
                break;
            }
        }
        // CompiledAutodetCond = NULL;  // unnecessary, done in n->Init() in the constructor
        // CompiledParser = NULL;

        if (err) // not everything was duplicated
        {
            delete n;
            n = NULL;
        }
    }
    else
        TRACE_E(LOW_MEMORY);
    return n;
}

BOOL GetTypeNameForUser(const char* typeName, std::wstring& text) noexcept
{
    if (typeName == NULL)
        return FALSE;
    return FtpFormatLocalTypeName(typeName, UserDefinedSuffix, text);
}

constexpr size_t WSTF_LINE_WRAP = 80; // persisted .STR formatting width, not an ownership ceiling

static BOOL WriteEncodedFileBytes(HANDLE file, std::string_view bytes, DWORD* err) noexcept
{
    while (!bytes.empty())
    {
        const DWORD requested = static_cast<DWORD>((std::min)(bytes.size(),
                                                              static_cast<size_t>(MAXDWORD)));
        DWORD written = 0;
        if (!WriteFile(file, bytes.data(), requested, &written, NULL))
        {
            *err = GetLastError();
            return FALSE;
        }
        if (written == 0)
        {
            *err = ERROR_DISK_FULL;
            return FALSE;
        }
        bytes.remove_prefix(written);
    }
    return TRUE;
}

// writes string 'str' to file 'file', returns any error in 'err' (must not be NULL);
// format (apart from "// " at the beginning of the line exactly as it will be in the file - '"', '\', etc. have no C++ meaning):
// "first line starts with double quotes
// long lines has backslash after each part of line:
// line-part1\
// line-part2\
// line-part3
// next-line with escape sequence \" for double quotes and \\ for backslash
// NULL string is written only as \0 escape sequence
// lastline ends with double quotes"
void WriteStrToFile(HANDLE file, const char* str, DWORD* err)
{
    if (str == NULL)
    {
        WriteEncodedFileBytes(file, "\"\\0\"\r\n", err);
        return;
    }

    try
    {
        std::string line;
        line.reserve(WSTF_LINE_WRAP + 3);
        const char* s = str;
        BOOL firstLine = TRUE;
        while (1)
        {
            line.clear();
            if (firstLine)
            {
                line.push_back('"');
                firstLine = FALSE;
            }
            while (*s != 0 && *s != '\r' && *s != '\n')
            {
                if (line.size() > WSTF_LINE_WRAP - 2)
                {
                    line.push_back('\\');
                    break; // finish this part of the line, continue on the next line in the file
                }
                if (*s == '"' || *s == '\\')
                    line.push_back('\\');
                line.push_back(*s++);
            }
            if (*s == 0)
                line.push_back('"');
            line.append("\r\n");

            if (!WriteEncodedFileBytes(file, line, err))
                break; // write error, abort with error

            if (*s == 0)
                break; // finished writing the string

            if (*s == '\r')
                s++;
            if (*s == '\n')
                s++;
        }
    }
    catch (...)
    {
        *err = ERROR_NOT_ENOUGH_MEMORY;
    }
}

// texts in the file with an exported "server type" (extension .STR)
const char* STR_FILE_HEADER = "Sally - FTP Client - Exported Server Type";
const char* STR_FILE_TYPENAME = "Type Name:";
const char* STR_FILE_ADCOND = "Autodetect Condition:";
const char* STR_FILE_COLUMNS = "Columns:";
const char* STR_FILE_RULES = "Rules for Parsing:";

DWORD
CServerType::ExportToFile(HANDLE file)
{
    DWORD err = NO_ERROR;
    std::string line;
    int i = 0;
    while (1)
    {
        switch (i)
        {
        case 0:
            if (!FTPFormatString(line, "%s\r\n\r\n", STR_FILE_HEADER))
                err = ERROR_NOT_ENOUGH_MEMORY;
            break;
        case 1:
            if (!FTPFormatString(line, "%s %s\r\n", STR_FILE_TYPENAME, TypeName))
                err = ERROR_NOT_ENOUGH_MEMORY;
            break;
        case 2:
            if (!FTPFormatString(line, "%s ", STR_FILE_ADCOND))
                err = ERROR_NOT_ENOUGH_MEMORY;
            break; // header for AutodetectCond

        case 3: // write the AutodetectCond string
        {
            WriteStrToFile(file, AutodetectCond, &err);
            line.clear(); // already written by the encoded-string formatter
            break;
        }

        case 4:
            if (!FTPFormatString(line, "\r\n%s\r\n", STR_FILE_COLUMNS))
                err = ERROR_NOT_ENOUGH_MEMORY;
            break; // header for columns

        case 5: // write all columns
        {
            std::string colStr;
            int j;
            for (j = 0; j < Columns.Count; j++)
            {
                if (!Columns[j]->SaveToStr(colStr, TRUE))
                {
                    err = ERROR_NOT_ENOUGH_MEMORY;
                    break;
                }
                WriteStrToFile(file, colStr.c_str(), &err);
                if (err != NO_ERROR)
                    break; // file error, aborting
            }
            line.clear(); // column strings were written individually
            break;
        }

        case 6:
            if (!FTPFormatString(line, "\r\n%s\r\n", STR_FILE_RULES))
                err = ERROR_NOT_ENOUGH_MEMORY;
            break; // header for RulesForParsing

        case 7: // write the RulesForParsing string
        {
            WriteStrToFile(file, RulesForParsing, &err);
            line.clear(); // already written by the encoded-string formatter
            break;
        }

        default:
            i = -1;
            break; // end of file
        }
        if (err != NO_ERROR)
            break; // file error, aborting
        if (i == -1)
            break; // end of file, finishing successfully
        else
            i++; // move to the next line

        // write the data line to the file
        if (!line.empty() && !WriteEncodedFileBytes(file, line, &err))
        {
            break; // aborting with an error
        }
    }
    return err;
}

// on the line from 'beg' to 'end' it searches for the string 'str' - if it is there (comparison is case sensitive),
// returns TRUE and stores in 'afterStr' the continuation of the line after the found string,
// if it is not there, returns FALSE and puts 'beg' into 'afterStr'
BOOL MatchString(char* beg, char* end, const char* str, char** afterStr)
{
    int l = (int)strlen(str);
    if (l <= end - beg && strncmp(beg, str, l) == 0)
    {
        *afterStr = beg + l;
        return TRUE;
    }
    *afterStr = beg;
    return FALSE;
}

// returns a null-terminated allocated string from 'beg' to 'end'; on low memory
// returns NULL and stores FALSE in 'ret'
char* AllocString(char* beg, char* end, BOOL* ret)
{
    char* s = (char*)malloc(end - beg + 1);
    if (s != NULL)
    {
        memcpy(s, beg, end - beg);
        s[end - beg] = 0;
    }
    else
    {
        TRACE_E(LOW_MEMORY);
        *ret = FALSE;
    }
    return s;
}

// gradually reads a string from the file lines; the currently processed line is 'line' to 'lineEnd'
// (not null-terminated); 'firstLine' is an IN/OUT variable initialized to
// TRUE, the ReadStrFromLines function changes it to FALSE as soon as it finds the beginning of the string;
// 'dynStr' receives the part of the string read from the currently processed line; the pointers
// 'firstLine' and 'dynStr' do not change during the loading of one string (they act as global
// data); in 'success' (must not be NULL) FALSE is returned on a syntax error or a
// 'dynStr' allocation error; returns TRUE if the entire string has already been read and in 'isNULLStr' returns
// TRUE if it is NULL (otherwise the string is stored in 'dynStr')
BOOL ReadStrFromLines(const char* line, const char* lineEnd, BOOL* firstLine,
                      CDynString* dynStr, BOOL* success, BOOL* isNULLStr)
{
    const char* s = line;
    *isNULLStr = FALSE;
    if (*firstLine)
    {
        while (s < lineEnd && *s <= ' ')
            s++;
        if (s == lineEnd)
            return FALSE; // we still have to find the string
        if (*s != '"')    // the string was supposed to start here
        {
            *success = FALSE; // syntax error
            return TRUE;      // error, stop processing the string
        }
        s++;
        *firstLine = FALSE;
        dynStr->Clear();
        if (s + 2 < lineEnd && strncmp(s, "\\0\"", 3) == 0) // escape sequence for NULL
        {
            *isNULLStr = TRUE;
            return TRUE;
        }
    }
    BOOL eol = TRUE;
    BOOL foundEnd = FALSE;
    std::string decoded;
    try
    {
        decoded.reserve(static_cast<size_t>(lineEnd - s) + 2);
        while (s < lineEnd)
        {
            if (*s == '\\')
            {
                if (s + 1 < lineEnd)
                    s++;
                else
                {
                    eol = FALSE;
                    break; // the string continues directly on the next line (without an EOL)
                }
            }
            else
            {
                if (*s == '"')
                {
                    foundEnd = TRUE;
                    eol = FALSE;
                    break; // end of the string found (+ no more EOL)
                }
            }
            decoded.push_back(*s++);
        }
        if (eol)
            decoded.append("\r\n");
    }
    catch (...)
    {
        *success = FALSE;
        return TRUE;
    }
    if (!decoded.empty() &&
        (decoded.size() > static_cast<size_t>((std::numeric_limits<int>::max)()) ||
         !dynStr->Append(decoded.data(), static_cast<int>(decoded.size()))))
        *success = FALSE; // allocation error
    return foundEnd;
}

// helper function: if 'isNULLStr' is TRUE, it returns NULL in 'str'; otherwise, if 'success'
// is TRUE, it tries to duplicate the 'dynStr' string into 'str', and on low memory returns
// 'success' as FALSE
void GetStrResult(char** str, BOOL* success, CDynString* dynStr, BOOL isNULLStr)
{
    if (isNULLStr)
        *str = NULL;
    else
    {
        if (*success)
        {
            const char* s = dynStr->GetString();
            if (s == NULL)
                s = ""; // this is not NULL, we must duplicate an empty string
            *str = DupSallyText(s);
            if (*str == NULL)
                *success = FALSE; // out of memory, aborting
        }
    }
}

BOOL CServerType::ImportFromFile(HANDLE file, DWORD* err, int* errResID)
{
    *err = NO_ERROR;
    *errResID = 0;
    BOOL ret = TRUE;

    if (CompiledAutodetCond != NULL)
    {
        delete CompiledAutodetCond;
        CompiledAutodetCond = NULL;
    }
    if (CompiledParser != NULL)
    {
        delete CompiledParser;
        CompiledParser = NULL;
    }

    std::vector<char> buffer;
    try
    {
        buffer.resize(4096); // transport chunk; grows when a logical line spans it
    }
    catch (...)
    {
        *err = ERROR_NOT_ENOUGH_MEMORY;
        return FALSE;
    }
    size_t readBytes = 0; // how many bytes have been read
    int i = 0;
    BOOL skipNextEOLN = FALSE; // TRUE = '\r' was already processed, if '\n' is at the start of the buffer it is the remainder of the EOL and should be skipped
    CDynString dynStr;         // shared dynamic string for reading strings of unknown length from the file
    BOOL strFirstLine = TRUE;  // shared helper variable for reading strings of unknown length from the file
    while (ret)
    {
        DWORD read;
        const DWORD available = (DWORD)(std::min)(buffer.size() - readBytes,
                                                  static_cast<size_t>(MAXDWORD));
        if (!ReadFile(file, buffer.data() + readBytes, available, &read, NULL))
        {
            *err = GetLastError();
            ret = FALSE;
            break; // aborting with an error
        }
        readBytes += read;
        if (readBytes == 0)
            break; // end of file

        // process the read buffer
        char* end = buffer.data() + readBytes; // end of data in the buffer
        char* s = buffer.data();               // searching for the end of the line
        char* lineBeg = s;              // start of the line
        while (1)
        {
            if (skipNextEOLN && s < end && *s == '\n') // in case the EOL '\r\n' was split
            {
                lineBeg = ++s;
                skipNextEOLN = FALSE;
            }
            while (s < end && *s != '\r' && *s != '\n')
                s++;
            if (s == end)
            {
                if (readBytes == buffer.size())
                {
                    if (lineBeg == buffer.data()) // grow rather than rejecting a long logical line
                    {
                        try
                        {
                            if (buffer.size() > (std::numeric_limits<size_t>::max)() / 2)
                            {
                                *err = ERROR_NOT_ENOUGH_MEMORY;
                                ret = FALSE;
                                break;
                            }
                            buffer.resize(buffer.size() * 2);
                        }
                        catch (...)
                        {
                            *err = ERROR_NOT_ENOUGH_MEMORY;
                            ret = FALSE;
                        }
                        break;
                    }
                    else // the rest of the line is no longer in the buffer, read it
                    {
                        memmove(buffer.data(), lineBeg, end - lineBeg);
                        readBytes = (size_t)(end - lineBeg);
                        break;
                    }
                }
                else // the last line is not terminated by an EOL
                {
                    if (s == lineBeg)
                    { // end of buffer; if this is an empty line, the EOL will be at the start of the next buffer read
                        readBytes = 0;
                        break;
                    }
                }
            }
            // else;  // line ended with an EOL

            // process the line ('lineBeg' to 'lineEnd')
            char* lineEnd = s;
            char* afterTitle;
            BOOL isNULLStr = FALSE;
            switch (i)
            {
            case 0: // signature
            {
                if ((int)strlen(STR_FILE_HEADER) != lineEnd - lineBeg ||
                    strncmp(STR_FILE_HEADER, lineBeg, lineEnd - lineBeg) != 0) // bad signature, not an STR file
                {
                    *errResID = IDS_SRVTYPEIMPNOTSTRFILE;
                    ret = FALSE;
                }
                break;
            }

            case 1: // type name
            {
                if (lineEnd == lineBeg)
                    i--; // skip empty lines
                else
                {
                    if (MatchString(lineBeg, lineEnd, STR_FILE_TYPENAME, &afterTitle))
                    {
                        if (afterTitle < lineEnd && *afterTitle == ' ')
                            afterTitle++; // skip the space after ':'
                        if (afterTitle < lineEnd)
                            TypeName = AllocString(afterTitle, lineEnd, &ret);
                        else
                            ret = FALSE; // error
                    }
                    else
                        ret = FALSE; // error
                }
                break;
            }

            case 2: // header for AutodetectCond
            {
                if (lineEnd == lineBeg)
                    i--; // skip empty lines
                else
                {
                    if (MatchString(lineBeg, lineEnd, STR_FILE_ADCOND, &afterTitle))
                    {
                        strFirstLine = TRUE;
                        if (afterTitle < lineEnd &&
                            ReadStrFromLines(afterTitle, lineEnd, &strFirstLine, &dynStr, &ret, &isNULLStr))
                        {
                            GetStrResult(&AutodetectCond, &ret, &dynStr, isNULLStr);
                            i++; // string fully loaded, skip reading the remainder
                        }
                    }
                    else
                        ret = FALSE; // error
                }
                break;
            }

            case 3: // finish reading the AutodetectCond string
            {
                if (!ReadStrFromLines(lineBeg, lineEnd, &strFirstLine, &dynStr, &ret, &isNULLStr))
                    i--; // the string continues on the next line
                else
                    GetStrResult(&AutodetectCond, &ret, &dynStr, isNULLStr);
                break;
            }

            case 4: // header for Columns
            {
                if (lineEnd == lineBeg)
                    i--; // skip empty lines
                else
                {
                    if (MatchString(lineBeg, lineEnd, STR_FILE_COLUMNS, &afterTitle))
                    {
                        strFirstLine = TRUE;
                        while (afterTitle < lineEnd && *afterTitle <= ' ')
                            afterTitle++; // skip whitespace after ':'
                        if (afterTitle < lineEnd)
                            ret = FALSE; // syntax error, nothing should be here
                    }
                    else
                        ret = FALSE; // error
                }
                break;
            }

            case 5: // load columns
            {
                BOOL read2 = TRUE;
                if (strFirstLine) // the string has not started yet, check whether there is still a column on the line or whether they have ended
                {
                    char* s2 = lineBeg;
                    while (s2 < lineEnd && *s2 <= ' ')
                        s2++;
                    if (s2 < lineEnd && *lineBeg != '"') // this line is no longer ours (the column description is a string)
                    {
                        read2 = FALSE;
                        i++;
                    }
                }
                if (read2)
                {
                    if (!ReadStrFromLines(lineBeg, lineEnd, &strFirstLine, &dynStr, &ret, &isNULLStr))
                        i--; // the string continues on the next line
                    else
                    {
                        if (isNULLStr)
                            ret = FALSE; // syntax error, NULL cannot appear here
                        if (ret)
                        {
                            CSrvTypeColumn* c = new CSrvTypeColumn;
                            if (c != NULL && c->IsGood() && c->LoadFromStr(dynStr.GetString()))
                            {
                                Columns.Add(c);
                                if (Columns.IsGood())
                                {
                                    i--;
                                    strFirstLine = TRUE; // go read the next line with column data
                                }
                                else
                                {
                                    delete c;
                                    Columns.ResetState();
                                    ret = FALSE; // out of memory
                                }
                            }
                            else
                            {
                                if (c != NULL)
                                {
                                    if (c->IsGood())
                                        TRACE_E("Unable to load CSrvTypeColumn from string: " << dynStr.GetString());
                                    delete c;
                                }
                                else
                                    TRACE_E(LOW_MEMORY);
                                ret = FALSE; // out of memory or syntax error
                            }
                        }
                    }
                    break;
                }
                // else break;  // break must not be here (the received line is not ours, pass it on)
            }
            case 6: // load RulesForParsing
            {
                if (lineEnd == lineBeg)
                    i--; // skip empty lines
                else
                {
                    if (MatchString(lineBeg, lineEnd, STR_FILE_RULES, &afterTitle))
                    {
                        strFirstLine = TRUE;
                        if (afterTitle < lineEnd &&
                            ReadStrFromLines(afterTitle, lineEnd, &strFirstLine, &dynStr, &ret, &isNULLStr))
                        {
                            GetStrResult(&RulesForParsing, &ret, &dynStr, isNULLStr);
                            i++; // string fully loaded, skip reading the remainder
                        }
                    }
                    else
                        ret = FALSE; // error
                }
                break;
            }

            case 7: // finish reading the RulesForParsing string
            {
                if (!ReadStrFromLines(lineBeg, lineEnd, &strFirstLine, &dynStr, &ret, &isNULLStr))
                    i--; // the string continues on the next line
                else
                    GetStrResult(&RulesForParsing, &ret, &dynStr, isNULLStr);
                break;
            }

            default: // all expected lines have already been read
            {
                if (lineEnd - lineBeg == 0)
                    i--; // skip empty lines
                else
                {
                    *errResID = IDS_SRVTYPEIMPMOREDATA;
                    ret = FALSE;
                }
                break;
            }
            }
            if (!ret)
            {
                if (*errResID == 0)
                    *errResID = IDS_SRVTYPEIMPSYNERR;
                break; // error, aborting
            }
            i++;

            // skip the EOL
            if (s < end && *s == '\r' && ++s == end)
                skipNextEOLN = TRUE;
            if (s < end && *s == '\n')
                s++;
            lineBeg = s;
        }
    }

    // verify that the list of columns is valid
    if (ret)
        ret = ValidateSrvTypeColumns(&Columns, errResID);

    // verify that the parsing rules are valid
    if (ret)
    {
        CFTPParser* parser = CompileParsingRules(HandleNULLStr(RulesForParsing), &Columns, NULL, NULL, NULL);
        if (parser != NULL)
            delete parser; // parser is OK, discard it again
        else
        {
            ret = FALSE; // error in the rules
            *errResID = IDS_SRVTYPEIMPERRINRULES;
        }
    }

    // verify that the autodetect condition is valid
    if (ret)
    {
        CFTPAutodetCondNode* node = CompileAutodetectCond(
            HandleNULLStr(AutodetectCond), NULL, NULL, NULL, NULL);
        if (node != NULL)
            delete node; // condition is OK, discard it again
        else
        {
            ret = FALSE; // error in the condition
            *errResID = IDS_SRVTYPEIMPERRINACOND;
        }
    }

    return ret;
}

//
// ***********************************************************************************
// Functions ProcessProxyScript plus helper function ExpandText plus function GetProxyScriptText
// ***********************************************************************************
//

struct CEncodedProxyScriptCredentials
{
    std::string ProxyHost;
    std::string ProxyUser;
    std::string ProxyPassword;
    std::string Host;
    std::string User;
    std::string Password;
    std::string Account;

    ~CEncodedProxyScriptCredentials()
    {
        FTPSecureWipe(ProxyPassword);
        FTPSecureWipe(Password);
        FTPSecureWipe(Account);
    }
};

CProxyScriptParams::CProxyScriptParams(CFTPProxyServer* proxyServer, const wchar_t* host,
                                       int port, const wchar_t* user, const wchar_t* password,
                                       const wchar_t* account, BOOL allowEmptyPassword) noexcept
{
    Valid = TRUE;
    if (proxyServer == NULL)
    {
        ProxyHost.clear();
        ProxyPort = 21;
        ProxyUser.clear();
        ProxyPassword.clear();
    }
    else
    {
        Valid = FtpStoreWideText(proxyServer->ProxyHost, ProxyHost);
        ProxyPort = proxyServer->ProxyPort;
        Valid = Valid && FtpStoreWideText(proxyServer->ProxyUser, ProxyUser) &&
                FtpStoreWideText(proxyServer->ProxyPlainPassword, ProxyPassword);
    }

    Valid = Valid && FtpStoreWideText(host != NULL ? host : L"", Host);
    Port = port;
    Valid = Valid && FtpStoreWideText(user != NULL ? user : L"", User) &&
            FtpStoreWideText(password != NULL ? password : L"", Password) &&
            FtpStoreWideText(account != NULL ? account : L"", Account);

    NeedProxyHost = FALSE;
    NeedProxyPassword = FALSE;
    NeedUser = FALSE;
    NeedPassword = FALSE;
    NeedAccount = FALSE;

    AllowEmptyPassword = allowEmptyPassword;
}

CProxyScriptParams::CProxyScriptParams()
{
    Valid = TRUE;
    ProxyHost.clear();
    ProxyPort = 21;
    ProxyUser.clear();
    ProxyPassword.clear();
    Host.clear();
    Port = 21;
    User.clear();
    Password.clear();
    Account.clear();

    NeedProxyHost = FALSE;
    NeedProxyPassword = FALSE;
    NeedUser = FALSE;
    NeedPassword = FALSE;
    NeedAccount = FALSE;

    AllowEmptyPassword = FALSE;
}

CProxyScriptParams::~CProxyScriptParams()
{
    FTPSecureWipe(ProxyPassword);
    FTPSecureWipe(Password);
    FTPSecureWipe(Account);
}

// 'text' owns the resulting host or command; 'logText' owns the result that can
// be published in the log (passwords are replaced with the word "hidden").
// 'strBeg' to 'strEnd'
// is the script text to be expanded; 'hostVarsOnly' is TRUE when expanding
// the host (only the Host and ProxyHost variables are allowed, and CRLF is not appended at the end as it is for commands);
// 'proxyHostNeeded' (if not NULL) returns TRUE when the $(ProxyHost) variable is used during the expansion
// return values:
// - error: returns FALSE, the error code is returned in 'errCode' and '*errorPos' gives the error position
// - the line should be skipped: returns TRUE with 'skipThisLine'==TRUE and with
//   'text' and 'logText' emptied (a skipped line is not a command)
// - missing variable values: returns FALSE, but 'errCode' is 0
// - everything OK: returns TRUE with transactionally published owned strings
static BOOL ExpandEncodedText(std::string* text, std::string* logText,
                              const char* strBeg, const char* strEnd,
                              CProxyScriptParams* scriptParams,
                              const CEncodedProxyScriptCredentials* credentials,
                              const char** errorPos, int* errCode,
                              BOOL hostVarsOnly, BOOL* skipThisLine, BOOL* proxyHostNeeded)
{
    BOOL ret = TRUE;
    DWORD needUserInput = 0; // != 0 - the user should enter the values of the marked variables (bitwise ORed in this DWORD)
    BOOL localSkipThisLine = FALSE;
    if (skipThisLine != NULL)
        *skipThisLine = FALSE;
    std::string stagedText;
    CFTPSecureByteStringGuard stagedTextGuard(stagedText);
    std::string stagedLog;
    const char* s = strBeg;
    while (s < strEnd)
    {
        if (*s == '$')
        {
            if (s + 1 < strEnd && *(s + 1) == '$') // escape sequence for '$'
            {
                if (text != NULL)
                    stagedText.push_back('$');
                if (logText != NULL)
                    stagedLog.push_back('$');
                s += 2;
            }
            else
            {
                if (s + 1 < strEnd && *(s + 1) == '(')
                {
                    s += 2;
                    int i;
                    for (i = 0; i < 9; i++)
                    {
                        const char* varName;
                        switch (i)
                        {
                        case 0:
                            varName = "ProxyHost";
                            break;
                        case 1:
                            varName = "ProxyPort";
                            break;
                        case 2:
                            varName = "ProxyUser";
                            break;
                        case 3:
                            varName = "ProxyPassword";
                            break;
                        case 4:
                            varName = "Host";
                            break;
                        case 5:
                            varName = "Port";
                            break;
                        case 6:
                            varName = "User";
                            break;
                        case 7:
                            varName = "Password";
                            break;
                        case 8:
                            varName = "Account";
                            break;
                        }
                        int varNameLen = (int)strlen(varName);
                        if (s + varNameLen < strEnd && *(s + varNameLen) == ')' &&
                            FtpEqualAsciiTokenNoCase(
                                std::string_view(s, static_cast<size_t>(varNameLen)),
                                varName)) // variable found
                        {
                            if (i == 0 && proxyHostNeeded != NULL)
                                *proxyHostNeeded = TRUE;
                            if (hostVarsOnly && i != 0 && i != 4)
                            {
                                *errorPos = s;
                                *errCode = IDS_PRXSCRERR_HOSTVARSONLY; // variable not allowed
                                ret = FALSE;
                                break;
                            }
                            if (scriptParams != NULL) // if we have variable values, insert the variable value
                            {
                                std::string portText;
                                const char* value = "";
                                BOOL hidden = FALSE;
                                switch (i)
                                {
                                case 0:
                                {
                                    if (!scriptParams->ProxyHost.empty())
                                        value = credentials->ProxyHost.c_str();
                                    else
                                        needUserInput |= 1;
                                    break;
                                }

                                case 1:
                                {
                                    portText = std::to_string(scriptParams->ProxyPort);
                                    value = portText.c_str();
                                    break;
                                }

                                case 2:
                                {
                                    if (!scriptParams->ProxyUser.empty())
                                        value = credentials->ProxyUser.c_str();
                                    else
                                        localSkipThisLine = TRUE;
                                    break;
                                }

                                case 3:
                                {
                                    hidden = TRUE;
                                    if (!scriptParams->ProxyPassword.empty())
                                        value = credentials->ProxyPassword.c_str();
                                    else
                                        needUserInput |= 1 << 3;
                                    break;
                                }

                                case 4:
                                    value = credentials->Host.c_str();
                                    break;

                                case 5:
                                {
                                    portText = std::to_string(scriptParams->Port);
                                    value = portText.c_str();
                                    break;
                                }

                                case 6:
                                {
                                    if (!scriptParams->User.empty())
                                        value = credentials->User.c_str();
                                    else
                                        needUserInput |= 1 << 6;
                                    break;
                                }

                                case 7:
                                {
                                    hidden = TRUE;
                                    if (!scriptParams->Password.empty() || scriptParams->AllowEmptyPassword)
                                    {
                                        scriptParams->AllowEmptyPassword = FALSE;
                                        value = credentials->Password.c_str();
                                    }
                                    else
                                        needUserInput |= 1 << 7;
                                    break;
                                }

                                case 8:
                                {
                                    hidden = TRUE;
                                    if (!scriptParams->Account.empty())
                                        value = credentials->Account.c_str();
                                    else
                                        needUserInput |= 1 << 8;
                                    break;
                                }
                                }
                                if (text != NULL)
                                    stagedText.append(value);
                                if (hidden)
                                    value = LoadStr(IDS_HIDDENPASSWORD);
                                if (logText != NULL)
                                {
                                    if (hidden)
                                        stagedLog.push_back('(');
                                    stagedLog.append(value);
                                    if (hidden)
                                        stagedLog.push_back(')');
                                }
                            }
                            s += varNameLen + 1; // skip the variable
                            break;
                        }
                    }
                    if (i == 9)
                    {
                        *errorPos = s;
                        *errCode = IDS_PRXSCRERR_UNKNOWNVAR; // unknown variable
                        ret = FALSE;
                    }
                    if (*errCode != 0)
                        break; // stop
                }
                else // something other than '(' and '$' follows '$' - copy 1:1
                {
                    if (text != NULL)
                        stagedText.push_back(*s);
                    if (logText != NULL)
                        stagedLog.push_back(*s);
                    s++;
                }
            }
        }
        else
        {
            if (text != NULL)
                stagedText.push_back(*s);
            if (logText != NULL)
                stagedLog.push_back(*s);
            s++;
        }
    }
    if (ret)
    {
        if (localSkipThisLine && skipThisLine != NULL)
            *skipThisLine = TRUE;
        if (!localSkipThisLine && needUserInput != 0) // transfer flags from needUserInput to scriptParams->NeedXXX
        {
            ret = FALSE;
            int i;
            for (i = 0; i < 9; i++)
            {
                if (needUserInput & (1 << i)) // necessarily means that 'scriptParams' is not NULL
                {
                    switch (i)
                    {
                    case 0:
                        scriptParams->NeedProxyHost = TRUE;
                        break;
                    case 3:
                        scriptParams->NeedProxyPassword = TRUE;
                        break;
                    case 6:
                        scriptParams->NeedUser = TRUE;
                        break;
                    case 7:
                        scriptParams->NeedPassword = TRUE;
                        break;
                    case 8:
                        scriptParams->NeedAccount = TRUE;
                        break;
                    }
                }
            }
        }
    }
    if (ret)
    {
        if (localSkipThisLine)
        {
            // A skipped line produces no command at all, so nothing may be
            // published: the half-expanded text never got its CRLF (that append
            // lives in the other arm), and ProcessProxyScript's caller reads a
            // non-empty command as "send this to the server". A skipped last
            // line would otherwise write a line with no terminator and hang the
            // login on the reply timeout instead of reporting
            // IDS_INCOMPLETEPRXSCR2. The wipe also keeps any credential that was
            // expanded before the optional variable out of the caller's string.
            if (text != NULL)
                FTPSecureWipe(*text);
            if (logText != NULL)
                FTPSecureWipe(*logText);
        }
        else
        {
            if (!hostVarsOnly) // append CRLF for FTP commands
            {
                if (text != NULL)
                    stagedText.append("\r\n");
                if (logText != NULL)
                    stagedLog.append("\r\n");
            }
            if (text != NULL)
                text->swap(stagedText);
            if (logText != NULL)
                logText->swap(stagedLog);
        }
    }
    return ret;
}

static BOOL ExpandTextDynamic(std::string* text, std::string* logText,
                              const char* strBeg, const char* strEnd,
                              const CFtpTextCodec& textCodec, CProxyScriptParams* scriptParams,
                              const char** errorPos, int* errCode,
                              BOOL hostVarsOnly, BOOL* skipThisLine, BOOL* proxyHostNeeded,
                              BOOL* lowMemory) noexcept
{
    if (lowMemory != NULL)
        *lowMemory = FALSE;
    if (scriptParams != NULL && !scriptParams->IsGood())
    {
        if (lowMemory != NULL)
            *lowMemory = TRUE;
        return FALSE;
    }
    if (strBeg == NULL || strEnd == NULL || strEnd < strBeg)
        return FALSE;

    try
    {
        CEncodedProxyScriptCredentials credentials;
        if (scriptParams != NULL)
        {
            if (!textCodec.Encode(scriptParams->ProxyHost.data(), scriptParams->ProxyHost.size(), credentials.ProxyHost) ||
                !textCodec.Encode(scriptParams->ProxyUser.data(), scriptParams->ProxyUser.size(), credentials.ProxyUser) ||
                !textCodec.Encode(scriptParams->ProxyPassword.data(), scriptParams->ProxyPassword.size(), credentials.ProxyPassword) ||
                !textCodec.Encode(scriptParams->Host.data(), scriptParams->Host.size(), credentials.Host) ||
                !textCodec.Encode(scriptParams->User.data(), scriptParams->User.size(), credentials.User) ||
                !textCodec.Encode(scriptParams->Password.data(), scriptParams->Password.size(), credentials.Password) ||
                !textCodec.Encode(scriptParams->Account.data(), scriptParams->Account.size(), credentials.Account))
            {
                *errCode = IDS_PRXSCRERR_CANNOTENCODE;
                return FALSE;
            }
        }
        return ExpandEncodedText(text, logText, strBeg, strEnd, scriptParams,
                                 scriptParams != NULL ? &credentials : NULL,
                                 errorPos, errCode, hostVarsOnly, skipThisLine,
                                 proxyHostNeeded);
    }
    catch (...)
    {
        if (lowMemory != NULL)
            *lowMemory = TRUE;
        return FALSE;
    }
}

static BOOL ExpandHostTextDynamic(std::wstring* text, const char* strBeg, const char* strEnd,
                                  CProxyScriptParams* scriptParams, const char** errorPos,
                                  int* errCode, BOOL* proxyHostNeeded,
                                  BOOL* lowMemory) noexcept
{
    if (lowMemory != NULL)
        *lowMemory = FALSE;
    if (scriptParams != NULL && !scriptParams->IsGood())
    {
        if (lowMemory != NULL)
            *lowMemory = TRUE;
        return FALSE;
    }
    if (strBeg == NULL || strEnd == NULL || strEnd < strBeg)
        return FALSE;

    static const char* const variableNames[] = {
        "ProxyHost", "ProxyPort", "ProxyUser", "ProxyPassword", "Host",
        "Port", "User", "Password", "Account",
    };
    try
    {
        std::wstring staged;
        const char* current = strBeg;
        while (current < strEnd)
        {
            if (*current != '$')
            {
                const char* literalEnd = current + 1;
                while (literalEnd < strEnd && *literalEnd != '$')
                    literalEnd++;
                std::wstring literal;
                if (!FtpDecodeLocalText(
                        std::string_view(current, static_cast<size_t>(literalEnd - current)),
                        literal))
                {
                    if (lowMemory != NULL)
                        *lowMemory = TRUE;
                    return FALSE;
                }
                if (text != NULL)
                    staged.append(literal);
                current = literalEnd;
                continue;
            }

            if (current + 1 < strEnd && current[1] == '$')
            {
                if (text != NULL)
                    staged.push_back(L'$');
                current += 2;
                continue;
            }
            if (current + 1 >= strEnd || current[1] != '(')
            {
                if (text != NULL)
                    staged.push_back(L'$');
                current++;
                continue;
            }

            const char* name = current + 2;
            int variable = -1;
            for (int i = 0; i < static_cast<int>(_countof(variableNames)); i++)
            {
                const size_t length = strlen(variableNames[i]);
                if (name + length < strEnd && name[length] == ')' &&
                    _strnicmp(name, variableNames[i], length) == 0)
                {
                    variable = i;
                    current = name + length + 1;
                    break;
                }
            }
            if (variable < 0)
            {
                *errorPos = name;
                *errCode = IDS_PRXSCRERR_UNKNOWNVAR;
                return FALSE;
            }
            if (variable != 0 && variable != 4)
            {
                *errorPos = name;
                *errCode = IDS_PRXSCRERR_HOSTVARSONLY;
                return FALSE;
            }
            if (variable == 0)
            {
                if (proxyHostNeeded != NULL)
                    *proxyHostNeeded = TRUE;
                if (scriptParams != NULL)
                {
                    if (scriptParams->ProxyHost.empty())
                    {
                        scriptParams->NeedProxyHost = TRUE;
                        return FALSE;
                    }
                    if (text != NULL)
                        staged.append(scriptParams->ProxyHost);
                }
            }
            else if (scriptParams != NULL && text != NULL)
                staged.append(scriptParams->Host);
        }
        if (text != NULL)
            text->swap(staged);
        return TRUE;
    }
    catch (...)
    {
        if (lowMemory != NULL)
            *lowMemory = TRUE;
        return FALSE;
    }
}

BOOL ProcessProxyScript(const CFtpTextCodec& textCodec,
                        const char* script, const char** execPoint, int lastCmdReply,
                        CProxyScriptParams* scriptParams, std::wstring* connectionHost, unsigned short* port,
                        std::string* sendCommand, std::string* logCommand,
                        std::string* errorDescription,
                        BOOL* proxyHostNeeded, BOOL* lowMemory) noexcept
{
    CALL_STACK_MESSAGE2("ProcessProxyScript(, , %d, , , , , , ,)", lastCmdReply);

    if (script == NULL || execPoint == NULL)
    {
        TRACE_E("ProcessProxyScript(): script and execPoint may not be NULL!");
        return FALSE;
    }
    if (lowMemory != NULL)
        *lowMemory = FALSE;
    try
    {
    if (scriptParams != NULL)
    {
        scriptParams->NeedProxyHost = FALSE;
        scriptParams->NeedProxyPassword = FALSE;
        scriptParams->NeedUser = FALSE;
        scriptParams->NeedPassword = FALSE;
        scriptParams->NeedAccount = FALSE;
    }
    if (connectionHost != NULL)
        connectionHost->clear();
    if (port != NULL)
        *port = 21;
    std::string stagedSendCommand;
    CFTPSecureByteStringGuard stagedSendCommandGuard(stagedSendCommand);
    std::string stagedLogCommand;
    if (errorDescription != NULL)
        errorDescription->clear();
    if (proxyHostNeeded != NULL)
        *proxyHostNeeded = FALSE;
    BOOL allocationFailure = FALSE;
    int errCode = 0;
    const char* s = *execPoint == NULL ? script : *execPoint;
    BOOL validateScript = scriptParams == NULL;
    BOOL processNextLine = s > script; // TRUE = we should process the next line with a command in the script
    if (s == script)                   // we are at the beginning of the script
    {
        while (*s != 0 && *s <= ' ')
            s++; // skip white-spaces
        const std::string_view scriptTail(s);
        if (scriptTail.size() >= 11 &&
            FtpEqualAsciiTokenNoCase(scriptTail.substr(0, 11), "Connect to:"))
        {
            s += 11;
            while (*s != 0 && *s <= ' ' && *s != '\r' && *s != '\n')
                s++; // skip white-spaces to EOL
            const char* host = s;
            while (*s > ' ' && *s != ':')
                s++; // look for the end of the host
            if (s > host)
            {
                const char* hostEnd = s;
                const char* portStr = NULL;
                const char* portEnd = NULL;
                if (*s == ':')
                {
                    s++;
                    while (*s != 0 && *s <= ' ' && *s != '\r' && *s != '\n')
                        s++; // skip white-spaces to EOL
                    portStr = s;
                    while (*s > ' ')
                        s++; // look for the end of the port
                    if (s > portStr)
                        portEnd = s;
                    else
                        portStr = NULL;
                }
                while (*s != 0 && *s <= ' ' && *s != '\r' && *s != '\n')
                    s++; // skip white-spaces to EOL
                if (*s == 0 || *s == '\r' || *s == '\n')
                {
                    // 'host' to 'hostEnd' is the host; 'portStr' to 'portEnd' is the port ('portStr'==NULL -> port 21)
                    if (portStr != NULL)
                    {
                        if (portEnd - portStr == 12 &&
                            FtpEqualAsciiTokenNoCase(std::string_view(portStr, 12),
                                                     "$(ProxyPort)"))
                        {
                            if (scriptParams != NULL && port != NULL)
                                *port = (unsigned short)scriptParams->ProxyPort;
                        }
                        else
                        {
                            if (portEnd - portStr == 7 &&
                                FtpEqualAsciiTokenNoCase(std::string_view(portStr, 7),
                                                         "$(Port)"))
                            {
                                if (scriptParams != NULL && port != NULL)
                                    *port = (unsigned short)scriptParams->Port;
                            }
                            else
                            {
                                int portNum = 0;
                                const char* n = portStr;
                                while (n < portEnd && *n >= '0' && *n <= '9')
                                {
                                    portNum = 10 * portNum + (*n - '0');
                                    n++;
                                }
                                if (n == portEnd)
                                {
                                    if (portNum >= 1 && portNum <= 65535)
                                    {
                                        if (port != NULL)
                                            *port = (unsigned short)portNum;
                                    }
                                    else
                                    {
                                        s = portStr;
                                        errCode = IDS_PORTISUSHORT;
                                    }
                                }
                                else
                                {
                                    s = n;
                                    errCode = IDS_PRXSCRERR_INVPORT;
                                }
                            }
                        }
                    }
                    if (errCode == 0) // port is OK, continue with the host
                    {
                        std::wstring expandedHost;
                        if (ExpandHostTextDynamic(connectionHost != NULL ? &expandedHost : NULL,
                                                  host, hostEnd, scriptParams, &s, &errCode,
                                                  proxyHostNeeded, &allocationFailure))
                        {
                            if (connectionHost != NULL)
                                connectionHost->swap(expandedHost);
                            if (*s == '\r')
                                s++;
                            if (*s == '\n')
                                s++;
                            if (validateScript)
                                processNextLine = TRUE;
                        }
                    }
                }
                else
                    errCode = IDS_PRXSCRERR_INVHOSTORPORT;
            }
            else
                errCode = IDS_PRXSCRERR_HOSTEMPTY;
        }
        else
            errCode = IDS_PRXSCRERR_INVSTART;
    }

    BOOL testIfFirstCmdLineIs3xx = validateScript;
    BOOL errorNoCommandsInScript = processNextLine && validateScript;
    while (processNextLine && *s != 0) // processing command lines
    {
        processNextLine = FALSE;
        while (*s != 0 && *s <= ' ')
            s++; // skip white-spaces (including EOL)
        const std::string_view commandTail(s);
        BOOL sendOnlyIf3xxReply = commandTail.size() >= 4 &&
                                  FtpEqualAsciiTokenNoCase(commandTail.substr(0, 4),
                                                           "3xx:");
        if (sendOnlyIf3xxReply)
        {
            if (testIfFirstCmdLineIs3xx) // on the first line, "3xx:" makes no sense (no previous command, therefore no reply)
            {
                errCode = IDS_PRXSCRERR_3XXONFIRSTLINE;
                break;
            }
            else
            {
                s += 4;
                while (*s != 0 && *s <= ' ' && *s != '\r' && *s != '\n')
                    s++; // skip white-spaces (without EOL)
            }
        }
        testIfFirstCmdLineIs3xx = FALSE;
        const char* lineBeg = s;
        while (*s != 0 && *s != '\r' && *s != '\n')
            s++; // to the end of the script or end of the line

        if (s > lineBeg &&
            (validateScript || !sendOnlyIf3xxReply ||
             lastCmdReply != -1 && FTP_DIGIT_1(lastCmdReply) == FTP_D1_PARTIALSUCCESS /* 3xx */))
        {
            BOOL skipThisLine;
            if (ExpandTextDynamic(sendCommand != NULL ? &stagedSendCommand : NULL,
                                  logCommand != NULL ? &stagedLogCommand : NULL,
                                  lineBeg, s, textCodec, scriptParams,
                                  &s, &errCode, FALSE, &skipThisLine, NULL,
                                  &allocationFailure))
            {
                if (skipThisLine) // the line should be skipped (contains an optional variable)
                {
                    lastCmdReply = -1;
                    processNextLine = TRUE;
                }
                else // everything OK, return the command and the text for the log
                {
                    if (*s == '\r')
                        s++;
                    if (*s == '\n')
                        s++;
                }
                if (validateScript)
                    processNextLine = TRUE;
                errorNoCommandsInScript = FALSE;
            }
        }
        else // empty line at the end of the script or after "3xx:" or a line skipped due to the "3xx:" condition
        {
            if (s > lineBeg)
                lastCmdReply = -1; // just skip the empty line (pretend it was not there)
            processNextLine = TRUE;
        }
    }

    if (errCode == 0 && errorNoCommandsInScript)
        errCode = IDS_PRXSCRERR_NONECMDS;
    if (!allocationFailure && (errCode != 0 || scriptParams == NULL || !scriptParams->NeedUserInput()))
        *execPoint = s;
    if (errCode != 0 && errorDescription != NULL &&
        !FtpStoreProtocolBytes(LoadStr(errCode), *errorDescription))
    {
        if (lowMemory != NULL)
            *lowMemory = TRUE;
        return FALSE;
    }
    if (lowMemory != NULL)
        *lowMemory = allocationFailure;
    const BOOL result = !allocationFailure && errCode == 0;
    if (result)
    {
        if (sendCommand != NULL)
            sendCommand->swap(stagedSendCommand);
        if (logCommand != NULL)
            logCommand->swap(stagedLogCommand);
    }
    return result;
    }
    catch (...)
    {
        if (lowMemory != NULL)
            *lowMemory = TRUE;
        return FALSE;
    }
}

const char* GetProxyScriptText(CFTPProxyServerType type, BOOL textForDialog)
{
    switch (type)
    {
    case fpstNotUsed: // "direct connection" (without a proxy server)
        return "Connect to: $(Host):$(Port)\r\n"
               "USER $(User)\r\n"
               "3xx: PASS $(Password)\r\n"
               "3xx: ACCT $(Account)\r\n";

    case fpstSocks4:
    case fpstSocks4A:
    case fpstSocks5:
    case fpstHTTP1_1:
        return textForDialog ? "" : "Connect to: $(ProxyHost):$(ProxyPort)\r\n"
                                    "USER $(User)\r\n"
                                    "3xx: PASS $(Password)\r\n"
                                    "3xx: ACCT $(Account)\r\n";

    case fpstFTP_SITE_host_colon_port: // USER fw_user;PASS fw_pass;SITE host:port;USER user;PASS pass;ACCT acct
        return "Connect to: $(ProxyHost):$(ProxyPort)\r\n"
               "USER $(ProxyUser)\r\n"
               "3xx: PASS $(ProxyPassword)\r\n"
               "SITE $(Host):$(Port)\r\n"
               "USER $(User)\r\n"
               "3xx: PASS $(Password)\r\n"
               "3xx: ACCT $(Account)\r\n";

    case fpstFTP_SITE_host_space_port: // USER fw_user;PASS fw_pass;SITE host port;USER user;PASS pass;ACCT acct
        return "Connect to: $(ProxyHost):$(ProxyPort)\r\n"
               "USER $(ProxyUser)\r\n"
               "3xx: PASS $(ProxyPassword)\r\n"
               "SITE $(Host) $(Port)\r\n"
               "USER $(User)\r\n"
               "3xx: PASS $(Password)\r\n"
               "3xx: ACCT $(Account)\r\n";

    case fpstFTP_SITE_user_host_colon_port: // USER fw_user;PASS fw_pass;SITE user@host:port;PASS pass;ACCT acct
        return "Connect to: $(ProxyHost):$(ProxyPort)\r\n"
               "USER $(ProxyUser)\r\n"
               "3xx: PASS $(ProxyPassword)\r\n"
               "SITE $(User)@$(Host):$(Port)\r\n"
               "3xx: PASS $(Password)\r\n"
               "3xx: ACCT $(Account)\r\n";

    case fpstFTP_SITE_user_host_space_port: // USER fw_user;PASS fw_pass;SITE user@host port;PASS pass;ACCT acct
        return "Connect to: $(ProxyHost):$(ProxyPort)\r\n"
               "USER $(ProxyUser)\r\n"
               "3xx: PASS $(ProxyPassword)\r\n"
               "SITE $(User)@$(Host) $(Port)\r\n"
               "3xx: PASS $(Password)\r\n"
               "3xx: ACCT $(Account)\r\n";

    case fpstFTP_OPEN_host_port: // USER fw_user;PASS fw_pass;OPEN host:port;USER user;PASS pass;ACCT acct
        return "Connect to: $(ProxyHost):$(ProxyPort)\r\n"
               "USER $(ProxyUser)\r\n"
               "3xx: PASS $(ProxyPassword)\r\n"
               "OPEN $(Host):$(Port)\r\n"
               "USER $(User)\r\n"
               "3xx: PASS $(Password)\r\n"
               "3xx: ACCT $(Account)\r\n";

    case fpstFTP_transparent: // (fw_host+fw_port are not used - connect to host+port) USER fw_user;PASS fw_pass;USER user;PASS pass;ACCT acct
        return "Connect to: $(Host):$(Port)\r\n"
               "USER $(ProxyUser)\r\n"
               "3xx: PASS $(ProxyPassword)\r\n"
               "USER $(User)\r\n"
               "3xx: PASS $(Password)\r\n"
               "3xx: ACCT $(Account)\r\n";

    case fpstFTP_USER_user_host_colon_port: // USER fw_user;PASS fw_pass;USER user@host:port;PASS pass;ACCT acct
        return "Connect to: $(ProxyHost):$(ProxyPort)\r\n"
               "USER $(ProxyUser)\r\n"
               "3xx: PASS $(ProxyPassword)\r\n"
               "USER $(User)@$(Host):$(Port)\r\n"
               "3xx: PASS $(Password)\r\n"
               "3xx: ACCT $(Account)\r\n";

    case fpstFTP_USER_user_host_space_port: // USER fw_user;PASS fw_pass;USER user@host port;PASS pass;ACCT acct
        return "Connect to: $(ProxyHost):$(ProxyPort)\r\n"
               "USER $(ProxyUser)\r\n"
               "3xx: PASS $(ProxyPassword)\r\n"
               "USER $(User)@$(Host) $(Port)\r\n"
               "3xx: PASS $(Password)\r\n"
               "3xx: ACCT $(Account)\r\n";

    case fpstFTP_USER_fireuser_host: // USER fw_user@host:port;PASS fw_pass;USER user;PASS pass;ACCT acct
        return "Connect to: $(ProxyHost):$(ProxyPort)\r\n"
               "USER $(ProxyUser)@$(Host):$(Port)\r\n"
               "3xx: PASS $(ProxyPassword)\r\n"
               "USER $(User)\r\n"
               "3xx: PASS $(Password)\r\n"
               "3xx: ACCT $(Account)\r\n";

    case fpstFTP_USER_user_host_fireuser: // USER user@host:port fw_user;PASS pass;ACCT fw_pass;ACCT acct
        return "Connect to: $(ProxyHost):$(ProxyPort)\r\n"
               "USER $(User)@$(Host):$(Port) $(ProxyUser)\r\n"
               "3xx: PASS $(Password)\r\n"
               "3xx: ACCT $(ProxyPassword)\r\n"
               "3xx: ACCT $(Account)\r\n";

    case fpstFTP_USER_user_fireuser_host: // USER user@fw_user@host:port;PASS pass@fw_pass;ACCT acct
        return "Connect to: $(ProxyHost):$(ProxyPort)\r\n"
               "USER $(User)@$(ProxyUser)@$(Host):$(Port)\r\n"
               "3xx: PASS $(Password)@$(ProxyPassword)\r\n"
               "3xx: ACCT $(Account)\r\n";

    default:
        return "";
    }
}

unsigned short GetProxyDefaultPort(CFTPProxyServerType type)
{
    switch (type)
    {
    case fpstSocks4:
    case fpstSocks4A:
    case fpstSocks5:
        return 1080;

    case fpstHTTP1_1:
        return 8080;

    case fpstFTP_transparent:
        return 0; // (fw_host+fw_port are not used - connect to host+port) USER fw_user;PASS fw_pass;USER user;PASS pass;ACCT acct

        //    case fpstNotUsed:                        // "direct connection" (without a proxy server)
        //    case fpstFTP_SITE_host_colon_port:       // USER fw_user;PASS fw_pass;SITE host:port;USER user;PASS pass;ACCT acct
        //    case fpstFTP_SITE_host_space_port:       // USER fw_user;PASS fw_pass;SITE host port;USER user;PASS pass;ACCT acct
        //    case fpstFTP_SITE_user_host_colon_port:  // USER fw_user;PASS fw_pass;SITE user@host:port;PASS pass;ACCT acct
        //    case fpstFTP_SITE_user_host_space_port:  // USER fw_user;PASS fw_pass;SITE user@host port;PASS pass;ACCT acct
        //    case fpstFTP_OPEN_host_port:             // USER fw_user;PASS fw_pass;OPEN host:port;USER user;PASS pass;ACCT acct
        //    case fpstFTP_USER_user_host_colon_port:  // USER fw_user;PASS fw_pass;USER user@host:port;PASS pass;ACCT acct
        //    case fpstFTP_USER_user_host_space_port:  // USER fw_user;PASS fw_pass;USER user@host port;PASS pass;ACCT acct
        //    case fpstFTP_USER_fireuser_host:         // USER fw_user@host:port;PASS fw_pass;USER user;PASS pass;ACCT acct
        //    case fpstFTP_USER_user_host_fireuser:    // USER user@host:port fw_user;PASS pass;ACCT fw_pass;ACCT acct
        //    case fpstFTP_USER_user_fireuser_host:    // USER user@fw_user@host:port;PASS pass@fw_pass;ACCT acct
        //    case fpstOwnScript:                      // the user wrote their own script for connecting to the FTP server
    default:
        return 21;
    }
}

BOOL HavePassword(CFTPProxyServerType type)
{
    return type != fpstSocks4 && type != fpstSocks4A;
}

BOOL HaveHostAndPort(CFTPProxyServerType type)
{
    return type != fpstFTP_transparent;
}

static int GetProxyTypeNameResourceID(CFTPProxyServerType type)
{
    switch (type)
    {
    case fpstSocks4:
        return IDS_PROXYSERVER_SOCKS4;
    case fpstSocks4A:
        return IDS_PROXYSERVER_SOCKS4A;
    case fpstSocks5:
        return IDS_PROXYSERVER_SOCKS5;
    case fpstHTTP1_1:
        return IDS_PROXYSERVER_HTTP11;
    case fpstFTP_SITE_host_colon_port:
        return IDS_PROXYSERVER_SITE1;
    case fpstFTP_SITE_host_space_port:
        return IDS_PROXYSERVER_SITE2;
    case fpstFTP_SITE_user_host_colon_port:
        return IDS_PROXYSERVER_SITE3;
    case fpstFTP_SITE_user_host_space_port:
        return IDS_PROXYSERVER_SITE4;
    case fpstFTP_OPEN_host_port:
        return IDS_PROXYSERVER_OPEN;
    case fpstFTP_transparent:
        return IDS_PROXYSERVER_TRANSPAR;
    case fpstFTP_USER_user_host_colon_port:
        return IDS_PROXYSERVER_USER1;
    case fpstFTP_USER_user_host_space_port:
        return IDS_PROXYSERVER_USER2;
    case fpstFTP_USER_fireuser_host:
        return IDS_PROXYSERVER_USER3;
    case fpstFTP_USER_user_host_fireuser:
        return IDS_PROXYSERVER_USER4;
    case fpstFTP_USER_user_fireuser_host:
        return IDS_PROXYSERVER_USER5;
    case fpstOwnScript:
        return IDS_PROXYSERVER_USERDEF;
    default:
        TRACE_E("GetProxyTypeNameResourceID(): unknown proxy server type!");
        return 0;
    }
}

BOOL GetProxyTypeName(CFTPProxyServerType type, std::string& name) noexcept
{
    const int resourceID = GetProxyTypeNameResourceID(type);
    return resourceID != 0 && FtpStoreLocalTextBytes(LoadStr(resourceID), name);
}

BOOL GetProxyTypeNameW(CFTPProxyServerType type, std::wstring& name) noexcept
{
    const int resourceID = GetProxyTypeNameResourceID(type);
    if (resourceID == 0)
        return FALSE;
    try
    {
        std::wstring staged = LangStr(resourceID);
        name.swap(staged);
        return TRUE;
    }
    catch (...)
    {
        return FALSE;
    }
}

BOOL IsSOCKSOrHTTPProxy(CFTPProxyServerType type)
{
    switch (type)
    {
    case fpstSocks4:
    case fpstSocks4A:
    case fpstSocks5:
    case fpstHTTP1_1:
        return TRUE;

    default:
        return FALSE;
    }
}

//
// ****************************************************************************
// CFTPProxyForDataCon
//

CFTPProxyForDataCon::CFTPProxyForDataCon(CFTPProxyServerType proxyType, DWORD proxyHostIP,
                                         unsigned short proxyPort, const wchar_t* proxyUser,
                                         const wchar_t* proxyPassword, const wchar_t* host,
                                         DWORD hostIP, unsigned short hostPort) noexcept
{
    ProxyType = proxyType;
    ProxyHostIP = proxyHostIP;
    ProxyPort = proxyPort;
    Valid = FtpStoreWideText(proxyUser != NULL ? proxyUser : L"", ProxyUser) &&
            FtpStoreWideText(proxyPassword != NULL ? proxyPassword : L"", ProxyPassword) &&
            FtpStoreWideText(host != NULL ? host : L"", Host);
    HostIP = hostIP;
    HostPort = hostPort;
}

CFTPProxyForDataCon::~CFTPProxyForDataCon()
{
    FTPSecureWipe(ProxyPassword);
}

//
// ****************************************************************************
// CFTPProxyServer
//

void CFTPProxyServer::Init(int proxyUID)
{
    ProxyUID = proxyUID;
    ProxyName.clear();
    ProxyType = fpstFTP_USER_user_host_colon_port;
    ProxyHost.clear();
    ProxyPort = 21;
    ProxyUser.clear();
    ProxyEncryptedPassword = NULL;
    ProxyEncryptedPasswordSize = 0;
    ProxyPlainPassword.clear();
    SaveProxyPassword = FALSE;
    ProxyScript.clear();

    // NOTE: the default values here must match the default values used in the Save() method
}

void CFTPProxyServer::Release()
{
    if (ProxyEncryptedPassword != NULL)
    {
        memset(ProxyEncryptedPassword, 0, ProxyEncryptedPasswordSize); // cleaning memory containing the password
        SalamanderGeneral->Free(ProxyEncryptedPassword);
    }
    FTPSecureWipe(ProxyPlainPassword);
    Init(ProxyUID);
}

CFTPProxyServer*
CFTPProxyServer::MakeCopy() noexcept
{
    CFTPProxyServer* n = NULL;
    try
    {
        n = new CFTPProxyServer(0);
        if (n == NULL)
            return NULL;
        n->ProxyUID = ProxyUID;
        if (!FtpStoreWideText(ProxyName, n->ProxyName))
        {
            delete n;
            return NULL;
        }
        n->ProxyType = ProxyType;
        if (!FtpStoreWideText(ProxyHost, n->ProxyHost))
        {
            delete n;
            return NULL;
        }
        n->ProxyPort = ProxyPort;
        if (!FtpStoreWideText(ProxyUser, n->ProxyUser))
        {
            delete n;
            return NULL;
        }
        n->ProxyEncryptedPassword = DupEncryptedPassword(ProxyEncryptedPassword, ProxyEncryptedPasswordSize);
        if (ProxyEncryptedPassword != NULL && ProxyEncryptedPasswordSize > 0 &&
            n->ProxyEncryptedPassword == NULL)
        {
            delete n;
            return NULL;
        }
        n->ProxyEncryptedPasswordSize = n->ProxyEncryptedPassword != NULL ? ProxyEncryptedPasswordSize : 0;
        if (!FtpStoreWideText(ProxyPlainPassword, n->ProxyPlainPassword))
        {
            delete n;
            return NULL;
        }
        n->SaveProxyPassword = SaveProxyPassword;
        if (!FtpStoreLocalTextBytes(ProxyScript, n->ProxyScript))
        {
            delete n;
            return NULL;
        }
    }
    catch (...)
    {
        delete n;
        n = NULL;
        TRACE_E(LOW_MEMORY);
    }
    return n;
}

CFTPProxyForDataCon*
CFTPProxyServer::AllocProxyForDataCon(DWORD proxyHostIP, const wchar_t* host,
                                      DWORD hostIP, unsigned short hostPort)
{
    CFTPProxyForDataCon* n = NULL;
    try
    {
        n = new CFTPProxyForDataCon(ProxyType, proxyHostIP, ProxyPort,
                                    ProxyUser.c_str(), ProxyPlainPassword.c_str(),
                                    host, hostIP, hostPort);
    }
    catch (...)
    {
        delete n;
        n = NULL;
    }
    if (n != NULL && !n->IsGood())
    {
        delete n;
        n = NULL;
    }
    if (n == NULL)
        TRACE_E(LOW_MEMORY);
    return n;
}

BOOL CFTPProxyServer::SetProxyHost(const wchar_t* proxyHost) noexcept
{
    return FtpStoreWideText(proxyHost != NULL ? proxyHost : L"", ProxyHost);
}

void CFTPProxyServer::SetProxyPort(int proxyPort)
{
    ProxyPort = proxyPort;
}

BOOL CFTPProxyServer::SetProxyPassword(const wchar_t* proxyPassword) noexcept
{
    std::wstring staged;
    if (!FtpStoreWideText(proxyPassword != NULL ? proxyPassword : L"", staged))
        return FALSE;
    ProxyPlainPassword.swap(staged);
    FTPSecureWipe(staged);
    return TRUE;
}

BOOL CFTPProxyServer::SetProxyUser(const wchar_t* proxyUser) noexcept
{
    return FtpStoreWideText(proxyUser != NULL ? proxyUser : L"", ProxyUser);
}

BOOL CFTPProxyServer::Set(int proxyUID,
                          const wchar_t* proxyName,
                          CFTPProxyServerType proxyType,
                          const wchar_t* proxyHost,
                          int proxyPort,
                          const wchar_t* proxyUser,
                          const BYTE* proxyEncryptedPassword,
                          int proxyEncryptedPasswordSize,
                          int saveProxyPassword,
                          const char* proxyScript) noexcept
{
    if (proxyEncryptedPassword != NULL && proxyEncryptedPasswordSize <= 0)
        return FALSE;
    std::wstring stagedName;
    std::wstring stagedHost;
    std::wstring stagedUser;
    std::string stagedScript;
    if (!FtpStoreWideText(proxyName != NULL ? proxyName : L"", stagedName) ||
        !FtpStoreWideText(proxyHost != NULL ? proxyHost : L"", stagedHost) ||
        !FtpStoreWideText(proxyUser != NULL ? proxyUser : L"", stagedUser) ||
        !FtpStoreLocalTextBytes(proxyScript != NULL ? proxyScript : "", stagedScript))
        return FALSE;
    BYTE* stagedEncryptedPassword = DupEncryptedPassword(proxyEncryptedPassword, proxyEncryptedPasswordSize);
    if (proxyEncryptedPassword != NULL && proxyEncryptedPasswordSize > 0 && stagedEncryptedPassword == NULL)
    {
        return FALSE;
    }
    BYTE* oldEncryptedPassword = ProxyEncryptedPassword;
    int oldEncryptedPasswordSize = ProxyEncryptedPasswordSize;
    ProxyUID = proxyUID;
    ProxyName.swap(stagedName);
    ProxyType = proxyType;
    ProxyHost.swap(stagedHost);
    ProxyPort = proxyPort;
    ProxyUser.swap(stagedUser);
    ProxyEncryptedPassword = stagedEncryptedPassword;
    ProxyEncryptedPasswordSize = stagedEncryptedPassword != NULL ? proxyEncryptedPasswordSize : 0;
    FTPSecureWipe(ProxyPlainPassword);
    SaveProxyPassword = saveProxyPassword;
    ProxyScript.swap(stagedScript);

    if (oldEncryptedPassword != NULL)
    {
        SecureZeroMemory(oldEncryptedPassword, oldEncryptedPasswordSize);
        SalamanderGeneral->Free(oldEncryptedPassword);
    }
    return TRUE;
}

BOOL CFTPProxyServer::Load(HWND parent, HKEY regKey, CSalamanderRegistryAbstract* registry)
{
    int proxyUID;
    std::wstring proxyName = ProxyName;
    DWORD proxyType;
    std::wstring proxyHost = ProxyHost;
    int proxyPort;
    std::wstring proxyUser = ProxyUser;
    BYTE* proxyEncryptedPassword;
    int proxyEncryptedPasswordSize;
    int saveProxyPassword;
    std::string proxyScript;

    // take over default values (the object is clean, just initialized)
    proxyType = ProxyType;
    proxyPort = ProxyPort;
    proxyEncryptedPassword = NULL;
    proxyEncryptedPasswordSize = 0;
    saveProxyPassword = FALSE;
    if (!FtpStoreLocalTextBytes(ProxyScript, proxyScript))
        return FALSE;

    if (!registry->GetValue(regKey, CONFIG_FTPPRXUID, REG_DWORD, &proxyUID, sizeof(DWORD)))
    {
        TRACE_E("Unexpected error in CFTPProxyServer::Load(): UID of proxy server was not found!");
        return FALSE; // UID is mandatory
    }
    if (!GetValueStringW(registry, regKey, CONFIG_FTPPRXNAME, proxyName) || proxyName.empty())
    {
        TRACE_E("Unexpected error in CFTPProxyServer::Load(): empty name of proxy server is not allowed!");
        return FALSE; // name is mandatory
    }
    registry->GetValue(regKey, CONFIG_FTPPRXTYPE, REG_DWORD, &proxyType, sizeof(DWORD));
    if (proxyType < 0 || proxyType > fpstOwnScript)
    {
        TRACE_E("Unexpected error in CFTPProxyServer::Load(): unknown type of proxy server!");
        return FALSE; // unknown proxy server type
    }
    GetValueStringW(registry, regKey, CONFIG_FTPPRXHOST, proxyHost);
    registry->GetValue(regKey, CONFIG_FTPPRXPORT, REG_DWORD, &proxyPort, sizeof(DWORD));
    GetValueStringW(registry, regKey, CONFIG_FTPPRXUSER, proxyUser);

    LoadPassword(regKey, registry, CONFIG_FTPPRXPASSWD_OLD, CONFIG_FTPPRXPASSWD_SCRAMBLED, CONFIG_FTPPRXPASSWD_ENCRYPTED,
                 &proxyEncryptedPassword, &proxyEncryptedPasswordSize);
    if (proxyEncryptedPassword != NULL && proxyEncryptedPasswordSize > 0)
    {
        saveProxyPassword = TRUE;

        // detect and, if needed, clear the "unnecessary" (due to storing "save password" TRUE) empty scrambled password
        CSalamanderPasswordManagerAbstract* passwordManager = SalamanderGeneral->GetSalamanderPasswordManager();
        if (!passwordManager->IsPasswordEncrypted(proxyEncryptedPassword, proxyEncryptedPasswordSize))
        {
            std::wstring plainPassword;
            if (FTPDecryptPasswordW(passwordManager, proxyEncryptedPassword,
                                    proxyEncryptedPasswordSize, &plainPassword))
            {
                if (plainPassword.empty())
                {
                    memset(proxyEncryptedPassword, 0, proxyEncryptedPasswordSize);
                    SalamanderGeneral->Free(proxyEncryptedPassword);
                    proxyEncryptedPassword = NULL;
                    proxyEncryptedPasswordSize = 0;
                }
                FTPSecureWipe(plainPassword);
            }
        }
    }

    std::string proxyScriptReg;
    if (GetValueSZ(registry, regKey, CONFIG_FTPPRXSCRIPT, proxyScriptReg))
    {
        if (!FtpDecodePersistedText(proxyScriptReg.c_str(), proxyScript))
        {
            if (proxyEncryptedPassword != NULL)
            {
                SecureZeroMemory(proxyEncryptedPassword, proxyEncryptedPasswordSize);
                SalamanderGeneral->Free(proxyEncryptedPassword);
            }
            TRACE_E(LOW_MEMORY);
            return FALSE;
        }
    }
    const char* errPos = NULL;
    std::string errorDescription;
    BOOL proxyHostNeeded = proxyType != fpstFTP_transparent;
    BOOL lowMemory = FALSE;
    if (proxyType == fpstOwnScript &&
        !ProcessProxyScript(FtpLocalTextCodec(), proxyScript.c_str(), &errPos, -1, NULL, NULL, NULL, NULL, NULL,
                            &errorDescription, &proxyHostNeeded, &lowMemory))
    {
        if (proxyEncryptedPassword != NULL)
        {
            SecureZeroMemory(proxyEncryptedPassword, proxyEncryptedPasswordSize);
            SalamanderGeneral->Free(proxyEncryptedPassword);
        }
        if (lowMemory)
            TRACE_E(LOW_MEMORY);
        else
            TRACE_E("Unexpected error in CFTPProxyServer::Load(): syntax error in proxy script! err-pos: " << (errPos != NULL ? errPos - proxyScript.c_str() : 0) << ", error: " << errorDescription.c_str());
        return FALSE;
    }
    if (proxyHostNeeded && proxyHost.empty())
    {
        if (proxyEncryptedPassword != NULL)
        {
            SecureZeroMemory(proxyEncryptedPassword, proxyEncryptedPasswordSize);
            SalamanderGeneral->Free(proxyEncryptedPassword);
        }
        TRACE_E("Unexpected error in CFTPProxyServer::Load(): ProxyHost is empty but it may not be!");
        return FALSE;
    }

    BOOL ret = Set(proxyUID,
                   proxyName.c_str(),
                   (CFTPProxyServerType)proxyType,
                   proxyHost.empty() ? NULL : proxyHost.c_str(),
                   proxyPort,
                   proxyUser.c_str(),
                   proxyEncryptedPassword, proxyEncryptedPasswordSize,
                   saveProxyPassword,
                   GetStrOrNULL(proxyScript.c_str()));
    if (proxyEncryptedPassword != NULL)
    {
        memset(proxyEncryptedPassword, 0, proxyEncryptedPasswordSize);
        SalamanderGeneral->Free(proxyEncryptedPassword);
    }
    return ret;
}

void CFTPProxyServer::Save(HWND parent, HKEY regKey, CSalamanderRegistryAbstract* registry,
                           const std::string& persistedProxyScript)
{
    registry->SetValue(regKey, CONFIG_FTPPRXUID, REG_DWORD, &ProxyUID, sizeof(DWORD));
    SetValueStringW(registry, regKey, CONFIG_FTPPRXNAME, ProxyName);
    if (ProxyType != fpstFTP_USER_user_host_colon_port)
    {
        DWORD dw = ProxyType;
        registry->SetValue(regKey, CONFIG_FTPPRXTYPE, REG_DWORD, &dw, sizeof(DWORD));
    }
    if (!ProxyHost.empty())
        SetValueStringW(registry, regKey, CONFIG_FTPPRXHOST, ProxyHost);
    if (ProxyPort != 21)
        registry->SetValue(regKey, CONFIG_FTPPRXPORT, REG_DWORD, &ProxyPort, sizeof(DWORD));
    if (!ProxyUser.empty())
        SetValueStringW(registry, regKey, CONFIG_FTPPRXUSER, ProxyUser);
    if (SaveProxyPassword) // "save password" is not stored (if a password is saved, it means "save password" is TRUE - so we must also store an empty password)
    {
        CSalamanderPasswordManagerAbstract* passwordManager = SalamanderGeneral->GetSalamanderPasswordManager();
        if (ProxyEncryptedPassword != NULL && ProxyEncryptedPasswordSize > 0)
        {
            BOOL encrypted = passwordManager->IsPasswordEncrypted(ProxyEncryptedPassword, ProxyEncryptedPasswordSize);
            registry->SetValue(regKey, encrypted ? CONFIG_FTPPRXPASSWD_ENCRYPTED : CONFIG_FTPPRXPASSWD_SCRAMBLED,
                               REG_BINARY, ProxyEncryptedPassword, ProxyEncryptedPasswordSize);
        }
        else // store an artificially created empty password only to keep "save password" TRUE
        {
            BYTE* scrambledPassword;
            int scrambledPasswordSize;
            if (FTPEncryptPasswordW(passwordManager, L"", &scrambledPassword,
                                    &scrambledPasswordSize, FALSE))
            {
                registry->SetValue(regKey, CONFIG_FTPPRXPASSWD_SCRAMBLED, REG_BINARY, scrambledPassword, scrambledPasswordSize);
                // free the buffer allocated in EncryptPassword()
                SalamanderGeneral->Free(scrambledPassword);
            }
        }
    }
    if (!persistedProxyScript.empty())
        SetValueSZ(registry, regKey, CONFIG_FTPPRXSCRIPT, persistedProxyScript.c_str());
}

//
// ****************************************************************************
// CFTPProxyServerList
//

CFTPProxyServerList::CFTPProxyServerList() : TIndirectArray<CFTPProxyServer>(5, 10)
{
    HANDLES(InitializeCriticalSection(&ProxyServerListCS));
    NextFreeProxyUID = 1;
}

CFTPProxyServerList::~CFTPProxyServerList()
{
    HANDLES(DeleteCriticalSection(&ProxyServerListCS));
}

BOOL CFTPProxyServerList::CopyMembersToList(CFTPProxyServerList& dstList)
{
    HANDLES(EnterCriticalSection(&ProxyServerListCS));
    dstList.DestroyMembers();
    dstList.NextFreeProxyUID = NextFreeProxyUID;
    BOOL ret = TRUE;
    int i;
    for (i = 0; i < Count; i++)
    {
        CFTPProxyServer* n = At(i)->MakeCopy();
        if (n != NULL)
        {
            dstList.Add(n);
            if (!dstList.IsGood())
            {
                dstList.ResetState();
                delete n;
                ret = FALSE;
                break;
            }
        }
        else
        {
            ret = FALSE; // error
            break;
        }
    }
    HANDLES(LeaveCriticalSection(&ProxyServerListCS));
    return ret;
}

void CFTPProxyServerList::Load(HWND parent, HKEY regKey, CSalamanderRegistryAbstract* registry)
{
    HANDLES(EnterCriticalSection(&ProxyServerListCS));
    HKEY actKey;
    if (registry->OpenKey(regKey, CONFIG_FTPPROXYLIST, actKey))
    {
        HKEY subKey;
        std::wstring keyName;
        int i = 0;
        DestroyMembers();
        NextFreeProxyUID = 1;
        while (FTPFormatDecimalIndex(keyName, ++i) &&
               registry->OpenKey(actKey, keyName.c_str(), subKey))
        {
            CFTPProxyServer* item = new CFTPProxyServer(0);
            if (item == NULL)
            {
                TRACE_E(LOW_MEMORY);
                break;
            }
            if (!item->Load(parent, subKey, registry)) // loading failed, skip this item
            {
                delete item;
            }
            else
            {
                BOOL notUnique = FALSE;
                if (item->ProxyUID < 0)
                {
                    TRACE_E("Unexpected situation in CFTPProxyServerList::Load(): proxy server UID < 0: " << item->ProxyUID);
                    notUnique = TRUE;
                }
                else
                {
                    int x;
                    for (x = 0; x < Count; x++)
                    {
                        CFTPProxyServer* proxy = At(x);
                        if (proxy->ProxyUID == item->ProxyUID ||
                            SalamanderGeneral->StrICmp(proxy->ProxyName.c_str(), item->ProxyName.c_str()) == 0)
                        {
                            TRACE_E("Unexpected situation in CFTPProxyServerList::Load(): not unique proxy server - skipping it...");
                            notUnique = TRUE;
                            break;
                        }
                    }
                }
                if (!notUnique)
                    Add(item);
                if (notUnique || !IsGood())
                {
                    if (!notUnique)
                        ResetState();
                    delete item;
                    break;
                }
                else
                { // addition succeeded, ensure NextFreeProxyUID is unique with respect to the newly added proxy server
                    if (NextFreeProxyUID <= item->ProxyUID)
                        NextFreeProxyUID = item->ProxyUID + 1;
                }
            }
            registry->CloseKey(subKey);
        }
        registry->CloseKey(actKey);
    }
    HANDLES(LeaveCriticalSection(&ProxyServerListCS));
}

void CFTPProxyServerList::Save(HWND parent, HKEY regKey, CSalamanderRegistryAbstract* registry)
{
    HANDLES(EnterCriticalSection(&ProxyServerListCS));
    std::vector<std::string> persistedProxyScripts;
    try
    {
        persistedProxyScripts.resize(Count);
    }
    catch (...)
    {
        TRACE_E(LOW_MEMORY);
        HANDLES(LeaveCriticalSection(&ProxyServerListCS));
        return;
    }
    for (int i = 0; i < Count; i++)
    {
        if (!At(i)->ProxyScript.empty() &&
            !FtpEncodePersistedText(At(i)->ProxyScript.c_str(), persistedProxyScripts[i]))
        {
            TRACE_E(LOW_MEMORY);
            HANDLES(LeaveCriticalSection(&ProxyServerListCS));
            return;
        }
    }
    HKEY actKey;
    if (registry->CreateKey(regKey, CONFIG_FTPPROXYLIST, actKey))
    {
        registry->ClearKey(actKey);
        HKEY subKey;
        std::wstring keyName;
        int i;
        for (i = 0; i < Count; i++)
        {
            if (FTPFormatDecimalIndex(keyName, i + 1) &&
                registry->CreateKey(actKey, keyName.c_str(), subKey))
            {
                At(i)->Save(parent, subKey, registry, persistedProxyScripts[i]);
                registry->CloseKey(subKey);
            }
            else
                break;
        }
        registry->CloseKey(actKey);
    }
    HANDLES(LeaveCriticalSection(&ProxyServerListCS));
}

void CFTPProxyServerList::InitCombo(HWND combo, int focusProxyUID, BOOL addDefault)
{
    std::wstring notUsed;
    std::wstring defaultProxy;
    try
    {
        notUsed = LangStr(IDS_PROXYSERVER_NOTUSED);
        if (addDefault)
            defaultProxy = LangStr(IDS_PROXYSERVER_DEFAULT);
    }
    catch (...)
    {
        TRACE_E(LOW_MEMORY);
        return;
    }
    HANDLES(EnterCriticalSection(&ProxyServerListCS));
    SendMessageW(combo, CB_RESETCONTENT, 0, 0);
    SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(notUsed.c_str()));
    if (addDefault)
        SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(defaultProxy.c_str()));
    int focusIndex = addDefault ? 1 : 0;
    if (focusProxyUID == -1 && addDefault)
        focusIndex = 0;
    int i;
    for (i = 0; i < Count; i++) // fill the combo-box list
    {
        CFTPProxyServer* proxy = At(i);
        SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)proxy->ProxyName.c_str());
        if (focusProxyUID >= 0 && proxy->ProxyUID == focusProxyUID)
            focusIndex = i + (addDefault ? 2 : 1);
    }
    SendMessageW(combo, CB_SETCURSEL, focusIndex, 0);
    HANDLES(LeaveCriticalSection(&ProxyServerListCS));
}

void CFTPProxyServerList::GetProxyUIDFromCombo(HWND combo, int& focusedProxyUID, BOOL addDefault)
{
    HANDLES(EnterCriticalSection(&ProxyServerListCS));
    int sel = (int)SendMessage(combo, CB_GETCURSEL, 0, 0);
    focusedProxyUID = addDefault ? -2 : -1;
    if (sel != CB_ERR)
    {
        int index = sel - (addDefault ? 2 : 1);
        if (index >= 0 && index < Count)
            focusedProxyUID = At(index)->ProxyUID;
        else
        {
            if (index == -2 && addDefault)
                focusedProxyUID = -1;
        }
    }
    HANDLES(LeaveCriticalSection(&ProxyServerListCS));
}

BOOL CFTPProxyServerList::IsValidUID(int proxyServerUID)
{
    HANDLES(EnterCriticalSection(&ProxyServerListCS));
    int i;
    for (i = 0; i < Count; i++) // fill the combo-box list
    {
        if (At(i)->ProxyUID == proxyServerUID)
            break;
    }
    BOOL found = i < Count;
    HANDLES(LeaveCriticalSection(&ProxyServerListCS));
    return found;
}

BOOL CFTPProxyServerList::IsProxyNameOK(CFTPProxyServer* proxyServer, const wchar_t* proxyName)
{
    BOOL ok = FALSE;
    if (proxyName[0] != 0)
    {
        HANDLES(EnterCriticalSection(&ProxyServerListCS));
        int i;
        for (i = 0; i < Count; i++) // check whether the name has already been used elsewhere
        {
            CFTPProxyServer* proxy = At(i);
            if (proxy != proxyServer && SalamanderGeneral->StrICmp(proxy->ProxyName.c_str(), proxyName) == 0)
                break;
        }
        ok = (i == Count);
        HANDLES(LeaveCriticalSection(&ProxyServerListCS));
    }
    return ok;
}

BOOL CFTPProxyServerList::SetProxyServer(CFTPProxyServer* proxyServer,
                                         int proxyUID,
                                         const wchar_t* proxyName,
                                         CFTPProxyServerType proxyType,
                                         const wchar_t* proxyHost,
                                         int proxyPort,
                                         const wchar_t* proxyUser,
                                         const BYTE* proxyEncryptedPassword,
                                         int proxyEncryptedPasswordSize,
                                         int saveProxyPassword,
                                         const char* proxyScript) noexcept
{
    HANDLES(EnterCriticalSection(&ProxyServerListCS));
    BOOL ret = proxyServer->Set(proxyUID,
                                proxyName,
                                proxyType,
                                proxyHost,
                                proxyPort,
                                proxyUser,
                                proxyEncryptedPassword,
                                proxyEncryptedPasswordSize,
                                saveProxyPassword,
                                proxyScript);
    HANDLES(LeaveCriticalSection(&ProxyServerListCS));
    return ret;
}

CFTPProxyServer*
CFTPProxyServerList::MakeCopyOfProxyServer(int proxyServerUID, BOOL* lowMem) noexcept
{
    if (lowMem != NULL)
        *lowMem = FALSE;
    HANDLES(EnterCriticalSection(&ProxyServerListCS));
    CFTPProxyServer* ret = NULL;
    int i;
    for (i = 0; i < Count; i++)
    {
        if (At(i)->ProxyUID == proxyServerUID)
        {
            ret = At(i)->MakeCopy();
            if (ret == NULL && lowMem != NULL)
                *lowMem = TRUE;
            break;
        }
    }
    HANDLES(LeaveCriticalSection(&ProxyServerListCS));
    return ret;
}

void CFTPProxyServerList::AddProxyServer(HWND parent, HWND combo)
{
    CFTPProxyServer* n = new CFTPProxyServer(0);
    if (n != NULL)
    {
        // the value change happens in CProxyServerDlg::Transfer(), which calls SetProxyServer() on this object
        if (CProxyServerDlg(parent, this, n, FALSE).Execute() == IDOK)
        {
            HANDLES(EnterCriticalSection(&ProxyServerListCS));
            Add(n);
            if (IsGood())
            {
                n->ProxyUID = NextFreeProxyUID++; // initialization of ProxyUID
                // add to the combo box and focus the added item
                SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)n->ProxyName.c_str());
                int count = (int)SendMessage(combo, CB_GETCOUNT, 0, 0);
                if (count != CB_ERR && count > 0)
                    PostMessage(combo, CB_SETCURSEL, count - 1, 0);
            }
            else
            {
                ResetState();
                delete n;
            }
            HANDLES(LeaveCriticalSection(&ProxyServerListCS));
        }
        else
            delete n;
    }
    else
        TRACE_E(LOW_MEMORY);
}

void CFTPProxyServerList::EditProxyServer(HWND parent, HWND combo, BOOL addDefault)
{
    int sel = (int)SendMessage(combo, CB_GETCURSEL, 0, 0);
    if (sel != CB_ERR)
    {
        int index = sel - (addDefault ? 2 : 1);
        if (index >= 0 && index < Count) // reading values in the main thread does not need a critical section
        {
            CFTPProxyServer* e = At(index);
            // the value change happens in CProxyServerDlg::Transfer(), which calls SetProxyServer() on this object
            BOOL edited = (CProxyServerDlg(parent, this, e, TRUE).Execute() == IDOK);
            if (edited)
            { // just refresh the combo box
                if (SendMessageW(combo, CB_INSERTSTRING, sel, (LPARAM)e->ProxyName.c_str()) == sel)
                {
                    SendMessage(combo, CB_DELETESTRING, sel + 1, 0);
                    SendMessage(combo, CB_SETCURSEL, sel, 0);
                }
            }
        }
    }
}

void CFTPProxyServerList::DeleteProxyServer(HWND parent, HWND combo, BOOL addDefault)
{
    int sel = (int)SendMessage(combo, CB_GETCURSEL, 0, 0);
    if (sel != CB_ERR)
    {
        int index = sel - (addDefault ? 2 : 1);
        if (index >= 0 && index < Count) // reading values in the main thread does not need a critical section
        {
            std::wstring prompt;
            try
            {
                prompt = SPLFormatStringOwned(LangStr(IDS_WANTDELPRXSRV).c_str(),
                                              At(index)->ProxyName.c_str());
            }
            catch (...)
            {
                TRACE_E(LOW_MEMORY);
                return;
            }
            BOOL del = (SalamanderGeneral->SalMessageBox(parent, prompt.c_str(),
                                                         SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_FTPPLUGINTITLE).c_str(),
                                                         MB_YESNO | MSGBOXEX_ESCAPEENABLED |
                                                             MB_ICONQUESTION) == IDYES);
            if (del && index >= 0 && index < Count)
            {
                SendMessage(combo, CB_DELETESTRING, sel, 0);
                int count = (int)SendMessage(combo, CB_GETCOUNT, 0, 0);
                if (count != CB_ERR)
                {
                    if (sel < count)
                        SendMessage(combo, CB_SETCURSEL, sel, 0);
                    else
                        SendMessage(combo, CB_SETCURSEL, count - 1, 0);
                }
                HANDLES(EnterCriticalSection(&ProxyServerListCS));
                Delete(index);
                HANDLES(LeaveCriticalSection(&ProxyServerListCS));
            }
        }
    }
}

void CFTPProxyServerList::MoveUpProxyServer(HWND combo, BOOL addDefault)
{
    int sel = (int)SendMessage(combo, CB_GETCURSEL, 0, 0);
    if (sel != CB_ERR)
    {
        int index = sel - (addDefault ? 2 : 1);
        if (index > 0 && index < Count) // reading values in the main thread does not need a critical section
        {
            if (SendMessageW(combo, CB_INSERTSTRING, sel - 1, (LPARAM)At(index)->ProxyName.c_str()) == sel - 1)
            {
                SendMessage(combo, CB_DELETESTRING, sel + 1, 0);
                SendMessage(combo, CB_SETCURSEL, sel - 1, 0);
                HANDLES(EnterCriticalSection(&ProxyServerListCS));
                CFTPProxyServer* swap = At(index - 1);
                At(index - 1) = At(index);
                At(index) = swap;
                HANDLES(LeaveCriticalSection(&ProxyServerListCS));
            }
        }
    }
}

void CFTPProxyServerList::MoveDownProxyServer(HWND combo, BOOL addDefault)
{
    int sel = (int)SendMessage(combo, CB_GETCURSEL, 0, 0);
    int count = (int)SendMessage(combo, CB_GETCOUNT, 0, 0);
    if (sel != CB_ERR && count != CB_ERR)
    {
        int index = sel - (addDefault ? 2 : 1);
        if (index >= 0 && index + 1 < Count) // reading values in the main thread does not need a critical section
        {
            if (SendMessageW(combo, CB_INSERTSTRING, sel, (LPARAM)At(index + 1)->ProxyName.c_str()) == sel)
            {
                SendMessage(combo, CB_DELETESTRING, sel + 2, 0);
                SendMessage(combo, CB_SETCURSEL, sel + 1, 0);
                HANDLES(EnterCriticalSection(&ProxyServerListCS));
                CFTPProxyServer* swap = At(index);
                At(index) = At(index + 1);
                At(index + 1) = swap;
                HANDLES(LeaveCriticalSection(&ProxyServerListCS));
            }
        }
    }
}

BOOL CFTPProxyServerList::GetProxyName(std::wstring& name, int proxyServerUID)
{
    try
    {
        std::wstring staged;
        if (proxyServerUID == -1)
            staged = LangStr(IDS_ADVSTRPROXYNOTUSED);
        else
        {
            int i;
            for (i = 0; i < Count; i++) // reading values in the main thread does not need a critical section
            {
                CFTPProxyServer* s = At(i);
                if (s->ProxyUID == proxyServerUID)
                {
                    if (!FtpStoreWideText(s->ProxyName, staged))
                        return FALSE;
                    break;
                }
            }
            if (i == Count)
                return FALSE;
        }
        name.swap(staged);
        return TRUE;
    }
    catch (...)
    {
        return FALSE;
    }
}

CFTPProxyServerType
CFTPProxyServerList::GetProxyType(int proxyServerUID)
{
    int i;
    for (i = 0; i < Count; i++) // reading values in the main thread does not need a critical section
    {
        CFTPProxyServer* s = At(i);
        if (s->ProxyUID == proxyServerUID)
            return s->ProxyType;
    }
    return fpstNotUsed;
}

BOOL CFTPProxyServerList::ContainsUnsecuredPassword()
{
    // NOTE, the same method exists for CFTPServerList
    CSalamanderPasswordManagerAbstract* passwordManager = SalamanderGeneral->GetSalamanderPasswordManager();
    int i;
    for (i = 0; i < Count; i++) // reading values in the main thread does not need a critical section
    {
        CFTPProxyServer* s = At(i);
        if (s->SaveProxyPassword && s->ProxyEncryptedPassword != NULL)
        {
            if (!passwordManager->IsPasswordEncrypted(s->ProxyEncryptedPassword, s->ProxyEncryptedPasswordSize))
                return TRUE;
        }
    }
    return FALSE;
}

BOOL CFTPProxyServerList::EncryptPasswords(HWND hParent, BOOL encrypt)
{
    // NOTE, the same method exists for CFTPServerList
    HANDLES(EnterCriticalSection(&ProxyServerListCS));
    BOOL ret = TRUE;
    int i;
    for (i = 0; i < Count; i++)
    {
        CFTPProxyServer* s = At(i);
        ret &= EncryptPasswordAux(&s->ProxyEncryptedPassword, &s->ProxyEncryptedPasswordSize, s->SaveProxyPassword, encrypt);
    }
    HANDLES(LeaveCriticalSection(&ProxyServerListCS));
    return ret;
}

BOOL CFTPProxyServerList::EnsurePasswordCanBeDecrypted(HWND hParent, int proxyServerUID)
{
    if (proxyServerUID == -1 /* not used */)
        return TRUE; // proxy server is not used, nothing to verify

    CFTPProxyServer* s = NULL;
    int i;
    for (i = 0; i < Count; i++) // reading values in the main thread does not need a critical section
    {
        if (At(i)->ProxyUID == proxyServerUID)
        {
            s = At(i);
            break;
        }
    }

    if (s == NULL)
    {
        TRACE_E("CFTPProxyServerList::EnsurePasswordCanBeDecrypted(): cannot find proxy server with proxyServerUID " << proxyServerUID);
        return FALSE; // invalid proxy server
    }

    CSalamanderPasswordManagerAbstract* passwordManager = SalamanderGeneral->GetSalamanderPasswordManager();
    if (s->ProxyEncryptedPassword != NULL &&
        passwordManager->IsPasswordEncrypted(s->ProxyEncryptedPassword, s->ProxyEncryptedPasswordSize))
    {
        // verify whether entering the master password is necessary to decrypt the password
        if (passwordManager->IsUsingMasterPassword() && !passwordManager->IsMasterPasswordSet())
        {
            if (!passwordManager->AskForMasterPassword(hParent))
                return FALSE; // the user did not enter the correct master password
        }
        // verify that this is the correct master password for this password
        if (!FTPDecryptPasswordW(passwordManager, s->ProxyEncryptedPassword,
                                 s->ProxyEncryptedPasswordSize, NULL))
        {
            int ret = SalamanderGeneral->SalMessageBox(hParent, SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_CANNOT_DECRYPT_PASSWORD_DELETE).c_str(),
                                                       SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_FTPERRORTITLE).c_str(), MB_YESNO | MSGBOXEX_ESCAPEENABLED | MB_DEFBUTTON2 | MB_ICONEXCLAMATION);
            if (ret == IDNO)
                return FALSE; // failed to decrypt the password

            // the user wanted to delete the password
            HANDLES(EnterCriticalSection(&ProxyServerListCS));
            UpdateEncryptedPassword(&s->ProxyEncryptedPassword, &s->ProxyEncryptedPasswordSize, NULL, 0);
            // clear the save password checkbox
            s->SaveProxyPassword = FALSE;
            HANDLES(LeaveCriticalSection(&ProxyServerListCS));
        }
    }
    return TRUE;
}
