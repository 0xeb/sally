// Checks that the error texts are wired completely. The lookup itself (ui/error_text.cpp) needs Sally's string table, so
// these tests read the resource files as plain text: sftp.rh2 (ids), lang/lang.rc2 (strings) and ui/error_text.cpp.
#include <gtest/gtest.h>

#include <fstream>
#include <map>
#include <regex>
#include <set>
#include <sstream>
#include <string>

#include "ssh_types.h"

#ifndef SFTP_SOURCE_DIR
#error SFTP_SOURCE_DIR must be defined by CMake (the plugin's source folder)
#endif

namespace
{
std::string ReadSourceFile(const std::string& relativePath)
{
    std::ifstream in(std::string(SFTP_SOURCE_DIR) + "/" + relativePath, std::ios::binary);
    EXPECT_TRUE(in.is_open()) << relativePath;
    std::stringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

// Every "#define IDS_ERRMSG_x 123" of sftp.rh2: name -> number (and the list of numbers in file order).
struct CErrMsgIds
{
    std::map<std::string, int> byName;
    std::vector<int> numbers;
};

CErrMsgIds ReadErrMsgIds()
{
    CErrMsgIds ids;
    const std::string text = ReadSourceFile("sftp.rh2");
    const std::regex define(R"(#define\s+(IDS_ERRMSG_\w+)\s+(\d+))");
    for (std::sregex_iterator it(text.begin(), text.end(), define), end; it != end; ++it)
    {
        ids.byName[(*it)[1]] = std::stoi((*it)[2]);
        ids.numbers.push_back(std::stoi((*it)[2]));
    }
    return ids;
}
} // namespace

TEST(ErrorTextResources, EveryErrMsgIdHasANonEmptyString)
{
    const CErrMsgIds ids = ReadErrMsgIds();
    ASSERT_FALSE(ids.byName.empty());

    std::map<std::string, std::string> strings; // name -> the text between the quotes
    const std::string rc = ReadSourceFile("lang/lang.rc2");
    const std::regex entry(R"re(\n\s*(IDS_ERRMSG_\w+),\s*"(.*)"[ \t\r]*(?=\n))re");
    for (std::sregex_iterator it(rc.begin(), rc.end(), entry), end; it != end; ++it)
        strings[(*it)[1]] = (*it)[2];

    for (const auto& id : ids.byName)
    {
        const auto found = strings.find(id.first);
        ASSERT_NE(found, strings.end()) << id.first << " has no string in lang.rc2";
        EXPECT_FALSE(found->second.empty()) << id.first << " has an empty string";
    }
    for (const auto& text : strings)
        EXPECT_NE(ids.byName.find(text.first), ids.byName.end()) << text.first << " is not defined in sftp.rh2";
}

TEST(ErrorTextResources, ErrMsgNumbersAreUniqueAndContiguous)
{
    const CErrMsgIds ids = ReadErrMsgIds();
    ASSERT_FALSE(ids.numbers.empty());
    const std::set<int> unique(ids.numbers.begin(), ids.numbers.end());
    EXPECT_EQ(unique.size(), ids.numbers.size()) << "two IDS_ERRMSG_* share a number";
    EXPECT_EQ(*unique.rbegin() - *unique.begin() + 1, static_cast<int>(unique.size())) << "a number is missing";
}

TEST(ErrorTextResources, EveryErrMsgIdIsUsedInErrorTextCpp)
{
    const std::string source = ReadSourceFile("ui/error_text.cpp");
    for (const auto& id : ReadErrMsgIds().byName)
        EXPECT_NE(source.find(id.first), std::string::npos) << id.first << " is never used";
}

// StringIdOfMsg has one "case SshMsg::X:" per id. Comparing the number of distinct labels with the number of ids
// catches a new SshMsg value that was not added to the switch (an id that always falls back to English).
TEST(ErrorTextResources, EverySshMsgHasACaseInStringIdOfMsg)
{
    const std::string source = ReadSourceFile("ui/error_text.cpp");
    std::set<std::string> labels;
    const std::regex caseLabel(R"(case\s+SshMsg::(\w+)\s*:)");
    for (std::sregex_iterator it(source.begin(), source.end(), caseLabel), end; it != end; ++it)
        labels.insert((*it)[1]);

    EXPECT_EQ(labels.size(), static_cast<size_t>(static_cast<int>(SshMsg::Count_) - 1))
        << "ids 1..Count_-1 must each have a case in StringIdOfMsg (ui/error_text.cpp)";
}
