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
#include <map>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>

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
/// This class is thread-safe.
class ModuleDepsCache {
public:
  /// Returns the group of translation units with \p Invocation in
  /// \p WorkingDirectory, creating it if necessary. The per-TU options of
  /// \p Invocation are expected to be reset already.
  std::shared_ptr<ModuleDepsGroup>
  getOrCreateGroup(const CompilerInvocation &Invocation,
                   StringRef WorkingDirectory);

private:
  using GroupKey = llvm::BLAKE3Result<16>;

  /// Identifies the group of translation units with \p Invocation in
  /// \p WorkingDirectory.
  static GroupKey getKey(const CompilerInvocation &Invocation,
                         StringRef WorkingDirectory);

  std::mutex Mutex;
  std::map<GroupKey, std::shared_ptr<ModuleDepsGroup>> Groups;
};

} // namespace dependencies
} // namespace clang

#endif // LLVM_CLANG_DEPENDENCYSCANNING_MODULEDEPSCACHE_H
