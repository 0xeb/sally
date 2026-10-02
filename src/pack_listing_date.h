// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

// #114: external archivers print each member's date as three numeric groups whose order the
// archiver table fixes (RAR 4 printed DD-MM-YY). Current RAR prints ISO YYYY-MM-DD, which the
// day-first table read as day 2026, so every member failed validation, triggered "The file
// date and/or time reported by archiver is invalid" and fell back to 1.1.1980.
//
// A four-digit leading group can only be a year, so it selects year-month-day regardless of
// the configured order; every other listing keeps the order from its table. Two-digit years
// are expanded as before (80-99 -> 19xx, 00-79 -> 20xx).
struct PackListingDate
{
    unsigned short Year;
    unsigned short Month;
    unsigned short Day;
};

// field     - the listing text starting at the date column (may continue past the date)
// dateYIdx  - 1-based position of the year group in the archiver table
// dateMIdx  - 1-based position of the month group; the remaining group is the day
inline PackListingDate ParsePackListingDate(const char* field, int dateYIdx, int dateMIdx)
{
    unsigned short groups[3] = {0, 0, 0};
    int firstGroupDigits = 0;
    for (int i = 0; i < 3; i++)
    {
        int digits = 0;
        while (*field >= '0' && *field <= '9')
        {
            groups[i] = (unsigned short)(groups[i] * 10 + (*field - '0'));
            field++;
            digits++;
        }
        if (i == 0)
            firstGroupDigits = digits;
        if (*field != '\0') // skip the separator, never the terminator
            field++;
    }

    if (firstGroupDigits == 4)
    {
        dateYIdx = 1;
        dateMIdx = 2;
    }

    PackListingDate date = {0, 0, 0};
    for (int i = 1; i <= 3; i++)
    {
        if (dateYIdx == i)
            date.Year = groups[i - 1];
        else if (dateMIdx == i)
            date.Month = groups[i - 1];
        else
            date.Day = groups[i - 1];
    }

    if (date.Year < 100)
        date.Year = (unsigned short)(date.Year + (date.Year >= 80 ? 1900 : 2000));
    return date;
}
