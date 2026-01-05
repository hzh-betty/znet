#include "byte_array.h"
#include "znet_endian.h"
#include "znet_logger.h"
#include <cmath>
#include <iomanip>
#include <sstream>


namespace znet {

// ========== Node 实现 ==========

ByteArray::Node::Node(size_t s) : ptr(new char[s]), next(nullptr), size(s) {}

ByteArray::Node::Node() : ptr(nullptr), next(nullptr), size(0) {}

ByteArray::Node::~Node() {
  if (ptr) {
    delete[] ptr;
  }
}

// ========== ByteArray 构造/析构 ==========

ByteArray::ByteArray(size_t base_size)
    : base_size_(base_size), position_(0), capacity_(base_size), size_(0),
      endian_(ZNET_BIG_ENDIAN), root_(new Node(base_size)), cur_(root_) {}

ByteArray::~ByteArray() {
  Node *tmp = root_;
  while (tmp) {
    cur_ = tmp;
    tmp = tmp->next;
    delete cur_;
  }
}

// ========== 字节序 ==========

bool ByteArray::is_little_endian() const {
  return endian_ == ZNET_LITTLE_ENDIAN;
}

void ByteArray::set_is_little_endian(bool val) {
  if (val) {
    endian_ = ZNET_LITTLE_ENDIAN;
  } else {
    endian_ = ZNET_BIG_ENDIAN;
  }
}

// ========== 写入固定长度类型 ==========

void ByteArray::write_fint8(int8_t value) { write(&value, sizeof(value)); }

void ByteArray::write_fuint8(uint8_t value) { write(&value, sizeof(value)); }

void ByteArray::write_fint16(int16_t value) {
  if (endian_ != ZNET_BYTE_ORDER) {
    value = byteswap(value);
  }
  write(&value, sizeof(value));
}

void ByteArray::write_fuint16(uint16_t value) {
  if (endian_ != ZNET_BYTE_ORDER) {
    value = byteswap(value);
  }
  write(&value, sizeof(value));
}

void ByteArray::write_fint32(int32_t value) {
  if (endian_ != ZNET_BYTE_ORDER) {
    value = byteswap(value);
  }
  write(&value, sizeof(value));
}

void ByteArray::write_fuint32(uint32_t value) {
  if (endian_ != ZNET_BYTE_ORDER) {
    value = byteswap(value);
  }
  write(&value, sizeof(value));
}

void ByteArray::write_fint64(int64_t value) {
  if (endian_ != ZNET_BYTE_ORDER) {
    value = byteswap(value);
  }
  write(&value, sizeof(value));
}

void ByteArray::write_fuint64(uint64_t value) {
  if (endian_ != ZNET_BYTE_ORDER) {
    value = byteswap(value);
  }
  write(&value, sizeof(value));
}

// ========== Zigzag 编码/解码 ==========

static uint32_t encode_zigzag32(const int32_t &v) {
  if (v < 0) {
    return ((uint32_t)(-v)) * 2 - 1;
  } else {
    return v * 2;
  }
}

static uint64_t encode_zigzag64(const int64_t &v) {
  if (v < 0) {
    return ((uint64_t)(-v)) * 2 - 1;
  } else {
    return v * 2;
  }
}

static int32_t decode_zigzag32(const uint32_t &v) {
  return (v >> 1) ^ -(v & 1);
}

static int64_t decode_zigzag64(const uint64_t &v) {
  return (v >> 1) ^ -(v & 1);
}


void ByteArray::write_int32(int32_t value) {
  write_uint32(encode_zigzag32(value));
}

void ByteArray::write_uint32(uint32_t value) {
  uint8_t tmp[5];
  uint8_t i = 0;
  while (value >= 0x80) {
    tmp[i++] = (value & 0x7F) | 0x80;
    value >>= 7;
  }
  tmp[i++] = value;
  write(tmp, i);
}

void ByteArray::write_int64(int64_t value) {
  write_uint64(encode_zigzag64(value));
}

void ByteArray::write_uint64(uint64_t value) {
  uint8_t tmp[10];
  uint8_t i = 0;
  while (value >= 0x80) {
    tmp[i++] = (value & 0x7F) | 0x80;
    value >>= 7;
  }
  tmp[i++] = value;
  write(tmp, i);
}


void ByteArray::write_float(float value) {
  uint32_t v;
  memcpy(&v, &value, sizeof(value));
  write_fuint32(v);
}

void ByteArray::write_double(double value) {
  uint64_t v;
  memcpy(&v, &value, sizeof(value));
  write_fuint64(v);
}


void ByteArray::write_string_f16(const std::string &value) {
  write_fuint16(value.size());
  write(value.c_str(), value.size());
}

void ByteArray::write_string_f32(const std::string &value) {
  write_fuint32(value.size());
  write(value.c_str(), value.size());
}

void ByteArray::write_string_f64(const std::string &value) {
  write_fuint64(value.size());
  write(value.c_str(), value.size());
}

void ByteArray::write_string_vint(const std::string &value) {
  write_uint64(value.size());
  write(value.c_str(), value.size());
}

void ByteArray::write_string_without_length(const std::string &value) {
  write(value.c_str(), value.size());
}


int8_t ByteArray::read_fint8() {
  int8_t v;
  read(&v, sizeof(v));
  return v;
}

uint8_t ByteArray::read_fuint8() {
  uint8_t v;
  read(&v, sizeof(v));
  return v;
}

int16_t ByteArray::read_fint16() {
  int16_t v;
  read(&v, sizeof(v));
  if (endian_ == ZNET_BYTE_ORDER) {
    return v;
  } else {
    return byteswap(v);
  }
}

uint16_t ByteArray::read_fuint16() {
  uint16_t v;
  read(&v, sizeof(v));
  if (endian_ == ZNET_BYTE_ORDER) {
    return v;
  } else {
    return byteswap(v);
  }
}

int32_t ByteArray::read_fint32() {
  int32_t v;
  read(&v, sizeof(v));
  if (endian_ == ZNET_BYTE_ORDER) {
    return v;
  } else {
    return byteswap(v);
  }
}

uint32_t ByteArray::read_fuint32() {
  uint32_t v;
  read(&v, sizeof(v));
  if (endian_ == ZNET_BYTE_ORDER) {
    return v;
  } else {
    return byteswap(v);
  }
}

int64_t ByteArray::read_fint64() {
  int64_t v;
  read(&v, sizeof(v));
  if (endian_ == ZNET_BYTE_ORDER) {
    return v;
  } else {
    return byteswap(v);
  }
}

uint64_t ByteArray::read_fuint64() {
  uint64_t v;
  read(&v, sizeof(v));
  if (endian_ == ZNET_BYTE_ORDER) {
    return v;
  } else {
    return byteswap(v);
  }
}


int32_t ByteArray::read_int32() { return decode_zigzag32(read_uint32()); }

uint32_t ByteArray::read_uint32() {
  uint32_t result = 0;
  for (int i = 0; i < 32; i += 7) {
    uint8_t b = read_fuint8();
    if (b < 0x80) {
      result |= ((uint32_t)b) << i;
      break;
    } else {
      result |= (((uint32_t)(b & 0x7f)) << i);
    }
  }
  return result;
}

int64_t ByteArray::read_int64() { return decode_zigzag64(read_uint64()); }

uint64_t ByteArray::read_uint64() {
  uint64_t result = 0;
  for (int i = 0; i < 64; i += 7) {
    uint8_t b = read_fuint8();
    if (b < 0x80) {
      result |= ((uint64_t)b) << i;
      break;
    } else {
      result |= (((uint64_t)(b & 0x7f)) << i);
    }
  }
  return result;
}


float ByteArray::read_float() {
  uint32_t v = read_fuint32();
  float value;
  memcpy(&value, &v, sizeof(v));
  return value;
}

double ByteArray::read_double() {
  uint64_t v = read_fuint64();
  double value;
  memcpy(&value, &v, sizeof(v));
  return value;
}


std::string ByteArray::read_string_f16() {
  uint16_t len = read_fuint16();
  std::string buff;
  buff.resize(len);
  read(&buff[0], len);
  return buff;
}

std::string ByteArray::read_string_f32() {
  uint32_t len = read_fuint32();
  std::string buff;
  buff.resize(len);
  read(&buff[0], len);
  return buff;
}

std::string ByteArray::read_string_f64() {
  uint64_t len = read_fuint64();
  std::string buff;
  buff.resize(len);
  read(&buff[0], len);
  return buff;
}

std::string ByteArray::read_string_vint() {
  uint64_t len = read_uint64();
  std::string buff;
  buff.resize(len);
  read(&buff[0], len);
  return buff;
}

// ========== 基本操作 ==========

void ByteArray::clear() {
  position_ = size_ = 0;
  capacity_ = base_size_;
  Node *tmp = root_->next;
  while (tmp) {
    cur_ = tmp;
    tmp = tmp->next;
    delete cur_;
  }
  cur_ = root_;
  root_->next = nullptr;
}

void ByteArray::write(const void *buf, size_t size) {
  if (size == 0) {
    return;
  }
  add_capacity(size);

  size_t npos = position_ % base_size_;
  size_t ncap = cur_->size - npos;
  size_t bpos = 0;

  while (size > 0) {
    if (ncap >= size) {
      memcpy(cur_->ptr + npos, (const char *)buf + bpos, size);
      if (cur_->size == (npos + size)) {
        cur_ = cur_->next;
      }
      position_ += size;
      bpos += size;
      size = 0;
    } else {
      memcpy(cur_->ptr + npos, (const char *)buf + bpos, ncap);
      position_ += ncap;
      bpos += ncap;
      size -= ncap;
      cur_ = cur_->next;
      ncap = cur_->size;
      npos = 0;
    }
  }

  if (position_ > size_) {
    size_ = position_;
  }
}

void ByteArray::read(void *buf, size_t size) {
  if (size > readable_size()) {
    throw std::out_of_range("not enough len");
  }

  size_t npos = position_ % base_size_;
  size_t ncap = cur_->size - npos;
  size_t bpos = 0;
  while (size > 0) {
    if (ncap >= size) {
      memcpy((char *)buf + bpos, cur_->ptr + npos, size);
      if (cur_->size == (npos + size)) {
        cur_ = cur_->next;
      }
      position_ += size;
      bpos += size;
      size = 0;
    } else {
      memcpy((char *)buf + bpos, cur_->ptr + npos, ncap);
      position_ += ncap;
      bpos += ncap;
      size -= ncap;
      cur_ = cur_->next;
      ncap = cur_->size;
      npos = 0;
    }
  }
}

void ByteArray::read(void *buf, size_t size, size_t position) const {
  if (size > (size_ - position)) {
    throw std::out_of_range("not enough len");
  }

  size_t npos = position % base_size_;
  size_t ncap = cur_->size - npos;
  size_t bpos = 0;
  Node *cur = cur_;
  while (size > 0) {
    if (ncap >= size) {
      memcpy((char *)buf + bpos, cur->ptr + npos, size);
      if (cur->size == (npos + size)) {
        cur = cur->next;
      }
      position += size;
      bpos += size;
      size = 0;
    } else {
      memcpy((char *)buf + bpos, cur->ptr + npos, ncap);
      position += ncap;
      bpos += ncap;
      size -= ncap;
      cur = cur->next;
      ncap = cur->size;
      npos = 0;
    }
  }
}

void ByteArray::set_position(size_t v) {
  if (v > capacity_) {
    throw std::out_of_range("set_position out of range");
  }
  position_ = v;
  if (position_ > size_) {
    size_ = position_;
  }
  cur_ = root_;
  while (v > cur_->size) {
    v -= cur_->size;
    cur_ = cur_->next;
  }
  if (v == cur_->size) {
    cur_ = cur_->next;
  }
}

bool ByteArray::write_to_file(const std::string &name) const {
  std::ofstream ofs;
  ofs.open(name, std::ios::trunc | std::ios::binary);
  if (!ofs) {
    ZNET_LOG_ERROR("ByteArray::write_to_file name={} error, errno={} errstr={}",
                   name, errno, strerror(errno));
    return false;
  }

  int64_t read_size = readable_size();
  int64_t pos = position_;
  Node *cur = cur_;

  while (read_size > 0) {
    int diff = pos % base_size_;
    int64_t len =
        (read_size > (int64_t)base_size_ ? base_size_ : read_size) - diff;
    ofs.write(cur->ptr + diff, len);
    cur = cur->next;
    pos += len;
    read_size -= len;
  }

  return true;
}

bool ByteArray::read_from_file(const std::string &name) {
  std::ifstream ifs;
  ifs.open(name, std::ios::binary);
  if (!ifs) {
    ZNET_LOG_ERROR("ByteArray::read_from_file name={} error, errno={} errstr={}",
                   name, errno, strerror(errno));
    return false;
  }

  std::shared_ptr<char> buff(new char[base_size_],
                             [](char *ptr) { delete[] ptr; });
  while (!ifs.eof()) {
    ifs.read(buff.get(), base_size_);
    write(buff.get(), ifs.gcount());
  }
  return true;
}

void ByteArray::add_capacity(size_t size) {
  if (size == 0) {
    return;
  }
  size_t old_cap = capacity();
  if (old_cap >= size) {
    return;
  }

  size = size - old_cap;
  size_t count = std::ceil(1.0 * size / base_size_);
  Node *tmp = root_;
  while (tmp->next) {
    tmp = tmp->next;
  }

  Node *first = nullptr;
  for (size_t i = 0; i < count; ++i) {
    tmp->next = new Node(base_size_);
    if (first == nullptr) {
      first = tmp->next;
    }
    tmp = tmp->next;
    capacity_ += base_size_;
  }

  if (old_cap == 0) {
    cur_ = first;
  }
}

std::string ByteArray::to_string() const {
  std::string str;
  str.resize(readable_size());
  if (str.empty()) {
    return str;
  }
  read(&str[0], str.size(), position_);
  return str;
}

std::string ByteArray::to_hex_string() const {
  std::string str = to_string();
  std::stringstream ss;

  for (size_t i = 0; i < str.size(); ++i) {
    if (i > 0 && i % 32 == 0) {
      ss << std::endl;
    }
    ss << std::setw(2) << std::setfill('0') << std::hex
       << (int)(uint8_t)str[i] << " ";
  }

  return ss.str();
}

uint64_t ByteArray::get_read_buffers(std::vector<iovec> &buffers,
                                     uint64_t len) const {
  len = len > readable_size() ? readable_size() : len;
  if (len == 0) {
    return 0;
  }

  uint64_t size = len;

  size_t npos = position_ % base_size_;
  size_t ncap = cur_->size - npos;
  struct iovec iov;
  Node *cur = cur_;

  while (len > 0) {
    if (ncap >= len) {
      iov.iov_base = cur->ptr + npos;
      iov.iov_len = len;
      len = 0;
    } else {
      iov.iov_base = cur->ptr + npos;
      iov.iov_len = ncap;
      len -= ncap;
      cur = cur->next;
      ncap = cur->size;
      npos = 0;
    }
    buffers.push_back(iov);
  }
  return size;
}

uint64_t ByteArray::get_read_buffers(std::vector<iovec> &buffers, uint64_t len,
                                     uint64_t position) const {
  len = len > readable_size() ? readable_size() : len;
  if (len == 0) {
    return 0;
  }

  uint64_t size = len;
  size_t npos = position % base_size_;
  size_t count = position / base_size_;
  Node *cur = root_;
  while (count > 0) {
    cur = cur->next;
    --count;
  }

  size_t ncap = cur->size - npos;
  struct iovec iov;
  while (len > 0) {
    if (ncap >= len) {
      iov.iov_base = cur->ptr + npos;
      iov.iov_len = len;
      len = 0;
    } else {
      iov.iov_base = cur->ptr + npos;
      iov.iov_len = ncap;
      len -= ncap;
      cur = cur->next;
      ncap = cur->size;
      npos = 0;
    }
    buffers.push_back(iov);
  }
  return size;
}

uint64_t ByteArray::get_write_buffers(std::vector<iovec> &buffers,
                                      uint64_t len) {
  if (len == 0) {
    return 0;
  }
  add_capacity(len);
  uint64_t size = len;

  size_t npos = position_ % base_size_;
  size_t ncap = cur_->size - npos;
  struct iovec iov;
  Node *cur = cur_;
  while (len > 0) {
    if (ncap >= len) {
      iov.iov_base = cur->ptr + npos;
      iov.iov_len = len;
      len = 0;
    } else {
      iov.iov_base = cur->ptr + npos;
      iov.iov_len = ncap;

      len -= ncap;
      cur = cur->next;
      ncap = cur->size;
      npos = 0;
    }
    buffers.push_back(iov);
  }
  return size;
}

} // namespace znet
