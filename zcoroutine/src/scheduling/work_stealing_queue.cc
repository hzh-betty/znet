#include "scheduling/work_stealing_queue.h"

#include <chrono>
#include <utility>

namespace zcoroutine {

void WorkStealingQueue::push(Task &&task) {
  {
    std::lock_guard<Spinlock> lock(lock_);
    tasks_.push_back(std::move(task));
  }
  size_.fetch_add(1, std::memory_order_relaxed);

  // 如果有等待者则唤醒，减少无效的notify调用
  if (waiters_.load(std::memory_order_acquire) > 0) {
    cv_.notify_one();
  }
}

size_t WorkStealingQueue::pop_batch(Task *out, size_t max_count) {
  if (max_count == 0) {
    return 0;
  }

  std::lock_guard<Spinlock> lock(lock_);
  size_t n = 0;
  while (n < max_count && !tasks_.empty()) {
    out[n++] = std::move(tasks_.back());
    tasks_.pop_back();
  }
  if (n > 0) {
    size_.fetch_sub(n, std::memory_order_relaxed);
  }
  return n;
}

size_t WorkStealingQueue::steal_batch(Task *out, size_t max_count) {
  if (max_count == 0) {
    return 0;
  }

  std::lock_guard<Spinlock> lock(lock_);
  size_t n = 0;
  while (n < max_count && !tasks_.empty()) {
    out[n++] = std::move(tasks_.front());
    tasks_.pop_front();
  }
  if (n > 0) {
    size_.fetch_sub(n, std::memory_order_relaxed);
  }
  return n;
}

size_t WorkStealingQueue::wait_pop_batch(Task *out, size_t max_count,
                                        int timeout_ms) {
  if (max_count == 0) {
    return 0;
  }

  // 快速路径
  if (approx_size() == 0) {
    std::unique_lock<Spinlock> lock(lock_);
    waiters_.fetch_add(1, std::memory_order_release);

    if (timeout_ms > 0) {
      const auto deadline = std::chrono::steady_clock::now() +
                            std::chrono::milliseconds(timeout_ms);
      cv_.wait_until(lock, deadline, [this] {
        return stopped_.load(std::memory_order_relaxed) || !tasks_.empty();
      });
    } else {
      cv_.wait(lock, [this] {
        return stopped_.load(std::memory_order_relaxed) || !tasks_.empty();
      });
    }

    waiters_.fetch_sub(1, std::memory_order_release);

    // 如果超时返回，队列仍然为空则返回0
    if (tasks_.empty()) {
      return 0;
    }

    size_t n = 0;
    while (n < max_count && !tasks_.empty()) {
      out[n++] = std::move(tasks_.back());
      tasks_.pop_back();
    }
    if (n > 0) {
      size_.fetch_sub(n, std::memory_order_relaxed);
    }
    return n;
  }

  return pop_batch(out, max_count);
}

void WorkStealingQueue::stop() {
  stopped_.store(true, std::memory_order_relaxed);
  cv_.notify_all();
}

} // namespace zcoroutine
