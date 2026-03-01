# Plugin build definition for Sally
# Plugin: renamer

sal_add_plugin(NAME renamer
  SOURCES
    "${SAL_PLUGINS}/renamer/rename_script_builder.cpp"
    "${SAL_PLUGINS}/renamer/crenamer.cpp"
    "${SAL_PLUGINS}/renamer/dialogs.cpp"
    "${SAL_PLUGINS}/renamer/editor.cpp"
    "${SAL_PLUGINS}/renamer/menu.cpp"
    "${SAL_PLUGINS}/renamer/precomp.cpp"
    "${SAL_PLUGINS}/renamer/preview.cpp"
    "${SAL_PLUGINS}/renamer/regexp.cpp"
    "${SAL_PLUGINS}/renamer/renamer.cpp"
    "${SAL_PLUGINS}/renamer/rendlg.cpp"
    "${SAL_PLUGINS}/renamer/renamer_execute_undo.cpp"
    "${SAL_PLUGINS}/renamer/renamer_file_ops.cpp"
    "${SAL_PLUGINS}/renamer/renamer_external_command.cpp"
    "${SAL_PLUGINS}/renamer/utils.cpp"
    "${SAL_PLUGINS}/renamer/varstr.cpp"
    # Character utilities from shared/plugcore
    "${SAL_SHARED}/plugcore/str.cpp"
  RC "${SAL_PLUGINS}/renamer/renamer.rc"
  DEF "${SAL_PLUGINS}/renamer/renamer.def"
  INCLUDES "${SAL_SHARED}/plugcore"
  DEFINES _CHAR_UNSIGNED DECLARE_REGIFACE_FUNCTIONS
)

