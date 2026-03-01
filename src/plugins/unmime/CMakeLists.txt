# Plugin build definition for Sally
# Plugin: unmime

sal_add_plugin(NAME unmime
  SOURCES
    "${SAL_PLUGINS}/unmime/decoder.cpp"
    "${SAL_PLUGINS}/unmime/parser.cpp"
    "${SAL_PLUGINS}/unmime/precomp.cpp"
    "${SAL_PLUGINS}/unmime/unmime.cpp"
  RC "${SAL_PLUGINS}/unmime/unmime.rc"
  DEF "${SAL_PLUGINS}/unmime/unmime.def"
)

