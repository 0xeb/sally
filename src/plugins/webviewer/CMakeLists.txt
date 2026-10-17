# Plugin build definition for Sally
# Plugin: webviewer

sal_add_plugin(NAME webviewer
  SOURCES
    "${SAL_SHARED}/auxtools.cpp"
    "${SAL_SHARED}/dbg.cpp"
    "${SAL_SHARED}/plugindarkmode.cpp"
    "${SAL_SRC}/combo_dark_paint.cpp"
    "${SAL_PLUGINS}/webviewer/cmark-gfm/extensions/autolink.c"
    "${SAL_PLUGINS}/webviewer/cmark-gfm/extensions/core-extensions.c"
    "${SAL_PLUGINS}/webviewer/cmark-gfm/extensions/ext_scanners.c"
    "${SAL_PLUGINS}/webviewer/cmark-gfm/extensions/strikethrough.c"
    "${SAL_PLUGINS}/webviewer/cmark-gfm/extensions/table.c"
    "${SAL_PLUGINS}/webviewer/cmark-gfm/extensions/tagfilter.c"
    "${SAL_PLUGINS}/webviewer/cmark-gfm/extensions/tasklist.c"
    "${SAL_PLUGINS}/webviewer/webviewer.cpp"
    "${SAL_PLUGINS}/webviewer/markdown.cpp"
    "${SAL_PLUGINS}/webviewer/markdown_document.cpp"
    "${SAL_PLUGINS}/webviewer/webviewer_text.cpp"
    "${SAL_PLUGINS}/webviewer/precomp.cpp"
  RC "${SAL_PLUGINS}/webviewer/webviewer.rc"
  DEF "${SAL_PLUGINS}/webviewer/webviewer.def"
  INCLUDES
    "${SAL_PLUGINS}/webviewer/cmark-gfm/src"
    "${SAL_PLUGINS}/webviewer/cmark-gfm/extensions"
    "${SAL_PLUGINS}/webviewer/cmark-gfm/build/src"
    "${SAL_PLUGINS}/webviewer/cmark-gfm/build/extensions"
    "${WEBVIEW2_INCLUDE}"
  DEFINES CMARK_GFM_STATIC_DEFINE CMARK_GFM_EXTENSIONS_STATIC_DEFINE
  LIBS "${WEBVIEW2_LIB}" sally_cmark_md
  NO_SHARED
)

file(GLOB CMARK_C_SOURCES
  "${SAL_PLUGINS}/webviewer/cmark-gfm/extensions/*.c"
)
set_source_files_properties(${CMARK_C_SOURCES} PROPERTIES SKIP_PRECOMPILE_HEADERS ON)
set_source_files_properties("${SAL_PLUGINS}/webviewer/markdown_document.cpp" PROPERTIES SKIP_PRECOMPILE_HEADERS ON)
