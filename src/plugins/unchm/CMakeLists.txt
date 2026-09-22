# Plugin build definition for Sally
# Plugin: unchm

sal_add_plugin(NAME unchm
  SOURCES
    "${SAL_PLUGINS}/unchm/chmfile.cpp"
    "${SAL_PLUGINS}/unchm/precomp.cpp"
    "${SAL_PLUGINS}/unchm/unchm.cpp"
    "${SAL_PLUGINS}/unchm/unchm_text.cpp"
  RC "${SAL_PLUGINS}/unchm/unchm.rc"
  DEF "${SAL_PLUGINS}/unchm/unchm.def"
)

add_library(unchm_chmlib SHARED
  "${SAL_PLUGINS}/unchm/chmlib/chm_lib.c"
  "${SAL_PLUGINS}/unchm/chmlib/lzx.c"
  "${SAL_PLUGINS}/unchm/chmlib/main.c"
  "${SAL_PLUGINS}/unchm/chmlib/chmlib.def"
)
set_target_properties(unchm_chmlib PROPERTIES
  OUTPUT_NAME "chmlib"
  PREFIX ""
  RUNTIME_OUTPUT_DIRECTORY "${SAL_OUTPUT_BASE}/$<CONFIG>_${SAL_PLATFORM}/plugins/unchm"
  LIBRARY_OUTPUT_DIRECTORY "${SAL_OUTPUT_BASE}/$<CONFIG>_${SAL_PLATFORM}/plugins/unchm"
)
target_compile_definitions(unchm_chmlib PRIVATE _CRT_SECURE_NO_WARNINGS)
add_dependencies(plugin_unchm unchm_chmlib)

