set_project("pano-reader")
set_version("0.2.0")
set_license("MIT")
set_languages("c++20")

add_rules("mode.debug", "mode.release")
-- Optional targets. All are OFF by default: a normal build only produces the
-- formal application and its internal static libraries.
option("build_tests")
    set_default(false)
    set_showmenu(true)
    set_description("Build the functional regression probes")
option_end()

option("build_tools")
    set_default(false)
    set_showmenu(true)
    set_description("Build the performance diagnostics")
option_end()

option("build_demo")
    set_default(false)
    set_showmenu(true)
    set_description("Build the archived Phase 0 PDF demo")
option_end()

option("build_markdown_demo")
    set_default(false)
    set_showmenu(true)
    set_description("Build the Phase 3.0 Markdown + math demo")
option_end()

if is_plat("windows") then
    add_cxflags("/utf-8")
end

-- Qt SDK prefix. Configure with: xmake f -c -m debug --qt=/path/to/Qt/6.x.y/gcc_64
local qt_prefix = get_config("qt") or os.getenv("PANO_QT_PREFIX")
function pano_qt_prefix()
    return qt_prefix
end

add_requires("md4c")

includes("src/xmake.lua")

if has_config("build_tests") then
    includes("tests/xmake.lua")
end
if has_config("build_tools") then
    includes("tools/xmake.lua")
end
if has_config("build_demo") then
    includes("demo/xmake.lua")
end
if has_config("build_markdown_demo") then
    includes("demo/markdown/xmake.lua")
end
