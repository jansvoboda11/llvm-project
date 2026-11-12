// Test that PCMs built in-process get spilled to disk when exceeding the memory
// limit of the module cache.

// UNSUPPORTED: system-windows
// RUN: rm -rf %t
// RUN: split-file %s %t
// RUN: sed -e "s|DIR|%/t|g" %t/cdb.json.template > %t/cdb.json

// Without a memory limit, nothing gets written to disk.
// RUN: clang-scan-deps -compilation-database %t/cdb.json -no-flush-module-cache \
// RUN:   -format experimental-full -j 1 -o %t/deps-unlimited.json
// RUN: find %t/cache -name "*.pcm" | count 0

// With a memory limit, PCM of M gets spilled at the start of the second TU, but
// the PCM of N stays in memory.
// RUN: rm -rf %t/cache
// RUN: clang-scan-deps -compilation-database %t/cdb.json -no-flush-module-cache \
// RUN:   -module-cache-memory-limit=1 -log-path=%t/scan.log \
// RUN:   -format experimental-full -j 1 -o %t/deps-limited.json
// RUN: find %t/cache -name "M-*.pcm" | count 1
// RUN: find %t/cache -name "N-*.pcm" | count 0
// RUN: FileCheck %s < %t/scan.log
// RUN: diff %t/deps-unlimited.json %t/deps-limited.json

// CHECK: pcm_write: {{.*}}M-{{.*}}.pcm
// CHECK: starting scanning command:{{.*}}tu2.c
// CHECK: pcm_spill: {{.*}}M-{{.*}}.pcm
// CHECK: pcm_write: {{.*}}N-{{.*}}.pcm
// CHECK-NOT: pcm_spill

//--- cdb.json.template
[{
  "directory": "DIR",
  "command": "clang -fsyntax-only DIR/tu1.c -fmodules -fimplicit-module-maps -fmodules-cache-path=DIR/cache",
  "file": "DIR/tu1.c"
},{
  "directory": "DIR",
  "command": "clang -fsyntax-only DIR/tu2.c -fmodules -fimplicit-module-maps -fmodules-cache-path=DIR/cache",
  "file": "DIR/tu2.c"
}]

//--- module.modulemap
module M { header "M.h" }
module N { header "N.h" }

//--- M.h
void m(void);

//--- N.h
void n(void);

//--- tu1.c
#include "M.h"

//--- tu2.c
#include "N.h"
