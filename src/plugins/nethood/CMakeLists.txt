# Plugin build definition for Sally
# Plugin: nethood

sal_add_plugin(NAME nethood
  SOURCES
    "${SAL_PLUGINS}/nethood/cache.cpp"
    "${SAL_PLUGINS}/nethood/cfgdlg.cpp"
    "${SAL_PLUGINS}/nethood/entry.cpp"
    "${SAL_PLUGINS}/nethood/globals.cpp"
    "${SAL_PLUGINS}/nethood/icons.cpp"
    "${SAL_PLUGINS}/nethood/nethood.cpp"
    "${SAL_PLUGINS}/nethood/nethooddata.cpp"
    "${SAL_PLUGINS}/nethood/nethoodfs.cpp"
    "${SAL_PLUGINS}/nethood/nethood_fs_operations.cpp"
    "${SAL_PLUGINS}/nethood/nethoodmenu.cpp"
    "${SAL_PLUGINS}/nethood/precomp.cpp"
    "${SAL_PLUGINS}/nethood/salutils.cpp"
  RC "${SAL_PLUGINS}/nethood/nethood.rc"
  DEF "${SAL_PLUGINS}/nethood/nethood.def"
  LIBS mpr netapi32 wtsapi32 shlwapi
)

