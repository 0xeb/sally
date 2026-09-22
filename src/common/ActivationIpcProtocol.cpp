// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#ifdef SALLY_ACTIVATION_IPC_STANDALONE
#define NOMINMAX
#include <windows.h>
#else
#include "precomp.h"
#endif

#include "common/ActivationIpcProtocol.h"

#include <cstdio>
#include <cstring>
#include <limits>
#include <new>
#include <utility>

namespace
{
using sally::cmdline::CommandLineRequest;

bool AddFieldSize(const std::wstring& text, std::size_t& total)
{
    if (text.empty())
        return true;
    if (text.size() > (std::numeric_limits<DWORD>::max)())
        return false;
    const std::size_t chars = text.size() + 1;
    if (chars > ((std::numeric_limits<DWORD>::max)() - total) / sizeof(wchar_t))
        return false;
    total += chars * sizeof(wchar_t);
    return true;
}

void WriteField(std::vector<BYTE>& payload, CActivationPayloadField& field,
                const std::wstring& text, std::size_t& cursor)
{
    if (text.empty())
    {
        field.Offset = 0;
        field.LengthChars = 0;
        return;
    }
    field.Offset = static_cast<DWORD>(cursor);
    field.LengthChars = static_cast<DWORD>(text.size());
    const std::size_t bytes = (text.size() + 1) * sizeof(wchar_t);
    std::memcpy(payload.data() + cursor, text.c_str(), bytes);
    cursor += bytes;
}

bool ReadField(const BYTE* bytes, std::size_t total,
               const CActivationPayloadField& field, std::size_t& cursor,
               std::wstring& output)
{
    if (field.LengthChars == 0)
    {
        if (field.Offset != 0)
            return false;
        output.clear();
        return true;
    }
    if (field.Offset != cursor || (field.Offset & 1u) != 0)
        return false;
    const std::size_t chars = static_cast<std::size_t>(field.LengthChars) + 1;
    if (chars > ((std::numeric_limits<std::size_t>::max)() / sizeof(wchar_t)))
        return false;
    const std::size_t fieldBytes = chars * sizeof(wchar_t);
    if (cursor > total || fieldBytes > total - cursor)
        return false;

    const wchar_t* text = reinterpret_cast<const wchar_t*>(bytes + cursor);
    if (text[field.LengthChars] != L'\0')
        return false;
    for (DWORD i = 0; i < field.LengthChars; ++i)
        if (text[i] == L'\0')
            return false;
    output.assign(text, field.LengthChars);
    cursor += fieldBytes;
    return true;
}
} // namespace

bool BuildActivationPayload(const CommandLineRequest& request,
                            DWORD requestUID, DWORD generation, DWORD senderPID,
                            std::vector<BYTE>& payload)
{
    if (requestUID == 0 || generation == 0 || senderPID == 0 ||
        request.activatePanel > 2 ||
        (request.setMainWindowIconIndex && request.mainWindowIconIndex > 3) ||
        (!request.setMainWindowIconIndex && request.mainWindowIconIndex != 0) ||
        request.setTitlePrefix != !request.titlePrefix.empty())
    {
        return false;
    }

    std::size_t total = sizeof(CActivationPayloadHeader);
    if (!AddFieldSize(request.leftPath, total) ||
        !AddFieldSize(request.rightPath, total) ||
        !AddFieldSize(request.activePath, total) ||
        !AddFieldSize(request.titlePrefix, total) ||
        total > (std::numeric_limits<DWORD>::max)())
    {
        return false;
    }

    try
    {
        std::vector<BYTE> built(total, 0);
        CActivationPayloadHeader* header =
            reinterpret_cast<CActivationPayloadHeader*>(built.data());
        header->Magic = SALLY_ACTIVATION_PROTOCOL_MAGIC;
        header->Version = SALLY_ACTIVATION_PROTOCOL_VERSION;
        header->HeaderSize = sizeof(*header);
        header->TotalSize = static_cast<DWORD>(total);
        header->RequestUID = requestUID;
        header->Generation = generation;
        header->SenderPID = senderPID;
        header->ActivatePanel = request.activatePanel;
        header->Flags = (request.setTitlePrefix ? SALLY_ACTIVATION_FLAG_SET_TITLE : 0) |
                        (request.setMainWindowIconIndex ? SALLY_ACTIVATION_FLAG_SET_ICON : 0);
        header->MainWindowIconIndex = request.mainWindowIconIndex;

        std::size_t cursor = sizeof(*header);
        WriteField(built, header->LeftPath, request.leftPath, cursor);
        WriteField(built, header->RightPath, request.rightPath, cursor);
        WriteField(built, header->ActivePath, request.activePath, cursor);
        WriteField(built, header->TitlePrefix, request.titlePrefix, cursor);
        if (cursor != total)
            return false;
        payload.swap(built);
        return true;
    }
    catch (const std::bad_alloc&)
    {
        return false;
    }
}

bool ParseActivationPayload(const void* payload, std::size_t payloadBytes,
                            DWORD expectedRequestUID, DWORD expectedGeneration,
                            DWORD expectedSenderPID, CommandLineRequest& request)
{
    if (payload == nullptr || payloadBytes < sizeof(CActivationPayloadHeader) ||
        payloadBytes > (std::numeric_limits<DWORD>::max)())
    {
        return false;
    }

    const BYTE* bytes = static_cast<const BYTE*>(payload);
    const CActivationPayloadHeader* header =
        reinterpret_cast<const CActivationPayloadHeader*>(bytes);
    if (header->Magic != SALLY_ACTIVATION_PROTOCOL_MAGIC ||
        header->Version != SALLY_ACTIVATION_PROTOCOL_VERSION ||
        header->HeaderSize != sizeof(*header) ||
        header->TotalSize != payloadBytes ||
        header->RequestUID != expectedRequestUID ||
        header->Generation != expectedGeneration ||
        header->SenderPID != expectedSenderPID ||
        header->ActivatePanel > 2 ||
        (header->Flags & ~(SALLY_ACTIVATION_FLAG_SET_TITLE |
                           SALLY_ACTIVATION_FLAG_SET_ICON)) != 0 ||
        ((header->Flags & SALLY_ACTIVATION_FLAG_SET_ICON) != 0 &&
         header->MainWindowIconIndex > 3) ||
        ((header->Flags & SALLY_ACTIVATION_FLAG_SET_ICON) == 0 &&
         header->MainWindowIconIndex != 0))
    {
        return false;
    }

    try
    {
        CommandLineRequest parsed;
        parsed.requestUID = header->RequestUID;
        parsed.activatePanel = header->ActivatePanel;
        parsed.setTitlePrefix =
            (header->Flags & SALLY_ACTIVATION_FLAG_SET_TITLE) != 0;
        parsed.setMainWindowIconIndex =
            (header->Flags & SALLY_ACTIVATION_FLAG_SET_ICON) != 0;
        parsed.mainWindowIconIndex = header->MainWindowIconIndex;

        std::size_t cursor = sizeof(*header);
        if (!ReadField(bytes, payloadBytes, header->LeftPath, cursor,
                       parsed.leftPath) ||
            !ReadField(bytes, payloadBytes, header->RightPath, cursor,
                       parsed.rightPath) ||
            !ReadField(bytes, payloadBytes, header->ActivePath, cursor,
                       parsed.activePath) ||
            !ReadField(bytes, payloadBytes, header->TitlePrefix, cursor,
                       parsed.titlePrefix) ||
            cursor != payloadBytes ||
            parsed.setTitlePrefix != !parsed.titlePrefix.empty())
        {
            return false;
        }
        request = std::move(parsed);
        return true;
    }
    catch (const std::bad_alloc&)
    {
        return false;
    }
}

std::string BuildActivationPayloadName(const std::string& namespacedBase,
                                       DWORD senderPID, DWORD requestUID,
                                       DWORD generation)
{
    char suffix[40] = {};
    std::snprintf(suffix, sizeof(suffix), "_%08lX_%08lX_%08lX",
                  static_cast<unsigned long>(senderPID),
                  static_cast<unsigned long>(requestUID),
                  static_cast<unsigned long>(generation));
    return namespacedBase + suffix;
}
