// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#include "tar_text.h"
#include "archive_name.h"

#include "common/Win32TextCodec.h"

bool DecodeTarLegacyName(const char* bytes, std::wstring& name)
{
    // Lenient, like every other member name in this plugin (untar.cpp uses the lenient variant
    // for real tar members). A .gz ORIG_NAME records no encoding, and the strict variant returned
    // an EMPTY name for bytes the active code page disliked - the entry then had no usable name
    // at all. pre-unicode passed the stored bytes through unconverted.
    return DecodeArchiveMemberNameLenient(bytes, false, name);
}

bool EncodeTarUtf8Name(const std::wstring& name, std::string& bytes)
{
    return Win32EncodeText(CP_UTF8, name, bytes).Succeeded();
}
