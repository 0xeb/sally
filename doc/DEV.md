# Developer Guide

## Repository Structure

```
\.github         GitHub Actions CI/CD workflows
\cmake           CMake build modules and cross-compilation toolchains
\convert         Conversion tables for the Convert command
\doc             Documentation
\help            User manual source files
\src             Open Salamander core source code
\src\common      Shared libraries
\src\common\dep  Shared third-party libraries
\src\lang        English resources
\src\plugins     Plugins source code
\src\reglib      Access to Windows Registry files
\src\res         Image resources
\src\salmon      Crash detecting and reporting
\src\salopen     Open files helper
\src\salspawn    Process spawning helper
\src\setup       Installer and uninstaller
\src\sfx7zip     Self-extractor based on 7-Zip
\src\shellext    Shell extension DLL
\src\tests       Unit tests
\src\translator  Translate Salamander UI to other languages
\src\tserver     Trace Server to display info and error messages
\src\vcxproj     Visual Studio project files
\tools           Minor utilities
\translations    Translations into other languages
```

## Missing Plugins

A few Altap Salamander 4.0 plugins are either not included or cannot be compiled:

- **PictView**: The engine `pvw32cnv.dll` is not open-sourced. Consider switching to [WIC](https://learn.microsoft.com/en-us/windows/win32/wic/-wic-about-windows-imaging-codec) or another library.
- **Encrypt**: Incompatible with modern SSD disks and has been deprecated.
- **UnRAR**: Missing [unrar.dll](https://www.rarlab.com/rar_add.htm) (solvable, open source).
- **FTP**: Missing [OpenSSL](https://www.openssl.org/) libraries (solvable, open source).
- **WinSCP**: Requires Embarcadero C++ Builder to build.

## Code Style

All source code uses UTF-8-BOM encoding and is formatted with `clang-format`. Refer to the `\tools\normalize.ps1` script for more information.
