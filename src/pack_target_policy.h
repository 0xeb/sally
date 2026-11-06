// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <string>

// A Copy/Move (F5/F6) target that names an existing, packable archive written
// WITHOUT a trailing backslash used to be classified as a plain file to overwrite —
// so the copy silently replaced (destroyed) the whole archive after a generic
// "Confirm File Overwrite". Drag & drop onto the same archive correctly packs into it.
//
// This decides whether such a target should instead be routed to the archive/pack flow
// (the caller appends a backslash so the path splitter classifies it as PATH_TYPE_ARCHIVE,
// which then shows the "Add to existing archive?" confirm). Kept pure / UI-free so the
// decision can be exercised headlessly.
//
// Inputs are the facts the caller already has:
//   isCopyOrMove            - the operation is a Copy or Move (not Delete/etc.)
//   targetHadTrailingBackslash - user already signalled a container with a trailing '\\'
//   targetExists            - the resolved target path exists on disk
//   targetIsDirectory       - the existing target is a directory
//   packIsArchiveFormat     - CPackerFormatConfig::PackIsArchive() result (0 = not an archive)
//   packerUsableForFormat   - GetUsePacker(format-1): a packer can update this format
bool ShouldRerouteCopyTargetToArchive(bool isCopyOrMove,
                                      bool targetHadTrailingBackslash,
                                      bool targetExists,
                                      bool targetIsDirectory,
                                      int packIsArchiveFormat,
                                      bool packerUsableForFormat);

// Builds an ANSI probe name for CPackerFormatConfig::PackIsArchive() from a WIDE path.
//
// Why this exists. The Copy/Move target is carried both wide (exact) and ANSI (a best-fit mirror
// from WideToAnsi with flags 0). Probing the ANSI mirror is unsafe: under CP-1252 a target of
// L"Ā.zip" collapses to "A.zip", so the probe answers about a DIFFERENT file and the caller
// reroutes the operation into the wrong archive.
//
// Returns the LONGEST suffix of the final path component that survives an exact ANSI round-trip.
//
// It must be the longest suffix, not merely the text after the last dot. PackIsArchive supports
// multi-part associations and stores each one reversed with a trailing dot - "tar.gz" becomes
// "g.rat." - so matching it requires the literal ".tar" ahead of the ".gz". Probing only the final
// extension can never match "tar.gz": it matches a plain "gz" association instead, or nothing,
// and nothing sends F5/F6 back to the destructive overwrite path this policy exists to prevent.
//
// Requiring only a SUFFIX to convert - rather than the whole path - is what keeps a genuinely
// non-ANSI archive name such as L"Ā.tar.gz" detectable. Truncation can only cost a match,
// never invent one, because a pattern longer than the probe runs off the front and fails the
// bounds check.
//
// Returns false when nothing in the component is exactly representable: refusing is deliberate,
// since guessing is what selects the wrong archive.
bool BuildArchiveProbeNameFromWidePath(const wchar_t* fullWidePath, std::string& probeName);