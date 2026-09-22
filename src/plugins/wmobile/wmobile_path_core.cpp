// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#include "wmobile_path_core.h"

#include <cwchar>

namespace wmobile
{

void AppendDeviceComponent(std::wstring& path, const wchar_t* name)
{
    if (name == nullptr)
        return;
    if (*name == L'\\')
        ++name;
    // The separator goes in whenever the path does not already end with one - including when the
    // path is exactly the root, which is the whole point. pre-unicode added it before it looked at
    // the component, so an empty component leaves a trailing separator; that is preserved here.
    if (!path.empty() && path.back() != L'\\')
        path.push_back(L'\\');
    path.append(name);
}

bool DecodeOperationTarget(const std::wstring& payload, bool hasMask,
                           OperationTarget& target)
{
    target.path.clear();
    target.mask.clear();
    const size_t pathLength = payload.find(L'\0');
    if (pathLength == std::wstring::npos)
    {
        if (hasMask)
            return false;
        target.path = payload;
        return true;
    }
    target.path.assign(payload.data(), pathLength);

    if (hasMask)
    {
        const size_t maskOffset = pathLength + 1;
        if (maskOffset > payload.size())
            return false;
        const size_t maskEnd = payload.find(L'\0', maskOffset);
        target.mask.assign(payload.data() + maskOffset,
                           (maskEnd == std::wstring::npos ? payload.size() : maskEnd) - maskOffset);
    }
    return true;
}

bool ResolveRelativeDeviceTarget(const wchar_t* fsName, const std::wstring& currentPath,
                                 const std::wstring& enteredPath, std::wstring& fullTarget,
                                 size_t& userPartOffset, std::wstring& nextFocus)
{
    fullTarget.clear();
    nextFocus.clear();
    userPartOffset = std::wstring::npos;
    if (fsName == nullptr || *fsName == L'\0' || enteredPath.find(L':') != std::wstring::npos)
        return false;

    const size_t slash = enteredPath.find(L'\\');
    if (slash == std::wstring::npos || slash + 1 == enteredPath.size())
        nextFocus.assign(enteredPath, 0, slash);

    std::wstring devicePath;
    if (!enteredPath.empty() && enteredPath.front() == L'\\')
    {
        devicePath = L"\\";
        devicePath.append(enteredPath, 1, std::wstring::npos);
    }
    else
    {
        devicePath = currentPath;
        AppendDeviceComponent(devicePath, enteredPath.c_str());
    }

    fullTarget.assign(fsName);
    fullTarget.push_back(L':');
    userPartOffset = fullTarget.size();
    fullTarget.append(devicePath);
    return true;
}

bool PathComposer::SetBase(const wchar_t* base)
{
    HasBase_ = false;
    BaseLength_ = 0;
    Buffer_.clear();
    if (base == nullptr)
        return false;

    Buffer_ = base;
    if (!Buffer_.empty() && Buffer_.back() != L'\\')
        Buffer_.push_back(L'\\');
    HasBase_ = true;
    BaseLength_ = Buffer_.size();
    return true;
}

bool PathComposer::SetLeaf(const wchar_t* leaf)
{
    if (leaf == nullptr || !HasBase_)
        return false;
    Buffer_.resize(BaseLength_);
    Buffer_.append(leaf);
    return true;
}

} // namespace wmobile
