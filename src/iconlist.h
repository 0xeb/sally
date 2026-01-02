// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-License-Identifier: GPL-2.0-or-later
// CommentsTranslationProject: TRANSLATED

#pragma once

/************************************************************************************

What can be extracted from HICON provided by the OS?

  Using GetIconInfo() the OS returns copies of the MASK and COLOR bitmaps. These can be
  further examined by calling GetObject(), which allows us to extract geometry and color
  arrangement. These are copies of bitmaps, not the original bitmaps held inside the OS. MASK is
  always a 1-bit bitmap. COLOR is a bitmap compatible with the screen DC. There is
  therefore no way to get information about the actual color depth of the COLOR bitmap.

  A special case is purely black-and-white icons. These are provided entirely in MASK, which
  is then 2x higher. COLOR is then NULL. The upper half of the MASK bitmap is the AND part
  and the lower half is the XOR part. This case can be easily detected by testing COLOR == NULL.

  Starting from Windows XP, there is another special case: icons containing an ALPHA channel.
  These are DIBs with a color depth of 32 bits, where each pixel consists of ARGB components.

  




************************************************************************************/

//
// There is potential room for optimization of our ImageList implementation.
// We could keep the DIB in the same format as the screen runs on. BitBlt
// is then allegedly faster (I have not verified it) according to MSDN:
//   http://support.microsoft.com/default.aspx?scid=kb;EN-US;230492
//   (HOWTO: Retrieving an Optimal DIB Format for a Device)
//
// Several factors speak against this optimization:
//   - we would need to support various data formats in the code (15, 16, 24, 32 bits)
//   - because we render at most tens of icons simultaneously, rendering speed is not
//     critical for us; I measured these drawing speeds:
//     ((100 000 times a 16x16, 32bpp DIB was drawn to the screen via BitBlt))
//     Screen resolution     Total time (W2K, Matrox G450)
//     32 bpp                  0.40 s
//     24 bpp                  0.80 s
//     16 bpp                  0.65 s
//      8 bpp                  1.16 s
//   - we would somehow still need to keep icons with ALPHA channel, which are 32 bpp
//

//
// Why do we need our own equivalent of ImageList:
//
// ImageList from CommonControls has one fundamental problem: if we ask it
// to hold DeviceDependentBitmaps, it cannot display a blended item. Instead,
// it renders it with a pattern.
//
// If the bitmap held is a DIB, blending works great, but rendering
// a regular item is orders of magnitude slower (DIB->screen conversion).
//
// Furthermore, there is a risk that in some implementations, calling ImageList_SetBkColor
// does not physically change the held bitmap based on the mask, but only sets an internal
// variable. Of course, then drawing is slower, because masking needs to be performed.
// I tested it under W2K and the function works correctly.
//
// The only option would be to keep ImageList for data storage and only reprogram blending.
// But a problem arises in the ImageList_GetImageInfo function, which
// allows access to internal Image/Mask bitmaps. ImageList always has them selected
// in MemDC, so according to MSDN (Q131279: SelectObject() Fails After
// ImageList_GetImageInfo()), the only option is to first call CopyImage and only then
// work on the bitmap. This would lead to extremely slow rendering of
// blended items.
//
// Another risk for ImageList are icon invert dots. An icon consists of
// two bitmaps: MASK and COLORS. The mask is ANDed to the target and colors are XORed through it.
// Thanks to XORing, icons can invert some of their parts. Cursors use this
// especially, see WINDOWS\Cursors.
//

//******************************************************************************
//
// CIconList
//
//
// Following the W2K pattern, we keep items in a bitmap wide 4 items. Probably
// operations on a bitmap oriented this way will be faster.

#define IL_DRAW_BLEND 0x00000001       // 50% of the blend color will be used
#define IL_DRAW_TRANSPARENT 0x00000002 // when drawing, the original background is preserved (if not specified, the background will be filled with the defined color)
#define IL_DRAW_ASALPHA 0x00000004     // uses the (inverted) color in the BLUE channel as alpha, by which it blends the specified foreground color to the background; currently used for throbber
#define IL_DRAW_MASK 0x00000010        // draw the mask

class CIconList : public CGUIIconListAbstract
{
private:
    int ImageWidth; // dimensions of one image
    int ImageHeight;
    int ImageCount;  // number of images in the bitmap
    int BitmapWidth; // dimensions of held bitmaps
    int BitmapHeight;

    // images are arranged from left to right and top to bottom
    HBITMAP HImage;   // DIB, its raw data are in the ImageRaw variable
    DWORD* ImageRaw;  // ARGB values; Alpha: 0x00=transparent, 0xFF=opaque, others=partial_transparency(only for IL_TYPE_ALPHA)
    BYTE* ImageFlags; // array of 'imageCount' elements; (IL_TYPE_xxx)

    COLORREF BkColor; // current background color (pixels where Alpha==0x00)

    // shared variables across all imagelists -- we save memory
    static HDC HMemDC;                       // shared mem dc
    static HBITMAP HOldBitmap;               // original bitmap
    static HBITMAP HTmpImage;                // cache for paint + temporary mask storage
    static DWORD* TmpImageRaw;               // raw data from HTmpImage
    static int TmpImageWidth;                // dimensions of HTmpImage in pixels
    static int TmpImageHeight;               // dimensions of HTmpImage in pixels
    static int MemDCLocks;                   // for destruction of mem dc
    static CRITICAL_SECTION CriticalSection; // access synchronization
    static int CriticalSectionLocks;         // for construction/destruction of CriticalSection

public:
    //    BOOL     Dump; // if TRUE, raw data is dumped to TRACE

public:
    CIconList();
    ~CIconList();

    virtual BOOL WINAPI Create(int imageWidth, int imageHeight, int imageCount);
    virtual BOOL WINAPI CreateFromImageList(HIMAGELIST hIL, int requiredImageSize = -1);          // if 'requiredImageSize' is -1, geometry from hIL will be used
    virtual BOOL WINAPI CreateFromPNG(HINSTANCE hInstance, LPCTSTR lpBitmapName, int imageWidth); // loads from PNG resource, must be a long strip one row high
    virtual BOOL WINAPI CreateFromRawPNG(const void* rawPNG, DWORD rawPNGSize, int imageWidth);
    virtual BOOL WINAPI CreateFromBitmap(HBITMAP hBitmap, int imageCount, COLORREF transparentClr); // loads bitmap (maximum 256 colors), must be a long strip one row high
    virtual BOOL WINAPI CreateAsCopy(const CIconList* iconList, BOOL grayscale);
    virtual BOOL WINAPI CreateAsCopy(const CGUIIconListAbstract* iconList, BOOL grayscale);

    // converts icon list to grayscale version
    virtual BOOL WINAPI ConvertToGrayscale(BOOL forceAlphaForBW);

    // compresses the bitmap to a 32-bit PNG with alpha channel (one long row)
    // if successful, returns TRUE and a pointer to allocated memory, which must be deallocated later
    // returns FALSE on error
    virtual BOOL WINAPI SaveToPNG(BYTE** rawPNG, DWORD* rawPNGSize);

    virtual BOOL WINAPI ReplaceIcon(int index, HICON hIcon);

    // creates an icon from position 'index'; returns its handle or NULL on failure
    // the returned icon must be destroyed using the DestroyIcon API after use
    virtual HICON WINAPI GetIcon(int index);
    HICON GetIcon(int index, BOOL useHandles);

    // creates an imagelist (one row, number of columns based on number of items); returns its handle or NULL on failure
    // the returned imagelist must be destroyed using the ImageList_Destroy() API after use
    virtual HIMAGELIST WINAPI GetImageList();

    // copies one item from 'srcIL' at position 'srcIndex' to position 'dstIndex'
    virtual BOOL WINAPI Copy(int dstIndex, CIconList* srcIL, int srcIndex);

    // copies one item from position 'srcIndex' to 'hDstImageList' at position 'dstIndex'
    //    BOOL CopyToImageList(HIMAGELIST hDstImageList, int dstIndex, int srcIndex);

    virtual BOOL WINAPI Draw(int index, HDC hDC, int x, int y, COLORREF blendClr, DWORD flags);

    virtual BOOL WINAPI SetBkColor(COLORREF bkColor);
    virtual COLORREF WINAPI GetBkColor();

private:
    // if it does not exist, creates HTmpImage
    // if HTmpImage exists and is smaller than 'width' x 'height', creates a new one
    // returns TRUE on success, otherwise returns FALSE and preserves the previous HTmpImage
    BOOL CreateOrEnlargeTmpImage(int width, int height);

    // returns the handle of the bitmap currently selected in HMemDC
    // if HMemDC does not exist, returns NULL
    HBITMAP GetCurrentBitmap();

    // 'index' determines the position of the icon in HImage
    // returns TRUE if the image 'index' in HImage contained an alpha channel
    BYTE ApplyMaskToImage(int index, BYTE forceXOR);

    // for debugging purposes -- displays a dump of ARGB values of the color bitmap and mask
    //    void DumpToTrace(int index, BOOL dumpMask);

    // rendering pixel by pixel and subsequent BitBlt is in RELEASE version
    // only about 30% slower than pure BitBlt

    BOOL DrawALPHA(HDC hDC, int x, int y, int index, COLORREF bkColor);
    BOOL DrawXOR(HDC hDC, int x, int y, int index, COLORREF bkColor);
    BOOL AlphaBlend(HDC hDC, int x, int y, int index, COLORREF bkColor, COLORREF fgColor);
    BOOL DrawMask(HDC hDC, int x, int y, int index, COLORREF fgColor, COLORREF bkColor);
    BOOL DrawALPHALeaveBackground(HDC hDC, int x, int y, int index);
    BOOL DrawAsAlphaLeaveBackground(HDC hDC, int x, int y, int index, COLORREF fgColor);

    void StoreMonoIcon(int index, WORD* mask);

    // special helper function for CreateFromBitmap(); copy from 'hSrcBitmap'
    // the selected number of items to 'dstIndex'; assumes that 'hSrcBitmap' will be long
    // strip of icons, one row high
    // transparentClr specifies the color to be treated as transparent
    // it is assumed that the source bitmap has the same icon size as the target (ImageWidth, ImageHeight)
    // with one copy operation, you can work with at most one row of the target bitmap,
    // for example, you cannot copy data to two rows in the target bitmap
    BOOL CopyFromBitmapIternal(int dstIndex, HBITMAP hSrcBitmap, int srcIndex, int imageCount, COLORREF transparentClr);
};

HBITMAP LoadPNGBitmap(HINSTANCE hInstance, LPCTSTR lpBitmapName, DWORD flags);
HBITMAP LoadRawPNGBitmap(const void* rawPNG, DWORD rawPNGSize, DWORD flags);

inline BYTE GetGrayscaleFromRGB(int red, int green, int blue)
{
    //  int brightness = (76*(int)red + 150*(int)green + 29*(int)blue) / 255;
    int brightness = (55 * (int)red + 183 * (int)green + 19 * (int)blue) / 255;
    //  int brightness = (40*(int)red + 175*(int)green + 60*(int)blue) / 255;
    if (brightness > 255)
        brightness = 255;
    return (BYTE)brightness;
}
