# SFTP plugin tests

Unit and integration tests for `core/` (GoogleTest, executable `sftp_tests`). `core/` has no Sally code,
so the tests run without Sally. Most tests talk to a real OpenSSH server in Docker.

## Test server

Requirements: Docker. Run once (and again after changing anything in `tests/docker/`):

    powershell -File src/plugins/sftp/tests/docker/setup.ps1

It builds the image, (re)creates the container `sally-sftp-test`, and writes generated test keys
(OpenSSH and PuTTY `.ppk`) to `tests/docker/out/` (git-ignored; no private key is ever committed).
Start it later with `docker start sally-sftp-test`.

| Port | Purpose |
| --- | --- |
| 2222 | normal server: password, public key, keyboard-interactive, SFTP (user `test` / `test`) |
| 2223 | no SFTP subsystem |
| 2224 | keyboard-interactive only |
| 2225 | public key only |
| 2226 | legacy algorithms only (the plugin must refuse to connect) |

Fixtures are created by `docker/entrypoint.sh` (small files, nested dirs, symlinks, a `caf\xE9.txt` name,
a directory with 5000 files, a 5 GB sparse file, a writable `upload/` dir). Tests never change shared fixtures.

## Build and run

    cmake --preset msvc -DSAL_BUILD_SFTP=ON
    cmake --build build --config Debug --target sftp_tests
    ctest --test-dir build/src/plugins/sftp -C Debug --output-on-failure

If the container is not reachable, server tests are reported as **skipped**, not failed.
`AuthTest.AgentWhenAvailable` is skipped unless an SSH agent holding the test key is running.
`SFTP_TEST_DIR` overrides the folder with the generated keys.

## Benchmark

`Benchmark.DISABLED_DownloadUpload64MiB` is not run by default (Docker loopback numbers are only relative):

    build\src\plugins\sftp\Debug\sftp_tests.exe --gtest_also_run_disabled_tests --gtest_filter=Benchmark.*
