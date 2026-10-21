// SPDX-FileCopyrightText: 2025-2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#include "common/configuration/ConfigurationSearch.h"
#include "common/text/CaseFolding.h"
#include <cwctype>
#include <utility>

namespace sally::configuration
{
    SearchQuery SearchQuery::Compile(const std::wstring& text)
    {
        SearchQuery result;
        std::vector<Instruction> operators;
        bool expectsTerm = true;
        bool hasToken = false;
        const auto priority = [](Operation op)
        {
            return op == Operation::Not ? 3 : op == Operation::And ? 2
                                          : op == Operation::Or    ? 1
                                                                   : 0;
        };
        const auto fail = [&](SearchError error, std::size_t offset)
        {
            result.Error = error;
            result.ErrorOffset = offset;
            result.Program.clear();
        };
        const auto pushOperator = [&](Operation op, std::size_t offset)
        {
            while (!operators.empty() && operators.back().Op != Operation::Open &&
                   (priority(operators.back().Op) > priority(op) ||
                    (priority(operators.back().Op) == priority(op) && op != Operation::Not)))
            {
                result.Program.push_back(std::move(operators.back()));
                operators.pop_back();
            }
            operators.push_back({op, {}, offset});
        };

        for (std::size_t at = 0; at < text.size();)
        {
            if (iswspace(text[at]))
            {
                ++at;
                continue;
            }
            const std::size_t offset = at;
            const wchar_t first = text[at++];
            Operation op = Operation::Term;
            std::wstring word;
            bool quoted = false;
            if (first == L'(')
                op = Operation::Open;
            else if (first == L')')
            {
                if (expectsTerm)
                {
                    fail(SearchError::ExpectedTerm, offset);
                    return result;
                }
                while (!operators.empty() && operators.back().Op != Operation::Open)
                {
                    result.Program.push_back(std::move(operators.back()));
                    operators.pop_back();
                }
                if (operators.empty())
                {
                    fail(SearchError::UnexpectedClosingParenthesis, offset);
                    return result;
                }
                operators.pop_back();
                hasToken = true;
                continue;
            }
            else if (first == L'"')
            {
                quoted = true;
                bool closed = false;
                while (at < text.size())
                {
                    wchar_t ch = text[at++];
                    if (ch == L'"')
                    {
                        closed = true;
                        break;
                    }
                    if (ch == L'\\' && at < text.size() && (text[at] == L'\\' || text[at] == L'"'))
                        ch = text[at++];
                    word.push_back(ch);
                }
                if (!closed || word.empty())
                {
                    fail(closed ? SearchError::EmptyPhrase : SearchError::UnterminatedPhrase, offset);
                    return result;
                }
            }
            else
            {
                word.push_back(first);
                while (at < text.size() && !iswspace(text[at]) && text[at] != L'(' && text[at] != L')' && text[at] != L'"')
                    word.push_back(text[at++]);
            }
            if (op == Operation::Term)
            {
                word = sally::text::Fold(word);
                if (!quoted)
                {
                    if (word == L"NOT")
                        op = Operation::Not;
                    else if (word == L"AND")
                        op = Operation::And;
                    else if (word == L"OR")
                        op = Operation::Or;
                }
            }
            if (op == Operation::And || op == Operation::Or)
            {
                if (expectsTerm)
                {
                    fail(SearchError::ExpectedTerm, offset);
                    return result;
                }
                pushOperator(op, offset);
                expectsTerm = true;
            }
            else
            {
                if (!expectsTerm)
                    pushOperator(Operation::And, offset);
                if (op == Operation::Term)
                {
                    result.Program.push_back({op, std::move(word), offset});
                    expectsTerm = false;
                }
                else
                {
                    if (op == Operation::Open)
                        operators.push_back({op, {}, offset});
                    else
                        pushOperator(op, offset);
                    expectsTerm = true;
                }
            }
            hasToken = true;
        }
        if (hasToken && expectsTerm)
        {
            fail(SearchError::ExpectedTerm, text.size());
            return result;
        }
        while (!operators.empty())
        {
            if (operators.back().Op == Operation::Open)
            {
                fail(SearchError::MissingClosingParenthesis, operators.back().Offset);
                return result;
            }
            result.Program.push_back(std::move(operators.back()));
            operators.pop_back();
        }
        return result;
    }

    bool SearchQuery::Matches(const std::wstring& text) const
    {
        if (!IsValid())
            return false;
        if (IsEmpty())
            return true;
        const std::wstring folded = sally::text::Fold(text);
        std::vector<bool> values;
        for (const auto& instruction : Program)
        {
            if (instruction.Op == Operation::Term)
                values.push_back(folded.find(instruction.Text) != std::wstring::npos);
            else if (instruction.Op == Operation::Not)
            {
                if (values.empty())
                    return false;
                values.back() = !values.back();
            }
            else
            {
                if (values.size() < 2)
                    return false;
                const bool right = values.back();
                values.pop_back();
                const bool left = values.back();
                values.back() = instruction.Op == Operation::And ? left && right : left || right;
            }
        }
        return values.size() == 1 && values.back();
    }
} // namespace sally::configuration
