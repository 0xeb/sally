# Plugin build definition for Sally
# Plugin: checkver

sal_add_plugin(NAME checkver
  SOURCES
    "${SAL_SHARED}/dbg.cpp"
    "${SAL_PLUGINS}/checkver/checkver.cpp"
    "${SAL_PLUGINS}/checkver/data.cpp"
    "${SAL_PLUGINS}/checkver/dialogs.cpp"
    "${SAL_PLUGINS}/checkver/github_release.cpp"
    "${SAL_PLUGINS}/checkver/internet.cpp"
    "${SAL_PLUGINS}/checkver/logwnd.cpp"
    "${SAL_PLUGINS}/checkver/precomp.cpp"
    "${SAL_PLUGINS}/checkver/release_check.cpp"
    "${SAL_PLUGINS}/checkver/self_update.cpp"
    "${SAL_SRC}/common/update/ReleaseChecksums.cpp"
    "${SAL_SRC}/common/update/UpdatePackage.cpp"
    "${SAL_SRC}/common/update/UpdateProcesses.cpp"
    "${SAL_SRC}/common/update/UpdaterCommand.cpp"
  RC "${SAL_PLUGINS}/checkver/checkver.rc"
  DEF "${SAL_PLUGINS}/checkver/checkver.def"
  LIBS wininet yyjson bcrypt version shell32 ole32
  NO_SHARED
)

