// SPDX-FileCopyrightText: 2026 Open Salamander Authors
// SPDX-License-Identifier: GPL-2.0-or-later
// CommentsTranslationProject: TRANSLATED

#include <windows.h>
#include <gtest/gtest.h>
#include "../../common/IClipboard.h"
#include <string>
#include <vector>

// Define the global pointer (normally defined in Win32Clipboard.cpp)
IClipboard* gClipboard = nullptr;

// Mock implementation for testing
class MockClipboard : public IClipboard
{
public:
    struct Call
    {
        std::wstring op;
        std::wstring arg;
    };

    std::vector<Call> calls;
    std::wstring storedText;
    std::vector<std::wstring> storedPaths;
    bool hasText = false;
    bool hasFileDrop = false;
    ClipboardResult opResult = ClipboardResult::Ok();

    ClipboardResult SetText(const wchar_t* text) override
    {
        calls.push_back({L"SetText", text ? text : L""});
        if (opResult.success && text)
        {
            storedText = text;
            hasText = true;
        }
        return opResult;
    }

    ClipboardResult GetText(std::wstring& text) override
    {
        calls.push_back({L"GetText", L""});
        if (opResult.success && hasText)
        {
            text = storedText;
            return ClipboardResult::Ok();
        }
        text.clear();
        return opResult.success ? ClipboardResult::Error(ERROR_NOT_FOUND) : opResult;
    }

    bool HasText() override
    {
        calls.push_back({L"HasText", L""});
        return hasText;
    }

    bool HasFileDrop() override
    {
        calls.push_back({L"HasFileDrop", L""});
        return hasFileDrop;
    }

    ClipboardResult GetFilePaths(std::vector<std::wstring>& paths) override
    {
        calls.push_back({L"GetFilePaths", L""});
        if (opResult.success && hasFileDrop)
        {
            paths = storedPaths;
            return ClipboardResult::Ok();
        }
        paths.clear();
        return opResult.success ? ClipboardResult::Error(ERROR_NOT_FOUND) : opResult;
    }

    ClipboardResult Clear() override
    {
        calls.push_back({L"Clear", L""});
        if (opResult.success)
        {
            storedText.clear();
            storedPaths.clear();
            hasText = false;
            hasFileDrop = false;
        }
        return opResult;
    }

    bool HasFormat(uint32_t format) override
    {
        calls.push_back({L"HasFormat", std::to_wstring(format)});
        return false;
    }

    ClipboardResult SetRawData(uint32_t format, const void* data, size_t size) override
    {
        calls.push_back({L"SetRawData", std::to_wstring(format)});
        return opResult;
    }

    ClipboardResult GetRawData(uint32_t format, std::vector<uint8_t>& data) override
    {
        calls.push_back({L"GetRawData", std::to_wstring(format)});
        data.clear();
        return opResult;
    }

    uint32_t RegisterFormat(const wchar_t* name) override
    {
        calls.push_back({L"RegisterFormat", name ? name : L""});
        return 0x1234;  // Mock format ID
    }
};

TEST(ClipboardMockTest, RecordsOperations)
{
    MockClipboard mock;
    gClipboard = &mock;

    gClipboard->SetText(L"Hello World");
    gClipboard->HasText();
    gClipboard->Clear();

    ASSERT_EQ(mock.calls.size(), 3u);
    EXPECT_EQ(mock.calls[0].op, L"SetText");
    EXPECT_EQ(mock.calls[0].arg, L"Hello World");
    EXPECT_EQ(mock.calls[1].op, L"HasText");
    EXPECT_EQ(mock.calls[2].op, L"Clear");
}

TEST(ClipboardMockTest, SetAndGetText)
{
    MockClipboard mock;
    gClipboard = &mock;

    auto result = gClipboard->SetText(L"Test Unicode Text: привет мир 你好世界");
    EXPECT_TRUE(result.success);

    std::wstring retrieved;
    result = gClipboard->GetText(retrieved);
    EXPECT_TRUE(result.success);
    EXPECT_EQ(retrieved, L"Test Unicode Text: привет мир 你好世界");
}

TEST(ClipboardMockTest, GetTextWhenEmpty)
{
    MockClipboard mock;
    gClipboard = &mock;
    mock.hasText = false;

    std::wstring text;
    auto result = gClipboard->GetText(text);
    EXPECT_FALSE(result.success);
    EXPECT_EQ(result.errorCode, (uint32_t)ERROR_NOT_FOUND);
    EXPECT_TRUE(text.empty());
}

TEST(ClipboardMockTest, FileDropOperations)
{
    MockClipboard mock;
    mock.hasFileDrop = true;
    mock.storedPaths = {L"C:\\file1.txt", L"C:\\folder\\file2.doc", L"D:\\path with spaces\\file.txt"};
    gClipboard = &mock;

    EXPECT_TRUE(gClipboard->HasFileDrop());

    std::vector<std::wstring> paths;
    auto result = gClipboard->GetFilePaths(paths);
    EXPECT_TRUE(result.success);
    ASSERT_EQ(paths.size(), 3u);
    EXPECT_EQ(paths[0], L"C:\\file1.txt");
    EXPECT_EQ(paths[2], L"D:\\path with spaces\\file.txt");
}

TEST(ClipboardMockTest, ErrorHandling)
{
    MockClipboard mock;
    mock.opResult = ClipboardResult::Error(ERROR_ACCESS_DENIED);
    gClipboard = &mock;

    auto result = gClipboard->SetText(L"test");
    EXPECT_FALSE(result.success);
    EXPECT_EQ(result.errorCode, (uint32_t)ERROR_ACCESS_DENIED);
}

TEST(ClipboardMockTest, RuntimeSwap)
{
    MockClipboard mock1;
    MockClipboard mock2;

    gClipboard = &mock1;
    gClipboard->SetText(L"text1");

    gClipboard = &mock2;
    gClipboard->SetText(L"text2");

    EXPECT_EQ(mock1.calls.size(), 1u);
    EXPECT_EQ(mock2.calls.size(), 1u);
    EXPECT_EQ(mock1.calls[0].arg, L"text1");
    EXPECT_EQ(mock2.calls[0].arg, L"text2");
}

int main(int argc, char** argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
