# Plugin build definition for Sally
# Plugin: portables

sal_add_plugin(NAME portables
  SOURCES
    "${SAL_SHARED}/dbg.cpp"
    "${SAL_SHARED}/mhandles.cpp"
    "${SAL_SHARED}/winliblt.cpp"
    "${SAL_PLUGINS}/portables/device.cpp"
    "${SAL_PLUGINS}/portables/entry.cpp"
    "${SAL_PLUGINS}/portables/fx.cpp"
    "${SAL_PLUGINS}/portables/fxfs.cpp"
    "${SAL_PLUGINS}/portables/globals.cpp"
    "${SAL_PLUGINS}/portables/notifier.cpp"
    "${SAL_PLUGINS}/portables/precomp.cpp"
    "${SAL_PLUGINS}/portables/wpddeviceicons.cpp"
    "${SAL_PLUGINS}/portables/wpdfs.cpp"
    "${SAL_PLUGINS}/portables/wpdfscontentlevel.cpp"
    "${SAL_PLUGINS}/portables/wpdfsdevicelevel.cpp"
    "${SAL_PLUGINS}/portables/wpdplug.cpp"
  RC "${SAL_PLUGINS}/portables/wpd.rc"
  DEF "${SAL_PLUGINS}/portables/portables.def"
  LIBS PortableDeviceGuids
  NO_SHARED
)

