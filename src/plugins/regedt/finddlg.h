// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <string>
#include <vector>

extern std::vector<std::wstring> PatternHistory;
extern std::vector<std::wstring> LookInHistory;

//*********************************************************************************
//
// CStatusBar
//

class CStatusBar : public CWindow
{
protected:
    std::wstring Text;
    size_t BaseLen;
    BOOL Dirty;
    BOOL UpdateInIdle;
    CCS Section; // critical section for accessing the text buffer
    int Width;
    int TextWidth;
    int Height;
    HBITMAP HBitmap;

public:
    CStatusBar();
    virtual ~CStatusBar();
    void AllocateBitmap();

    // set the base to which Set appends additional text
    void SetBase(LPCWSTR text, BOOL updateInIdle = FALSE);
    // append to the base set via SetBase
    void Set(LPCWSTR text, BOOL updateInIdle = FALSE);
    void OnEnterIdle();

    virtual LRESULT WindowProc(UINT uMsg, WPARAM wParam, LPARAM lParam);
};

//*********************************************************************************
//
// CFoundFilesData
// CFoundFilesListView
//

// column indices

#define CI_NAME 0
#define CI_TYPE 1
#define CI_DATA 2
#define CI_PATH 3
#define CI_SIZE 4
#define CI_DATE 5
#define CI_TIME 6

struct CFoundFilesData
{
    std::wstring Name;
    std::wstring Path;
    DWORD Type;
    DWORD Size;
    DWORD_PTR Data;
    unsigned Allocated : 1;
    FILETIME Time;
    BOOL IsDir;
    DWORD State;          // stores the item state
    unsigned Default : 1; // denotes the default item

    CFoundFilesData()
    {
        Data = NULL;
        Allocated = 0;
        IsDir = FALSE;
    }
    CFoundFilesData(LPWSTR name, int root, LPWSTR key, DWORD type,
                    DWORD size, unsigned char* data,
                    FILETIME time, BOOL isDir);
    ~CFoundFilesData()
    {
        if (Allocated && Data)
            free((void*)Data);
    }
    LPCWSTR GetText(int i, std::wstring& buffer);
};

#define SYMBOL_CX 16 // bitmap dimensions
#define SYMBOL_CY 16

#define ICON_CX 16 // icon dimensions in panels and in the cache bitmap
#define ICON_CY 16

class CFindDialog;

class CFoundFilesListView : public CWindow
{
protected:
    TIndirectArray<CFoundFilesData> Data;
    CCS DataCriticalSection; // critical section for accessing the data
    CFindDialog* SearchDialog;

public:
    CFoundFilesListView(CFindDialog* searchDialog);
    virtual ~CFoundFilesListView();

    void StoreItemsState();
    void RestoreItemsState();

    int CompareFunc(CFoundFilesData* f1, CFoundFilesData* f2, int sortBy);
    void QuickSort(int left, int right, int sortBy);
    void SortItems(int sortBy);

    // interface pro Data
    CFoundFilesData* At(int index);
    void DestroyMembers();
    int GetCount();
    int Add(CFoundFilesData* item);
    BOOL IsGood();

    BOOL InitColumns();
    void GetContextMenuPos(POINT* p);

    virtual LRESULT WindowProc(UINT uMsg, WPARAM wParam, LPARAM lParam);
};

/*
//******************************************************************************
//
// CComboboxEdit
//
// Because the combo box is emptied, the classic way (CB_GETEDITSEL) cannot
// determine what the selection was after it loses focus. This control handles it.
//

class CComboboxEdit: public CWindow
{
  protected:
    DWORD SelStart;
    DWORD SelEnd;

  public:
    CComboboxEdit();

    void GetSel(DWORD *start, DWORD *end);

    void ReplaceText(const char *text);

  protected:
    virtual LRESULT WindowProc(UINT uMsg, WPARAM wParam, LPARAM lParam);
};
*/

//*********************************************************************************
//
// CFindDialog
//

#define IDT_REFRESH_LISTVIEW 1

class CFindDialog : public CDialogEx
{
protected:
    std::wstring LookInInit;

    // layout parameters
    int MinDlgW;
    int MinDlgH;
    int HMargin;
    int VMargin;
    int CombosH;
    int CombosX;
    int FindNowW;
    INT FindNowY;
    int OptionsY;
    int OptionsH;
    int OptionsW;
    int ResultsY;
    int StatusHeight;
    int AddY;
    int FoundItemsH;

    CStatusBar* StatusBar;
    CFoundFilesListView* List;
    CFindDialog** ZeroOnDestroy; // the pointer value will be cleared during destruction
    //CComboboxEdit * LookIn;

    // query parameters
    std::wstring Pattern;
    std::vector<std::wstring> LookInList;
    BOOL IncludeSubkeys;
    BOOL LookAtKeys;
    BOOL LookAtValues;
    BOOL LookAtData;
    BOOL Hex;
    BOOL CaseSensitive;
    BOOL WholeWords;
    BOOL RegExp;
    BOOL UseMinTime, UseMaxTime;
    SYSTEMTIME MinTime, MaxTime;

    BOOL SearchInProgress;
    HANDLE CancelEvent;
    int FoundVisibleCount;
    DWORD NextUpdate;
    BOOL CloseWhenSearchFinishes;
    std::wstring LVItemTextBuffer;
    BOOL Stopped;
    BOOL ShowOptions;

public:
    explicit CFindDialog(const wchar_t* lookInInit);
    virtual ~CFindDialog() { ; }
    void SetZeroOnDestroy(CFindDialog** zeroOnDestroy) { ZeroOnDestroy = zeroOnDestroy; }

    void GetLayoutParams();
    int GetMinDlgH() { return ShowOptions ? MinDlgH : MinDlgH - ResultsY + (int)(OptionsY + OptionsH * 1.1); }
    void LayoutControls(BOOL showOrHideControls);

    void EnableControls(BOOL enable);
    void UpdateListViewItems();
    void UpdateStatusText(BOOL searchFinished = FALSE);
    void OnEnterIdle();

    void StartSearch();
    void StopSearch();
    virtual void Validate(CTransferInfoEx& ti);
    virtual void Transfer(CTransferInfoEx& ti);

protected:
    virtual INT_PTR DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam);

    friend class CFindThread;
};

class CFindDialogThread : public CThread
{
    std::wstring LookIn;

public:
    explicit CFindDialogThread(const wchar_t* lookIn)
        : CThread(L"CFindDialogThread"), LookIn(lookIn != NULL ? lookIn : L"") {}
    virtual unsigned Body();
};

//*********************************************************************************
//
// CFindThread
//

class CFindThread : public CThread
{
    std::vector<std::wstring> LookIn;
    std::string PatternA;
    std::vector<char> PatternW;
    int PatternALen, PatternWLen;
    BOOL IncludeSubkeys;
    BOOL LookAtKeys;
    BOOL LookAtValues;
    BOOL LookAtData;
    BOOL CaseSensitive;
    BOOL WholeWords;
    BOOL RegExp;
    QWORD Number;
    BOOL UseNumber;
    BOOL UseMinTime, UseMaxTime;
    SYSTEMTIME MinTime, MaxTime;
    CFindDialog* FindDialog;
    HANDLE CancelEvent;

    // used to translate a Unicode string when searching with a regexp
    std::string AsciiBuffer;

    CSalamanderBMSearchData* BMForPatternA;
    CSalamanderBMSearchData* BMForPatternW;
    CSalamanderREGEXPSearchData* SalRegExp;

    DWORD NextStatusUpdate;

public:
    CFindThread(const std::vector<std::wstring>& lookIn,
                const std::string& patternA,
                const std::vector<char>& patternW,
                BOOL includeSubkeys, BOOL lookAtKeys, BOOL lookAtValues,
                BOOL lookAtData, BOOL caseSensitive, BOOL wholeWords,
                BOOL regExp, QWORD number, BOOL useNumber,
                BOOL useMinTime, BOOL useMaxTime,
                SYSTEMTIME& minTime, SYSTEMTIME& maxTime,
                CFindDialog* findDlg, HANDLE cancelEvent)
        : CThread(L"CFindThread"), LookIn(lookIn)
    {
        PatternA = patternA;
        PatternW = patternW;
        PatternALen = static_cast<int>(PatternA.size());
        PatternWLen = static_cast<int>(PatternW.size());
        IncludeSubkeys = includeSubkeys;
        LookAtKeys = lookAtKeys;
        LookAtValues = lookAtValues;
        LookAtData = lookAtData;
        CaseSensitive = caseSensitive;
        WholeWords = wholeWords;
        RegExp = regExp;
        Number = number;
        UseNumber = useNumber;
        UseMinTime = useMinTime;
        UseMaxTime = useMaxTime;
        MinTime = minTime;
        MaxTime = maxTime;
        FindDialog = findDlg;
        CancelEvent = cancelEvent;
        BMForPatternA = NULL;
        BMForPatternW = NULL;
        SalRegExp = NULL;
    }

    ~CFindThread()
    {
        if (BMForPatternA)
            SG->FreeSalamanderBMSearchData(BMForPatternA);
        if (BMForPatternW && BMForPatternA != BMForPatternW)
            SG->FreeSalamanderBMSearchData(BMForPatternW);
        if (SalRegExp)
            SG->FreeSalamanderREGEXPSearchData(SalRegExp);
    }

    BOOL TestTime(FILETIME& ft);
    BOOL Test(char* text, int len, BOOL name, DWORD type);
    BOOL ScanKeyAux(int root, std::wstring& key, BOOL& skip, BOOL& skipAllErrors,
                    std::vector<std::wstring>& stack);
    BOOL ScanKey(int root, std::wstring& key, BOOL& skip, BOOL& skipAllErrors,
                 std::vector<std::wstring>& stack);
    BOOL ScanRegistry(BOOL& skipAllErrors);
    BOOL Init();
    unsigned BodyCore();
    virtual unsigned Body();
};
