// The user's saved sites: an ordered list with unique (case-insensitive) names, plus validation of the fields the
// site dialog edits. Pure model: no Sally, no registry, no UI.
#pragma once

#include <string>
#include <vector>

#include "site.h"

// Upper bound of the connect/idle timeout of a site (one hour); the dialog, ValidateSite and the registry loader use it.
const int MaxTimeoutSeconds = 3600;

// Upper bound of the parallel transfer connections of a site.
const int MaxSiteWorkers = 8;

// Largest value of the upload permission bits (octal 07777: set-id and sticky bits included).
const unsigned MaxUploadPermissions = 07777;

enum class SiteError
{
    None,
    NameEmpty,
    NameInvalid,     // '/', '\', control characters, leading '@', leading/trailing blanks
    HostEmpty,
    HostInvalid,     // blanks, control characters, or one of / \ @ [ ]
    PortRange,       // not 1..65535
    UserEmpty,       // the user is required (every SSH login needs one)
    UserInvalid,     // control characters
    TimeoutRange,    // not 1..MaxTimeoutSeconds
    CodePageInvalid, // encoding is CodePage and the code page is 0
    WorkersRange,    // not 1..MaxSiteWorkers
    UploadPermissionsInvalid // upload permissions are on and the mode is above MaxUploadPermissions
};

// A site name must always be usable as "sftp:@Name/path" (see site_path.h: the part after '@' ends at the first '/').
SiteError ValidateSiteName(const std::wstring& name);

// Name (ValidateSiteName) and every other field. Does not look at other sites (uniqueness is the list's job).
SiteError ValidateSite(const CSite& site);

// Case-insensitive (ordinal) comparison of two site names: <0, 0, >0. This is the ONE definition of "same name";
// everything that keys data by site name (the secrets map, the list) must use it.
int CompareSiteNames(const std::wstring& a, const std::wstring& b);

// Case-insensitive (ordinal) equality of two site names.
bool SiteNamesEqual(const std::wstring& a, const std::wstring& b);

// Ordering for std::map keyed by site name: two names are the same key exactly when SiteNamesEqual says so.
struct SiteNameLess
{
    bool operator()(const std::wstring& a, const std::wstring& b) const { return CompareSiteNames(a, b) < 0; }
};

class CSiteList
{
public:
    size_t Count() const { return sites_.size(); }
    const std::vector<CSite>& Sites() const { return sites_; }
    const CSite& At(size_t index) const { return sites_[index]; }

    // Index of the site with this name (case-insensitive), or -1.
    int IndexOf(const std::wstring& name) const;
    // Pointer into the list (valid until the list changes), or nullptr.
    const CSite* Find(const std::wstring& name) const;

    // Appends. False (list unchanged) when the name is invalid or already taken. Other fields are not checked
    // (a loaded list must tolerate what is stored); the dialog calls ValidateSite.
    bool Add(const CSite& site);
    bool Remove(const std::wstring& name);
    // False when oldName is unknown, newName is invalid, or newName belongs to ANOTHER site (changing only the
    // letter case of the same site is fine).
    bool Rename(const std::wstring& oldName, const std::wstring& newName);
    // Replaces the site called 'name' with 'site' (which may carry a different name: then it is a rename with the
    // same duplicate check). False when unknown or the new name is invalid/taken; the list is unchanged then.
    bool Update(const std::wstring& name, const CSite& site);

    // base, or "base (2)", "base (3)", ... whichever is the first free name. An empty base gives "Site".
    std::wstring MakeUniqueName(const std::wstring& base) const;
    // Appends a copy of 'name' under a unique name ("Name (2)") and returns that name; empty when unknown.
    std::wstring Duplicate(const std::wstring& name);

    void Clear() { sites_.clear(); }

private:
    std::vector<CSite> sites_;
};
