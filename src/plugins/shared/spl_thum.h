// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-License-Identifier: GPL-2.0-or-later
// CommentsTranslationProject: TRANSLATED

//****************************************************************************
//
// Copyright (c) 2023 Open Salamander Authors
//
// This is a part of the Open Salamander SDK library.
//
//****************************************************************************

#pragma once

#ifdef _MSC_VER
#pragma pack(push, enter_include_spl_thum) // so that structures are independent of the set alignment
#pragma pack(4)
#endif // _MSC_VER
#ifdef __BORLANDC__
#pragma option -a4
#endif // __BORLANDC__

//
// ****************************************************************************
// CSalamanderThumbnailMakerAbstract
//

// information about the image from which we generate the thumbnail, these flags are used
// in CSalamanderThumbnailMakerAbstract::SetParameters():
#define SSTHUMB_MIRROR_HOR 1                                            // the image needs to be horizontally mirrored
#define SSTHUMB_MIRROR_VERT 2                                           // the image needs to be vertically mirrored
#define SSTHUMB_ROTATE_90CW 4                                           // the image needs to be rotated 90 degrees clockwise
#define SSTHUMB_ROTATE_180 (SSTHUMB_MIRROR_VERT | SSTHUMB_MIRROR_HOR)   // the image needs to be rotated 180 degrees
#define SSTHUMB_ROTATE_90CCW (SSTHUMB_ROTATE_90CW | SSTHUMB_ROTATE_180) // the image needs to be rotated 90 degrees counter-clockwise
// the image is in lower quality or smaller than needed, Salamander will try to get
// a "quality" thumbnail for this image after completing the first round of getting "fast" thumbnails
#define SSTHUMB_ONLY_PREVIEW 8

class CSalamanderThumbnailMakerAbstract
{
public:
    // sets parameters for image processing when creating thumbnails; must be called
    // as the first method of this interface; 'picWidth' and 'picHeight' are dimensions
    // of the processed image (in pixels); 'flags' is a combination of SSTHUMB_XXX flags,
    // which provides information about the image passed in the 'buffer' parameter in the
    // ProcessBuffer method; returns TRUE if the buffers for scaling were successfully
    // allocated and ProcessBuffer can be called subsequently; if it returns FALSE, an error
    // occurred and thumbnail loading must be terminated
    virtual BOOL WINAPI SetParameters(int picWidth, int picHeight, DWORD flags) = 0;

    // processes part of the image in buffer 'buffer' (the processed part of the image is stored
    // row by row from top to bottom, pixels in rows are stored from left to right, each pixel
    // is represented by a 32-bit value composed of three bytes with R+G+B colors and
    // a fourth byte that is ignored); we distinguish two types of processing: copying
    // the image to the resulting thumbnail (if the size of the processed image does not exceed
    // the thumbnail size) and scaling down the image to the thumbnail (image larger than
    // thumbnail); 'buffer' is used only for reading; 'rowsCount' specifies how many rows
    // of the image are in the buffer;
    // if 'buffer' is NULL, data is taken from the internal buffer (plugin gets it via GetBuffer);
    // returns TRUE if the plugin should continue loading the image, returns FALSE if
    // thumbnail creation is complete (the entire image was processed) or should be
    // interrupted as soon as possible (e.g., user changed the path in the panel, so the thumbnail
    // is no longer needed)
    //
    // WARNING: while the CPluginInterfaceForThumbLoader::LoadThumbnail method is running,
    // path changes in the panel are blocked. For this reason, larger images need to be passed
    // and especially loaded in parts, and by testing the return value of
    // the ProcessBuffer method, verify whether loading should be interrupted.
    // If time-consuming operations need to be performed before calling SetParameters
    // or before calling ProcessBuffer, GetCancelProcessing must be called occasionally during this time.
    virtual BOOL WINAPI ProcessBuffer(void* buffer, int rowsCount) = 0;

    // returns internal buffer of the size needed to store 'rowsCount' rows of the image
    // (4 * 'rowsCount' * 'picWidth' bytes); if the object is in an error state (after calling
    // SetError), returns NULL;
    // the plugin must not deallocate the obtained buffer (it is deallocated automatically in Salamander)
    virtual void* WINAPI GetBuffer(int rowsCount) = 0;

    // notification of an error when obtaining the image (the thumbnail is considered invalid
    // and will not be used), other methods of this interface will from the moment of calling
    // SetError only return errors (GetBuffer and SetParameters) or work interruption
    // (ProcessBuffer)
    virtual void WINAPI SetError() = 0;

    // returns TRUE if the plugin should interrupt thumbnail loading
    // returns FALSE if the plugin should continue loading the image
    //
    // this method can be called before or after calling the SetParameters method
    //
    // serves to detect interruption requests in cases when the plugin
    // needs to perform time-consuming operations before calling SetParameters
    // or in case the plugin needs to pre-render the image, i.e., after calling
    // SetParameters, but before calling ProcessBuffer
    virtual BOOL WINAPI GetCancelProcessing() = 0;
};

//
// ****************************************************************************
// CPluginInterfaceForThumbLoaderAbstract
//

class CPluginInterfaceForThumbLoaderAbstract
{
#ifdef INSIDE_SALAMANDER
private: // protection against incorrect direct method calls (see CPluginInterfaceForThumbLoaderEncapsulation)
    friend class CPluginInterfaceForThumbLoaderEncapsulation;
#else  // INSIDE_SALAMANDER
public:
#endif // INSIDE_SALAMANDER

    // loads thumbnail for file 'filename'; 'thumbWidth' and 'thumbHeight' are
    // dimensions of the requested thumbnail; 'thumbMaker' is the interface to the algorithm for
    // creating thumbnails (can accept a finished thumbnail or create it by scaling down
    // the image); returns TRUE if the format of file 'filename' is known, if it returns
    // FALSE, Salamander will try to load the thumbnail using another plugin; errors when
    // obtaining the thumbnail (e.g., file read error) are reported by the plugin using
    // the 'thumbMaker' interface - see the SetError method; 'fastThumbnail' is TRUE in the first
    // round of reading thumbnails - the goal is to return a thumbnail as quickly as possible (even
    // in lower quality or smaller than needed), in the second round of reading thumbnails
    // (only if the SSTHUMB_ONLY_PREVIEW flag was set in the first round)
    // 'fastThumbnail' is FALSE - the goal is to return a quality thumbnail
    // limitation: since it is called from the icon loading thread (not the main thread), only
    // methods from CSalamanderGeneralAbstract that can be called from any thread can be used
    //
    // Recommended implementation schema:
    //   - try to open the image
    //   - if unsuccessful, return FALSE
    //   - extract image dimensions
    //   - pass them to Salamander via thumbMaker->SetParameters
    //   - if it returns FALSE, cleanup and exit (failed to allocate buffers)
    //   - LOOP
    //     - load part of the data from the image
    //     - send them to Salamander via thumbMaker->ProcessBuffer
    //     - if it returns FALSE, cleanup and exit (interruption due to path change)
    //     - continue in the LOOP until the entire image is passed
    //   - cleanup and exit
    virtual BOOL WINAPI LoadThumbnail(const char* filename, int thumbWidth, int thumbHeight,
                                      CSalamanderThumbnailMakerAbstract* thumbMaker,
                                      BOOL fastThumbnail) = 0;
};

#ifdef _MSC_VER
#pragma pack(pop, enter_include_spl_thum)
#endif // _MSC_VER
#ifdef __BORLANDC__
#pragma option -a
#endif // __BORLANDC__
