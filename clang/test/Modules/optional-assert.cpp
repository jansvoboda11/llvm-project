// RUN: rm -rf %t && split-file %s %t

// RUN: %clang -std=c++17 -fsyntax-only -c %t/tu.cpp -fmodules -fcxx-modules \
// RUN:   -fmodules-cache-path=%t/module.cache -arch arm64 -isysroot /Applications/Xcode_SummitE_0.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX13.3.sdk -mmacosx-version-min=13.1 \
// RUN:   -Rmodule-import

//--- module.modulemap
module A {
  module A { header "a.h" }
}

module B {
  module B1 { header "b1.h" }
  module B2 { header "b2.h" }
}

//--- a.h
#include <optional>

void bar() {
  std::optional<uint64_t> X = 0;
}

//--- b1.h
#include "a.h"

//--- b2.h
#include <optional>

std::optional<uint64_t> foo() {
  std::optional<uint64_t> X;
  return X;
}

//--- tu.cpp
#include "b1.h"
