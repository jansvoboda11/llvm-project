// Check that the dependency directives of a file aren't used to lex contents
// that replaced it via -remap-file.

// RUN: rm -rf %t
// RUN: split-file %s %t
// RUN: sed "s|DIR|%/t|g" %t/cdb.json.template > %t/cdb.json
// RUN: clang-scan-deps -compilation-database %t/cdb.json -j 1 | FileCheck %s

// CHECK:      tu.c
// CHECK-NEXT: h.h
// CHECK-NEXT: y.h
// CHECK-NOT:  x.h

//--- cdb.json.template
[{
  "directory": "DIR",
  "command": "clang -fsyntax-only -Xclang -remap-file -Xclang DIR/h.h;DIR/r.h DIR/tu.c",
  "file": "DIR/tu.c"
}]

//--- tu.c
#include "h.h"

//--- h.h
#include "x.h"

//--- r.h
// The replacement has different contents than the original.
#include "y.h"

//--- x.h
//--- y.h
