// Test that diagnostics emitted while building modules are reported the same
// way with -async-scan-modules as without it, even though asynchronous module
// compilations don't report their diagnostics.

// RUN: rm -rf %t
// RUN: split-file %s %t
// RUN: sed -e "s|DIR|%/t|g" %t/cdb.json.in > %t/cdb.json
// RUN: sed -e "s|DIR|%/t|g" %t/cdb-remarks.json.in > %t/cdb-remarks.json

// A warning in a module imported by another module.
// RUN: clang-scan-deps -compilation-database %t/cdb.json -j 1 \
// RUN:   -format experimental-full -o %t/sync.json 2> %t/sync.err
// RUN: rm -rf %t/cache
// RUN: clang-scan-deps -compilation-database %t/cdb.json -j 1 -async-scan-modules \
// RUN:   -format experimental-full -o %t/async.json 2> %t/async.err
// RUN: diff -u %t/sync.err %t/async.err
// RUN: diff -u %t/sync.json %t/async.json
// RUN: FileCheck %s --check-prefix=WARN < %t/async.err
// WARN:      While building module 'A' imported from {{.*}}tu.c:1:
// WARN-NEXT: While building module 'B' imported from {{.*}}a.h:1:
// WARN:      b.h:2:9: warning: 'X' macro redefined

// Remarks about building modules.
// RUN: rm -rf %t/cache
// RUN: clang-scan-deps -compilation-database %t/cdb-remarks.json -j 1 \
// RUN:   -format experimental-full -o %t/sync-remarks.json 2> %t/sync-remarks.err
// RUN: rm -rf %t/cache
// RUN: clang-scan-deps -compilation-database %t/cdb-remarks.json -j 1 \
// RUN:   -async-scan-modules -format experimental-full -o %t/async-remarks.json \
// RUN:   2> %t/async-remarks.err
// RUN: diff -u %t/sync-remarks.err %t/async-remarks.err
// RUN: FileCheck %s --check-prefix=REMARK < %t/async-remarks.err
// REMARK: remark: building module 'C'

//--- cdb.json.in
[{
  "directory": "DIR",
  "command": "clang -fsyntax-only DIR/tu.c -fmodules -fimplicit-module-maps -fmodules-cache-path=DIR/cache",
  "file": "DIR/tu.c"
}]

//--- cdb-remarks.json.in
[{
  "directory": "DIR",
  "command": "clang -fsyntax-only DIR/tu-remarks.c -fmodules -fimplicit-module-maps -fmodules-cache-path=DIR/cache -Rmodule-build",
  "file": "DIR/tu-remarks.c"
}]

//--- module.modulemap
module A { header "a.h" }
module B { header "b.h" }
module C { header "c.h" }

//--- a.h
#include "b.h"

//--- b.h
#define X 1
#define X 2

//--- c.h
void c(void);

//--- tu.c
#include "a.h"

//--- tu-remarks.c
#include "c.h"
