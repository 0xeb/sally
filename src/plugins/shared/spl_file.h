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
#pragma pack(push, enter_include_spl_file) // to make structures independent of the alignment settings
#pragma pack(4)
#endif // _MSC_VER
#ifdef __BORLANDC__
#pragma option -a4
#endif // __BORLANDC__

//****************************************************************************
//
// CSalamanderSafeFileAbstract
//
// English summary: SafeFile method family provides robust file operations. The methods
// detect API error states and show appropriate error dialogs. Dialogs may include
// various button combinations (OK, Retry/Cancel, Retry/Skip/Skip all/Cancel),
// configured by the caller via parameters. During error handling, methods need the
// file name and original CreateFile parameters to be able to close and reopen the
// handle, reposition the pointer, and retry the operation. Therefore, SafeFileRead
// and SafeFileWrite may change SAFE_FILE::HFile while recovering from errors. For
// this reason a dedicated SAFE_FILE structure is used to hold operation context.
// The optional 'silentMask' bit-field allows suppressing prompts (Skip all / Overwrite all)
// and records user choices across a group of operations.
//

struct SAFE_FILE
{
    HANDLE HFile;                // handle of the opened file (note: managed by Salamander core HANDLES)
    char* FileName;              // name of the opened file with full path
    HWND HParentWnd;             // hParent window handle from SafeFileOpen/SafeFileCreate call; used
                                 // when hParent in subsequent calls is set to HWND_STORED
    DWORD dwDesiredAccess;       // > backup of CreateFile API parameters
    DWORD dwShareMode;           // > for potential retry calls
    DWORD dwCreationDisposition; // > in case of errors during read or write
    DWORD dwFlagsAndAttributes;  // >
    BOOL WholeFileAllocated;     // TRUE if SafeFileCreate pre-allocated the entire file
};

#define HWND_STORED ((HWND) - 1)

#define SAFE_FILE_CHECK_SIZE 0x00010000 // FIXME: verify it doesn't conflict with BUTTONS_xxx

// silentMask bit definitions
// skip section
#define SILENT_SKIP_FILE_NAMEUSED 0x00000001 // skips files that cannot be created because a directory \
                                             // with the same name already exists (old CNFRM_MASK_NAMEUSED)
#define SILENT_SKIP_DIR_NAMEUSED 0x00000002  // skips directories that cannot be created because a file \
                                             // with the same name already exists (old CNFRM_MASK_NAMEUSED)
#define SILENT_SKIP_FILE_CREATE 0x00000004   // skips files that cannot be created for other reasons (old CNFRM_MASK_ERRCREATEFILE)
#define SILENT_SKIP_DIR_CREATE 0x00000008    // skips directories that cannot be created for other reasons (old CNFRM_MASK_ERRCREATEDIR)
#define SILENT_SKIP_FILE_EXIST 0x00000010    // skips files that already exist (old CNFRM_MASK_FILEOVERSKIP) \
                                             // mutually exclusive with SILENT_OVERWRITE_FILE_EXIST
#define SILENT_SKIP_FILE_SYSHID 0x00000020   // skips System/Hidden files that already exist (old CNFRM_MASK_SHFILEOVERSKIP) \
                                             // mutually exclusive with SILENT_OVERWRITE_FILE_SYSHID
#define SILENT_SKIP_FILE_READ 0x00000040     // skips files where a read error occurred
#define SILENT_SKIP_FILE_WRITE 0x00000080    // skips files where a write error occurred
#define SILENT_SKIP_FILE_OPEN 0x00000100     // skips files that cannot be opened

// overwrite section
#define SILENT_OVERWRITE_FILE_EXIST 0x00001000  // overwrites files that already exist (old CNFRM_MASK_FILEOVERYES) \
                                                // mutually exclusive with SILENT_SKIP_FILE_EXIST
#define SILENT_OVERWRITE_FILE_SYSHID 0x00002000 // overwrites System/Hidden files that already exist (old CNFRM_MASK_SHFILEOVERYES) \
                                                // mutually exclusive with SILENT_SKIP_FILE_SYSHID
#define SILENT_RESERVED_FOR_PLUGINS 0xFFFF0000  // this space is available for plugins to use for their own flags

class CSalamanderSafeFileAbstract
{
public:
    //
    // SafeFileOpen
    //   Opens an existing file.
    //
    // Parameters
    //   'file'
    //      [out] Pointer to a 'SAFE_FILE' structure that receives information about
    //      the opened file. This structure serves as context for other methods in the
    //      SafeFile family. The structure values are meaningful only if SafeFileOpen
    //      returned TRUE. To close the file, call the SafeFileClose method.
    //
    //   'fileName'
    //      [in] Pointer to a null-terminated string containing the name of the file
    //      to open.
    //
    //   'dwDesiredAccess'
    //   'dwShareMode'
    //   'dwCreationDisposition'
    //   'dwFlagsAndAttributes'
    //      [in] see CreateFile API.
    //
    //   'hParent'
    //      [in] Handle of the window to which error dialogs will be displayed modally.
    //
    //   'flags'
    //      [in] One of the BUTTONS_xxx values, determines the buttons displayed in error dialogs.
    //
    //   'pressedButton'
    //      [out] Pointer to a variable that receives the button pressed during the error
    //      dialog. The variable is meaningful only if SafeFileOpen returns FALSE,
    //      otherwise its value is undefined. Returns one of the DIALOG_xxx values.
    //      In case of errors, returns DIALOG_CANCEL.
    //      If an error dialog is suppressed due to 'silentMask', returns the value
    //      of the corresponding button (e.g., DIALOG_SKIP or DIALOG_YES).
    //
    //      'pressedButton' can be NULL (e.g., for BUTTONS_OK or BUTTONS_RETRYCANCEL
    //      there is no point in testing the pressed button).
    //
    //   'silentMask'
    //      [in/out] Pointer to a variable containing a bit field of SILENT_xxx values.
    //      For the SafeFileOpen method, only SILENT_SKIP_FILE_OPEN is meaningful.
    //
    //      If the SILENT_SKIP_FILE_OPEN bit is set in the bit field, and the displayed
    //      dialog would have a Skip button (controlled by 'flags'), and an error occurs
    //      during file opening, the error dialog will be suppressed.
    //      SafeFileOpen will then return FALSE and if 'pressedButton' is not NULL,
    //      it will be set to DIALOG_SKIP.
    //
    // Return Values
    //   Returns TRUE if the file was successfully opened. The 'file' structure is initialized
    //   and SafeFileClose must be called to close the file.
    //
    //   In case of error, returns FALSE and sets the values of 'pressedButton'
    //   and 'silentMask' if they are not NULL.
    //
    // Remarks
    //   This method can be called from any thread.
    //
    virtual BOOL WINAPI SafeFileOpen(SAFE_FILE* file,
                                     const char* fileName,
                                     DWORD dwDesiredAccess,
                                     DWORD dwShareMode,
                                     DWORD dwCreationDisposition,
                                     DWORD dwFlagsAndAttributes,
                                     HWND hParent,
                                     DWORD flags,
                                     DWORD* pressedButton,
                                     DWORD* silentMask) = 0;

    //
    // SafeFileCreate
    //   Creates a new file including its path if it doesn't exist. If the file already exists,
    //   offers to overwrite it. This method is primarily intended for creating files and directories
    //   extracted from archives.
    //
    // Parameters
    //   'fileName'
    //      [in] Pointer to a null-terminated string specifying the name of the file
    //      to create.
    //
    //   'dwDesiredAccess'
    //   'dwShareMode'
    //   'dwFlagsAndAttributes'
    //      [in] see CreateFile API.
    //
    //   'isDir'
    //      [in] Specifies whether the last component of 'fileName' path should be a directory (TRUE)
    //      or a file (FALSE). If 'isDir' is TRUE, the variables 'dwDesiredAccess', 'dwShareMode',
    //      'dwFlagsAndAttributes', 'srcFileName', 'srcFileInfo' and 'file' are ignored.
    //
    //   'hParent'
    //      [in] Handle of the window to which error dialogs will be displayed modally.
    //
    //   'srcFileName'
    //      [in] Pointer to a null-terminated string specifying the name of the source file.
    //      This name will be displayed together with size and time ('srcFileInfo') in the
    //      overwrite confirmation dialog if file 'fileName' already exists.
    //      'srcFileName' can be NULL, then 'srcFileInfo' is ignored.
    //      In this case, the overwrite confirmation dialog will show "a newly created file"
    //      in place of the source file.
    //
    //   'srcFileInfo'
    //      [in] Pointer to a null-terminated string containing the size, date and time of
    //      the source file. This information will be displayed together with the source file
    //      name 'srcFileName' in the overwrite confirmation dialog.
    //      Format: "size, date, time".
    //      Size is obtained using CSalamanderGeneralAbstract::NumberToStr,
    //      date using GetDateFormat(LOCALE_USER_DEFAULT, DATE_SHORTDATE, ...
    //      and time using GetTimeFormat(LOCALE_USER_DEFAULT, 0, ...
    //      See implementation of GetFileInfo method in the UnFAT plugin.
    //      'srcFileInfo' can be NULL if 'srcFileName' is also NULL.
    //
    //    'silentMask'
    //      [in/out] Pointer to a bit field composed of SILENT_SKIP_xxx and SILENT_OVERWRITE_xxx,
    //      see the introduction at the beginning of this file. If 'silentMask' is NULL, it is ignored.
    //      The SafeFileCreate method tests and sets these constants:
    //        SILENT_SKIP_FILE_NAMEUSED
    //        SILENT_SKIP_DIR_NAMEUSED
    //        SILENT_OVERWRITE_FILE_EXIST
    //        SILENT_SKIP_FILE_EXIST
    //        SILENT_OVERWRITE_FILE_SYSHID
    //        SILENT_SKIP_FILE_SYSHID
    //        SILENT_SKIP_DIR_CREATE
    //        SILENT_SKIP_FILE_CREATE
    //
    //      If 'srcFileName' is not NULL, i.e., this is a COPY/MOVE operation, then:
    //        If "Confirm on file overwrite" is disabled in Salamander configuration (Confirmations page),
    //        the method behaves as if 'silentMask' contained SILENT_OVERWRITE_FILE_EXIST.
    //        If "Confirm on system or hidden file overwrite" is disabled, the method behaves
    //        as if 'silentMask' contained SILENT_OVERWRITE_FILE_SYSHID.
    //
    //    'allowSkip'
    //      [in] Specifies whether dialogs and error messages will also include "Skip"
    //      and "Skip all" buttons.
    //
    //    'skipped'
    //      [out] Returns TRUE if the user clicked the "Skip" or "Skip all" button in a dialog
    //      or error message. Otherwise returns FALSE. The 'skipped' variable can be NULL.
    //      The variable is meaningful only if SafeFileCreate returns INVALID_HANDLE_VALUE.
    //
    //    'skipPath'
    //      [out] Pointer to a buffer that receives the path that the user wanted to skip
    //      using the "Skip" or "Skip all" button in one of the dialogs. The buffer size is
    //      given by skipPathMax variable, which will not be exceeded. The path will be null-terminated.
    //      At the beginning of SafeFileCreate method, the buffer is set to an empty string.
    //      'skipPath' can be NULL, 'skipPathMax' is then ignored.
    //
    //    'skipPathMax'
    //      [in] Size of the 'skipPath' buffer in characters. Must be set if 'skipPath'
    //      is not NULL.
    //
    //    'allocateWholeFile'
    //      [in/out] Pointer to a CQuadWord specifying the size to which the file should be
    //      pre-allocated using SetEndOfFile. If the pointer is NULL, it is ignored and
    //      SafeFileCreate will not attempt pre-allocation. If the pointer is not NULL,
    //      the function will attempt pre-allocation. The requested size must be greater than
    //      CQuadWord(2, 0) and less than CQuadWord(0, 0x80000000) (8EB).
    //
    //      If SafeFileCreate should also perform a test (the pre-allocation mechanism may not
    //      always work), the highest bit of the size must be set, i.e., add
    //      CQuadWord(0, 0x80000000) to the value.
    //
    //      If the file is successfully created (SafeFileCreate returns a handle other than
    //      INVALID_HANDLE_VALUE), the 'allocateWholeFile' variable will be set to one of
    //      the following values:
    //       CQuadWord(0, 0x80000000): file could not be pre-allocated and during the next
    //                                 call to SafeFileCreate for files to the same destination,
    //                                 'allocateWholeFile' should be NULL
    //       CQuadWord(0, 0):          file could not be pre-allocated, but it's not fatal
    //                                 and in subsequent calls to SafeFileCreate for files
    //                                 to this destination, you can request pre-allocation
    //       other:                    pre-allocation completed correctly
    //                                 In this case, SAFE_FILE::WholeFileAllocated is set to TRUE
    //                                 and during SafeFileClose, SetEndOfFile will be called to
    //                                 truncate the file and prevent storing unnecessary data.
    //
    //    'file'
    //      [out] Pointer to a 'SAFE_FILE' structure that receives information about the opened
    //      file. This structure serves as context for other methods in the SafeFile family.
    //      The structure values are meaningful only if SafeFileCreate returned a value other
    //      than INVALID_HANDLE_VALUE. To close the file, call SafeFileClose method. If 'file'
    //      is not NULL, SafeFileCreate adds the created handle to Salamander's HANDLES. If 'file'
    //      is NULL, the handle will not be added to HANDLES. If 'isDir' is TRUE, the 'file'
    //      variable is ignored.
    //
    // Return Values
    //   If 'isDir' is TRUE, returns a value other than INVALID_HANDLE_VALUE on success.
    //   Note: this is not a valid handle of the created directory. On failure, returns
    //   INVALID_HANDLE_VALUE and sets the 'silentMask', 'skipped' and 'skipPath' variables.
    //
    //   If 'isDir' is FALSE, returns the handle of the created file on success and if
    //   'file' is not NULL, fills the SAFE_FILE structure.
    //   On failure, returns INVALID_HANDLE_VALUE and sets the 'silentMask', 'skipped'
    //   and 'skipPath' variables.
    //
    // Remarks
    //   This method can only be called from the main thread. (It may call FlashWindow(MainWindow) API,
    //   which must be called from the window's thread, otherwise it causes a deadlock)
    //
    virtual HANDLE WINAPI SafeFileCreate(const char* fileName,
                                         DWORD dwDesiredAccess,
                                         DWORD dwShareMode,
                                         DWORD dwFlagsAndAttributes,
                                         BOOL isDir,
                                         HWND hParent,
                                         const char* srcFileName,
                                         const char* srcFileInfo,
                                         DWORD* silentMask,
                                         BOOL allowSkip,
                                         BOOL* skipped,
                                         char* skipPath,
                                         int skipPathMax,
                                         CQuadWord* allocateWholeFile,
                                         SAFE_FILE* file) = 0;

    //
    // SafeFileClose
    //   Closes the file and frees allocated data in the 'file' structure.
    //
    // Parameters
    //   'file'
    //      [in] Pointer to a 'SAFE_FILE' structure that was initialized by a successful
    //      call to SafeFileCreate or SafeFileOpen method.
    //
    // Remarks
    //   This method can be called from any thread.
    //
    virtual void WINAPI SafeFileClose(SAFE_FILE* file) = 0;

    //
    // SafeFileSeek
    //   Sets the file pointer position in an open file.
    //
    // Parameters
    //   'file'
    //      [in] Pointer to a 'SAFE_FILE' structure that was initialized
    //      by calling SafeFileOpen or SafeFileCreate method.
    //
    //   'distance'
    //      [in/out] Number of bytes by which to move the file pointer.
    //      On success, receives the value of the new pointer position.
    //
    //      The CQuadWord::Value is interpreted as signed for all three 'moveMethod'
    //      values (note the error in MSDN for SetFilePointerEx, which states that
    //      the value is unsigned for FILE_BEGIN). Therefore, if we want to move
    //      backward from the current position (FILE_CURRENT) or from the end (FILE_END),
    //      we set CQuadWord::Value to a negative number. You can directly assign
    //      for example __int64 to CQuadWord::Value.
    //
    //      The returned value is the absolute position from the beginning of the file
    //      and its values range from 0 to 2^63. Files over 2^63 are not supported by
    //      any current Windows version.
    //
    //   'moveMethod'
    //      [in] Starting position for the pointer. Can be one of the values:
    //           FILE_BEGIN, FILE_CURRENT or FILE_END.
    //
    //   'error'
    //      [out] Pointer to a DWORD variable that will contain the value returned
    //      from GetLastError() in case of error. 'error' can be NULL.
    //
    // Return Values
    //   On success, returns TRUE and the 'distance' variable is set to the new
    //   file pointer position.
    //
    //   On error, returns FALSE and sets 'error' to GetLastError value if 'error'
    //   is not NULL. Does not display the error; use SafeFileSeekMsg for that.
    //
    // Remarks
    //   The method calls SetFilePointer API, so its limitations apply.
    //
    //   It is not an error to set the pointer past the end of the file. The file size
    //   does not increase until you call SetEndOfFile or SafeFileWrite. See SetFilePointer API.
    //
    //   This method can be used to get the file size by setting 'distance' to 0
    //   and 'moveMethod' to FILE_END. The returned 'distance' value will be the file size.
    //
    //   This method can be called from any thread.
    //
    virtual BOOL WINAPI SafeFileSeek(SAFE_FILE* file,
                                     CQuadWord* distance,
                                     DWORD moveMethod,
                                     DWORD* error) = 0;

    //
    // SafeFileSeekMsg
    //   Sets the file pointer position in an open file. Displays an error if one occurs.
    //
    // Parameters
    //   'file'
    //   'distance'
    //   'moveMethod'
    //      See SafeFileSeek comment.
    //
    //   'hParent'
    //      [in] Handle of the window to which error dialogs will be displayed modally.
    //      If equal to HWND_STORED, uses 'hParent' from SafeFileOpen/SafeFileCreate call.
    //
    //   'flags'
    //      [in] One of the BUTTONS_xxx values, determines the buttons displayed in the error dialog.
    //
    //   'pressedButton'
    //      [out] Pointer to a variable that receives the button pressed during the error
    //      dialog. The variable is meaningful only if SafeFileSeekMsg returns FALSE.
    //      'pressedButton' can be NULL (e.g., for BUTTONS_OK there is no point in testing
    //      the pressed button)
    //
    //   'silentMask'
    //      [in/out] Pointer to a variable containing a bit field of SILENT_SKIP_xxx values.
    //      See SafeFileOpen comment for details.
    //      SafeFileSeekMsg tests and sets the SILENT_SKIP_FILE_READ bit if 'seekForRead'
    //      is TRUE, or SILENT_SKIP_FILE_WRITE if 'seekForRead' is FALSE.
    //
    //   'seekForRead'
    //      [in] Tells the method the purpose of the seek operation. The method uses this
    //      variable only in case of error. Determines which bit is used for 'silentMask'
    //      and what the error dialog title will be: "Error Reading File" or "Error Writing File".
    //
    // Return Values
    //   On success, returns TRUE and the 'distance' variable is set to the new
    //   file pointer position.
    //
    //   On error, returns FALSE and sets the values of 'pressedButton' and 'silentMask'
    //   if they are not NULL.
    //
    // Remarks
    //   See SafeFileSeek method.
    //
    //   This method can be called from any thread.
    //
    virtual BOOL WINAPI SafeFileSeekMsg(SAFE_FILE* file,
                                        CQuadWord* distance,
                                        DWORD moveMethod,
                                        HWND hParent,
                                        DWORD flags,
                                        DWORD* pressedButton,
                                        DWORD* silentMask,
                                        BOOL seekForRead) = 0;

    //
    // SafeFileGetSize
    //   Returns the file size.
    //
    //   'file'
    //      [in] Pointer to a 'SAFE_FILE' structure that was initialized
    //      by calling SafeFileOpen or SafeFileCreate method.
    //
    //   'lpBuffer'
    //      [out] Pointer to a CQuadWord structure that receives the file size.
    //
    //   'error'
    //      [out] Pointer to a DWORD variable that will contain the value returned
    //      from GetLastError() in case of error. 'error' can be NULL.
    //
    // Return Values
    //   On success, returns TRUE and sets the 'fileSize' variable.
    //   On error, returns FALSE and sets the 'error' variable value if not NULL.
    //
    // Remarks
    //   This method can be called from any thread.
    //
    virtual BOOL WINAPI SafeFileGetSize(SAFE_FILE* file,
                                        CQuadWord* fileSize,
                                        DWORD* error) = 0;

    //
    // SafeFileRead
    //   Reads data from the file starting at the file pointer position. After the operation
    //   completes, the pointer is moved by the number of bytes read. The method supports only
    //   synchronous reading, i.e., it does not return until the data is read or an error occurs.
    //
    // Parameters
    //   'file'
    //      [in] Pointer to a 'SAFE_FILE' structure that was initialized
    //      by calling SafeFileOpen or SafeFileCreate method.
    //
    //   'lpBuffer'
    //      [out] Pointer to a buffer that receives the data read from the file.
    //
    //   'nNumberOfBytesToRead'
    //      [in] Specifies how many bytes to read from the file.
    //
    //   'lpNumberOfBytesRead'
    //      [out] Pointer to a variable that receives the number of bytes actually read into the buffer.
    //
    //   'hParent'
    //      [in] Handle of the window to which error dialogs will be displayed modally.
    //      If equal to HWND_STORED, uses 'hParent' from SafeFileOpen/SafeFileCreate call.
    //
    //   'flags'
    //      [in] One of the BUTTONS_xxx values optionally combined with SAFE_FILE_CHECK_SIZE, determines
    //      the buttons displayed in error dialogs. If the SAFE_FILE_CHECK_SIZE bit is set, SafeFileRead
    //      considers it an error if it fails to read the requested number of bytes and displays an
    //      error dialog. Without this bit, it behaves the same as ReadFile API.
    //
    //   'pressedButton'
    //   'silentMask'
    //      See SafeFileOpen.
    //
    // Return Values
    //   On success, returns TRUE and the 'lpNumberOfBytesRead' variable is set to the number
    //   of bytes read.
    //
    //   On error, returns FALSE and sets the values of 'pressedButton' and 'silentMask'
    //   if they are not NULL.
    //
    // Remarks
    //   This method can be called from any thread.
    //
    virtual BOOL WINAPI SafeFileRead(SAFE_FILE* file,
                                     LPVOID lpBuffer,
                                     DWORD nNumberOfBytesToRead,
                                     LPDWORD lpNumberOfBytesRead,
                                     HWND hParent,
                                     DWORD flags,
                                     DWORD* pressedButton,
                                     DWORD* silentMask) = 0;

    //
    // SafeFileWrite
    //   Writes data to the file starting at the file pointer position. After the operation
    //   completes, the pointer is moved by the number of bytes written. The method supports only
    //   synchronous writing, i.e., it does not return until the data is written or an error occurs.
    //
    // Parameters
    //   'file'
    //      [in] Pointer to a 'SAFE_FILE' structure that was initialized
    //      by calling SafeFileOpen or SafeFileCreate method.
    //
    //   'lpBuffer'
    //      [in] Pointer to a buffer containing data to be written to the file.
    //
    //   'nNumberOfBytesToWrite'
    //      [in] Specifies how many bytes to write from the buffer to the file.
    //
    //   'lpNumberOfBytesWritten'
    //      [out] Pointer to a variable that receives the number of bytes actually written.
    //
    //   'hParent'
    //      [in] Handle of the window to which error dialogs will be displayed modally.
    //      If equal to HWND_STORED, uses 'hParent' from SafeFileOpen/SafeFileCreate call.
    //
    //   'flags'
    //      [in] One of the BUTTONS_xxx values, determines the buttons displayed in error dialogs.
    //
    //   'pressedButton'
    //   'silentMask'
    //      See SafeFileOpen.
    //
    // Return Values
    //   On success, returns TRUE and the 'lpNumberOfBytesWritten' variable is set to the number
    //   of bytes written.
    //
    //   On error, returns FALSE and sets the values of 'pressedButton' and 'silentMask'
    //   if they are not NULL.
    //
    // Remarks
    //   This method can be called from any thread.
    //
    virtual BOOL WINAPI SafeFileWrite(SAFE_FILE* file,
                                      LPVOID lpBuffer,
                                      DWORD nNumberOfBytesToWrite,
                                      LPDWORD lpNumberOfBytesWritten,
                                      HWND hParent,
                                      DWORD flags,
                                      DWORD* pressedButton,
                                      DWORD* silentMask) = 0;
};

#ifdef _MSC_VER
#pragma pack(pop, enter_include_spl_file)
#endif // _MSC_VER
#ifdef __BORLANDC__
#pragma option -a
#endif // __BORLANDC__
