# Plugin build definition for Sally
# Plugin: pak

sal_add_plugin(NAME pak
  SOURCES
    "${SAL_SHARED}/dbg.cpp"
    "${SAL_PLUGINS}/pak/dll/add_del.cpp"
    "${SAL_PLUGINS}/pak/dll/pak_dll.cpp"
    "${SAL_PLUGINS}/pak/spl/pak.cpp"
    "${SAL_PLUGINS}/pak/spl/pak2.cpp"
    "${SAL_PLUGINS}/pak/spl/pak_text.cpp"
    "${SAL_PLUGINS}/pak/spl/precomp.cpp"
  RC "${SAL_PLUGINS}/pak/spl/pak.rc"
  DEF "${SAL_PLUGINS}/pak/pak.def"
  INCLUDES
    "${SAL_PLUGINS}/pak/spl"
    "${SAL_SHARED}/plugcore"
  NO_SHARED
  NO_LANG
)

# pak's languages are in spl/lang/ instead of lang/
sal_add_plugin_languages(TARGET plugin_pak LANG_DIR "${SAL_PLUGINS}/pak/spl/lang")
