/*
 *  Copyright (c) 2024 hikyuu.org
 *
 *  Created on: 2024-07-23
 *      Author: fasiondog
 */

#include <locale>
#include <csignal>
#include "hikyuu/httpd/noded/NodeServer.h"
#include "hikyuu/httpd/pod/all.h"

using namespace hku;

NodeServer server;

void signal_handle(int signal) {
    if (signal == SIGINT || signal == SIGTERM) {
        HKU_INFO("Shutdown now ...");
        hku::pod::quit();
        server.stop();
        exit(0);
    }
}

int main(int argc, char* argv[]) {
    initLogger();

    std::signal(SIGINT, signal_handle);
    std::signal(SIGTERM, signal_handle);

    try {
        pod::init("node_server.ini");

        server.setAddr("tcp://0.0.0.0:9080");

        server.regHandle(2, [](json&& req) {
            json res;
            HKU_INFO("Hello world!");
            return res;
        });

        HKU_INFO("start node server ... You can press Ctrl-C stop");
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
