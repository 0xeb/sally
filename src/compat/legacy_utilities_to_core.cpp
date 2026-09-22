// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

// legacy_utilities_to_core — frozen v107 General utility children over the
// live interfaces.
//
// Never include precomp.h here. The frozen sdk107 types and the live SDK must
// coexist in this translation unit; precomp.h would defeat that isolation.

#define NOMINMAX
#include <windows.h>

#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <string>
#include <type_traits>

#include "compat/legacy_to_core.h"

#include "compat/legacy_convert.h"

namespace sally::compat
{
    namespace
    {

        static_assert(std::is_standard_layout_v<sdk107::CSalZLIB>);
        static_assert(std::is_standard_layout_v<::CSalZLIB>);
        static_assert(sizeof(sdk107::CSalZLIB) == sizeof(::CSalZLIB));
        static_assert(alignof(sdk107::CSalZLIB) == alignof(::CSalZLIB));
        static_assert(offsetof(sdk107::CSalZLIB, next_in) == offsetof(::CSalZLIB, next_in));
        static_assert(offsetof(sdk107::CSalZLIB, avail_in) == offsetof(::CSalZLIB, avail_in));
        static_assert(offsetof(sdk107::CSalZLIB, total_in) == offsetof(::CSalZLIB, total_in));
        static_assert(offsetof(sdk107::CSalZLIB, next_out) == offsetof(::CSalZLIB, next_out));
        static_assert(offsetof(sdk107::CSalZLIB, avail_out) == offsetof(::CSalZLIB, avail_out));
        static_assert(offsetof(sdk107::CSalZLIB, total_out) == offsetof(::CSalZLIB, total_out));
        static_assert(offsetof(sdk107::CSalZLIB, internal) == offsetof(::CSalZLIB, internal));

        static_assert(std::is_standard_layout_v<sdk107::CSalBZIP2>);
        static_assert(std::is_standard_layout_v<::CSalBZIP2>);
        static_assert(sizeof(sdk107::CSalBZIP2) == sizeof(::CSalBZIP2));
        static_assert(alignof(sdk107::CSalBZIP2) == alignof(::CSalBZIP2));
        static_assert(offsetof(sdk107::CSalBZIP2, next_in) == offsetof(::CSalBZIP2, next_in));
        static_assert(offsetof(sdk107::CSalBZIP2, avail_in) == offsetof(::CSalBZIP2, avail_in));
        static_assert(offsetof(sdk107::CSalBZIP2, total_in) == offsetof(::CSalBZIP2, total_in));
        static_assert(offsetof(sdk107::CSalBZIP2, next_out) == offsetof(::CSalBZIP2, next_out));
        static_assert(offsetof(sdk107::CSalBZIP2, avail_out) == offsetof(::CSalBZIP2, avail_out));
        static_assert(offsetof(sdk107::CSalBZIP2, total_out) == offsetof(::CSalBZIP2, total_out));
        static_assert(offsetof(sdk107::CSalBZIP2, internal) == offsetof(::CSalBZIP2, internal));
        static_assert(sizeof(sdk107::CQuadWord) == sizeof(::CQuadWord));
        static_assert(alignof(sdk107::CQuadWord) == alignof(::CQuadWord));
        static_assert(offsetof(sdk107::CQuadWord, LoDWord) == offsetof(::CQuadWord, LoDWord));
        static_assert(offsetof(sdk107::CQuadWord, HiDWord) == offsetof(::CQuadWord, HiDWord));

        static_assert(std::is_standard_layout_v<sdk107::CSalAES>);
        static_assert(std::is_standard_layout_v<::CSalAES>);
        static_assert(sizeof(sdk107::CSalAES) == sizeof(::CSalAES));
        static_assert(alignof(sdk107::CSalAES) == alignof(::CSalAES));
        static_assert(offsetof(sdk107::CSalAES, nonce) == offsetof(::CSalAES, nonce));
        static_assert(offsetof(sdk107::CSalAES, encr_bfr) == offsetof(::CSalAES, encr_bfr));
        static_assert(offsetof(sdk107::CSalAES, encr_ctx) == offsetof(::CSalAES, encr_ctx));
        static_assert(offsetof(sdk107::CSalAES, auth_ctx) == offsetof(::CSalAES, auth_ctx));
        static_assert(offsetof(sdk107::CSalAES, encr_pos) == offsetof(::CSalAES, encr_pos));
        static_assert(offsetof(sdk107::CSalAES, pwd_len) == offsetof(::CSalAES, pwd_len));
        static_assert(offsetof(sdk107::CSalAES, mode) == offsetof(::CSalAES, mode));

        static_assert(std::is_standard_layout_v<sdk107::CSalSHA1>);
        static_assert(std::is_standard_layout_v<::CSalSHA1>);
        static_assert(sizeof(sdk107::CSalSHA1) == sizeof(::CSalSHA1));
        static_assert(alignof(sdk107::CSalSHA1) == alignof(::CSalSHA1));
        static_assert(offsetof(sdk107::CSalSHA1, state) == offsetof(::CSalSHA1, state));
        static_assert(offsetof(sdk107::CSalSHA1, count) == offsetof(::CSalSHA1, count));
        static_assert(offsetof(sdk107::CSalSHA1, buffer) == offsetof(::CSalSHA1, buffer));

        ::CSalZLIB* LiveContext(sdk107::CSalZLIB* context)
        {
            return reinterpret_cast<::CSalZLIB*>(context);
        }

        ::CSalBZIP2* LiveContext(sdk107::CSalBZIP2* context)
        {
            return reinterpret_cast<::CSalBZIP2*>(context);
        }

        ::CSalAES* LiveContext(sdk107::CSalAES* context)
        {
            return reinterpret_cast<::CSalAES*>(context);
        }

        ::CSalSHA1* LiveContext(sdk107::CSalSHA1* context)
        {
            return reinterpret_cast<::CSalSHA1*>(context);
        }

    } // namespace

    CLegacySalamanderZLIB::CLegacySalamanderZLIB(::CSalamanderZLIBAbstract& wideZLIB)
        : WideZLIB(wideZLIB)
    {
    }

    int WINAPI CLegacySalamanderZLIB::DeflateInit(sdk107::CSalZLIB* zlibInfo,
                                                  int compressLevel)
    {
        return WideZLIB.DeflateInit(LiveContext(zlibInfo), compressLevel);
    }

    int WINAPI CLegacySalamanderZLIB::Deflate(sdk107::CSalZLIB* zlibInfo, int flush)
    {
        return WideZLIB.Deflate(LiveContext(zlibInfo), flush);
    }

    int WINAPI CLegacySalamanderZLIB::DeflateEnd(sdk107::CSalZLIB* zlibInfo)
    {
        return WideZLIB.DeflateEnd(LiveContext(zlibInfo));
    }

    int WINAPI CLegacySalamanderZLIB::InflateInit(sdk107::CSalZLIB* zlibInfo)
    {
        return WideZLIB.InflateInit(LiveContext(zlibInfo));
    }

    int WINAPI CLegacySalamanderZLIB::Inflate(sdk107::CSalZLIB* zlibInfo, int flush)
    {
        return WideZLIB.Inflate(LiveContext(zlibInfo), flush);
    }

    int WINAPI CLegacySalamanderZLIB::InflateEnd(sdk107::CSalZLIB* zlibInfo)
    {
        return WideZLIB.InflateEnd(LiveContext(zlibInfo));
    }

    int WINAPI CLegacySalamanderZLIB::InflateInit2(sdk107::CSalZLIB* zlibInfo,
                                                   int windowBits)
    {
        return WideZLIB.InflateInit2(LiveContext(zlibInfo), windowBits);
    }

    CLegacySalamanderBZIP2::CLegacySalamanderBZIP2(
        ::CSalamanderBZIP2Abstract& wideBZIP2)
        : WideBZIP2(wideBZIP2)
    {
    }

    int WINAPI CLegacySalamanderBZIP2::CompressInit(sdk107::CSalBZIP2* bzip2Info,
                                                    int blockSize100k,
                                                    int workFactor)
    {
        return WideBZIP2.CompressInit(LiveContext(bzip2Info), blockSize100k, workFactor);
    }

    int WINAPI CLegacySalamanderBZIP2::Compress(sdk107::CSalBZIP2* bzip2Info,
                                                int action)
    {
        return WideBZIP2.Compress(LiveContext(bzip2Info), action);
    }

    int WINAPI CLegacySalamanderBZIP2::CompressEnd(sdk107::CSalBZIP2* bzip2Info)
    {
        return WideBZIP2.CompressEnd(LiveContext(bzip2Info));
    }

    int WINAPI CLegacySalamanderBZIP2::DecompressInit(
        sdk107::CSalBZIP2* bzip2Info, BOOL conserveMemory)
    {
        return WideBZIP2.DecompressInit(LiveContext(bzip2Info), conserveMemory);
    }

    int WINAPI CLegacySalamanderBZIP2::Decompress(sdk107::CSalBZIP2* bzip2Info)
    {
        return WideBZIP2.Decompress(LiveContext(bzip2Info));
    }

    int WINAPI CLegacySalamanderBZIP2::DecompressEnd(sdk107::CSalBZIP2* bzip2Info)
    {
        return WideBZIP2.DecompressEnd(LiveContext(bzip2Info));
    }

    CLegacySalamanderCrypt::CLegacySalamanderCrypt(
        ::CSalamanderCryptAbstract& wideCrypt)
        : WideCrypt(wideCrypt)
    {
    }

    int WINAPI CLegacySalamanderCrypt::AESInit(sdk107::CSalAES* aes, int mode,
                                               LPCSTR password,
                                               size_t passwordLength, LPBYTE salt,
                                               LPWORD passwordVerifier)
    {
        return WideCrypt.AESInit(LiveContext(aes), mode, password, passwordLength,
                                 salt, passwordVerifier);
    }

    void WINAPI CLegacySalamanderCrypt::AESEncrypt(sdk107::CSalAES* aes, LPVOID data,
                                                   size_t dataLength)
    {
        WideCrypt.AESEncrypt(LiveContext(aes), data, dataLength);
    }

    void WINAPI CLegacySalamanderCrypt::AESDecrypt(sdk107::CSalAES* aes, LPVOID data,
                                                   size_t dataLength)
    {
        WideCrypt.AESDecrypt(LiveContext(aes), data, dataLength);
    }

    void WINAPI CLegacySalamanderCrypt::AESEnd(sdk107::CSalAES* aes, LPBYTE mac,
                                               LPDWORD macLength)
    {
        WideCrypt.AESEnd(LiveContext(aes), mac, macLength);
    }

    void WINAPI CLegacySalamanderCrypt::SHA1Init(sdk107::CSalSHA1* sha1)
    {
        WideCrypt.SHA1Init(LiveContext(sha1));
    }

    void WINAPI CLegacySalamanderCrypt::SHA1Update(sdk107::CSalSHA1* sha1,
                                                   const LPBYTE data,
                                                   size_t dataLength)
    {
        WideCrypt.SHA1Update(LiveContext(sha1), data, dataLength);
    }

    void WINAPI CLegacySalamanderCrypt::SHA1Final(sdk107::CSalSHA1* sha1,
                                                  BYTE digest[20])
    {
        WideCrypt.SHA1Final(LiveContext(sha1), digest);
    }

    CLegacySalamanderPNG::CLegacySalamanderPNG(::CSalamanderPNGAbstract& widePNG)
        : WidePNG(widePNG)
    {
    }

    HBITMAP WINAPI CLegacySalamanderPNG::LoadPNGBitmap(
        HINSTANCE instance, sdk107::LPCTSTR bitmapName, DWORD flags, COLORREF unused)
    {
        LPCWSTR liveName = nullptr;
        std::wstring wideName;
        if (bitmapName != nullptr)
        {
            if (IS_INTRESOURCE(bitmapName))
                liveName = reinterpret_cast<LPCWSTR>(bitmapName);
            else
            {
                if (!WidenPluginText(
                        reinterpret_cast<const char*>(bitmapName), wideName))
                    return nullptr;
                liveName = wideName.c_str();
            }
        }
        return WidePNG.LoadPNGBitmap(instance, liveName, flags, unused);
    }

    HBITMAP WINAPI CLegacySalamanderPNG::LoadRawPNGBitmap(
        const void* rawPNG, DWORD rawPNGSize, DWORD flags, COLORREF unused)
    {
        return WidePNG.LoadRawPNGBitmap(rawPNG, rawPNGSize, flags, unused);
    }

    CLegacySalamanderPasswordManager::CLegacySalamanderPasswordManager(
        ::CSalamanderPasswordManagerAbstract& widePasswordManager)
        : WidePasswordManager(widePasswordManager)
    {
    }

    BOOL WINAPI CLegacySalamanderPasswordManager::IsUsingMasterPassword()
    {
        return WidePasswordManager.IsUsingMasterPassword();
    }

    BOOL WINAPI CLegacySalamanderPasswordManager::IsMasterPasswordSet()
    {
        return WidePasswordManager.IsMasterPasswordSet();
    }

    BOOL WINAPI CLegacySalamanderPasswordManager::AskForMasterPassword(HWND parent)
    {
        return WidePasswordManager.AskForMasterPassword(parent);
    }

    BOOL WINAPI CLegacySalamanderPasswordManager::EncryptPassword(
        const char* plainPassword, BYTE** encryptedPassword,
        int* encryptedPasswordSize, BOOL encrypt)
    {
        if (plainPassword == nullptr)
            return FALSE;
        std::wstring plainPasswordW;
        if (!WidenPluginText(plainPassword, plainPasswordW))
            return FALSE;
        const BOOL result = WidePasswordManager.EncryptPassword(
            plainPasswordW.c_str(), encryptedPassword, encryptedPasswordSize, encrypt);
        if (!plainPasswordW.empty())
            SecureZeroMemory(plainPasswordW.data(),
                             plainPasswordW.size() * sizeof(wchar_t));
        return result;
    }

    BOOL WINAPI CLegacySalamanderPasswordManager::DecryptPassword(
        const BYTE* encryptedPassword, int encryptedPasswordSize,
        char** plainPassword)
    {
        if (plainPassword == nullptr)
            return WidePasswordManager.DecryptPassword(
                encryptedPassword, encryptedPasswordSize, nullptr);

        *plainPassword = nullptr;
        CSalamanderStringBufferOwner owner;
        if (!owner.IsValid() ||
            !WidePasswordManager.DecryptPassword(
                encryptedPassword, encryptedPasswordSize, owner.Buffer()))
            return FALSE;

        std::wstring plainPasswordW;
        if (!owner.GetValue(plainPasswordW))
            return FALSE;
        NarrowResult narrow = NarrowExact(plainPasswordW);
        if (owner.Buffer() != nullptr && owner.Buffer()->Data != nullptr)
            SecureZeroMemory(owner.Buffer()->Data,
                             owner.Buffer()->Capacity * sizeof(wchar_t));
        if (!plainPasswordW.empty())
            SecureZeroMemory(plainPasswordW.data(),
                             plainPasswordW.size() * sizeof(wchar_t));
        if (!narrow.ok)
            return FALSE;

        char* allocated = static_cast<char*>(std::malloc(narrow.value.size() + 1));
        if (allocated == nullptr)
        {
            if (!narrow.value.empty())
                SecureZeroMemory(narrow.value.data(), narrow.value.size());
            SetLastError(ERROR_NOT_ENOUGH_MEMORY);
            return FALSE;
        }
        std::memcpy(allocated, narrow.value.c_str(), narrow.value.size() + 1);
        if (!narrow.value.empty())
            SecureZeroMemory(narrow.value.data(), narrow.value.size());
        *plainPassword = allocated;
        return TRUE;
    }

    BOOL WINAPI CLegacySalamanderPasswordManager::IsPasswordEncrypted(
        const BYTE* encryptedPassword, int encryptedPasswordSize)
    {
        return WidePasswordManager.IsPasswordEncrypted(encryptedPassword,
                                                       encryptedPasswordSize);
    }

} // namespace sally::compat
