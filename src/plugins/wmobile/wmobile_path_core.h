// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Dynamically owned path composition used by both Windows Mobile copy/move directions and tested
// without requiring a connected device.

#pragma once

#include <cstddef>
#include <string>

namespace wmobile
{

// Appends one component to a device path, keeping the root separator.
//
// The device namespace is rooted at a bare L"\\": Windows CE has no drive letters and no current
// directory, so every path is absolute and that leading separator IS the root. SalPathAppend -
// which SPLSalPathAppendOwned reproduces exactly - resolves L"\\" + L"dir" to L"dir". On a PC that
// is harmless, because a path there is rooted by its drive; on the device it turns an absolute
// path into a relative one that nothing can resolve, so listing, entering, launching, copying or
// deleting anything sitting in the device root fails. pre-unicode carried its own
// CRAPI::PathAppend for exactly this reason, with the difference spelled out in a comment above
// it; the widening replaced every call with the generic helper and lost the distinction.
//
// PC-side paths in this plugin keep using SPLSalPathAppendOwned, matching pre-unicode, which chose
// between the two helpers per call site.
void AppendDeviceComponent(std::wstring& path, const wchar_t* name);

struct OperationTarget
{
    std::wstring path;
    std::wstring mask;
};

// Reads the live callback's one- or two-string payload without scanning beyond its published
// capacity. The returned operation state is dynamically owned UTF-16.
bool DecodeOperationTarget(const std::wstring& payload, bool hasMask,
                           OperationTarget& target);

// Resolves a target relative to the current device directory. This is the Unicode-native branch
// used before the host's general-path parser becomes involved.
bool ResolveRelativeDeviceTarget(const wchar_t* fsName, const std::wstring& currentPath,
                                 const std::wstring& enteredPath, std::wstring& fullTarget,
                                 size_t& userPartOffset, std::wstring& nextFocus);

// A dynamically owned base path plus a leaf that changes every iteration.
//
// WHY THIS EXISTS. wmobile's two copy loops each carry three of these:
//
//     strcpy(sourceName, Path);
//     char* endSource = sourceName + strlen(sourceName);
//     if (endSource > sourceName && *(endSource - 1) != '\\') { *endSource++ = '\\'; *endSource = 0; }
//     int endSourceSize = sourceName.Size() - (int)(endSource - sourceName);
//     ...
//     if ((int)strlen(name) >= endSourceSize) -> refuse
//     lstrcpynA(endSource, name, endSourceSize);
//
// A raw pointer into a buffer, a separately-maintained remaining size, and a refusal check the
// caller must remember to write. Three of those in one loop, and the source and target buffers
// belong to DIFFERENT path domains - one names a file on the phone, one names a file on the PC -
// with nothing in their types saying so. Widening in place would have kept all of that and only
// changed the width.
//
// PathComposer owns the complete semantic path. The old implementation wrapped caller-provided
// fixed storage and preserved its refusal threshold; that was useful during the first widening,
// but it also preserved the arbitrary capacity. Dynamic ownership removes that final ceiling.
class PathComposer
{
public:
    PathComposer() = default;

    // Copies 'base' in and appends a separator only if it lacks one.
    bool SetBase(const wchar_t* base);

    // Replaces the leaf without an arbitrary path ceiling.
    bool SetLeaf(const wchar_t* leaf);

    const wchar_t* Get() const { return Buffer_.c_str(); }
    operator const wchar_t*() const { return Buffer_.c_str(); }
    // The leaf position, for the few callers that need to post-process just the leaf.
    wchar_t* Leaf() { return HasBase_ ? Buffer_.data() + BaseLength_ : nullptr; }

private:
    std::wstring Buffer_;
    size_t BaseLength_ = 0;
    bool HasBase_ = false;
};

} // namespace wmobile
