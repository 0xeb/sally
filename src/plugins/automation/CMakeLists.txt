# Plugin build definition for Sally
# Plugin: automation

# MIDL compilation for COM interfaces
set(AUTOMATION_IDL "${SAL_PLUGINS}/automation/salamander.idl")
set(AUTOMATION_GEN "${SAL_PLUGINS}/automation/generated")
set(AUTOMATION_MIDL_OUTPUTS
  "${AUTOMATION_GEN}/salamander_h.h"
  "${AUTOMATION_GEN}/salamander_i.c"
  "${AUTOMATION_GEN}/salamander_p.c"
  "${AUTOMATION_GEN}/dlldata.c"
  "${AUTOMATION_GEN}/automation.tlb"
)

if(MSVC)
  add_custom_command(
    OUTPUT ${AUTOMATION_MIDL_OUTPUTS}
    COMMAND midl
      /nologo
      /char signed
      /env win32
      /Oicf
      /h "${AUTOMATION_GEN}/salamander_h.h"
      /iid "${AUTOMATION_GEN}/salamander_i.c"
      /proxy "${AUTOMATION_GEN}/salamander_p.c"
      /dlldata "${AUTOMATION_GEN}/dlldata.c"
      /tlb "${AUTOMATION_GEN}/automation.tlb"
      "${AUTOMATION_IDL}"
    DEPENDS "${AUTOMATION_IDL}"
    WORKING_DIRECTORY "${SAL_PLUGINS}/automation"
    COMMENT "Compiling IDL: salamander.idl"
    VERBATIM
  )
endif()

sal_add_plugin(NAME automation
  SOURCES
    "${SAL_PLUGINS}/automation/abortmodal.cpp"
    "${SAL_PLUGINS}/automation/abortpalette.cpp"
    "${SAL_PLUGINS}/automation/automationplug.cpp"
    "${SAL_PLUGINS}/automation/aututils.cpp"
    "${SAL_PLUGINS}/automation/cfgdlg.cpp"
    "${SAL_PLUGINS}/automation/dialogimpl.cpp"
    "${SAL_PLUGINS}/automation/engassoc.cpp"
    "${SAL_PLUGINS}/automation/entry.cpp"
    "${SAL_PLUGINS}/automation/fileinfo.cpp"
    "${SAL_PLUGINS}/automation/guibutton.cpp"
    "${SAL_PLUGINS}/automation/guichkbox.cpp"
    "${SAL_PLUGINS}/automation/guicomponent.cpp"
    "${SAL_PLUGINS}/automation/guicontainer.cpp"
    "${SAL_PLUGINS}/automation/guiform.cpp"
    "${SAL_PLUGINS}/automation/guilabel.cpp"
    "${SAL_PLUGINS}/automation/guinamespace.cpp"
    "${SAL_PLUGINS}/automation/guitxtbox.cpp"
    "${SAL_PLUGINS}/automation/itemaut.cpp"
    "${SAL_PLUGINS}/automation/itemcoll.cpp"
    "${SAL_PLUGINS}/automation/knownengines.cpp"
    "${SAL_PLUGINS}/automation/panelaut.cpp"
    "${SAL_PLUGINS}/automation/persistence.cpp"
    "${SAL_PLUGINS}/automation/precomp.cpp"
    "${SAL_PLUGINS}/automation/processlist.cpp"
    "${SAL_PLUGINS}/automation/progressaut.cpp"
    "${SAL_PLUGINS}/automation/raiserr.cpp"
    "${SAL_PLUGINS}/automation/salamanderaut.cpp"
    "${SAL_PLUGINS}/automation/saltypelib.cpp"
    "${SAL_PLUGINS}/automation/scriptinfoaut.cpp"
    "${SAL_PLUGINS}/automation/scriptlist.cpp"
    "${SAL_PLUGINS}/automation/scriptsite.cpp"
    "${SAL_PLUGINS}/automation/shim.cpp"
    "${SAL_PLUGINS}/automation/strconv.cpp"
    "${SAL_PLUGINS}/automation/waitwndaut.cpp"
    "${SAL_PLUGINS}/automation/generated/salamander_i.c"
  RC "${SAL_PLUGINS}/automation/automation.rc"
  DEF "${SAL_PLUGINS}/automation/automation.def"
  INCLUDES "${SAL_PLUGINS}/automation/generated"
  LIBS comsuppw shlwapi comctl32 version
)

# Exclude the generated C file from precompiled headers
set_source_files_properties(
  "${SAL_PLUGINS}/automation/generated/salamander_i.c"
  PROPERTIES SKIP_PRECOMPILE_HEADERS ON
)
