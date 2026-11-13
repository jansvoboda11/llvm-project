// Test that PCMs built asynchronously go through the in-process module cache
// like the ones built synchronously, instead of being written to disk directly.

// RUN: rm -rf %t
// RUN: split-file %s %t
// RUN: sed -e "s|DIR|%/t|g" %t/cdb.json.in > %t/cdb.json

// Without flushing, nothing gets written to disk.
// RUN: clang-scan-deps -compilation-database %t/cdb.json -j 1 -async-scan-modules \
// RUN:   -no-flush-module-cache -format experimental-full \
// RUN:   -mode preprocess-dependency-directives -o %t/no-flush.json
// RUN: find %t/module-cache -name "*.pcm" | count 0

// By default, all PCMs get written through the module cache and flushed.
// RUN: rm -rf %t/module-cache
// RUN: clang-scan-deps -compilation-database %t/cdb.json -j 1 -async-scan-modules \
// RUN:   -log-path=%t/scan.log -format experimental-full \
// RUN:   -mode preprocess-dependency-directives -o %t/flush.json
// RUN: find %t/module-cache -name "*.pcm" | count 3
// RUN: FileCheck %s < %t/scan.log
// RUN: diff -u %t/no-flush.json %t/flush.json

// CHECK-DAG: pcm_write: {{.*}}/A-{{[^/]*}}.pcm
// CHECK-DAG: pcm_write: {{.*}}/B-{{[^/]*}}.pcm
// CHECK-DAG: pcm_write: {{.*}}/C-{{[^/]*}}.pcm

//--- cdb.json.in
[{
  "directory": "DIR",
  "command": "clang -c DIR/main.c -IDIR -fmodules -fmodules-cache-path=DIR/module-cache -fimplicit-modules -fimplicit-module-maps",
  "file": "DIR/main.c"
}]

//--- module.modulemap
module A { header "a.h" }
module B { header "b.h" }
module C { header "c.h" }

//--- a.h
#include "b.h"
void a(void);

//--- b.h
void b(void);

//--- c.h
void c(void);

//--- main.c
#include "a.h"
#include "c.h"
void m(void) {
  a();
  c();
}
