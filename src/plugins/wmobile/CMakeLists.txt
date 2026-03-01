# Plugin build definition for Sally
# Plugin: wmobile

sal_add_plugin(NAME wmobile
  SOURCES
    "${SAL_PLUGINS}/wmobile/dialogs.cpp"
    "${SAL_PLUGINS}/wmobile/fs_init.cpp"
    "${SAL_PLUGINS}/wmobile/fs_operations.cpp"
    "${SAL_PLUGINS}/wmobile/precomp.cpp"
    "${SAL_PLUGINS}/wmobile/rapi.cpp"
    "${SAL_PLUGINS}/wmobile/wmobile.cpp"
  RC "${SAL_PLUGINS}/wmobile/wmobile.rc"
  DEF "${SAL_PLUGINS}/wmobile/wmobile.def"
  INCLUDES "${SAL_PLUGINS}/wmobile/rapi"
)

