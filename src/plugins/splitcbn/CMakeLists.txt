# Plugin build definition for Sally
# Plugin: splitcbn

sal_add_plugin(NAME splitcbn
  SOURCES
    "${SAL_PLUGINS}/splitcbn/combine.cpp"
    "${SAL_PLUGINS}/splitcbn/dialogs.cpp"
    "${SAL_PLUGINS}/splitcbn/precomp.cpp"
    "${SAL_PLUGINS}/splitcbn/split.cpp"
    "${SAL_PLUGINS}/splitcbn/splitcbn.cpp"
  RC "${SAL_PLUGINS}/splitcbn/splitcbn.rc"
  DEF "${SAL_PLUGINS}/splitcbn/splitcbn.def"
)

