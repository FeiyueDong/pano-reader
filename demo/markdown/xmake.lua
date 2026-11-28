-- Phase 3.0 Markdown + math demo.
-- Included by the root xmake.lua only when -o build_markdown_demo=y.

local demo_dir = os.scriptdir()
local jkqt = os.getenv("JKQT_ROOT")
    or path.join(os.projectdir(), "build/deps/jkqt/usr")

target("pano-markdown-demo")
    add_rules("qt.widgetapp")
    add_files(path.join(demo_dir, "src/**.cpp"))
    add_files(path.join(demo_dir, "src/**.h"))
    add_frameworks("QtWidgets", "QtXml")
    -- Demo 自身源码也包含 <md4c-html.h>，必须在本目标上引入 md4c 包。
    add_packages("md4c")
    add_includedirs(path.join(demo_dir, "src"), path.join(jkqt, "include"))
    add_linkdirs(path.join(jkqt, "lib"))
    add_links("JKQTMathText6_RelWithDebInfo", "JKQTCommon6_RelWithDebInfo")
    add_links("md4c-html", "md4c")
    add_defines('PANO_MARKDOWN_DIR="' .. (demo_dir:gsub("\\", "/")) .. '"')
    add_rpathdirs(path.join(jkqt, "lib"))
    set_encodings("utf-8")
