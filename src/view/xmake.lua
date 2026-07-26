target("view")
    add_rules("qt.static")
    add_files("./**.cpp", "./**.hpp")
    add_files("./qml/anime_land_qml.qrc")
    add_includedirs("$(projectdir)/src")
    add_packages("ilias", "neko-proto-tools")
    add_deps("presentation")
    add_frameworks("QtCore", "QtGui", "QtNetwork", "QtQml", "QtQuick",
                   "QtQuickControls2", "QtQuickDialogs2", {public = true})
    on_load(function (target)
        import("lua.auto", {rootdir = os.projectdir()})
        auto().auto_add_packages(target,
                                {uses_ilias = true,
                                 uses_expected = true,
                                 uses_neko_proto_tools = true})
    end)
target_end()
