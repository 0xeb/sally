// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#ifndef SALLY_DRAGDROP_DIAG_STANDALONE
#include "precomp.h"
#endif

#include "dragdrop_diag.h"
#include "common/DiagnosticTextEncoding.h"

#include <stdio.h>
#include <string>

// MK_ALT is a valid IDropTarget grfKeyState flag but is not in every SDK's MK_* set.
#ifndef MK_ALT
#define MK_ALT 0x0020
#endif

// Debug builds only.
//
// This existed to find why Alt+drag reported DROPEFFECT_NONE (#101). That is fixed and
// confirmed, so the diagnostic is a development aid now, not a product feature - a shipped
// build should carry neither the code nor its strings. Release gets the no-op stubs below,
// so call sites need no #ifdef of their own.
#ifdef _DEBUG

namespace
{
// Default log location. Derived from %TEMP% rather than hardcoded: an absolute path to one
// developer machine has no business in shipped source, and the sibling diagnostic in the
// webviewer plugin already does it this way.
const wchar_t* DefaultDiagPath()
{
    static std::wstring path;
    if (path.empty())
    {
        const DWORD required = GetTempPathW(0, NULL);
        if (required == 0)
            return NULL;
        path.assign(required, L'\0');
        const DWORD written = GetTempPathW(required, &path[0]);
        if (written == 0 || written >= required)
        {
            path.clear();
            return NULL;
        }
        path.resize(written);
        path.append(L"sally-dragdrop.log");
    }
    return path.c_str();
}

const wchar_t* DiagPath()
{
    static std::wstring path;
    static int state = 0; // 0 = unchecked, 1 = enabled, -1 = disabled
    if (state == 0)
    {
        const DWORD required = GetEnvironmentVariableW(L"SALLY_DRAGDROP_LOG", NULL, 0);
        if (required > 0)
        {
            std::wstring value(required, L'\0');
            const DWORD written = GetEnvironmentVariableW(L"SALLY_DRAGDROP_LOG", &value[0], required);
            if (written == 0 || written >= required)
                return NULL;
            value.resize(written);
            if (value == L"0")
                state = -1; // explicit opt-out wins in either configuration
            else
            {
                if (value == L"1")
                {
                    const wchar_t* def = DefaultDiagPath();
                    if (def == NULL)
                        return NULL; // no %TEMP% - stay disabled rather than guess a path
                    path = def;
                }
                else
                    path = value;
                state = 1;
            }
        }
        // Opt-in in every configuration.
        //
        // This defaulted ON in Debug while #101 was being investigated, because the log
        // silently never appeared when Sally is started from Visual Studio (no environment
        // variable), and that cost a round trip. The investigation is finished, so it goes back
        // to opt-in rather than writing a file on every drag of every debug session.
        //
        // To turn it on: set SALLY_DRAGDROP_LOG to a path, or to "1" for the default path
        // above. In Visual Studio that is Project > Properties > Debugging > Environment.
        else
        {
            state = -1;
        }
    }
    return state == 1 ? path.c_str() : NULL;
}

std::string Utf8ForLog(const wchar_t* text)
{
    if (text == NULL)
        return "(null)";
    std::string bytes;
    if (!sally::diagnostic::EncodeUtf8(text, bytes))
        bytes.clear();
    return bytes;
}

void AppendEffect(char* buf, int bufSize, DWORD effect)
{
    if (effect == 0)
    {
        lstrcpynA(buf, "NONE", bufSize);
        return;
    }
    buf[0] = 0;
    if (effect & DROPEFFECT_COPY)
        lstrcpynA(buf + strlen(buf), "COPY ", bufSize - (int)strlen(buf));
    if (effect & DROPEFFECT_MOVE)
        lstrcpynA(buf + strlen(buf), "MOVE ", bufSize - (int)strlen(buf));
    if (effect & DROPEFFECT_LINK)
        lstrcpynA(buf + strlen(buf), "LINK ", bufSize - (int)strlen(buf));
}
} // namespace

bool DragDropDiagEnabled()
{
    return DiagPath() != NULL;
}

void DragDropDiagRecord(const char* branch, DWORD keyState, DWORD effectIn, DWORD effectOut,
                        int tgtType, bool haveShellTarget, bool ownFolderDrop, bool tgtFile)
{
    const wchar_t* path = DiagPath();
    if (path == NULL)
        return;

    // DragOver fires on every mouse move; only a change is worth a line.
    static DWORD lastKey = 0xFFFFFFFF;
    static DWORD lastIn = 0xFFFFFFFF;
    static DWORD lastOut = 0xFFFFFFFF;
    static const char* lastBranch = NULL;
    if (keyState == lastKey && effectIn == lastIn && effectOut == lastOut && branch == lastBranch)
        return;
    lastKey = keyState;
    lastIn = effectIn;
    lastOut = effectOut;
    lastBranch = branch;

    char in[32], out[32];
    AppendEffect(in, (int)sizeof(in), effectIn);
    AppendEffect(out, (int)sizeof(out), effectOut);

    FILE* f = _wfopen(path, L"at");
    if (f == NULL)
        return;
    fprintf(f, "branch=%-12s key=0x%04X%s%s%s tgtType=%d shellTgt=%d ownFolder=%d tgtFile=%d "
               "allowedIn=[%s] effectOut=[%s]\n",
            branch, keyState,
            (keyState & MK_ALT) ? " ALT" : "",
            (keyState & MK_CONTROL) ? " CTRL" : "",
            (keyState & MK_SHIFT) ? " SHIFT" : "",
            tgtType, haveShellTarget ? 1 : 0, ownFolderDrop ? 1 : 0, tgtFile ? 1 : 0,
            in, out);
    fclose(f);
}

void DragDropDiagDataObject(IDataObject* dataObject, const wchar_t* curDir)
{
    const wchar_t* path = DiagPath();
    if (path == NULL)
        return;

    FILE* f = _wfopen(path, L"at");
    if (f == NULL)
        return;

    const std::string curDirUtf8 = Utf8ForLog(curDir);
    fprintf(f, "--- drag start: curDir=[%s] dataObject=%p\n", curDirUtf8.c_str(),
            (void*)dataObject);

    if (dataObject != NULL)
    {
        IEnumFORMATETC* enumFmt = NULL;
        if (dataObject->EnumFormatEtc(DATADIR_GET, &enumFmt) == S_OK && enumFmt != NULL)
        {
            FORMATETC fe;
            ULONG fetched = 0;
            int count = 0;
            while (enumFmt->Next(1, &fe, &fetched) == S_OK && fetched == 1 && count < 40)
            {
                char name[128];
                if (GetClipboardFormatNameA(fe.cfFormat, name, (int)sizeof(name)) == 0)
                    _snprintf_s(name, sizeof(name), _TRUNCATE, "(standard #%u)", fe.cfFormat);
                fprintf(f, "    offers: %s\n", name);
                if (fe.ptd != NULL)
                    CoTaskMemFree(fe.ptd);
                count++;
            }
            enumFmt->Release();
        }
        else
        {
            fprintf(f, "    offers: <EnumFormatEtc failed>\n");
        }

        // Direct probes for the two formats that decide whether the shell folder target can
        // offer LINK: plain file paths versus the PIDL array the shortcut path wants.
        struct
        {
            const char* label;
            UINT cf;
        } probes[] = {
            {"CF_HDROP", CF_HDROP},
            {"Shell IDList Array", RegisterClipboardFormatA("Shell IDList Array")},
            {"FileGroupDescriptorW", RegisterClipboardFormatA("FileGroupDescriptorW")},
            {"Preferred DropEffect", RegisterClipboardFormatA("Preferred DropEffect")},
        };
        for (int i = 0; i < (int)(sizeof(probes) / sizeof(probes[0])); i++)
        {
            FORMATETC fe;
            fe.cfFormat = (CLIPFORMAT)probes[i].cf;
            fe.ptd = NULL;
            fe.dwAspect = DVASPECT_CONTENT;
            fe.lindex = -1;
            fe.tymed = TYMED_HGLOBAL;
            HRESULT hr = dataObject->QueryGetData(&fe);
            fprintf(f, "    probe %-22s -> %s\n", probes[i].label,
                    hr == S_OK ? "PRESENT" : "absent");
        }
    }
    fclose(f);
}

#else // !_DEBUG

// Release: the diagnostic compiles away entirely. Enabled() is constant-false, so the call
// sites in CImpDropTarget optimise out and no log path or format string reaches the binary.
bool DragDropDiagEnabled()
{
    return false;
}

void DragDropDiagRecord(const char* branch, DWORD keyState, DWORD effectIn, DWORD effectOut,
                        int tgtType, bool haveShellTarget, bool ownFolderDrop, bool tgtFile)
{
    UNREFERENCED_PARAMETER(branch);
    UNREFERENCED_PARAMETER(keyState);
    UNREFERENCED_PARAMETER(effectIn);
    UNREFERENCED_PARAMETER(effectOut);
    UNREFERENCED_PARAMETER(tgtType);
    UNREFERENCED_PARAMETER(haveShellTarget);
    UNREFERENCED_PARAMETER(ownFolderDrop);
    UNREFERENCED_PARAMETER(tgtFile);
}

void DragDropDiagDataObject(IDataObject* dataObject, const wchar_t* curDir)
{
    UNREFERENCED_PARAMETER(dataObject);
    UNREFERENCED_PARAMETER(curDir);
}

#endif // _DEBUG
