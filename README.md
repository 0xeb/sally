# Sally

[![Release](https://img.shields.io/github/v/release/0xeb/sally)](https://github.com/0xeb/sally/releases)
[![Build](https://img.shields.io/github/actions/workflow/status/0xeb/sally/pr-msbuild.yml?label=build)](https://github.com/0xeb/sally/actions)
[![License: GPL v2](https://img.shields.io/badge/License-GPLv2-blue.svg)](LICENSE)

A fast, modern dual-panel file manager for Windows — fork of Open Salamander with Unicode, long paths, and ARM64 support.

<!-- Screenshot: this is the classic annotated UI image from the help docs. Replace with a fresh Windows 11 screenshot when available. -->
<p align="center">
  <img src="help/src/hh/salamand/images/main.png" alt="Sally screenshot" width="800">
</p>

## About

Sally is an independent fork of [Open Salamander](https://github.com/OpenSalamander/salamander), maintained by [Elias Bachaalany](https://github.com/0xeb). The original Open Salamander authors are not responsible for this project and do not provide support for it. Development is AI-assisted using [Claude Code](https://docs.anthropic.com/en/docs/claude-code). See [Origin](doc/ORIGIN.md) for the full project history.

## What's New

**Unicode & Long Path Support**
- Wide-primary Win32 API wrappers (`W`-suffix) for all file operations
- `CPathBuffer` replaces `char[MAX_PATH]` buffers — supports up to 32,767-character paths
- 100+ unit and integration tests covering long paths, Unicode filenames, and edge cases

**Modern Build System**
- Complete CMake port — all 25+ plugins, trace server, translator, and shell extension
- Native x64, x86, and ARM64 targets; clang-cl cross-compilation support
- GitHub Actions CI/CD with automated releases

**Architecture Improvements**
- `IPrompter` interface decouples UI dialogs from file operation logic
- `IWorkerObserver` enables headless I/O testing without a GUI
- WebView2 markdown viewer replaces the legacy IE WebBrowser control
- Plugin naming standardized: `ieviewer` renamed to `webviewer` to match its WebView2 implementation

**Platform & Compatibility**
- ARM64 native build for Windows on ARM
- Runs under Wine on Linux (`imageres.dll` dependency made optional)
- Consent-based loading of legacy Altap Salamander 4.0 plugins

**Code Quality**
- Czech-to-English comment translation across the entire codebase
- Modern C++ idioms: `std::wstring`, RAII, `std::unique_ptr`
- Fixed heap corruption bugs, buffer overflows, and Win32 API pitfalls

## Downloads

Pre-built binaries are available on the [Releases](https://github.com/0xeb/sally/releases) page.

## Building

### Prerequisites

- [Visual Studio 2022](https://visualstudio.microsoft.com/downloads/) with the **Desktop development with C++** workload
- [Windows 11 SDK](https://developer.microsoft.com/en-us/windows/downloads/windows-sdk/) (10.0.26100 or newer)

### Build Commands

```bash
# Configure and build (x64 default)
cmake -S . -B build
cmake --build build --config RelWithDebInfo

# Populate output directory
cmake --build build --config RelWithDebInfo --target populate
```

Output: `build/out/sally/<Config>_<Arch>/`

### Other Architectures

| Preset | Description |
|--------|-------------|
| `cmake --preset msvc` | Native MSVC x64 (default) |
| `cmake --preset msvc-arm64` | Native MSVC ARM64 |
| `cmake --preset x64` | clang-cl x64 cross-compile |
| `cmake --preset arm64` | clang-cl ARM64 cross-compile |

### Running Tests

```bash
ctest --test-dir build -C Debug --output-on-failure
```

## Contributing

Contributions are welcome! See the [Developer Guide](doc/DEV.md) for repository structure and internals.

## License

Sally is based on Open Salamander, open source software licensed under [GPLv2](LICENSE). Individual components and libraries have separate but compatible licenses — see [third_party.txt](doc/third_party.txt) for details.

## Resources

[Altap Website](https://www.altap.cz/) | [Features](https://www.altap.cz/salamander/features/) | [Documentation](https://www.altap.cz/salamander/help/) | [Community Forum](https://forum.altap.cz/) | [Upstream Repository](https://github.com/OpenSalamander/salamander) | [Wikipedia](https://en.wikipedia.org/wiki/Altap_Salamander)
