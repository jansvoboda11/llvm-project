//===--- CodeGenOptions.h ---------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file defines the CodeGenOptions interface.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_BASIC_CODEGENOPTIONS_H
#define LLVM_CLANG_BASIC_CODEGENOPTIONS_H

#include "clang/Basic/CFProtectionOptions.h"
#include "clang/Basic/PointerAuthOptions.h"
#include "clang/Basic/Sanitizers.h"
#include "clang/Basic/XRayInstr.h"
#include "llvm/ADT/FloatingPointMode.h"
#include "llvm/Frontend/Debug/Options.h"
#include "llvm/Frontend/Driver/CodeGenOptions.h"
#include "llvm/MC/MCTargetOptions.h"
#include "llvm/Support/CodeGen.h"
#include "llvm/Support/Regex.h"
#include "llvm/Target/TargetOptions.h"
#include "llvm/Transforms/Instrumentation/AddressSanitizerOptions.h"
#include "llvm/Transforms/Utils/KCFIHash.h"
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace llvm {
class PassBuilder;
}
namespace clang {

/// Bitfields of CodeGenOptions, split out from CodeGenOptions to ensure
/// that this large collection of bitfields is a trivial class type.
class CodeGenOptionsBase {
  friend class CompilerInvocation;

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

  using CFBranchLabelSchemeKind = clang::CFBranchLabelSchemeKind;
  using ProfileInstrKind = llvm::driver::ProfileInstrKind;
  using AsanDetectStackUseAfterReturnMode =
      llvm::AsanDetectStackUseAfterReturnMode;
  using AsanDtorKind = llvm::AsanDtorKind;
  using VectorLibrary = llvm::driver::VectorLibrary;
  using ZeroCallUsedRegsKind = llvm::ZeroCallUsedRegs::ZeroCallUsedRegsKind;
  using WinX64EHUnwindMode = llvm::WinX64EHUnwindMode;
  using ControlFlowGuardMechanism = llvm::ControlFlowGuardMechanism;

  using DebugCompressionType = llvm::DebugCompressionType;
  using EmitDwarfUnwindType = llvm::EmitDwarfUnwindType;
  using DebugTemplateNamesKind = llvm::codegenoptions::DebugTemplateNamesKind;
  using DebugInfoKind = llvm::codegenoptions::DebugInfoKind;
  using DebuggerKind = llvm::DebuggerKind;
  using RelocSectionSymType = llvm::RelocSectionSymType;

#define CODEGENOPT(Name, Bits, Default, Compatibility) unsigned Name : Bits;
#define ENUM_CODEGENOPT(Name, Type, Bits, Default, Compatibility)
#include "clang/Basic/CodeGenOptions.def"

protected:
#define CODEGENOPT(Name, Bits, Default, Compatibility)
#define ENUM_CODEGENOPT(Name, Type, Bits, Default, Compatibility)              \
  unsigned Name : Bits;
#include "clang/Basic/CodeGenOptions.def"
};

/// CodeGenOptions - Track various options which control how the code
/// is optimized and passed to the backend.
class CodeGenOptions : public CodeGenOptionsBase {
public:
  enum InliningMethod {
    NormalInlining,     // Use the standard function inlining pass.
    OnlyHintInlining,   // Inline only (implicitly) hinted functions.
    OnlyAlwaysInlining  // Only run the always inlining pass.
  };

  enum ObjCDispatchMethodKind {
    Legacy = 0,
    NonLegacy = 1,
    Mixed = 2
  };

  enum TLSModel {
    GeneralDynamicTLSModel,
    LocalDynamicTLSModel,
    InitialExecTLSModel,
    LocalExecTLSModel
  };

  enum StructReturnConventionKind {
    SRCK_Default,  // No special option was passed.
    SRCK_OnStack,  // Small structs on the stack (-fpcc-struct-return).
    SRCK_InRegs    // Small structs in registers (-freg-struct-return).
  };

  enum EmbedBitcodeKind {
    Embed_Off,      // No embedded bitcode.
    Embed_All,      // Embed both bitcode and commandline in the output.
    Embed_Bitcode,  // Embed just the bitcode in the output.
    Embed_Marker    // Embed a marker as a placeholder for bitcode.
  };

  enum class ExtendVariableLivenessKind {
    None,
    This,
    All,
  };

  enum InlineAsmDialectKind {
    IAD_ATT,
    IAD_Intel,
  };

  enum DebugSrcHashKind {
    DSH_MD5,
    DSH_SHA1,
    DSH_SHA256,
    DSH_NONE,
  };

  // BBSections and BinutilsVersion are migrated to TYPED_CODEGENOPT below;
  // see clang/Basic/CodeGenOptions.def.

  enum class FramePointerKind {
    NonLeafNoReserve, // Keep non-leaf frame pointers, allow the FP to be used
                      // as a GPR in leaf functions.
    None,             // Omit all frame pointers.
    Reserved,         // Maintain valid frame pointer chain.
    NonLeaf, // Keep non-leaf frame pointers, don't allow the FP to be used as a
             // GPR in leaf functions.
    All,     // Keep all frame pointers.
  };

  static StringRef getFramePointerKindName(FramePointerKind Kind) {
    switch (Kind) {
    case FramePointerKind::None:
      return "none";
    case FramePointerKind::Reserved:
      return "reserved";
    case FramePointerKind::NonLeafNoReserve:
      return "non-leaf-no-reserve";
    case FramePointerKind::NonLeaf:
      return "non-leaf";
    case FramePointerKind::All:
      return "all";
    }

    llvm_unreachable("invalid FramePointerKind");
  }

  /// Possible exception handling behavior.
  enum class ExceptionHandlingKind { None, SjLj, WinEH, DwarfCFI, Wasm };

  enum class SwiftAsyncFramePointerKind {
    Auto, // Choose Swift async extended frame info based on deployment target.
    Always, // Unconditionally emit Swift async extended frame info.
    Never,  // Don't emit Swift async extended frame info.
    Default = Always,
  };

  enum FiniteLoopsKind {
    Language, // Not specified, use language standard.
    Always,   // All loops are assumed to be finite.
    Never,    // No loop is assumed to be finite.
  };

  enum AssignmentTrackingOpts {
    Disabled,
    Enabled,
    Forced,
  };

  enum SanitizeDebugTrapReasonKind {
    None,  ///< Trap Messages are omitted. This offers the smallest debug info
           ///< size but at the cost of making traps hard to debug.
    Basic, ///< Trap Message is fixed per SanitizerKind. Produces smaller debug
           ///< info than `Detailed` but is not as helpful for debugging.
    Detailed, ///< Trap Message includes more context (e.g. the expression being
              ///< overflowed). This is more helpful for debugging but produces
              ///< larger debug info than `Basic`.
  };

  enum class BoolFromMem {
    Strict,   ///< In-memory bool values are assumed to be 0 or 1, and any other
              ///< value is UB.
    Truncate, ///< Convert in-memory bools to i1 by checking if the least
              ///< significant bit is 1.
    NonZero,  ///< Convert in-memory bools to i1 by checking if any bit is set
              ///< to 1.
    NonStrictDefault = NonZero
  };

  struct BitcodeFileToLink {
    /// The filename of the bitcode file to link in.
    std::string Filename;
    /// If true, we set attributes functions in the bitcode library according to
    /// our CodeGenOptions, much as we set attrs on functions that we generate
    /// ourselves.
    bool PropagateAttrs = false;
    /// If true, we use LLVM module internalizer.
    bool Internalize = false;
    /// Bitwise combination of llvm::Linker::Flags, passed to the LLVM linker.
    unsigned LinkFlags = 0;
  };

  enum RemarkKind {
    RK_Missing,            // Remark argument not present on the command line.
    RK_Enabled,            // Remark enabled via '-Rgroup'.
    RK_EnabledEverything,  // Remark enabled via '-Reverything'.
    RK_Disabled,           // Remark disabled via '-Rno-group'.
    RK_DisabledEverything, // Remark disabled via '-Rno-everything'.
    RK_WithPattern,        // Remark pattern specified via '-Rgroup=regexp'.
  };

  /// Optimization remark with an optional regular expression pattern.
  struct OptRemark {
    RemarkKind Kind = RK_Missing;
    std::string Pattern;
    std::shared_ptr<llvm::Regex> Regex;

    /// By default, optimization remark is missing.
    OptRemark() = default;

    /// Returns true iff the optimization remark holds a valid regular
    /// expression.
    bool hasValidPattern() const { return Regex != nullptr; }

    /// Matches the given string against the regex, if there is some.
    bool patternMatches(StringRef String) const {
      return hasValidPattern() && Regex->match(String);
    }
  };

  /// Aliases used to keep TYPED_CODEGENOPT lines free of nested commas in
  /// template arguments and to expose array types behind a single identifier.
  using StringPairList =
      llvm::SmallVector<std::pair<std::string, std::string>, 0>;
  using PassBuilderCallbackList =
      std::vector<std::function<void(llvm::PassBuilder &)>>;

  /// The version string to put into coverage files. Hand-declared because a
  /// `char[4]` brace-initializer can't be passed through a function-like macro
  /// (the commas in `{'0','0','0','0'}` would split into separate macro args).
  char CoverageVersion[4] = {'0', '0', '0', '0'};

#define CODEGENOPT(Name, Bits, Default, Compatibility)
#define ENUM_CODEGENOPT(Name, Type, Bits, Default, Compatibility)
#define TYPED_CODEGENOPT(Type, Name, Default) Type Name = Default;
#include "clang/Basic/CodeGenOptions.def"

public:
  // Define accessors/mutators for code generation options of enumeration type.
#define CODEGENOPT(Name, Bits, Default, Compatibility)
#define ENUM_CODEGENOPT(Name, Type, Bits, Default, Compatibility)              \
  Type get##Name() const { return static_cast<Type>(Name); }                   \
  void set##Name(Type Value) { Name = static_cast<unsigned>(Value); }
#include "clang/Basic/CodeGenOptions.def"

  CodeGenOptions();

  const std::vector<std::string> &getNoBuiltinFuncs() const {
    return NoBuiltinFuncs;
  }

  bool hasSjLjExceptions() const {
    return getExceptionHandling() == ExceptionHandlingKind::SjLj;
  }

  bool hasSEHExceptions() const {
    return getExceptionHandling() == ExceptionHandlingKind::WinEH;
  }

  bool hasDWARFExceptions() const {
    return getExceptionHandling() == ExceptionHandlingKind::DwarfCFI;
  }

  bool hasWasmExceptions() const {
    return getExceptionHandling() == ExceptionHandlingKind::Wasm;
  }

  /// Check if Clang profile instrumenation is on.
  bool hasProfileClangInstr() const {
    return getProfileInstr() ==
           llvm::driver::ProfileInstrKind::ProfileClangInstr;
  }

  /// Check if IR level profile instrumentation is on.
  bool hasProfileIRInstr() const {
    return getProfileInstr() == llvm::driver::ProfileInstrKind::ProfileIRInstr;
  }

  /// Check if CS IR level profile instrumentation is on.
  bool hasProfileCSIRInstr() const {
    return getProfileInstr() ==
           llvm::driver::ProfileInstrKind::ProfileCSIRInstr;
  }

  /// Check if any form of instrumentation is on.
  bool hasProfileInstr() const {
    return getProfileInstr() != llvm::driver::ProfileInstrKind::ProfileNone;
  }

  /// Check if Clang profile use is on.
  bool hasProfileClangUse() const {
    return getProfileUse() == llvm::driver::ProfileInstrKind::ProfileClangInstr;
  }

  /// Check if IR level profile use is on.
  bool hasProfileIRUse() const {
    return getProfileUse() == llvm::driver::ProfileInstrKind::ProfileIRInstr ||
           getProfileUse() == llvm::driver::ProfileInstrKind::ProfileCSIRInstr;
  }

  /// Check if CSIR profile use is on.
  bool hasProfileCSIRUse() const {
    return getProfileUse() == llvm::driver::ProfileInstrKind::ProfileCSIRInstr;
  }

  /// Check if type and variable info should be emitted.
  bool hasReducedDebugInfo() const {
    return getDebugInfo() >= llvm::codegenoptions::DebugInfoConstructor;
  }

  /// Check if maybe unused type info should be emitted.
  bool hasMaybeUnusedDebugInfo() const {
    return getDebugInfo() >= llvm::codegenoptions::UnusedTypeInfo;
  }

  // Check if any one of SanitizeCoverage* is enabled.
  bool hasSanitizeCoverage() const {
    return SanitizeCoverageType || SanitizeCoverageIndirectCalls ||
           SanitizeCoverageTraceCmp || SanitizeCoverageTraceLoads ||
           SanitizeCoverageTraceStores || SanitizeCoverageControlFlow;
  }

  // Check if any one of SanitizeBinaryMetadata* is enabled.
  bool hasSanitizeBinaryMetadata() const {
    return SanitizeBinaryMetadataCovered || SanitizeBinaryMetadataAtomics ||
           SanitizeBinaryMetadataUAR;
  }

  /// Reset all of the options that are not considered when building a
  /// module.
  void resetNonModularOptions(StringRef ModuleFormat);

  // Is the given function name one of the functions that can be replaced by the
  // loader?
  bool isLoaderReplaceableFunctionName(StringRef FuncName) const {
    return llvm::is_contained(LoaderReplaceableFunctionNames, FuncName);
  }

  /// Are we building at -O1 or higher?
  bool isOptimizedBuild() const { return OptimizationLevel > 0; }

  /// When loading a bool from a storage unit larger than i1, should it
  /// be converted to i1 by comparing to 0 or by truncating to i1?
  bool isConvertingBoolWithCmp0() const {
    switch (getLoadBoolFromMem()) {
    case BoolFromMem::Strict:
      return !isOptimizedBuild();

    case BoolFromMem::Truncate:
      return false;

    case BoolFromMem::NonZero:
      return true;
    }
    llvm_unreachable("Unknown BoolFromMem enum");
  }
};

}  // end namespace clang

#endif
