#include "buffer.h"
#include "znet_endian.h"
#include <arpa/inet.h>
#include <cstring>
#include <errno.h>
#include <sys/uio.h>
#include <unistd.h>

// 为了使用be64toh和htobe64
#ifdef __linux__
#include <endian.h>
#endif

namespace znet {

// 使用znet_endian定义字节序转换宏
namespace {

// 主机字节序转网络字节序（大端）
template <typename T>
T host_to_network(T value) {
#if ZNET_BYTE_ORDER == ZNET_LITTLE_ENDIAN
  return byteswap(value);
#else
  return value;
#endif
}

// 网络字节序（大端）转主机字节序
template <typename T>
T network_to_host(T value) {
#if ZNET_BYTE_ORDER == ZNET_LITTLE_ENDIAN
  return byteswap(value);
#else
  return value;
#endif
}

} // anonymous namespace

Buffer::Buffer(size_t initial_size)
    : buffer_(kCheapPrepend + initial_size), reader_index_(kCheapPrepend),
      writer_index_(kCheapPrepend) {}

size_t Buffer::read(void *data, size_t len) {
  size_t readable = readable_bytes();
  if (len > readable) {
    len = readable;
  }
  if (len > 0) {
    memcpy(data, peek(), len);
    retrieve(len);
  }
  return len;
}

int8_t Buffer::read_int8() {
  int8_t x = 0;
  read(&x, sizeof(x));
  return x;
}

int16_t Buffer::read_int16() {
  int16_t x = 0;
  read(&x, sizeof(x));
  return network_to_host(x);
}

int32_t Buffer::read_int32() {
  int32_t x = 0;
  read(&x, sizeof(x));
  return network_to_host(x);
}

int64_t Buffer::read_int64() {
  int64_t x = 0;
  read(&x, sizeof(x));
  return network_to_host(x);
}

std::string Buffer::read_string(size_t len) {
  size_t readable = readable_bytes();
  if (len > readable) {
    len = readable;
  }
  std::string str(peek(), len);
  retrieve(len);
  return str;
}

std::string Buffer::read_all_as_string() {
  return read_string(readable_bytes());
}

void Buffer::retrieve(size_t len) {
  if (len < readable_bytes()) {
    reader_index_ += len;
  } else {
    retrieve_all();
  }
}

void Buffer::retrieve_all() {
  reader_index_ = kCheapPrepend;
  writer_index_ = kCheapPrepend;
}


void Buffer::ensure_writable_bytes(size_t len) {
  if (writable_bytes() < len) {
    make_space(len);
  }
}

void Buffer::append(const void *data, size_t len) {
  ensure_writable_bytes(len);
  const char *d = static_cast<const char *>(data);
  std::copy(d, d + len, begin_write());
  has_written(len);
}

void Buffer::append(const std::string &str) { append(str.data(), str.size()); }

void Buffer::write_int8(int8_t x) { append(&x, sizeof(x)); }

void Buffer::write_int16(int16_t x) {
  int16_t be16 = host_to_network(x);
  append(&be16, sizeof(be16));
}

void Buffer::write_int32(int32_t x) {
  int32_t be32 = host_to_network(x);
  append(&be32, sizeof(be32));
}

void Buffer::write_int64(int64_t x) {
  int64_t be64 = host_to_network(x);
  append(&be64, sizeof(be64));
}

void Buffer::has_written(size_t len) { writer_index_ += len; }


const char *Buffer::find_crlf() const {
  const char crlf_pattern[] = "\r\n";
  const char *crlf = std::search(peek(), begin_write(), crlf_pattern, crlf_pattern + 2);
  return crlf == begin_write() ? nullptr : crlf;
}

const char *Buffer::find_eol() const {
  const char *eol =
      std::find(peek(), begin_write(), '\n');
  return eol == begin_write() ? nullptr : eol;
}


ssize_t Buffer::read_fd(int fd, int *saved_errno) {
  // 使用 readv 配合栈上缓冲区，减少系统调用和内存分配
  // 如果当前 buffer 可写空间不足，多余数据会读到栈缓冲区
  char extrabuf[65536];
  struct iovec vec[2];

  const size_t writable = writable_bytes();
  vec[0].iov_base = begin_write();
  vec[0].iov_len = writable;
  vec[1].iov_base = extrabuf;
  vec[1].iov_len = sizeof(extrabuf);

  // 当 buffer 可写空间 >= 64K 时，不使用栈缓冲区
  const int iovcnt = (writable < sizeof(extrabuf)) ? 2 : 1;
  const ssize_t n = ::readv(fd, vec, iovcnt);

  if (n < 0) {
    *saved_errno = errno;
  } else if (static_cast<size_t>(n) <= writable) {
    writer_index_ += n;
  } else {
    writer_index_ = buffer_.size();
    append(extrabuf, n - writable);
  }

  return n;
}

ssize_t Buffer::write_fd(int fd, int *saved_errno) {
  ssize_t n = ::write(fd, peek(), readable_bytes());
  if (n < 0) {
    *saved_errno = errno;
  } else {
    retrieve(n);
  }
  return n;
}

// ========== 内部辅助 ==========

void Buffer::make_space(size_t len) {
  // 如果预留空间 + 可写空间 < 需要的空间，则扩容
  // 否则将数据搬移到前面，复用预留空间
  if (writable_bytes() + prependable_bytes() < len + kCheapPrepend) {
    // 扩容
    buffer_.resize(writer_index_ + len);
  } else {
    // 搬移数据
    size_t readable = readable_bytes();
    std::copy(begin() + reader_index_, begin() + writer_index_,
              begin() + kCheapPrepend);
    reader_index_ = kCheapPrepend;
    writer_index_ = reader_index_ + readable;
  }
}

void Buffer::shrink(size_t reserve) {
  Buffer other(readable_bytes() + reserve);
  other.append(peek(), readable_bytes());
  swap(other);
}

void Buffer::swap(Buffer &rhs) {
  buffer_.swap(rhs.buffer_);
  std::swap(reader_index_, rhs.reader_index_);
  std::swap(writer_index_, rhs.writer_index_);
}

} // namespace znet
