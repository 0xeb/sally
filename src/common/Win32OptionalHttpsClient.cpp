// SPDX-FileCopyrightText: 2025-2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later
#include "Win32OptionalHttpsClient.h"
#include "update/DisplayContent.h"
#include "Win32TextCodec.h"
#include <winhttp.h>
#include <array>
#include <memory>
#include <mutex>

namespace Sally::Update
{
    namespace
    {
        struct Request
        {
            std::recursive_mutex Mutex;
            HANDLE Done = CreateEventW(nullptr, TRUE, FALSE, nullptr);
            HINTERNET Session = nullptr, Connection = nullptr, Handle = nullptr;
            HttpsSink Sink;
            HttpsFailure Failure;
            std::array<unsigned char, 8192> Buffer{};
            unsigned long long Limit = 0, Received = 0;
            bool Closing = false, Success = false;
            ~Request()
            {
                if (Connection)
                    WinHttpCloseHandle(Connection);
                if (Session)
                    WinHttpCloseHandle(Session);
                if (Done)
                    CloseHandle(Done);
            }
            void Finish(bool success, DWORD error = 0)
            {
                Success = success;
                Failure.Code = error;
                SetEvent(Done);
            }
        };
        using Context = std::shared_ptr<Request>;
        void CALLBACK Notification(HINTERNET handle, DWORD_PTR context, DWORD status, LPVOID information, DWORD length)
        {
            auto owner = reinterpret_cast<Context*>(context);
            if (!owner)
                return;
            Context request = *owner;
            if (status == WINHTTP_CALLBACK_STATUS_HANDLE_CLOSING)
            {
                delete owner;
                return;
            }
            std::lock_guard<std::recursive_mutex> lock(request->Mutex);
            if (request->Closing)
                return;
            try
            {
                if (status == WINHTTP_CALLBACK_STATUS_REQUEST_ERROR)
                {
                    request->Finish(false, static_cast<WINHTTP_ASYNC_RESULT*>(information)->dwError);
                    return;
                }
                if (status == WINHTTP_CALLBACK_STATUS_SENDREQUEST_COMPLETE)
                {
                    if (!WinHttpReceiveResponse(handle, nullptr))
                        request->Finish(false, GetLastError());
                }
                else if (status == WINHTTP_CALLBACK_STATUS_HEADERS_AVAILABLE)
                {
                    DWORD code = 0, size = sizeof(code);
                    if (!WinHttpQueryHeaders(handle, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &code, &size, WINHTTP_NO_HEADER_INDEX))
                        request->Finish(false, GetLastError());
                    else if (code < 200 || code >= 300)
                    {
                        request->Failure.Status = code;
                        request->Finish(false);
                    }
                    else if (!WinHttpReadData(handle, request->Buffer.data(), static_cast<DWORD>(request->Buffer.size()), nullptr))
                        request->Finish(false, GetLastError());
                }
                else if (status == WINHTTP_CALLBACK_STATUS_READ_COMPLETE)
                {
                    if (!length)
                    {
                        request->Finish(true);
                        return;
                    }
                    if (length > request->Limit - request->Received)
                    {
                        request->Failure.TooLarge = true;
                        request->Finish(false);
                        return;
                    }
                    request->Received += length;
                    DWORD error = 0;
                    if (!request->Sink(static_cast<const BYTE*>(information), length, error))
                    {
                        request->Finish(false, error);
                        return;
                    }
                    if (!WinHttpReadData(handle, request->Buffer.data(), static_cast<DWORD>(request->Buffer.size()), nullptr))
                        request->Finish(false, GetLastError());
                }
            }
            catch (...)
            {
                request->Finish(false, ERROR_NOT_ENOUGH_MEMORY);
            }
        }
    } // namespace
    bool Win32OptionalHttpsClient::Get(const std::wstring& url, const wchar_t* headers, unsigned long long maxBytes, const HttpsSink& sink, HttpsFailure& failure)
    {
        failure = {};
        if (Cancel || GetTickCount64() >= Deadline)
        {
            failure.Cancelled = true;
            return false;
        }
        std::string utf8;
        Win32EncodeText(CP_UTF8, url, utf8);
        if (!ValidContentLink(utf8))
            return false;
        URL_COMPONENTS parts = {sizeof(parts)};
        parts.dwHostNameLength = parts.dwUrlPathLength = parts.dwExtraInfoLength = static_cast<DWORD>(-1);
        if (!WinHttpCrackUrl(url.c_str(), 0, 0, &parts) || parts.nScheme != INTERNET_SCHEME_HTTPS)
            return false;
        auto request = std::make_shared<Request>();
        if (!request->Done)
            return false;
        request->Sink = sink;
        request->Limit = maxBytes;
        // On older unsupported systems, optional content is unavailable; package downloads retain WinINet.
        request->Session = WinHttpOpen(L"Sally Updater", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, WINHTTP_FLAG_ASYNC);
        if (!request->Session)
        {
            failure.Code = GetLastError();
            return false;
        }
        WinHttpSetTimeouts(request->Session, 3000, 3000, 3000, 3000);
        std::wstring host(parts.lpszHostName, parts.dwHostNameLength);
        request->Connection = WinHttpConnect(request->Session, host.c_str(), parts.nPort, 0);
        if (!request->Connection)
        {
            failure.Code = GetLastError();
            return false;
        }
        std::wstring path(parts.lpszUrlPath, parts.dwUrlPathLength);
        if (parts.dwExtraInfoLength)
            path.append(parts.lpszExtraInfo, parts.dwExtraInfoLength);
        request->Handle = WinHttpOpenRequest(request->Connection, L"GET", path.empty() ? L"/" : path.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
        if (!request->Handle)
        {
            failure.Code = GetLastError();
            return false;
        }
        DWORD disable = WINHTTP_DISABLE_COOKIES;
        WinHttpSetOption(request->Handle, WINHTTP_OPTION_DISABLE_FEATURE, &disable, sizeof(disable));
        DWORD redirect = WINHTTP_OPTION_REDIRECT_POLICY_DISALLOW_HTTPS_TO_HTTP;
        WinHttpSetOption(request->Handle, WINHTTP_OPTION_REDIRECT_POLICY, &redirect, sizeof(redirect));
        DWORD redirectLimit = 5;
        WinHttpSetOption(request->Handle, WINHTTP_OPTION_MAX_HTTP_AUTOMATIC_REDIRECTS, &redirectLimit, sizeof(redirectLimit));
        Context* owner = new Context(request);
        DWORD_PTR context = reinterpret_cast<DWORD_PTR>(owner);
        if (!WinHttpSetOption(request->Handle, WINHTTP_OPTION_CONTEXT_VALUE, &context, sizeof(context)) ||
            WinHttpSetStatusCallback(request->Handle, Notification, WINHTTP_CALLBACK_FLAG_ALL_COMPLETIONS | WINHTTP_CALLBACK_FLAG_HANDLES, 0) == WINHTTP_INVALID_STATUS_CALLBACK)
        {
            failure.Code = GetLastError();
            WinHttpCloseHandle(request->Handle);
            delete owner;
            return false;
        }
        {
            std::lock_guard<std::recursive_mutex> lock(request->Mutex);
            if (!WinHttpSendRequest(request->Handle, headers, headers ? static_cast<DWORD>(-1) : 0, WINHTTP_NO_REQUEST_DATA, 0, 0, context))
                request->Finish(false, GetLastError());
        }
        while (WaitForSingleObject(request->Done, 25) == WAIT_TIMEOUT)
            if (Cancel || GetTickCount64() >= Deadline)
                break;
        {
            std::lock_guard<std::recursive_mutex> lock(request->Mutex);
            bool expired = Cancel || GetTickCount64() >= Deadline;
            request->Closing = true;
            failure = request->Failure;
            failure.Cancelled = Cancel.load();
            if (expired && !failure.Cancelled)
                failure.Code = ERROR_TIMEOUT;
            HINTERNET handle = request->Handle;
            request->Handle = nullptr;
            WinHttpCloseHandle(handle); // callback context owns request until HANDLE_CLOSING
            return request->Success && !expired;
        }
    }
} // namespace Sally::Update
