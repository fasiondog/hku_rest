set_xmakever("3.0.0")
set_project("hku_rest")

set_version("1.0.7", {build="%Y%m%d%H%M"})

set_warnings("all")
set_languages("cxx17", "c99")

option("mysql", {description = "Enable sqlite driver.", default = true})
option("sqlite", {description = "Enable sqlite.", default = false})
option("stacktrace", {description = "Enable check/assert with stack trace info.", default = false})
option("log_level", {description = "set log level.", default = 2, values = {1, 2, 3, 4, 5, 6}})
option("async_log", {description = "Use async log.", default = false})
option("leak_check", {description = "Enable leak check for test", default = false})

option("use_hikyuu", {description = "Use hikyuu as hku_utils.", default = false})

add_rules("mode.debug", "mode.release")

add_repositories("hikyuu-repo https://gitee.com/fasiondog/hikyuu_extern_libs.git")

local log_level = get_config("log_level")
if get_config("use_hikyuu") then
    add_requires("hikyuu", 
    {configs = {
        shared = is_kind("shared"), 
        log_level = log_level,
        mo = true,
        http_client = true,
        http_client_ssl = true,
        http_client_zip = true,
        async_log = has_config("async_log"),
        mysql = has_config("mysql"), 
        sqlite = has_config("sqlite"),
        stacktrace = has_config("stacktrace")
    }})
else
    add_requires("hku_utils", 
        {configs = {
            shared = is_kind("shared"), 
            log_level = log_level,
            mo = true,
            http_client = true,
            http_client_ssl = true,
            http_client_zip = true,
            async_log = has_config("async_log"),
            mysql = has_config("mysql"), 
            sqlite = has_config("sqlite"),
            stacktrace = has_config("stacktrace")
    }})
end

set_objectdir("$(builddir)/$(mode)/$(plat)/$(arch)/.objs")
set_targetdir("$(builddir)/$(mode)/$(plat)/$(arch)/lib")

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

if get_config("use_hikyuu") then
    add_packages("hikyuu")
else
    add_packages("hku_utils") 
end

if get_config("leak_check") then
    -- 需要 export LD_PRELOAD=libasan.so
    set_policy("build.sanitizer.address", true)
    set_policy("build.sanitizer.leak", true)
    -- set_policy("build.sanitizer.memory", true)
    -- set_policy("build.sanitizer.thread", true)
end

target("hku_httpd")
    set_kind("$(kind)")

    set_configdir("$(projectdir)/hikyuu/httpd")
    add_configfiles("$(projectdir)/version.h.in")
    add_configfiles("$(projectdir)/config.h.in")    

    set_configvar("HKU_HTTPD_POD_USE_SQLITE", has_config("sqlite") and 1 or 0)
    set_configvar("HKU_HTTPD_POD_USE_MYSQL", has_config("mysql") and 1 or 0)
    
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
        add_links("iconv")
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

        local libdir = get_config("builddir") .. "/" .. get_config("mode") .. "/" .. get_config("plat") .. "/" ..
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