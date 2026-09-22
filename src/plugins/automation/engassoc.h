// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

/*
	Automation Plugin for Open Salamander
	
	Copyright (c) 2009-2023 Milan Kase <manison@manison.cz>
	Copyright (c) 2010-2023 Open Salamander Authors
	
	engassoc.h
	Holds information about mapping between script file extensions and
	script engines.
*/

#pragma once

/// Information about script engine association.
struct SCRIPT_ENGINE_ASSOCIATION
{
    /// File name extension.
    wchar_t szExt[8];

    /// Class ID of the script engine.
    CLSID clsidEngine;
};

class CScriptEngineAssociations : private TDirectArray<SCRIPT_ENGINE_ASSOCIATION>
{
protected:
    SCRIPT_ENGINE_ASSOCIATION* FindAssoc(PCWSTR pszExt);

private:
    static HRESULT QueryHardcodedScriptEngineAssociation(PCWSTR pszExt, __out CLSID* clsidEngine);

public:
    CScriptEngineAssociations() : TDirectArray<SCRIPT_ENGINE_ASSOCIATION>(2, 2)
    {
    }

    bool FindEngineByExt(PCWSTR pszExt, __out_opt CLSID* clsidEngine = NULL);
};

extern CScriptEngineAssociations g_oScriptAssociations;
