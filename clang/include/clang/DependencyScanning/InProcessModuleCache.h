//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_DEPENDENCYSCANNING_INPROCESSMODULECACHE_H
#define LLVM_CLANG_DEPENDENCYSCANNING_INPROCESSMODULECACHE_H

#include "clang/Basic/AtomicLineLogger.h"
#include "clang/Serialization/ModuleCache.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringSet.h"

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <optional>
#include <vector>

namespace llvm {
class MemoryBuffer;
} // namespace llvm

namespace clang {
namespace dependencies {

struct ModuleCacheEntry {
  std::mutex Mutex;
  std::condition_variable CondVar;
  bool Locked = false;
  unsigned Generation = 0;

  std::atomic<std::time_t> Timestamp = 0;

  enum {
    S_Unknown,
    S_Read,
    /// Written to the module cache, but only kept in memory.
    S_Written,
    /// Written to the module cache, then spilled to disk.
    S_Spilled,
  } State = S_Unknown;
  /// The buffer we've read from disk or written to the module cache, if any.
  /// Buffers returned from \c ModuleCache::read() share ownership of this, so
  /// it can be replaced while readers still reference the previous contents.
  std::shared_ptr<const llvm::MemoryBuffer> Buffer;
  /// The modification time of the entry.
  time_t ModTime = 0;
  /// The value of \c ModuleCacheEntries::AccessClock at the last access.
  std::atomic<uint64_t> LastAccess = 0;
};

using ModuleCacheMapEntry =
    llvm::StringMapEntry<std::unique_ptr<ModuleCacheEntry>>;

struct ModuleCacheEntries {
  std::mutex Mutex;
  llvm::StringMap<std::unique_ptr<ModuleCacheEntry>> Map;

  bool ValidateAgainstInvalidatedPaths = false;

  /// The number of bytes of PCMs written in-process that may be kept in memory
  /// before being spilled to disk. Zero means unlimited.
  uint64_t MemoryLimit = 0;

  void addInvalidatedPath(StringRef Path);
  std::optional<bool> isDirectoryInvalidated(StringRef Directory) const;

  /// Flushes all PCMs built in-process to disk.
  void flush();

  /// Records an access of \p Entry for the purposes of spilling.
  void touch(ModuleCacheEntry &Entry);

  /// Records that \p MapEntry was written and is now held in memory.
  void addResident(ModuleCacheMapEntry &MapEntry);

  /// If PCMs written in-process occupy more memory than \c MemoryLimit, writes
  /// the least recently used ones to disk and replaces their in-memory buffers
  /// with memory-mapped files until we're back within the limit. Buffers still
  /// referenced by readers are freed once the last reader is done with them.
  void maybeSpill(AtomicLineLogger &Logger);

private:
  /// Spills single entry, returning true if its memory got released.
  bool spill(ModuleCacheMapEntry &MapEntry, AtomicLineLogger &Logger);

  std::atomic<uint64_t> AccessClock = 0;

  /// The total size of buffers of entries in the \c S_Written state.
  std::atomic<uint64_t> ResidentBytes = 0;
  /// Entries that might be in the \c S_Written state. These point into \c Map,
  /// whose entries have stable addresses.
  std::mutex ResidentMutex;
  std::vector<ModuleCacheMapEntry *> Resident;
  /// Ensures only one thread spills at a time.
  std::mutex SpillMutex;

  mutable std::mutex InvalidatedPathsMutex;
  llvm::StringSet<> InvalidatedPaths;
  std::atomic<bool> AnyInvalidatedPaths = false;
};

std::shared_ptr<ModuleCache>
makeInProcessModuleCache(ModuleCacheEntries &Entries, AtomicLineLogger &Logger);

} // namespace dependencies
} // namespace clang

#endif // LLVM_CLANG_DEPENDENCYSCANNING_INPROCESSMODULECACHE_H
