//===--- PreprocessorOutputOptions.h ----------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_FRONTEND_PREPROCESSOROUTPUTOPTIONS_H
#define LLVM_CLANG_FRONTEND_PREPROCESSOROUTPUTOPTIONS_H

#include "llvm/Support/Compiler.h"

namespace clang {

/// PreprocessorOutputOptions - Options for controlling the C preprocessor
/// output (e.g., -E).
class PreprocessorOutputOptions {
public:
#define TYPED_PPOUTPUTOPT(Type, Name, Default) Type Name = Default;
#define BITFIELD_PPOUTPUTOPT(Type, Name, Bits, Default)                        \
  LLVM_PREFERRED_TYPE(Type) unsigned Name : Bits;
#include "clang/Frontend/PreprocessorOutputOptions.def"
#undef TYPED_PPOUTPUTOPT
#undef BITFIELD_PPOUTPUTOPT

  PreprocessorOutputOptions() {
#define TYPED_PPOUTPUTOPT(Type, Name, Default)
#define BITFIELD_PPOUTPUTOPT(Type, Name, Bits, Default) Name = Default;
#include "clang/Frontend/PreprocessorOutputOptions.def"
#undef TYPED_PPOUTPUTOPT
#undef BITFIELD_PPOUTPUTOPT
  }
};

} // end namespace clang

#endif
