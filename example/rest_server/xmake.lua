target("rest_server")
    set_kind("binary")
    set_default(false)

    add_packages("hku_utils", "nlohmann_json", "nng")
    add_deps("hku_httpd")

    add_includedirs("$(projectdir)")
    add_files("**.cpp")

    before_build(function(target)
        if is_plat("windows") then
            local pkg = target:dep("hku_httpd")
            if pkg:kind() == "shared" then
                target:add("defines", "HKU_HTTP_API=__declspec(dllimport)")
            end
        end
    end)
target_end()