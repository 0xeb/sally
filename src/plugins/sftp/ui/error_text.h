// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

// The texts of core errors from the string table (ops/ and core/ have no texts of their own). Lives in ui/ so the
// operation window and the solve-error dialog can show them; fs/ uses it too.

#pragma once

#include <string>

#include "ssh_types.h"

// The string-table pattern (with %1, %2) of a core error id; empty when there is none.
std::wstring LangStrForMsg(SshMsg msg);

// The error text with the words taken from the string table; the English message when there is no translation.
std::wstring DescribeError(const CSshError& error);
