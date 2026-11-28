local project_src = path.join(os.projectdir(), "src")

target("pano-common")
    add_rules("qt.static")
    add_files(path.join(project_src, "common/*.cpp"))
    add_files(path.join(project_src, "common/*.h"))
    add_frameworks("QtCore")
    add_includedirs(project_src)
    set_encodings("utf-8")
