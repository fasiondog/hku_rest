/*
 *  Copyright (c) 2026 hikyuu.org
 *
 *  Created on: 2026-04-07
 *      Author: fasiondog
 */

#pragma once

#include <memory>
#include <atomic>

#include <hikyuu/utilities/Parameter.h>
#include <hikyuu/utilities/Log.h>

namespace hku {

/**
 * @brief MQTT Broker 服务器
 *
 * 基于 async_mqtt 实现的 MQTT 代理服务器，支持：
 * - MQTT over TCP
 * - MQTT over WebSocket (可选)
 * - MQTT over TLS (可选)
 * - MQTT over WebSocket Secure (可选)
 * - 多 io_context 线程池
 * - 认证授权管理
 *
 * @note 所有 async_mqtt 相关的头文件依赖都隐藏在实现文件中，
 *       使用者无需引入 async_mqtt 依赖
 */
class MqttBroker {
    CLASS_LOGGER_IMP(MqttBroker)
    PARAMETER_SUPPORT

public:
    /**
     * @brief 构造函数
     * @param param 配置参数，支持的配置项：
     *   - mqtt.port: MQTT TCP 端口（默认 1883）
     *   - ws.port: WebSocket 端口（可选，默认不启用）
     *   - tls.port: TLS 端口（可选，默认不启用）
     *   - wss.port: WebSocket Secure 端口（可选，默认不启用）
     *   - iocs: io_context 数量（默认 0 = CPU 核心数）
     *   - threads_per_ioc: 每个 io_context 的线程数（默认 1）
     *   - read_buf_size: 读取缓冲区大小（默认 65536）
     *   - bulk_write: 是否启用批量写入（默认 true）
     *   - tcp_no_delay: 是否禁用 Nagle 算法（默认 true）
     *   - recv_buf_size: 接收缓冲区大小（可选）
     *   - send_buf_size: 发送缓冲区大小（可选）
     *   - auth_file: 认证文件路径（可选）
     *
     *   TLS 相关配置（当启用 TLS 时必需）：
     *   - certificate: 证书文件路径
     *   - private_key: 私钥文件路径
     *   - verify_file: CA 证书文件（可选）
     *   - verify_field: 证书验证字段（默认 "CN"）
     */
    explicit MqttBroker(const Parameter& param);

    ~MqttBroker();

    // 禁止拷贝和移动
    MqttBroker(const MqttBroker&) = delete;
    MqttBroker& operator=(const MqttBroker&) = delete;
    MqttBroker(MqttBroker&&) = delete;
    MqttBroker& operator=(MqttBroker&&) = delete;

    /**
     * @brief 启动 MQTT Broker
     * @note 此方法会阻塞直到 stop() 被调用
     */
    void start();

    /**
     * @brief 停止 MQTT Broker
     */
    void stop();

    /**
     * @brief 检查 Broker 是否正在运行
     * @return true 如果正在运行
     */
    bool isRunning() const;

private:
    // 前向声明内部实现类
    class Impl;
    std::unique_ptr<Impl> m_impl;
};

}  // namespace hku
