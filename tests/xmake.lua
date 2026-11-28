local tests = os.scriptdir()
local project_src = path.join(os.projectdir(), "src")
local qt_prefix = get_config("qt") or os.getenv("PANO_QT_PREFIX")
local sample = path.join(os.projectdir(), "examples", "sample-paper.pdf")

local function finish(name, widget)
    target(name)
    if widget then
        add_rules("qt.widgetapp")
        add_files(path.join(os.projectdir(), "resources", "resources.qrc"))
        add_frameworks("QtCore", "QtGui", "QtWidgets", "QtPdf", "QtSvg", "QtXml")
    else
        add_rules("qt.console")
        add_frameworks("QtCore", "QtGui")
    end
    add_includedirs(project_src)
    add_defines('PANO_SAMPLE_PDF="' .. sample .. '"')
    set_encodings("utf-8")
    if qt_prefix then
        add_rpathdirs(path.join(qt_prefix, "lib"))
    end
end

target("pano-coordinate-probe")
    add_rules("qt.console")
    add_files(path.join(tests, "coordinate_probe.cpp"))
    add_frameworks("QtCore", "QtGui")
    add_deps("pano-reader-view")
    add_includedirs(project_src)
    if qt_prefix then add_rpathdirs(path.join(qt_prefix, "lib")) end

target("pano-text-geometry-probe")
    add_rules("qt.console")
    add_files(path.join(tests, "text_geometry_probe.cpp"))
    add_frameworks("QtCore", "QtGui")
    add_deps("pano-annotation")
    add_includedirs(project_src)
    if qt_prefix then add_rpathdirs(path.join(qt_prefix, "lib")) end

target("pano-library-probe")
    add_rules("qt.console")
    add_files(path.join(tests, "library_probe.cpp"))
    add_frameworks("QtCore", "QtGui", "QtPdf")
    add_deps("pano-library", "pano-annotation")
    add_includedirs(project_src)
    add_defines('PANO_SAMPLE_PDF="' .. sample .. '"')
    if qt_prefix then add_rpathdirs(path.join(qt_prefix, "lib")) end

for _, name in ipairs({"phase1", "phase2", "interaction", "note", "theme"}) do
    target("pano-" .. name .. "-probe")
    add_rules("qt.widgetapp")
    add_files(path.join(tests, name .. "_probe.cpp"))
    add_files(path.join(os.projectdir(), "resources", "resources.qrc"))
    add_frameworks("QtCore", "QtGui", "QtWidgets", "QtPdf", "QtSvg", "QtXml")
    add_deps("pano-app")
    add_includedirs(project_src)
    add_defines('PANO_SAMPLE_PDF="' .. sample .. '"')
    set_encodings("utf-8")
    if qt_prefix then add_rpathdirs(path.join(qt_prefix, "lib")) end
end
