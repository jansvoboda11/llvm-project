//===- InMemoryModuleCache.h - In-memory cache for modules ------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_SERIALIZATION_INMEMORYMODULECACHE_H
#define LLVM_CLANG_SERIALIZATION_INMEMORYMODULECACHE_H

#include "llvm/ADT/IntrusiveRefCntPtr.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/Support/MemoryBuffer.h"
#include <memory>

namespace clang {

/// In-memory cache for modules.
///
/// This is a cache for modules for use across a compilation, sharing state
/// between the CompilerInstances in an implicit modules build.  It must be
/// shared by each CompilerInstance, ASTReader, ASTWriter, and ModuleManager
/// that are coordinating.
///
/// Critically, it ensures that a single process has a consistent view of each
/// PCM.  This is used by \a CompilerInstance when building PCMs to ensure that
/// each \a ModuleManager sees the same files.
class InMemoryModuleCache : public llvm::RefCountedBase<InMemoryModuleCache> {
  /// Cache of buffers.
  llvm::StringMap<std::vector<std::unique_ptr<llvm::MemoryBuffer>>> PCMs;

public:
  /// Get a pointer to the PCM if it exists; else nullptr.
  llvm::MemoryBuffer *lookupPCM(llvm::StringRef Filename) const;

  /// Store a PCM under the Filename.
  ///
  /// \return a reference to the buffer as a convenience.
  llvm::MemoryBuffer &addPCM(llvm::StringRef Filename,
                             std::unique_ptr<llvm::MemoryBuffer> Buffer);
};

} // end namespace clang

#endif // LLVM_CLANG_SERIALIZATION_INMEMORYMODULECACHE_H
