// Client7z.cpp

#include "StdAfx.h"

#include "Common/IntToString.h"
#include "Common/MyInitGuid.h"
#include "Common/StringConvert.h"

#include "Windows/DLL.h"
#include "Windows/FileDir.h"
#include "Windows/FileFind.h"
#include "Windows/FileName.h"
#include "Windows/NtCheck.h"
#include "Windows/PropVariant.h"
#include "Windows/PropVariantConv.h"

#include "../../Common/FileStreams.h"

#include "../../Archive/IArchive.h"

#include "../../IPassword.h"
#include "../../MyVersion.h"

#include <string>
#include <vector>

#ifdef _WIN32
HINSTANCE g_hInstance = 0;
#endif

// use another CLSIDs, if you want to support other formats (zip, rar, ...).
// {23170F69-40C1-278A-1000-000110070000}
DEFINE_GUID(CLSID_CFormat7z,
  0x23170F69, 0x40C1, 0x278A, 0x10, 0x00, 0x00, 0x01, 0x10, 0x07, 0x00, 0x00);

using namespace NWindows;

#define kDllName L"7za.dll"

typedef UINT32 (WINAPI * CreateObjectFunc)(
    const GUID *clsID,
    const GUID *interfaceID,
    void **outObject);


thread_local std::wstring OutputText;

static void PrintString(const UString &s)
{
  OutputText += s.Ptr();
}

static void PrintString(const AString &s)
{
  OutputText += GetUnicodeString(s).Ptr();
}

static void PrintNewLine()
{
  OutputText += L'\n';
}

static void PrintStringLn(const AString &s)
{
  PrintString(s);
  PrintNewLine();
}

static void PrintError(const char *message, const FString &name)
{
  OutputText += L"Error: ";
  OutputText += GetUnicodeString(AString(message)).Ptr();
  PrintNewLine();
  PrintString(name);
  PrintNewLine();
}

static void PrintError(const AString &s)
{
  PrintNewLine();
  PrintString(s);
  PrintNewLine();
}

static HRESULT IsArchiveItemProp(IInArchive *archive, UInt32 index, PROPID propID, bool &result)
{
  NCOM::CPropVariant prop;
  RINOK(archive->GetProperty(index, propID, &prop));
  if (prop.vt == VT_BOOL)
    result = VARIANT_BOOLToBool(prop.boolVal);
  else if (prop.vt == VT_EMPTY)
    result = false;
  else
    return E_FAIL;
  return S_OK;
}

static HRESULT IsArchiveItemFolder(IInArchive *archive, UInt32 index, bool &result)
{
  return IsArchiveItemProp(archive, index, kpidIsDir, result);
}


static const wchar_t *kEmptyFileAlias = L"[Content]";


//////////////////////////////////////////////////////////////
// Archive Open callback class


class CArchiveOpenCallback:
  public IArchiveOpenCallback,
  public ICryptoGetTextPassword,
  public CMyUnknownImp
{
public:
  MY_UNKNOWN_IMP1(ICryptoGetTextPassword)

  STDMETHOD(SetTotal)(const UInt64 *files, const UInt64 *bytes);
  STDMETHOD(SetCompleted)(const UInt64 *files, const UInt64 *bytes);

  STDMETHOD(CryptoGetTextPassword)(BSTR *password);

  bool PasswordIsDefined;
  UString Password;

  CArchiveOpenCallback() : PasswordIsDefined(false) {}
};

STDMETHODIMP CArchiveOpenCallback::SetTotal(const UInt64 * /* files */, const UInt64 * /* bytes */)
{
  return S_OK;
}

STDMETHODIMP CArchiveOpenCallback::SetCompleted(const UInt64 * /* files */, const UInt64 * /* bytes */)
{
  return S_OK;
}
  
STDMETHODIMP CArchiveOpenCallback::CryptoGetTextPassword(BSTR *password)
{
  if (!PasswordIsDefined)
  {
    // You can ask real password here from user
    // Password = GetPassword(OutStream);
    // PasswordIsDefined = true;
    PrintError("Password is not defined");
    return E_ABORT;
  }
  return StringToBstr(Password, password);
}


//////////////////////////////////////////////////////////////
// Archive Extracting callback class

static const char *kTestingString    =  "Testing     ";
static const char *kExtractingString =  "Extracting  ";
static const char *kSkippingString   =  "Skipping    ";

static const char *kUnsupportedMethod = "Unsupported Method";
static const char *kCRCFailed = "CRC Failed";
static const char *kDataError = "Data Error";
static const char *kUnknownError = "Unknown Error";

class CArchiveExtractCallback:
  public IArchiveExtractCallback,
  public ICryptoGetTextPassword,
  public CMyUnknownImp
{
public:
  MY_UNKNOWN_IMP1(ICryptoGetTextPassword)

  // IProgress
  STDMETHOD(SetTotal)(UInt64 size);
  STDMETHOD(SetCompleted)(const UInt64 *completeValue);

  // IArchiveExtractCallback
  STDMETHOD(GetStream)(UInt32 index, ISequentialOutStream **outStream, Int32 askExtractMode);
  STDMETHOD(PrepareOperation)(Int32 askExtractMode);
  STDMETHOD(SetOperationResult)(Int32 resultEOperationResult);

  // ICryptoGetTextPassword
  STDMETHOD(CryptoGetTextPassword)(BSTR *aPassword);

private:
  CMyComPtr<IInArchive> _archiveHandler;
  FString _directoryPath;  // Output directory
  UString _filePath;       // name inside arcvhive
  FString _diskFilePath;   // full path to file on disk
  bool _extractMode;
  struct CProcessedFileInfo
  {
    FILETIME MTime;
    UInt32 Attrib;
    bool isDir;
    bool AttribDefined;
    bool MTimeDefined;
  } _processedFileInfo;

  COutFileStream *_outFileStreamSpec;
  CMyComPtr<ISequentialOutStream> _outFileStream;

public:
  void Init(IInArchive *archiveHandler, const FString &directoryPath);

  UInt64 NumErrors;
  bool PasswordIsDefined;
  UString Password;

  CArchiveExtractCallback() : PasswordIsDefined(false) {}
};

void CArchiveExtractCallback::Init(IInArchive *archiveHandler, const FString &directoryPath)
{
  NumErrors = 0;
  _archiveHandler = archiveHandler;
  _directoryPath = directoryPath;
  NFile::NName::NormalizeDirPathPrefix(_directoryPath);
}

STDMETHODIMP CArchiveExtractCallback::SetTotal(UInt64 /* size */)
{
  return S_OK;
}

STDMETHODIMP CArchiveExtractCallback::SetCompleted(const UInt64 * /* completeValue */)
{
  return S_OK;
}

STDMETHODIMP CArchiveExtractCallback::GetStream(UInt32 index,
    ISequentialOutStream **outStream, Int32 askExtractMode)
{
  *outStream = 0;
  _outFileStream.Release();

  {
    // Get Name
    NCOM::CPropVariant prop;
    RINOK(_archiveHandler->GetProperty(index, kpidPath, &prop));
    
    UString fullPath;
    if (prop.vt == VT_EMPTY)
      fullPath = kEmptyFileAlias;
    else
    {
      if (prop.vt != VT_BSTR)
        return E_FAIL;
      fullPath = prop.bstrVal;
    }
    _filePath = fullPath;
  }

  if (askExtractMode != NArchive::NExtract::NAskMode::kExtract)
    return S_OK;

  {
    // Get Attrib
    NCOM::CPropVariant prop;
    RINOK(_archiveHandler->GetProperty(index, kpidAttrib, &prop));
    if (prop.vt == VT_EMPTY)
    {
      _processedFileInfo.Attrib = 0;
      _processedFileInfo.AttribDefined = false;
    }
    else
    {
      if (prop.vt != VT_UI4)
        return E_FAIL;
      _processedFileInfo.Attrib = prop.ulVal;
      _processedFileInfo.AttribDefined = true;
    }
  }

  RINOK(IsArchiveItemFolder(_archiveHandler, index, _processedFileInfo.isDir));

  {
    // Get Modified Time
    NCOM::CPropVariant prop;
    RINOK(_archiveHandler->GetProperty(index, kpidMTime, &prop));
    _processedFileInfo.MTimeDefined = false;
    switch(prop.vt)
    {
      case VT_EMPTY:
        // _processedFileInfo.MTime = _utcMTimeDefault;
        break;
      case VT_FILETIME:
        _processedFileInfo.MTime = prop.filetime;
        _processedFileInfo.MTimeDefined = true;
        break;
      default:
        return E_FAIL;
    }

  }
  {
    // Get Size
    NCOM::CPropVariant prop;
    RINOK(_archiveHandler->GetProperty(index, kpidSize, &prop));
    bool newFileSizeDefined = (prop.vt != VT_EMPTY);
    UInt64 newFileSize;
    if (newFileSizeDefined)
      ConvertPropVariantToUInt64(prop, newFileSize);
  }

  
  {
    // Create folders for file
    int slashPos = _filePath.ReverseFind(WCHAR_PATH_SEPARATOR);
    if (slashPos >= 0)
      NFile::NDir::CreateComplexDir(_directoryPath + us2fs(_filePath.Left(slashPos)));
  }

  FString fullProcessedPath = _directoryPath + us2fs(_filePath);
  _diskFilePath = fullProcessedPath;

  if (_processedFileInfo.isDir)
  {
    NFile::NDir::CreateComplexDir(fullProcessedPath);
  }
  else
  {
    NFile::NFind::CFileInfo fi;
    if (fi.Find(fullProcessedPath))
    {
      if (!NFile::NDir::DeleteFileAlways(fullProcessedPath))
      {
        PrintError("Can not delete output file", fullProcessedPath);
        return E_ABORT;
      }
    }
    
    _outFileStreamSpec = new COutFileStream;
    CMyComPtr<ISequentialOutStream> outStreamLoc(_outFileStreamSpec);
    if (!_outFileStreamSpec->Open(fullProcessedPath, CREATE_ALWAYS))
    {
      PrintError("Can not open output file", fullProcessedPath);
      return E_ABORT;
    }
    _outFileStream = outStreamLoc;
    *outStream = outStreamLoc.Detach();
  }
  return S_OK;
}

STDMETHODIMP CArchiveExtractCallback::PrepareOperation(Int32 askExtractMode)
{
  _extractMode = false;
  switch (askExtractMode)
  {
    case NArchive::NExtract::NAskMode::kExtract:  _extractMode = true; break;
  };
  switch (askExtractMode)
  {
    case NArchive::NExtract::NAskMode::kExtract:  PrintString(kExtractingString); break;
    case NArchive::NExtract::NAskMode::kTest:  PrintString(kTestingString); break;
    case NArchive::NExtract::NAskMode::kSkip:  PrintString(kSkippingString); break;
  };
  PrintString(_filePath);
  return S_OK;
}

STDMETHODIMP CArchiveExtractCallback::SetOperationResult(Int32 operationResult)
{
  switch(operationResult)
  {
    case NArchive::NExtract::NOperationResult::kOK:
      break;
    default:
    {
      NumErrors++;
      PrintString("     ");
      switch(operationResult)
      {
        case NArchive::NExtract::NOperationResult::kUnsupportedMethod:
          PrintString(kUnsupportedMethod);
          break;
        case NArchive::NExtract::NOperationResult::kCRCError:
          PrintString(kCRCFailed);
          break;
        case NArchive::NExtract::NOperationResult::kDataError:
          PrintString(kDataError);
          break;
        default:
          PrintString(kUnknownError);
      }
    }
  }

  if (_outFileStream != NULL)
  {
    if (_processedFileInfo.MTimeDefined)
      _outFileStreamSpec->SetMTime(&_processedFileInfo.MTime);
    RINOK(_outFileStreamSpec->Close());
  }
  _outFileStream.Release();
  if (_extractMode && _processedFileInfo.AttribDefined)
    NFile::NDir::SetFileAttrib(_diskFilePath, _processedFileInfo.Attrib);
  PrintNewLine();
  return S_OK;
}


STDMETHODIMP CArchiveExtractCallback::CryptoGetTextPassword(BSTR *password)
{
  if (!PasswordIsDefined)
  {
    // You can ask real password here from user
    // Password = GetPassword(OutStream);
    // PasswordIsDefined = true;
    PrintError("Password is not defined");
    return E_ABORT;
  }
  return StringToBstr(Password, password);
}



//////////////////////////////////////////////////////////////
// Archive Creating callback class

struct CDirItem
{
  UInt64 Size;
  FILETIME CTime;
  FILETIME ATime;
  FILETIME MTime;
  UString Name;
  FString FullPath;
  UInt32 Attrib;

  bool isDir() const { return (Attrib & FILE_ATTRIBUTE_DIRECTORY) != 0 ; }
};

class CArchiveUpdateCallback:
  public IArchiveUpdateCallback2,
  public ICryptoGetTextPassword2,
  public CMyUnknownImp
{
public:
  MY_UNKNOWN_IMP2(IArchiveUpdateCallback2, ICryptoGetTextPassword2)

  // IProgress
  STDMETHOD(SetTotal)(UInt64 size);
  STDMETHOD(SetCompleted)(const UInt64 *completeValue);

  // IUpdateCallback2
  STDMETHOD(EnumProperties)(IEnumSTATPROPSTG **enumerator);
  STDMETHOD(GetUpdateItemInfo)(UInt32 index,
      Int32 *newData, Int32 *newProperties, UInt32 *indexInArchive);
  STDMETHOD(GetProperty)(UInt32 index, PROPID propID, PROPVARIANT *value);
  STDMETHOD(GetStream)(UInt32 index, ISequentialInStream **inStream);
  STDMETHOD(SetOperationResult)(Int32 operationResult);
  STDMETHOD(GetVolumeSize)(UInt32 index, UInt64 *size);
  STDMETHOD(GetVolumeStream)(UInt32 index, ISequentialOutStream **volumeStream);

  STDMETHOD(CryptoGetTextPassword2)(Int32 *passwordIsDefined, BSTR *password);

public:
  CRecordVector<UInt64> VolumesSizes;
  UString VolName;
  UString VolExt;

  FString DirPrefix;
  const CObjectVector<CDirItem> *DirItems;

  bool PasswordIsDefined;
  UString Password;
  bool AskPassword;

  bool m_NeedBeClosed;

  FStringVector FailedFiles;
  CRecordVector<HRESULT> FailedCodes;

  CArchiveUpdateCallback(): PasswordIsDefined(false), AskPassword(false), DirItems(0) {};

  ~CArchiveUpdateCallback() { Finilize(); }
  HRESULT Finilize();

  void Init(const CObjectVector<CDirItem> *dirItems)
  {
    DirItems = dirItems;
    m_NeedBeClosed = false;
    FailedFiles.Clear();
    FailedCodes.Clear();
  }
};

STDMETHODIMP CArchiveUpdateCallback::SetTotal(UInt64 /* size */)
{
  return S_OK;
}

STDMETHODIMP CArchiveUpdateCallback::SetCompleted(const UInt64 * /* completeValue */)
{
  return S_OK;
}


STDMETHODIMP CArchiveUpdateCallback::EnumProperties(IEnumSTATPROPSTG ** /* enumerator */)
{
  return E_NOTIMPL;
}

STDMETHODIMP CArchiveUpdateCallback::GetUpdateItemInfo(UInt32 /* index */,
      Int32 *newData, Int32 *newProperties, UInt32 *indexInArchive)
{
  if (newData != NULL)
    *newData = BoolToInt(true);
  if (newProperties != NULL)
    *newProperties = BoolToInt(true);
  if (indexInArchive != NULL)
    *indexInArchive = (UInt32)-1;
  return S_OK;
}

STDMETHODIMP CArchiveUpdateCallback::GetProperty(UInt32 index, PROPID propID, PROPVARIANT *value)
{
  NCOM::CPropVariant prop;
  
  if (propID == kpidIsAnti)
  {
    prop = false;
    prop.Detach(value);
    return S_OK;
  }

  {
    const CDirItem &dirItem = (*DirItems)[index];
    switch(propID)
    {
      case kpidPath:  prop = dirItem.Name; break;
      case kpidIsDir:  prop = dirItem.isDir(); break;
      case kpidSize:  prop = dirItem.Size; break;
      case kpidAttrib:  prop = dirItem.Attrib; break;
      case kpidCTime:  prop = dirItem.CTime; break;
      case kpidATime:  prop = dirItem.ATime; break;
      case kpidMTime:  prop = dirItem.MTime; break;
    }
  }
  prop.Detach(value);
  return S_OK;
}

HRESULT CArchiveUpdateCallback::Finilize()
{
  if (m_NeedBeClosed)
  {
    PrintNewLine();
    m_NeedBeClosed = false;
  }
  return S_OK;
}

static void GetStream2(const wchar_t *name)
{
  PrintString("Compressing  ");
  if (name[0] == 0)
    name = kEmptyFileAlias;
  PrintString(name);
}

STDMETHODIMP CArchiveUpdateCallback::GetStream(UInt32 index, ISequentialInStream **inStream)
{
  RINOK(Finilize());

  const CDirItem &dirItem = (*DirItems)[index];
  GetStream2(dirItem.Name);
 
  if (dirItem.isDir())
    return S_OK;

  {
    CInFileStream *inStreamSpec = new CInFileStream;
    CMyComPtr<ISequentialInStream> inStreamLoc(inStreamSpec);
    FString path = DirPrefix + dirItem.FullPath;
    if (!inStreamSpec->Open(path))
    {
      DWORD sysError = ::GetLastError();
      FailedCodes.Add(sysError);
      FailedFiles.Add(path);
      // if (systemError == ERROR_SHARING_VIOLATION)
      {
        PrintNewLine();
        PrintError("WARNING: can't open file");
        // PrintString(NError::MyFormatMessageW(systemError));
        return S_FALSE;
      }
      // return sysError;
    }
    *inStream = inStreamLoc.Detach();
  }
  return S_OK;
}

STDMETHODIMP CArchiveUpdateCallback::SetOperationResult(Int32 /* operationResult */)
{
  m_NeedBeClosed = true;
  return S_OK;
}

STDMETHODIMP CArchiveUpdateCallback::GetVolumeSize(UInt32 index, UInt64 *size)
{
  if (VolumesSizes.Size() == 0)
    return S_FALSE;
  if (index >= (UInt32)VolumesSizes.Size())
    index = VolumesSizes.Size() - 1;
  *size = VolumesSizes[index];
  return S_OK;
}

STDMETHODIMP CArchiveUpdateCallback::GetVolumeStream(UInt32 index, ISequentialOutStream **volumeStream)
{
  wchar_t temp[16];
  ConvertUInt32ToString(index + 1, temp);
  UString res = temp;
  while (res.Len() < 2)
    res = UString(L'0') + res;
  UString fileName = VolName;
  fileName += L'.';
  fileName += res;
  fileName += VolExt;
  COutFileStream *streamSpec = new COutFileStream;
  CMyComPtr<ISequentialOutStream> streamLoc(streamSpec);
  if (!streamSpec->Create(us2fs(fileName), false))
    return ::GetLastError();
  *volumeStream = streamLoc.Detach();
  return S_OK;
}

STDMETHODIMP CArchiveUpdateCallback::CryptoGetTextPassword2(Int32 *passwordIsDefined, BSTR *password)
{
  if (!PasswordIsDefined)
  {
    if (AskPassword)
    {
      // You can ask real password here from user
      // Password = GetPassword(OutStream);
      // PasswordIsDefined = true;
      PrintError("Password is not defined");
      return E_ABORT;
    }
  }
  *passwordIsDefined = BoolToInt(PasswordIsDefined);
  return StringToBstr(Password, password);
}


HINSTANCE HModule = NULL;

// see http://msdn.microsoft.com/en-us/library/bb687850.aspx
#define EXPORT comment(linker, "/EXPORT:" __FUNCTION__ "=" __FUNCDNAME__)

class CErrorOutputPublisher
{
  wchar_t **Output;

public:
  explicit CErrorOutputPublisher(wchar_t **output): Output(output)
  {
    *Output = NULL;
    OutputText.clear();
  }

  ~CErrorOutputPublisher()
  {
    if (OutputText.empty())
      return;
    const size_t bytes = (OutputText.size() + 1) * sizeof(wchar_t);
    wchar_t *published = static_cast<wchar_t *>(CoTaskMemAlloc(bytes));
    if (published != NULL)
    {
      memcpy(published, OutputText.c_str(), bytes);
      *Output = published;
    }
  }
};

static bool GetWrapperModulePath(std::wstring &path)
{
  std::vector<wchar_t> buffer(256);
  for (;;)
  {
    SetLastError(ERROR_SUCCESS);
    const DWORD length = GetModuleFileNameW(HModule, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length == 0)
      return false;
    if (length < buffer.size() && GetLastError() != ERROR_INSUFFICIENT_BUFFER)
    {
      path.assign(buffer.data(), length);
      return true;
    }
    if (buffer.size() > MAXDWORD / 2)
      return false;
    buffer.resize(buffer.size() * 2);
  }
}

BOOL WINAPI CompressFilesW(const wchar_t* archiveName7z, const wchar_t* sourceDir, const wchar_t* filter, wchar_t **errorMessage)
{
  #pragma EXPORT

  CErrorOutputPublisher publishError(errorMessage);
  try
  {

  std::wstring dllPath;
  if (!GetWrapperModulePath(dllPath))
  {
    PrintError("Can not locate 7-zip wrapper library");
    return FALSE;
  }
  const size_t separator = dllPath.rfind(L'\\');
  if (separator != std::wstring::npos)
    dllPath.resize(separator + 1);
  else
    dllPath.clear();
  dllPath += kDllName;

  NDLL::CLibrary lib;
  if (!lib.Load((CFSTR)dllPath.c_str()))
  {
    PrintError("Can not load 7-zip library");
    return FALSE;
  }
  CreateObjectFunc createObjectFunc = (CreateObjectFunc)lib.GetProc("CreateObject");
  if (createObjectFunc == 0)
  {
    PrintError("Can not get CreateObject");
    return FALSE;
  }

  FString archiveName = archiveName7z;
  CObjectVector<CDirItem> dirItems;

  WIN32_FIND_DATAW find;
  HANDLE hFind = FindFirstFileW(filter, &find);
  if (hFind != INVALID_HANDLE_VALUE)
  {
    do
    {
      if (find.cFileName[0] != 0 && wcscmp(find.cFileName, L".") != 0 && wcscmp(find.cFileName, L"..") != 0)
      {
        CDirItem di;
        FString name = find.cFileName;
        FString fullPath = sourceDir;
        if (!fullPath.IsEmpty() && fullPath.Back() != L'\\')
          fullPath += L'\\';
        fullPath += find.cFileName;
        
        NFile::NFind::CFileInfo fi;
        if (!fi.Find(fullPath))
        {
          PrintError("Can't find file", name);
          FindClose(hFind);
          return FALSE;
        }

        di.Attrib = fi.Attrib;
        di.Size = fi.Size;
        di.CTime = fi.CTime;
        di.ATime = fi.ATime;
        di.MTime = fi.MTime;
        di.Name = fs2us(name);
        di.FullPath = fullPath;
        dirItems.Add(di);
      }
    } while (FindNextFileW(hFind, &find));
    FindClose(hFind);
  }

  COutFileStream *outFileStreamSpec = new COutFileStream;
  CMyComPtr<IOutStream> outFileStream = outFileStreamSpec;
  if (!outFileStreamSpec->Create(archiveName, false))
  {
    PrintError("Can't create archive file");
    return FALSE;
  }

  CMyComPtr<IOutArchive> outArchive;
  if (createObjectFunc(&CLSID_CFormat7z, &IID_IOutArchive, (void **)&outArchive) != S_OK)
  {
    PrintError("Can not get class object");
    return FALSE;
  }

  CArchiveUpdateCallback *updateCallbackSpec = new CArchiveUpdateCallback;
  CMyComPtr<IArchiveUpdateCallback2> updateCallback(updateCallbackSpec);
  updateCallbackSpec->Init(&dirItems);

  // SOLID MODE + ULTRA MODE
  {
    const wchar_t *names[] =
    {
      L"s",
      L"x"
    };
    const int kNumProps = sizeof(names) / sizeof(names[0]);
    NCOM::CPropVariant values[kNumProps] =
    {
      true,    // solid mode ON
      (UInt32)9 // compression level = 9 - ultra
    };
    CMyComPtr<ISetProperties> setProperties;
    outArchive->QueryInterface(IID_ISetProperties, (void **)&setProperties);
    if (!setProperties)
    {
      PrintError("ISetProperties unsupported");
      return FALSE;
    }
    RINOK(setProperties->SetProperties(names, values, kNumProps));
  }
  
  
  HRESULT result = outArchive->UpdateItems(outFileStream, dirItems.Size(), updateCallback);
  updateCallbackSpec->Finilize();
  if (result != S_OK)
  {
    PrintError("Update Error");
    return FALSE;
  }
  for (unsigned int i = 0; i < updateCallbackSpec->FailedFiles.Size(); i++)
  {
    PrintNewLine();
    PrintError("Error for file", updateCallbackSpec->FailedFiles[i]);
  }
  if (updateCallbackSpec->FailedFiles.Size() != 0)
    return FALSE;

  return TRUE;
  }
  catch (const std::bad_alloc &)
  {
    return FALSE;
  }
  catch (...)
  {
    try { PrintError("Unexpected failure while creating archive"); }
    catch (...) {}
    return FALSE;
  }
}

BOOL APIENTRY DllMain( HMODULE hModule,
                       DWORD  ul_reason_for_call,
                       LPVOID lpReserved
					 )
{
	switch (ul_reason_for_call)
	{
	case DLL_PROCESS_ATTACH:
    HModule = hModule;
	case DLL_THREAD_ATTACH:
	case DLL_THREAD_DETACH:
	case DLL_PROCESS_DETACH:
		break;
	}
	return TRUE;
}
