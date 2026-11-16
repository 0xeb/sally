#include "precomp.h"

#include "info_dialog.h"
#include "info_list_dialog.h"
#include "progress_format.h"

namespace
{
std::wstring Text(const std::string& utf8)
{
    return CNameCodec::Utf8().Decode(utf8);
}

std::wstring OrUnknown(const std::string& utf8)
{
    return utf8.empty() ? LangStr(IDS_INFO_UNKNOWN) : Text(utf8);
}

void Add(std::vector<CInfoRow>& rows, int labelId, std::wstring value)
{
    rows.push_back({LangStr(labelId), std::move(value)});
}

// One row when both directions use the same algorithm, two rows ("... (to server)", "... (from server)") otherwise.
void AddDirectional(std::vector<CInfoRow>& rows, int labelId, const std::string& toServer, const std::string& fromServer)
{
    if (toServer == fromServer)
    {
        Add(rows, labelId, OrUnknown(toServer));
        return;
    }
    rows.push_back({LangStr(labelId) + LangStr(IDS_INFO_TO_SERVER), OrUnknown(toServer)});
    rows.push_back({LangStr(labelId) + LangStr(IDS_INFO_FROM_SERVER), OrUnknown(fromServer)});
}

void AddMethodRows(std::vector<CInfoRow>& rows, const CSshMethods& m)
{
    Add(rows, IDS_INFO_KEX, OrUnknown(m.kex));
    AddDirectional(rows, IDS_INFO_CIPHER, m.cipherClientToServer, m.cipherServerToClient);
    AddDirectional(rows, IDS_INFO_MAC, m.macClientToServer, m.macServerToClient);
    AddDirectional(rows, IDS_INFO_COMPRESSION, m.compressionClientToServer, m.compressionServerToClient);
}

std::wstring TrustText(HostKeyTrust trust)
{
    switch (trust)
    {
    case HostKeyTrust::Known: return LangStr(IDS_SEC_TRUST_KNOWN);
    case HostKeyTrust::AcceptedAndSaved: return LangStr(IDS_SEC_TRUST_SAVED);
    default: return LangStr(IDS_SEC_TRUST_ONCE);
    }
}
} // namespace

bool IsHostKeyTrusted(const CPanelSessionInfo& session)
{
    return session.valid && session.trust != HostKeyTrust::AcceptedOnce;
}

void ShowServerInfoDialog(HWND parent, const CInfoDialogInput& input)
{
    try
    {
        std::vector<CInfoRow> rows;
        Add(rows, IDS_INFO_HOST, input.host);
        Add(rows, IDS_INFO_PORT, std::to_wstring(input.port));
        Add(rows, IDS_INFO_USER, input.user);
        if (!input.session.valid)
            Add(rows, IDS_INFO_BANNER, LangStr(IDS_INFO_NOT_CONNECTED));
        else
        {
            Add(rows, IDS_INFO_BANNER, OrUnknown(input.session.server.banner));
            Add(rows, IDS_INFO_SFTP_VERSION,
                SPLFormatStringOwned(LangStr(IDS_INFO_SFTP_VERSION_VALUE).c_str(),
                                     input.session.server.sftpVersionSupported));
        }
        Add(rows, IDS_INFO_FREE, input.freeKnown ? FormatBytes(input.bytesFree) : LangStr(IDS_INFO_NOT_REPORTED));
        Add(rows, IDS_INFO_TOTAL, input.totalKnown ? FormatBytes(input.bytesTotal) : LangStr(IDS_INFO_NOT_REPORTED));
        if (input.session.valid)
            AddMethodRows(rows, input.session.methods);

        CInfoListDialog dialog(parent, IDD_FSINFO, IDH_SERVER_INFO,
                               SPLFormatStringOwned(LangStr(IDS_INFO_TITLE).c_str(), input.connection.c_str()),
                               std::move(rows));
        dialog.Run();
    }
    catch (...)
    {
        TRACE_E("ShowServerInfoDialog: unexpected exception.");
    }
}

void ShowSecurityDialog(HWND parent, const std::wstring& connection, const CPanelSessionInfo& session)
{
    try
    {
        std::vector<CInfoRow> rows;
        if (!session.valid)
            Add(rows, IDS_SEC_TRUST, LangStr(IDS_INFO_NOT_CONNECTED));
        else
        {
            Add(rows, IDS_SEC_TRUST, TrustText(session.trust));
            Add(rows, IDS_SEC_HOSTKEY_TYPE, OrUnknown(session.hostKeyType));
            Add(rows, IDS_SEC_FINGERPRINT, OrUnknown(session.hostKeyFingerprint));
            AddMethodRows(rows, session.methods);
        }
        CInfoListDialog dialog(parent, IDD_FSSECURITY, IDH_SECURITY_INFO,
                               SPLFormatStringOwned(LangStr(IDS_SEC_TITLE).c_str(), connection.c_str()),
                               std::move(rows));
        dialog.Run();
    }
    catch (...)
    {
        TRACE_E("ShowSecurityDialog: unexpected exception.");
    }
}
