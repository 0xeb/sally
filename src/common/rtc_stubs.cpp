// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-License-Identifier: GPL-2.0-or-later
// CommentsTranslationProject: TRANSLATED

//
// Stub implementations of MSVC Runtime Check functions
//
// These stubs are needed for no-CRT utilities (salspawn, salopen, fcremote)
// when building in Debug mode. Debug builds enable /RTC1 which generates
// calls to _RTC_* functions, but these utilities don't link against the CRT.
//
// The stubs are empty - runtime checks are not meaningful for these
// minimal utilities anyway.
//

// Note: Always compiled (not just _DEBUG) because these targets define NDEBUG
// unconditionally but CMake still enables /RTC1 in Debug configuration.

extern "C" {

// Called at function entry to initialize stack frame checking
void _RTC_InitBase(void)
{
}

// Called at program exit to report any runtime check failures
void _RTC_Shutdown(void)
{
}

// Called to check for stack buffer overruns
void _RTC_CheckStackVars(void* frame, void* rtc_var_desc)
{
    (void)frame;
    (void)rtc_var_desc;
}

// Called when an uninitialized local variable is used
void _RTC_UninitUse(const char* varname)
{
    (void)varname;
}

} // extern "C"
