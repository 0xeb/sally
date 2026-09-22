// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

/*
	Automation Plugin for Open Salamander
	
	Copyright (c) 2009-2023 Milan Kase <manison@manison.cz>
	Copyright (c) 2010-2023 Open Salamander Authors
	
	saltypelib.cpp
	Type library encapsulation.
*/

#include "precomp.h"
#include "saltypelib.h"
#include "salamander_h.h"

#define REGISTER_LIB 0

CSalamanderTypeLib SalamanderTypeLib;

CSalamanderTypeLib::CSalamanderTypeLib()
{
    m_hrLoad = E_PENDING;
    m_pTypeLib = NULL;
}

CSalamanderTypeLib::~CSalamanderTypeLib()
{
    if (m_pTypeLib != NULL)
    {
        m_pTypeLib->Release();
    }
}

HRESULT CSalamanderTypeLib::Get(ITypeLib** ppTypeLib)
{
    for (;;)
    {
        if (m_pTypeLib == NULL)
        {
            if (m_hrLoad == E_PENDING)
            {
                Load();
            }
            else
            {
                *ppTypeLib = NULL;
                return m_hrLoad;
            }
        }
        else
        {
            *ppTypeLib = m_pTypeLib;
            m_pTypeLib->AddRef();
            return S_OK;
        }
    }
}

void CSalamanderTypeLib::Load()
{
    std::wstring modulePath;
    extern HINSTANCE g_hInstance;

    _ASSERTE(m_pTypeLib == NULL);
    _ASSERTE(m_hrLoad == E_PENDING);

#if REGISTER_TYPELIB
    m_hrLoad = LoadRegTypeLib(LIBID_SalamanderLib, 1, 0, 0, &m_pTypeLib);
    if (FAILED(m_hrLoad))
    {
#endif

        if (!SPLGetModuleFileNameOwned(g_hInstance, modulePath))
        {
            const DWORD error = GetLastError();
            m_hrLoad = HRESULT_FROM_WIN32(error != ERROR_SUCCESS
                                             ? error
                                             : ERROR_INSUFFICIENT_BUFFER);
            return;
        }
        m_hrLoad = LoadTypeLib(modulePath.c_str(), &m_pTypeLib);
        _ASSERTE(SUCCEEDED(m_hrLoad));

#if REGISTER_TYPELIB
        if (SUCCEEDED(m_hrLoad))
        {
            HRESULT hrRegister;
            hrRegister = RegisterTypeLib(m_pTypeLib, modulePath.c_str(), NULL);
            _ASSERTE(SUCCEEDED(hrRegister));
        }
    }
#endif
}
