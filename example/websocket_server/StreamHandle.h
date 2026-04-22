#include "EchoWsHandle.h"
#include "QuotePushHandle.h"
#include <hikyuu/httpd/HttpHandle.h>
#include <fstream>
#include <chrono>

using json = nlohmann::json;
namespace net = boost::asio;

namespace hku {

/**
 * 文件下载 Handle - 使用流式分批传输大文件
 */
class FileDownloadHandle : public HttpHandle {
public:
    explicit FileDownloadHandle(void* beast_context) : HttpHandle(beast_context) {}

    net::awaitable<VoidBizResult> run() override {
        auto* ctx = static_cast<BeastContext*>(m_beast_context);

        // 获取要下载的文件路径
        std::string filepath = getQueryValue("file");

        if (filepath.empty()) {
            ctx->res.result(http::status::bad_request);
            ctx->res.set(http::field::content_type, "application/json");
            ctx->res.body() = R"({"error": "Missing 'file' parameter"})";
            co_return BIZ_OK;
        }

        // 打开文件
        std::ifstream file(filepath, std::ios::binary | std::ios::ate);
        if (!file.is_open()) {
            ctx->res.result(http::status::not_found);
            ctx->res.set(http::field::content_type, "application/json");
            ctx->res.body() = R"({"error": "File not found"})";
            co_return BIZ_OK;
        }

        // 获取文件大小
        auto filesize = static_cast<std::size_t>(file.tellg());
        file.seekg(0, std::ios::beg);

        HKU_INFO("Downloading file: {}, size: {} bytes", filepath, filesize);

        // 设置响应头
        ctx->res.result(http::status::ok);
        ctx->res.set(http::field::content_type, "application/octet-stream");
        ctx->res.set(http::field::content_disposition,
                     fmt::format("attachment; filename=\"{}\"",
                                 filepath.substr(filepath.find_last_of("/\\") + 1)));

        // 启用分块传输，流式发送，避免一次性加载大文件到内存
        enableChunkedTransfer();

        // 分批读取并发送文件内容（使用配置的分块大小）
        std::vector<char> buffer(HttpConfig::CHUNK_SIZE);

        while (file) {
            file.read(buffer.data(), HttpConfig::CHUNK_SIZE);
            std::streamsize bytes_read = file.gcount();

            if (bytes_read > 0) {
                std::string chunk(buffer.data(), bytes_read);

                // 使用同步方式写入（简单场景）
                if (!co_await writeChunk(chunk)) {
                    HKU_ERROR("Failed to write chunk");
                    break;
                }
            }
        }

        HKU_INFO("File download completed: {}", filepath);

        // 完成分块传输（即使出错也要标记 response_sent）
        co_await finishChunkedTransfer();

        // HKU_ERROR(">>> After finishChunkedTransfer: response_sent = {}",
        //          ctx->response_sent ? "true" : "false");
        // HKU_DEBUG("response_sent = {}", ctx->response_sent ? "true" : "false");
        co_return BIZ_OK;
    }

private:
    std::string getQueryValue(const std::string& key) {
        auto params = getQueryParams().value_or(QueryParams());
        auto it = params.find(key);
        if (it != params.end()) {
            return it->second;
        }
        return "";
    }
};

/**
 * SSE (Server-Sent Events) Handle - 实时数据推送
 */
class SSEHandle : public HttpHandle {
public:
    explicit SSEHandle(void* beast_context) : HttpHandle(beast_context) {}

    net::awaitable<VoidBizResult> run() override {
        auto* ctx = static_cast<BeastContext*>(m_beast_context);

        // 设置 SSE 响应头
        ctx->res.result(http::status::ok);
        ctx->res.set(http::field::content_type, "text/event-stream");
        ctx->res.set(http::field::cache_control, "no-cache");
        ctx->res.set(http::field::connection, "keep-alive");
        ctx->res.set("Access-Control-Allow-Origin", "*");

        // 启用分块传输
        enableChunkedTransfer();

        HKU_INFO("SSE connection established");

        // 发送 10 条消息，每秒 1 条
        for (int i = 0; i < 10; ++i) {
            // SSE 格式：data: {message}\n\n
            std::string message = fmt::format("data: Message {} at {}\n\n", i, std::time(nullptr));

            if (!co_await writeChunk(message)) {
                HKU_DEBUG("Client disconnected during SSE stream");
                break;
            }

            HKU_DEBUG("Sent SSE message {}", i);

            // 等待 1 秒
            net::steady_timer timer(ctx->timer.get_executor());
            timer.expires_after(std::chrono::seconds(1));
            co_await timer.async_wait(net::use_awaitable);
        }

        // 完成分块传输
        co_await finishChunkedTransfer();

        HKU_INFO("SSE connection closed");
        co_return BIZ_OK;
    }
};

/**
 * 大数据量 CSV 导出 Handle
 */
class CSVExportHandle : public HttpHandle {
public:
    explicit CSVExportHandle(void* beast_context) : HttpHandle(beast_context) {}

    net::awaitable<VoidBizResult> run() override {
        auto* ctx = static_cast<BeastContext*>(m_beast_context);

        HKU_INFO("Exporting CSV data");

        // ========== 批量响应模式检测 ==========
        // 如果启用了批量响应且数据量超过阈值，自动切换到流式导出
        constexpr std::size_t TOTAL_RECORDS = 10000;
        if (HttpConfig::ENABLE_BATCH_RESPONSE && TOTAL_RECORDS >= HttpConfig::BATCH_THRESHOLD) {
            HKU_DEBUG(
              "Batch response mode enabled: {} records >= threshold {}, using streaming export",
              TOTAL_RECORDS, HttpConfig::BATCH_THRESHOLD);
            // 自动启用分块传输
            enableChunkedTransfer();
        }

        // 设置 CSV 响应头
        ctx->res.result(http::status::ok);
        ctx->res.set(http::field::content_type, "text/csv; charset=utf-8");
        ctx->res.set(http::field::content_disposition, "attachment; filename=\"export.csv\"");

        // 启用分块传输（如果还未启用）
        if (!isChunkedTransferEnabled()) {
            enableChunkedTransfer();
        }

        // 发送 CSV 头部
        std::string header = "symbol,name,price,change,volume\n";
        co_await writeChunk(header);

        // 模拟生成 10000 条数据（分批发送）
        constexpr std::size_t BATCH_SIZE = 500;  // 每批 500 条（业务层决策）
        std::ostringstream batch;

        for (int i = 0; i < TOTAL_RECORDS; ++i) {
            batch << fmt::format("SH{:06d},Stock{},{:.2f},{:.2f},{}\n", i, i,
                                 10.0 + (i % 100) * 0.1, (i % 10) * 0.01, 1000000 + i * 1000);

            // 每达到 BATCH_SIZE 条发送一批
            if ((i + 1) % BATCH_SIZE == 0) {
                if (!co_await writeChunk(batch.str())) {
                    HKU_DEBUG("Client disconnected during CSV export");
                    break;
                }
                batch.str("");  // 清空缓冲区

                // 短暂延迟，避免网络拥塞
                net::steady_timer timer(ctx->timer.get_executor());
                timer.expires_after(std::chrono::milliseconds(10));
                co_await timer.async_wait(net::use_awaitable);
            }
        }

        // 发送剩余数据
        if (!batch.str().empty()) {
            co_await writeChunk(batch.str());
        }

        // 完成分块传输
        co_await finishChunkedTransfer();

        HKU_INFO("CSV export completed: {} records", TOTAL_RECORDS);

        co_return BIZ_OK;
    }
};

}  // namespace hku
