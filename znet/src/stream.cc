#include "stream.h"

namespace znet {

int Stream::read_fix_size(void *buffer, size_t length) {
  size_t offset = 0;
  int64_t left = length;
  while (left > 0) {
    int64_t len = read((char *)buffer + offset, left);
    if (len <= 0) {
      return len;
    }
    offset += len;
    left -= len;
  }
  return length;
}

int Stream::read_fix_size(ByteArray::ptr ba, size_t length) {
  int64_t left = length;
  while (left > 0) {
    int64_t len = read(ba, left);
    if (len <= 0) {
      return len;
    }
    left -= len;
  }
  return length;
}

int Stream::write_fix_size(const void *buffer, size_t length) {
  size_t offset = 0;
  int64_t left = length;
  while (left > 0) {
    int64_t len = write((const char *)buffer + offset, left);
    if (len <= 0) {
      return len;
    }
    offset += len;
    left -= len;
  }
  return length;
}

int Stream::write_fix_size(ByteArray::ptr ba, size_t length) {
  int64_t left = length;
  while (left > 0) {
    int64_t len = write(ba, left);
    if (len <= 0) {
      return len;
    }
    left -= len;
  }
  return length;
}

} // namespace znet
