# Plugin build definition for Sally
# Plugin: peviewer

sal_add_plugin(NAME peviewer
  SOURCES
    "${SAL_PLUGINS}/peviewer/cfg.cpp"
    "${SAL_PLUGINS}/peviewer/cfgdlg.cpp"
    "${SAL_PLUGINS}/peviewer/dump_res.cpp"
    "${SAL_PLUGINS}/peviewer/pefile.cpp"
    "${SAL_PLUGINS}/peviewer/peviewer.cpp"
    "${SAL_PLUGINS}/peviewer/precomp.cpp"
  RC "${SAL_PLUGINS}/peviewer/peviewer.rc"
  DEF "${SAL_PLUGINS}/peviewer/peviewer.def"
  LIBS version
)

