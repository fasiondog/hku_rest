target("mqtt_broker")
    set_kind("binary")
    set_default(false)

    add_deps("hku_httpd")
    add_packages("async_mqtt")
    if has_config("use_hikyuu") then
        add_packages("hikyuu")
    else
        add_packages("hku_utils")
    end    

    add_includedirs("$(projectdir)")

    add_files("main.cpp")
   
    if is_plat("macosx") then
        add_links("iconv")
    end
    
target_end()

