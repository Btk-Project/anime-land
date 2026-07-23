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
add_requires("ilias", "libsodium", "neko-proto-tools", "ilias-sql")
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

local sqlite_fts5_flag = is_plat("windows")
    and "/DSQLITE_ENABLE_FTS5"
    or "-DSQLITE_ENABLE_FTS5"

add_requireconfs("**ilias-sql", {
    version = "dev", -- 使用最新版本
    override = true, -- 强制覆盖
    configs = {shared = true,
               stdcxx = tonumber(get_config("stdcxx")),
               enable_sqlite = "sqlcipher",
               enable_mysql = true,
               enable_orm_interface = true,
               -- Keep the package hash tied to the required SQLite feature so
               -- ilias-sql is relinked when its static sqlite dependency changes.
               cxflags = sqlite_fts5_flag,
               }
})

-- subject_fts is part of the v0.1 SQLite schema. Compile FTS5 into the
-- SQLCipher amalgamation used for both encrypted and unencrypted SQLite files.
add_requireconfs("**sqlcipher", {
    configs = {cflags = sqlite_fts5_flag,
               cxflags = sqlite_fts5_flag}
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

includes("./tests")
includes("./src/bangumi")
includes("./src/common")
includes("./src/persistence")
includes("./src/presentation")

-- MARK: targets
target("main")
    add_rules("qt.console")
    add_includedirs("./src")
    add_packages("libsodium", "neko-proto-tools", "ilias-sql", "ilias")
    add_deps("presentation", "bangumi", "common", "persistence")
    add_frameworks("QtCore", "QtGui", "QtNetwork")
    add_files("src/*.cpp")
    add_options("enable_spdlog")
    on_load(function (target)
        import("lua.auto", {rootdir = os.projectdir()})
        auto().auto_add_packages(target, 
                                {uses_ilias = true, 
                                uses_expected = true, 
                                uses_neko_proto_tools = true})
    end)
target_end()
