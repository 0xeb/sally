// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"

// The owned STRRET normalization helper comes from shlwapi, already linked project-wide.
#include <shlwapi.h>
#include <limits>
#include <new>
#include <stdexcept>
// shlwapi.h's PathIsPrefix macro (-> PathIsPrefixA, since this project never
// defines UNICODE) silently renames plugins.h's CSalamanderGeneral::PathIsPrefix override to
// PathIsPrefixA if left active across that include - it then fails to satisfy
// CSalamanderGeneralAbstract::PathIsPrefix's pure virtual, and CSalamanderGeneral becomes
// non-instantiable. Same guard already used in files_window_clipboard_paths.cpp,
// main_window_commands_help.cpp, and main_window_config_persistence.cpp for the same reason.
#undef PathIsPrefix // otherwise, collision with CSalamanderGeneral::PathIsPrefix

#include "shellib.h"
#include "drop_effect_policy.h"
#include "dragdrop_diag.h"
#include "cfgdlg.h"
#include "plugins.h"
extern "C"
{
#include "shexreg.h"
}
#include "salshlib.h"
#include "common/widepath.h"
#include "common/IFileSystem.h"
#include "common/IShell.h"
#include "common/unicode/helpers.h"
#include "common/fsutil.h"
#include "common/SalPathWide.h"
#include "common/FixedUtf16Buffer.h"
#include "common/clipboard/HDropSelection.h"
#include "common/clipboard/ClipboardTextPayload.h"
#include "common/Win32TextCodec.h"

// original location in fileswnd.h (here only because of MakeCopyOfName in CImpDropTarget::ProcessClipboardData)
extern BOOL OurClipDataObject; // TRUE during "paste" of our IDataObject
                               // (detection of own copy/move routine with foreign data)

void* LastSafeDataObject = NULL;

DWORD ExecuteAssociationTlsIndex = TLS_OUT_OF_INDEXES; // allows only one call at a time (prevents recursion) in each thread

BOOL DragFromPluginFSEffectIsFromPlugin = FALSE;

static DWORD GetOwnFolderDropEffect(DWORD allowedEffects, DWORD keyState)
{
    // #101: the modifier->effect mapping (incl. Alt / Ctrl+Shift -> shortcut) lives in the
    // testable drop_effect_policy unit.
    //
    // This is the OWN-FOLDER branch, which is entered only when CurDirDropTarget is NULL - i.e.
    // the shell drop target could not be created. Nothing here can make a .lnk, so no shell
    // target is available to perform a link and the policy must not offer one.
    return ComputeOwnFolderDropEffect(allowedEffects, keyState, /*shellTargetAvailable*/ false);
}

static BOOL CanUseOwnFolderDrop(IDataObject* dataObject, BOOL isFakeDataObject, BOOL tgtFile,
                                CUseOwnRutine useOwnRutine)
{
    return dataObject != NULL && !isFakeDataObject && !tgtFile &&
           (useOwnRutine == NULL || useOwnRutine(dataObject));
}

//*****************************************************************************
//
// CCopyMoveRecord
//

CCopyMoveRecord::CCopyMoveRecord(const wchar_t* fileName, const wchar_t* mapName) noexcept
    : Valid(false)
{
    if (fileName == NULL)
        return;
    try
    {
        std::wstring stagedFileName(fileName);
        std::optional<std::wstring> stagedMapName;
        if (mapName != NULL)
            stagedMapName.emplace(mapName);
        FileName.swap(stagedFileName);
        MapName.swap(stagedMapName);
        Valid = true;
    }
    catch (const std::bad_alloc&)
    {
        // This constructor is used below COM drop callbacks. Keep failure represented by
        // IsValid() and do not invoke diagnostic machinery from the noexcept boundary.
    }
    catch (const std::length_error&)
    {
        // See the bad_alloc arm above.
    }
}

//*****************************************************************************
//
// DestroyCopyMoveData
//

void DestroyCopyMoveData(CCopyMoveData* data)
{
    // TIndirectArray destructor calls delete on each CCopyMoveRecord.
    delete data;
}

//*****************************************************************************
//
// CImpDropTarget
//

void CImpDropTarget::SetDirectory(const wchar_t* path, DWORD grfKeyState, POINTL pt,
                                  DWORD* effect, IDataObject* dataObject, BOOL tgtIsFile,
                                  int tgtType)
{
    CALL_STACK_MESSAGE5("CImpDropTarget::SetDirectory(%ls, 0x%X, , , , %d, %d)", path,
                        grfKeyState, tgtIsFile, tgtType);

    if (path == NULL)
    {
        if (CurDirDropTarget != NULL)
        {
            CurDirDropTarget->DragLeave();
            CurDirDropTarget->Release();
        }
        CurDirDropTarget = NULL;
        CurDir.clear();
        TgtType = idtttWindows;
        return;
    }

    TgtType = tgtType;
    if (tgtType == idtttWindows)
    {
        BOOL pathChanged = CurDir != path;
        if (pathChanged || CurDirDropTarget == NULL)
        {
            if (CurDirDropTarget != NULL)
            {
                CurDirDropTarget->DragLeave();
                CurDirDropTarget->Release();
            }
            if (tgtIsFile && dataObject != NULL && IsFakeDataObject(dataObject, NULL, NULL))
                CurDirDropTarget = NULL;
            else if (*path != 0)
                CurDirDropTarget = CreateIDropTargetW(OwnerWindow, path);
            if (CurDirDropTarget != NULL && dataObject != NULL && effect != NULL)
            {
                if (CurDirDropTarget->DragEnter(dataObject, grfKeyState, pt, effect) != S_OK)
                { // drop-target error -> release it
                    CurDirDropTarget->Release();
                    CurDirDropTarget = NULL;
                    CurDir.clear();
                    return;
                }
            }
            CurDir = path;
        }
    }
    else // archives + FS
    {
        if (CurDirDropTarget != NULL)
        {
            CurDirDropTarget->DragLeave();
            CurDirDropTarget->Release();
        }
        CurDirDropTarget = NULL;
        CurDir = path;
    }
}

BOOL CImpDropTarget::ProcessClipboardData(
    BOOL copy, const std::vector<std::wstring>& paths,
    const std::vector<std::wstring>* mappedNames)
{
    CALL_STACK_MESSAGE2("CImpDropTarget::ProcessClipboardData(%d, ,)", copy);
    if (paths.empty() || paths.size() > static_cast<size_t>(INT_MAX))
        return FALSE;

    BOOL ret = FALSE;
    CCopyMoveData* array = NULL;
    try
    {
        array = new CCopyMoveData(100, 50);
    }
    catch (const std::bad_alloc&)
    {
        return FALSE;
    }
    if (array != NULL)
    {
        // array->MakeCopyOfName will be TRUE if it's our own copy & paste from clipboard
        // (copying with the condition that if target already exists, "Copy of ..." will be created)
        array->MakeCopyOfName = copy && mappedNames == NULL; // only our data-object gets here

        for (size_t i = 0; i < paths.size(); ++i)
        {
            const wchar_t* mapName = NULL;
            if (mappedNames != NULL)
                mapName = i < mappedNames->size() ? (*mappedNames)[i].c_str() : L"";

            CCopyMoveRecord* record = NULL;
            try
            {
                record = new CCopyMoveRecord(paths[i].c_str(), mapName);
            }
            catch (const std::bad_alloc&)
            {
                break;
            }
            if (record == NULL || !record->IsValid())
            {
                delete record;
                break;
            }
            array->Add(record);
            if (!array->IsGood())
            {
                array->ResetState();
                break;
            }
        }

        if (array->IsGood() && array->Count == static_cast<int>(paths.size()) && array->Count > 0)
        {
            ret = DoCopyMove(copy, CurDir.c_str(), array, DoCopyMoveParam);
            array = NULL; // released by DoCopyMove
        }
        if (array != NULL)
            DestroyCopyMoveData(array);
    }
    return ret;
}

BOOL CImpDropTarget::TryCopyOrMove(BOOL copy, IDataObject* pDataObject, UINT CF_FileMapA,
                                   UINT CF_FileMapW, BOOL cfFileMapA, BOOL cfFileMapW)
{
    CALL_STACK_MESSAGE2("CImpDropTarget::TryCopyOrMove(%d, , , , ,)", copy);

    FORMATETC formatEtc;
    formatEtc.cfFormat = CF_HDROP;
    formatEtc.ptd = NULL;
    formatEtc.dwAspect = DVASPECT_CONTENT;
    formatEtc.lindex = -1;
    formatEtc.tymed = TYMED_HGLOBAL;

    STGMEDIUM stgMedium;
    stgMedium.tymed = TYMED_HGLOBAL;
    stgMedium.hGlobal = NULL;
    stgMedium.pUnkForRelease = NULL;

    BOOL ret = FALSE;
    if (pDataObject->GetData(&formatEtc, &stgMedium) == S_OK)
    {
        if (stgMedium.tymed == TYMED_HGLOBAL && stgMedium.hGlobal != NULL)
        {
            DROPFILES* data = (DROPFILES*)HANDLES(GlobalLock(stgMedium.hGlobal));
            if (data != NULL)
            {
                std::vector<std::wstring> paths;
                const SIZE_T dataSize = GlobalSize(stgMedium.hGlobal);
                const bool havePaths = sally::clipboard::TryDecodeHDropPaths(
                    data, dataSize, paths);
                if (cfFileMapA || cfFileMapW)
                {
                    // ONE discriminator for both the request and the decode. cfFileMapA and
                    // cfFileMapW are set independently while enumerating the source object's
                    // formats, so both can be TRUE; asking for CF_FileMapA and then decoding the
                    // answer as UTF-16 (which is what testing cfFileMapW separately did) read every
                    // ANSI name two bytes at a time and ended the list at the first name whose
                    // second byte was zero.
                    const bool wideMap = cfFileMapA == FALSE;
                    formatEtc.cfFormat = (CLIPFORMAT)(wideMap ? CF_FileMapW : CF_FileMapA);
                    formatEtc.ptd = NULL;
                    formatEtc.dwAspect = DVASPECT_CONTENT;
                    formatEtc.lindex = -1;
                    formatEtc.tymed = TYMED_HGLOBAL;

                    STGMEDIUM stgMediumMap;
                    stgMediumMap.tymed = TYMED_HGLOBAL;
                    stgMediumMap.hGlobal = NULL;
                    stgMediumMap.pUnkForRelease = NULL;

                    if (pDataObject->GetData(&formatEtc, &stgMediumMap) == S_OK)
                    {
                        if (stgMediumMap.tymed == TYMED_HGLOBAL && stgMediumMap.hGlobal != NULL)
                        {
                            void* map = HANDLES(GlobalLock(stgMediumMap.hGlobal));

                            if (map != NULL)
                            {
                                std::vector<std::wstring> mappedNames;
                                const SIZE_T mapSize = GlobalSize(stgMediumMap.hGlobal);
                                if (havePaths &&
                                    sally::clipboard::TryDecodeClipboardStringList(
                                        map, mapSize, wideMap, mappedNames))
                                {
                                    ret = ProcessClipboardData(copy, paths, &mappedNames);
                                }
                                HANDLES(GlobalUnlock(stgMediumMap.hGlobal));
                            }
                        }
                        ReleaseStgMedium(&stgMediumMap);
                    }
                }
                else if (havePaths)
                    ret = ProcessClipboardData(copy, paths, NULL);
                HANDLES(GlobalUnlock(stgMedium.hGlobal));
            }
        }
        ReleaseStgMedium(&stgMedium);
    }
    return ret;
}

BOOL IsSimpleSelection(IDataObject* pDataObject, CDragDropOperData* namesList)
{
    CALL_STACK_MESSAGE1("IsSimpleSelection()");
    BOOL ret = FALSE;
    if (pDataObject != NULL && !IsFakeDataObject(pDataObject, NULL, NULL)) // from archive/FS it's not received this way
    {
        IEnumFORMATETC* enumFormat;
        if (pDataObject->EnumFormatEtc(DATADIR_GET, &enumFormat) == S_OK)
        {
            BOOL cfHDrop = FALSE;
            BOOL cfFileMapA = FALSE;
            BOOL cfFileMapW = FALSE;
            UINT CF_FileMapA = RegisterClipboardFormat(CFSTR_FILENAMEMAPA);
            UINT CF_FileMapW = RegisterClipboardFormat(CFSTR_FILENAMEMAPW);

            // Windows XP Remote Desktop problem, see https://forum.altap.cz/viewtopic.php?p=13176#13176
            // If we detect truncated format names, it's most likely Remote Desktop
            // and we must not call pDataObject->GetData(), because it would trigger copying
            // files to our temp on the remote machine and we would be frozen during that time
            // Since Windows Vista the problem is fixed and names are no longer truncated, so this patch
            // affects only XP.
            BOOL cfRemoteDesktop1 = FALSE;
            BOOL cfRemoteDesktop2 = FALSE;
            BOOL cfRemoteDesktop3 = FALSE;
            // narrow literals - explicit RegisterClipboardFormatA.
            UINT CF_RemoteDesktop1 = RegisterClipboardFormatA("Preferred DropEf");
            UINT CF_RemoteDesktop2 = RegisterClipboardFormatA("Shell Object Off");
            UINT CF_RemoteDesktop3 = RegisterClipboardFormatA("Shell IDList Arr");

            FORMATETC formatEtc;
            enumFormat->Reset();
            while (enumFormat->Next(1, &formatEtc, NULL) == S_OK)
            {
                // debug only
                // char formatName[1000];
                // if (GetClipboardFormatName(formatEtc.cfFormat, formatName, 1000) == 0)
                //   formatName[0] = 0;
                // TRACE_I("formatEtc.cfFormat="<<formatEtc.cfFormat<<" tymed="<<formatEtc.tymed<<" name:"<<formatName);

                if (formatEtc.cfFormat == CF_FileMapA)
                    cfFileMapA = TRUE;
                if (formatEtc.cfFormat == CF_FileMapW)
                    cfFileMapW = TRUE;
                if (formatEtc.cfFormat == CF_HDROP)
                    cfHDrop = TRUE;
                if (formatEtc.cfFormat == CF_RemoteDesktop1)
                    cfRemoteDesktop1 = TRUE;
                if (formatEtc.cfFormat == CF_RemoteDesktop2)
                    cfRemoteDesktop2 = TRUE;
                if (formatEtc.cfFormat == CF_RemoteDesktop3)
                    cfRemoteDesktop3 = TRUE;
            }
            enumFormat->Release();

            BOOL remoteDesktop = cfRemoteDesktop1 && cfRemoteDesktop2 && cfRemoteDesktop3; // data originates from remote desktop

            if (cfHDrop && !cfFileMapA && !cfFileMapW && !remoteDesktop) // no mapping (we block Recycle Bin)
            {
                FORMATETC formatEtc2;
                formatEtc2.cfFormat = CF_HDROP;
                formatEtc2.ptd = NULL;
                formatEtc2.dwAspect = DVASPECT_CONTENT;
                formatEtc2.lindex = -1;
                formatEtc2.tymed = TYMED_HGLOBAL;

                STGMEDIUM stgMedium;
                stgMedium.tymed = TYMED_HGLOBAL;
                stgMedium.hGlobal = NULL;
                stgMedium.pUnkForRelease = NULL;

                if (pDataObject->GetData(&formatEtc2, &stgMedium) == S_OK)
                {
                    if (stgMedium.tymed == TYMED_HGLOBAL && stgMedium.hGlobal != NULL)
                    {
                        DROPFILES* data = (DROPFILES*)HANDLES(GlobalLock(stgMedium.hGlobal));
                        if (data != NULL)
                        {
                            // DROPFILES::fWide is an external format discriminator. Decode
                            // either arm once at the Win32 boundary; internal ownership is wide.
                            sally::clipboard::HDropSelection selection;
                            ret = sally::clipboard::TryParseHDropSelection(data, GlobalSize(stgMedium.hGlobal), selection);
                            if (ret && namesList != NULL)
                            {
                                namesList->SrcPath = selection.SourcePath;
                                for (const std::wstring& name : selection.Names)
                                {
                                    wchar_t* add = DupStr(name.c_str());
                                    if (add == NULL)
                                    {
                                        ret = FALSE;
                                        break;
                                    }
                                    namesList->Names.Add(add);
                                    if (!namesList->Names.IsGood())
                                    {
                                        namesList->Names.ResetState();
                                        free(add);
                                        ret = FALSE;
                                        break;
                                    }
                                }
                            }
                            HANDLES(GlobalUnlock(stgMedium.hGlobal));
                        }
                    }
                    ReleaseStgMedium(&stgMedium);
                }
            }
        }
    }
    return ret;
}

STDMETHODIMP CImpDropTarget::QueryInterface(REFIID refiid, void FAR * FAR * ppv)
{
    if (refiid == IID_IUnknown || refiid == IID_IDropTarget)
    {
        *ppv = this;
        AddRef();
        return NOERROR;
    }
    else
    {
        *ppv = NULL;
        return E_NOINTERFACE;
    }
}

STDMETHODIMP CImpDropTarget::DragEnter(IDataObject* pDataObject,
                                       DWORD grfKeyState,
                                       POINTL pt, DWORD* pdwEffect)
{
    CALL_STACK_MESSAGE2("CImpDropTarget::DragEnter(, 0x%X, ,)", grfKeyState);

    DWORD origEffect = *pdwEffect;
    DWORD origKeyState = grfKeyState;
    if (EnterLeaveDrop != NULL)
        EnterLeaveDrop(TRUE, EnterLeaveDropParam);
    RButton = (grfKeyState & MK_RBUTTON) && !(grfKeyState & MK_LBUTTON);

    if (OldDataObject != NULL)
        OldDataObject->Release();
    OldDataObject = pDataObject;
    OldDataObjectIsFake = IsFakeDataObject(OldDataObject, &OldDataObjectSrcType,
                                           &OldDataObjectSrcFSPath);

    OldDataObjectIsSimple = -1; // unknown value
    OldDataObject->AddRef();

    if (ImageDragging)
        ImageDragEnter(pt.x, pt.y);
    BOOL ownFolderDrop = FALSE;
    if (GetCurDir != NULL)
    {
        BOOL tgtFile;
        int tgtType;
        const wchar_t* tgtPathWide = GetCurDir(pt, GetCurDirParam, pdwEffect, RButton, tgtFile,
                                               grfKeyState, tgtType, OldDataObjectSrcType);
        SetDirectory(tgtPathWide, 0, pt, NULL, OldDataObject, tgtFile, tgtType);
        if (TgtType != idtttWindows && TgtType != idtttFullPluginFSPath)
        { // if selection is not from one path (risk likely only with Find), we can't copy/move to archive or FS
            OldDataObjectIsSimple = IsSimpleSelection(OldDataObject, NULL);
            if (!OldDataObjectIsSimple)
                SetDirectory(NULL, 0, pt, NULL, OldDataObject, FALSE, idtttWindows);
        }
        ownFolderDrop = TgtType == idtttWindows && CurDirDropTarget == NULL && !CurDir.empty() && tgtPathWide != NULL &&
                        CanUseOwnFolderDrop(OldDataObject, OldDataObjectIsFake, tgtFile, UseOwnRutine);
    }

    if (DragDropDiagEnabled())
        DragDropDiagDataObject(OldDataObject, CurDir.c_str());

    if (CurDirDropTarget != NULL) // only idtttWindows
    {
        HRESULT res = CurDirDropTarget->DragEnter(pDataObject, grfKeyState, pt, pdwEffect);
        if (res != S_OK) // drop-target error - we report it as "none" drop-effect, because
        {                // other drop-targets in the panel may still work
            LastEffect = -1;
            *pdwEffect = DROPEFFECT_NONE;
            CurDirDropTarget->Release(); // release drop-target so drag-over won't be called on it
            CurDirDropTarget = NULL;
        }
        else
        {
            if (OldDataObjectIsFake)
            { // our data-object (may not be from this process): default is Copy (fake is in TEMP, on the same disk it did Move by default, so we work around it this way)
                if ((origKeyState & MK_SHIFT) != 0 && (origKeyState & MK_CONTROL) == 0 &&
                    (origEffect & DROPEFFECT_MOVE) != 0)
                {
                    *pdwEffect = DROPEFFECT_MOVE;
                }
                else
                {
                    if ((origEffect & DROPEFFECT_COPY) != 0)
                        *pdwEffect = DROPEFFECT_COPY;
                    else
                    {
                        if ((origEffect & DROPEFFECT_MOVE) != 0)
                            *pdwEffect = DROPEFFECT_MOVE;
                        else // drop-target error
                        {
                            *pdwEffect = DROPEFFECT_NONE;
                            pdwEffect = NULL;
                            CurDirDropTarget->DragLeave();
                            CurDirDropTarget->Release(); // release drop-target so drag-over won't be called on it
                            CurDirDropTarget = NULL;
                        }
                    }
                }
            }
            LastEffect = (pdwEffect != NULL) ? *pdwEffect : -1;
        }
    }
    else if (ownFolderDrop)
    {
        *pdwEffect = GetOwnFolderDropEffect(*pdwEffect, origKeyState);
        LastEffect = *pdwEffect != DROPEFFECT_NONE ? *pdwEffect : -1;
    }
    else
    {
        if (TgtType == idtttArchive || TgtType == idtttPluginFS ||
            TgtType == idtttArchiveOnWinPath || TgtType == idtttFullPluginFSPath)
        {
            DWORD allowedEffects = *pdwEffect;
            if ((origKeyState & MK_SHIFT) != 0 && (origKeyState & MK_CONTROL) == 0 &&
                (*pdwEffect & DROPEFFECT_MOVE) != 0) // user wants Move
            {
                *pdwEffect = DROPEFFECT_MOVE;
            }
            else
            {
                if ((origKeyState & MK_SHIFT) == 0 && (origKeyState & MK_CONTROL) != 0 &&
                    (*pdwEffect & DROPEFFECT_COPY) != 0) // user wants Copy
                {
                    *pdwEffect = DROPEFFECT_COPY;
                }
            }
            // determine default drop effect
            if (TgtType == idtttFullPluginFSPath && OldDataObjectSrcType == 2 /* FS */ &&
                !OldDataObjectSrcFSPath.empty() && GetFSToFSDropEffect != NULL)
            { // FS to FS: get preferred effect from plugin
                GetFSToFSDropEffect(OldDataObjectSrcFSPath.c_str(), CurDir.c_str(), allowedEffects, origKeyState,
                                    pdwEffect, GetFSToFSDropEffectParam);
                DragFromPluginFSEffectIsFromPlugin = TRUE;
            }
            else // from disk to archive + from disk to FS: Copy has priority
            {
                if ((*pdwEffect & DROPEFFECT_COPY) != 0)
                    *pdwEffect = DROPEFFECT_COPY;
                else
                {
                    if ((*pdwEffect & DROPEFFECT_MOVE) != 0)
                        *pdwEffect = DROPEFFECT_MOVE;
                    else
                        *pdwEffect = DROPEFFECT_NONE; // drop-target error
                }
            }
            if (*pdwEffect == DROPEFFECT_NONE)
                pdwEffect = NULL; // drop-target error
            LastEffect = (pdwEffect != NULL) ? *pdwEffect : -1;
        }
        else
        {
            const DWORD diagAllowed = *pdwEffect;
            *pdwEffect = DROPEFFECT_NONE;
            LastEffect = -1;
            if (DragDropDiagEnabled())
                DragDropDiagRecord("fallbackNONE", origKeyState, diagAllowed, *pdwEffect,
                                   TgtType, CurDirDropTarget != NULL, false, false);
        }
    }
    return S_OK;
}

STDMETHODIMP CImpDropTarget::DragOver(DWORD grfKeyState, POINTL pt,
                                      DWORD* pdwEffect)
{
    CALL_STACK_MESSAGE2("CImpDropTarget::DragOver(0x%X, ,)", grfKeyState);

    DWORD origEffect = *pdwEffect;
    DWORD origKeyState = grfKeyState;
    RButton = (grfKeyState & MK_RBUTTON) && !(grfKeyState & MK_LBUTTON);

    if (ImageDragging)
        ImageDragMove(pt.x, pt.y);

    BOOL ownFolderDrop = FALSE;
    BOOL diagTgtFile = FALSE;
    if (GetCurDir != NULL)
    {
        BOOL tgtFile;
        int tgtType;
        const wchar_t* tgtPathWide = GetCurDir(pt, GetCurDirParam, pdwEffect, RButton, tgtFile,
                                               grfKeyState, tgtType, OldDataObjectSrcType);
        // [merge:main->unicode] main's #101 drag-drop diagnostic, re-inserted onto the wide
        // GetCurDir/SetDirectory call shape this branch uses.
        diagTgtFile = tgtFile;
        SetDirectory(tgtPathWide, grfKeyState, pt, pdwEffect, OldDataObject, tgtFile, tgtType);
        if (TgtType != idtttWindows && TgtType != idtttFullPluginFSPath)
        { // if selection is not from one path (risk likely only with Find), we can't copy/move to archive or FS
            if (OldDataObjectIsSimple == -1)
                OldDataObjectIsSimple = IsSimpleSelection(OldDataObject, NULL);
            if (!OldDataObjectIsSimple)
                SetDirectory(NULL, grfKeyState, pt, pdwEffect, OldDataObject, FALSE, idtttWindows);
        }
        ownFolderDrop = TgtType == idtttWindows && CurDirDropTarget == NULL && !CurDir.empty() && tgtPathWide != NULL &&
                        CanUseOwnFolderDrop(OldDataObject, OldDataObjectIsFake, tgtFile, UseOwnRutine);
    }
    if (CurDirDropTarget != NULL) // only idtttWindows
    {
        HRESULT res = CurDirDropTarget->DragOver(grfKeyState, pt, pdwEffect);
        if (res == S_OK && OldDataObjectIsFake)
        { // our data-object (may not be from this process): default is Copy (fake is in TEMP, on the same disk it did Move by default, so we work around it this way)
            if ((origKeyState & MK_SHIFT) != 0 && (origKeyState & MK_CONTROL) == 0 &&
                (origEffect & DROPEFFECT_MOVE) != 0)
            {
                *pdwEffect = DROPEFFECT_MOVE;
            }
            else
            {
                if ((origEffect & DROPEFFECT_COPY) != 0)
                    *pdwEffect = DROPEFFECT_COPY;
                else
                {
                    if ((origEffect & DROPEFFECT_MOVE) != 0)
                        *pdwEffect = DROPEFFECT_MOVE;
                    else // drop-target error
                    {
                        *pdwEffect = DROPEFFECT_NONE;
                        pdwEffect = NULL;
                        CurDirDropTarget->DragLeave();
                        CurDirDropTarget->Release(); // release drop-target so drag-over won't be called on it
                        CurDirDropTarget = NULL;
                    }
                }
            }
        }
        LastEffect = (pdwEffect != NULL) ? *pdwEffect : -1;
        if (DragDropDiagEnabled())
            DragDropDiagRecord("shellTarget", origKeyState, origEffect,
                               pdwEffect != NULL ? *pdwEffect : 0, TgtType, true,
                               ownFolderDrop != FALSE, diagTgtFile != FALSE);
        return res;
    }
    else if (ownFolderDrop)
    {
        const DWORD diagAllowed = *pdwEffect;
        *pdwEffect = GetOwnFolderDropEffect(*pdwEffect, origKeyState);
        LastEffect = *pdwEffect != DROPEFFECT_NONE ? *pdwEffect : -1;
        if (DragDropDiagEnabled())
            DragDropDiagRecord("ownFolder", origKeyState, diagAllowed, *pdwEffect, TgtType,
                               false, true, diagTgtFile != FALSE);
        return S_OK;
    }
    else
    {
        if (TgtType == idtttArchive || TgtType == idtttPluginFS ||
            TgtType == idtttArchiveOnWinPath || TgtType == idtttFullPluginFSPath)
        {
            DWORD allowedEffects = *pdwEffect;
            if ((origKeyState & MK_SHIFT) != 0 && (origKeyState & MK_CONTROL) == 0 &&
                (*pdwEffect & DROPEFFECT_MOVE) != 0) // user wants Move
            {
                *pdwEffect = DROPEFFECT_MOVE;
            }
            else
            {
                if ((origKeyState & MK_SHIFT) == 0 && (origKeyState & MK_CONTROL) != 0 &&
                    (*pdwEffect & DROPEFFECT_COPY) != 0) // user wants Copy
                {
                    *pdwEffect = DROPEFFECT_COPY;
                }
            }
            // determine default drop effect
            if (TgtType == idtttFullPluginFSPath && OldDataObjectSrcType == 2 /* FS */ &&
                !OldDataObjectSrcFSPath.empty() && GetFSToFSDropEffect != NULL)
            { // FS to FS: get preferred effect from plugin
                GetFSToFSDropEffect(OldDataObjectSrcFSPath.c_str(), CurDir.c_str(), allowedEffects,
                                    origKeyState, pdwEffect, GetFSToFSDropEffectParam);
                DragFromPluginFSEffectIsFromPlugin = TRUE;
            }
            else // from disk to archive + from disk to FS: Copy has priority
            {
                if ((*pdwEffect & DROPEFFECT_COPY) != 0)
                    *pdwEffect = DROPEFFECT_COPY;
                else
                {
                    if ((*pdwEffect & DROPEFFECT_MOVE) != 0)
                        *pdwEffect = DROPEFFECT_MOVE;
                    else
                        *pdwEffect = DROPEFFECT_NONE; // drop-target error
                }
            }
            if (*pdwEffect == DROPEFFECT_NONE)
                pdwEffect = NULL; // drop-target error
            LastEffect = (pdwEffect != NULL) ? *pdwEffect : -1;
        }
        else
        {
            const DWORD diagAllowed = *pdwEffect;
            *pdwEffect = DROPEFFECT_NONE;
            LastEffect = -1;
            if (DragDropDiagEnabled())
                DragDropDiagRecord("fallbackNONE", origKeyState, diagAllowed, *pdwEffect,
                                   TgtType, CurDirDropTarget != NULL, false, false);
        }
        return S_OK;
    }
}

STDMETHODIMP CImpDropTarget::DragLeave()
{
    CALL_STACK_MESSAGE1("CImpDropTarget::DragLeave()");

    if (ImageDragging)
        ImageDragLeave();
    if (EnterLeaveDrop != NULL)
        EnterLeaveDrop(FALSE, EnterLeaveDropParam);

    RButton = FALSE;
    if (OldDataObject != NULL)
    {
        OldDataObject->Release();
        OldDataObject = NULL;
        OldDataObjectIsFake = FALSE;
        OldDataObjectIsSimple = -1; // unknown value
        OldDataObjectSrcType = 0;
        OldDataObjectSrcFSPath.clear();
    }

    HRESULT ret = S_OK;
    if (CurDirDropTarget != NULL)
    {
        ret = CurDirDropTarget->DragLeave();
        CurDirDropTarget->Release();
        CurDirDropTarget = NULL;
    }
    if (DropEnd != NULL)
        DropEnd(FALSE, FALSE, DropEndParam, FALSE, FALSE, TgtType);
    TgtType = idtttWindows;
    LastEffect = -1;
    return ret;
}

STDMETHODIMP CImpDropTarget::Drop(IDataObject* pDataObject, DWORD grfKeyState,
                                  POINTL pt, DWORD* pdwEffect)
{
    CALL_STACK_MESSAGE2("CImpDropTarget::Drop(, 0x%X, ,)", grfKeyState);

    DWORD lastEffect = LastEffect;
    LastEffect = -1; // simplified invalidation (doesn't have to be before every return)

    if (pdwEffect == NULL)
    {
        DragLeave();
        return E_INVALIDARG;
    }

    DWORD origEffect = *pdwEffect;
    DWORD origKeyState = grfKeyState;

    if (ImageDragging)
        ImageDragLeave();
    if (EnterLeaveDrop != NULL)
        EnterLeaveDrop(FALSE, EnterLeaveDropParam);

    DWORD defEffect = -1;
    if (RButton || ConfirmDropEnable != NULL && *ConfirmDropEnable)
    {
        if (GetCurDir != NULL) // we need to let pdwEffect be limited when dragging within panel
        {
            BOOL tgtFile;
            int tgtType;
            const wchar_t* tgtPathWide = GetCurDir(pt, GetCurDirParam, pdwEffect, RButton, tgtFile,
                                                   grfKeyState, tgtType, OldDataObjectSrcType);
            SetDirectory(tgtPathWide, grfKeyState, pt, pdwEffect, OldDataObject, tgtFile, tgtType);
            if (TgtType != idtttWindows && TgtType != idtttFullPluginFSPath)
            { // if selection is not from one path (risk likely only with Find), we can't copy/move to archive or FS
                if (OldDataObjectIsSimple == -1)
                    OldDataObjectIsSimple = IsSimpleSelection(OldDataObject, NULL);
                if (!OldDataObjectIsSimple)
                {
                    SetDirectory(NULL, grfKeyState, pt, pdwEffect, OldDataObject, FALSE, idtttWindows);
                    *pdwEffect = DROPEFFECT_NONE;
                    return DragLeave();
                }
            }
        }
        defEffect = *pdwEffect;

        if (TgtType == idtttWindows)
        {
            if (OldDataObjectIsFake)
            {
                if ((origKeyState & MK_SHIFT) != 0 && (origKeyState & MK_CONTROL) == 0 &&
                    (origEffect & DROPEFFECT_MOVE) != 0)
                {
                    defEffect = DROPEFFECT_MOVE;
                }
                else
                {
                    if ((origEffect & DROPEFFECT_COPY) != 0)
                        defEffect = DROPEFFECT_COPY;
                    else
                    {
                        if ((origEffect & DROPEFFECT_MOVE) != 0)
                            defEffect = DROPEFFECT_MOVE;
                        else
                            defEffect = 0; // drop-target error
                    }
                }
            }
            else
            {
                if (CurDirDropTarget != NULL) // determine default drop effect
                {
                    CurDirDropTarget->DragOver(grfKeyState, pt, &defEffect);
                }
                else
                    defEffect = 0;
            }
        }
        else
        {
            if (TgtType == idtttArchive || TgtType == idtttPluginFS ||
                TgtType == idtttArchiveOnWinPath || TgtType == idtttFullPluginFSPath)
            {
                defEffect = *pdwEffect;
                if ((origKeyState & MK_SHIFT) != 0 && (origKeyState & MK_CONTROL) == 0 &&
                    (defEffect & DROPEFFECT_MOVE) != 0) // user wants Move
                {
                    defEffect = DROPEFFECT_MOVE;
                }
                else
                {
                    if ((origKeyState & MK_SHIFT) == 0 && (origKeyState & MK_CONTROL) != 0 &&
                        (defEffect & DROPEFFECT_COPY) != 0) // user wants Copy
                    {
                        defEffect = DROPEFFECT_COPY;
                    }
                }
                // determine default drop effect
                if (TgtType == idtttFullPluginFSPath && OldDataObjectSrcType == 2 /* FS */ &&
                    !OldDataObjectSrcFSPath.empty() && GetFSToFSDropEffect != NULL)
                { // FS to FS: get preferred effect from plugin
                    GetFSToFSDropEffect(OldDataObjectSrcFSPath.c_str(), CurDir.c_str(), *pdwEffect,
                                        origKeyState, &defEffect, GetFSToFSDropEffectParam);
                    if (defEffect == DROPEFFECT_NONE)
                        defEffect = 0; // drop-target error
                    DragFromPluginFSEffectIsFromPlugin = TRUE;
                }
                else // from disk to archive + from disk to FS: Copy has priority
                {
                    if ((defEffect & DROPEFFECT_COPY) != 0)
                        defEffect = DROPEFFECT_COPY;
                    else
                    {
                        if ((defEffect & DROPEFFECT_MOVE) != 0)
                            defEffect = DROPEFFECT_MOVE;
                        else
                            defEffect = DROPEFFECT_NONE; // should not happen (handled via: TgtType==idtttWindows + CurDirDropTarget==NULL)
                    }
                }
            }
            else
                defEffect = 0;
        }

        if (ConfirmDrop != NULL && !ConfirmDrop(*pdwEffect, defEffect, grfKeyState))
        {
            *pdwEffect = DROPEFFECT_NONE;
            return DragLeave();
        }
        *pdwEffect = defEffect;
        origEffect = *pdwEffect;

        if (CurDirDropTarget != NULL) // info about key changes (shift+control for other...), probably unnecessary since W2K
        {
            CurDirDropTarget->DragOver(grfKeyState, pt, &defEffect);
            defEffect = *pdwEffect;
        }
    }

    if (OldDataObject != NULL)
    {
        OldDataObject->Release();
        OldDataObject = NULL;
        OldDataObjectIsFake = FALSE;
        OldDataObjectIsSimple = -1; // unknown value
        OldDataObjectSrcType = 0;
        OldDataObjectSrcFSPath.clear();
    }

    int dataObjectSrcType;
    std::wstring dataObjectSrcFSPath;
    BOOL isFake = IsFakeDataObject(pDataObject, &dataObjectSrcType, &dataObjectSrcFSPath);
    BOOL tgtFile = TRUE; // is the operation target a file?
    CDragDropOperData* namesList = new CDragDropOperData;
    if (GetCurDir != NULL)
    {
        int tgtType;
        const wchar_t* tgtPathWide = GetCurDir(pt, GetCurDirParam, pdwEffect, RButton, tgtFile,
                                               grfKeyState, tgtType, dataObjectSrcType);
        SetDirectory(tgtPathWide, grfKeyState, pt, pdwEffect, pDataObject, tgtFile, tgtType);
        if (TgtType != idtttWindows && TgtType != idtttFullPluginFSPath &&
            !IsSimpleSelection(pDataObject, namesList))
        { // if selection is not from one path (risk likely only with Find), we can't copy/move to archive or FS
            SetDirectory(NULL, grfKeyState, pt, pdwEffect, pDataObject, FALSE, idtttWindows);
            if (DropEnd != NULL)
                DropEnd(FALSE, FALSE, DropEndParam, FALSE, FALSE, TgtType);
            if (namesList != NULL)
                delete namesList;
            return S_OK;
        }
    }

    BOOL operationDone = FALSE;
    HRESULT ret = E_UNEXPECTED;
    if (TgtType == idtttWindows)
    {
        // determine defEffect
        BOOL ownRutine = !tgtFile && !isFake && (UseOwnRutine == NULL || UseOwnRutine(pDataObject));
        if (ownRutine && defEffect == -1)
        {
            if (lastEffect != -1)
                defEffect = lastEffect;
            else
            {
                defEffect = *pdwEffect;
                if (CurDirDropTarget != NULL) // determine default drop effect
                {
                    CurDirDropTarget->DragOver(grfKeyState, pt, &defEffect);
                }
                else
                    defEffect = 0;
            }
        }

        // try to handle it ourselves
        defEffect &= DROPEFFECT_COPY | DROPEFFECT_MOVE;
        if (ownRutine &&
            (defEffect == DROPEFFECT_COPY || defEffect == DROPEFFECT_MOVE) &&
            pDataObject != NULL && DoCopyMove != NULL)
        { // won't we be able to perform the operation ourselves?
            IEnumFORMATETC* enumFormat;
            if (pDataObject->EnumFormatEtc(DATADIR_GET, &enumFormat) == S_OK)
            {
                BOOL cfHDrop = FALSE;
                BOOL cfFileMapA = FALSE;
                BOOL cfFileMapW = FALSE;
                UINT CF_FileMapA = RegisterClipboardFormat(CFSTR_FILENAMEMAPA);
                UINT CF_FileMapW = RegisterClipboardFormat(CFSTR_FILENAMEMAPW);

                FORMATETC formatEtc;
                enumFormat->Reset();
                while (enumFormat->Next(1, &formatEtc, NULL) == S_OK)
                {
                    if (formatEtc.cfFormat == CF_FileMapA)
                        cfFileMapA = TRUE;
                    if (formatEtc.cfFormat == CF_FileMapW)
                        cfFileMapW = TRUE;
                    if (formatEtc.cfFormat == CF_HDROP)
                        cfHDrop = TRUE;
                }
                enumFormat->Release();

                if (cfHDrop &&
                    TryCopyOrMove(defEffect == DROPEFFECT_COPY, pDataObject, CF_FileMapA,
                                  CF_FileMapW, cfFileMapA, cfFileMapW))
                {
                    if (CurDirDropTarget != NULL)
                    {
                        CurDirDropTarget->DragLeave();
                        CurDirDropTarget->Release();
                        CurDirDropTarget = NULL;
                    }
                    operationDone = TRUE;
                    ret = S_OK;
                }
            }
        }

        // if it's a "fake" directory (unpack from archive, copy/move from FS), we handle it here
        if (!operationDone && isFake && !CurDir.empty())
        {
            // determine default drop effect - our data-object (may not be from this process): default is
            // Copy (fake is in TEMP, on the same disk it did Move by default, so we work around it this way)
            if ((origKeyState & MK_SHIFT) != 0 && (origKeyState & MK_CONTROL) == 0 &&
                (origEffect & DROPEFFECT_MOVE) != 0)
            {
                *pdwEffect = DROPEFFECT_MOVE;
            }
            else
            {
                if ((origEffect & DROPEFFECT_COPY) != 0)
                    *pdwEffect = DROPEFFECT_COPY;
                else
                {
                    if ((origEffect & DROPEFFECT_MOVE) != 0)
                        *pdwEffect = DROPEFFECT_MOVE;
                    else
                        *pdwEffect = DROPEFFECT_NONE; // drop-target error
                }
            }

            if (*pdwEffect == DROPEFFECT_COPY || *pdwEffect == DROPEFFECT_MOVE)
            {
                BOOL success = FALSE;
                if (SalShExtSharedMemView != NULL)
                {
                    WaitForSingleObject(SalShExtSharedMemMutex, INFINITE);
                    if ((SalShExtSharedMemView->StateFlags & SALSHEXT_STATE_DRAG_ACTIVE) != 0 &&
                        SalShExtPublishLocalResponseLocked(CurDir))
                    {
                        SalShExtSharedMemView->StateFlags |= SALSHEXT_STATE_DROP_DONE;
                        SalShExtSharedMemView->StateFlags &= ~SALSHEXT_STATE_PASTE_DONE;
                        SalShExtSharedMemView->Operation = *pdwEffect == DROPEFFECT_COPY ? SALSHEXT_COPY : SALSHEXT_MOVE;
                        success = TRUE;
                    }
                    ReleaseMutex(SalShExtSharedMemMutex);
                }
                if (success && CurDirDropTarget != NULL)
                {
                    CurDirDropTarget->DragLeave();
                    CurDirDropTarget->Release();
                    CurDirDropTarget = NULL;
                    operationDone = TRUE;
                    ret = S_OK;
                }
            }
        }

        // we can't handle it ourselves, let the system do it
        if (!operationDone && CurDirDropTarget != NULL)
        {
            ret = CurDirDropTarget->Drop(pDataObject, grfKeyState, pt, pdwEffect);
            CurDirDropTarget->Release();
            CurDirDropTarget = NULL;
        }
    }
    else // archives and FS
    {
        if (TgtType == idtttArchive || TgtType == idtttPluginFS ||
            TgtType == idtttArchiveOnWinPath || TgtType == idtttFullPluginFSPath)
        {
            DWORD allowedEffects = *pdwEffect;
            if ((origKeyState & MK_SHIFT) != 0 && (origKeyState & MK_CONTROL) == 0 &&
                (*pdwEffect & DROPEFFECT_MOVE) != 0) // user wants Move
            {
                *pdwEffect = DROPEFFECT_MOVE;
            }
            else
            {
                if ((origKeyState & MK_SHIFT) == 0 && (origKeyState & MK_CONTROL) != 0 &&
                    (*pdwEffect & DROPEFFECT_COPY) != 0) // user wants Copy
                {
                    *pdwEffect = DROPEFFECT_COPY;
                }
            }
            // determine default drop effect
            if (TgtType == idtttFullPluginFSPath && dataObjectSrcType == 2 /* FS */ &&
                !dataObjectSrcFSPath.empty() && GetFSToFSDropEffect != NULL)
            { // FS to FS: get preferred effect from plugin
                GetFSToFSDropEffect(dataObjectSrcFSPath.c_str(), CurDir.c_str(), allowedEffects,
                                    origKeyState, pdwEffect, GetFSToFSDropEffectParam);
                DragFromPluginFSEffectIsFromPlugin = TRUE;
            }
            else // from disk to archive + from disk to FS: Copy has priority
            {
                if ((*pdwEffect & DROPEFFECT_COPY) != 0)
                    *pdwEffect = DROPEFFECT_COPY;
                else
                {
                    if ((*pdwEffect & DROPEFFECT_MOVE) != 0)
                        *pdwEffect = DROPEFFECT_MOVE;
                    else
                        *pdwEffect = DROPEFFECT_NONE; // should not occur (handled via: TgtType==idtttWindows + CurDirDropTarget==NULL)
                }
            }

            if (*pdwEffect != DROPEFFECT_NONE)
            {
                BOOL operationAccepted = TRUE;
                if (TgtType == idtttFullPluginFSPath) // drag&drop z FS na FS
                {
                    if (isFake && dataObjectSrcType == 2 /* FS */ && !CurDir.empty() &&    // "always true"
                        (*pdwEffect == DROPEFFECT_COPY || *pdwEffect == DROPEFFECT_MOVE)) // "always true"
                    {
                        operationAccepted = FALSE;
                        if (SalShExtSharedMemView != NULL)
                        {
                            WaitForSingleObject(SalShExtSharedMemMutex, INFINITE);
                            if ((SalShExtSharedMemView->StateFlags & SALSHEXT_STATE_DRAG_ACTIVE) != 0 &&
                                SalShExtPublishLocalResponseLocked(CurDir))
                            {
                                SalShExtSharedMemView->StateFlags |= SALSHEXT_STATE_DROP_DONE;
                                SalShExtSharedMemView->StateFlags &= ~SALSHEXT_STATE_PASTE_DONE;
                                SalShExtSharedMemView->Operation = *pdwEffect == DROPEFFECT_COPY ? SALSHEXT_COPY : SALSHEXT_MOVE;
                                operationAccepted = TRUE;
                            }
                            ReleaseMutex(SalShExtSharedMemMutex);
                        }
                    }
                }
                else // TgtType: idtttArchive, idtttArchiveOnWinPath, idtttPluginFS
                {
                    if (DoDragDropOper != NULL && (*pdwEffect == DROPEFFECT_COPY || *pdwEffect == DROPEFFECT_MOVE) &&
                        namesList != NULL)
                    {
                        DoDragDropOper(*pdwEffect == DROPEFFECT_COPY, TgtType == idtttArchive || TgtType == idtttArchiveOnWinPath,
                                       TgtType == idtttArchiveOnWinPath ? CurDir.c_str() : (const wchar_t*)NULL,
                                       TgtType == idtttArchiveOnWinPath ? L"" : CurDir.c_str(), namesList, DoDragDropOperParam);
                        namesList = NULL; // DoDragDropOper will have it deallocated, we won't do it here anymore
                    }
                }
                if (operationAccepted)
                    ret = S_OK;
                else
                    *pdwEffect = DROPEFFECT_NONE;
            }
        }
    }

    if (DropEnd != NULL) // parameters 'operationDone' and 'isFake' are ignored in DropEnd for TgtType != idtttWindows
        DropEnd(TRUE, (*pdwEffect == DROPEFFECT_LINK), DropEndParam, operationDone, isFake, TgtType);
    TgtType = idtttWindows;
    if (namesList != NULL)
        delete namesList;
    return ret;
}

//*****************************************************************************
//
// CImpIDropSource
//

STDMETHODIMP CImpIDropSource::QueryInterface(REFIID refiid, void FAR * FAR * ppv)
{
    if (refiid == IID_IUnknown || refiid == IID_IDropSource)
    {
        *ppv = this;
        AddRef();
        return NOERROR;
    }
    else
    {
        *ppv = NULL;
        return E_NOINTERFACE;
    }
}

//*****************************************************************************
//
// InitializeShellib
//

BOOL InitializeShellib()
{
    CALL_STACK_MESSAGE1("InitializeShellib()");

    // OLE is now initialized directly in WinMainBody, because we attach SPY to it
    //  if (OleInitialize(NULL) != S_OK) // CoInitialize is no longer enough, for example window registration for drag&drop doesn't work then
    //  {
    //    TRACE_E("Error in OleInitialize.");
    //    return FALSE;
    //  }
    if (ExecuteAssociationTlsIndex == TLS_OUT_OF_INDEXES)
        ExecuteAssociationTlsIndex = HANDLES(TlsAlloc());
    return TRUE;
}

//*****************************************************************************
//
// ReleaseShellib
//

void ReleaseShellib()
{
    __try
    {
        if (ExecuteAssociationTlsIndex != TLS_OUT_OF_INDEXES)
        {
            HANDLES(TlsFree(ExecuteAssociationTlsIndex));
            ExecuteAssociationTlsIndex = TLS_OUT_OF_INDEXES;
        }
        OleFlushClipboard(); // hand over data from IDataObject that we left on clipboard to the system (this IDataObject will be released)
        // OLE is now deinitialized directly in WinMainBody (SPY)
        //    OleUninitialize();
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        OCUExceptionHasOccured++;
    }
}

//*****************************************************************************
//
// GetItemIdListForFileName
//

static BOOL StrRetToStringOwnedW(STRRET* str, LPCITEMIDLIST pidl, std::wstring& value)
{
    value.clear();
    wchar_t* shellValue = NULL;
    const HRESULT result = StrRetToStrW(str, pidl, &shellValue);
    if (FAILED(result) || shellValue == NULL)
        return FALSE;
    value = shellValue;
    CoTaskMemFree(shellValue);
    return TRUE;
}

LPITEMIDLIST GetItemIdListForFileName(LPSHELLFOLDER folder, const wchar_t* fileName,
                                      BOOL addUNCPrefix = FALSE, BOOL useEnumForPIDLs = FALSE,
                                      const wchar_t* enumNamePrefix = NULL)
{
    CALL_STACK_MESSAGE4("GetItemIdListForFileName(, %ls, %d, %d,)", fileName, addUNCPrefix, useEnumForPIDLs);

    // if we're looking for a name ending with space/dot, we have no choice but to search slowly
    // using enumeration of the entire folder
    if (!useEnumForPIDLs && enumNamePrefix != NULL && !addUNCPrefix)
    {
        int len = (int)wcslen(fileName);
        if (len > 0 && (fileName[len - 1] <= L' ' || fileName[len - 1] == L'.'))
            useEnumForPIDLs = TRUE;
    }
    if (useEnumForPIDLs) // slower variant, unfortunately necessary for getting PIDL of share on server
    {
        LPITEMIDLIST foundPidl = NULL;
        LPENUMIDLIST enumIDList;
        if (SUCCEEDED(folder->EnumObjects(NULL, SHCONTF_FOLDERS | SHCONTF_NONFOLDERS | SHCONTF_INCLUDEHIDDEN,
                                          &enumIDList)))
        {
            ULONG celt;
            LPITEMIDLIST idList;
            STRRET str;
            enumIDList->Reset();
            IMalloc* alloc;
            if (SUCCEEDED(CoGetMalloc(1, &alloc)))
            {
                int enumNamePrefixLen = enumNamePrefix == NULL ? 0 : (int)wcslen(enumNamePrefix);
                if (enumNamePrefixLen > 0 && enumNamePrefix[enumNamePrefixLen - 1] == L'\\')
                    enumNamePrefixLen--;
                while (1)
                {
                    if (enumIDList->Next(1, &idList, &celt) == NOERROR)
                    {
                        if (folder->GetDisplayNameOf(idList, SHGDN_FORPARSING, &str) == NOERROR)
                        {
                            std::wstring name;
                            if (StrRetToStringOwnedW(&str, idList, name))
                            {
                                if (!name.empty() && name.back() == L'\\')
                                    name.pop_back();
                                if (enumNamePrefix != NULL && StrNICmpW(name.c_str(), enumNamePrefix, enumNamePrefixLen) == 0 &&
                                        name.size() > (size_t)enumNamePrefixLen && name[enumNamePrefixLen] == L'\\' &&
                                        StrICmpW(name.c_str() + enumNamePrefixLen + 1, fileName) == 0 ||
                                    enumNamePrefix == NULL && StrICmpW(name.c_str(), fileName) == 0) // we have the share we're looking for
                                {
                                    foundPidl = idList;
                                    break; // pidl found (obtained)
                                }
                            }
                        }
                        if (alloc->DidAlloc(idList) == 1)
                            alloc->Free(idList);
                    }
                    else
                        break;
                }
                alloc->Release();
            }
            enumIDList->Release();
        }
        if (foundPidl != NULL)
            return foundPidl;
        else
            TRACE_E("GetItemIdListForFileName(): unable to find PIDL usign enumeration, trying to get it using ParseDisplayName...");
    }

    std::wstring olePath = addUNCPrefix ? L"\\\\" : L"";
    olePath += fileName;

    LPITEMIDLIST pidl;
    ULONG chEaten;
    HRESULT ret;
    if (SUCCEEDED((ret = folder->ParseDisplayName(NULL, NULL, &olePath[0], &chEaten,
                                                  &pidl, NULL))))
    {
        return pidl;
    }
    else
    {
        TRACE_E("ParseDisplayName error: 0x" << std::hex << ret << std::dec);
        return NULL;
    }
}

//*****************************************************************************
//
// DestroyItemIdList
//

void DestroyItemIdList(ITEMIDLIST** list, int itemsInList)
{
    CALL_STACK_MESSAGE2("DestroyItemIdList(, %d)", itemsInList);
    IMalloc* alloc;
    if (SUCCEEDED(CoGetMalloc(1, &alloc)))
    {
        int i;
        for (i = 0; i < itemsInList; i++)
        {
            if (list[i] != NULL && alloc->DidAlloc(list[i]) == 1)
            {
                alloc->Free(list[i]);
            }
        }
        alloc->Free(list);
        alloc->Release();
    }
}

//*****************************************************************************
//
// CreateItemIdList
//

ITEMIDLIST** CreateItemIdList(LPSHELLFOLDER folder, int files,
                              CEnumFileNamesFunction nextFile, void* param,
                              UINT& itemsInList, BOOL addUNCPrefix = FALSE,
                              BOOL useEnumForPIDLs = FALSE, const wchar_t* enumNamePrefix = NULL,
                              BOOL namesMustBeValid = FALSE)
{
    CALL_STACK_MESSAGE5("CreateItemIdList(, %d, , , , %d, %d, , %d)",
                        files, addUNCPrefix, useEnumForPIDLs, namesMustBeValid);
    if (files <= 0)
        return NULL;

    ITEMIDLIST** list = NULL;
    IMalloc* alloc;
    if (SUCCEEDED(CoGetMalloc(1, &alloc)))
    {
        list = (ITEMIDLIST**)alloc->Alloc(sizeof(ITEMIDLIST*) * files);
        alloc->Release();
    }
    if (list == NULL)
    {
        TRACE_E(LOW_MEMORY);
        return NULL;
    }
    memset(list, 0, sizeof(ITEMIDLIST*) * files);

    ITEMIDLIST* pidl = NULL;
    int i;
    for (i = 0; i < files; i++)
    {
        const wchar_t* fileNameW = nextFile(i, param);
        // e.g. for getting a functional data-object, it's necessary that contained names are valid,
        // drag&drop of invalid name means operation on name with silently trimmed spaces/dots
        // at the end (instead of "a   " it takes "a"), we definitely don't want that
        // FileNameIsInvalid scans raw bytes for ':' and the trailing character;
        // under a DBCS code page a multi-byte character's trailing byte can coincidentally
        // equal one of those ASCII values, misjudging a valid name. FileNameIsInvalidW scans
        // codepoints instead of raw bytes.
        if (namesMustBeValid && FileNameIsInvalidW(fileNameW, FALSE))
        {
            TRACE_I("CreateItemIdList: unable to create IdList because of invalid name");
            pidl = NULL;
            break;
        }
        pidl = (ITEMIDLIST*)GetItemIdListForFileName(folder, fileNameW, addUNCPrefix, useEnumForPIDLs, enumNamePrefix);
        if (pidl != NULL)
            list[i] = pidl;
        else
            break; // some error
    }

    if (pidl == NULL)
    {
        DestroyItemIdList(list, files);
        itemsInList = 0;
        return NULL;
    }
    else
    {
        itemsInList = files;
        return list;
    }
}

//*****************************************************************************
//
// GetShellFolder
//

BOOL GetShellFolder(const wchar_t* dir, IShellFolder*& shellFolderObj, LPITEMIDLIST& pidlFolder)
{
    CALL_STACK_MESSAGE2("GetShellFolder(%ls, ,)", dir);
    shellFolderObj = NULL;
    pidlFolder = NULL;
    HRESULT ret;
    LPSHELLFOLDER desktop;
    // if path contains components ending with spaces/dots, shell won't return
    // folder for the requested path, but for the path created by trimming these
    // spaces/dots, so we'd better give up on it early...
    if (PathContainsValidComponents(dir))
    {
        if (SUCCEEDED((ret = SHGetDesktopFolder(&desktop))))
        {
            int rootFolder;
            if (dir[0] != L'\\')
                rootFolder = CSIDL_DRIVES; // normal path
            else
                rootFolder = CSIDL_NETWORK; // UNC - network resources
            LPITEMIDLIST rootFolderID;
            if (SUCCEEDED((ret = SHGetSpecialFolderLocation(NULL, rootFolder, &rootFolderID))))
            {
                if (SUCCEEDED((ret = desktop->BindToObject(rootFolderID, NULL,
                                                           IID_IShellFolder,
                                                           (LPVOID*)&shellFolderObj))))
                {
                    std::wstring dirPart(dir);
                    std::wstring root = GetRootPath(dir);
                    if (root.size() < wcslen(dir)) // it's not a root path
                    {
                        std::wstring fullPath(dir);
                        if (!fullPath.empty() && fullPath.back() == L'\\')
                            fullPath.pop_back();
                        const size_t separator = fullPath.find_last_of(L'\\');
                        const std::wstring upperDir = fullPath.substr(0, separator + 1);
                        LPITEMIDLIST pidlUpperDir = GetItemIdListForFileName(shellFolderObj, upperDir.c_str());
                        LPSHELLFOLDER folder2;
                        if (pidlUpperDir != NULL &&
                            SUCCEEDED((ret = shellFolderObj->BindToObject(pidlUpperDir, NULL,
                                                                          IID_IShellFolder, (LPVOID*)&folder2))))
                        {
                            shellFolderObj->Release();
                            shellFolderObj = folder2;
                            dirPart = fullPath.substr(separator + 1);
                        }
                        else
                            TRACE_E("BindToObject error: 0x" << std::hex << ret << std::dec); // dir stays unchanged
                        CoTaskMemFree(pidlUpperDir);
                    }
                    else
                    {
                        if (rootFolder == CSIDL_DRIVES)
                        {
                            LPENUMIDLIST enumIDList;
                            if (SUCCEEDED((ret = shellFolderObj->EnumObjects(NULL, SHCONTF_FOLDERS | SHCONTF_INCLUDEHIDDEN,
                                                                             &enumIDList))))
                            {
                                ULONG celt;
                                LPITEMIDLIST idList;
                                STRRET str;
                                enumIDList->Reset();
                                IMalloc* alloc;
                                if (SUCCEEDED(CoGetMalloc(1, &alloc)))
                                {
                                    while (1)
                                    {
                                        ret = enumIDList->Next(1, &idList, &celt);
                                        if (ret == NOERROR)
                                        {
                                            ret = shellFolderObj->GetDisplayNameOf(idList, SHGDN_FORPARSING, &str);
                                            if (ret == NOERROR)
                                            {
                                                // Normalize every STRRET arm into owned UTF-16; never mutate shell PIDL bytes.
                                                std::wstring name;
                                                if (StrRetToStringOwnedW(&str, idList, name))
                                                {
                                                    if (name.size() <= 3 && StrNICmpW(name.c_str(), root.c_str(), 2) == 0) // name = "c:" or "c:\"
                                                    {
                                                        pidlFolder = idList;
                                                        break; // pidl found (obtained)
                                                    }
                                                }
                                            }
                                            if (alloc->DidAlloc(idList) == 1)
                                                alloc->Free(idList);
                                        }
                                        else
                                            break;
                                    }
                                    alloc->Release();
                                }
                                enumIDList->Release();
                            }
                        }
                        else
                        {
                            if (rootFolder == CSIDL_NETWORK) // we need to get complex pidl, otherwise mapping doesn't work
                            {
                                if (!root.empty() && root.back() == L'\\')
                                    root.pop_back();
                                const std::wstring rootW = root;
                                dirPart = root;
                                const size_t serverSeparator = root.find(L'\\', 2);
                                if (root == L"\\\\") // root of network
                                {
                                    shellFolderObj->Release();
                                    shellFolderObj = desktop;
                                    desktop = NULL;
                                    pidlFolder = rootFolderID;
                                    rootFolderID = NULL;
                                }
                                else
                                {
                                    BOOL setWait = (GetCursor() != LoadCursor(NULL, IDC_WAIT)); // already waiting?
                                    HCURSOR oldCur;
                                    if (setWait)
                                        oldCur = SetCursor(LoadCursor(NULL, IDC_WAIT));

                                    BOOL dirIsOnlyServer = serverSeparator == std::wstring::npos;
                                    const std::wstring server = dirIsOnlyServer ? root : root.substr(0, serverSeparator);
                                    LPITEMIDLIST pidl = GetItemIdListForFileName(shellFolderObj, server.c_str());
                                    if (dirIsOnlyServer) // network path "\\\\server" (server on network)
                                    {
                                        pidlFolder = pidl;
                                        pidl = NULL;
                                    }
                                    else
                                    {
                                        LPSHELLFOLDER folder2;
                                        if (pidl != NULL &&
                                            SUCCEEDED((ret = shellFolderObj->BindToObject(pidl, NULL,
                                                                                          IID_IShellFolder, (LPVOID*)&folder2))))
                                        {
                                            LPENUMIDLIST enumIDList;
                                            if (SUCCEEDED((ret = folder2->EnumObjects(NULL, SHCONTF_FOLDERS | SHCONTF_NONFOLDERS | SHCONTF_INCLUDEHIDDEN,
                                                                                      &enumIDList))))
                                            {
                                                ULONG celt;
                                                LPITEMIDLIST idList;
                                                STRRET str;
                                                enumIDList->Reset();
                                                IMalloc* alloc;
                                                if (SUCCEEDED(CoGetMalloc(1, &alloc)))
                                                {
                                                    while (1)
                                                    {
                                                        ret = enumIDList->Next(1, &idList, &celt);
                                                        if (ret == NOERROR)
                                                        {
                                                            ret = folder2->GetDisplayNameOf(idList, SHGDN_FORPARSING, &str);
                                                            if (ret == NOERROR)
                                                            {
                                                                // Normalize every STRRET arm into owned UTF-16; never mutate shell PIDL bytes.
                                                                std::wstring name;
                                                                if (StrRetToStringOwnedW(&str, idList, name))
                                                                {
                                                                    if (!name.empty() && name.back() == L'\\')
                                                                        name.pop_back();
                                                                    if (StrICmpW(name.c_str(), rootW.c_str()) == 0)
                                                                    {
                                                                        pidlFolder = idList;
                                                                        LPSHELLFOLDER swap = shellFolderObj;
                                                                        shellFolderObj = folder2;
                                                                        folder2 = swap;
                                                                        break; // pidl found (obtained)
                                                                    }
                                                                }
                                                            }
                                                            if (alloc->DidAlloc(idList) == 1)
                                                                alloc->Free(idList);
                                                        }
                                                        else
                                                            break;
                                                    }
                                                    alloc->Release();
                                                }
                                                enumIDList->Release();
                                            }
                                            folder2->Release();
                                        }
                                    }
                                    IMalloc* alloc;
                                    if (pidl != NULL && SUCCEEDED(CoGetMalloc(1, &alloc)))
                                    {
                                        if (alloc->DidAlloc(pidl) == 1)
                                            alloc->Free(pidl);
                                        alloc->Release();
                                    }
                                    if (setWait)
                                        SetCursor(oldCur);
                                }
                            }
                        }
                    }
                    if (pidlFolder == NULL)
                        pidlFolder = GetItemIdListForFileName(shellFolderObj, dirPart.c_str());

                    // shellFolderObj + pidlFolder  -> together they represent "dir" folder
                }
                else
                    TRACE_E("BindToObject error: 0x" << std::hex << ret << std::dec);
                IMalloc* alloc;
                if (rootFolderID != NULL && SUCCEEDED(CoGetMalloc(1, &alloc)))
                {
                    if (alloc->DidAlloc(rootFolderID) == 1)
                        alloc->Free(rootFolderID);
                    alloc->Release();
                }
            }
            else
                TRACE_E("SHGetSpecialFolderLocation error: 0x" << std::hex << ret << std::dec);
            if (desktop != NULL)
                desktop->Release();
        }
        else
            TRACE_E("SHGetDesktopFolder error: 0x" << std::hex << ret << std::dec);
    }
    else
        TRACE_IW(L"GetShellFolder: unable to get folder for path containing invalid components: \"" << dir << L"\"");
    if (shellFolderObj != NULL && pidlFolder != NULL)
        return TRUE;
    else
    {
        if (shellFolderObj != NULL)
            shellFolderObj->Release();
        if (pidlFolder != NULL)
        {
            IMalloc* alloc;
            if (SUCCEEDED(CoGetMalloc(1, &alloc)))
            {
                if (alloc->DidAlloc(pidlFolder) == 1)
                    alloc->Free(pidlFolder);
                alloc->Release();
            }
        }
        return FALSE;
    }
}

// The narrow CreateIDataObject / CreateIDataObjectAux stood here.
// CreateIDataObjectW above replaces them, and with their four callers migrated they
// had no callers left. They were also the LAST dependent of the ANSI shell-namespace
// walk that had no wide sibling.

//*****************************************************************************
//
// CreateIContextMenu2
//

IContextMenu2* CreateIContextMenu2Aux(HWND hOwnerWindow, const wchar_t* rootDirW, int files,
                                      CEnumFileNamesFunction nextFile, void* param)
{
    CALL_STACK_MESSAGE3("CreateIContextMenu2Aux(, %ls, %d, ,)", rootDirW, files);
    if (rootDirW == NULL)
        return NULL;

    IContextMenu2* contextMenu2Obj = NULL;
    IShellFolder* shellFolderObj;
    LPITEMIDLIST pidlFolder;
    if (GetShellFolder(rootDirW, shellFolderObj, pidlFolder))
    {
        HRESULT ret;
        LPSHELLFOLDER folder;
        if (SUCCEEDED((ret = shellFolderObj->BindToObject(pidlFolder, NULL,
                                                          IID_IShellFolder, (LPVOID*)&folder))))
        {
            UINT itemsInList;
            ITEMIDLIST** list;

            list = CreateItemIdList(folder, files, nextFile, param, itemsInList,
                                    wcscmp(rootDirW, L"\\\\") == 0,
                                    wcslen(rootDirW) > 2 && rootDirW[0] == L'\\' && rootDirW[1] == L'\\' &&
                                        wcschr(rootDirW + 2, L'\\') == NULL,
                                    rootDirW);
            if (list != NULL)
            {
                IContextMenu* contextMenuObj;
                if (SUCCEEDED((ret = folder->GetUIObjectOf(hOwnerWindow, itemsInList, (LPCITEMIDLIST*)list,
                                                           IID_IContextMenu, NULL,
                                                           (LPVOID*)&contextMenuObj))))
                {
                    if (!SUCCEEDED((ret = contextMenuObj->QueryInterface(IID_IContextMenu2,
                                                                         (void**)&contextMenu2Obj))))
                    {
                        TRACE_E("QueryInterface error: 0x" << std::hex << ret << std::dec);
                    }
                    contextMenuObj->Release();
                }
                else
                    TRACE_E("GetUIObjectOf error: 0x" << std::hex << ret << std::dec);
                DestroyItemIdList(list, itemsInList);
            }
            folder->Release();
        }
        else
            TRACE_E("BindToObject error: 0x" << std::hex << ret << std::dec);

        IMalloc* alloc;
        if (pidlFolder != NULL && SUCCEEDED(CoGetMalloc(1, &alloc)))
        {
            if (alloc->DidAlloc(pidlFolder) == 1)
                alloc->Free(pidlFolder);
            alloc->Release();
        }
        shellFolderObj->Release();
    }
    return contextMenu2Obj;
}

IContextMenu2* CreateIContextMenu2(HWND hOwnerWindow, const wchar_t* rootDirW, int files,
                                   CEnumFileNamesFunction nextFile, void* param)
{
    __try
    {
        return CreateIContextMenu2Aux(hOwnerWindow, rootDirW, files, nextFile, param);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        SHLExceptionHasOccured++;
    }
    return NULL; // error
}

// Folder-only form for the two shell-namespace roots that SHParseDisplayName
// does not bind. Keep its argument wide until this exact legacy boundary.
IContextMenu2* CreateNetworkRootContextMenuAux(HWND hOwnerWindow, const wchar_t* dirW)
{
    if (dirW == NULL || *dirW == L'\0')
        return NULL;
    CALL_STACK_MESSAGE2("CreateNetworkRootContextMenuAux(, %ls)", dirW);
    IContextMenu2* contextMenu2Obj = NULL;
    IShellFolder* shellFolderObj;
    LPITEMIDLIST pidlFolder;
    if (GetShellFolder(dirW, shellFolderObj, pidlFolder))
    {
        HRESULT ret;
        IContextMenu* contextMenuObj;
        if (SUCCEEDED((ret = shellFolderObj->GetUIObjectOf(
                hOwnerWindow, 1, (LPCITEMIDLIST*)&pidlFolder, IID_IContextMenu,
                NULL, (LPVOID*)&contextMenuObj))))
        {
            if (!SUCCEEDED((ret = contextMenuObj->QueryInterface(
                    IID_IContextMenu2, (void**)&contextMenu2Obj))))
                TRACE_E("QueryInterface error: 0x" << std::hex << ret << std::dec);
            contextMenuObj->Release();
        }
        else
            TRACE_E("GetUIObjectOf error: 0x" << std::hex << ret << std::dec);

        IMalloc* alloc;
        if (pidlFolder != NULL && SUCCEEDED(CoGetMalloc(1, &alloc)))
        {
            if (alloc->DidAlloc(pidlFolder) == 1)
                alloc->Free(pidlFolder);
            alloc->Release();
        }
        shellFolderObj->Release();
    }
    return contextMenu2Obj;
}

IContextMenu2* CreateNetworkRootContextMenu(HWND hOwnerWindow, const wchar_t* dirW)
{
    __try
    {
        return CreateNetworkRootContextMenuAux(hOwnerWindow, dirW);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        SHLExceptionHasOccured++;
    }
    return NULL;
}

//*****************************************************************************
//
// CreateIContextMenu2W - wide, partial-failure-tolerant selection menu (issue #79)
//

// Binds the IShellFolder for a wide path.
//
// The special network-root walker above handles namespace roots that SHParseDisplayName
// cannot bind. General filesystem paths use this direct wide binding helper.
//
// Returns NULL on failure; the caller releases the folder.
static IShellFolder* BindShellFolderW(const wchar_t* dirW)
{
    if (dirW == NULL || *dirW == 0)
        return NULL;

    // Same early refusal GetShellFolder makes, and for the same reason: if a component ends in a
    // space or a dot the shell does not fail - it silently binds the TRIMMED path, so a data
    // object or a context menu built here would act on "a" when the user selected "a   ".
    // SHParseDisplayName trims exactly the same way, so losing this check did not turn the case
    // into an error, it turned it into a wrong-target success.
    if (!PathContainsValidComponents(dirW))
    {
        TRACE_IW(L"BindShellFolderW: refusing a path with invalid components: \"" << dirW << L"\"");
        return NULL;
    }

    LPITEMIDLIST folderPidl = NULL;
    if (FAILED(SHParseDisplayName(dirW, NULL, &folderPidl, 0, NULL)) || folderPidl == NULL)
        return NULL;

    IShellFolder* desktop = NULL;
    IShellFolder* folder = NULL;
    if (SUCCEEDED(SHGetDesktopFolder(&desktop)))
    {
        if (FAILED(desktop->BindToObject(folderPidl, NULL, IID_IShellFolder, (LPVOID*)&folder)))
            folder = NULL;
        desktop->Release();
    }
    CoTaskMemFree(folderPidl);
    return folder;
}

IDataObject* CreateIDataObjectWAux(HWND hOwnerWindow, const wchar_t* rootDirW, int files,
                                   CEnumFileNamesFunction nextFile, void* param)
{
    CALL_STACK_MESSAGE2("CreateIDataObjectWAux(, , %d, ,)", files);

    if (rootDirW == NULL || *rootDirW == 0 || files <= 0 || nextFile == NULL)
        return NULL;

    IShellFolder* folder = BindShellFolderW(rootDirW);
    if (folder == NULL)
    {
        TRACE_E("CreateIDataObjectWAux(): could not bind the root path");
        return NULL;
    }

    // ALL-OR-NOTHING, deliberately unlike CreateIContextMenu2WAux above.
    // That one skips a name it cannot resolve, because losing one entry from a context
    // menu beats losing the menu. A data object is the opposite: it becomes a clipboard
    // payload or a drag, so dropping one file silently would copy nine of ten and report
    // success. The narrow CreateItemIdList() it replaces was called with
    // namesMustBeValid = TRUE, so this also preserves the existing behaviour exactly.
    std::vector<LPITEMIDLIST> pidls;
    pidls.reserve(files);
    BOOL ok = TRUE;
    for (int i = 0; ok && i < files; i++)
    {
        const wchar_t* name = nextFile(i, param);
        if (name == NULL || *name == 0)
        {
            TRACE_E("CreateIDataObjectWAux(): enumeration returned no name");
            ok = FALSE;
            break;
        }
        // The namesMustBeValid = TRUE arm the comment above claims, actually implemented.
        // CreateItemIdList still carries it (see its own reasoning at the FileNameIsInvalidW call):
        // a name ending in spaces or dots is not rejected by ParseDisplayName, it is silently
        // TRIMMED, so the data object would name "a" where the user selected "a   " - and a data
        // object is a clipboard payload or a drop, so that wrong target is what gets copied,
        // moved, or deleted.
        if (FileNameIsInvalidW(name, FALSE))
        {
            TRACE_IW(L"CreateIDataObjectWAux(): invalid name in selection: \"" << name << L"\"");
            ok = FALSE;
            break;
        }
        LPITEMIDLIST pidl = NULL;
        ULONG chEaten = 0;
        // ParseDisplayName takes a non-const buffer in some SDKs, so hand it a copy.
        std::wstring mutableName(name);
        if (SUCCEEDED(folder->ParseDisplayName(NULL, NULL, &mutableName[0], &chEaten, &pidl, NULL)) &&
            pidl != NULL)
        {
            pidls.push_back(pidl);
        }
        else
        {
            TRACE_E("CreateIDataObjectWAux(): could not resolve a selected name");
            ok = FALSE;
        }
    }

    IDataObject* dataObj = NULL;
    if (ok && !pidls.empty())
    {
        HRESULT ret = folder->GetUIObjectOf(hOwnerWindow, (UINT)pidls.size(),
                                            (LPCITEMIDLIST*)&pidls[0], IID_IDataObject, NULL,
                                            (LPVOID*)&dataObj);
        if (!SUCCEEDED(ret))
        {
            TRACE_E("GetUIObjectOf error: 0x" << std::hex << ret << std::dec);
            dataObj = NULL;
        }
    }

    for (size_t i = 0; i < pidls.size(); i++)
        CoTaskMemFree(pidls[i]);
    folder->Release();
    return dataObj;
}

IDataObject* CreateIDataObjectW(HWND hOwnerWindow, const wchar_t* rootDirW, int files,
                                CEnumFileNamesFunction nextFile, void* param)
{
    __try
    {
        return CreateIDataObjectWAux(hOwnerWindow, rootDirW, files, nextFile, param);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        SHLExceptionHasOccured++;
    }
    return NULL; // error
}

//
// CreateIContextMenu2W - wide, partial-failure-tolerant selection menu (issue #79)
//
IContextMenu2* CreateIContextMenu2WAux(HWND hOwnerWindow, const wchar_t* rootDirW,
                                       const std::vector<std::wstring>& names,
                                       CShellPidlResolveStats* stats)
{
    CALL_STACK_MESSAGE1("CreateIContextMenu2WAux()");

    if (stats != NULL)
    {
        stats->Requested = (int)names.size();
        stats->Resolved = 0;
    }
    if (rootDirW == NULL || names.empty())
        return NULL;

    IShellFolder* folder = BindShellFolderW(rootDirW);
    if (folder == NULL)
    {
        TRACE_E("CreateIContextMenu2WAux(): could not bind the panel path");
        return NULL;
    }

    // Resolve each selected name from its wide form. Unlike CreateItemIdList(), a name
    // that cannot be resolved is skipped rather than discarding the whole selection: one
    // unrepresentable name must not cost the user their entire context menu.
    std::vector<LPITEMIDLIST> pidls;
    pidls.reserve(names.size());
    for (size_t i = 0; i < names.size(); i++)
    {
        LPITEMIDLIST pidl = NULL;
        ULONG chEaten = 0;
        // ParseDisplayName takes a non-const buffer in some SDKs, so hand it a copy.
        std::wstring mutableName = names[i];
        if (SUCCEEDED(folder->ParseDisplayName(NULL, NULL, &mutableName[0], &chEaten, &pidl, NULL)) &&
            pidl != NULL)
        {
            pidls.push_back(pidl);
        }
        else
        {
            TRACE_I("CreateIContextMenu2WAux(): could not resolve a selected name");
        }
    }

    if (stats != NULL)
        stats->Resolved = (int)pidls.size();

    IContextMenu2* contextMenu2Obj = NULL;
    if (!pidls.empty())
    {
        IContextMenu* contextMenuObj = NULL;
        if (SUCCEEDED(folder->GetUIObjectOf(hOwnerWindow, (UINT)pidls.size(),
                                            (LPCITEMIDLIST*)&pidls[0], IID_IContextMenu, NULL,
                                            (LPVOID*)&contextMenuObj)))
        {
            if (FAILED(contextMenuObj->QueryInterface(IID_IContextMenu2, (void**)&contextMenu2Obj)))
                contextMenu2Obj = NULL;
            contextMenuObj->Release();
        }
    }

    for (size_t i = 0; i < pidls.size(); i++)
        CoTaskMemFree(pidls[i]);
    folder->Release();

    return contextMenu2Obj;
}

IContextMenu2* CreateIContextMenu2W(HWND hOwnerWindow, const wchar_t* rootDirW,
                                    const std::vector<std::wstring>& names,
                                    CShellPidlResolveStats* stats)
{
    __try
    {
        return CreateIContextMenu2WAux(hOwnerWindow, rootDirW, names, stats);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        SHLExceptionHasOccured++;
    }
    return NULL; // error
}

// The generic narrow folder-only overload stood here. General
// callers now use CreateIContextMenu2W(hwnd, dirW), which binds through
// SHParseDisplayName. The only retained namespace-walk case is the explicitly
// named CreateNetworkRootContextMenu above for "\\\\" and "\\\\server".

// Wide sibling of the folder-only overload above.
//
// Note this is the folder's menu *as an item in its parent* (Open, Cut, Copy, Properties,
// and whatever extensions add), not the background menu - which is why it binds the parent
// and asks for the child, exactly as the ANSI version does via GetShellFolder(). The
// background menu is GetNewOrBackgroundMenuW().
IContextMenu2* CreateIContextMenu2WAux(HWND hOwnerWindow, const wchar_t* dirW)
{
    CALL_STACK_MESSAGE1("CreateIContextMenu2WAux(dir)");
    if (dirW == NULL || *dirW == 0)
        return NULL;

    LPITEMIDLIST absPidl = NULL;
    if (FAILED(SHParseDisplayName(dirW, NULL, &absPidl, 0, NULL)) || absPidl == NULL)
        return NULL;

    IContextMenu2* contextMenu2Obj = NULL;
    IShellFolder* parentFolder = NULL;
    LPCITEMIDLIST childPidl = NULL;
    // childPidl points into absPidl, so absPidl must outlive this block.
    if (SUCCEEDED(SHBindToParent(absPidl, IID_IShellFolder, (void**)&parentFolder, &childPidl)) &&
        parentFolder != NULL && childPidl != NULL)
    {
        IContextMenu* contextMenuObj = NULL;
        if (SUCCEEDED(parentFolder->GetUIObjectOf(hOwnerWindow, 1, &childPidl, IID_IContextMenu,
                                                  NULL, (LPVOID*)&contextMenuObj)) &&
            contextMenuObj != NULL)
        {
            if (FAILED(contextMenuObj->QueryInterface(IID_IContextMenu2, (void**)&contextMenu2Obj)))
                contextMenu2Obj = NULL;
            contextMenuObj->Release();
        }
        parentFolder->Release();
    }
    CoTaskMemFree(absPidl);
    return contextMenu2Obj;
}

IContextMenu2* CreateIContextMenu2W(HWND hOwnerWindow, const wchar_t* dirW)
{
    __try
    {
        return CreateIContextMenu2WAux(hOwnerWindow, dirW);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        SHLExceptionHasOccured++;
    }
    return NULL; // error
}

//*****************************************************************************
//
// HasDropTarget
//

// The definition lagged its own (already wide) declaration, so both
// callers were binding to a promise with no body. Widened here rather than reverting
// the header because neither caller has anything narrow to offer, and the wide
// implementation this delegates to already exists.
BOOL HasDropTarget(const wchar_t* dir)
{
    CALL_STACK_MESSAGE2("HasDropTarget(%ls)", dir);
    /*
  IShellFolder *shellFolderObj;
  LPITEMIDLIST pidlFolder;
  ULONG attrs = 0;
  if (GetShellFolder(dir, shellFolderObj, pidlFolder))
  {
    HRESULT ret;
    attrs = SFGAO_DROPTARGET;  // we only query this attribute
    if (!SUCCEEDED((ret = shellFolderObj->GetAttributesOf(1, (LPCITEMIDLIST *)&pidlFolder, &attrs))))
    {
      TRACE_E("GetAttributesOf error: " << hex << ret);
      attrs = 0;
    }

    IMalloc *alloc;
    if (pidlFolder != NULL && SUCCEEDED(CoGetMalloc(1, &alloc)))
    {
      if (alloc->DidAlloc(pidlFolder) == 1) alloc->Free(pidlFolder);
      alloc->Release();
    }
    shellFolderObj->Release();
  }
  return (attrs & SFGAO_DROPTARGET) != 0;
*/
    // The W sibling binds through SHParseDisplayName instead of walking the shell
    // namespace out of a CP_ACP string, so this no longer answers "no drop target"
    // for a directory the active code page cannot spell.
    IDropTarget* drop = CreateIDropTargetW(NULL, dir); // unfortunately there's no other way...
    if (drop != NULL)
    {
        drop->Release();
        return TRUE;
    }
    return FALSE;
}

// Wide: same "bind through SHParseDisplayName/SHBindToParent" shape as
// CreateIContextMenu2WAux's directory overload (this file) - reuses the already-built wide
// binding infrastructure instead of a widened copy of GetShellFolder's component-by-component
// narrow walk.
IDropTarget* CreateIDropTargetWAux(HWND hOwnerWindow, const wchar_t* dirW)
{
    CALL_STACK_MESSAGE1("CreateIDropTargetWAux(dir)");
    if (dirW == NULL || *dirW == 0)
        return NULL;

    LPITEMIDLIST absPidl = NULL;
    if (FAILED(SHParseDisplayName(dirW, NULL, &absPidl, 0, NULL)) || absPidl == NULL)
        return NULL;

    IDropTarget* dropTargetObj = NULL;
    IShellFolder* parentFolder = NULL;
    LPCITEMIDLIST childPidl = NULL;
    // childPidl points into absPidl, so absPidl must outlive this block.
    if (SUCCEEDED(SHBindToParent(absPidl, IID_IShellFolder, (void**)&parentFolder, &childPidl)) &&
        parentFolder != NULL && childPidl != NULL)
    {
        if (FAILED(parentFolder->GetUIObjectOf(hOwnerWindow, 1, &childPidl, IID_IDropTarget,
                                               NULL, (LPVOID*)&dropTargetObj)))
            dropTargetObj = NULL;
        parentFolder->Release();
    }
    CoTaskMemFree(absPidl);
    return dropTargetObj;
}

IDropTarget* CreateIDropTargetW(HWND hOwnerWindow, const wchar_t* dirW)
{
    __try
    {
        return CreateIDropTargetWAux(hOwnerWindow, dirW);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        SHLExceptionHasOccured++;
    }
    return NULL; // error
}

//*****************************************************************************
//
// OpenSpecFolder
//

void OpenSpecFolder(HWND hOwnerWindow, int specFolder)
{
    CALL_STACK_MESSAGE2("OpenSpecFolder(, %d)", specFolder);
    ITEMIDLIST* pidl;
    if (SHGetSpecialFolderLocation(NULL, specFolder, &pidl) == NOERROR && pidl != NULL)
    {
        CShellExecuteWnd shellExecuteWnd;
        // Explicitly SHELLEXECUTEINFOA/ShellExecuteExA - se.lpVerb is a narrow literal and no
        // path/argument field is set here, so the whole struct stays narrow.
        SHELLEXECUTEINFOA se;
        memset(&se, 0, sizeof(SHELLEXECUTEINFOA));
        se.cbSize = sizeof(SHELLEXECUTEINFOA);
        se.fMask = SEE_MASK_IDLIST;
        se.lpVerb = "open";
        se.hwnd = shellExecuteWnd.Create(hOwnerWindow, L"SEW: OpenSpecFolder specFolder=%d verb=%hs", specFolder, se.lpVerb);
        se.nShow = SW_SHOWNORMAL;
        se.lpIDList = pidl;
        ShellExecuteExA(&se);

        IMalloc* alloc;
        if (SUCCEEDED(CoGetMalloc(1, &alloc)))
        {
            if (pidl != NULL && alloc->DidAlloc(pidl) == 1)
                alloc->Free(pidl);
            alloc->Release();
        }
    }
}

//*****************************************************************************
//
// OpenFolder
//

void OpenFolderAndFocusItemW(HWND hOwnerWindow, const wchar_t* dir, const wchar_t* item)
{
    CALL_STACK_MESSAGE2("OpenFolder(, %ls)", dir);
    // if path contains components ending with spaces/dots, shell won't return
    // pidl for the requested path, but for the path created by trimming these
    // spaces/dots, so we'd better give up on it early...
    std::wstring mydir(dir);
    if (item[0] != 0)
        SalPathAppendW(mydir, item);
    if (PathContainsValidComponents(mydir.c_str()))
    {
        BOOL useOldMethod = TRUE; // SHOpenFolderAndSelectItems is supported since XP and we still run on W2K and XP without SPx
        if (item[0] != 0)         // if we don't have an item to select, we don't use SHOpenFolderAndSelectItems, because it would show parent directory, see MSDN
        {
            HMODULE hShell32 = LoadLibraryW(L"shell32.dll");
            if (hShell32 != NULL)
            {
                typedef HRESULT(WINAPI * F_SHOpenFolderAndSelectItems)(PCIDLIST_ABSOLUTE pidlFolder, UINT cidl, PCUITEMID_CHILD_ARRAY apidl, DWORD dwFlags);
                F_SHOpenFolderAndSelectItems mySHOpenFolderAndSelectItems = NULL;
                mySHOpenFolderAndSelectItems = (F_SHOpenFolderAndSelectItems)GetProcAddress(hShell32, "SHOpenFolderAndSelectItems"); // Min: XP
                if (mySHOpenFolderAndSelectItems != NULL)
                {
                    LPITEMIDLIST folderPidl = NULL;
                    LPITEMIDLIST childPidl = NULL;
                    IShellFolder* folder = BindShellFolderW(dir);
                    std::wstring mutableItem(item);
                    ULONG eaten = 0;
                    if (folder != NULL &&
                        SUCCEEDED(SHParseDisplayName(dir, NULL, &folderPidl, 0, NULL)) &&
                        SUCCEEDED(folder->ParseDisplayName(NULL, NULL, &mutableItem[0], &eaten,
                                                           &childPidl, NULL)) &&
                        folderPidl != NULL && childPidl != NULL)
                    {
                        PCUITEMID_CHILD children[] = {childPidl};
                        if (SUCCEEDED(mySHOpenFolderAndSelectItems(folderPidl, 1, children, 0)))
                            useOldMethod = FALSE;
                    }
                    CoTaskMemFree(childPidl);
                    CoTaskMemFree(folderPidl);
                    if (folder != NULL)
                        folder->Release();
                }
                FreeLibrary(hShell32);
            }
        }

        if (useOldMethod)
        {
            LPITEMIDLIST pidl = NULL;
            SHParseDisplayName(dir, NULL, &pidl, 0, NULL);

            if (pidl != NULL)
            {
                CShellExecuteWnd shellExecuteWnd;
                SHELLEXECUTEINFOW se;
                memset(&se, 0, sizeof(SHELLEXECUTEINFOW));
                se.cbSize = sizeof(SHELLEXECUTEINFOW);
                se.fMask = SEE_MASK_IDLIST;
                se.lpVerb = L"open";
                se.hwnd = shellExecuteWnd.Create(hOwnerWindow, L"SEW: OpenFolderAndFocusItem verb=%ls", se.lpVerb);
                se.nShow = SW_SHOWNORMAL;
                se.lpIDList = pidl;
                ShellExecuteExW(&se);

                CoTaskMemFree(pidl);
            }
        }
    }
    else
        TRACE_I("OpenFolderAndFocusItemW: unable to open folder for a path containing invalid components");
}

//*****************************************************************************
//
// GetTargetDirectory
//
// The narrow picker and the legacy tree-dialog callback are deleted. GetTargetDirectoryW routes
// the complete UTF-16 options and result through the modern shell adapter.

// Wide-native. The former implementation projected the input to ACP and let a
// legacy in-place resolver write the resolved shortcut target into storage sized for the input.
// That was not merely lossy: a folder shortcut's target is routinely LONGER than its shortcut path
// ("C:\...\NetHood\srv" -> "\\server\some\long\share"). That is a heap write past the end,
// reachable from the Find results "Change Directory" browse via GetTargetDirectoryW.
//
// Written wide throughout instead, so the result is assigned to a std::wstring and cannot
// overflow by construction, and a NetHood target the code page cannot spell survives.
void ResolveNetHoodPathW(std::wstring& path)
{
    if (path.empty() || path[0] == L'\\')
        return; // UNC path -> can't be NetHood

    const std::wstring root = GetRootPath(path.c_str());
    if (root.empty() || GetDriveTypeW(root.c_str()) != DRIVE_FIXED)
        return; // not a local fixed path -> can't be NetHood

    BOOL tryTarget = FALSE; // if TRUE, it's worth trying to find file "target.lnk"
    std::wstring name = path;
    SalPathAppendW(name, L"desktop.ini");
    HANDLE hFile = gFileSystem->CreateFile(name.c_str(), GENERIC_READ,
                                           FILE_SHARE_WRITE | FILE_SHARE_READ, NULL,
                                           OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, NULL);
    HANDLES_ADD_EX(__otQuiet, hFile != INVALID_HANDLE_VALUE, __htFile, __hoCreateFile, hFile, GetLastError(), TRUE);
    if (hFile != INVALID_HANDLE_VALUE)
    {
        uint64_t size = 0;
        if (gFileSystem->GetHandleFileSize(hFile, &size).success && size <= 1000) // so far all had 92 bytes
        {
            // The scan below stays BYTE-domain on purpose: it is looking for an ASCII CLSID
            // inside desktop.ini, not for text in any code page.
            char buf[1000];
            DWORD read;
            if (gFileSystem->ReadFromHandle(hFile, buf, 1000, &read).success && read != 0)
            {
                char* s = buf;
                char* end = buf + read;
                while (s < end) // search for CLSID "folder shortcut" in file
                {
                    if (*s == '{')
                    {
                        s++;
                        char* beg = s;
                        while (s < end && *s != '}')
                            s++;
                        if (s < end)
                        {
                            const char* folderShortcutCLSID = "0AFACED1-E828-11D1-9187-B532F1E9575D";
                            if (StrNICmp(beg, folderShortcutCLSID, (int)(s - beg)) == 0)
                            {
                                tryTarget = TRUE;
                                break;
                            }
                        }
                    }
                    else
                        s++;
                }
            }
        }
        HANDLES_REMOVE(hFile, __htFile, "IFileSystem::CloseHandle");
        gFileSystem->CloseFileHandle(hFile);
    }

    if (!tryTarget)
        return;

    name = path;
    SalPathAppendW(name, L"target.lnk");
    WIN32_FIND_DATAW data;
    HANDLE find = gFileSystem->FindFirstFile(name.c_str(), &data);
    if (find == INVALID_HANDLE_VALUE)
        return; // no target.lnk
    gFileSystem->CloseFind(find);

    HCURSOR oldCur = SetCursor(LoadCursor(NULL, IDC_WAIT));
    IShellLinkW* link;
    if (CoCreateInstance(CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER, IID_IShellLinkW,
                         (LPVOID*)&link) == S_OK)
    {
        IPersistFile* fileInt;
        if (link->QueryInterface(IID_IPersistFile, (LPVOID*)&fileInt) == S_OK)
        {
            if (fileInt->Load(name.c_str(), STGM_READ) == S_OK)
            {
                // we don't use Resolve because it's not that critical here and would slow
                // things down considerably
                std::wstring target;
                WIN32_FIND_DATAW dataW;
                if (GetShellLinkPathOwned(link, SLGP_UNCPRIORITY, target, &dataW))
                    path = std::move(target); // eureka, finally we know where that link leads
            }
            fileInt->Release();
        }
        link->Release();
    }
    SetCursor(oldCur);
}

BOOL GetTargetDirectoryW(HWND parent, HWND hCenterWindow, const wchar_t* title, const wchar_t* comment,
                         std::wstring& path, BOOL onlyNet, const wchar_t* initDir)
{
    (void)hCenterWindow; // IFileDialog is centered by its owner.
    FolderPickerOptions options;
    options.owner = parent;
    options.title = title;
    options.instruction = comment;
    options.initialDirectory = initDir;
    options.networkOnly = onlyNet != FALSE;

    IShell* shell = gShell != NULL ? gShell : GetWin32Shell();
    const ShellResult result = shell->PickFolder(options, path);
    if (result.success)
        ResolveNetHoodPathW(path);
    return result.success;
}

//*****************************************************************************
//
// GetNewOrBackgroundMenu
//
// hOwnerWindow - parent of opened windows (both error and context menu command windows)
// dir - directory from which to get New menu
// menu - return value - New submenu + its interfaces
// minCmd, maxCmd - range of possible command values in 'menu'
// backgoundMenu - TRUE = we want complete view-background menu (right-click behind items in Explorer; not just New menu, but also e.g. Tortoise CVS, etc.)

void GetMenuNewAux(IContextMenu2* contextMenu2, HMENU m, int minCmd, int maxCmd)
{
    CALL_STACK_MESSAGE_NONE

    // temporarily lower thread priority so some confused shell extension doesn't eat up CPU
    HANDLE hThread = GetCurrentThread(); // pseudo-handle, no need to release
    int oldThreadPriority = GetThreadPriority(hThread);
    SetThreadPriority(hThread, THREAD_PRIORITY_NORMAL);

    __try
    {
        UINT flags = CMF_NORMAL | CMF_EXPLORE;
        // handle pressed shift - extended context menu, under W2K for example Run as... is there
#define CMF_EXTENDEDVERBS 0x00000100 // rarely used verbs
        BOOL shiftPressed = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
        if (shiftPressed)
            flags |= CMF_EXTENDEDVERBS;

        contextMenu2->QueryContextMenu(m, 0, minCmd, maxCmd, flags);
    }
    __except (CCallStack::HandleException(GetExceptionInformation(), 16))
    {
        QCMExceptionHasOccured++;
    }

    SetThreadPriority(hThread, oldThreadPriority);
}

// Shared body of both GetNewOrBackgroundMenu overloads: everything from the bound folder
// onwards is identical, only the way the folder is reached differs (CP_ACP namespace walk
// vs. SHParseDisplayName). 'folder' stays owned by the caller.
static void FillNewOrBackgroundMenu(HWND hOwnerWindow, IShellFolder* folder, CMenuNew* menu,
                                    int minCmd, int maxCmd, BOOL backgoundMenu)
{
    HRESULT ret;
    IContextMenu* contextMenu;
    if (SUCCEEDED((ret = folder->CreateViewObject(hOwnerWindow, IID_IContextMenu,
                                                  (void**)&contextMenu))))
    {
        IContextMenu2* contextMenu2 = NULL;
        if (SUCCEEDED((ret = contextMenu->QueryInterface(IID_IContextMenu2,
                                                         (void**)&contextMenu2))))
        {
            HMENU m = CreatePopupMenu();
            if (m != NULL)
            {
                GetMenuNewAux(contextMenu2, m, minCmd, maxCmd);
                RemoveUselessSeparatorsFromMenu(m);

                if (backgoundMenu) // we take entire background menu
                {
                    menu->Set(contextMenu2, m);
                }
                else // we cut out only New menu
                {
                    MENUITEMINFO mi;
                    int index = 0;
                    int foundIndex = -1;
                    HMENU foundSubMenu = NULL;
                    while (1)
                    {
                        mi.cbSize = sizeof(mi);
                        mi.fMask = MIIM_SUBMENU;
                        if (GetMenuItemInfo(m, index, TRUE, &mi))
                        {
                            if (mi.hSubMenu != NULL)
                            { // looking for last submenu (user items hopefully only appear before Windows items, we'll see over time)
                                foundIndex = index;
                                foundSubMenu = mi.hSubMenu;
                            }
                        }
                        else
                            break;
                        index++;
                    }
                    if (foundIndex != -1)
                    {
                        menu->Set(contextMenu2, foundSubMenu);
                        RemoveMenu(m, foundIndex, MF_BYPOSITION);
                    }
                    DestroyMenu(m);
                }
            }
            if (!menu->MenuIsAssigned())
                contextMenu2->Release();
        }
        contextMenu->Release();
    }
}

// The narrow GetNewOrBackgroundMenu stood here. Its last three callers
// moved to GetNewOrBackgroundMenuW at P1.7k, leaving it dead.

// Wide sibling. Without it the New submenu and the background menu are built for the
// folder the CP_ACP mirror happens to name, so in a folder the code page cannot spell
// they come back empty - which reads as "this folder has no New menu".
void GetNewOrBackgroundMenuW(HWND hOwnerWindow, const wchar_t* dirW, CMenuNew* menu,
                             int minCmd, int maxCmd, BOOL backgoundMenu)
{
    CALL_STACK_MESSAGE3("GetNewOrBackgroundMenuW(, , , %d, %d)", minCmd, maxCmd);
    menu->Init();
    IShellFolder* folder = BindShellFolderW(dirW);
    if (folder == NULL)
        return;

    FillNewOrBackgroundMenu(hOwnerWindow, folder, menu, minCmd, maxCmd, backgoundMenu);
    folder->Release();
}

//*****************************************************************************
//
// CMenuNew
//

void CMenuNew::ReleaseBody()
{
    __try
    {
        // HMENU Menu is destroyed directly from the menu it was attached to
        if (Menu2 != NULL)
            Menu2->Release(); // this call sometimes crashes
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        MenuNewExceptionHasOccured++;
    }
    Init();
}

void CMenuNew::Release()
{
    CALL_STACK_MESSAGE1("CMenuNew::Release()");
    ReleaseBody();
}

//
//*****************************************************************************
// CTextDataObject
//

STDMETHODIMP CTextDataObject::QueryInterface(REFIID iid, void** ppv)
{
    if (iid == IID_IUnknown || iid == IID_IDataObject)
    {
        *ppv = this;
        AddRef();
        return NOERROR;
    }
    else
    {
        *ppv = NULL;
        return E_NOINTERFACE;
    }
}

STDMETHODIMP CTextDataObject::GetData(FORMATETC* formatEtc, STGMEDIUM* medium)
{
    if (formatEtc == NULL || medium == NULL)
        return E_INVALIDARG;
    if ((formatEtc->cfFormat == CF_TEXT || formatEtc->cfFormat == CF_UNICODETEXT) && (formatEtc->tymed & TYMED_HGLOBAL))
    {
        HGLOBAL dataDup = NULL; // we make a copy of Data
        if (Data != NULL || UnicodeData != NULL)
        {
            BOOL ok = FALSE;
            if (formatEtc->cfFormat == CF_TEXT)
            {
                if (Data != NULL)
                {
                    SIZE_T size = GlobalSize(Data);
                    dataDup = NOHANDLES(GlobalAlloc(GMEM_MOVEABLE | GMEM_DDESHARE, size));
                    if (dataDup != NULL)
                    {
                        void* ptr1 = HANDLES(GlobalLock(dataDup));
                        void* ptr2 = HANDLES(GlobalLock(Data));
                        if (ptr1 != NULL && ptr2 != NULL)
                        {
                            memcpy(ptr1, ptr2, size);
                            ok = TRUE;
                        }
                        if (ptr2 != NULL)
                            HANDLES(GlobalUnlock(Data));
                        if (ptr1 != NULL)
                            HANDLES(GlobalUnlock(dataDup));
                    }
                }
                else
                {
                    const wchar_t* ptr2 = (const wchar_t*)HANDLES(GlobalLock(UnicodeData));
                    if (ptr2 != NULL)
                    {
                        std::wstring wideText;
                        const SIZE_T sourceSize = GlobalSize(UnicodeData);
                        if (sally::clipboard::DecodeUnicodeClipboardPayload(
                                ptr2, sourceSize, wideText) == ERROR_SUCCESS)
                        {
                            std::string encoded;
                            if (Win32EncodeTextLossy(GetACP(), wideText.data(), wideText.size(), encoded))
                            {
                                dataDup = NOHANDLES(GlobalAlloc(
                                    GMEM_MOVEABLE | GMEM_DDESHARE, encoded.size() + 1));
                                if (dataDup != NULL)
                                {
                                    char* ptr1 = (char*)HANDLES(GlobalLock(dataDup));
                                    if (ptr1 != NULL)
                                    {
                                        if (!encoded.empty())
                                            memcpy(ptr1, encoded.data(), encoded.size());
                                        ptr1[encoded.size()] = '\0';
                                        ok = TRUE;
                                        HANDLES(GlobalUnlock(dataDup));
                                    }
                                }
                            }
                        }
                        HANDLES(GlobalUnlock(UnicodeData));
                    }
                }
            }
            else // formatEtc->cfFormat == CF_UNICODETEXT
            {
                if (UnicodeData != NULL)
                {
                    SIZE_T size = GlobalSize(UnicodeData);
                    dataDup = NOHANDLES(GlobalAlloc(GMEM_MOVEABLE | GMEM_DDESHARE, size));
                    if (dataDup != NULL)
                    {
                        void* ptr1 = HANDLES(GlobalLock(dataDup));
                        void* ptr2 = HANDLES(GlobalLock(UnicodeData));
                        if (ptr1 != NULL && ptr2 != NULL)
                        {
                            memcpy(ptr1, ptr2, size);
                            ok = TRUE;
                        }
                        if (ptr2 != NULL)
                            HANDLES(GlobalUnlock(UnicodeData));
                        if (ptr1 != NULL)
                            HANDLES(GlobalUnlock(dataDup));
                    }
                }
                else
                {
                    const char* ptr2 = (const char*)HANDLES(GlobalLock(Data));
                    if (ptr2 != NULL)
                    {
                        std::wstring decoded;
                        const SIZE_T sourceSize = GlobalSize(Data);
                        if (sally::clipboard::DecodeAnsiClipboardPayload(
                                ptr2, sourceSize, GetACP(), decoded) == ERROR_SUCCESS)
                        {
                            if (decoded.size() != (std::numeric_limits<size_t>::max)() &&
                                decoded.size() + 1 <=
                                    (std::numeric_limits<SIZE_T>::max)() / sizeof(wchar_t))
                            {
                                const SIZE_T byteSize =
                                    (decoded.size() + 1) * sizeof(wchar_t);
                                dataDup = NOHANDLES(GlobalAlloc(
                                    GMEM_MOVEABLE | GMEM_DDESHARE, byteSize));
                                if (dataDup != NULL)
                                {
                                    WCHAR* ptr1 = (WCHAR*)HANDLES(GlobalLock(dataDup));
                                    if (ptr1 != NULL)
                                    {
                                        memcpy(ptr1, decoded.c_str(), byteSize);
                                        ok = TRUE;
                                        HANDLES(GlobalUnlock(dataDup));
                                    }
                                }
                            }
                        }
                        HANDLES(GlobalUnlock(Data));
                    }
                }
            }
            if (!ok && dataDup != NULL)
            {
                NOHANDLES(GlobalFree(dataDup));
                dataDup = NULL;
            }
        }
        if (dataDup != NULL) // we have data, save to medium and return
        {
            medium->tymed = TYMED_HGLOBAL;
            medium->hGlobal = dataDup;
            medium->pUnkForRelease = NULL;
            return S_OK;
        }
        else
            return E_UNEXPECTED;
    }
    return (formatEtc->tymed & TYMED_HGLOBAL) ? DV_E_FORMATETC : DV_E_TYMED;
}

//
//*****************************************************************************
// GetMyDocumentsOrDesktopPath
//

BOOL GetMyDocumentsOrDesktopPathW(std::wstring& path)
{
    path.clear();
    IShell* shell = gShell != NULL ? gShell : GetWin32Shell();
    return shell->GetKnownFolderPath(FOLDERID_Documents, path).success ||
           shell->GetKnownFolderPath(FOLDERID_Desktop, path).success;
}

//
//*****************************************************************************
// GetSHObjectName
//

BOOL GetSHObjectNameOwned(ITEMIDLIST* pidl, DWORD flags, std::wstring& name)
{
    BOOL ret = FALSE;
    if (pidl != NULL && pidl->mkid.cb != 0) // there must be at least one ID in the list, otherwise nothing to determine
    {
        // find the last ID in the list
        ITEMIDLIST* lastID = pidl;
        while (1)
        {
            ITEMIDLIST* nextID = (ITEMIDLIST*)((BYTE*)lastID + lastID->mkid.cb);
            if (nextID->mkid.cb != 0)
                lastID = nextID;
            else
                break;
        }

        // temporarily shorten ID list and get IShellFolder where original 'pidl' resides
        USHORT lastCB = lastID->mkid.cb;
        lastID->mkid.cb = 0;

        // get Desktop folder
        IShellFolder* desktopFolder;
        if (SHGetDesktopFolder(&desktopFolder) == NOERROR && desktopFolder != NULL)
        {
            IShellFolder* folder;
            if (pidl->mkid.cb != 0) // non-empty ID list, ask desktop for appropriate folder
            {
                if (desktopFolder->BindToObject(pidl, NULL, IID_IShellFolder, (void**)&folder) != S_OK)
                {
                    folder = NULL;
                    TRACE_E("GetSHObjectName(): unable to get folder for 'pidl' without last ID");
                }
                desktopFolder->Release();
            }
            else // empty ID list = folder is desktop itself
                folder = desktopFolder;

            if (folder != NULL)
            {
                // restore list ('pidl') to original size
                lastID->mkid.cb = lastCB;

                STRRET str;
                if (folder->GetDisplayNameOf(lastID, flags, &str) == S_OK)
                {
                    wchar_t* converted = NULL;
                    const HRESULT conversion = StrRetToStrW(&str, lastID, &converted);
                    if (SUCCEEDED(conversion) && converted != NULL)
                    {
                        try
                        {
                            std::wstring staged(converted);
                            name.swap(staged);
                            ret = TRUE;
                        }
                        catch (const std::bad_alloc&)
                        {
                            ret = FALSE;
                        }
                        catch (const std::length_error&)
                        {
                            ret = FALSE;
                        }
                    }
                    if (converted != NULL)
                        CoTaskMemFree(converted);
                }
                else
                    TRACE_E("GetSHObjectName(): GetDisplayNameOf has failed");

                folder->Release();
            }
        }
        else
            TRACE_E("GetSHObjectName(): unable to get Desktop folder");

        // restore list ('pidl') to original size
        lastID->mkid.cb = lastCB;
    }
    else
        TRACE_E("GetSHObjectName(): unable to get name for empty 'pidl'");
    return ret;
}

BOOL GetShellLinkPathOwned(IShellLinkW* link, DWORD flags, std::wstring& path,
                           WIN32_FIND_DATAW* findData)
{
    path.clear();
    if (link == NULL)
        return FALSE;

    std::vector<wchar_t> buffer(256, L'\0');
    for (;;)
    {
        std::fill(buffer.begin(), buffer.end(), L'\0');
        if (link->GetPath(buffer.data(), static_cast<int>(buffer.size()), findData, flags) != S_OK)
            return FALSE;
        const size_t length = wcsnlen_s(buffer.data(), buffer.size());
        if (length + 1 < buffer.size())
        {
            path.assign(buffer.data(), length);
            return TRUE;
        }
        if (buffer.size() > static_cast<size_t>(INT_MAX) / 2)
            return FALSE;
        buffer.resize(buffer.size() * 2, L'\0');
    }
}
