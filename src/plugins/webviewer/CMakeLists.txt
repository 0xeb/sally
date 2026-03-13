# Plugin build definition for Sally
# Plugin: webviewer

sal_add_plugin(NAME webviewer
  SOURCES
    "${SAL_SHARED}/auxtools.cpp"
    "${SAL_SHARED}/dbg.cpp"
    "${SAL_PLUGINS}/webviewer/cmark-gfm/extensions/autolink.c"
    "${SAL_PLUGINS}/webviewer/cmark-gfm/extensions/core-extensions.c"
    "${SAL_PLUGINS}/webviewer/cmark-gfm/extensions/ext_scanners.c"
    "${SAL_PLUGINS}/webviewer/cmark-gfm/extensions/strikethrough.c"
    "${SAL_PLUGINS}/webviewer/cmark-gfm/extensions/table.c"
    "${SAL_PLUGINS}/webviewer/cmark-gfm/extensions/tagfilter.c"
    "${SAL_PLUGINS}/webviewer/cmark-gfm/extensions/tasklist.c"
    "${SAL_PLUGINS}/webviewer/cmark-gfm/src/arena.c"
    "${SAL_PLUGINS}/webviewer/cmark-gfm/src/blocks.c"
    "${SAL_PLUGINS}/webviewer/cmark-gfm/src/buffer.c"
    "${SAL_PLUGINS}/webviewer/cmark-gfm/src/cmark.c"
    "${SAL_PLUGINS}/webviewer/cmark-gfm/src/cmark_ctype.c"
    "${SAL_PLUGINS}/webviewer/cmark-gfm/src/commonmark.c"
    "${SAL_PLUGINS}/webviewer/cmark-gfm/src/footnotes.c"
    "${SAL_PLUGINS}/webviewer/cmark-gfm/src/houdini_href_e.c"
    "${SAL_PLUGINS}/webviewer/cmark-gfm/src/houdini_html_e.c"
    "${SAL_PLUGINS}/webviewer/cmark-gfm/src/houdini_html_u.c"
    "${SAL_PLUGINS}/webviewer/cmark-gfm/src/html.c"
    "${SAL_PLUGINS}/webviewer/cmark-gfm/src/inlines.c"
    "${SAL_PLUGINS}/webviewer/cmark-gfm/src/iterator.c"
    "${SAL_PLUGINS}/webviewer/cmark-gfm/src/latex.c"
    "${SAL_PLUGINS}/webviewer/cmark-gfm/src/linked_list.c"
    "${SAL_PLUGINS}/webviewer/cmark-gfm/src/main.c"
    "${SAL_PLUGINS}/webviewer/cmark-gfm/src/man.c"
    "${SAL_PLUGINS}/webviewer/cmark-gfm/src/map.c"
    "${SAL_PLUGINS}/webviewer/cmark-gfm/src/node.c"
    "${SAL_PLUGINS}/webviewer/cmark-gfm/src/plaintext.c"
    "${SAL_PLUGINS}/webviewer/cmark-gfm/src/plugin.c"
    "${SAL_PLUGINS}/webviewer/cmark-gfm/src/references.c"
    "${SAL_PLUGINS}/webviewer/cmark-gfm/src/registry.c"
    "${SAL_PLUGINS}/webviewer/cmark-gfm/src/render.c"
    "${SAL_PLUGINS}/webviewer/cmark-gfm/src/scanners.c"
    "${SAL_PLUGINS}/webviewer/cmark-gfm/src/syntax_extension.c"
    "${SAL_PLUGINS}/webviewer/cmark-gfm/src/utf8.c"
    "${SAL_PLUGINS}/webviewer/cmark-gfm/src/xml.c"
    "${SAL_PLUGINS}/webviewer/webviewer.cpp"
    "${SAL_PLUGINS}/webviewer/markdown.cpp"
    "${SAL_PLUGINS}/webviewer/markdown_document.cpp"
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
  LIBS "${WEBVIEW2_LIB}"
  NO_SHARED
)

file(GLOB CMARK_C_SOURCES
  "${SAL_PLUGINS}/webviewer/cmark-gfm/extensions/*.c"
  "${SAL_PLUGINS}/webviewer/cmark-gfm/src/*.c"
)
set_source_files_properties(${CMARK_C_SOURCES} PROPERTIES SKIP_PRECOMPILE_HEADERS ON)
set_source_files_properties("${SAL_PLUGINS}/webviewer/markdown_document.cpp" PROPERTIES SKIP_PRECOMPILE_HEADERS ON)
