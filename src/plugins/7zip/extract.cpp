// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"

#include "7zip.h"
#include "extract.h"
#include "7zip.rh"
#include "7zip.rh2"
#include "lang\lang.rh"
#include "math.h"
#include "dialogs.h"
#include "7zthreads.h"
#include "7zclient.h"

#include "Common/StringConvert.h"
#include "Windows/Defs.h"
#include "Windows/PropVariant.h"
#include "Windows/PropVariantConv.h"
#include "7zip/IPassword.h"

using namespace NWindows;
using namespace NFile;

CExtractCallbackImp::CExtractCallbackImp(HWND _hProgWnd, UString& password,
                                         ItemsToExtractMap& itemsToExtract) : Password(password), ItemsToExtract(itemsToExtract)
{
    //  Silent = false; // preparation for skip and skip all

    InitializeCriticalSection(&CSExtract);

    OutFileStreamSpec = NULL;
    TargetDir = NULL;
    hProgWnd = _hProgWnd;
}

CExtractCallbackImp::~CExtractCallbackImp()
{
    DeleteCriticalSection(&CSExtract);
}

// silent - if TRUE and a DataError occurs during extraction (usually an incorrect password), the file is deleted
// automatically and the user is not asked anything (used when extracting a single file for preview - F3)
BOOL CExtractCallbackImp::Init(IInArchive* archive, const wchar_t* outDir,
                               const FILETIME& utcLastWriteTimeDefault, DWORD attributesDefault, BOOL silentDelete /* = FALSE*/)
{
    NumErrors = 0;

    UTCLastWriteTimeDefault = utcLastWriteTimeDefault;
    AttributesDefault = attributesDefault;
    ArchiveHandler = archive;
    TargetDir = outDir;

    //  ItemsToExtract = itemsToExtract;

    PasswordIsDefined = !Password.IsEmpty();

    TargetFileName.clear();

    // initialize variables for interacting with the user
    SilentDelete = silentDelete;
    if (silentDelete)
    {
        DataErrorMode = Delete;
        DataErrorSilent = TRUE;
        DataErrorDeleteSilent = TRUE;
    }
    else
    {
        DataErrorSilent = FALSE;
        DataErrorMode = Ask;
        DataErrorDeleteSilent = FALSE;
    }

    //
    //  OverwriteMode = Ask;
    //  OverwriteSilent = FALSE;
    OverwriteSilent = 0;
    OverwriteSkip = FALSE;
    OverwriteCancel = FALSE;

    return TRUE;
}

BOOL CExtractCallbackImp::InitTest()
{
    NumErrors = 0;
    TargetFileName.clear();

    OverwriteCancel = FALSE;
    PasswordIsDefined = !Password.IsEmpty();

    return TRUE;
}

void CExtractCallbackImp::Cleanup()
{
    if (!WholeFile && OutFileStreamSpec != NULL)
    {
        //    TRACE_I("Cleanup File: " << GetAnsiString(ProcessedFileInfo.FileName));

        OutFileStream.Release();

        BOOL silent = FALSE;
        SafeDeleteFile(ProcessedFileInfo.FileName, silent);
    }
}

STDMETHODIMP CExtractCallbackImp::SetTotal(UINT64 size)
{
    //  TRACE_I("CExtractCallbackImp::SetTotal: size=" << (DWORD)size);

    Total.Value = size;
    SendMessage(hProgWnd, WM_7ZIP, WM_7ZIP_SETTOTAL, (LPARAM)&Total);

    return S_OK;
}

STDMETHODIMP CExtractCallbackImp::SetCompleted(const UINT64* completeValue)
{
    //  TRACE_I("CExtractCallbackImp::SetCompleted: completeValue=" << (DWORD)(*completeValue));

    if (completeValue != NULL)
    {
        Completed.Value = *completeValue;

        return (HRESULT)SendMessage(hProgWnd, WM_7ZIP, WM_7ZIP_PROGRESS, (LPARAM)&Completed);
    }

    return S_OK;
}

STDMETHODIMP CExtractCallbackImp::GetStream(UINT32 index, ISequentialOutStream** outStream, INT32 askExtractMode)
{
    HRESULT ret = S_OK;

    // we already pressed cancel and yet the callback was invoked again (caused by the 7za.dll implementation)
    if (OverwriteCancel)
        return E_ABORT;

    EnterCriticalSection(&CSExtract);

    try
    {
        /*    char u[1024];
    sprintf(u, "CExtractCallbackImp::GetStream: index: %d, askExtractMode: %d", index, askExtractMode);
    TRACE_I(u);
*/
        //    TRACE_I("CExtractCallbackImp::GetStream: index=" << index << ", askExtractMode=" << askExtractMode);

        *outStream = NULL;
        OutFileStream.Release();

        // if we are extracting a file
        if (askExtractMode == NArchive::NExtract::NAskMode::kExtract)
        {

            const CArchiveItemInfo* aii = ItemsToExtract[index];
            if (!aii)
            {
                // Already extracted??? Not to be extracted?
                throw S_OK;
            }
            ItemsToExtract.erase(index);
            const CFileData* fd = aii->FileData;
            // fd is certainly not NULL

            // because we handed over a list of CArchiveItem with all information we need, we can extract directly.
            // however, we need reverse mapping (a hash function) that tells us the index of the item to use
            // because this function receives the index in the archive
            //
            // we could save memory and retrieve the properties we need via ArchiveHandler->GetProperty,
            // but the problem is with names: GetProperty returns path+filename relative to the archive root and if we extract
            // from a different root, we would have to strip the path

            ProcessedFileInfo.Attributes = fd->Attr;
            ProcessedFileInfo.AttributesAreDefined = true;

            ProcessedFileInfo.IsDirectory = aii->IsDir;
            ProcessedFileInfo.LastWrite = fd->LastWrite;
            ProcessedFileInfo.Size = fd->Size.Value;
            ProcessedFileInfo.Name = aii->NameInArchive;

            // TODO: check for free space

            TargetFileName = TargetDir;
            SPLSalPathAppendOwned(TargetFileName, aii->NameInArchive);
            ProcessedFileInfo.FileName = TargetFileName.c_str();

                // Show file name in the progress dialog
                SendMessage(hProgWnd, WM_7ZIP, WM_7ZIP_ADDTEXT, (LPARAM)GetName());

                if (ProcessedFileInfo.IsDirectory)
                {
                    // create the directory if it does not already exist
                    SalamanderGeneral->CheckAndCreateDirectory(TargetFileName.c_str());
                    throw S_OK;
                }
                else
                {
                    // WARNING! This performs the overwrite test
                    CCreateFileParams cfp;
                    FILETIME ftLW(GetLastWrite());
                    const std::wstring fileInfo = GetInfoW(&ftLW, GetSize());
                    cfp.FileInfo = fileInfo.c_str();
                    cfp.FileName = GetFileName();
                    cfp.Name = GetName();
                    cfp.pSilent = GetOverwriteSilent();
                    cfp.pSkip = GetOverwriteSkip();

                    if (SendMessage(hProgWnd, WM_7ZIP, WM_7ZIP_CREATEFILE, (LPARAM)&cfp))
                    {
                        if (!*GetOverwriteSkip())
                            SetOverwriteCancel();
                    }

                    *outStream = NULL;
                    if (OverwriteSkip)
                        throw S_OK;
                    // the overall operation cannot continue (cancel)
                    if (OverwriteCancel)
                        throw E_ABORT;

                    // open the stream for extraction
                    // at this point there is an empty file with FILE_ATTRIBUTES_NORMAL
                    // it is the result of the overwrite test - see above

                    // if the path we are extracting to does not exist -> create it
                    const size_t lastComp = TargetFileName.find_last_of(L'\\');
                    if (lastComp != std::wstring::npos)
                    {
                        const std::wstring targetDirectory = TargetFileName.substr(0, lastComp);
                        SalamanderGeneral->CheckAndCreateDirectory(targetDirectory.c_str());
                    } // if

                    OutFileStreamSpec = new CRetryableOutFileStream(hProgWnd);
                    CMyComPtr<ISequentialOutStream> outStreamLoc(OutFileStreamSpec);
                    if (!OutFileStreamSpec->Open(ProcessedFileInfo.FileName, OPEN_ALWAYS))
                    {
                        SysError(IDS_ERROR, ::GetLastError());
                        NumErrors++;
                        throw S_OK;
                    }
                    OutFileStream = outStreamLoc;
                    *outStream = outStreamLoc.Detach();
                }
        }
        else
        {
            *outStream = NULL;
        }
    }
    catch (HRESULT e)
    {
        ret = e;
    }

    LeaveCriticalSection(&CSExtract);

    return ret;
}

STDMETHODIMP CExtractCallbackImp::PrepareOperation(INT32 askExtractMode)
{
    /*  char u[1024];
  sprintf(u, "CExtractCallbackImp::PrepareOperation: askExtractMode: %d", askExtractMode);
  TRACE_I(u);
*/
    WholeFile = FALSE;
    ExtractMode = false;
    switch (askExtractMode)
    {
    case NArchive::NExtract::NAskMode::kExtract:
        ExtractMode = true;
        //      TRACE_I("CExtractCallbackImp::PrepareOperation: Extract!");
        break;

    case NArchive::NExtract::NAskMode::kTest:
        //      TRACE_I("CExtractCallbackImp::PrepareOperation: Test!");
        break;

    case NArchive::NExtract::NAskMode::kSkip:
        //      TRACE_I("CExtractCallbackImp::PrepareOperation: Skip!");
        break;
    }

    return S_OK;
}

//
// Let the user choose whether to keep or delete the damaged file
//
// TRUE if we continue
// FALSE if canceled
BOOL CExtractCallbackImp::OnDataError()
{
    EOperationMode mode = DataErrorMode;

    if (DataErrorMode == Ask)
    {
        /* used by the export_mnu.py script that generates salmenu.mnu for Translator
           we let the message box buttons resolve hotkey collisions by simulating a menu
MENU_TEMPLATE_ITEM MsgBoxButtons[] =
{
  {MNTT_PB, 0
  {MNTT_IT, IDS_BTN_DELETE
  {MNTT_IT, IDS_BTN_KEEP
  {MNTT_PE, 0
};
*/
        const std::wstring btnBuffer = SPLFormatStringOwned(
            L"%d\t%s\t%d\t%s", DIALOG_YES, LangStr(IDS_BTN_DELETE).c_str(),
            DIALOG_NO, LangStr(IDS_BTN_KEEP).c_str());
        //buffer: "1\t&Start\t2\tE&xit"

        // ProcessedFileInfo.Name is a UString - wide at the source. It was
        // being flattened through CP_ACP by GetAnsiString for no reason other than the
        // struct's narrowness, which meant this dialog could not name the very file it was
        // reporting an error about. Wide end to end now.
        const std::wstring msg = SPLFormatStringOwned(
            LangStr(PasswordIsDefined ? IDS_ERROR_PROCESSING_FILE_PWD : IDS_ERROR_PROCESSING_FILE).c_str(),
            (const wchar_t*)ProcessedFileInfo.Name);

        MSGBOXEX_PARAMS mbep;
        const std::wstring caption = LangStr(IDS_PLUGINNAME);
        ZeroMemory(&mbep, sizeof(mbep));
        mbep.HParent = hProgWnd;
        mbep.Caption = caption.c_str();
        mbep.Text = msg.c_str();
        mbep.Flags = MSGBOXEX_ICONEXCLAMATION | MSGBOXEX_YESNOCANCEL;
        mbep.AliasBtnNames = btnBuffer.c_str();

        int mbRet = (int)SendMessage(hProgWnd, WM_7ZIP, WM_7ZIP_SHOWMBOXEX, (LPARAM)&mbep);

        switch (mbRet)
        {
        case DIALOG_YES:
            mode = Delete;
            break;
        case DIALOG_NO:
            mode = Keep;
            break;
            /*
      case DIALOG_YES: mode = Delete; break;
      case DIALOG_ALL: mode = Delete; DataErrorSilent = TRUE; break;
      case DIALOG_SKIP: mode = Keep; break;
      case DIALOG_SKIPALL: mode = Keep; DataErrorSilent = TRUE; break;
*/
        case DIALOG_CANCEL:
            mode = Cancel;
            break;
        }
    }

    // release the OutStream so it can be deleted if needed
    if (OutFileStream != NULL)
        OutFileStreamSpec->SetMTime(&ProcessedFileInfo.LastWrite);
    OutFileStream.Release();

    switch (mode)
    {
    case Cancel:
        SafeDeleteFile(ProcessedFileInfo.FileName, DataErrorDeleteSilent);
        return FALSE;

    case Delete:
        // if cancel, bail out
        if (!SafeDeleteFile(ProcessedFileInfo.FileName, DataErrorDeleteSilent))
            return FALSE;
        break;

    case Keep:
        // keep the file, so set its attributes
        if (ExtractMode && ProcessedFileInfo.AttributesAreDefined)
            SetFileAttributesW(ProcessedFileInfo.FileName, ProcessedFileInfo.Attributes);
        break;
    }

    // we will not ask again, so set the mode to the chosen operation
    if (DataErrorSilent)
        DataErrorMode = mode;

    return TRUE;
}

LRESULT CExtractCallbackImp::Error(int resID, ...)
{
    MSGBOXEX_PARAMS mbep;
    const std::wstring caption = LangStr(IDS_PLUGINNAME);
    va_list arglist;

    va_start(arglist, resID);
    const std::wstring msg = SPLFormatStringOwnedV(LangStr(resID).c_str(), arglist);
    va_end(arglist);

    ZeroMemory(&mbep, sizeof(mbep));
    mbep.HParent = hProgWnd;
    mbep.Caption = caption.c_str();
    mbep.Text = msg.c_str();
    mbep.Flags = MSGBOXEX_ICONHAND | MSGBOXEX_OK;

    return SendMessage(hProgWnd, WM_7ZIP, WM_7ZIP_SHOWMBOXEX, (LPARAM)&mbep);
}

STDMETHODIMP CExtractCallbackImp::SetOperationResult(INT32 resultEOperationResult)
{
    //  TRACE_I("CExtractCallbackImp::SetOperationResult: result = " << resultEOperationResult);

    WholeFile = TRUE;

    switch (resultEOperationResult)
    {
    case NArchive::NExtract::NOperationResult::kOK:
        break;

    default:
        NumErrors++;

        switch (resultEOperationResult)
        {
        case NArchive::NExtract::NOperationResult::kUnsupportedMethod:
            Error(IDS_UNSUPPORTED_METHOD);
            break;

        case NArchive::NExtract::NOperationResult::kCRCError:
            Error(IDS_CRC_FAILED);
            break;

        case NArchive::NExtract::NOperationResult::kDataError:
            //          // use this code if a patch for the keep/delete functionality cannot be applied
            //          if (ExtractMode)
            //            Error(IDS_DATA_ERROR, GetAnsiString(ProcessedFileInfo.FileName));

            // this code works only with the keep/delete functionality patch
            if (ExtractMode)
            {
                // give the user the option to keep/delete the damaged file
                if (!OnDataError())
                    return E_ABORT;
                // an error occurred during extraction; delete the file (handled by the function above) and finish
                if (SilentDelete)
                {
                    // Error() is BOOL Error(int resID, BOOL quiet = FALSE, ...),
                    // so the name was landing in 'quiet' - a non-null pointer made
                    // it TRUE and the message box was never shown at all, while
                    // the %s in the resource had no argument behind it. Pass the
                    // wide name as a real vararg, with quiet spelled out, matching
                    // the wide formatting already used for this same Name above.
                    Error(PasswordIsDefined ? IDS_DATA_ERROR_PWD : IDS_DATA_ERROR, FALSE,
                          (const wchar_t*)ProcessedFileInfo.Name);
                    if (PasswordIsDefined)
                        Password = L""; // Allow entering another password
                    return E_ABORT;
                }
                return S_OK;
            }

            break;

        default:
            Error(IDS_UNKNOWN_ERROR);
            break;
        }

        break;
    }

    if (OutFileStream != NULL)
        OutFileStreamSpec->SetMTime(&ProcessedFileInfo.LastWrite);
    OutFileStream.Release();

    if (ExtractMode && ProcessedFileInfo.AttributesAreDefined)
    {
        SetFileAttributesW(ProcessedFileInfo.FileName, ProcessedFileInfo.Attributes);
    }

    if (TargetDir && !ItemsToExtract.size())
    {
        // NULL TargetDir means testing the archive
        // Doing partial extract & everything has been extracted. Looks like a solid archive
        return E_STOPEXTRACTION;
    }

    return S_OK;
}

STDMETHODIMP CExtractCallbackImp::CryptoGetTextPassword(BSTR* password)
{
    if (!PasswordIsDefined /*&& !Silent*/) // Silent is for skip, which is not implemented in 7za.dll
    {
        char pwd[PASSWORD_LEN];

        pwd[0] = 0;
        switch (SendMessage(hProgWnd, WM_7ZIP, WM_7ZIP_PASSWORD, (LPARAM)pwd))
        {
        case IDOK:
            PasswordIsDefined = true;
            Password = GetUnicodeString(pwd);
            break;

        case IDCANCEL:
            return E_ABORT;
            /*
      // prepared for skip and skip all (hopefully it will be completed in the next version)
      case DIALOG_SKIP:
        PasswordIsDefined = false;
        Silent = false;
        return S_OK;

      case DIALOG_SKIPALL:
        PasswordIsDefined = false;
        Silent = true;
        return S_FALSE;
*/
        }
    }
    StringToBstr(Password, password);

    return S_OK;
}
