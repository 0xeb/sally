# Plugin build definition for Sally
# Plugin: unfat

sal_add_plugin(NAME unfat
  SOURCES
    "${SAL_PLUGINS}/unfat/fat.cpp"
    "${SAL_PLUGINS}/unfat/precomp.cpp"
    "${SAL_PLUGINS}/unfat/unfat.cpp"
  RC "${SAL_PLUGINS}/unfat/unfat.rc"
  DEF "${SAL_PLUGINS}/unfat/unfat.def"
)

