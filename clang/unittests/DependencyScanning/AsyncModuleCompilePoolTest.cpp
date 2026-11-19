//===- AsyncModuleCompilePoolTest.cpp -------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "clang/DependencyScanning/DependencyScanningService.h"

#include "gtest/gtest.h"

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <functional>
#include <string>
#include <thread>

using namespace clang;
using namespace clang::dependencies;

namespace {
/// A one-shot event a compile can block on, notifying the pool like the
/// in-process module cache does around lock waits.
class Event {
  std::mutex Mutex;
  std::condition_variable CondVar;
  bool Signaled = false;

public:
  void signal() {
    {
      std::lock_guard<std::mutex> Lock(Mutex);
      Signaled = true;
    }
    CondVar.notify_all();
  }

  void wait(ModuleLockWaitListener &Listener) {
    Listener.willWait();
    {
      std::unique_lock<std::mutex> Lock(Mutex);
      CondVar.wait(Lock, [&] { return Signaled; });
    }
    Listener.didWait();
  }
};

/// For waits outside of the pool.
struct NoWaitListener : ModuleLockWaitListener {
  void willWait() override {}
  void didWait() override {}
};

/// Tracks how many compiles are active at once.
struct ActiveCounter {
  std::atomic<unsigned> Active = 0;
  std::atomic<unsigned> MaxActive = 0;

  void enter() {
    unsigned Now = ++Active;
    unsigned Max = MaxActive;
    while (Now > Max && !MaxActive.compare_exchange_weak(Max, Now))
      ;
  }
  void leave() { --Active; }
};
} // namespace

// A chain of compiles, each waiting for the next one, which it queues itself.
// That's much longer than the number of slots: the compiles must give up their
// slots while they wait, or the chain deadlocks.
TEST(AsyncModuleCompilePool, ChainLongerThanPool) {
  constexpr unsigned Depth = 64;
  AsyncModuleCompilePool Pool(/*MaxActive=*/2);
  AsyncModuleCompilePool::Group G;
  Event Done[Depth];
  ActiveCounter Counter;

  std::function<void(unsigned)> Enqueue = [&](unsigned I) {
    std::string Key = "M" + std::to_string(I);
    ASSERT_TRUE(Pool.reserve(Key));
    Pool.enqueue(G, Key, [&, I] {
      Counter.enter();
      if (I + 1 < Depth) {
        Enqueue(I + 1);
        Counter.leave();
        Done[I + 1].wait(Pool);
        Counter.enter();
      }
      Counter.leave();
      Done[I].signal();
    });
  };
  Enqueue(0);
  NoWaitListener NoListener;
  Done[0].wait(NoListener);
  Pool.cancel(G);
  EXPECT_LE(Counter.MaxActive, 2u);
}

// Queued compiles get deduplicated by their key until they start.
TEST(AsyncModuleCompilePool, Reserve) {
  AsyncModuleCompilePool Pool(/*MaxActive=*/1);
  AsyncModuleCompilePool::Group G;
  Event Started, Release;
  NoWaitListener NoListener;

  ASSERT_TRUE(Pool.reserve("A"));
  Pool.enqueue(G, "A", [&] {
    Started.signal();
    Release.wait(NoListener);
  });
  Started.wait(NoListener);
  // A is running, so it can be queued again.
  EXPECT_TRUE(Pool.reserve("A"));
  Pool.enqueue(G, "A", [] {});
  // The second A is queued behind the first one.
  EXPECT_FALSE(Pool.reserve("A"));
  Release.signal();
  Pool.cancel(G);
}

// Cancelling a group drops its queued compiles and waits for the running ones.
TEST(AsyncModuleCompilePool, Cancel) {
  AsyncModuleCompilePool Pool(/*MaxActive=*/1);
  AsyncModuleCompilePool::Group G1, G2;
  Event Started, Release;
  NoWaitListener NoListener;
  std::atomic<bool> RanA = false, RanB = false, RanC = false;

  ASSERT_TRUE(Pool.reserve("A"));
  Pool.enqueue(G1, "A", [&] {
    Started.signal();
    Release.wait(NoListener);
    RanA = true;
  });
  Started.wait(NoListener);
  // The only slot is taken, so B stays queued.
  ASSERT_TRUE(Pool.reserve("B"));
  Pool.enqueue(G2, "B", [&] { RanB = true; });
  Pool.cancel(G2);
  EXPECT_FALSE(RanB);
  // B isn't queued anymore.
  EXPECT_TRUE(Pool.reserve("B"));
  // Compiles queued after cancelling get dropped.
  Pool.enqueue(G2, "B", [&] { RanB = true; });

  std::thread Releaser([&] {
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    Release.signal();
  });
  Pool.cancel(G1);
  EXPECT_TRUE(RanA);
  Releaser.join();

  AsyncModuleCompilePool::Group G3;
  ASSERT_TRUE(Pool.reserve("C"));
  Pool.enqueue(G3, "C", [&] { RanC = true; });
  // With a single slot, D runs after C.
  Event CDone;
  ASSERT_TRUE(Pool.reserve("D"));
  Pool.enqueue(G3, "D", [&] { CDone.signal(); });
  CDone.wait(NoListener);
  Pool.cancel(G3);
  EXPECT_TRUE(RanC);
  EXPECT_FALSE(RanB);
}
