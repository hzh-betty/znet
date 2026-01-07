#include "tcp_connection.h"
#include "io/io_scheduler.h"
#include "znet_logger.h"
#include <errno.h>
#include <string.h>

namespace znet {

/**
 * @brief RAII锁守护，用于Spinlock
 */
class SpinlockGuard {
public:
  explicit SpinlockGuard(zcoroutine::Spinlock &lock) : lock_(lock) {
    lock_.lock();
  }
  ~SpinlockGuard() { lock_.unlock(); }
private:
  zcoroutine::Spinlock &lock_;
};

const char* TcpConnection::state_to_string(State s) {
  switch (s) {
    case State::Connecting:
      return "Connecting";
    case State::Connected:
      return "Connected";
    case State::Disconnecting:
      return "Disconnecting";
    case State::Disconnected:
      return "Disconnected";
    default:
      return "Unknown";
  }
}

TcpConnection::TcpConnection(const std::string &name, Socket::ptr socket,
                             const Address::ptr &local_addr,
                             const Address::ptr &peer_addr,
                             zcoroutine::IoScheduler *io_scheduler)
    : name_(name), state_(State::Connecting),
      socket_(std::move(socket)), local_addr_(local_addr),
      peer_addr_(peer_addr), io_scheduler_(io_scheduler) {

  ZNET_LOG_INFO("TcpConnection::TcpConnection [{}] fd={} local={} peer={}",
                name_, socket_->fd(), local_addr_->to_string(),
                peer_addr_->to_string());
}

TcpConnection::~TcpConnection() {
  ZNET_LOG_INFO("TcpConnection::~TcpConnection [{}] fd={} state={}", name_,
                socket_->fd(), state_to_string(state_.load(std::memory_order_acquire)));
  
  // 防御性清理：如果连接还没有关闭，移除 epoll 事件
  if (io_scheduler_ && !disconnected()) {
    socket_->close();
  }
}

void TcpConnection::connect_established() {
  set_state(State::Connected);

  // 设置Socket为非阻塞模式
  socket_->set_non_blocking(true);

  // 注册读事件到IoScheduler
  if (io_scheduler_) {
    io_scheduler_->add_event(
        socket_->fd(), zcoroutine::FdContext::kRead,
        std::bind(&TcpConnection::handle_read, shared_from_this()));
  }

  // 触发连接建立回调（异步调度）
  if (connection_callback_) {
    std::weak_ptr<TcpConnection> weak_self = shared_from_this();
    auto cb = connection_callback_;
    io_scheduler_->schedule([weak_self, cb]() {
      if (auto self = weak_self.lock()) {
        cb(self);
      }
    });
  }

  ZNET_LOG_INFO("TcpConnection::connect_established [{}] fd={}", name_,
                socket_->fd());
}


void TcpConnection::send(const void *data, size_t len) {
  if (state_.load(std::memory_order_acquire) != State::Connected) {
    ZNET_LOG_WARN("TcpConnection::send [{}] not connected, state={}", name_,
                  state_to_string(state_.load(std::memory_order_acquire)));
    return;
  }

  send_in_loop(data, len);
}

void TcpConnection::send(const std::string &message) {
  send(message.data(), message.size());
}

void TcpConnection::send(Buffer *buf) {
  send(buf->peek(), buf->readable_bytes());
  buf->retrieve_all();
}

void TcpConnection::shutdown() {
  State expected = State::Connected;
  if (state_.compare_exchange_strong(expected, State::Disconnecting,
                                      std::memory_order_acq_rel)) {
    shutdown_in_loop();
  }
}

void TcpConnection::force_close() {
  State current = state_.load(std::memory_order_acquire);
  if (current == State::Connected || current == State::Disconnecting) {
    state_.store(State::Disconnecting, std::memory_order_release);
    force_close_in_loop();
  }
}

void TcpConnection::set_tcp_no_delay(bool on) { socket_->set_tcp_nodelay(on); }

void TcpConnection::set_keep_alive(bool on) { socket_->set_keep_alive(on); }

void TcpConnection::handle_read() {
  int saved_errno = 0;
  ssize_t n = input_buffer_.read_fd(socket_->fd(), &saved_errno);

  if (n > 0) {
    // 数据到达，触发消息回调（异步调度）
    if (message_callback_ && io_scheduler_) {
      std::weak_ptr<TcpConnection> weak_self = shared_from_this();
      auto cb = message_callback_;
      auto buf = std::make_shared<Buffer>();
      buf->swap(input_buffer_);
      io_scheduler_->schedule([weak_self, cb, buf]() {
        if (auto self = weak_self.lock()) {
          cb(self, buf.get());
        }
      });
    }
  } else if (n == 0) {
    // 对端关闭连接
    ZNET_LOG_INFO("TcpConnection::handle_read [{}] peer closed", name_);
    handle_close();
  } else {
    // 读取错误
    errno = saved_errno;
    ZNET_LOG_ERROR("TcpConnection::handle_read [{}] error: {}", name_,
                   strerror(errno));
    handle_error();
  }
}

void TcpConnection::handle_write() {
  if (!connected()) {
    ZNET_LOG_WARN("TcpConnection::handle_write [{}] not connected", name_);
    return;
  }

  int saved_errno = 0;
  ssize_t n = 0;
  size_t remaining = 0;
  
  {
    SpinlockGuard lock(output_buffer_lock_);
    n = output_buffer_.write_fd(socket_->fd(), &saved_errno);
    remaining = output_buffer_.readable_bytes();
  }

  if (n > 0) {
    if (remaining == 0) {
      // 数据全部发送完成，取消写事件
      if (io_scheduler_) {
        io_scheduler_->del_event(socket_->fd(), zcoroutine::FdContext::kWrite);
      }

      // 触发写完成回调（异步调度）
      if (write_complete_callback_ && io_scheduler_) {
        std::weak_ptr<TcpConnection> weak_self = shared_from_this();
        auto cb = write_complete_callback_;
        io_scheduler_->schedule([weak_self, cb]() {
          if (auto self = weak_self.lock()) {
            cb(self);
          }
        });
      }

      if (state_.load(std::memory_order_acquire) == State::Disconnecting) {
        // 如果正在断开连接，且数据已发送完成，则关闭写端
        shutdown_in_loop();
      }
    }
  } else {
    ZNET_LOG_ERROR("TcpConnection::handle_write [{}] error", name_);
  }
}

void TcpConnection::handle_close() {
  // 避免重复关闭
  State current = state_.load(std::memory_order_acquire);
  if (current == State::Disconnected) {
    return;
  }

  ZNET_LOG_INFO("TcpConnection::handle_close [{}] state={}", name_,
                state_to_string(current));

  set_state(State::Disconnected);

  // 关闭 socket
  socket_->close();

  // 触发关闭回调（异步调度）
  if (close_callback_ && io_scheduler_) {
    std::weak_ptr<TcpConnection> weak_self = shared_from_this();
    auto cb = close_callback_;
    io_scheduler_->schedule([weak_self, cb]() {
      if (auto self = weak_self.lock()) {
        cb(self);
      }
    });
  }
}

void TcpConnection::handle_error() {
  int err = socket_->get_error();
  ZNET_LOG_ERROR("TcpConnection::handle_error [{}] SO_ERROR={} {}", name_, err,
                 strerror(err));
  // 发生错误时关闭连接
  handle_close();
}

void TcpConnection::send_in_loop(const void *data, size_t len) {
  ssize_t n_wrote = 0;
  size_t remaining = len;
  bool fault_error = false;

  SpinlockGuard lock(output_buffer_lock_);
  
  // 如果输出缓冲区没有数据，尝试直接发送
  if (output_buffer_.readable_bytes() == 0) {
    n_wrote = socket_->send(data, len);
    if (n_wrote >= 0) {
      remaining = len - n_wrote;
      if (remaining == 0 && write_complete_callback_ && io_scheduler_) {
        // 数据全部发送完成（异步调度）
        std::weak_ptr<TcpConnection> weak_self = shared_from_this();
        auto cb = write_complete_callback_;
        io_scheduler_->schedule([weak_self, cb]() {
          if (auto self = weak_self.lock()) {
            cb(self);
          }
        });
      }
    } else {
      n_wrote = 0;
      if (errno != EWOULDBLOCK && errno != EAGAIN) {
        ZNET_LOG_ERROR("TcpConnection::send_in_loop [{}] error: {}", name_,
                       strerror(errno));
        if (errno == EPIPE || errno == ECONNRESET) {
          fault_error = true;
        }
      }
    }
  }

  // 如果还有数据未发送完，写入输出缓冲区
  if (!fault_error && remaining > 0) {
    output_buffer_.append(static_cast<const char *>(data) + n_wrote, remaining);

    // 注册写事件
    if (io_scheduler_) {
      io_scheduler_->add_event(
          socket_->fd(), zcoroutine::FdContext::kWrite,
          std::bind(&TcpConnection::handle_write, shared_from_this()));
    }

    ZNET_LOG_DEBUG("TcpConnection::send_in_loop [{}] buffered {} bytes, total "
                   "{} bytes in buffer",
                   name_, remaining, output_buffer_.readable_bytes());
  }
}

void TcpConnection::shutdown_in_loop() {
  SpinlockGuard lock(output_buffer_lock_);
  if (output_buffer_.readable_bytes() == 0) {
    // 输出缓冲区为空，可以关闭写端
    socket_->shutdown_write();
  }
}

void TcpConnection::force_close_in_loop() {
  State current = state_.load(std::memory_order_acquire);
  if (current == State::Connected || current == State::Disconnecting) {
    handle_close();
  }
}

} // namespace znet
