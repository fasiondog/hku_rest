set_project("hku_rest")
set_version("1.0.0", {build="%Y%m%d%H%M"})

set_warnings("all")
set_languages("cxx17", "c99")

option("mysql")
    set_default(true)
    set_showmenu(true)
    set_description("Enable mysql kdata engine.")
    if is_plat("macosx") then
        if os.exists("/usr/local/opt/mysql-client/lib") then
            add_includedirs("/usr/local/opt/mysql-client/include/mysql")
            add_includedirs("/usr/local/opt/mysql-client/include")
            add_linkdirs("/usr/local/opt/mysql-client/lib")
            add_rpathdirs("/usr/local/opt/mysql-client/lib")
        end
        if os.exists("/usr/local/mysql/lib") then
            add_linkdirs("/usr/local/mysql/lib")
            add_rpathdirs("/usr/local/mysql/lib")
        end
        if not os.exists("/usr/local/include/mysql") then
            if os.exists("/usr/local/mysql/include") then
                os.run("ln -s /usr/local/mysql/include /usr/local/include/mysql")
            else
                print("Not Found MySQL include dir!")
            end
        end
        add_links("mysqlclient")
    elseif is_plat("windows") then
        add_defines("NOMINMAX")
    end        
option_end()

option("sqlite", {description = "Enable sqlite.", default = false})
option("stacktrace", {description = "Enable check/assert with stack trace info.", default = false})

add_rules("mode.debug", "mode.release")

add_repositories("hikyuu-repo https://github.com/fasiondog/hikyuu_extern_libs.git")
add_requires("hku_utils", 
    {configs = {
        shared = is_kind("shared"), 
        mo = true,
        http_client = true,
        http_client_zip = true,
        mysql = has_config("mysql"), 
        sqlite = has_config("sqlite"),
        stacktrace = has_config("stacktrace")
}})

set_objectdir("$(buildir)/$(mode)/$(plat)/$(arch)/.objs")
set_targetdir("$(buildir)/$(mode)/$(plat)/$(arch)/lib")

-- is release now
if is_mode("release") then
    if is_plat("windows") then
        -- Unix-like systems hidden symbols will cause the link dynamic libraries to failed!
        set_symbols("hidden")
    end
end

-- for the windows platform (msvc)
if is_plat("windows") then
    -- add some defines only for windows
    add_defines("NOCRYPT", "NOGDI")
    add_cxflags("-EHsc", "/Zc:__cplusplus", "/utf-8")
    add_cxflags("-wd4819") -- template dll export warning
    add_defines("WIN32_LEAN_AND_MEAN")
    if is_mode("debug") then
        add_cxflags("-Gs", "-RTC1", "/bigobj")
    end
end

add_packages("hku_utils") 

target("hku_httpd")
    set_kind("$(kind)")

    set_configdir("$(projectdir)/hikyuu/httpd")
    add_configfiles("$(projectdir)/version.h.in")
    add_configfiles("$(projectdir)/config.h.in")    
    
    add_includedirs(".")

    if is_kind("shared") then 
        if is_plat("windows") then
            add_defines("HKU_HTTPD_API=__declspec(dllexport)")
        else
            add_defines("HKU_HTTPD_API=__attribute__((visibility(\"default\")))")
            add_cxflags("-fPIC", {force=true})
        end
    elseif is_kind("static") and not is_plat("windows") then
        add_cxflags("-fPIC", {force=true})
    end    

    if is_plat("windows") then
        add_cxflags("-wd4819")  
        add_cxflags("-wd4251")  --template dll export warning
        add_cxflags("-wd4267")
        add_cxflags("-wd4834")  --C++17 discarding return value of function with 'nodiscard' attribute
        add_cxflags("-wd4996")
        add_cxflags("-wd4244")  --discable double to int
        add_cxflags("-wd4566")
    else
        add_rpathdirs("$ORIGIN")
        add_cxflags("-Wno-sign-compare", "-Wno-missing-braces")
    end
    
    if is_plat("macosx") then
        --add_linkdirs("/usr/local/opt/libiconv/lib")
        add_links("iconv")
        if has_config("mysql") then
            add_includedirs("/usr/local/opt/mysql-client/include")
            add_linkdirs("/usr/local/opt/mysql-client/lib")
        end
    end

    if is_plat("windows") then 
        -- nng 静态链接需要的系统库
        add_syslinks("ws2_32", "advapi32")
    end

    if is_plat("linux") or is_plat("macosx") then
        if has_config("sqlite") then
            add_links("sqlite3")
        end
        if has_config("mysql") then
            add_links("mysqlclient")
        end
    end

    add_headerfiles("$(projectdir)/(hikyuu/httpd/**.h)")
    
    -- add files
    add_files("hikyuu/httpd/*.cpp")
    add_files("hikyuu/httpd/pod/*.cpp")

    if has_config("sqlite") then
        add_files("hikyuu/httpd/pod/sqlite/*.cpp")
    end

    if has_config("mysql") then
        add_files("hikyuu/httpd/pod/mysql/*.cpp")
    end

    after_build(function(target)
        -- os.cp("$(projectdir)/i8n/", target:targetdir())

        -- 不同平台的库后缀名
        local lib_suffix = ".so"
        if is_plat("windows") then
            lib_suffix = ".dll"
        elseif is_plat("macosx") then
            lib_suffix = ".dylib"
        end

        local libdir = get_config("buildir") .. "/" .. get_config("mode") .. "/" .. get_config("plat") .. "/" ..
                        get_config("arch") .. "/lib"
        -- 将依赖的库拷贝至build的输出目录
        for libname, pkg in pairs(target:pkgs()) do
            local pkg_path = pkg:installdir()
            if pkg_path ~= nil then
                print("copy dependents: " .. pkg_path)
                os.trycp(pkg_path .. "/bin/*" .. lib_suffix, libdir)
                os.trycp(pkg_path .. "/lib/*" .. lib_suffix, libdir)
                os.trycp(pkg_path .. "/lib/*.so.*", libdir)
            end
        end
    end)
    
target_end()


includes("example/rest_server")
includes("example/node_server")