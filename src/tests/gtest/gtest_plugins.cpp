// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-License-Identifier: GPL-2.0-or-later

// Tests for DllExportsSalamanderEntry (PE export validation).

#include <gtest/gtest.h>
#include "peutils.h"

// TEST_DUMMY_PLUGIN_PATH is passed via CMake compile definition.
#ifndef TEST_DUMMY_PLUGIN_PATH
#error "TEST_DUMMY_PLUGIN_PATH must be defined by CMake"
#endif

// Helper: convert narrow string literal to wide string at runtime
static std::wstring ToWide(const char* s)
{
    int len = MultiByteToWideChar(CP_UTF8, 0, s, -1, nullptr, 0);
    std::wstring w(len - 1, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s, -1, &w[0], len);
    return w;
}

TEST(DllExportsSalamanderEntry, ReturnsTrueForPluginDll)
{
    std::wstring path = ToWide(TEST_DUMMY_PLUGIN_PATH);
    EXPECT_TRUE(DllExportsSalamanderEntry(path.c_str()));
}

TEST(DllExportsSalamanderEntry, ReturnsFalseForNonPluginDll)
{
    // kernel32.dll does not export SalamanderPluginEntry
    EXPECT_FALSE(DllExportsSalamanderEntry(L"kernel32.dll"));
}

TEST(DllExportsSalamanderEntry, ReturnsFalseForNonExistentFile)
{
    EXPECT_FALSE(DllExportsSalamanderEntry(L"C:\\nonexistent_path\\no_such_file.dll"));
}
