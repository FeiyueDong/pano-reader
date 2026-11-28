local src = os.scriptdir()
local project_src = path.join(os.projectdir(), "src")
local qt_prefix = get_config("qt") or os.getenv("PANO_QT_PREFIX")
local jkqt_root = os.getenv("JKQT_ROOT") or path.join(os.projectdir(), "build/deps/jkqt/usr")
-- JKQTMathText / JKQTCommon 的库名。Linux 用发行版包（Arch: jkqtplotter），Windows 用
-- scripts/fetch-jkqt-windows.ps1 构建，两边都打开 JKQtPlotter_BUILD_DECORATE_LIBNAMES_WITH_BUILDTYPE，
-- 因此都带 _RelWithDebInfo 后缀。若要换成别的构建类型或重命名库，用环境变量覆盖。
local jkqt_math_lib = os.getenv("JKQT_MATHTEXT_LIB") or "JKQTMathText6_RelWithDebInfo"
local jkqt_common_lib = os.getenv("JKQT_COMMON_LIB") or "JKQTCommon6_RelWithDebInfo"

local function common_target(name)
    add_rpathdirs()
end

target("pano-common")
    add_rules("qt.static")
    add_files(path.join(src, "common/*.cpp"))
    add_files(path.join(src, "common/*.h"))
    add_frameworks("QtCore")
    add_includedirs(project_src)
    set_encodings("utf-8")

target("pano-pdf")
    add_rules("qt.static")
    add_files(path.join(src, "pdf/*.cpp"))
    add_files(path.join(src, "pdf/*.h"))
    add_frameworks("QtCore", "QtGui", "QtPdf")
    add_deps("pano-common")
    add_includedirs(project_src)
    set_encodings("utf-8")

target("pano-annotation")
    add_rules("qt.static")
    add_files(path.join(src, "annotation/*.cpp"))
    add_files(path.join(src, "annotation/*.h"))
    add_frameworks("QtCore", "QtGui")
    add_deps("pano-common")
    add_includedirs(project_src)
    set_encodings("utf-8")

target("pano-note")
    add_rules("qt.static")
    add_files(path.join(src, "note/*.cpp"))
    add_files(path.join(src, "note/*.h"))
    add_frameworks("QtCore", "QtGui", "QtWidgets")
    add_deps("pano-common")
    -- note/MarkdownRenderer.cpp 直接包含 <md4c-html.h>，包必须加在编译它的目标上，
    -- 依赖它的可执行目标加 add_packages 不会把包含目录传给静态库。
    add_packages("md4c")
    add_includedirs(project_src)
    add_includedirs(path.join(jkqt_root, "include"))
    add_linkdirs(path.join(jkqt_root, "lib"))
    add_links(jkqt_math_lib, jkqt_common_lib,
              "md4c-html", "md4c")
    if qt_prefix then
        add_rpathdirs(path.join(qt_prefix, "lib"))
    end
    add_rpathdirs(path.join(jkqt_root, "lib"))
    set_encodings("utf-8")

target("pano-library")
    add_rules("qt.static")
    add_files(path.join(src, "library/*.cpp"))
    add_files(path.join(src, "library/*.h"))
    add_files(path.join(src, "storage/*.cpp"))
    add_files(path.join(src, "storage/*.h"))
    add_frameworks("QtCore", "QtGui", "QtPdf")
    add_deps("pano-common", "pano-pdf")
    add_includedirs(project_src)
    set_encodings("utf-8")

target("pano-reader-view")
    add_rules("qt.static")
    add_files(path.join(src, "reader/*.cpp"))
    add_files(path.join(src, "reader/*.h"))
    add_frameworks("QtCore", "QtGui", "QtWidgets", "QtPdf")
    add_deps("pano-common", "pano-pdf", "pano-annotation")
    add_includedirs(project_src)
    set_encodings("utf-8")

target("pano-app")
    add_rules("qt.static")
    add_files(path.join(src, "app/*.cpp"))
    add_files(path.join(src, "app/*.h"))
    add_files(path.join(src, "document/*.cpp"))
    add_files(path.join(src, "document/*.h"))
    add_files(path.join(src, "settings/*.cpp"))
    add_files(path.join(src, "settings/*.h"))
    add_frameworks("QtCore", "QtGui", "QtWidgets", "QtPdf", "QtSvg")
    add_deps("pano-common", "pano-pdf", "pano-annotation", "pano-note", "pano-library",
             "pano-reader-view")
    add_includedirs(project_src)
    set_encodings("utf-8")

target("pano-reader")
    add_rules("qt.widgetapp")
    add_files(path.join(src, "main.cpp"))
    add_files(path.join(src, "..", "resources", "resources.qrc"))
    add_deps("pano-app")
    add_packages("md4c")
    -- JKQTCommon 静态库自身链接 Qt6::Xml，最终可执行文件必须显式带上 QtXml。
    add_frameworks("QtCore", "QtGui", "QtWidgets", "QtPdf", "QtSvg", "QtXml")
    add_includedirs(project_src)
    set_encodings("utf-8")
    if qt_prefix then
        add_rpathdirs(path.join(qt_prefix, "lib"))
    end
