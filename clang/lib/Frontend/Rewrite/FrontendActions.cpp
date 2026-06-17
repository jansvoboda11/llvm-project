//===--- FrontendActions.cpp ----------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "clang/Rewrite/Frontend/FrontendActions.h"
#include "clang/AST/ASTConsumer.h"
#include "clang/Basic/CharInfo.h"
#include "clang/Basic/LangStandard.h"
#include "clang/Config/config.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendActions.h"
#include "clang/Frontend/MutOptsHandle.h"
#include "clang/Frontend/Utils.h"
#include "clang/Lex/Preprocessor.h"
#include "clang/Lex/PreprocessorOptions.h"
#include "clang/Rewrite/Frontend/ASTConsumers.h"
#include "clang/Rewrite/Frontend/FixItRewriter.h"
#include "clang/Rewrite/Frontend/Rewriters.h"
#include "clang/Serialization/ASTReader.h"
#include "clang/Serialization/ModuleFile.h"
#include "clang/Serialization/ModuleManager.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/Support/CrashRecoveryContext.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/raw_ostream.h"
#include <memory>
#include <utility>

using namespace clang;

//===----------------------------------------------------------------------===//
// AST Consumer Actions
//===----------------------------------------------------------------------===//

std::unique_ptr<ASTConsumer>
HTMLPrintAction::CreateASTConsumer(CompilerInstance &CI, StringRef InFile) {
  if (std::unique_ptr<raw_ostream> OS =
          CI.createDefaultOutputFile(false, InFile))
    return CreateHTMLPrinter(std::move(OS), CI.getPreprocessor());
  return nullptr;
}

FixItAction::FixItAction() {}
FixItAction::~FixItAction() {}

std::unique_ptr<ASTConsumer>
FixItAction::CreateASTConsumer(CompilerInstance &CI, StringRef InFile) {
  return std::make_unique<ASTConsumer>();
}

namespace {
class FixItRewriteInPlace : public FixItOptions {
public:
  FixItRewriteInPlace() { InPlace = true; }

  std::string RewriteFilename(const std::string &Filename, int &fd) override {
    llvm_unreachable("don't call RewriteFilename for inplace rewrites");
  }
};

class FixItActionSuffixInserter : public FixItOptions {
  std::string NewSuffix;

public:
  FixItActionSuffixInserter(std::string NewSuffix, bool FixWhatYouCan)
      : NewSuffix(std::move(NewSuffix)) {
    this->FixWhatYouCan = FixWhatYouCan;
  }

  std::string RewriteFilename(const std::string &Filename, int &fd) override {
    fd = -1;
    SmallString<128> Path(Filename);
    llvm::sys::path::replace_extension(Path,
      NewSuffix + llvm::sys::path::extension(Path));
    return std::string(Path);
  }
};

class FixItRewriteToTemp : public FixItOptions {
public:
  std::string RewriteFilename(const std::string &Filename, int &fd) override {
    SmallString<128> Path;
    llvm::sys::fs::createTemporaryFile(llvm::sys::path::filename(Filename),
                                       llvm::sys::path::extension(Filename).drop_front(), fd,
                                       Path);
    return std::string(Path);
  }
};
} // end anonymous namespace

bool FixItAction::BeginSourceFileAction(CompilerInstance &CI) {
  const FrontendOptions &FEOpts = getCompilerInstance().getFrontendOpts();
  if (!FEOpts.FixItSuffix.empty()) {
    FixItOpts.reset(new FixItActionSuffixInserter(FEOpts.FixItSuffix,
                                                  FEOpts.FixWhatYouCan));
  } else {
    FixItOpts.reset(new FixItRewriteInPlace);
    FixItOpts->FixWhatYouCan = FEOpts.FixWhatYouCan;
  }
  Rewriter.reset(new FixItRewriter(CI.getDiagnostics(), CI.getSourceManager(),
                                   CI.getLangOpts(), FixItOpts.get()));
  return ASTFrontendAction::BeginSourceFileAction(CI);
}

void FixItAction::EndSourceFileAction() {
  // Otherwise rewrite all files.
  Rewriter->WriteFixedFiles();
  ASTFrontendAction::EndSourceFileAction();
}

bool FixItRecompile::BeginInvocation(const CompilerInvocation &Invocation,
                                     FrontendInputFile &Input,
                                     CompilerInstance &CI) {
  DiagnosticsEngine &Diags = CI.getDiagnostics();
  llvm::vfs::FileSystem &VFS = CI.getVirtualFileSystem();
  // Run a SyntaxOnly fix-it pass on a separate CompilerInstance with a copy
  // of the outer invocation, then forward the rewritten files into the outer
  // invocation as remapped inputs so the wrapped action sees them.
  std::vector<std::pair<std::string, std::string>> RewrittenFiles;
  bool err = false;
  {
    auto FixInvocation = std::make_shared<CompilerInvocation>(Invocation);
    auto FixCI = std::make_unique<CompilerInstance>(FixInvocation);
    FixCI->createVirtualFileSystem(
        llvm::IntrusiveRefCntPtr<llvm::vfs::FileSystem>(&VFS));
    FixCI->createDiagnostics(Diags.getClient(), /*ShouldOwnClient=*/false);
    FixCI->createFileManager();
    CompilerInstance::TargetCreationResult TR;
    if (!CompilerInstance::createTarget(FixCI->getDiagnostics(), *FixInvocation,
                                        TR))
      return false;
    FixCI->setTarget(TR.Target.get());
    FixCI->setAuxTarget(TR.AuxTarget.get());
    FixCI->setAuxTargetOpts(std::move(TR.AuxTargetOpts));

    const FrontendOptions &FEOpts = FixInvocation->getFrontendOpts();
    std::unique_ptr<FrontendAction> FixAction(new SyntaxOnlyAction());
    if (FixAction->BeginSourceFile(*FixCI, FEOpts.Inputs[0])) {
      std::unique_ptr<FixItOptions> FixItOpts;
      if (FEOpts.FixToTemporaries)
        FixItOpts.reset(new FixItRewriteToTemp());
      else
        FixItOpts.reset(new FixItRewriteInPlace());
      FixItOpts->Silent = true;
      FixItOpts->FixWhatYouCan = FEOpts.FixWhatYouCan;
      FixItOpts->FixOnlyWarnings = FEOpts.FixOnlyWarnings;
      FixItRewriter Rewriter(FixCI->getDiagnostics(), FixCI->getSourceManager(),
                             FixCI->getLangOpts(), FixItOpts.get());
      if (llvm::Error Err = FixAction->Execute()) {
        // FIXME this drops the error on the floor.
        consumeError(std::move(Err));
        return false;
      }

      err = Rewriter.WriteFixedFiles(&RewrittenFiles);

      FixAction->EndSourceFile();
    } else {
      err = true;
    }
  }
  if (err)
    return false;

  // The inner fix-it run shared the outer DiagnosticConsumer; clear any
  // diagnostics accumulated there so the wrapped action's compilation is
  // not affected. The outer DiagnosticsEngine itself was not used, so it
  // does not need to be reset.
  Diags.getClient()->clear();

  // Append the freshly rewritten files to PreprocessorOpts.RemappedFiles
  // through the scoped handle. We seed the new vector from the existing
  // entries because the handle's setter takes a value, not an appender.
  std::vector<std::pair<std::string, std::string>> NewRemapped =
      Invocation.getPreprocessorOpts().RemappedFiles;
  NewRemapped.insert(NewRemapped.end(), RewrittenFiles.begin(),
                     RewrittenFiles.end());
  Invocation.withMutPreprocessorOpts(
      [&](MutPreprocessorOptsHandle &H) {
        H.setRemappedFiles(std::move(NewRemapped));
        H.setRemappedFilesKeepOriginalName(false);
      });

  return true;
}

#if CLANG_ENABLE_OBJC_REWRITER

std::unique_ptr<ASTConsumer>
RewriteObjCAction::CreateASTConsumer(CompilerInstance &CI, StringRef InFile) {
  if (std::unique_ptr<raw_ostream> OS =
          CI.createDefaultOutputFile(false, InFile, "cpp")) {
    if (CI.getLangOpts().ObjCRuntime.isNonFragile())
      return CreateModernObjCRewriter(std::string(InFile), std::move(OS),
                                      CI.getDiagnostics(), CI.getLangOpts(),
                                      CI.getDiagnosticOpts().NoRewriteMacros,
                                      (CI.getCodeGenOpts().getDebugInfo() !=
                                       llvm::codegenoptions::NoDebugInfo));
    return CreateObjCRewriter(std::string(InFile), std::move(OS),
                              CI.getDiagnostics(), CI.getLangOpts(),
                              CI.getDiagnosticOpts().NoRewriteMacros);
  }
  return nullptr;
}

#endif

//===----------------------------------------------------------------------===//
// Preprocessor Actions
//===----------------------------------------------------------------------===//

void RewriteMacrosAction::ExecuteAction() {
  CompilerInstance &CI = getCompilerInstance();
  std::unique_ptr<raw_ostream> OS =
      CI.createDefaultOutputFile(/*Binary=*/true, getCurrentFileOrBufferName());
  if (!OS) return;

  RewriteMacrosInInput(CI.getPreprocessor(), OS.get());
}

void RewriteTestAction::ExecuteAction() {
  CompilerInstance &CI = getCompilerInstance();
  std::unique_ptr<raw_ostream> OS =
      CI.createDefaultOutputFile(/*Binary=*/false, getCurrentFileOrBufferName());
  if (!OS) return;

  DoRewriteTest(CI.getPreprocessor(), OS.get());
}

class RewriteIncludesAction::RewriteImportsListener : public ASTReaderListener {
  CompilerInstance &CI;
  std::weak_ptr<raw_ostream> Out;

  llvm::DenseSet<const serialization::ModuleFile *> Rewritten;

public:
  RewriteImportsListener(CompilerInstance &CI, std::shared_ptr<raw_ostream> Out)
      : CI(CI), Out(Out) {}

  void visitModuleFile(ModuleFileName Filename, serialization::ModuleKind Kind,
                       bool DirectlyImported) override {
    serialization::ModuleFile *MF =
        CI.getASTReader()->getModuleManager().lookupByFileName(Filename);
    assert(MF && "missing module file for loaded module?");

    // Only rewrite each module file once.
    if (!Rewritten.insert(MF).second)
      return;

    // Not interested in PCH / preambles.
    if (!MF->isModule())
      return;

    auto OS = Out.lock();
    assert(OS && "loaded module file after finishing rewrite action?");

    (*OS) << "#pragma clang module build ";
    if (isValidAsciiIdentifier(MF->ModuleName))
      (*OS) << MF->ModuleName;
    else {
      (*OS) << '"';
      OS->write_escaped(MF->ModuleName);
      (*OS) << '"';
    }
    (*OS) << '\n';

    // Rewrite the contents of the module in a separate compiler instance.
    auto NewInvocation =
        std::make_shared<CompilerInvocation>(CI.getInvocation());
    NewInvocation->getMutFrontendOpts().DisableFree = false;
    NewInvocation->getMutFrontendOpts().Inputs.clear();
    NewInvocation->getMutFrontendOpts().Inputs.emplace_back(
        Filename, InputKind(Language::Unknown, InputKind::Precompiled));
    NewInvocation->getMutFrontendOpts().ModuleFiles.clear();
    NewInvocation->getMutFrontendOpts().ModuleMapFiles.clear();
    // Don't recursively rewrite imports. We handle them all at the top level.
    NewInvocation->getMutPreprocessorOutputOpts().RewriteImports = false;

    CompilerInvocation &MutNewInv = *NewInvocation;
    CompilerInstance Instance(std::move(NewInvocation),
                              CI.getPCHContainerOperations(),
                              CI.getModuleCachePtr());
    Instance.setVirtualFileSystem(CI.getVirtualFileSystemPtr());
    Instance.createDiagnostics(
        new ForwardingDiagnosticConsumer(CI.getDiagnosticClient()),
        /*ShouldOwnClient=*/true);

    CompilerInstance::TargetCreationResult TR;
    if (!CompilerInstance::createTarget(Instance.getDiagnostics(), MutNewInv,
                                        TR))
      return;
    Instance.installTarget(std::move(TR));

    llvm::CrashRecoveryContext().RunSafelyOnThread([&]() {
      RewriteIncludesAction Action;
      Action.OutputStream = OS;
      Instance.ExecuteAction(Action);
    });

    (*OS) << "#pragma clang module endbuild /*" << MF->ModuleName << "*/\n";
  }
};

bool RewriteIncludesAction::BeginSourceFileAction(CompilerInstance &CI) {
  if (!OutputStream) {
    OutputStream =
        CI.createDefaultOutputFile(/*Binary=*/true, getCurrentFileOrBufferName());
    if (!OutputStream)
      return false;
  }

  auto &OS = *OutputStream;

  // If we're preprocessing a module map, start by dumping the contents of the
  // module itself before switching to the input buffer.
  auto &Input = getCurrentInput();
  if (Input.getKind().getFormat() == InputKind::ModuleMap) {
    if (Input.isFile()) {
      OS << "# 1 \"";
      OS.write_escaped(Input.getFile());
      OS << "\"\n";
    }
    getCurrentModule()->print(OS);
    OS << "#pragma clang module contents\n";
  }

  // If we're rewriting imports, set up a listener to track when we import
  // module files.
  if (CI.getPreprocessorOutputOpts().RewriteImports) {
    CI.createASTReader();
    CI.getASTReader()->addListener(
        std::make_unique<RewriteImportsListener>(CI, OutputStream));
  }

  return PreprocessorFrontendAction::BeginSourceFileAction(CI);
}

void RewriteIncludesAction::ExecuteAction() {
  CompilerInstance &CI = getCompilerInstance();

  // If we're rewriting imports, emit the module build output first rather
  // than switching back and forth (potentially in the middle of a line).
  if (CI.getPreprocessorOutputOpts().RewriteImports) {
    std::string Buffer;
    llvm::raw_string_ostream OS(Buffer);

    RewriteIncludesInInput(CI.getPreprocessor(), &OS,
                           CI.getPreprocessorOutputOpts());

    (*OutputStream) << OS.str();
  } else {
    RewriteIncludesInInput(CI.getPreprocessor(), OutputStream.get(),
                           CI.getPreprocessorOutputOpts());
  }

  OutputStream.reset();
}
