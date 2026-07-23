target("persistence")
    add_rules("qt.static")
    add_files("./**.cpp")
    add_includedirs("../../src", {public = true})
    add_packages("ilias-sql", "ilias", {public = true})
    add_deps("common")
    add_frameworks("QtCore", {public = true})
    on_load(function (target)
        import("lua.auto", {rootdir = os.projectdir()})
        auto().auto_add_packages(target,
                                {uses_ilias = true,
                                 uses_expected = true,
                                 uses_neko_proto_tools = true})
    end)
target_end()
