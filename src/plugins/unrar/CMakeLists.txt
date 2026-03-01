# Plugin build definition for Sally
# Plugin: unrar

if(SAL_PLATFORM STREQUAL "x64")
  sal_add_plugin(NAME unrar
    SOURCES
      "${SAL_SHARED}/dbg.cpp"
      "${SAL_PLUGINS}/unrar/dialogs.cpp"
      "${SAL_PLUGINS}/unrar/precomp.cpp"
      "${SAL_PLUGINS}/unrar/unrar.cpp"
    RC "${SAL_PLUGINS}/unrar/unrar.rc"
    DEF "${SAL_PLUGINS}/unrar/unrar.def"
    INCLUDES "${SAL_SHARED}/plugcore"
    NO_SHARED
  )

  # Rename to salunrar.dll to avoid clash with RARLab's unrar.dll
  set_target_properties(plugin_unrar PROPERTIES OUTPUT_NAME "salunrar")

  # Copy unrar.dll to build output (needed at runtime via LoadLibrary)
  add_custom_command(TARGET plugin_unrar POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${SAL_PLUGINS}/unrar/bin/${SAL_PLATFORM}/unrar.dll"
      "${SAL_OUTPUT_BASE}/$<CONFIG>_${SAL_PLATFORM}/plugins/unrar/unrar.dll"
    COMMENT "Copying unrar.dll to output"
  )

  # Install unrar.dll runtime dependency
  install(FILES "${SAL_PLUGINS}/unrar/bin/${SAL_PLATFORM}/unrar.dll"
    DESTINATION "plugins/unrar"
  )
endif()
