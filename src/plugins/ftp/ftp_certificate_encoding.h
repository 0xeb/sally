// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <windows.h>
#include <openssl/x509.h>
#include <openssl/pkcs7.h>
#include <new>
#include <stdexcept>
#include <vector>
#include <utility>

struct FtpEncodedCertificates
{
    std::vector<BYTE> Leaf;
    std::vector<BYTE> Chain;
};

inline bool FtpValidCertificateBytes(const BYTE* leaf, int leafSize, const BYTE* chain, int chainSize)
{
    return leaf != nullptr && leafSize > 0 && chainSize >= 0 && (chainSize == 0 || chain != nullptr);
}

// The API is the plugin's existing dynamically loaded OpenSSL table. Keeping
// this helper free of socket/dialog state permits deterministic offline faults.
// The caller owns the peer certificate reference; chain is borrowed from SSL.
template <class OpenSslApi>
DWORD EncodeFtpCertificates(OpenSslApi& api, X509* peer, STACK_OF(X509) * chain, FtpEncodedCertificates& output)
{
    if (peer == nullptr)
        return ERROR_INVALID_DATA;
    try
    {
        FtpEncodedCertificates candidate;
        const int leafSize = api.i2d_X509(peer, nullptr);
        if (leafSize <= 0)
            return ERROR_INVALID_DATA;
        candidate.Leaf.resize(static_cast<size_t>(leafSize));
        BYTE* cursor = candidate.Leaf.data();
        if (api.i2d_X509(peer, &cursor) != leafSize || cursor != candidate.Leaf.data() + leafSize)
            return ERROR_INVALID_DATA;
        // Resumed TLS sessions may have no peer chain. A valid leaf remains
        // required; the optional chain is represented by exactly zero bytes.
        if (chain != nullptr)
        {
            PKCS7* p7 = api.PKCS7_new();
            if (p7 == nullptr)
                return ERROR_NOT_ENOUGH_MEMORY;
            struct Owner
            {
                OpenSslApi& Api;
                PKCS7* Value;
                bool Borrowed = false;
                ~Owner()
                {
                    if (Borrowed)
                        Value->d.sign->cert = nullptr;
                    Api.PKCS7_free(Value);
                }
            } owner{api, p7};
            ASN1_OBJECT* signedType = api.OBJ_nid2obj(NID_pkcs7_signed);
            if (signedType == nullptr)
                return ERROR_INVALID_DATA;
            PKCS7_SIGNED* signedData = api.PKCS7_SIGNED_new();
            if (signedData == nullptr)
                return ERROR_NOT_ENOUGH_MEMORY;
            p7->type = signedType;
            p7->d.sign = signedData;
            if (signedData->contents == nullptr || signedData->version == nullptr)
                return ERROR_INVALID_DATA;
            ASN1_OBJECT* dataType = api.OBJ_nid2obj(NID_pkcs7_data);
            if (dataType == nullptr)
                return ERROR_INVALID_DATA;
            signedData->contents->type = dataType;
            if (api.ASN1_INTEGER_set(signedData->version, 1) != 1)
                return ERROR_INVALID_DATA;
            signedData->cert = chain;
            owner.Borrowed = true;
            signedData->crl = api.sk_new_null();
            if (signedData->crl == nullptr)
                return ERROR_NOT_ENOUGH_MEMORY;
            const int chainSize = api.i2d_PKCS7(p7, nullptr);
            if (chainSize <= 0)
                return ERROR_INVALID_DATA;
            candidate.Chain.resize(static_cast<size_t>(chainSize));
            cursor = candidate.Chain.data();
            if (api.i2d_PKCS7(p7, &cursor) != chainSize || cursor != candidate.Chain.data() + chainSize)
                return ERROR_INVALID_DATA;
        }
        output = std::move(candidate);
        return ERROR_SUCCESS;
    }
    catch (const std::bad_alloc&)
    {
        return ERROR_NOT_ENOUGH_MEMORY;
    }
    catch (const std::length_error&)
    {
        return ERROR_FILE_TOO_LARGE;
    }
}
