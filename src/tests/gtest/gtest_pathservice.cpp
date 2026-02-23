// SPDX-FileCopyrightText: 2026 Open Salamander Authors
// SPDX-License-Identifier: GPL-2.0-or-later
// CommentsTranslationProject: TRANSLATED

#include <gtest/gtest.h>

#include "../../common/IPathService.h"
#include "../../common/widepath.h"

TEST(PathService, ToLongPathShortPathKeepsPath)
{
    IPathService* svc = GetWin32PathService();
    ASSERT_NE(svc, nullptr);

    std::wstring out;
    PathResult res = svc->ToLongPath(L"C:\\Windows", out);
    ASSERT_TRUE(res.success);
    EXPECT_EQ(out, L"C:\\Windows");
}

TEST(PathService, ToLongPathLongLocalAddsPrefix)
{
    IPathService* svc = GetWin32PathService();
    ASSERT_NE(svc, nullptr);

    std::wstring path = L"C:\\";
    while (path.size() < (size_t)SAL_LONG_PATH_THRESHOLD + 10)
        path += L"segment\\";

    std::wstring out;
    PathResult res = svc->ToLongPath(path.c_str(), out);
    ASSERT_TRUE(res.success);
    EXPECT_TRUE(out.rfind(L"\\\\?\\", 0) == 0);
}

TEST(PathService, ToLongPathLongUNCAddsUNCPrefix)
{
    IPathService* svc = GetWin32PathService();
    ASSERT_NE(svc, nullptr);

    std::wstring path = L"\\\\server\\share\\";
    while (path.size() < (size_t)SAL_LONG_PATH_THRESHOLD + 10)
        path += L"segment\\";

    std::wstring out;
    PathResult res = svc->ToLongPath(path.c_str(), out);
    ASSERT_TRUE(res.success);
    EXPECT_TRUE(out.rfind(L"\\\\?\\UNC\\", 0) == 0);
}

TEST(PathService, GetCurrentDirectoryReturnsPath)
{
    IPathService* svc = GetWin32PathService();
    ASSERT_NE(svc, nullptr);

    std::wstring out;
    PathResult res = svc->GetCurrentDirectory(out);
    ASSERT_TRUE(res.success);
    EXPECT_FALSE(out.empty());
}

TEST(PathService, GetModuleFileNameReturnsPath)
{
    IPathService* svc = GetWin32PathService();
    ASSERT_NE(svc, nullptr);

    std::wstring out;
    PathResult res = svc->GetModuleFileName(NULL, out);
    ASSERT_TRUE(res.success);
    EXPECT_FALSE(out.empty());
}

TEST(PathService, GetTempPathReturnsPath)
{
    IPathService* svc = GetWin32PathService();
    ASSERT_NE(svc, nullptr);

    std::wstring out;
    PathResult res = svc->GetTempPath(out);
    ASSERT_TRUE(res.success);
    EXPECT_FALSE(out.empty());
}

TEST(PathService, GetFullPathNameExpandsRelativePath)
{
    IPathService* svc = GetWin32PathService();
    ASSERT_NE(svc, nullptr);

    std::wstring out;
    PathResult res = svc->GetFullPathName(L".", out);
    ASSERT_TRUE(res.success);
    EXPECT_FALSE(out.empty());
}

