// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

// ****************************************************************************
//
// CCounterDriver

class CCounterDriver
{
public:
    CCounterDriver(CCounter& counter) : Counter(counter)
    {
        Counter.HoldOff();
    }
    ~CCounterDriver()
    {
        Counter.HoldOn();
    }

private:
    CCounter& Counter;
};

extern CCounter TotalRuntime;
extern CCounter ShiftBoundaries;
extern CCounter Diff;
extern CCounter EditScriptBuilder;
