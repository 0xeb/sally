// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-License-Identifier: GPL-2.0-or-later
// CommentsTranslationProject: TRANSLATED

#pragma once

struct NSVGrasterizer;
struct NSVGimage;
void RenderSVGImage(NSVGrasterizer* rast, HDC hDC, int x, int y, const char* svgName, int iconSize, COLORREF bkColor, BOOL enabled);

// returns SysColor in format for SVG library (BGR instead of Win32 RGB)
DWORD GetSVGSysColor(int index);

//*****************************************************************************
//
// CSVGSprite
//

#define SVGSTATE_ORIGINAL 0x0001 // unchanged original SVG
#define SVGSTATE_ENABLED 0x0002  // SVG tinted to enabled text color
#define SVGSTATE_DISABLED 0x0004 // SVG tinted to disabled text color
#define SVGSTATE_COUNT 3

// Object used to render SVG via a cached bitmap.
// Primarily holds a color version of the image rendered according to colors in the source SVG.
// It can also hold colored versions of the bitmap (hence Sprite in the name - internally uses a larger bitmap with multiple images),
// for example "disabled", "active", "selected".
class CSVGSprite
{
public:
    CSVGSprite();
    ~CSVGSprite();

    // discards bitmap, initializes variables to default state
    void Clean();

    // 'states' is a combination of bits from the SVGSTATE_* family
    BOOL Load(int resID, int width, int height, DWORD states);

    void GetSize(SIZE* s);
    int GetWidth();
    int GetHeight();

    // 'hDC' is the target DC where the bitmap should be rendered
    // 'x' and 'y' are target coordinates in 'hDC'
    // 'width' and 'height' are target size; if they are -1, 'Width'/'Height' is used
    void AlphaBlend(HDC hDC, int x, int y, int width, int height, DWORD state);

protected:
    // loads resource into memory, allocates a buffer one byte longer and null-terminates the resource
    // on success returns pointer to allocated memory (must be freed), on error returns NULL
    char* LoadSVGResource(int resID);

    // Input 'sz' defines size in pixels to fit the SVG after conversion to bitmap.
    // If one dimension is -1, it is unspecified and computed by preserving aspect ratio.
    // If both dimensions are unspecified, values are taken from source data.
    // Output returns size of the resulting bitmap in pixels.
    void GetScaleAndSize(const NSVGimage* image, const SIZE* sz, float* scale, int* width, int* height);

    // creates a DIB of size 'width' and 'height', returns its handle and data pointer
    void CreateDIB(int width, int height, HBITMAP* hMemBmp, void** lpMemBits);

    // tints SVG 'image' to color defined by 'state'
    void ColorizeSVG(NSVGimage* image, DWORD state);

protected:
    int Width; // size of one image in pixels
    int Height;
    HBITMAP HBitmaps[SVGSTATE_COUNT];
};

//*****************************************************************************
//
// global variables
//

//extern HBITMAP HArrowRight;         // bitmapa vytvorena z SVG, pouzivame pro tlacitka jako sipku vpravo
//extern SIZE ArrowRightSize;         // rozmery v bodech
//HBITMAP HArrowRight = NULL;
//SIZE ArrowRightSize = { 0 };

extern CSVGSprite SVGArrowRight;
extern CSVGSprite SVGArrowRightSmall;
extern CSVGSprite SVGArrowMore;
extern CSVGSprite SVGArrowLess;
extern CSVGSprite SVGArrowDropDown;
