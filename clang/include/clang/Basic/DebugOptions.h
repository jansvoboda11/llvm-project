//===--- DebugOptions.h -----------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_BASIC_DEBUGOPTIONS_H
#define LLVM_CLANG_BASIC_DEBUGOPTIONS_H

#include "clang/Basic/LLVM.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Frontend/Debug/Options.h"
#include "llvm/MC/MCTargetOptions.h"
#include "llvm/Support/Compression.h"
#include "llvm/Target/TargetOptions.h"

#include <string>
#include <utility>

namespace clang {
class DebugOptions {
  friend class CompilerInvocation;
  friend class CompilerInvocationBase;

public:
  /// For ASTs produced with different option value, signifies their level of
  /// compatibility.
  enum class CompatibilityKind {
    /// Does affect the construction of the AST in a way that does prevent
    /// module interoperability.
    NotCompatible,
    /// Does affect the construction of the AST in a way that doesn't prevent
    /// interoperability (that is, the value can be different between an
    /// explicit module and the user of that module).
    Compatible,
    /// Does not affect the construction of the AST in any way (that is, the
    /// value can be different between an implicit module and the user of that
    /// module).
    Benign,
  };

  /// Enable additional debugging information.
  std::string DebugPass;

  /// The string to embed in debug information as the current working directory.
  std::string DebugCompilationDir;

  /// The string to embed in the debug information for the compile unit, if
  /// non-empty.
  std::string DwarfDebugFlags;

  llvm::SmallVector<std::pair<std::string, std::string>, 0> DebugPrefixMap;

  /// The file to use for dumping bug report by `Debugify` for original
  /// debug info.
  std::string DIBugsReportFilePath;

  /// The name for the split debug info file used for the DW_AT_[GNU_]dwo_name
  /// attribute in the skeleton CU.
  std::string SplitDwarfFile;

  /// Output filename for the split debug info, not used in the skeleton CU.
  std::string SplitDwarfOutput;

  /// Output filename used in the COFF debug information.
  std::string ObjectFilenameForDebug;

  using DebugCompressionType = llvm::DebugCompressionType;
  using EmitDwarfUnwindType = llvm::EmitDwarfUnwindType;
  using DebugTemplateNamesKind = llvm::codegenoptions::DebugTemplateNamesKind;
  using DebugInfoKind = llvm::codegenoptions::DebugInfoKind;
  using DebuggerKind = llvm::DebuggerKind;

  enum AssignmentTrackingOpts {
    Disabled,
    Enabled,
    Forced,
  };

  enum DebugSrcHashKind {
    DSH_MD5,
    DSH_SHA1,
    DSH_SHA256,
    DSH_NONE,
  };

#define DEBUGOPT(Name, Bits, Default, Compatibility) unsigned Name : Bits;
#define ENUM_DEBUGOPT(Name, Type, Bits, Default, Compatibility)
#include "clang/Basic/DebugOptions.def"

protected:
#define DEBUGOPT(Name, Bits, Default, Compatibility)
#define ENUM_DEBUGOPT(Name, Type, Bits, Default, Compatibility)                \
  unsigned Name : Bits;
#include "clang/Basic/DebugOptions.def"

public:
  DebugOptions();

  // Define accessors/mutators for code generation options of enumeration type.
#define DEBUGOPT(Name, Bits, Default, Compatibility)
#define ENUM_DEBUGOPT(Name, Type, Bits, Default, Compatibility)                \
  Type get##Name() const { return static_cast<Type>(Name); }                   \
  void set##Name(Type Value) { Name = static_cast<unsigned>(Value); }
#include "clang/Basic/DebugOptions.def"

  /// Check if type and variable info should be emitted.
  bool hasReducedDebugInfo() const {
    return getDebugInfo() >= llvm::codegenoptions::DebugInfoConstructor;
  }

  /// Check if maybe unused type info should be emitted.
  bool hasMaybeUnusedDebugInfo() const {
    return getDebugInfo() >= llvm::codegenoptions::UnusedTypeInfo;
  }

  /// Reset all the options that are not considered when building a module.
  void resetNonModularOptions(StringRef ModuleFormat);
};
} // namespace clang

#endif
