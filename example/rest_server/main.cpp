/*
 *  Copyright (c) 2024 hikyuu.org
 *
 *  Created on: 2024-07-23
 *      Author: fasiondog
 */

#include <locale>
#include <csignal>
#include "hikyuu/httpd/HttpServer.h"
#include "HelloService.h"

using namespace hku;

#define HKU_SERVICE_API(name) "/hku/" #name "/v1"

void signal_handle(int signal) {
    if (signal == SIGINT || signal == SIGTERM) {
        HKU_INFO("Shutdown now ...");
        HttpServer::stop();
        exit(0);
    }
}

int main(int argc, char* argv[]) {
    initLogger();

    // 初始化多语言支持
    MOHelper::init();

    std::signal(SIGINT, signal_handle);
    std::signal(SIGTERM, signal_handle);

    HttpServer server("http://*", 8080);
    HttpHandle::enableTrace(true, false);

    try {
        // 设置 404 返回信息
        server.set_error_msg(NNG_HTTP_STATUS_NOT_FOUND,
                             fmt::format(R"({{"ret": false,"errcode":{}, "errmsg":"Not Found"}})",
                                         int(NNG_HTTP_STATUS_NOT_FOUND)));

        HelloService hello_service(HKU_SERVICE_API(hello));
        hello_service.bind(&server);

        HKU_INFO("start server ... You can press Ctrl-C stop");
        server.start();

    } catch (std::exception& e) {
        HKU_FATAL(e.what());
        server.stop();
    } catch (...) {
        HKU_FATAL("Unknow error!");
        server.stop();
    }

    return 0;
}
