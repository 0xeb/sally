// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"

#ifdef _WMA_SUPPORT_

#include "wmaparser.h"
#include "..\output.h"
#include "..\renderer.h"
#include "..\mmviewer.h"
#include "..\mmviewer.rh"
#include "..\mmviewer.rh2"
#include "..\lang\lang.rh"
#include "..\output.h"

extern std::wstring LangStr(int resID);

char* FormatDiagnostic(const char* format, ...);

typedef HRESULT(STDMETHODCALLTYPE* WMCREATEEDITOR)(IWMMetadataEditor** ppEditor);

WMCREATEEDITOR wiWMCreateEditor;

#ifndef SAFE_RELEASE

#define SAFE_RELEASE(x) \
    if (NULL != x) \
    { \
        x->Release(); \
        x = NULL; \
    }

#endif // SAFE_RELEASE

typedef struct
{
    LPCWSTR tag;
    int str_id;
    std::wstring str;
} WMA_TAG;

HRESULT CParserWMA::GetHeaderAttribute(IWMHeaderInfo* pHdrInfo, LPCWSTR pwszName,
                                       BOOL* pbIsPresent, std::wstring& output)
{
    HRESULT hr = S_OK;

    *pbIsPresent = FALSE;

    WORD nstreamNum = 0;
    WORD cbLength = 0;
    output.clear();

    WMT_ATTR_DATATYPE type;

    hr = pHdrInfo->GetAttributeByName(&nstreamNum, pwszName, &type, NULL, &cbLength);

    if (FAILED(hr) && hr != ASF_E_NOTFOUND)
    {
        TRACE_E(FormatDiagnostic("GetAttributeByName failed for Attribute name %ws (hr=0x%08x).\n", pwszName, hr));
        return hr;
    }

    if (cbLength == 0 && hr == ASF_E_NOTFOUND)
    {
        hr = S_OK;
        return hr;
    }

    std::vector<BYTE> value;
    try
    {
        value.resize(cbLength);
    }
    catch (...)
    {
        hr = E_OUTOFMEMORY;
        TRACE_E(FormatDiagnostic("Internal Error (hr=0x%08x).\n", hr));
        return hr;
    }

    hr = pHdrInfo->GetAttributeByName(&nstreamNum, pwszName, &type, value.data(), &cbLength);
    if (FAILED(hr))
    {
        TRACE_E(FormatDiagnostic("GetAttributeByName failed for Attribute name %ws (hr=0x%08x).\n", pwszName, hr));
        return hr;
    }

    try
    {
        switch (type)
        {
    case WMT_TYPE_DWORD:
        if (cbLength < sizeof(DWORD))
            return E_UNEXPECTED;
        output = std::to_wstring(*reinterpret_cast<const DWORD*>(value.data()));
        break;

    case WMT_TYPE_STRING:
        if ((cbLength % sizeof(wchar_t)) != 0)
            return E_UNEXPECTED;
        output.resize(cbLength / sizeof(wchar_t));
        memcpy(output.data(), value.data(), cbLength);
        while (!output.empty() && output.back() == L'\0')
            output.pop_back();
        break;

    case WMT_TYPE_BINARY:
        output = FStrW(LangStr(IDS_WMA_BINARY).c_str(), cbLength);
        break;

    case WMT_TYPE_BOOL:
        if (cbLength < sizeof(BOOL))
            return E_UNEXPECTED;
        output = LangStr(*reinterpret_cast<const BOOL*>(value.data()) ? IDS_YES : IDS_NO);
        break;

    case WMT_TYPE_WORD:
        if (cbLength < sizeof(WORD))
            return E_UNEXPECTED;
        output = std::to_wstring(*reinterpret_cast<const WORD*>(value.data()));
        break;

    case WMT_TYPE_QWORD:
        if (cbLength < sizeof(QWORD))
            return E_UNEXPECTED;
        output = std::to_wstring(*reinterpret_cast<const QWORD*>(value.data()));
        break;

    case WMT_TYPE_GUID:
    {
        if (cbLength < sizeof(GUID))
            return E_UNEXPECTED;
        wchar_t guidText[39]; // StringFromGUID2's documented GUID text contract.
        if (StringFromGUID2(*reinterpret_cast<const GUID*>(value.data()), guidText,
                            _countof(guidText)) == 0)
            hr = E_FAIL;
        else
            output = guidText;
        break;
    }

        default:
            hr = E_INVALIDARG;
            break;
        }
    }
    catch (...)
    {
        return E_OUTOFMEMORY;
    }

    *pbIsPresent = SUCCEEDED(hr);

    return hr;
}

CParserWMA::CParserWMA() : pEditor(NULL), pHeaderInfo(NULL), module(NULL)
{
    module = LoadLibraryW(L"wmvcore.dll");

    if (module)
    {
        if ((wiWMCreateEditor = (WMCREATEEDITOR)GetProcAddress(module, "WMCreateEditor")) != NULL) // Windows Media
        {
            //OK
        }
        else
        {
            // error
            FreeLibrary(module);
        }
    }
}

CParserResultEnum
CParserWMA::OpenFile(const wchar_t* fileName)
{
    if (!module)
        return preExtensionError;

    CloseFile();

    HRESULT hr = S_OK;
    hr = wiWMCreateEditor(&pEditor);
    if (FAILED(hr))
    {
        TRACE_E(FormatDiagnostic("Could not create Metadata Editor (hr=0x%08x).\n", hr));
        return preExtensionError;
    }

    hr = pEditor->Open(fileName);
    if (FAILED(hr))
    {
        TRACE_E(FormatDiagnostic("Could not open outfile %ws (hr=0x%08x).\n", fileName, hr));
        return preOpenError;
    }

    hr = pEditor->QueryInterface(IID_IWMHeaderInfo, (void**)&pHeaderInfo);
    if (FAILED(hr))
    {
        TRACE_E(FormatDiagnostic("Could not QI for IWMHeaderInfo (hr=0x%08x).\n", hr));
        return preExtensionError;
    }

    return preOK;
}

CParserResultEnum
CParserWMA::CloseFile()
{
    SAFE_RELEASE(pHeaderInfo);

    if (pEditor)
        pEditor->Close();

    SAFE_RELEASE(pEditor);

    return preOK;
}

CParserResultEnum
CParserWMA::GetFileInfo(COutputInterface* output)
{
    if (pEditor) // at least some validation
    {
        // tags will be printed in this order. reorder them as you like
        WMA_TAG wmatags[] = {
            {g_wszWMBitrate, IDS_WMA_BITRATE},
            {g_wszWMDuration, IDS_WMA_DURATION},
            {g_wszWMTrack, IDS_WMA_TRACK},
            {g_wszWMTitle, IDS_WMA_TITLE},
            {g_wszWMAuthor, IDS_WMA_AUTHOR},
            {g_wszWMAlbumTitle, IDS_WMA_ALBUMTITLE},
            {g_wszWMYear, IDS_WMA_YEAR},
            {g_wszWMGenre, IDS_WMA_GENRE},
            {g_wszWMSignature_Name, IDS_WMA_SIGNATURENAME},
            {g_wszWMDescription, IDS_WMA_DESCRIPTION},
            {g_wszWMCopyright, IDS_WMA_COPYRIGHT},
            {g_wszWMRating, IDS_WMA_RATING},
            {g_wszWMPromotionURL, IDS_WMA_PROMOTIONURL},
            {g_wszWMAlbumCoverURL, IDS_WMA_ALBUMCOVERURL},
            {g_wszWMMCDI, IDS_WMA_MCDI},
            {g_wszWMBannerImageType, IDS_WMA_BANNERIMAGETYPE},
            {g_wszWMBannerImageData, IDS_WMA_BANNERIMAGEDATA},
            {g_wszWMBannerImageURL, IDS_WMA_BANNERIMAGEURL},
            {g_wszWMCopyrightURL, IDS_WMA_COPYRIGHTURL},
            {g_wszWMNSCName, IDS_WMA_NSCNAME},
            {g_wszWMNSCAddress, IDS_WMA_NSCADDRESS},
            {g_wszWMNSCPhone, IDS_WMA_NSCPHONE},
            {g_wszWMNSCEmail, IDS_WMA_NSCEMAIL},
            {g_wszWMNSCDescription, IDS_WMA_NSCDESCRIPTION},
            {g_wszWMSeekable, IDS_WMA_SEEKABLE},
            {g_wszWMStridable, IDS_WMA_STRIDABLE},
            {g_wszWMBroadcast, IDS_WMA_BROADCAST},
            {g_wszWMProtected, IDS_WMA_PROTECTED},
            {g_wszWMTrusted, IDS_WMA_TRUSTED},
        };

        int countTags = sizeof(wmatags) / sizeof(WMA_TAG);

        std::wstring value;
        BOOL isPresent = FALSE;
        HRESULT hr;
        int i;

        for (i = 0; i < countTags; i++)
        {
            hr = GetHeaderAttribute(pHeaderInfo, wmatags[i].tag, &isPresent, value);

            if (FAILED(hr))
                break;

            if (isPresent && !value.empty())
            {
                if (wmatags[i].tag == g_wszWMDuration)
                {
                    int dur = static_cast<int>(_wtoi64(value.c_str()) / 10000000);
                    if (dur / 3600)
                        wmatags[i].str = FStrW(L"%02lu:%02lu:%02lu", dur / 3600,
                                               dur / 60 % 60, dur % 60);
                    else
                        wmatags[i].str = FStrW(L"%02lu:%02lu", dur / 60 % 60, dur % 60);
                }
                else if (wmatags[i].tag == g_wszWMBitrate)
                {
                    wmatags[i].str = std::to_wstring(_wtol(value.c_str()) / 1000);
                }
                else
                {
                    wmatags[i].str = value;
                }
            }
        }

        // dump
        output->AddHeader(LangStr(IDS_WMA_INFO).c_str());
        for (i = 0; i < countTags; i++)
            if (!wmatags[i].str.empty())
                output->AddItem(LangStr(wmatags[i].str_id).c_str(), wmatags[i].str.c_str());

        return preOK;
    }

    return preUnknownFile;
}

#endif
