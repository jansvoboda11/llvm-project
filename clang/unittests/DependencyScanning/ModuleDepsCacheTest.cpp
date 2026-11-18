//===- ModuleDepsCacheTest.cpp --------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "clang/DependencyScanning/ModuleDepsCache.h"
#include "clang/DependencyScanning/DependencyScanningService.h"
#include "clang/DependencyScanning/DependencyScanningWorker.h"
#include "clang/Frontend/CompilerInvocation.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/TextDiagnosticBuffer.h"
#include "llvm/Support/VirtualFileSystem.h"

#include "gtest/gtest.h"

#include <atomic>

using namespace clang;
using namespace clang::dependencies;

namespace {
/// Returns an invocation that is unique to the target with \p Index.
CompilerInvocation makeInvocation(unsigned Index) {
  DiagnosticOptions DiagOpts;
  IntrusiveRefCntPtr<DiagnosticsEngine> Diags =
      CompilerInstance::createDiagnostics(*llvm::vfs::getRealFileSystem(),
                                          DiagOpts, new TextDiagnosticBuffer());
  std::string Define = "-DTARGET=" + std::to_string(Index);
  const char *Args[] = {"-x", "c", Define.c_str()};
  CompilerInvocation CI;
  bool Success = CompilerInvocation::CreateFromArgs(CI, Args, *Diags);
  EXPECT_TRUE(Success);
  return CI;
}

std::shared_ptr<ModuleDepsGroup> getGroup(ModuleDepsCache &Cache,
                                          unsigned Index) {
  return Cache.getOrCreateGroup(makeInvocation(Index), "/cwd");
}
} // namespace

TEST(ModuleDepsCache, SameInvocationSameGroup) {
  std::atomic<unsigned> PeakNumWorkers = 1;
  ModuleDepsCache Cache(PeakNumWorkers);
  auto A = getGroup(Cache, 0);
  EXPECT_EQ(A, getGroup(Cache, 0));
  EXPECT_NE(A, getGroup(Cache, 1));
  EXPECT_NE(A, Cache.getOrCreateGroup(makeInvocation(0), "/other-cwd"));
}

TEST(ModuleDepsCache, PrunesLeastRecentlyUsedUnheldGroups) {
  constexpr unsigned PerWorker = ModuleDepsCache::UnheldGroupsPerWorker;
  std::atomic<unsigned> PeakNumWorkers = 1;
  ModuleDepsCache Cache(PeakNumWorkers);

  // Weak pointers don't count as holding the group, but tell if it's gone.
  std::vector<std::weak_ptr<ModuleDepsGroup>> Groups;
  for (unsigned I = 0; I != PerWorker; ++I)
    Groups.push_back(getGroup(Cache, I));
  EXPECT_EQ(Cache.getNumGroups(), PerWorker);
  for (const auto &G : Groups)
    EXPECT_FALSE(G.expired());

  // Using group 0 again makes group 1 the least recently used one, so that's
  // what gets pruned when a new group gets created.
  getGroup(Cache, 0);
  Groups.push_back(getGroup(Cache, PerWorker));
  EXPECT_EQ(Cache.getNumGroups(), PerWorker);
  EXPECT_FALSE(Groups[0].expired());
  EXPECT_TRUE(Groups[1].expired());
  EXPECT_FALSE(Groups[PerWorker].expired());

  // Using an existing group never prunes.
  for (unsigned I = 0; I != 2 * PerWorker; ++I)
    getGroup(Cache, 0);
  EXPECT_EQ(Cache.getNumGroups(), PerWorker);
}

TEST(ModuleDepsCache, NeverPrunesHeldGroups) {
  constexpr unsigned PerWorker = ModuleDepsCache::UnheldGroupsPerWorker;
  std::atomic<unsigned> PeakNumWorkers = 1;
  ModuleDepsCache Cache(PeakNumWorkers);

  std::vector<std::shared_ptr<ModuleDepsGroup>> Held;
  for (unsigned I = 0; I != 3 * PerWorker; ++I)
    Held.push_back(getGroup(Cache, I));
  EXPECT_EQ(Cache.getNumGroups(), 3 * PerWorker);
  for (unsigned I = 0; I != 3 * PerWorker; ++I)
    EXPECT_EQ(Held[I], getGroup(Cache, I));

  // Once released, the excess gets pruned with the next new group.
  Held.clear();
  getGroup(Cache, 3 * PerWorker).reset();
  EXPECT_EQ(Cache.getNumGroups(), PerWorker);
}

TEST(ModuleDepsCache, ScalesWithPeakNumberOfWorkers) {
  constexpr unsigned PerWorker = ModuleDepsCache::UnheldGroupsPerWorker;
  std::atomic<unsigned> PeakNumWorkers = 1;
  ModuleDepsCache Cache(PeakNumWorkers);
  PeakNumWorkers = 4;
  for (unsigned I = 0; I != 5 * PerWorker; ++I)
    getGroup(Cache, I).reset();
  EXPECT_EQ(Cache.getNumGroups(), 4 * PerWorker);
}

TEST(ModuleDepsCache, NoWorkers) {
  constexpr unsigned PerWorker = ModuleDepsCache::UnheldGroupsPerWorker;
  std::atomic<unsigned> PeakNumWorkers = 0;
  ModuleDepsCache Cache(PeakNumWorkers);
  for (unsigned I = 0; I != 2 * PerWorker; ++I)
    getGroup(Cache, I).reset();
  EXPECT_EQ(Cache.getNumGroups(), PerWorker);
}

TEST(ModuleDepsCache, ServiceTracksPeakNumberOfWorkers) {
  constexpr unsigned PerWorker = ModuleDepsCache::UnheldGroupsPerWorker;
  DependencyScanningServiceOptions Opts;
  Opts.FlushModuleCache = false;
  DependencyScanningService Service(std::move(Opts));
  ModuleDepsCache &Cache = Service.getModuleDepsCache();

  {
    std::vector<std::unique_ptr<DependencyScanningWorker>> Workers;
    for (unsigned I = 0; I != 4; ++I)
      Workers.push_back(std::make_unique<DependencyScanningWorker>(Service));
  }

  // The workers were destroyed (e.g. between builds) and only one is created
  // again. The cache still keeps as many groups as the peak allows.
  DependencyScanningWorker Worker(Service);
  for (unsigned I = 0; I != 5 * PerWorker; ++I)
    getGroup(Cache, I).reset();
  EXPECT_EQ(Cache.getNumGroups(), 4 * PerWorker);
}
