target("rest_server")
    set_kind("binary")
    set_default(false)

    set_languages("c++20")
    
    add_deps("hku_httpd")
    add_packages("boost", "nlohmann_json", "fmt", "openssl3")

    if is_plat("linux", "cross") then
        add_cxflags("-fcoroutines")
    end    
    
    -- 添加 OpenSSL 依赖（用于 TLS/SSL）
    if is_plat("macosx") then
        add_syslinks("ssl", "crypto")
        add_frameworks("CoreFoundation", "Security")
    elseif is_plat("linux") then
        add_syslinks("ssl", "crypto")
        add_ldflags("-pthread", {force=true})
    elseif is_plat("windows") then
        add_syslinks("ws2_32", "mswsock", "wldap32", "crypt32")
        -- 如果使用 vcpkg
        -- add_requires("openssl", {configs = {shared = true}})
        -- add_packages("openssl")
    end

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
        os.cp("$(projectdir)/example/rest_server/rest_server.ini", "$(builddir)/$(mode)/$(plat)/$(arch)/lib/")
    end)
target_end()
