# Developer Guide

## Repository Structure

```
\.github         GitHub Actions CI/CD workflows
\cmake           CMake build modules and cross-compilation toolchains
\convert         Conversion tables for the Convert command
\doc             Documentation
\src             Sally core source code
\src\common      Shared libraries
\src\common\dep  Shared third-party libraries
\src\lang        UI resources: English and every translation
\src\plugins     Plugins source code
\src\reglib      Access to Windows Registry files
\src\res         Image resources
\src\salmon      Crash detecting and reporting
\src\salspawn    Process spawning helper
\src\sfx7zip     Self-extractor based on 7-Zip
\src\shellext    Shell extension DLL
\src\tserver     Trace Server to display info and error messages
\tools           Minor utilities
\translations-draft  Unfinished translations (not built)
```

The user manual is published at https://sally-filemanager.app/manual/. Help commands (F1, What's
This?, dialog Help buttons, the Help menu, plugin help) open the matching page there:
`src/web_help_url.h` builds the address and `src/help_topics.inc` maps help context IDs to pages.

## Missing Plugins

A few Altap Salamander 4.0 plugins are either not included or cannot be compiled:

- **PictView**: The original closed engine `pvw32cnv.dll` was never open-sourced. Since 1.0.34 PictView is a new implementation built on [WIC](https://learn.microsoft.com/en-us/windows/win32/wic/-wic-about-windows-imaging-codec); scanning and the formats only that engine could read are not part of it.
- **Encrypt**: Incompatible with modern SSD disks and has been deprecated.
- **UnRAR**: RAR handling is statically linked into `salunrar.dll` from vendored [RARLAB UnRAR source](https://www.rarlab.com/rar_add.htm), which is source-available/freeware for handling RAR archives and is not GPL/OSI-free.
- **FTP**: Missing [OpenSSL](https://www.openssl.org/) libraries (solvable, open source).
- **WinSCP**: Requires Embarcadero C++ Builder to build.

## Code Style

Source and project files use UTF-8 text with CRLF line endings in the Windows working tree and are formatted with `clang-format`. Refer to the `\tools\normalize.ps1` script for more information.

## Developer Tool Targets

The CMake build keeps a few standalone maintenance utilities as explicit targets only. They are not part of default builds, `populate`, installs, or release packages. These targets replace old first-party `.vcxproj` files that were preserved from the Open Salamander tree:

- `utfnames`: creates and lists unusual NTFS names, including names with invalid UTF-16 sequences.
- `salbreak`: hidden hotkey helper that asks running Sally instances to trigger their break/report path.
- `regparser`: builds `RegParser.exe`, a standalone parser/dumper for `.reg` files using `src/reglib`.

```bash
cmake --build build --config Debug --target utfnames salbreak regparser
```

The resulting executables are written under `build/out/sally/<Config>_<Arch>/devtools/`.
Because these maintenance targets are `EXCLUDE_FROM_ALL`, a normal CMake build or `ALL_BUILD`
does not compile them. Before handing off a change that affects the generated Visual Studio
solution, verify the complete target set in both configurations:

```bash
cmake --build build --config Debug   --target ALL_BUILD utfnames salbreak regparser
cmake --build build --config Release --target ALL_BUILD utfnames salbreak regparser
```

The old Portables plugin Visual Studio project was removed without a separate dev-tool replacement because the normal CMake plugin build already covers `plugin_portables` and its English language file. `packages.config` is still used by `cmake/sal_nuget.cmake` to restore the WebView2 SDK for the CMake build.

## Translations

Every language is ordinary resource source compiled into `sally.exe` and into each plugin
DLL; there are no separate language files. Sally uses the language chosen in Configuration,
or the Windows display language, when it starts.

- Sally: English is `src/lang/lang.rc` (with `lang.rc2` and `texts.rc2`); each translation
  is `src/lang/<tag>/lang.rc`, for example `src/lang/de-DE/lang.rc`.
- Plugins: English is `src/plugins/<plugin>/lang/lang.rc`; each translation is
  `src/plugins/<plugin>/lang/<tag>/lang.rc`.
- Each module's `languages.rc` includes its English file and all of its translations.

To fix a translation, edit the text in the translated `lang.rc`. If a longer text needs
more room, adjust the size or position of the affected controls, but keep every resource
ID, style and the order of controls identical to the English file. Windows falls back to
English only for a whole dialog, menu or string table, so a missing string or a changed
control is not reported by the build; it just shows up wrong at runtime.

If you spot a problem but do not want to fix it yourself, open a GitHub issue that names
the language and the dialog, menu or message.

To add a language, create `<tag>/lang.rc` for Sally and for every plugin that is already
translated (start from the English file and set its `LANGUAGE`), add its `#include` line to
each module's `languages.rc`, and add an entry to the table in
`src/common/BuiltinLanguages.h`.
