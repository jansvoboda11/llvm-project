//===- DependencyScanningService.cpp - Scanning Service -------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "clang/DependencyScanning/DependencyScanningService.h"

#include "clang/Basic/Stack.h"
#include "llvm/Support/Chrono.h"
#include "llvm/Support/Process.h"
#include "llvm/Support/Threading.h"

using namespace clang;
using namespace dependencies;

bool dependencies::shouldCacheNegativeStatsDefault() {
  if (std::optional<std::string> MaybeNegStats =
          llvm::sys::Process::GetEnv("CLANG_SCAN_CACHE_NEGATIVE_STATS")) {
    if (MaybeNegStats->empty())
      return true;
    return llvm::StringSwitch<bool>(*MaybeNegStats)
        .Case("1", true)
        .Case("0", false)
        .Default(false);
  }
  return false;
}

// Negative caching directories can cause build failures due to incorrectly
// configured projects.
bool dependencies::shouldCacheNegativeStatsForPath(StringRef Path) {
  StringRef Ext = llvm::sys::path::extension(Path);
  if (Ext.empty())
    return false;
  if (Ext == ".framework")
    return false;
  return true;
}

DependencyScanningServiceOptions::DependencyScanningServiceOptions()
    : MakeVFS([] { return llvm::vfs::createPhysicalFileSystem(); }),
      BuildSessionTimestamp(
          llvm::sys::toTimeT(std::chrono::system_clock::now())) {}

static unsigned getMaxActiveAsyncCompiles(unsigned MaxActive) {
  if (MaxActive)
    return MaxActive;
  // The scanning workers compile modules too.
  return std::max(1u, llvm::hardware_concurrency().compute_thread_count() / 2);
}

AsyncModuleCompilePool::AsyncModuleCompilePool(unsigned MaxActive)
    : MaxActive(getMaxActiveAsyncCompiles(MaxActive)) {}

AsyncModuleCompilePool::~AsyncModuleCompilePool() {
  {
    std::lock_guard<std::mutex> Lock(Mutex);
    assert(Queue.empty() && NumActive == 0 && "Uncancelled compiles");
    Stop = true;
  }
  WorkCondVar.notify_all();
  for (llvm::thread &Thread : Threads)
    Thread.join();
}

bool AsyncModuleCompilePool::reserve(StringRef Key) {
  std::lock_guard<std::mutex> Lock(Mutex);
  return Queued.insert(Key).second;
}

void AsyncModuleCompilePool::enqueue(Group &G, StringRef Key,
                                     llvm::unique_function<void()> Compile) {
  std::unique_lock<std::mutex> Lock(Mutex);
  assert(Queued.contains(Key) && "Compile not reserved");
  if (G.Cancelled) {
    Queued.erase(Key);
    Lock.unlock();
    // Destroys the dropped compile outside the lock.
    Compile = nullptr;
    return;
  }
  Queue.push_back({&G, Key.str(), std::move(Compile)});
  slotFreed(Lock);
}

void AsyncModuleCompilePool::cancel(Group &G) {
  std::vector<Task> Dropped;
  std::unique_lock<std::mutex> Lock(Mutex);
  G.Cancelled = true;
  for (auto It = Queue.begin(); It != Queue.end();) {
    if (It->G == &G) {
      Queued.erase(It->Key);
      Dropped.push_back(std::move(*It));
      It = Queue.erase(It);
    } else {
      ++It;
    }
  }
  DoneCondVar.wait(Lock, [&] { return G.NumRunning == 0; });
  Lock.unlock();
  // Destroys the dropped compiles outside the lock.
  Dropped.clear();
}

bool AsyncModuleCompilePool::hasQueuedCompiles() {
  std::lock_guard<std::mutex> Lock(Mutex);
  return !Queue.empty();
}

void AsyncModuleCompilePool::willWait() {
  std::unique_lock<std::mutex> Lock(Mutex);
  assert(NumActive > 0 && "Waiting without a slot");
  --NumActive;
  slotFreed(Lock);
}

void AsyncModuleCompilePool::didWait() {
  std::unique_lock<std::mutex> Lock(Mutex);
  ++NumResuming;
  ResumeCondVar.wait(Lock, [&] { return NumActive < MaxActive; });
  --NumResuming;
  ++NumActive;
}

void AsyncModuleCompilePool::slotFreed(std::unique_lock<std::mutex> &Lock) {
  // Compiles that resume hold locks others may wait for, so they go first.
  if (NumResuming)
    ResumeCondVar.notify_all();
  if (Queue.empty() || NumActive + NumResuming >= MaxActive)
    return;
  if (NumIdle) {
    WorkCondVar.notify_one();
    return;
  }
  ++NumIdle;
  Threads.emplace_back(std::optional<unsigned>(DesiredStackSize),
                       [this] { work(); });
}

void AsyncModuleCompilePool::work() {
  std::unique_lock<std::mutex> Lock(Mutex);
  while (true) {
    WorkCondVar.wait(Lock, [&] {
      return (Stop && Queue.empty()) ||
             (!Queue.empty() && NumActive + NumResuming < MaxActive);
    });
    if (Queue.empty()) {
      --NumIdle;
      return;
    }
    Task T = std::move(Queue.front());
    Queue.pop_front();
    Queued.erase(T.Key);
    --NumIdle;
    ++NumActive;
    ++T.G->NumRunning;
    Lock.unlock();
    T.Compile();
    T.Compile = nullptr;
    Lock.lock();
    ++NumIdle;
    --NumActive;
    if (--T.G->NumRunning == 0 && T.G->Cancelled)
      DoneCondVar.notify_all();
    slotFreed(Lock);
  }
}
