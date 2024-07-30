/*
 *  Copyright (c) 2024 hikyuu.org
 *
 *  Created on: 2024-07-23
 *      Author: fasiondog
 */

#include <locale>
#include <csignal>
#include "hikyuu/utilities/node/NodeServer.h"
#include "hikyuu/utilities/node/NodeClient.h"
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

static std::string server_addr = "inproc://tmp";

int main(int argc, char* argv[]) {
    initLogger();

    std::signal(SIGINT, signal_handle);
    std::signal(SIGTERM, signal_handle);

    try {
        pod::init("node_server.ini");

        server.setAddr(server_addr);

        server.regHandle("2", [](json&& req) {
            json res;
            HKU_INFO("Hello world!");
            return res;
        });

        HKU_INFO("start node server ... You can press Ctrl-C stop");
        server.start();

        auto t = std::thread([]() {
            NodeClient cli(server_addr);
            cli.dial();

            json req, res;
            req["cmd"] = 2;
            cli.post(req, res);
        });
        t.join();

        // server.loop();

    } catch (std::exception& e) {
        HKU_FATAL(e.what());
    } catch (...) {
        HKU_FATAL("Unknow error!");
    }

    hku::pod::quit();
    server.stop();
    return 0;
}
