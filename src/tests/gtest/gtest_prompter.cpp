#include <gtest/gtest.h>
#include "../../ui/IPrompter.h"
#include "../../common/unicode/helpers.h"
#include <string>
#include <vector>

IPrompter* gPrompter = nullptr; // required external symbol

// ANSI overloads — duplicated here because test doesn't link UIPrompter.cpp
void IPrompter::ShowError(const char* title, const char* message) { ShowError(AnsiToWide(title).c_str(), AnsiToWide(message).c_str()); }
void IPrompter::ShowInfo(const char* title, const char* message) { ShowInfo(AnsiToWide(title).c_str(), AnsiToWide(message).c_str()); }
PromptResult IPrompter::ConfirmError(const char* title, const char* message) { return ConfirmError(AnsiToWide(title).c_str(), AnsiToWide(message).c_str()); }
PromptResult IPrompter::ConfirmDelete(const char* path, bool recycleBin) { return ConfirmDelete(AnsiToWide(path).c_str(), recycleBin); }
PromptResult IPrompter::ConfirmOverwrite(const char* path, const char* existingInfo) { return ConfirmOverwrite(path ? AnsiToWide(path).c_str() : nullptr, existingInfo ? AnsiToWide(existingInfo).c_str() : nullptr); }
PromptResult IPrompter::AskYesNo(const char* title, const char* message) { return AskYesNo(AnsiToWide(title).c_str(), AnsiToWide(message).c_str()); }

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

    PromptResult ConfirmError(const wchar_t* title, const wchar_t* message) override
    {
        log.push_back(L"ConfirmError:" + std::wstring(title ? title : L"") + L":" + std::wstring(message ? message : L""));
        return {PromptResult::kOk};
    }

    PromptResult AskYesNo(const wchar_t* title, const wchar_t* message) override
    {
        log.push_back(L"AskYesNo:" + std::wstring(title ? title : L"") + L":" + std::wstring(message ? message : L""));
        return {PromptResult::kYes};
    }

    PromptResult AskYesNoCancel(const wchar_t* title, const wchar_t* message) override
    {
        log.push_back(L"AskYesNoCancel:" + std::wstring(title ? title : L"") + L":" + std::wstring(message ? message : L""));
        return {PromptResult::kYes};
    }

    PromptResult AskYesNoWithCheckbox(const wchar_t* title, const wchar_t* message,
                                      const wchar_t* checkboxText, bool* checkboxValue) override
    {
        log.push_back(L"AskYesNoWithCheckbox:" + std::wstring(title ? title : L"") + L":" + std::wstring(message ? message : L""));
        if (checkboxValue)
            *checkboxValue = true; // simulate "don't show again" checked
        return {PromptResult::kYes};
    }

    void ShowInfoWithCheckbox(const wchar_t* title, const wchar_t* message,
                              const wchar_t* checkboxText, bool* checkboxValue) override
    {
        log.push_back(L"ShowInfoWithCheckbox:" + std::wstring(title ? title : L"") + L":" + std::wstring(message ? message : L""));
        if (checkboxValue)
            *checkboxValue = true;
    }

    void ShowErrorWithCheckbox(const wchar_t* title, const wchar_t* message,
                               const wchar_t* checkboxText, bool* checkboxValue) override
    {
        log.push_back(L"ShowErrorWithCheckbox:" + std::wstring(title ? title : L"") + L":" + std::wstring(message ? message : L""));
        if (checkboxValue)
            *checkboxValue = true;
    }

    PromptResult ConfirmWithCheckbox(const wchar_t* title, const wchar_t* message,
                                     const wchar_t* checkboxText, bool* checkboxValue) override
    {
        log.push_back(L"ConfirmWithCheckbox:" + std::wstring(title ? title : L"") + L":" + std::wstring(message ? message : L""));
        if (checkboxValue)
            *checkboxValue = true;
        return {PromptResult::kOk};
    }

    PromptResult AskSkipSkipAllFocus(const wchar_t* title, const wchar_t* message) override
    {
        log.push_back(L"AskSkipSkipAllFocus:" + std::wstring(title ? title : L"") + L":" + std::wstring(message ? message : L""));
        return {PromptResult::kSkip};
    }

    PromptResult AskSkipSkipAllCancel(const wchar_t* title, const wchar_t* message) override
    {
        log.push_back(L"AskSkipSkipAllCancel:" + std::wstring(title ? title : L"") + L":" + std::wstring(message ? message : L""));
        return {PromptResult::kSkip};
    }

    PromptResult AskRetryCancel(const wchar_t* title, const wchar_t* message) override
    {
        log.push_back(L"AskRetryCancel:" + std::wstring(title ? title : L"") + L":" + std::wstring(message ? message : L""));
        return {PromptResult::kRetry};
    }

    void ShowErrorWithHelp(const wchar_t* title, const wchar_t* message, uint32_t helpId) override
    {
        log.push_back(L"ShowErrorWithHelp:" + std::wstring(title ? title : L"") + L":" + std::wstring(message ? message : L""));
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

TEST(PrompterTest, AnsiOverloadsConvertAndForward)
{
    MockPrompter mock;
    gPrompter = &mock;

    gPrompter->ShowError("Error Title", "error message");
    gPrompter->ShowInfo("Info Title", "info message");
    auto r1 = gPrompter->ConfirmDelete("C:\\delete.txt", false);
    EXPECT_EQ(r1.type, PromptResult::kOk);
    auto r2 = gPrompter->AskYesNo("Question", "proceed?");
    EXPECT_EQ(r2.type, PromptResult::kYes);

    ASSERT_EQ(mock.log.size(), 4u);
    EXPECT_EQ(mock.log[0], L"ShowError:Error Title:error message");
    EXPECT_EQ(mock.log[1], L"ShowInfo:Info Title:info message");
    EXPECT_EQ(mock.log[2], L"ConfirmDelete:C:\\delete.txt:permanent");
    EXPECT_EQ(mock.log[3], L"AskYesNo:Question:proceed?");
}

int main(int argc, char** argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
