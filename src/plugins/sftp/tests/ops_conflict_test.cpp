#include <gtest/gtest.h>

#include <set>

#include "conflict.h"

namespace
{
    CTransferSide File(uint64_t size, int64_t mtime)
    {
        CTransferSide s;
        s.exists = true;
        s.size = size;
        s.mtime = mtime;
        return s;
    }

    CTransferSide Dir()
    {
        CTransferSide s;
        s.exists = true;
        s.isDir = true;
        return s;
    }

    CConflictDecision Decide(ConflictPolicy p, SolveAction f, const CTransferSide& src, const CTransferSide& tgt)
    {
        return DecideConflict(p, f, src, tgt);
    }

    void Expect(const CConflictDecision& d, ConflictAction action, uint64_t offset, Problem problem)
    {
        EXPECT_EQ(d.action, action);
        EXPECT_EQ(d.resumeOffset, offset);
        EXPECT_EQ(d.problem, problem);
    }

    const ConflictPolicy kPolicies[] = {ConflictPolicy::Ask,    ConflictPolicy::Overwrite,
                                        ConflictPolicy::Resume, ConflictPolicy::Skip,
                                        ConflictPolicy::OverwriteIfOlder, ConflictPolicy::Rename};
    const SolveAction kForces[] = {SolveAction::Skip, SolveAction::Overwrite, SolveAction::Resume, SolveAction::Rename};

    // source: 100 bytes, mtime 1000
    const CTransferSide kSource = File(100, 1000);
} // namespace

TEST(Conflict, MissingTargetAlwaysTransfers)
{
    CTransferSide missing; // exists = false
    for (ConflictPolicy p : kPolicies)
    {
        Expect(Decide(p, SolveAction::Retry, kSource, missing), ConflictAction::Transfer, 0, Problem::None);
        for (SolveAction f : kForces)
            Expect(Decide(p, f, kSource, missing), ConflictAction::Transfer, 0, Problem::None);
    }
}

TEST(Conflict, PolicyAsk)
{
    Expect(Decide(ConflictPolicy::Ask, SolveAction::Retry, kSource, File(50, 500)), ConflictAction::Park, 0,
           Problem::TargetExists);
}

TEST(Conflict, PolicyOverwrite)
{
    for (const CTransferSide& t : {File(0, 0), File(50, 500), File(100, 1000), File(150, 2000)})
        Expect(Decide(ConflictPolicy::Overwrite, SolveAction::Retry, kSource, t), ConflictAction::Transfer, 0,
               Problem::None);
}

TEST(Conflict, PolicySkipAndRename)
{
    for (const CTransferSide& t : {File(0, 0), File(50, 500), File(100, 1000), File(150, 2000)})
    {
        Expect(Decide(ConflictPolicy::Skip, SolveAction::Retry, kSource, t), ConflictAction::Skip, 0, Problem::None);
        Expect(Decide(ConflictPolicy::Rename, SolveAction::Retry, kSource, t), ConflictAction::Rename, 0, Problem::None);
    }
}

TEST(Conflict, PolicyResumeRule)
{
    const ConflictPolicy p = ConflictPolicy::Resume;
    Expect(Decide(p, SolveAction::Retry, kSource, File(0, 0)), ConflictAction::Transfer, 0, Problem::None);
    Expect(Decide(p, SolveAction::Retry, kSource, File(1, 0)), ConflictAction::Transfer, 1, Problem::None);
    Expect(Decide(p, SolveAction::Retry, kSource, File(99, 0)), ConflictAction::Transfer, 99, Problem::None);
    Expect(Decide(p, SolveAction::Retry, kSource, File(100, 0)), ConflictAction::Skip, 0, Problem::None);
    Expect(Decide(p, SolveAction::Retry, kSource, File(101, 0)), ConflictAction::Park, 0, Problem::TargetExists);
}

TEST(Conflict, PolicyOverwriteIfOlder)
{
    const ConflictPolicy p = ConflictPolicy::OverwriteIfOlder;
    Expect(Decide(p, SolveAction::Retry, kSource, File(100, 999)), ConflictAction::Transfer, 0, Problem::None);  // target older
    Expect(Decide(p, SolveAction::Retry, kSource, File(100, 1000)), ConflictAction::Skip, 0, Problem::None);     // same time
    Expect(Decide(p, SolveAction::Retry, kSource, File(100, 1001)), ConflictAction::Skip, 0, Problem::None);     // target newer
    Expect(Decide(p, SolveAction::Retry, kSource, File(5, 999)), ConflictAction::Transfer, 0, Problem::None);    // size is ignored
}

TEST(Conflict, ForcedActionWinsOverEveryPolicy)
{
    for (ConflictPolicy p : kPolicies)
    {
        const CTransferSide smaller = File(50, 500);
        Expect(Decide(p, SolveAction::Skip, kSource, smaller), ConflictAction::Skip, 0, Problem::None);
        Expect(Decide(p, SolveAction::Overwrite, kSource, smaller), ConflictAction::Transfer, 0, Problem::None);
        Expect(Decide(p, SolveAction::Resume, kSource, smaller), ConflictAction::Transfer, 50, Problem::None);
        Expect(Decide(p, SolveAction::Rename, kSource, smaller), ConflictAction::Rename, 0, Problem::None);
    }
}

TEST(Conflict, ForcedResumeRule)
{
    for (ConflictPolicy p : kPolicies)
    {
        Expect(Decide(p, SolveAction::Resume, kSource, File(0, 0)), ConflictAction::Transfer, 0, Problem::None);
        Expect(Decide(p, SolveAction::Resume, kSource, File(100, 0)), ConflictAction::Skip, 0, Problem::None);
        Expect(Decide(p, SolveAction::Resume, kSource, File(200, 0)), ConflictAction::Park, 0, Problem::TargetExists);
    }
}

TEST(Conflict, ForcedOverwriteIgnoresTimestamps)
{
    Expect(Decide(ConflictPolicy::OverwriteIfOlder, SolveAction::Overwrite, kSource, File(100, 5000)),
           ConflictAction::Transfer, 0, Problem::None);
}

TEST(Conflict, DirectoryOnBothSidesIsMerged)
{
    for (ConflictPolicy p : kPolicies)
        Expect(Decide(p, SolveAction::Retry, Dir(), Dir()), ConflictAction::Transfer, 0, Problem::None);
}

TEST(Conflict, TypeClashParksForEveryPolicy)
{
    for (ConflictPolicy p : kPolicies)
    {
        Expect(Decide(p, SolveAction::Retry, kSource, Dir()), ConflictAction::Park, 0, Problem::TargetExists);
        Expect(Decide(p, SolveAction::Retry, Dir(), File(5, 5)), ConflictAction::Park, 0, Problem::TargetExists);
        Expect(Decide(p, SolveAction::Overwrite, kSource, Dir()), ConflictAction::Park, 0, Problem::TargetExists);
        Expect(Decide(p, SolveAction::Resume, Dir(), File(5, 5)), ConflictAction::Park, 0, Problem::TargetExists);
    }
}

TEST(Conflict, TypeClashCanStillBeSkippedOrRenamedByTheUser)
{
    Expect(Decide(ConflictPolicy::Ask, SolveAction::Skip, kSource, Dir()), ConflictAction::Skip, 0, Problem::None);
    Expect(Decide(ConflictPolicy::Ask, SolveAction::Rename, kSource, Dir()), ConflictAction::Rename, 0, Problem::None);
}

TEST(AutoRename, FirstFreeNumber)
{
    auto none = [](const std::wstring&) { return false; };
    EXPECT_EQ(AutoRenameLocal(L"a.txt", none), L"a (2).txt");
    EXPECT_EQ(AutoRenameLocal(L"noext", none), L"noext (2)");
}

TEST(AutoRename, SkipsTakenNamesAndGaps)
{
    std::set<std::wstring> taken = {L"a (2).txt", L"a (3).txt", L"a (5).txt"};
    auto exists = [&](const std::wstring& n) { return taken.count(n) != 0; };
    EXPECT_EQ(AutoRenameLocal(L"a.txt", exists), L"a (4).txt"); // the gap is used
}

TEST(AutoRename, ExtensionEdgeCases)
{
    auto none = [](const std::wstring&) { return false; };
    EXPECT_EQ(AutoRenameLocal(L"a.tar.gz", none), L"a.tar (2).gz"); // chosen: only the last extension is kept apart
    EXPECT_EQ(AutoRenameLocal(L".bashrc", none), L".bashrc (2)");
    EXPECT_EQ(AutoRenameLocal(L".config.bak", none), L".config (2).bak");
    EXPECT_EQ(AutoRenameLocal(L"x.", none), L"x. (2)");
}

TEST(AutoRename, GivesUpAfter9999)
{
    int calls = 0;
    auto all = [&](const std::wstring&) { calls++; return true; };
    EXPECT_EQ(AutoRenameLocal(L"a.txt", all), L"");
    EXPECT_EQ(calls, 9998); // (2) .. (9999)
    auto last = [](const std::wstring& n) { return n != L"a (9999).txt"; };
    EXPECT_EQ(AutoRenameLocal(L"a.txt", last), L"a (9999).txt");
}

TEST(AutoRename, RemoteKeepsRawBytes)
{
    std::set<std::string> taken = {"caf\xE9.txt (2)"}; // irrelevant name: not a candidate
    auto exists = [&](const std::string& n) { return taken.count(n) != 0; };
    EXPECT_EQ(AutoRenameRemote("caf\xE9.txt", exists), "caf\xE9 (2).txt"); // invalid UTF-8 bytes stay as they are
    EXPECT_EQ(AutoRenameRemote(".bashrc", exists), ".bashrc (2)");
    std::set<std::string> t2 = {"a (2).gz", "a (3).gz"};
    EXPECT_EQ(AutoRenameRemote("a.gz", [&](const std::string& n) { return t2.count(n) != 0; }), "a (4).gz");
    EXPECT_EQ(AutoRenameRemote("a", [](const std::string&) { return true; }), "");
}

TEST(SafeUpload, NameAndCheck)
{
    EXPECT_EQ(SafeUploadName("a.txt"), "a.txt.filepart");
    EXPECT_EQ(SafeUploadName("caf\xE9"), "caf\xE9.filepart");
    EXPECT_TRUE(IsSafeUploadName("a.txt.filepart"));
    EXPECT_TRUE(IsSafeUploadName(SafeUploadName("x")));
    EXPECT_FALSE(IsSafeUploadName("a.txt"));
    EXPECT_FALSE(IsSafeUploadName(".filepart"));
    EXPECT_FALSE(IsSafeUploadName(""));
}
