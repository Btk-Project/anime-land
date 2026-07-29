-- MARK: Basic
set_project("anime-land")
add_rules("mode.debug", "mode.release", "mode.releasedbg")
set_version("0.0.1", {build = "%Y%m%d%H%M"})
add_repositories("btk-repo https://github.com/Btk-Project/xmake-repo.git")
set_warnings("allextra")
set_encodings("utf-8")
set_policy("package.cmake_generator.ninja", true)

-- MARK: Options
option("stdc",   {showmenu = true, default = 23, values = {23}})
option("stdcxx", {showmenu = true, default = 23, values = {26, 23, 20}})
function stdc()   return "c"   .. tostring(get_config("stdc"))   end
function stdcxx() return "c++" .. tostring(get_config("stdcxx")) end

set_languages(stdc(), stdcxx())

-- RapidJSON defaults do not validate same-encoding UTF-8 on input/output.
-- Keep protocol strings strict at the application boundary.
add_defines("RAPIDJSON_PARSE_DEFAULT_FLAGS=kParseValidateEncodingFlag",
            "RAPIDJSON_WRITE_DEFAULT_FLAGS=kWriteValidateEncodingFlag")

add_configfiles("src/common/config.h.in")
set_configdir("src/common")
includes("lua/check")
check_macros("has_std_out_ptr",         "__cpp_lib_out_ptr",            {languages = stdcxx(), includes = "version"})
check_macros("has_std_expected",        "__cpp_lib_expected",           {languages = stdcxx(), includes = "version"})
check_macros("has_std_format",          "__cpp_lib_format",             {languages = stdcxx(), includes = "version"})

option("enable_tests")
    set_default(false)
    set_showmenu(true)
    set_description("Enable test")
    set_category("enable test")
option_end()

option("enable_spdlog")
    set_default(false)
    set_showmenu(true)
    set_description("Enable spdlog for log, should install spdlog")
    set_category("log provider")
    set_configvar("ANIME_LAND_USE_SPDLOG", true)
option_end()

if is_plat("linux") then
    option("memcheck")
        set_default(false)
        set_showmenu(true)
        set_description("Run unit targets through Valgrind")
        set_category("enable test")
    option_end()
end

if has_config("enable_tests") then
    add_requires("gtest")
end

if has_config("enable_spdlog") then
    add_requires("spdlog")
else
    -- Keep QMessageLogContext source locations available in release builds.
    add_defines("QT_MESSAGELOGCONTEXT")
end

if is_mode("debug") or is_mode("asan") or is_mode("ubsan") or is_mode("tsan") then
    add_defines("NEKO_PROTO_LOG_CONTEXT")
    if is_plat("linux") then
        add_cxxflags("-ftemplate-backtrace-limit=0")
    end
end 

if is_plat("linux") then
    add_cxxflags("-fcoroutines")
end

if is_plat("windows") then 
    add_cxxflags("/bigobj", "/Zc:preprocessor")
end
-- MARK: add requirements
add_requires("ilias", "libsodium", "neko-proto-tools", "ilias-sql",
             "ffmpeg", "miniaudio")
if not is_plat("linux") then
    add_requires("qt6quick >=6.2.0")
end

add_requireconfs("**ilias", {
    version = "x.x.x", -- 使用最新版本
    override = true, -- 强制覆盖
    configs = {shared = true,
               stdcxx = tonumber(get_config("stdcxx"))}
})

add_requireconfs("**libsodium", {
    version = "x.x.x", -- 使用最新版本
    override = true, -- 强制覆盖
    configs = {shared = true}
})

add_requireconfs("**neko-proto-tools", {
    version = "dev", -- 使用最新版本
    override = true, -- 强制覆盖
    configs = {shared = true,
               stdcxx = tonumber(get_config("stdcxx")),
               enable_rapidjson = true,
               enable_simdjson = false,
               enable_pugixml = false,
               enable_stdformat = true,
               enable_fmt = false,
               enable_spdlog = false,
               enable_communication = false,
               enable_jsonrpc = false,
               enable_protocol = false,
               enable_tomlplusplus = true}
})

add_requireconfs("**ilias-sql", {
    version = "dev", -- 使用最新版本
    override = true, -- 强制覆盖
    configs = {shared = true,
               stdcxx = tonumber(get_config("stdcxx")),
               enable_sqlite = "sqlite",
               enable_mysql = true,
               enable_orm_interface = true}
})

add_requireconfs("**spdlog", {
    override = true, 
    system = false, 
    version = "x.x.x", 
    configs = {shared = true, 
               header_only = false, 
               fmt_external = not has_config("has_std_format"), 
               std_format = has_config("has_std_format"), 
               wchar = true, 
               wchar_console = true}
})

includes("./third_party")
includes("./tests")
includes("./src/model")
includes("./src/presentation")
includes("./src/view")

task("docs")
    set_category("plugin")
    on_run(function ()
        import("lib.detect.find_tool")

        local doxygen = find_tool("doxygen")
        -- Graphviz uses -V (not --version), so give xmake the tool-specific
        -- probe instead of relying on find_tool's default version check.
        local dot = find_tool("dot", {check = "-V"})
        assert(doxygen, "doxygen was not found; install Doxygen before generating documentation")
        assert(dot, "Graphviz 'dot' was not found; install Graphviz to generate dependency and call graphs")

        local outputdir = path.join(os.projectdir(), "build", "docs", "doxygen")
        local htmldir = path.join(outputdir, "html")
        local indexfile = path.join(htmldir, "index.html")
        local warningfile = path.join(outputdir, "warnings.log")
        -- Doxygen updates existing output but does not remove pages for files
        -- that have since been renamed or excluded. Rebuild this generated
        -- subtree so navigation and dependency graphs never contain stale data.
        os.rm(htmldir)
        os.rm(warningfile)
        os.mkdir(outputdir)
        os.execv(doxygen.program, {"Doxyfile"}, {curdir = os.projectdir()})

        print("Doxygen documentation: " .. indexfile)
        if os.isfile(warningfile) and os.filesize(warningfile) > 0 then
            print("Doxygen warnings: " .. warningfile)
        end

        -- Opening the generated entry point is a convenience only. Keep the
        -- documentation task successful on headless hosts and CI machines.
        if os.isfile(indexfile) then
            local opener
            local openargs
            if is_host("windows") then
                opener = "cmd"
                openargs = {"/c", "start", "", indexfile}
            elseif is_host("macosx") then
                opener = "open"
                openargs = {indexfile}
            else
                opener = "xdg-open"
                openargs = {indexfile}
            end
            os.execv(opener, openargs, {
                try = true,
                detach = true,
                stdout = os.nuldev(),
                stderr = os.nuldev()
            })
        end
    end)
    set_menu {
        usage = "xmake docs",
        description = "Generate and open API reference and architecture graphs with Doxygen."
    }
