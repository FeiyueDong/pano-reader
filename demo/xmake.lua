set_project("pano-reader")
set_version("0.1.0")
set_license("MIT")
set_languages("c++20")

add_rules("mode.debug", "mode.release")
add_rules("qt.widgetapp")

if is_plat("windows") then
    add_cxflags("/utf-8")
end

-- Qt SDK prefix. Configure with: xmake f --qt=/path/to/Qt/6.x.y/gcc_64
local qt_prefix = get_config("qt") or os.getenv("PANO_QT_PREFIX")

target("pano-reader")
    add_files("src/**.cpp")
    add_files("src/**.h")
    add_frameworks("QtWidgets", "QtPdf")
    add_includedirs("src")
    set_encodings("utf-8")
    if qt_prefix then
        add_rpathdirs(path.join(qt_prefix, "lib"))
    end

target("pano-preview-probe")
    set_kind("binary")
    add_files("tools/preview_probe.cpp")
    add_files("src/note/MarkdownRenderer.cpp")
    add_files("src/note/MathRenderer.cpp")
    add_files("src/note/MarkdownPreview.cpp")
    add_files("src/note/MarkdownPreview.h")
    add_frameworks("QtWidgets")
    add_includedirs("src")
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

target("pano-math-probe")
    set_kind("binary")
    add_files("tools/math_probe.cpp")
    add_files("src/note/MathRenderer.cpp")
    add_frameworks("QtWidgets")
    add_includedirs("src")
    if qt_prefix then
        add_rpathdirs(path.join(qt_prefix, "lib"))
    end


target("pano-ui-probe")
    set_kind("binary")
    add_files("src/app/*.cpp")
    add_files("src/pdf/*.cpp")
    add_files("src/reader/*.cpp")
    add_files("src/annotation/*.cpp")
    add_files("src/note/*.cpp")
    add_files("src/storage/*.cpp")
    add_files("src/app/*.h")
    add_files("src/pdf/*.h")
    add_files("src/reader/*.h")
    add_files("src/annotation/*.h")
    add_files("src/note/*.h")
    add_files("src/storage/*.h")
    add_files("tools/ui_probe.cpp")
    add_frameworks("QtWidgets", "QtPdf")
    add_includedirs("src")
    set_encodings("utf-8")
    if qt_prefix then
        add_rpathdirs(path.join(qt_prefix, "lib"))
    end

target("pano-flow-probe")
    set_kind("binary")
    add_files("src/app/*.cpp")
    add_files("src/pdf/*.cpp")
    add_files("src/reader/*.cpp")
    add_files("src/annotation/*.cpp")
    add_files("src/note/*.cpp")
    add_files("src/storage/*.cpp")
    add_files("src/app/*.h")
    add_files("src/pdf/*.h")
    add_files("src/reader/*.h")
    add_files("src/annotation/*.h")
    add_files("src/note/*.h")
    add_files("src/storage/*.h")
    add_files("tools/flow_probe.cpp")
    add_frameworks("QtWidgets", "QtPdf")
    add_includedirs("src")
    set_encodings("utf-8")
    if qt_prefix then
        add_rpathdirs(path.join(qt_prefix, "lib"))
    end
