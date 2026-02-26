// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

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

// Note: Explicit calling conventions are required because some targets
// (e.g. salext) use /Gz (default stdcall), but the compiler emits RTC
// calls with fixed calling conventions regardless of the /G? switch.

// Called at function entry to initialize stack frame checking
void __cdecl _RTC_InitBase(void)
{
}

// Called at program exit to report any runtime check failures
void __cdecl _RTC_Shutdown(void)
{
}

// Called to verify ESP is preserved across function calls (x86 only)
void __cdecl _RTC_CheckEsp(void)
{
}

// Called to check for stack buffer overruns
#ifdef _M_IX86
void __fastcall _RTC_CheckStackVars(void* frame, void* rtc_var_desc)
#else
void _RTC_CheckStackVars(void* frame, void* rtc_var_desc)
#endif
{
    (void)frame;
    (void)rtc_var_desc;
}

// Called when an uninitialized local variable is used
void __cdecl _RTC_UninitUse(const char* varname)
{
    (void)varname;
}

} // extern "C"
