# Plugin build definition for Sally
# Plugin: tar

sal_add_plugin(NAME tar
  SOURCES
    "${SAL_SHARED}/dbg.cpp"
    "${SAL_PLUGINS}/tar/bzip/bunzip.cpp"
    "${SAL_PLUGINS}/tar/bzip/bzlib.c"
    "${SAL_PLUGINS}/tar/bzip/crctable.c"
    "${SAL_PLUGINS}/tar/bzip/decompress.c"
    "${SAL_PLUGINS}/tar/bzip/huffman.c"
    "${SAL_PLUGINS}/tar/bzip/randtable.c"
    "${SAL_PLUGINS}/tar/compress/uncompress.cpp"
    "${SAL_PLUGINS}/tar/deb/deb.cpp"
    "${SAL_PLUGINS}/tar/fileio.cpp"
    "${SAL_PLUGINS}/tar/gzip/gunzip.cpp"
    "${SAL_PLUGINS}/tar/lzh/lzh.cpp"
    "${SAL_PLUGINS}/tar/names.cpp"
    "${SAL_PLUGINS}/tar/precomp.cpp"
    "${SAL_PLUGINS}/tar/rpm/rpm.cpp"
    "${SAL_PLUGINS}/tar/rpm/rpmview.cpp"
    "${SAL_PLUGINS}/tar/tardll.cpp"
    "${SAL_PLUGINS}/tar/untar.cpp"
  RC "${SAL_PLUGINS}/tar/tar.rc"
  DEF "${SAL_PLUGINS}/tar/tar.def"
  INCLUDES "${SAL_PLUGINS}/tar"
  DEFINES BZ_NO_STDIO
  NO_SHARED
)

set_source_files_properties(
  "${SAL_PLUGINS}/tar/bzip/bzlib.c"
  "${SAL_PLUGINS}/tar/bzip/crctable.c"
  "${SAL_PLUGINS}/tar/bzip/decompress.c"
  "${SAL_PLUGINS}/tar/bzip/huffman.c"
  "${SAL_PLUGINS}/tar/bzip/randtable.c"
  PROPERTIES SKIP_PRECOMPILE_HEADERS ON
)
