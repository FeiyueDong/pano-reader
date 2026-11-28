local tools = os.scriptdir()
local project_src = path.join(os.projectdir(), "src")
local qt_prefix = get_config("qt") or os.getenv("PANO_QT_PREFIX")

target("pano-pdf-timing")
    add_rules("qt.console")
    add_files(path.join(tools, "pdf_timing_probe.cpp"))
    add_frameworks("QtCore", "QtGui", "QtPdf")
    add_includedirs(project_src)
    if qt_prefix then add_rpathdirs(path.join(qt_prefix, "lib")) end

target("pano-open-timing")
    add_rules("qt.widgetapp")
    add_files(path.join(tools, "open_timing_probe.cpp"))
    add_files(path.join(os.projectdir(), "resources", "resources.qrc"))
    add_frameworks("QtCore", "QtGui", "QtWidgets", "QtPdf", "QtSvg")
    add_deps("pano-app")
    add_includedirs(project_src)
    set_encodings("utf-8")
    if qt_prefix then add_rpathdirs(path.join(qt_prefix, "lib")) end
