#ifndef ZNET_STREAM_H
#define ZNET_STREAM_H

#include "byte_array.h"
#include <memory>

namespace znet {

/**
 * @brief 流接口抽象
 */
class Stream {
public:
  using ptr = std::shared_ptr<Stream>;

  /**
   * @brief 析构函数
   */
  virtual ~Stream() {}

  /**
   * @brief 读数据
   * @param buffer 接收数据的内存
   * @param length 接收数据的内存大小
   * @return
   *      @retval >0 返回接收到的数据的实际大小
   *      @retval =0 被关闭
   *      @retval <0 出现流错误
   */
  virtual int read(void *buffer, size_t length) = 0;

  /**
   * @brief 读数据
   * @param ba 接收数据的 ByteArray
   * @param length 接收数据的内存大小
   * @return
   *      @retval >0 返回接收到的数据的实际大小
   *      @retval =0 被关闭
   *      @retval <0 出现流错误
   */
  virtual int read(ByteArray::ptr ba, size_t length) = 0;

  /**
   * @brief 读固定长度的数据
   * @param buffer 接收数据的内存
   * @param length 接收数据的内存大小
   * @return
   *      @retval >0 返回接收到的数据的实际大小
   *      @retval =0 被关闭
   *      @retval <0 出现流错误
   */
  virtual int read_fix_size(void *buffer, size_t length);

  /**
   * @brief 读固定长度的数据
   * @param ba 接收数据的 ByteArray
   * @param length 接收数据的内存大小
   * @return
   *      @retval >0 返回接收到的数据的实际大小
   *      @retval =0 被关闭
   *      @retval <0 出现流错误
   */
  virtual int read_fix_size(ByteArray::ptr ba, size_t length);

  /**
   * @brief 写数据
   * @param buffer 写数据的内存
   * @param length 写入数据的内存大小
   * @return
   *      @retval >0 返回写入到的数据的实际大小
   *      @retval =0 被关闭
   *      @retval <0 出现流错误
   */
  virtual int write(const void *buffer, size_t length) = 0;

  /**
   * @brief 写数据
   * @param ba 写数据的 ByteArray
   * @param length 写入数据的内存大小
   * @return
   *      @retval >0 返回写入到的数据的实际大小
   *      @retval =0 被关闭
   *      @retval <0 出现流错误
   */
  virtual int write(ByteArray::ptr ba, size_t length) = 0;

  /**
   * @brief 写固定长度的数据
   * @param buffer 写数据的内存
   * @param length 写入数据的内存大小
   * @return
   *      @retval >0 返回写入到的数据的实际大小
   *      @retval =0 被关闭
   *      @retval <0 出现流错误
   */
  virtual int write_fix_size(const void *buffer, size_t length);

  /**
   * @brief 写固定长度的数据
   * @param ba 写数据的 ByteArray
   * @param length 写入数据的内存大小
   * @return
   *      @retval >0 返回写入到的数据的实际大小
   *      @retval =0 被关闭
   *      @retval <0 出现流错误
   */
  virtual int write_fix_size(ByteArray::ptr ba, size_t length);

  /**
   * @brief 关闭流
   */
  virtual void close() = 0;
};

} // namespace znet

#endif // ZNET_STREAM_H
