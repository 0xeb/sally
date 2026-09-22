// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"

#include <limits>

// Error codes returned by SSL_get_error()
#define SSL_ERROR_NONE 0
#define SSL_ERROR_SSL 1
#define SSL_ERROR_WANT_READ 2
#define SSL_ERROR_WANT_WRITE 3
#define SSL_ERROR_WANT_X509_LOOKUP 4
#define SSL_ERROR_SYSCALL 5 // look at error stack/return value/errno
#define SSL_ERROR_ZERO_RETURN 6
#define SSL_ERROR_WANT_CONNECT 7
#define SSL_ERROR_WANT_ACCEPT 8

// Return codes of SSL_read()
#define SSL_NOTHING 1
#define SSL_READING 2
#define SSL_WRITING 3
#define SSL_X509_LOOKUP 4

// Transferred from WinSocket
// #define WSAECONNCLOSED                 (WSABASEERR + 10000)

#define SSL_CTRL_OPTIONS 32

/* SSL_OP_ALL: various bug workarounds that should be rather harmless.
 *             This used to be 0x000FFFFFL before 0.9.7. */
#define SSL_OP_ALL 0x80000BFFL

#define SSL_OP_NO_SSLv2 0x01000000L

#define SizeOf(x) (sizeof(x) / sizeof(x[0]))

#ifndef CERT_VERIFY_REV_SERVER_OCSP_FLAG
#define CERT_VERIFY_REV_SERVER_OCSP_FLAG 8 // defined in SDK's newer than 2003
#endif

// Brought from ssl.h
#define SSL_ST_CONNECT 0x1000
#define SSL_ST_ACCEPT 0x2000
#define SSL_ST_MASK 0x0FFF
#define SSL_ST_INIT (SSL_ST_CONNECT | SSL_ST_ACCEPT)
#define SSL_ST_BEFORE 0x4000
#define SSL_ST_OK 0x03
#define SSL_ST_RENEGOTIATE (0x04 | SSL_ST_INIT)

#define SSL_CB_LOOP 0x01
#define SSL_CB_EXIT 0x02
#define SSL_CB_READ 0x04
#define SSL_CB_WRITE 0x08
#define SSL_CB_ALERT 0x4000 /* used in callback */
#define SSL_CB_READ_ALERT (SSL_CB_ALERT | SSL_CB_READ)
#define SSL_CB_WRITE_ALERT (SSL_CB_ALERT | SSL_CB_WRITE)
#define SSL_CB_ACCEPT_LOOP (SSL_ST_ACCEPT | SSL_CB_LOOP)
#define SSL_CB_ACCEPT_EXIT (SSL_ST_ACCEPT | SSL_CB_EXIT)
#define SSL_CB_CONNECT_LOOP (SSL_ST_CONNECT | SSL_CB_LOOP)
#define SSL_CB_CONNECT_EXIT (SSL_ST_CONNECT | SSL_CB_EXIT)
#define SSL_CB_HANDSHAKE_START 0x10
#define SSL_CB_HANDSHAKE_DONE 0x20

sSSLLib SSLLib;

static bool bSSLInited = false;

// OpenSSL 0.9.x exposes diagnostic strings as library-owned narrow bytes. Keep
// that legacy encoding decision at this adapter instead of widening ad hoc at
// every log call.
static BOOL DecodeSSLLibraryText(const char* bytes, std::wstring& text) noexcept
{
    return FtpDecodeLocalText(bytes != NULL ? bytes : "", text);
}

static BOOL GetOpenSSLNameOneLine(X509_NAME* name, std::string& text) noexcept
{
    if (name == NULL)
        return FALSE;
    try
    {
        size_t capacity = 256;
        for (;;)
        {
            if (capacity > static_cast<size_t>((std::numeric_limits<int>::max)()))
                return FALSE;
            std::string staged(capacity, '\0');
            if (SSLLib.X509_NAME_oneline(name, staged.data(),
                                         static_cast<int>(staged.size())) == NULL)
                return FALSE;
            const size_t length = strnlen_s(staged.data(), staged.size());
            if (length + 1 < staged.size())
            {
                staged.resize(length);
                text.swap(staged);
                return TRUE;
            }
            if (capacity > static_cast<size_t>((std::numeric_limits<int>::max)()) / 2)
                return FALSE;
            capacity *= 2;
        }
    }
    catch (...)
    {
        return FALSE;
    }
}

static BOOL GetOpenSSLErrorText(unsigned long errorCode, std::string& text) noexcept
{
    // ERR_error_string's frozen OpenSSL 0.9 ABI requires caller storage of at
    // least 256 bytes. Keep that contract inside this adapter and publish an
    // exact dynamically owned byte string to the rest of the plugin.
    constexpr size_t OpenSSLErrorStringStorage = 256;
    try
    {
        std::string staged(OpenSSLErrorStringStorage, '\0');
        const char* result = SSLLib.ERR_error_string(errorCode, staged.data());
        if (result == NULL)
            return FALSE;
        staged.resize(strnlen_s(staged.data(), staged.size()));
        text.swap(staged);
        return TRUE;
    }
    catch (...)
    {
        return FALSE;
    }
}

static void LogSSLFormatted(int logUID, BOOL addTime, const wchar_t* format, ...) noexcept
{
    va_list args;
    va_start(args, format);
    try
    {
        std::wstring message = SPLFormatStringOwnedV(format, args);
        if (!message.empty())
            Logs.LogMessage(logUID, message.c_str(), -1, addTime);
    }
    catch (...)
    {
        // Diagnostics are best effort and must never escape a socket callback.
    }
    va_end(args);
}

static void LogSSLResource(int logUID, int resourceID, BOOL addTime, ...) noexcept
{
    va_list args;
    va_start(args, addTime);
    try
    {
        const std::wstring format = LangStr(resourceID);
        std::wstring message = SPLFormatStringOwnedV(format.c_str(), args);
        if (!message.empty())
            Logs.LogMessage(logUID, message.c_str(), -1, addTime);
    }
    catch (...)
    {
        // Diagnostics are best effort and must never escape a socket callback.
    }
    va_end(args);
}

static void LogSSLLibraryText(int logUID, const char* bytes, BOOL addTime = FALSE) noexcept
{
    try
    {
        std::wstring text;
        if (DecodeSSLLibraryText(bytes, text))
            Logs.LogMessage(logUID, text.c_str(), -1, addTime);
    }
    catch (...)
    {
        // Diagnostics are best effort and must never escape a socket callback.
    }
}

static void LogSSLAlgorithm(int logUID, const char* versionBytes, const char* cipherBytes,
                            int bits) noexcept
{
    std::wstring version;
    std::wstring cipher;
    DecodeSSLLibraryText(versionBytes, version);
    DecodeSSLLibraryText(cipherBytes, cipher);
    LogSSLResource(logUID, IDS_SSL_LOG_ALGO, FALSE, version.c_str(), cipher.c_str(), bits);
}

BYTE hex(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    return 0;
}

#define IMP_SYMBOL(name) {(void**)&SSLLib.name, #name},

typedef struct _SymbolInfo
{
    void** Addr;
    const char* Name;
} TSymbolInfo, *PSymbolInfo;

static TSymbolInfo SSLSymbols[] = {
    IMP_SYMBOL(SSL_get_error)
        IMP_SYMBOL(SSL_write)
            IMP_SYMBOL(SSL_read)
                IMP_SYMBOL(SSL_library_init)
                    IMP_SYMBOL(SSL_load_error_strings)
    //   IMP_SYMBOL(SSLv3_client_method)
    IMP_SYMBOL(SSLv23_client_method)
        IMP_SYMBOL(SSL_CTX_new)
            IMP_SYMBOL(SSL_CTX_free)
                IMP_SYMBOL(SSL_CTX_ctrl)
                    IMP_SYMBOL(SSL_new)
                        IMP_SYMBOL(SSL_free)
                            IMP_SYMBOL(SSL_set_fd)
                                IMP_SYMBOL(SSL_connect)
                                    IMP_SYMBOL(SSL_shutdown)
                                        IMP_SYMBOL(SSL_get_current_cipher)
                                            IMP_SYMBOL(SSL_CIPHER_get_version)
                                                IMP_SYMBOL(SSL_CIPHER_get_bits)
                                                    IMP_SYMBOL(SSL_CIPHER_get_name)
                                                        IMP_SYMBOL(SSL_get_verify_result)
                                                            IMP_SYMBOL(SSL_set_verify)
                                                                IMP_SYMBOL(SSL_pending)
                                                                    IMP_SYMBOL(SSL_get_peer_certificate)
                                                                        IMP_SYMBOL(SSL_get_peer_cert_chain)
                                                                            IMP_SYMBOL(SSL_COMP_get_compression_methods)
                                                                                IMP_SYMBOL(SSL_get1_session)
                                                                                    IMP_SYMBOL(SSL_set_session)
                                                                                        IMP_SYMBOL(SSL_SESSION_free)
                                                                                            IMP_SYMBOL(SSL_ctrl)
#ifdef _DEBUG
                                                                                                IMP_SYMBOL(SSL_state_string_long)
                                                                                                    IMP_SYMBOL(SSL_alert_type_string_long)
                                                                                                        IMP_SYMBOL(SSL_alert_desc_string_long)
                                                                                                            IMP_SYMBOL(SSL_set_info_callback)
#endif
                                                                                                                {NULL, NULL}};

static TSymbolInfo SSLUtilSymbols[] = {
    IMP_SYMBOL(SSLeay_version)
#ifdef _DEBUG
        IMP_SYMBOL(X509_verify_cert_error_string)
#endif
            IMP_SYMBOL(X509_NAME_oneline)
                IMP_SYMBOL(i2d_X509)
                    IMP_SYMBOL(X509_free)
                        IMP_SYMBOL(PKCS7_new)
                            IMP_SYMBOL(PKCS7_SIGNED_new)
                                IMP_SYMBOL(OBJ_nid2obj)
                                    IMP_SYMBOL(ASN1_INTEGER_set)
                                        IMP_SYMBOL(PKCS7_free)
                                            IMP_SYMBOL(i2d_PKCS7)
                                                IMP_SYMBOL(sk_new_null)
    //   IMP_SYMBOL(BIO_s_mem)
    //   IMP_SYMBOL(BIO_new)
    //   IMP_SYMBOL(BIO_free)
    //   IMP_SYMBOL(BIO_read)
    //   IMP_SYMBOL(ERR_clear_error)
    IMP_SYMBOL(ERR_error_string)
    //   IMP_SYMBOL(ERR_print_errors)
    IMP_SYMBOL(ERR_get_error)
    //   IMP_SYMBOL(ERR_get_state)
    IMP_SYMBOL(ERR_remove_state)
        IMP_SYMBOL(ENGINE_cleanup)
            IMP_SYMBOL(CONF_modules_finish)
                IMP_SYMBOL(CONF_modules_free)
                    IMP_SYMBOL(CONF_modules_unload)
                        IMP_SYMBOL(ERR_free_strings)
                            IMP_SYMBOL(sk_free)
                                IMP_SYMBOL(EVP_cleanup)
                                    IMP_SYMBOL(CRYPTO_cleanup_all_ex_data)
                                        IMP_SYMBOL(CRYPTO_num_locks)
                                            IMP_SYMBOL(CRYPTO_set_locking_callback)
                                                IMP_SYMBOL(RAND_seed){NULL, NULL}};

static bool LoadSymbols(LPCWSTR libName, LPCWSTR altLibName, HINSTANCE& hLib, PSymbolInfo pSymbols, int logUID)
{
    hLib = LoadLibraryW(libName);
    if (!hLib && altLibName)
    {
        hLib = LoadLibraryW(altLibName);
        if (hLib)
            libName = altLibName;
    }
    const DWORD loadError = hLib ? ERROR_SUCCESS : GetLastError();
    if (hLib)
        SalamanderDebug->AddModuleWithPossibleMemoryLeaks(libName);

    std::string libNameBytes;
    const char* libNameForLog = FtpEncodeLocalText(libName, libNameBytes)
                                    ? libNameBytes.c_str()
                                    : "<Unicode library path>";
    std::string message;
    if (!hLib)
    {
        if (FTPFormatString(message, "Err %u: Unable to load %s\r\n", loadError, libNameForLog))
            Logs.LogMessage(logUID, message.c_str(), -1);
        return false;
    }
    while (pSymbols->Addr)
    {
        *pSymbols->Addr = GetProcAddress(hLib, pSymbols->Name);
        if (!*pSymbols->Addr)
        {
            if (FTPFormatString(message, "Err %u: Unable to find %s in %s\r\n", GetLastError(), pSymbols->Name, libNameForLog))
                Logs.LogMessage(logUID, message.c_str(), -1);
            return false;
        }
        pSymbols++;
    }
    return true;
} /* LoadSymbols */

static void AddNewLine(std::wstring& text)
{
    if (!text.empty() && text.back() != L'\n' && text.back() != L'\r')
        text += L'\n';
} /* AddNewLine */

static void CopyCertificatePolicyError(std::wstring& text, DWORD error)
{
    text = SPLGetErrorTextOwned(SalamanderGeneral, error);
}

static bool CheckCertificate(BYTE* pCert, int certLen, std::wstring& errorText,
                             const wchar_t* host)
{
    CALL_STACK_MESSAGE4("CheckCertificate(0x%p, %d, %ls)", pCert, certLen, host);
    CERT_CHAIN_PARA ChainPara;
    PCCERT_CHAIN_CONTEXT pChainContext = NULL;
    CERT_CHAIN_POLICY_PARA PolicyPara;
    CERT_CHAIN_POLICY_STATUS PolicyStatus;
    HTTPSPolicyCallbackData polHttps;
    PCCERT_CONTEXT pCertContext = CertCreateCertificateContext(X509_ASN_ENCODING | PKCS_7_ASN_ENCODING, pCert, certLen);
    errorText.clear();
    if (!pCertContext)
    {
        CopyCertificatePolicyError(errorText, GetLastError());
        return false;
    }

    bool checkRevocation = true;

CHECK_CERT_AGAIN:

    memset(&ChainPara, 0, sizeof(ChainPara));
    ChainPara.cbSize = sizeof(ChainPara);
    if (!CertGetCertificateChain(NULL,
                                 pCertContext,
                                 NULL,
                                 NULL,
                                 &ChainPara,
                                 (checkRevocation ? CERT_CHAIN_REVOCATION_CHECK_CHAIN : 0) | // Revocation checking is done on all of the certificates in every chain.
                                     CERT_CHAIN_CACHE_END_CERT |                             // When this flag is set, the end certificate is cached, which might speed up the chain-building process. By default, the end certificate is not cached, and it would need to be verified each time a chain is built for it.
                                     CERT_CHAIN_DISABLE_AUTH_ROOT_AUTO_UPDATE,               // Inhibits the auto update of third-party roots from the Windows Update Web Server
                                 NULL,
                                 &pChainContext))
    {
        CopyCertificatePolicyError(errorText, GetLastError());
        CertFreeCertificateContext(pCertContext);
        return false;
    }
    memset(&polHttps, 0, sizeof(HTTPSPolicyCallbackData));
    polHttps.cbStruct = sizeof(HTTPSPolicyCallbackData);
    polHttps.dwAuthType = AUTHTYPE_SERVER;
    polHttps.fdwChecks = 0;
    polHttps.pwszServerName = const_cast<wchar_t*>(host != NULL ? host : L"");

    memset(&PolicyPara, 0, sizeof(PolicyPara));
    PolicyPara.cbSize = sizeof(PolicyPara);
    PolicyPara.pvExtraPolicyPara = &polHttps;

    memset(&PolicyStatus, 0, sizeof(PolicyStatus));
    PolicyStatus.cbSize = sizeof(PolicyStatus);

    if (!CertVerifyCertificateChainPolicy(CERT_CHAIN_POLICY_SSL,
                                          pChainContext, &PolicyPara, &PolicyStatus))
    {
        CopyCertificatePolicyError(errorText, GetLastError());
        CertFreeCertificateChain(pChainContext);
        CertFreeCertificateContext(pCertContext);
        return false;
    }

    bool ok = true;

    if (PolicyStatus.dwError)
    {
        if (checkRevocation && PolicyStatus.dwError == CRYPT_E_NO_REVOCATION_CHECK)
        { // The revocation function was unable to check revocation for the certificate. OK, so try it without checking revocation (MS probably also skips it because they accept the same certificate at the same time on the same machine).
            CertFreeCertificateChain(pChainContext);
            pChainContext = NULL;
            checkRevocation = false;
            goto CHECK_CERT_AGAIN;
        }
        else
        {
            ok = false;
            CopyCertificatePolicyError(errorText, PolicyStatus.dwError);
        }
    }
    int res = CertVerifyTimeValidity(NULL, pCertContext->pCertInfo);
    if (res != 0)
    {
        ok = false;
        AddNewLine(errorText);
        errorText += LangStr((res < 0) ? IDS_SSL_ERR_NOTYETVALID : IDS_SSL_ERR_EXPIRED).c_str();
    }
    // Whatever flag is used, revokation check fails on most servers :-/
    /*int i;
  for (i = 0; i < pChainContext->cChain; i++)
  {
    CERT_REVOCATION_STATUS  revStat;

    revStat.cbSize = sizeof(CERT_REVOCATION_STATUS);
    if (!CertVerifyRevocation(X509_ASN_ENCODING,
                              CERT_CONTEXT_REVOCATION_TYPE,
                              1,
                              (void**)&pChainContext->rgpChain[i]->rgpElement[0]->pCertContext,
                              //CERT_VERIFY_CACHE_ONLY_BASED_REVOCATION/
                              CERT_VERIFY_REV_SERVER_OCSP_FLAG
                              //CERT_VERIFY_REV_CHAIN_FLAG,
                              NULL,
                              &revStat))
    {
      ok = false;
      AddNewLine(buf, maxlen);
      CopyCertificatePolicyError(buf, maxlen, revStat.dwError);
      break;
    }
  }*/

    CertFreeCertificateChain(pChainContext);
    CertFreeCertificateContext(pCertContext);
    return ok;
} /* CheckCertificate */

static bool ViewCertificate(HWND hParent, BYTE* pCertData, int CertDataLen, BYTE* pPKCS7Cert, int PKCS7CertLen, LPCWSTR pTitle)
{
    CALL_STACK_MESSAGE5("ViewCertificate(0x%p, 0x%p, %d, %ls)", hParent, pCertData, CertDataLen, pTitle);
    CRYPTUI_VIEWCERTIFICATE_STRUCTW cvi;
    PCCERT_CONTEXT pCertContext = CertCreateCertificateContext(X509_ASN_ENCODING | PKCS_7_ASN_ENCODING, pCertData, CertDataLen);
    if (!pCertContext)
    {
        const std::wstring errText = SPLGetErrorTextOwned(SalamanderGeneral, GetLastError());
        const wchar_t* errStr = errText.c_str();
        SalamanderGeneral->SalMessageBox(hParent, errStr, SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_FTPPLUGINTITLE).c_str(), MB_OK | MB_ICONSTOP);
        return false;
    }

    // Put all other certificates provided by the server, possibly untrusted, to hCertStore
    CRYPT_INTEGER_BLOB blob = {(DWORD)PKCS7CertLen, pPKCS7Cert};
    HCERTSTORE hCertStore = CertOpenStore(CERT_STORE_PROV_PKCS7, PKCS_7_ASN_ENCODING, NULL, 0, &blob);

    memset(&cvi, 0, sizeof(cvi));
    cvi.dwSize = sizeof(cvi);
    cvi.hwndParent = hParent;
    cvi.dwFlags = CRYPTUI_DISABLE_EDITPROPERTIES;
    cvi.szTitle = pTitle;
    cvi.pCertContext = pCertContext;
    if (hCertStore)
    {
        cvi.cStores = 1;
        cvi.rghStores = &hCertStore;
    }

    if (!CryptUIDlgViewCertificateW(&cvi, NULL))
    {
        DWORD err = GetLastError();
        if (ERROR_CANCELLED != err)
        {
            const std::wstring errText = SPLGetErrorTextOwned(SalamanderGeneral, err);
            const wchar_t* errStr = errText.c_str();
            CertFreeCertificateContext(pCertContext);
            if (hCertStore)
                CertCloseStore(hCertStore, CERT_CLOSE_STORE_FORCE_FLAG);
            SalamanderGeneral->SalMessageBox(hParent, errStr, SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_FTPPLUGINTITLE).c_str(), MB_OK | MB_ICONSTOP);
            return false;
        }
    }
    if (hCertStore)
        CertCloseStore(hCertStore, CERT_CLOSE_STORE_FORCE_FLAG);
    CertFreeCertificateContext(pCertContext);
    return true;
} /* ViewCertificate */

#ifdef _DEBUG
static void InfoCallback(const SSL* s, int where, int ret) noexcept
{
    const char* str;
    int w;
    std::string message;

    w = where & ~SSL_ST_MASK;

    if (w & SSL_ST_CONNECT)
        str = "SSL_connect";
    else if (w & SSL_ST_ACCEPT)
        str = "SSL_accept";
    else
        str = "undefined";

    if (where & SSL_CB_LOOP)
    {
        FTPFormatString(message, "%s:%s\n", str, SSLLib.SSL_state_string_long(s));
    }
    else if (where & SSL_CB_ALERT)
    {
        str = (where & SSL_CB_READ) ? "read" : "write";
        FTPFormatString(message, "SSL3 alert %s:%s:%s\n", str,
                        SSLLib.SSL_alert_type_string_long(ret),
                        SSLLib.SSL_alert_desc_string_long(ret));
    }
    else if (where & SSL_CB_EXIT)
    {
        if (ret == 0)
        {
            FTPFormatString(message, "%s:failed in %s\n", str,
                            SSLLib.SSL_state_string_long(s));
        }
        else if (ret < 0)
        {
            FTPFormatString(message, "%s:error %d in %s\n", str, ret,
                            SSLLib.SSL_state_string_long(s));
        }
    }
    if (!message.empty())
    {
        OutputDebugStringA(message.c_str());
        TRACE_I(message.c_str());
    }
}
#endif

void WriteSSLErrorStackToLog(int logUID, const char* errSrc) noexcept
{
    // log OpenSSL error stack
    int err2;
    while ((err2 = SSLLib.ERR_get_error()) != 0)
    {
        std::string errorBytes;
        std::wstring source;
        std::wstring error;
        if (GetOpenSSLErrorText(err2, errorBytes) &&
            DecodeSSLLibraryText(errSrc, source) &&
            DecodeSSLLibraryText(errorBytes.c_str(), error))
            LogSSLFormatted(logUID, FALSE, L"SSL ERROR: %s: %s\r\n",
                            source.c_str(), error.c_str());
    }
}

BOOL CSocket::EncryptSocket(int logUID, int* sslErrorOccured, CCertificate** unverifiedCert,
                            int* errorID, std::string* errorText, CSocket* conForReuse)
{
    int err;
    SSL* Conn;

    CALL_STACK_MESSAGE3("CSocket::EncryptSocket(%d, , , , , , 0x%p)", logUID, conForReuse);
    if (errorID != NULL)
        *errorID = -1;
    if (errorText != NULL)
        errorText->clear();
    if (unverifiedCert != NULL)
        *unverifiedCert = NULL;
    if (sslErrorOccured != NULL)
        *sslErrorOccured = SSLConn != NULL ? SSLCONERR_NOERROR : SSLCONERR_DONOTRETRY;
    if (SSLConn != NULL)
        return TRUE; // socket has already been encrypted
    if (!bSSLInited)
        return FALSE;
    WriteSSLErrorStackToLog(logUID, "unknown source");
    Conn = SSLLib.SSL_new(SSLLib.Ctx);
    if (Conn)
    {
        HWND hWnd = SocketsThread->GetHiddenWindow();
        u_long argp = 0;

        WSAAsyncSelect(Socket, hWnd, 0, 0);
        err = ioctlsocket(Socket, FIONBIO, &argp);
        // On x64, SOCKET is a 64-bit value, but the OpenSSL folks assume it never exceeds 2^32
        // see http://comments.gmane.org/gmane.comp.encryption.openssl.devel/13621
        // http://msdn.microsoft.com/en-us/library/ms724485%28VS.85%29.aspx
        // if that happens and this condition starts failing, maybe an x64 version of SSL will already exist
        if (Socket > 0x00000000ffffffff)
        {
            DWORD* crash = NULL;
            *crash = 0;
        }
        if (!SSLLib.SSL_set_fd(Conn, (int)Socket))
            WriteSSLErrorStackToLog(logUID, "SSL_set_fd");
        SSLLib.SSL_set_verify(Conn, 0, NULL);

#ifdef _DEBUG
        SSLLib.SSL_set_info_callback(Conn, InfoCallback);
        Logs.LogMessage(logUID, L"SSL DEBUG INFO: See Trace Server for messages from information callback for this SSL connection.\r\n", -1);
#endif

        BOOL testReuseSSLSession = FALSE;
        if (conForReuse != NULL && conForReuse->SSLConn != NULL && conForReuse->ReuseSSLSession != 2 /* no */)
        {
            SSL_SESSION* ssl_sessionid = SSLLib.SSL_get1_session(conForReuse->SSLConn); // sessionid.addref()
            if (ssl_sessionid == NULL)
                Logs.LogMessage(logUID, L"SSL ERROR: SSL_get1_session returns NULL!\r\n", -1);
            else
            {
                if (!SSLLib.SSL_set_session(Conn, ssl_sessionid))
                    WriteSSLErrorStackToLog(logUID, "SSL_set_session");
                else
                    testReuseSSLSession = TRUE;
                SSLLib.SSL_SESSION_free(ssl_sessionid); // sessionid.release()
            }
        }

        TRACE_I("SSL_connect: begin");
        {
            CALL_STACK_MESSAGE1("CSocket::EncryptSocket::SSL_connect()");
            err = SSLLib.SSL_connect(Conn);
        }
        TRACE_I("SSL_connect: end");

        if (err > 0)
        {
            if (testReuseSSLSession)
            {
                if (SSLLib.SSL_session_reused(Conn))
                {
                    Logs.LogMessage(logUID, L"SSL INFO: SSL session reused for data-connection\r\n", -1);
                    if (conForReuse->ReuseSSLSession == 0 /* try */)
                        conForReuse->ReuseSSLSession = 1 /* yes */;
                }
                else // SSL session not reused
                {
                    if (conForReuse->ReuseSSLSession == 0 /* try */) // do not try again for future data-connections (or it will fail)
                    {
                        Logs.LogMessage(logUID, L"SSL INFO: SSL session was NOT reused, will not try for future data-connections...\r\n", -1);
                        conForReuse->ReuseSSLSession = 2 /* no */;
                    }
                    else // try for all future data-cons to set ReuseSSLSessionFailed to TRUE and so reconnect ctrl-con (except if this is keep-alive data-con)
                    {
                        Logs.LogMessage(logUID, L"SSL INFO: SSL session was NOT reused, it has expired in server session cache, reconnect of control connection is needed...\r\n", -1);
                        conForReuse->ReuseSSLSessionFailed = TRUE; // To open the data connection, reuse is probably necessary, but it reports an error; the only solution is to reconnect the control connection.
                    }
                }
            }

            void* ssl_cipher;
            char* ssl_version;
            char* cipher_name;
            int ssl_bits;
            X509* peerCert;
            STACK_OF(X509) * certStack;
            BYTE *DERCert, *PKCS7Cert = NULL, *tmp;
            int DERCertLen, PKCS7CertLen;
            int verRes = SSLLib.SSL_get_verify_result(Conn);

            LogSSLResource(logUID, IDS_SSL_LOG_OSSL_CERT_VERIFY, FALSE, verRes);
#ifdef _DEBUG
            const char* str = SSLLib.X509_verify_cert_error_string(verRes);
#endif

            peerCert = SSLLib.SSL_get_peer_certificate(Conn);
            std::string subjectBytes;
            std::wstring subject;
            if (GetOpenSSLNameOneLine(peerCert->cert_info->subject, subjectBytes) &&
                DecodeSSLLibraryText(subjectBytes.c_str(), subject))
                LogSSLResource(logUID, IDS_SSL_LOG_SUBJECT, FALSE, subject.c_str());
            std::string issuerBytes;
            std::wstring issuer;
            if (GetOpenSSLNameOneLine(peerCert->cert_info->issuer, issuerBytes) &&
                DecodeSSLLibraryText(issuerBytes.c_str(), issuer))
                LogSSLResource(logUID, IDS_SSL_LOG_ISSUER, FALSE, issuer.c_str());

            // Obtain entire certificate chain upto root certificate
            certStack = SSLLib.SSL_get_peer_cert_chain(Conn);
            if (certStack)
            {
                PKCS7* p7 = SSLLib.PKCS7_new();
                if (p7)
                {
                    PKCS7_SIGNED* p7s = SSLLib.PKCS7_SIGNED_new();
                    if (p7s)
                    {
                        p7->type = SSLLib.OBJ_nid2obj(NID_pkcs7_signed);
                        p7->d.sign = p7s;
                        p7s->contents->type = SSLLib.OBJ_nid2obj(NID_pkcs7_data);
                        SSLLib.ASN1_INTEGER_set(p7s->version, 1);
                        p7s->cert = certStack;
                        p7s->crl = SSLLib.sk_new_null();
                        PKCS7CertLen = SSLLib.i2d_PKCS7(p7, NULL);
                        tmp = PKCS7Cert = (BYTE*)malloc(PKCS7CertLen);
                        SSLLib.i2d_PKCS7(p7, &tmp);
                    }
                }
                p7->d.sign->cert = NULL; // Avoid freeing it
                SSLLib.PKCS7_free(p7);
            }

            DERCertLen = SSLLib.i2d_X509(peerCert, NULL);
            tmp = DERCert = (BYTE*)malloc(DERCertLen);
            SSLLib.i2d_X509(peerCert, &tmp);
            SSLLib.X509_free(peerCert);

            ssl_version = SSLLib.SSL_CIPHER_get_version(SSLLib.SSL_get_current_cipher(Conn));
            ssl_cipher = SSLLib.SSL_get_current_cipher(Conn);
            SSLLib.SSL_CIPHER_get_bits(ssl_cipher, &ssl_bits);
            cipher_name = SSLLib.SSL_CIPHER_get_name(ssl_cipher);
            LogSSLAlgorithm(logUID, ssl_version, cipher_name, ssl_bits);

            BOOL certAcceptedOrVerified = FALSE;
            if (pCertificate)
            {
                if (pCertificate->IsSame(DERCert, DERCertLen, PKCS7Cert, PKCS7CertLen))
                {
                    Logs.LogMessage(logUID, LangStr(pCertificate->IsVerified() ? IDS_SSL_LOG_CERTVERIFIED : IDS_SSL_LOG_CERTACCEPTED).c_str(), -1, TRUE);
                    certAcceptedOrVerified = TRUE;
                }
                else // Huh! The certificate has changed?????
                {
                    Logs.LogMessage(logUID, LangStr(IDS_SSL_LOG_CERTCHANGED).c_str(), -1, TRUE);
                    pCertificate->Release();
                    pCertificate = NULL;
                }
            }
            if (!certAcceptedOrVerified)
            {
                std::wstring certificateError;
                if (CheckCertificate(DERCert, DERCertLen, certificateError, HostAddress.c_str()))
                {
                    Logs.LogMessage(logUID, LangStr(IDS_SSL_LOG_CERTVERIFIED).c_str(), -1, TRUE);
                    pCertificate = new CCertificate(DERCert, DERCertLen, PKCS7Cert, PKCS7CertLen, true, HostAddress.c_str());
                    certAcceptedOrVerified = TRUE; // Passed
                }
                else
                {
                    if (errorText != NULL &&
                        !FtpEncodeLocalText(certificateError.c_str(), *errorText))
                        errorText->clear();
                    Logs.LogMessage(logUID, LangStr(IDS_SSL_LOG_CERTNOTVERIFIED).c_str(), -1, TRUE);
                }
            }
            if (!certAcceptedOrVerified)
            {
                // The certificate was not verified nor previously accepted by user, so user should accept
                // it before further using of this socket.
                if (unverifiedCert != NULL)
                    *unverifiedCert = new CCertificate(DERCert, DERCertLen, PKCS7Cert, PKCS7CertLen, false, HostAddress.c_str());
                else
                {
                    SSLLib.SSL_shutdown(Conn);
                    SSLLib.SSL_free(Conn);
                    if (PKCS7Cert)
                        free(PKCS7Cert);
                    free(DERCert);
                    if (sslErrorOccured != NULL)
                        *sslErrorOccured = SSLCONERR_UNVERIFIEDCERT; // The certificate was not verified nor previously accepted by user.
                    return FALSE;
                }
            }
            if (PKCS7Cert)
                free(PKCS7Cert);
            free(DERCert);
            WSAAsyncSelect(Socket, hWnd, Msg, FD_READ | FD_CLOSE | FD_WRITE);
            SSLConn = Conn;
            if (sslErrorOccured != NULL)
                *sslErrorOccured = SSLCONERR_NOERROR; // But the certificate must not be verified nor previously accepted by user.
            return TRUE;
        }
        else
        {
            /*      ERR_STATE *es = SSLLib.ERR_get_state();
      fd_set  fs;
      timeval tv = {0,10};
      FD_ZERO(&fs);
      FD_SET(Socket, &fs);
      select(1, NULL, NULL, &fs, &tv);*/
            err = SSLLib.SSL_get_error(Conn, err);
            //      err = SSLLib.ERR_get_error();
            //      err = GetLastError();

            std::string sslErrorBytes;
            std::wstring errorTextW;
            if (GetOpenSSLErrorText(err, sslErrorBytes) &&
                DecodeSSLLibraryText(sslErrorBytes.c_str(), errorTextW))
                LogSSLResource(logUID, IDS_SSL_ERR_CONNECT_LOG, TRUE, err,
                               errorTextW.c_str());
            WriteSSLErrorStackToLog(logUID, "SSL_connect");

            if (errorID != NULL)
                *errorID = IDS_SSL_ERR_CONNECT;
            if (errorText != NULL)
            {
                if (sslErrorBytes.empty() && !GetOpenSSLErrorText(err, sslErrorBytes))
                    sslErrorBytes = "OpenSSL error";
                if (!FTPFormatString(*errorText, LoadStr(IDS_SSL_ERR_CONNECT_ERR), err,
                                     sslErrorBytes.c_str()))
                    errorText->clear();
            }
            SSLLib.SSL_free(Conn);
            if (sslErrorOccured != NULL)
                *sslErrorOccured = SSLCONERR_CANRETRY;
        }
    }
    else
    {
        Logs.LogMessage(logUID, LangStr(IDS_SSL_ERR_NEW_LOG).c_str(), -1, TRUE);
        if (errorID != NULL)
            *errorID = IDS_SSL_ERR_NEW;
        WriteSSLErrorStackToLog(logUID, "SSL_new");
    }
    return FALSE;
}

void FreeSSL(int loadStatus)
{
    if (bSSLInited || loadStatus != 0)
    {
        if (SSLLib.Locks)
        {
            SSLLib.CRYPTO_set_locking_callback(NULL);
            for (int i = 0; i < SSLLib.CRYPTO_num_locks(); i++)
                if (SSLLib.Locks[i])
                    CloseHandle(SSLLib.Locks[i]);
            free(SSLLib.Locks);
        }
        if (SSLLib.Ctx)
            SSLLib.SSL_CTX_free(SSLLib.Ctx);

        if (loadStatus == 0 || loadStatus == 2)
        {
            // Petr: OpenSSL left a bunch of memory leaks, so I added this block

            // thread-local cleanup
            SSLLib.ERR_remove_state(0);

            // thread-safe cleanup
            SSLLib.ENGINE_cleanup();
            SSLLib.CONF_modules_finish();
            SSLLib.CONF_modules_free();
            SSLLib.CONF_modules_unload(1);

            // global application exit cleanup (after all SSL activity is shutdown)
            SSLLib.ERR_free_strings();
            SSLLib.EVP_cleanup();
            SSLLib.CRYPTO_cleanup_all_ex_data();

            // The stack with compression methods probably cannot be released "legally", so it is handled manually
            STACK_OF(SSL_COMP)* comp_sk = SSLLib.SSL_COMP_get_compression_methods();
            SSLLib.sk_free(CHECKED_STACK_OF(SSL_COMP, comp_sk));

            // Petr: end of block
        }

        if (SSLLib.hSSLLib)
            FreeLibrary(SSLLib.hSSLLib);
        if (SSLLib.hSSLUtilLib)
            FreeLibrary(SSLLib.hSSLUtilLib);
        bSSLInited = false;
    }
}

void SSLThreadLocalCleanup()
{
    if (bSSLInited)
        SSLLib.ERR_remove_state(0);
}

static void LockingCallback(int mode, int type, const char* file, int line)
{
    if (mode & CRYPTO_LOCK)
    {
        WaitForSingleObject(SSLLib.Locks[type], INFINITE);
    }
    else
    {
        ReleaseMutex(SSLLib.Locks[type]);
    }
}

bool InitSSL(int logUID, int* errorID)
{
    if (errorID != NULL)
        *errorID = -1;
    if (bSSLInited)
        return true;

    CALL_STACK_MESSAGE2("InitSSL(%d,)", logUID);

    memset(&SSLLib, 0, sizeof(SSLLib));

    bool ret = false;
    std::wstring dir;
    int loadStatus = 1;
    if (SPLGetModuleFileNameOwned(NULL, dir) &&
        SPLCutDirectoryOwned(SalamanderGeneral, dir))
    {
        SPLSalPathAppendOwned(dir, L"utils");
        ret = true;
        const std::wstring utilDirectory = dir;
        SPLSalPathAppendOwned(dir, L"libeay32.dll");
        if (!LoadSymbols(dir.c_str(), NULL, SSLLib.hSSLUtilLib, SSLUtilSymbols, logUID))
        {
            ret = false;
        }
        dir = utilDirectory;
        SPLSalPathAppendOwned(dir, L"ssleay32.dll");
        if (!ret || !LoadSymbols(dir.c_str(), NULL /*L"libssl32.dll"*/, SSLLib.hSSLLib, SSLSymbols, logUID))
        {
            ret = false;
        }
    }

    if (ret)
    {
        loadStatus = 2;
        std::wstring version;
        if (DecodeSSLLibraryText(SSLLib.SSLeay_version(SSLEAY_VERSION), version))
            LogSSLFormatted(logUID, FALSE,
                            L"SSL INFO: Version: %s \r\nSSL INFO: Compile flags: ",
                            version.c_str());
        LogSSLLibraryText(logUID, SSLLib.SSLeay_version(SSLEAY_CFLAGS));
        Logs.LogMessage(logUID, L"\r\n", -1);
        // NOTE: There are no unload counterparts for SSL_library_init & SSL_load_error_strings
        SSLLib.SSL_library_init();
        SSLLib.SSL_load_error_strings();
        DWORD seed = GetTickCount();
        SSLLib.RAND_seed(&seed, sizeof(seed));
        int locksCount = SSLLib.CRYPTO_num_locks();
        SSLLib.Locks = (HANDLE*)malloc(locksCount * sizeof(HANDLE));
        if (SSLLib.Locks)
        {
            for (int i = 0; i < locksCount; i++)
            {
                SSLLib.Locks[i] = !ret ? NULL : CreateMutex(NULL, FALSE, NULL);
                if (ret && SSLLib.Locks[i] == NULL)
                    ret = false;
            }
        }
        else
            ret = false;
        if (!ret)
        {
            LogSSLFormatted(logUID, FALSE, L"SSL Err: Unable to alloc %d locks\r\n",
                            locksCount);
        }

        if (ret)
        {
            SSLLib.CRYPTO_set_locking_callback(/*(void (*)(int,int,const char *,int))*/ LockingCallback);

            // NOTE: the pointer returned SSLv23_client_method is not to be freed
            //
            // SSLv23_client_method() is default method used in OpenSLL.exe and CURL.
            // Unsafe SSL2 protocol is disabled using OPENSSL_NO_SSL2 define.
            // SSLv3_client_method() didn't work with wedos server: https://forum.altap.cz/viewtopic.php?f=2&t=6667
            //    SSLLib.Meth = SSLLib.SSLv3_client_method();
            SSLLib.Meth = SSLLib.SSLv23_client_method();
            if (SSLLib.Meth)
            {
                SSLLib.Ctx = SSLLib.SSL_CTX_new(SSLLib.Meth);
                if (SSLLib.Ctx)
                {
                    /* also switch on all the interoperability and bug
           * workarounds so that we will communicate with people
           * that cannot read poorly written specs :-)
           */
                    SSLLib.SSL_CTX_ctrl(SSLLib.Ctx, SSL_CTRL_OPTIONS, SSL_OP_ALL, NULL);
                    bSSLInited = true;
                    return true;
                }
            } // if Meth <> NULL then
        }
    }
    FreeSSL(loadStatus);
    memset(&SSLLib, 0, sizeof(SSLLib)); // clean up, everything is freed
    if (errorID != NULL)
        *errorID = IDS_SSL_ERR_OPENSSLNOTFOUND;
    Logs.LogMessage(logUID, LangStr(IDS_SSL_ERR_OPENSSLNOTFOUND).c_str(), -1);
    Logs.LogMessage(logUID, "\r\n", -1);
    return false;
} /* InitSSL */

int SSLtoWS2Error(int err)
{
    switch (err)
    {
    case SSL_ERROR_WANT_READ:
        return WSAEWOULDBLOCK;
    case SSL_ERROR_WANT_WRITE:
        return WSAEWOULDBLOCK;
        //    case SSL_ERROR_SYSCALL:     return WSAECONNCLOSED;
        //    case SSL_ERROR_ZERO_RETURN: return WSAECONNCLOSED;
    default:
        return err;
    }
}

//////////////////////// CCertificateErrDialog ////////////////////////
CCertificateErrDialog::CCertificateErrDialog(HWND hParent, const wchar_t* errorStr)
    : CCenteredDialog(HLanguage, IDD_CERTIFICATE, hParent), ErrorStr(errorStr)
{
}

INT_PTR CCertificateErrDialog::DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    CALL_STACK_MESSAGE4("CCertificateErrDialog::DialogProc(0x%X, 0x%IX, 0x%IX)", uMsg, wParam, lParam);
    switch (uMsg)
    {
    case WM_INITDIALOG:
        SetDlgItemTextW(HWindow, IDT_CERTIFICATE_ERROR, ErrorStr);
        break;
    case WM_COMMAND:
        switch (LOWORD(wParam))
        {
        case IDB_CERTIFICATE_VIEW:
            if (HIWORD(wParam) == BN_CLICKED)
            {
                EndDialog(HWindow, IDB_CERTIFICATE_VIEW);
            }
            break;
        }
    }
    return CCenteredDialog::DialogProc(uMsg, wParam, lParam);
}

//////////////////////// CCertificate ////////////////////////
CCertificate::CCertificate(BYTE* pDERCert, int DERCertLen, BYTE* pPKCS7Cert, int PKCS7CertLen, bool bValid, const wchar_t* host)
{
    CALL_STACK_MESSAGE7("CCertificate::ctor(0x%p, %d, 0x%p, %d, %d, %ls)", pDERCert, DERCertLen, pPKCS7Cert, PKCS7CertLen, bValid, host);
    bVerified = bValid;
    pDERData = (BYTE*)malloc(DERCertLen);
    if (pDERData)
    {
        nDERDataLen = DERCertLen;
        memcpy(pDERData, pDERCert, nDERDataLen);
    }
    else
    {
        nDERDataLen = 0;
    }
    pPKCS7Data = (BYTE*)malloc(PKCS7CertLen);
    if (pPKCS7Data)
    {
        nPKCS7DataLen = PKCS7CertLen;
        memcpy(pPKCS7Data, pPKCS7Cert, nPKCS7DataLen);
    }
    else
    {
        nPKCS7DataLen = 0;
    }
    FtpStoreWideText(host != NULL ? host : L"", Host);
    nRefCount = 1;
}

CCertificate::~CCertificate()
{
    if (pDERData)
        free(pDERData);
    if (pPKCS7Data)
        free(pPKCS7Data);
}

LONG CCertificate::AddRef()
{
    return InterlockedIncrement(&nRefCount);
}

LONG CCertificate::Release()
{
    LONG ret = InterlockedDecrement(&nRefCount);

    if (!ret)
        delete this;
    return ret;
}

void CCertificate::ShowCertificate(HWND hParent)
{
    ViewCertificate(hParent, pDERData, nDERDataLen, pPKCS7Data, nPKCS7DataLen, Host.c_str());
}

bool CCertificate::CheckCertificate(std::wstring& errorText)
{
    return ::CheckCertificate(pDERData, nDERDataLen, errorText, Host.c_str());
}

bool CCertificate::IsSame(BYTE* pDERCert, int DERCertLen, BYTE* pPKCS7Cert, int PKCS7CertLen)
{
    if (DERCertLen != nDERDataLen)
        return false;
    if (PKCS7CertLen != nPKCS7DataLen)
        return false;
    if (memcmp(pDERData, pDERCert, nDERDataLen))
        return false;
    return memcmp(pPKCS7Data, pPKCS7Cert, nPKCS7DataLen) ? false : true;
}
