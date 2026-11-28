local project_src = path.join(os.projectdir(), "src")
local qt_prefix = get_config("qt") or os.getenv("PANO_QT_PREFIX")

target("pano-reader")
    add_rules("qt.widgetapp")
    add_files(path.join(project_src, "main.cpp"))
    add_files(path.join(project_src, "..", "resources", "resources.qrc"))
    add_deps("pano-app")
    add_frameworks("QtCore", "QtGui", "QtWidgets", "QtPdf", "QtSvg")
    add_includedirs(project_src)
    set_encodings("utf-8")
    if qt_prefix then
        add_rpathdirs(path.join(qt_prefix, "lib"))
    end
