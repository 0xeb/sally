// SPDX-FileCopyrightText: 2025-2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace sally::configuration
{
    enum class SearchError
    {
        None,
        ExpectedTerm,
        UnterminatedPhrase,
        EmptyPhrase,
        UnexpectedClosingParenthesis,
        MissingClosingParenthesis,
    };

    // UI-free Boolean query over translated option metadata. Values entered by the
    // user (paths, passwords, histories) do not belong in the searchable metadata.
    class SearchQuery
    {
    public:
        static SearchQuery Compile(const std::wstring& text);
        bool IsValid() const { return Error == SearchError::None; }
        bool IsEmpty() const { return Program.empty() && IsValid(); }
        SearchError GetError() const { return Error; }
        std::size_t GetErrorOffset() const { return ErrorOffset; }
        bool Matches(const std::wstring& text) const;

    private:
        enum class Operation
        {
            Term,
            Not,
            And,
            Or,
            Open
        };
        struct Instruction
        {
            Operation Op;
            std::wstring Text;
            std::size_t Offset;
        };

        std::vector<Instruction> Program;
        SearchError Error = SearchError::None;
        std::size_t ErrorOffset = 0;
    };
} // namespace sally::configuration
