// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

//******************************************************************************
//
// CShrinkImage
//

class CShrinkImage
{
protected:
    DWORD NormCoeffX, NormCoeffY;
    DWORD* RowCoeff;
    DWORD* ColCoeff;
    DWORD* YCoeff;
    DWORD NormCoeff;
    DWORD Y, YBndr;
    DWORD* OutLine;
    DWORD* Buff;
    DWORD OrigHeight;
    WORD NewWidth;
    BOOL ProcessTopDown;

public:
    CShrinkImage();
    ~CShrinkImage();

    // allocates internal data for shrinking and returns TRUE on success
    // returns FALSE if the allocations fail
    BOOL Alloc(DWORD origWidth, DWORD origHeight,
               WORD newWidth, WORD newHeight,
               DWORD* outBuff, BOOL processTopDown);

    // destroys allocated buffers and initializes variables
    void Destroy();

    void ProcessRows(DWORD* inBuff, DWORD rowCount);

protected:
    DWORD* CreateCoeff(DWORD origLen, WORD newLen, DWORD& norm);
    void Cleanup();
};

//******************************************************************************
//
// CSalamanderThumbnailMaker
//
// Used to shrink the original image into a thumbnail.
//

class CSalamanderThumbnailMaker : public CSalamanderThumbnailMakerAbstract
{
protected:
    CFilesWindow* Window; // panel window in whose icon-reader we operate

    DWORD* Buffer;  // private buffer for row data from the plugin
    int BufferSize; // size of 'Buffer'
    BOOL Error;     // if TRUE, an error occurred while processing the thumbnail (result not usable)
    int NextLine;   // index of the next processed row

    DWORD* ThumbnailBuffer;    // downsized image
    DWORD* AuxTransformBuffer; // helper buffer the same size as ThumbnailBuffer (used to move data during transform + buffers are swapped after transform)
    int ThumbnailMaxWidth;     // maximum theoretical thumbnail dimensions (in pixels)
    int ThumbnailMaxHeight;
    int ThumbnailRealWidth;  // actual dimensions of the downsized image (in pixels)
    int ThumbnailRealHeight; //

    // parameters of the processed image
    int OriginalWidth;
    int OriginalHeight;
    DWORD PictureFlags;
    BOOL ProcessTopDown;

    CShrinkImage Shrinker; // handles image shrinking
    BOOL ShrinkImage;

public:
    CSalamanderThumbnailMaker(CFilesWindow* window);
    ~CSalamanderThumbnailMaker();

    // clears the object - called before processing the next thumbnail or when
    // the thumbnail from this object is no longer needed (ready or not)
    // parameter 'thumbnailMaxSize' specifies the maximum possible width and height
    // of the thumbnail in pixels; if it is -1, it is ignored
    void Clear(int thumbnailMaxSize = -1);

    // returns TRUE if a complete thumbnail is ready in this object (it was obtained
    // from the plugin)
    BOOL ThumbnailReady();

    // performs thumbnail transform according to PictureFlags (SSTHUMB_MIRROR_VERT is already done,
    // SSTHUMB_MIRROR_HOR and SSTHUMB_ROTATE_90CW remain)
    void TransformThumbnail();

    // converts finished thumbnail to a DDB and stores its dimensions and raw data in 'data'
    BOOL RenderToThumbnailData(CThumbnailData* data);

    // if the whole thumbnail was not created and no error occurred (see 'Error'), fills
    // the rest of the thumbnail with white (so the undefined part does not show
    // leftovers of the previous thumbnail); if not even three thumbnail rows were created,
    // nothing is filled (the thumbnail would be useless anyway)
    void HandleIncompleteImages();

    BOOL IsOnlyPreview() { return (PictureFlags & SSTHUMB_ONLY_PREVIEW) != 0; }

    // *********************************************************************************
    // methods of the CSalamanderThumbnailMakerAbstract interface
    // *********************************************************************************

    virtual BOOL WINAPI SetParameters(int picWidth, int picHeight, DWORD flags);
    virtual BOOL WINAPI ProcessBuffer(void* buffer, int rowsCount);
    virtual void* WINAPI GetBuffer(int rowsCount);
    virtual void WINAPI SetError() { Error = TRUE; }
    virtual BOOL WINAPI GetCancelProcessing();
};
