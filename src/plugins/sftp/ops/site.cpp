#include "site.h"

CNameCodec CSite::MakeCodec() const
{
    switch (encoding)
    {
    case Encoding::Auto:
        return CNameCodec::Auto();
    case Encoding::CodePage:
        return CNameCodec::CodePage(codePage);
    default:
        return CNameCodec::Utf8();
    }
}

bool EncodingFromStored(unsigned long stored, CSite::Encoding& out)
{
    if (stored > static_cast<unsigned long>(CSite::Encoding::CodePage))
        return false;
    out = static_cast<CSite::Encoding>(stored);
    return true;
}

CSshSite ToSshSite(const CSite& site, CSecret password, CSecret passphrase)
{
    const CNameCodec utf8 = CNameCodec::Utf8();
    CSshSite out;
    out.host = utf8.Encode(site.host);
    out.port = site.port;
    out.user = utf8.Encode(site.user);
    out.password = std::move(password);
    out.keyFile = utf8.Encode(site.keyFile);
    out.passphrase = std::move(passphrase);
    out.useAgent = site.useAgent;
    out.compression = site.compression;
    out.timeoutSeconds = site.timeoutSeconds;
    out.nameCodec = site.MakeCodec();
    return out;
}

bool SiteNeedsPasswordPrompt(const CSite& site, bool havePassword)
{
    return !havePassword && site.keyFile.empty() && !site.useAgent;
}
