# Plugin build definition for Sally
# Plugin: undelete

sal_add_plugin(NAME undelete
  SOURCES
    # Shared sources (compiled with plugin's precomp.h from library/)
    "${SAL_SHARED}/auxtools.cpp"
    "${SAL_SHARED}/dbg.cpp"
    "${SAL_SHARED}/mhandles.cpp"
    "${SAL_SHARED}/winliblt.cpp"
    # Plugin sources
    "${SAL_PLUGINS}/undelete/dialogs.cpp"
    "${SAL_PLUGINS}/undelete/fs1.cpp"
    "${SAL_PLUGINS}/undelete/fs2.cpp"
    "${SAL_PLUGINS}/undelete/restore.cpp"
    "${SAL_PLUGINS}/undelete/undelete.cpp"
    # Library sources
    "${SAL_PLUGINS}/undelete/library/fat.cpp"
    "${SAL_PLUGINS}/undelete/library/miscstr.cpp"
    "${SAL_PLUGINS}/undelete/library/ntfs.cpp"
    "${SAL_PLUGINS}/undelete/library/os.cpp"
    "${SAL_PLUGINS}/undelete/library/precomp.cpp"
    "${SAL_PLUGINS}/undelete/library/stream.cpp"
    "${SAL_PLUGINS}/undelete/library/volenum.cpp"
    "${SAL_PLUGINS}/undelete/library/volume.cpp"
  RC "${SAL_PLUGINS}/undelete/undelete.rc"
  DEF "${SAL_PLUGINS}/undelete/undelete.def"
  INCLUDES "${SAL_PLUGINS}/undelete/library"
  LIBS uxtheme
  PCH "library/precomp.h"
  NO_SHARED
)

