target("bangumi")
    add_rules("qt.static")
    add_files("./**.cpp", "./**.hpp")
    add_includedirs("../../src")
    add_packages("ilias")
    add_packages("libsodium", {public = true})
    add_deps("common")
    add_frameworks("QtCore", "QtGui", "QtNetwork")
    if is_plat("linux") then
        add_defines("ANIME_LAND_HAS_SECRET_SERVICE")
        add_frameworks("QtDBus")
    elseif is_plat("macosx") then
        add_frameworks("Security")
    elseif is_plat("windows") then
        add_links("Advapi32")
    end
    on_load(function (target)
        import("lua.auto", {rootdir = os.projectdir()})
        auto().auto_add_packages(target, 
                                {uses_ilias = true, 
                                uses_expected = true, 
                                uses_neko_proto_tools = false})
    end)
target_end()
