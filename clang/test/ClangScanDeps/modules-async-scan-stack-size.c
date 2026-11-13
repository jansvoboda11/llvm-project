// Test that modules built by asynchronous module scans get as much stack as the
// ones built by the regular scan. Deeply nested parentheses in a preprocessor
// condition recurse in the expression evaluator.

// RUN: rm -rf %t
// RUN: split-file %s %t
// RUN: sed -e "s|DIR|%/t|g" %t/cdb.json.in > %t/cdb.json
// RUN: %python -c "print('#if ' + '(' * 4000 + '1' + ')' * 4000 + '\nvoid m(void);\n#endif')" > %t/m.h

// RUN: clang-scan-deps -compilation-database %t/cdb.json -j 1 \
// RUN:   -format experimental-full > %t/sync.json
// RUN: rm -rf %t/cache
// RUN: clang-scan-deps -compilation-database %t/cdb.json -j 1 -async-scan-modules \
// RUN:   -format experimental-full > %t/async.json
// RUN: diff -u %t/sync.json %t/async.json
// RUN: FileCheck %s < %t/async.json
// CHECK: "name": "M"

//--- cdb.json.in
[{
  "directory": "DIR",
  "command": "clang -fsyntax-only DIR/tu.c -fmodules -fimplicit-module-maps -fmodules-cache-path=DIR/cache",
  "file": "DIR/tu.c"
}]

//--- module.modulemap
module M { header "m.h" }

//--- tu.c
#include "m.h"
