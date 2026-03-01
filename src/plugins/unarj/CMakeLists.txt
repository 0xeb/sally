# Plugin build definition for Sally
# Plugin: unarj

sal_add_plugin(NAME unarj
  SOURCES
    "${SAL_PLUGINS}/unarj/decode.cpp"
    "${SAL_PLUGINS}/unarj/dialogs.cpp"
    "${SAL_PLUGINS}/unarj/precomp.cpp"
    "${SAL_PLUGINS}/unarj/unarj.cpp"
    "${SAL_PLUGINS}/unarj/unarjspl.cpp"
  RC "${SAL_PLUGINS}/unarj/unarj.rc"
  DEF "${SAL_PLUGINS}/unarj/unarj.def"
  INCLUDES "${SAL_SHARED}/plugcore"
)

