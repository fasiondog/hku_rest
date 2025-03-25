/*
 *  Copyright (c) 2024 hikyuu.org
 *
 *  Created on: 2024-07-23
 *      Author: fasiondog
 */

#include <locale>
#include <csignal>
#include <nng/nng.h>
#include "hikyuu/httpd/HttpServer.h"
#include "hikyuu/httpd/pod/all.h"
#include "HelloService.h"

using namespace hku;

void signal_handle(int signal) {
    HKU_INFO("Shutdown now ...");
    hku::pod::quit();
    HttpServer::stop();
    exit(0);
}

int main(int argc, char* argv[]) {
    initLogger();

    std::signal(SIGINT, signal_handle);
    std::signal(SIGTERM, signal_handle);
    std::signal(SIGABRT, signal_handle);
    std::signal(SIGSEGV, signal_handle);

    HttpServer server("https://*", 8080);
    HttpHandle::enableTrace(true, false);

    try {
        pod::init("rest_server.ini");

        // 设置 404 返回信息
        server.set_error_msg(NNG_HTTP_STATUS_NOT_FOUND,
                             fmt::format(R"({{"ret": {}, "errmsg":"Content not Found"}})",
                                         int(NNG_HTTP_STATUS_NOT_FOUND)));

        HelloService hello_service("/");
        hello_service.bind(&server);

        HKU_INFO("start server ... You can press Ctrl-C stop");
        server.start();
        server.loop();

    } catch (std::exception& e) {
        HKU_FATAL(e.what());
    } catch (...) {
        HKU_FATAL("Unknow error!");
    }

    hku::pod::quit();
    server.stop();
    return 0;
}
