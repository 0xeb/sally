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
#pragma pack(push, enter_include_spl_arc) // so that structures are independent of the alignment setting
#pragma pack(4)
#endif // _MSC_VER
#ifdef __BORLANDC__
#pragma option -a4
#endif // __BORLANDC__

class CSalamanderDirectoryAbstract;
class CSalamanderForOperationsAbstract;
class CPluginDataInterfaceAbstract;

//
// ****************************************************************************
// CPluginInterfaceForArchiverAbstract
//

class CPluginInterfaceForArchiverAbstract
{
#ifdef INSIDE_SALAMANDER
private: // protection against incorrect direct method calls (see CPluginInterfaceForArchiverEncapsulation)
    friend class CPluginInterfaceForArchiverEncapsulation;
#else  // INSIDE_SALAMANDER
public:
#endif // INSIDE_SALAMANDER

    // function for "panel archiver view"; called to load the contents of archive 'fileName';
    // contents are filled into the 'dir' object; Salamander retrieves the contents
    // of plugin-added columns using the 'pluginData' interface (if the plugin does not
    // add columns, 'pluginData'==NULL is returned); returns TRUE on successful loading of archive contents,
    // if it returns FALSE, the return value of 'pluginData' is ignored (data in 'dir' needs to be
    // released using 'dir.Clear(pluginData)', otherwise only the Salamander part of data is released);
    // 'salamander' is a set of useful methods exported from Salamander,
    // WARNING: the file fileName may also not exist (if it is open in the panel and deleted from elsewhere),
    // ListArchive is not called for zero-length files, they automatically have empty contents,
    // when packing into such files, the file is deleted before calling PackToArchive (for
    // compatibility with external packers)
    virtual BOOL WINAPI ListArchive(CSalamanderForOperationsAbstract* salamander, const char* fileName,
                                    CSalamanderDirectoryAbstract* dir,
                                    CPluginDataInterfaceAbstract*& pluginData) = 0;

    // function for "panel archiver view", called when extracting files/directories
    // from archive 'fileName' to directory 'targetDir' from path in archive 'archiveRoot'; 'pluginData'
    // is an interface for working with file/directory information that is plugin-specific
    // (e.g., data from added columns; this is the same interface returned by the ListArchive method
    // in parameter 'pluginData' - so it can also be NULL); files/directories are specified by enumeration
    // function 'next' whose parameter is 'nextParam'; returns TRUE on successful extraction (Cancel was not
    // used, Skip could have been used) - the source of the operation in the panel is deselected, otherwise returns
    // FALSE (deselection is not performed); 'salamander' is a set of useful methods exported from
    // Salamander
    virtual BOOL WINAPI UnpackArchive(CSalamanderForOperationsAbstract* salamander, const char* fileName,
                                      CPluginDataInterfaceAbstract* pluginData, const char* targetDir,
                                      const char* archiveRoot, SalEnumSelection next,
                                      void* nextParam) = 0;

    // function for "panel archiver view", called when extracting a single file for view/edit
    // from archive 'fileName' to directory 'targetDir'; the file name in the archive is 'nameInArchive';
    // 'pluginData' is an interface for working with file information that is plugin-specific
    // (e.g., data from added columns; this is the same interface returned by the ListArchive method
    // in parameter 'pluginData' - so it can also be NULL); 'fileData' is a pointer to the CFileData structure
    // of the file being extracted (the structure was built by the plugin when listing the archive); 'newFileName' (if not
    // NULL) is the new name for the file being extracted (used if the original name from the archive cannot be
    // extracted to disk (e.g., "aux", "prn", etc.)); write TRUE to 'renamingNotSupported' (only if 'newFileName'
    // is not NULL) if the plugin does not support renaming during extraction (the standard error message
    // "renaming not supported" will be displayed from Salamander); returns TRUE on successful file extraction
    // (the file is at the specified path, neither Cancel nor Skip was used), 'salamander' is a set of useful methods
    // exported from Salamander
    virtual BOOL WINAPI UnpackOneFile(CSalamanderForOperationsAbstract* salamander, const char* fileName,
                                      CPluginDataInterfaceAbstract* pluginData, const char* nameInArchive,
                                      const CFileData* fileData, const char* targetDir,
                                      const char* newFileName, BOOL* renamingNotSupported) = 0;

    // function for "panel archiver edit" and "custom archiver pack", called when packing
    // files/directories into archive 'fileName' at path 'archiveRoot', files/directories are specified by
    // source path 'sourcePath' and enumeration function 'next' with parameter 'nextParam',
    // if 'move' is TRUE, packed files/directories should be removed from disk, returns TRUE
    // if all files/directories are successfully packed/removed (Cancel was not used, Skip could have been
    // used) - the source of the operation in the panel is deselected, otherwise returns FALSE (deselection is not performed),
    // 'salamander' is a set of useful methods exported from Salamander
    virtual BOOL WINAPI PackToArchive(CSalamanderForOperationsAbstract* salamander, const char* fileName,
                                      const char* archiveRoot, BOOL move, const char* sourcePath,
                                      SalEnumSelection2 next, void* nextParam) = 0;

    // function for "panel archiver edit", called when deleting files/directories from archive
    // 'fileName'; files/directories are specified by path 'archiveRoot' and enumeration function 'next'
    // with parameter 'nextParam'; 'pluginData' is an interface for working with file/directory information
    // that is plugin-specific (e.g., data from added columns; this is the same interface returned by
    // the ListArchive method in parameter 'pluginData' - so it can also be NULL); returns TRUE if
    // all files/directories are successfully deleted (Cancel was not used, Skip could have been used) - the source
    // of the operation in the panel is deselected, otherwise returns FALSE (deselection is not performed); 'salamander' is a set
    // of useful methods exported from Salamander
    virtual BOOL WINAPI DeleteFromArchive(CSalamanderForOperationsAbstract* salamander, const char* fileName,
                                          CPluginDataInterfaceAbstract* pluginData, const char* archiveRoot,
                                          SalEnumSelection next, void* nextParam) = 0;

    // function for "custom archiver unpack"; called when requested to extract files/directories from archive
    // 'fileName' to directory 'targetDir'; files/directories are specified by mask 'mask'; returns TRUE if
    // all files/directories are successfully extracted (Cancel was not used, Skip could have been used);
    // if 'delArchiveWhenDone' is TRUE, all archive volumes need to be added to 'archiveVolumes'
    // (including the null-terminator; if not multi-volume, only 'fileName' will be there), if this function
    // returns TRUE (successful extraction), all files from 'archiveVolumes' will be subsequently deleted;
    // 'salamander' is a set of useful methods exported from Salamander
    virtual BOOL WINAPI UnpackWholeArchive(CSalamanderForOperationsAbstract* salamander, const char* fileName,
                                           const char* mask, const char* targetDir, BOOL delArchiveWhenDone,
                                           CDynamicString* archiveVolumes) = 0;

    // function for "panel archiver view/edit", called before closing the panel with the archive
    // WARNING: if opening a new path fails, the archive may remain in the panel (regardless of
    //          what CanCloseArchive returns); therefore this method cannot be used for context destruction;
    //          it is intended for example for optimizing the Delete operation from archive, when upon
    //          leaving it can offer "compacting" the archive
    //          for context destruction, use the CPluginInterfaceAbstract::ReleasePluginDataInterface method,
    //          see document archivatory.txt
    // 'fileName' is the archive name; 'salamander' is a set of useful methods exported from Salamander;
    // 'panel' indicates the panel in which the archive is open (PANEL_LEFT or PANEL_RIGHT);
    // returns TRUE if closing is possible, if 'force' is TRUE, always returns TRUE; if
    // critical shutdown is in progress (see CSalamanderGeneralAbstract::IsCriticalShutdown for more info),
    // there is no point in asking the user anything
    virtual BOOL WINAPI CanCloseArchive(CSalamanderForOperationsAbstract* salamander, const char* fileName,
                                        BOOL force, int panel) = 0;

    // retrieves the required disk-cache settings (disk-cache is used for temporary copies
    // of files when opening files from archive in viewers, editors, and through system
    // associations); normally (if allocating a copy of 'tempPath' succeeds after the call) it is called
    // only once before the first use of disk-cache (e.g., before first opening
    // a file from archive in viewer/editor); if it returns FALSE, the standard
    // settings are used (files in TEMP directory, copies are deleted using Win32
    // API function DeleteFile() when exceeding the cache size limit or when closing the archive)
    // and all other return values are ignored; if it returns TRUE, the following
    // return values are used: if 'tempPath' (buffer of size MAX_PATH) is not an empty string, all
    // temporary copies extracted by the plugin from the archive will be stored in subdirectories of this path
    // (these subdirectories are removed by disk-cache when Salamander exits, but nothing prevents the plugin
    // from deleting them earlier, e.g., during its unload; also it is recommended during the load of the first instance
    // of the plugin (not only within one running Salamander) to clean up "SAL*.tmp" subdirectories on this
    // path - solves problems caused by locked files and software crashes) + if 'ownDelete' is TRUE,
    // the DeleteTmpCopy and PrematureDeleteTmpCopy methods will be called for deleting copies + if
    // 'cacheCopies' is FALSE, copies will be deleted as soon as they are released (e.g., when the
    // viewer is closed), if 'cacheCopies' is TRUE, copies will be deleted when exceeding the cache size
    // limit or when closing the archive
    virtual BOOL WINAPI GetCacheInfo(char* tempPath, BOOL* ownDelete, BOOL* cacheCopies) = 0;

    // used only if the GetCacheInfo method returns TRUE in parameter 'ownDelete':
    // deletes the temporary copy extracted from this archive (beware of read-only files,
    // their attributes must be changed first, and only then can they be deleted), if possible
    // it should not display any windows (the user did not directly invoke the action, it may disturb them
    // during other activities), for longer actions it is useful to use a wait-window (see
    // CSalamanderGeneralAbstract::CreateSafeWaitWindow); 'fileName' is the name of the file
    // with the copy; if multiple files are deleted at once (may occur e.g., after closing
    // an edited archive), 'firstFile' is TRUE for the first file and FALSE for the other
    // files (used for correct display of the wait-window - see DEMOPLUG)
    //
    // WARNING: called in the main thread based on message delivery from disk-cache to the main window - a message
    // is sent about the need to release the temporary copy (typically when closing a viewer or
    // an "edited" archive in the panel), so re-entry into the plugin may occur
    // (if the message is distributed by a message-loop inside the plugin), further entry into DeleteTmpCopy
    // is excluded, because until the DeleteTmpCopy call ends, disk-cache does not send any further
    // messages
    virtual void WINAPI DeleteTmpCopy(const char* fileName, BOOL firstFile) = 0;

    // used only if the GetCacheInfo method returns TRUE in parameter 'ownDelete':
    // during plugin unload determines whether DeleteTmpCopy should be called for copies that are
    // still in use (e.g., open in viewer) - called only if such copies
    // exist; 'parent' is the parent of a possible messagebox with a question for the user (possibly
    // a recommendation for the user to close all files from the archive so the plugin can delete them);
    // 'copiesCount' is the number of used file copies from the archive; returns TRUE if
    // DeleteTmpCopy should be called, if it returns FALSE, copies will remain on disk; if
    // critical shutdown is in progress (see CSalamanderGeneralAbstract::IsCriticalShutdown for more info), there is no
    // point in asking the user anything and performing lengthy actions (e.g., file shredding)
    // NOTE: during execution of PrematureDeleteTmpCopy it is ensured that DeleteTmpCopy
    // will not be called
    virtual BOOL WINAPI PrematureDeleteTmpCopy(HWND parent, int copiesCount) = 0;
};

#ifdef _MSC_VER
#pragma pack(pop, enter_include_spl_arc)
#endif // _MSC_VER
#ifdef __BORLANDC__
#pragma option -a
#endif // __BORLANDC__
