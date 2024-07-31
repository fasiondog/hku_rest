target("node_server")
    set_kind("binary")
    set_default(false)

    add_deps("hku_httpd")

    add_includedirs("$(projectdir)")
    add_files("**.cpp")

    before_build(function(target)
        if is_plat("windows") then
            local pkg = target:dep("hku_httpd")
            if pkg:kind() == "shared" then
                target:add("defines", "HKU_HTTPD_API=__declspec(dllimport)")
            end
        end
    end)

    before_run(function(target)
        os.cp("$(projectdir)/example/node_server/node_server.ini", "$(buildir)/$(mode)/$(plat)/$(arch)/lib/")
    end)
target_end()