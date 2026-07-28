-- Keep nekoav as a source-level dependency without importing the submodule's
-- standalone tests, compile-commands rule, or Qt Widgets example target.
target("nekoav")
    set_kind("shared")
    set_license("MIT")
    add_files("$(projectdir)/third_party/nekoav/src/**.cpp")
    add_includedirs("$(projectdir)/third_party/nekoav/include",
                    {public = true})
    add_includedirs("$(projectdir)/third_party/nekoav/src")
    add_defines("_NEKOAV_SOURCE")
    add_packages("ilias", {public = true})
    add_packages("ffmpeg", "miniaudio")
target_end()
