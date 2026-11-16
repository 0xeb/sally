// What the site dialogs need from the saved sites: the list, editing it together with the remembered secrets, and
// saving. A thin interface so ui/ does not depend on fs/ (fs/site_config.h implements it). Main thread only.
#pragma once

#include <windows.h>

#include <string>

#include "secret.h"
#include "site_list.h"

class ISiteStore
{
public:
    virtual ~ISiteStore() = default;

    virtual const CSiteList& Sites() const = 0;

    // True when the stored sites were written by a newer plugin version: they are not loaded and must not be
    // changed. Every editing method below then returns false (empty string) without changing anything.
    virtual bool IsReadOnly() const = 0;

    virtual bool AddSite(const CSite& site) = 0;
    virtual bool RemoveSite(const std::wstring& name) = 0;
    // May rename; the remembered secrets move with the site.
    virtual bool UpdateSite(const std::wstring& name, const CSite& site) = 0;
    // Copy under a unique name; the new name, or empty.
    virtual std::wstring DuplicateSite(const std::wstring& name) = 0;

    virtual bool HasPassword(const std::wstring& siteName) const = 0;
    virtual bool HasPassphrase(const std::wstring& siteName) const = 0;
    // An empty secret clears the stored one. False: unknown site, master password refused or encryption failed.
    virtual bool SetPassword(const std::wstring& siteName, const CSecret& secret, HWND parent) = 0;
    virtual bool SetPassphrase(const std::wstring& siteName, const CSecret& secret, HWND parent) = 0;
    virtual void ClearSecrets(const std::wstring& siteName) = 0;

    // Writes the configuration to the registry now (instead of when Sally saves its configuration).
    virtual void SaveNow() = 0;
};
