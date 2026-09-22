// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"
#include "..\undelete.rh2"
#include "snapshot.h"

#include "miscstr.h"
#include "os.h"
#include "volume.h"

const wchar_t* CVolume<wchar_t>::STRING_VOLUME_NT = L"\\\\.\\";

const wchar_t* CVolume<wchar_t>::STRING_VOLUME_95 = L"\\\\.\\vwin32";

const wchar_t* CVolume<wchar_t>::STRING_ROOT = L"?:\\";

const wchar_t* CVolume<wchar_t>::STRING_FAT32 = L"FAT32";

const wchar_t CVolume<wchar_t>::CHAR_A = L'A';

const wchar_t CVolume<wchar_t>::CHAR_BSLASH = L'\\';

const wchar_t CVolume<wchar_t>::CHAR_COLON = L':';
