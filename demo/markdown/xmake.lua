set_project("pano-markdown-demo")
set_version("0.1.0")
set_languages("c++20")
add_rules("mode.debug", "mode.release")
add_rules("qt.widgetapp")

-- JKQTMathText root: directory containing include/ and lib/.
-- Default: build/deps/jkqt/usr (created by tools/fetch-deps.sh).
local jkqt = get_config("jkqt") or os.getenv("JKQT_ROOT")
    or path.join(os.projectdir(), "build/deps/jkqt/usr")

target("pano-markdown-demo")
    set_kind("binary")
    add_files("src/**.cpp")
    add_files("src/**.h")
    add_frameworks("QtWidgets")
    add_includedirs("src", path.join(jkqt, "include"))
    add_linkdirs(path.join(jkqt, "lib"))
    add_links("JKQTMathText6_RelWithDebInfo", "JKQTCommon6_RelWithDebInfo")
    add_links("md4c-html", "md4c")
    add_defines('PANO_MARKDOWN_DIR="' .. os.projectdir() .. '"')
    add_rpathdirs(path.join(jkqt, "lib"))
    set_encodings("utf-8")
