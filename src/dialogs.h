// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <string>

//
// ****************************************************************************

class CSelectDialog : public CCommonDialog
{
public:
    CSelectDialog(HINSTANCE modul, int resID, UINT helpID, HWND parent, std::wstring& mask,
                  CObjectOrigin origin = ooStandard)
        : CCommonDialog(modul, resID, helpID, parent, origin), Mask(mask) {}

    virtual void Validate(CTransferInfo& ti);
    virtual void Transfer(CTransferInfo& ti);

protected:
    virtual INT_PTR DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam);

protected:
    std::wstring& Mask;
};

//
// ****************************************************************************

class CCopyMoveDialog : public CCommonDialog
{
protected:
    const wchar_t* Title;
    std::wstring& Path;
    CTruncatedString* Subject;
    wchar_t** History;
    int HistoryCount;
    BOOL DirectoryHelper;
    int SelectionEnd;

    // Owned; freed on WM_DESTROY. The caller-owned std::wstring is the sole path state.
    HFONT UnicodeFont;

public:
    // 'history' determines whether the dialog will contain a combobox (TRUE) or an editline (FALSE)
    // 'directoryHelper' specifies if a resource with a button behind the editline will be used to select a directory
    // 'selectionEnd' specifies up to which character the name is selected (used for quick rename), -1 == all
    // 'titleW' is GONE - 'title' is wide itself now. No caller ever supplied
    // titleW, so the caption was taking the SetWindowTextA fallback and narrowing through
    // CP_ACP on a window created unicodeWnd=TRUE. It is SetWindowTextW unconditionally now.
    //
    CCopyMoveDialog(HWND parent, std::wstring& path, const wchar_t* title,
                    CTruncatedString* subject, DWORD helpID,
                    wchar_t* history[], int historyCount, BOOL directoryHelper);

    void SetSelectionEnd(int selectionEnd);
    virtual void Transfer(CTransferInfo& ti);

protected:
    virtual INT_PTR DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam);
};

class CEditNewFileDialog : public CCopyMoveDialog
{
public:
    CEditNewFileDialog(HWND parent, std::wstring& path, CTruncatedString* subject,
                       wchar_t* history[], int historyCount);

protected:
    virtual INT_PTR DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam);
};

//
// ****************************************************************************

class CCriteriaData;
class CButton;

class CCopyMoveMoreDialog : public CCommonDialog
{
protected:
    const wchar_t* Title;
    std::wstring& Path;
    CTruncatedString* Subject;
    wchar_t** History;
    int HistoryCount;
    CCriteriaData* CriteriaInOut; // used to transfer data in and out of the dialog (on OK)
    CCriteriaData* Criteria;      // allocated because static declaration would require juggling headers
    BOOL HavePermissions;
    BOOL SupportsADS;

    int OriginalWidth;    // full dialog width
    int OriginalHeight;   // full dialog height
    int OriginalButtonsY; // Y position of the buttons in client coordinates
    int SpacerHeight;     // spacer used when shrinking/expanding the dialog
    BOOL Expanded;        // is the dialog currently expanded?

    CButton* MoreButton;

    // Owned; freed on WM_DESTROY. The caller-owned std::wstring is the sole path state.
    HFONT UnicodeFont;

public:
    // 'history' determines whether the dialog will contain a combobox (TRUE) or an editline (FALSE)
    // 'directoryHelper' specifies if a resource with a button behind the editline will be used to select a directory
    CCopyMoveMoreDialog(HWND parent, std::wstring& path, const wchar_t* title,
                        CTruncatedString* subject, DWORD helpID,
                        wchar_t* history[], int historyCount, CCriteriaData* criteriaInOut,
                        BOOL havePermissions, BOOL supportsADS);
    ~CCopyMoveMoreDialog();

    virtual void Validate(CTransferInfo& ti);
    virtual void Transfer(CTransferInfo& ti);

protected:
    virtual INT_PTR DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam);

    void SetOptionsButtonState(BOOL more);
    void DisplayMore(BOOL more, BOOL fast); // fast means everything is freshly initialized and we don't need to reset values
    HDWP OffsetControl(HDWP hdwp, int id, int yOffset);
    void EnableControls();
    void TransferCriteriaControls(CTransferInfo& ti);
    void UpdateAdvancedText();
};

//
// ****************************************************************************

#define MESSAGEBOX_MAXBUTTONS 4 // maximum number of buttons

class CMessageBox : public CCommonDialog
{
protected:
    // wide-primary: every string the box renders is UTF-16 and the
    // dialog itself is created wide, so nothing narrows on the way to the screen.
    DWORD Flags;
    std::wstring Title;
    std::wstring CheckText;
    CTruncatedString Text;
    BOOL* Check;
    HICON HOwnIcon;
    MSGBOXEX_CALLBACK HelpCallback;
    std::wstring AliasBtnNames;
    std::wstring URL;
    std::wstring URLText;
    // for WM_COPY:
    int ButtonsID[MESSAGEBOX_MAXBUTTONS]; // IDs of the buttons after remapping
    int BackgroundSeparator;              // Y offset dividing white/gray (Vista+)

public:
    CMessageBox(HWND parent, DWORD flags, const wchar_t* title, const wchar_t* text,
                const wchar_t* checkText, BOOL* check, HICON hOwnIcon,
                DWORD contextHelpId, MSGBOXEX_CALLBACK helpCallback,
                const wchar_t* aliasBtnNames, const wchar_t* url, const wchar_t* urlText);

    CMessageBox(HWND parent, DWORD flags, const wchar_t* title, CTruncatedString* text,
                const wchar_t* checkText, BOOL* check, HICON hOwnIcon,
                DWORD contextHelpId, MSGBOXEX_CALLBACK helpCallback,
                const wchar_t* aliasBtnNames, const wchar_t* url, const wchar_t* urlText);

    ~CMessageBox();

    virtual void Transfer(CTransferInfo& ti);

    int Execute();

    BOOL CopyToClipboard();

protected:
    virtual INT_PTR DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam);
    BOOL EscapeEnabled();
};

//
// ****************************************************************************

class CChangeAttrDialog : public CCommonDialog
{
private:
    // handles for the TimeDate controls
    HWND HModifiedDate;
    HWND HModifiedTime;
    HWND HCreatedDate;
    HWND HCreatedTime;
    HWND HAccessedDate;
    HWND HAccessedTime;

    // state variables used to disable checkboxes
    BOOL SelectionContainsDirectory;
    BOOL FileBasedCompression;
    BOOL FileBasedEncryption;

    // variable is set to TRUE when the user clicks the corresponding checkbox
    BOOL ArchiveDirty;
    BOOL ReadOnlyDirty;
    BOOL HiddenDirty;
    BOOL SystemDirty;
    BOOL CompressedDirty;
    BOOL EncryptedDirty;

public:
    int Archive,
        ReadOnly,
        Hidden,
        System,
        Compressed,
        Encrypted,
        ChangeTimeModified,
        ChangeTimeCreated,
        ChangeTimeAccessed,
        RecurseSubDirs;

    SYSTEMTIME TimeModified;
    SYSTEMTIME TimeCreated;
    SYSTEMTIME TimeAccessed;

    CChangeAttrDialog(HWND parent, DWORD attr, DWORD attrDiff,
                      BOOL selectedDirectory, BOOL fileBasedCompression,
                      BOOL fileBasedEncryption,
                      const SYSTEMTIME* timeModified,
                      const SYSTEMTIME* timeCreated,
                      const SYSTEMTIME* timeAccessed);

    virtual void Transfer(CTransferInfo& ti);

    void EnableWindows();

protected:
    virtual INT_PTR DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam);

    BOOL GetAndValidateTime(CTransferInfo* ti, int resIDDate, int resIDTime, SYSTEMTIME* time);
};

//******************************************************************************
//
// CProgressDlgArray
//

struct CProgressDlgArrItem
{
    HANDLE DlgThread; // handle of the dialog thread (may also be a handle of a terminated dialog thread)
    HWND DlgWindow;   // handle of the dialog window (NULL = the dialog has already closed)

    CProgressDlgArrItem()
    {
        DlgThread = NULL;
        DlgWindow = NULL;
    }
};

class CProgressDlgArray
{
protected:
    CRITICAL_SECTION Monitor;                 // section used to synchronize this object (behaves like a monitor)
    TIndirectArray<CProgressDlgArrItem> Dlgs; // array of operation dialogs (only those running in their own threads)

public:
    CProgressDlgArray();
    ~CProgressDlgArray();

    // allocates and inserts a structure for a new dialog into the array (data are filled in outside)
    // returns NULL on insufficient memory, otherwise returns the requested structure
    // call only from the main thread (otherwise a conflict with cleanup of finished threads may occur)
    CProgressDlgArrItem* PrepareNewDlg();

    // within the array's critical section set data in 'dlg' according to 'dlgThread' and 'dlgWindow'
    // data change only to values different from NULL (parameter NULL = no change)
    void SetDlgData(CProgressDlgArrItem* dlg, HANDLE dlgThread, HWND dlgWindow);

    // removes the 'dlg' structure from the array; 'dlg->DlgThread' must be NULL;
    // call only if starting the dialog for which 'dlg' was acquired via
    // PrepareNewDlg() function failed
    void RemoveDlg(CProgressDlgArrItem* dlg);

    // removes all dialogs whose threads have already finished from the array (closes their handles);
    // returns the number of still running operation dialog threads (so when it returns zero,
    // for example Salamander can be terminated)
    int RemoveFinishedDlgs();

    // finds a dialog with window 'hdlg' in the array and stores NULL to its 'DlgWindow'
    // (this way the dialog reports it is closing; the dialog thread should end shortly after)
    void ClearDlgWindow(HWND hdlg);

    // returns the next open dialog; if no dialog is open, returns NULL;
    // before the first call set 'index' to 0, use the returned value of 'index'
    // for subsequent calls (do not touch 'index' between calls); dialogs are returned
    // in cycles (after the last one it returns to the first)
    HWND GetNextOpenedDlg(int* index);

    // ensures all open dialogs are closed
    // call only from the main thread (otherwise another dialog might open and it won't
    // know it should terminate)
    void PostCancelToAllDlgs();

    // sends a message to all dialogs that the icon (color) has changed and needs
    // to be set again; call only from the main thread
    void PostIconChange();
};

//******************************************************************************
//
// CProgressDialog
//

struct CChangeAttrsData;
struct CConvertData;
class COperations;
class CStaticText;
class CProgressBar;
struct CStartProgressDialogData;

// returns FALSE if the progress dialog could not be opened in the new thread or
// if starting an operation in the worker thread failed in this dialog; when FALSE
// is returned the caller must free the script 'script' manually (otherwise the script
// is freed after the operation in the worker thread finishes)
BOOL StartProgressDialog(COperations* script, const wchar_t* caption,
                         CChangeAttrsData* attrsData, CConvertData* convertData);

class CProgressDialog : public CCommonDialog
{
public:
    CProgressDialog(HWND parent, COperations* script, const wchar_t* caption,
                    CChangeAttrsData* attrsData, CConvertData* convertData,
                    BOOL runningInOwnThread, CStartProgressDialogData* progrDlgData);
    ~CProgressDialog();

protected:
    virtual INT_PTR DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam);

    BOOL FlushCachedData(); // sends modified data to statics and progress bars; returns TRUE if there was something to update (something was dirty)

    void SetDlgTitle(BOOL minimized);
    void SetWindowIcon();

protected:
    BOOL RunningInOwnThread;                // TRUE/FALSE = dialog runs in its own thread ("background") / dialog runs in the main thread and is modal to its parent (usually one of the panels)
    CStartProgressDialogData* ProgrDlgData; // non-NULL only if the dialog runs in its own thread and the main thread hasn't been resumed yet (waiting for dialog opening and operation start)

    HANDLE Worker;                // worker thread associated with this dialog (NULL if it doesn't exist yet/any more)
    HANDLE WContinue;             // multi-purpose event
    HANDLE WorkerNotSuspended;    // non-signaled == the worker should enter suspend mode
    BOOL CancelWorker;            // if TRUE, the worker thread will terminate
    int OperationProgress;        // progress value shared with the worker thread
    int SummaryProgress;          // progress value shared with the worker thread
    BOOL ShowPause;               // the "pause" button text: TRUE = pause, FALSE = resume
    BOOL IsInQueue;               // TRUE = the operation is queued (requested by the user and successfully added)
    BOOL AutoPaused;              // TRUE if the operation is queued and therefore paused
    BOOL StatusPaused;            // TRUE = the operation is stopped, e.g. when querying Cancel (+ other dialogs)
    DWORD NextTimeLeftUpdateTime; // time of the next allowed time-left update (frequent updates hurt for long times)
    CQuadWord TimeLeftLastValue;  // last displayed time-left value
    CITaskBarList3 TaskBarList3;  // controls taskbar progress since Windows 7

    CProgressBar *Operation,
        *Summary;
    CStaticText *OperationText,
        *Source,
        *Target,
        *Status;
    COperations* Script;
    std::wstring Caption;
    CChangeAttrsData* AttrsData;
    CConvertData* ConvertData;
    BOOL AcceptCommands;

    HWND HPreposition;

    BOOL CanClose; // prevent unwanted closing (handled inside this object's methods)

    BOOL TimerIsRunning; // if TRUE, a timer for text changes and the progress bar updates is running

    BOOL FirstUserSetDialog; // TRUE = WM_USER_SETDIALOG message isn't processed yet (the first call forces a repaint so the user sees the dialog at least flash)

    HWND NextForegroundWindow; // window that should be foreground after closing this dialog (on XP with service pack 1 and with a top-most window opened the activation may fail after closing the dialog - focus sometimes went to the top-most window instead of Salamander)

    BOOL DoNotBeepOnClose; // TRUE = operation cancels because Salamander is exiting; beeping makes no sense

    // texts are stored in a cache and drawn when the timer fires
    BOOL CacheIsDirty;
    std::wstring OperationCache;
    std::wstring PrepositionCache;
    std::wstring SourceCacheW;
    std::wstring TargetCacheW;

    // values are stored and drawn only when the timer fires
    BOOL OperationProgressCacheIsDirty;
    int OperationProgressCache;
    BOOL SummaryProgressCacheIsDirty;
    int SummaryProgressCache;
};

//
// ****************************************************************************

class CFileErrorDlg : public CCommonDialog
{
public:
    // Caption and error are WIDE. The file name already was (fileW),
    // so this dialog could show a Unicode file name correctly while narrowing the
    // error text describing it - the two halves are consistent now.
    // 'file' is wide, and the fileW duplicate is GONE. It existed only
    // because 'file' could not represent every filename; two wide names cannot
    // usefully disagree. Same reasoning that deleted CFileData::NameW in P1.3.
    CFileErrorDlg(HWND parent, const wchar_t* caption, const wchar_t* file, const wchar_t* error,
                  BOOL noSkip = FALSE, int altRes = 0);

protected:
    virtual INT_PTR DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam);

    const wchar_t* Caption;
    const wchar_t* File;
    const wchar_t* Error;
};

//
// ****************************************************************************

class CErrorReadingADSDlg : public CCommonDialog
{
public:
    // The fileW/titleW/errorW duplicates are GONE - file, error and title
    // are wide themselves now, and two wide strings cannot usefully disagree.
    // unicodeWnd=TRUE is still required for the text to reach the screen intact.
    CErrorReadingADSDlg(HWND parent, const wchar_t* file, const wchar_t* error,
                        const wchar_t* title = NULL);

protected:
    virtual INT_PTR DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam);

    const wchar_t *File,
        *Error,
        *Title;
};

//
// ****************************************************************************

class CErrorSettingAttrsDlg : public CCommonDialog
{
public:
    CErrorSettingAttrsDlg(HWND parent, const wchar_t* file, DWORD neededAttrs, DWORD currentAttrs);

protected:
    virtual INT_PTR DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam);

    const wchar_t* File;
    DWORD NeededAttrs;
    DWORD CurrentAttrs;
};

//
// ****************************************************************************

class CErrorCopyingPermissionsDlg : public CCommonDialog
{
public:
    CErrorCopyingPermissionsDlg(HWND parent, const wchar_t* sourceFile,
                                const wchar_t* targetFile, DWORD error);

protected:
    virtual INT_PTR DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam);

    const wchar_t* SourceFile;
    const wchar_t* TargetFile;
    DWORD Error;
};

//
// ****************************************************************************

class CErrorCopyingDirTimeDlg : public CCommonDialog
{
public:
    CErrorCopyingDirTimeDlg(HWND parent, const wchar_t* targetFile, DWORD error);

protected:
    virtual INT_PTR DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam);

    const wchar_t* TargetFile;
    DWORD Error;
};

//
// ****************************************************************************

class COverwriteDlg : public CCommonDialog
{
public:
    // The ATTR strings are wide (shown with SetWindowTextW, which is
    // not class-bound). The NAMES stay narrow: they pass through RemapNames, an
    // ANSI remap table that is genuinely narrow for now.
    // The sourceNameW/targetNameW duplicates are GONE - the names are
    // wide themselves now. unicodeWnd=TRUE remains required for them to reach the screen.
    COverwriteDlg(HWND parent, const wchar_t* sourceName, const wchar_t* sourceAttr,
                  const wchar_t* targetName, const wchar_t* targetAttr, BOOL yesnocancel = FALSE,
                  BOOL dirOverwrite = FALSE);

protected:
    virtual INT_PTR DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam);

    const wchar_t* SourceName;
    const wchar_t* SourceAttr;
    const wchar_t* TargetName;
    const wchar_t* TargetAttr;
};

//
// ****************************************************************************

class CHiddenOrSystemDlg : public CCommonDialog
{
public:
    // Caption and error wide, matching its sibling CFileErrorDlg.
    // 'name' is WIDE. It is a file name shown next to a wide
    // question, so narrowing it was the one lossy half of this dialog.
    CHiddenOrSystemDlg(HWND parent, const wchar_t* caption, const wchar_t* name,
                       const wchar_t* error, BOOL yesnocancel = FALSE, BOOL yesallcancel = FALSE);

protected:
    virtual INT_PTR DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam);

    const wchar_t* Caption;
    const wchar_t* Name;
    const wchar_t* Error;
};

//
// ****************************************************************************

class CConfirmADSLossDlg : public CCommonDialog
{
public:
    CConfirmADSLossDlg(HWND parent, BOOL isFile, const wchar_t* name, const wchar_t* streams,
                       BOOL isMove);

protected:
    virtual INT_PTR DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam);

    const wchar_t* Name;
    const wchar_t* Streams;
    BOOL IsFile;
    BOOL IsMove;
};

//
// ****************************************************************************

class CConfirmLinkTgtCopyDlg : public CCommonDialog
{
public:
    CConfirmLinkTgtCopyDlg(HWND parent, const wchar_t* name, const wchar_t* details);

protected:
    virtual INT_PTR DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam);

    const wchar_t* Name;
    const wchar_t* Details;
};

//
// ****************************************************************************

class CConfirmEncryptionLossDlg : public CCommonDialog
{
public:
    CConfirmEncryptionLossDlg(HWND parent, BOOL isFile, const wchar_t* name, BOOL isMove);

protected:
    virtual INT_PTR DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam);

    const wchar_t* Name;
    BOOL IsFile;
    BOOL IsMove;
};

//
// ****************************************************************************

class CCannotMoveDlg : public CCommonDialog
{
public:
    // All three W duplicates are gone; the names and the error are wide.
    // IDS_ERROR is a child static control (always Unicode-native regardless of the
    // dialog's own window class), so no unicodeWnd opt-in is needed here.
    CCannotMoveDlg(HWND parent, int resID, wchar_t* sourceName, wchar_t* targetName,
                   wchar_t* error);

protected:
    virtual INT_PTR DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam);

    wchar_t *SourceName,
        *TargetName,
        *Error;
};

//
// ****************************************************************************

class CSizeResultsDlg : public CCommonDialog
{
public:
    CSizeResultsDlg(HWND parent, const CQuadWord& size, const CQuadWord& compressed,
                    const CQuadWord& occupied, int files, int dirs,
                    TDirectArray<CQuadWord>* sizes);

protected:
    virtual INT_PTR DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam);

    void UpdateEstimate();

    CQuadWord Size, Compressed, Occupied;
    int Files, Dirs;
    TDirectArray<CQuadWord>* Sizes;
    std::wstring UnknownText;
};

//
// ****************************************************************************

class CColorGraph;

class CDriveInfo : public CCommonDialog
{
protected:
    std::wstring VolumePath; // which drive information should be shown (either the root or a junction point)
    std::wstring OldVolumeName; // for change detection
    CColorGraph* Graph;
    HICON HDriveIcon;

public:
    CDriveInfo(HWND parent, const wchar_t* path, CObjectOrigin origin = ooStandard);

    virtual void Validate(CTransferInfo& ti);
    virtual void Transfer(CTransferInfo& ti);

protected:
    virtual INT_PTR DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam);

    void GrowWidth(int resID, int& width);
};

//
// ****************************************************************************

class CChangeCaseDlg : public CCommonDialog
{
private:
    BOOL SelectionContainsDirectory;

public:
    int FileNameFormat; // numbers compatible with AlterFileName function
    int Change;         // which part of the name should be modified  --||--
    BOOL SubDirs;       // including subdirectories?

    CChangeCaseDlg(HWND parent, BOOL selectionContainsDirectory);

    virtual void Transfer(CTransferInfo& ti);
};

//
// ****************************************************************************

class CConvertFilesDlg : public CCommonDialog
{
private:
    BOOL SelectionContainsDirectory;

public:
    std::wstring Mask; // which files will be converted?
    int Change;          // which conversion should be performed?
    BOOL SubDirs;        // include subdirectories?
    int CodeType;        // selected encoding (0 = none)
    int EOFType;         // selected line endings (0 = none)
                         // 1 = CRLF
                         // 2 = LF
                         // 3 = CR

    CConvertFilesDlg(HWND parent, BOOL selectionContainsDirectory);

    virtual void Validate(CTransferInfo& ti);
    virtual void Transfer(CTransferInfo& ti);

    void UpdateCodingText();
    //    void UpdateEOFText();

protected:
    INT_PTR DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam);
};

//
// ****************************************************************************

class CFilterDialog : public CCommonDialog
{
protected:
    CMaskGroup* Filter;
    BOOL* UseFilter;
    //    BOOL       *Inverse;
    wchar_t** FilterHistory;

public:
    CFilterDialog(HWND parent, CMaskGroup* filter, wchar_t** filterHistory,
                  BOOL* use /*, BOOL *inverse*/);

    virtual void Validate(CTransferInfo& ti);
    virtual void Transfer(CTransferInfo& ti);

protected:
    virtual INT_PTR DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam);

    void EnableControls();
};

//
// ****************************************************************************

class CSetSpeedLimDialog : public CCommonDialog
{
protected:
    BOOL* UseSpeedLim; // TRUE = speed limit enabled
    DWORD* SpeedLimit; // speed limit in bytes per second

public:
    CSetSpeedLimDialog(HWND parent, BOOL* useSpeedLim, DWORD* speedLimit);

    virtual void Validate(CTransferInfo& ti);
    virtual void Transfer(CTransferInfo& ti);

protected:
    virtual INT_PTR DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam);

    void EnableControls();
};

//
// ****************************************************************************

#define USERNAME_MAXLEN (256 + 1 + 256 + 1) // user + '@' + domain + '\0'
#define PASSWORD_MAXLEN (256 + 1)
#define DOMAIN_MAXLEN (256 + 1)

class CEnterPasswdDialog : public CCommonDialog
{
public:
    std::wstring Passwd;
    std::wstring User;

    CEnterPasswdDialog(HWND parent, const wchar_t* path, const wchar_t* user, CObjectOrigin origin = ooStandard);

    virtual void Validate(CTransferInfo& ti);
    virtual void Transfer(CTransferInfo& ti);

protected:
    virtual INT_PTR DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam);

    const wchar_t* Path;
};

//
// ****************************************************************************

class CChangeDirDlg : public CCommonDialog
{
public:
    CChangeDirDlg(HWND parent, std::wstring& path, BOOL* sendDirectlyToPlugin);

    virtual void Transfer(CTransferInfo& ti);

protected:
    INT_PTR DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam);

    std::wstring& Path;
    BOOL* SendDirectlyToPlugin;
};

//
// ****************************************************************************

class CPackerConfig;

class CPackDialog : public CCommonDialog
{
public:
    CPackDialog(HWND parent, std::wstring& path, const std::wstring& pathAlt,
                CTruncatedString* subject, CPackerConfig* config);

    virtual void Transfer(CTransferInfo& ti);

    void SetSelectionEnd(int selectionEnd);

protected:
    virtual INT_PTR DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam);

    // changes the extension in 'name' to 'ext'; returns TRUE on success
    BOOL ChangeExtension(std::wstring& name, const wchar_t* ext);

    std::wstring& Path;
    // HWND HUnicodeEdit removed - P0.5a keeps the native IDE_PATH
    // combo's edit child Unicode, so there is nothing left to overlay.
    const std::wstring& PathAlt;
    CTruncatedString* Subject;
    CPackerConfig* PackerConfig;
    int SelectionEnd;
};

//
// ****************************************************************************

class CUnpackerConfig;

class CUnpackDialog : public CCommonDialog
{
public:
    CUnpackDialog(HWND parent, std::wstring& path, const std::wstring& pathAlt, std::wstring& mask,
                  CTruncatedString* subject, CUnpackerConfig* config,
                  BOOL* delArchiveWhenDone);

    virtual void Transfer(CTransferInfo& ti);

protected:
    virtual INT_PTR DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam);

    void EnableDelArcCheckbox();

    std::wstring& Mask;
    std::wstring& Path;
    const std::wstring& PathAlt;
    CTruncatedString* Subject;
    CUnpackerConfig* UnpackerConfig;
    BOOL* DelArchiveWhenDone;
};

//
// ****************************************************************************

class CZIPSizeResultsDlg : public CCommonDialog
{
public:
    CZIPSizeResultsDlg(HWND parent, const CQuadWord& size, int files, int dirs);

protected:
    virtual INT_PTR DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam);

    CQuadWord Size;
    int Files, Dirs;
};

//
// ****************************************************************************

class CSplashScreen : public CDialog
{
public:
    CSplashScreen();
    ~CSplashScreen();

    BOOL PaintText(const wchar_t* text, int x, int y, BOOL bold, COLORREF clr); // wide (DrawTextW)
    void SetText(const wchar_t* text); // wide
    int GetWidth() { return Width; }
    int GetHeight() { return Height; }

    BOOL PrepareBitmap();
    void DestroyBitmap();

protected:
    virtual INT_PTR DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam);

protected:
    CBitmap* Bitmap;         // includes the text
    CBitmap* OriginalBitmap; // graphics only, without text
    HFONT HNormalFont;
    HFONT HBoldFont;
    RECT OpenSalR;
    RECT VersionR;
    RECT CopyrightR;
    RECT StatusR;
    int GradientY;
    int Width;
    int Height;
};

//
// ****************************************************************************

class CAboutDialog : public CCommonDialog
{
protected:
    HFONT HSmallFont;
    HBRUSH HGradientBkBrush;
    CBitmap* BackgroundBitmap;

public:
    CAboutDialog(HWND parent);
    ~CAboutDialog();

protected:
    virtual INT_PTR DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam);
};

//
// ****************************************************************************

struct CExecuteItem;
class CComboboxEdit;

class CFileListDialog : public CCommonDialog
{
protected:
    CComboboxEdit* EditLine;

public:
    CFileListDialog(HWND parent);
    ~CFileListDialog();

    virtual void Validate(CTransferInfo& ti);
    void Transfer(CTransferInfo& ti);

protected:
    virtual INT_PTR DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam);

    void EnableControls();
};

//
// ****************************************************************************

#ifdef USE_BETA_EXPIRATION_DATE

class CBetaExpiredDialog : public CCommonDialog
{
protected:
    int Count;
    std::wstring OldOK;

public:
    CBetaExpiredDialog(HWND parent);

protected:
    virtual INT_PTR DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam);

    void OnTimer();
};

#endif // USE_BETA_EXPIRATION_DATE

//
// ****************************************************************************

class CTaskListDialog : public CCommonDialog
{
protected:
    DWORD DisplayedVersion; // currently shown version of the list

public:
    CTaskListDialog(HWND parent);

protected:
    void Refresh();
    DWORD GetCurPID(); // returns the PID selected in the list box

    virtual INT_PTR DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam);
};

//
// ****************************************************************************

class CChangeIconDialog : public CCommonDialog
{
protected:
    // for transferring data to and from the dialog
    std::wstring* IconFile;
    int* IconIndex;
    BOOL Dirty;
    HICON* Icons;     // array of enumerated icon handles
    DWORD IconsCount; // number of icons in the array

public:
    CChangeIconDialog(HWND hParent, std::wstring& iconFile, int* iconIndex);
    ~CChangeIconDialog();

    virtual void Transfer(CTransferInfo& ti);

protected:
    virtual INT_PTR DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam);

    void GetShell32(std::wstring& fileName);

    BOOL LoadIcons();    // enumerates icons and fills the Icons array
    void DestroyIcons(); // clears the Icons array
};

//
// ****************************************************************************

class CToolbarHeader;
struct CPluginData;
class CHyperLink;

class CPluginsDlg : public CCommonDialog
{
protected:
    HWND HListView; // our listview
    CToolbarHeader* Header;
    HIMAGELIST HImageList;      // image list for the listview
    BOOL RefreshPanels;         // should the panels be refreshed after closing the dialog?
    BOOL DrivesBarChange;       // should the Drives bars be refreshed after closing the dialog?
    std::wstring FocusPlugin; // empty if no plugin should get focus; otherwise contains its path
    CHyperLink* Url;
    std::wstring ShowInBarText;        // text taken from the checkbox when the dialog opens
    std::wstring ShowInChDrvText;      // text taken from the checkbox when the dialog opens
    std::wstring InstalledPluginsText; // text taken from the listview caption when the dialog opens

public:
    CPluginsDlg(HWND hParent);

    // dialog return values:
    BOOL GetRefreshPanels() { return RefreshPanels; }
    BOOL GetDrivesBarChange() { return DrivesBarChange; }
    const wchar_t* GetFocusPlugin() { return FocusPlugin.c_str(); }

protected:
    virtual INT_PTR DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam);

    void InitColumns();     // add columns to the listview
    void SetColumnWidths(); // set optimal column widths
    void RefreshListView(BOOL setOnly = TRUE, int selIndex = -1, const CPluginData* selectPlugin = NULL, BOOL setColumnWidths = FALSE);
    void OnSelChanged();                                                    // selected item in the listview changed
    CPluginData* GetSelectedPlugin(int* index = NULL, int* lvIndex = NULL); // returns NULL if no item is selected; index returns index to the Plugins array; lvIndex returns index within listview, can be NULL
    void EnableButtons(CPluginData* plugin);
    void OnContextMenu(int x, int y); // show the context menu for the selected item at coordinates x, y
    void OnMove(BOOL up);
    void OnSort();
    void EnableHeader();
};

//
// ****************************************************************************

struct CPluginMenuItem;

class CPluginKeys : public CCommonDialog
{
public:
    BOOL Reset; // return value (TRUE if the dialog was closed via Reset)

protected:
    HWND HListView; // our listview
    CToolbarHeader* Header;
    CPluginData* Plugin;
    DWORD* HotKeys; // local copy of hot keys (so cancel works)

public:
    CPluginKeys(HWND hParent, CPluginData* plugin);
    ~CPluginKeys();
    BOOL IsGood();

protected:
    virtual INT_PTR DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam);
    void InitColumns();     // add columns to the listview
    void SetColumnWidths(); // set optimal column widths
    void RefreshListView(BOOL setOnly = TRUE);

    WORD GetHotKey(BYTE* virtKey = NULL, BYTE* mods = NULL);
    CPluginMenuItem* GetSelectedItem(int* orgIndex); // orgIndex returns index to Plugin array->MenuItems; may be NULL
    CPluginMenuItem* GetItem(int index);
    void EnableButtons();
    void HandleConflictWarning();

    virtual void Transfer(CTransferInfo& ti);
};

//
// ****************************************************************************

class CFileTimeStamps;
class CFilesWindow;
class CArchiveUpdateDlg : public CCommonDialog
{
protected:
    CFileTimeStamps* FileStamps;
    CFilesWindow* Panel;

public:
    CArchiveUpdateDlg(HWND hParent, CFileTimeStamps* fileStamps, CFilesWindow* panel);

    void EnableButtons();

protected:
    virtual INT_PTR DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam);
};

//****************************************************************************
//
// CWaitWindow
//
// Modal window used to display information about an operation in progress.
// The text may contain multiple lines separated by '\n'.
// 'showCloseButton' specifies whether the dialog includes a Close button,
// which is effectively the same as pressing Escape but accessible by mouse.
//

class CWaitWindow : public CWindow
{
protected:
    // The caption is a window title, so its storage, the registered class, and
    // the CWindow::CreateEx call must remain an explicitly wide adapter as one unit.
    std::wstring Caption;
    // Text is wide: it is only ever drawn with DrawText, a GDI call that is NOT
    // class-bound, so the wide form works today.
    std::wstring Text;
    SIZE TextSize;
    HWND HParent;
    HWND HForegroundWnd;
    BOOL ShowCloseButton;
    BOOL NeedWrap;

    BOOL ShowProgressBar;
    RECT BarRect;
    DWORD BarMax;
    DWORD BarPos;

    CBitmap* CacheBitmap; // used for flicker-free text drawing

public:
    CWaitWindow(HWND hParent, int textResID, BOOL showCloseButton, CObjectOrigin origin = ooAllocated, BOOL showProgressBar = FALSE);
    ~CWaitWindow();

    void SetCaption(const wchar_t* text); // if not called, the caption will be "Open Salamander"
    void SetText(const wchar_t* text); // wide - see the Text member
    void SetProgressMax(DWORD max);
    void SetProgressPos(DWORD pos);

    // if hForegroundWnd is non-NULL the window will be centered relative to it
    // but not shown (display occurs in ThreadSafeWaitWindowFBody)
    HWND Create(HWND hForegroundWnd = NULL);

protected:
    virtual LRESULT WindowProc(UINT uMsg, WPARAM wParam, LPARAM lParam);
    void PaintProgressBar(HDC dc);
    void PaintText(HDC hDC);
};

//****************************************************************************
//
// CTipOfTheDayWindow and CTipOfTheDayDialog
//
//
/*
class CTipOfTheDayDialog;

class CTipOfTheDayWindow: public CWindow
{
  protected:
    HFONT HHeadingFont;
    HFONT HBodyFont;
    CTipOfTheDayDialog *Parent;

  public:
    CTipOfTheDayWindow();
    ~CTipOfTheDayWindow();

    void PaintBodyText(HDC hDC = NULL);

  protected:
    virtual LRESULT WindowProc(UINT uMsg, WPARAM wParam, LPARAM lParam);

  friend class CTipOfTheDayDialog;
};

class CTipOfTheDayDialog: public CCommonDialog
{
  protected:
    CTipOfTheDayWindow  TipWindow;
    TDirectArray<DWORD> Tips;

  public:
    CTipOfTheDayDialog(BOOL quiet);
    ~CTipOfTheDayDialog();

    BOOL IsGood() {return Tips.Count > 0;}
    void IncrementTipIndex();
    void InvalidateTipWindow() {InvalidateRect(TipWindow.HWindow, NULL, FALSE);}

  protected:
    virtual INT_PTR DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam);

    virtual void Transfer(CTransferInfo &ti);

    BOOL LoadTips(BOOL quiet);  // loads tips from the file into the Tips array
    void FreeTips();  // frees the array

  friend class CTipOfTheDayWindow;
};
*/
//****************************************************************************
//
// CImportConfigDialog
//

struct CImportOldKey
{
    wchar_t* SalamanderVersion;
    wchar_t* SalamanderPath;
};

class CImportConfigDialog : public CCommonDialog
{
public:
    // array corresponding to SalamanderConfigurationRoots array; TRUE:the configuration exists, FALSE:it doesn't
    BOOL ConfigurationExist[SALCFG_ROOTS_COUNT];
    // pointer to the same sized array where the dialog stores TRUE for configurations to delete
    BOOL* DeleteConfigurations;
    // dialog returns here which configuration the user wants to import; -1 -> none
    // index points into the SalamanderConfigurationRoots array
    int IndexOfConfigurationToLoad;

public:
    CImportConfigDialog();
    ~CImportConfigDialog();

protected:
    virtual void Transfer(CTransferInfo& ti);
    virtual INT_PTR DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam);
};

//****************************************************************************
//
// CLanguageSelectorDialog
//

class CHyperLink;

class CLanguageSelectorDialog : public CCommonDialog
{
protected:
    TDirectArray<CLanguage> Items;
    CHyperLink* Web;
    std::wstring& SLGName;
    BOOL OpenedFromConfiguration;
    BOOL OpenedForPlugin;
    HWND HListView;
    const wchar_t* PluginName;
    std::wstring ExitButtonLabel;

public:
    CLanguageSelectorDialog(HWND hParent, std::wstring& slgName, const wchar_t* pluginName);
    ~CLanguageSelectorDialog();

    int Execute();

    // scans the 'lang' directory and adds all valid SLG files to the array
    BOOL Initialize(const wchar_t* slgSearchPath = NULL, HINSTANCE pluginDLL = NULL);

    int GetLanguagesCount() { return Items.Count; }
    BOOL GetSLGName(std::wstring& path, int index = 0); // returns xxxx.slg of the item at index 'index'
    BOOL SLGNameExists(const wchar_t* slgName);    // checks whether 'slgName' exists in 'Items'

    void FillControls();

    void LoadListView();

    void Transfer(CTransferInfo& ti);

    // returns the index into Items array; highest priority is 'selectSLGName' (if not NULL), then
    // the Windows setting, and if 'exactMatch' is FALSE then english.slg or otherwise the first
    // found .slg; if 'exactMatch' is TRUE, it returns the index of 'selectSLGName' (if not NULL)
    // or the Windows setting or -1 (nothing found)
    int GetPreferredLanguageIndex(const wchar_t* selectSLGName, BOOL exactMatch = FALSE);

protected:
    virtual INT_PTR DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam);
};

//****************************************************************************
//
// CConversionTablesDialog
//

class CConversionTablesDialog : public CCommonDialog
{
protected:
    HWND HListView;
    std::wstring& DirName;

public:
    CConversionTablesDialog(HWND parent, std::wstring& dirName);
    ~CConversionTablesDialog();

    void Transfer(CTransferInfo& ti);

protected:
    virtual INT_PTR DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam);
};

//****************************************************************************
//
// CSkillLevelDialog
//

class CSkillLevelDialog : public CCommonDialog
{
protected:
    int* Level;

public:
    CSkillLevelDialog(HWND hParent, int* level);

    void Transfer(CTransferInfo& ti);
};

//****************************************************************************
//
// CSharesDialog
//

class CSharesDialog : public CCommonDialog
{
protected:
    HWND HListView;
    CShares SharedDirs; // keep our own instance so nobody
                        // refreshes it in the background
    BYTE SortBy;        // indicates the column used for sorting
    int FocusedIndex;   // used to return the value

public:
    CSharesDialog(HWND hParent);

    const wchar_t* GetFocusedPathW(); // returns the path of the selected share, the exact wide form from
                                      // NetShareEnum; call only after the dialog returns. Returns NULL if
                                      // "Focus" wasn't clicked and the dialog returned IDOK from Execute()

protected:
    virtual INT_PTR DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam);

    HIMAGELIST CreateImageList(); // creates an image list with a single icon (shared folder)

    void InitColumns(); // add columns to the ListView
    void Refresh();     // loads shared folders and adds them to the listview
    static int CALLBACK SortFunc(LPARAM lParam1, LPARAM lParam2, LPARAM lParamSort);
    void SortItems();                          // sorts items based on the SortBy variable
    int GetFocusedIndex();                     // returns index to SharetDirs array or -1 if no item is selected
    void DeleteShare(const wchar_t* shareName); // request the system to remove the share; takes
                                                // the exact wide share name now, no ANSI round trip before NetShareDel
    void OnContextMenu(int x, int y);        // shows the context menu for the selected item at coordinates x, y
    void EnableControls();                   // button enabler
};

//****************************************************************************
//
// CDisconnectDialog
//

// types for CConnectionItem
enum CConnectionItemType
{
    citGroup,   // marks the following group (network resources, plugins, ...)
    citNetwork, // a network resource
    citPlugin   // a plugin connection
};

#define CONNECTION_ICON_NETWORK 0
#define CONNECTION_ICON_PLUGIN 1
#define CONNECTION_ICON_ACCESSIBLE 2
#define CONNECTION_ICON_INACCESSIBLE 3

// individual items displayed in the Disconnect dialog; filled by EnumConnections()
struct CConnectionItem
{
    CConnectionItemType Type;
    int IconIndex; // icon index in HImageList (CONNECTION_ICON_xxx)
    wchar_t* Name;    // Name column
    wchar_t* Path;    // Path column
    BOOL Default;  // if TRUE, the item is FOCUSED+SELECTED after the listview is filled; only one item in the array should have Default == TRUE

    // only for Type == citPlugin: FS interface (might not be valid, verify)
    CPluginFSInterfaceAbstract* PluginFS;
};

class CDisconnectDialog : public CCommonDialog
{
protected:
    CFilesWindow* Panel; // panel from which Disconnect was invoked and from which we take the default path
    HWND HListView;
    HIMAGELIST HImageList;
    BOOL NoConncection;
    TDirectArray<CConnectionItem> Connections;

public:
    CDisconnectDialog(CFilesWindow* panel);
    ~CDisconnectDialog();

    const wchar_t* GetFocusedPath();                 // returns the path of the selected share; call only after the dialog returns
                                                  // returns NULL if "Focus" wasn't clicked and the dialog returned IDOK
                                                  // from Execute()
    BOOL NoConnection() { return NoConncection; } // returns TRUE if the dialog wasn't opened because there was no connection

protected:
    virtual INT_PTR DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam);
    virtual void Validate(CTransferInfo& ti);

    HIMAGELIST CreateImageList(); // creates an image list 0-accessible, 1-inaccessible network drive

    void EnumConnections(); // fills the Connections array

    void InitColumns(); // adds columns to the ListView
    void Refresh();

    // disconnects SELECTED items; returns TRUE if everything succeeded and the dialog can close
    // if an error occurs it shows a message box and returns FALSE; the dialog then stays open
    BOOL OnDisconnect();

    void EnableControls(); // button enabler

    // inserts a new item into the Connections array
    // if index == -1 the item is appended to the end of the list
    // if ignoreDuplicate is set, the array is searched first and the item is skipped if already present
    BOOL InsertItem(int index, BOOL ignoreDuplicate, CConnectionItemType type, int iconIndex,
                    const wchar_t* name, const wchar_t* path, BOOL defaultItem,
                    CPluginFSInterfaceAbstract* pluginFS);

    void DestroyConnections(); // empties/frees the Connections array
};

//****************************************************************************
//
// CSaveSelectionDialog
//

class CSaveSelectionDialog : public CCommonDialog
{
public:
    CSaveSelectionDialog(HWND hParent, BOOL* clipboard);

    virtual void Transfer(CTransferInfo& ti);

protected:
    BOOL* Clipboard;
};

//****************************************************************************
//
// CLoadSelectionDialog
//

enum CLoadSelectionOperation
{
    lsoCOPY,
    lsoOR,
    lsoDIFF,
    lsoAND
};

class CLoadSelectionDialog : public CCommonDialog
{
public:
    CLoadSelectionDialog(HWND hParent, CLoadSelectionOperation* operation, BOOL* clipboard,
                         BOOL clipboardValid, BOOL globalValid);

    virtual void Transfer(CTransferInfo& ti);

protected:
    CLoadSelectionOperation* Operation;
    BOOL* Clipboard;
    BOOL ClipboardValid;
    BOOL GlobalValid;
};

//****************************************************************************
//
// CCompareDirsDialog
//

class CCompareDirsDialog : public CCommonDialog
{
protected:
    BOOL EnableByDateAndTime;
    BOOL EnableBySize;
    BOOL EnableByAttrs;
    BOOL EnableByContent;
    BOOL EnableSubdirs;
    BOOL EnableCompAttrsOfSubdirs;
    CFilesWindow* LeftPanel;
    CFilesWindow* RightPanel;

    int OriginalWidth;    // full dialog width
    int OriginalHeight;   // full dialog height
    int OriginalButtonsY; // Y position of the buttons in client coordinates
    int SpacerHeight;     // spacer used when shrinking/expanding the dialog
    BOOL Expanded;        // are we currently expanded?

public:
    CCompareDirsDialog(HWND hParent, BOOL enableByDateAndTime, BOOL enableBySize,
                       BOOL enableByAttrs, BOOL enableByContent, BOOL enableSubdirs,
                       BOOL enableCompAttrsOfSubdirs, CFilesWindow* leftPanel,
                       CFilesWindow* rightPanel);

    virtual void Validate(CTransferInfo& ti);
    virtual void Transfer(CTransferInfo& ti);

protected:
    virtual INT_PTR DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam);

    void EnableControls();
    void DisplayMore(BOOL more);
    HDWP OffsetControl(HDWP hdwp, int id, int yOffset);
};

//****************************************************************************
//
// CCmpDirProgressDialog
//

class CCmpDirProgressDialog : public CCommonDialog
{
protected:
    BOOL HasProgress;
    BOOL Cancel;

    // dialog controls
    CStaticText* Source;
    CStaticText* Target;
    CProgressBar* Progress;
    CProgressBar* TotalProgress;

    std::wstring DelayedSource; // text displayed later
    std::wstring DelayedTarget; // text displayed later
    BOOL DelayedSourceDirty;
    BOOL DelayedTargetDirty;

    // progress displayed later
    BOOL SizeIsDirty;
    CQuadWord FileSize;
    CQuadWord ActualFileSize;
    CQuadWord TotalSize;
    CQuadWord ActualTotalSize;

    DWORD LastTickCount; // used to detect when the date must be redrawn

    CITaskBarList3* TaskBarList3; // pointer to the interface owned by the Salamander main window

public:
    CCmpDirProgressDialog(HWND hParent, BOOL hasProgress, CITaskBarList3* taskBarList3); // if 'hasProgress' is TRUE, the dialog shows a progress bar

    // text setup
    // One wide parameter. The old narrow/wide pair mirrored the same text twice -
    // the narrow half was a CP_ACP round trip that the paint never read, because
    // the paint preferred the wide one.
    void SetSource(const wchar_t* text);
    void SetTarget(const wchar_t* text);

    // the following three methods are relevant only when the progress bar is shown

    // sets the total size of a single file
    void SetFileSize(const CQuadWord& size);
    // sets absolute progress
    void SetActualFileSize(const CQuadWord& size);
    // sets the total size of all files
    void SetTotalSize(const CQuadWord& size);
    // sets the current total size of all files
    void SetActualTotalSize(const CQuadWord& size);
    // retrieves the current total size of all files
    void GetActualTotalSize(CQuadWord& size);
    // changes progress relatively
    void AddSize(const CQuadWord& size);

    // distributes messages; returns FALSE if the user cancelled the operation
    BOOL Continue();

    void FlushDataToControls(); // passes stored values to controls for display

protected:
    virtual INT_PTR DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam);
};

//****************************************************************************
//
// CExitingOpenSal
//

class CExitingOpenSal : public CCommonDialog
{
protected:
    int NextOpenedDlgIndex;

public:
    CExitingOpenSal(HWND hParent);

protected:
    INT_PTR DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam);
};

//
// ****************************************************************************

class CDriveSelectErrDlg : public CCommonDialog
{
public:
    CDriveSelectErrDlg(HWND parent, const wchar_t* errText, const wchar_t* drvPath);

protected:
    virtual INT_PTR DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam);

protected:
    const wchar_t* ErrText;
    std::wstring DrvPath;
    int CounterForAllowedUseOfTimer;
};

//
// ****************************************************************************

class CCompareArgsDlg : public CCommonDialog
{
public:
    CCompareArgsDlg(HWND parent, BOOL comparingFiles, std::wstring& compareName1,
                    std::wstring& compareName2, int* cnfrmShowNamesToCompare);

    virtual void Validate(CTransferInfo& ti);
    virtual void Transfer(CTransferInfo& ti);

protected:
    virtual INT_PTR DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam);

protected:
    BOOL ComparingFiles;
    std::wstring& CompareName1;
    std::wstring& CompareName2;
    int* CnfrmShowNamesToCompare;
};

extern CProgressDlgArray ProgressDlgArray; // array of disk operation dialogs (only those running in their own threads)
