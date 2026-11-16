#include "site_edit.h"

#include <windows.h>

namespace
{
std::wstring Trim(const std::wstring& text)
{
    size_t first = 0;
    size_t last = text.size();
    while (first < last && (text[first] == L' ' || text[first] == L'\t'))
        first++;
    while (last > first && (text[last - 1] == L' ' || text[last - 1] == L'\t'))
        last--;
    return text.substr(first, last - first);
}

// Digits only, at most 9 of them (no overflow). False for empty or anything else.
bool ParseNumber(const std::wstring& text, long& value)
{
    if (text.empty() || text.size() > 9)
        return false;
    long result = 0;
    for (wchar_t c : text)
    {
        if (c < L'0' || c > L'9')
            return false;
        result = result * 10 + (c - L'0');
    }
    value = result;
    return true;
}

// One to four octal digits (0..07777). False for empty or anything else.
bool ParseOctalMode(const std::wstring& text, unsigned& value)
{
    if (text.empty() || text.size() > 4)
        return false;
    unsigned result = 0;
    for (wchar_t c : text)
    {
        if (c < L'0' || c > L'7')
            return false;
        result = result * 8 + static_cast<unsigned>(c - L'0');
    }
    value = result;
    return true;
}

std::wstring FormatOctalMode(unsigned mode)
{
    std::wstring text;
    do
    {
        text.insert(text.begin(), static_cast<wchar_t>(L'0' + (mode & 7)));
        mode >>= 3;
    } while (mode != 0);
    return text;
}
} // namespace

bool ShouldClearSecrets(bool rememberPassword, bool forgetSecrets)
{
    return forgetSecrets || !rememberPassword;
}

CSiteForm FormFromSite(const CSite& site)
{
    CSiteForm form;
    form.name = site.name;
    form.host = site.host;
    form.port = std::to_wstring(site.port);
    form.user = site.user;
    form.rememberPassword = site.rememberPassword;
    form.useAgent = site.useAgent;
    form.keyFile = site.keyFile;
    form.timeout = std::to_wstring(site.timeoutSeconds);
    form.compression = site.compression;
    form.encoding = site.encoding;
    form.codePage = std::to_wstring(site.codePage);
    form.initialRemotePath = site.initialRemotePath;
    form.initialLocalPath = site.initialLocalPath;
    form.workers = std::to_wstring(site.workers);
    form.safeUpload = site.safeUpload;
    form.preserveTime = site.preserveTime;
    form.setUploadPermissions = site.setUploadPermissions;
    form.uploadPermissions = FormatOctalMode(site.uploadPermissions);
    return form;
}

SiteError SiteFromForm(const CSiteForm& form, CSite& out)
{
    CSite site;
    site.name = Trim(form.name);
    site.host = Trim(form.host);
    site.user = Trim(form.user);
    site.rememberPassword = form.rememberPassword;
    site.useAgent = form.useAgent;
    site.keyFile = Trim(form.keyFile);
    site.compression = form.compression;
    site.encoding = form.encoding;
    site.initialRemotePath = Trim(form.initialRemotePath);
    site.initialLocalPath = Trim(form.initialLocalPath);

    // the number fields are converted here because the text may not be a number at all; 0 is reported by
    // ValidateSite as the range error of that field
    long number = 0;
    if (ParseNumber(Trim(form.port), number) && number >= 1 && number <= 65535)
        site.port = static_cast<int>(number);
    else
        site.port = 0;

    if (ParseNumber(Trim(form.timeout), number) && number >= 1 && number <= MaxTimeoutSeconds)
        site.timeoutSeconds = static_cast<int>(number);
    else
        site.timeoutSeconds = 0;

    // a code page is only needed (and only checked) when it is the chosen encoding
    if (ParseNumber(Trim(form.codePage), number) && number > 0 && number < 65536 &&
        (form.encoding != CSite::Encoding::CodePage || IsValidCodePage(static_cast<UINT>(number))))
        site.codePage = static_cast<unsigned>(number);
    else if (form.encoding == CSite::Encoding::CodePage)
        site.codePage = 0;

    if (ParseNumber(Trim(form.workers), number) && number >= 1 && number <= MaxSiteWorkers)
        site.workers = static_cast<int>(number);
    else
        site.workers = 0;

    site.safeUpload = form.safeUpload;
    site.preserveTime = form.preserveTime;
    site.setUploadPermissions = form.setUploadPermissions;
    // like the code page: the mode is only checked when it is switched on; otherwise the default stays
    unsigned mode = 0;
    if (ParseOctalMode(Trim(form.uploadPermissions), mode))
        site.uploadPermissions = mode;
    else if (form.setUploadPermissions)
        site.uploadPermissions = MaxUploadPermissions + 1; // reported by ValidateSite

    const SiteError error = ValidateSite(site);
    if (error == SiteError::None)
        out = std::move(site);
    return error;
}

SiteField FieldOfError(SiteError error)
{
    switch (error)
    {
    case SiteError::None:
    case SiteError::NameEmpty:
    case SiteError::NameInvalid:
        return SiteField::Name;
    case SiteError::HostEmpty:
    case SiteError::HostInvalid:
        return SiteField::Host;
    case SiteError::PortRange:
        return SiteField::Port;
    case SiteError::UserEmpty:
    case SiteError::UserInvalid:
        return SiteField::User;
    case SiteError::TimeoutRange:
        return SiteField::Timeout;
    case SiteError::CodePageInvalid:
        return SiteField::CodePage;
    case SiteError::WorkersRange:
        return SiteField::Workers;
    case SiteError::UploadPermissionsInvalid:
        return SiteField::UploadPermissions;
    }
    return SiteField::Name;
}

SiteTab TabOfField(SiteField field)
{
    switch (field)
    {
    case SiteField::Timeout:
    case SiteField::CodePage:
    case SiteField::Workers:
    case SiteField::UploadPermissions:
        return SiteTabConnection;
    default:
        return SiteTabSession;
    }
}

std::wstring SiteHostColumn(const CSite& site)
{
    std::wstring text = site.host;
    if (text.find(L':') != std::wstring::npos)
        text = L"[" + text + L"]";
    if (site.port != 22)
        text += L":" + std::to_wstring(site.port);
    return text;
}

bool ParseQuickConnect(const std::wstring& input, CSftpPath& out)
{
    std::wstring text = Trim(input);
    if (text.size() >= 5 && CompareStringOrdinal(text.data(), 5, L"sftp:", 5, TRUE) == CSTR_EQUAL)
        text = Trim(text.substr(5));
    CSftpPath parsed;
    if (text.empty() || !ParseSftpPath(text, parsed) || parsed.isSite)
        return false;
    out = std::move(parsed);
    return true;
}

std::wstring SiteConnectPath(const std::wstring& siteName)
{
    CSftpPath path;
    path.isSite = true;
    path.siteName = siteName;
    return FormatSftpPath(path);
}
