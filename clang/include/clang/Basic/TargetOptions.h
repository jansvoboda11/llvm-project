//===--- TargetOptions.h ----------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// Defines the clang::TargetOptions class.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_BASIC_TARGETOPTIONS_H
#define LLVM_CLANG_BASIC_TARGETOPTIONS_H

#include "clang/Basic/OpenCLOptions.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/Support/VersionTuple.h"
#include "llvm/Target/TargetOptions.h"
#include <cstdint>
#include <string>
#include <vector>

namespace clang {

/// Options for controlling the target.
class TargetOptions {
public:
  /// Aliased to keep TYPED_TARGETOPT lines free of nested commas in template
  /// arguments.
  using StringBoolMap = llvm::StringMap<bool>;

  /// Enumeration values for AMDGPU printf lowering scheme
  enum class AMDGPUPrintfKind {
    /// printf lowering scheme involving hostcalls, currently used by HIP
    /// programs by default
    Hostcall = 0,

    /// printf lowering scheme involving implicit printf buffers,
    Buffered = 1,
  };

#define TYPED_TARGETOPT(Type, Name, Default) Type Name = Default;
#include "clang/Basic/TargetOptions.def"
#undef TYPED_TARGETOPT
};

} // end namespace clang

#endif
