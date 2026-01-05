#include "allocator.h"
#include "znet_logger.h"
#include <cstdlib>

namespace znet {

void *Allocator::allocate(size_t size) {
  if (size == 0) {
    ZNET_LOG_ERROR("Allocator::allocate failed: size is 0");
    return nullptr;
  }

  // 使用 malloc 分配内存
  // 注意：不进行 memset 清零，因为：
  // 1. 依赖内核的 COW 机制，按需分配物理页
  // 2. 避免触发大量 page fault
  void *ptr = malloc(size);
  if (!ptr) {
    ZNET_LOG_ERROR("Allocator::allocate malloc failed: requested_size={}", size);
    return nullptr;
  }

  ZNET_LOG_DEBUG("Allocator::allocate success: ptr={}, size={}", ptr, size);
  return ptr;
}

void Allocator::deallocate(void *ptr, size_t size) {
  if (!ptr) {
    ZNET_LOG_WARN("Allocator::deallocate received null pointer");
    return;
  }

  ZNET_LOG_DEBUG("Allocator::deallocate: ptr={}, size={}", ptr, size);
  free(ptr);
}

} // namespace znet
