#include "tcp_connection.h"
#include "io/io_scheduler.h"
#include "znet_logger.h"
#include <errno.h>
#include <string.h>

namespace znet {

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
                socket_->fd(), static_cast<int>(state_));
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

  // 触发连接建立回调（直接调用，不异步化）
  if (connection_callback_) {
    connection_callback_(shared_from_this());
  }

  ZNET_LOG_INFO("TcpConnection::connect_established [{}] fd={}", name_,
                socket_->fd());
}

void TcpConnection::connect_destroyed() {
  // 避免重复清理
  if (state_ == State::Disconnected) {
    // 已经通过 handle_close 清理过了
    ZNET_LOG_DEBUG("TcpConnection::connect_destroyed [{}] already disconnected", name_);
    return;
  }

  set_state(State::Disconnected);

  // 取消所有IO事件
  if (io_scheduler_) {
    io_scheduler_->cancel_all(socket_->fd());
  }

  // 触发连接断开回调（直接调用，不异步化）
  if (connection_callback_) {
    connection_callback_(shared_from_this());
  }

  socket_->close();
  ZNET_LOG_INFO("TcpConnection::connect_destroyed [{}] fd={}", name_,
                socket_->fd());
}

void TcpConnection::send(const void *data, size_t len) {
  if (state_ != State::Connected) {
    ZNET_LOG_WARN("TcpConnection::send [{}] not connected, state={}", name_,
                  static_cast<int>(state_));
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
  if (state_ == State::Connected) {
    set_state(State::Disconnecting);
    shutdown_in_loop();
  }
}

void TcpConnection::force_close() {
  if (state_ == State::Connected || state_ == State::Disconnecting) {
    set_state(State::Disconnecting);
    force_close_in_loop();
  }
}

void TcpConnection::set_tcp_no_delay(bool on) { socket_->set_tcp_nodelay(on); }

void TcpConnection::set_keep_alive(bool on) { socket_->set_keep_alive(on); }

void TcpConnection::handle_read() {
  int saved_errno = 0;
  ssize_t n = input_buffer_.read_fd(socket_->fd(), &saved_errno);

  if (n > 0) {
    // 数据到达，触发消息回调（直接调用，不异步化）
    if (message_callback_) {
      message_callback_(shared_from_this(), &input_buffer_);
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
  ssize_t n = output_buffer_.write_fd(socket_->fd(), &saved_errno);

  if (n > 0) {
    if (output_buffer_.readable_bytes() == 0) {
      // 数据全部发送完成，取消写事件
      if (io_scheduler_) {
        io_scheduler_->del_event(socket_->fd(), zcoroutine::FdContext::kWrite);
      }

      // 触发写完成回调（直接调用，不异步化）
      if (write_complete_callback_) {
        write_complete_callback_(shared_from_this());
      }

      if (state_ == State::Disconnecting) {
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
  if (state_ == State::Disconnected) {
    return;
  }

  ZNET_LOG_INFO("TcpConnection::handle_close [{}] state={}", name_,
                static_cast<int>(state_));

  set_state(State::Disconnected);

  // 关闭 socket
  socket_->close();

  // 触发关闭回调（直接调用，不异步化）
  if (close_callback_) {
    close_callback_(shared_from_this());
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

  // 如果输出缓冲区没有数据，尝试直接发送
  if (output_buffer_.readable_bytes() == 0) {
    n_wrote = socket_->send(data, len);
    if (n_wrote >= 0) {
      remaining = len - n_wrote;
      if (remaining == 0 && write_complete_callback_) {
        // 数据全部发送完成
        write_complete_callback_(shared_from_this());
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
  if (output_buffer_.readable_bytes() == 0) {
    // 输出缓冲区为空，可以关闭写端
    socket_->shutdown_write();
  }
}

void TcpConnection::force_close_in_loop() {
  if (state_ == State::Connected || state_ == State::Disconnecting) {
    handle_close();
  }
}

} // namespace znet
