// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"

#include "path_suggestions.h"

#include "cfgdlg.h"
#include "mainwnd.h"
#include "plugins.h"
#include "fileswnd.h"
#include "common/IFileEnumerator.h"
#include "common/PathCompletion.h"

#include <shldisp.h>
#include <shlobj.h>

#include <mutex>
#include <string>
#include <vector>

namespace
{

// The strings Windows' autocomplete offers. It calls this object on a worker thread of its own:
// IACList::Expand when the typed text reaches a separator, then Reset and Next. The folder is
// listed there, never on the dialog's thread, so a slow network folder cannot stall typing.
class CPathSuggestionSource final : public IEnumString, public IACList
{
public:
    explicit CPathSuggestionSource(std::wstring baseDirectory)
        : BaseDirectory(std::move(baseDirectory))
    {
    }

    // IUnknown
    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override
    {
        if (ppv == nullptr)
            return E_POINTER;
        *ppv = nullptr;
        if (riid == IID_IUnknown || riid == IID_IEnumString)
            *ppv = static_cast<IEnumString*>(this);
        else if (riid == IID_IACList)
            *ppv = static_cast<IACList*>(this);
        else
            return E_NOINTERFACE;
        AddRef();
        return S_OK;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return (ULONG)InterlockedIncrement(&Refs); }
    STDMETHODIMP_(ULONG) Release() override
    {
        const LONG refs = InterlockedDecrement(&Refs);
        if (refs == 0)
            delete this;
        return (ULONG)refs;
    }

    // IACList
    STDMETHODIMP Expand(PCWSTR root) override
    {
        std::lock_guard<std::mutex> lock(Lock);
        Head = root != nullptr ? root : L"";
        FillLocked();
        return S_OK;
    }

    // IEnumString
    STDMETHODIMP Next(ULONG count, LPOLESTR* strings, ULONG* fetched) override
    {
        if (strings == nullptr || (count > 1 && fetched == nullptr))
            return E_POINTER;
        std::lock_guard<std::mutex> lock(Lock);
        if (!Filled)
            FillLocked();
        ULONG done = 0;
        while (done < count && Position < Items.size())
        {
            const std::wstring& item = Items[Position];
            const size_t bytes = (item.size() + 1) * sizeof(wchar_t);
            LPOLESTR copy = static_cast<LPOLESTR>(CoTaskMemAlloc(bytes));
            if (copy == nullptr)
                break;
            memcpy(copy, item.c_str(), bytes);
            strings[done++] = copy;
            Position++;
        }
        if (fetched != nullptr)
            *fetched = done;
        return done == count ? S_OK : S_FALSE;
    }
    STDMETHODIMP Skip(ULONG count) override
    {
        std::lock_guard<std::mutex> lock(Lock);
        Position += count;
        return Position <= Items.size() ? S_OK : S_FALSE;
    }
    STDMETHODIMP Reset() override
    {
        std::lock_guard<std::mutex> lock(Lock);
        Position = 0;
        return S_OK;
    }
    STDMETHODIMP Clone(IEnumString** clone) override
    {
        if (clone != nullptr)
            *clone = nullptr;
        return E_NOTIMPL;
    }

private:
    void FillLocked()
    {
        using namespace Sally::PathCompletion;
        Items = gFileEnumerator != nullptr
                    ? ListFolderCandidates(*gFileEnumerator, Resolve(Head, BaseDirectory), MaxSuggestions)
                    : std::vector<std::wstring>();
        Position = 0;
        Filled = true;
    }

    LONG Refs = 1;
    std::mutex Lock;
    const std::wstring BaseDirectory;
    std::wstring Head;
    std::vector<std::wstring> Items;
    size_t Position = 0;
    bool Filled = false;
};

HWND EditOf(HWND control)
{
    wchar_t className[16] = {};
    GetClassNameW(control, className, (int)(sizeof(className) / sizeof(className[0])));
    if (_wcsicmp(className, L"Edit") == 0)
        return control;
    COMBOBOXINFO info = {sizeof(info)};
    return GetComboBoxInfo(control, &info) ? info.hwndItem : NULL;
}

} // namespace

void AttachPathSuggestions(HWND dialog, int controlId)
{
    if (!Configuration.SuggestPathNames)
        return;
    HWND edit = EditOf(GetDlgItem(dialog, controlId));
    if (edit == NULL)
        return;

    std::wstring baseDirectory;
    CFilesWindow* panel = MainWindow != NULL ? MainWindow->GetActivePanel() : NULL;
    if (panel != NULL && panel->Is(ptDisk))
        baseDirectory = panel->GetPathW();

    IAutoComplete2* autoComplete = NULL;
    if (FAILED(CoCreateInstance(CLSID_AutoComplete, NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&autoComplete))))
        return;
    CPathSuggestionSource* source = new CPathSuggestionSource(baseDirectory);
    // The drop-down only: completed text is never typed for the user (no inline append).
    if (SUCCEEDED(autoComplete->Init(edit, static_cast<IEnumString*>(source), NULL, NULL)))
        autoComplete->SetOptions(ACO_AUTOSUGGEST | ACO_UPDOWNKEYDROPSLIST);
    source->Release();       // the autocomplete object keeps its own reference
    autoComplete->Release(); // and lives as long as the edit control
}
