#include "address.h"
#include "tcp_connection.h"
#include "tcp_server.h"
#include "znet_logger.h"
#include "io/io_scheduler.h"
#include <iostream>
#include <memory>
#include <signal.h>
#include <sstream>

using namespace znet;
using namespace zcoroutine;

/**
 * @brief 简单的HTTP服务器实现
 * 只处理GET请求，返回固定的响应
 */
class SimpleHttpServer : public TcpServer {
public:
  SimpleHttpServer(IoScheduler *io_worker, IoScheduler *accept_worker)
      : TcpServer(io_worker, accept_worker) {
    set_name("SimpleHttpServer/1.0");
  }

protected:
  void handle_client(TcpConnectionPtr conn) override {
    ZNET_LOG_INFO("HttpServer: new connection [{}] from {}", conn->name(),
                  conn->peer_address()->to_string());

    // 设置回调函数
    conn->set_connection_callback([](const TcpConnectionPtr &conn) {
      if (conn->connected()) {
        ZNET_LOG_INFO("HTTP connection established: [{}]", conn->name());
      } else {
        ZNET_LOG_INFO("HTTP connection disconnected: [{}]", conn->name());
      }
    });

    conn->set_message_callback([](const TcpConnectionPtr &conn, Buffer *buf) {
      // 读取HTTP请求
      std::string request = buf->read_all_as_string();
      
      ZNET_LOG_INFO("HttpServer: received request from [{}]:\n{}",
                    conn->name(), request);

      // 解析请求行（非常简单的解析）
      std::istringstream iss(request);
      std::string method, uri, version;
      iss >> method >> uri >> version;

      ZNET_LOG_INFO("HTTP Request: {} {} {}", method, uri, version);

      // 构造HTTP响应
      std::ostringstream response;
      response << "HTTP/1.1 200 OK\r\n";
      response << "Content-Type: text/html; charset=UTF-8\r\n";
      response << "Connection: close\r\n";
      
      std::string body = "<html><body>"
                        "<h1>Welcome to znet HTTP Server!</h1>"
                        "<p>This is a simple HTTP server built with znet.</p>"
                        "<p>Request URI: " + uri + "</p>"
                        "<p>Method: " + method + "</p>"
                        "</body></html>";
      
      response << "Content-Length: " << body.size() << "\r\n";
      response << "\r\n";
      response << body;

      // 发送响应
      conn->send(response.str());
      
      // HTTP/1.0 或 Connection: close，发送完毕后关闭连接
      conn->shutdown();
    });

    conn->set_write_complete_callback([](const TcpConnectionPtr &conn) {
      ZNET_LOG_DEBUG("HttpServer: response sent for [{}]", conn->name());
    });

    conn->set_close_callback([](const TcpConnectionPtr &conn) {
      ZNET_LOG_INFO("HttpServer: connection closed [{}]", conn->name());
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
  ZNET_LOG_INFO("Starting Simple HTTP Server...");

  // 设置信号处理
  signal(SIGINT, signal_handler);
  signal(SIGTERM, signal_handler);

  // 创建IoScheduler
  IoScheduler accept_worker(1, "accept");
  IoScheduler io_worker(4, "io");

  // 创建HttpServer
  auto server = std::make_shared<SimpleHttpServer>(&io_worker, &accept_worker);

  // 绑定地址
  uint16_t port = 8888;
  if (argc >= 2) {
    port = static_cast<uint16_t>(std::atoi(argv[1]));
  }

  Address::ptr addr = std::make_shared<IPv4Address>("0.0.0.0", port);
  if (!server->bind(addr)) {
    ZNET_LOG_ERROR("Failed to bind address {}", addr->to_string());
    return 1;
  }

  ZNET_LOG_INFO("HTTP Server listening on {}", addr->to_string());

  // 启动server
  if (!server->start()) {
    ZNET_LOG_ERROR("Failed to start server");
    return 1;
  }

  ZNET_LOG_INFO("HTTP Server started successfully.");
  ZNET_LOG_INFO("Open your browser and visit http://localhost:{}", port);
  ZNET_LOG_INFO("Press Ctrl+C to stop.");

  // 主线程等待退出信号
  while (!g_stop) {
    sleep(1);
  }

  // 停止server
  server->stop();
  ZNET_LOG_INFO("HTTP Server stopped.");

  return 0;
}
