// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

/*
	Automation Plugin for Open Salamander
	
	Copyright (c) 2009-2023 Milan Kase <manison@manison.cz>
	Copyright (c) 2010-2023 Open Salamander Authors
	
	fileinfo.cpp
	Information about the file object.
*/

#include "precomp.h"
#include "fileinfo.h"
#include "aututils.h"

CFileInfo::CFileInfo()
{
}

CFileInfo::~CFileInfo()
{
}

void CFileInfo::Clear()
{
    m_path.clear();
    m_info.clear();
}

HRESULT CFileInfo::FromVariant(VARIANT* var)
{
    HRESULT hr;

    if (V_VT(var) == VT_BSTR)
    {
        return FromString(V_BSTR(var));
    }

    if (V_VT(var) == VT_DISPATCH || V_VT(var) == VT_UNKNOWN)
    {
        IDispatch* pdisp;
        hr = V_UNKNOWN(var)->QueryInterface(IID_IDispatch, (void**)&pdisp);
        if (SUCCEEDED(hr))
        {
            hr = FromDispatch(pdisp);
            pdisp->Release();
            return hr;
        }
    }

    try
    {
        return FromString(_bstr_t(_variant_t(var)));
    }
    catch (_com_error& e)
    {
        return e.Error();
    }
}

HRESULT CFileInfo::FromString(PCWSTR s)
{
    PCWSTR end;

    Clear();

    if (s == NULL || *s == L'\0')
    {
        SetPath(NULL, 0);
        SetInfo(NULL, 0);
        return S_OK;
    }

    // trim whitespace
    while (*s && iswspace(*s))
    {
        ++s;
    }

    // find end of the path
    end = s;
    while (*end && *end != L'\r' && *end != '\n')
    {
        ++end;
    }

    SetPath(s, (int)(end - s));

    s = end;
    while (*s && iswspace(*s))
    {
        ++s;
    }

    // find end of the info
    end = s;
    while (*end && *end != L'\r' && *end != '\n')
    {
        ++end;
    }

    SetInfo(s, (int)(end - s));

    return S_OK;
}

HRESULT CFileInfo::FromDispatch(IDispatch* pdisp)
{
    HRESULT hr;
    _variant_t var;
    UINT uValidFields = 0;
    CQuadWord size;
    DATE date;

    Clear();

    hr = DispPropGet(pdisp, L"Path", &var);
    if (hr == DISP_E_MEMBERNOTFOUND)
    {
        var.Clear();
        hr = DispPropGet(pdisp, L"Name", &var);
    }

    if (FAILED(hr))
    {
        return DISP_E_TYPEMISMATCH;
    }

    try
    {
        SetPath(_bstr_t(var));
    }
    catch (_com_error& e)
    {
        return e.Error();
    }

    var.Clear();
    hr = DispPropGet(pdisp, L"Size", &var);
    if (SUCCEEDED(hr))
    {
        LONGLONG val64;

        try
        {
            val64 = var;
            size.SetUI64(val64);
            uValidFields |= VALID_DATA_SIZE;
        }
        catch (_com_error)
        {
        }
    }

    var.Clear();
    hr = DispPropGet(pdisp, L"DateLastModified", &var);
    if (SUCCEEDED(hr))
    {
        try
        {
            var.ChangeType(VT_DATE);
            date = var;
            uValidFields |= VALID_DATA_DATE | VALID_DATA_TIME;
        }
        catch (_com_error)
        {
        }
    }

    SetInfo(size, date, uValidFields);

    return S_OK;
}

void CFileInfo::SetInfo(const CQuadWord& size, DATE date, UINT uValidFields)
{
    _ASSERTE(m_info.empty());

    if (uValidFields & VALID_DATA_SIZE)
        m_info = SPLNumberToStrOwned(SalamanderGeneral, size);

    if (uValidFields & (VALID_DATA_DATE | VALID_DATA_TIME))
    {
        SYSTEMTIME time;

        if (VariantTimeToSystemTime(date, &time))
        {
            const int dateLength = GetDateFormatW(
                LOCALE_USER_DEFAULT, DATE_SHORTDATE, &time, NULL, NULL, 0);
            const int timeLength = GetTimeFormatW(
                LOCALE_USER_DEFAULT, 0, &time, NULL, NULL, 0);
            std::vector<wchar_t> dateText(dateLength > 0 ? dateLength : 1, L'\0');
            std::vector<wchar_t> timeText(timeLength > 0 ? timeLength : 1, L'\0');
            if (dateLength > 0)
                GetDateFormatW(LOCALE_USER_DEFAULT, DATE_SHORTDATE, &time, NULL,
                               dateText.data(), dateLength);
            if (timeLength > 0)
                GetTimeFormatW(LOCALE_USER_DEFAULT, 0, &time, NULL,
                               timeText.data(), timeLength);
            if (!m_info.empty())
                m_info += L", ";
            m_info += dateText.data();
            m_info += L", ";
            m_info += timeText.data();
        }
    }
}

void CFileInfo::SetPath(PCWSTR pszPath, int len)
{
    _ASSERTE(m_path.empty());
    if (pszPath != NULL)
        m_path.assign(pszPath, len >= 0 ? static_cast<size_t>(len) : wcslen(pszPath));
}

void CFileInfo::SetInfo(PCWSTR pszInfo, int len)
{
    _ASSERTE(m_info.empty());
    if (pszInfo != NULL)
        m_info.assign(pszInfo, len >= 0 ? static_cast<size_t>(len) : wcslen(pszInfo));
}
