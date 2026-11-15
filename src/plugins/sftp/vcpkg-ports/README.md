# vcpkg overlay ports of the SFTP plugin

`src/plugins/sftp/CMakeLists.txt` runs `vcpkg install` with `--overlay-ports=<this folder>`. A port in here replaces
the port of the same name in the vcpkg registry. That is why `vcpkg.json` has no `overrides` entry for `openssl` and
`libssh2` (the overlay decides the version); `zlib` and `gtest` come from the registry (pinned by `builtin-baseline`
and `overrides`).

Why: the registry is behind. OpenSSL 3.5 (the LTS line, supported until April 2030) stops at 3.5.4 there, and the
libssh2 port is plain 1.11.1 without any of the security fixes published since (there is no libssh2 release after 1.11.1).

## openssl (3.5.9, LTS)

- Base: vcpkg `ports/openssl` at master (commit `fe8e78fa`, 3.6.5 port-version 2), all patches and scripts unchanged
  (all apply to 3.5.9).
- Changed: `vcpkg.json` version `3.5.9` (port-version dropped), `portfile.cmake` SHA512 of
  `https://github.com/openssl/openssl/archive/refs/tags/openssl-3.5.9.tar.gz`.
  The GitHub archive has the same file contents as the official release tarball `openssl-3.5.9.tar.gz`
  (sha256 `603f5602e2eef00d77fbd429d34dcd5822bb301757a1bc9cdb24c670f1eb859a`, from the release page).

## libssh2 (1.11.1 plus security patches, port-version 4)

- Base: vcpkg `ports/libssh2` at master (1.11.1 port-version 3). `cmake-config.diff`, `pkgconfig.diff`, features
  (`openssl`, `zlib`) are unchanged.
- Changed: `vcpkg.json` port-version 4, `portfile.cmake` lists the patches below (order matters).
- The `CVE-*.patch` files are the 1.11.1 backports of Debian (salsa.debian.org/debian/libssh2, `debian/patches`,
  package 1.11.1-6); each is the upstream fix commit adapted to 1.11.1. `libssh2-unconst-backport.patch` is needed by the
  `CVE-2025-15661` patch. `ghsa-949f-w9qj-9q6h.patch` is our own adaptation of upstream commit `70864b47`.

| Patch | CVE / advisory | Upstream fix |
|---|---|---|
| `CVE-2026-7598.patch` | CVE-2026-7598 integer overflow in `userauth_password` | 256d04b6 |
| `libssh2-unconst-backport.patch` | (helper for the next patch) | 606c102e |
| `CVE-2025-15661.patch` | CVE-2025-15661 out-of-bounds read in `sftp_symlink` | 2dae3024(local fix, see below) |
| `CVE-2026-55199.patch` | CVE-2026-55199 CPU loop in the `EXT_INFO` handler | 17626857 |
| `CVE-2026-55200.patch` | CVE-2026-55200 missing `packet_length` upper bound | 97acf3df |
| `CVE-2026-66032.patch` | CVE-2026-66032 double free in `sftp_open` | 5e477614 |
| `CVE-2026-66033.patch` | CVE-2026-66033 AES-GCM underflow in `ssh2_cipher_crypt` | a2ed82d4 |
| `CVE-2026-66034.patch` | CVE-2026-66034 publickey list out-of-bounds read | a13bb6c7 |
| `CVE-2026-66035.patch` | CVE-2026-66035 heap overflow on ETM decrypt | 42e33d81 |
| `CVE-2026-58050.patch` | CVE-2026-58050 multiplication overflow in the publickey list | 34497525 |
| `CVE-2026-58051.patch` | CVE-2026-58051 uninitialised free in the publickey list | a9758da4 |
| `ghsa-949f-w9qj-9q6h.patch` | GHSA-949f-w9qj-9q6h `packet_length` check on the ETM path (no CVE id known yet) | 70864b47 |

CVE-2023-48795 (Terrapin) is already fixed inside 1.11.1 (strict KEX).

## How to drop an overlay

When the registry has the version you want (check `versions/o-/openssl.json`, `versions/l-/libssh2.json` in
https://github.com/microsoft/vcpkg and that the libssh2 port carries the fixes above or is a release newer than 1.11.1):

1. Delete the port folder from here (delete this folder when both are gone) and the `--overlay-ports` lines and the
   `_SFTP_OVERLAY_*` block in `../CMakeLists.txt`.
2. In `../vcpkg.json` move `builtin-baseline` to a vcpkg commit that has the versions and add `overrides` entries for them.
3. Reconfigure `build/`; check `build/vcpkg_installed/vcpkg/status` and the licence texts (`doc/third_party.txt`).

To update an overlay instead: copy the new registry port over it, then re-apply the changes listed above.

Note on `CVE-2025-15661.patch`: the Debian 1.11.1-6 version read the status code right after the packet type, i.e. it took the
request id for the status code, so every SYMLINK request failed with a random status. Our copy skips the request id first
(as upstream 2dae3024 does) and keeps the bounds checks.
