# Plugin build definition for Sally
# Plugin: zip

sal_add_plugin(NAME zip
  SOURCES
    # Shared sources (zip only uses dbg.cpp + plugcore/resedit.cpp)
    "${SAL_SHARED}/dbg.cpp"
    "${SAL_SHARED}/plugcore/resedit.cpp"
    # Plugin sources
    "${SAL_PLUGINS}/zip/add.cpp"
    "${SAL_PLUGINS}/zip/add_del.cpp"
    "${SAL_PLUGINS}/zip/bits.cpp"
    "${SAL_PLUGINS}/zip/chicon.cpp"
    "${SAL_PLUGINS}/zip/common.cpp"
    "${SAL_PLUGINS}/zip/common2.cpp"
    "${SAL_PLUGINS}/zip/const.cpp"
    "${SAL_PLUGINS}/zip/crypt.cpp"
    "${SAL_PLUGINS}/zip/deflate.cpp"
    "${SAL_PLUGINS}/zip/del.cpp"
    "${SAL_PLUGINS}/zip/dialogs.cpp"
    "${SAL_PLUGINS}/zip/dialogs2.cpp"
    "${SAL_PLUGINS}/zip/dialogs3.cpp"
    "${SAL_PLUGINS}/zip/explode.cpp"
    "${SAL_PLUGINS}/zip/extract.cpp"
    "${SAL_PLUGINS}/zip/inflate.cpp"
    "${SAL_PLUGINS}/zip/iosfxset.cpp"
    "${SAL_PLUGINS}/zip/list.cpp"
    "${SAL_PLUGINS}/zip/main.cpp"
    "${SAL_PLUGINS}/zip/memapi.cpp"
    "${SAL_PLUGINS}/zip/precomp.cpp"
    "${SAL_PLUGINS}/zip/prevsfx.cpp"
    "${SAL_PLUGINS}/zip/repair.cpp"
    "${SAL_PLUGINS}/zip/trees.cpp"
    "${SAL_PLUGINS}/zip/unbzip2.cpp"
    "${SAL_PLUGINS}/zip/unreduce.cpp"
    "${SAL_PLUGINS}/zip/unshrink.cpp"
  RC "${SAL_PLUGINS}/zip/zip.rc"
  DEF "${SAL_PLUGINS}/zip/zip.def"
  INCLUDES "${SAL_SHARED}/plugcore"
  DEFINES ZIP_DLL
  NO_SHARED
)

