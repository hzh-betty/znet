#include "socket.h"
#include "znet_logger.h"
#include "io/io_scheduler.h"
#include <errno.h>
#include <fcntl.h>
#include <netinet/tcp.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

namespace znet {

Socket::Socket(int family, int type, int protocol)
    : sockfd_(-1), family_(family), type_(type), protocol_(protocol),
      is_connected_(false) {
  new_sock();
}

Socket::Socket(int sockfd)
    : sockfd_(sockfd), is_connected_(false) {
  // 获取 socket 信息
  socklen_t len = sizeof(family_);
  getsockopt(sockfd_, SOL_SOCKET, SO_DOMAIN, &family_, &len);
  len = sizeof(type_);
  getsockopt(sockfd_, SOL_SOCKET, SO_TYPE, &type_, &len);
  protocol_ = 0;
  init_sock();
}

Socket::~Socket() { close(); }

// ========== 创建 Socket 静态工厂方法 ==========

Socket::ptr Socket::create_tcp() {
  return std::make_shared<Socket>(AF_INET, SOCK_STREAM, IPPROTO_TCP);
}

Socket::ptr Socket::create_tcp(Address::ptr address) {
  return std::make_shared<Socket>(address->family(), SOCK_STREAM, IPPROTO_TCP);
}

Socket::ptr Socket::create_tcp_v6() {
  return std::make_shared<Socket>(AF_INET6, SOCK_STREAM, IPPROTO_TCP);
}

Socket::ptr Socket::create_udp() {
  return std::make_shared<Socket>(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
}

Socket::ptr Socket::create_udp_v6() {
  return std::make_shared<Socket>(AF_INET6, SOCK_DGRAM, IPPROTO_UDP);
}

// ========== 连接管理 ==========

bool Socket::bind(const Address::ptr addr) {
  if (!is_valid()) {
    ZNET_LOG_ERROR("Socket::bind invalid socket");
    return false;
  }

  if (addr->family() != family_) {
    ZNET_LOG_ERROR("Socket::bind address family mismatch: socket={}, addr={}",
                   family_, addr->family());
    return false;
  }

  if (::bind(sockfd_, addr->sockaddr_ptr(), addr->sockaddr_len()) != 0) {
    ZNET_LOG_ERROR("Socket::bind failed: fd={}, errno={}, error={}", sockfd_,
                   errno, strerror(errno));
    return false;
  }

  get_local_address();
  ZNET_LOG_INFO("Socket::bind success: fd={}, addr={}", sockfd_,
                local_address_->to_string());
  return true;
}

bool Socket::listen(int backlog) {
  if (!is_valid()) {
    ZNET_LOG_ERROR("Socket::listen invalid socket");
    return false;
  }

  if (::listen(sockfd_, backlog) != 0) {
    ZNET_LOG_ERROR("Socket::listen failed: fd={}, errno={}, error={}", sockfd_,
                   errno, strerror(errno));
    return false;
  }

  ZNET_LOG_INFO("Socket::listen success: fd={}, backlog={}", sockfd_, backlog);
  return true;
}

Socket::ptr Socket::accept() {
  if (!is_valid()) {
    ZNET_LOG_ERROR("Socket::accept invalid socket");
    return nullptr;
  }

  sockaddr_storage addr;
  socklen_t len = sizeof(addr);
  int clientfd =
      ::accept(sockfd_, reinterpret_cast<sockaddr *>(&addr), &len);

  if (clientfd == -1) {
    if (errno != EAGAIN && errno != EWOULDBLOCK) {
      ZNET_LOG_ERROR("Socket::accept failed: fd={}, errno={}, error={}",
                     sockfd_, errno, strerror(errno));
    }
    return nullptr;
  }

  Socket::ptr client_sock = std::make_shared<Socket>(clientfd);
  ZNET_LOG_INFO("Socket::accept success: fd={}, client_fd={}", sockfd_,
                clientfd);
  return client_sock;
}

bool Socket::connect(const Address::ptr addr, uint64_t timeout_ms) {
  if (!is_valid()) {
    ZNET_LOG_ERROR("Socket::connect invalid socket");
    return false;
  }

  if (addr->family() != family_) {
    ZNET_LOG_ERROR(
        "Socket::connect address family mismatch: socket={}, addr={}", family_,
        addr->family());
    return false;
  }

  remote_address_ = addr;

  // TODO: 支持超时连接
  if (::connect(sockfd_, addr->sockaddr_ptr(), addr->sockaddr_len()) != 0) {
    ZNET_LOG_ERROR("Socket::connect failed: fd={}, addr={}, errno={}, error={}",
                   sockfd_, addr->to_string(), errno, strerror(errno));
    return false;
  }

  is_connected_ = true;
  get_local_address();
  ZNET_LOG_INFO("Socket::connect success: fd={}, remote={}, local={}", sockfd_,
                remote_address_->to_string(), local_address_->to_string());
  return true;
}

bool Socket::reconnect(uint64_t timeout_ms) {
  if (!remote_address_) {
    ZNET_LOG_ERROR("Socket::reconnect no remote address");
    return false;
  }
  local_address_.reset();
  return connect(remote_address_, timeout_ms);
}

bool Socket::close() {
  if (!is_valid()) {
    return true;
  }

  is_connected_ = false;
  if (::close(sockfd_) != 0) {
    ZNET_LOG_ERROR("Socket::close failed: fd={}, errno={}, error={}", sockfd_,
                   errno, strerror(errno));
    return false;
  }

  ZNET_LOG_DEBUG("Socket::close success: fd={}", sockfd_);
  sockfd_ = -1;
  return true;
}

bool Socket::shutdown_write() {
  if (!is_valid()) {
    return false;
  }

  if (::shutdown(sockfd_, SHUT_WR) != 0) {
    ZNET_LOG_ERROR("Socket::shutdown_write failed: fd={}, errno={}, error={}",
                   sockfd_, errno, strerror(errno));
    return false;
  }

  ZNET_LOG_DEBUG("Socket::shutdown_write success: fd={}", sockfd_);
  return true;
}

// ========== 数据传输 ==========

ssize_t Socket::send(const void *buffer, size_t length, int flags) {
  if (!is_valid()) {
    return -1;
  }
  return ::send(sockfd_, buffer, length, flags);
}

ssize_t Socket::recv(void *buffer, size_t length, int flags) {
  if (!is_valid()) {
    return -1;
  }
  return ::recv(sockfd_, buffer, length, flags);
}

ssize_t Socket::send_to(const void *buffer, size_t length,
                        const Address::ptr to, int flags) {
  if (!is_valid()) {
    return -1;
  }
  return ::sendto(sockfd_, buffer, length, flags, to->sockaddr_ptr(),
                  to->sockaddr_len());
}

ssize_t Socket::recv_from(void *buffer, size_t length, Address::ptr from,
                          int flags) {
  if (!is_valid()) {
    return -1;
  }

  sockaddr_storage addr;
  socklen_t len = sizeof(addr);
  ssize_t ret = ::recvfrom(sockfd_, buffer, length, flags,
                           reinterpret_cast<sockaddr *>(&addr), &len);

  if (ret >= 0 && from) {
    from = Address::create(reinterpret_cast<sockaddr *>(&addr), len);
  }

  return ret;
}

// ========== Socket 选项配置 ==========

bool Socket::set_send_timeout(uint64_t timeout_ms) {
  struct timeval tv;
  tv.tv_sec = timeout_ms / 1000;
  tv.tv_usec = (timeout_ms % 1000) * 1000;
  return set_option(SOL_SOCKET, SO_SNDTIMEO, tv);
}

bool Socket::set_recv_timeout(uint64_t timeout_ms) {
  struct timeval tv;
  tv.tv_sec = timeout_ms / 1000;
  tv.tv_usec = (timeout_ms % 1000) * 1000;
  return set_option(SOL_SOCKET, SO_RCVTIMEO, tv);
}

bool Socket::set_tcp_nodelay(bool on) {
  int optval = on ? 1 : 0;
  return set_option(IPPROTO_TCP, TCP_NODELAY, optval);
}

bool Socket::set_reuse_addr(bool on) {
  int optval = on ? 1 : 0;
  return set_option(SOL_SOCKET, SO_REUSEADDR, optval);
}

bool Socket::set_reuse_port(bool on) {
  int optval = on ? 1 : 0;
  return set_option(SOL_SOCKET, SO_REUSEPORT, optval);
}

bool Socket::set_keep_alive(bool on) {
  int optval = on ? 1 : 0;
  return set_option(SOL_SOCKET, SO_KEEPALIVE, optval);
}

bool Socket::set_non_blocking(bool on) {
  int flags = fcntl(sockfd_, F_GETFL, 0);
  if (flags == -1) {
    ZNET_LOG_ERROR("Socket::set_non_blocking fcntl F_GETFL failed: fd={}",
                   sockfd_);
    return false;
  }

  if (on) {
    flags |= O_NONBLOCK;
  } else {
    flags &= ~O_NONBLOCK;
  }

  if (fcntl(sockfd_, F_SETFL, flags) == -1) {
    ZNET_LOG_ERROR("Socket::set_non_blocking fcntl F_SETFL failed: fd={}",
                   sockfd_);
    return false;
  }

  return true;
}

// ========== 地址查询 ==========

Address::ptr Socket::get_local_address() {
  if (local_address_) {
    return local_address_;
  }

  sockaddr_storage addr;
  socklen_t len = sizeof(addr);
  if (getsockname(sockfd_, reinterpret_cast<sockaddr *>(&addr), &len) != 0) {
    ZNET_LOG_ERROR("Socket::get_local_address failed: fd={}", sockfd_);
    return nullptr;
  }

  local_address_ = Address::create(reinterpret_cast<sockaddr *>(&addr), len);
  return local_address_;
}

Address::ptr Socket::get_remote_address() {
  if (remote_address_) {
    return remote_address_;
  }

  sockaddr_storage addr;
  socklen_t len = sizeof(addr);
  if (getpeername(sockfd_, reinterpret_cast<sockaddr *>(&addr), &len) != 0) {
    ZNET_LOG_ERROR("Socket::get_remote_address failed: fd={}", sockfd_);
    return nullptr;
  }

  remote_address_ = Address::create(reinterpret_cast<sockaddr *>(&addr), len);
  return remote_address_;
}

int Socket::get_error() {
  int error = 0;
  socklen_t len = sizeof(error);
  if (!get_option(SOL_SOCKET, SO_ERROR, &error)) {
    return -1;
  }
  (void)len;
  return error;
}

// ========== IoScheduler 集成 ==========

bool Socket::cancel_read() {
  zcoroutine::IoScheduler *scheduler = zcoroutine::IoScheduler::get_this();
  if (!scheduler) {
    return false;
  }
  return scheduler->cancel_event(sockfd_, zcoroutine::FdContext::kRead) == 0;
}

bool Socket::cancel_write() {
  zcoroutine::IoScheduler *scheduler = zcoroutine::IoScheduler::get_this();
  if (!scheduler) {
    return false;
  }
  return scheduler->cancel_event(sockfd_, zcoroutine::FdContext::kWrite) == 0;
}

bool Socket::cancel_accept() {
  // accept 就是读事件
  return cancel_read();
}

bool Socket::cancel_all() {
  zcoroutine::IoScheduler *scheduler = zcoroutine::IoScheduler::get_this();
  if (!scheduler) {
    return false;
  }
  return scheduler->cancel_all(sockfd_) == 0;
}

// ========== 私有方法 ==========

void Socket::init_sock() {
  // 设置常用选项
  set_reuse_addr(true);

  if (type_ == SOCK_STREAM) {
    set_tcp_nodelay(true);
  }
}

bool Socket::new_sock() {
  sockfd_ = socket(family_, type_, protocol_);
  if (sockfd_ == -1) {
    ZNET_LOG_ERROR("Socket::new_sock failed: family={}, type={}, protocol={}, "
                   "errno={}, error={}",
                   family_, type_, protocol_, errno, strerror(errno));
    return false;
  }

  init_sock();
  ZNET_LOG_DEBUG("Socket::new_sock success: fd={}, family={}, type={}",
                 sockfd_, family_, type_);
  return true;
}

} // namespace znet
