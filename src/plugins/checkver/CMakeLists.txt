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
  RC "${SAL_PLUGINS}/checkver/checkver.rc"
  DEF "${SAL_PLUGINS}/checkver/checkver.def"
  LIBS wininet
  NO_SHARED
)

