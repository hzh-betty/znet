#ifndef ZCOROUTINE_WORK_STEALING_QUEUE_H_
#define ZCOROUTINE_WORK_STEALING_QUEUE_H_

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <deque>

#include "scheduling/task_queue.h"
#include "sync/spinlock.h"
#include "util/noncopyable.h"

namespace zcoroutine {

/**
 * @brief 每线程本地队列 + 窃取队列
 *
 * - owner 线程：push_back / pop_back（LIFO，提升缓存局部性）
 * - thief 线程：从 front 批量 steal（FIFO，减少与 owner 争用）
 *
 */
class WorkStealingQueue : public NonCopyable {
public:
  WorkStealingQueue() = default;
  ~WorkStealingQueue() = default;

  void push(Task &&task);

  /**
   * @brief owner 批量 pop（LIFO）
   * @param out 输出任务数组指针
   * @param max_count 最大取出任务数量
   * @return 实际取出的任务数量
   */
  size_t pop_batch(Task *out, size_t max_count);

  /**
   * @brief thief 批量 steal（FIFO）
   * @param out 输出任务数组指针
   * @param max_count 最大取出任务数量
   * @return 实际取出的任务数量
   */
  size_t steal_batch(Task *out, size_t max_count);

  /**
   * @brief 等待直到队列非空或停止，然后 owner 批量 pop。
   * @param out 输出任务数组指针
   * @param max_count 最大取出任务数量
   * @param timeout_ms 超时时间，单位毫秒
   * @return 实际取出的任务数量，0 表示超时或已停止且无任务。
   */
  size_t wait_pop_batch(Task *out, size_t max_count, int timeout_ms);

  /**
   * @brief 停止队列，唤醒所有等待的线程
   */
  void stop();

  /**
   * @brief 唤醒一个在 wait_pop_batch 上等待的线程（若有等待者）
   */
  void notify_one();

  /**
   * @brief 近似获取队列大小（非严格准确）
   * @return 队列中任务数量
   */
  size_t approx_size() const { return size_.load(std::memory_order_relaxed); }

  /**
   * @brief 判断队列是否为空
   * @return true表示空，false表示非空
   */
  bool empty() const { return approx_size() == 0; }

private:
  mutable Spinlock lock_;
  std::condition_variable_any cv_;
  std::deque<Task> tasks_;

  std::atomic<size_t> size_{0};
  std::atomic<int> waiters_{0};
  std::atomic<bool> stopped_{false};
};

} // namespace zcoroutine

#endif // ZCOROUTINE_WORK_STEALING_QUEUE_H_
