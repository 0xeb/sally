// SPDX-FileCopyrightText: 2025-2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

// The calls PictView makes into Sally, Windows and its own top-level windows from the plugin
// interface. Each one can be replaced; an empty entry means the built-in behavior, which is what
// the shipped plugin always uses.

#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <functional>
#include <string>
#include <vector>

#include "thumbnail_update.h"
#include "viewer.h"

namespace pictview
{

struct PluginServices
{
    // The installed WIC decoders' extension lists (ImageEngine::DecoderFileExtensions).
    std::function<std::vector<std::wstring>()> DecoderFileExtensions;
    // Whether the clipboard holds an image (View Bitmap from Clipboard).
    std::function<bool()> ClipboardHasImage;
    // Tells Sally a folder's content changed (after Rename, Copy To, Delete).
    std::function<void(const std::wstring& directory)> PathChanged;
    // Sally's panel refresh after a thumbnail update.
    std::function<void(int panel, BOOL forceRefresh, BOOL focusFirstNewItem)> RefreshPanelPath;
    // Update Thumbnail: the files it works on (the source panel's selected files, else its
    // focused file), its first question (with "don't ask again"), its questions per file and its
    // closing message.
    std::function<std::vector<std::wstring>()> SourcePanelFiles;
    std::function<bool(HWND parent, bool& dontAskAgain)> ConfirmThumbnailUpdate;
    std::function<ThumbnailUpdateAnswer(ThumbnailUpdateQuestion question, const std::wstring& path, const std::wstring& detail,
                                        size_t fileCount)>
        AskThumbnailUpdate;
    std::function<void(const std::wstring& text)> ShowMessage;
    // Sally's settings the plugin follows.
    std::function<bool()> SelectWholeNameOnRename;
    std::function<bool()> AlwaysOnTopForViewer;
    std::function<bool()> SaveHistory;
    // Opening viewer windows.
    std::function<BOOL(const std::wstring& path, int left, int top, int width, int height, UINT showCmd, BOOL alwaysOnTop,
                       BOOL returnLock, HANDLE* lock, BOOL* lockOwner, const ViewerHostActions& hostActions,
                       const ViewerSourceNavigation& sourceNavigation)>
        OpenViewer;
    std::function<BOOL(int commandId, int left, int top, int width, int height, UINT showCmd, BOOL alwaysOnTop)> LaunchMenuViewer;
    // Dialogs the plugin interface shows.
    std::function<bool(HWND parent, HINSTANCE instance, ViewerPreferences& preferences)> PromptConfiguration;
    std::function<void(HWND parent)> AboutDialog;
    // Notifications to Sally and the viewer windows' lifetime.
    std::function<void(ViewerHostEvent event)> HostEvent;
    std::function<bool(bool force, bool& result)> CloseAllViewers; // true when it decided 'result'
    std::function<void()> ReleaseViewers;
};

PluginServices& Services();

} // namespace pictview
