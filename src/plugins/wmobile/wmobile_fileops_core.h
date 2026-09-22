// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later
//
// The destructive step itself, behind an injectable interface.
//
// Fourth use of the same trick, and the one it was building toward. Everything before this covered
// the decisions AROUND deletion - which paths, in what order, with which prompt. This covers the
// call that actually removes the file, which is the last piece of wmobile that could only be
// exercised on a phone.
//
// The retry/skip/skip-all prompt stays in fs_operations.cpp: it is UI, it fails loudly, and a
// human notices immediately when it misbehaves. What moves here is the part that fails SILENTLY -
// the attribute clearing that has to happen FIRST, and the choice between removing a directory and
// deleting a file. Get the first wrong and a read-only file simply refuses to delete, which reads
// as a device error. Get the second wrong and you call RemoveDirectory on a file, or worse.
//
// WHY AN INTERFACE RATHER THAN A FAKE rapi.dll: CDynRapi lives inside rapi.cpp and reaching it
// would mean compiling that file against precomp.h and the whole Salamander SDK. Injecting at the
// operation level gets the logic under test with no SDK in the test binary - the same reasoning as
// wmobile_treewalk_core's DeviceEnumerator, which is now the established shape in this plugin.

#pragma once

namespace wmobile
{

// The four device calls deletion needs. Implemented over CRAPI in production and in memory by the
// tests.
// The method names carry an OnDevice suffix for a reason that cost a build: DeleteFile and
// RemoveDirectory are Win32 MACROS, expanding to the ...W forms under UNICODE. Declaring members
// with those names silently renames them, and the class then fails to satisfy its own interface.
// The same TCHAR-macro trap this codebase has hit repeatedly, in a new place.
class DeviceFileOps
{
public:
    virtual ~DeviceFileOps() = default;

    virtual bool SetAttributesOnDevice(const wchar_t* path, unsigned long attributes) = 0;
    virtual bool DeleteFileOnDevice(const wchar_t* path) = 0;
    virtual bool RemoveDirectoryOnDevice(const wchar_t* path) = 0;
};

// Attributes that stop a delete until they are cleared. Named rather than spelled out at each use,
// because the two call sites in fs_operations.cpp had the triple written out separately and a
// divergence between them would have been invisible.
unsigned long BlockingDeleteAttributes();

// Deletes one item, clearing blocking attributes first. Returns false if the device refused; the
// caller decides whether to retry, skip or cancel.
//
// 'attributes' is what the walk reported for this item, not a fresh query - deleting is driven
// from the walk's snapshot, and re-querying here would be a second round trip per file.
bool DeleteOneItem(DeviceFileOps& ops,
                   const wchar_t* path,
                   unsigned long attributes,
                   bool isDirectory);

} // namespace wmobile
