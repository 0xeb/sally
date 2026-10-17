// SPDX-FileCopyrightText: 2025-2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later
#include "update/Win32DisplayContent.h"
#include "SallyUrls.h"
#include "Win32OptionalHttpsClient.h"
#include "Win32TextCodec.h"
#include "ExtendedLengthPath.h"
#include "supporter/SupporterState.h"
#include <sodium.h>
#include <filesystem>
#include <fstream>
#include <thread>

namespace Sally::Update
{
    namespace
    {
        std::string Read(const std::wstring& path, size_t limit)
        {
            std::ifstream stream(std::filesystem::path(Sally::ToExtendedLengthPath(path)), std::ios::binary);
            if (!stream)
                return {};
            std::string bytes(limit + 1, '\0');
            stream.read(bytes.data(), bytes.size());
            if (stream.bad() || static_cast<size_t>(stream.gcount()) > limit)
                return {};
            bytes.resize(static_cast<size_t>(stream.gcount()));
            return bytes;
        }
        std::wstring Hash(const std::wstring& value)
        {
            unsigned char bytes[crypto_hash_sha256_BYTES];
            crypto_hash_sha256(bytes, reinterpret_cast<const unsigned char*>(value.data()), value.size() * sizeof(wchar_t));
            static const wchar_t hex[] = L"0123456789abcdef";
            std::wstring result;
            for (unsigned char c : bytes)
            {
                result += hex[c >> 4];
                result += hex[c & 15];
            }
            return result;
        }
        std::string Pair(const std::string& bytes, const std::string& signature)
        {
            std::string result;
            for (size_t n : {bytes.size(), signature.size()})
                for (unsigned i = 0; i < 4; ++i)
                    result += static_cast<char>((n >> (8 * i)) & 255);
            return result + bytes + signature;
        }
        bool Unpair(const std::string& pair, std::string& bytes, std::string& signature)
        {
            if (pair.size() < 8)
                return false;
            uint32_t lengths[2]{};
            for (unsigned n = 0; n < 2; ++n)
                for (unsigned i = 0; i < 4; ++i)
                    lengths[n] |= uint32_t(static_cast<unsigned char>(pair[n * 4 + i])) << (8 * i);
            if (lengths[0] > 512 * 1024 || lengths[1] > 4096 || uint64_t(lengths[0]) + lengths[1] + 8 != pair.size())
                return false;
            bytes = pair.substr(8, lengths[0]);
            signature = pair.substr(8 + lengths[0]);
            return true;
        }
        void Cache(const std::wstring& path, const std::string& bytes, const std::string& sig)
        {
            if (path.empty())
                return;
            std::error_code error;
            std::filesystem::create_directories(std::filesystem::path(path).parent_path(), error);
            if (error)
                return;
            std::wstring temporary = path + L"." + std::to_wstring(GetCurrentProcessId()) + L"." + std::to_wstring(GetCurrentThreadId()) + L".tmp";
            std::ofstream output(std::filesystem::path(Sally::ToExtendedLengthPath(temporary)), std::ios::binary | std::ios::trunc);
            auto data = Pair(bytes, sig);
            output.write(data.data(), data.size());
            output.close();
            if (output)
                MoveFileExW(Sally::ToExtendedLengthPath(temporary).c_str(), Sally::ToExtendedLengthPath(path).c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
            DeleteFileW(Sally::ToExtendedLengthPath(temporary).c_str());
        }
    } // namespace
    bool ReadBundledDisplayContent(const std::wstring& install, const std::string& version, DisplayContent& result)
    {
        return ParseReleaseMetadata(Read(install + L"\\doc\\release-metadata.json", 512 * 1024), version, result);
    }
    std::wstring DisplayWindowIdentity(const std::wstring& install, const std::wstring& profile, bool offline)
    {
        std::error_code error;
        auto canonical = std::filesystem::canonical(install, error).wstring();
        if (error)
            canonical = install;
        std::wstring value = canonical + L"\n" + profile + (offline ? L"\nwhats-new" : L"\ncheck");
        int needed = LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_UPPERCASE, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr, 0);
        if (needed > 0)
        {
            std::wstring upper(needed, L'\0');
            if (LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_UPPERCASE, value.data(), static_cast<int>(value.size()), upper.data(), needed, nullptr, nullptr, 0))
                value.swap(upper);
        }
        return Hash(value);
    }
    void DisplayContentSession::Start(HWND window, UINT message, const std::string& version, const std::wstring& metadataUrl,
                                      const std::wstring& signatureUrl, const std::wstring& cacheRoot)
    {
        ULONGLONG deadline = GetTickCount64() + 10000;
        auto self = shared_from_this();
        auto launch = [=](bool live, const std::wstring& url, const std::wstring& sigUrl)
        {
            if (url.empty() || sigUrl.empty())
                return;
            try
            {
                std::thread([=]
                            {
                try
                {
                    std::wstring wideVersion(version.begin(), version.end());
                    std::wstring cache = cacheRoot.empty() ? std::wstring() : cacheRoot + L"\\display-content\\" + Hash(url + L"\n" + sigUrl + L"\n" + wideVersion) + L".cache";
                    auto accept = [&](const std::string& bytes, const std::string& sig)
                    {
                        DisplayContent model;
                        if (self->Cancel || !VerifyContent(bytes, sig, Supporter::BuiltinPublicKeys()) ||
                            !(live ? ParseLiveMessages(bytes, Supporter::NowUtc(), model) : ParseReleaseMetadata(bytes, version, model))) return false;
                        {
                            std::lock_guard<std::mutex> lock(self->Mutex);
                            if (self->Cancel) return false;
                            if (live) { self->Live = std::move(model); self->HasLive = true; }
                            else { self->Release = std::move(model); self->HasRelease = true; }
                            ++self->Revision;
                        }
                        PostMessageW(window, message, 0, 0); return true;
                    };
                    std::string bytes, sig;
                    if (!cache.empty() && Unpair(Read(cache, 512 * 1024 + 4096 + 8), bytes, sig)) accept(bytes, sig);
                    Win32OptionalHttpsClient client(self->Cancel, deadline);
                    auto fetch = [&](const std::wstring& address, size_t limit, std::string& body)
                    {
                        body.clear(); HttpsFailure failure;
                        return client.Get(address, L"Cache-Control: no-cache\r\n", limit,
                            [&](const BYTE* data, DWORD count, DWORD&) { body.append(reinterpret_cast<const char*>(data), count); return true; }, failure);
                    };
                    if (fetch(url, 512 * 1024, bytes) && fetch(sigUrl, 4096, sig) && accept(bytes, sig)) Cache(cache, bytes, sig);
                }
                catch (...) { /* Optional content never breaks update actions. */ } })
                    .detach();
            }
            catch (...)
            { /* Thread exhaustion is an optional-content failure. */
            }
        };
        launch(false, metadataUrl, signatureUrl);
        launch(true, Sally::Urls::LiveMessages, Sally::Urls::LiveMessagesSignature);
    }
} // namespace Sally::Update
