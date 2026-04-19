/*
 *  Copyright (c) 2024 hikyuu.org
 *
 *  Created on: 2024-07-23
 *      Author: fasiondog
 */

#pragma once

#include <string>
#include <unordered_map>
#include <shared_mutex>
#include <chrono>
#include <mutex>
#include <nlohmann/json.hpp>
#include "hikyuu/utilities/Log.h"

namespace hku {

/**
 * Session 数据结构
 * 存储会话相关信息
 */
struct SessionData {
    std::string session_id;           // 会话 ID（由客户端提供）
    std::string client_info;          // 客户端信息
    std::chrono::system_clock::time_point created_at;  // 创建时间
    std::chrono::system_clock::time_point last_active; // 最后活动时间
    nlohmann::json metadata;          // 元数据（可存储用户自定义信息）
    
    SessionData() : created_at(std::chrono::system_clock::now()), 
                    last_active(std::chrono::system_clock::now()) {}
    
    SessionData(const std::string& sid, const std::string& info) 
        : session_id(sid), client_info(info),
          created_at(std::chrono::system_clock::now()),
          last_active(std::chrono::system_clock::now()) {}
    
    /**
     * 检查会话是否过期
     * @param timeout_seconds 超时时间（秒）
     */
    bool isExpired(int timeout_seconds = 3600) const {
        auto now = std::chrono::system_clock::now();
        auto age = std::chrono::duration_cast<std::chrono::seconds>(
            now - last_active).count();
        return age > timeout_seconds;
    }
    
    /**
     * 更新最后活动时间
     */
    void touch() {
        last_active = std::chrono::system_clock::now();
    }
};

/**
 * Session 管理器
 * 线程安全的会话管理系统
 * 
 * 特性：
 * - 线程安全（读写锁）
 * - 自动过期清理
 * - 支持自定义元数据存储
 * - 会话统计信息
 * 
 * 注意：Session ID 由客户端生成和管理，服务端仅负责验证和存储
 */
class SessionManager {
    CLASS_LOGGER_IMP(SessionManager)

public:
    explicit SessionManager(int timeout_seconds = 3600, int max_sessions = 10000)
        : m_timeout_seconds(timeout_seconds), m_max_sessions(max_sessions) {
        HKU_INFO("SessionManager initialized: timeout={}s, max_sessions={}", 
                 timeout_seconds, max_sessions);
    }
    
    ~SessionManager() {
        clearAllSessions();
    }
    
    /**
     * 注册或获取会话
     * 如果会话不存在则创建，存在则更新时间
     * @param session_id 会话 ID（由客户端提供）
     * @param client_info 客户端信息（如 IP、User-Agent）
     * @return 是否成功
     */
    bool registerSession(const std::string& session_id, const std::string& client_info = "") {
        if (session_id.empty()) {
            HKU_WARN("Empty session_id rejected");
            return false;
        }
        
        std::unique_lock<std::shared_mutex> lock(m_mutex);
        
        auto it = m_sessions.find(session_id);
        if (it != m_sessions.end()) {
            // 会话已存在，更新时间
            it->second.touch();
            HKU_DEBUG("Session refreshed: {}", session_id);
            return true;
        }
        
        // 检查是否达到最大会话数
        if (m_sessions.size() >= m_max_sessions) {
            // 清理过期会话
            cleanupExpiredSessions();
            
            // 如果仍然超出限制，拒绝创建
            if (m_sessions.size() >= m_max_sessions) {
                HKU_WARN("Session limit reached ({}), rejecting new session", m_max_sessions);
                return false;
            }
        }
        
        SessionData session(session_id, client_info);
        m_sessions[session_id] = session;
        
        HKU_DEBUG("Session registered: {} (total: {})", session_id, m_sessions.size());
        return true;
    }
    
    /**
     * 获取会话数据
     * @param session_id 会话 ID
     * @return 会话数据指针，如果不存在或已过期则返回 nullptr
     */
    std::shared_ptr<SessionData> getSession(const std::string& session_id) {
        if (session_id.empty()) {
            return nullptr;
        }

        std::shared_lock<std::shared_mutex> lock(m_mutex);
        
        auto it = m_sessions.find(session_id);
        if (it == m_sessions.end()) {
            return nullptr;
        }
        
        // 检查是否过期
        if (it->second.isExpired(m_timeout_seconds)) {
            HKU_DEBUG("Session expired: {}", session_id);
            return nullptr;
        }
        
        return std::make_shared<SessionData>(it->second);
    }
    
    /**
     * 验证会话是否存在且有效
     * @param session_id 会话 ID
     * @return 是否有效
     */
    bool validateSession(const std::string& session_id) {
        return getSession(session_id) != nullptr;
    }
    
    /**
     * 更新会话活动时间
     * @param session_id 会话 ID
     * @return 是否成功
     */
    bool touchSession(const std::string& session_id) {
        if (session_id.empty()) {
            return false;
        }

        std::unique_lock<std::shared_mutex> lock(m_mutex);
        
        auto it = m_sessions.find(session_id);
        if (it == m_sessions.end()) {
            return false;
        }
        
        it->second.touch();
        return true;
    }
    
    /**
     * 注销会话
     * @param session_id 会话 ID
     * @return 是否成功
     */
    bool unregisterSession(const std::string& session_id) {
        std::unique_lock<std::shared_mutex> lock(m_mutex);
        
        auto it = m_sessions.find(session_id);
        if (it == m_sessions.end()) {
            return false;
        }
        
        m_sessions.erase(it);
        HKU_DEBUG("Session unregistered: {} (remaining: {})", session_id, m_sessions.size());
        return true;
    }
    
    /**
     * 设置会话元数据
     * @param session_id 会话 ID
     * @param key 键
     * @param value 值
     * @return 是否成功
     */
    bool setSessionMetadata(const std::string& session_id, 
                           const std::string& key, 
                           const nlohmann::json& value) {
        if (session_id.empty()) {
            return false;
        }

        std::unique_lock<std::shared_mutex> lock(m_mutex);
        
        auto it = m_sessions.find(session_id);
        if (it == m_sessions.end()) {
            return false;
        }
        
        it->second.metadata[key] = value;
        it->second.touch();
        return true;
    }
    
    /**
     * 获取会话元数据
     * @param session_id 会话 ID
     * @param key 键
     * @return 元数据值，如果不存在则返回 null
     */
    nlohmann::json getSessionMetadata(const std::string& session_id, 
                                     const std::string& key) {
        if (session_id.empty()) {
            return nullptr;
        }

        std::shared_lock<std::shared_mutex> lock(m_mutex);
        
        auto it = m_sessions.find(session_id);
        if (it == m_sessions.end()) {
            return nullptr;
        }
        
        if (!it->second.metadata.contains(key)) {
            return nullptr;
        }
        
        return it->second.metadata[key];
    }
    
    /**
     * 清理所有过期会话
     * @return 清理的会话数量
     */
    int cleanupExpiredSessions() {
        std::unique_lock<std::shared_mutex> lock(m_mutex);
        
        int count = 0;
        auto now = std::chrono::system_clock::now();
        
        for (auto it = m_sessions.begin(); it != m_sessions.end();) {
            if (it->second.isExpired(m_timeout_seconds)) {
                it = m_sessions.erase(it);
                count++;
            } else {
                ++it;
            }
        }
        
        if (count > 0) {
            HKU_DEBUG("Cleaned up {} expired sessions (remaining: {})", 
                     count, m_sessions.size());
        }
        
        return count;
    }
    
    /**
     * 清空所有会话
     */
    void clearAllSessions() {
        std::unique_lock<std::shared_mutex> lock(m_mutex);
        m_sessions.clear();
        HKU_INFO("All sessions cleared");
    }
    
    /**
     * 获取当前活跃会话数
     */
    size_t getActiveSessionCount() const {
        std::shared_lock<std::shared_mutex> lock(m_mutex);
        return m_sessions.size();
    }
    
    /**
     * 获取会话统计信息
     */
    nlohmann::json getStats() const {
        std::shared_lock<std::shared_mutex> lock(m_mutex);
        
        nlohmann::json stats;
        stats["active_sessions"] = m_sessions.size();
        stats["max_sessions"] = m_max_sessions;
        stats["timeout_seconds"] = m_timeout_seconds;
        stats["utilization"] = (double)m_sessions.size() / m_max_sessions * 100.0;
        
        return stats;
    }

private:
    mutable std::shared_mutex m_mutex;
    std::unordered_map<std::string, SessionData> m_sessions;
    int m_timeout_seconds;
    int m_max_sessions;
};

} // namespace hku
