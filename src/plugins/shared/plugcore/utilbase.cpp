// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"
#include "plugin_narrow_compat.h"

// ****************************************************************************

HINSTANCE DLLInstance = NULL; // handle to SPL - language-independent resources
HINSTANCE HLanguage = NULL;   // handle to SLG - language-dependent resources
BOOL WindowsVistaAndLater;    // Windows Vista or later in the NT line (6.0+)
BOOL WindowsXP64AndLater;     // Windows XP 64, Vista or later (5.2+)

// Open Salamander interfaces - valid from InitUtils() call until
// plugin shutdown
CSalamanderGeneralAbstract* SG = NULL;
CSalamanderGUIAbstract* SalGUI = NULL;

// variable definition for "dbg.h"
CSalamanderDebugAbstract* SalamanderDebug = NULL;

// variable definition for "spl_com.h"
int SalamanderVersion = 0;

DWORD MainThreadID;

CDialogStack DialogStack;

BOOL WINAPI
DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved)
{
    CALL_STACK_MESSAGE_NONE
    switch (fdwReason)
    {
    case DLL_PROCESS_ATTACH:
    {
        DLLInstance = hinstDLL;
        break;
    }

    case DLL_PROCESS_DETACH:
    {
        break;
    }
    }
    return TRUE; // DLL can be loaded
}

BOOL InitLCUtils(CSalamanderPluginEntryAbstract* salamander, const char* pluginName)
{
    CALL_STACK_MESSAGE_NONE

    // set SalamanderDebug for "dbg.h"
    SalamanderDebug = salamander->GetSalamanderDebug();

    // set SalamanderVersion for "spl_com.h"
    SalamanderVersion = salamander->GetVersion();

    CALL_STACK_MESSAGE1("InitLCUtils()");

    // this plugin targets the current Salamander version and newer - perform the check
    if (SalamanderVersion < LAST_VERSION_OF_SALAMANDER)
    { // cannot call Error here because it uses SG->SalMessageBox (SG not initialized + incompatible interface)
        // wide: REQUIRE_LAST_VERSION_OF_SALAMANDER is a shared narrow SDK macro
        // and pluginName is a caller-supplied narrow string - no SalamanderGeneral yet, so widen
        // via MessageBoxW+ToWideArg (plugin_narrow_compat.h, already included above).
        MessageBoxW(salamander->GetParentWindow(),
                    ToWideArg(REQUIRE_LAST_VERSION_OF_SALAMANDER).c_str(),
                    ToWideArg(pluginName).c_str(), MB_OK | MB_ICONERROR);
        return FALSE;
    }

    // load language module (.slg)
    HLanguage = salamander->LoadLanguageModule(salamander->GetParentWindow(), ToWideArg(pluginName).c_str());
    if (HLanguage == NULL)
        return FALSE;

    // get Salamander interfaces
    SG = salamander->GetSalamanderGeneral();
    SalGUI = salamander->GetSalamanderGUI();

    // determine which OS we are running on
    WindowsXP64AndLater = SalIsWindowsVersionOrGreater(5, 2, 0);
    WindowsVistaAndLater = SalIsWindowsVersionOrGreater(6, 0, 0);

    MainThreadID = GetCurrentThreadId();

    return TRUE;
}

void ReleaseLCUtils()
{
    CALL_STACK_MESSAGE_NONE
}

std::wstring LangStr(int resID)
{
    return SPLLoadStrOwned(SG, HLanguage, resID);
}

BOOL ErrorHelper(HWND parent, const wchar_t* message, int lastError, va_list arglist)
{
    CALL_STACK_MESSAGE3("ErrorHelper(, %S, %d, )", message, lastError);
    // Wide throughout. `message` is a resource string used as a printf FORMAT, so
    // widening LangStr without widening the buffer and the vprintf would have handed a wchar_t*
    // format to the narrow vprintf - which compiles and prints garbage. The FormatMessage that appends the
    // system error text follows for the same reason.
    wchar_t buf[1024];
    *buf = 0;
    _vsnwprintf_s(buf, _countof(buf), _TRUNCATE, message, arglist);
    if (lastError != ERROR_SUCCESS)
    {
        int l = lstrlenW(buf);
        FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL, lastError,
                       MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), buf + l, _countof(buf) - l, NULL);
    }
    if (SG)
    {
        if (parent == HWND(-1))
            parent = GetCurrentThreadId() == MainThreadID ? SG->GetMsgBoxParent() : NULL;
        SG->SalMessageBox(parent, buf, SPLLoadStrOwned(SG, HLanguage, IDS_SPLERROR).c_str(), MB_OK | MB_ICONERROR | (parent == NULL && AlwaysOnTop ? MB_TOPMOST : 0));
    }
    else
    {
        // Reachable before InitLCUtils has a SalamanderGeneral - the plugin's own resource
        // module is loaded by then, but nothing else is.
        if (parent == HWND(-1))
            parent = 0;
        MessageBoxW(parent, buf, LangStr(IDS_SPLERROR).c_str(), MB_OK | MB_ICONERROR | (parent == NULL && AlwaysOnTop ? MB_TOPMOST : 0));
    }
    return FALSE;
}

BOOL Error(HWND parent, int error, ...)
{
    CALL_STACK_MESSAGE_NONE
    int lastError = GetLastError();
    CALL_STACK_MESSAGE2("Error(, %d, )", error);
    va_list arglist;
    va_start(arglist, error);
    BOOL ret = ErrorHelper(parent, LangStr(error).c_str(), lastError, arglist);
    va_end(arglist);
    return ret;
}

BOOL Error(HWND parent, const wchar_t* error, ...)
{
    CALL_STACK_MESSAGE_NONE
    int lastError = GetLastError();
    CALL_STACK_MESSAGE2("Error(, %ls, )", error);
    va_list arglist;
    va_start(arglist, error);
    BOOL ret = ErrorHelper(parent, error, lastError, arglist);
    va_end(arglist);
    return ret;
}

BOOL Error(int error, ...)
{
    CALL_STACK_MESSAGE_NONE
    int lastError = GetLastError();
    CALL_STACK_MESSAGE2("Error(%d, )", error);
    va_list arglist;
    va_start(arglist, error);
    BOOL ret = ErrorHelper(DialogStack.Peek(), LangStr(error).c_str(), lastError, arglist);
    va_end(arglist);
    return ret;
}

BOOL ErrorL(int lastError, HWND parent, int error, ...)
{
    CALL_STACK_MESSAGE3("ErrorL(%d, , %d, )", lastError, error);
    va_list arglist;
    va_start(arglist, error);
    BOOL ret = ErrorHelper(parent, LangStr(error).c_str(), lastError, arglist);
    va_end(arglist);
    return ret;
}

BOOL ErrorL(int lastError, int error, ...)
{
    CALL_STACK_MESSAGE3("ErrorL(%d, %d, )", lastError, error);
    va_list arglist;
    va_start(arglist, error);
    BOOL ret = ErrorHelper(DialogStack.Peek(), LangStr(error).c_str(), lastError, arglist);
    va_end(arglist);
    return ret;
}

int SalPrintf(char* buffer, unsigned count, const char* format, ...)
{
    CALL_STACK_MESSAGE_NONE
    va_list arglist;
    va_start(arglist, format);
    int ret = _vsnprintf_s(buffer, count, _TRUNCATE, format, arglist);
    va_end(arglist);
    return ret;
}

const char*
FormatString(const char* format, ...)
{
    CALL_STACK_MESSAGE_NONE
    static char buffer[5120];
    static int iterator = 0;

    if (iterator > 4096)
        iterator = 0;
    char* actual = buffer + iterator;
    int size = 5120 - iterator;

    va_list arglist;
    va_start(arglist, format);
    int ret = _vsnprintf_s(actual, size, _TRUNCATE, format, arglist);
    iterator += ret >= 0 ? ret + 1 : size;
    va_end(arglist);

    actual[size - 1] = 0;
    return actual;
}

const char*
Concatenate(const char* string1, const char* string2)
{
    CALL_STACK_MESSAGE_NONE
    static char buffer[5120];
    static int iterator = 0;

    int len1 = lstrlenA(string1);
    int len2 = lstrlenA(string2);

    if (len1 + len2 >= 5120)
        return "STRING TOO LONG";
    if (iterator + len1 + len2 >= 5120)
        iterator = 0;

    const char* ret = buffer + iterator;
    memcpy(buffer + iterator, string1, len1);
    iterator += len1;
    memcpy(buffer + iterator, string2, len2);
    iterator += len2;

    buffer[iterator++] = 0;
    return ret;
}

// ****************************************************************************
//
// CDialogStack
//

void CDialogStack::Push(HWND hWindow)
{
    CALL_STACK_MESSAGE1("CDialogStack::Push()");
    CS.Enter();
    Stack.Add(uintptr_t(hWindow));
    Stack.Add(GetCurrentThreadId());
    CS.Leave();
}

void CDialogStack::Pop()
{
    CALL_STACK_MESSAGE1("CDialogStack::Pop()");
    CS.Enter();
    int i = Stack.Count - 1;
    while (i > 0)
    {
        if (Stack[i] == GetCurrentThreadId())
        {
            Stack.Delete(i--);
            Stack.Delete(i);
            break;
        }
        i -= 2;
    }
    CS.Leave();
}

HWND CDialogStack::Peek()
{
    CALL_STACK_MESSAGE1("CDialogStack::Peek()");
    CS.Enter();
    HWND ret = (HWND)-1;
    int i = Stack.Count - 1;
    while (i > 0)
    {
        if (Stack[i] == GetCurrentThreadId())
        {
            ret = HWND(Stack[--i]);
            break;
        }
        i -= 2;
    }
    CS.Leave();
    return ret;
}

HWND CDialogStack::GetParent()
{
    CALL_STACK_MESSAGE1("CDialogStack::GetParent()");
    HWND ret = Peek();
    if (ret == (HWND)-1)
        ret = GetCurrentThreadId() == MainThreadID ? SG->GetMsgBoxParent() : NULL;
    return ret;
}
