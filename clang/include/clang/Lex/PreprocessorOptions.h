//===- PreprocessorOptions.h ------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_LEX_PREPROCESSOROPTIONS_H_
#define LLVM_CLANG_LEX_PREPROCESSOROPTIONS_H_

#include "clang/Basic/BitmaskEnum.h"
#include "clang/Basic/FileEntry.h"
#include "clang/Basic/LLVM.h"
#include "clang/Lex/DependencyDirectivesScanner.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/StringSet.h"
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace llvm {

class MemoryBuffer;

} // namespace llvm

namespace clang {

/// Enumerate the kinds of standard library that
enum ObjCXXARCStandardLibraryKind {
  ARCXX_nolib,

  /// libc++
  ARCXX_libcxx,

  /// libstdc++
  ARCXX_libstdcxx
};

/// Whether to disable the normal validation performed on precompiled
/// headers and module files when they are loaded.
enum class DisableValidationForModuleKind {
  /// Perform validation, don't disable it.
  None = 0,

  /// Disable validation for a precompiled header and the modules it depends on.
  PCH = 0x1,

  /// Disable validation for module files.
  Module = 0x2,

  /// Disable validation for all kinds.
  All = PCH | Module,

  LLVM_MARK_AS_BITMASK_ENUM(Module)
};

/// PreprocessorOptions - This class is used for passing the various options
/// used in preprocessor initialization to InitializePreprocessor().
class PreprocessorOptions {
public:
  /// Aliases used to keep TYPED_PREPROCESSOROPT lines free of nested commas
  /// in template arguments.
  using MacroDef = std::pair<std::string, /*isUndef*/ bool>;
  using PreambleBytes = std::pair<unsigned, bool>;
  using RemappedFile = std::pair<std::string, std::string>;
  using RemappedFileBuffer = std::pair<std::string, llvm::MemoryBuffer *>;

#define TYPED_PREPROCESSOROPT(Type, Name, Default) Type Name = Default;
#include "clang/Lex/PreprocessorOptions.def"
#undef TYPED_PREPROCESSOROPT

  PreprocessorOptions() = default;

  void addMacroDef(StringRef Name) {
    Macros.emplace_back(std::string(Name), false);
  }
  void addMacroUndef(StringRef Name) {
    Macros.emplace_back(std::string(Name), true);
  }

  void addRemappedFile(StringRef From, StringRef To) {
    RemappedFiles.emplace_back(std::string(From), std::string(To));
  }

  void addRemappedFile(StringRef From, llvm::MemoryBuffer *To) {
    RemappedFileBuffers.emplace_back(std::string(From), To);
  }

  void clearRemappedFiles() {
    RemappedFiles.clear();
    RemappedFileBuffers.clear();
  }

  /// Reset any options that are not considered when building a
  /// module.
  void resetNonModularOptions() {
    Includes.clear();
    MacroIncludes.clear();
    ChainedIncludes.clear();
    DumpDeserializedPCHDecls = false;
    ImplicitPCHInclude.clear();
    SingleFileParseMode = false;
    LexEditorPlaceholders = true;
    RetainRemappedFileBuffers = true;
    PrecompiledPreambleBytes.first = 0;
    PrecompiledPreambleBytes.second = false;
    RetainExcludedConditionalBlocks = false;
  }
};

} // namespace clang

#endif // LLVM_CLANG_LEX_PREPROCESSOROPTIONS_H_
