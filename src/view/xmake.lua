target("view")
    add_rules("qt.static")
    add_files("./qml/**.cpp", "./qml/**.hpp")
    add_files("./playback/**.cpp", "./playback/**.hpp")
    add_files("./qml/anime_land_qml.qrc")
    add_includedirs("$(projectdir)/src")
    add_packages("ilias", "neko-proto-tools")
    add_deps("presentation")
    add_frameworks("QtCore", "QtGui", "QtNetwork", "QtQml", "QtQuick",
                   "QtQuickControls2", "QtQuickDialogs2", {public = true})
    add_frameworks("QtGuiPrivate")
    on_load(function (target)
        import("lua.auto", {rootdir = os.projectdir()})
        auto().auto_add_packages(target,
                                {uses_ilias = true,
                                 uses_expected = true,
                                 uses_neko_proto_tools = true})
    end)
target_end()

target("main")
    add_rules("qt.console")
    set_basename("anime-land")
    add_files("./gui/main.cpp", "./gui/application.cpp", "$(projectdir)/src/process.cpp")
    add_includedirs("$(projectdir)/src")
    add_packages("libsodium", "neko-proto-tools", "ilias-sql", "ilias")
    add_deps("view", "presentation", "model", "episode_provider_js")
    add_frameworks("QtCore", "QtGui", "QtNetwork", "QtQml", "QtQuick",
                   "QtQuickControls2", "QtQuickDialogs2")
    add_options("enable_spdlog")
    on_load(function (target)
        import("lua.auto", {rootdir = os.projectdir()})
        auto().auto_add_packages(target,
                                {uses_ilias = true,
                                 uses_expected = true,
                                 uses_neko_proto_tools = true})
    end)
target_end()

target("cli")
    add_rules("qt.static")
    add_files("./cli/**.cpp", "./cli/**.hpp")
    remove_files("./cli/main.cpp")
    add_includedirs("$(projectdir)/src")
    add_packages("ilias", "neko-proto-tools")
    add_deps("presentation")
    add_frameworks("QtCore", "QtGui", "QtNetwork", {public = true})
    add_options("enable_spdlog")
    on_load(function (target)
        import("lua.auto", {rootdir = os.projectdir()})
        auto().auto_add_packages(target,
                                {uses_ilias = true,
                                 uses_expected = true,
                                 uses_neko_proto_tools = true})
    end)
target_end()

target("anime_land_cli")
    add_rules("qt.console")
    set_basename("anime-land-cli")
    add_files("./cli/main.cpp", "$(projectdir)/src/process.cpp")
    add_includedirs("$(projectdir)/src")
    add_deps("cli")
    add_options("enable_spdlog")
target_end()
