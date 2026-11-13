// Test that asynchronously built modules waiting for each other because of a
// dependency cycle don't deadlock until the lock timeout, and that the cycle
// gets diagnosed like without -async-scan-modules.

// RUN: rm -rf %t
// RUN: split-file %s %t
// RUN: sed -e "s|DIR|%/t|g" %t/cdb.json.in > %t/cdb.json

// RUN: not clang-scan-deps -compilation-database %t/cdb.json -j 1 \
// RUN:   -format experimental-full 2> %t/sync.err
// RUN: rm -rf %t/cache
// RUN: %python %t/run_with_timeout.py 30 %t/async.err clang-scan-deps \
// RUN:   -compilation-database %t/cdb.json -j 1 -async-scan-modules \
// RUN:   -format experimental-full
// RUN: diff -u %t/sync.err %t/async.err
// RUN: FileCheck %s < %t/async.err
// CHECK: fatal error: cyclic dependency in module 'M': M -> N -> M

//--- run_with_timeout.py
import subprocess, sys
# usage: run_with_timeout.py SECONDS STDERR_FILE CMD...
# Fails if CMD doesn't finish in time or unexpectedly succeeds.
with open(sys.argv[2], "w") as err:
    try:
        rc = subprocess.run(sys.argv[3:], stdout=subprocess.DEVNULL, stderr=err,
                            timeout=int(sys.argv[1])).returncode
    except subprocess.TimeoutExpired:
        sys.exit("timed out")
sys.exit(0 if rc == 1 else f"unexpected exit code {rc}")

//--- cdb.json.in
[{
  "directory": "DIR",
  "command": "clang -fsyntax-only DIR/tu.c -fmodules -fimplicit-module-maps -fmodules-cache-path=DIR/cache -fimplicit-modules-lock-timeout=120",
  "file": "DIR/tu.c"
}]

//--- module.modulemap
module M { header "m.h" }
module N { header "n.h" }

//--- m.h
#pragma once
#include "n.h"

//--- n.h
#pragma once
#include "m.h"

//--- tu.c
#include "m.h"
#include "n.h"
