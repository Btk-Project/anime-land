local autofunc = autofunc or {}

import("core.project.project")

-- private

function _Camel(str)
    return str:sub(1, 1):upper() .. str:sub(2)
end

-- public

function autofunc.target_autoclean(target)
    os.tryrm(target:targetdir() .. "/" .. target:basename() .. ".exp")
    os.tryrm(target:targetdir() .. "/" .. target:basename() .. ".ilk")
    os.tryrm(target:targetdir() .. "/compile." .. target:basename() .. ".pdb")
    if is_plat("linux") then
        if target:kind() == "static" then
            os.tryrm(target:targetdir() .. "/lib" .. target:basename() .. ".so")
        elseif target:kind() == "shared" then
            os.tryrm(target:targetdir() .. "/lib" .. target:basename() .. ".a")
        end
    end
end

function autofunc.auto_add_packages(target, options)
    options = options or {}
    local uses_ilias = options.uses_ilias or false
    local uses_expected = options.uses_expected or false
    local uses_neko_proto_tools = options.uses_neko_proto_tools or false
    
    if has_config("enable_spdlog") then
        target:add("packages", "spdlog", {public = true})
    end
    
    if not has_config("has_std_expected") then
        target:add("packages", "zeus_expected", {public = true})
    end
    
    if uses_ilias then
        target:add("packages", "ilias", {public = true})
    end

    if uses_neko_proto_tools then
        target:add("packages", "neko-proto-tools", {public = true})
    end
    
    if has_config("enable_tests") then
        target:add("packages", "gtest", {public = true})
        target:add("ldflags", "-lgmock", {public = true, force = true})
    end
end

function main()
    return autofunc
end
