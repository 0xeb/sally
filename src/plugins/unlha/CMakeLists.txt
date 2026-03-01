# Plugin build definition for Sally
# Plugin: unlha

sal_add_plugin(NAME unlha
  SOURCES
    "${SAL_PLUGINS}/unlha/lha.cpp"
    "${SAL_PLUGINS}/unlha/precomp.cpp"
    "${SAL_PLUGINS}/unlha/unlha.cpp"
  RC "${SAL_PLUGINS}/unlha/unlha.rc"
  DEF "${SAL_PLUGINS}/unlha/unlha.def"
)

