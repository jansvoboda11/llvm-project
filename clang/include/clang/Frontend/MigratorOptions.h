//===--- MigratorOptions.h - MigratorOptions Options ------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This header contains the structures necessary for a front-end to specify
// various migration analysis.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_FRONTEND_MIGRATOROPTIONS_H
#define LLVM_CLANG_FRONTEND_MIGRATOROPTIONS_H

#include "llvm/Support/Compiler.h"

namespace clang {

class MigratorOptions {
public:
#define TYPED_MIGRATOROPT(Type, Name, Default) Type Name = Default;
#define BITFIELD_MIGRATOROPT(Type, Name, Bits, Default)                        \
  LLVM_PREFERRED_TYPE(Type) unsigned Name : Bits;
#include "clang/Frontend/MigratorOptions.def"
#undef TYPED_MIGRATOROPT
#undef BITFIELD_MIGRATOROPT

  MigratorOptions() {
#define TYPED_MIGRATOROPT(Type, Name, Default)
#define BITFIELD_MIGRATOROPT(Type, Name, Bits, Default) Name = Default;
#include "clang/Frontend/MigratorOptions.def"
#undef TYPED_MIGRATOROPT
#undef BITFIELD_MIGRATOROPT
  }
};

} // namespace clang
#endif
