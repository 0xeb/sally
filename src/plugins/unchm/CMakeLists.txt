# Plugin build definition for Sally
# Plugin: unchm

sal_add_plugin(NAME unchm
  SOURCES
    "${SAL_PLUGINS}/unchm/chmfile.cpp"
    "${SAL_PLUGINS}/unchm/precomp.cpp"
    "${SAL_PLUGINS}/unchm/unchm.cpp"
  RC "${SAL_PLUGINS}/unchm/unchm.rc"
  DEF "${SAL_PLUGINS}/unchm/unchm.def"
)

