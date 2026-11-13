#include "clang/DependencyScanning/InProcessModuleCache.h"

#include "clang/Basic/AtomicLineLogger.h"
#include "llvm/Support/AdvisoryLock.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Path.h"
#include "llvm/Testing/Support/Error.h"

#include "gtest/gtest.h"

#include <chrono>
#include <thread>

using namespace clang;
using namespace clang::dependencies;

TEST(InProcessModuleCache, ReadWriteInvalidation) {
  ModuleCacheEntries Entries;
  AtomicLineLogger Logger;
  std::shared_ptr<ModuleCache> ModCache =
      makeInProcessModuleCache(Entries, Logger);

  int FD;
  llvm::SmallString<256> Path;
  ASSERT_FALSE(llvm::sys::fs::createTemporaryFile("M", "pcm", FD, Path));

  {
    llvm::raw_fd_ostream OS(FD, /*shouldClose=*/true);
    OS << "original\n";
  }

  off_t Size;
  time_t ModTime;
  std::unique_ptr<llvm::MemoryBuffer> Buf;
  ASSERT_THAT_ERROR(ModCache->read(Path, Size, ModTime).moveInto(Buf),
                    llvm::Succeeded());
  EXPECT_EQ(Buf->getBuffer(), "original\n");

  auto NewBuf = llvm::MemoryBuffer::getMemBufferCopy("modified\n", Path);
  ASSERT_EQ(ModCache->write(Path, std::move(NewBuf), Size, ModTime).getError(),
            std::error_code{});

  // Writing a new buffer should not invalidate the previously-read buffer.
  EXPECT_EQ(Buf->getBuffer(), "original\n");
}

TEST(InProcessModuleCache, ReadReadInvalidation) {
  ModuleCacheEntries Entries;
  AtomicLineLogger Logger;
  std::shared_ptr<ModuleCache> ModCache =
      makeInProcessModuleCache(Entries, Logger);

  int FD;
  llvm::SmallString<256> Path;
  ASSERT_FALSE(llvm::sys::fs::createTemporaryFile("M", "pcm", FD, Path));

  {
    llvm::raw_fd_ostream OS(FD, /*shouldClose=*/true);
    OS << "original\n";
  }

  off_t Size1;
  time_t ModTime1;
  std::unique_ptr<llvm::MemoryBuffer> Buf1;
  ASSERT_THAT_ERROR(ModCache->read(Path, Size1, ModTime1).moveInto(Buf1),
                    llvm::Succeeded());
  EXPECT_EQ(Buf1->getBuffer(), "original\n");

  off_t Size2;
  time_t ModTime2;
  std::unique_ptr<llvm::MemoryBuffer> Buf2;
  ASSERT_THAT_ERROR(ModCache->read(Path, Size2, ModTime2).moveInto(Buf2),
                    llvm::Succeeded());
  EXPECT_EQ(Buf2->getBuffer(), "original\n");

  // Subsequent reads should not invalidate previous reads.
  EXPECT_EQ(Buf1->getBuffer(), "original\n");
  // All read buffers should point to the same memory.
  EXPECT_EQ(Buf1->getBuffer().begin(), Buf2->getBuffer().begin());
  EXPECT_EQ(Buf1->getBuffer().end(), Buf2->getBuffer().end());
}

TEST(InProcessModuleCache, WriteReturnsSharedBuffer) {
  ModuleCacheEntries Entries;
  AtomicLineLogger Logger;
  std::shared_ptr<ModuleCache> ModCache =
      makeInProcessModuleCache(Entries, Logger);

  llvm::SmallString<256> Path;
  llvm::sys::fs::createUniquePath("M-%%%%%%.pcm", Path, /*MakeAbsolute=*/true);

  off_t Size;
  time_t ModTime;
  std::unique_ptr<llvm::MemoryBuffer> Written;
  {
    std::string Contents = "contents\n";
    auto WrittenOrErr = ModCache->write(
        Path, llvm::MemoryBuffer::getMemBufferCopy(Contents, Path), Size,
        ModTime);
    ASSERT_EQ(WrittenOrErr.getError(), std::error_code{});
    Written = std::move(*WrittenOrErr);
  }
  // The returned buffer doesn't reference the written contents.
  ASSERT_TRUE(Written);
  EXPECT_EQ(Written->getBuffer(), "contents\n");

  // Reads share the memory of the written buffer.
  std::unique_ptr<llvm::MemoryBuffer> Read;
  ASSERT_THAT_ERROR(ModCache->read(Path, Size, ModTime).moveInto(Read),
                    llvm::Succeeded());
  EXPECT_EQ(Read->getBufferStart(), Written->getBufferStart());

  // Writing the same PCM again shares the memory of the first write.
  auto RewrittenOrErr = ModCache->write(
      Path, llvm::MemoryBuffer::getMemBufferCopy("contents\n", Path), Size,
      ModTime);
  ASSERT_EQ(RewrittenOrErr.getError(), std::error_code{});
  ASSERT_TRUE(*RewrittenOrErr);
  EXPECT_EQ((*RewrittenOrErr)->getBufferStart(), Written->getBufferStart());
}

TEST(InProcessModuleCache, FlushInBackground) {
  AtomicLineLogger Logger;
  ModuleCacheEntries Entries;
  Entries.startFlushing(Logger);
  std::shared_ptr<ModuleCache> ModCache =
      makeInProcessModuleCache(Entries, Logger);

  llvm::SmallString<256> Dir;
  ASSERT_FALSE(llvm::sys::fs::createUniqueDirectory("cache", Dir));
  llvm::SmallString<256> Path(Dir);
  llvm::sys::path::append(Path, "M.pcm");

  // Large enough for the buffer to be memory-mapped when read back.
  std::string Contents(1024 * 1024, 'm');
  off_t Size;
  time_t WrittenModTime;
  auto WrittenOrErr = ModCache->write(
      Path, llvm::MemoryBuffer::getMemBufferCopy(Contents, Path), Size,
      WrittenModTime);
  ASSERT_EQ(WrittenOrErr.getError(), std::error_code{});
  std::unique_ptr<llvm::MemoryBuffer> Written = std::move(*WrittenOrErr);

  Entries.stopFlushing();

  // The PCM got written to disk without an explicit flush.
  EXPECT_TRUE(llvm::sys::fs::exists(Path));

  // Flushing should not invalidate the previously-returned buffer.
  EXPECT_EQ(Written->getBuffer(), Contents);

  // The flushed PCM is now memory-mapped from disk, with the same metadata.
  time_t ModTime;
  std::unique_ptr<llvm::MemoryBuffer> Read;
  ASSERT_THAT_ERROR(ModCache->read(Path, Size, ModTime).moveInto(Read),
                    llvm::Succeeded());
  EXPECT_EQ(Read->getBuffer(), Contents);
  EXPECT_EQ(Read->getBufferKind(), llvm::MemoryBuffer::MemoryBuffer_MMap);
  EXPECT_EQ(Size, off_t(Contents.size()));
  EXPECT_EQ(ModTime, WrittenModTime);

  // Writing the same contents again keeps the PCM flushed.
  ASSERT_EQ(ModCache
                ->write(Path,
                        llvm::MemoryBuffer::getMemBufferCopy(Contents, Path),
                        Size, ModTime)
                .getError(),
            std::error_code{});
  EXPECT_EQ(ModTime, WrittenModTime);

  llvm::sys::fs::remove_directories(Dir);
}

TEST(InProcessModuleCache, WaitDetectsDeadlock) {
  ModuleCacheEntries Entries;
  AtomicLineLogger Logger;
  // Two compilations, each holding the lock of one PCM and waiting for the
  // other one.
  std::shared_ptr<ModuleCache> CacheA =
      makeInProcessModuleCache(Entries, Logger);
  std::shared_ptr<ModuleCache> CacheB =
      makeInProcessModuleCache(Entries, Logger);

  auto LockAM = CacheA->getLock("M.pcm");
  auto LockBN = CacheB->getLock("N.pcm");
  ASSERT_THAT_EXPECTED(LockAM->tryLock(), llvm::HasValue(true));
  ASSERT_THAT_EXPECTED(LockBN->tryLock(), llvm::HasValue(true));

  auto LockAN = CacheA->getLock("N.pcm");
  auto LockBM = CacheB->getLock("M.pcm");
  ASSERT_THAT_EXPECTED(LockAN->tryLock(), llvm::HasValue(false));
  ASSERT_THAT_EXPECTED(LockBM->tryLock(), llvm::HasValue(false));

  // Whoever starts waiting last closes the cycle and must not wait. The other
  // one gets unblocked when the lock it waits for is released.
  auto Start = std::chrono::steady_clock::now();
  llvm::WaitForUnlockResult ResultA;
  std::thread WaiterA([&] {
    ResultA = LockAN->waitForUnlockFor(std::chrono::seconds(60));
    if (ResultA == llvm::WaitForUnlockResult::Timeout)
      LockAM.reset();
  });
  llvm::WaitForUnlockResult ResultB =
      LockBM->waitForUnlockFor(std::chrono::seconds(60));
  if (ResultB == llvm::WaitForUnlockResult::Timeout)
    LockBN.reset();
  WaiterA.join();
  auto Elapsed = std::chrono::steady_clock::now() - Start;

  EXPECT_NE(ResultA, ResultB);
  EXPECT_TRUE(ResultA == llvm::WaitForUnlockResult::Timeout ||
              ResultB == llvm::WaitForUnlockResult::Timeout);
  EXPECT_LT(Elapsed, std::chrono::seconds(30));
}

TEST(InProcessModuleCache, WaitWithoutDeadlock) {
  ModuleCacheEntries Entries;
  AtomicLineLogger Logger;
  std::shared_ptr<ModuleCache> CacheA =
      makeInProcessModuleCache(Entries, Logger);
  std::shared_ptr<ModuleCache> CacheB =
      makeInProcessModuleCache(Entries, Logger);

  // B waits for M held by A, which doesn't wait for anything held by B.
  auto LockAM = CacheA->getLock("M.pcm");
  ASSERT_THAT_EXPECTED(LockAM->tryLock(), llvm::HasValue(true));
  auto LockBM = CacheB->getLock("M.pcm");
  ASSERT_THAT_EXPECTED(LockBM->tryLock(), llvm::HasValue(false));

  std::thread Releaser([&] {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    LockAM.reset();
  });
  EXPECT_EQ(LockBM->waitForUnlockFor(std::chrono::seconds(60)),
            llvm::WaitForUnlockResult::Success);
  Releaser.join();
}
