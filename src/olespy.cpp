// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"

#include "common/DiagnosticTextEncoding.h"

#include "olespy.h"

#ifdef _DEBUG

//-------------------------------------------------------------------------
//
// Notes on the implementation of IMallocSpy
//
// 1) OLE caches (among other things) BSTRs, so under a normal OS it returns
//    nonsensical leaks via IMallocSpy. Caching can be disabled by setting the environment
//    variable OANOCACHE=1 and using the debug (Checked) version of OLE. Ideally,
//    use the complete Checked Build of W2K or extract the appropriate DLL from the
//    Checked version of the Service Pack which is freely available.
//
// 2) If there are any remaining leaks in OLE, when trying to unregister the spy via
//    CoRevokeMallocSpy, it returns E_ACCESSDENIED value and postpones the Release call.
//    In practice, Release is never going to call us (W2K). Because some allocations are
//    freed only during DLL_PROCESS_DETACH in OLEAUT32.DLL, the spy reports
//    them as leaks. It is useful to compare the reported leaks with what the
//    CairOLE system outputs to the debug window under the W2K Checked Build.
//
// 3) Two issues had to be avoided: we do not want the CRT to report a memory
//    leak caused by allocating the CMallocSpy instance on the heap. At the same
//    time, we need to keep the instance of this object alive as long as possible because OLE will
//    call our methods (especially PreFree and PostFree with fSpy == FALSE) even
//    after CoRevokeMallocSpy is called. We solve it by copying the object into static
//    memory in OleSpyRegister(). The destructor is thus never called and the
//    critical section is never destroyed.
//
// 4) When testing under W2K Checked Build, more reports can be obtained from OLE by
//    adding a section to win.ini file:
//    [CairOLE InfoLevels]
//    cairole=7
//    heap=7
//
//    if we want to display detailed messages including call stacks of individual allocations
//    we have to set both values to the decimal equivalent of 0xffffffff:
//    [CairOLE InfoLevels]
//    cairole=4294967295
//    heap=4294967295
//
//    CairOLE often reports a higher amount of leaks than our spy detects. It is also common
//    that CairOLE reports leaks while the spy reports that everything is OK. What
//    matters is that these leaks are consistent (for example on Salamander
//    start/exit CairOLE reports 13 leaks and about 800 bytes of memory). Since this number
//    does not increase, I consider it unimportant.
//
// 5) The spy will report leaks from calls to functions like SHBrowseForFolder or
//    SHGetSpecialFolderLocation. These are not real leaks but cached PIDLs which
//    shell32.dll frees only when it detaches from the process. We can recognize
//    them by the fact that repeated calls do not increase the number of leaks.
//

//-------------------------------------------------------------------------

#define SHID_JUNCTION 0x80

#define SHID_GROUPMASK 0x70
#define SHID_TYPEMASK 0x7f
#define SHID_INGROUPMASK 0x0f

#define SHID_ROOT 0x10
#define SHID_ROOT_REGITEM 0x1f // Mail

#if ((DRIVE_REMOVABLE | DRIVE_FIXED | DRIVE_REMOTE | DRIVE_CDROM | DRIVE_RAMDISK) != 0x07)
#error Definitions of DRIVE_* are changed!
#endif

#define SHID_COMPUTER 0x20
#define SHID_COMPUTER_1 0x21                             // free
#define SHID_COMPUTER_REMOVABLE (0x20 | DRIVE_REMOVABLE) // 2
#define SHID_COMPUTER_FIXED (0x20 | DRIVE_FIXED)         // 3
#define SHID_COMPUTER_REMOTE (0x20 | DRIVE_REMOTE)       // 4
#define SHID_COMPUTER_CDROM (0x20 | DRIVE_CDROM)         // 5
#define SHID_COMPUTER_RAMDISK (0x20 | DRIVE_RAMDISK)     // 6
#define SHID_COMPUTER_7 0x27                             // free
#define SHID_COMPUTER_DRIVE525 0x28                      // 5.25 inch floppy disk drive
#define SHID_COMPUTER_DRIVE35 0x29                       // 3.5 inch floppy disk drive
#define SHID_COMPUTER_NETDRIVE 0x2a                      // Network drive
#define SHID_COMPUTER_NETUNAVAIL 0x2b                    // Network drive that is not restored.
#define SHID_COMPUTER_C 0x2c                             // free
#define SHID_COMPUTER_D 0x2d                             // free
#define SHID_COMPUTER_REGITEM 0x2e                       // Controls, Printers, ...
#define SHID_COMPUTER_MISC 0x2f                          // Unknown drive type

#define SHID_FS 0x30
#define SHID_FS_TYPEMASK 0x3F
#define SHID_FS_DIRECTORY 0x31   // CHICAGO
#define SHID_FS_FILE 0x32        // FOO.TXT
#define SHID_FS_UNICODE 0x34     // Is it unicode? (this is a bitmask)
#define SHID_FS_DIRUNICODE 0x35  // Folder with a unicode name
#define SHID_FS_FILEUNICODE 0x36 // File with a unicode name

#define SHID_NET 0x40
#define SHID_NET_DOMAIN (SHID_NET | RESOURCEDISPLAYTYPE_DOMAIN)
#define SHID_NET_SERVER (SHID_NET | RESOURCEDISPLAYTYPE_SERVER)
#define SHID_NET_SHARE (SHID_NET | RESOURCEDISPLAYTYPE_SHARE)
#define SHID_NET_FILE (SHID_NET | RESOURCEDISPLAYTYPE_FILE)
#define SHID_NET_GROUP (SHID_NET | RESOURCEDISPLAYTYPE_GROUP)
#define SHID_NET_NETWORK (SHID_NET | RESOURCEDISPLAYTYPE_NETWORK)
#define SHID_NET_RESTOFNET (SHID_NET | RESOURCEDISPLAYTYPE_ROOT)
#define SHID_NET_SHAREADMIN (SHID_NET | RESOURCEDISPLAYTYPE_SHAREADMIN)
#define SHID_NET_DIRECTORY (SHID_NET | RESOURCEDISPLAYTYPE_DIRECTORY)
#define SHID_NET_TREE (SHID_NET | RESOURCEDISPLAYTYPE_TREE)
#define SHID_NET_REGITEM 0x4e // Remote Computer items
#define SHID_NET_PRINTER 0x4f // \\PYREX\LASER1

#define SIL_GetType(pidl) (ILIsEmpty(pidl) ? 0 : (pidl)->mkid.abID[0])
#define FS_IsValidID(pidl) ((SIL_GetType(pidl) & SHID_GROUPMASK) == SHID_FS)
#define NET_IsValidID(pidl) ((SIL_GetType(pidl) & SHID_GROUPMASK) == SHID_NET)
#define ROOT_IsValidID(pidl) ((SIL_GetType(pidl) & SHID_GROUPMASK) == SHID_ROOT)

// unsafe macros
#define _ILSkip(pidl, cb) ((LPITEMIDLIST)(((BYTE*)(pidl)) + cb))
#define _ILNext(pidl) _ILSkip(pidl, (pidl)->mkid.cb)
/*
#define IS_VALID_READ_PTR(ptr, type) (!IsBadReadPtr((ptr), sizeof(type)))
#define IS_VALID_READ_BUFFER(ptr, type, len) (!IsBadReadPtr((ptr), sizeof(type)*(len)))
#define IS_VALID_PIDL(ptr) (IsValidPIDL(ptr))

// IsBadReadPtr throws exceptions in the debug version and we don't want that
BOOL
IsValidPIDL(LPCITEMIDLIST pidl)
{
  return (IS_VALID_READ_PTR(pidl, USHORT) &&
          IS_VALID_READ_BUFFER((LPBYTE)pidl+sizeof(USHORT), BYTE, pidl->mkid.cb) &&
          (0 == _ILNext(pidl)->mkid.cb || IS_VALID_PIDL(_ILNext(pidl))));
}
*/

// IsBadReadPtr throws exceptions in the debug version and we don't want that
BOOL IsGoodPIDL(LPCITEMIDLIST pidl, int cb)
{
    if (cb < sizeof(USHORT) || cb < (int)(pidl->mkid.cb + sizeof(USHORT)))
        return FALSE;

    if (pidl->mkid.cb > 512)
        return FALSE;

    if (_ILNext(pidl)->mkid.cb == 0) // terminator == valid
        return TRUE;

    cb -= pidl->mkid.cb;

    return IsGoodPIDL(_ILNext(pidl), cb);
}

const wchar_t* DumpPidl(LPCITEMIDLIST pidl) noexcept
{
    static std::wstring text;
    text.clear();
    try
    {
        if (NULL == pidl)
        {
            text = L"Empty pidl";
            return text.c_str();
        }

        while (!ILIsEmpty(pidl))
        {
            wchar_t fragment[32]; // bounded "cb:%x id:" diagnostic fragment
            const USHORT cb = pidl->mkid.cb;
            _snwprintf_s(fragment, _TRUNCATE, L"cb:%x id:", cb);
            text += fragment;

            LPCWSTR typeText;
            switch (SIL_GetType(pidl) & SHID_TYPEMASK)
            {
            case SHID_ROOT: typeText = L"SHID_ROOT"; break;
            case SHID_ROOT_REGITEM: typeText = L"SHID_ROOT_REGITEM"; break;
            case SHID_COMPUTER: typeText = L"SHID_COMPUTER"; break;
            case SHID_COMPUTER_1: typeText = L"SHID_COMPUTER_1"; break;
            case SHID_COMPUTER_REMOVABLE: typeText = L"SHID_COMPUTER_REMOVABLE"; break;
            case SHID_COMPUTER_FIXED: typeText = L"SHID_COMPUTER_FIXED"; break;
            case SHID_COMPUTER_REMOTE: typeText = L"SHID_COMPUTER_REMOTE"; break;
            case SHID_COMPUTER_CDROM: typeText = L"SHID_COMPUTER_CDROM"; break;
            case SHID_COMPUTER_RAMDISK: typeText = L"SHID_COMPUTER_RAMDISK"; break;
            case SHID_COMPUTER_7: typeText = L"SHID_COMPUTER_7"; break;
            case SHID_COMPUTER_DRIVE525: typeText = L"SHID_COMPUTER_DRIVE525"; break;
            case SHID_COMPUTER_DRIVE35: typeText = L"SHID_COMPUTER_DRIVE35"; break;
            case SHID_COMPUTER_NETDRIVE: typeText = L"SHID_COMPUTER_NETDRIVE"; break;
            case SHID_COMPUTER_NETUNAVAIL: typeText = L"SHID_COMPUTER_NETUNAVAIL"; break;
            case SHID_COMPUTER_C: typeText = L"SHID_COMPUTER_C"; break;
            case SHID_COMPUTER_D: typeText = L"SHID_COMPUTER_D"; break;
            case SHID_COMPUTER_REGITEM: typeText = L"SHID_COMPUTER_REGITEM"; break;
            case SHID_COMPUTER_MISC: typeText = L"SHID_COMPUTER_MISC"; break;
            case SHID_FS: typeText = L"SHID_FS"; break;
            case SHID_FS_TYPEMASK: typeText = L"SHID_FS_TYPEMASK"; break;
            case SHID_FS_DIRECTORY: typeText = L"SHID_FS_DIRECTORY"; break;
            case SHID_FS_FILE: typeText = L"SHID_FS_FILE"; break;
            case SHID_FS_UNICODE: typeText = L"SHID_FS_UNICODE"; break;
            case SHID_FS_DIRUNICODE: typeText = L"SHID_FS_DIRUNICODE"; break;
            case SHID_FS_FILEUNICODE: typeText = L"SHID_FS_FILEUNICODE"; break;
            case SHID_NET: typeText = L"SHID_NET"; break;
            case SHID_NET_DOMAIN: typeText = L"SHID_NET_DOMAIN"; break;
            case SHID_NET_SERVER: typeText = L"SHID_NET_SERVER"; break;
            case SHID_NET_SHARE: typeText = L"SHID_NET_SHARE"; break;
            case SHID_NET_FILE: typeText = L"SHID_NET_FILE"; break;
            case SHID_NET_GROUP: typeText = L"SHID_NET_GROUP"; break;
            case SHID_NET_NETWORK: typeText = L"SHID_NET_NETWORK"; break;
            case SHID_NET_RESTOFNET: typeText = L"SHID_NET_RESTOFNET"; break;
            case SHID_NET_SHAREADMIN: typeText = L"SHID_NET_SHAREADMIN"; break;
            case SHID_NET_DIRECTORY: typeText = L"SHID_NET_DIRECTORY"; break;
            case SHID_NET_TREE: typeText = L"SHID_NET_TREE"; break;
            case SHID_NET_REGITEM: typeText = L"SHID_NET_REGITEM"; break;
            case SHID_NET_PRINTER: typeText = L"SHID_NET_PRINTER"; break;
            default: typeText = L"unknown"; break;
            }
            text += typeText;

            if (SIL_GetType(pidl) & SHID_JUNCTION)
                text += L", junction";

            pidl = _ILNext(pidl);

            if (!ILIsEmpty(pidl))
                text += L"; ";
        }
        return text.c_str();
    }
    catch (...)
    {
        text.clear();
        return L"<pidl dump unavailable>";
    }
}

//-------------------------------------------------------------------------

void _OutputDebugString(BOOL useTServer, const wchar_t* text)
{
    if (useTServer)
        TRACE_IW(text);
    OutputDebugStringW(text);
    OutputDebugStringW(L"\n");
}

// Narrow overload so callers building ANSI diagnostic text (e.g. DumpLeaks'
// sprintf-based buffers) don't need to convert at the call site themselves -
// DumpLeaks uses __try/__except, and MSVC rejects a std::wstring temporary
// (or any object needing unwinding) anywhere in a function that does (C2712).
// Converting here, in a plain function, keeps that temporary out of DumpLeaks.
void _OutputDebugString(BOOL useTServer, const char* text) noexcept
{
    try
    {
        std::wstring wide;
        if (!sally::diagnostic::DecodeAcp(text, wide))
        {
            _OutputDebugString(useTServer, L"<OLE byte diagnostic unavailable>");
            return;
        }
        _OutputDebugString(useTServer, wide.c_str());
    }
    catch (...)
    {
        _OutputDebugString(useTServer, L"<OLE byte diagnostic unavailable>");
    }
}

static void OutputOleSpyWideValue(BOOL useTServer, const wchar_t* prefix,
                                  const wchar_t* value,
                                  const wchar_t* suffix = L"") noexcept
{
    try
    {
        std::wstring text = prefix != NULL ? prefix : L"";
        if (value != NULL)
            text += value;
        if (suffix != NULL)
            text += suffix;
        _OutputDebugString(useTServer, text.c_str());
    }
    catch (...)
    {
        _OutputDebugString(useTServer, L"<OLE diagnostic text unavailable>");
    }
}

static void OutputOleSpyNarrowValue(BOOL useTServer, const char* prefix,
                                    const char* value, size_t valueCapacity,
                                    const char* suffix = "") noexcept
{
    try
    {
        std::string text = prefix != NULL ? prefix : "";
        if (value != NULL)
        {
            const size_t length = strnlen(value, valueCapacity);
            text.append(value, length);
        }
        if (suffix != NULL)
            text += suffix;
        _OutputDebugString(useTServer, text.c_str());
    }
    catch (...)
    {
        _OutputDebugString(useTServer, L"<OLE byte diagnostic unavailable>");
    }
}

static const wchar_t* GetPidlFileSystemPath(LPCITEMIDLIST pidl) noexcept
{
    static std::wstring path;
    path.clear();
    PWSTR allocatedPath = NULL;
    const HRESULT result = SHGetNameFromIDList(pidl, SIGDN_FILESYSPATH, &allocatedPath);
    if (FAILED(result) || allocatedPath == NULL)
        return NULL;
    try
    {
        path = allocatedPath;
    }
    catch (...)
    {
        path.clear();
    }
    CoTaskMemFree(allocatedPath);
    return path.empty() ? NULL : path.c_str();
}

static const wchar_t* GetPidlDisplayName(LPCITEMIDLIST pidl) noexcept
{
    static std::wstring name;
    name.clear();
    IShellFolder* desktopFolder = NULL;
    if (FAILED(SHGetDesktopFolder(&desktopFolder)) || desktopFolder == NULL)
        return NULL;

    STRRET str = {};
    const HRESULT displayResult = desktopFolder->GetDisplayNameOf(
        const_cast<LPITEMIDLIST>(pidl), SHGDN_NORMAL, &str);
    desktopFolder->Release();
    if (FAILED(displayResult))
        return NULL;

    PWSTR allocatedName = str.uType == STRRET_WSTR ? str.pOleStr : NULL;
    try
    {
        if (allocatedName != NULL)
            name = allocatedName;
        else
        {
            const char* legacyName = str.uType == STRRET_CSTR
                                         ? str.cStr
                                         : (str.uType == STRRET_OFFSET && pidl != NULL
                                                ? reinterpret_cast<const char*>(pidl) + str.uOffset
                                                : NULL);
            if (legacyName != NULL)
                sally::diagnostic::DecodeAcp(legacyName, name);
        }
    }
    catch (...)
    {
        name.clear();
    }
    if (allocatedName != NULL)
        CoTaskMemFree(allocatedName);
    return name.empty() ? NULL : name.c_str();
}

//-------------------------------------------------------------------------

#define ASSERT(exp) (void)((exp) || (TRACE_E(#exp), DebugBreak(), 0))

#define SPYSIG 0x66FFAA55

// our helper data block placed before the actual allocated data

#define SPYBLK_STACKLEN 100 // number of characters from the call stack including the terminator

struct SPYBLK
{
    DWORD dwSig;                       // signature
    SPYBLK* psbNext;                   // next allocated block
    SPYBLK* psbPrev;                   // previous allocated block
    SIZE_T cbRequest;                  // required size
    DWORD cRealloc;                    // how many times the item was reallocated
    DWORD cOrder;                      // overall allocation count (used for breakpoints)
    DWORD dwThreadId;                  // which thread leaked?
    wchar_t szStackHead[SPYBLK_STACKLEN]; // the first line from the call stack
    wchar_t szStackTail[SPYBLK_STACKLEN]; // the last line from the call stack
};

class CMallocSpy : public IMallocSpy
{
private:
    ULONG _ulRef;      // number of CMallocSpy object references
    SIZE_T _iAllocs;   // number of allocations
    SIZE_T _iBytes;    // currently allocated size
    SPYBLK* _psbHead;  // first in the list of allocations
    SIZE_T _cbRequest; // cache from PRE -> POST
    //void             *_pvRequest;   // cache from PRE -> POST
    LONG _iTotalAllocs;   // total number of allocations
    SIZE_T _iTotalBytes;  // total number of bytes
    SIZE_T _iPeakAllocs;  // maximum number of allocations at one time
    SIZE_T _iPeakBytes;   // maximum number of bytes at one time
    LONG _iBreakAlloc;    // allocation on which we have to trigger a break (-1 == no break)
    CRITICAL_SECTION _CS; // critical section for access to our data (for example during a dump)

public:
    CMallocSpy();
    ~CMallocSpy();

    // IUnknown methods

    STDMETHOD(QueryInterface)
    (REFIID riid, void** ppv);
    STDMETHOD_(ULONG, AddRef)
    ();
    STDMETHOD_(ULONG, Release)
    ();

    // IMallocSpy methods

    STDMETHOD_(SIZE_T, PreAlloc)
    (SIZE_T cbRequest);
    STDMETHOD_(void*, PostAlloc)
    (void* pvActual);
    STDMETHOD_(void*, PreFree)
    (void* pvRequest, BOOL fSpyed);
    STDMETHOD_(void, PostFree)
    (BOOL fSpyed);
    STDMETHOD_(SIZE_T, PreRealloc)
    (void* pvRequest, SIZE_T cbRequest, void** ppvActual, BOOL fSpyed);
    STDMETHOD_(void*, PostRealloc)
    (void* pvActual, BOOL fSpyed);
    STDMETHOD_(void*, PreGetSize)
    (void* pvRequest, BOOL fSpyed);
    STDMETHOD_(SIZE_T, PostGetSize)
    (SIZE_T cbActual, BOOL fSpyed);
    STDMETHOD_(void*, PreDidAlloc)
    (void* pvRequest, BOOL fSpyed);
    STDMETHOD_(BOOL, PostDidAlloc)
    (void* pvRequest, BOOL fSpyed, BOOL fActual);
    STDMETHOD_(void, PreHeapMinimize)
    ();
    STDMETHOD_(void, PostHeapMinimize)
    ();

    // Other methods

    void SetBreakOnAlloc(int alloc);
    BOOL DumpLeaks();

private:
    // Helper Functions

    void EnterCS();
    void LeaveCS();

    void SpyEnqueue(SPYBLK* psb);
    void SpyDequeue(SPYBLK* psb);

    LPVOID SpyPostAlloc(SPYBLK* psb);
    LPVOID SpyPreFree(void* pvRequest);
    size_t SpyPreRealloc(void* pvRequest, size_t cbRequest, void** ppv);
    LPVOID SpyPostRealloc(SPYBLK* psb);
    void SpyStoreStack(SPYBLK* psb);
};

// ******************************************************************
// ******************************************************************
// Constructor/Destructor
// ******************************************************************
// ******************************************************************

CMallocSpy::CMallocSpy()
{
    _ulRef = 0;
    _iAllocs = 0;
    _iBytes = 0;
    _psbHead = NULL;
    _cbRequest = 0;
    //_pvRequest = NULL;
    _iTotalAllocs = 0;
    _iTotalBytes = 0;
    _iPeakAllocs = 0;
    _iPeakBytes = 0;
    _iBreakAlloc = -1;

    InitializeCriticalSection(&_CS);
}

CMallocSpy::~CMallocSpy()
{
    // NOTE: this destructor will be called for templateSpy, see below
    // it must not destroy the critical section
    //  DeleteCriticalSection(&_CS);
}

// ******************************************************************
// ******************************************************************
// IUnknown support ...
// ******************************************************************
// ******************************************************************

STDMETHODIMP CMallocSpy::QueryInterface(REFIID riid, void** ppv)
{
    if (riid == IID_IUnknown || riid == IID_IMallocSpy)
    {
        *ppv = (IMallocSpy*)this;
    }
    else
    {
        *ppv = NULL;
        return E_NOINTERFACE;
    }

    AddRef();
    return S_OK;
}

STDMETHODIMP_(ULONG)
CMallocSpy::AddRef()
{
    InterlockedIncrement((LONG*)&_ulRef);
    return 2;

#if 0
  return InterlockedIncrement((LONG *)&_ulRef);
#endif
}

STDMETHODIMP_(ULONG)
CMallocSpy::Release()
{
    InterlockedDecrement((LONG*)&_ulRef);
    return 1;
#if 0
  if (InterlockedDecrement((LONG *)&_ulRef))
    return _ulRef;
  else
  {
    delete this;
    return 0;
  }
#endif
}

// ******************************************************************
// ******************************************************************
// IMallocSpy methods ...
// ******************************************************************
// ******************************************************************

STDMETHODIMP_(SIZE_T)
CMallocSpy::PreAlloc(SIZE_T cbRequest)
{
    _cbRequest = cbRequest;

    return sizeof(SPYBLK) + cbRequest;
}

STDMETHODIMP_(void*)
CMallocSpy::PostAlloc(void* pvActual)
{
    void* pv;

    pv = SpyPostAlloc((SPYBLK*)pvActual);

    return pv;
}

STDMETHODIMP_(void*)
CMallocSpy::PreFree(void* pvRequest, BOOL fSpyed)
{
    void* pv;

    if (fSpyed)
        pv = SpyPreFree(pvRequest);
    else
        pv = pvRequest;

    return pv;
}

STDMETHODIMP_(void)
CMallocSpy::PostFree(BOOL fSpyed)
{

    // These deallocations occur regularly; we won't pollute the log with them
    //  if (!fSpyed)
    //  {
    //    char buff[200];
    //    sprintf(buff, "CMallocSpy freeing a block alloced before we started. Thread 0x%X", GetCurrentThreadId());
    //    _OutputDebugString(FALSE, buff);
    //  }
}

STDMETHODIMP_(SIZE_T)
CMallocSpy::PreRealloc(void* pvRequest, SIZE_T cbRequest, void** ppvActual, BOOL fSpyed)
{
    SIZE_T cb;

    if (fSpyed)
        cb = SpyPreRealloc(pvRequest, cbRequest, ppvActual);
    else
    {
        *ppvActual = pvRequest;
        cb = cbRequest;
    }

    return cb;
}

STDMETHODIMP_(void*)
CMallocSpy::PostRealloc(void* pvActual, BOOL fSpyed)
{
    void* pv;

    if (fSpyed)
        pv = SpyPostRealloc((SPYBLK*)pvActual);
    else
        pv = pvActual;

    return pv;
}

STDMETHODIMP_(void*)
CMallocSpy::PreGetSize(void* pvRequest, BOOL fSpyed)
{
    void* pv;

    if (fSpyed)
    {
        pv = (void*)(((BYTE*)pvRequest) - sizeof(SPYBLK));
    }
    else
        pv = pvRequest;

    return pv;
}

STDMETHODIMP_(SIZE_T)
CMallocSpy::PostGetSize(SIZE_T cbActual, BOOL fSpyed)
{
    SIZE_T cb;

    if (fSpyed)
    {
        return cbActual - sizeof(SPYBLK);
    }
    else
        cb = cbActual;

    return cb;
}

STDMETHODIMP_(void*)
CMallocSpy::PreDidAlloc(void* pvRequest, BOOL fSpyed)
{
    void* pv;

    if (fSpyed)
        pv = (((BYTE*)pvRequest) - sizeof(SPYBLK));
    else
        pv = pvRequest;

    return pv;
}

STDMETHODIMP_(BOOL)
CMallocSpy::PostDidAlloc(void* pvRequest, BOOL fSpyed, BOOL fActual)
{
    return fActual;
}

STDMETHODIMP_(void)
CMallocSpy::PreHeapMinimize()
{
}

STDMETHODIMP_(void)
CMallocSpy::PostHeapMinimize()
{
}

// ******************************************************************
// ******************************************************************
// Helper Functions ...
// ******************************************************************
// ******************************************************************

void CMallocSpy::EnterCS()
{
    EnterCriticalSection(&_CS);
}

void CMallocSpy::LeaveCS()
{
    LeaveCriticalSection(&_CS);
}

void CMallocSpy::SpyEnqueue(SPYBLK* psb)
{
    EnterCS();

    _iAllocs++;
    _iBytes += psb->cbRequest;

    _iTotalAllocs++;
    _iTotalBytes += psb->cbRequest;

    // for statistical purposes only
    if (_iBytes > _iPeakBytes)
        _iPeakBytes = _iBytes;
    if (_iAllocs > _iPeakAllocs)
        _iPeakAllocs = _iAllocs;

    // insert at the beginning
    psb->psbPrev = NULL;
    psb->psbNext = _psbHead;
    if (_psbHead != NULL)
    {
        ASSERT(_psbHead->psbPrev == NULL);
        _psbHead->psbPrev = psb;
    }
    _psbHead = psb;

    psb->dwSig = SPYSIG;
    psb->cOrder = _iTotalAllocs;

    SpyStoreStack(psb);

    if (_iBreakAlloc == _iTotalAllocs)
        DebugBreak();

    LeaveCS();
}

void CMallocSpy::SpyDequeue(SPYBLK* psb)
{
    EnterCS();

    if (psb->psbPrev != NULL)
    {
        ASSERT(psb->psbPrev->psbNext != NULL);
        psb->psbPrev->psbNext = psb->psbNext;
    }

    if (psb->psbNext != NULL)
    {
        ASSERT(psb->psbNext->psbPrev != NULL);
        psb->psbNext->psbPrev = psb->psbPrev;
    }

    if (psb == _psbHead)
        _psbHead = psb->psbNext;

    _iAllocs--;
    _iBytes -= psb->cbRequest;

    LeaveCS();
}

LPVOID
CMallocSpy::SpyPostAlloc(SPYBLK* psb)
{
    psb->cbRequest = _cbRequest;
    psb->cRealloc = 0;
    psb->dwThreadId = GetCurrentThreadId();

    if (psb != NULL)
        SpyEnqueue(psb);

    return (BYTE*)psb + sizeof(SPYBLK);
}

LPVOID
CMallocSpy::SpyPreFree(void* pvRequest)
{
    if (!pvRequest)
        return NULL;

    pvRequest = (void*)(((BYTE*)pvRequest) - sizeof(SPYBLK));
    SPYBLK* psb = (SPYBLK*)pvRequest;
    if (psb->dwSig != SPYSIG)
        _OutputDebugString(FALSE, L"psb->dwSig != SPYSIG");
    SpyDequeue(psb);

    return (psb);
}

size_t
CMallocSpy::SpyPreRealloc(void* pvRequest, size_t cbRequest, void** ppv)
{
    ASSERT(pvRequest != NULL); // in this case OLE calls IMallocSpy::PreAlloc
    ASSERT(cbRequest != 0);    // in this case OLE calls IMallocSpy::PreFree

    size_t cb;

    _cbRequest = cbRequest;
    //_pvRequest = pvRequest;

    SPYBLK* psb = (SPYBLK*)(((BYTE*)pvRequest) - sizeof(SPYBLK));
    SpyDequeue(psb);
    ASSERT(psb->dwSig == SPYSIG);

    *ppv = psb;
    cb = sizeof(SPYBLK) + cbRequest;

    return cb;
}

void* CMallocSpy::SpyPostRealloc(SPYBLK* psb)
{
    ASSERT(psb != NULL);

    void* pvReturn;

    psb->cbRequest = _cbRequest;
    psb->cRealloc++;

    SpyEnqueue(psb);

    pvReturn = (BYTE*)psb + sizeof(SPYBLK);

    return pvReturn;
}

void CMallocSpy::SpyStoreStack(SPYBLK* psb)
{
    CCallStack* stack = CCallStack::GetThis();
    if (stack != NULL)
    {
        stack->Reset();
        sally::diagnostic::CopyDecodedAcp(stack->GetNextLine(), psb->szStackHead,
                                         SPYBLK_STACKLEN);
        const char* sOld = NULL;
        const char* sNew;
        do
        {
            sNew = stack->GetNextLine();
            if (sNew == NULL && sOld != NULL)
                sally::diagnostic::CopyDecodedAcp(sOld, psb->szStackTail,
                                                 SPYBLK_STACKLEN);
            sOld = sNew;
        } while (sNew != NULL);
    }
    else
    {
        psb->szStackHead[0] = 0;
        psb->szStackTail[0] = 0;
    }
}

void CMallocSpy::SetBreakOnAlloc(int alloc)
{
    _iBreakAlloc = alloc;
}

BOOL IsAsciiString(LPSTR pv, int cb)
{
    BOOL bRet = TRUE;
    while (cb && *pv && bRet)
    {
        if (*pv > '~' || *pv < ' ')
            bRet = FALSE;
        cb--;
        pv++;
    }
    return (!cb && bRet && !*pv); // we've only had ascii characters, and we're null terminated
}

BOOL CMallocSpy::DumpLeaks()
{
    EnterCS();

    // Bounded final byte sink for numeric and byte-native leak diagnostics. Semantic
    // wide values bypass it through OutputOleSpyWideValue.
    char buff[1024]; // raw storage avoids a C++ destructor near the __try below
    _OutputDebugString(TRUE, L"~~~~~~~~~~~~ CMallocSpy Report Begin ~~~~~~~~~~~~");
    _snprintf_s(buff, _countof(buff), _TRUNCATE,
                "Memory Stats: %d allocations, %Iu bytes allocated", _iTotalAllocs,
                _iTotalBytes);
    _OutputDebugString(TRUE, buff);
    _snprintf_s(buff, _countof(buff), _TRUNCATE,
                "Memory Peaks: %Iu allocations; %Iu bytes allocated", _iPeakAllocs,
                _iPeakBytes);
    _OutputDebugString(TRUE, buff);

    SIZE_T leakedBytes = 0;
    int leakedAllocs = 0;
    SPYBLK* psbWalk = _psbHead;

    // reach the end and move forward
    if (psbWalk != NULL)
        while (psbWalk->psbNext != NULL)
            psbWalk = psbWalk->psbNext;

    while (psbWalk != NULL)
    {
        // FIXME_X64 psbWalk->cbRequest is of type size_t, yet we print it as %d which is likely wrong
        // check the rest of the code where the bug might appear; it probably should be (%Id) - http://msdn.microsoft.com/en-us/library/tcxf1dw6.aspx
        _snprintf_s(buff, _countof(buff), _TRUNCATE,
                    "[%u] Leaked %Iu bytes at 0x%p, from thread 0x%X",
                    psbWalk->cOrder, psbWalk->cbRequest,
                    (BYTE*)psbWalk + sizeof(SPYBLK), psbWalk->dwThreadId);
        _OutputDebugString(TRUE, buff);
        if (psbWalk->cRealloc)
        {
            _snprintf_s(buff, _countof(buff), _TRUNCATE,
                        "  Data was re-alloced %u times", psbWalk->cRealloc);
            _OutputDebugString(TRUE, buff);
        }
        if (psbWalk->szStackHead[0] != 0)
        {
            OutputOleSpyWideValue(TRUE, L"  Call Stack Head: '",
                                  psbWalk->szStackHead, L"'");
        }
        if (psbWalk->szStackTail[0] != 0)
        {
            OutputOleSpyWideValue(TRUE, L"  Call Stack Tail: '",
                                  psbWalk->szStackTail, L"'");
        }
        __try
        {
            int iUniFlags = IS_TEXT_UNICODE_ASCII16 | IS_TEXT_UNICODE_STATISTICS;  // each IsTextUnicode call needs its own variable because the function modifies it
            int iUniFlags2 = IS_TEXT_UNICODE_ASCII16 | IS_TEXT_UNICODE_STATISTICS; // --||--
            void* pvRequest = (BYTE*)psbWalk + sizeof(SPYBLK);
            if (psbWalk->cbRequest >= *((USHORT*)pvRequest) &&
                *((USHORT*)pvRequest) != 0 && // two zeros at the start of memory are not considered a PIDL
                IsGoodPIDL((LPCITEMIDLIST)pvRequest, (int)psbWalk->cbRequest))
            {
                // some PIDL
                OutputOleSpyWideValue(TRUE, L"  Data is pidl ",
                                      DumpPidl((LPITEMIDLIST)pvRequest));

                if (FS_IsValidID((LPITEMIDLIST)pvRequest))
                {
                    const wchar_t* path = GetPidlFileSystemPath((LPCITEMIDLIST)pvRequest);
                    if (path != NULL && path[0] != 0)
                    {
                        OutputOleSpyWideValue(TRUE, L"  Pidl for '", path, L"'");
                    }
                    else if (psbWalk->cbRequest > 16 && SIL_GetType((LPITEMIDLIST)pvRequest) == SHID_FS_FILE) // SHID_FS_FILE == 0x32
                    {
                        OutputOleSpyNarrowValue(
                            TRUE, "  May be a relative pidl for '",
                            reinterpret_cast<const char*>((LPBYTE)pvRequest + 14),
                            psbWalk->cbRequest - 14, "'");
                    }
                }
                else if (ROOT_IsValidID((LPITEMIDLIST)pvRequest))
                {
                    const wchar_t* path = GetPidlFileSystemPath((LPCITEMIDLIST)pvRequest);
                    if (path != NULL && path[0] != 0)
                    {
                        OutputOleSpyWideValue(TRUE, L"  Pidl for '", path, L"'");
                    }
                    else
                    {
                        const wchar_t* name = GetPidlDisplayName((LPCITEMIDLIST)pvRequest);
                        if (name != NULL && name[0] != 0)
                            OutputOleSpyWideValue(TRUE, L"  Pidl for '", name, L"'");
                    }
                }
            }
            else if (psbWalk->cbRequest > 8 &&
                     *((DWORD*)pvRequest) != 0 && // the first 4 bytes are 0 -> so this is certainly not a UNICODE string
                     IsTextUnicode((LPWSTR)pvRequest, (int)psbWalk->cbRequest - 2, &iUniFlags))
            {
                OutputOleSpyWideValue(TRUE, L"  Data is UNICODE string '",
                                      (LPWSTR)pvRequest, L"'");
            }
            else if (psbWalk->cbRequest > 8 && *((DWORD*)pvRequest) <= psbWalk->cbRequest - 4 && IsTextUnicode((LPWSTR)((BYTE*)pvRequest + 4), *((DWORD*)pvRequest), &iUniFlags2))
            {
                OutputOleSpyWideValue(TRUE, L"  Data is BSTR string '",
                                      (LPWSTR)((BYTE*)pvRequest + 4), L"'");
            }
            else if (IsAsciiString((LPSTR)pvRequest, (int)psbWalk->cbRequest))
            {
                OutputOleSpyNarrowValue(TRUE, "  Data is ASCII string '",
                                        (LPSTR)pvRequest, psbWalk->cbRequest, "'");
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            _OutputDebugString(TRUE, L"Exception during memory analyzing");
        }

        leakedBytes += psbWalk->cbRequest;
        leakedAllocs++;
        psbWalk = psbWalk->psbPrev;
        if (psbWalk != NULL)
            _OutputDebugString(TRUE, L"");
    }
    if (leakedAllocs != 0)
    {
        _OutputDebugString(TRUE, L"-------------------------------------------------");
        _snprintf_s(buff, _countof(buff), _TRUNCATE,
                    "Memory Leaks Summary: %d allocations, %Iu bytes leaked",
                    leakedAllocs, leakedBytes);
        _OutputDebugString(TRUE, buff);
        _OutputDebugString(TRUE, L"Hint: to break on [n] allocation call OleSpySetBreak(n).");
    }
    else
    {
        _OutputDebugString(TRUE, L"All allocations were deallocated.");
    }
    _OutputDebugString(TRUE, L"~~~~~~~~~~~~~~~~~~ Report End ~~~~~~~~~~~~~~~~~~~");
    LeaveCS();
    return TRUE;
}

CMallocSpy* OleSpy = NULL;

#endif // _DEBUG

BOOL OleSpyRegister()
{
#ifdef _DEBUG
    if (OleSpy != NULL)
    {
        TRACE_E("OleSpyRegister: There is already a registered spy.");
        return FALSE;
    }

    // a correct instance of the object
    CMallocSpy templateSpy;

    // dirty hack: we do not want memory leaks in the CRT so the object is not
    // allocated on the heap, and we also need a correctly constructed VMT, so
    // we copy the contents of the template
    static byte _Spy[sizeof(templateSpy)];
    memcpy(_Spy, &templateSpy, sizeof(templateSpy));

    // the CRT does not know that OleSpy is an object, so it never calls its destructor
    OleSpy = (CMallocSpy*)_Spy;
    HRESULT hr = CoRegisterMallocSpy(OleSpy);
    if (hr == CO_E_OBJISREG)
    {
        TRACE_E("CoRegisterMallocSpy: There is already a registered spy.");
        OleSpy = NULL;
        return FALSE;
    }
#endif // _DEBUG

    return TRUE;
}

void OleSpyRevoke()
{
#ifdef _DEBUG
    if (OleSpy == NULL)
    {
        TRACE_E("OleSpyRevoke: No spy is currently registered.");
        return;
    }

    HRESULT hr = CoRevokeMallocSpy();
    if (hr != S_OK)
    {
        if (hr == CO_E_OBJNOTREG)
            TRACE_E("CoRevokeMallocSpy: No spy is currently registered.");
        else if (hr == E_ACCESSDENIED)
            TRACE_I("CoRevokeMallocSpy: Spy is registered but there are outstanding allocations (not yet freed) made while this spy was active.");
    }
#endif // _DEBUG
}

void OleSpySetBreak(int alloc)
{
#ifdef _DEBUG
    if (OleSpy == NULL)
    {
        TRACE_E("OleSpyDump: No spy is currently registered.");
        return;
    }
    OleSpy->SetBreakOnAlloc(alloc);
#endif // _DEBUG
}

void OleSpyDump()
{
#ifdef _DEBUG
    if (OleSpy == NULL)
    {
        TRACE_E("OleSpyDump: No spy is currently registered.");
        return;
    }
    OleSpy->DumpLeaks();
#endif // _DEBUG
}

/*
unsigned OleSpyStressTest(void *param)
{
  CALL_STACK_MESSAGE1("OleSpyStressTest()");
  SetThreadNameInVCAndTrace(L"OleSpyStressTest");
  TRACE_I("Begin");

  IMalloc *alloc;
  if (SUCCEEDED(CoGetMalloc(1, &alloc)))
  {
  int i;
    for (i = 0; i < 10000; i++)
    {
      int size = 2 + rand();
      BYTE *p = (BYTE*)alloc->Alloc(size);
      
      BYTE pattern = 0;
      int j;
      for (j = 0; j < size; j++)
        p[j] = pattern++;

      size = size / 2;
      p = (BYTE*)alloc->Realloc(p, size);
      pattern = 0;
      for (j = 0; j < size; j++)
        if (p[j] != pattern++)
          TRACE_E("error");

      alloc->Free(p);

      //Sleep(1);
    }
    alloc->Release();
  }
    
  TRACE_I("End");
  return 0;
}

DWORD WINAPI OleSpyStressTestF(void *param)
{
  CALL_STACK_MESSAGE_NONE
#ifndef CALLSTK_DISABLE
  CCallStack stack;
#endif // CALLSTK_DISABLE
  return OleSpyStressTest(param);
}

void OleSpyStressTest()
{
  // start threads that stress IMalloc

  DWORD threadID;
  HANDLE thread1 = HANDLES(CreateThread(NULL, 0, OleSpyStressTestF, 0, 0, &threadID));
  HANDLE thread2 = HANDLES(CreateThread(NULL, 0, OleSpyStressTestF, 0, 0, &threadID));
  HANDLE thread3 = HANDLES(CreateThread(NULL, 0, OleSpyStressTestF, 0, 0, &threadID));
  HANDLE thread4 = HANDLES(CreateThread(NULL, 0, OleSpyStressTestF, 0, 0, &threadID));
  HANDLE thread5 = HANDLES(CreateThread(NULL, 0, OleSpyStressTestF, 0, 0, &threadID));

  if (thread1 != NULL && WaitForSingleObject(thread1, INFINITE) == WAIT_OBJECT_0) HANDLES(CloseHandle(thread1));
  if (thread2 != NULL && WaitForSingleObject(thread2, INFINITE) == WAIT_OBJECT_0) HANDLES(CloseHandle(thread2));
  if (thread3 != NULL && WaitForSingleObject(thread3, INFINITE) == WAIT_OBJECT_0) HANDLES(CloseHandle(thread3));
  if (thread4 != NULL && WaitForSingleObject(thread4, INFINITE) == WAIT_OBJECT_0) HANDLES(CloseHandle(thread4));
  if (thread5 != NULL && WaitForSingleObject(thread5, INFINITE) == WAIT_OBJECT_0) HANDLES(CloseHandle(thread5));
}
*/
