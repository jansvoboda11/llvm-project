//===--- InputInfo.h - Input Source & Type Information ----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_DRIVER_INPUTINFO_H
#define LLVM_CLANG_DRIVER_INPUTINFO_H

#include "clang/Driver/Action.h"
#include "clang/Driver/Types.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Option/Arg.h"
#include <cassert>
#include <string>

namespace clang {
namespace driver {

/// InputInfo - Wrapper for information about an input source.
class InputInfo {
  // FIXME: The distinction between filenames and inputarg here is
  // gross; we should probably drop the idea of a "linker
  // input". Doing so means tweaking pipelining to still create link
  // steps when it sees linker inputs (but not treat them as
  // arguments), and making sure that arguments get rendered
  // correctly.
  enum Class {
    Nothing,
    Filename,
    InputArg,
    Pipe
  };

  llvm::StringRef FilenameStr;
  const llvm::opt::Arg *Arg = nullptr;
  Class Kind;
  const Action* Act;
  types::ID Type;
  llvm::StringRef BaseInput;

  static types::ID GetActionType(const Action *A) {
    return A != nullptr ? A->getType() : types::TY_Nothing;
  }

public:
  InputInfo() : InputInfo(nullptr, llvm::StringRef()) {}
  InputInfo(const Action *A, llvm::StringRef _BaseInput)
      : Kind(Nothing), Act(A), Type(GetActionType(A)), BaseInput(_BaseInput) {}

  InputInfo(types::ID _Type, llvm::StringRef _Filename,
            llvm::StringRef _BaseInput)
      : FilenameStr(_Filename), Kind(Filename), Act(nullptr), Type(_Type),
        BaseInput(_BaseInput) {}
  InputInfo(const Action *A, llvm::StringRef _Filename,
            llvm::StringRef _BaseInput)
      : FilenameStr(_Filename), Kind(Filename), Act(A),
        Type(GetActionType(A)), BaseInput(_BaseInput) {}

  InputInfo(types::ID _Type, const llvm::opt::Arg *_InputArg,
            llvm::StringRef _BaseInput)
      : Arg(_InputArg), Kind(InputArg), Act(nullptr), Type(_Type),
        BaseInput(_BaseInput) {}
  InputInfo(const Action *A, const llvm::opt::Arg *_InputArg,
            llvm::StringRef _BaseInput)
      : Arg(_InputArg), Kind(InputArg), Act(A), Type(GetActionType(A)),
        BaseInput(_BaseInput) {}

  bool isNothing() const { return Kind == Nothing; }
  bool isFilename() const { return Kind == Filename; }
  bool isInputArg() const { return Kind == InputArg; }
  types::ID getType() const { return Type; }
  llvm::StringRef getBaseInput() const { return BaseInput; }
  /// The action for which this InputInfo was created.  May be null.
  const Action *getAction() const { return Act; }
  void setAction(const Action *A) { Act = A; }

  llvm::StringRef getFilename() const {
    assert(isFilename() && "Invalid accessor.");
    return FilenameStr;
  }
  const llvm::opt::Arg &getInputArg() const {
    assert(isInputArg() && "Invalid accessor.");
    return *Arg;
  }

  /// getAsString - Return a string name for this input, for
  /// debugging.
  std::string getAsString() const {
    if (isFilename())
      return ("\"" + getFilename() + "\"").str();
    else if (isInputArg())
      return "(input arg)";
    else
      return "(nothing)";
  }
};

} // end namespace driver
} // end namespace clang

#endif
