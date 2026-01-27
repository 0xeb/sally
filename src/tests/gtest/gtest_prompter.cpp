#include <gtest/gtest.h>
#include "../../ui/IPrompter.h"
#include <string>
#include <vector>

IPrompter* gPrompter = nullptr; // required external symbol

class MockPrompter : public IPrompter
{
public:
    PromptResult ConfirmOverwrite(const wchar_t* path, const wchar_t* existingInfo) override
    {
        log.push_back(L"ConfirmOverwrite:" + std::wstring(path ? path : L"") + L":" + std::wstring(existingInfo ? existingInfo : L""));
        return {PromptResult::kYes};
    }

    PromptResult ConfirmAdsLoss(const wchar_t* path) override
    {
        log.push_back(L"ConfirmAdsLoss:" + std::wstring(path ? path : L""));
        return {PromptResult::kNo};
    }

    PromptResult ConfirmDelete(const wchar_t* path, bool recycleBin) override
    {
        log.push_back(L"ConfirmDelete:" + std::wstring(path ? path : L"") + (recycleBin ? L":recycle" : L":permanent"));
        return {PromptResult::kOk};
    }

    void ShowError(const wchar_t* title, const wchar_t* message) override
    {
        log.push_back(L"ShowError:" + std::wstring(title ? title : L"") + L":" + std::wstring(message ? message : L""));
    }

    void ShowInfo(const wchar_t* title, const wchar_t* message) override
    {
        log.push_back(L"ShowInfo:" + std::wstring(title ? title : L"") + L":" + std::wstring(message ? message : L""));
    }

    std::vector<std::wstring> log;
};

TEST(PrompterTest, RecordsInteractions)
{
    MockPrompter mock;
    gPrompter = &mock;

    auto r1 = gPrompter->ConfirmOverwrite(L"C:\\test.txt", L"existing");
    EXPECT_EQ(r1.type, PromptResult::kYes);

    auto r2 = gPrompter->ConfirmAdsLoss(L"C:\\ads.txt");
    EXPECT_EQ(r2.type, PromptResult::kNo);

    auto r3 = gPrompter->ConfirmDelete(L"C:\\delete.txt", true);
    EXPECT_EQ(r3.type, PromptResult::kOk);

    gPrompter->ShowError(L"Error", L"oops");
    gPrompter->ShowInfo(L"Info", L"ok");

    ASSERT_EQ(mock.log.size(), 5u);
    EXPECT_EQ(mock.log[0], L"ConfirmOverwrite:C:\\test.txt:existing");
    EXPECT_EQ(mock.log[1], L"ConfirmAdsLoss:C:\\ads.txt");
    EXPECT_EQ(mock.log[2], L"ConfirmDelete:C:\\delete.txt:recycle");
    EXPECT_EQ(mock.log[3], L"ShowError:Error:oops");
    EXPECT_EQ(mock.log[4], L"ShowInfo:Info:ok");
}

int main(int argc, char** argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
