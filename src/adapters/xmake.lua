target("episode_provider_js")
    add_rules("qt.static")
    add_files("./episode_provider_js/**.cpp", "./episode_provider_js/**.hpp")
    add_files("$(projectdir)/plugins/episode-providers/episode_provider_plugins.qrc")
    add_includedirs("$(projectdir)/src", {public = true})
    add_deps("model")
    add_packages("ilias", "libxml2", {public = true})
    add_frameworks("QtCore", "QtNetwork", "QtQml", {public = true})
    on_load(function (target)
        import("lua.auto", {rootdir = os.projectdir()})
        auto().auto_add_packages(target,
                                {uses_ilias = true,
                                 uses_expected = true,
                                 uses_neko_proto_tools = true})
    end)
target_end()
