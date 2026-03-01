# Plugin build definition for Sally
# Plugin: regedt

sal_add_plugin(NAME regedt
  SOURCES
    "${SAL_PLUGINS}/regedt/chmon.cpp"
    "${SAL_PLUGINS}/regedt/dialogs.cpp"
    "${SAL_PLUGINS}/regedt/editor.cpp"
    "${SAL_PLUGINS}/regedt/export.cpp"
    "${SAL_PLUGINS}/regedt/finddlg.cpp"
    "${SAL_PLUGINS}/regedt/find_dialog_layout.cpp"
    "${SAL_PLUGINS}/regedt/fs.cpp"
    "${SAL_PLUGINS}/regedt/fs_paths.cpp"
    "${SAL_PLUGINS}/regedt/fs_events_rename.cpp"
    "${SAL_PLUGINS}/regedt/fs_listing_columns.cpp"
    "${SAL_PLUGINS}/regedt/fs_registry_io.cpp"
    "${SAL_PLUGINS}/regedt/menu.cpp"
    "${SAL_PLUGINS}/regedt/precomp.cpp"
    "${SAL_PLUGINS}/regedt/regedt.cpp"
    "${SAL_PLUGINS}/regedt/utils.cpp"
  RC "${SAL_PLUGINS}/regedt/regedt.rc"
  DEF "${SAL_PLUGINS}/regedt/regedt.def"
)

