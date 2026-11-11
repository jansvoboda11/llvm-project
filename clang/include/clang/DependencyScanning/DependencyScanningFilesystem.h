//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_DEPENDENCYSCANNING_DEPENDENCYSCANNINGFILESYSTEM_H
#define LLVM_CLANG_DEPENDENCYSCANNING_DEPENDENCYSCANNINGFILESYSTEM_H

#include "clang/Basic/LLVM.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/STLFunctionalExtras.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/Support/Allocator.h"
#include "llvm/Support/ErrorOr.h"
#include "llvm/Support/VirtualFileSystem.h"
#include <atomic>
#include <memory>
#include <mutex>
#include <optional>
#include <variant>

namespace clang {
namespace dependencies {

class DependencyScanningService;

/// Data that a client of the scanning VFS attaches to the contents of a cached
/// file, e.g. the result of scanning them.
///
/// Each kind of attachment is identified by the address of the static \c ID
/// member of the class deriving from this one. An attachment is created at most
/// once for the given contents, never changes afterwards, and lives as long as
/// the contents.
class CachedFileAttachment {
public:
  virtual ~CachedFileAttachment() = default;

private:
  friend struct CachedFileContents;

  /// The kind of this attachment.
  const void *Kind = nullptr;

  /// The next attachment of the same contents.
  CachedFileAttachment *Next = nullptr;
};

/// Contents of a cached file entry and their attachments. Single instance can
/// be shared between multiple entries.
struct CachedFileContents {
  CachedFileContents(std::unique_ptr<llvm::MemoryBuffer> Contents)
      : Original(std::move(Contents)) {}

  ~CachedFileContents();

  /// Owning storage for the original contents.
  std::unique_ptr<llvm::MemoryBuffer> Original;

  /// Returns the attachment of the given kind, creating it with \p Create first
  /// if needed. Concurrent calls for the same kind call \p Create only once.
  /// \p Create must not create attachments for the same contents.
  const CachedFileAttachment &getOrCreateAttachment(
      const void *Kind,
      llvm::function_ref<std::unique_ptr<CachedFileAttachment>()> Create);

private:
  /// The list of attachments. Attachments are only ever prepended (while
  /// holding \c AttachmentsLock), so the list can be read without holding any
  /// lock.
  std::atomic<CachedFileAttachment *> Attachments = nullptr;

  /// Serializes creating attachments.
  std::mutex AttachmentsLock;
};

/// An in-memory representation of a file system entity that is of interest to
/// the dependency scanning filesystem.
///
/// It represents one of the following:
/// - opened file with contents and a stat value,
/// - directory entry with its stat value,
/// - filesystem error.
///
/// Single instance of this class can be shared across different filenames (e.g.
/// a regular file and a symlink). For this reason the status filename is empty
/// and is only materialized by \c EntryRef that knows the requested filename.
class CachedFileSystemEntry {
public:
  /// Creates an entry without contents: either a filesystem error or
  /// a directory with stat value.
  CachedFileSystemEntry(llvm::ErrorOr<llvm::vfs::Status> Stat)
      : MaybeStat(std::move(Stat)), Contents(nullptr) {
    clearStatName();
  }

  /// Creates an entry representing a file with contents.
  CachedFileSystemEntry(llvm::ErrorOr<llvm::vfs::Status> Stat,
                        CachedFileContents *Contents)
      : MaybeStat(std::move(Stat)), Contents(std::move(Contents)) {
    clearStatName();
  }

  /// \returns True if the entry is a filesystem error.
  bool isError() const { return !MaybeStat; }

  /// \returns True if the current entry represents a directory.
  bool isDirectory() const { return !isError() && MaybeStat->isDirectory(); }

  /// \returns Original contents of the file.
  StringRef getOriginalContents() const {
    assert(!isError() && "error");
    assert(!MaybeStat->isDirectory() && "not a file");
    assert(Contents && "contents not initialized");
    return Contents->Original->getBuffer();
  }

  /// \returns The error.
  std::error_code getError() const { return MaybeStat.getError(); }

  /// \returns The entry status with empty filename.
  llvm::vfs::Status getStatus() const {
    assert(!isError() && "error");
    assert(MaybeStat->getName().empty() && "stat name must be empty");
    return *MaybeStat;
  }

  /// \returns The unique ID of the entry.
  llvm::sys::fs::UniqueID getUniqueID() const {
    assert(!isError() && "error");
    return MaybeStat->getUniqueID();
  }

  /// \returns The data structure holding both contents and their attachments.
  CachedFileContents *getCachedContents() const {
    assert(!isError() && "error");
    assert(!isDirectory() && "not a file");
    return Contents;
  }

private:
  void clearStatName() {
    if (MaybeStat)
      MaybeStat = llvm::vfs::Status::copyWithNewName(*MaybeStat, "");
  }

  /// Either the filesystem error or status of the entry.
  /// The filename is empty and only materialized by \c EntryRef.
  llvm::ErrorOr<llvm::vfs::Status> MaybeStat;

  /// Non-owning pointer to the file contents.
  ///
  /// We're using pointer here to keep the size of this class small. Instances
  /// representing directories and filesystem errors don't hold any contents
  /// anyway.
  CachedFileContents *Contents;
};

using CachedRealPath = llvm::ErrorOr<std::string>;

/// This class is a shared cache, that caches the 'stat' and 'open' calls to the
/// underlying real file system, and the scanned preprocessor directives of
/// files.
///
/// It is sharded based on the hash of the key to reduce the lock contention for
/// the worker threads.
class DependencyScanningFilesystemSharedCache {
public:
  struct CacheShard {
    /// The mutex that needs to be locked before mutation of the maps,
    /// allocators and real paths. Never held while locking any other mutex.
    /// Lookups only hold it for short periods of time and never while doing
    /// IO (unlike \c getOutOfDateEntries(), which runs after the scan).
    mutable std::mutex CacheLock;

    /// Cache entry per filename.
    struct FilenameCacheEntry {
      /// The cached file. Set at most once while holding \c Mutex, and can be
      /// read without holding any lock.
      std::atomic<const CachedFileSystemEntry *> File = nullptr;

      /// Serializes lookups of this filename that don't find \c File, so that
      /// only one of them stats the file and the others reuse its result.
      std::mutex Mutex;

      const CachedRealPath *RealPath = nullptr;
    };

    /// Cache entry per unique ID.
    struct UIDCacheEntry {
      /// The cached file. Set at most once while holding \c Mutex, and can be
      /// read without holding any lock.
      std::atomic<const CachedFileSystemEntry *> File = nullptr;

      /// Serializes lookups of this file via different filenames that don't
      /// find \c File, so that only one of them reads the file and the others
      /// reuse its result.
      std::mutex Mutex;
    };

    /// Map from filenames to their cache entries.
    llvm::StringMap<FilenameCacheEntry, llvm::BumpPtrAllocator> CacheByFilename;

    /// Map from unique IDs to their cache entries.
    llvm::DenseMap<llvm::sys::fs::UniqueID, UIDCacheEntry *> EntriesByUID;

    /// The backing storage for cache entries in \c EntriesByUID.
    llvm::SpecificBumpPtrAllocator<UIDCacheEntry> UIDCacheEntryStorage;

    /// The backing storage for cached entries.
    llvm::SpecificBumpPtrAllocator<CachedFileSystemEntry> EntryStorage;

    /// The backing storage for cached contents.
    llvm::SpecificBumpPtrAllocator<CachedFileContents> ContentsStorage;

    /// The backing storage for cached real paths.
    llvm::SpecificBumpPtrAllocator<CachedRealPath> RealPathStorage;
  };

  DependencyScanningFilesystemSharedCache();

  /// Returns shard for the given key.
  CacheShard &getShardForFilename(StringRef Filename) const;
  CacheShard &getShardForUID(llvm::sys::fs::UniqueID UID) const;

  struct OutOfDateEntry {
    // A null terminated string that contains a path.
    const char *Path = nullptr;

    struct NegativelyCachedInfo {};
    struct SizeChangedInfo {
      uint64_t CachedSize = 0;
      uint64_t ActualSize = 0;
    };

    std::variant<NegativelyCachedInfo, SizeChangedInfo> Info;

    OutOfDateEntry(const char *Path)
        : Path(Path), Info(NegativelyCachedInfo{}) {}

    OutOfDateEntry(const char *Path, uint64_t CachedSize, uint64_t ActualSize)
        : Path(Path), Info(SizeChangedInfo{CachedSize, ActualSize}) {}
  };

  /// Visits all cached entries and re-stat an entry using UnderlyingFS to check
  /// if the cache contains out-of-date entries. An entry can be out-of-date for
  /// two reasons:
  ///  1. The entry contains a stat error, indicating the file did not exist
  ///     in the cache, but the file exists on the UnderlyingFS.
  ///  2. The entry is associated with a file whose size is different from the
  ///     size of the file on the same path on the UnderlyingFS.
  std::vector<OutOfDateEntry>
  getOutOfDateEntries(llvm::vfs::FileSystem &UnderlyingFS) const;

private:
  std::unique_ptr<CacheShard[]> CacheShards;
  unsigned NumShards;
};

/// Reference to a CachedFileSystemEntry.
/// If the underlying entry is an opened file, this wrapper returns the file
/// contents and their attachments.
class EntryRef {
  /// The filename used to access this entry.
  std::string Filename;

  /// The underlying cached entry.
  const CachedFileSystemEntry &Entry;

public:
  EntryRef(StringRef Name, const CachedFileSystemEntry &Entry)
      : Filename(Name), Entry(Entry) {}

  llvm::vfs::Status getStatus() const {
    llvm::vfs::Status Stat = Entry.getStatus();
    if (!Stat.isDirectory())
      Stat = llvm::vfs::Status::copyWithNewSize(Stat, getContents().size());
    return llvm::vfs::Status::copyWithNewName(Stat, Filename);
  }

  bool isError() const { return Entry.isError(); }
  bool isDirectory() const { return Entry.isDirectory(); }

  /// If the cached entry represents an error, promotes it into `ErrorOr`.
  llvm::ErrorOr<EntryRef> unwrapError() const {
    if (isError())
      return Entry.getError();
    return *this;
  }

  StringRef getContents() const { return Entry.getOriginalContents(); }

  /// Returns the attachment of type \p T to the contents of this file,
  /// creating it with \p Create first if needed. \p T must derive from
  /// \c CachedFileAttachment and have a \c static \c const \c char \c ID
  /// member. See \c CachedFileContents::getOrCreateAttachment().
  template <typename T>
  const T &getOrCreateAttachment(
      llvm::function_ref<std::unique_ptr<T>(StringRef Contents)> Create) const {
    StringRef Contents = getContents();
    return static_cast<const T &>(
        Entry.getCachedContents()->getOrCreateAttachment(
            &T::ID, [&]() -> std::unique_ptr<CachedFileAttachment> {
              return Create(Contents);
            }));
  }
};

/// A virtual file system optimized for the dependency discovery.
///
/// It is primarily designed to work with source files whose contents was
/// preprocessed to remove any tokens that are unlikely to affect the dependency
/// computation.
///
/// This is not a thread safe VFS. A single instance is meant to be used only in
/// one thread. Multiple instances are allowed to service multiple threads
/// running in parallel.
class DependencyScanningWorkerFilesystem
    : public llvm::RTTIExtends<DependencyScanningWorkerFilesystem,
                               llvm::vfs::ProxyFileSystem> {
public:
  static const char ID;

  DependencyScanningWorkerFilesystem(
      DependencyScanningService &Service,
      IntrusiveRefCntPtr<llvm::vfs::FileSystem> FS,
      IntrusiveRefCntPtr<llvm::vfs::FileSystem> LocalFS = nullptr);

  llvm::ErrorOr<llvm::vfs::Status> status(const Twine &Path) override;
  llvm::ErrorOr<std::unique_ptr<llvm::vfs::File>>
  openFileForRead(const Twine &Path) override;

  std::error_code getRealPath(const Twine &Path,
                              SmallVectorImpl<char> &Output) override;

  std::error_code setCurrentWorkingDirectory(const Twine &Path) override;

  /// Returns entry for the given filename.
  ///
  /// Attempts to use the local and shared caches first, then falls back to
  /// using the underlying filesystem.
  llvm::ErrorOr<EntryRef> getOrCreateFileSystemEntry(StringRef Filename);

  /// Check whether \p Path exists. By default checks cached result of \c
  /// status(), and falls back on FS if unable to do so.
  bool exists(const Twine &Path) override;

private:
  /// Represents a filesystem entry that has been stat-ed (and potentially read)
  /// and that's about to be inserted into the cache as `CachedFileSystemEntry`.
  struct TentativeEntry {
    llvm::vfs::Status Status;
    std::unique_ptr<llvm::MemoryBuffer> Contents;

    TentativeEntry(llvm::vfs::Status Status,
                   std::unique_ptr<llvm::MemoryBuffer> Contents = nullptr)
        : Status(std::move(Status)), Contents(std::move(Contents)) {}
  };

  /// Reads file at the given path. Enforces consistency between the file size
  /// in status and size of read contents.
  static llvm::ErrorOr<TentativeEntry> readFile(StringRef Filename,
                                                llvm::vfs::FileSystem &FS);

  void printImpl(raw_ostream &OS, PrintType Type,
                 unsigned IndentLevel) const override {
    printIndent(OS, IndentLevel);
    OS << "DependencyScanningFilesystem\n";
    getUnderlyingFS().print(OS, Type, IndentLevel + 1);
  }

  /// The service associated with this VFS.
  DependencyScanningService &Service;

  /// Per-filename state cached locally by this worker thread, so repeated
  /// queries can be served without touching the shared cache. The entry and
  /// real path are arena-owned by the shared cache and outlive this worker, so
  /// borrowing raw pointers here is safe.
  struct LocalEntry {
    const CachedFileSystemEntry *File = nullptr;
    const CachedRealPath *RealPath = nullptr;
  };

  /// The local cache is used by the worker thread to cache file system queries
  /// locally instead of querying the global cache every time.
  llvm::StringMap<LocalEntry, llvm::BumpPtrAllocator> LocalCache;

  /// The working directory to use for making relative paths absolute before
  /// using them for cache lookups.
  llvm::ErrorOr<std::string> WorkingDirForCacheLookup;

  /// If non-null it is used to resolve files without going through the global
  /// shared cache. This is useful if the caller wants to use a specific VFS
  /// for a compiler invocation
  IntrusiveRefCntPtr<llvm::vfs::FileSystem> LocalFS;

  /// The FS that is used for queries going through the global shared cache.
  IntrusiveRefCntPtr<llvm::vfs::FileSystem> BaseFS;

  /// The backing storage for cached entries resolved from local FS only.
  llvm::SpecificBumpPtrAllocator<CachedFileSystemEntry> LocalEntryStorage;

  /// The backing storage for cached contents resolved from local FS only.
  llvm::SpecificBumpPtrAllocator<CachedFileContents> LocalContentsStorage;

  static const CachedFileSystemEntry *createEntry(
      llvm::ErrorOr<TentativeEntry> &Entry,
      llvm::SpecificBumpPtrAllocator<CachedFileContents> &ContentsStorage,
      llvm::SpecificBumpPtrAllocator<CachedFileSystemEntry> &EntryStorage);

  void updateWorkingDirForCacheLookup();

  llvm::ErrorOr<StringRef>
  tryGetFilenameForLookup(StringRef OriginalFilename,
                          llvm::SmallVectorImpl<char> &PathBuf) const;
};

} // end namespace dependencies
} // end namespace clang

#endif // LLVM_CLANG_DEPENDENCYSCANNING_DEPENDENCYSCANNINGFILESYSTEM_H
