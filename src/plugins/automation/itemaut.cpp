// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

/*
	Automation Plugin for Open Salamander
	
	Copyright (c) 2009-2023 Milan Kase <manison@manison.cz>
	Copyright (c) 2010-2023 Open Salamander Authors
	
	itemaut.cpp
	Panel item automation object.
*/

#include "precomp.h"
#include "salamander_h.h"
#include "itemaut.h"
#include "aututils.h"

extern CSalamanderGeneralAbstract* SalamanderGeneral;

CSalamanderPanelItemAutomation::CSalamanderPanelItemAutomation()
{
    _ctor();
}

CSalamanderPanelItemAutomation::CSalamanderPanelItemAutomation(const CFileData* pData, PCWSTR pszPath)
{
    _ctor();
    Set(pData, pszPath);
}

CSalamanderPanelItemAutomation::CSalamanderPanelItemAutomation(const CFileData* pData, int nPanel)
{
    _ctor();
    Set(pData, nPanel);
}

void CSalamanderPanelItemAutomation::_ctor()
{
    m_fullPath.clear();
    m_nameOffset = 0;
    m_size.QuadPart = 0;
    m_dwAttributes = INVALID_FILE_ATTRIBUTES;
    m_dateLastModified = 0;
}

void CSalamanderPanelItemAutomation::Set(const CFileData* pData, PCWSTR pszPath)
{
    FILETIME ftLocal;
    SYSTEMTIME st;

    _ASSERTE(pszPath != NULL && *pszPath != 0);

    m_fullPath = pszPath;
    if (!m_fullPath.empty() && m_fullPath.back() != L'\\')
        m_fullPath += L'\\';
    m_nameOffset = m_fullPath.size();
    m_fullPath += pData->Name;

    m_dwAttributes = pData->Attr;
    m_size.QuadPart = pData->Size.Value;

    m_dateLastModified = 0;
    if (FileTimeToLocalFileTime(&pData->LastWrite, &ftLocal))
    {
        if (FileTimeToSystemTime(&ftLocal, &st))
        {
            SystemTimeToVariantTime(&st, &m_dateLastModified);
        }
    }
}

void CSalamanderPanelItemAutomation::Set(const CFileData* pData, int nPanel)
{
    std::wstring path;
    if (SPLGetPanelPathOwned(SalamanderGeneral, nPanel, path))
        Set(pData, path.c_str());
}

/* [propget][id] */ HRESULT STDMETHODCALLTYPE CSalamanderPanelItemAutomation::get_Path(
    /* [retval][out] */ BSTR* path)
{
    *path = SysAllocString(m_fullPath.c_str());
    return S_OK;
}

/* [propget][id] */ HRESULT STDMETHODCALLTYPE CSalamanderPanelItemAutomation::get_Name(
    /* [retval][out] */ BSTR* name)
{
    *name = SysAllocString(m_fullPath.c_str() + m_nameOffset);
    return S_OK;
}

/* [propget][id] */ HRESULT STDMETHODCALLTYPE CSalamanderPanelItemAutomation::get_Size(
    /* [retval][out] */ VARIANT* size)
{
    QuadWordToVariant(m_size, size);
    return S_OK;
}

/* [propget][id] */ HRESULT STDMETHODCALLTYPE CSalamanderPanelItemAutomation::get_DateLastModified(
    /* [retval][out] */ DATE* date)
{
    *date = m_dateLastModified;
    return S_OK;
}

/* [propget][id] */ HRESULT STDMETHODCALLTYPE CSalamanderPanelItemAutomation::get_Attributes(
    /* [retval][out] */ int* attrs)
{
    *attrs = m_dwAttributes;
    return S_OK;
}
