//===--- APINotesOptions.h --------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_APINOTES_APINOTESOPTIONS_H
#define LLVM_CLANG_APINOTES_APINOTESOPTIONS_H

#include "llvm/Support/VersionTuple.h"
#include <string>
#include <vector>

namespace clang {

/// Tracks various options which control how API notes are found and handled.
class APINotesOptions {
public:
#define TYPED_APINOTESOPT(Type, Name, Default) Type Name = Default;
#include "clang/APINotes/APINotesOptions.def"
#undef TYPED_APINOTESOPT
};

} // namespace clang

#endif // LLVM_CLANG_APINOTES_APINOTESOPTIONS_H
