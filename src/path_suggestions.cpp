// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"

#include "path_suggestions.h"

#include "cfgdlg.h"
#include "mainwnd.h"
#include "plugins.h"
#include "fileswnd.h"
#include "pack.h"
#include "zip.h" // CSalamanderDirectory: the archive panel listing copied for archive folder suggestions
#include "common/IFileEnumerator.h"
#include "common/PathCompletion.h"

#include <shldisp.h>
#include <shlobj.h>

#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace
{

// The dialog manager handles Enter/Escape before forwarding key messages to an edit. When
// a suggestion is visible, let autocomplete consume that key instead of submitting/canceling
// the dialog (F7, Copy/Move of one file). The COM reference belongs to this edit subclass,
// not to a dialog/global registry.
LRESULT CALLBACK SuggestionAcceptEditProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam,
                                          UINT_PTR subclassId, DWORD_PTR reference)
{
    IAutoCompleteDropDown* dropdown = reinterpret_cast<IAutoCompleteDropDown*>(reference);
    if (message == WM_NCDESTROY)
    {
        RemoveWindowSubclass(window, SuggestionAcceptEditProc, subclassId);
        const LRESULT result = DefSubclassProc(window, message, wParam, lParam);
        dropdown->Release();
        return result;
    }
    DWORD beforeStatus = 0;
    const bool accepting = message == WM_KEYDOWN && wParam == VK_RETURN &&
                           SUCCEEDED(dropdown->GetDropDownStatus(&beforeStatus, nullptr)) &&
                           (beforeStatus & ACDD_VISIBLE) != 0;
    const LRESULT result = DefSubclassProc(window, message, wParam, lParam);
    if (accepting)
    {
        // Autocomplete leaves its completion selected. F7 offers a parent, so typing next
        // must append the new leaf instead of replacing the accepted parent path; an accepted
        // file name is likewise edited at its end.
        const LRESULT length = SendMessageW(window, WM_GETTEXTLENGTH, 0, 0);
        SendMessageW(window, EM_SETSEL, static_cast<WPARAM>(length), length);
    }
    if (message == WM_GETDLGCODE && lParam != 0)
    {
        const MSG* key = reinterpret_cast<const MSG*>(lParam);
        if (key->message == WM_KEYDOWN && (key->wParam == VK_RETURN || key->wParam == VK_ESCAPE))
        {
            DWORD status = 0;
            if (SUCCEEDED(dropdown->GetDropDownStatus(&status, nullptr)) && (status & ACDD_VISIBLE) != 0)
                return result | DLGC_WANTMESSAGE;
        }
    }
    return result;
}

void KeepSuggestionKeysInEdit(HWND edit, IAutoComplete2* autocomplete)
{
    IAutoCompleteDropDown* dropdown = nullptr;
    if (SUCCEEDED(autocomplete->QueryInterface(IID_PPV_ARGS(&dropdown))))
    {
        if (!SetWindowSubclass(edit, SuggestionAcceptEditProc, 1, reinterpret_cast<DWORD_PTR>(dropdown)))
            dropdown->Release();
    }
}

// Recognized archives are left out of file suggestions. The autocomplete lists on its own
// worker thread, which may still run after the dialog closes, while plugin loading and the
// Configuration dialog rebuild the global format table on the UI thread. So the worker gets a
// private copy taken on the UI thread when the dialog attaches.
std::function<bool(const std::wstring&)> ArchiveFilterSnapshot()
{
    auto formats = std::make_shared<CPackerFormatConfig>();
    if (!formats->Load(PackerFormatConfig))
        return {};
    // A duplicate extension makes BuildArray report failure after building every other
    // entry; Sally's own table is used the same way, so the copy recognizes what Sally does.
    formats->BuildArray();
    return [formats](const std::wstring& name) { return formats->PackIsArchive(name.c_str()) != 0; };
}

// The folders of the archives open in the panels, copied here on the UI thread (the
// autocomplete lists on its own thread, and a panel replaces its listing when it refreshes). Read-only
// walk of the panel's tree, bounded by MaxArchiveFolders; the same archive in both panels is copied once.
std::shared_ptr<Sally::PathCompletion::ArchiveFolderEnumerator> ArchiveFoldersSnapshot()
{
    using namespace Sally::PathCompletion;
    std::vector<ArchiveFolderTree> archives;
    if (MainWindow == NULL)
        return {};
    for (CFilesWindow* panel : {MainWindow->LeftPanel, MainWindow->RightPanel})
    {
        if (panel == NULL || !panel->Is(ptZIPArchive) || panel->GetArchiveDir() == NULL)
            continue;
        const std::wstring archive = panel->GetZIPArchive();
        bool known = false;
        for (const ArchiveFolderTree& tree : archives)
            known = known || FoldArchiveKey(tree.Archive) == FoldArchiveKey(archive);
        if (known || archive.empty())
            continue;
        ArchiveFolderTree tree;
        tree.Archive = archive;
        size_t copied = 0;
        std::vector<std::pair<std::wstring, const CSalamanderDirectoryAbstract*>> pending;
        pending.emplace_back(std::wstring(), panel->GetArchiveDir());
        while (!pending.empty() && copied < MaxArchiveFolders)
        {
            const auto [inner, dir] = pending.back();
            pending.pop_back();
            std::vector<std::wstring>& names = tree.Children[FoldArchiveKey(inner)];
            const int count = dir->GetDirsCount();
            for (int i = 0; i < count && copied < MaxArchiveFolders; i++)
            {
                const CFileData* folder = dir->GetDir(i);
                const CSalamanderDirectoryAbstract* sub = dir->GetSalDir(i);
                if (folder == NULL || folder->Name == NULL)
                    continue;
                const std::wstring name(folder->Name, folder->NameLen);
                names.push_back(name);
                copied++;
                if (sub != NULL)
                    pending.emplace_back(inner.empty() ? name : inner + L"\\" + name, sub);
            }
        }
        archives.push_back(std::move(tree));
    }
    if (archives.empty())
        return {};
    return std::make_shared<ArchiveFolderEnumerator>(std::move(archives));
}

// The strings Windows' autocomplete offers. It calls this object on a worker thread of its own:
// IACList::Expand when the typed text reaches a separator, then Reset and Next. The folder is
// listed there, never on the dialog's thread, so a slow network folder cannot stall typing.
class CPathSuggestionSource final : public IEnumString, public IACList
{
public:
    CPathSuggestionSource(std::wstring baseDirectory, Sally::PathCompletion::CandidateOptions options,
                          std::shared_ptr<Sally::PathCompletion::ArchiveFolderEnumerator> archives = {})
        : BaseDirectory(std::move(baseDirectory)), Options(std::move(options)), Archives(std::move(archives))
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
        const Target target = Resolve(Head, BaseDirectory);
        if (Archives != nullptr && Archives->Claims(target.Directory))
            Items = ListCandidates(*Archives, target, MaxSuggestions, Options); // inside an open archive
        else
            Items = gFileEnumerator != nullptr ? ListCandidates(*gFileEnumerator, target, MaxSuggestions, Options)
                                               : std::vector<std::wstring>();
        Position = 0;
        Filled = true;
    }

    LONG Refs = 1;
    std::mutex Lock;
    const std::wstring BaseDirectory;
    const Sally::PathCompletion::CandidateOptions Options;
    const std::shared_ptr<Sally::PathCompletion::ArchiveFolderEnumerator> Archives; // immutable snapshot
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

void AttachPathSuggestions(HWND dialog, int controlId, const PathSuggestionOptions& options)
{
    using Sally::PathCompletion::CandidateKinds;
    if (!Configuration.SuggestPathNames)
        return;
    HWND edit = EditOf(GetDlgItem(dialog, controlId));
    if (edit == NULL)
        return;

    std::wstring baseDirectory;
    if (options.BaseDirectory.has_value())
        baseDirectory = *options.BaseDirectory;
    else
    {
        CFilesWindow* panel = MainWindow != NULL ? MainWindow->GetActivePanel() : NULL;
        if (panel != NULL && panel->Is(ptDisk))
            baseDirectory = panel->GetPathW();
    }

    Sally::PathCompletion::CandidateOptions candidates;
    candidates.Kinds = options.Kinds;
    candidates.AppendFolderSeparator = options.AppendFolderSeparator;
    if (candidates.Kinds == CandidateKinds::FoldersAndFiles)
    {
        candidates.ExcludeFile = ArchiveFilterSnapshot();
        if (!candidates.ExcludeFile)
            candidates.Kinds = CandidateKinds::Folders; // never offer an archive unfiltered
    }

    IAutoComplete2* autoComplete = NULL;
    if (FAILED(CoCreateInstance(CLSID_AutoComplete, NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&autoComplete))))
        return;
    CPathSuggestionSource* source = new CPathSuggestionSource(
        baseDirectory, std::move(candidates), options.ArchiveFolders ? ArchiveFoldersSnapshot() : nullptr);
    // The drop-down only: completed text is never typed for the user (no inline append).
    if (SUCCEEDED(autoComplete->Init(edit, static_cast<IEnumString*>(source), NULL, NULL)))
    {
        autoComplete->SetOptions(ACO_AUTOSUGGEST | ACO_UPDOWNKEYDROPSLIST);
        if (options.AcceptOnlyWhileVisible)
            KeepSuggestionKeysInEdit(edit, autoComplete);
    }
    source->Release();       // the autocomplete object keeps its own reference
    autoComplete->Release(); // and lives as long as the edit control
}

void AttachPathSuggestions(HWND dialog, int controlId, bool appendSeparator)
{
    PathSuggestionOptions options;
    options.AppendFolderSeparator = appendSeparator;
    options.AcceptOnlyWhileVisible = appendSeparator;
    AttachPathSuggestions(dialog, controlId, options);
}
