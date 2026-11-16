#include "site_list.h"

#include <windows.h>

#include <cwchar>

int CompareSiteNames(const std::wstring& a, const std::wstring& b)
{
    if (a.empty() || b.empty())
        return a.empty() == b.empty() ? 0 : (a.empty() ? -1 : 1);
    const int result =
        CompareStringOrdinal(a.data(), static_cast<int>(a.size()), b.data(), static_cast<int>(b.size()), TRUE);
    return result == CSTR_LESS_THAN ? -1 : (result == CSTR_GREATER_THAN ? 1 : 0);
}

bool SiteNamesEqual(const std::wstring& a, const std::wstring& b)
{
    return CompareSiteNames(a, b) == 0;
}

namespace
{
bool IsControl(wchar_t c)
{
    return c < L' ' || c == 0x7F || (c >= 0x80 && c < 0xA0);
}

bool HasControl(const std::wstring& text)
{
    for (wchar_t c : text)
        if (IsControl(c))
            return true;
    return false;
}
} // namespace

SiteError ValidateSiteName(const std::wstring& name)
{
    if (name.empty())
        return SiteError::NameEmpty;
    if (name[0] == L'@' || name.front() == L' ' || name.back() == L' ' || HasControl(name) ||
        name.find_first_of(L"/\\") != std::wstring::npos)
        return SiteError::NameInvalid;
    return SiteError::None;
}

SiteError ValidateSite(const CSite& site)
{
    const SiteError nameError = ValidateSiteName(site.name);
    if (nameError != SiteError::None)
        return nameError;
    if (site.host.empty())
        return SiteError::HostEmpty;
    for (wchar_t c : site.host)
        if (c <= L' ' || IsControl(c) || wcschr(L"/\\@[]", c) != nullptr)
            return SiteError::HostInvalid;
    if (site.port < 1 || site.port > 65535)
        return SiteError::PortRange;
    if (site.user.empty())
        return SiteError::UserEmpty;
    if (HasControl(site.user))
        return SiteError::UserInvalid;
    if (site.timeoutSeconds < 1 || site.timeoutSeconds > MaxTimeoutSeconds)
        return SiteError::TimeoutRange;
    if (site.encoding == CSite::Encoding::CodePage && site.codePage == 0)
        return SiteError::CodePageInvalid;
    if (site.workers < 1 || site.workers > MaxSiteWorkers)
        return SiteError::WorkersRange;
    if (site.setUploadPermissions && site.uploadPermissions > MaxUploadPermissions)
        return SiteError::UploadPermissionsInvalid;
    return SiteError::None;
}

int CSiteList::IndexOf(const std::wstring& name) const
{
    for (size_t i = 0; i < sites_.size(); i++)
        if (SiteNamesEqual(sites_[i].name, name))
            return static_cast<int>(i);
    return -1;
}

const CSite* CSiteList::Find(const std::wstring& name) const
{
    const int index = IndexOf(name);
    return index < 0 ? nullptr : &sites_[index];
}

bool CSiteList::Add(const CSite& site)
{
    if (ValidateSiteName(site.name) != SiteError::None || IndexOf(site.name) >= 0)
        return false;
    sites_.push_back(site);
    return true;
}

bool CSiteList::Remove(const std::wstring& name)
{
    const int index = IndexOf(name);
    if (index < 0)
        return false;
    sites_.erase(sites_.begin() + index);
    return true;
}

bool CSiteList::Rename(const std::wstring& oldName, const std::wstring& newName)
{
    const int index = IndexOf(oldName);
    if (index < 0 || ValidateSiteName(newName) != SiteError::None)
        return false;
    const int other = IndexOf(newName);
    if (other >= 0 && other != index)
        return false;
    sites_[index].name = newName;
    return true;
}

bool CSiteList::Update(const std::wstring& name, const CSite& site)
{
    const int index = IndexOf(name);
    if (index < 0 || ValidateSiteName(site.name) != SiteError::None)
        return false;
    const int other = IndexOf(site.name);
    if (other >= 0 && other != index)
        return false;
    sites_[index] = site;
    return true;
}

std::wstring CSiteList::MakeUniqueName(const std::wstring& base) const
{
    const std::wstring root = base.empty() ? L"Site" : base;
    if (IndexOf(root) < 0)
        return root;
    for (int n = 2;; n++)
    {
        std::wstring candidate = root + L" (" + std::to_wstring(n) + L")";
        if (IndexOf(candidate) < 0)
            return candidate;
    }
}

std::wstring CSiteList::Duplicate(const std::wstring& name)
{
    const CSite* source = Find(name);
    if (source == nullptr)
        return std::wstring();
    CSite copy = *source;
    copy.name = MakeUniqueName(source->name);
    if (!Add(copy))
        return std::wstring();
    return copy.name;
}
