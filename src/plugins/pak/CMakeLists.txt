# Plugin build definition for Sally
# Plugin: pak

sal_add_plugin(NAME pak
  SOURCES
    "${SAL_SHARED}/dbg.cpp"
    "${SAL_PLUGINS}/pak/dll/add_del.cpp"
    "${SAL_PLUGINS}/pak/dll/pak_dll.cpp"
    "${SAL_PLUGINS}/pak/spl/pak.cpp"
    "${SAL_PLUGINS}/pak/spl/pak2.cpp"
    "${SAL_PLUGINS}/pak/spl/precomp.cpp"
  RC "${SAL_PLUGINS}/pak/spl/pak.rc"
  DEF "${SAL_PLUGINS}/pak/pak.def"
  INCLUDES
    "${SAL_PLUGINS}/pak/spl"
    "${SAL_SHARED}/plugcore"
  NO_SHARED
  NO_LANG
)

# pak lang file is in spl/lang/ instead of lang/
set_source_files_properties("${SAL_PLUGINS}/pak/spl/lang/lang.rc" PROPERTIES
  COMPILE_DEFINITIONS "_LANG;WINVER=0x0601"
)
add_library(plugin_pak_lang SHARED "${SAL_PLUGINS}/pak/spl/lang/lang.rc")
target_include_directories(plugin_pak_lang PRIVATE
  "${SAL_PLUGINS}/pak/spl"
  "${SAL_SHARED}"
)
if(MSVC)
  target_link_options(plugin_pak_lang PRIVATE /NOENTRY /MANIFEST:NO)
elseif(MINGW)
  target_sources(plugin_pak_lang PRIVATE "${SAL_SRC}/common/dllstub.c")
endif()
set_target_properties(plugin_pak_lang PROPERTIES
  OUTPUT_NAME english
  SUFFIX .slg
  RUNTIME_OUTPUT_DIRECTORY "${SAL_OUTPUT_BASE}/$<CONFIG>_${SAL_PLATFORM}/plugins/pak/lang"
  LIBRARY_OUTPUT_DIRECTORY "${SAL_OUTPUT_BASE}/$<CONFIG>_${SAL_PLATFORM}/plugins/pak/lang"
)
set_property(GLOBAL APPEND PROPERTY SAL_PLUGIN_LANGS_LIST plugin_pak_lang)
