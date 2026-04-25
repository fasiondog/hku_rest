set_xmakever("3.0.0")
set_project("hku_rest")

set_version("1.2.1", {build="%Y%m%d%H%M"})

set_warnings("all")
set_languages("c++20")

option("mysql", {description = "Enable sqlite driver.", default = true})
option("sqlite", {description = "Enable sqlite.", default = false})
option("stacktrace", {description = "Enable check/assert with stack trace info.", default = false})
option("log_level", {description = "set log level.", default = 2, values = {1, 2, 3, 4, 5, 6}})
option("async_log", {description = "Use async log.", default = false})
option("leak_check", {description = "Enable leak check for test", default = false})
option("mqtt", {description = "Enable mqtt broker support.", default = false})
option("mcp", {description = "Enable mcp server support.", default = false})

option("use_hikyuu", {description = "Use hikyuu as hku_utils.", default = false})

add_rules("mode.debug", "mode.release")

add_repositories("hikyuu-repo https://github.com/fasiondog/hikyuu_extern_libs.git")

local log_level = get_config("log_level")
if has_config("use_hikyuu") then
    add_requires("hikyuu", 
    {configs = {
        shared = is_kind("shared"), 
        log_level = log_level,
        http_client = true,
        http_client_ssl = true,
        http_client_zip = true,
        async_log = has_config("async_log"),
        mysql = has_config("mysql"), 
        sqlite = has_config("sqlite"),
        stacktrace = has_config("stacktrace")
    }})
else
    add_requires("hku_utils >=1.3.6", 
        {configs = {
            shared = is_kind("shared"), 
            log_level = log_level,
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
    add_defines("WIN32_LEAN_AND_MEAN", "_WIN32_WINNT=0x0601")
    add_cxflags("/bigobj")
    if is_mode("debug") then
        add_cxflags("-Gs", "-RTC1")
    end
end

local boost_config
if is_plat("windows") then
    boost_config = {
        system = false,
        debug = is_mode("debug"),
        configs = {
            shared = true,
            runtimes = get_config("runtimes"),
            multi = true,
            date_time = true,
            filesystem = false,
            property_tree= true, --mqtt need
            serialization = true,
            system = true,
            python = false,
            asio = true,
            beast = true,
            cmake = false,
    }}
else
    boost_config = {
        system = false,
        configs = {
            shared = true, -- is_plat("windows"),
            runtimes = get_config("runtimes"),
            multi = true,
            date_time = true,
            filesystem = false,
            serialization = true, --get_config("serialize"),
            system = true,
            python = false,
            -- 以下为兼容 arrow 等其他组件
            thread = true,   -- parquet need
            chrono = true,   -- parquet need
            charconv = true, -- parquet need
            atomic = true,
            container = true,
            math = true,
            regex = true,
            random = true,
            thread = true,
            asio = true,  
            beast = true,
            cmake = true,
    }}
end
if has_config("use_hikyuu") then
    add_requireconfs("hikyuu.boost", {override=true, system = false, configs = boost_config.configs})
else
    add_requireconfs("hku_utils.boost", {override=true, system = false, configs = boost_config.configs})
end

if has_config("leak_check") then
    -- 需要 export LD_PRELOAD=libasan.so
    set_policy("build.sanitizer.address", true)
    set_policy("build.sanitizer.leak", true)
    -- set_policy("build.sanitizer.memory", true)
    -- set_policy("build.sanitizer.thread", true)
end

add_requires("gzip-hpp", {system = false})

if is_plat("linux") then
  add_requires("apt::libssl-dev", {alias="openssl3"})
else
  add_requires("openssl3", {system = false})
end

if has_config("mqtt") then
    add_requires("async_mqtt", {system = false, configs = {tls = true, ws = true, log=false}})
    add_requireconfs("async_mqtt.boost", {override=true, system = false, configs = boost_config.configs})
end

target("hku_httpd")
    set_kind("$(kind)")

    set_configdir("$(projectdir)/hikyuu/httpd")
    add_configfiles("$(projectdir)/version.h.in")
    add_configfiles("$(projectdir)/config.h.in")    

    set_configvar("HKU_HTTPD_POD_USE_SQLITE", has_config("sqlite") and 1 or 0)
    set_configvar("HKU_HTTPD_POD_USE_MYSQL", has_config("mysql") and 1 or 0)

    if has_config("use_hikyuu") then
        add_packages("hikyuu")
    else
        add_packages("hku_utils")
    end

    add_packages("openssl3", "gzip-hpp")

    if has_config("mqtt") then
        add_packages("async_mqtt")
    end

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

    add_defines("BOOST_ASIO_HAS_CO_AWAIT=1", "BOOST_ASIO_HAS_CXX20_COROUTINES=1", "DBOOST_ASIO_DISABLE_DEPRECATED=1")    

    if is_plat("linux", "cross") then
        add_cxflags("-fcoroutines")
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

    if has_config("mqtt") then
        add_headerfiles("$(projectdir)/(hikyuu/mqtt/**.h)")
        add_files("hikyuu/mqtt/*.cpp")
    end

    if has_config("mcp") then 
        add_headerfiles("$(projectdir)/(hikyuu/mcp/**.h)")
        add_files("hikyuu/mcp/*.cpp")
    end

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
includes("example/websocket_server")
includes("example/mqtt_broker")
includes("example/sse_server")
includes("example/mcp_server")
