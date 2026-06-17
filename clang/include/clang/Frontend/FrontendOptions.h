//===- FrontendOptions.h ----------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_FRONTEND_FRONTENDOPTIONS_H
#define LLVM_CLANG_FRONTEND_FRONTENDOPTIONS_H

#include "clang/AST/ASTDumperUtils.h"
#include "clang/Basic/LangStandard.h"
#include "clang/Frontend/CommandLineSourceLoc.h"
#include "clang/Sema/CodeCompleteOptions.h"
#include "clang/Serialization/ModuleFileExtension.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/MemoryBuffer.h"
#include <cassert>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace llvm {

class MemoryBuffer;

} // namespace llvm

namespace clang {

namespace frontend {

enum ActionKind {
  /// Parse ASTs and list Decl nodes.
  ASTDeclList,

  /// Parse ASTs and dump them.
  ASTDump,

  /// Parse ASTs and print them.
  ASTPrint,

  /// Parse ASTs and view them in Graphviz.
  ASTView,

  /// Dump the compiler configuration.
  DumpCompilerOptions,

  /// Dump out raw tokens.
  DumpRawTokens,

  /// Dump out preprocessed tokens.
  DumpTokens,

  /// Emit a .s file.
  EmitAssembly,

  /// Emit a .bc file.
  EmitBC,

  /// Translate input source into HTML.
  EmitHTML,

  /// Emit a .cir file
  EmitCIR,

  /// Emit a .ll file.
  EmitLLVM,

  /// Generate LLVM IR, but do not emit anything.
  EmitLLVMOnly,

  /// Generate machine code, but don't emit anything.
  EmitCodeGenOnly,

  /// Emit a .o file.
  EmitObj,

  // Extract API information
  ExtractAPI,

  /// Parse and apply any fixits to the source.
  FixIt,

  /// Generate pre-compiled module from a module map.
  GenerateModule,

  /// Generate pre-compiled module from a standard C++ module interface unit.
  GenerateModuleInterface,

  /// Generate reduced module interface for a standard C++ module interface
  /// unit.
  GenerateReducedModuleInterface,

  /// Generate a C++20 header unit module from a header file.
  GenerateHeaderUnit,

  /// Generate pre-compiled header.
  GeneratePCH,

  /// Generate Interface Stub Files.
  GenerateInterfaceStubs,

  /// Only execute frontend initialization.
  InitOnly,

  /// Dump information about a module file.
  ModuleFileInfo,

  /// Load and verify that a PCH file is usable.
  VerifyPCH,

  /// Parse and perform semantic analysis.
  ParseSyntaxOnly,

  /// Run a plugin action, \see ActionName.
  PluginAction,

  /// Print the "preamble" of the input file
  PrintPreamble,

  /// -E mode.
  PrintPreprocessedInput,

  /// Expand macros but not \#includes.
  RewriteMacros,

  /// ObjC->C Rewriter.
  RewriteObjC,

  /// Rewriter playground
  RewriteTest,

  /// Run one or more source code analyses.
  RunAnalysis,

  /// Dump template instantiations
  TemplightDump,

  /// Just lex, no output.
  RunPreprocessorOnly,

  /// Print the output of the dependency directives source minimizer.
  PrintDependencyDirectivesSourceMinimizerOutput
};

} // namespace frontend

/// The kind of a file that we've been handed as an input.
class InputKind {
public:
  /// The input file format.
  enum Format {
    Source,
    ModuleMap,
    Precompiled
  };

  // If we are building a header unit, what kind it is; this affects whether
  // we look for the file in the user or system include search paths before
  // flagging a missing input.
  enum HeaderUnitKind {
    HeaderUnit_None,
    HeaderUnit_User,
    HeaderUnit_System,
    HeaderUnit_Abs
  };

private:
  Language Lang;
  LLVM_PREFERRED_TYPE(Format)
  unsigned Fmt : 3;
  LLVM_PREFERRED_TYPE(bool)
  unsigned Preprocessed : 1;
  LLVM_PREFERRED_TYPE(HeaderUnitKind)
  unsigned HeaderUnit : 3;
  LLVM_PREFERRED_TYPE(bool)
  unsigned IsHeader : 1;

public:
  constexpr InputKind(Language L = Language::Unknown, Format F = Source,
                      bool PP = false, HeaderUnitKind HU = HeaderUnit_None,
                      bool HD = false)
      : Lang(L), Fmt(F), Preprocessed(PP), HeaderUnit(HU), IsHeader(HD) {}

  Language getLanguage() const { return static_cast<Language>(Lang); }
  Format getFormat() const { return static_cast<Format>(Fmt); }
  HeaderUnitKind getHeaderUnitKind() const {
    return static_cast<HeaderUnitKind>(HeaderUnit);
  }
  bool isPreprocessed() const { return Preprocessed; }
  bool isHeader() const { return IsHeader; }
  bool isHeaderUnit() const { return HeaderUnit != HeaderUnit_None; }

  /// Is the input kind fully-unknown?
  bool isUnknown() const { return Lang == Language::Unknown && Fmt == Source; }

  /// Is the language of the input some dialect of Objective-C?
  bool isObjectiveC() const {
    return Lang == Language::ObjC || Lang == Language::ObjCXX;
  }

  InputKind getPreprocessed() const {
    return InputKind(getLanguage(), getFormat(), true, getHeaderUnitKind(),
                     isHeader());
  }

  InputKind getHeader() const {
    return InputKind(getLanguage(), getFormat(), isPreprocessed(),
                     getHeaderUnitKind(), true);
  }

  InputKind withHeaderUnit(HeaderUnitKind HU) const {
    return InputKind(getLanguage(), getFormat(), isPreprocessed(), HU,
                     isHeader());
  }

  InputKind withFormat(Format F) const {
    return InputKind(getLanguage(), F, isPreprocessed(), getHeaderUnitKind(),
                     isHeader());
  }
};

/// An input file for the front end.
class FrontendInputFile {
  /// The file name, or "-" to read from standard input.
  std::string File;

  /// The input, if it comes from a buffer rather than a file. This object
  /// does not own the buffer, and the caller is responsible for ensuring
  /// that it outlives any users.
  std::optional<llvm::MemoryBufferRef> Buffer;

  /// The kind of input, e.g., C source, AST file, LLVM IR.
  InputKind Kind;

  /// Whether we're dealing with a 'system' input (vs. a 'user' input).
  bool IsSystem = false;

  friend class CompilerInvocation;

public:
  FrontendInputFile() = default;
  FrontendInputFile(StringRef File, InputKind Kind, bool IsSystem = false)
      : File(File.str()), Kind(Kind), IsSystem(IsSystem) {}
  FrontendInputFile(llvm::MemoryBufferRef Buffer, InputKind Kind,
                    bool IsSystem = false)
      : Buffer(Buffer), Kind(Kind), IsSystem(IsSystem) {}

  InputKind getKind() const { return Kind; }
  bool isSystem() const { return IsSystem; }

  bool isEmpty() const { return File.empty() && Buffer == std::nullopt; }
  bool isFile() const { return !isBuffer(); }
  bool isBuffer() const { return Buffer != std::nullopt; }
  bool isPreprocessed() const { return Kind.isPreprocessed(); }
  bool isHeader() const { return Kind.isHeader(); }
  InputKind::HeaderUnitKind getHeaderUnitKind() const {
    return Kind.getHeaderUnitKind();
  }

  StringRef getFile() const {
    assert(isFile());
    return File;
  }

  llvm::MemoryBufferRef getBuffer() const {
    assert(isBuffer());
    return *Buffer;
  }
};

/// FrontendOptions - Options for controlling the behavior of the frontend.
class FrontendOptions {
public:
  /// Aliases used to keep TYPED_FRONTENDOPT lines free of nested commas in
  /// template arguments.
  using PluginArgsMap = std::map<std::string, std::vector<std::string>>;
  using InputsList = SmallVector<FrontendInputFile, 0>;
  using ModuleFileExtensionList =
      std::vector<std::shared_ptr<ModuleFileExtension>>;

#define TYPED_FRONTENDOPT(Type, Name, Default) Type Name = Default;
#define BITFIELD_FRONTENDOPT(Type, Name, Bits, Default)                        \
  LLVM_PREFERRED_TYPE(Type) unsigned Name : Bits;
#include "clang/Frontend/FrontendOptions.def"
#undef TYPED_FRONTENDOPT
#undef BITFIELD_FRONTENDOPT

  FrontendOptions() {
#define TYPED_FRONTENDOPT(Type, Name, Default)
#define BITFIELD_FRONTENDOPT(Type, Name, Bits, Default) Name = Default;
#include "clang/Frontend/FrontendOptions.def"
#undef TYPED_FRONTENDOPT
#undef BITFIELD_FRONTENDOPT
  }

  /// getInputKindForExtension - Return the appropriate input kind for a file
  /// extension. For example, "c" would return Language::C.
  ///
  /// \return The input kind for the extension, or Language::Unknown if the
  /// extension is not recognized.
  static InputKind getInputKindForExtension(StringRef Extension);
};

} // namespace clang

#endif // LLVM_CLANG_FRONTEND_FRONTENDOPTIONS_H
