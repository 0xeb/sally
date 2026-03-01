# Plugin build definition for Sally
# Plugin: diskmap

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

