-- Phase 0 PDF reader demo (archived).
-- Included by the root xmake.lua only when -o build_demo=y is configured.
-- Local paths are resolved from this file, never from the repository root.

local demo_dir = os.scriptdir()
local qt_prefix = get_config("qt") or os.getenv("PANO_QT_PREFIX")

target("pano-demo-reader")
    add_rules("qt.widgetapp")
    add_files(path.join(demo_dir, "src/**.cpp"))
    add_files(path.join(demo_dir, "src/**.h"))
    add_frameworks("QtWidgets", "QtPdf")
    add_includedirs(path.join(demo_dir, "src"))
    set_encodings("utf-8")
    if qt_prefix then
        add_rpathdirs(path.join(qt_prefix, "lib"))
    end

target("pano-demo-preview-probe")
    add_rules("qt.widgetapp")
    add_files(path.join(demo_dir, "tools/preview_probe.cpp"))
    add_files(path.join(demo_dir, "src/note/MarkdownRenderer.cpp"))
    add_files(path.join(demo_dir, "src/note/MathRenderer.cpp"))
    add_files(path.join(demo_dir, "src/note/MarkdownPreview.cpp"))
    add_files(path.join(demo_dir, "src/note/MarkdownPreview.h"))
    add_frameworks("QtWidgets")
    add_includedirs(path.join(demo_dir, "src"))
    if qt_prefix then
        add_rpathdirs(path.join(qt_prefix, "lib"))
    end

target("pano-demo-coordinate-probe")
    add_rules("qt.console")
    add_files(path.join(demo_dir, "tools/coordinate_probe.cpp"))
    add_files(path.join(demo_dir, "src/reader/PageLayout.cpp"))
    add_frameworks("QtGui")
    add_includedirs(path.join(demo_dir, "src"))
    if qt_prefix then
        add_rpathdirs(path.join(qt_prefix, "lib"))
    end

target("pano-demo-math-probe")
    add_rules("qt.widgetapp")
    add_files(path.join(demo_dir, "tools/math_probe.cpp"))
    add_files(path.join(demo_dir, "src/note/MathRenderer.cpp"))
    add_frameworks("QtWidgets")
    add_includedirs(path.join(demo_dir, "src"))
    if qt_prefix then
        add_rpathdirs(path.join(qt_prefix, "lib"))
    end

target("pano-demo-ui-probe")
    add_rules("qt.widgetapp")
    add_files(path.join(demo_dir, "src/app/*.cpp"))
    add_files(path.join(demo_dir, "src/pdf/*.cpp"))
    add_files(path.join(demo_dir, "src/reader/*.cpp"))
    add_files(path.join(demo_dir, "src/annotation/*.cpp"))
    add_files(path.join(demo_dir, "src/note/*.cpp"))
    add_files(path.join(demo_dir, "src/storage/*.cpp"))
    add_files(path.join(demo_dir, "src/app/*.h"))
    add_files(path.join(demo_dir, "src/pdf/*.h"))
    add_files(path.join(demo_dir, "src/reader/*.h"))
    add_files(path.join(demo_dir, "src/annotation/*.h"))
    add_files(path.join(demo_dir, "src/note/*.h"))
    add_files(path.join(demo_dir, "src/storage/*.h"))
    add_files(path.join(demo_dir, "tools/ui_probe.cpp"))
    add_frameworks("QtWidgets", "QtPdf")
    add_includedirs(path.join(demo_dir, "src"))
    set_encodings("utf-8")
    if qt_prefix then
        add_rpathdirs(path.join(qt_prefix, "lib"))
    end

target("pano-demo-flow-probe")
    add_rules("qt.widgetapp")
    add_files(path.join(demo_dir, "src/app/*.cpp"))
    add_files(path.join(demo_dir, "src/pdf/*.cpp"))
    add_files(path.join(demo_dir, "src/reader/*.cpp"))
    add_files(path.join(demo_dir, "src/annotation/*.cpp"))
    add_files(path.join(demo_dir, "src/note/*.cpp"))
    add_files(path.join(demo_dir, "src/storage/*.cpp"))
    add_files(path.join(demo_dir, "src/app/*.h"))
    add_files(path.join(demo_dir, "src/pdf/*.h"))
    add_files(path.join(demo_dir, "src/reader/*.h"))
    add_files(path.join(demo_dir, "src/annotation/*.h"))
    add_files(path.join(demo_dir, "src/note/*.h"))
    add_files(path.join(demo_dir, "src/storage/*.h"))
    add_files(path.join(demo_dir, "tools/flow_probe.cpp"))
    add_frameworks("QtWidgets", "QtPdf")
    add_includedirs(path.join(demo_dir, "src"))
    set_encodings("utf-8")
    if qt_prefix then
        add_rpathdirs(path.join(qt_prefix, "lib"))
    end
