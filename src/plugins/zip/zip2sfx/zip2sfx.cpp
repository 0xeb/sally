// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"
//#include <windows.h>
#include <crtdbg.h>
#include <stdio.h>
#include <conio.h>

#pragma warning(3 : 4706) // warning C4706: assignment within conditional expression

#include "selfextr\\comdefs.h"
#include "typecons.h"
#include "sfxmake\\sfxmake.h"
#include "chicon.h"
#include "crc32.h"
#include "iosfxset.h"
#include "checkzip.h"
#include "zip2sfx.h"
#include "inflate.h"
#include "common/Win32TextCodec.h"

#include "zip2sfx.rh"

#define STRING(code, string) string,
const char* const StringTable[] =
    {
#include "texts.h"
        NULL};

#undef STRING

std::wstring ZipName; // archive path
HANDLE ZipFile = INVALID_HANDLE_VALUE;
DWORD ArcSize;
DWORD EOCentrDirOffs;
BOOL Encrypt = FALSE;

std::wstring ExeName;
HANDLE ExeFile = INVALID_HANDLE_VALUE;

wchar_t ParamKind = L'\0';
std::wstring ParamPath;
std::wstring SfxPackageName;

HANDLE SettingsFile = INVALID_HANDLE_VALUE; // optional settings file
char* SettingsTextData;

HANDLE SfxPackage = INVALID_HANDLE_VALUE; // sfx package

CSfxSettings Settings; // sfx options
char About[SE_MAX_ABOUT];
char DefVendor[SE_MAX_VENDOR];
char DefWWW[SE_MAX_WWW];
char DefAbout[SE_MAX_ABOUT];
CIcon* Icons = NULL;
int IconsCount;

char* IOBuffer;
__UINT32* CrcTab = NULL;
BOOL OvewriteExe = FALSE;

BOOL InflatingTexts;

BOOL Error(int error, ...)
{
    int lastErr = GetLastError();
    va_list arglist;
    va_start(arglist, error);
    vprintf(StringTable[error], arglist);
    va_end(arglist);
    if (lastErr != ERROR_SUCCESS)
    {
        char buf[1024]; //temp variable
        *buf = 0;
        FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL, lastErr,
                      MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), buf, 1024, NULL);
        printf("%s", buf);
    }

    return FALSE;
}

void PrintWideText(const std::wstring& text)
{
    HANDLE output = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode = 0;
    if (output != INVALID_HANDLE_VALUE && GetConsoleMode(output, &mode))
    {
        DWORD written = 0;
        WriteConsoleW(output, text.c_str(), static_cast<DWORD>(text.size()), &written, NULL);
        return;
    }

    std::string bytes;
    if (!Win32EncodeText(CP_UTF8, text, bytes) ||
        bytes.size() > static_cast<size_t>(MAXDWORD))
        return;
    fflush(stdout);
    DWORD written = 0;
    WriteFile(output, bytes.data(), static_cast<DWORD>(bytes.size()), &written, NULL);
}

BOOL ErrorPath(int error, const wchar_t* path)
{
    const DWORD lastErr = GetLastError();
    const char* source = StringTable[error];
    std::wstring message;
    while (*source != '\0')
        message.push_back(static_cast<unsigned char>(*source++));
    const size_t marker = message.find(L"%s");
    if (marker != std::wstring::npos)
        message.replace(marker, 2, path != NULL ? path : L"");
    PrintWideText(message);

    if (lastErr != ERROR_SUCCESS)
    {
        wchar_t* systemText = NULL;
        if (FormatMessageW(
                FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                    FORMAT_MESSAGE_IGNORE_INSERTS,
                NULL, lastErr, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                reinterpret_cast<wchar_t*>(&systemText), 0, NULL) != 0)
        {
            PrintWideText(systemText);
            LocalFree(systemText);
        }
    }
    return FALSE;
}

BOOL Read(HANDLE file, void* buffer, DWORD size)
{
    DWORD read;
    if (!ReadFile(file, buffer, size, &read, NULL))
        return FALSE;
    if (read != size)
        return Error(STR_EOF);
    return TRUE;
}

BOOL Write(HANDLE file, const void* buffer, DWORD size)
{
    DWORD written;
    if (!WriteFile(file, buffer, size, &written, NULL))
        return FALSE;
    return TRUE;
}

void PathRenameExtension(std::wstring& path, const wchar_t* extension)
{
    const size_t ext = path.find_last_of(L'.');
    if (ext != std::wstring::npos)
        path.resize(ext); // ".cvspass" is treated as an extension in Windows
    path += extension;
}

BOOL ProcessCommandline(int argc, wchar_t* argv[])
{
    BOOL b = FALSE;
    int i;
    for (i = 1; i < argc; i++)
    {
        if (argv[i][0] == L'-')
        {
            switch (argv[i][1])
            {
            case L'p':
            case L's':
                if (!b)
                {
                    b = TRUE;
                    ParamKind = argv[i][1];
                    ParamPath = argv[i] + 2;
                }
                else
                    return FALSE;
                break;

            case L'o':
                OvewriteExe = TRUE;
                break;
            default:
                return FALSE;
            }
        }
        else
            break;
    }
    if (i >= argc)
        return FALSE;
    ZipName = argv[i];
    const size_t fileOffset = ZipName.find_last_of(L"\\/");
    ExeName = fileOffset == std::wstring::npos ? ZipName : ZipName.substr(fileOffset + 1);
    PathRenameExtension(ExeName, L".exe");
    i++;
    if (i >= argc)
        return TRUE;
    ExeName = argv[i];
    if (i + 1 < argc)
        return FALSE;
    return TRUE;
}

DWORD SalGetFileAttributes(const wchar_t* fileName)
{
    std::wstring fileNameCopy(fileName != NULL ? fileName : L"");
    // if the path ends with a trailing space or dot we must append '\\'; otherwise
    // GetFileAttributes trims that character and works with a different path.
    // The workaround still beats returning attributes of another file or directory
    // (for "c:\\file.txt   " it ends up working with "c:\\file.txt").
    if (!fileNameCopy.empty() &&
        (fileNameCopy.back() <= L' ' || fileNameCopy.back() == L'.'))
    {
        fileNameCopy.push_back(L'\\');
        return GetFileAttributesW(fileNameCopy.c_str());
    }
    else // ordinary path, nothing special here, just call the Windows GetFileAttributes
    {
        return GetFileAttributesW(fileNameCopy.c_str());
    }
}

static BOOL GetModuleFileNameOwned(std::wstring& fileName)
{
    std::vector<wchar_t> buffer(256, L'\0');
    for (;;)
    {
        const DWORD length = GetModuleFileNameW(NULL, buffer.data(),
                                                static_cast<DWORD>(buffer.size()));
        if (length == 0)
            return FALSE;
        if (length < buffer.size() - 1)
        {
            fileName.assign(buffer.data(), length);
            return TRUE;
        }
        if (buffer.size() > (std::numeric_limits<DWORD>::max)() / 2)
            return FALSE;
        buffer.resize(buffer.size() * 2);
    }
}

BOOL GetZip2SfxDir(std::string& zip2sfxDir)
{
    std::wstring directory;
    if (!GetModuleFileNameOwned(directory))
        return FALSE;
    const size_t slash = directory.find_last_of(L"\\/");
    if (slash == std::wstring::npos)
        return FALSE;
    directory.resize(slash + 1);
    return WideToLegacyTextExact(directory.c_str(), zip2sfxDir);
}

static BOOL GetEnvironmentVariableOwned(const wchar_t* name, std::wstring& value)
{
    SetLastError(ERROR_SUCCESS);
    const DWORD required = GetEnvironmentVariableW(name, NULL, 0);
    if (required == 0)
    {
        value.clear();
        return GetLastError() == ERROR_ENVVAR_NOT_FOUND;
    }
    std::vector<wchar_t> buffer(required, L'\0');
    const DWORD length = GetEnvironmentVariableW(name, buffer.data(), required);
    if (length == 0 || length >= required)
        return FALSE;
    value.assign(buffer.data(), length);
    return TRUE;
}

BOOL LoadSettings()
{
    HANDLE file = CreateFileW(ParamPath.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN,
                              NULL);
    if (file == INVALID_HANDLE_VALUE)
        return ErrorPath(STR_ERROPEN, ParamPath.c_str());

    BOOL ret = TRUE;
    DWORD fsiz = GetFileSize(file, NULL);
    if (fsiz != 0xFFFFFFFF)
    {
        SettingsTextData = (char*)malloc(fsiz + 1);
        if (SettingsTextData)
        {
            if (Read(file, SettingsTextData, fsiz))
            {
                std::string zip2sfxDir;
                if (!GetZip2SfxDir(zip2sfxDir))
                {
                    CloseHandle(file);
                    return Error(STR_BADSETFORMAT);
                }

                SettingsTextData[fsiz] = 0;
                int err;
                switch (ImportSFXSettings(SettingsTextData, &Settings, zip2sfxDir.c_str()))
                {
                case 0:
                    err = 0;
                    break; //OK
                case 1:
                    err = STR_BADTEMP;
                    break;
                case 2:
                    err = STR_MISBAR;
                    break;
                case 3:
                    err = STR_BADVAR;
                    break;
                case 4:
                    err = STR_BADKEY;
                    break;
                case 5:
                    err = STR_MISSINGVERSION;
                    break;
                case 6:
                    err = STR_BADVERSION;
                    break;
                case 8:
                    err = STR_BADMSGBOXTYPE;
                    break;
                case 7:
                default:
                    err = STR_BADSETFORMAT;
                    break;
                }
                if (err)
                    ret = Error(err);
            }
            else
                ret = ErrorPath(STR_ERRREAD, ParamPath.c_str());
        }
        else
            ret = Error(STR_LOWMEM);
    }
    else
        ret = ErrorPath(STR_ERRACCESS, ParamPath.c_str());

    CloseHandle(file);

    return ret;
}

BOOL LoadDefaults()
{
    CSfxFileHeader sfxHead;
    Settings.Flags = SE_SHOWSUMARY;
    *Settings.Command = 0;
    *Settings.TargetDir = 0;
    Settings.MBoxStyle = MB_OK;
    Settings.SetMBoxText("");
    *Settings.MBoxTitle = 0;
    *Settings.WaitFor = 0;
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

    if (sfxHead.TotalTextSize > 0xFFFF)
        return ErrorPath(STR_LARGETEXT, SfxPackageName.c_str());
    if (!Read(SfxPackage, IOBuffer, sfxHead.TotalTextSize))
        return ErrorPath(STR_ERRACCESS, SfxPackageName.c_str());
    CloseHandle(SfxPackage);
    SfxPackage = INVALID_HANDLE_VALUE;

    SlideWin = (unsigned char*)malloc(WSIZE);
    if (!SlideWin)
        return Error(STR_LOWMEM);
    InPtr = (unsigned char*)IOBuffer;
    InEnd = InPtr + sfxHead.TotalTextSize;
    InflatingTexts = TRUE;

    switch (Inflate())
    {
    case 4:
    case 1:
    case 2:
        return ErrorPath(STR_CORRUPTSFX, SfxPackageName.c_str());
    case 3:
        return Error(STR_LOWMEM);
    case 5:
        return ErrorPath(STR_LARGETEXT, SfxPackageName.c_str());
    }
    if (Crc != sfxHead.TextsCRC)
        return ErrorPath(STR_CORRUPTSFX, SfxPackageName.c_str());

    char* ptr = (char*)SlideWin;
    int size = min(sfxHead.TextLen[TITLELEN], SE_MAX_TITLE - 1);
    memcpy(Settings.Title, ptr, size);
    Settings.Title[size] = 0;
    ptr += sfxHead.TextLen[TITLELEN];

    size = min(sfxHead.TextLen[TEXTLEN], SE_MAX_TEXT - 1);
    memcpy(Settings.Text, ptr, size);
    Settings.Text[size] = 0;
    ptr += sfxHead.TextLen[TEXTLEN];

    size = min(sfxHead.TextLen[ABOUTLICENCEDLEN], SE_MAX_ABOUT - 1);
    memcpy(About, ptr, size);
    About[size] = 0;
    lstrcpy(DefAbout, About); // keep for later use
    ptr += sfxHead.TextLen[ABOUTLICENCEDLEN];

    size = min(sfxHead.TextLen[BUTTONTEXTLEN], SE_MAX_EXTRBTN - 1);
    memcpy(Settings.ExtractBtnText, ptr, size);
    Settings.ExtractBtnText[size] = 0;
    ptr += sfxHead.TextLen[BUTTONTEXTLEN];

    size = min(sfxHead.TextLen[VENDORLEN], SE_MAX_VENDOR - 1);
    memcpy(Settings.Vendor, ptr, size);
    Settings.Vendor[size] = 0;
    lstrcpy(DefVendor, Settings.Vendor); // keep for later use
    ptr += sfxHead.TextLen[VENDORLEN];

    size = min(sfxHead.TextLen[WWWLEN], SE_MAX_WWW - 1);
    memcpy(Settings.WWW, ptr, size);
    Settings.WWW[size] = 0;
    lstrcpy(DefWWW, Settings.WWW); // keep for later use
    ptr += sfxHead.TextLen[WWWLEN];

    std::wstring moduleFileName;
    if (!GetModuleFileNameOwned(moduleFileName) ||
        !CopyWideToLegacyTextExact(moduleFileName.c_str(), Settings.IconFile,
                                   _countof(Settings.IconFile)))
        Settings.IconFile[0] = '\0';
    Settings.IconIndex = -IDI_SFXICON;

    return TRUE;
}

BOOL main2()
{
    CrcTab = (__UINT32*)malloc(CRC_TAB_SIZE);
    if (!CrcTab)
        return Error(STR_LOWMEM);
    MakeCrcTable(CrcTab);

    if (!GetEnvironmentVariableOwned(L"SFX_DEFPACKAGE", SfxPackageName))
        return Error(STR_NODEFPACKAGE);
    SetLastError(ERROR_SUCCESS);

    if (ParamKind != L'\0')
    {
        switch (ParamKind)
        {
        case L'p':
            SfxPackageName = ParamPath;
            break;
        case L's':
            if (!LoadSettings())
                return FALSE;
            // package= is an OPTIONAL key. LoadSettings leaves Settings.SfxFile untouched when the
            // file omits it, so adopting the field unconditionally erased the SFX_DEFPACKAGE
            // default and aborted with STR_NODEFPACKAGE. pre-unicode seeded Settings.SfxFile from
            // the environment and let ImportSFXSettings overwrite it only when the key was there.
            if (*Settings.SfxFile != 0 && !LegacyTextToWide(Settings.SfxFile, SfxPackageName))
                return Error(STR_BADSETFORMAT);
        }
    }

    if (SfxPackageName.empty())
        return Error(STR_NODEFPACKAGE);
    if (!LoadDefaults())
        return FALSE;
    if (ParamKind == L's')
    {
        std::string zip2sfxDir;
        if (!GetZip2SfxDir(zip2sfxDir))
            return Error(STR_BADSETFORMAT);

        ImportSFXSettings(SettingsTextData, &Settings, zip2sfxDir.c_str());
        if (lstrcmpi(Settings.Vendor, DefVendor) != 0 || lstrcmpi(Settings.WWW, DefWWW) != 0)
        {
            char buffer[SE_MAX_VENDOR + SE_MAX_WWW + 10 + SE_MAX_ABOUT];
            sprintf(buffer, "%s\r\n%s\r\n\r\n%s", DefVendor, DefWWW, DefAbout);
            lstrcpyn(About, buffer, SE_MAX_ABOUT);
        }
    }
    const std::wstring iconFile = ZipTextToWide(Settings.IconFile);
    switch (LoadIcons(iconFile.c_str(), Settings.IconIndex, &Icons, &IconsCount))
    {
    case 1:
        return Error(STR_ERROPENICO, Settings.IconFile);
    case 2:
        return Error(STR_ERRLOADLIB, Settings.IconFile);
    case 3:
        return Error(STR_ERRLOADLIB2, Settings.IconFile);
    case 4:
        return Error(STR_ERRLOADICON, Settings.IconFile);
    }

    ZipFile = CreateFileW(ZipName.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                          FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, NULL);
    if (ZipFile == INVALID_HANDLE_VALUE)
        return ErrorPath(STR_ERROPEN, ZipName.c_str());

    if (!CheckZip())
        return FALSE;

    if (!OvewriteExe && SalGetFileAttributes(ExeName.c_str()) != 0xFFFFFFFF)
    {
        std::wstring overwrite = L"File '" + ExeName + L"' already exists. Overwrite (y/n)? ";
        PrintWideText(overwrite);
        char c;
        while ((c = _getch()) != 'n' && c != 'y')
            ;
        printf("%c\n", c);
        if (c != 'y')
            return FALSE;
    }
    ExeFile = CreateFileW(ExeName.c_str(), GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS,
                          FILE_ATTRIBUTE_NORMAL, NULL);
    if (ExeFile == INVALID_HANDLE_VALUE)
        return ErrorPath(STR_ERRCREATE, ExeName.c_str());

    if (!WriteSfxExecutable())
        return FALSE;
    if (!AppendArchive())
        return FALSE;
    return TRUE;
}

// We want to catch SEH exceptions even on x64 Windows 7 SP1 and newer
// http://blog.paulbetts.org/index.php/2010/07/20/the-case-of-the-disappearing-onload-exception-user-mode-callback-exceptions-in-x64/
// http://connect.microsoft.com/VisualStudio/feedback/details/550944/hardware-exceptions-on-x64-machines-are-silently-caught-in-wndproc-messages
// http://support.microsoft.com/kb/976038
void EnableExceptionsOn64()
{
    typedef BOOL(WINAPI * FSetProcessUserModeExceptionPolicy)(DWORD dwFlags);
    typedef BOOL(WINAPI * FGetProcessUserModeExceptionPolicy)(LPDWORD dwFlags);
    typedef BOOL(WINAPI * FIsWow64Process)(HANDLE, PBOOL);
#define PROCESS_CALLBACK_FILTER_ENABLED 0x1

    HINSTANCE hDLL = LoadLibraryW(L"KERNEL32.DLL");
    if (hDLL != NULL)
    {
        FIsWow64Process isWow64 = (FIsWow64Process)GetProcAddress(hDLL, "IsWow64Process");                                                      // Min: XP SP2
        FSetProcessUserModeExceptionPolicy set = (FSetProcessUserModeExceptionPolicy)GetProcAddress(hDLL, "SetProcessUserModeExceptionPolicy"); // Min: Vista with hotfix
        FGetProcessUserModeExceptionPolicy get = (FGetProcessUserModeExceptionPolicy)GetProcAddress(hDLL, "GetProcessUserModeExceptionPolicy"); // Min: Vista with hotfix
        if (isWow64 != NULL && set != NULL && get != NULL)
        {
            BOOL bIsWow64;
            if (isWow64(GetCurrentProcess(), &bIsWow64) && bIsWow64)
            {
                DWORD dwFlags;
                if (get(&dwFlags))
                    set(dwFlags & ~PROCESS_CALLBACK_FILTER_ENABLED);
            }
        }
        FreeLibrary(hDLL);
    }
}

int wmain(int argc, wchar_t* argv[])
{
    EnableExceptionsOn64();

    printf(StringTable[STR_WELCOME_MESSAGE]);
    if (argc < 2 || argc > 5 || !ProcessCommandline(argc, argv))
    {
        printf(StringTable[STR_HELP]);
        return 1;
    }

    IOBuffer = (char*)malloc(0xFFFF);
    if (!IOBuffer)
        return Error(STR_LOWMEM);

    BOOL ret = main2();

    if (SettingsTextData)
        free(SettingsTextData);
    if (ZipFile != INVALID_HANDLE_VALUE)
        CloseHandle(ZipFile);
    if (SettingsFile != INVALID_HANDLE_VALUE)
        CloseHandle(SettingsFile);
    if (SfxPackage != INVALID_HANDLE_VALUE)
        CloseHandle(SfxPackage);
    if (ExeFile != INVALID_HANDLE_VALUE)
    {
        CloseHandle(ExeFile);
        if (!ret)
            DeleteFileW(ExeName.c_str());
    }
    if (CrcTab)
        free(CrcTab);
    if (SlideWin)
        free(SlideWin);
    if (Icons)
        DestroyIcons(Icons, IconsCount);

    if (ret)
    {
        printf(StringTable[STR_SUCCESS]);
        return 0;
    }

    return 1;
}
