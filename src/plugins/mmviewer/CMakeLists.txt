# Plugin build definition for Sally
# Plugin: mmviewer

sal_add_plugin(NAME mmviewer
  SOURCES
    "${SAL_PLUGINS}/mmviewer/buffer.cpp"
    "${SAL_PLUGINS}/mmviewer/dialogs.cpp"
    "${SAL_PLUGINS}/mmviewer/exporthtml.cpp"
    "${SAL_PLUGINS}/mmviewer/exportxml.cpp"
    "${SAL_PLUGINS}/mmviewer/mmviewer.cpp"
    "${SAL_PLUGINS}/mmviewer/output.cpp"
    "${SAL_PLUGINS}/mmviewer/parser.cpp"
    "${SAL_PLUGINS}/mmviewer/precomp.cpp"
    "${SAL_PLUGINS}/mmviewer/renmain.cpp"
    "${SAL_PLUGINS}/mmviewer/renpaint.cpp"
    # Format parsers
    "${SAL_PLUGINS}/mmviewer/mod/modparser.cpp"
    "${SAL_PLUGINS}/mmviewer/mp3/id3tagv1.cpp"
    "${SAL_PLUGINS}/mmviewer/mp3/id3tagv2.cpp"
    "${SAL_PLUGINS}/mmviewer/mp3/mpeghead.cpp"
    "${SAL_PLUGINS}/mmviewer/mp3/mpgparser.cpp"
    "${SAL_PLUGINS}/mmviewer/mp4/mp4head.cpp"
    "${SAL_PLUGINS}/mmviewer/mp4/mp4parser.cpp"
    "${SAL_PLUGINS}/mmviewer/mp4/mp4tag.cpp"
    "${SAL_PLUGINS}/mmviewer/ogg/oggparser.cpp"
    "${SAL_PLUGINS}/mmviewer/VQF/VQFPARSER.CPP"
    "${SAL_PLUGINS}/mmviewer/wav/wavparser.cpp"
    "${SAL_PLUGINS}/mmviewer/wma/wmaparser.cpp"
  RC "${SAL_PLUGINS}/mmviewer/mmviewer.rc"
  DEF "${SAL_PLUGINS}/mmviewer/mmviewer.def"
  LIBS winmm msacm32
)

