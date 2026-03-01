# Plugin build definition for Sally
# Plugin: checksum

sal_add_plugin(NAME checksum
  SOURCES
    "${SAL_PLUGINS}/checksum/checksum.cpp"
    "${SAL_PLUGINS}/checksum/dialogs.cpp"
    "${SAL_PLUGINS}/checksum/misc.cpp"
    "${SAL_PLUGINS}/checksum/precomp.cpp"
    "${SAL_PLUGINS}/checksum/wrappers.cpp"
    "${SAL_PLUGINS}/checksum/tomcrypt/sha256.cpp"
    "${SAL_PLUGINS}/checksum/tomcrypt/sha512.cpp"
  RC "${SAL_PLUGINS}/checksum/checksum.rc"
  DEF "${SAL_PLUGINS}/checksum/checksum.def"
)

