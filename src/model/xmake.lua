target("model")
    add_rules("qt.static")
    set_pcxxheader("$(projectdir)/src/pch.hpp")
    add_deps("nekoav")
    add_files("$(projectdir)/src/common/**.cpp")
    add_files("$(projectdir)/src/model/**.cpp", "$(projectdir)/src/model/**.hpp")
    add_includedirs("$(projectdir)/src", {public = true})
    add_packages("ilias", "ilias-sql", "libsodium", "neko-proto-tools", {public = true})
    add_frameworks("QtCore", "QtGui", "QtNetwork", {public = true})
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
                                 uses_neko_proto_tools = true})
    end)
target_end()
