// Check that the asynchronous module compiles of a context don't see the VFS
// overlay of another context, even though they reuse each other's scanning
// filesystems.

// RUN: rm -rf %t
// RUN: split-file %s %t
// RUN: sed -e "s|DIR|%/t|g" %t/cdb.json.in > %t/cdb.json
// RUN: sed -e "s|DIR|%/t|g" %t/overlay-x.yaml.in > %t/overlay-x.yaml
// RUN: sed -e "s|DIR|%/t|g" %t/overlay-y.yaml.in > %t/overlay-y.yaml
// RUN: %python -c "print(''.join(f'#define M{i} {i}\\n' for i in range(20000)))" > %t/xy/slow.h

// RUN: clang-scan-deps -compilation-database %t/cdb.json -j 1 \
// RUN:   -format experimental-full > %t/sync.json
// RUN: rm -rf %t/cache
// RUN: clang-scan-deps -compilation-database %t/cdb.json -j 1 -async-scan-modules \
// RUN:   -format experimental-full > %t/async.json
// RUN: diff -u %t/sync.json %t/async.json

// The overlay of the first TU makes module A depend on X, the overlay of the
// second TU on Y.
// RUN: %python %t/deps-of-a.py %t/async.json | FileCheck %s
// CHECK: TU 0: A imports X
// CHECK-NEXT: TU 1: A imports Y

//--- cdb.json.in
[{
  "directory": "DIR",
  "command": "clang -c DIR/tu.c -IDIR/a -IDIR/xy -fmodules -fmodules-cache-path=DIR/cache -fimplicit-modules -fimplicit-module-maps -ivfsoverlay DIR/overlay-x.yaml -o DIR/tu-x.o",
  "file": "DIR/tu.c"
},{
  "directory": "DIR",
  "command": "clang -c DIR/tu.c -IDIR/a -IDIR/xy -fmodules -fmodules-cache-path=DIR/cache -fimplicit-modules -fimplicit-module-maps -ivfsoverlay DIR/overlay-y.yaml -o DIR/tu-y.o",
  "file": "DIR/tu.c"
}]

//--- overlay-x.yaml.in
{
  "version": 0,
  "case-sensitive": "false",
  "roots": [{
    "name": "DIR/a/a.h", "type": "file",
    "external-contents": "DIR/impl/a-x.h"
  }]
}

//--- overlay-y.yaml.in
{
  "version": 0,
  "case-sensitive": "false",
  "roots": [{
    "name": "DIR/a/a.h", "type": "file",
    "external-contents": "DIR/impl/a-y.h"
  }]
}

//--- a/module.modulemap
module A { header "a.h" }

//--- a/a.h
#error "only reachable through an overlay"

//--- impl/a-x.h
#include "x.h"

//--- impl/a-y.h
#include "y.h"

//--- xy/module.modulemap
module X { header "x.h" }
module Y { header "y.h" }
module Slow { header "slow.h" }

//--- xy/x.h
//--- xy/y.h

//--- deps-of-a.py
import json, sys

deps = json.load(open(sys.argv[1]))
modules = {(m["name"], m["context-hash"]): m for m in deps["modules"]}
for i, tu in enumerate(deps["translation-units"]):
    for dep in tu["commands"][0]["clang-module-deps"]:
        if dep["module-name"] != "A":
            continue
        a = modules[(dep["module-name"], dep["context-hash"])]
        imports = " ".join(sorted(d["module-name"] for d in a["clang-module-deps"]))
        print(f"TU {i}: {a['name']} imports {imports}")

//--- tu.c
// The regular scan waits for the asynchronous build of Slow, so the
// asynchronous build of A finishes first and the regular scan uses its PCM.
#include "slow.h"
#include "a.h"
