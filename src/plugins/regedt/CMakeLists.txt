# Plugin build definition for Sally
# Plugin: regedt

sal_add_plugin(NAME regedt
  SOURCES
    "${SAL_PLUGINS}/regedt/chmon.cpp"
    "${SAL_PLUGINS}/regedt/dialogs.cpp"
    "${SAL_PLUGINS}/regedt/editor.cpp"
    "${SAL_PLUGINS}/regedt/export.cpp"
    "${SAL_PLUGINS}/regedt/finddlg.cpp"
    "${SAL_PLUGINS}/regedt/finddlg2.cpp"
    "${SAL_PLUGINS}/regedt/fs.cpp"
    "${SAL_PLUGINS}/regedt/fs2.cpp"
    "${SAL_PLUGINS}/regedt/fs3.cpp"
    "${SAL_PLUGINS}/regedt/fs4.cpp"
    "${SAL_PLUGINS}/regedt/fs5.cpp"
    "${SAL_PLUGINS}/regedt/menu.cpp"
    "${SAL_PLUGINS}/regedt/precomp.cpp"
    "${SAL_PLUGINS}/regedt/regedt.cpp"
    "${SAL_PLUGINS}/regedt/utils.cpp"
  RC "${SAL_PLUGINS}/regedt/regedt.rc"
  DEF "${SAL_PLUGINS}/regedt/regedt.def"
)

