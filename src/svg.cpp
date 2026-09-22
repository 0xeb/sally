// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"

#include "svg.h"
#include "common/IFileSystem.h"
#include "common/IPathService.h"
#include "common/ModuleRelativePath.h"
#include "common/unicode/helpers.h"

#define NANOSVG_IMPLEMENTATION
#include "nanosvg\nanosvg.h"
#define NANOSVGRAST_IMPLEMENTATION
#include "nanosvg\nanosvgrast.h"

CSVGSprite SVGArrowRight;
CSVGSprite SVGArrowRightSmall;
CSVGSprite SVGArrowMore;
CSVGSprite SVGArrowLess;
CSVGSprite SVGArrowDropDown;

// alternative: http://stackoverflow.com/questions/11376288/fast-computing-of-log2-for-64-bit-integers
// (could probably be found for shorter versions too)
//
// the following solution has the advantage that for constants it will be computed at compile time
// LOG2_k(n) returns floor(log2(n)) and is valid for values 0 <= n < 1 << k
#define LOG2_2(n) ((n) & 0x2 ? 1 : 0)
#define LOG2_4(n) ((n) & 0xC ? 2 + LOG2_2((n) >> 2) : LOG2_2(n))
#define LOG2_8(n) ((n) & 0xF0 ? 4 + LOG2_4((n) >> 4) : LOG2_4(n))
#define LOG2_16(n) ((n) & 0xFF00 ? 8 + LOG2_8((n) >> 8) : LOG2_8(n))
#define LOG2_32(n) ((n) & 0xFFFF0000 ? 16 + LOG2_16((n) >> 16) : LOG2_16(n))
#define LOG2_64(n) ((n) & 0xFFFFFFFF00000000 ? 32 + LOG2_32((n) >> 32) : LOG2_32(n))

//__popcnt16, __popcnt, __popcnt64
//https://msdn.microsoft.com/en-us/library/bb385231(v=vs.100).aspx

DWORD GetSVGSysColor(int index)
{
    DWORD color = GetSysColor(index);
    DWORD ret = 0xFF000000;
    ret |= GetBValue(color) << 16;
    ret |= GetGValue(color) << 8;
    ret |= GetRValue(color);
    return ret;
}

//*****************************************************************************
//
// RenderSVGImage
//

// wide: fileName is now the genuine wide path built in RenderSVGImage below - no
// more narrow round-trip through AnsiToWide, which could not have recovered characters the
// caller's own module-path narrowing had already lost.
char* ReadSVGFile(const wchar_t* fileName)
{
    char* buff = NULL;
    HANDLE hFile = gFileSystem->CreateFile(fileName, GENERIC_READ,
                                           FILE_SHARE_READ, NULL,
                                           OPEN_EXISTING,
                                           FILE_FLAG_SEQUENTIAL_SCAN,
                                           NULL);
    HANDLES_ADD_EX(__otQuiet, hFile != INVALID_HANDLE_VALUE, __htFile, __hoCreateFile, hFile, GetLastError(), TRUE);
    if (hFile != INVALID_HANDLE_VALUE)
    {
        uint64_t size64 = 0;
        const FileResult sizeResult = gFileSystem->GetHandleFileSize(hFile, &size64);
        if (sizeResult.success && size64 <= MAXDWORD)
        {
            const DWORD size = (DWORD)size64;
            buff = (char*)malloc(size + 1);
            DWORD read;
            const FileResult readResult = gFileSystem->ReadFromHandle(hFile, buff, size, &read);
            if (readResult.success && read == size)
            {
                buff[size] = 0;
            }
            else
            {
                TRACE_EW(L"ReadSVGFile(): ReadFile() failed on " << fileName);
                free(buff);
                buff = NULL;
            }
        }
        else
        {
            TRACE_EW(L"ReadSVGFile(): GetFileSize() failed on " << fileName);
        }
        HANDLES_REMOVE(hFile, __htFile, "IFileSystem::CloseHandle");
        gFileSystem->CloseFileHandle(hFile);
    }
    else
    {
        TRACE_IW(L"ReadSVGFile(): cannot open SVG file " << fileName);
    }
    return buff;
}

// render icons for which we have SVG representation
void RenderSVGImage(NSVGrasterizer* rast, HDC hDC, int x, int y, const wchar_t* svgName, int iconSize, COLORREF bkColor, BOOL enabled)
{
    // wide: GetModuleFileName (ANSI) narrowed Sally's own EXE path via CP_ACP right
    // at the source, with no wide value ever captured - a non-ASCII install path (Unicode
    // user-profile name, non-English portable install) made toolbar SVG icons silently fail to
    // load, since ReadSVGFile's own AnsiToWide round-trip could not recover what was already
    // lost. Same shape as the jump-list and shell-link icon-path fixes.
    if (gPathService == NULL || svgName == NULL)
        return;

    std::wstring modulePath;
    if (!gPathService->GetModuleFileName(NULL, modulePath).success)
        return;

    std::wstring relativePath;
    std::wstring svgFile;
    try
    {
        relativePath.assign(L"toolbars\\");
        relativePath.append(svgName);
        relativePath.append(L".svg");
    }
    catch (const std::bad_alloc&)
    {
        return;
    }
    catch (const std::length_error&)
    {
        return;
    }
    if (!sally::path::BuildModuleRelativePath(modulePath.c_str(), relativePath.c_str(), svgFile))
        return;

    char* svg = ReadSVGFile(svgFile.c_str());
    if (svg != NULL)
    {
        HDC hMemDC = HANDLES(CreateCompatibleDC(NULL));
        BITMAPINFOHEADER bmhdr;
        memset(&bmhdr, 0, sizeof(bmhdr));
        bmhdr.biSize = sizeof(bmhdr);
        bmhdr.biWidth = iconSize;
        bmhdr.biHeight = -iconSize;
        if (bmhdr.biHeight == 0)
            bmhdr.biHeight = -1;
        bmhdr.biPlanes = 1;
        bmhdr.biBitCount = 32;
        bmhdr.biCompression = BI_RGB;
        void* lpMemBits = NULL;
        HBITMAP hMemBmp = HANDLES(CreateDIBSection(hMemDC, (CONST BITMAPINFO*)&bmhdr, DIB_RGB_COLORS, &lpMemBits, NULL, 0));
        SelectObject(hMemDC, hMemBmp);

        RECT r;
        r.left = x;
        r.top = y;
        r.right = x + iconSize;
        r.bottom = y + iconSize;
        SetBkColor(hDC, bkColor);
        ExtTextOutW(hDC, 0, 0, ETO_OPAQUE, &r, L"", 0, NULL);

        float sysDPIScale = (float)GetScaleForSystemDPI();
        NSVGimage* image = nsvgParse(svg, "px", sysDPIScale);

        if (!enabled)
        {
            DWORD disabledColor = GetSVGSysColor(COLOR_BTNSHADOW); // JRYFIXME - initial guess, where will we get the disabled color from?
            NSVGshape* shape = image->shapes;
            while (shape != NULL)
            {
                if ((shape->fill.color & 0x00FFFFFF) != 0x00FFFFFF)
                    shape->fill.color = disabledColor;
                shape = shape->next;
            }
        }

        float scale = sysDPIScale / 100;
        nsvgRasterize(rast, image, 0, 0, scale, (BYTE*)lpMemBits, iconSize, iconSize, iconSize * 4);
        nsvgDelete(image);

        BLENDFUNCTION bf;
        bf.BlendOp = AC_SRC_OVER;
        bf.BlendFlags = 0;
        bf.SourceConstantAlpha = 0xff; // want to use per-pixel alpha values
        bf.AlphaFormat = AC_SRC_ALPHA;
        AlphaBlend(hDC, x, y, iconSize, iconSize, hMemDC, 0, 0, iconSize, iconSize, bf);

        HANDLES(DeleteObject(hMemBmp));
        HANDLES(DeleteDC(hMemDC));

        free(svg);
    }
}

//*****************************************************************************
//
// CSVGSprite
//

CSVGSprite::CSVGSprite()
{
    for (int i = 0; i < SVGSTATE_COUNT; i++)
        HBitmaps[i] = NULL;
    Clean();
}

CSVGSprite::~CSVGSprite()
{
    Clean();
}

void CSVGSprite::Clean()
{
    for (int i = 0; i < SVGSTATE_COUNT; i++)
    {
        if (HBitmaps[i] != NULL)
        {
            HANDLES(DeleteObject(HBitmaps[i]));
            HBitmaps[i] = NULL;
        }
    }
    Width = -1;
    Height = -1;
}

char* CSVGSprite::LoadSVGResource(int resID)
{
    char* ret = NULL;
    HRSRC hRsrc = FindResource(HInstance, MAKEINTRESOURCE(resID), RT_RCDATA);
    if (hRsrc != NULL)
    {
        char* rawSVG = (char*)LoadResource(HInstance, hRsrc);
        if (rawSVG != NULL)
        {
            DWORD size = SizeofResource(HInstance, hRsrc);
            if (size > 0)
            {
                NSVGimage* image = NULL;
                NSVGrasterizer* rast = NULL;

                char* terminatedSVG = (char*)malloc(size + 1);
                memcpy(terminatedSVG, rawSVG, size);
                terminatedSVG[size] = 0;
                ret = terminatedSVG;
            }
            else
            {
                TRACE_E("LoadSVGResource() Invalid resource data! resID=" << resID);
            }
        }
        else
        {
            TRACE_E("LoadSVGResource() Cannot load resource! resID=" << resID);
        }
    }
    else
    {
        TRACE_E("LoadSVGResource() Resource not found! resID=" << resID);
    }
    return ret;
}

void CSVGSprite::GetScaleAndSize(const NSVGimage* image, const SIZE* sz, float* scale, int* width, int* height)
{
    if (sz->cx != -1 || sz->cy != -1)
    {
        float scaleX, scaleY;
        if (sz->cx != -1)
            scaleX = sz->cx / image->width;
        if (sz->cy != -1)
            scaleY = sz->cy / image->height;
        if (sz->cx == -1)
        {
            *scale = scaleY;
            *height = sz->cy;
            *width = (int)(image->width * *scale);
        }
        else
        {
            if (sz->cy == -1)
            {
                *scale = scaleX;
                *width = sz->cx;
                *height = (int)(image->height * *scale);
            }
            else
            {
                *scale = min(scaleX, scaleY);
                *width = (int)(image->width * *scale);
                *height = (int)(image->height * *scale);
            }
        }
    }
    else
    {
        *scale = (float)GetScaleForSystemDPI() / 100;
        *width = (int)(image->width * *scale);
        *height = (int)(image->height * *scale);
    }
}
/*
HBITMAP
CSVGSprite::LoadSVGToBitmap(int resID, SIZE *sz)
{
  if (sz == NULL)
    TRACE_C("LoadSVGToBitmap(): invalid parameters!");

  HBITMAP hMemBmp = NULL;

  char *terminatedSVG = LoadSVGResource(resID);
  if (terminatedSVG != NULL)
  {
    NSVGimage *image = NULL;
    image = nsvgParse(terminatedSVG, "px", (float)GetSystemDPI());
    free(terminatedSVG);

    float scale;
    int w, h;
    GetScaleAndSize(image, sz, &scale, &w, &h);

    NSVGrasterizer *rast = NULL;
    rast = nsvgCreateRasterizer();

    HDC hMemDC = HANDLES(CreateCompatibleDC(NULL));
    BITMAPINFOHEADER bmhdr;
    memset(&bmhdr, 0, sizeof(bmhdr));
    bmhdr.biSize = sizeof(bmhdr);
    bmhdr.biWidth = w;
    bmhdr.biHeight = -h;
    if (bmhdr.biHeight == 0) bmhdr.biHeight = -1;
    bmhdr.biPlanes = 1;
    bmhdr.biBitCount = 32;
    bmhdr.biCompression = BI_RGB;
    void *lpMemBits = NULL;
    hMemBmp = HANDLES(CreateDIBSection(hMemDC, (CONST BITMAPINFO *)&bmhdr, DIB_RGB_COLORS, &lpMemBits, NULL, 0));
    HANDLES(DeleteDC(hMemDC));

    nsvgRasterize(rast, image, 0, 0, scale, (BYTE*)lpMemBits, w, h, w * 4);

    sz->cx = w;
    sz->cy = h;

    nsvgDeleteRasterizer(rast);
    nsvgDelete(image);
  }
  return hMemBmp;
}
*/
void CSVGSprite::CreateDIB(int width, int height, HBITMAP* hMemBmp, void** lpMemBits)
{
    HDC hMemDC = HANDLES(CreateCompatibleDC(NULL));
    BITMAPINFOHEADER bmhdr;
    memset(&bmhdr, 0, sizeof(bmhdr));
    bmhdr.biSize = sizeof(bmhdr);
    bmhdr.biWidth = width;
    bmhdr.biHeight = -height;
    if (bmhdr.biHeight == 0)
        bmhdr.biHeight = -1;
    bmhdr.biPlanes = 1;
    bmhdr.biBitCount = 32;
    bmhdr.biCompression = BI_RGB;
    *hMemBmp = HANDLES(CreateDIBSection(hMemDC, (CONST BITMAPINFO*)&bmhdr, DIB_RGB_COLORS, lpMemBits, NULL, 0));
    HANDLES(DeleteDC(hMemDC));
}

void CSVGSprite::ColorizeSVG(NSVGimage* image, DWORD state)
{
    if (state == SVGSTATE_ORIGINAL)
        return;

    int sysIndex;
    switch (state)
    {
    case SVGSTATE_ENABLED:
        sysIndex = COLOR_BTNTEXT;
        break;

    case SVGSTATE_DISABLED:
        sysIndex = COLOR_BTNSHADOW;
        break;

    default:
        sysIndex = COLOR_BTNTEXT;
        TRACE_E("CSVGSprite::ColorizeSVG() unknown state=" << state);
    }
    DWORD color = GetSVGSysColor(sysIndex);
    NSVGshape* shape = image->shapes;
    while (shape != NULL)
    {
        shape->fill.color = color;
        shape = shape->next;
    }
}

BOOL CSVGSprite::Load(int resID, int width, int height, DWORD states)
{
    if (states == 0 || states >= (1 << SVGSTATE_COUNT))
    {
        TRACE_E("CSVGSprite::Load() wrong states combination: " << states);
        states |= SVGSTATE_ORIGINAL;
    }
    Clean();

    char* terminatedSVG = LoadSVGResource(resID);
    if (terminatedSVG != NULL)
    {
        NSVGimage* image = NULL;
        image = nsvgParse(terminatedSVG, "px", (float)GetSystemDPI());
        free(terminatedSVG);

        float scale;
        SIZE sz = {width, height};
        GetScaleAndSize(image, &sz, &scale, &Width, &Height);

        NSVGrasterizer* rast = NULL;
        rast = nsvgCreateRasterizer();

        for (int i = 0; i < SVGSTATE_COUNT; i++)
        {
            DWORD state = 1 << i;
            if (states & state)
            {
                void* lpMemBits;
                CreateDIB(Width, Height, &HBitmaps[i], &lpMemBits);
                ColorizeSVG(image, state);
                nsvgRasterize(rast, image, 0, 0, scale, (BYTE*)lpMemBits, Width, Height, Width * 4);
            }
        }

        nsvgDeleteRasterizer(rast);
        nsvgDelete(image);
    }
    return TRUE;
}

void CSVGSprite::GetSize(SIZE* s)
{
    s->cx = Width;
    s->cy = Height;
}

int CSVGSprite::GetWidth()
{
    return Width;
}

int CSVGSprite::GetHeight()
{
    return Height;
}

void CSVGSprite::AlphaBlend(HDC hDC, int x, int y, int width, int height, DWORD state)
{
    HDC hMemTmpDC = HANDLES(CreateCompatibleDC(hDC));
    int index = LOG2_32(state);
    HBITMAP hOldBitmap = (HBITMAP)SelectObject(hMemTmpDC, HBitmaps[index]);

    if (width == -1)
        width = Width;
    if (height == -1)
        height = Height;

    BLENDFUNCTION bf;
    bf.BlendOp = AC_SRC_OVER;
    bf.BlendFlags = 0;
    bf.SourceConstantAlpha = 0xff; // want to use per-pixel alpha values
    bf.AlphaFormat = AC_SRC_ALPHA;
    ::AlphaBlend(hDC, x, y, width, height, hMemTmpDC, 0, 0, Width, Height, bf);

    SelectObject(hMemTmpDC, hOldBitmap);
    HANDLES(DeleteDC(hMemTmpDC));
}
