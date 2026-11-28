local project_src = path.join(os.projectdir(), "src")
local qt_prefix = get_config("qt") or os.getenv("PANO_QT_PREFIX")
local jkqt_root = os.getenv("JKQT_ROOT") or path.join(os.projectdir(), "build/deps/jkqt/usr")

target("pano-note")
    add_rules("qt.static")
    add_files(path.join(project_src, "note/*.cpp"))
    add_files(path.join(project_src, "note/*.h"))
    add_frameworks("QtCore", "QtGui", "QtWidgets")
    add_deps("pano-common")
    add_includedirs(project_src)
    add_includedirs(path.join(jkqt_root, "include"))
    add_linkdirs(path.join(jkqt_root, "lib"))
    add_links("JKQTMathText6_RelWithDebInfo", "JKQTCommon6_RelWithDebInfo",
              "md4c-html", "md4c")
    if qt_prefix then
        add_rpathdirs(path.join(qt_prefix, "lib"))
    end
    add_rpathdirs(path.join(jkqt_root, "lib"))
    set_encodings("utf-8")
