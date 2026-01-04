# Open Salamander

Open Salamander is a fast and reliable two-panel file manager for Windows. See [Origin](doc/ORIGIN.md) for project history.

This fork, maintained by Elias Bachaalany, uses AI-assisted development to accelerate progress. Goals include modernizing the C++ codebase, adding Unicode and long path support, and improving cross-platform compatibility (including Wine).


## Building

### Prerequisites
- Windows 11 or newer
- [Visual Studio 2022](https://visualstudio.microsoft.com/downloads/)
- [Desktop development with C++](https://learn.microsoft.com/en-us/cpp/build/vscpp-step-0-installation?view=msvc-170) workload installed in VS2022
- [Windows 11 (10.0.26100.4654) SDK](https://developer.microsoft.com/en-us/windows/downloads/windows-sdk/) optional component installed in VS2022

### Optional
- [Git](https://git-scm.com/downloads)
- [PowerShell 7.4](https://learn.microsoft.com/en-us/powershell/scripting/install/installing-powershell-on-windows) or newer
- [HTMLHelp Workshop 1.3](https://learn.microsoft.com/en-us/answers/questions/265752/htmlhelp-workshop-download-for-chm-compiler-instal)

### Building with CMake

```bash
# Configure
cmake -S . -B build                    # x64 (default)
cmake --preset msvc-arm64              # ARM64

# Build
cmake --build build --config RelWithDebInfo

# Populate output directory
cmake --build build --config RelWithDebInfo --target populate
```

Output: `build\out\salamander\<Config>_<Arch>\`

## Contributing

This project welcomes contributions to build and enhance Open Salamander!

See [Developer Guide](doc/DEV.md) for repository structure and internals.

## License

Open Salamander is open source software licensed under [GPLv2](doc/license_gpl.txt) and later.
Individual [files and libraries](doc/third_party.txt) have a different, but compatible license.
