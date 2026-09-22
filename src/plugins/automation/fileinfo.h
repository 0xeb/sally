// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

/*
	Automation Plugin for Open Salamander
	
	Copyright (c) 2009-2023 Milan Kase <manison@manison.cz>
	Copyright (c) 2010-2023 Open Salamander Authors
	
	fileinfo.h
	Information about the file object.
*/

#pragma once

/// Holds information about the file object.
class CFileInfo
{
private:
    std::wstring m_path;
    std::wstring m_info;

    /// Clears the structure and releases resources.
    void Clear();

    HRESULT FromString(PCWSTR s);
    HRESULT FromDispatch(IDispatch* pdisp);

    void SetPath(PCWSTR pszPath, int len = -1);
    void SetInfo(PCWSTR pszInfo, int len = -1);
    void SetInfo(const CQuadWord& size, DATE date, UINT uValidFields);

public:
    /// Constructor.
    CFileInfo();

    /// Destructor.
    ~CFileInfo();

    /// Retrieves information about a file from the variant.
    HRESULT FromVariant(VARIANT* var);

    PCWSTR Path() const
    {
        return m_path.c_str();
    }

    PCWSTR Info() const
    {
        return m_info.c_str();
    }
};
