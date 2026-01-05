#include "address.h"
#include "tcp_connection.h"
#include "tcp_server.h"
#include "znet_logger.h"
#include "io/io_scheduler.h"
#include <memory>
#include <signal.h>

using namespace znet;
using namespace zcoroutine;

/**
 * @brief Echo服务器实现
 * 简单的回显服务器，将客户端发送的数据原样返回
 */
class EchoServer : public TcpServer {
public:
  EchoServer(IoScheduler *io_worker, IoScheduler *accept_worker)
      : TcpServer(io_worker, accept_worker) {
    set_name("EchoServer/1.0");
  }

protected:
  void handle_client(TcpConnectionPtr conn) override {
    ZNET_LOG_INFO("EchoServer: new connection [{}] from {}", conn->name(),
                  conn->peer_address()->to_string());

    // 设置回调函数
    conn->set_connection_callback(
        [](const TcpConnectionPtr &conn) {
          if (conn->connected()) {
            ZNET_LOG_INFO("Connection established: [{}]", conn->name());
          } else {
            ZNET_LOG_INFO("Connection disconnected: [{}]", conn->name());
          }
        });

    conn->set_message_callback(
        [](const TcpConnectionPtr &conn, Buffer *buf) {
          // Echo: 读取所有数据并原样返回
          std::string msg = buf->read_all_as_string();
          ZNET_LOG_INFO("EchoServer: received {} bytes from [{}]: {}",
                        msg.size(), conn->name(), msg);
          
          // 回显数据
          conn->send(msg);
        });

    conn->set_write_complete_callback(
        [](const TcpConnectionPtr &conn) {
          ZNET_LOG_DEBUG("EchoServer: write complete for [{}]", conn->name());
        });

    conn->set_close_callback(
        [](const TcpConnectionPtr &conn) {
          ZNET_LOG_INFO("EchoServer: connection closed [{}]", conn->name());
        });

    // 建立连接
    conn->connect_established();
  }
};

static bool g_stop = false;

void signal_handler(int sig) {
  if (sig == SIGINT || sig == SIGTERM) {
    ZNET_LOG_INFO("Received signal {}, stopping server...", sig);
    g_stop = true;
  }
}

int main(int argc, char *argv[]) {
  // 初始化日志
  ZNET_LOG_INFO("Starting Echo Server...");

  // 设置信号处理
  signal(SIGINT, signal_handler);
  signal(SIGTERM, signal_handler);

  // 创建IoScheduler
  // accept_worker: 1个线程专门处理accept
  // io_worker: 4个线程处理IO事件
  IoScheduler accept_worker(1, "accept");
  IoScheduler io_worker(4, "io");

  // 创建EchoServer
  auto server = std::make_shared<EchoServer>(&io_worker, &accept_worker);

  // 绑定地址
  uint16_t port = 8080;
  if (argc >= 2) {
    port = static_cast<uint16_t>(std::atoi(argv[1]));
  }

  Address::ptr addr = std::make_shared<IPv4Address>("0.0.0.0", port);
  if (!server->bind(addr)) {
    ZNET_LOG_ERROR("Failed to bind address {}", addr->to_string());
    return 1;
  }

  ZNET_LOG_INFO("Echo Server listening on {}", addr->to_string());

  // 启动server
  if (!server->start()) {
    ZNET_LOG_ERROR("Failed to start server");
    return 1;
  }

  ZNET_LOG_INFO("Echo Server started successfully. Press Ctrl+C to stop.");

  // 主线程等待退出信号
  while (!g_stop) {
    sleep(1);
  }

  // 停止server
  server->stop();
  ZNET_LOG_INFO("Echo Server stopped.");

  return 0;
}
