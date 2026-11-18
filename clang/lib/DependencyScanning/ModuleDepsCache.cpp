//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "clang/DependencyScanning/ModuleDepsCache.h"
#include "clang/Frontend/CompilerInvocation.h"
#include "clang/Serialization/ModuleFile.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/Support/HashBuilder.h"
#include "llvm/Support/raw_ostream.h"

using namespace clang;
using namespace dependencies;

std::string ModuleDepsGroup::getKey(const serialization::ModuleFile &MF) {
  std::string Key;
  llvm::raw_string_ostream OS(Key);
  OS << MF.FileName.str() << '\0' << MF.Size << '\0' << MF.ModTime << '\0'
     << llvm::toHex(MF.Signature);
  return Key;
}

std::shared_ptr<const ModuleDeps>
ModuleDepsGroup::lookup(const serialization::ModuleFile &MF) const {
  std::string Key = getKey(MF);
  std::shared_lock<std::shared_mutex> Lock(Mutex);
  auto It = Entries.find(Key);
  return It == Entries.end() ? nullptr : It->second;
}

std::shared_ptr<const ModuleDeps>
ModuleDepsGroup::add(const serialization::ModuleFile &MF,
                     std::shared_ptr<const ModuleDeps> MD) {
  std::string Key = getKey(MF);
  std::unique_lock<std::shared_mutex> Lock(Mutex);
  return Entries.try_emplace(Key, std::move(MD)).first->second;
}

ModuleDepsCache::GroupKey
ModuleDepsCache::getKey(const CompilerInvocation &Invocation,
                        StringRef WorkingDirectory) {
  llvm::HashBuilder<llvm::TruncatedBLAKE3<16>, llvm::endianness::native>
      HashBuilder;

  // The working directory affects path canonicalization and whether it can be
  // ignored by module builds.
  HashBuilder.add(WorkingDirectory);

  // TODO: The per-TU options are reset, including the inputs, so their kinds
  //   are not part of this. The language options mostly represent the language
  //   of the inputs, but this may not be entirely sound.
  SmallString<0> ArgVec;
  ArgVec.reserve(4096);
  Invocation.generateCC1CommandLine([&](const Twine &Arg) {
    Arg.toVector(ArgVec);
    ArgVec.push_back('\0');
  });
  HashBuilder.add(ArgVec);

  return HashBuilder.final();
}

std::shared_ptr<ModuleDepsGroup>
ModuleDepsCache::getOrCreateGroup(const CompilerInvocation &Invocation,
                                  StringRef WorkingDirectory) {
  GroupKey Key = getKey(Invocation, WorkingDirectory);
  std::lock_guard<std::mutex> Lock(Mutex);
  std::shared_ptr<ModuleDepsGroup> &Group = Groups[Key];
  if (!Group)
    Group = std::make_shared<ModuleDepsGroup>();
  return Group;
}
