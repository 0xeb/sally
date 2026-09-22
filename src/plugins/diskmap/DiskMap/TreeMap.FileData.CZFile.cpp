// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"
#include "TreeMap.FileData.CZFile.h"
#include "TreeMap.FileData.CZDirectory.h"

size_t CZFile::GetFullName(std::wstring& path)
{
    path.clear();
    if (this->_parent)
        this->_parent->GetFullName(path);
    if (!path.empty() && path.back() != L'\\')
        path.push_back(L'\\');
    path.append(this->_name, this->_namelen);
    return path.size();
}

size_t CZFile::GetRelativeName(CZDirectory* root, std::wstring& path)
{
    path.clear();
    if (this == root)
        return 0;

    if (this->_parent)
        this->_parent->GetRelativeName(root, path);
    if (!path.empty() && path.back() != L'\\')
        path.push_back(L'\\');
    path.append(this->_name, this->_namelen);
    return path.size();
}
