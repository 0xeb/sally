// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "common/widepath.h"
#include "common/find/FindDialogSeed.h"

// structure for adding messages to the Find Log; sent as the message parameter
// of WM_USER_ADDLOG; parameters will be copied into the log data (can be deallocated after returning)
#define FLI_INFO 0x00000000   // INFORMATION item
#define FLI_ERROR 0x00000001  // ERROR item
#define FLI_IGNORE 0x00000002 // allow the Ignore button on this item
struct FIND_LOG_ITEM
{
    DWORD Flags;      // FLI_xxx
    const wchar_t* Text; // message text, must not be NULL
    const wchar_t* Path; // path to a file or directory, may be NULL
};

#define WM_USER_ADDLOG WM_APP + 210     // add an item to the log [FIND_LOG_ITEM* item, 0]
#define WM_USER_ADDFILE WM_APP + 211    // [0, 0]
#define WM_USER_SETREADING WM_APP + 212 // [0, 0] - request to repaint the status bar ("Reading:")
#define WM_USER_BUTTONS WM_APP + 213    // call EnableButtons() yourself [HWND hButton]
#define WM_USER_FLASHICON WM_APP + 214  // after activating the find, flash the status icon

extern BOOL IsNotAlpha[256];

#define GREP_LINE_LEN 10000      // maximum line length for regular expressions (viewer uses a different macro)

// Length of the mapped view; must be greater than the length of a line for regexp + EOL +
// AllocationGranularity
#define VOF_VIEW_SIZE 0x2800400 // 40 MB (more is risky, virtual memory may be limited) + 1 KB (space for a reasonable text line)

// history for the Named combobox
#define FIND_NAMED_HISTORY_SIZE 30 // number of remembered strings
extern wchar_t* FindNamedHistory[FIND_NAMED_HISTORY_SIZE];

// history for the LookIn combobox
#define FIND_LOOKIN_HISTORY_SIZE 30 // number of remembered strings
extern wchar_t* FindLookInHistory[FIND_LOOKIN_HISTORY_SIZE];

// history for the Containing combobox
#define FIND_GREP_HISTORY_SIZE 30 // number of remembered strings
extern wchar_t* FindGrepHistory[FIND_GREP_HISTORY_SIZE];

extern BOOL FindManageInUse; // is the Manage dialog open?
extern BOOL FindIgnoreInUse; // is the Ignore dialog open?

BOOL InitializeFind();
void ReleaseFind();

// clears all Find histories; if 'dataOnly' == TRUE, comboboxes of open windows are not cleared
void ClearFindHistory(BOOL dataOnly);

DWORD WINAPI GrepThreadF(void* ptr); // body of the grep thread

extern HACCEL FindDialogAccelTable;

class CFoundFilesListView;
class CFindDialog;
class CMenuPopup;
class CMenuBar;

//*********************************************************************************
//
// CSearchForData
//

struct CSearchForData
{
    // DirW replaced a (dir, dirW) pair whose narrow half was derived with
    // WideToAnsi() - substituting the literal path "?" when the wide path had no
    // CP_ACP form. Two of the three constructors and two of the three Set
    // overloads had no caller at all.
    std::wstring DirW;
    CMaskGroup MasksGroup;
    BOOL IncludeSubDirs;

    CSearchForData(const wchar_t* dirW, const wchar_t* masksGroupW, BOOL includeSubDirs)
    {
        Set(dirW, masksGroupW, includeSubDirs);
    }

    void Set(const wchar_t* dirW, const wchar_t* masksGroupW, BOOL includeSubDirs);
    const wchar_t* GetText(int i)
    {
        switch (i)
        {
        case 0:
            return MasksGroup.GetMasksString();
        case 1:
            return DirW.c_str();
        default:
            return IncludeSubDirs ? LoadStrW(IDS_INCLUDESUBDIRSYES) : LoadStrW(IDS_INCLUDESUBDIRSNO);
        }
    }
};

//*********************************************************************************
//
// CSearchingString
//
// synchronized buffer for the "Searching" text in the Find dialog's status bar

class CSearchingString
{
protected:
    std::wstring Buffer;
    size_t BaseLen;
    BOOL Dirty;
    CRITICAL_SECTION Section;

public:
    CSearchingString();
    ~CSearchingString();

    // each of these was declared twice: a sweep widened both halves of an A/W
    // pair into identical signatures, leaving the narrow definitions with nothing to match.
    // sets the base to which additional text is appended via Set, and sets dirty to FALSE
    void SetBase(const wchar_t* buf);
    // appending to the base value set via SetBase
    void Set(const wchar_t* buf);
    // returns the complete string
    void Get(wchar_t* buf, int bufSize);
    std::wstring GetWString();

    // sets the dirty flag (is a redraw already pending?)
    void SetDirty(BOOL dirty);
    // returns TRUE if a redraw is already pending
    BOOL GetDirty();
};

//*********************************************************************************
//
// CGrepData
//

// flags for searching identical files
// at least _NAME or _SIZE must be specified
// _CONTENT can be set only when _SIZE is set as well
#define FIND_DUPLICATES_NAME 0x00000001    // same name
#define FIND_DUPLICATES_SIZE 0x00000002    // same size
#define FIND_DUPLICATES_CONTENT 0x00000004 // same content

struct CGrepData
{
    BOOL FindDuplicates; // do we search for duplicates?
    DWORD FindDupFlags;  // FIND_DUPLICATES_xxx; meaningful only if 'FindDuplicates' is TRUE
    int Refine;          // 0: search new data, 1 & 2: search within found data; 1: intersect with old data; 2: subtract from old data
    BOOL Grep;           // use grep?
    BOOL WholeWords;     // match whole words only?
    BOOL Regular;        // regular expression?
    BOOL EOL_CRLF,       // EOL handling when searching regular expressions
        EOL_CR,
        EOL_LF;
    //       EOL_NULL;              // unsupported by the regular expression parser :(

    CSearchData SearchData;
    // Wide literal needle used by the encoding-aware content search. Hex and
    // regular-expression byte renderings remain explicit in their own engines.
    std::wstring GrepText;
    // Case sensitivity for the wide path. SearchData carries it as an sf* flag for
    // the byte engine; this mirrors it in the form ContentSearcher takes.
    BOOL GrepCaseSensitive;
    CRegularExpression RegExp;
    // The SAME pattern compiled from its UTF-8 rendering, used when
    // the file's content is UTF-8.
    //
    // `RegExp` is compiled only when the wide pattern has an exact process-ACP
    // representation. It remains invalid for an unrepresentable pattern so legacy
    // byte content cannot produce substitution-driven false matches.
    //
    // UTF-8 needs no other machinery, which is why it is worth a member of its own:
    // the encoding is ASCII-transparent, so CR/LF bytes never occur inside a
    // multi-byte sequence and the existing line splitter and byte engine are
    // already correct over UTF-8 bytes. Only the PATTERN was in the wrong encoding.
    //
    // UTF-16 content must be re-encoded before this byte engine can run over it.
    // TestUtf16RegexContent (find.cpp) does that via common/text/Utf16RegexBridge,
    // in its own __try-free function so its decode buffers can be plain locals.
    CRegularExpression RegExpUtf8;
    // advanced search
    DWORD AttributesMask;  // mask first
    DWORD AttributesValue; // then compare
    CFilterCriteria Criteria;
    int FileTypeMode;
    // control and data
    BOOL StopSearch;    // the main thread sets this to terminate the grep thread
    BOOL SearchStopped; // has it been terminated or not?
    HWND HWindow;       // window the grep thread communicates with
    TIndirectArray<CSearchForData>* Data;
    CFoundFilesListView* FoundFilesListView; // found files are loaded here
    // two criteria for updating the list view
    int FoundVisibleCount;  // number of items displayed in the list view
    DWORD FoundVisibleTick; // when it was last displayed
    BOOL NeedRefresh;       // need to refresh the display (an item was added without being shown)

    CSearchingString* SearchingText;  // synchronized "Searching" text in the Find status bar
    CSearchingString* SearchingText2; // [optional] second text on the right; used for "Total: 35%"
};

//*********************************************************************************
//
// CFindOptionsItem
//

enum CFindFileTypeMode
{
    fftmAll = 0,
    fftmFiles = 1,
    fftmFolders = 2
};

class CFindOptionsItem
{
public:
    // Internal
    std::wstring ItemName;

    CFilterCriteria Criteria;

    // Find dialog
    int SubDirectories;
    int WholeWords;
    int CaseSensitive;
    int HexMode;
    int RegularExpresions;
    int FileTypeMode;

    BOOL AutoLoad;

    std::wstring NamedText;
    std::wstring LookInText;
    std::wstring GrepText;

public:
    CFindOptionsItem();
    // WARNING! Once the object contains allocated data the code for moving items
    // in the Options Manager stops working because temporary items get destroyed

    CFindOptionsItem& operator=(const CFindOptionsItem& s);

    // builds name of the item (ItemName) based on NamedText and LookInText
    void BuildItemName();

    // WARNING: saving is optimized; only changed values are stored. The key has to be cleared first, before saving.
    BOOL Save(HKEY hKey);                   // saves the item
    BOOL Load(HKEY hKey, DWORD cfgVersion); // loads the item
};

//*********************************************************************************
//
// CFindOptions
//

class CFindOptions
{
protected:
    TIndirectArray<CFindOptionsItem> Items;

public:
    CFindOptions();

    BOOL Save(HKEY hKey);                   // saves the entire array
    BOOL Load(HKEY hKey, DWORD cfgVersion); // loads the entire array

    BOOL Load(CFindOptions& source);

    int GetCount() { return Items.Count; }
    BOOL Add(CFindOptionsItem* item);
    CFindOptionsItem* At(int i) { return Items[i]; }
    void Delete(int i) { Items.Delete(i); }

    // clears previous inserted items and fills in the new ones
    void InitMenu(CMenuPopup* popup, BOOL enabled, int originalCount);
};

//*********************************************************************************
//
// CFindIgnore
//

enum CFindIgnoreItemType
{
    fiitUnknow,
    fiitFull,     // Full path including root: 'C:\' 'D:\TMP\' \\server\share\'
    fiitRooted,   // Path starting in any root
    fiitRelative, // Path without a root: 'aaa' 'aaa\bbbb\ccc'
};

class CFindIgnoreItem
{
public:
    BOOL Enabled;
    std::wstring Path; // wide, with the REG_SZ that persists it

    // the following data are not saved; they are initialized in Prepare()
    CFindIgnoreItemType Type;
    int Len;

public:
    CFindIgnoreItem();
    ~CFindIgnoreItem();
};

// The CFindIgnore object serves two purposes:
// 1. A global object holding the list of paths editable in the Find/Options/Ignore Directory List
// 2. A temporary copy used for searching -- contains only Enabled items which are
//    adjusted (backslashes added) and classified (CFindIgnoreItem::Type set)
class CFindIgnore
{
protected:
    TIndirectArray<CFindIgnoreItem> Items;

public:
    CFindIgnore();

    BOOL Save(HKEY hKey);                   // saves the entire array
    BOOL Load(HKEY hKey, DWORD cfgVersion); // loads the entire array

    // called for the local copy of the object which is then used for searching
    // must be called before calling Contains()
    // copies items from 'source' and prepares them for search
    // returns TRUE on success, otherwise FALSE
    BOOL Prepare(CFindIgnore* source);

    // returns TRUE if the list contains an item matching the 'path'
    // only items with 'Enabled' == TRUE are evaluated
    // returns FALSE if no such item is found
    // Note: the method must receive the full path with a trailing slash
    BOOL Contains(const wchar_t* path, int startPathLen);

    // adds the path only if it does not already exist in the list
    BOOL AddUnique(BOOL enabled, const wchar_t* path);

protected:
    void DeleteAll();
    void Reset(); // clears existing items and adds default values

    BOOL Load(CFindIgnore* source);

    int GetCount() { return Items.Count; }
    BOOL Add(BOOL enabled, const wchar_t* path);
    BOOL Set(int index, BOOL enabled, const wchar_t* path);
    CFindIgnoreItem* At(int i) { return Items[i]; }
    void Delete(int i) { Items.Delete(i); }
    BOOL Move(int srcIndex, int dstIndex);

    friend class CFindIgnoreDialog;
};

//*********************************************************************************
//
// CFindAdvancedDialog
//
/*
class CFindAdvancedDialog: public CCommonDialog
{
  public:
    BOOL             SetDateAndTime;
    CFindOptionsItem *Data;

  public:
    CFindAdvancedDialog(CFindOptionsItem *data);

    int Execute();
    virtual void Validate(CTransferInfo &ti);
    virtual void Transfer(CTransferInfo &ti);
    void EnableControls();   // handles disabling/enabling operations
    void LoadTime();

  protected:
    virtual INT_PTR DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam);
};
*/
//*********************************************************************************
//
// CFindManageDialog
//

class CFindManageDialog : public CCommonDialog
{
protected:
    CEditListBox* EditLB;
    CFindOptions* FO;
    const CFindOptionsItem* CurrenOptionsItem;

public:
    CFindManageDialog(HWND hParent, const CFindOptionsItem* currenOptionsItem);
    ~CFindManageDialog();

    virtual void Transfer(CTransferInfo& ti);
    void LoadControls();

    BOOL IsGood() { return FO != NULL; }

protected:
    virtual INT_PTR DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam);
};

//*********************************************************************************
//
// CFindIgnoreDialog
//

class CFindIgnoreDialog : public CCommonDialog
{
protected:
    CEditListBox* EditLB;
    CFindIgnore* IgnoreList; // our working copy of the data
    CFindIgnore* GlobalIgnoreList;
    BOOL DisableNotification;
    HICON HChecked;
    HICON HUnchecked;

public:
    CFindIgnoreDialog(HWND hParent, CFindIgnore* globalIgnoreList);
    ~CFindIgnoreDialog();

    virtual void Transfer(CTransferInfo& ti);
    virtual void Validate(CTransferInfo& ti);

    BOOL IsGood() { return IgnoreList != NULL; }

protected:
    virtual INT_PTR DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam);
    void FillList();
};

//****************************************************************************
//
// CFindDuplicatesDialog
//

class CFindDuplicatesDialog : public CCommonDialog
{
public:
    // the settings will be remembered for the duration of Salamander's run
    static BOOL SameName;
    static BOOL SameSize;
    static BOOL SameContent;

public:
    CFindDuplicatesDialog(HWND hParent);

    virtual void Validate(CTransferInfo& ti);
    virtual void Transfer(CTransferInfo& ti);

protected:
    virtual INT_PTR DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam);

    void EnableControls(); // handles disabling/enabling operations
};

//*********************************************************************************
//
// CFindLog
//
// Used for storing errors that occurred during the search.
//

struct CFindLogItem
{
    DWORD Flags;
    wchar_t* Text;
    wchar_t* Path;
};

class CFindLog
{
protected:
    TDirectArray<CFindLogItem> Items;
    int SkippedErrors; // number of errors that were not stored
    int ErrorCount;
    int InfoCount;

public:
    CFindLog();
    ~CFindLog();

    void Clean(); // releases all held items

    BOOL Add(DWORD flags, const wchar_t* text, const wchar_t* path);
    int GetCount() { return Items.Count; }
    int GetSkippedCount() { return SkippedErrors; }
    const CFindLogItem* Get(int index);
    int GetErrorCount() { return ErrorCount; }
    int GetInfoCount() { return InfoCount; }
};

//*********************************************************************************
//
// CFindLogDialog
//
// Used to display errors that occurred during the search.
//

class CFindLogDialog : public CCommonDialog
{
protected:
    CFindLog* Log;
    HWND HListView;

public:
    CFindLogDialog(HWND hParent, CFindLog* log);

    virtual void Transfer(CTransferInfo& ti);

protected:
    INT_PTR DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam);

    void OnFocusFile();
    void OnIgnore();
    void EnableControls();
    const CFindLogItem* GetSelectedItem();
};

//*********************************************************************************
//
// CFoundFilesData
// CFoundFilesListView
//

#define MD5_DIGEST_SIZE 16

struct CMD5Digest
{
    BYTE Digest[MD5_DIGEST_SIZE];
};

struct CFoundFilesData
{
    // the narrow Name/Path halves are gone; these two are the only
    // representation. Their W suffix is now redundant and comes off in the P1.5 rename pass.
    std::wstring NameW;
    std::wstring PathW;
    CQuadWord Size;
    DWORD Attr;
    FILETIME LastWrite;

    // 'Group' is used in two ways:
    // 1) while searching for duplicate files, when contents are compared,
    //    it holds a pointer to CMD5Digest with the computed MD5 of the file
    // 2) before passing duplicate search results to the ListView
    //    it contains a number connecting multiple files into an equivalent group
    DWORD_PTR Group;

    unsigned IsDir : 1; // 0 - item is a file, 1 - item is a directory
    // Selected and Focused are used only locally for StoreItemsState/RestoreItemsState
    unsigned Selected : 1; // 0 - item not selected, 1 - item selected
    unsigned Focused : 1;  // 0 - item is focused, 1 - item is not focused
    // 'Different' is used to distinguish file groups during duplicate search
    unsigned Different : 1; // 0 - item has standard white background, 1 - item uses a different one (for difference highlighting)

    CFoundFilesData()
    {
        Attr = 0;
        ZeroMemory(&LastWrite, sizeof(LastWrite));
        Group = 0;
        IsDir = 0;
        Selected = 0;
        Different = 0;
    }
    ~CFoundFilesData() = default;
    BOOL Set(const wchar_t* path, const wchar_t* name, const CQuadWord& size, DWORD attr,
             const FILETIME* lastWrite, BOOL isDir);
    // 'fileNameFormat' determines formatting of names of found items
    std::wstring GetTextW(int i, int fileNameFormat) const;
    std::wstring GetNameTextW(int fileNameFormat) const;
    std::wstring GetFullNameW() const;
    std::wstring GetFullNameTextW(int fileNameFormat) const;
};

class CFoundFilesListView : public CWindow
{
protected:
    TIndirectArray<CFoundFilesData> Data;
    CRITICAL_SECTION DataCriticalSection; // critical section for accessing data
    CFindDialog* FindDialog;
    TIndirectArray<CFoundFilesData> DataForRefine;

public:
    int EnumFileNamesSourceUID; // UID of the source for name enumeration in viewers

public:
    CFoundFilesListView(HWND dlg, int ctrlID, CFindDialog* findDialog);
    ~CFoundFilesListView();

    virtual LRESULT WindowProc(UINT uMsg, WPARAM wParam, LPARAM lParam);

    BOOL InitColumns();

    void StoreItemsState();
    void RestoreItemsState();

    int CompareFunc(CFoundFilesData* f1, CFoundFilesData* f2, int sortBy);
    void QuickSort(int left, int right, int sortBy);
    void SortItems(int sortBy);

    void QuickSortDuplicates(int left, int right, BOOL byName);
    int CompareDuplicatesFunc(CFoundFilesData* f1, CFoundFilesData* f2, BOOL byName);
    void SetDifferentByGroup(); // sets the Different bit based on Group so that the Different bit alternates at group boundaries
    void ClearDuplicateState();

    // interface for Data
    CFoundFilesData* At(int index);
    void DestroyMembers();
    int GetCount();
    int Add(CFoundFilesData* item);
    void Delete(int index);
    BOOL IsGood();
    void ResetState();

    // moves the necessary parts from Data to DataForRefine
    // may only be called  when the search thread is not running
    BOOL TakeDataForRefine();
    void DestroyDataForRefine();
    int GetDataForRefineCount();
    CFoundFilesData* GetDataForRefine(int index);

    void GetSelectedPaths(std::vector<std::wstring>& paths);

    // scans all selected files and directories and removes those that no longer exist
    // if 'forceRemove' variable is TRUE, selected items are removed without needing checks
    // 'lastFocusedIndex' indicates which index was focused before deletion started
    // 'lastFocusedItem' points to a copy of the focused item so we can try to find it again by name and path
    void CheckAndRemoveSelectedItems(BOOL forceRemove, int lastFocusedIndex, const CFoundFilesData* lastFocusedItem);
};

//****************************************************************************
//
// CFindTBHeader
//

class CToolBar;

class CFindTBHeader : public CWindow
{
protected:
    CToolBar* ToolBar;
    CToolBar* LogToolBar;
    HWND HNotifyWindow; // window to which commands are sent
    // The owning CFindDialog and its IDC_FIND_FOUND_FILES child are Unicode windows, so
    // template caption ("Fo&und Items: (%d)") is delivered as genuine wide text to this attached
    // control - narrow GetWindowText would round-trip a translated caption through CP_ACP for
    // no reason. This control is self-painted (WM_ERASEBKGND draws Text directly via DrawTextW,
    // no WM_SETTEXT marshalling), so it's free to hold/paint wide regardless of its own class.
    WCHAR Text[200];
    int FoundCount;
    int ErrorsCount;
    int InfosCount;
    HICON HWarningIcon;
    HICON HInfoIcon;
    HICON HEmptyIcon;
    BOOL WarningDisplayed;
    BOOL InfoDisplayed;
    int FlashIconCounter;
    BOOL StopFlash;

public:
    CFindTBHeader(HWND hDlg, int ctrlID);

    void SetNotifyWindow(HWND hWnd) { HNotifyWindow = hWnd; }

    int GetNeededHeight();

    BOOL EnableItem(DWORD position, BOOL byPosition, BOOL enabled);

    void SetFoundCount(int foundCount);
    void SetErrorsInfosCount(int errorsCount, int infosCount);

    void OnColorsChange();

    BOOL CreateLogToolbar(BOOL errors, BOOL infos);

    void StartFlashIcon();
    void StopFlashIcon();

    void SetFont();

protected:
    virtual LRESULT WindowProc(UINT uMsg, WPARAM wParam, LPARAM lParam);
};

//*********************************************************************************
//
// CFindDialog
//

enum CCopyNameToClipboardModeEnum
{
    cntcmFullName,
    cntcmName,
    cntcmFullPath,
    cntcmUNCName,
};

enum CStateOfFindCloseQueryEnum
{
    sofcqNotUsed,     // idle, nothing is happening
    sofcqSentToFind,  // request sent; the response has not arrived yet or the user has not answered "stop searching?"
    sofcqCanClose,    // the Find window can be closed
    sofcqCannotClose, // the Find window cannot be closed
};

class CButton;

class CFindDialog : public CCommonDialog
{
protected:
    // Seed captured at construction time from the opening panel and applied after
    // the framework's initial transfer so the active panel wins over saved history.
    sally::find::LookInSeed InitialLookInSeed;
    // DEFAULT_CHARSET clone of the dialog font, made only when the Look-in text needs
    // glyphs the dialog font's charset cannot supply. Owned; freed on WM_DESTROY.
    HFONT LookInUnicodeFont;

    // data needed for laying out the dialog
    BOOL FirstWMSize;
    int VMargin; // space on the left and right between the dialog frame and controls
    int HMargin; // space below between buttons and the status bar
    int ButtonW; // button width
    int ButtonH;
    int RegExpButtonW; // size of the RegExpBrowse button
    int RegExpButtonY; // position of the RegExpBrowse button
    int MenuBarHeight; // height of the menu bar
    int StatusHeight;  // height of the status bar
    int ResultsY;      // position of the results list
    int AdvancedY;     // position of the Advanced button
    int AdvancedTextY; // position of the text after the Advanced button
    int AdvancedTextX; // position of the text after the Advanced button
    int FindTextY;     // position of the header above the results
    int FindTextH;     // height of the header
    int CombosX;       // position of the comboboxes
    int CombosH;       // height of the comboboxes
    int BrowseY;       // position of the Browse button
    int Line2X;        // position of the separator line of Search file content
    int FindNowY;      // position of the Find now button
    int SpacerH;       // height by which the dialog shrinks or expands

    BOOL Expanded; // dialog is expanded - the SearchFileContent items are visible

    int MinDlgW; // minimal width of the Find dialog
    int MinDlgH; // minimal height

    int FileNameFormat; // how to adjust filenames after reading from disk, taken from global configuration due to synchronization issues
    BOOL SkipCharacter; // prevents a beep when Alt+Enter is pressed in Find

    // additional data
    BOOL DlgFailed;
    CMenuPopup* MainMenu;
    CMenuBar* MenuBar;
    HWND HStatusBar;
    HWND HProgressBar; // status bar child window shown for certain operations in a special field
    BOOL TwoParts;     // does the status bar have two texts?
                       //    CFindAdvancedDialog FindAdvanced;
    CFoundFilesListView* FoundFilesListView;
    std::wstring FoundFilesDataTextBufferW; // for obtaining text from CFoundFilesData::GetTextW
    CFindTBHeader* TBHeader;
    BOOL SearchInProgress;
    BOOL CanClose; // the window can be closed (we are not inside a method of this object)
    HANDLE GrepThread;
    CGrepData GrepData;
    CSearchingString SearchingText;
    CSearchingString SearchingText2;
    BOOL UpdateStatusBar;
    IContextMenu2* ContextMenu;
    CFindDialog** ZeroOnDestroy; // the pointer will be zeroed on destruction
    CButton* OKButton;

    BOOL OleInitialized;

    TIndirectArray<CSearchForData> SearchForData; // list of directories and masks that will be searched

    // a single item worked with by both the Find dialog and the Advanced dialog
    CFindOptionsItem Data;

    BOOL ProcessingEscape; // the message loop is currently handling ESCAPE -- if
                           // IDCANCEL is generated, a confirmation is shown

    CFindLog Log; // storage for errors and information

    CBitmap* CacheBitmap; // used when drawing the path

    BOOL FlashIconsOnActivation; // flash the status icons when we get activated

    std::wstring FindNowText;

public:
    CStateOfFindCloseQueryEnum StateOfFindCloseQuery; // main thread asks the Find thread whether the window can close; unsynchronized, used only during shutdown, more than enough...

public:
    CFindDialog(HWND hCenterAgainst, const wchar_t* initPath);
    ~CFindDialog();

    virtual void Validate(CTransferInfo& ti);
    virtual void Transfer(CTransferInfo& ti);

    BOOL IsGood() { return TRUE; }

    void SetZeroOnDestroy(CFindDialog** zeroOnDestroy) { ZeroOnDestroy = zeroOnDestroy; }

    BOOL GetFocusedFile(std::wstring& fullName, int* viewedIndex /* can be NULL */);
    const wchar_t* GetName(int index);
    const wchar_t* GetPath(int index);
    void UpdateInternalViewerData();

    BOOL IsSearchInProgress() { return SearchInProgress; }

    void OnEnterIdle();

    // If the message is translated, the return value is TRUE.
    BOOL IsMenuBarMessage(CONST MSG* lpMsg);

    // allocates the appropriate data for selected items
    HGLOBAL CreateHDrop();
    HGLOBAL CreateShellIdList();

    // the main window calls this method for all Find dialogs - colors have changed
    void OnColorsChange();

    void SetProcessingEscape(BOOL value) { ProcessingEscape = value; }

    // allows processing Alt+C and other hotkeys that belong to hidden controls:
    // if the dialog is collapsed and a hidden control's hot key is pressed,
    // the dialog expands. Always returns FALSE
    BOOL ManageHiddenShortcuts(const MSG* msg);

protected:
    void GetLayoutParams();
    void LayoutControls(); // arranges controls within the dialog

    void SetTwoStatusParts(BOOL two, BOOL force = FALSE); // sets one or two status bar parts; sizes are adjusted according to the status bar length

    void SetContentVisible(BOOL visible);
    void UpdateAdvancedText();

    void LoadControls(int index); // loads the item from Items[index] into the dialog

    void StartSearch(WORD command);
    void StopSearch();

    void BuildSerchForData(); // fills the SearchForData list

    void EnableControls(BOOL nextIsButton = FALSE);
    void EnableToolBar();

    void InsertDrives(HWND hEdit, BOOL network); // fills hEdit with a list of fixed drives (and network drives if requested)

    void UpdateListViewItems();

    void OnContextMenu(int x, int y);
    void OnFocusFile();
    void OnViewFile(BOOL alternate);
    void OnEditFile();
    void OnViewFileWith();
    void OnEditFileWith();
    void OnHideSelection();
    void OnHideDuplicateNames();
    void OnSaveResults();
    void OnLoadResults();
    void OnDelete(BOOL toRecycle);
    void OnSelectAll();
    void OnInvertSelection();
    void OnShowLog();
    void OnOpen(BOOL onlyFocused);
    void UpdateStatusText(); // if we are not in search mode, displays the count and total size of selected items

    // OLE clipboard operations

    // creates a context menu for the selected items and calls ContextMenuInvoke for the specified lpVerb
    // returns TRUE if Invoke was called, otherwise returns FALSE if something fails
    BOOL InvokeContextMenu(const wchar_t* lpVerb);

    void OnCutOrCopy(BOOL cut);
    void OnDrag(BOOL rightMouseButton);

    void OnProperties();

    void OnUserMenu();

    void OnCopyNameToClipboard(CCopyNameToClipboardModeEnum mode);

    virtual INT_PTR DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam);

    // Iterates over selected list-view items and returns their common parent prefix.
    BOOL GetCommonPrefixPath(std::wstring& prefix, int& commonPrefixChars);

    // Returns the common prefix and each selected item's UTF-16 path relative to that prefix.
    BOOL GetCommonPrefixPathW(std::wstring& prefix, std::vector<std::wstring>& namesW);

    BOOL InitializeOle();
    void UninitializeOle();

    BOOL CanCloseWindow();

    BOOL DoYouWantToStopSearching();

    void SetFullRowSelect(BOOL fullRow);

    friend class CFoundFilesListView;
};

class CFindDialogQueue : public CWindowQueue
{
public:
    // wchar_t, matching CWindowQueue's own base-class parameter - was hardcoded
    // const char*, which no longer converts once wchar_t resolves to wchar_t under _UNICODE.
    CFindDialogQueue(const wchar_t* queueName) : CWindowQueue(queueName) {}

    void AddToArray(TDirectArray<HWND>& arr);
};

//*********************************************************************************
//
// externs
//

// Open the Find dialog seeded with the panel's UTF-16 path.
BOOL OpenFindDialog(HWND hCenterAgainst, const wchar_t* initPath);

extern CFindOptions FindOptions;
extern CFindIgnore FindIgnore;
extern CFindDialogQueue FindDialogQueue; // list of all Find dialogs
