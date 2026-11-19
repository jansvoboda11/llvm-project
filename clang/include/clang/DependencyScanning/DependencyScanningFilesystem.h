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
#include "clang/Lex/DependencyDirectivesScanner.h"
#include "llvm/ADT/CachedHashString.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/Support/Allocator.h"
#include "llvm/Support/ErrorOr.h"
#include "llvm/Support/TrailingObjects.h"
#include "llvm/Support/VirtualFileSystem.h"
#include <condition_variable>
#include <memory>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace clang {
namespace dependencies {

class DependencyScanningService;

/// The scanned dependency directives of a file and the tokens they refer to,
/// stored exactly sized in a single allocation that's never resized.
class CachedDependencyDirectives final
    : private llvm::TrailingObjects<CachedDependencyDirectives,
                                    dependency_directives_scan::Directive,
                                    dependency_directives_scan::Token> {
  friend TrailingObjects;

  /// Whether the scan succeeded.
  bool Success;
  unsigned NumDirectives;

  CachedDependencyDirectives(bool Success, unsigned NumDirectives)
      : Success(Success), NumDirectives(NumDirectives) {}

  size_t numTrailingObjects(
      OverloadToken<dependency_directives_scan::Directive>) const {
    return NumDirectives;
  }

public:
  /// Creates the result of a successful scan. Copies \p Tokens and
  /// \p Directives, which must refer to \p Tokens, into a single allocation,
  /// and makes the copied directives refer to the copied tokens.
  static std::unique_ptr<CachedDependencyDirectives>
  create(ArrayRef<dependency_directives_scan::Token> Tokens,
         ArrayRef<dependency_directives_scan::Directive> Directives);

  /// Creates the result of a failed scan.
  static std::unique_ptr<CachedDependencyDirectives> createFailed();

  /// \returns The directives if the scan succeeded.
  std::optional<ArrayRef<dependency_directives_scan::Directive>>
  getDirectives() const {
    if (!Success)
      return std::nullopt;
    return ArrayRef(getTrailingObjects<dependency_directives_scan::Directive>(),
                    NumDirectives);
  }

  void operator delete(void *Ptr) { ::operator delete(Ptr); }
};

/// Contents and directive tokens of a cached file entry. Single instance can
/// be shared between multiple entries.
struct CachedFileContents {
  CachedFileContents(std::unique_ptr<llvm::MemoryBuffer> Contents)
      : Original(std::move(Contents)), DepDirectives(nullptr) {}

  /// Owning storage for the original contents. The buffer has no name.
  std::unique_ptr<llvm::MemoryBuffer> Original;

  /// Accessor to the scanned directives that's atomic to avoid data races.
  /// Scanning for directives is guarded by the shared cache's
  /// \c DependencyScanningFilesystemSharedCache::getDirectivesLock().
  /// \p CachedFileContents has ownership of the pointer.
  std::atomic<const CachedDependencyDirectives *> DepDirectives;

  ~CachedFileContents() { delete DepDirectives.load(); }
};

/// An in-memory representation of a file system entity that is of interest to
/// the dependency scanning filesystem.
///
/// It represents one of the following:
/// - opened file with contents and a stat value,
/// - opened file with contents, directive tokens and a stat value,
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

  /// \returns The scanned preprocessor directive tokens of the file that are
  /// used to speed up preprocessing, if available.
  std::optional<ArrayRef<dependency_directives_scan::Directive>>
  getDirectiveTokens() const {
    assert(!isError() && "error");
    assert(!isDirectory() && "not a file");
    assert(Contents && "contents not initialized");
    if (auto *Directives = Contents->DepDirectives.load())
      return Directives->getDirectives();
    return std::nullopt;
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

  /// \returns The data structure holding both contents and directive tokens.
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
  /// In-flight slot used to dedup concurrent producers for the same key.
  /// The producer publishes via \c publish(); waiters block on \c Mutex/CV
  /// until \c Done is set. \c Result holds the resolved entry, or an
  /// uncached-negative error shared with overlapping waiters but not
  /// persisted in the shard.
  struct InProgressEntry {
    std::mutex Mutex;
    std::condition_variable CondVar;
    bool Done = false;
    llvm::ErrorOr<const CachedFileSystemEntry *> Result = std::error_code{};

    /// Publishes the producer's outcome to this slot and wakes all waiters.
    void publish(llvm::ErrorOr<const CachedFileSystemEntry *> R) {
      {
        std::lock_guard<std::mutex> EntryLock(Mutex);
        assert(!Done && "slot already published");
        Result = R;
        Done = true;
      }
      CondVar.notify_all();
    }
  };

  struct CacheShard {
    /// The mutex that needs to be locked before mutation of any member.
    mutable std::mutex CacheLock;

    /// Cache state per filename: resolved entry, real path, and an in-flight
    /// slot (if any). \c InProgress is reset on publish.
    struct FilenameCacheState {
      const CachedFileSystemEntry *Entry = nullptr;
      const CachedRealPath *RealPath = nullptr;
      std::shared_ptr<InProgressEntry> InProgress;
    };

    /// Cache state stored per unique ID; similar to
    /// \c FilenameCacheState.
    struct UIDCacheState {
      const CachedFileSystemEntry *Entry = nullptr;
      std::shared_ptr<InProgressEntry> InProgress;
    };

    /// Map from filenames to their cached state.
    ///
    /// The entries are allocated individually, so that the slots of uncached
    /// negative stats can be freed once the stat has been resolved. Slots with
    /// an entry or a real path are never erased, and \c StringMap never moves
    /// its entries, so their keys live as long as the shard.
    llvm::StringMap<FilenameCacheState> CacheByFilename;

    /// Map from unique IDs to their cached state.
    llvm::DenseMap<llvm::sys::fs::UniqueID, UIDCacheState> EntriesByUID;

    /// The backing storage for cached entries.
    llvm::SpecificBumpPtrAllocator<CachedFileSystemEntry> EntryStorage;

    /// The backing storage for cached contents.
    llvm::SpecificBumpPtrAllocator<CachedFileContents> ContentsStorage;

    /// The backing storage for cached real paths.
    llvm::SpecificBumpPtrAllocator<CachedRealPath> RealPathStorage;

    /// Returns the real path associated with the filename or nullptr if none is
    /// found. If found, sets \p StoredFilename to the copy of \p Filename owned
    /// by this shard, which lives as long as the shard.
    const CachedRealPath *
    findRealPathByFilename(StringRef Filename, StringRef &StoredFilename) const;

    /// Returns the real path associated with the filename if there is some.
    /// Otherwise, constructs new one with the given one, associates it with the
    /// filename and returns the result. Sets \p StoredFilename to the copy of
    /// \p Filename owned by this shard, which lives as long as the shard.
    const CachedRealPath &
    getOrEmplaceRealPathForFilename(StringRef Filename,
                                    llvm::ErrorOr<StringRef> RealPath,
                                    StringRef &StoredFilename);
  };

  DependencyScanningFilesystemSharedCache();

  /// Returns shard for the given key.
  CacheShard &getShardForFilename(StringRef Filename) const;
  CacheShard &getShardForUID(llvm::sys::fs::UniqueID UID) const;

  /// Returns the mutex that must be locked before scanning \p Contents for
  /// directives. The mutexes are striped, so that the cached contents don't
  /// each need their own.
  std::mutex &getDirectivesLock(const CachedFileContents &Contents) const;

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

  /// Records that nothing gets created in the absolute directory \p Dir while
  /// this cache is alive, e.g. because it's an SDK or the compiler's resource
  /// directory. Missing files in it are then cached, independently of
  /// \c DependencyScanningServiceOptions::CacheNegativeStats. Ignores relative
  /// paths and root directories.
  void addStableDirectory(StringRef Dir);

  /// Returns whether \p Path is spelled as a path in a directory passed to
  /// \c addStableDirectory(). Paths with ".." components never are.
  bool isInStableDirectory(StringRef Path) const;

private:
  std::unique_ptr<CacheShard[]> CacheShards;
  unsigned NumShards;

  /// The directories passed to \c addStableDirectory(), without trailing
  /// separators.
  std::vector<std::string> StableDirs;
  mutable std::shared_mutex StableDirsLock;

  static constexpr unsigned NumDirectivesLocks = 256;
  std::unique_ptr<std::mutex[]> DirectivesLocks;
};

/// Reference to a CachedFileSystemEntry.
/// If the underlying entry is an opened file, this wrapper returns the file
/// contents and the scanned preprocessor directives.
///
/// Doesn't own the filename, so it must not outlive the string passed to
/// \c DependencyScanningWorkerFilesystem::getOrCreateFileSystemEntry().
class EntryRef {
  /// The filename used to access this entry.
  StringRef Filename;

  /// The underlying cached entry.
  const CachedFileSystemEntry &Entry;

  friend class DependencyScanningWorkerFilesystem;

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

  std::optional<ArrayRef<dependency_directives_scan::Directive>>
  getDirectiveTokens() const {
    return Entry.getDirectiveTokens();
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

  /// Ensure the directive tokens are populated for this file entry.
  ///
  /// Returns true if the directive tokens are populated for this file entry,
  /// false if not (i.e. this entry is not a file or its scan fails).
  bool ensureDirectiveTokensArePopulated(EntryRef Entry);

  /// \returns The scanned preprocessor directive tokens of the file that are
  /// used to speed up preprocessing, if available.
  std::optional<ArrayRef<dependency_directives_scan::Directive>>
  getDirectiveTokens(const Twine &Path) {
    SmallString<256> OwnedFilename;
    StringRef Filename = Path.toStringRef(OwnedFilename);
    if (llvm::ErrorOr<EntryRef> Entry = getOrCreateFileSystemEntry(Filename))
      if (ensureDirectiveTokensArePopulated(*Entry))
        return Entry->getDirectiveTokens();
    return std::nullopt;
  }

  /// Check whether \p Path exists. By default checks cached result of \c
  /// status(), and falls back on FS if unable to do so.
  bool exists(const Twine &Path) override;

private:
  /// Resolves the cache entry for \p FilenameForLookup through the shared
  /// cache: returns an entry already produced by another worker (a cache hit
  /// or the result of an in-flight wait), or claims the producer slot and
  /// computes the entry via the underlying filesystem.
  /// \p FilenameForLookup is always absolute, and may differ from
  /// \p OriginalFilename if the latter is relative. Unless an error is
  /// returned, sets \p StoredFilename to a copy of \p FilenameForLookup that
  /// outlives this filesystem's local caches.
  llvm::ErrorOr<const CachedFileSystemEntry *>
  resolveFilenameThroughSharedCache(StringRef OriginalFilename,
                                    StringRef FilenameForLookup,
                                    StringRef &StoredFilename);

  /// Resolves the cache entry for the on-disk file identified by \p Stat
  /// through the UID-keyed shared cache. Reads the file's contents on the
  /// producer path. Always returns a non-null entry (which may carry a
  /// readFile error).
  const CachedFileSystemEntry *
  resolveUIDThroughSharedCache(StringRef OriginalFilename,
                               const llvm::vfs::Status &Stat);

  /// Resolves the cache entry through the local VFS and without going through
  /// the global shared cache. Only relevant if \c LocalFS is non-null and
  /// \p OriginalFilename exists in \c LocalFS.
  const CachedFileSystemEntry *
  createCachedFSEntryForLocal(StringRef OriginalFilename,
                              const llvm::vfs::Status &Stat);

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

  /// Per-filename entries and real paths cached locally by this worker thread,
  /// so repeated queries can be served without touching the shared cache.
  ///
  /// The keys are absolute filenames that don't own their characters: they
  /// point to the keys of the shared cache's filename map, which are never
  /// freed, or into \c LocalFilenameStorage for results from \c LocalFS. This
  /// way, the workers don't each keep their own copy of every path. Similarly,
  /// the entries and real paths are arena-owned by the shared cache (or by this
  /// worker for results from \c LocalFS) and outlive the local caches, so
  /// borrowing raw pointers here is safe.
  ///
  /// Only results that the shared cache keeps are cached locally; uncached
  /// negative stats are looked up again every time.
  llvm::DenseMap<llvm::CachedHashStringRef, const CachedFileSystemEntry *>
      LocalEntries;
  llvm::DenseMap<llvm::CachedHashStringRef, const CachedRealPath *>
      LocalRealPaths;

  /// The backing storage for local cache keys resolved from local FS only.
  llvm::BumpPtrAllocator LocalFilenameStorage;

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

  /// The backing storage for cached real paths resolved from local FS only.
  llvm::SpecificBumpPtrAllocator<CachedRealPath> LocalRealPathStorage;

  static const CachedFileSystemEntry *createEntry(
      llvm::ErrorOr<TentativeEntry> &Entry,
      llvm::SpecificBumpPtrAllocator<CachedFileContents> &ContentsStorage,
      llvm::SpecificBumpPtrAllocator<CachedFileSystemEntry> &EntryStorage);

  void updateWorkingDirForCacheLookup();

  llvm::ErrorOr<StringRef>
  tryGetFilenameForLookup(StringRef OriginalFilename,
                          llvm::SmallVectorImpl<char> &PathBuf) const;
};

/// A pool of worker filesystems for the scanning compiles that run on threads
/// of their own (the asynchronous module compiles).
///
/// A fresh worker filesystem starts with empty local caches, so all of its
/// lookups go to the shared cache and its locks. Reusing the filesystems of
/// finished compiles lets later compiles hit in the local caches instead.
/// Worker filesystems aren't thread-safe, so each one is checked out by a
/// single compile at a time.
///
/// The pooled filesystems are created like the asynchronous compiles created
/// them before: over \c DependencyScanningServiceOptions::MakeVFS, without a
/// local FS. Nothing in their local caches depends on the compile that used
/// them (the entries come from the service-wide shared cache and are keyed by
/// absolute path), and per-compile filesystems such as the command line's VFS
/// overlays are layered on top of them, not inside. The only state a compile
/// could change is the working directory: a filesystem whose working directory
/// changed isn't reused.
class DependencyScanningWorkerFilesystemPool {
public:
  /// Exclusive use of a pooled worker filesystem. Unless discarded, the
  /// filesystem goes back to the pool on destruction, provided nothing else
  /// still references it.
  class Lease {
  public:
    Lease() = default;
    Lease(Lease &&Other) { *this = std::move(Other); }
    Lease &operator=(Lease &&Other) {
      if (this != &Other) {
        reset();
        Pool = std::exchange(Other.Pool, nullptr);
        FS = std::move(Other.FS);
        WorkingDir = std::move(Other.WorkingDir);
      }
      return *this;
    }
    ~Lease() { reset(); }

    DependencyScanningWorkerFilesystem &operator*() const { return *FS; }
    DependencyScanningWorkerFilesystem *operator->() const { return FS.get(); }
    IntrusiveRefCntPtr<DependencyScanningWorkerFilesystem> get() const {
      return FS;
    }

    /// Returns the filesystem to the pool (if it can be reused) and releases
    /// this lease.
    void reset();

    /// Releases this lease without returning the filesystem to the pool,
    /// e.g. because the compile using it crashed.
    void discard() {
      Pool = nullptr;
      FS = nullptr;
    }

  private:
    friend class DependencyScanningWorkerFilesystemPool;

    DependencyScanningWorkerFilesystemPool *Pool = nullptr;
    IntrusiveRefCntPtr<DependencyScanningWorkerFilesystem> FS;
    /// The working directory of \c FS when it was checked out, if any.
    std::optional<std::string> WorkingDir;
  };

  /// Checks out a worker filesystem, reusing one returned earlier if possible.
  Lease acquire(DependencyScanningService &Service);

  /// Returns the number of filesystems available for reuse.
  size_t getNumAvailable() {
    std::lock_guard<std::mutex> Guard(Lock);
    return Available.size();
  }

private:
  std::mutex Lock;
  std::vector<IntrusiveRefCntPtr<DependencyScanningWorkerFilesystem>> Available;
};

} // end namespace dependencies
} // end namespace clang

#endif // LLVM_CLANG_DEPENDENCYSCANNING_DEPENDENCYSCANNINGFILESYSTEM_H
