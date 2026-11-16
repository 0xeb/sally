// Tests for ops/session_log: the log store (size cap, retention of closed logs, change counter), the sink (level filter,
// time stamp, titles) and many threads writing at once.
#include <gtest/gtest.h>

#include <string>
#include <thread>
#include <vector>

#include "global_config.h"
#include "session_log.h"

namespace
{
const size_t kBig = 1024 * 1024;

// The sink reads the level and the limits from the global configuration: every test that changes it restores it.
class SessionLogConfig : public ::testing::Test
{
protected:
    void SetUp() override { saved_ = GlobalConfigStore().Get(); }
    void TearDown() override { GlobalConfigStore().Set(saved_); }
    static void SetLevel(SessionLogLevel level, int maxKiB = 512, int closedKept = 10)
    {
        CGlobalConfig c = GlobalConfigStore().Get();
        c.logLevel = level;
        c.logMaxKiB = maxKiB;
        c.closedLogsKept = closedKept;
        GlobalConfigStore().Set(c);
    }

private:
    CGlobalConfig saved_;
};

size_t CountLines(const std::wstring& text)
{
    size_t n = 0;
    for (wchar_t ch : text)
        if (ch == L'\n')
            ++n;
    return n;
}
} // namespace

TEST(SessionLogStore, CreateGivesIncreasingIdsAndOpenLogs)
{
    CSessionLogStore store;
    const unsigned a = store.Create(PanelLogTitle(L"a@h:22"));
    const unsigned b = store.Create(WorkerLogTitle(L"a@h:22", 3, 1));
    EXPECT_NE(a, 0u);
    EXPECT_GT(b, a);
    const std::vector<CSessionLogInfo> list = store.List();
    ASSERT_EQ(list.size(), 2u);
    EXPECT_EQ(list[0].id, a);
    EXPECT_TRUE(list[0].open);
    EXPECT_TRUE(list[0].title == PanelLogTitle(L"a@h:22"));
    EXPECT_EQ(list[1].title.operation, 3u);
    EXPECT_EQ(list[1].title.worker, 1);
}

TEST(SessionLogStore, AppendBuildsTextWithCrLf)
{
    CSessionLogStore store;
    const unsigned id = store.Create(PanelLogTitle(L"t"));
    store.Append(id, L"one", kBig);
    store.Append(id, L"two", kBig);
    std::wstring text;
    ASSERT_TRUE(store.GetText(id, text));
    EXPECT_EQ(text, L"one\r\ntwo\r\n");
    CSessionLogInfo info;
    ASSERT_TRUE(store.GetInfo(id, info));
    EXPECT_EQ(info.chars, text.size());
}

TEST(SessionLogStore, UnknownIdIsIgnored)
{
    CSessionLogStore store;
    store.Append(99, L"x", kBig);
    store.Clear(99);
    store.Close(99, 10);
    std::wstring text = L"stale";
    EXPECT_FALSE(store.GetText(99, text));
    EXPECT_TRUE(text.empty());
}

TEST(SessionLogStore, SizeCapDropsTheOldestLines)
{
    CSessionLogStore store;
    const unsigned id = store.Create(PanelLogTitle(L"t"));
    // each line "line NN" is 7 characters + 2 for the line break; a cap of 40 holds 4 of them
    for (int i = 10; i < 30; i++)
        store.Append(id, L"line " + std::to_wstring(i), 40);
    std::wstring text;
    ASSERT_TRUE(store.GetText(id, text));
    EXPECT_LE(text.size(), 40u);
    EXPECT_EQ(CountLines(text), 4u);
    EXPECT_EQ(text, L"line 26\r\nline 27\r\nline 28\r\nline 29\r\n");
    CSessionLogInfo info;
    ASSERT_TRUE(store.GetInfo(id, info));
    EXPECT_EQ(info.chars, text.size());
}

TEST(SessionLogStore, ALineLongerThanTheCapEmptiesTheLog)
{
    CSessionLogStore store;
    const unsigned id = store.Create(PanelLogTitle(L"t"));
    store.Append(id, L"short", 100);
    store.Append(id, std::wstring(200, L'x'), 100);
    std::wstring text;
    ASSERT_TRUE(store.GetText(id, text));
    EXPECT_TRUE(text.empty());
}

TEST(SessionLogStore, ClearKeepsTheLogButEmptiesIt)
{
    CSessionLogStore store;
    const unsigned id = store.Create(PanelLogTitle(L"t"));
    store.Append(id, L"a", kBig);
    store.Clear(id);
    std::wstring text;
    ASSERT_TRUE(store.GetText(id, text));
    EXPECT_TRUE(text.empty());
    EXPECT_EQ(store.List().size(), 1u);
    store.Append(id, L"b", kBig);
    ASSERT_TRUE(store.GetText(id, text));
    EXPECT_EQ(text, L"b\r\n");
}

TEST(SessionLogStore, CloseMarksTheLogClosedAndKeepsTheText)
{
    CSessionLogStore store;
    const unsigned id = store.Create(PanelLogTitle(L"t"));
    store.Append(id, L"a", kBig);
    store.Close(id, 10);
    CSessionLogInfo info;
    ASSERT_TRUE(store.GetInfo(id, info));
    EXPECT_FALSE(info.open);
    std::wstring text;
    ASSERT_TRUE(store.GetText(id, text));
    EXPECT_EQ(text, L"a\r\n");
}

TEST(SessionLogStore, RetentionKeepsTheNewestClosedLogs)
{
    CSessionLogStore store;
    std::vector<unsigned> ids;
    for (int i = 0; i < 5; i++)
        ids.push_back(store.Create(PanelLogTitle(L"t" + std::to_wstring(i))));
    // closed in the order 0, 1, 2, 3, 4 with 2 kept: only 3 and 4 stay
    for (unsigned id : ids)
        store.Close(id, 2);
    const std::vector<CSessionLogInfo> list = store.List();
    ASSERT_EQ(list.size(), 2u);
    EXPECT_EQ(list[0].id, ids[3]);
    EXPECT_EQ(list[1].id, ids[4]);
}

TEST(SessionLogStore, RetentionGoesByClosingOrderNotByCreationOrder)
{
    CSessionLogStore store;
    const unsigned a = store.Create(PanelLogTitle(L"a"));
    const unsigned b = store.Create(PanelLogTitle(L"b"));
    const unsigned c = store.Create(PanelLogTitle(L"c"));
    store.Close(c, 2);
    store.Close(a, 2);
    store.Close(b, 2); // c was closed first: it goes
    const std::vector<CSessionLogInfo> list = store.List();
    ASSERT_EQ(list.size(), 2u);
    EXPECT_EQ(list[0].id, a);
    EXPECT_EQ(list[1].id, b);
}

TEST(SessionLogStore, RetentionNeverDropsOpenLogs)
{
    CSessionLogStore store;
    const unsigned open = store.Create(PanelLogTitle(L"open"));
    const unsigned x = store.Create(PanelLogTitle(L"x"));
    const unsigned y = store.Create(PanelLogTitle(L"y"));
    store.Close(x, 0);
    store.Close(y, 0);
    const std::vector<CSessionLogInfo> list = store.List();
    ASSERT_EQ(list.size(), 1u);
    EXPECT_EQ(list[0].id, open);
    EXPECT_TRUE(list[0].open);
}

TEST(SessionLogStore, ClosingTwiceDoesNotCountTwice)
{
    CSessionLogStore store;
    const unsigned a = store.Create(PanelLogTitle(L"a"));
    const unsigned b = store.Create(PanelLogTitle(L"b"));
    store.Close(a, 1);
    store.Close(a, 1);
    store.Close(b, 1);
    const std::vector<CSessionLogInfo> list = store.List();
    ASSERT_EQ(list.size(), 1u);
    EXPECT_EQ(list[0].id, b);
}

TEST(SessionLogStore, ChangeCounterMovesWithEveryChange)
{
    CSessionLogStore store;
    unsigned long last = store.ChangeCounter();
    const unsigned id = store.Create(PanelLogTitle(L"t"));
    EXPECT_GT(store.ChangeCounter(), last);
    last = store.ChangeCounter();
    store.Append(id, L"a", kBig);
    EXPECT_GT(store.ChangeCounter(), last);
    last = store.ChangeCounter();
    store.Clear(id);
    EXPECT_GT(store.ChangeCounter(), last);
    last = store.ChangeCounter();
    store.Close(id, 10);
    EXPECT_GT(store.ChangeCounter(), last);
    last = store.ChangeCounter();
    EXPECT_EQ(store.List().size(), 1u); // reading changes nothing
    std::wstring text;
    store.GetText(id, text);
    EXPECT_EQ(store.ChangeCounter(), last);
}

TEST(SessionLogStore, ManyThreadsWritingLoseNoLine)
{
    CSessionLogStore store;
    const int threads = 8;
    const int lines = 500;
    const unsigned shared = store.Create(PanelLogTitle(L"shared"));
    std::vector<unsigned> own;
    for (int t = 0; t < threads; t++)
        own.push_back(store.Create(PanelLogTitle(L"own" + std::to_wstring(t))));

    std::vector<std::thread> workers;
    for (int t = 0; t < threads; t++)
        workers.emplace_back(
            [&, t]
            {
                for (int i = 0; i < lines; i++)
                {
                    store.Append(shared, L"t" + std::to_wstring(t) + L" line " + std::to_wstring(i), 100 * kBig);
                    store.Append(own[t], L"line " + std::to_wstring(i), 100 * kBig);
                    if (i % 100 == 0)
                    {
                        std::wstring text; // readers run at the same time
                        store.GetText(shared, text);
                        store.List();
                    }
                }
            });
    for (std::thread& w : workers)
        w.join();

    std::wstring text;
    ASSERT_TRUE(store.GetText(shared, text));
    EXPECT_EQ(CountLines(text), static_cast<size_t>(threads * lines));
    for (int t = 0; t < threads; t++)
    {
        ASSERT_TRUE(store.GetText(own[t], text));
        EXPECT_EQ(CountLines(text), static_cast<size_t>(lines));
    }
}

TEST(SessionLogStore, ManyThreadsWritingKeepTheCap)
{
    CSessionLogStore store;
    const unsigned id = store.Create(PanelLogTitle(L"t"));
    std::vector<std::thread> workers;
    for (int t = 0; t < 4; t++)
        workers.emplace_back(
            [&]
            {
                for (int i = 0; i < 1000; i++)
                    store.Append(id, L"some log text number " + std::to_wstring(i), 2000);
            });
    for (std::thread& w : workers)
        w.join();
    CSessionLogInfo info;
    ASSERT_TRUE(store.GetInfo(id, info));
    EXPECT_LE(info.chars, 2000u);
    std::wstring text;
    ASSERT_TRUE(store.GetText(id, text));
    EXPECT_EQ(text.size(), info.chars);
}

// ---- sink ---------------------------------------------------------------------------------------------------------

TEST_F(SessionLogConfig, SinkWritesTimeStampedLines)
{
    SetLevel(SessionLogLevel::Verbose);
    CSessionLogStore store;
    CSessionLogSink sink(store);
    sink.Begin(PanelLogTitle(L"u@h:22"));
    ASSERT_NE(sink.LogId(), 0u);
    sink.Log(SshLogLevel::Normal, "Connected");
    std::wstring text;
    ASSERT_TRUE(store.GetText(sink.LogId(), text));
    // "hh:mm:ss.mmm Connected"
    ASSERT_EQ(text.size(), 13u + 9u + 2u);
    EXPECT_EQ(text[2], L':');
    EXPECT_EQ(text[5], L':');
    EXPECT_EQ(text[8], L'.');
    EXPECT_EQ(text.substr(13), L"Connected\r\n");
}

TEST_F(SessionLogConfig, SinkDecodesUtf8)
{
    SetLevel(SessionLogLevel::Verbose);
    CSessionLogStore store;
    CSessionLogSink sink(store);
    sink.Begin(PanelLogTitle(L"t"));
    sink.Log(SshLogLevel::Normal, "caf\xC3\xA9");
    std::wstring text;
    ASSERT_TRUE(store.GetText(sink.LogId(), text));
    EXPECT_NE(text.find(L"caf\u00E9"), std::wstring::npos); // U+00E9 written as an escape: the source file is not UTF-8
}

TEST_F(SessionLogConfig, SinkKeepsEachLineOnOneLine)
{
    SetLevel(SessionLogLevel::Verbose);
    CSessionLogStore store;
    CSessionLogSink sink(store);
    sink.Begin(PanelLogTitle(L"t"));
    sink.Log(SshLogLevel::Error, "first\r\nsecond\nthird");
    std::wstring text;
    ASSERT_TRUE(store.GetText(sink.LogId(), text));
    EXPECT_EQ(CountLines(text), 1u);
    EXPECT_NE(text.find(L"first  second third"), std::wstring::npos);
}

TEST_F(SessionLogConfig, LevelFilterErrorsOnly)
{
    SetLevel(SessionLogLevel::Errors);
    CSessionLogStore store;
    CSessionLogSink sink(store);
    sink.Begin(PanelLogTitle(L"t"));
    sink.Log(SshLogLevel::Verbose, "v");
    sink.Log(SshLogLevel::Normal, "n");
    sink.Log(SshLogLevel::Error, "e");
    std::wstring text;
    ASSERT_TRUE(store.GetText(sink.LogId(), text));
    EXPECT_EQ(CountLines(text), 1u);
    EXPECT_NE(text.find(L" e\r\n"), std::wstring::npos);
}

TEST_F(SessionLogConfig, LevelFilterNormalDropsVerbose)
{
    SetLevel(SessionLogLevel::Normal);
    CSessionLogStore store;
    CSessionLogSink sink(store);
    sink.Begin(PanelLogTitle(L"t"));
    sink.Log(SshLogLevel::Verbose, "v");
    sink.Log(SshLogLevel::Normal, "n");
    sink.Log(SshLogLevel::Error, "e");
    std::wstring text;
    ASSERT_TRUE(store.GetText(sink.LogId(), text));
    EXPECT_EQ(CountLines(text), 2u);
}

TEST_F(SessionLogConfig, LevelFilterVerboseKeepsAll)
{
    SetLevel(SessionLogLevel::Verbose);
    CSessionLogStore store;
    CSessionLogSink sink(store);
    sink.Begin(PanelLogTitle(L"t"));
    sink.Log(SshLogLevel::Verbose, "v");
    sink.Log(SshLogLevel::Normal, "n");
    sink.Log(SshLogLevel::Error, "e");
    std::wstring text;
    ASSERT_TRUE(store.GetText(sink.LogId(), text));
    EXPECT_EQ(CountLines(text), 3u);
}

TEST_F(SessionLogConfig, LevelOffWritesNothing)
{
    SetLevel(SessionLogLevel::Off);
    CSessionLogStore store;
    CSessionLogSink sink(store);
    sink.Begin(PanelLogTitle(L"t"));
    sink.Log(SshLogLevel::Error, "e");
    sink.Line(SshLogLevel::Error, L"e");
    std::wstring text;
    ASSERT_TRUE(store.GetText(sink.LogId(), text));
    EXPECT_TRUE(text.empty());
}

TEST_F(SessionLogConfig, ALevelChangeAppliesToTheNextLine)
{
    SetLevel(SessionLogLevel::Errors);
    CSessionLogStore store;
    CSessionLogSink sink(store);
    sink.Begin(PanelLogTitle(L"t"));
    sink.Log(SshLogLevel::Normal, "dropped");
    SetLevel(SessionLogLevel::Normal);
    sink.Log(SshLogLevel::Normal, "kept");
    std::wstring text;
    ASSERT_TRUE(store.GetText(sink.LogId(), text));
    EXPECT_EQ(CountLines(text), 1u);
    EXPECT_NE(text.find(L"kept"), std::wstring::npos);
}

TEST_F(SessionLogConfig, SizeLimitComesFromTheConfiguration)
{
    SetLevel(SessionLogLevel::Verbose, MinLogKiB); // 16 KiB
    CSessionLogStore store;
    CSessionLogSink sink(store);
    sink.Begin(PanelLogTitle(L"t"));
    for (int i = 0; i < 2000; i++)
        sink.Log(SshLogLevel::Normal, "a line of log text that is fairly long, number " + std::to_string(i));
    CSessionLogInfo info;
    ASSERT_TRUE(store.GetInfo(sink.LogId(), info));
    EXPECT_LE(info.chars, static_cast<size_t>(MinLogKiB) * 1024);
    EXPECT_GT(info.chars, static_cast<size_t>(MinLogKiB) * 1024 / 2);
}

TEST_F(SessionLogConfig, SinkWithoutALogIgnoresLines)
{
    SetLevel(SessionLogLevel::Verbose);
    CSessionLogStore store;
    CSessionLogSink sink(store);
    sink.Log(SshLogLevel::Error, "nobody listens");
    sink.Line(SshLogLevel::Error, L"nobody listens");
    EXPECT_EQ(sink.LogId(), 0u);
    EXPECT_TRUE(store.List().empty());
    sink.End(); // safe without a log
}

TEST_F(SessionLogConfig, BeginWithTheSameTitleContinuesTheLog)
{
    SetLevel(SessionLogLevel::Verbose);
    CSessionLogStore store;
    CSessionLogSink sink(store);
    sink.Begin(PanelLogTitle(L"u@h:22"));
    const unsigned first = sink.LogId();
    sink.Begin(PanelLogTitle(L"u@h:22"));
    EXPECT_EQ(sink.LogId(), first);
    EXPECT_EQ(store.List().size(), 1u);
}

TEST_F(SessionLogConfig, BeginWithAnotherTitleClosesTheOldLog)
{
    SetLevel(SessionLogLevel::Verbose);
    CSessionLogStore store;
    CSessionLogSink sink(store);
    sink.Begin(PanelLogTitle(L"u@h:22"));
    const unsigned first = sink.LogId();
    sink.Begin(PanelLogTitle(L"u@other:22"));
    EXPECT_NE(sink.LogId(), first);
    const std::vector<CSessionLogInfo> list = store.List();
    ASSERT_EQ(list.size(), 2u);
    EXPECT_FALSE(list[0].open);
    EXPECT_TRUE(list[1].open);
}

TEST_F(SessionLogConfig, EndClosesTheLogAndTheDestructorToo)
{
    SetLevel(SessionLogLevel::Verbose);
    CSessionLogStore store;
    {
        CSessionLogSink sink(store);
        sink.Begin(PanelLogTitle(L"a"));
        sink.End();
        EXPECT_EQ(sink.LogId(), 0u);
        sink.Begin(PanelLogTitle(L"b"));
    }
    const std::vector<CSessionLogInfo> list = store.List();
    ASSERT_EQ(list.size(), 2u);
    EXPECT_FALSE(list[0].open);
    EXPECT_FALSE(list[1].open);
}

TEST_F(SessionLogConfig, ClosedLogsKeptComesFromTheConfiguration)
{
    SetLevel(SessionLogLevel::Verbose, 512, 1);
    CSessionLogStore store;
    for (int i = 0; i < 3; i++)
    {
        CSessionLogSink sink(store);
        sink.Begin(PanelLogTitle(L"t" + std::to_wstring(i)));
    }
    const std::vector<CSessionLogInfo> list = store.List();
    ASSERT_EQ(list.size(), 1u);
    EXPECT_EQ(list[0].title.target, L"t2");
}

TEST_F(SessionLogConfig, ManySinksOnOneStoreFromManyThreads)
{
    SetLevel(SessionLogLevel::Verbose, 512, 100);
    CSessionLogStore store;
    std::vector<std::thread> workers;
    for (int t = 0; t < 8; t++)
        workers.emplace_back(
            [&, t]
            {
                CSessionLogSink sink(store);
                sink.Begin(WorkerLogTitle(L"u@h:22", 1, t + 1));
                for (int i = 0; i < 300; i++)
                    sink.Log(SshLogLevel::Normal, "line " + std::to_string(i));
            });
    for (std::thread& w : workers)
        w.join();
    const std::vector<CSessionLogInfo> list = store.List();
    ASSERT_EQ(list.size(), 8u);
    for (const CSessionLogInfo& info : list)
    {
        EXPECT_FALSE(info.open);
        std::wstring text;
        ASSERT_TRUE(store.GetText(info.id, text));
        EXPECT_EQ(CountLines(text), 300u);
    }
}

TEST(SessionLogTitles, TargetAndTitles)
{
    EXPECT_EQ(SessionLogTarget(L"user", L"host", 2222), L"user@host:2222");
    const CSessionLogTitle panel = PanelLogTitle(L"x");
    EXPECT_EQ(panel.kind, CSessionLogTitle::Kind::Panel);
    const CSessionLogTitle worker = WorkerLogTitle(L"x", 4, 2);
    EXPECT_EQ(worker.kind, CSessionLogTitle::Kind::Worker);
    EXPECT_FALSE(panel == worker);
    EXPECT_TRUE(worker == WorkerLogTitle(L"x", 4, 2));
    EXPECT_FALSE(worker == WorkerLogTitle(L"x", 4, 3));
}
