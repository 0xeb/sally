# Plugin build definition for Sally
# Plugin: pictview

sal_add_plugin(NAME pictview
  SOURCES
    "${SAL_PLUGINS}/pictview/adapter_actions.cpp"
    "${SAL_PLUGINS}/pictview/codec_masks.cpp"
    "${SAL_PLUGINS}/pictview/configuration.cpp"
    "${SAL_PLUGINS}/pictview/configuration_dialog.cpp"
    "${SAL_PLUGINS}/pictview/dialogs.cpp"
    "${SAL_PLUGINS}/pictview/engine/wic_engine.cpp"
    "${SAL_PLUGINS}/pictview/exif_text.cpp"
    "${SAL_PLUGINS}/pictview/jpeg_thumbnail.cpp"
    "${SAL_PLUGINS}/pictview/thumbnail_update.cpp"
    "${SAL_PLUGINS}/pictview/pictview.cpp"
    "${SAL_PLUGINS}/pictview/print_layout.cpp"
    "${SAL_PLUGINS}/pictview/rename_utils.cpp"
    "${SAL_PLUGINS}/pictview/runtime.cpp"
    "${SAL_PLUGINS}/pictview/thumbnail.cpp"
    "${SAL_PLUGINS}/pictview/viewer.cpp"
  RC "${SAL_PLUGINS}/pictview/pictview.rc"
  DEF "${SAL_PLUGINS}/pictview/pictview.def"
  LIBS windowscodecs ole32 uuid comctl32 d3d11 dxgi
  DEFINES ENABLE_PROPERTYDIALOG
  NO_PCH
)
