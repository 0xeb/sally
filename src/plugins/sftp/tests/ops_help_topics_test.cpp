// Checks that the help topic ids are wired completely. The help call itself needs Sally, so these tests read the
// sources as plain text: sftp.rh2 (the IDH_* ids), ui/*.cpp (dialog constructors) and fs/*.cpp (menu help).
#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <map>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#ifndef SFTP_SOURCE_DIR
#error SFTP_SOURCE_DIR must be defined by CMake (the plugin's source folder)
#endif

namespace
{
std::string ReadFileText(const std::filesystem::path& path)
{
    std::ifstream in(path, std::ios::binary);
    EXPECT_TRUE(in.is_open()) << path.string();
    std::stringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

// The text of every .cpp file of a sub folder of the plugin (name -> text).
std::map<std::string, std::string> ReadSources(const std::string& folder)
{
    std::map<std::string, std::string> files;
    for (const auto& entry : std::filesystem::directory_iterator(std::filesystem::path(SFTP_SOURCE_DIR) / folder))
    {
        if (entry.path().extension() == ".cpp")
            files[entry.path().filename().string()] = ReadFileText(entry.path());
    }
    return files;
}

// Every "#define IDH_x 2001" of sftp.rh2: name -> number.
std::map<std::string, int> ReadHelpIds()
{
    std::map<std::string, int> ids;
    const std::string text = ReadFileText(std::filesystem::path(SFTP_SOURCE_DIR) / "sftp.rh2");
    const std::regex define(R"(#define\s+(IDH_\w+)\s+(\d+))");
    for (std::sregex_iterator it(text.begin(), text.end(), define), end; it != end; ++it)
        ids[(*it)[1]] = std::stoi((*it)[2]);
    return ids;
}
} // namespace

TEST(HelpTopics, IdsAreUniqueAndFromTwoThousand)
{
    const std::map<std::string, int> ids = ReadHelpIds();
    ASSERT_FALSE(ids.empty());
    std::set<int> numbers;
    for (const auto& id : ids)
    {
        EXPECT_GE(id.second, 2000) << id.first;
        numbers.insert(id.second);
    }
    EXPECT_EQ(numbers.size(), ids.size()) << "two IDH_* share a number";
}

TEST(HelpTopics, EveryIdIsUsedOutsideTheResourceFile)
{
    std::map<std::string, std::string> sources = ReadSources("ui");
    for (const auto& file : ReadSources("fs"))
        sources[file.first] = file.second;
    sources["site_edit_dialog.h"] = ReadFileText(std::filesystem::path(SFTP_SOURCE_DIR) / "ui" / "site_edit_dialog.h");

    for (const auto& id : ReadHelpIds())
    {
        if (id.first == "IDH_SFTP_CONTENTS") // the manual's start: for the Help menu of Sally, no dialog uses it
            continue;
        bool used = false;
        for (const auto& file : sources)
            used = used || file.second.find(id.first) != std::string::npos;
        EXPECT_TRUE(used) << id.first << " is never used";
    }
}

// A dialog without a topic would swallow F1 silently: every constructor call of the base classes has to pass an id.
TEST(HelpTopics, EveryDialogConstructorPassesATopic)
{
    int found = 0;
    for (const auto& file : ReadSources("ui"))
    {
        const std::string& text = file.second;
        size_t pos = 0;
        while ((pos = text.find("(HLanguage,", pos)) != std::string::npos)
        {
            const size_t start = text.rfind(' ', pos);
            const std::string name = text.substr(start + 1, pos - start - 1);
            const size_t end = text.find(')', pos);
            if (name == "CCenteredDialog" || name == "CDialog")
            {
                found++;
                const std::string args = text.substr(pos, end - pos);
                EXPECT_TRUE(args.find("IDH_") != std::string::npos || args.find("helpID") != std::string::npos)
                    << file.first << ": " << name << args << ") has no help id";
            }
            pos = end;
        }
    }
    EXPECT_GE(found, 14) << "the scan found too few dialog constructors";
}

TEST(HelpTopics, MenuItemsMapToTopics)
{
    const std::string source = ReadFileText(std::filesystem::path(SFTP_SOURCE_DIR) / "fs" / "fs_interface.cpp");
    for (const char* command : {"SftpCmdConnect", "SftpCmdDisconnect", "SftpCmdOperations", "SftpCmdConfiguration",
                                "SftpCmdSessionLog"})
    {
        const std::regex mapping(std::string("case ") + command + R"(:\s*return IDH_\w+;)");
        EXPECT_TRUE(std::regex_search(source, mapping)) << command << " has no help topic in HelpTopicOfMenuItem";
    }
}
