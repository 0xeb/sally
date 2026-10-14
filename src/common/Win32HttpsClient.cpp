// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#include "Win32HttpsClient.h"

#include <wininet.h>

#include <vector>

namespace Sally::Update
{

namespace
{

const wchar_t* const kAgentName = L"Sally Updater";

class InternetHandle
{
public:
    explicit InternetHandle(HINTERNET handle) : Handle(handle) {}
    ~InternetHandle()
    {
        if (Handle != NULL)
            InternetCloseHandle(Handle);
    }
    InternetHandle(const InternetHandle&) = delete;
    InternetHandle& operator=(const InternetHandle&) = delete;
    HINTERNET Get() const { return Handle; }

private:
    HINTERNET Handle;
};

bool Fail(HttpsFailure& failure, DWORD code)
{
    failure.Code = code;
    failure.Text = Win32NetworkErrorText(code, 0);
    return false;
}

// The address the request ended at, after the redirects WinINet followed.
std::wstring FinalUrl(HINTERNET request)
{
    DWORD bytes = 0;
    InternetQueryOptionW(request, INTERNET_OPTION_URL, NULL, &bytes);
    if (bytes == 0)
        return std::wstring();
    std::vector<wchar_t> buffer(bytes / sizeof(wchar_t) + 1, L'\0');
    bytes = static_cast<DWORD>(buffer.size() * sizeof(wchar_t));
    if (!InternetQueryOptionW(request, INTERNET_OPTION_URL, buffer.data(), &bytes))
        return std::wstring();
    return std::wstring(buffer.data());
}

} // namespace

std::wstring Win32NetworkErrorText(DWORD error, LANGID language)
{
    wchar_t* allocated = NULL;
    DWORD flags = FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS;
    HMODULE wininet = NULL;
    if (error >= INTERNET_ERROR_BASE && error <= INTERNET_ERROR_LAST)
    {
        wininet = GetModuleHandleW(L"wininet.dll");
        if (wininet != NULL)
            flags = FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_HMODULE | FORMAT_MESSAGE_IGNORE_INSERTS;
    }
    DWORD count = FormatMessageW(flags, wininet, error, language, reinterpret_cast<wchar_t*>(&allocated), 0, NULL);
    if (count == 0 && language != 0)
        count = FormatMessageW(flags, wininet, error, 0, reinterpret_cast<wchar_t*>(&allocated), 0, NULL);
    std::wstring text = count > 0 && allocated != NULL ? std::wstring(allocated, count) : std::wstring();
    if (allocated != NULL)
        LocalFree(allocated);
    while (!text.empty() && (text.back() == L'\n' || text.back() == L'\r' || text.back() == L' '))
        text.pop_back();
    return text.empty() ? L"error " + std::to_wstring(error) : text;
}

bool Win32HttpsClient::Get(const std::wstring& url, const wchar_t* headers, unsigned long long maxBytes,
                           const HttpsSink& sink, HttpsFailure& failure)
{
    failure = HttpsFailure();
    if (!IsHttpsUrl(url))
    {
        failure.Text = L"not an HTTPS address: " + url;
        return false;
    }
    InternetHandle session(InternetOpenW(kAgentName, INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, 0));
    if (session.Get() == NULL)
        return Fail(failure, GetLastError());
    InternetHandle request(InternetOpenUrlW(session.Get(), url.c_str(), headers, headers != NULL ? (DWORD)-1 : 0,
                                            INTERNET_FLAG_SECURE | INTERNET_FLAG_RELOAD | INTERNET_FLAG_DONT_CACHE |
                                                INTERNET_FLAG_NO_CACHE_WRITE | INTERNET_FLAG_PRAGMA_NOCACHE |
                                                INTERNET_FLAG_NO_UI | INTERNET_FLAG_NO_COOKIES,
                                            0));
    if (request.Get() == NULL)
        return Fail(failure, GetLastError());
    const std::wstring landed = FinalUrl(request.Get());
    if (!landed.empty() && !IsHttpsUrl(landed))
    {
        failure.Text = L"redirected to an address that is not HTTPS: " + landed;
        return false;
    }
    DWORD status = 0;
    DWORD statusSize = sizeof(status);
    if (!HttpQueryInfoW(request.Get(), HTTP_QUERY_STATUS_CODE | HTTP_QUERY_FLAG_NUMBER, &status, &statusSize, NULL))
        return Fail(failure, GetLastError());
    if (status < 200 || status >= 300)
    {
        failure.Status = status;
        failure.Text = L"HTTP " + std::to_wstring(status);
        return false;
    }

    unsigned long long total = 0;
    std::vector<BYTE> buffer(64 * 1024);
    for (;;)
    {
        if (Cancel != nullptr && Cancel->load())
        {
            failure.Cancelled = true;
            failure.Text = L"cancelled";
            return false;
        }
        DWORD read = 0;
        if (!InternetReadFile(request.Get(), buffer.data(), static_cast<DWORD>(buffer.size()), &read))
            return Fail(failure, GetLastError());
        if (read == 0)
            return true;
        total += read;
        if (total > maxBytes)
        {
            failure.TooLarge = true;
            failure.Text = L"the download is larger than expected";
            return false;
        }
        DWORD error = 0;
        if (!sink(buffer.data(), read, error))
            return error != 0 ? Fail(failure, error) : (failure.Text = L"the download was refused", false);
    }
}

} // namespace Sally::Update
