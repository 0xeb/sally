# Cross-Build with Clang-CL (MSVC ABI)

This documents cross-compiling Open Salamander from Linux to Windows using Clang-CL,
which targets the MSVC ABI and fully supports SEH (__try/__except/__finally).

## Why Clang-CL Instead of MinGW?

- **SEH Support**: Full native SEH support (601 usages across 40+ files in Salamander)
- **MSVC ABI**: Binary compatible with MSVC, `_MSC_VER` is defined
- **No Compat Hacks**: No need for fake SEH macros, OOM handler disabling, etc.
- **Links Against**: vcruntime140.dll (MSVC runtime)

## Prerequisites

### 1. Install LLVM Tools (Ubuntu/Debian)

```bash
# Clang with clang-cl and LLD linker
sudo apt install clang-18 lld-18 llvm-18

# Create symlinks for MSVC-style tool names
sudo update-alternatives --install /usr/bin/clang-cl clang-cl /usr/lib/llvm-18/bin/clang 100
sudo update-alternatives --install /usr/bin/lld-link lld-link /usr/lib/llvm-18/bin/ld.lld 100
sudo update-alternatives --install /usr/bin/llvm-rc llvm-rc /usr/lib/llvm-18/bin/llvm-rc 100
sudo update-alternatives --install /usr/bin/llvm-lib llvm-lib /usr/lib/llvm-18/bin/llvm-lib 100
```

Or create wrapper scripts (see Setup Scripts section below).

### 2. Install Windows SDK via xwin

```bash
# Install xwin (Windows SDK/CRT downloader)
# Option A: From cargo (requires recent Rust)
cargo install xwin

# Option B: Download binary from GitHub releases
curl -L https://github.com/Jake-Shadle/xwin/releases/latest/download/xwin-x86_64-unknown-linux-musl.tar.gz | tar xz
mv xwin-*/xwin ~/.local/bin/

# Download Windows SDK for x64
xwin --accept-license splat --output ~/xwin

# For ARM64 (separate directory recommended)
xwin --accept-license --arch aarch64 splat --output ~/xwin-arm64
```

## Current Status

### Missing Package: lld-18

The system has clang-18 but NOT lld-18 (the LLD linker). Install it:

```bash
sudo apt install lld-18
```

### Toolchain Files Created

- `cmake/toolchain-clang-cl-x64.cmake` - x64 cross-compilation
- `cmake/toolchain-clang-cl-arm64.cmake` - ARM64 cross-compilation

### CMake Presets

- `x64` - Release build for x64
- `x64-debug` - Debug build for x64
- `arm64` - Release build for ARM64
- `arm64-debug` - Debug build for ARM64

## Build Commands

```bash
# Configure for x64
cmake --preset x64

# Build
cmake --build --preset x64

# Or using build directory directly
cmake --build build-x64
```

## Setup Scripts

### Create MSVC-Compatible Tool Wrappers

If you can't use update-alternatives, create wrapper scripts:

```bash
mkdir -p ~/.local/bin

# clang-cl wrapper
cat > ~/.local/bin/clang-cl << 'EOF'
#!/bin/sh
exec /usr/bin/clang-18 --driver-mode=cl "$@"
EOF
chmod +x ~/.local/bin/clang-cl

# lld-link wrapper (requires lld-18 installed)
cat > ~/.local/bin/lld-link << 'EOF'
#!/bin/sh
exec /usr/lib/llvm-18/bin/lld -flavor link "$@"
EOF
chmod +x ~/.local/bin/lld-link

# llvm-rc and llvm-lib - symlink from llvm-18
ln -sf /usr/lib/llvm-18/bin/llvm-rc ~/.local/bin/llvm-rc
ln -sf /usr/lib/llvm-18/bin/llvm-lib ~/.local/bin/llvm-lib
```

Make sure ~/.local/bin is in your PATH.

## Known Issues

### 1. Path Separators
Some source files may use Windows-style backslashes in #include paths.
These need to be converted to forward slashes for Linux host compilation.

### 2. Header Case Sensitivity
Windows headers like `Windows.h` vs `windows.h` - the xwin headers should
handle this but some local includes may need adjustment.

## Files Modified from main Branch

- `cmake/sal_common.cmake` - Added cross-compilation platform detection
- `cmake/toolchain-clang-cl-x64.cmake` - New file
- `cmake/toolchain-clang-cl-arm64.cmake` - New file
- `CMakePresets.json` - New file with clang-cl presets
