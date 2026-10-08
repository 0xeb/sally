// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"
//#include <windows.h>
#include <crtdbg.h>
#include <stdio.h>

#pragma warning(3 : 4706) // warning C4706: assignment within conditional expression

#include "selfextr\\comdefs.h"
#include "typecons.h"
#include "sfxmake\\sfxmake.h"
#include "chicon.h"
#include "crc32.h"
#include "iosfxset.h"
#include "zip2sfx.h"
#include "inflate.h"

#include "zip2sfx.rh"

// The SFX header and target-directory specification are code-page bytes.
char* StrNChr(const char* lpStart, int nChar, char wMatch)
{
    if (lpStart == NULL)
        return NULL;
    int i = lstrlenA(lpStart);
    if (i > nChar)
        i = nChar;
    const char* lpEnd = lpStart + i; // never past the terminator
    while (lpStart < lpEnd)
    {
        if (*lpStart == wMatch)
            return (char*)lpStart;
        lpStart++;
    }
    return NULL;
}

char* StrRChr(const char* lpStart, const char* lpEnd, char wMatch)
{
    lpEnd--;
    while (lpEnd >= lpStart)
    {
        if (*lpEnd == wMatch)
            return (char*)lpEnd;
        lpEnd--;
    }
    return NULL;
}

void* LoadRCData(int id, DWORD& size)
{
    HINSTANCE module = GetModuleHandle(NULL);
    HRSRC hRsrc = FindResource(module, MAKEINTRESOURCE(id), RT_RCDATA);
    if (hRsrc)
    {
        void* data = LoadResource(module, hRsrc);
        if (data)
        {
            size = SizeofResource(module, hRsrc);
            return data;
        }
    }
    return NULL;
}

BOOL WriteSFXHeader()
{
    CSelfExtrHeader header;
    int offs = sizeof(CSelfExtrHeader);

    int l = lstrlenA(Settings.Command);
    if (l)
    {
        header.CommandOffs = offs;
        offs += ++l;
    }
    else
        header.CommandOffs = 0;
    header.TextOffs = offs;
    l = lstrlenA(Settings.Text);
    offs += ++l;
    header.TitleOffs = offs;
    l = lstrlenA(Settings.Title);
    offs += ++l;
    header.SubDirOffs = offs;

    unsigned td;
    const char* sd;
    const char* sdl;
    const char* sdr;
    HKEY key;
    // no need to test the return value; it was verified earlier
    ParseTargetDir(Settings.TargetDir, &td, &sd, &sdl, &sdr, &key);
    l = lstrlenA(sd);
    offs += ++l;
    header.AboutOffs = offs;
    l = lstrlenA(About);
    offs += ++l;
    header.ExtractBtnTextOffs = offs;
    l = lstrlenA(Settings.ExtractBtnText);
    offs += ++l;
    header.VendorOffs = offs;
    l = lstrlenA(Settings.Vendor);
    offs += ++l;
    header.WWWOffs = offs;
    l = lstrlenA(Settings.WWW);
    offs += ++l;
    header.ArchiveNameOffs = offs;
    //l = lstrlenA(archName);
    //ArchiveDataOffs += l; // we accounted for this earlier
    //offs += ++l;
    offs++;
    header.TargetDirSpecOffs = offs;
    offs += (sdr - sdl) + 1;
    if (td == SE_REGVALUE)
    {
        offs += sizeof(LONG); // prepend the HKEY value; even the 64-bit build stores only a 32-bit key for known roots (e.g. HKEY_CURRENT_USER)
        const char* bs = StrNChr(sdl, sdr - sdl, '\\');
        if (!bs)
            offs += 1; // add a separator character between the subkey and value
    }
    header.MBoxStyle = (lstrlenA(Settings.MBoxTitle) ||
                        !Settings.MBoxText.empty())
                           ? Settings.MBoxStyle
                           : -1;
    header.MBoxTitleOffs = offs;
    l = lstrlenA(Settings.MBoxTitle);
    offs += ++l;
    header.MBoxTextOffs = offs;
    if (!Settings.MBoxText.empty())
    {
        l = lstrlenA(Settings.MBoxText.c_str());
        offs += ++l;
    }
    else
        offs++;
    header.WaitForOffs = offs;
    l = lstrlenA(Settings.WaitFor);
    offs += Settings.Flags & SE_REMOVEAFTER ? ++l : 1;

    header.Signature = SELFEXTR_SIG;
    header.HeaderSize = offs;
    header.Flags = Settings.Flags;
    header.Flags = Settings.Flags | td;
    if (td == SE_TEMPDIREX)
        header.Flags |= SE_TEMPDIR;
    header.EOCentrDirOffs = EOCentrDirOffs;
    header.ArchiveSize = ArcSize;

    if (!Write(ExeFile, &header, sizeof(CSelfExtrHeader)))
        return FALSE;
    l = lstrlenA(Settings.Command);
    if (l &&
        !Write(ExeFile, Settings.Command, ++l))
        return FALSE;
    if (!Write(ExeFile, Settings.Text, lstrlenA(Settings.Text) + 1))
        return FALSE;
    if (!Write(ExeFile, Settings.Title, lstrlenA(Settings.Title) + 1))
        return FALSE;
    if (!Write(ExeFile, (void*)sd, lstrlenA(sd) + 1))
        return FALSE;
    if (!Write(ExeFile, About, lstrlenA(About) + 1))
        return FALSE;
    if (!Write(ExeFile, Settings.ExtractBtnText, lstrlenA(Settings.ExtractBtnText) + 1))
        return FALSE;
    if (!Write(ExeFile, Settings.Vendor, lstrlenA(Settings.Vendor) + 1))
        return FALSE;
    if (!Write(ExeFile, Settings.WWW, lstrlenA(Settings.WWW) + 1))
        return FALSE;
    if (!Write(ExeFile, "", 1))
        return FALSE;
    if (td == SE_REGVALUE)
    {
        // key - even the 64-bit build stores only a 32-bit key; known roots (e.g. HKEY_CURRENT_USER) are defined as 32-bit IDs
        LONG lkey = (LONG)key;
        if (!Write(ExeFile, &lkey, sizeof(lkey)))
            return FALSE;

        //subkey
        const char* bs = StrRChr(sdl, sdr, '\\');
        if (bs)
        {
            if (!Write(ExeFile, (void*)sdl, bs - sdl))
                return FALSE;
        }
        if (!Write(ExeFile, "", 1))
            return FALSE;

        //value
        if (bs)
        {
            bs++;
            if (!Write(ExeFile, (void*)bs, sdr - bs))
                return FALSE;
        }
        else
        {
            if (!Write(ExeFile, (void*)sdl, sdr - sdl))
                return FALSE;
        }
        if (!Write(ExeFile, "", 1))
            return FALSE;
    }
    else
    {
        if (!Write(ExeFile, (void*)sdl, sdr - sdl))
            return FALSE;
        if (!Write(ExeFile, "", 1))
            return FALSE;
    }
    if (!Write(ExeFile, Settings.MBoxTitle, lstrlenA(Settings.MBoxTitle) + 1))
        return FALSE;
    const char* str = Settings.MBoxText.c_str();
    if (!Write(ExeFile, str, lstrlenA(str) + 1))
        return FALSE;
    str = Settings.Flags & SE_REMOVEAFTER ? Settings.WaitFor : "";
    if (!Write(ExeFile, str, lstrlenA(str) + 1))
        return FALSE;
    return TRUE;
}

BOOL WriteSfxExecutable()
{
    char* buffer;
    unsigned size;
    DWORD offset;
    CSfxFileHeader sfxHead;
    wchar_t langName[128];

    printf(StringTable[STR_WRITINGEXE]);
    SfxPackage = CreateFileW(SfxPackageName.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL,
                             OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN,
                             NULL);
    if (SfxPackage == INVALID_HANDLE_VALUE)
        return ErrorPath(STR_ERROPEN, SfxPackageName.c_str());

    if (!Read(SfxPackage, &sfxHead, sizeof(CSfxFileHeader)))
        return ErrorPath(STR_ERRREAD, SfxPackageName.c_str());
    if (sfxHead.Signature != SFX_SIGNATURE)
        return ErrorPath(STR_CORRUPTSFX, SfxPackageName.c_str());
    if (sfxHead.CompatibleVersion != SFX_SUPPORTEDVERSION)
        return ErrorPath(STR_BADSFXVER, SfxPackageName.c_str());
    if (sfxHead.HeaderCRC != UpdateCrc((__UINT8*)&sfxHead, sizeof(CSfxFileHeader) - sizeof(DWORD), INIT_CRC, CrcTab))
        return ErrorPath(STR_CORRUPTSFX, SfxPackageName.c_str());

    if (GetLocaleInfoW(
            MAKELCID(MAKELANGID(sfxHead.LangID, SUBLANG_NEUTRAL), SORT_DEFAULT),
            LOCALE_SLANGUAGE, langName, _countof(langName)))
    {
        wchar_t* c = wcschr(langName, L' ');
        if (c)
            *c = 0;
    }
    else
        langName[0] = 0;
    PrintWideText(L"Used sfx package: " + SfxPackageName + L"\nLanguage: " + langName + L"\n");

    //copy executable
    BOOL bigSfx = Encrypt || Settings.Flags & SE_REMOVEAFTER && Settings.WaitFor;
    if (bigSfx)
    {
        size = sfxHead.BigSfxSize;
        offset = sfxHead.BigSfxOffset;
    }
    else
    {
        size = sfxHead.SmallSfxSize;
        offset = sfxHead.SmallSfxOffset;
    }
    buffer = (char*)malloc(size);
    if (!buffer)
        return Error(STR_LOWMEM);
    BOOL success = FALSE;
    LONG dummy = 0;
    if (SetFilePointer(SfxPackage, offset, &dummy, FILE_BEGIN) == 0xFFFFFFFF)
        ErrorPath(STR_ERRACCESS, SfxPackageName.c_str());
    else
    {
        if (Read(SfxPackage, buffer, size))
        {
            InPtr = (unsigned char*)buffer;
            InEnd = InPtr + size;
            InflatingTexts = FALSE;

            switch (Inflate())
            {
            case 0:
            {
                if (Crc == (bigSfx ? sfxHead.BigSfxCRC : sfxHead.SmallSfxCRC))
                    success = TRUE;
                else
                    ErrorPath(STR_CORRUPTSFX, SfxPackageName.c_str());
                break;
            }
            case 4:
            case 1:
            case 2:
                ErrorPath(STR_CORRUPTSFX, SfxPackageName.c_str());
                break;
            case 3:
                Error(STR_LOWMEM);
                break;
            case 5:
                break;
            }
        }
        else
            ErrorPath(STR_ERRREAD, SfxPackageName.c_str());
    }
    free(buffer);
    if (!success)
        return FALSE;
    CloseHandle(ExeFile);
    ExeFile = INVALID_HANDLE_VALUE;

    // load appropriate manifest
    DWORD manifestSize;
    void* manifest = LoadRCData((Settings.Flags & SE_REQUIRESADMIN)
                                    ? ID_SFX_MANIFEST_ADMIN
                                    : ID_SFX_MANIFEST_REGULAR,
                                manifestSize);
    if (!manifest)
        return Error(STR_ERRMANIFEST); // this should never happen

    //change icon
    if (!ChangeSfxIconAndAddManifest(ExeName.c_str(), Icons, IconsCount, manifest,
                                     manifestSize))
        return Error(STR_ERRICON);

    ExeFile = CreateFileW(ExeName.c_str(), GENERIC_WRITE, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                          FILE_ATTRIBUTE_NORMAL, NULL);
    if (ExeFile == INVALID_HANDLE_VALUE)
        return ErrorPath(STR_ERROPEN, ExeName.c_str());

    dummy = 0;
    if (SetFilePointer(ExeFile, 0, &dummy, FILE_END) == 0xFFFFFFFF)
        return ErrorPath(STR_ERRACCESS, ExeName.c_str());

    if (!WriteSFXHeader())
        return ErrorPath(STR_ERRWRITE, ExeName.c_str());

    return TRUE;
}

BOOL AppendArchive()
{
    printf(StringTable[STR_WRITINGARC]);
    if (SetFilePointer(ZipFile, 0, NULL, FILE_BEGIN) == 0xFFFFFFFF)
        return ErrorPath(STR_ERRACCESS, ZipName.c_str());
    DWORD left = ArcSize;
    DWORD toRead;
    while (left)
    {
        toRead = left > 0xFFFF ? 0xFFFF : left;
        if (!Read(ZipFile, IOBuffer, toRead))
            return ErrorPath(STR_ERRREAD, ZipName.c_str());
        if (!Write(ExeFile, IOBuffer, toRead))
            return ErrorPath(STR_ERRWRITE, ExeName.c_str());
        left -= toRead;
    }
    return TRUE;
}
