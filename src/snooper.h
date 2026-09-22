// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

class CFilesWindow;

extern HANDLE RefreshFinishedEvent;
extern int SnooperSuspended;

// The ANSI AddDirectory/ChangeDirectory entry points are DELETED:
// watching a lossy mirror watched the wrong path. Reach these through
// IChangeNotifier (common/IChangeNotifier.h) rather than calling them directly;
// they remain here only because the snooper thread owns the handles.
void AddDirectoryW(CFilesWindow* win, const wchar_t* pathW, BOOL registerDevNotification);
void ChangeDirectoryW(CFilesWindow* win, const wchar_t* newPathW, BOOL registerDevNotification);

void DetachDirectory(CFilesWindow* win, BOOL waitForHandleClosure = FALSE, BOOL closeDevNotifification = TRUE); // no longer need to snoop

BOOL InitializeThread();
void TerminateThread();

void BeginSuspendMode(BOOL debugDoNotTestCaller = FALSE);
void EndSuspendMode(BOOL debugDoNotTestCaller = FALSE);

typedef TDirectArray<CFilesWindow*> CWindowArray; // (CFilesWindow *)
typedef TDirectArray<HANDLE> CObjectArray;        // (HANDLE)

extern CWindowArray WindowArray; // arrays indexed the same way
extern CObjectArray ObjectArray; // ObjectHandle belongs to MainWindow
