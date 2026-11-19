// Check that sharing what the scanner learns about modules between translation
// units whose invocations only differ in per-TU paths does not change the
// results.

// RUN: rm -rf %t
// RUN: split-file %s %t
// RUN: sed -e "s|DIR|%/t|g" %t/cdb.json.template > %t/cdb.json

// RUN: clang-scan-deps -compilation-database %t/cdb.json -j 1 \
// RUN:   -format experimental-full > %t/plain.json
// RUN: rm -rf %t/cache
// RUN: clang-scan-deps -compilation-database %t/cdb.json -j 1 \
// RUN:   -format experimental-full -share-module-deps > %t/shared.json
// RUN: diff %t/plain.json %t/shared.json

// Also with a warm module cache and multiple workers.
// RUN: clang-scan-deps -compilation-database %t/cdb.json -j 4 \
// RUN:   -format experimental-full -share-module-deps > %t/shared-warm.json
// RUN: diff %t/plain.json %t/shared-warm.json

// RUN: cat %t/shared.json | sed 's:\\\\\?:/:g' | FileCheck %s -DPREFIX=%/t

// The two translation units with the same flags agree on the module, the one
// with a different macro definition gets its own variant.
// CHECK:      "modules": [
// CHECK:          "context-hash": "[[HASH_PLAIN:.*]]",
// CHECK:          "name": "Mod"
// CHECK:          "context-hash": "[[HASH_MACRO:.*]]",
// CHECK:          "name": "Mod"
// CHECK:      "translation-units": [
// CHECK:              "context-hash": "[[HASH_PLAIN]]",
// CHECK-NEXT:         "module-name": "Mod"
// CHECK:              "[[PREFIX]]/a.o"
// CHECK:              "[[PREFIX]]/a.c"
// CHECK:          "input-file": "[[PREFIX]]/a.c"
// CHECK:              "context-hash": "[[HASH_PLAIN]]",
// CHECK-NEXT:         "module-name": "Mod"
// CHECK:              "[[PREFIX]]/b.o"
// CHECK:              "[[PREFIX]]/b.c"
// CHECK:          "input-file": "[[PREFIX]]/b.c"
// CHECK:              "context-hash": "[[HASH_MACRO]]",
// CHECK-NEXT:         "module-name": "Mod"
// CHECK:              "[[PREFIX]]/c.o"
// CHECK:          "input-file": "[[PREFIX]]/c.c"

//--- cdb.json.template
[{
  "directory": "DIR",
  "file": "DIR/a.c",
  "command": "clang -fmodules -fimplicit-module-maps -fmodules-cache-path=DIR/cache -I DIR/include -MD -MF DIR/a.d --serialize-diagnostics DIR/a.dia -c DIR/a.c -o DIR/a.o"
},{
  "directory": "DIR",
  "file": "DIR/b.c",
  "command": "clang -fmodules -fimplicit-module-maps -fmodules-cache-path=DIR/cache -I DIR/include -MD -MF DIR/b.d --serialize-diagnostics DIR/b.dia -c DIR/b.c -o DIR/b.o"
},{
  "directory": "DIR",
  "file": "DIR/c.c",
  "command": "clang -fmodules -fimplicit-module-maps -fmodules-cache-path=DIR/cache -I DIR/include -DMACRO=1 -c DIR/c.c -o DIR/c.o"
}]

//--- include/module.modulemap
module Mod { header "mod.h" }
//--- include/mod.h
#ifdef MACRO
#endif
//--- a.c
#include "mod.h"
//--- b.c
#include "mod.h"
//--- c.c
#include "mod.h"
