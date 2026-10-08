// SPDX-FileCopyrightText: 2025-2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

// File masks for the image formats the installed WIC codecs add (HEIC, AVIF, WebP, camera RAW,
// ...), worked out from the decoders' extension lists.

#pragma once

#include <string>
#include <vector>

namespace pictview
{

// The extensions in WIC decoder lists such as ".heic,.HEIF": lower case, without the dot,
// sorted and unique. Anything that is not a plain extension (letters, digits, '-', '_') is
// skipped, so a mask can always be built from the result.
std::vector<std::wstring> ParseDecoderExtensions(const std::vector<std::wstring>& extensionLists);

// Whether "*.<extension>" is one of the ';'-separated masks (case-insensitive).
bool MasksCoverExtension(const std::wstring& masks, const std::wstring& extension);

// The extensions no mask in 'masks' covers.
std::vector<std::wstring> ExtensionsBeyondMasks(const std::vector<std::wstring>& extensions, const std::wstring& masks);

// "*.avif;*.heic" from {"avif", "heic"}.
std::wstring MasksFromExtensions(const std::vector<std::wstring>& extensions);

// "avif;heic" <-> {"avif", "heic"} (how the offered extensions are stored).
std::wstring JoinExtensions(const std::vector<std::wstring>& extensions);
std::vector<std::wstring> SplitExtensions(const std::wstring& joined);

// The extensions of 'installed' not in 'offered': the formats to add to the viewer list now.
std::vector<std::wstring> ExtensionsNotOffered(const std::vector<std::wstring>& installed, const std::vector<std::wstring>& offered);

} // namespace pictview
