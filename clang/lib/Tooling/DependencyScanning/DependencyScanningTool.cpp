//===- DependencyScanningTool.cpp - clang-scan-deps service ---------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "clang/Tooling/DependencyScanning/DependencyScanningTool.h"
#include "clang/Frontend/Utils.h"
#include <optional>

using namespace clang;
using namespace tooling;
using namespace dependencies;

DependencyScanningTool::DependencyScanningTool(
    DependencyScanningService &Service,
    llvm::IntrusiveRefCntPtr<llvm::vfs::FileSystem> FS)
    : Worker(Service, std::move(FS)) {}

llvm::Expected<P1689Rule> DependencyScanningTool::getP1689ModuleDependencyFile(
    const CompileCommand &Command, StringRef CWD, std::string &MakeformatOutput,
    std::string &MakeformatOutputPath) {
  llvm::llvm_unreachable_internal("x");
//
//  class P1689ModuleDependencyPrinterConsumer
//      : public MakeDependencyPrinterConsumer {
//  public:
//    P1689ModuleDependencyPrinterConsumer(P1689Rule &Rule,
//                                         const CompileCommand &Command)
//        : Filename(Command.Filename), Rule(Rule) {
//      Rule.PrimaryOutput = Command.Output;
//    }
//
//    void handleProvidedAndRequiredStdCXXModules(
//        std::optional<P1689ModuleInfo> Provided,
//        std::vector<P1689ModuleInfo> Requires) override {
//      Rule.Provides = Provided;
//      if (Rule.Provides)
//        Rule.Provides->SourcePath = Filename.str();
//      Rule.Requires = Requires;
//    }
//
//    StringRef getMakeFormatDependencyOutputPath() {
//      if (Opts->OutputFormat != DependencyOutputFormat::Make)
//        return {};
//      return Opts->OutputFile;
//    }
//
//  private:
//    StringRef Filename;
//    P1689Rule &Rule;
//  };
//
//  class P1689ActionController : public DependencyActionController {
//  public:
//    // The lookupModuleOutput is for clang modules. P1689 format don't need it.
//    std::string lookupModuleOutput(const ModuleID &,
//                                   ModuleOutputKind Kind) override {
//      return "";
//    }
//  };
//
//  P1689Rule Rule;
//  P1689ModuleDependencyPrinterConsumer Consumer(Rule, Command);
//  P1689ActionController Controller;
//  auto Result = Worker.computeDependencies(CWD, Command.CommandLine, Consumer,
//                                           Controller);
//  if (Result)
//    return std::move(Result);
//
//  MakeformatOutputPath = Consumer.getMakeFormatDependencyOutputPath();
//  if (!MakeformatOutputPath.empty())
//    Consumer.printDependencies(MakeformatOutput);
//  return Rule;
}

llvm::Expected<TranslationUnitDeps>
DependencyScanningTool::getTranslationUnitDependencies(
    const std::vector<std::string> &CommandLine, StringRef CWD,
    const llvm::DenseSet<ModuleID> &AlreadySeen,
    LookupModuleOutputCallback LookupModuleOutput) {
  FullDependencyConsumer Consumer(AlreadySeen);
  CallbackActionController Controller(LookupModuleOutput);
  llvm::Error Result =
      Worker.computeDependencies(CWD, CommandLine, Consumer, Controller);
  if (Result)
    return std::move(Result);
  return Consumer.takeTranslationUnitDeps();
}

llvm::Expected<ModuleDepsGraph> DependencyScanningTool::getModuleDependencies(
    StringRef ModuleName, const std::vector<std::string> &CommandLine,
    StringRef CWD, const llvm::DenseSet<ModuleID> &AlreadySeen,
    LookupModuleOutputCallback LookupModuleOutput) {
  FullDependencyConsumer Consumer(AlreadySeen);
  CallbackActionController Controller(LookupModuleOutput);
  llvm::Error Result = Worker.computeDependencies(CWD, CommandLine, Consumer,
                                                  Controller, ModuleName);
  if (Result)
    return std::move(Result);
  return Consumer.takeModuleGraphDeps();
}

TranslationUnitDeps FullDependencyConsumer::takeTranslationUnitDeps() {
  TranslationUnitDeps TU;

  TU.FileDeps = std::move(Dependencies);
  TU.PrebuiltModuleDeps = std::move(PrebuiltModuleDeps);
  TU.Commands = std::move(Commands);

  for (auto &&M : ClangModuleDeps) {
    auto &MD = M.second;
    // TODO: Avoid handleModuleDependency even being called for modules
    //   we've already seen.
    if (AlreadySeen.count(M.first))
      continue;
    TU.ModuleGraph.push_back(std::move(MD));
  }
  TU.ClangModuleDeps = std::move(DirectModuleDeps);

  return TU;
}

ModuleDepsGraph FullDependencyConsumer::takeModuleGraphDeps() {
  ModuleDepsGraph ModuleGraph;

  for (auto &&M : ClangModuleDeps) {
    auto &MD = M.second;
    // TODO: Avoid handleModuleDependency even being called for modules
    //   we've already seen.
    if (AlreadySeen.count(M.first))
      continue;
    ModuleGraph.push_back(std::move(MD));
  }

  return ModuleGraph;
}

CallbackActionController::~CallbackActionController() {}
