target("common")
    add_rules("qt.static")
    add_files("./**.cpp")
    add_includedirs("../../src")
    add_packages("neko-proto-tools", "ilias", "libsodium", {public = true})
    add_frameworks("QtCore", {public = true})
    on_load(function (target)
        import("lua.auto", {rootdir = os.projectdir()})
        auto().auto_add_packages(target, 
                                {uses_ilias = true, 
                                uses_expected = true, 
                                uses_neko_proto_tools = true})
    end)
