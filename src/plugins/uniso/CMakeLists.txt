# Plugin build definition for Sally
# Plugin: uniso

sal_add_plugin(NAME uniso
  SOURCES
    "${SAL_PLUGINS}/uniso/audio.cpp"
    "${SAL_PLUGINS}/uniso/blockedfile.cpp"
    "${SAL_PLUGINS}/uniso/dialogs.cpp"
    "${SAL_PLUGINS}/uniso/dmg.cpp"
    "${SAL_PLUGINS}/uniso/eltorito.cpp"
    "${SAL_PLUGINS}/uniso/file.cpp"
    "${SAL_PLUGINS}/uniso/hfs.cpp"
    "${SAL_PLUGINS}/uniso/img.cpp"
    "${SAL_PLUGINS}/uniso/iso9660.cpp"
    "${SAL_PLUGINS}/uniso/isoimage.cpp"
    "${SAL_PLUGINS}/uniso/isz.cpp"
    "${SAL_PLUGINS}/uniso/nrg.cpp"
    "${SAL_PLUGINS}/uniso/precomp.cpp"
    "${SAL_PLUGINS}/uniso/rawfs.cpp"
    "${SAL_PLUGINS}/uniso/udf.cpp"
    "${SAL_PLUGINS}/uniso/udfiso.cpp"
    "${SAL_PLUGINS}/uniso/uniso.cpp"
    "${SAL_PLUGINS}/uniso/viewer.cpp"
    "${SAL_PLUGINS}/uniso/xdvdfs.cpp"
  RC "${SAL_PLUGINS}/uniso/uniso.rc"
  DEF "${SAL_PLUGINS}/uniso/uniso.def"
)

