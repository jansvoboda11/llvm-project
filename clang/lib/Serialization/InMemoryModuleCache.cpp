//===- InMemoryModuleCache.cpp - Cache for loaded memory buffers ----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "clang/Serialization/InMemoryModuleCache.h"
#include "llvm/Support/MemoryBuffer.h"

using namespace clang;

llvm::MemoryBuffer *
InMemoryModuleCache::lookupPCM(llvm::StringRef Filename) const {
  auto I = PCMs.find(Filename);
  if (I == PCMs.end())
    return nullptr;
  return I->second.back().get();
}

llvm::MemoryBuffer &
InMemoryModuleCache::addPCM(llvm::StringRef Filename,
                            std::unique_ptr<llvm::MemoryBuffer> Buffer) {
  auto &PCM = PCMs[Filename];
  PCM.push_back(std::move(Buffer));
  return *PCM.back();
}
