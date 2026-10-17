<!-- SPDX-FileCopyrightText: 2025-2026 Elias Bachaalany -->
<!-- SPDX-License-Identifier: GPL-2.0-or-later -->

# Bundled 7-Zip

Upstream: Igor Pavlov, https://www.7-zip.org/, version 26.04 (2026-10-05).
Source archive: https://www.7-zip.org/a/7z2604-src.7z
SHA-256: `6bcfd2679d178174df06d09d50147dd7b5980d7beac896136689dfc9d51e0fd9`.

The C, CPP and DOC source trees retain upstream licenses and notices. Sally's
existing `spl/` integration and `CPP/7zip/UI/7zwrapper/` are separate inherited
Open Salamander integration code. CMake follows Format7zF/Arc.mak with portable
C implementations of the optional assembly optimizations.

The modern extractor closes its output before SetOperationResult, preserving
Keep/Delete Corrupt File semantics without the historical patch. Upstream's own
DllExports2 supplies initialization and exports. Sally worker tracing is retained
in LzFindMt.c and VirtThread.cpp through spl/splthread.c. Upstream propagates
password/decode failures through SetOperationResult; Sally's callback counts those
errors. Signature scanning behind SFX prefixes is upstream behavior. Historical patch
notes describe the retired 16.04 baseline,
not a recipe to reapply every old patch.
