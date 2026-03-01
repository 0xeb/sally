# Plugin build definition for Sally
# Plugin: unole

sal_add_plugin(NAME unole
  SOURCES
    "${SAL_PLUGINS}/unole/dialogs.cpp"
    "${SAL_PLUGINS}/unole/precomp.cpp"
    "${SAL_PLUGINS}/unole/unole_plugin.cpp"
  RC "${SAL_PLUGINS}/unole/unole2.rc"
  DEF "${SAL_PLUGINS}/unole/unole.def"
)

