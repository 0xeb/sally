# Developer Guide

## Repository Structure

```
\.github         GitHub Actions CI/CD workflows
\cmake           CMake build modules and cross-compilation toolchains
\convert         Conversion tables for the Convert command
\doc             Documentation
\help            User manual source files
\src             Sally core source code
\src\common      Shared libraries
\src\common\dep  Shared third-party libraries
\src\lang        English resources
\src\plugins     Plugins source code
\src\reglib      Access to Windows Registry files
\src\res         Image resources
\src\salmon      Crash detecting and reporting
\src\salspawn    Process spawning helper
\src\sfx7zip     Self-extractor based on 7-Zip
\src\shellext    Shell extension DLL
\src\tserver     Trace Server to display info and error messages
\tools           Minor utilities
\translations    Translations into other languages
```

## Missing Plugins

A few Altap Salamander 4.0 plugins are either not included or cannot be compiled:

- **PictView**: The engine `pvw32cnv.dll` is not open-sourced. Consider switching to [WIC](https://learn.microsoft.com/en-us/windows/win32/wic/-wic-about-windows-imaging-codec) or another library.
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

## Localization

Translation source archives live under `translations/`. Contributors can submit
updates to those `.slt` files; release-time validation and language-pack
generation are maintained outside this repository.
