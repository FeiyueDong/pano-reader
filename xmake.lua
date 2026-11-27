set_project("pano-reader")
set_version("0.2.0")
set_license("MIT")
set_languages("c++20")

add_rules("mode.debug", "mode.release")
add_rules("qt.widgetapp")

if is_plat("windows") then
    add_cxflags("/utf-8")
end

-- Qt SDK prefix. Configure with: xmake f -c -m debug --qt=/path/to/Qt/6.x.y/gcc_64
local qt_prefix = get_config("qt") or os.getenv("PANO_QT_PREFIX")

target("pano-reader")
    add_files("src/**.cpp")
    add_files("src/**.h")
    add_files("resources/resources.qrc")
    add_frameworks("QtWidgets", "QtPdf", "QtSvg")
    add_includedirs("src")
    set_encodings("utf-8")
    if qt_prefix then
        add_rpathdirs(path.join(qt_prefix, "lib"))
    end



target("pano-coordinate-probe")
    set_kind("binary")
    add_files("tools/coordinate_probe.cpp")
    add_files("src/reader/PageLayout.cpp")
    add_frameworks("QtGui")
    add_includedirs("src")
    if qt_prefix then
        add_rpathdirs(path.join(qt_prefix, "lib"))
    end

target("pano-library-probe")
    set_kind("binary")
    add_files("tools/library_probe.cpp")
    add_files("src/library/*.cpp")
    add_files("src/storage/*.cpp")
    add_files("src/pdf/*.cpp")
    add_files("src/annotation/*.cpp")
    add_files("src/library/*.h")
    add_files("src/storage/*.h")
    add_files("src/pdf/*.h")
    add_files("src/annotation/*.h")
    add_frameworks("QtGui", "QtPdf")
    add_includedirs("src")
    add_defines("PANO_SAMPLE_PDF=\"$(projectdir)/examples/sample-paper.pdf\"")
    if qt_prefix then
        add_rpathdirs(path.join(qt_prefix, "lib"))
    end


target("pano-text-geometry-probe")
    set_kind("binary")
    add_files("tools/text_geometry_probe.cpp")
    add_files("src/annotation/TextSelectionGeometry.cpp")
    add_files("src/annotation/AnnotationGeometry.cpp")
    add_frameworks("QtGui")
    add_includedirs("src")
    if qt_prefix then
        add_rpathdirs(path.join(qt_prefix, "lib"))
    end

target("pano-theme-probe")
    set_kind("binary")
    add_files("tools/theme_probe.cpp")
    add_files("src/app/*.cpp")
    add_files("src/library/*.cpp")
    add_files("src/document/*.cpp")
    add_files("src/pdf/*.cpp")
    add_files("src/reader/*.cpp")
    add_files("src/annotation/*.cpp")
    add_files("src/note/*.cpp")
    add_files("src/settings/*.cpp")
    add_files("src/storage/*.cpp")
    add_files("src/app/*.h")
    add_files("src/library/*.h")
    add_files("src/document/*.h")
    add_files("src/pdf/*.h")
    add_files("src/reader/*.h")
    add_files("src/annotation/*.h")
    add_files("src/note/*.h")
    add_files("src/settings/*.h")
    add_files("src/storage/*.h")
    add_frameworks("QtWidgets", "QtPdf", "QtSvg")
    add_files("resources/resources.qrc")
    add_includedirs("src")
    add_defines("PANO_SAMPLE_PDF=\"$(projectdir)/examples/sample-paper.pdf\"")
    if qt_prefix then
        add_rpathdirs(path.join(qt_prefix, "lib"))
    end

target("pano-phase1-probe")
    set_kind("binary")
    add_files("tools/phase1_probe.cpp")
    add_files("src/app/*.cpp")
    add_files("src/library/*.cpp")
    add_files("src/document/*.cpp")
    add_files("src/pdf/*.cpp")
    add_files("src/reader/*.cpp")
    add_files("src/annotation/*.cpp")
    add_files("src/note/*.cpp")
    add_files("src/settings/*.cpp")
    add_files("src/storage/*.cpp")
    add_files("src/app/*.h")
    add_files("src/library/*.h")
    add_files("src/document/*.h")
    add_files("src/pdf/*.h")
    add_files("src/reader/*.h")
    add_files("src/annotation/*.h")
    add_files("src/note/*.h")
    add_files("src/settings/*.h")
    add_files("src/storage/*.h")
    add_frameworks("QtWidgets", "QtPdf", "QtSvg")
    add_files("resources/resources.qrc")
    add_includedirs("src")
    add_defines("PANO_SAMPLE_PDF=\"$(projectdir)/examples/sample-paper.pdf\"")
    if qt_prefix then
        add_rpathdirs(path.join(qt_prefix, "lib"))
    end

target("pano-interaction-probe")
    set_kind("binary")
    add_files("tools/interaction_probe.cpp")
    add_files("src/app/*.cpp")
    add_files("src/library/*.cpp")
    add_files("src/document/*.cpp")
    add_files("src/pdf/*.cpp")
    add_files("src/reader/*.cpp")
    add_files("src/annotation/*.cpp")
    add_files("src/note/*.cpp")
    add_files("src/settings/*.cpp")
    add_files("src/storage/*.cpp")
    add_files("src/app/*.h")
    add_files("src/library/*.h")
    add_files("src/document/*.h")
    add_files("src/pdf/*.h")
    add_files("src/reader/*.h")
    add_files("src/annotation/*.h")
    add_files("src/note/*.h")
    add_files("src/settings/*.h")
    add_files("src/storage/*.h")
    add_frameworks("QtWidgets", "QtPdf", "QtSvg")
    add_files("resources/resources.qrc")
    add_includedirs("src")
    add_defines("PANO_SAMPLE_PDF=\"$(projectdir)/examples/sample-paper.pdf\"")
    if qt_prefix then
        add_rpathdirs(path.join(qt_prefix, "lib"))
    end
