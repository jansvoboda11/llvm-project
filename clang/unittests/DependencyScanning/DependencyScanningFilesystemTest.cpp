//===- DependencyScanningFilesystemTest.cpp -------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "clang/DependencyScanning/DependencyScanningFilesystem.h"
#include "clang/DependencyScanning/DependencyScanningService.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/VirtualFileSystem.h"
#include "llvm/Testing/Support/SupportHelpers.h"
#include "gtest/gtest.h"
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <set>
#include <thread>

using namespace clang::dependencies;

namespace {

/// Releases all waiting threads simultaneously so that the worker logic can
/// be observed under maximal concurrency rather than a thread-spawn cascade.
struct StartBarrier {
  std::mutex M;
  std::condition_variable CV;
  bool Released = false;

  void wait() {
    std::unique_lock<std::mutex> Lock(M);
    CV.wait(Lock, [&] { return Released; });
  }

  void release() {
    {
      std::lock_guard<std::mutex> Lock(M);
      Released = true;
    }
    CV.notify_all();
  }
};

/// Build \p NumWorkers DependencyScanningWorkerFilesystems sharing \p Service
/// and \p FS, fan out one thread per worker waiting on a common barrier, then
/// release the barrier and join. Returns each worker's result (initialized to
/// \p Default for slots a thread did not write to).
template <typename R, typename Fn>
std::vector<R>
runConcurrentWorkers(DependencyScanningService &Service,
                     llvm::IntrusiveRefCntPtr<llvm::vfs::FileSystem> FS,
                     unsigned NumWorkers, R Default, Fn &&PerWorker) {
  std::vector<std::unique_ptr<DependencyScanningWorkerFilesystem>> Workers;
  Workers.reserve(NumWorkers);
  for (unsigned I = 0; I < NumWorkers; ++I)
    Workers.push_back(
        std::make_unique<DependencyScanningWorkerFilesystem>(Service, FS));

  StartBarrier Barrier;
  std::vector<R> Results(NumWorkers, std::move(Default));
  std::vector<std::thread> Threads;
  Threads.reserve(NumWorkers);
  for (unsigned I = 0; I < NumWorkers; ++I) {
    Threads.emplace_back([&, I] {
      Barrier.wait();
      Results[I] = PerWorker(*Workers[I], I);
    });
  }
  Barrier.release();
  for (auto &T : Threads)
    T.join();
  return Results;
}

} // namespace

TEST(DependencyScanningFilesystem, OpenFileAndGetBufferRepeatedly) {
  auto InMemoryFS = llvm::makeIntrusiveRefCnt<llvm::vfs::InMemoryFileSystem>();
  InMemoryFS->setCurrentWorkingDirectory("/");
  InMemoryFS->addFile("/foo", 0, llvm::MemoryBuffer::getMemBuffer("content"));

  DependencyScanningService Service({});
  DependencyScanningWorkerFilesystem DepFS(Service, InMemoryFS);

  auto FileOrErr1 = DepFS.openFileForRead("foo");
  auto FileOrErr2 = DepFS.openFileForRead("foo");
  ASSERT_EQ(FileOrErr1.getError(), std::error_code{});
  ASSERT_EQ(FileOrErr1.getError(), std::error_code{});
  std::unique_ptr<llvm::vfs::File> File1 = std::move(*FileOrErr1);
  std::unique_ptr<llvm::vfs::File> File2 = std::move(*FileOrErr2);
  ASSERT_NE(File1, nullptr);
  ASSERT_NE(File2, nullptr);
  auto BufOrErr11 = File1->getBuffer("buf11");
  auto BufOrErr12 = File1->getBuffer("buf12");
  auto BufOrErr21 = File1->getBuffer("buf21");
  ASSERT_EQ(BufOrErr11.getError(), std::error_code{});
  ASSERT_EQ(BufOrErr12.getError(), std::error_code{});
  ASSERT_EQ(BufOrErr21.getError(), std::error_code{});
  std::unique_ptr<llvm::MemoryBuffer> Buf11 = std::move(*BufOrErr11);
  std::unique_ptr<llvm::MemoryBuffer> Buf12 = std::move(*BufOrErr12);
  std::unique_ptr<llvm::MemoryBuffer> Buf21 = std::move(*BufOrErr21);
  ASSERT_NE(Buf11, nullptr);
  ASSERT_NE(Buf12, nullptr);
  ASSERT_NE(Buf21, nullptr);
  ASSERT_EQ(Buf11->getBuffer().data(), Buf12->getBuffer().data());
  ASSERT_EQ(Buf11->getBuffer().data(), Buf21->getBuffer().data());
  EXPECT_EQ(Buf11->getBuffer(), "content");
}

TEST(DependencyScanningWorkerFilesystem, CacheStatusFailures) {
  auto InMemoryFS = llvm::makeIntrusiveRefCnt<llvm::vfs::InMemoryFileSystem>();

  auto InstrumentingFS =
      llvm::makeIntrusiveRefCnt<llvm::vfs::TracingFileSystem>(InMemoryFS);

  DependencyScanningServiceOptions Opts;
  Opts.CacheNegativeStats = true;
  DependencyScanningService Service(std::move(Opts));
  DependencyScanningWorkerFilesystem DepFS(Service, InstrumentingFS);
  DependencyScanningWorkerFilesystem DepFS2(Service, InstrumentingFS);

  DepFS.status("/foo.c");
  EXPECT_EQ(InstrumentingFS->NumStatusCalls, 1u);

  DepFS.status("/foo.c");
  EXPECT_EQ(InstrumentingFS->NumStatusCalls, 1u); // Cached, no increase.

  DepFS.status("/bar.c");
  EXPECT_EQ(InstrumentingFS->NumStatusCalls, 2u);

  DepFS2.status("/foo.c");
  EXPECT_EQ(InstrumentingFS->NumStatusCalls, 2u); // Shared cache.
}

TEST(DependencyScanningFilesystem, CacheGetRealPath) {
  auto InMemoryFS = llvm::makeIntrusiveRefCnt<llvm::vfs::InMemoryFileSystem>();
  InMemoryFS->setCurrentWorkingDirectory("/");
  InMemoryFS->addFile("/foo", 0, llvm::MemoryBuffer::getMemBuffer(""));
  InMemoryFS->addFile("/bar", 0, llvm::MemoryBuffer::getMemBuffer(""));

  auto InstrumentingFS =
      llvm::makeIntrusiveRefCnt<llvm::vfs::TracingFileSystem>(InMemoryFS);

  DependencyScanningService Service({});
  DependencyScanningWorkerFilesystem DepFS(Service, InstrumentingFS);
  DependencyScanningWorkerFilesystem DepFS2(Service, InstrumentingFS);

  {
    llvm::SmallString<128> Result;
    DepFS.getRealPath("/foo", Result);
    EXPECT_EQ(InstrumentingFS->NumGetRealPathCalls, 1u);
  }

  {
    llvm::SmallString<128> Result;
    DepFS.getRealPath("/foo", Result);
    EXPECT_EQ(InstrumentingFS->NumGetRealPathCalls, 1u); // Cached, no increase.
  }

  {
    llvm::SmallString<128> Result;
    DepFS.getRealPath("/bar", Result);
    EXPECT_EQ(InstrumentingFS->NumGetRealPathCalls, 2u);
  }

  {
    llvm::SmallString<128> Result;
    DepFS2.getRealPath("/foo", Result);
    EXPECT_EQ(InstrumentingFS->NumGetRealPathCalls, 2u); // Shared cache.
  }
}

TEST(DependencyScanningFilesystem, RealPathAndStatusInvariants) {
  auto InMemoryFS = llvm::makeIntrusiveRefCnt<llvm::vfs::InMemoryFileSystem>();
  InMemoryFS->setCurrentWorkingDirectory("/");
  InMemoryFS->addFile("/foo.c", 0, llvm::MemoryBuffer::getMemBuffer(""));
  InMemoryFS->addFile("/bar.c", 0, llvm::MemoryBuffer::getMemBuffer(""));

  DependencyScanningService Service({});
  DependencyScanningWorkerFilesystem DepFS(Service, InMemoryFS);

  // Success.
  {
    DepFS.status("/foo.c");

    llvm::SmallString<128> Result;
    DepFS.getRealPath("/foo.c", Result);
  }
  {
    llvm::SmallString<128> Result;
    DepFS.getRealPath("/bar.c", Result);

    DepFS.status("/bar.c");
  }

  // Failure.
  {
    DepFS.status("/foo.m");

    llvm::SmallString<128> Result;
    DepFS.getRealPath("/foo.m", Result);
  }
  {
    llvm::SmallString<128> Result;
    DepFS.getRealPath("/bar.m", Result);

    DepFS.status("/bar.m");
  }

  // Failure without caching.
  {
    DepFS.status("/foo");

    llvm::SmallString<128> Result;
    DepFS.getRealPath("/foo", Result);
  }
  {
    llvm::SmallString<128> Result;
    DepFS.getRealPath("/bar", Result);

    DepFS.status("/bar");
  }
}

TEST(DependencyScanningFilesystem, CacheStatOnExists) {
  auto InMemoryFS = llvm::makeIntrusiveRefCnt<llvm::vfs::InMemoryFileSystem>();
  auto InstrumentingFS =
      llvm::makeIntrusiveRefCnt<llvm::vfs::TracingFileSystem>(InMemoryFS);
  InMemoryFS->setCurrentWorkingDirectory("/");
  InMemoryFS->addFile("/foo", 0, llvm::MemoryBuffer::getMemBuffer(""));
  InMemoryFS->addFile("/bar", 0, llvm::MemoryBuffer::getMemBuffer(""));
  DependencyScanningService Service({});
  DependencyScanningWorkerFilesystem DepFS(Service, InstrumentingFS);

  DepFS.status("/foo");
  DepFS.status("/foo");
  DepFS.status("/bar");
  EXPECT_EQ(InstrumentingFS->NumStatusCalls, 2u);

  DepFS.exists("/foo");
  DepFS.exists("/bar");
  EXPECT_EQ(InstrumentingFS->NumStatusCalls, 2u);
  EXPECT_EQ(InstrumentingFS->NumExistsCalls, 0u);
}

TEST(DependencyScanningFilesystem, CacheStatFailures) {
  auto InMemoryFS = llvm::makeIntrusiveRefCnt<llvm::vfs::InMemoryFileSystem>();
  InMemoryFS->setCurrentWorkingDirectory("/");
  InMemoryFS->addFile("/dir/present.h", 0,
                      llvm::MemoryBuffer::getMemBuffer(""));

  auto InstrumentingFS =
      llvm::makeIntrusiveRefCnt<llvm::vfs::TracingFileSystem>(InMemoryFS);

  DependencyScanningServiceOptions Opts;
  Opts.CacheNegativeStats = true;
  DependencyScanningService Service(std::move(Opts));
  DependencyScanningWorkerFilesystem DepFS(Service, InstrumentingFS);

  DepFS.status("/dir");
  EXPECT_EQ(InstrumentingFS->NumStatusCalls, 1u);
  DepFS.status("/dir");
  EXPECT_EQ(InstrumentingFS->NumStatusCalls, 1u);

  DepFS.status("/dir/present.h");
  EXPECT_EQ(InstrumentingFS->NumStatusCalls, 2u);
  DepFS.status("/dir/present.h");
  EXPECT_EQ(InstrumentingFS->NumStatusCalls, 2u);

  DepFS.status("/dir/missing.h");
  EXPECT_EQ(InstrumentingFS->NumStatusCalls, 3u);
  DepFS.status("/dir/missing.h");
  EXPECT_EQ(InstrumentingFS->NumStatusCalls, 3u);
}

TEST(DependencyScanningFilesystem, NoNegativeCache) {
  auto InMemoryFS = llvm::makeIntrusiveRefCnt<llvm::vfs::InMemoryFileSystem>();
  InMemoryFS->setCurrentWorkingDirectory("/");
  InMemoryFS->addFile("/dir/present.h", 0,
                      llvm::MemoryBuffer::getMemBuffer(""));

  auto InstrumentingFS =
      llvm::makeIntrusiveRefCnt<llvm::vfs::TracingFileSystem>(InMemoryFS);

  DependencyScanningServiceOptions Opts;
  Opts.CacheNegativeStats = false;
  DependencyScanningService Service(std::move(Opts));
  DependencyScanningWorkerFilesystem DepFS(Service, InstrumentingFS);

  DepFS.status("/dir");
  EXPECT_EQ(InstrumentingFS->NumStatusCalls, 1u);
  DepFS.status("/dir");
  EXPECT_EQ(InstrumentingFS->NumStatusCalls, 1u);

  DepFS.status("/dir/present.h");
  EXPECT_EQ(InstrumentingFS->NumStatusCalls, 2u);
  DepFS.status("/dir/present.h");
  EXPECT_EQ(InstrumentingFS->NumStatusCalls, 2u);

  DepFS.status("/dir/missing.h");
  EXPECT_EQ(InstrumentingFS->NumStatusCalls, 3u);
  DepFS.status("/dir/missing.h");
  EXPECT_EQ(InstrumentingFS->NumStatusCalls, 4u);
}

TEST(DependencyScanningFilesystem, NoSlotsForUncachedNegativeStats) {
  auto InMemoryFS = llvm::makeIntrusiveRefCnt<llvm::vfs::InMemoryFileSystem>();
  InMemoryFS->setCurrentWorkingDirectory("/");
  InMemoryFS->addFile("/dir/present.h", 0,
                      llvm::MemoryBuffer::getMemBuffer(""));

  DependencyScanningServiceOptions Opts;
  Opts.CacheNegativeStats = false;
  DependencyScanningService Service(std::move(Opts));
  DependencyScanningWorkerFilesystem DepFS(Service, InMemoryFS);

  auto HasSlot = [&](llvm::StringRef Filename) {
    return Service.getSharedCache()
        .getShardForFilename(Filename)
        .CacheByFilename.contains(Filename);
  };

  // Positive stats keep their slot.
  EXPECT_TRUE(DepFS.status("/dir/present.h"));
  EXPECT_TRUE(HasSlot("/dir/present.h"));

  // Uncached negative stats don't leave a slot behind, not even on repeated
  // queries.
  EXPECT_FALSE(DepFS.status("/dir/missing.h"));
  EXPECT_FALSE(HasSlot("/dir/missing.h"));
  EXPECT_FALSE(DepFS.status("/dir/missing.h"));
  EXPECT_FALSE(HasSlot("/dir/missing.h"));

  // The slot of a cached real path is kept, also after a negative stat.
  llvm::SmallString<128> RealPath;
  (void)DepFS.getRealPath("/dir/missing.h", RealPath);
  EXPECT_TRUE(HasSlot("/dir/missing.h"));
  DependencyScanningWorkerFilesystem DepFS2(Service, InMemoryFS);
  EXPECT_FALSE(DepFS2.status("/dir/missing.h"));
  EXPECT_TRUE(HasSlot("/dir/missing.h"));
  (void)DepFS2.getRealPath("/dir/missing.h", RealPath);

  // The file showing up later is noticed.
  InMemoryFS->addFile("/dir/missing.h", 0,
                      llvm::MemoryBuffer::getMemBuffer(""));
  EXPECT_TRUE(DepFS2.status("/dir/missing.h"));
}

TEST(DependencyScanningFilesystem, StableDirectories) {
  DependencyScanningFilesystemSharedCache Cache;
  Cache.addStableDirectory("/sdk/");
  Cache.addStableDirectory("/res");
  // Relative paths and root directories are ignored.
  Cache.addStableDirectory("rel");
  Cache.addStableDirectory("/");
  Cache.addStableDirectory("");

  EXPECT_TRUE(Cache.isInStableDirectory("/sdk/a.h"));
  EXPECT_TRUE(Cache.isInStableDirectory("/sdk/usr/include/a"));
  EXPECT_TRUE(Cache.isInStableDirectory("/sdk/usr/./a.h"));
  EXPECT_TRUE(Cache.isInStableDirectory("/sdk//a.h"));
  EXPECT_TRUE(Cache.isInStableDirectory("/res/include/a.h"));
  EXPECT_FALSE(Cache.isInStableDirectory("/sdk"));
  EXPECT_FALSE(Cache.isInStableDirectory("/sdk2/a.h"));
  EXPECT_FALSE(Cache.isInStableDirectory("/sdk/../a.h"));
  EXPECT_FALSE(Cache.isInStableDirectory("/sdk/usr/../../a.h"));
  EXPECT_FALSE(Cache.isInStableDirectory("/sdk/usr/.."));
  EXPECT_FALSE(Cache.isInStableDirectory("/sdk//../a.h"));
  EXPECT_TRUE(Cache.isInStableDirectory("/sdk/..a/a.h"));
  EXPECT_FALSE(Cache.isInStableDirectory("/a.h"));
  EXPECT_FALSE(Cache.isInStableDirectory("/rel/a.h"));
  EXPECT_FALSE(Cache.isInStableDirectory("rel/a.h"));
}

TEST(DependencyScanningFilesystem, CacheStatFailuresInStableDirectories) {
  auto InMemoryFS = llvm::makeIntrusiveRefCnt<llvm::vfs::InMemoryFileSystem>();
  InMemoryFS->setCurrentWorkingDirectory("/");
  InMemoryFS->addFile("/sdk/usr/include/present.h", 0,
                      llvm::MemoryBuffer::getMemBuffer(""));
  InMemoryFS->addFile("/proj/present.h", 0,
                      llvm::MemoryBuffer::getMemBuffer(""));

  auto InstrumentingFS =
      llvm::makeIntrusiveRefCnt<llvm::vfs::TracingFileSystem>(InMemoryFS);

  DependencyScanningServiceOptions Opts;
  Opts.CacheNegativeStats = false;
  DependencyScanningService Service(std::move(Opts));
  Service.getSharedCache().addStableDirectory("/sdk");
  DependencyScanningWorkerFilesystem DepFS(Service, InstrumentingFS);

  auto CountStats = [&](llvm::StringRef Path) {
    size_t Before = InstrumentingFS->NumStatusCalls;
    EXPECT_FALSE(DepFS.status(Path));
    EXPECT_FALSE(DepFS.status(Path));
    return InstrumentingFS->NumStatusCalls - Before;
  };

  // Failures in a stable directory are cached, with or without an extension.
  EXPECT_EQ(CountStats("/sdk/usr/include/missing.h"), 1u);
  EXPECT_EQ(CountStats("/sdk/usr/include/Missing.framework"), 1u);
  EXPECT_EQ(CountStats("/sdk/usr/include/missing"), 1u);

  // Failures elsewhere aren't.
  EXPECT_EQ(CountStats("/proj/missing.h"), 2u);
  EXPECT_EQ(CountStats("/sdk2/missing.h"), 2u);
  EXPECT_EQ(CountStats("/sdk/../proj/missing2.h"), 2u);

  // Relative paths are resolved against the working directory first.
  ASSERT_FALSE(DepFS.setCurrentWorkingDirectory("/sdk/usr"));
  EXPECT_EQ(CountStats("include/missing3.h"), 1u);
  ASSERT_FALSE(DepFS.setCurrentWorkingDirectory("/proj"));
  EXPECT_EQ(CountStats("missing3.h"), 2u);

  // Files showing up outside stable directories are noticed.
  InMemoryFS->addFile("/proj/missing.h", 0,
                      llvm::MemoryBuffer::getMemBuffer(""));
  EXPECT_TRUE(DepFS.status("/proj/missing.h"));

  // Files showing up in stable directories aren't, but they're diagnosed.
  InMemoryFS->addFile("/sdk/usr/include/missing.h", 0,
                      llvm::MemoryBuffer::getMemBuffer(""));
  EXPECT_FALSE(DepFS.status("/sdk/usr/include/missing.h"));
  auto InvalidEntries =
      Service.getSharedCache().getOutOfDateEntries(*InMemoryFS);
  ASSERT_EQ(InvalidEntries.size(), 1u);
  EXPECT_STREQ("/sdk/usr/include/missing.h", InvalidEntries[0].Path);
}

TEST(DependencyScanningFilesystem, NoCachedOtherErrorsInStableDirectories) {
  struct DenyingFS : llvm::vfs::ProxyFileSystem {
    unsigned NumStatusCalls = 0;
    DenyingFS(llvm::IntrusiveRefCntPtr<llvm::vfs::FileSystem> UnderlyingFS)
        : ProxyFileSystem(std::move(UnderlyingFS)) {}
    llvm::ErrorOr<llvm::vfs::Status> status(const llvm::Twine &Path) override {
      ++NumStatusCalls;
      return std::make_error_code(std::errc::permission_denied);
    }
  };

  auto InMemoryFS = llvm::makeIntrusiveRefCnt<llvm::vfs::InMemoryFileSystem>();
  InMemoryFS->setCurrentWorkingDirectory("/");
  auto FS = llvm::makeIntrusiveRefCnt<DenyingFS>(InMemoryFS);

  DependencyScanningServiceOptions Opts;
  Opts.CacheNegativeStats = false;
  DependencyScanningService Service(std::move(Opts));
  Service.getSharedCache().addStableDirectory("/sdk");
  DependencyScanningWorkerFilesystem DepFS(Service, FS);

  // Only missing files are cached, not other (maybe transient) errors.
  EXPECT_EQ(DepFS.status("/sdk/denied.h").getError(),
            std::errc::permission_denied);
  EXPECT_EQ(DepFS.status("/sdk/denied.h").getError(),
            std::errc::permission_denied);
  EXPECT_EQ(FS->NumStatusCalls, 2u);
}

TEST(DependencyScanningFilesystem, DiagnoseStaleStatFailures) {
  auto InMemoryFS = llvm::makeIntrusiveRefCnt<llvm::vfs::InMemoryFileSystem>();
  InMemoryFS->setCurrentWorkingDirectory("/");

  DependencyScanningServiceOptions Opts;
  Opts.CacheNegativeStats = true;
  DependencyScanningService Service(std::move(Opts));
  DependencyScanningWorkerFilesystem DepFS(Service, InMemoryFS);

  bool Path1Exists = DepFS.exists("/path1.suffix");
  ASSERT_EQ(Path1Exists, false);

  // Adding a file that has been stat-ed,
  InMemoryFS->addFile("/path1.suffix", 0, llvm::MemoryBuffer::getMemBuffer(""));
  Path1Exists = DepFS.exists("/path1.suffix");
  // Due to caching in SharedCache, path1 should not exist in
  // DepFS's eyes.
  ASSERT_EQ(Path1Exists, false);

  auto InvalidEntries =
      Service.getSharedCache().getOutOfDateEntries(*InMemoryFS);

  EXPECT_EQ(InvalidEntries.size(), 1u);
  ASSERT_STREQ("/path1.suffix", InvalidEntries[0].Path);
}

TEST(DependencyScanningFilesystem, DiagnoseCachedFileSizeChange) {
  auto InMemoryFS1 = llvm::makeIntrusiveRefCnt<llvm::vfs::InMemoryFileSystem>();
  auto InMemoryFS2 = llvm::makeIntrusiveRefCnt<llvm::vfs::InMemoryFileSystem>();
  InMemoryFS1->setCurrentWorkingDirectory("/");
  InMemoryFS2->setCurrentWorkingDirectory("/");

  DependencyScanningServiceOptions Opts;
  Opts.CacheNegativeStats = true;
  DependencyScanningService Service(std::move(Opts));
  DependencyScanningWorkerFilesystem DepFS(Service, InMemoryFS1);

  InMemoryFS1->addFile("/path1.suffix", 0,
                       llvm::MemoryBuffer::getMemBuffer(""));
  bool Path1Exists = DepFS.exists("/path1.suffix");
  ASSERT_EQ(Path1Exists, true);

  // Add a file to a new FS that has the same path but different content.
  InMemoryFS2->addFile("/path1.suffix", 1,
                       llvm::MemoryBuffer::getMemBuffer("        "));

  // Check against the new file system. InMemoryFS2 could be the underlying
  // physical system in the real world.
  auto InvalidEntries =
      Service.getSharedCache().getOutOfDateEntries(*InMemoryFS2);

  ASSERT_EQ(InvalidEntries.size(), 1u);
  ASSERT_STREQ("/path1.suffix", InvalidEntries[0].Path);
  auto SizeInfo = std::get_if<
      DependencyScanningFilesystemSharedCache::OutOfDateEntry::SizeChangedInfo>(
      &InvalidEntries[0].Info);
  ASSERT_TRUE(SizeInfo);
  ASSERT_EQ(SizeInfo->CachedSize, 0u);
  ASSERT_EQ(SizeInfo->ActualSize, 8u);
}

TEST(DependencyScanningFilesystem, DoNotDiagnoseDirSizeChange) {
  llvm::SmallString<128> Dir;
  ASSERT_FALSE(llvm::sys::fs::createUniqueDirectory("tmp", Dir));

  llvm::IntrusiveRefCntPtr<llvm::vfs::FileSystem> FS =
      llvm::vfs::createPhysicalFileSystem();

  DependencyScanningService Service({});
  DependencyScanningWorkerFilesystem DepFS(Service, FS);

  // Trigger the file system cache.
  ASSERT_EQ(DepFS.exists(Dir), true);

  // Add a file to the FS to change its size.
  // It seems that directory sizes reported are not meaningful,
  // and should not be used to check for size changes.
  // This test is setup only to trigger a size change so that we
  // know we are excluding directories from reporting.
  llvm::SmallString<128> FilePath = Dir;
  llvm::sys::path::append(FilePath, "file.h");
  {
    std::error_code EC;
    llvm::raw_fd_ostream TempFile(FilePath, EC);
    ASSERT_FALSE(EC);
  }

  // We do not report directory size changes.
  auto InvalidEntries = Service.getSharedCache().getOutOfDateEntries(*FS);
  EXPECT_EQ(InvalidEntries.size(), 0u);
}

TEST(DependencyScanningWorkerFilesystem, DirectiveTokens) {
  using namespace clang::dependency_directives_scan;

  // Enough directives and tokens to spill the scanner's vectors to the heap.
  std::string Source;
  for (unsigned I = 0; I < 200; ++I)
    Source +=
        "#define M" + std::to_string(I) + "(x) x + " + std::to_string(I) + "\n";
  Source += "#include \"bar.h\"\nint x;\n";

  auto InMemoryFS = llvm::makeIntrusiveRefCnt<llvm::vfs::InMemoryFileSystem>();
  InMemoryFS->setCurrentWorkingDirectory("/");
  InMemoryFS->addFile("/foo.h", 0, llvm::MemoryBuffer::getMemBuffer(Source));
  InMemoryFS->addFile("/bad.h", 0,
                      llvm::MemoryBuffer::getMemBuffer("@import A\n"));

  DependencyScanningService Service({});
  DependencyScanningWorkerFilesystem DepFS(Service, InMemoryFS);
  DependencyScanningWorkerFilesystem DepFS2(Service, InMemoryFS);

  llvm::SmallVector<Token> ExpectedTokens;
  llvm::SmallVector<Directive> ExpectedDirectives;
  ASSERT_FALSE(clang::scanSourceForDependencyDirectives(Source, ExpectedTokens,
                                                        ExpectedDirectives));

  auto Directives = DepFS.getDirectiveTokens("/foo.h");
  ASSERT_TRUE(Directives);
  ASSERT_EQ(Directives->size(), ExpectedDirectives.size());
  const Token *NextToken = Directives->front().Tokens.data();
  for (auto [D, Expected] : llvm::zip_equal(*Directives, ExpectedDirectives)) {
    EXPECT_EQ(D.Kind, Expected.Kind);
    // The tokens of all directives are stored contiguously.
    EXPECT_EQ(D.Tokens.data(), NextToken);
    NextToken = D.Tokens.end();
    ASSERT_EQ(D.Tokens.size(), Expected.Tokens.size());
    for (auto [T, ExpectedT] : llvm::zip_equal(D.Tokens, Expected.Tokens)) {
      EXPECT_EQ(T.Offset, ExpectedT.Offset);
      EXPECT_EQ(T.Length, ExpectedT.Length);
      EXPECT_EQ(T.Kind, ExpectedT.Kind);
      EXPECT_EQ(T.Flags, ExpectedT.Flags);
    }
  }
  EXPECT_EQ(Directives->back().Kind, pp_eof);

  // The scanned directives are shared.
  auto Directives2 = DepFS2.getDirectiveTokens("/foo.h");
  ASSERT_TRUE(Directives2);
  EXPECT_EQ(Directives2->data(), Directives->data());

  // Scanning failures are cached too.
  EXPECT_FALSE(DepFS.getDirectiveTokens("/bad.h"));
  EXPECT_FALSE(DepFS2.getDirectiveTokens("/bad.h"));
}

TEST(DependencyScanningWorkerFilesystem, ConcurrentSameFilenameDeduplicates) {
  llvm::unittest::TempDir TmpDir("dswf-same-filename", /*Unique=*/true);
  llvm::unittest::TempFile TmpFile(TmpDir.path("foo.c"), /*Suffix=*/"",
                                   /*Contents=*/"hello");

  auto TracingFS =
      llvm::makeIntrusiveRefCnt<llvm::vfs::AtomicTracingFileSystem>(
          llvm::vfs::getRealFileSystem());
  DependencyScanningService Service({});

  constexpr unsigned NumWorkers = 16;
  auto Results = runConcurrentWorkers<llvm::ErrorOr<EntryRef>>(
      Service, TracingFS, NumWorkers, std::error_code{},
      [&](DependencyScanningWorkerFilesystem &W, unsigned) {
        return W.getOrCreateFileSystemEntry(TmpFile.path());
      });

  EXPECT_EQ(TracingFS->NumStatusCalls.load(), 1u);
  EXPECT_EQ(TracingFS->NumOpenFileForReadCalls.load(), 1u);

  // All workers must have observed the same underlying entry.
  ASSERT_TRUE(Results[0]);
  llvm::sys::fs::UniqueID FirstUID = Results[0]->getStatus().getUniqueID();
  const char *FirstContents = Results[0]->getContents().data();
  for (unsigned I = 0; I < NumWorkers; ++I) {
    ASSERT_TRUE(Results[I]) << "worker " << I << " failed";
    EXPECT_EQ(Results[I]->getStatus().getUniqueID(), FirstUID);
    EXPECT_EQ(Results[I]->getContents().data(), FirstContents);
  }
}

// On Windows, llvm::sys::fs::UniqueID is computed from a hashed canonical
// path, so two hard-linked filenames produce different UniqueIDs and the
// premise of this test does not hold.
#ifndef _WIN32
TEST(DependencyScanningWorkerFilesystem,
     ConcurrentSameUIDDifferentFilenamesDeduplicatesOpen) {
  // Use a real on-disk file plus a hard link so the two filenames share a
  // UniqueID, exercising the per-UID slot.
  llvm::unittest::TempDir TmpDir("dswf-same-uid", /*Unique=*/true);
  llvm::SmallString<128> RealPath = TmpDir.path("real.c");
  llvm::SmallString<128> AliasPath = TmpDir.path("alias.c");
  llvm::unittest::TempFile TmpFile(RealPath, /*Suffix=*/"", /*Contents=*/"hi");
  llvm::unittest::TempLink TmpLink(RealPath, AliasPath);

  auto TracingFS =
      llvm::makeIntrusiveRefCnt<llvm::vfs::AtomicTracingFileSystem>(
          llvm::vfs::getRealFileSystem());
  DependencyScanningService Service({});

  constexpr unsigned NumWorkers = 16;
  auto Results = runConcurrentWorkers<llvm::ErrorOr<EntryRef>>(
      Service, TracingFS, NumWorkers, std::error_code{},
      [&](DependencyScanningWorkerFilesystem &W, unsigned I) {
        llvm::StringRef Path = (I % 2 == 0) ? llvm::StringRef(RealPath)
                                            : llvm::StringRef(AliasPath);
        return W.getOrCreateFileSystemEntry(Path);
      });

  // Each filename's slot dedupes its own stats; with two filenames we expect
  // at most two stats. The UID slot then collapses the actual reads.
  EXPECT_LE(TracingFS->NumStatusCalls.load(), 2u);
  EXPECT_EQ(TracingFS->NumOpenFileForReadCalls.load(), 1u);

  ASSERT_TRUE(Results[0]);
  llvm::sys::fs::UniqueID FirstUID = Results[0]->getStatus().getUniqueID();
  for (unsigned I = 0; I < NumWorkers; ++I) {
    ASSERT_TRUE(Results[I]) << "worker " << I << " failed";
    EXPECT_EQ(Results[I]->getStatus().getUniqueID(), FirstUID);
  }
}
#endif // !_WIN32

TEST(DependencyScanningWorkerFilesystem, ConcurrentNegativeStatDeduplicates) {
  // Construct a path inside a temporary directory but never create the
  // file, so concurrent stat() calls land on the negative-stat path through
  // the real filesystem.
  llvm::unittest::TempDir TmpDir("dswf-negative", /*Unique=*/true);
  llvm::SmallString<128> MissingPath = TmpDir.path("missing.h");

  auto TracingFS =
      llvm::makeIntrusiveRefCnt<llvm::vfs::AtomicTracingFileSystem>(
          llvm::vfs::getRealFileSystem());
  // The dedup under test only applies when negative stats are cached; enable
  // caching and use a path whose extension is eligible for it.
  DependencyScanningServiceOptions Opts;
  Opts.CacheNegativeStats = true;
  DependencyScanningService Service(std::move(Opts));

  constexpr unsigned NumWorkers = 16;
  auto Results = runConcurrentWorkers<llvm::ErrorOr<llvm::vfs::Status>>(
      Service, TracingFS, NumWorkers, std::error_code{},
      [&](DependencyScanningWorkerFilesystem &W, unsigned) {
        return W.status(MissingPath);
      });

  EXPECT_EQ(TracingFS->NumStatusCalls.load(), 1u);
  EXPECT_EQ(TracingFS->NumOpenFileForReadCalls.load(), 0u);

  // Every worker observed the same negative result.
  ASSERT_FALSE(Results[0]);
  std::error_code FirstError = Results[0].getError();
  for (unsigned I = 0; I < NumWorkers; ++I) {
    ASSERT_FALSE(Results[I]) << "worker " << I << " unexpectedly succeeded";
    EXPECT_EQ(Results[I].getError(), FirstError);
  }
}

TEST(DependencyScanningWorkerFilesystem,
     ConcurrentUncachedNegativeStatIsSharedButNotPersisted) {
  // Same as above, but with negative-stat caching disabled. Concurrent queries
  // that overlap the producer's in-flight stat still adopt its negative
  // result (sharing an answer for overlapping queries is not "caching"), so no
  // worker opens the missing file. Because the result is not persisted, a
  // later, separate query must re-run the stat.
  llvm::unittest::TempDir TmpDir("dswf-uncached-negative", /*Unique=*/true);
  llvm::SmallString<128> MissingPath = TmpDir.path("missing.h");

  auto TracingFS =
      llvm::makeIntrusiveRefCnt<llvm::vfs::AtomicTracingFileSystem>(
          llvm::vfs::getRealFileSystem());
  DependencyScanningServiceOptions Opts;
  Opts.CacheNegativeStats = false;
  DependencyScanningService Service(std::move(Opts));

  constexpr unsigned NumWorkers = 16;
  auto Results = runConcurrentWorkers<llvm::ErrorOr<llvm::vfs::Status>>(
      Service, TracingFS, NumWorkers, std::error_code{},
      [&](DependencyScanningWorkerFilesystem &W, unsigned) {
        return W.status(MissingPath);
      });

  // A negative stat never opens the file, regardless of how many workers
  // produced versus adopted. Producers are bounded by the worker count;
  // adopters add none. With caching off the result is not persisted, so the
  // count is not pinned to one, but it can never exceed one stat per worker.
  EXPECT_EQ(TracingFS->NumOpenFileForReadCalls.load(), 0u);
  unsigned StatsAfterBurst = TracingFS->NumStatusCalls.load();
  EXPECT_GE(StatsAfterBurst, 1u);
  EXPECT_LE(StatsAfterBurst, NumWorkers);

  // Every worker observed the same negative result.
  ASSERT_FALSE(Results[0]);
  std::error_code FirstError = Results[0].getError();
  for (unsigned I = 0; I < NumWorkers; ++I) {
    ASSERT_FALSE(Results[I]) << "worker " << I << " unexpectedly succeeded";
    EXPECT_EQ(Results[I].getError(), FirstError);
  }

  // The uncached negative was shared, not cached: a later, separate query
  // re-runs the stat rather than adopting a persisted miss.
  DependencyScanningWorkerFilesystem PostWorker(Service, TracingFS);
  EXPECT_FALSE(PostWorker.status(MissingPath));
  EXPECT_EQ(TracingFS->NumStatusCalls.load(), StatsAfterBurst + 1);

  // No empty slot is left behind in the shared cache.
  EXPECT_FALSE(Service.getSharedCache()
                   .getShardForFilename(MissingPath)
                   .CacheByFilename.contains(MissingPath));
}

TEST(DependencyScanningWorkerFilesystem, LocalFS) {
  auto GlobalFS = llvm::makeIntrusiveRefCnt<llvm::vfs::InMemoryFileSystem>();
  GlobalFS->setCurrentWorkingDirectory("/");
  GlobalFS->addFile("/glob.c", 0, llvm::MemoryBuffer::getMemBuffer(""));

  DependencyScanningServiceOptions Opts;
  DependencyScanningService Service(std::move(Opts));

  {
    auto InstrGlobalFS =
        llvm::makeIntrusiveRefCnt<llvm::vfs::TracingFileSystem>(GlobalFS);
    auto LocalFS = llvm::makeIntrusiveRefCnt<llvm::vfs::InMemoryFileSystem>();
    LocalFS->setCurrentWorkingDirectory("/");
    LocalFS->addFile("/loc1.c", 0, llvm::MemoryBuffer::getMemBuffer(""));
    auto InstrLocalFS =
        llvm::makeIntrusiveRefCnt<llvm::vfs::TracingFileSystem>(LocalFS);

    DependencyScanningWorkerFilesystem DepFS(Service, InstrGlobalFS,
                                             InstrLocalFS);

    DepFS.status("/glob.c");
    EXPECT_EQ(InstrGlobalFS->NumStatusCalls, 1u);

    DepFS.status("/glob.c");
    EXPECT_EQ(InstrGlobalFS->NumStatusCalls, 1u); // Cached, no increase.

    // LocalFS got a stat call for "/glob.c".
    EXPECT_EQ(InstrLocalFS->NumStatusCalls, 1u);

    auto Stat = DepFS.status("/loc1.c");
    ASSERT_TRUE(Stat);
    EXPECT_EQ(InstrLocalFS->NumStatusCalls, 2u);

    DepFS.status("/loc1.c");
    EXPECT_EQ(InstrLocalFS->NumStatusCalls, 2u); // Cached, no increase.
  }

  {
    auto InstrGlobalFS =
        llvm::makeIntrusiveRefCnt<llvm::vfs::TracingFileSystem>(GlobalFS);
    auto LocalFS = llvm::makeIntrusiveRefCnt<llvm::vfs::InMemoryFileSystem>();
    LocalFS->setCurrentWorkingDirectory("/");
    auto InstrLocalFS =
        llvm::makeIntrusiveRefCnt<llvm::vfs::TracingFileSystem>(LocalFS);

    DependencyScanningWorkerFilesystem DepFS(Service, InstrGlobalFS,
                                             InstrLocalFS);

    DepFS.status("/glob.c");
    EXPECT_EQ(InstrGlobalFS->NumStatusCalls, 0u); // Cached, no increase.

    // LocalFS got a stat call for "/glob.c".
    EXPECT_EQ(InstrLocalFS->NumStatusCalls, 1u);

    auto Stat = DepFS.status("/loc1.c");
    ASSERT_FALSE(Stat);
    EXPECT_EQ(InstrLocalFS->NumStatusCalls, 2u); // not using global cache.
  }
}

namespace {
/// Returns real paths prefixed with "/local-real", to tell them apart from real
/// paths returned by other filesystems.
class LocalRealPathFileSystem : public llvm::vfs::ProxyFileSystem {
public:
  using ProxyFileSystem::ProxyFileSystem;

  std::error_code getRealPath(const llvm::Twine &Path,
                              llvm::SmallVectorImpl<char> &Output) override {
    llvm::SmallString<128> RealPath;
    if (std::error_code EC = ProxyFileSystem::getRealPath(Path, RealPath))
      return EC;
    llvm::StringRef Prefix = "/local-real";
    Output.assign(Prefix.begin(), Prefix.end());
    Output.append(RealPath.begin(), RealPath.end());
    return {};
  }
};
} // namespace

TEST(DependencyScanningWorkerFilesystem, LocalFSRealPath) {
  auto GlobalFS = llvm::makeIntrusiveRefCnt<llvm::vfs::InMemoryFileSystem>();
  GlobalFS->setCurrentWorkingDirectory("/");
  GlobalFS->addFile("/both.c", 0, llvm::MemoryBuffer::getMemBuffer(""));
  GlobalFS->addFile("/glob.c", 0, llvm::MemoryBuffer::getMemBuffer(""));

  auto LocalInMemoryFS =
      llvm::makeIntrusiveRefCnt<llvm::vfs::InMemoryFileSystem>();
  LocalInMemoryFS->setCurrentWorkingDirectory("/");
  LocalInMemoryFS->addFile("/both.c", 0, llvm::MemoryBuffer::getMemBuffer(""));
  LocalInMemoryFS->addFile("/loc.c", 0, llvm::MemoryBuffer::getMemBuffer(""));
  auto LocalFS =
      llvm::makeIntrusiveRefCnt<LocalRealPathFileSystem>(LocalInMemoryFS);

  DependencyScanningServiceOptions Opts;
  DependencyScanningService Service(std::move(Opts));

  auto GetRealPath = [](DependencyScanningWorkerFilesystem &DepFS,
                        llvm::StringRef Path) -> std::string {
    llvm::SmallString<128> Result;
    if (DepFS.getRealPath(Path, Result))
      return "<error>";
    // InMemoryFileSystem returns real paths with native separators.
    return llvm::sys::path::convert_to_slash(Result);
  };

  // Populate the shared cache for "/glob.c" from a worker without LocalFS.
  {
    DependencyScanningWorkerFilesystem DepFS(Service, GlobalFS);
    EXPECT_EQ(GetRealPath(DepFS, "/glob.c"), "/glob.c");
  }

  // LocalFS takes precedence over the global FS, like for status().
  {
    DependencyScanningWorkerFilesystem DepFS(Service, GlobalFS, LocalFS);
    EXPECT_EQ(GetRealPath(DepFS, "/loc.c"), "/local-real/loc.c");
    EXPECT_EQ(GetRealPath(DepFS, "/both.c"), "/local-real/both.c");
    EXPECT_EQ(GetRealPath(DepFS, "/glob.c"), "/glob.c");
  }

  // Real paths from LocalFS don't leak to other workers via the shared cache.
  {
    DependencyScanningWorkerFilesystem DepFS(Service, GlobalFS);
    EXPECT_EQ(GetRealPath(DepFS, "/loc.c"), "/loc.c");
    EXPECT_EQ(GetRealPath(DepFS, "/both.c"), "/both.c");
  }
}

TEST(DependencyScanningWorkerFilesystem, LocalCacheKeysOutliveQueries) {
  auto GlobalFS = llvm::makeIntrusiveRefCnt<llvm::vfs::InMemoryFileSystem>();
  GlobalFS->setCurrentWorkingDirectory("/");
  GlobalFS->addFile("/dir/global.h", 0, llvm::MemoryBuffer::getMemBuffer(""));
  auto LocalFS = llvm::makeIntrusiveRefCnt<llvm::vfs::InMemoryFileSystem>();
  LocalFS->setCurrentWorkingDirectory("/");
  LocalFS->addFile("/dir/local.h", 0, llvm::MemoryBuffer::getMemBuffer(""));

  auto InstrGlobalFS =
      llvm::makeIntrusiveRefCnt<llvm::vfs::TracingFileSystem>(GlobalFS);
  auto InstrLocalFS =
      llvm::makeIntrusiveRefCnt<llvm::vfs::TracingFileSystem>(LocalFS);

  DependencyScanningService Service({});
  DependencyScanningWorkerFilesystem DepFS(Service, InstrGlobalFS,
                                           InstrLocalFS);

  // The local caches must not refer to the strings passed in by the caller.
  // Overwrite each one after use, so that a dangling key makes the next query
  // miss the local cache and reach the underlying filesystems again.
  auto Query = [&](llvm::StringRef Path) {
    std::string OwnedPath = Path.str();
    EXPECT_TRUE(DepFS.status(OwnedPath));
    llvm::SmallString<128> RealPath;
    EXPECT_FALSE(DepFS.getRealPath(OwnedPath, RealPath));
    std::fill(OwnedPath.begin(), OwnedPath.end(), 'x');
  };
  auto Counts = [&] {
    return std::vector<std::size_t>{
        InstrGlobalFS->NumStatusCalls,      InstrGlobalFS->NumExistsCalls,
        InstrGlobalFS->NumGetRealPathCalls, InstrLocalFS->NumStatusCalls,
        InstrLocalFS->NumExistsCalls,       InstrLocalFS->NumGetRealPathCalls,
    };
  };

  Query("/dir/global.h");
  Query("/dir/local.h");
  auto CountsAfterFirstQueries = Counts();
  EXPECT_EQ(InstrGlobalFS->NumStatusCalls, 1u);
  EXPECT_EQ(InstrLocalFS->NumGetRealPathCalls, 1u);

  Query("/dir/global.h");
  Query("/dir/local.h");
  EXPECT_EQ(Counts(), CountsAfterFirstQueries);

  // Relative paths are made absolute in a temporary buffer before the lookup.
  ASSERT_FALSE(DepFS.setCurrentWorkingDirectory("/dir"));
  CountsAfterFirstQueries = Counts();
  Query("global.h");
  Query("./local.h");
  EXPECT_EQ(Counts(), CountsAfterFirstQueries);
}

TEST(DependencyScanningWorkerFilesystem, LocalFSDirectoryIteration) {
  auto GlobalFS = llvm::makeIntrusiveRefCnt<llvm::vfs::InMemoryFileSystem>();
  GlobalFS->setCurrentWorkingDirectory("/");
  GlobalFS->addFile("/dir/both", 0, llvm::MemoryBuffer::getMemBuffer(""));
  GlobalFS->addFile("/dir/glob.c", 0, llvm::MemoryBuffer::getMemBuffer(""));

  auto LocalFS = llvm::makeIntrusiveRefCnt<llvm::vfs::InMemoryFileSystem>();
  LocalFS->setCurrentWorkingDirectory("/");
  LocalFS->addFile("/dir/both/loc.c", 0, llvm::MemoryBuffer::getMemBuffer(""));

  DependencyScanningServiceOptions Opts;
  DependencyScanningService Service(std::move(Opts));
  DependencyScanningWorkerFilesystem DepFS(Service, GlobalFS, LocalFS);

  // "/dir/both" is a directory in LocalFS, which takes precedence.
  auto Stat = DepFS.status("/dir/both");
  ASSERT_TRUE(Stat);
  EXPECT_TRUE(Stat->isDirectory());

  // Directory iteration is consistent with status().
  std::error_code EC;
  llvm::StringMap<llvm::sys::fs::file_type> Entries;
  for (llvm::vfs::directory_iterator It = DepFS.dir_begin("/dir", EC), End;
       !EC && It != End; It.increment(EC))
    Entries[llvm::sys::path::filename(It->path())] = It->type();
  ASSERT_FALSE(EC);
  EXPECT_EQ(Entries.size(), 2u);
  EXPECT_EQ(Entries.lookup("both"), llvm::sys::fs::file_type::directory_file);
  EXPECT_EQ(Entries.lookup("glob.c"), llvm::sys::fs::file_type::regular_file);
}

namespace {
/// Options whose \c MakeVFS creates in-memory filesystems and counts them.
DependencyScanningServiceOptions
makePoolTestOptions(std::shared_ptr<std::atomic<unsigned>> NumVFSs) {
  DependencyScanningServiceOptions Opts;
  Opts.MakeVFS = [NumVFSs] {
    ++*NumVFSs;
    auto FS = llvm::makeIntrusiveRefCnt<llvm::vfs::InMemoryFileSystem>();
    FS->setCurrentWorkingDirectory("/");
    FS->addFile("/dir/file.h", 0, llvm::MemoryBuffer::getMemBuffer(""));
    return FS;
  };
  return Opts;
}
} // namespace

TEST(DependencyScanningWorkerFilesystemPool, ReusesReturnedFilesystems) {
  auto NumVFSs = std::make_shared<std::atomic<unsigned>>(0);
  DependencyScanningService Service(makePoolTestOptions(NumVFSs));
  DependencyScanningWorkerFilesystemPool &Pool =
      Service.getWorkerFilesystemPool();

  // Leases held at the same time get different filesystems.
  auto Lease1 = Pool.acquire(Service);
  auto Lease2 = Pool.acquire(Service);
  ASSERT_TRUE(Lease1.get() && Lease2.get());
  EXPECT_NE(Lease1.get(), Lease2.get());
  EXPECT_EQ(NumVFSs->load(), 2u);
  EXPECT_TRUE(Lease1->status("/dir/file.h"));
  EXPECT_EQ(Pool.getNumAvailable(), 0u);

  DependencyScanningWorkerFilesystem *FS1 = Lease1.get().get();
  DependencyScanningWorkerFilesystem *FS2 = Lease2.get().get();
  Lease1.reset();
  Lease2.reset();
  EXPECT_EQ(Pool.getNumAvailable(), 2u);

  // Later leases reuse them instead of creating new ones.
  auto Lease3 = Pool.acquire(Service);
  auto Lease4 = Pool.acquire(Service);
  EXPECT_EQ(NumVFSs->load(), 2u);
  EXPECT_EQ(Pool.getNumAvailable(), 0u);
  EXPECT_TRUE((Lease3.get() == FS1 && Lease4.get() == FS2) ||
              (Lease3.get() == FS2 && Lease4.get() == FS1));
  EXPECT_TRUE(Lease3->status("/dir/file.h"));

  // A moved-from lease doesn't return anything.
  DependencyScanningWorkerFilesystemPool::Lease Moved = std::move(Lease3);
  Lease3.reset();
  EXPECT_EQ(Pool.getNumAvailable(), 0u);
  Moved.reset();
  EXPECT_EQ(Pool.getNumAvailable(), 1u);
}

TEST(DependencyScanningWorkerFilesystemPool, DoesNotReuseModifiedFilesystems) {
  auto NumVFSs = std::make_shared<std::atomic<unsigned>>(0);
  DependencyScanningService Service(makePoolTestOptions(NumVFSs));
  DependencyScanningWorkerFilesystemPool &Pool =
      Service.getWorkerFilesystemPool();

  // A filesystem that's still referenced, e.g. by an overlay of the compile
  // that used it, isn't returned.
  {
    auto Lease = Pool.acquire(Service);
    auto Overlay =
        llvm::makeIntrusiveRefCnt<llvm::vfs::OverlayFileSystem>(Lease.get());
    Lease.reset();
    EXPECT_EQ(Pool.getNumAvailable(), 0u);
  }

  // Neither is one whose working directory changed.
  {
    auto Lease = Pool.acquire(Service);
    ASSERT_FALSE(Lease->setCurrentWorkingDirectory("/dir"));
    Lease.reset();
    EXPECT_EQ(Pool.getNumAvailable(), 0u);
  }

  // Nor a discarded one.
  {
    auto Lease = Pool.acquire(Service);
    Lease.discard();
    Lease.reset();
    EXPECT_EQ(Pool.getNumAvailable(), 0u);
  }
  EXPECT_EQ(NumVFSs->load(), 3u);

  // Changing the working directory and changing it back is fine.
  {
    auto Lease = Pool.acquire(Service);
    ASSERT_FALSE(Lease->setCurrentWorkingDirectory("/dir"));
    ASSERT_FALSE(Lease->setCurrentWorkingDirectory("/"));
    Lease.reset();
    EXPECT_EQ(Pool.getNumAvailable(), 1u);
  }
}

TEST(DependencyScanningWorkerFilesystemPool, LeasesAreExclusive) {
  auto NumVFSs = std::make_shared<std::atomic<unsigned>>(0);
  DependencyScanningService Service(makePoolTestOptions(NumVFSs));
  DependencyScanningWorkerFilesystemPool &Pool =
      Service.getWorkerFilesystemPool();

  constexpr unsigned NumThreads = 8;
  constexpr unsigned NumIterations = 200;
  std::mutex Mutex;
  std::set<DependencyScanningWorkerFilesystem *> InUse;
  std::atomic<bool> Overlapped = false;
  StartBarrier Barrier;
  std::vector<std::thread> Threads;
  for (unsigned T = 0; T < NumThreads; ++T)
    Threads.emplace_back([&] {
      Barrier.wait();
      for (unsigned I = 0; I < NumIterations; ++I) {
        auto Lease = Pool.acquire(Service);
        {
          std::lock_guard<std::mutex> Guard(Mutex);
          if (!InUse.insert(Lease.get().get()).second)
            Overlapped = true;
        }
        // Use the filesystem (and its local caches) without synchronization.
        EXPECT_TRUE(Lease->status("/dir/file.h"));
        EXPECT_FALSE(Lease->status("/dir/missing.h"));
        {
          std::lock_guard<std::mutex> Guard(Mutex);
          InUse.erase(Lease.get().get());
        }
      }
    });
  Barrier.release();
  for (std::thread &Thread : Threads)
    Thread.join();

  EXPECT_FALSE(Overlapped);
  // Never more filesystems than leases held at the same time.
  EXPECT_LE(NumVFSs->load(), NumThreads);
  EXPECT_EQ(Pool.getNumAvailable(), NumVFSs->load());
}
