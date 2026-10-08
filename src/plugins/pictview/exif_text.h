// SPDX-FileCopyrightText: 2025-2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

// How the EXIF dialog names a tag and shows its value: translated titles, and enumerated values
// (orientation, exposure program, metering, flash, ...) decoded into translated words.

#pragma once

#include <string>

#include "engine/wic_engine.h"

namespace pictview
{

// The tag's translated title, or the engine's name for a tag PictView does not know.
std::wstring ExifTagTitle(const ImageExifEntry& entry);
// The value in words when the tag is an enumeration PictView knows, else the engine's text.
std::wstring ExifValueText(const ImageExifEntry& entry);

} // namespace pictview
