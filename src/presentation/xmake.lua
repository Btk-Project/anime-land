target("presentation")
    add_rules("qt.static")
    add_files("./**.cpp", "./**.hpp")
    add_includedirs("$(projectdir)/src")
    add_packages("ilias", "neko-proto-tools")
    add_deps("model")
    add_frameworks("QtCore", "QtGui", "QtNetwork")
    on_load(function (target)
        import("lua.auto", {rootdir = os.projectdir()})
        auto().auto_add_packages(target,
                                {uses_ilias = true,
                                 uses_expected = true,
                                 uses_neko_proto_tools = true})
    end)
target_end()
