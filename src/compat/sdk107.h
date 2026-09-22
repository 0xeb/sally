// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

// sdk107 — the frozen v107 plugin SDK.
//
// WHAT THIS IS
// A byte-for-byte snapshot of the plugin SDK headers as they stood at
// sally commit b1de5d4adae8e3d0bc52e680ab60a40cb128f399, wrapped in
// `namespace sdk107` so it can coexist in one translation unit with the LIVE
// SDK — which is exactly what the legacy adapter needs: it speaks v107 to old
// binary plugins on one side and the current ABI to the core on the other.
//
// WHY IT MUST NEVER CHANGE
// Binary plugins built against v107 have that layout compiled into them. Once
// the live SDK widens at v108, this snapshot is the ONLY remaining
// description of what those binaries believe. Editing it would not "update" any
// plugin; it would silently break the adapter's marshalling and corrupt data at
// the boundary. gtest_plugin_abi hash-pins every file here for that reason: a
// diff fails the gate with "the frozen SDK is immutable".
//
// HOW TO USE IT
//   #include "compat/sdk107.h"        // types become sdk107::CFileData, ...
//   #include "plugins/shared/spl_com.h"  // live types stay unqualified
// Both in the same TU is the intended, tested arrangement.
//
// WHAT IT IS NOT
// It is not a place to fix bugs, not a second copy to keep in sync, and not the
// build's SDK. Nothing outside sally/src/compat/ may include it.

#pragma once

#include <windows.h>

// The SDK headers assume these are already available at global scope; pulling
// them in outside the namespace keeps the snapshot's own text untouched.
#include <commctrl.h>
#include <shlobj.h>

namespace sdk107
{

// The historical SDK was built without UNICODE. These TCHAR aliases must stay
// byte-based even after the live host later defines UNICODE globally; otherwise
// the snapshot text's LPCTSTR/LPTSTR slots would silently change C++ type while
// the already-compiled v107 binary vtables remain narrow.
using LPCTSTR = const char*;
using LPTSTR = char*;

// Verbatim snapshot. Order follows the SDK's own dependency chain: spl_com.h
// defines the shared value types, spl_base.h the interfaces, the rest layer on
// top. The files are unmodified — every edit here must instead become a change
// in the LIVE SDK plus an adapter update.
#include "compat/sdk107/spl_com.h"
#include "compat/sdk107/spl_base.h"
#include "compat/sdk107/spl_gen.h"
#include "compat/sdk107/spl_gui.h"
#include "compat/sdk107/spl_menu.h"
#include "compat/sdk107/spl_file.h"
#include "compat/sdk107/spl_fs.h"
#include "compat/sdk107/spl_arc.h"
#include "compat/sdk107/spl_view.h"
#include "compat/sdk107/spl_thum.h"
#include "compat/sdk107/spl_zlib.h"
#include "compat/sdk107/spl_bzip2.h"
#include "compat/sdk107/spl_crypt.h"
// spl_vers.h uses a NAMED include guard (__SPL_VERS_H) rather than #pragma once,
// because the resource compiler includes it and does not support #pragma once.
// That guard is a real hazard for a frozen snapshot: the frozen copy defining
// __SPL_VERS_H would make the LIVE spl_vers.h expand to NOTHING later in the same
// translation unit, silently taking the live version constants with it. Save the
// guard state, let the frozen copy in, then hand the name back.
#ifdef __SPL_VERS_H
#define SDK107_SPL_VERS_GUARD_WAS_SET
#endif
#undef __SPL_VERS_H
#undef VERSINFO_SALAMANDER_MINORB
#undef VERSINFO_SALAMANDER_VERSION
#undef VERSINFO_SAL_SHORT_VERSION
#undef LAST_VERSION_OF_SALAMANDER
#include "compat/sdk107/spl_vers.h"
#undef __SPL_VERS_H
#ifdef SDK107_SPL_VERS_GUARD_WAS_SET
#define __SPL_VERS_H
#undef SDK107_SPL_VERS_GUARD_WAS_SET
#endif

// ---------------------------------------------------------------------------
// MACROS ESCAPE THIS NAMESPACE — read before writing adapter code.
//
// `namespace sdk107 { }` scopes TYPES and functions. It does NOT scope
// preprocessor macros: the snapshot's ~500 #defines land at global scope exactly
// as the live SDK's do, and there is no `sdk107::SOME_MACRO` to disambiguate
// them. Today every value is identical so nothing shows; at v108 the live values
// diverge and the second #define silently wins by include order.
//
// THE RULE: the adapter may depend on frozen TYPES (which are scoped and
// therefore safe) but never on frozen MACROS. Any frozen constant the adapter
// needs is captured here as a real namespaced constant while the frozen
// definition is still the active one, and then #undef'd so the live SDK defines
// its own version cleanly.
// ---------------------------------------------------------------------------

// The ABI version this snapshot describes. Captured as a constant precisely
// because the macro cannot be namespaced.
constexpr int LastVersionOfSalamander = LAST_VERSION_OF_SALAMANDER;
static_assert(LastVersionOfSalamander == 107,
              "sdk107 must describe ABI version 107 - the wrong tree was copied");
constexpr int ArchiveNameWVersion = SALLY_PLUGIN_ARCHIVE_NAMEW_VERSION;
static_assert(ArchiveNameWVersion == 107,
              "sdk107 must preserve the version that made CFileData::NameW readable");

} // namespace sdk107

// Release the version macros back to the live SDK - and actually restore them
// here, rather than just undefining and hoping a later #include "plugins/shared/spl_vers.h"
// will do it. That hope fails whenever the live spl_vers.h was ALREADY included earlier in this
// translation unit (which is the common case: precomp.h includes it for nearly every core .cpp
// file). plugins/shared/spl_vers.h uses a named include guard (__SPL_VERS_H, not #pragma once -
// see the guard-preservation dance above), and that guard is restored to "already set" by the
// dance above whenever it started that way - so a later #include of the live header is a silent
// no-op, and these macros stay undefined for the rest of the TU. That was a real, latent bug:
// it broke plugins.h and plugins_fs_encapsulation.cpp (both use LAST_VERSION_OF_SALAMANDER after
// this point) even though plugins_fs_encapsulation.cpp already does exactly the "just re-include
// it" thing the old comment relied on. Force a fresh re-inclusion instead of relying on some
// later, uncontrolled include site to happen to do it.
#undef LAST_VERSION_OF_SALAMANDER
#undef VERSINFO_SALAMANDER_MINORB
#undef VERSINFO_SALAMANDER_VERSION
#undef VERSINFO_SAL_SHORT_VERSION
#undef SALLY_PLUGIN_ARCHIVE_NAMEW_VERSION
#undef __SPL_VERS_H
#include "plugins/shared/spl_vers.h"

// spl_base.h's own debug-only overlap-check macro (#define memcpy
// _sal_safe_memcpy) is the same namespace-escape hazard as the version macros above, just
// discovered later: it is a blind textual substitution, so it also rewrites every
// EXPLICITLY-QUALIFIED std::memcpy(...) call site in this translation unit into
// std::_sal_safe_memcpy(...) - a name that does not exist, since _sal_safe_memcpy is declared
// extern "C" (global-namespace only, it cannot be namespaced) - a hard compile error, not a
// silent divergence like the version macros. This snapshot's spl_base.h is immutable
// (gtest_sdk107_frozen hash-pins it), so the fix belongs here, not there: release the macro
// back to the real memcpy for the rest of this translation unit. This trades away the frozen
// snapshot's debug-only overlap assertion for adapter code specifically; the adapter layer's
// own memcpy call sites are pre-existing, stable code, not the kind of new-and-unverified
// pointer arithmetic that check exists to catch.
#if defined(_DEBUG) && defined(TRACE_ENABLE)
#undef memcpy
#endif
