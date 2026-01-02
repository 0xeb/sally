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
#pragma pack(push, enter_include_spl_view) // to make structures independent of the set alignment
#pragma pack(4)
#endif // _MSC_VER
#ifdef __BORLANDC__
#pragma option -a4
#endif // __BORLANDC__

struct CSalamanderPluginViewerData;

//
// ****************************************************************************
// CPluginInterfaceForViewerAbstract
//

class CPluginInterfaceForViewerAbstract
{
#ifdef INSIDE_SALAMANDER
private: // protection against incorrect direct method calls (see CPluginInterfaceForViewerEncapsulation)
    friend class CPluginInterfaceForViewerEncapsulation;
#else  // INSIDE_SALAMANDER
public:
#endif // INSIDE_SALAMANDER

    // function for "file viewer", called when there is a request to open the viewer and load the file
    // 'name', 'left'+'right'+'width'+'height'+'showCmd'+'alwaysOnTop' is the recommended window
    // placement, if 'returnLock' is FALSE then 'lock'+'lockOwner' have no meaning, if 'returnLock'
    // is TRUE, the viewer should return a system-event 'lock' in nonsignaled state, 'lock' transitions
    // to signaled state when viewing of file 'name' ends (the file is removed from the temporary
    // directory at this moment), additionally it should return TRUE in 'lockOwner' if the 'lock' object
    // should be closed by the caller (FALSE means the viewer destroys 'lock' itself - in this case
    // the viewer must use CSalamanderGeneralAbstract::UnlockFileInCache method to transition 'lock'
    // to signaled state); if the viewer does not set 'lock' (remains NULL) the file 'name' is valid
    // only until the end of this ViewFile method call; if 'viewerData' is not NULL, it means extended
    // parameters are being passed to the viewer (see CSalamanderGeneralAbstract::ViewFileInPluginViewer);
    // 'enumFilesSourceUID' is the UID of the source (panel or Find window) from which the viewer is
    // being opened, if -1, the source is unknown (archives and file systems or Alt+F11, etc.) - see
    // e.g. CSalamanderGeneralAbstract::GetNextFileNameForViewer; 'enumFilesCurrentIndex' is the index
    // of the file being opened in the source (panel or Find window), if -1, the source or index is
    // unknown; returns TRUE on success (FALSE means failure, 'lock' and 'lockOwner' have no meaning
    // in this case)
    virtual BOOL WINAPI ViewFile(const char* name, int left, int top, int width, int height,
                                 UINT showCmd, BOOL alwaysOnTop, BOOL returnLock, HANDLE* lock,
                                 BOOL* lockOwner, CSalamanderPluginViewerData* viewerData,
                                 int enumFilesSourceUID, int enumFilesCurrentIndex) = 0;

    // function for "file viewer", called when there is a request to open the viewer and load the file
    // 'name'; this function should not display any windows like "invalid file format", these windows
    // will be displayed when calling the ViewFile method of this interface; determines whether the
    // file 'name' can be displayed (e.g. the file has a matching signature) in the viewer, and if so,
    // returns TRUE; if it returns FALSE, Salamander will try to find another viewer for 'name'
    // (in the priority list of viewers, see the Viewers configuration page)
    virtual BOOL WINAPI CanViewFile(const char* name) = 0;
};

#ifdef _MSC_VER
#pragma pack(pop, enter_include_spl_view)
#endif // _MSC_VER
#ifdef __BORLANDC__
#pragma option -a
#endif // __BORLANDC__
