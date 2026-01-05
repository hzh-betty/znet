#ifndef ZNET_BYTE_ARRAY_H
#define ZNET_BYTE_ARRAY_H

#include <memory>
#include <string>
#include <stdint.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <vector>

namespace znet {

/**
 * @brief 二进制数组,提供基础类型的序列化/反序列化功能
 */
class ByteArray {
public:
  using ptr = std::shared_ptr<ByteArray>;

  /**
   * @brief ByteArray 的存储节点
   */
  struct Node {
    Node(size_t s);
    Node();
    ~Node();

    char *ptr;       // 内存块地址指针
    Node *next;      // 下一个内存块地址
    size_t size;     // 内存块大小
  };

  /**
   * @brief 使用指定长度的内存块构造 ByteArray
   * @param base_size 内存块大小
   */
  explicit ByteArray(size_t base_size = 4096);

  /**
   * @brief 析构函数
   */
  ~ByteArray();

  // ========== 写入固定长度类型 ==========
  void write_fint8(int8_t value);
  void write_fuint8(uint8_t value);
  void write_fint16(int16_t value);
  void write_fuint16(uint16_t value);
  void write_fint32(int32_t value);
  void write_fuint32(uint32_t value);
  void write_fint64(int64_t value);
  void write_fuint64(uint64_t value);

  // ========== 写入 Varint 类型 ==========
  void write_int32(int32_t value);
  void write_uint32(uint32_t value);
  void write_int64(int64_t value);
  void write_uint64(uint64_t value);

  // ========== 写入浮点类型 ==========
  void write_float(float value);
  void write_double(double value);

  // ========== 写入字符串类型 ==========
  void write_string_f16(const std::string &value);
  void write_string_f32(const std::string &value);
  void write_string_f64(const std::string &value);
  void write_string_vint(const std::string &value);
  void write_string_without_length(const std::string &value);

  // ========== 读取固定长度类型 ==========
  int8_t read_fint8();
  uint8_t read_fuint8();
  int16_t read_fint16();
  uint16_t read_fuint16();
  int32_t read_fint32();
  uint32_t read_fuint32();
  int64_t read_fint64();
  uint64_t read_fuint64();

  // ========== 读取 Varint 类型 ==========
  int32_t read_int32();
  uint32_t read_uint32();
  int64_t read_int64();
  uint64_t read_uint64();

  // ========== 读取浮点类型 ==========
  float read_float();
  double read_double();

  // ========== 读取字符串类型 ==========
  std::string read_string_f16();
  std::string read_string_f32();
  std::string read_string_f64();
  std::string read_string_vint();

  // ========== 基本操作 ==========
  void clear();
  void write(const void *buf, size_t size);
  void read(void *buf, size_t size);
  void read(void *buf, size_t size, size_t position) const;

  size_t position() const { return position_; }
  void set_position(size_t v);

  bool write_to_file(const std::string &name) const;
  bool read_from_file(const std::string &name);

  size_t base_size() const { return base_size_; }
  size_t readable_size() const { return size_ - position_; }

  bool is_little_endian() const;
  void set_is_little_endian(bool val);

  std::string to_string() const;
  std::string to_hex_string() const;

  uint64_t get_read_buffers(std::vector<iovec> &buffers,
                            uint64_t len = ~0ull) const;
  uint64_t get_read_buffers(std::vector<iovec> &buffers, uint64_t len,
                            uint64_t position) const;
  uint64_t get_write_buffers(std::vector<iovec> &buffers, uint64_t len);

  size_t size() const { return size_; }

private:
  void add_capacity(size_t size);
  size_t capacity() const { return capacity_ - position_; }

private:
  size_t base_size_;   // 内存块的大小
  size_t position_;    // 当前操作位置
  size_t capacity_;    // 当前的总容量
  size_t size_;        // 当前数据的大小
  int8_t endian_;      // 字节序,默认大端
  Node *root_;         // 第一个内存块指针
  Node *cur_;          // 当前操作的内存块指针
};

} // namespace znet

#endif // ZNET_BYTE_ARRAY_H
