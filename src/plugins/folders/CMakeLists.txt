# Plugin build definition for Sally
# Plugin: folders

sal_add_plugin(NAME folders
  SOURCES
    "${SAL_PLUGINS}/folders/dialogs.cpp"
    "${SAL_PLUGINS}/folders/folders.cpp"
    "${SAL_PLUGINS}/folders/fs1.cpp"
    "${SAL_PLUGINS}/folders/fs2.cpp"
    "${SAL_PLUGINS}/folders/iltools.cpp"
    "${SAL_PLUGINS}/folders/precomp.cpp"
  RC "${SAL_PLUGINS}/folders/folders.rc"
  DEF "${SAL_PLUGINS}/folders/folders.def"
  LIBS shlwapi
)

