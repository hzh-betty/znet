#include "tcp_server.h"
#include "io/io_scheduler.h"
#include "runtime/fiber_pool.h"
#include "znet_logger.h"
#include <errno.h>
#include <sstream>
#include <string.h>

namespace znet {

TcpServer::TcpServer(zcoroutine::IoScheduler::ptr io_worker,
                     zcoroutine::IoScheduler::ptr accept_worker)
    : io_worker_(io_worker),
      accept_worker_(accept_worker ? accept_worker
                                   : std::make_shared<zcoroutine::IoScheduler>(
                                         1, "TcpServer-Accept", false)),
      recv_timeout_(60 * 1000 * 2), // 默认 2 分钟
      name_("znet/1.0.0"), type_("tcp"), is_stop_(true) {}

TcpServer::~TcpServer() {
  for (auto &i : socks_) {
    i->close();
  }
  socks_.clear();
}

bool TcpServer::bind(Address::ptr addr) {
  std::vector<Address::ptr> addrs;
  std::vector<Address::ptr> fails;
  addrs.push_back(addr);
  return bind(addrs, fails);
}

bool TcpServer::bind(const std::vector<Address::ptr> &addrs,
                     std::vector<Address::ptr> &fails) {
  for (auto &addr : addrs) {
    Socket::ptr sock = Socket::create_tcp(addr);
    if (!sock) {
      ZNET_LOG_ERROR("TcpServer::bind create socket failed for addr={}",
                     addr->to_string());
      fails.push_back(addr);
      continue;
    }

    if (!sock->bind(addr)) {
      ZNET_LOG_ERROR("TcpServer::bind bind fail errno={} errstr={} addr={}",
                     errno, strerror(errno), addr->to_string());
      fails.push_back(addr);
      continue;
    }

    if (!sock->listen()) {
      ZNET_LOG_ERROR("TcpServer::bind listen fail errno={} errstr={} addr={}",
                     errno, strerror(errno), addr->to_string());
      fails.push_back(addr);
      continue;
    }

    socks_.push_back(sock);
  }

  if (!fails.empty()) {
    socks_.clear();
    return false;
  }

  for (auto &i : socks_) {
    ZNET_LOG_INFO("TcpServer type={} name={} bind success: fd={} addr={}",
                  type_, name_, i->fd(), i->get_local_address()->to_string());
  }
  return true;
}

void TcpServer::start_accept(Socket::ptr sock) {
  while (!is_stop_) {
    Socket::ptr client = sock->accept();
    if (client) {
      // 设置接收超时
      if (recv_timeout_ > 0) {
        client->set_recv_timeout(recv_timeout_);
      }

      // 创建TcpConnection
      auto local_addr = client->get_local_address();
      auto peer_addr = client->get_remote_address();
      std::string conn_name = name_ + "-" + peer_addr->to_string();

      TcpConnectionPtr conn = std::make_shared<TcpConnection>(
          conn_name, client->fd(), local_addr, peer_addr, io_worker_.get());

      // 处理连接 - 使用协程池创建协程
      if (io_worker_) {
        auto self = shared_from_this();
        auto fiber = zcoroutine::FiberPool::get_instance().get_fiber(
            [self, conn]() { self->handle_client(conn); });
        io_worker_->schedule(std::move(fiber));
      } else {
        handle_client(conn);
      }
    } else {
      if (errno != EAGAIN && errno != EWOULDBLOCK) {
        ZNET_LOG_ERROR("TcpServer::start_accept accept errno={} errstr={}",
                       errno, strerror(errno));
      }
    }
  }
}

bool TcpServer::start() {
  if (!is_stop_) {
    return true;
  }
  is_stop_ = false;

  // 启动 io_worker_
  if (io_worker_) {
    io_worker_->start();
  }

  // 启动 accept_worker_
  if (accept_worker_) {
    accept_worker_->start();
  }

  for (auto &sock : socks_) {
    if (accept_worker_) {
      // 使用协程池创建协程用于接受连接
      auto self = shared_from_this();
      auto fiber = zcoroutine::FiberPool::get_instance().get_fiber(
          [self, sock]() { self->start_accept(sock); });
      accept_worker_->schedule(std::move(fiber));
    } else if (io_worker_) {
      auto self = shared_from_this();
      auto fiber = zcoroutine::FiberPool::get_instance().get_fiber(
          [self, sock]() { self->start_accept(sock); });
      io_worker_->schedule(std::move(fiber));
    } else {
      ZNET_LOG_ERROR("TcpServer::start no scheduler available");
      return false;
    }
  }
  return true;
}

void TcpServer::stop() {
  is_stop_ = true;
  auto self = shared_from_this();

  auto stop_func = [this, self]() {
    for (auto &sock : socks_) {
      sock->close();
    }
    socks_.clear();
  };

  if (accept_worker_) {
    auto fiber = zcoroutine::FiberPool::get_instance().get_fiber(stop_func);
    accept_worker_->schedule(std::move(fiber));
  } else if (io_worker_) {
    auto fiber = zcoroutine::FiberPool::get_instance().get_fiber(stop_func);
    io_worker_->schedule(std::move(fiber));
  } else {
    stop_func();
  }
}

void TcpServer::handle_client(TcpConnectionPtr conn) {
  ZNET_LOG_INFO("TcpServer::handle_client connection [{}] fd={} remote={}",
                conn->name(), conn->socket()->fd(),
                conn->peer_address()->to_string());

  // 默认实现：什么都不做
  // 子类应该重写这个方法来处理客户端连接
}

std::string TcpServer::to_string(const std::string &prefix) {
  std::stringstream ss;
  ss << prefix << "[type=" << type_ << " name=" << name_
     << " recv_timeout=" << recv_timeout_ << "]" << std::endl;
  std::string pfx = prefix.empty() ? "    " : prefix;
  for (auto &i : socks_) {
    ss << pfx << pfx << "fd=" << i->fd()
       << " local=" << i->get_local_address()->to_string() << std::endl;
  }
  return ss.str();
}

} // namespace znet
