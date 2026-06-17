//===--- DependencyOutputOptions.h ------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_FRONTEND_DEPENDENCYOUTPUTOPTIONS_H
#define LLVM_CLANG_FRONTEND_DEPENDENCYOUTPUTOPTIONS_H

#include "clang/Basic/HeaderInclude.h"
#include "llvm/Support/Compiler.h"
#include <string>
#include <utility>
#include <vector>

namespace clang {

/// ShowIncludesDestination - Destination for /showIncludes output.
enum class ShowIncludesDestination { None, Stdout, Stderr };

/// DependencyOutputFormat - Format for the compiler dependency file.
enum class DependencyOutputFormat { Make, NMake };

/// ExtraDepKind - The kind of extra dependency file.
enum ExtraDepKind {
  EDK_SanitizeIgnorelist,
  EDK_ProfileList,
  EDK_ModuleFile,
  EDK_DepFileEntry,
};

/// ModuleFileDepsKind - Whether to include module file dependencies.
enum ModuleFileDepsKind {
  MFDK_None,   ///< Do not include module file dependencies.
  MFDK_All,    ///< Include all module file dependencies.
  MFDK_Direct, ///< Include only directly imported module file dependencies.
};

/// DependencyOutputOptions - Options for controlling the compiler dependency
/// file generation.
class DependencyOutputOptions {
public:
  /// One entry of the \c ExtraDeps list. Aliased to keep the
  /// \c TYPED_DEPOUTPUTOPT lines free of nested commas in template arguments.
  using ExtraDep = std::pair<std::string, ExtraDepKind>;

#define TYPED_DEPOUTPUTOPT(Type, Name, Default) Type Name = Default;
#define BITFIELD_DEPOUTPUTOPT(Type, Name, Bits, Default)                       \
  LLVM_PREFERRED_TYPE(Type) unsigned Name : Bits;
#include "clang/Frontend/DependencyOutputOptions.def"
#undef TYPED_DEPOUTPUTOPT
#undef BITFIELD_DEPOUTPUTOPT

  DependencyOutputOptions() {
#define TYPED_DEPOUTPUTOPT(Type, Name, Default)
#define BITFIELD_DEPOUTPUTOPT(Type, Name, Bits, Default) Name = Default;
#include "clang/Frontend/DependencyOutputOptions.def"
#undef TYPED_DEPOUTPUTOPT
#undef BITFIELD_DEPOUTPUTOPT
  }
};

}  // end namespace clang

#endif
