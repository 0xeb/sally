# Plugin target definitions for Open Salamander
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
    # Character utilities from shared/lukas
    "${SAL_SHARED}/lukas/str.cpp"
  RC "${SAL_PLUGINS}/renamer/renamer.rc"
  DEF "${SAL_PLUGINS}/renamer/renamer.def"
  INCLUDES "${SAL_SHARED}/lukas"
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

message(STATUS "Configured plugins with PCH support")
