// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

// mutex for access to shared memory
extern HANDLE SalShExtSharedMemMutex;
// shared memory - see CSalShExtSharedMem structure
extern HANDLE SalShExtSharedMem;
// event for sending a request to perform Paste in the source Salamander (used only on Vista+)
extern HANDLE SalShExtDoPasteEvent;
// mapped shared memory - see CSalShExtSharedMem structure
extern CSalShExtSharedMem* SalShExtSharedMemView;

// TRUE if SalShExt/SalExten/SalamExt/SalExtX86/SalExtX64.DLL was registered successfully or already registered
extern BOOL SalShExtRegistered;

// total hack: we need to determine which window will receive Drop, we detect it
// in GetData based on mouse position; this variable holds the last test result
extern HWND LastWndFromGetData;

// total hack: we need to determine which window will receive Paste, we detect it
// in GetData based on the foreground window; this variable holds the last test result
extern HWND LastWndFromPasteGetData;

extern BOOL OurDataOnClipboard; // TRUE = our data object is on the clipboard (copy&paste from archive)

//*****************************************************************************

// call before using the library
void InitSalShLib();

// call to release the library
void ReleaseSalShLib();

// returns TRUE if the data object contains only a "fake" directory; in 'fakeType' (if not NULL) it returns
// 1 if the source is an archive and 2 if the source is an FS; if the source is FS and 'srcFSPathBuf' is not NULL,
// it returns the source FS path ('srcFSPathBufSize' is the size of the 'srcFSPathBuf' buffer)
BOOL IsFakeDataObject(IDataObject* pDataObject, int* fakeType, char* srcFSPathBuf, int srcFSPathBufSize);

//
//*****************************************************************************
// CFakeDragDropDataObject
//
// data object used to detect the target of drag&drop operation (used when
// unpacking from an archive and when copying from a plugin file system),
// wraps a Windows data object obtained for a "fake" directory and adds
// format SALCF_FAKE_REALPATH (defines the path that should appear after drop
// in directory-line, command-line + blocks drop to usermenu-toolbar),
// SALCF_FAKE_SRCTYPE (source type - 1=archive, 2=FS) and in case of FS also
// SALCF_FAKE_SRCFSPATH (source FS path) to GetData()

class CFakeDragDropDataObject : public IDataObject
{
private:
    long RefCount;
    IDataObject* WinDataObject;   // wrapped data object
    char RealPath[2 * MAX_PATH];  // path for drop into directory and command line
    int SrcType;                  // source type (1=archive, 2=FS)
    char SrcFSPath[2 * MAX_PATH]; // only for FS source: source FS path
    UINT CFSalFakeRealPath;       // clipboard format for sal-fake-real-path
    UINT CFSalFakeSrcType;        // clipboard format for sal-fake-src-type
    UINT CFSalFakeSrcFSPath;      // clipboard format for sal-fake-src-fs-path

public:
    CFakeDragDropDataObject(IDataObject* winDataObject, const char* realPath, int srcType,
                            const char* srcFSPath)
    {
        RefCount = 1;
        WinDataObject = winDataObject;
        WinDataObject->AddRef();
        lstrcpyn(RealPath, realPath, 2 * MAX_PATH);
        if (srcFSPath != NULL && srcType == 2 /* FS */)
            lstrcpyn(SrcFSPath, srcFSPath, 2 * MAX_PATH);
        else
            SrcFSPath[0] = 0;
        SrcType = srcType;
        CFSalFakeRealPath = RegisterClipboardFormat(SALCF_FAKE_REALPATH);
        CFSalFakeSrcType = RegisterClipboardFormat(SALCF_FAKE_SRCTYPE);
        CFSalFakeSrcFSPath = RegisterClipboardFormat(SALCF_FAKE_SRCFSPATH);
    }

    virtual ~CFakeDragDropDataObject()
    {
        if (RefCount != 0)
            TRACE_E("Preliminary destruction of this object.");
        WinDataObject->Release();
    }

    STDMETHOD(QueryInterface)
    (REFIID, void FAR * FAR*);
    STDMETHOD_(ULONG, AddRef)
    (void) { return ++RefCount; }
    STDMETHOD_(ULONG, Release)
    (void)
    {
        if (--RefCount == 0)
        {
            delete this;
            return 0; // must not touch the object, it no longer exists
        }
        return RefCount;
    }

    STDMETHOD(GetData)
    (FORMATETC* formatEtc, STGMEDIUM* medium);

    STDMETHOD(GetDataHere)
    (FORMATETC* pFormatetc, STGMEDIUM* pmedium)
    {
        return WinDataObject->GetDataHere(pFormatetc, pmedium);
    }

    STDMETHOD(QueryGetData)
    (FORMATETC* formatEtc)
    {
        if (formatEtc->cfFormat == CF_HDROP)
            return DV_E_FORMATETC; // this ensures "NO" drop in simpler apps (BOSS, WinCmd, SpeedCommander, MSIE, Word, etc.)
        return WinDataObject->QueryGetData(formatEtc);
    }

    STDMETHOD(GetCanonicalFormatEtc)
    (FORMATETC* pFormatetcIn, FORMATETC* pFormatetcOut)
    {
        return WinDataObject->GetCanonicalFormatEtc(pFormatetcIn, pFormatetcOut);
    }

    STDMETHOD(SetData)
    (FORMATETC* pFormatetc, STGMEDIUM* pmedium, BOOL fRelease)
    {
        return WinDataObject->SetData(pFormatetc, pmedium, fRelease);
    }

    STDMETHOD(EnumFormatEtc)
    (DWORD dwDirection, IEnumFORMATETC** ppenumFormatetc)
    {
        return WinDataObject->EnumFormatEtc(dwDirection, ppenumFormatetc);
    }

    STDMETHOD(DAdvise)
    (FORMATETC* pFormatetc, DWORD advf, IAdviseSink* pAdvSink,
     DWORD* pdwConnection)
    {
        return WinDataObject->DAdvise(pFormatetc, advf, pAdvSink, pdwConnection);
    }

    STDMETHOD(DUnadvise)
    (DWORD dwConnection)
    {
        return WinDataObject->DUnadvise(dwConnection);
    }

    STDMETHOD(EnumDAdvise)
    (IEnumSTATDATA** ppenumAdvise)
    {
        return WinDataObject->EnumDAdvise(ppenumAdvise);
    }
};

//
//*****************************************************************************
// CSalShExtPastedData
//
// data for Paste from clipboard stored inside the "source" Salamander

class CSalamanderDirectory;

class CSalShExtPastedData
{
protected:
    DWORD DataID; // version of data stored for Paste from clipboard

    BOOL Lock; // TRUE = locked against deletion, FALSE = not locked

    char ArchiveFileName[MAX_PATH]; // full path to the archive
    char PathInArchive[MAX_PATH];   // path inside the archive where Copy to clipboard occurred
    CNames SelFilesAndDirs;         // names of files and directories from PathInArchive that will be unpacked

    CSalamanderDirectory* StoredArchiveDir;             // stored archive structure (used if the archive is not open in the panel)
    CPluginDataInterfaceEncapsulation StoredPluginData; // stored archive plugin-data interface (used if the archive is not open in the panel)
    FILETIME StoredArchiveDate;                         // archive file date (for validity tests of the archive listing)
    CQuadWord StoredArchiveSize;                        // archive file size (for validity tests of the archive listing)

public:
    CSalShExtPastedData();
    ~CSalShExtPastedData();

    DWORD GetDataID() { return DataID; }
    void SetDataID(DWORD dataID) { DataID = dataID; }

    BOOL IsLocked() { return Lock; }
    void SetLock(BOOL lock) { Lock = lock; }

    // sets object data, returns TRUE on success; on failure leaves the object empty
    // and returns FALSE
    BOOL SetData(const char* archiveFileName, const char* pathInArchive, CFilesArray* files,
                 CFilesArray* dirs, BOOL namesAreCaseSensitive, int* selIndexes,
                 int selIndexesCount);

    // clears data stored in StoredArchiveDir and StoredPluginData
    void ReleaseStoredArchiveData();

    // clears the object (removes all its data, object remains ready for further use)
    void Clear();

    // performs paste operation with current data; 'copy' is TRUE when data should be copied,
    // FALSE when data should be moved; 'tgtPath' is the target disk path of the operation
    void DoPasteOperation(BOOL copy, const char* tgtPath);

    // if the object can use the provided data, it keeps them and returns TRUE; otherwise returns
    // FALSE (the provided data will then be released)
    BOOL WantData(const char* archiveFileName, CSalamanderDirectory* archiveDir,
                  CPluginDataInterfaceEncapsulation pluginData,
                  FILETIME archiveDate, CQuadWord archiveSize);

    // returns TRUE if it is possible to unload plugin 'plugin'; if the object contains
    // data of plugin 'plugin', it tries to discard them so it can return TRUE
    BOOL CanUnloadPlugin(HWND parent, CPluginInterfaceAbstract* plugin);
};

// data for Paste from clipboard stored inside the "source" Salamander
extern CSalShExtPastedData SalShExtPastedData;

//
//*****************************************************************************
// CFakeCopyPasteDataObject
//
// data object used to detect the target of copy&paste operation (used when
// unpacking from an archive), wraps a Windows data object obtained for a "fake"
// directory and ensures deletion of the "fake" directory from disk after the object
// is released from the clipboard

class CFakeCopyPasteDataObject : public IDataObject
{
private:
    long RefCount;
    IDataObject* WinDataObject; // wrapped data object
    char FakeDir[MAX_PATH];     // "fake" dir
    UINT CFSalFakeRealPath;     // clipboard format for sal-fake-real-path
    UINT CFIdList;              // clipboard format for shell id list (Explorer uses instead of simpler CF_HDROP)

    DWORD LastGetDataCallTime; // time of last GetData() call
    BOOL CutOrCopyDone;        // FALSE = object is still being put on the clipboard, Release does nothing until CutOrCopyDone is TRUE

public:
    CFakeCopyPasteDataObject(IDataObject* winDataObject, const char* fakeDir)
    {
        RefCount = 1;
        WinDataObject = winDataObject;
        WinDataObject->AddRef();
        lstrcpyn(FakeDir, fakeDir, MAX_PATH);
        CFSalFakeRealPath = RegisterClipboardFormat(SALCF_FAKE_REALPATH);
        CFIdList = RegisterClipboardFormat(CFSTR_SHELLIDLIST);
        LastGetDataCallTime = GetTickCount() - 60000; // initialize to 1 minute before object creation
        CutOrCopyDone = FALSE;
    }

    virtual ~CFakeCopyPasteDataObject()
    {
        if (RefCount != 0)
            TRACE_E("Preliminary destruction of this object.");
        WinDataObject->Release();
    }

    void SetCutOrCopyDone() { CutOrCopyDone = TRUE; }

    STDMETHOD(QueryInterface)
    (REFIID, void FAR * FAR*);
    STDMETHOD_(ULONG, AddRef)
    (void)
    {
        //      TRACE_I("AddRef");
        return ++RefCount;
    }
    STDMETHOD_(ULONG, Release)
    (void);

    STDMETHOD(GetData)
    (FORMATETC* formatEtc, STGMEDIUM* medium);

    STDMETHOD(GetDataHere)
    (FORMATETC* pFormatetc, STGMEDIUM* pmedium)
    {
        //      TRACE_I("GetDataHere");
        return WinDataObject->GetDataHere(pFormatetc, pmedium);
    }

    STDMETHOD(QueryGetData)
    (FORMATETC* formatEtc)
    {
        //      TRACE_I("QueryGetData");
        if (formatEtc->cfFormat == CF_HDROP)
            return DV_E_FORMATETC; // this ensures "NO" drop in simpler apps (BOSS, WinCmd, SpeedCommander, MSIE, Word, etc.)
        return WinDataObject->QueryGetData(formatEtc);
    }

    STDMETHOD(GetCanonicalFormatEtc)
    (FORMATETC* pFormatetcIn, FORMATETC* pFormatetcOut)
    {
        //      TRACE_I("GetCanonicalFormatEtc");
        return WinDataObject->GetCanonicalFormatEtc(pFormatetcIn, pFormatetcOut);
    }

    STDMETHOD(SetData)
    (FORMATETC* pFormatetc, STGMEDIUM* pmedium, BOOL fRelease)
    {
        //      TRACE_I("SetData");
        return WinDataObject->SetData(pFormatetc, pmedium, fRelease);
    }

    STDMETHOD(EnumFormatEtc)
    (DWORD dwDirection, IEnumFORMATETC** ppenumFormatetc)
    {
        //      TRACE_I("EnumFormatEtc");
        return WinDataObject->EnumFormatEtc(dwDirection, ppenumFormatetc);
    }

    STDMETHOD(DAdvise)
    (FORMATETC* pFormatetc, DWORD advf, IAdviseSink* pAdvSink,
     DWORD* pdwConnection)
    {
        //      TRACE_I("DAdvise");
        return WinDataObject->DAdvise(pFormatetc, advf, pAdvSink, pdwConnection);
    }

    STDMETHOD(DUnadvise)
    (DWORD dwConnection)
    {
        //      TRACE_I("DUnadvise");
        return WinDataObject->DUnadvise(dwConnection);
    }

    STDMETHOD(EnumDAdvise)
    (IEnumSTATDATA** ppenumAdvise)
    {
        //      TRACE_I("EnumDAdvise");
        return WinDataObject->EnumDAdvise(ppenumAdvise);
    }
};
