// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <string>

// CHM directory entry names are UTF-8 bytes in the file format. Decode them at the
// chmlib boundary so encoded ownership never leaks into Sally's UTF-16 metadata.
bool DecodeChmPathUtf8(const char* pathBytes, std::wstring& path);
