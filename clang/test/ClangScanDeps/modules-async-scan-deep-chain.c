// Test that asynchronous module compiles don't deadlock when they run on fewer
// threads than the depth of the module graph: compiles waiting for a module
// that's queued behind them must let it run. The graph is a 64-deep chain plus
// layers of modules importing every module of the next layer (fan-in).

// RUN: rm -rf %t
// RUN: split-file %s %t
// RUN: %python %t/gen.py %t
// RUN: sed -e "s|DIR|%/t|g" %t/cdb.json.in > %t/cdb.json

// RUN: clang-scan-deps -compilation-database %t/cdb.json -j 1 \
// RUN:   -format experimental-full -o %t/sync.json
// RUN: rm -rf %t/cache
// RUN: %python %t/run_with_timeout.py 60 clang-scan-deps \
// RUN:   -compilation-database %t/cdb.json -j 1 -async-scan-modules \
// RUN:   -async-scan-modules-threads 1 -format experimental-full \
// RUN:   -o %t/async1.json
// RUN: diff -u %t/sync.json %t/async1.json
// RUN: rm -rf %t/cache
// RUN: %python %t/run_with_timeout.py 60 clang-scan-deps \
// RUN:   -compilation-database %t/cdb.json -j 4 -async-scan-modules \
// RUN:   -async-scan-modules-threads 2 -format experimental-full \
// RUN:   -o %t/async2.json
// RUN: diff -u %t/sync.json %t/async2.json

// RUN: FileCheck %s < %t/sync.json
// CHECK-DAG: "name": "Chain0"
// CHECK-DAG: "name": "Chain63"
// CHECK-DAG: "name": "Layer0_0"
// CHECK-DAG: "name": "Layer7_5"

//--- run_with_timeout.py
import subprocess, sys
# usage: run_with_timeout.py SECONDS CMD...
try:
    rc = subprocess.run(sys.argv[2:], timeout=int(sys.argv[1])).returncode
except subprocess.TimeoutExpired:
    sys.exit("timed out")
sys.exit(rc)

//--- gen.py
import os, sys
d = sys.argv[1]
depth, layers, width = 64, 8, 6
with open(os.path.join(d, "module.modulemap"), "w") as mm:
    for i in range(depth):
        mm.write(f'module Chain{i} {{ header "chain{i}.h" }}\n')
        with open(os.path.join(d, f"chain{i}.h"), "w") as h:
            if i + 1 < depth:
                h.write(f'#include "chain{i + 1}.h"\n')
            h.write(f"void chain{i}(void);\n")
    for l in range(layers):
        for w in range(width):
            mm.write(f'module Layer{l}_{w} {{ header "layer{l}_{w}.h" }}\n')
            with open(os.path.join(d, f"layer{l}_{w}.h"), "w") as h:
                if l + 1 < layers:
                    for n in range(width):
                        h.write(f'#include "layer{l + 1}_{n}.h"\n')
                else:
                    h.write('#include "chain48.h"\n')
                h.write(f"void layer{l}_{w}(void);\n")

//--- cdb.json.in
[
  {
    "directory": "DIR",
    "command": "clang -fsyntax-only DIR/tu0.c -fmodules -fimplicit-module-maps -fmodules-cache-path=DIR/cache -fimplicit-modules-lock-timeout=3600",
    "file": "DIR/tu0.c"
  },
  {
    "directory": "DIR",
    "command": "clang -fsyntax-only DIR/tu1.c -fmodules -fimplicit-module-maps -fmodules-cache-path=DIR/cache -fimplicit-modules-lock-timeout=3600",
    "file": "DIR/tu1.c"
  },
  {
    "directory": "DIR",
    "command": "clang -fsyntax-only DIR/tu2.c -fmodules -fimplicit-module-maps -fmodules-cache-path=DIR/cache -fimplicit-modules-lock-timeout=3600",
    "file": "DIR/tu2.c"
  }
]

//--- tu0.c
#include "chain0.h"

//--- tu1.c
#include "layer0_0.h"
#include "layer0_5.h"

//--- tu2.c
#include "chain32.h"
#include "layer4_2.h"
