# Plugin build definition for Sally
# Plugin: demoplug

if(SAL_BUILD_DEMOPLUG)
  sal_add_plugin(NAME demoplug
    SOURCES
      "${SAL_PLUGINS}/demoplug/archiver.cpp"
      "${SAL_PLUGINS}/demoplug/demoplug.cpp"
      "${SAL_PLUGINS}/demoplug/dialogs.cpp"
      "${SAL_PLUGINS}/demoplug/fs1.cpp"
      "${SAL_PLUGINS}/demoplug/fs2.cpp"
      "${SAL_PLUGINS}/demoplug/menu.cpp"
      "${SAL_PLUGINS}/demoplug/precomp.cpp"
      "${SAL_PLUGINS}/demoplug/thumbldr.cpp"
      "${SAL_PLUGINS}/demoplug/viewer.cpp"
    RC "${SAL_PLUGINS}/demoplug/demoplug.rc"
    DEF "${SAL_PLUGINS}/demoplug/demoplug.def"
    DEFINES ENABLE_PROPERTYDIALOG
    LIBS msimg32
  )
else()
  message(STATUS "Skipping plugin: demoplug (SAL_BUILD_DEMOPLUG=OFF)")
endif()
