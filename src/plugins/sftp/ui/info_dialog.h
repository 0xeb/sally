// "Server information" (Ctrl+F1) and "Security" (click on the lock icon) dialogs. Both are read-only lists built from data
// the FS already holds (no network traffic); the rows are made here so fs/ only passes the facts.
#pragma once

#include <cstdint>
#include <string>

#include "panel_session.h"

struct CInfoDialogInput
{
    std::wstring connection; // "user@host" (or the site name), for the title
    std::wstring host;
    int port = 22;
    std::wstring user;
    CPanelSessionInfo session; // valid == false: the connection is not available
    bool freeKnown = false;    // the cached free space of the current directory's file system
    uint64_t bytesFree = 0;
    bool totalKnown = false;
    uint64_t bytesTotal = 0;
};

// Modal; returns when closed. Never throws.
void ShowServerInfoDialog(HWND parent, const CInfoDialogInput& input);

// Host key type, fingerprint, how the key was trusted and the negotiated algorithms.
void ShowSecurityDialog(HWND parent, const std::wstring& connection, const CPanelSessionInfo& session);

// True when the lock should be shown closed: the host key matched known hosts or the user saved it.
bool IsHostKeyTrusted(const CPanelSessionInfo& session);
