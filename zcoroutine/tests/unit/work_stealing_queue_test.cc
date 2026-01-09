/**
 * @file work_stealing_queue_test.cc
 * @brief WorkStealingQueue 单元测试
 */

#include "scheduling/work_stealing_queue.h"
#include "util/zcoroutine_logger.h"

#include <atomic>
#include <chrono>
#include <gtest/gtest.h>
#include <mutex>
#include <thread>
#include <vector>

using namespace zcoroutine;

namespace {

Task make_id_task(int id, std::vector<int> *sink, std::mutex *sink_mutex) {
  return Task([id, sink, sink_mutex]() {
    std::lock_guard<std::mutex> lock(*sink_mutex);
    sink->push_back(id);
  });
}

} // namespace

class WorkStealingQueueTest : public ::testing::Test {
protected:
  static void SetUpTestSuite() {
    zcoroutine::init_logger(zlog::LogLevel::value::DEBUG);
  }

  void SetUp() override {}
  void TearDown() override {}
};

TEST_F(WorkStealingQueueTest, PushPopBatchLifoOrder) {
  WorkStealingQueue q;

  std::vector<int> executed;
  std::mutex executed_mutex;

  for (int i = 0; i < 8; ++i) {
    q.push(make_id_task(i, &executed, &executed_mutex));
  }

  Task out[8];
  const size_t n = q.pop_batch(out, 8);
  ASSERT_EQ(n, 8u);

  for (size_t i = 0; i < n; ++i) {
    ASSERT_TRUE(out[i].is_valid());
    ASSERT_TRUE(static_cast<bool>(out[i].callback));
    out[i].callback();
    out[i].reset();
  }

  // owner pop_back: LIFO
  ASSERT_EQ(executed.size(), 8u);
  for (int i = 0; i < 8; ++i) {
    EXPECT_EQ(executed[static_cast<size_t>(i)], 7 - i);
  }

  EXPECT_TRUE(q.empty());
  EXPECT_EQ(q.approx_size(), 0u);
}

TEST_F(WorkStealingQueueTest, StealBatchFifoOrder) {
  WorkStealingQueue q;

  std::vector<int> executed;
  std::mutex executed_mutex;

  for (int i = 0; i < 10; ++i) {
    q.push(make_id_task(i, &executed, &executed_mutex));
  }

  Task out[4];
  const size_t n = q.steal_batch(out, 4);
  ASSERT_EQ(n, 4u);

  for (size_t i = 0; i < n; ++i) {
    ASSERT_TRUE(out[i].is_valid());
    out[i].callback();
    out[i].reset();
  }

  // thief pop_front: FIFO
  ASSERT_EQ(executed.size(), 4u);
  for (int i = 0; i < 4; ++i) {
    EXPECT_EQ(executed[static_cast<size_t>(i)], i);
  }

  EXPECT_EQ(q.approx_size(), 6u);
}

TEST_F(WorkStealingQueueTest, ApproxSizeTracksPushPopSteal) {
  WorkStealingQueue q;

  std::vector<int> executed;
  std::mutex executed_mutex;

  EXPECT_EQ(q.approx_size(), 0u);

  for (int i = 0; i < 5; ++i) {
    q.push(make_id_task(i, &executed, &executed_mutex));
  }
  EXPECT_EQ(q.approx_size(), 5u);

  Task out[2];
  EXPECT_EQ(q.pop_batch(out, 2), 2u);
  EXPECT_EQ(q.approx_size(), 3u);

  EXPECT_EQ(q.steal_batch(out, 2), 2u);
  EXPECT_EQ(q.approx_size(), 1u);

  EXPECT_EQ(q.pop_batch(out, 2), 1u);
  EXPECT_EQ(q.approx_size(), 0u);
  EXPECT_TRUE(q.empty());
}

TEST_F(WorkStealingQueueTest, WaitPopBatchTimeoutReturnsZero) {
  WorkStealingQueue q;
  Task out[1];

  auto start = std::chrono::steady_clock::now();
  const size_t n = q.wait_pop_batch(out, 1, 30);
  auto elapsed = std::chrono::steady_clock::now() - start;

  EXPECT_EQ(n, 0u);
  // 不做严格下限断言（避免 CI/系统抖动），只要别卡死即可。
  EXPECT_LT(std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count(),
            500);
}

TEST_F(WorkStealingQueueTest, WaitPopBatchWakesOnPush) {
  WorkStealingQueue q;
  std::atomic<int> value{0};

  std::thread waiter([&]() {
    Task out[1];
    const size_t n = q.wait_pop_batch(out, 1, 1000);
    ASSERT_EQ(n, 1u);
    ASSERT_TRUE(out[0].is_valid());
    ASSERT_TRUE(static_cast<bool>(out[0].callback));
    out[0].callback();
  });

  // 确保 waiter 进入 wait（尽量减少竞态）
  std::this_thread::sleep_for(std::chrono::milliseconds(20));

  q.push(Task([&]() { value.store(123, std::memory_order_relaxed); }));

  waiter.join();
  EXPECT_EQ(value.load(std::memory_order_relaxed), 123);
}

TEST_F(WorkStealingQueueTest, StopUnblocksWaiters) {
  WorkStealingQueue q;
  std::atomic<bool> finished{false};

  std::thread waiter([&]() {
    Task out[1];
    const size_t n = q.wait_pop_batch(out, 1, 0); // 永久等待
    EXPECT_EQ(n, 0u);
    finished.store(true, std::memory_order_relaxed);
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(30));
  q.stop();

  waiter.join();
  EXPECT_TRUE(finished.load(std::memory_order_relaxed));
}

TEST_F(WorkStealingQueueTest, StopDoesNotDiscardExistingTasks) {
  WorkStealingQueue q;
  std::atomic<int> value{0};

  q.push(Task([&]() { value.fetch_add(1, std::memory_order_relaxed); }));
  q.stop();

  Task out[1];
  const size_t n = q.wait_pop_batch(out, 1, 0);
  ASSERT_EQ(n, 1u);
  out[0].callback();

  EXPECT_EQ(value.load(std::memory_order_relaxed), 1);
}

TEST_F(WorkStealingQueueTest, ConcurrentOwnerAndThiefConsumeAllExactlyOnce) {
  WorkStealingQueue q;

  constexpr int kTaskCount = 2000;
  auto seen = std::make_unique<std::atomic<int>[]>(kTaskCount);
  for (int i = 0; i < kTaskCount; ++i) {
    seen[i].store(0, std::memory_order_relaxed);
  }

  std::atomic<int> processed{0};
  std::atomic<bool> producer_done{false};

  auto consume_task = [&](Task &t) {
    if (!t.is_valid() || !t.callback) {
      return;
    }
    t.callback();
    t.reset();
  };

  std::thread producer([&]() {
    for (int id = 0; id < kTaskCount; ++id) {
      q.push(Task([&, id]() {
        seen[id].fetch_add(1, std::memory_order_relaxed);
        processed.fetch_add(1, std::memory_order_relaxed);
      }));
      if ((id % 200) == 0) {
        std::this_thread::yield();
      }
    }
    producer_done.store(true, std::memory_order_release);
  });

  std::thread owner([&]() {
    Task buf[16];
    while (true) {
      if (processed.load(std::memory_order_relaxed) >= kTaskCount) {
        break;
      }

      size_t n = q.pop_batch(buf, 16);
      if (n == 0) {
        n = q.wait_pop_batch(buf, 16, 10);
      }
      if (n == 0) {
        if (producer_done.load(std::memory_order_acquire) && q.empty()) {
          break;
        }
        continue;
      }
      for (size_t i = 0; i < n; ++i) {
        consume_task(buf[i]);
      }
    }
  });

  std::thread thief([&]() {
    Task buf[8];
    while (true) {
      if (processed.load(std::memory_order_relaxed) >= kTaskCount) {
        break;
      }

      const size_t n = q.steal_batch(buf, 8);
      if (n == 0) {
        if (producer_done.load(std::memory_order_acquire) && q.empty()) {
          break;
        }
        std::this_thread::yield();
        continue;
      }
      for (size_t i = 0; i < n; ++i) {
        consume_task(buf[i]);
      }
    }
  });

  producer.join();
  owner.join();
  thief.join();

  EXPECT_EQ(processed.load(std::memory_order_relaxed), kTaskCount);
  for (int i = 0; i < kTaskCount; ++i) {
    EXPECT_EQ(seen[i].load(std::memory_order_relaxed), 1);
  }
}

int main(int argc, char **argv) {
  zcoroutine::init_logger(zlog::LogLevel::value::DEBUG);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
