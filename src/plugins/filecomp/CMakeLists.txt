# Plugin build definition for Sally
# Plugin: filecomp

sal_add_plugin(NAME filecomp
  SOURCES
    # Shared sources (filecomp doesn't use mhandles.cpp)
    "${SAL_SHARED}/auxtools.cpp"
    "${SAL_SHARED}/dbg.cpp"
    "${SAL_SHARED}/winliblt.cpp"
    "${SAL_SHARED}/plugcore/messages.cpp"
    "${SAL_SHARED}/plugcore/str.cpp"
    "${SAL_SHARED}/plugcore/utilaux.cpp"
    "${SAL_SHARED}/plugcore/utilbase.cpp"
    # Plugin sources
    "${SAL_PLUGINS}/filecomp/controls.cpp"
    "${SAL_PLUGINS}/filecomp/cwbase.cpp"
    "${SAL_PLUGINS}/filecomp/cwoptim.cpp"
    "${SAL_PLUGINS}/filecomp/cwstrict.cpp"
    "${SAL_PLUGINS}/filecomp/dialogs.cpp"
    "${SAL_PLUGINS}/filecomp/dialogs2.cpp"
    "${SAL_PLUGINS}/filecomp/dialogs3.cpp"
    "${SAL_PLUGINS}/filecomp/dialogs4.cpp"
    "${SAL_PLUGINS}/filecomp/dlg_com.cpp"
    "${SAL_PLUGINS}/filecomp/filecache.cpp"
    "${SAL_PLUGINS}/filecomp/filecomp.cpp"
    "${SAL_PLUGINS}/filecomp/filemap.cpp"
    "${SAL_PLUGINS}/filecomp/mainwnd.cpp"
    "${SAL_PLUGINS}/filecomp/mtxtout.cpp"
    "${SAL_PLUGINS}/filecomp/precomp.cpp"
    "${SAL_PLUGINS}/filecomp/remote.cpp"
    "${SAL_PLUGINS}/filecomp/textio.cpp"
    "${SAL_PLUGINS}/filecomp/viewtext.cpp"
    "${SAL_PLUGINS}/filecomp/viewwnd.cpp"
    "${SAL_PLUGINS}/filecomp/viewwnd2.cpp"
    "${SAL_PLUGINS}/filecomp/viewwnd3.cpp"
    "${SAL_PLUGINS}/filecomp/worker.cpp"
    "${SAL_PLUGINS}/filecomp/worker2.cpp"
    "${SAL_PLUGINS}/filecomp/xunicode.cpp"
  RC "${SAL_PLUGINS}/filecomp/filecomp.rc"
  DEF "${SAL_PLUGINS}/filecomp/filecomp.def"
  INCLUDES "${SAL_SHARED}/plugcore"
  DEFINES ENABLE_PROPERTYDIALOG
  LIBS shlwapi
  NO_SHARED
)

# Remote file comparison helper executable
add_executable(fcremote
  "${SAL_SHARED}/plugcore/messages.cpp"
  "${SAL_SRC}/common/rtc_stubs.cpp"
  "${SAL_PLUGINS}/filecomp/fcremote/fcremote.cpp"
  "${SAL_PLUGINS}/filecomp/fcremote/fcremote.rc"
)

target_include_directories(fcremote PRIVATE
  "${SAL_PLUGINS}/filecomp/fcremote"
  "${SAL_PLUGINS}/filecomp"
  "${SAL_SHARED}/plugcore"
  ${SAL_COMMON_INCLUDES}
)

target_compile_definitions(fcremote PRIVATE
  WIN32 _WINDOWS
  WINVER=0x0601 _WIN32_WINNT=0x0601 _WIN32_IE=0x0800
  _CRT_SECURE_NO_WARNINGS _SCL_SECURE_NO_WARNINGS
)

if(MSVC)
  target_compile_options(fcremote PRIVATE /MP /W3 /MT /GS- /EHs-c-)
  target_link_options(fcremote PRIVATE
    /SUBSYSTEM:WINDOWS
    /MANIFEST:NO
    /NODEFAULTLIB
    /ENTRY:WinMainCRTStartup
  )
  target_link_libraries(fcremote PRIVATE kernel32 user32)
endif()

set_target_properties(fcremote PROPERTIES
  OUTPUT_NAME "fcremote"
  RUNTIME_OUTPUT_DIRECTORY "${SAL_OUTPUT_BASE}/$<CONFIG>_${SAL_PLATFORM}/plugins/filecomp"
)

install(TARGETS fcremote RUNTIME DESTINATION "plugins/filecomp")
set_property(GLOBAL APPEND PROPERTY SAL_EXTRA_POPULATE_DEPS fcremote)
