//===- CompilerInvocation.h - Compiler Invocation Helper Data ---*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_FRONTEND_COMPILERINVOCATION_H
#define LLVM_CLANG_FRONTEND_COMPILERINVOCATION_H

#include "clang/APINotes/APINotesOptions.h"
#include "clang/Basic/CodeGenOptions.h"
#include "clang/Basic/DiagnosticOptions.h"
#include "clang/Basic/FileSystemOptions.h"
#include "clang/Basic/LLVM.h"
#include "clang/Basic/LangOptions.h"
#include "clang/Basic/LangStandard.h"
#include "clang/Frontend/DependencyOutputOptions.h"
#include "clang/Frontend/FrontendOptions.h"
#include "clang/Frontend/MigratorOptions.h"
#include "clang/Frontend/PreprocessorOutputOptions.h"
#include "clang/StaticAnalyzer/Core/AnalyzerOptions.h"
#include "llvm/ADT/IntrusiveRefCntPtr.h"
#include "llvm/ADT/ArrayRef.h"
#include <memory>
#include <string>

namespace llvm {

class Triple;

namespace opt {

class ArgList;

} // namespace opt

namespace vfs {

class FileSystem;

} // namespace vfs

} // namespace llvm

namespace clang {

class DiagnosticsEngine;
class HeaderSearchOptions;
class PreprocessorOptions;
class TargetOptions;

// This lets us create the DiagnosticsEngine with a properly-filled-out
// DiagnosticOptions instance.
std::unique_ptr<DiagnosticOptions>
CreateAndPopulateDiagOpts(ArrayRef<const char *> Argv);

/// Fill out Opts based on the options given in Args.
///
/// Args must have been created from the OptTable returned by
/// createCC1OptTable().
///
/// When errors are encountered, return false and, if Diags is non-null,
/// report the error(s).
bool ParseDiagnosticArgs(DiagnosticOptions &Opts, llvm::opt::ArgList &Args,
                         DiagnosticsEngine *Diags = nullptr,
                         bool DefaultDiagColor = true);

unsigned getOptimizationLevel(const llvm::opt::ArgList &Args, InputKind IK,
                              DiagnosticsEngine &Diags);

unsigned getOptimizationLevelSize(const llvm::opt::ArgList &Args);

/// Helper class for holding the data necessary to invoke the compiler.
///
/// This class is designed to represent an abstract "invocation" of the
/// compiler, including data such as the include paths, the code generation
/// options, the warning flags, and so on.
///
/// Each \c *Options object is held behind a \c std::shared_ptr, so copying a
/// \c CompilerInvocation is shallow by default and individual options objects
/// only get their own copy on the first mutation through one of the
/// \c getMut*Opts() accessors. This makes copies cheap when most options are
/// reused unchanged (notably in the dependency scanner, which clones an
/// invocation per discovered module and tweaks a handful of fields).
///
/// Discipline: do not store long-lived non-const references to the underlying
/// \c *Options objects. A subsequent mutation through \c getMut*Opts() may
/// silently re-bind the invocation's storage to a fresh copy, leaving any such
/// reference pointing at the previous (now-shared) object.
class MutAPINotesOptsHandle;
class MutAnalyzerOptsHandle;
class MutCodeGenOptsHandle;
class MutDependencyOutputOptsHandle;
class MutDiagnosticOptsHandle;
class MutFileSystemOptsHandle;
class MutFrontendOptsHandle;
class MutHeaderSearchOptsHandle;
class MutLangOptsHandle;
class MutMigratorOptsHandle;
class MutPreprocessorOptsHandle;
class MutPreprocessorOutputOptsHandle;
class MutTargetOptsHandle;

class CompilerInvocation {
protected:
  /// Options controlling the language variant.
  std::shared_ptr<LangOptions> LangOpts;

  /// Options controlling the target.
  std::shared_ptr<TargetOptions> TargetOpts;

  /// Options controlling the diagnostic engine.
  std::shared_ptr<DiagnosticOptions> DiagnosticOpts;

  /// Options controlling the \#include directive.
  std::shared_ptr<HeaderSearchOptions> HSOpts;

  /// Options controlling the preprocessor (aside from \#include handling).
  std::shared_ptr<PreprocessorOptions> PPOpts;

  /// Options controlling the static analyzer.
  std::shared_ptr<AnalyzerOptions> AnalyzerOpts;

  std::shared_ptr<MigratorOptions> MigratorOpts;

  /// Options controlling API notes.
  std::shared_ptr<APINotesOptions> APINotesOpts;

  /// Options controlling IRgen and the backend.
  std::shared_ptr<CodeGenOptions> CodeGenOpts;

  /// Options controlling file system operations.
  std::shared_ptr<FileSystemOptions> FSOpts;

  /// Options controlling the frontend itself.
  std::shared_ptr<FrontendOptions> FrontendOpts;

  /// Options controlling dependency output.
  std::shared_ptr<DependencyOutputOptions> DependencyOutputOpts;

  /// Options controlling preprocessed output.
  std::shared_ptr<PreprocessorOutputOptions> PreprocessorOutputOpts;

  /// Dummy tag type whose instance can be passed into the constructor to
  /// prevent creation of the reference-counted option objects.
  struct EmptyConstructor {};

  CompilerInvocation(EmptyConstructor) {}

public:
  CompilerInvocation();
  CompilerInvocation(const CompilerInvocation &X)
      : CompilerInvocation(EmptyConstructor{}) {
    shallow_copy_assign(X);
  }
  CompilerInvocation(CompilerInvocation &&) = default;
  CompilerInvocation &operator=(const CompilerInvocation &X) {
    shallow_copy_assign(X);
    return *this;
  }
  CompilerInvocation &operator=(CompilerInvocation &&) = default;
  ~CompilerInvocation() = default;

  /// Const getters.
  /// @{
  const LangOptions &getLangOpts() const { return *LangOpts; }
  const TargetOptions &getTargetOpts() const { return *TargetOpts; }
  const DiagnosticOptions &getDiagnosticOpts() const { return *DiagnosticOpts; }
  const HeaderSearchOptions &getHeaderSearchOpts() const { return *HSOpts; }
  const PreprocessorOptions &getPreprocessorOpts() const { return *PPOpts; }
  const AnalyzerOptions &getAnalyzerOpts() const { return *AnalyzerOpts; }
  const MigratorOptions &getMigratorOpts() const { return *MigratorOpts; }
  const APINotesOptions &getAPINotesOpts() const { return *APINotesOpts; }
  const CodeGenOptions &getCodeGenOpts() const { return *CodeGenOpts; }
  const FileSystemOptions &getFileSystemOpts() const { return *FSOpts; }
  const FrontendOptions &getFrontendOpts() const { return *FrontendOpts; }
  const DependencyOutputOptions &getDependencyOutputOpts() const {
    return *DependencyOutputOpts;
  }
  const PreprocessorOutputOptions &getPreprocessorOutputOpts() const {
    return *PreprocessorOutputOpts;
  }
  /// @}

  /// Mutable getters.
  ///
  /// These follow copy-on-write semantics: if the underlying options object is
  /// shared with another \c CompilerInvocation (use_count > 1), it is cloned
  /// before the mutable reference is returned. Mutation must go through
  /// \c getMut*Opts() — there is no non-const \c get*Opts() variant, since a
  /// silent CoW clone behind a non-const accessor would invalidate the
  /// invariant that two shallow-copied invocations observe the same option
  /// objects.
  /// @{
  LangOptions &getMutLangOpts();
  TargetOptions &getMutTargetOpts();
  DiagnosticOptions &getMutDiagnosticOpts();
  HeaderSearchOptions &getMutHeaderSearchOpts();
  PreprocessorOptions &getMutPreprocessorOpts();
  AnalyzerOptions &getMutAnalyzerOpts();
  MigratorOptions &getMutMigratorOpts();
  APINotesOptions &getMutAPINotesOpts();
  CodeGenOptions &getMutCodeGenOpts();
  FileSystemOptions &getMutFileSystemOpts();
  FrontendOptions &getMutFrontendOpts();
  DependencyOutputOptions &getMutDependencyOutputOpts();
  PreprocessorOutputOptions &getMutPreprocessorOutputOpts();
  /// @}

  /// Scoped mutator handles.
  ///
  /// These let a holder of a const \c CompilerInvocation (e.g., callers
  /// observing the invocation through \c CompilerInstance::getInvocation())
  /// mutate a single \c *Options object inside a callback that receives a
  /// non-copyable, non-movable \c Mut*OptsHandle. The handle exposes only the
  /// per-field setters generated from the corresponding \c *Options.def, so
  /// the underlying options reference cannot escape. CoW semantics match
  /// \c getMut*Opts(): if the options object is shared with another
  /// \c CompilerInvocation, it is cloned before the callback runs.
  /// @{
  void withMutLangOpts(llvm::function_ref<void(MutLangOptsHandle &)> F) const;
  void
  withMutTargetOpts(llvm::function_ref<void(MutTargetOptsHandle &)> F) const;
  void withMutDiagnosticOpts(
      llvm::function_ref<void(MutDiagnosticOptsHandle &)> F) const;
  void withMutHeaderSearchOpts(
      llvm::function_ref<void(MutHeaderSearchOptsHandle &)> F) const;
  void withMutPreprocessorOpts(
      llvm::function_ref<void(MutPreprocessorOptsHandle &)> F) const;
  void withMutAnalyzerOpts(
      llvm::function_ref<void(MutAnalyzerOptsHandle &)> F) const;
  void withMutMigratorOpts(
      llvm::function_ref<void(MutMigratorOptsHandle &)> F) const;
  void withMutAPINotesOpts(
      llvm::function_ref<void(MutAPINotesOptsHandle &)> F) const;
  void
  withMutCodeGenOpts(llvm::function_ref<void(MutCodeGenOptsHandle &)> F) const;
  void withMutFileSystemOpts(
      llvm::function_ref<void(MutFileSystemOptsHandle &)> F) const;
  void withMutFrontendOpts(
      llvm::function_ref<void(MutFrontendOptsHandle &)> F) const;
  void withMutDependencyOutputOpts(
      llvm::function_ref<void(MutDependencyOutputOptsHandle &)> F) const;
  void withMutPreprocessorOutputOpts(
      llvm::function_ref<void(MutPreprocessorOutputOptsHandle &)> F) const;
  /// @}

  /// Visitation.
  /// @{
  /// Visits paths stored in the invocation. The callback may return true to
  /// short-circuit the visitation, or return false to continue visiting.
  void visitPaths(llvm::function_ref<bool(StringRef)> Callback) const;
  /// @}

  /// Command line generation.
  /// @{
  using StringAllocator = llvm::function_ref<const char *(const Twine &)>;
  /// Generate cc1-compatible command line arguments from this instance.
  ///
  /// \param [out] Args - The generated arguments. Note that the caller is
  /// responsible for inserting the path to the clang executable and "-cc1" if
  /// desired.
  /// \param SA - A function that given a Twine can allocate storage for a given
  /// command line argument and return a pointer to the newly allocated string.
  /// The returned pointer is what gets appended to Args.
  void generateCC1CommandLine(llvm::SmallVectorImpl<const char *> &Args,
                              StringAllocator SA) const {
    generateCC1CommandLine([&](const Twine &Arg) {
      // No need to allocate static string literals.
      Args.push_back(Arg.isSingleStringLiteral()
                         ? Arg.getSingleStringRef().data()
                         : SA(Arg));
    });
  }

  using ArgumentConsumer = llvm::function_ref<void(const Twine &)>;
  /// Generate cc1-compatible command line arguments from this instance.
  ///
  /// \param Consumer - Callback that gets invoked for every single generated
  /// command line argument.
  void generateCC1CommandLine(ArgumentConsumer Consumer) const;

  /// Generate cc1-compatible command line arguments from this instance,
  /// wrapping the result as a std::vector<std::string>.
  ///
  /// This is a (less-efficient) wrapper over generateCC1CommandLine().
  std::vector<std::string> getCC1CommandLine() const;

  /// Create a compiler invocation from a list of input options.
  /// \returns true on success.
  ///
  /// \returns false if an error was encountered while parsing the arguments
  /// and attempts to recover and continue parsing the rest of the arguments.
  /// The recovery is best-effort and only guarantees that \p Res will end up in
  /// one of the vaild-to-access (albeit arbitrary) states.
  ///
  /// \param [out] Res - The resulting invocation.
  /// \param [in] CommandLineArgs - Array of argument strings, this must not
  /// contain "-cc1".
  static bool CreateFromArgs(CompilerInvocation &Res,
                             ArrayRef<const char *> CommandLineArgs,
                             DiagnosticsEngine &Diags,
                             const char *Argv0 = nullptr);

  /// Populate \p Opts with the default set of pointer authentication-related
  /// options given \p LangOpts and \p Triple.
  ///
  /// Note: This is intended to be used by tools which must be aware of
  /// pointer authentication-related code generation, e.g. lldb.
  static void setDefaultPointerAuthOptions(PointerAuthOptions &Opts,
                                           const LangOptions &LangOpts,
                                           const llvm::Triple &Triple);

  /// Compute the context hash - a string that uniquely identifies compiler
  /// settings.
  /// This is currently used mainly for distinguishing different variants of the
  /// same implicitly-built Clang module.
  std::string computeContextHash() const;

  /// Check that \p Args can be parsed and re-serialized without change,
  /// emiting diagnostics for any differences.
  ///
  /// This check is only suitable for command-lines that are expected to already
  /// be canonical.
  ///
  /// \return false if there are any errors.
  static bool checkCC1RoundTrip(ArrayRef<const char *> Args,
                                DiagnosticsEngine &Diags,
                                const char *Argv0 = nullptr);

  /// Reset all of the options that are not considered when building a
  /// module.
  void resetNonModularOptions();

  /// Disable implicit modules and canonicalize options that are only used by
  /// implicit modules.
  void clearImplicitModuleBuildOptions();

protected:
  /// Visits paths stored in the invocation. This is generally unsafe to call
  /// directly, and each sub-class need to ensure calling this doesn't violate
  /// its invariants.
  void visitPathsImpl(llvm::function_ref<bool(std::string &)> Predicate);

private:
  /// Replace the underlying option storage with newly-allocated copies of \p
  /// X's options. After this returns, no shared_ptr is shared with \p X.
  CompilerInvocation &deep_copy_assign(const CompilerInvocation &X);
  /// Share the underlying option storage with \p X. Subsequent mutations
  /// through \c getMut*Opts() will create per-option copies on first write.
  CompilerInvocation &shallow_copy_assign(const CompilerInvocation &X);

  /// Generate command line options from DiagnosticOptions.
  static void GenerateDiagnosticArgs(const DiagnosticOptions &Opts,
                                     ArgumentConsumer Consumer,
                                     bool DefaultDiagColor);

  /// Generate command line options from LangOptions.
  static void GenerateLangArgs(const LangOptions &Opts,
                               ArgumentConsumer Consumer, const llvm::Triple &T,
                               InputKind IK);

  // Generate command line options from CodeGenOptions.
  static void GenerateCodeGenArgs(const CodeGenOptions &Opts,
                                  ArgumentConsumer Consumer,
                                  const llvm::Triple &T,
                                  const std::string &OutputFile,
                                  const LangOptions *LangOpts);

  static bool CreateFromArgsImpl(CompilerInvocation &Res,
                                 ArrayRef<const char *> CommandLineArgs,
                                 DiagnosticsEngine &Diags, const char *Argv0);

  /// Parse command line options that map to LangOptions.
  static bool ParseLangArgs(LangOptions &Opts, llvm::opt::ArgList &Args,
                            InputKind IK, const llvm::Triple &T,
                            std::vector<std::string> &Includes,
                            DiagnosticsEngine &Diags);

  /// Parse command line options that map to CodeGenOptions.
  static bool ParseCodeGenArgs(CodeGenOptions &Opts, llvm::opt::ArgList &Args,
                               InputKind IK, DiagnosticsEngine &Diags,
                               const llvm::Triple &T,
                               const std::string &OutputFile,
                               const LangOptions &LangOptsRef);
};

IntrusiveRefCntPtr<llvm::vfs::FileSystem>
createVFSFromCompilerInvocation(const CompilerInvocation &CI,
                                DiagnosticsEngine &Diags);

IntrusiveRefCntPtr<llvm::vfs::FileSystem> createVFSFromCompilerInvocation(
    const CompilerInvocation &CI, DiagnosticsEngine &Diags,
    IntrusiveRefCntPtr<llvm::vfs::FileSystem> BaseFS);

IntrusiveRefCntPtr<llvm::vfs::FileSystem>
createVFSFromOverlayFiles(ArrayRef<std::string> VFSOverlayFiles,
                          DiagnosticsEngine &Diags,
                          IntrusiveRefCntPtr<llvm::vfs::FileSystem> BaseFS);

} // namespace clang

#endif // LLVM_CLANG_FRONTEND_COMPILERINVOCATION_H
