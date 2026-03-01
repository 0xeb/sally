# Plugin build definition for Sally
# Plugin: dbviewer

sal_add_plugin(NAME dbviewer
  SOURCES
    "${SAL_SHARED}/auxtools.cpp"
    "${SAL_SHARED}/dbg.cpp"
    "${SAL_SHARED}/winliblt.cpp"
    "${SAL_PLUGINS}/dbviewer/csvlib/csvlib.cpp"
    "${SAL_PLUGINS}/dbviewer/dbflib/dbflib.cpp"
    "${SAL_PLUGINS}/dbviewer/data.cpp"
    "${SAL_PLUGINS}/dbviewer/dbviewer.cpp"
    "${SAL_PLUGINS}/dbviewer/dialogs.cpp"
    "${SAL_PLUGINS}/dbviewer/parser.cpp"
    "${SAL_PLUGINS}/dbviewer/precomp.cpp"
    "${SAL_PLUGINS}/dbviewer/renmain.cpp"
    "${SAL_PLUGINS}/dbviewer/renpaint.cpp"
  RC "${SAL_PLUGINS}/dbviewer/dbviewer.rc"
  DEF "${SAL_PLUGINS}/dbviewer/dbviewer.def"
  NO_SHARED
)

