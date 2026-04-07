target("mqtt_broker")
    set_kind("binary")
    add_deps("hku_httpd")
    add_files("main.cpp")
    
    -- 添加头文件搜索路径
    add_includedirs("../../")
    
    -- 添加 async_mqtt 包（从 hku_httpd 继承）
    add_packages("async_mqtt")
    
    -- 启用 WebSocket 支持（可选）
    -- add_defines("ASYNC_MQTT_USE_WS")
    
    -- 启用 TLS 支持（可选）
    -- add_defines("ASYNC_MQTT_USE_TLS")
    
    if is_plat("macosx") then
        add_links("iconv")
    end
    
target_end()

-- 测试任务
task("test_mqtt_broker")
    set_category("example")
    on_run(function(target)
        import("core.base.task")
        import("os")
        
        print("\n==========================================")
        print("MQTT Broker Test")
        print("==========================================\n")
        
        -- 检查 Python 依赖
        print("[1/3] Checking dependencies...")
        local result = os.execv("python3", {"-c", "import paho.mqtt.client"})
        if result ~= 0 then
            print("Installing paho-mqtt...")
            os.exec("pip install paho-mqtt")
        end
        print("✓ Dependencies OK\n")
        
        -- 构建 broker
        print("[2/3] Building mqtt_broker...")
        task.run("build", {target = "mqtt_broker"})
        print("✓ Build successful\n")
        
        -- 查找可执行文件
        local build_dir = get_config("builddir")
        local mode = get_config("mode") or "release"
        local plat = get_config("plat")
        local arch = get_config("arch")
        
        local broker_bin = string.format("%s/%s/%s/%s/mqtt_broker", build_dir, mode, plat, arch)
        
        if not os.isfile(broker_bin) then
            print("✗ Broker binary not found: " .. broker_bin)
            return
        end
        
        -- 启动 Broker
        print("[3/3] Starting MQTT Broker for testing...")
        print("Broker binary: " .. broker_bin .. "\n")
        
        local broker_proc = os.runv(broker_bin, {}, {timeout = 30000})
        
        print("\nTest completed")
    end)
    
    set_menu {
        usage = "xmake test_mqtt_broker",
        description = "Run MQTT Broker test"
    }
task_end()
