// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"

#include <iostream>
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#include <winsock2.h>
#include <charconv>
#include <limits>
#include <string>
#include <string_view>

#include "salmon_unicode.h"

using namespace std;

const char* SERVER_NAME = ""; // Dead server — bug report upload disabled

BOOL CreateHTTPOutput(CUploadParams* uploadParams, char** buffer, int* bufferSize)
{
    *buffer = NULL;
    *bufferSize = 0;
    uploadParams->ErrorMessage.clear();

    const wchar_t* p = wcsrchr(uploadParams->FileName.c_str(), L'\\');
    if (p == NULL)
        p = uploadParams->FileName.c_str();
    else
        p++;
    std::string fileNameOnly;
    if (!sally::salmon::EncodeUtf8(p, fileNameOnly))
    {
        uploadParams->ErrorMessage = L"The archive name is not valid Unicode.";
        return FALSE;
    }

    HANDLE hFile = CreateFileW(uploadParams->FileName.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile != INVALID_HANDLE_VALUE)
    {
        LARGE_INTEGER fileSize = {};
        if (GetFileSizeEx(hFile, &fileSize) && fileSize.QuadPart >= 0 &&
            static_cast<unsigned long long>(fileSize.QuadPart) <= (std::numeric_limits<DWORD>::max)())
        {
            std::string fileBytes(static_cast<size_t>(fileSize.QuadPart), '\0');
            size_t readOffset = 0;
            while (readOffset < fileBytes.size())
            {
                DWORD bytesRead = 0;
                const DWORD remaining = static_cast<DWORD>(fileBytes.size() - readOffset);
                if (!ReadFile(hFile, fileBytes.data() + readOffset, remaining, &bytesRead, NULL) || bytesRead == 0)
                    break;
                readOffset += bytesRead;
            }
            if (readOffset == fileBytes.size())
            {
                static constexpr char boundary[] = "---------------------------90721038027008";
                std::string body = "--";
                body += boundary;
                body += "\r\nContent-Disposition: form-data; name=\"altapfile\"; filename=\"";
                body += fileNameOnly;
                body += "\"\r\nContent-Type: application/octet-stream\r\n\r\n";
                body += fileBytes;
                body += "\r\n--";
                body += boundary;
                body += "--\r\n";

                std::string request = "POST /upload.php HTTP/1.1\r\nHost: ";
                request += SERVER_NAME;
                request += "\r\nConnection: Keep-Alive\r\nContent-Type: multipart/form-data; boundary=";
                request += boundary;
                request += "\r\nContent-Length: ";
                request += std::to_string(body.size());
                request += "\r\n\r\n";
                request += body;
                if (request.size() <= INT_MAX)
                {
                    char* result = (char*)malloc(request.size());
                    if (result != NULL)
                    {
                        memcpy(result, request.data(), request.size());
                        *buffer = result;
                        *bufferSize = static_cast<int>(request.size());
                        CloseHandle(hFile);
                        return TRUE;
                    }
                    uploadParams->ErrorMessage = FormatText(LoadStr(IDS_SALMON_OUT_OF_MEMORY, HLanguage).c_str());
                }
                else
                    uploadParams->ErrorMessage = FormatText(LoadStr(IDS_SALMON_FILE_SIZE, HLanguage).c_str(), uploadParams->FileName.c_str());
            }
            else
                uploadParams->ErrorMessage = FormatText(LoadStr(IDS_SALMON_READING_FILE, HLanguage).c_str(), uploadParams->FileName.c_str());
        }
        else
            uploadParams->ErrorMessage = FormatText(LoadStr(IDS_SALMON_FILE_SIZE, HLanguage).c_str(), uploadParams->FileName.c_str());

        CloseHandle(hFile);
    }
    else
        uploadParams->ErrorMessage = FormatText(LoadStr(IDS_SALMON_FILE_OPEN, HLanguage).c_str(), uploadParams->FileName.c_str());
    return FALSE;
}

// taken from PHP at http://php.net/manual/en/features.file-upload.errors.php
#define UPLOAD_ERR_OK 0
#define UPLOAD_ERR_INI_SIZE 1
#define UPLOAD_ERR_FORM_SIZE 2
#define UPLOAD_ERR_PARTIAL 3
#define UPLOAD_ERR_NO_FILE 4
#define UPLOAD_ERR_NO_TMP_DIR 5
#define UPLOAD_ERR_CANT_WRITE 6
#define UPLOAD_ERR_EXTENSION 7

BOOL GetFilesError(int err, CUploadParams* uploadParams)
{
    uploadParams->ErrorMessage.clear();
    switch (err)
    {
    case UPLOAD_ERR_OK:
    {
        return TRUE;
    }

    case UPLOAD_ERR_INI_SIZE:
    case UPLOAD_ERR_FORM_SIZE:
    {
        uploadParams->ErrorMessage = FormatText(LoadStr(IDS_SALMON_ERR_INI_SIZE, HLanguage).c_str(), err);
        break;
    }

    case UPLOAD_ERR_PARTIAL:
    {
        uploadParams->ErrorMessage = FormatText(LoadStr(IDS_SALMON_ERR_PARTIAL, HLanguage).c_str(), err);
        break;
    }

    case UPLOAD_ERR_NO_FILE:
    {
        uploadParams->ErrorMessage = FormatText(LoadStr(IDS_SALMON_ERR_NO_FILE, HLanguage).c_str(), err);
        break;
    }

    case UPLOAD_ERR_NO_TMP_DIR:
    {
        uploadParams->ErrorMessage = FormatText(LoadStr(IDS_SALMON_ERR_NO_TMP_DIR, HLanguage).c_str(), err);
        break;
    }

    case UPLOAD_ERR_CANT_WRITE:
    {
        uploadParams->ErrorMessage = FormatText(LoadStr(IDS_SALMON_ERR_CANT_WRITE, HLanguage).c_str(), err);
        break;
    }

    case UPLOAD_ERR_EXTENSION:
    {
        uploadParams->ErrorMessage = FormatText(LoadStr(IDS_SALMON_ERR_EXTENSION, HLanguage).c_str(), err);
        break;
    }

    default:
    {
        uploadParams->ErrorMessage = FormatText(LoadStr(IDS_SALMON_ERR_UNKNOWN, HLanguage).c_str(), err);
        break;
    }
    }
    return FALSE;
}

BOOL AnalyzeResponse(const char* str, size_t strLen, CUploadParams* uploadParams)
{
    // find our response from the PHP script in the form <response>X</response>, where X is
    // an error from http://php.net/manual/en/features.file-upload.errors.php
    const std::string_view response(str, strLen);
    static constexpr std::string_view TAG_OPEN = "<response>";
    static constexpr std::string_view TAG_CLOSE = "</response>";
    const size_t tagOpen = response.find(TAG_OPEN);
    if (tagOpen != std::string_view::npos)
    {
        const size_t numBegin = tagOpen + TAG_OPEN.size();
        size_t numEnd = numBegin;
        while (numEnd < response.size() && response[numEnd] >= '0' && response[numEnd] <= '9' &&
               numEnd - numBegin < 10)
            ++numEnd;
        if (numEnd > numBegin)
        {
            if (response.substr(numEnd, TAG_CLOSE.size()) == TAG_CLOSE)
            {
                int number = 0;
                const auto result = std::from_chars(response.data() + numBegin,
                                                    response.data() + numEnd, number);
                if (result.ec == std::errc())
                    return GetFilesError(number, uploadParams);
                uploadParams->ErrorMessage = FormatText(LoadStr(IDS_SALMON_SYNTAX_ERROR_VALUE, HLanguage).c_str());
            }
            else
                uploadParams->ErrorMessage = FormatText(LoadStr(IDS_SALMON_SYNTAX_ERROR_CLOSE, HLanguage).c_str());
        }
        else
            uploadParams->ErrorMessage = FormatText(LoadStr(IDS_SALMON_SYNTAX_ERROR_VALUE, HLanguage).c_str());
    }
    else
        uploadParams->ErrorMessage = FormatText(LoadStr(IDS_SALMON_SYNTAX_ERROR_OPEN, HLanguage).c_str());
    return FALSE;
}

DWORD WINAPI UploadThreadF(void* param)
{
    CUploadParams* uploadParams = (CUploadParams*)param;
    uploadParams->Result = FALSE;
    char* buffer = NULL;

    try
    {
        int bufferSize;
        if (CreateHTTPOutput(uploadParams, &buffer, &bufferSize))
        {
            // initialize Winsock
            WSADATA wsaData = {0};
            int iResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
            if (iResult == 0)
            {
                // resolve host name
                struct hostent* remoteHost = gethostbyname(SERVER_NAME);
                if (remoteHost != NULL)
                {
                    struct sockaddr_in addr;
                    addr.sin_family = AF_INET;
                    addr.sin_port = htons(80);
                    addr.sin_addr.s_addr = *(unsigned long*)remoteHost->h_addr;

                    SOCKET connectSocket = socket(AF_INET, SOCK_STREAM, 0);
                    if (connectSocket != INVALID_SOCKET)
                    {
                        iResult = connect(connectSocket, (SOCKADDR*)&addr, sizeof(addr));
                        if (iResult != SOCKET_ERROR)
                        {
                            int bytesSent = 0;
                            while (bytesSent < bufferSize)
                            {
                                iResult = send(connectSocket, buffer + bytesSent, bufferSize - bytesSent, 0);
                                if (iResult == SOCKET_ERROR || iResult == 0)
                                    break;
                                bytesSent += iResult;
                            }
                            if (bytesSent == bufferSize)
                            {
                                // shutdown the connection since no more data will be sent
                                iResult = shutdown(connectSocket, SD_SEND);
                                if (iResult != SOCKET_ERROR)
                                {
                                    // Receive until the peer closes the connection.
                                    std::string response;
                                    char recvbuf[4096];
                                    do
                                    {
                                        iResult = recv(connectSocket, recvbuf, sizeof(recvbuf), 0);
                                        if (iResult > 0)
                                            response.append(recvbuf, iResult);
                                        else if (iResult == 0)
                                            uploadParams->Result = AnalyzeResponse(response.data(), response.size(), uploadParams);
                                        else
                                            uploadParams->ErrorMessage = FormatText(LoadStr(IDS_SALMON_SOCK_ERR_RECV, HLanguage).c_str(), WSAGetLastError());
                                    } while (iResult > 0);
                                }
                                else
                                    uploadParams->ErrorMessage = FormatText(LoadStr(IDS_SALMON_SOCK_ERR_SHUTDOWN, HLanguage).c_str(), WSAGetLastError());
                            }
                            else
                                uploadParams->ErrorMessage = FormatText(LoadStr(IDS_SALMON_SOCK_ERR_SEND, HLanguage).c_str(), WSAGetLastError());
                        }
                        else
                            uploadParams->ErrorMessage = FormatText(LoadStr(IDS_SALMON_SOCK_ERR_CONNECT, HLanguage).c_str(), WSAGetLastError());

                        closesocket(connectSocket);
                    }
                    else
                        uploadParams->ErrorMessage = FormatText(LoadStr(IDS_SALMON_SOCK_ERR_SOCKET, HLanguage).c_str(), WSAGetLastError());
                }
                else
                    uploadParams->ErrorMessage = FormatText(LoadStr(IDS_SALMON_SOCK_ERR_HOST, HLanguage).c_str(), WSAGetLastError());

                WSACleanup();
            }
            else
                uploadParams->ErrorMessage = FormatText(LoadStr(IDS_SALMON_SOCK_ERR_INIT, HLanguage).c_str(), iResult);
        }
    }
    catch (const std::bad_alloc&)
    {
        try { uploadParams->ErrorMessage = L"Not enough memory to prepare or receive the bug report upload."; }
        catch (...) {}
    }
    catch (...)
    {
        try { uploadParams->ErrorMessage = L"Unexpected failure while preparing or sending the bug report upload."; }
        catch (...) {}
    }
    free(buffer);
    return EXIT_SUCCESS;
}

HANDLE HUploadThread = NULL;

BOOL StartUploadThread(CUploadParams* params)
{
    if (HUploadThread != NULL)
        return FALSE;
    DWORD id;
    HUploadThread = CreateThread(NULL, 0, UploadThreadF, params, 0, &id);
    return HUploadThread != NULL;
}

BOOL IsUploadThreadRunning()
{
    if (HUploadThread == NULL)
        return FALSE;
    DWORD res = WaitForSingleObject(HUploadThread, 0);
    if (res != WAIT_TIMEOUT)
    {
        CloseHandle(HUploadThread);
        HUploadThread = NULL;
        return FALSE;
    }
    return TRUE;
}
