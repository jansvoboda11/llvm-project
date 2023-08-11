// RUN: rm -rf %t
// RUN: split-file %s %t

//--- tu.c
#include "M.h"
#include "T2.h"
//--- module.modulemap
module M { header "M.h" }
//--- M.h
#include "T1.h"
//--- T1.h
//--- T2.h
//--- cdb.json.template
[{
  "directory": "DIR",
  "file": "DIR/tu.c",
  "command": "clang -fmodules -fimplicit-module-maps -fmodules-cache-path=DIR/cache -I DIR -c DIR/tu.c -o DIR/tu.o"
}]

// RUN: sed "s|DIR|%/t|g" %t/cdb.json.template > %t/cdb.json
// RUN: clang-scan-deps -compilation-database %t/cdb.json -format make > %t/result.mk
