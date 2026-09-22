// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <string>

bool DecodeTarLegacyName(const char* bytes, std::wstring& name);
bool EncodeTarUtf8Name(const std::wstring& name, std::string& bytes);
