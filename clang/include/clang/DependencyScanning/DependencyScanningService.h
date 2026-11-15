//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_DEPENDENCYSCANNING_DEPENDENCYSCANNINGSERVICE_H
#define LLVM_CLANG_DEPENDENCYSCANNING_DEPENDENCYSCANNINGSERVICE_H

#include "clang/Basic/AtomicLineLogger.h"
#include "clang/DependencyScanning/DependencyScanningFilesystem.h"
#include "clang/DependencyScanning/InProcessModuleCache.h"
#include "llvm/ADT/BitmaskEnum.h"
#include "llvm/ADT/FunctionExtras.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/Support/thread.h"

#include <condition_variable>
#include <deque>
#include <mutex>
#include <vector>

namespace clang {
namespace dependencies {

/// The mode in which the dependency scanner will operate to find the
/// dependencies.
enum class ScanningMode {
  /// This mode is used to compute the dependencies by running the preprocessor
  /// over the source files.
  CanonicalPreprocessing,

  /// This mode is used to compute the dependencies by running the preprocessor
  /// with special kind of lexing after scanning header and source files to get
  /// the minimum necessary preprocessor directives for evaluating includes.
  DependencyDirectivesScan,
};

#define DSS_LAST_BITMASK_ENUM(Id)                                              \
  LLVM_MARK_AS_BITMASK_ENUM(Id), All = llvm::NextPowerOf2(Id) - 1

enum class ScanningOptimizations {
  None = 0,

  /// Remove unused header search paths including header maps.
  HeaderSearch = 1,

  /// Remove warnings from system modules.
  SystemWarnings = (1 << 1),

  /// Remove unused -ivfsoverlay arguments.
  VFS = (1 << 2),

  /// Canonicalize -D and -U options.
  Macros = (1 << 3),

  /// Ignore the compiler's working directory if it is safe.
  IgnoreCWD = (1 << 4),

  DSS_LAST_BITMASK_ENUM(IgnoreCWD),

  // The build system needs to be aware that the current working
  // directory is ignored. Without a good way of notifying the build
  // system, it is less risky to default to off.
  Default = All & (~IgnoreCWD)
};

#undef DSS_LAST_BITMASK_ENUM

bool shouldCacheNegativeStatsDefault();

/// \return true if failed stats for files with this name should be cached,
///         false otherwise.
bool shouldCacheNegativeStatsForPath(StringRef Path);

/// The configuration knobs for the dependency scanning service.
struct DependencyScanningServiceOptions {
  DependencyScanningServiceOptions();

  /// The function invoked to create each worker's VFS. This function and the
  /// VFS itself must be thread-safe whenever using multiple workers
  /// concurrently or whenever \c AsyncScanModules is true.
  std::function<IntrusiveRefCntPtr<llvm::vfs::FileSystem>()>
      MakeVFS; // = [] { return llvm::vfs::createPhysicalFileSystem(); }
  /// Whether to use optimized dependency directive scan or full preprocessing.
  ScanningMode Mode = ScanningMode::DependencyDirectivesScan;
  /// How to optimize resulting explicit module command lines.
  ScanningOptimizations OptimizeArgs = ScanningOptimizations::Default;
  /// Whether the scanner should emit warnings.
  bool EmitWarnings = true;
  /// Whether to make reported file paths absolute.
  bool ReportAbsolutePaths = true;
  /// Whether to report modules visible from modules that are imported directly.
  bool ReportVisibleModules = false;
  /// Whether the resulting command lines should load explicit PCMs eagerly.
  bool EagerLoadModules = false;
  /// Whether to trace VFS accesses during the scan.
  bool TraceVFS = false;
  /// Whether to scan modules asynchronously.
  bool AsyncScanModules = false;
  /// The maximum number of modules compiled asynchronously at the same time,
  /// not counting the ones waiting for module locks. Zero means half the number
  /// of hardware threads.
  unsigned AsyncScanModulesThreads = 0;
  /// The build session timestamp for validate-once-per-build-session logic.
  std::time_t BuildSessionTimestamp; // = std::chrono::system_clock::now();
  /// Whether to automatically flush the module cache from memory to disk. PCMs
  /// get flushed on a background thread as soon as they're built.
  bool FlushModuleCache = true;
  /// Whether the caching VFS should cache missing filesystem entries.
  bool CacheNegativeStats = shouldCacheNegativeStatsDefault();
  /// Whether scanning modules validate their directory deps against paths
  /// reported via \c DependencyScanningService::addInvalidatedPath instead of
  /// the filesystem.
  bool ValidateAgainstInvalidatedPaths = false;
  /// The path to a log file, which logs timing of actions performed by
  /// the dependency scanner.
  std::string LogPath;
};

/// Runs asynchronous module compiles, at most a fixed number at a time.
///
/// A compile that blocks waiting for the lock of a PCM gives up its slot while
/// it waits, and another queued compile may start (on a new thread if needed).
/// So the number of running compiles is bounded, but the number of threads
/// isn't: it grows with the number of blocked compiles.
///
/// Queued compiles must not hold anything running compiles may wait for, like
/// the lock of their PCM: a compile waiting for a compile queued behind it
/// would deadlock once all slots are taken (e.g. on a long chain of imports).
/// Giving up slots while waiting would avoid the deadlock too, but this way any
/// chain of waits ends at a running compile, even without that.
class AsyncModuleCompilePool : public ModuleLockWaitListener {
public:
  /// Compiles that get cancelled together, see \c cancel().
  class Group {
    friend class AsyncModuleCompilePool;
    unsigned NumRunning = 0;
    bool Cancelled = false;
  };

  explicit AsyncModuleCompilePool(unsigned MaxActive);
  ~AsyncModuleCompilePool() override;

  /// Reserves a compile of the PCM \p Key. Returns false if a compile of the
  /// PCM is already queued.
  bool reserve(StringRef Key);

  /// Queues \p Compile of the PCM \p Key, which must have been reserved. Drops
  /// it instead if \p G has been cancelled.
  void enqueue(Group &G, StringRef Key, llvm::unique_function<void()> Compile);

  /// Drops the queued compiles of \p G, waits for the running ones to finish,
  /// and drops any compiles of \p G queued later.
  void cancel(Group &G);

  /// The module caches of the compiles must notify the pool about blocking
  /// waits (see \c makeInProcessModuleCache()), which then give up the slot.
  void willWait() override;
  void didWait() override;

private:
  struct Task {
    Group *G;
    std::string Key;
    llvm::unique_function<void()> Compile;
  };

  void work();
  /// Lets a resuming or a queued compile take a free slot, if there is one.
  void slotFreed(std::unique_lock<std::mutex> &Lock);

  const unsigned MaxActive;
  std::mutex Mutex;
  /// Notifies idle threads about new tasks, free slots, or stopping.
  std::condition_variable WorkCondVar;
  /// Notifies compiles waiting to resume about free slots.
  std::condition_variable ResumeCondVar;
  /// Notifies \c cancel() about finished tasks.
  std::condition_variable DoneCondVar;
  std::deque<Task> Queue;
  llvm::StringSet<> Queued;
  /// The number of compiles holding a slot.
  unsigned NumActive = 0;
  /// The number of compiles that finished waiting and need a slot.
  unsigned NumResuming = 0;
  /// The number of threads without a compile.
  unsigned NumIdle = 0;
  bool Stop = false;
  std::vector<llvm::thread> Threads;
};

/// The dependency scanning service contains shared configuration and state that
/// is used by the individual dependency scanning workers.
class DependencyScanningService {
public:
  explicit DependencyScanningService(DependencyScanningServiceOptions Opts)
      : Opts(std::move(Opts)), Logger(this->Opts.LogPath),
        AsyncCompilePool(this->Opts.AsyncScanModulesThreads) {
    ModCacheEntries.ValidateAgainstInvalidatedPaths =
        this->Opts.ValidateAgainstInvalidatedPaths;
    if (this->Opts.FlushModuleCache)
      ModCacheEntries.startBackgroundFlushing(Logger);
  }

  ~DependencyScanningService() {
    if (Opts.FlushModuleCache) {
      ModCacheEntries.stopBackgroundFlushing();
      // TODO: Consider allowing clients to skip this to terminate ASAP.
      ModCacheEntries.flushLeftovers(Logger);
    }
  }

  const DependencyScanningServiceOptions &getOpts() const { return Opts; }

  DependencyScanningFilesystemSharedCache &getSharedCache() {
    return SharedCache;
  }

  ModuleCacheEntries &getModuleCacheEntries() { return ModCacheEntries; }

  /// Add a path that changed since the previous scan, so that cached scanning
  /// modules depending on it are rebuilt. Requires
  /// \c ValidateAgainstInvalidatedPaths.
  ///
  /// Only directories are currently checked. \p Path is compared textually
  /// against \c ModuleDeps::DirectoryDeps, so it must be spelled the same way:
  /// absolute, with dots removed, and without resolving symlinks. It is never
  /// accessed on disk. Modules built since \c BuildSessionTimestamp are
  /// considered up to date with it.
  void addInvalidatedPath(StringRef Path) {
    assert(Opts.ValidateAgainstInvalidatedPaths &&
           "invalidated paths are only checked when validating against them");
    ModCacheEntries.addInvalidatedPath(Path);
  }

  AtomicLineLogger &getLogger() { return Logger; }

  /// The pool running asynchronous module compiles.
  AsyncModuleCompilePool &getAsyncModuleCompilePool() {
    return AsyncCompilePool;
  }

private:
  /// The options customizing dependency scanning behavior.
  DependencyScanningServiceOptions Opts;
  /// The global file system cache.
  DependencyScanningFilesystemSharedCache SharedCache;
  /// The global module cache entries.
  ModuleCacheEntries ModCacheEntries;
  /// The logger of dependency scanning actions.
  AtomicLineLogger Logger;
  /// Asynchronous module compiles. They reference everything above.
  AsyncModuleCompilePool AsyncCompilePool;
};

} // end namespace dependencies
} // end namespace clang

#endif // LLVM_CLANG_DEPENDENCYSCANNING_DEPENDENCYSCANNINGSERVICE_H
