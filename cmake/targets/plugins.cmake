# Plugin target definitions for Sally
# Each plugin is defined using sal_add_plugin()
#
# With PCH support enabled, most plugins should now build correctly.

include_guard(GLOBAL)
include("${CMAKE_CURRENT_LIST_DIR}/../sal_plugin.cmake")

# -----------------------------------------------------------------------------
# checksum - File checksum calculator (MD5, SHA-256, SHA-512, CRC32)
# -----------------------------------------------------------------------------
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

# -----------------------------------------------------------------------------
# diskmap - Visual disk space usage map
# -----------------------------------------------------------------------------
sal_add_plugin(NAME diskmap
  SOURCES
    # DiskMap core
    "${SAL_PLUGINS}/diskmap/DiskMap/GUI.CWindow.cpp"
    "${SAL_PLUGINS}/diskmap/DiskMap/GUI.MainWindow.cpp"
    "${SAL_PLUGINS}/diskmap/DiskMap/TreeMap.FileData.CZDirectory.cpp"
    "${SAL_PLUGINS}/diskmap/DiskMap/TreeMap.FileData.CZFile.cpp"
    "${SAL_PLUGINS}/diskmap/DiskMap/TreeMap.Graphics.CCushionGraphics.cpp"
    "${SAL_PLUGINS}/diskmap/DiskMap/Utils.CZLocalizer.cpp"
    # DiskMapPlugin wrapper
    "${SAL_PLUGINS}/diskmap/DiskMapPlugin/DiskMapPlugin.cpp"
    "${SAL_PLUGINS}/diskmap/DiskMapPlugin/precomp.cpp"
  RC "${SAL_PLUGINS}/diskmap/DiskMapPlugin/DiskMapPlugin.rc"
  DEF "${SAL_PLUGINS}/diskmap/DiskMapPlugin/DiskMapPlugin.def"
  INCLUDES
    "${SAL_PLUGINS}/diskmap/DiskMap"
    "${SAL_PLUGINS}/diskmap/DiskMapPlugin"
  PCH "DiskMapPlugin/precomp.h"
)

# -----------------------------------------------------------------------------
# nethood - Network neighborhood browser
# -----------------------------------------------------------------------------
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
    "${SAL_PLUGINS}/nethood/nethoodfs2.cpp"
    "${SAL_PLUGINS}/nethood/nethoodmenu.cpp"
    "${SAL_PLUGINS}/nethood/precomp.cpp"
    "${SAL_PLUGINS}/nethood/salutils.cpp"
  RC "${SAL_PLUGINS}/nethood/nethood.rc"
  DEF "${SAL_PLUGINS}/nethood/nethood.def"
  LIBS mpr netapi32 wtsapi32 shlwapi
)

# -----------------------------------------------------------------------------
# folders - Virtual folders plugin
# -----------------------------------------------------------------------------
sal_add_plugin(NAME folders
  SOURCES
    "${SAL_PLUGINS}/folders/dialogs.cpp"
    "${SAL_PLUGINS}/folders/folders.cpp"
    "${SAL_PLUGINS}/folders/fs1.cpp"
    "${SAL_PLUGINS}/folders/fs2.cpp"
    "${SAL_PLUGINS}/folders/iltools.cpp"
    "${SAL_PLUGINS}/folders/precomp.cpp"
  RC "${SAL_PLUGINS}/folders/folders.rc"
  DEF "${SAL_PLUGINS}/folders/folders.def"
  LIBS shlwapi
)

# -----------------------------------------------------------------------------
# splitcbn - File splitter/combiner
# -----------------------------------------------------------------------------
sal_add_plugin(NAME splitcbn
  SOURCES
    "${SAL_PLUGINS}/splitcbn/combine.cpp"
    "${SAL_PLUGINS}/splitcbn/dialogs.cpp"
    "${SAL_PLUGINS}/splitcbn/precomp.cpp"
    "${SAL_PLUGINS}/splitcbn/split.cpp"
    "${SAL_PLUGINS}/splitcbn/splitcbn.cpp"
  RC "${SAL_PLUGINS}/splitcbn/splitcbn.rc"
  DEF "${SAL_PLUGINS}/splitcbn/splitcbn.def"
)

# -----------------------------------------------------------------------------
# peviewer - PE (Portable Executable) file viewer
# -----------------------------------------------------------------------------
sal_add_plugin(NAME peviewer
  SOURCES
    "${SAL_PLUGINS}/peviewer/cfg.cpp"
    "${SAL_PLUGINS}/peviewer/cfgdlg.cpp"
    "${SAL_PLUGINS}/peviewer/dump_res.cpp"
    "${SAL_PLUGINS}/peviewer/pefile.cpp"
    "${SAL_PLUGINS}/peviewer/peviewer.cpp"
    "${SAL_PLUGINS}/peviewer/precomp.cpp"
  RC "${SAL_PLUGINS}/peviewer/peviewer.rc"
  DEF "${SAL_PLUGINS}/peviewer/peviewer.def"
  LIBS version
)

# -----------------------------------------------------------------------------
# regedt - Registry editor plugin
# -----------------------------------------------------------------------------
sal_add_plugin(NAME regedt
  SOURCES
    "${SAL_PLUGINS}/regedt/chmon.cpp"
    "${SAL_PLUGINS}/regedt/dialogs.cpp"
    "${SAL_PLUGINS}/regedt/editor.cpp"
    "${SAL_PLUGINS}/regedt/export.cpp"
    "${SAL_PLUGINS}/regedt/finddlg.cpp"
    "${SAL_PLUGINS}/regedt/finddlg2.cpp"
    "${SAL_PLUGINS}/regedt/fs.cpp"
    "${SAL_PLUGINS}/regedt/fs2.cpp"
    "${SAL_PLUGINS}/regedt/fs3.cpp"
    "${SAL_PLUGINS}/regedt/fs4.cpp"
    "${SAL_PLUGINS}/regedt/fs5.cpp"
    "${SAL_PLUGINS}/regedt/menu.cpp"
    "${SAL_PLUGINS}/regedt/precomp.cpp"
    "${SAL_PLUGINS}/regedt/regedt.cpp"
    "${SAL_PLUGINS}/regedt/utils.cpp"
  RC "${SAL_PLUGINS}/regedt/regedt.rc"
  DEF "${SAL_PLUGINS}/regedt/regedt.def"
)

# -----------------------------------------------------------------------------
# renamer - Batch file renamer
# -----------------------------------------------------------------------------
sal_add_plugin(NAME renamer
  SOURCES
    "${SAL_PLUGINS}/renamer/crename2.cpp"
    "${SAL_PLUGINS}/renamer/crenamer.cpp"
    "${SAL_PLUGINS}/renamer/dialogs.cpp"
    "${SAL_PLUGINS}/renamer/editor.cpp"
    "${SAL_PLUGINS}/renamer/menu.cpp"
    "${SAL_PLUGINS}/renamer/precomp.cpp"
    "${SAL_PLUGINS}/renamer/preview.cpp"
    "${SAL_PLUGINS}/renamer/regexp.cpp"
    "${SAL_PLUGINS}/renamer/renamer.cpp"
    "${SAL_PLUGINS}/renamer/rendlg.cpp"
    "${SAL_PLUGINS}/renamer/rendlg2.cpp"
    "${SAL_PLUGINS}/renamer/rendlg3.cpp"
    "${SAL_PLUGINS}/renamer/rendlg4.cpp"
    "${SAL_PLUGINS}/renamer/utils.cpp"
    "${SAL_PLUGINS}/renamer/varstr.cpp"
    # Character utilities from shared/plugcore
    "${SAL_SHARED}/plugcore/str.cpp"
  RC "${SAL_PLUGINS}/renamer/renamer.rc"
  DEF "${SAL_PLUGINS}/renamer/renamer.def"
  INCLUDES "${SAL_SHARED}/plugcore"
  DEFINES _CHAR_UNSIGNED DECLARE_REGIFACE_FUNCTIONS
)

# -----------------------------------------------------------------------------
# unlha - LHA/LZH archive extractor
# -----------------------------------------------------------------------------
sal_add_plugin(NAME unlha
  SOURCES
    "${SAL_PLUGINS}/unlha/lha.cpp"
    "${SAL_PLUGINS}/unlha/precomp.cpp"
    "${SAL_PLUGINS}/unlha/unlha.cpp"
  RC "${SAL_PLUGINS}/unlha/unlha.rc"
  DEF "${SAL_PLUGINS}/unlha/unlha.def"
)

# -----------------------------------------------------------------------------
# unrar - RAR archive extractor (requires unrar.dll from RARLab)
# Only build for x64 - ARM64 unrar.dll not available
# -----------------------------------------------------------------------------
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

# -----------------------------------------------------------------------------
# unole - OLE compound document extractor
# -----------------------------------------------------------------------------
sal_add_plugin(NAME unole
  SOURCES
    "${SAL_PLUGINS}/unole/dialogs.cpp"
    "${SAL_PLUGINS}/unole/precomp.cpp"
    "${SAL_PLUGINS}/unole/unole2.cpp"
  RC "${SAL_PLUGINS}/unole/unole2.rc"
  DEF "${SAL_PLUGINS}/unole/unole.def"
)

# -----------------------------------------------------------------------------
# uncab - Windows Cabinet (.cab) archive extractor
# -----------------------------------------------------------------------------
sal_add_plugin(NAME uncab
  SOURCES
    "${SAL_PLUGINS}/uncab/dialogs.cpp"
    "${SAL_PLUGINS}/uncab/precomp.cpp"
    "${SAL_PLUGINS}/uncab/uncab.cpp"
  RC "${SAL_PLUGINS}/uncab/uncab.rc"
  DEF "${SAL_PLUGINS}/uncab/uncab.def"
  INCLUDES "${SAL_SHARED}/plugcore"
  LIBS cabinet
)

# -----------------------------------------------------------------------------
# unchm - CHM (Compiled HTML Help) archive extractor
# -----------------------------------------------------------------------------
sal_add_plugin(NAME unchm
  SOURCES
    "${SAL_PLUGINS}/unchm/chmfile.cpp"
    "${SAL_PLUGINS}/unchm/precomp.cpp"
    "${SAL_PLUGINS}/unchm/unchm.cpp"
  RC "${SAL_PLUGINS}/unchm/unchm.rc"
  DEF "${SAL_PLUGINS}/unchm/unchm.def"
)

# -----------------------------------------------------------------------------
# unmime - MIME/email attachment extractor
# -----------------------------------------------------------------------------
sal_add_plugin(NAME unmime
  SOURCES
    "${SAL_PLUGINS}/unmime/decoder.cpp"
    "${SAL_PLUGINS}/unmime/parser.cpp"
    "${SAL_PLUGINS}/unmime/precomp.cpp"
    "${SAL_PLUGINS}/unmime/unmime.cpp"
  RC "${SAL_PLUGINS}/unmime/unmime.rc"
  DEF "${SAL_PLUGINS}/unmime/unmime.def"
)

# -----------------------------------------------------------------------------
# unarj - ARJ archive extractor
# -----------------------------------------------------------------------------
sal_add_plugin(NAME unarj
  SOURCES
    "${SAL_PLUGINS}/unarj/decode.cpp"
    "${SAL_PLUGINS}/unarj/dialogs.cpp"
    "${SAL_PLUGINS}/unarj/precomp.cpp"
    "${SAL_PLUGINS}/unarj/unarj.cpp"
    "${SAL_PLUGINS}/unarj/unarjspl.cpp"
  RC "${SAL_PLUGINS}/unarj/unarj.rc"
  DEF "${SAL_PLUGINS}/unarj/unarj.def"
  INCLUDES "${SAL_SHARED}/plugcore"
)

# -----------------------------------------------------------------------------
# unfat - FAT filesystem recovery tool
# -----------------------------------------------------------------------------
sal_add_plugin(NAME unfat
  SOURCES
    "${SAL_PLUGINS}/unfat/fat.cpp"
    "${SAL_PLUGINS}/unfat/precomp.cpp"
    "${SAL_PLUGINS}/unfat/unfat.cpp"
  RC "${SAL_PLUGINS}/unfat/unfat.rc"
  DEF "${SAL_PLUGINS}/unfat/unfat.def"
)

# -----------------------------------------------------------------------------
# automation - Scripting automation plugin (COM/ActiveScript)
# -----------------------------------------------------------------------------

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

# -----------------------------------------------------------------------------
# filecomp - File comparison plugin with text diff viewer
# -----------------------------------------------------------------------------
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

# -----------------------------------------------------------------------------
# fcremote - Remote file comparison helper executable (minimal, no CRT)
# -----------------------------------------------------------------------------
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
  # Minimal exe: static runtime, no exceptions, no buffer security
  target_compile_options(fcremote PRIVATE /MP /W3 /MT /GS- /EHs-c-)
  # No CRT, custom entry point
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

# Install fcremote with the filecomp plugin
install(TARGETS fcremote RUNTIME DESTINATION "plugins/filecomp")

# Add fcremote as a dependency of populate target (it's not a plugin so not auto-added)
# This must happen after sal_create_populate_target() is called in CMakeLists.txt
set_property(GLOBAL APPEND PROPERTY SAL_EXTRA_POPULATE_DEPS fcremote)

# -----------------------------------------------------------------------------
# tar - TAR/GZIP/BZIP2/LZH/RPM/DEB archive extractor
# -----------------------------------------------------------------------------
sal_add_plugin(NAME tar
  SOURCES
    # Shared sources (tar only uses dbg.cpp)
    "${SAL_SHARED}/dbg.cpp"
    # bzip2 sources (C files, no PCH)
    "${SAL_PLUGINS}/tar/bzip/bunzip.cpp"
    "${SAL_PLUGINS}/tar/bzip/bzlib.c"
    "${SAL_PLUGINS}/tar/bzip/crctable.c"
    "${SAL_PLUGINS}/tar/bzip/decompress.c"
    "${SAL_PLUGINS}/tar/bzip/huffman.c"
    "${SAL_PLUGINS}/tar/bzip/randtable.c"
    # Plugin sources
    "${SAL_PLUGINS}/tar/compress/uncompress.cpp"
    "${SAL_PLUGINS}/tar/deb/deb.cpp"
    "${SAL_PLUGINS}/tar/fileio.cpp"
    "${SAL_PLUGINS}/tar/gzip/gunzip.cpp"
    "${SAL_PLUGINS}/tar/lzh/lzh.cpp"
    "${SAL_PLUGINS}/tar/names.cpp"
    "${SAL_PLUGINS}/tar/precomp.cpp"
    "${SAL_PLUGINS}/tar/rpm/rpm.cpp"
    "${SAL_PLUGINS}/tar/rpm/rpmview.cpp"
    "${SAL_PLUGINS}/tar/tardll.cpp"
    "${SAL_PLUGINS}/tar/untar.cpp"
  RC "${SAL_PLUGINS}/tar/tar.rc"
  DEF "${SAL_PLUGINS}/tar/tar.def"
  INCLUDES "${SAL_PLUGINS}/tar"
  DEFINES BZ_NO_STDIO
  NO_SHARED
)

# Exclude bzip2 C files from precompiled headers
set_source_files_properties(
  "${SAL_PLUGINS}/tar/bzip/bzlib.c"
  "${SAL_PLUGINS}/tar/bzip/crctable.c"
  "${SAL_PLUGINS}/tar/bzip/decompress.c"
  "${SAL_PLUGINS}/tar/bzip/huffman.c"
  "${SAL_PLUGINS}/tar/bzip/randtable.c"
  PROPERTIES SKIP_PRECOMPILE_HEADERS ON
)

# -----------------------------------------------------------------------------
# uniso - ISO/NRG/DMG/UDF disc image extractor
# -----------------------------------------------------------------------------
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

# -----------------------------------------------------------------------------
# pak - PAK/PK3/WAD game archive extractor
# -----------------------------------------------------------------------------
sal_add_plugin(NAME pak
  SOURCES
    # Shared sources (pak only uses dbg.cpp)
    "${SAL_SHARED}/dbg.cpp"
    # Plugin sources
    "${SAL_PLUGINS}/pak/dll/add_del.cpp"
    "${SAL_PLUGINS}/pak/dll/pak_dll.cpp"
    "${SAL_PLUGINS}/pak/spl/pak.cpp"
    "${SAL_PLUGINS}/pak/spl/pak2.cpp"
    "${SAL_PLUGINS}/pak/spl/precomp.cpp"
  RC "${SAL_PLUGINS}/pak/spl/pak.rc"
  DEF "${SAL_PLUGINS}/pak/pak.def"
  INCLUDES
    "${SAL_PLUGINS}/pak/spl"
    "${SAL_SHARED}/plugcore"
  NO_SHARED
  NO_LANG
)

# pak lang file is in spl/lang/ instead of lang/
set_source_files_properties("${SAL_PLUGINS}/pak/spl/lang/lang.rc" PROPERTIES
  COMPILE_DEFINITIONS "_LANG;WINVER=0x0601"
)
add_library(plugin_pak_lang SHARED "${SAL_PLUGINS}/pak/spl/lang/lang.rc")
target_include_directories(plugin_pak_lang PRIVATE
  "${SAL_PLUGINS}/pak/spl"
  "${SAL_SHARED}"
)
if(MSVC)
  target_link_options(plugin_pak_lang PRIVATE /NOENTRY /MANIFEST:NO)
elseif(MINGW)
  target_sources(plugin_pak_lang PRIVATE "${SAL_SRC}/common/dllstub.c")
endif()
set_target_properties(plugin_pak_lang PROPERTIES
  OUTPUT_NAME english
  SUFFIX .slg
  RUNTIME_OUTPUT_DIRECTORY "${SAL_OUTPUT_BASE}/$<CONFIG>_${SAL_PLATFORM}/plugins/pak/lang"
  LIBRARY_OUTPUT_DIRECTORY "${SAL_OUTPUT_BASE}/$<CONFIG>_${SAL_PLATFORM}/plugins/pak/lang"
)
set_property(GLOBAL APPEND PROPERTY SAL_PLUGIN_LANGS_LIST plugin_pak_lang)

# -----------------------------------------------------------------------------
# zip - ZIP archive handler (create, extract, modify)
# -----------------------------------------------------------------------------
sal_add_plugin(NAME zip
  SOURCES
    # Shared sources (zip only uses dbg.cpp + plugcore/resedit.cpp)
    "${SAL_SHARED}/dbg.cpp"
    "${SAL_SHARED}/plugcore/resedit.cpp"
    # Plugin sources
    "${SAL_PLUGINS}/zip/add.cpp"
    "${SAL_PLUGINS}/zip/add_del.cpp"
    "${SAL_PLUGINS}/zip/bits.cpp"
    "${SAL_PLUGINS}/zip/chicon.cpp"
    "${SAL_PLUGINS}/zip/common.cpp"
    "${SAL_PLUGINS}/zip/common2.cpp"
    "${SAL_PLUGINS}/zip/const.cpp"
    "${SAL_PLUGINS}/zip/crypt.cpp"
    "${SAL_PLUGINS}/zip/deflate.cpp"
    "${SAL_PLUGINS}/zip/del.cpp"
    "${SAL_PLUGINS}/zip/dialogs.cpp"
    "${SAL_PLUGINS}/zip/dialogs2.cpp"
    "${SAL_PLUGINS}/zip/dialogs3.cpp"
    "${SAL_PLUGINS}/zip/explode.cpp"
    "${SAL_PLUGINS}/zip/extract.cpp"
    "${SAL_PLUGINS}/zip/inflate.cpp"
    "${SAL_PLUGINS}/zip/iosfxset.cpp"
    "${SAL_PLUGINS}/zip/list.cpp"
    "${SAL_PLUGINS}/zip/main.cpp"
    "${SAL_PLUGINS}/zip/memapi.cpp"
    "${SAL_PLUGINS}/zip/precomp.cpp"
    "${SAL_PLUGINS}/zip/prevsfx.cpp"
    "${SAL_PLUGINS}/zip/repair.cpp"
    "${SAL_PLUGINS}/zip/trees.cpp"
    "${SAL_PLUGINS}/zip/unbzip2.cpp"
    "${SAL_PLUGINS}/zip/unreduce.cpp"
    "${SAL_PLUGINS}/zip/unshrink.cpp"
  RC "${SAL_PLUGINS}/zip/zip.rc"
  DEF "${SAL_PLUGINS}/zip/zip.def"
  INCLUDES "${SAL_SHARED}/plugcore"
  DEFINES ZIP_DLL
  NO_SHARED
)

# -----------------------------------------------------------------------------
# webviewer - WebView2-based HTML/Markdown viewer
# -----------------------------------------------------------------------------
sal_add_plugin(NAME webviewer
  SOURCES
    # Shared sources
    "${SAL_SHARED}/auxtools.cpp"
    "${SAL_SHARED}/dbg.cpp"
    # cmark-gfm extensions (C files)
    "${SAL_PLUGINS}/webviewer/cmark-gfm/extensions/autolink.c"
    "${SAL_PLUGINS}/webviewer/cmark-gfm/extensions/core-extensions.c"
    "${SAL_PLUGINS}/webviewer/cmark-gfm/extensions/ext_scanners.c"
    "${SAL_PLUGINS}/webviewer/cmark-gfm/extensions/strikethrough.c"
    "${SAL_PLUGINS}/webviewer/cmark-gfm/extensions/table.c"
    "${SAL_PLUGINS}/webviewer/cmark-gfm/extensions/tagfilter.c"
    "${SAL_PLUGINS}/webviewer/cmark-gfm/extensions/tasklist.c"
    # cmark-gfm src (C files)
    "${SAL_PLUGINS}/webviewer/cmark-gfm/src/arena.c"
    "${SAL_PLUGINS}/webviewer/cmark-gfm/src/blocks.c"
    "${SAL_PLUGINS}/webviewer/cmark-gfm/src/buffer.c"
    "${SAL_PLUGINS}/webviewer/cmark-gfm/src/cmark.c"
    "${SAL_PLUGINS}/webviewer/cmark-gfm/src/cmark_ctype.c"
    "${SAL_PLUGINS}/webviewer/cmark-gfm/src/commonmark.c"
    "${SAL_PLUGINS}/webviewer/cmark-gfm/src/footnotes.c"
    "${SAL_PLUGINS}/webviewer/cmark-gfm/src/houdini_href_e.c"
    "${SAL_PLUGINS}/webviewer/cmark-gfm/src/houdini_html_e.c"
    "${SAL_PLUGINS}/webviewer/cmark-gfm/src/houdini_html_u.c"
    "${SAL_PLUGINS}/webviewer/cmark-gfm/src/html.c"
    "${SAL_PLUGINS}/webviewer/cmark-gfm/src/inlines.c"
    "${SAL_PLUGINS}/webviewer/cmark-gfm/src/iterator.c"
    "${SAL_PLUGINS}/webviewer/cmark-gfm/src/latex.c"
    "${SAL_PLUGINS}/webviewer/cmark-gfm/src/linked_list.c"
    "${SAL_PLUGINS}/webviewer/cmark-gfm/src/main.c"
    "${SAL_PLUGINS}/webviewer/cmark-gfm/src/man.c"
    "${SAL_PLUGINS}/webviewer/cmark-gfm/src/map.c"
    "${SAL_PLUGINS}/webviewer/cmark-gfm/src/node.c"
    "${SAL_PLUGINS}/webviewer/cmark-gfm/src/plaintext.c"
    "${SAL_PLUGINS}/webviewer/cmark-gfm/src/plugin.c"
    "${SAL_PLUGINS}/webviewer/cmark-gfm/src/references.c"
    "${SAL_PLUGINS}/webviewer/cmark-gfm/src/registry.c"
    "${SAL_PLUGINS}/webviewer/cmark-gfm/src/render.c"
    "${SAL_PLUGINS}/webviewer/cmark-gfm/src/scanners.c"
    "${SAL_PLUGINS}/webviewer/cmark-gfm/src/syntax_extension.c"
    "${SAL_PLUGINS}/webviewer/cmark-gfm/src/utf8.c"
    "${SAL_PLUGINS}/webviewer/cmark-gfm/src/xml.c"
    # Plugin sources
    "${SAL_PLUGINS}/webviewer/webviewer.cpp"
    "${SAL_PLUGINS}/webviewer/markdown.cpp"
    "${SAL_PLUGINS}/webviewer/precomp.cpp"
  RC "${SAL_PLUGINS}/webviewer/webviewer.rc"
  DEF "${SAL_PLUGINS}/webviewer/webviewer.def"
  INCLUDES
    "${SAL_PLUGINS}/webviewer/cmark-gfm/src"
    "${SAL_PLUGINS}/webviewer/cmark-gfm/extensions"
    "${SAL_PLUGINS}/webviewer/cmark-gfm/build/src"
    "${SAL_PLUGINS}/webviewer/cmark-gfm/build/extensions"
    "${WEBVIEW2_INCLUDE}"
  DEFINES CMARK_GFM_STATIC_DEFINE CMARK_GFM_EXTENSIONS_STATIC_DEFINE
  LIBS "${WEBVIEW2_LIB}"
  NO_SHARED
)

# Exclude cmark-gfm C files from precompiled headers
file(GLOB CMARK_C_SOURCES
  "${SAL_PLUGINS}/webviewer/cmark-gfm/extensions/*.c"
  "${SAL_PLUGINS}/webviewer/cmark-gfm/src/*.c"
)
set_source_files_properties(${CMARK_C_SOURCES} PROPERTIES SKIP_PRECOMPILE_HEADERS ON)

# -----------------------------------------------------------------------------
# checkver - Version checker for Salamander updates
# -----------------------------------------------------------------------------
sal_add_plugin(NAME checkver
  SOURCES
    "${SAL_SHARED}/dbg.cpp"
    "${SAL_PLUGINS}/checkver/checkver.cpp"
    "${SAL_PLUGINS}/checkver/data.cpp"
    "${SAL_PLUGINS}/checkver/dialogs.cpp"
    "${SAL_PLUGINS}/checkver/internet.cpp"
    "${SAL_PLUGINS}/checkver/logwnd.cpp"
    "${SAL_PLUGINS}/checkver/precomp.cpp"
  RC "${SAL_PLUGINS}/checkver/checkver.rc"
  DEF "${SAL_PLUGINS}/checkver/checkver.def"
  LIBS wininet
  NO_SHARED
)

# -----------------------------------------------------------------------------
# dbviewer - Database file viewer (DBF, CSV)
# -----------------------------------------------------------------------------
sal_add_plugin(NAME dbviewer
  SOURCES
    "${SAL_SHARED}/auxtools.cpp"
    "${SAL_SHARED}/dbg.cpp"
    "${SAL_SHARED}/winliblt.cpp"
    "${SAL_PLUGINS}/dbviewer/csvlib/csvlib.cpp"
    "${SAL_PLUGINS}/dbviewer/dbflib/dbflib.cpp"
    "${SAL_PLUGINS}/dbviewer/data.cpp"
    "${SAL_PLUGINS}/dbviewer/dbviewer.cpp"
    "${SAL_PLUGINS}/dbviewer/dialogs.cpp"
    "${SAL_PLUGINS}/dbviewer/parser.cpp"
    "${SAL_PLUGINS}/dbviewer/precomp.cpp"
    "${SAL_PLUGINS}/dbviewer/renmain.cpp"
    "${SAL_PLUGINS}/dbviewer/renpaint.cpp"
  RC "${SAL_PLUGINS}/dbviewer/dbviewer.rc"
  DEF "${SAL_PLUGINS}/dbviewer/dbviewer.def"
  NO_SHARED
)

# -----------------------------------------------------------------------------
# mmviewer - Multimedia file viewer (MP3, MP4, OGG, WAV, WMA, MOD, VQF)
# -----------------------------------------------------------------------------
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

# -----------------------------------------------------------------------------
# portables - Windows Portable Devices (WPD) filesystem plugin
# -----------------------------------------------------------------------------
sal_add_plugin(NAME portables
  SOURCES
    "${SAL_SHARED}/dbg.cpp"
    "${SAL_SHARED}/mhandles.cpp"
    "${SAL_SHARED}/winliblt.cpp"
    "${SAL_PLUGINS}/portables/device.cpp"
    "${SAL_PLUGINS}/portables/entry.cpp"
    "${SAL_PLUGINS}/portables/fx.cpp"
    "${SAL_PLUGINS}/portables/fxfs.cpp"
    "${SAL_PLUGINS}/portables/globals.cpp"
    "${SAL_PLUGINS}/portables/notifier.cpp"
    "${SAL_PLUGINS}/portables/precomp.cpp"
    "${SAL_PLUGINS}/portables/wpddeviceicons.cpp"
    "${SAL_PLUGINS}/portables/wpdfs.cpp"
    "${SAL_PLUGINS}/portables/wpdfscontentlevel.cpp"
    "${SAL_PLUGINS}/portables/wpdfsdevicelevel.cpp"
    "${SAL_PLUGINS}/portables/wpdplug.cpp"
  RC "${SAL_PLUGINS}/portables/wpd.rc"
  DEF "${SAL_PLUGINS}/portables/portables.def"
  LIBS PortableDeviceGuids
  NO_SHARED
)

# -----------------------------------------------------------------------------
# wmobile - Windows Mobile device filesystem plugin
# -----------------------------------------------------------------------------
sal_add_plugin(NAME wmobile
  SOURCES
    "${SAL_PLUGINS}/wmobile/dialogs.cpp"
    "${SAL_PLUGINS}/wmobile/fs1.cpp"
    "${SAL_PLUGINS}/wmobile/fs2.cpp"
    "${SAL_PLUGINS}/wmobile/precomp.cpp"
    "${SAL_PLUGINS}/wmobile/rapi.cpp"
    "${SAL_PLUGINS}/wmobile/wmobile.cpp"
  RC "${SAL_PLUGINS}/wmobile/wmobile.rc"
  DEF "${SAL_PLUGINS}/wmobile/wmobile.def"
  INCLUDES "${SAL_PLUGINS}/wmobile/rapi"
)

# -----------------------------------------------------------------------------
# undelete - Deleted file recovery (FAT/NTFS)
# -----------------------------------------------------------------------------
# -----------------------------------------------------------------------------
# demoplug - Demo/sample plugin (SDK example)
# -----------------------------------------------------------------------------
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

# -----------------------------------------------------------------------------
# undelete - Undelete files from FAT/NTFS
# -----------------------------------------------------------------------------
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

message(STATUS "Configured plugins with PCH support")
