// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#define MessageCenterName "RemoteComparator"
#define StartedEventName "RemoteComparatorStarted"
#define REMOTE_PATH_CAPACITY 32768

struct CRCMessage : public CMessage
{
    wchar_t CurrentDirectory[REMOTE_PATH_CAPACITY];
    wchar_t Path1[REMOTE_PATH_CAPACITY];
    wchar_t Path2[REMOTE_PATH_CAPACITY];
    char ReleaseEvent[20];
};
