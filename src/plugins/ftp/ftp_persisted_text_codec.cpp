// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#include "ftp_persisted_text_codec.h"

bool FtpEncodePersistedText(const char* text, std::string& encoded) noexcept
{
    if (text == nullptr)
        return false;
    try
    {
        std::string staged;
        for (const char* current = text; *current != '\0'; ++current)
        {
            if (*current == '\r')
            {
                if (*(current + 1) == '\n')
                {
                    ++current;
                    staged.push_back('|');
                }
                else
                    staged.push_back('$');
            }
            else if (*current == '\n')
                staged.push_back('!');
            else
            {
                if (*current == '|' || *current == '!' || *current == '$' || *current == '\\')
                    staged.push_back('\\');
                staged.push_back(*current);
            }
        }
        encoded.swap(staged);
        return true;
    }
    catch (...)
    {
        return false;
    }
}

bool FtpDecodePersistedText(const char* encoded, std::string& text) noexcept
{
    if (encoded == nullptr)
        return false;
    try
    {
        std::string staged;
        for (const char* current = encoded; *current != '\0'; ++current)
        {
            if (*current == '\\')
            {
                if (*(current + 1) != '\0')
                    staged.push_back(*++current);
            }
            else if (*current == '|')
                staged.append("\r\n");
            else if (*current == '!')
                staged.push_back('\n');
            else if (*current == '$')
                staged.push_back('\r');
            else
                staged.push_back(*current);
        }
        text.swap(staged);
        return true;
    }
    catch (...)
    {
        return false;
    }
}
