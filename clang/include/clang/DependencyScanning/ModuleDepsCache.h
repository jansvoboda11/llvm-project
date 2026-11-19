//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// A build system typically compiles many translation units with command lines
// that only differ in a handful of per-TU paths (the input, the output, ...).
// Whatever the scanner learns about the modular dependencies of one of them
// applies to all the others too. This file defines the cache where that
// knowledge is stored and shared.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_DEPENDENCYSCANNING_MODULEDEPSCACHE_H
#define LLVM_CLANG_DEPENDENCYSCANNING_MODULEDEPSCACHE_H

#include "clang/DependencyScanning/DependencyGraph.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/BLAKE3.h"
#include <atomic>
#include <map>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <vector>

namespace clang {
class CompilerInvocation;

namespace serialization {
class ModuleFile;
} // namespace serialization

namespace dependencies {

/// What the scanner learned about the modular dependencies of a group of
/// translation units whose compiler invocations only differ in per-TU options,
/// keyed by module file.
///
/// A module is only analyzed by the first translation unit that reaches it;
/// the resulting \c ModuleDeps is handed to all the others. This relies on the
/// following, none of which is checked:
///  * The \c DependencyActionController of all such scans behaves the same,
///    i.e. it is a pure function of its arguments. Note that for a reused
///    module its \c finalizeModuleInvocation() is not called again, and that
///    the \c ModuleDeps passed to \c lookupModuleOutput() may be shared with
///    other threads.
///  * What is known about a module does not depend on what else the
///    translation unit happened to load. (This is not strictly true today:
///    \c ModuleDeps::LinkLibraries depends on whether the "export_as" target
///    of the module is known, so the first translation unit wins, the same
///    way the first one wins when a client merges results of multiple scans.)
///
/// This class is thread-safe.
class ModuleDepsGroup {
public:
  /// Returns the dependency information previously stored for \p MF.
  std::shared_ptr<const ModuleDeps>
  lookup(const serialization::ModuleFile &MF) const;

  /// Stores the dependency information for \p MF, unless there already is
  /// one. Returns the stored information.
  std::shared_ptr<const ModuleDeps> add(const serialization::ModuleFile &MF,
                                        std::shared_ptr<const ModuleDeps> MD);

private:
  /// Identifies the contents of \p MF.
  static std::string getKey(const serialization::ModuleFile &MF);

  mutable std::shared_mutex Mutex;
  llvm::StringMap<std::shared_ptr<const ModuleDeps>> Entries;
};

/// The \c ModuleDepsGroup of each group of translation units whose invocations
/// only differ in per-TU options.
///
/// Groups are pruned as a whole. A group that is held outside of the cache
/// (i.e. by a translation unit being scanned) is never pruned. Of the remaining
/// groups, the cache keeps the \c UnheldGroupsPerWorker most recently used
/// ones for each worker. This gives some slack to builds that interleave
/// scanning of multiple build targets, and to serial builds where one
/// translation unit of a target finishes right before the next one starts. The
/// number of workers is the peak number of workers of the service that existed
/// at the same time, which approximates how many translation units are scanned
/// in parallel, and is not affected by workers being destroyed between builds
/// and lazily created again.
///
/// This class is thread-safe.
class ModuleDepsCache {
public:
  /// How many of the groups that are not held outside of the cache are kept
  /// around for each worker.
  static constexpr unsigned UnheldGroupsPerWorker = 2;

  /// \p PeakNumWorkers is the peak number of workers that existed at the same
  /// time, maintained by the owner of the cache.
  explicit ModuleDepsCache(const std::atomic<unsigned> &PeakNumWorkers)
      : PeakNumWorkers(PeakNumWorkers) {}

  /// Returns the group of translation units with \p Invocation in
  /// \p WorkingDirectory, creating it if necessary. The per-TU options of
  /// \p Invocation are expected to be reset already. The group is guaranteed
  /// to stay in the cache while the returned pointer (or a copy of it) is
  /// alive.
  std::shared_ptr<ModuleDepsGroup>
  getOrCreateGroup(const CompilerInvocation &Invocation,
                   StringRef WorkingDirectory);

  /// Returns the number of groups in the cache, for testing purposes.
  size_t getNumGroups();

private:
  using GroupKey = llvm::BLAKE3Result<16>;

  struct GroupEntry {
    std::shared_ptr<ModuleDepsGroup> Group;
    /// The value of \c UseCounter when the group was last returned.
    uint64_t LastUse = 0;
  };

  /// Identifies the group of translation units with \p Invocation in
  /// \p WorkingDirectory.
  static GroupKey getKey(const CompilerInvocation &Invocation,
                         StringRef WorkingDirectory);

  /// Removes the least recently used groups that are not held outside of the
  /// cache, until only as many of them as allowed are left. These are moved
  /// into \p Pruned, so that they can be destroyed without holding the lock.
  void pruneGroups(std::vector<std::shared_ptr<ModuleDepsGroup>> &Pruned);

  std::mutex Mutex;
  std::map<GroupKey, GroupEntry> Groups;
  /// Incremented each time a group is returned.
  uint64_t UseCounter = 0;
  /// The peak number of workers that existed at the same time.
  const std::atomic<unsigned> &PeakNumWorkers;
};

} // namespace dependencies
} // namespace clang

#endif // LLVM_CLANG_DEPENDENCYSCANNING_MODULEDEPSCACHE_H
