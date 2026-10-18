// SPDX-FileCopyrightText: 2025-2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later
#include "ApplicationAppearance.h"
#include "DialogFont.h"
#include "LegacyConfigTextEncoding.h"
#include <cstring>
#include <limits>

namespace Sally::Appearance
{
namespace
{
// Private versioned Windows-only payload: DWORD metadata, Windows LOGFONTW,
// followed by owned UTF-16 strings. No pointers or native-size values on wire.
struct Wire
{
    DWORD Version, Icon, Dpi, Custom, ProfileChars, InstallChars;
    LOGFONTW Font;
};
static_assert(sizeof(Wire) == 116, "appearance wire must match on x86/x64/ARM64");
constexpr size_t MaxPayload = 1024 * 1024;
bool ValidFont(const LOGFONTW& font)
{
    return font.lfHeight >= -4096 && font.lfHeight <= 4096 && font.lfHeight != 0 &&
           font.lfWeight >= 0 && font.lfWeight <= 1000 &&
           font.lfFaceName[0] != 0 && std::wmemchr(font.lfFaceName, 0, LF_FACESIZE) != nullptr;
}
bool Same(const std::wstring& a, const std::wstring& b)
{
    return a.size() <= INT_MAX && b.size() <= INT_MAX &&
           CompareStringOrdinal(a.data(), static_cast<int>(a.size()), b.data(),
                                static_cast<int>(b.size()), TRUE) == CSTR_EQUAL;
}
} // namespace

int ValidIcon(int index) { return index >= 0 && index < IconCount ? index : 0; }
unsigned SystemDpi()
{
    HDC dc = GetDC(nullptr);
    const int dpi = dc ? GetDeviceCaps(dc, LOGPIXELSY) : 96;
    if (dc) ReleaseDC(nullptr, dc);
    return dpi > 0 ? static_cast<unsigned>(dpi) : 96;
}
std::wstring InstallIdentity(bool updater)
{
    std::wstring path(256, L'\0');
    for (;;)
    {
        const DWORD count = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
        if (!count) return {};
        if (count < path.size()) { path.resize(count); break; }
        if (path.size() > MAXDWORD / 2) return {};
        path.resize(path.size() * 2);
    }
    const auto parent = [&]()
    {
        const size_t slash = path.find_last_of(L"\\/");
        if (slash == std::wstring::npos) path.clear(); else path.resize(slash);
    };
    parent();
    if (updater) parent();
    return path;
}
Snapshot Read(const std::wstring& profile, const std::wstring& install, IRegistry& registry)
{
    Snapshot result;
    result.Profile = profile; result.Install = install; result.SourceDpi = SystemDpi();
    HKEY key = nullptr;
    if (!registry.OpenKeyRead(HKEY_CURRENT_USER, (profile + L"\\Configuration").c_str(), key).success)
        return result;
    DWORD index = 0, custom = 0;
    if (registry.GetDWord(key, L"Main window icon index", index).success && index < IconCount)
        result.Icon = static_cast<int>(index);
    if (registry.GetDWord(key, L"Use Custom Panel Font", custom).success && custom == 1)
    {
        std::wstring text;
        bool parsed = registry.GetString(key, L"Panel Font W", text).success &&
                      sally::legacy_config::ParseLogFont(text, result.Font);
        if (!parsed)
            parsed = registry.GetString(key, L"Panel Font", text).success &&
                     sally::legacy_config::ParseLogFont(text, result.Font);
        result.CustomFont = parsed && ValidFont(result.Font);
    }
    registry.CloseKey(key);
    return result;
}
LOGFONTW FontForDpi(const Snapshot& snapshot, unsigned dpi)
{
    LOGFONTW font = {};
    if (snapshot.CustomFont && ValidFont(snapshot.Font))
    {
        font = snapshot.Font;
        // Persisted heights already use system-DPI pixels, as in CreatePanelFont.
        if (snapshot.SourceDpi && dpi && snapshot.SourceDpi != dpi)
            font.lfHeight = MulDiv(font.lfHeight, dpi, snapshot.SourceDpi);
        return font;
    }
    return SystemDialogFont(dpi);
}
std::vector<BYTE> Encode(const Snapshot& snapshot)
{
    const size_t chars = snapshot.Profile.size() + snapshot.Install.size();
    if (chars > (MaxPayload - sizeof(Wire)) / sizeof(wchar_t)) return {};
    Wire wire = {};
    wire.Version = 1; wire.Icon = ValidIcon(snapshot.Icon);
    wire.Dpi = snapshot.SourceDpi; wire.Custom = snapshot.CustomFont ? 1 : 0;
    wire.Font = snapshot.Font;
    wire.ProfileChars = static_cast<DWORD>(snapshot.Profile.size());
    wire.InstallChars = static_cast<DWORD>(snapshot.Install.size());
    std::vector<BYTE> result(sizeof(wire) + chars * sizeof(wchar_t));
    std::memcpy(result.data(), &wire, sizeof(wire));
    std::memcpy(result.data() + sizeof(wire), snapshot.Profile.data(), snapshot.Profile.size() * sizeof(wchar_t));
    std::memcpy(result.data() + sizeof(wire) + snapshot.Profile.size() * sizeof(wchar_t),
                snapshot.Install.data(), snapshot.Install.size() * sizeof(wchar_t));
    return result;
}
bool Decode(const void* bytes, size_t length, Snapshot& snapshot)
{
    if (!bytes || length < sizeof(Wire) || length > MaxPayload) return false;
    Wire wire; std::memcpy(&wire, bytes, sizeof(wire));
    const uint64_t chars = static_cast<uint64_t>(wire.ProfileChars) + wire.InstallChars;
    if (wire.Version != 1 || wire.Icon >= IconCount || wire.Custom > 1 ||
        wire.Dpi < 48 || wire.Dpi > 768 || sizeof(wire) + chars * sizeof(wchar_t) != length ||
        (wire.Custom && !ValidFont(wire.Font))) return false;
    Snapshot candidate;
    candidate.Icon = wire.Icon; candidate.SourceDpi = wire.Dpi;
    candidate.CustomFont = wire.Custom != 0; candidate.Font = wire.Font;
    candidate.Profile.resize(wire.ProfileChars); candidate.Install.resize(wire.InstallChars);
    const BYTE* tail = static_cast<const BYTE*>(bytes) + sizeof(wire);
    std::memcpy(candidate.Profile.data(), tail, wire.ProfileChars * sizeof(wchar_t));
    std::memcpy(candidate.Install.data(), tail + wire.ProfileChars * sizeof(wchar_t), wire.InstallChars * sizeof(wchar_t));
    if (candidate.Profile.empty() || candidate.Install.empty() ||
        candidate.Profile.find(L'\0') != std::wstring::npos || candidate.Install.find(L'\0') != std::wstring::npos)
        return false;
    snapshot = std::move(candidate);
    return true;
}
bool Matches(const Snapshot& snapshot, const std::wstring& profile, const std::wstring& install)
{
    return Same(snapshot.Profile, profile) && Same(snapshot.Install, install);
}
void SetWindowIcon(HWND window, HINSTANCE module, int index)
{
    const auto icon = [&](int width, int height)
    {
        return reinterpret_cast<LPARAM>(LoadImageW(module, MAKEINTRESOURCEW(IconResources[ValidIcon(index)]),
                                                  IMAGE_ICON, width, height, LR_SHARED));
    };
    SendMessageW(window, WM_SETICON, ICON_BIG, icon(GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON)));
    SendMessageW(window, WM_SETICON, ICON_SMALL, icon(GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON)));
}
UINT RequestMessage() { static const UINT message = RegisterWindowMessageW(L"Sally.ApplicationAppearance.Request.v1"); return message; }
void Send(HWND sender, HWND receiver, const Snapshot& snapshot)
{
    const auto bytes = Encode(snapshot);
    if (bytes.empty()) return;
    COPYDATASTRUCT data = {CopyDataId, static_cast<DWORD>(bytes.size()), const_cast<BYTE*>(bytes.data())};
    DWORD_PTR result = 0;
    SendMessageTimeoutW(receiver, WM_COPYDATA, reinterpret_cast<WPARAM>(sender), reinterpret_cast<LPARAM>(&data),
                        SMTO_ABORTIFHUNG, 500, &result);
}
void Broadcast(HWND sender, const Snapshot& snapshot)
{
    struct Context { HWND Sender; const Snapshot* Value; } context{sender, &snapshot};
    EnumWindows([](HWND window, LPARAM data) -> BOOL
    {
        wchar_t name[128] = {}; // Win32 class identifiers, not semantic paths.
        GetClassNameW(window, name, _countof(name));
        if (wcsncmp(name, L"SallyUpdateWindow", 17) == 0)
        {
            const auto& context = *reinterpret_cast<const Context*>(data);
            Send(context.Sender, window, *context.Value);
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&context));
}
void Request(HWND receiver, HWND owner)
{
    if (!owner || !IsWindow(owner)) return;
    DWORD_PTR result = 0;
    SendMessageTimeoutW(owner, RequestMessage(), reinterpret_cast<WPARAM>(receiver), 0,
                        SMTO_ABORTIFHUNG, 500, &result);
}
} // namespace Sally::Appearance
