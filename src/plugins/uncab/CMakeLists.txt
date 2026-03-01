# Plugin build definition for Sally
# Plugin: uncab

sal_add_plugin(NAME uncab
  SOURCES
    "${SAL_PLUGINS}/uncab/dialogs.cpp"
    "${SAL_PLUGINS}/uncab/precomp.cpp"
    "${SAL_PLUGINS}/uncab/uncab.cpp"
  RC "${SAL_PLUGINS}/uncab/uncab.rc"
  DEF "${SAL_PLUGINS}/uncab/uncab.def"
  INCLUDES "${SAL_SHARED}/plugcore"
  LIBS cabinet
)

